#include "clients.c"

/* handle sockets & select() */

int create_listening_socket(struct sockaddr_in addr);
int run_server(int argc, char* argv[]);
void add_client(int connfd, pool* p, char *log_file);
void check_clients(pool *p, char *www_folder_path, char *log_file);
void init_pool(int listenfd, pool *p);