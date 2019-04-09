#include "parser.c"

/* Contains functions needed for parsing HTTP/1.1 Requests */

int is_string_equal(char *string_1, char *string_2);
char *get_server_date();
char* get_str_from_int(int number, char *buffer);
int get_int_from_str(char *string);
int is_empty(char *string);
char *get_server_date();
int close_socket(int sock);
void send_err(client *client, char *messsage);
int get_main_headers(client *client, response *respond_buffer,
	char *status_line, char *extra_message_info, int extra_message_info_size);
void send_response(client *client, int status_code,char *extra_message_info, int headers);
int handle_message_header(client *client, char *string);
int set_request_line(client *client, char *string, char *new_request_line);
int get_file_size(FILE *fp);
char *get_last_modified(client *client, char *filepath);
int handle_POST(client *client, char *www_folder_path);
int handle_HEAD(client *client, char *www_folder_path);
void app_serve(client *client, char *www_folder_path);
int handle_request_header(client *client, int readret,
	char *www_folder_path, char *log_file);
void reset_request_info_buffer(client *client);
