
#include <errno.h>
#include <fcntl.h>
#include <ndbm.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef NI_MAXHOST
    #define NI_MAXHOST 1025
#endif

enum
{
    READ_BUF_SIZE    = 4096,
    HEADER_BUF_SIZE  = 512,
    PATH_BUF_SIZE    = 4096,
    REQUEST_BUF_SIZE = 8192,
    FILE_PERMISSIONS = 0644,
    METHOD_BUF_SIZE  = 16,
    VERSION_BUF_SIZE = 16,
    LINE_BUF_SIZE    = 512,
    BASE_TEN         = 10
};

enum
{
    HTTP_200 = 200,
    HTTP_400 = 400,
    HTTP_403 = 403,
    HTTP_404 = 404,
    HTTP_500 = 500,
    HTTP_501 = 501,
    HTTP_505 = 505
};

struct request
{
    char method[METHOD_BUF_SIZE];
    char path[PATH_BUF_SIZE];
    char version[VERSION_BUF_SIZE];
    long content_length;
};

static int         read_headers(int fd, char *buf, size_t cap);
static int         parse_request(const char *buf, struct request *req);
static void        send_error(int fd, int status);
static void        handle_get(int fd, const struct request *req, const char *root);
static void        handle_head(int fd, const struct request *req, const char *root);
static void        handle_post(int fd, const struct request *req, const char *root, const char *db_path);
static void        map_path(const char *root, const char *url_path, char *out, size_t out_size);
static const char *mime_type(const char *path);
static ssize_t     write_full(int fd, const void *buf, size_t n);
static ssize_t     read_full(int fd, void *buf, size_t n);
static long        parse_content_length(const char *buf);

void handle_request(int client_fd, const char *root_dir, const char *db_path)
{
    char buf[REQUEST_BUF_SIZE + 1];

    if(read_headers(client_fd, buf, REQUEST_BUF_SIZE) < 0)
    {
        send_error(client_fd, HTTP_400);
        return;
    }

    struct request req;
    memset(&req, 0, sizeof(req));

    if(parse_request(buf, &req) < 0)
    {
        send_error(client_fd, HTTP_400);
        return;
    }

    /* Store raw buffer in req for Content-Length parsing in POST */
    req.content_length = parse_content_length(buf);

    if(strcmp(req.version, "HTTP/1.0") != 0 && strcmp(req.version, "HTTP/1.1") != 0)
    {
        send_error(client_fd, HTTP_505);
        return;
    }

    if(strcmp(req.method, "GET") == 0)
    {
        handle_get(client_fd, &req, root_dir);
    }
    else if(strcmp(req.method, "HEAD") == 0)
    {
        handle_head(client_fd, &req, root_dir);
    }
    else if(strcmp(req.method, "POST") == 0)
    {
        handle_post(client_fd, &req, root_dir, db_path);
    }
    else
    {
        send_error(client_fd, HTTP_501);
    }
}

/* Request reading */
static int read_headers(int fd, char *buf, size_t cap)
{
    size_t      filled   = 0;
    const char *sentinel = "\r\n\r\n";
    size_t      slen     = strlen(sentinel);

    while(filled < cap)
    {
        ssize_t r = read(fd, buf + filled, 1);
        if(r == 0)
        {
            return -1;
        }
        if(r == -1)
        {
            if(errno == EINTR)
            {
                continue;
            }
            return -1;
        }
        filled++;
        if(filled >= slen && memcmp(buf + filled - slen, sentinel, slen) == 0)
        {
            buf[filled] = '\0';
            return (int)filled;
        }
    }
    return -1;
}

/* Request parsing */
static int parse_request(const char *buf, struct request *req)
{
    /* Parse only the first line: METHOD SP PATH SP VERSION CRLF */
    const char *end = strstr(buf, "\r\n");
    if(end == NULL)
    {
        return -1;
    }

    size_t line_len = (size_t)(end - buf);
    char   line[LINE_BUF_SIZE];
    if(line_len >= sizeof(line))
    {
        return -1;
    }
    memcpy(line, buf, line_len);
    line[line_len] = '\0';

    if(sscanf(line, "%15s %4095s %15s", req->method, req->path, req->version) != 3)
    {
        return -1;
    }
    return 0;
}

static long parse_content_length(const char *buf)
{
    const char *key = "Content-Length:";
    const char *p   = buf;

    while((p = strstr(p, key)) != NULL)
    {
        p += strlen(key);
        while(*p == ' ' || *p == '\t')
        {
            p++;
        }
        char *end;
        errno    = 0;
        long val = strtol(p, &end, BASE_TEN);
        if(errno == 0 && end != p && val >= 0)
        {
            return val;
        }
    }
    return -1;
}

