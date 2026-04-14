#ifndef HANDLER_H
#define HANDLER_H

void handle_request(int client_fd, const char *root_dir, const char *db_path);

#endif /* HANDLER_H */