#ifndef NETWORK_H
#define NETWORK_H

#include "main.h"

void init_server_socket(server_context *ctx);
int  accept_client(server_context *ctx, char *host_out, char *serv_out);
void close_client(int fd);
void cleanup_server(server_context *ctx);

#endif /* NETWORK_H */