/*  Path mapping  */
static void map_path(const char *root, const char *url_path, char *out, size_t out_size)
{
    out[0] = '\0';

    const char *rel = url_path;
    if(rel[0] == '/')
    {
        rel++;
    }
    if(rel[0] == '\0')
    {
        return;
    }

    /* Build candidate path */
    char candidate[PATH_BUF_SIZE];
    snprintf(candidate, sizeof(candidate), "%s/%s", root, rel);

    /* Resolve the FULL path with realpath — this catches all traversal attempts
     * including ../, URL encoding, and symlink escapes.
     * Note: file must exist for realpath to work, so this only protects GET/HEAD.
     * POST is handled separately below. */
    char *resolved = realpath(candidate, NULL);
    if(resolved == NULL)
    {
        return;
    }

    /* Check resolved path is still inside root */
    size_t root_len = strlen(root);
    if(strncmp(resolved, root, root_len) != 0 || (resolved[root_len] != '\0' && resolved[root_len] != '/'))
    {
        free(resolved);
        return;
    }

    snprintf(out, out_size, "%s", resolved);
    free(resolved);
}

/* MIME types */
static const char *mime_type(const char *path)
{
    const char *dot = strrchr(path, '.');
    if(dot == NULL)
    {
        return "application/octet-stream";
    }
    if(strcmp(dot, ".html") == 0 || strcmp(dot, ".htm") == 0)
    {
        return "text/html";
    }
    if(strcmp(dot, ".txt") == 0)
    {
        return "text/plain";
    }
    if(strcmp(dot, ".jpg") == 0 || strcmp(dot, ".jpeg") == 0)
    {
        return "image/jpeg";
    }
    if(strcmp(dot, ".png") == 0)
    {
        return "image/png";
    }
    if(strcmp(dot, ".gif") == 0)
    {
        return "image/gif";
    }
    if(strcmp(dot, ".css") == 0)
    {
        return "text/css";
    }
    if(strcmp(dot, ".js") == 0)
    {
        return "application/javascript";
    }
    return "application/octet-stream";
}

/* GET handler */
static void handle_get(int fd, const struct request *req, const char *root)
{
    char fspath[PATH_BUF_SIZE];
    map_path(root, req->path, fspath, sizeof(fspath));

    if(fspath[0] == '\0')
    {
        send_error(fd, HTTP_404);
        return;
    }

    struct stat st;
    if(stat(fspath, &st) != 0 || !S_ISREG(st.st_mode))
    {
        send_error(fd, HTTP_404);
        return;
    }

    if(access(fspath, R_OK) != 0)
    {
        send_error(fd, HTTP_403);
        return;
    }

    char header[HEADER_BUF_SIZE];
    int  hlen = snprintf(header,
                        sizeof(header),
                        "HTTP/1.0 200 OK\r\n"
                         "Server: prefork-httpd\r\n"
                         "Content-Type: %s\r\n"
                         "Content-Length: %lld\r\n"
                         "Connection: close\r\n"
                         "\r\n",
                        mime_type(fspath),
                        (long long)st.st_size);
    write_full(fd, header, (size_t)hlen);

    int file_fd = open(fspath, O_RDONLY | O_CLOEXEC);
    if(file_fd == -1)
    {
        return;
    }

    char    rbuf[READ_BUF_SIZE];
    ssize_t n;
    while((n = read(file_fd, rbuf, sizeof(rbuf))) > 0)
    {
        if(write_full(fd, rbuf, (size_t)n) != 0)
        {
            break;
        }
    }
    close(file_fd);
}

/* HEAD handler */
static void handle_head(int fd, const struct request *req, const char *root)
{
    char fspath[PATH_BUF_SIZE];
    map_path(root, req->path, fspath, sizeof(fspath));

    if(fspath[0] == '\0')
    {
        send_error(fd, HTTP_404);
        return;
    }

    struct stat st;
    if(stat(fspath, &st) != 0 || !S_ISREG(st.st_mode))
    {
        send_error(fd, HTTP_404);
        return;
    }

    if(access(fspath, R_OK) != 0)
    {
        send_error(fd, HTTP_403);
        return;
    }

    char header[HEADER_BUF_SIZE];
    int  hlen = snprintf(header,
                        sizeof(header),
                        "HTTP/1.0 200 OK\r\n"
                         "Server: prefork-httpd\r\n"
                         "Content-Type: %s\r\n"
                         "Content-Length: %lld\r\n"
                         "Connection: close\r\n"
                         "\r\n",
                        mime_type(fspath),
                        (long long)st.st_size);
    write_full(fd, header, (size_t)hlen);
}

