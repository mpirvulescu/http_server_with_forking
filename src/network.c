#include "../include/network.h"
#include "../include/main.h"
#include "../include/utils.h"
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

void init_server_socket(server_context *ctx)
{
    int sockfd = socket(ctx->addr.ss_family, SOCK_STREAM, 0);
    if(sockfd == -1)
    {
        perror("socket");
        ctx->exit_code = EXIT_FAILURE;
        quit(ctx);
    }

    if(fcntl(sockfd, F_SETFD, FD_CLOEXEC) == -1)
    {
        perror("fcntl FD_CLOEXEC");
        close(sockfd);
        ctx->exit_code = EXIT_FAILURE;
        quit(ctx);
    }

    int enable = 1;
    if(setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable)) == -1)
    {
        perror("setsockopt SO_REUSEADDR");
        close(sockfd);
        ctx->exit_code = EXIT_FAILURE;
        quit(ctx);
    }

    socklen_t addr_len;
    in_port_t net_port = htons(ctx->port_number);

    if(ctx->addr.ss_family == AF_INET)
    {
        struct sockaddr_in *a = (struct sockaddr_in *)&ctx->addr;
        a->sin_port           = net_port;
        addr_len              = sizeof(*a);
    }
    else
    {
        struct sockaddr_in6 *a = (struct sockaddr_in6 *)&ctx->addr;
        a->sin6_port           = net_port;
        addr_len               = sizeof(*a);
    }

    if(bind(sockfd, (struct sockaddr *)&ctx->addr, addr_len) == -1)
    {
        perror("bind");
        close(sockfd);
        ctx->exit_code = EXIT_FAILURE;
        quit(ctx);
    }

    if(listen(sockfd, SOMAXCONN) == -1)
    {
        perror("listen");
        close(sockfd);
        ctx->exit_code = EXIT_FAILURE;
        quit(ctx);
    }

    printf("[main] listening on port %u\n", ctx->port_number);
    ctx->listen_fd = sockfd;
}

int accept_client(server_context *ctx, char *host_out, char *serv_out)
{
    struct sockaddr_storage client_addr;
    socklen_t               addr_len = sizeof(client_addr);

    errno  = 0;
    int fd = accept(ctx->listen_fd, (struct sockaddr *)&client_addr, &addr_len);

    if(fd == -1)
    {
        if(errno != EAGAIN && errno != EWOULDBLOCK)
        {
            perror("accept");
        }
        return -1;
    }

    if(getnameinfo((struct sockaddr *)&client_addr, addr_len, host_out, NI_MAXHOST, serv_out, NI_MAXSERV, NI_NUMERICHOST | NI_NUMERICSERV) != 0)
    {
        strncpy(host_out, "unknown", NI_MAXHOST);
        strncpy(serv_out, "unknown", NI_MAXSERV);
    }

    return fd;
}

void close_client(int fd)
{
    if(fd != -1)
    {
        close(fd);
    }
}

void cleanup_server(server_context *ctx)
{
    if(ctx->listen_fd != -1)
    {
        close(ctx->listen_fd);
        ctx->listen_fd = -1;
    }
    if(ctx->worker_pids != NULL)
    {
        free(ctx->worker_pids);
        ctx->worker_pids = NULL;
    }
}