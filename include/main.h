#ifndef MAIN_H
#define MAIN_H

#include <stdint.h>
#include <sys/socket.h>
#include <sys/types.h>

#ifndef NI_MAXHOST
    #define NI_MAXHOST 1025
#endif
#ifndef NI_MAXSERV
    #define NI_MAXSERV 32
#endif

#ifndef HTTP_STATUS_OK
    #define HTTP_STATUS_OK                  200
    #define HTTP_STATUS_BAD_REQUEST         400
    #define HTTP_STATUS_FORBIDDEN           403
    #define HTTP_STATUS_NOT_FOUND           404
    #define HTTP_STATUS_INTERNAL_SERVER_ERROR 500
    #define HTTP_STATUS_NOT_IMPLEMENTED     501
    #define HTTP_STATUS_VERSION_NOT_SUPPORTED 505
#endif

enum
{
    PORT_INPUT_BASE  = 10,
    DEFAULT_WORKERS  = 5,
    FILE_PERMISSIONS = 0644
};

enum
{
    HTTP_RESPONSE_HEADER_BUFFER = 256,
    HTTP_RESPONSE_BODY_BUFFER   = 4096,
    HTTP_ERROR_RESPONSE_BUFFER  = 512,
    BASE_TEN                    = 10
};

enum
{
    RW_SUCCESS = 0,
    RW_ERROR   = -1,
    RW_EOF     = -2
};

enum
{
    BASE_REQUEST_BUFFER_CAPACITY      = 4096,
    REQUEST_BUFFER_INCREASE_THRESHOLD = 1024
};

enum
{
    MAX_PORT_NUMBER = 65535
};

struct http_request
{
    char *method;
    char *path;
    char *protocol_version;
};

typedef struct http_request http_request;

struct server_context
{
    int    argc;
    char **argv;

    int         exit_code;
    char       *exit_message;
    const char *ip_address;

    struct sockaddr_storage addr;
    int                     listen_fd;

    const char *user_entered_port;
    uint16_t    port_number;

    const char *root_directory;
    const char *db_path;
    const char *library_path;

    int    num_workers;
    pid_t *worker_pids;
};

typedef struct server_context server_context;

__attribute__((noreturn)) void print_usage(const server_context *ctx);
__attribute__((noreturn)) void quit(const server_context *ctx);

#endif /* MAIN_H */