/* POST handler */
static void handle_post(int fd, const struct request *req, const char *root, const char *db_path)
{
    (void)root;

    if(req->content_length < 0)
    {
        send_error(fd, HTTP_400);
        return;
    }

    /* Reject any path containing .. */
    if(strstr(req->path, "..") != NULL)
    {
        send_error(fd, HTTP_403);
        return;
    }

    /* Reject URL-encoded traversal */
    if(strstr(req->path, "%2e") != NULL || strstr(req->path, "%2E") != NULL)
    {
        send_error(fd, HTTP_403);
        return;
    }

    /* Read body */
    size_t body_size = (size_t)req->content_length;
    char  *body      = malloc(body_size + 1);
    if(body == NULL)
    {
        send_error(fd, HTTP_500);
        return;
    }

    if(read_full(fd, body, body_size) != 0)
    {
        free(body);
        send_error(fd, HTTP_500);
        return;
    }
    body[body_size] = '\0';

    /* Store in ndbm: key = URL path, value = body */
    DBM *db = dbm_open((char *)db_path, O_RDWR | O_CREAT, FILE_PERMISSIONS);
    if(db == NULL)
    {
        fprintf(stderr, "dbm_open failed for '%s': %s\n", db_path, strerror(errno));
        free(body);
        send_error(fd, HTTP_500);
        return;
    }

    datum key;
    datum val;
    key.dptr  = (char *)req->path;
    key.dsize = (int)strlen(req->path) + 1;
    val.dptr  = body;
    val.dsize = (int)body_size + 1;

    int store_result = dbm_store(db, key, val, DBM_REPLACE);
    dbm_close(db);
    free(body);

    if(store_result != 0)
    {
        send_error(fd, HTTP_500);
        return;
    }

    const char *msg  = "DATA stored successfully (version 2).\r\n";
    size_t      mlen = strlen(msg);
    char        header[HEADER_BUF_SIZE];
    int         hlen = snprintf(header,
                        sizeof(header),
                        "HTTP/1.0 200 OK\r\n"
                                "Server: prefork-httpd\r\n"
                                "Content-Type: text/plain\r\n"
                                "Content-Length: %zu\r\n"
                                "Connection: close\r\n"
                                "\r\n",
                        mlen);
    write_full(fd, header, (size_t)hlen);
    write_full(fd, msg, mlen);
}

/* Error responses */
static void send_error(int fd, int status)
{
    const char *status_line;
    const char *body;

    switch(status)
    {
        case HTTP_400:
            status_line = "HTTP/1.0 400 Bad Request";
            body        = "400 Bad Request\r\n";
            break;
        case HTTP_403:
            status_line = "HTTP/1.0 403 Forbidden";
            body        = "403 Forbidden\r\n";
            break;
        case HTTP_404:
            status_line = "HTTP/1.0 404 Not Found";
            body        = "404 Not Found\r\n";
            break;
        case HTTP_500:
            status_line = "HTTP/1.0 500 Internal Server Error";
            body        = "500 Internal Server Error\r\n";
            break;
        case HTTP_501:
            status_line = "HTTP/1.0 501 Not Implemented";
            body        = "501 Not Implemented\r\n";
            break;
        case HTTP_505:
            status_line = "HTTP/1.0 505 HTTP Version Not Supported";
            body        = "505 HTTP Version Not Supported\r\n";
            break;
        default:
            status_line = "HTTP/1.0 500 Internal Server Error";
            body        = "500 Internal Server Error\r\n";
            break;
    }

    char buf[HEADER_BUF_SIZE];
    int  len = snprintf(buf,
                       sizeof(buf),
                       "%s\r\n"
                        "Content-Type: text/plain\r\n"
                        "Content-Length: %zu\r\n"
                        "Connection: close\r\n"
                        "\r\n"
                        "%s",
                       status_line,
                       strlen(body),
                       body);
    write_full(fd, buf, (size_t)len);
}

/* I/O helpers */
static ssize_t write_full(int fd, const void *buf, size_t n)
{
    const unsigned char *p    = (const unsigned char *)buf;
    size_t               left = n;
    while(left > 0)
    {
        ssize_t w = write(fd, p, left);
        if(w == -1)
        {
            if(errno == EINTR)
            {
                continue;
            }
            return -1;
        }
        p += w;
        left -= (size_t)w;
    }
    return 0;
}

static ssize_t read_full(int fd, void *buf, size_t n)
{
    unsigned char *p    = (unsigned char *)buf;
    size_t         left = n;
    while(left > 0)
    {
        ssize_t r = read(fd, p, left);
        if(r == 0)
        {
            return -1;
        }
        if(r == -1)
        {
            if(errno == EINTR)
            {
                continue;
            }
            return -1;
        }
        p += r;
        left -= (size_t)r;
    }
    return 0;
}