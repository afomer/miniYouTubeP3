
/* General Headers */
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <ctype.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
//#include "logging.h"

#define CRLF "\r\n"
#define HT '\t'
#define SP ' '
#define GET "GET"
#define POST "POST"
#define HEAD "HEAD"
#define BUF_SIZE 9096
#define MAX_LINE 8192
#define BITRATES_NUM 50

/* indices of the supported headers */
#define ACCPETED_HEADERS_NUM 5
#define CONNECTION_INDEX 0
#define DATE_INDEX 1
#define CONTENT_LENGTH_INDEX 2
#define CONTENT_TYPE_INDEX 3
#define LAST_MODIFIED_INDEX 4

#define CLOSE_CONNECTION_CODE 0

/** Structs **/

/* struct to handle client's requests */
typedef struct {
        int clientfd;
        int sent_empty_line;
        char *bytes_to_be_read;
        char request_buffer[BUF_SIZE];
        char message_body_buffer[BUF_SIZE];
        char request_method[MAX_LINE]; // GET, POST, HEAD are the possibilities
        char *headers[ACCPETED_HEADERS_NUM];
        int bitrates[BITRATES_NUM];
        char uri[MAX_LINE];
        int outgoing_port;
        int is_web_server;
        int serving_clientfd;
        int timestamp;
        float t_curr;

} client;

typedef struct client* client_ptr;

/* struct for preparing a response for a client*/
typedef struct {
        int size;
        char *buffer;
} response;

/****/

/** Global Variables **/

char *LOG_FILE;
int SHOW_LOG = 1;
/****/

/** Function Headers **/

int handle_request_header(client *client, int readret, char *www_folder_path, char *log_file);
void app_serve(client *client, char *www_folder_path);
int handle_HEAD(client *client, char *www_folder_path);
char *get_content_type(char *uri, char *buff);
int handle_POST(client *client, char *www_folder_path);
FILE *get_path(client *client, char *www_folder_path);
char *get_last_modified(client *client, char *filepath);
void reset_request_info_buffer(client *client);
int get_file_size(FILE *fp);
int set_request_line(client *client, char *string, char *new_request_line);
int handle_message_header(client *client, char *string);
void send_response(client *client, int status_code, char *extra_message_info, int extra_message_info_size);
int send_response_helper(client *client, response *respond_buffer);
int get_main_headers(client *client, response *respond_buffer, char *status_line,
                     char *extra_message_info, int extra_message_info_size);
int is_request_line(char *string);
int supported_method(char *method_name);
void send_err(client *client, char *message);
int close_socket(int sock);
int is_empty(char *string);
char *to_lower_case(char *str);
char *get_server_date();
char* get_str_from_int(int number, char *buffer);
int get_int_from_str(char *string);
int is_string_equal(char *string_1, char *string_2);
void handle_path(client *client, char *new_request_line);

/****/

/*
 * is_string_equal:
 * binary checker for strings equality. Equal = 1, Inequal = 0
 */
int is_string_equal(char *string_1, char *string_2) {


        if ( (string_1 == NULL || string_2 == NULL) ) {
                return 0;
        }

        return strcmp(string_1, string_2) == 0;
}

/*
 * get_int_from_str:
 * Retrieving a int from a string
 */
int get_int_from_str(char *string) {

        if (string == NULL) {
                return -1;
        }

        int number;
        sscanf(string, "%d", &number);
        return number;
}

/*
 * get_str_from_int:
 * Retrieving a string from an int
 */
char* get_str_from_int(int number, char *buffer) {
        sprintf(buffer, "%d", number);
        return buffer;
}

/*
 * get_server_date:
 * return UTC time now using ANSI C's asctime() format
 * example: Sun Nov 6 08:49:37 1994
 */
char *get_server_date() {
        struct tm *newtime;
        time_t ltime;

        /* Get the time in seconds */
        time(&ltime);
        /* Convert it to the structure tm */
        newtime = gmtime(&ltime);

        /* Print the local time as a string */
        return asctime(newtime);
}

/*
 * to_lower_case:
 * change the letters in the string to lowercase letters
 */
char *to_lower_case(char *str) {
        int i = 0;
        while(str[i]) {
                str[i] = tolower(str[i]);
                i++;
        }

        return str;
}

/*
 * is_empty:
 * binary check for NULL or non-positive length strings
 */
int is_empty(char *string) {
        return string == NULL || strlen(string) == 0;
}

/*
 * close_socket:
 * close the socket if possible
 */
int close_socket(int sock) {

        if (close(sock))
        {
                fprintf(stderr, "Failed closing socket.\n");
                fprintf(stderr, "errno: %d\n", errno);
                return 1;
        }
        return 0;
}

/*
 * send_err:
 * send an error message at the clienfd or client_context incase of SSL
 */
void send_err(client *client, char *message) {

        send(client->clientfd, message, sizeof(message), 0);
}

/*
 * supported_request:
 * check if the method is GET, HEAD or POST (supported methods)
 */
int supported_method(char *method_name) {

        if ( is_string_equal(method_name, GET)  || is_string_equal(method_name, "ET") ||
             is_string_equal(method_name, HEAD) || is_string_equal(method_name, "EAD")||
             is_string_equal(method_name, POST) || is_string_equal(method_name, "OST") ) {
               // logger(LOG_FILE, "\nis_request_line method=[%s]\n", method_name, SHOW_LOG);
                return 1;
        }
        return 0;
}

/*
 * is_request_line:
 * check if a string represents HTTP/1.1 request_line
 * request-line: Method SP Request-URI SP HTTP-Version CRLF
 * format: string + " " + string + " " + string + CRLF
 */
int is_request_line(char *string) {
        char string_method_name[MAX_LINE];
        char uri[MAX_LINE];
        char n1[MAX_LINE];
        char n2[MAX_LINE];
        sscanf(string, "%s %s HTTP/%1s.%1s\r\n", string_method_name, uri, n1, n2  );
        // Check if it's a supported HTTP request
        return supported_method(string_method_name);
}

/*
 * get_main_headers:
 * Malloc space for respond_buffer, populate it with the headers:
 * Server, Connection, and Date
 * If extra_message_info is Non-NULL malloc space according to
 * metioned headers and "extra_message_info_size",
 * populate the response struct message pointer, and size accordingly,
 * then return the total number of bytes
 */
int get_main_headers(client *client, response *respond_buffer, char *status_line,
                     char *extra_message_info, int extra_message_info_size) {

        // General Header Fields: Connection and Date
        char *connection = client->headers[CONNECTION_INDEX];
        char *date = get_server_date();
        date[strlen(date)-1] = '\0';

        // response-header: Server: 'Liso/1.0'
        char *response_header = "Liso/1.0";
        char *end_of_headers  = CRLF;

        // if extra headers or/and message body is provided, then the CLRF is decided by it
        // else it's decided by this function
        if (extra_message_info == NULL) {
                extra_message_info = "";
        }
        else {
                end_of_headers = "";
        }

        respond_buffer->buffer = (char *)malloc(sizeof(char) * ( strlen(status_line) + extra_message_info_size + BUF_SIZE)); // for '\0' each part

        // seek tells memcpy where to start writing and serve as the
        // total size of the message
        int seek = 0;
        memcpy(respond_buffer->buffer + seek, status_line, strlen(status_line));
        seek += strlen(status_line);

        memcpy(respond_buffer->buffer + seek, "Connection: ", strlen("Connection: "));
        seek += strlen("Connection: ");

        if (strlen(connection) < 1) {
                memcpy(respond_buffer->buffer + seek, "Keep-Alive", strlen("Keep-Alive"));
                seek += strlen("Keep-Alive");
        }
        else {
                memcpy(respond_buffer->buffer + seek, connection, strlen(connection));
                seek += strlen(connection);

        }

        memcpy(respond_buffer->buffer + seek, CRLF, strlen(CRLF));
        seek += strlen(CRLF);

        memcpy(respond_buffer->buffer + seek, "Date: ", strlen("Date: "));
        seek += strlen("Date: ");

        memcpy(respond_buffer->buffer + seek, date, strlen(date));
        seek += strlen(date);

        memcpy(respond_buffer->buffer + seek, CRLF, strlen(CRLF));
        seek += strlen(CRLF);

        memcpy(respond_buffer->buffer + seek, "Server: ", strlen("Server: "));
        seek += strlen("Server: ");

        memcpy(respond_buffer->buffer + seek, response_header, strlen(response_header));
        seek += strlen(response_header);

        memcpy(respond_buffer->buffer + seek, CRLF, strlen(CRLF));
        seek += strlen(CRLF);

        if ( extra_message_info_size > 0 ) {
                memcpy(respond_buffer->buffer + seek, extra_message_info, extra_message_info_size);
                seek += extra_message_info_size;
        }
        else {
                memcpy(respond_buffer->buffer + seek, end_of_headers, strlen(status_line));
                seek += strlen(status_line);
        }
        respond_buffer->size = seek;
        return seek;
}

/*
 * send_response_helper:
 * helper function to send response to clients according to their
 * connection type
 */
int send_response_helper(client *client, response *respond_buffer) {
        return send(client->clientfd, respond_buffer->buffer, respond_buffer->size, 0);

}


/*
 * send_response: send responses according to the status_code provided
 *
 *   General Response Strucutre:*
 *     (Status-Line = HTTP-Version SP Status-Code SP Reason-Phrase CRLF)
 *
 *      Response  = Status-Line               ; Section 6.1
 *         (( general-header        ; Section 4.5
 *         | response-header        ; Section 6.2
 *         | entity-header ) CRLF)  ; Section 7.1
 *         CRLF
 *         [ message-body ]          ; Section 7.2
 */
void send_response(client *client, int status_code, char *extra_message_info, int extra_message_info_size) {
        char status_line[MAX_LINE];
        static response respond_buffer;
        int connfd = client->clientfd;

        if (connfd < 0) {
                return;
        }

        // Respond to the client according to the status code
        switch (status_code) {

        case 200: // OK
                strcpy(status_line, "HTTP/1.1 ");

                strcat(status_line, "200 OK\r\n");

                get_main_headers(client, &respond_buffer, status_line, extra_message_info, extra_message_info_size);
                //logger(LOG_FILE, "%s", "\n------- 200 RESPONSE-------\n", SHOW_LOG);
                //logger(LOG_FILE, "\nstatus_line: %s \n", status_line, SHOW_LOG);
                //logger(LOG_FILE, "%s", "\n---------\n", SHOW_LOG);
                send_response_helper(client, &respond_buffer);
                break;

        case 400: // Bad Request
                strcpy(status_line, "HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\n");
                get_main_headers(client, &respond_buffer, status_line, NULL, 0);
                //logger(LOG_FILE, "\n------- 400 RESPONSE-------\n%s---------\n", respond_buffer.buffer, SHOW_LOG);
                send_response_helper(client, &respond_buffer);
                break;

        case 404: // Not Found
                strcpy(status_line, "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n");
                get_main_headers(client, &respond_buffer, status_line, NULL, 0);
                //logger(LOG_FILE, "\n-------404 RESPONSE-------\n%s---------\n", respond_buffer.buffer, SHOW_LOG);
                send_response_helper(client, &respond_buffer);
                break;

        case 501: // Not Implemented
                strcpy(status_line, "HTTP/1.1 501 Not Implemented\r\nContent-Length: 0\r\n");
                get_main_headers(client, &respond_buffer, status_line, NULL, 0);
                //logger(LOG_FILE, "\n-------501 RESPONSE-------\n%s---------\n", respond_buffer.buffer, SHOW_LOG);
                send_response_helper(client, &respond_buffer);
                break;

        case 500: // Not Implemented
                strcpy(status_line, "HTTP/1.1 500 Internal Server Error\r\nContent-Length: 0\r\n");
                get_main_headers(client, &respond_buffer, status_line, NULL, 0);
                //logger(LOG_FILE, "\n-------500 RESPONSE-------\n%s---------\n", respond_buffer.buffer, SHOW_LOG);
                send_response_helper(client, &respond_buffer);
                break;

        case 413:
                strcpy(status_line, "HTTP/1.1 413 413 Payload Too Large\r\nContent-Length: 0\r\n");
                get_main_headers(client, &respond_buffer, status_line, NULL, 0);
                //logger(LOG_FILE, "\n-------413 RESPONSE-------\n%s---------\n", respond_buffer.buffer, SHOW_LOG);
                send_response_helper(client, &respond_buffer);
                break;

        default:
                break;
        }

        /* Non-null buffer implies used buffer, by implmentation, free the it */
        if (respond_buffer.buffer != NULL) {
                free(respond_buffer.buffer);
        }

}

/*
 * handle_message_header:
 * handle the header, and store it if it's one of the following headers:
 * responses according to the status_code provided:
 * connection, date, content-length, content-type, or last-modified
 *
 * HTTP header spec:
 * request-line: field-name: field_value
 *
 *    message-header = field-name ":" [ field-value ]
 *    field-name     = token
 *    field-value    = *( field-content | LWS )
 *    field-content  = <the OCTETs making up the field-value
 *                     and consisting of either *TEXT or combinations
 *                     of token, separators, and quoted-string>
 * formate: string + " " + string + " " + string + CRLF
 */
int handle_message_header(client *client, char *string) {

        char string_name_field[MAX_LINE];
        char *string_value_field = "";
        char *p = strchr(string, ':');

        if ( sscanf(string, "%s:", string_name_field) < 1 || p == NULL ) {
                // malformed header, notify caller
                return 1;
        }
        // put a null terminator at the end of the string_name_field
        string_name_field[MAX_LINE-1] = '\0';

        // find the first non-space to capture the value-field
        for (p = p + 1; p != NULL && ((char)*p == '\r' || (char)*p == ' ') && (char)*p != '\0'; p++) {
        }
        string_value_field = p;

        if ( is_string_equal(string_name_field, GET) || is_string_equal(string_name_field, "ET") ) {
                strcpy(client->request_method, GET);
        }
        else if ( is_string_equal(string_name_field, HEAD) || is_string_equal(string_name_field, "EAD") ) {
                strcpy(client->request_method, HEAD);
        }
        else if ( is_string_equal(string_name_field, POST) || is_string_equal(string_name_field, "OST") ) {
                strcpy(client->request_method, POST);
        }

        to_lower_case(string_name_field);

        // store the conncetion only if it's supported. else ignore it
        if ( is_string_equal(string_name_field, "connection") ) {
                strcpy(client->headers[CONNECTION_INDEX], string_value_field);
        }
        else if ( is_string_equal(string_name_field, "date") ) {
                strcpy(client->headers[DATE_INDEX], string_value_field);
        }
        else if ( is_string_equal(string_name_field, "content-length") ) {
                strcpy(client->headers[CONTENT_LENGTH_INDEX], string_value_field);
        }
        else if ( is_string_equal(string_name_field, "content-type") ) {
                strcpy(client->headers[CONTENT_TYPE_INDEX], string_value_field);
        }
        else if ( is_string_equal(string_name_field, "last-modified") ) {
                strcpy(client->headers[LAST_MODIFIED_INDEX], string_value_field);
        }

        return 0;

}

/*
 * DNS_resolve:
 * resolve the uri_buff aganist the DNS
 */
char *DNS_resolve(char *host) {
	return host;
}

/*
 * set_request_line:
 * set the request line to the appropriate host and copy it to string
 */
int set_request_line(client *client, char *string, char *new_request_line) {
        char string_method_name[50];
        char string_uri[MAX_LINE];
        char number1[2];
        char number2[2];
        char *begin;
        char *end;

        sscanf(string, "%s %s HTTP/%1s.%1s\r\n", string_method_name, string_uri, number1, number2);
        
        // TODO: for CP2, Perform DNS resolution on the host
        // for checkpoint hardcode known servers

        // normalize the uri be lowercasing it
        to_lower_case(string_uri);

        //format #1: http://host.com/...
        begin = strstr(string_uri, "//");

        if (begin == NULL) {
        	return -1;
        }
        begin = begin + 2; // start the host

        end   = strrchr(begin, '/');

        if (end == NULL) {
        	return -1;
        }

        /* get the main host name based on the format of the URI */
        char uri_buff[MAX_LINE];
		char *port_ptr = strchr(begin, ':');
        int str_len;
        
        // format #1: http://....:port/...
        if ( port_ptr != NULL && port_ptr < end ) {
        	str_len = port_ptr - begin;
        }
        else {
        // format #2: http://..../...
        	str_len = end - begin;
        }
		
		int i;
		
		for (i = 0; i < str_len; i++) {
			uri_buff[i] = begin[i];
		}
		uri_buff[str_len] = '\0';

		// resolve the uri_buff aganist the DNS
		// keep the rest the same
		char *new_host = DNS_resolve(uri_buff);
		char *path = end;
		sprintf(client->uri, "http://%s%s", new_host, path);
        sprintf(new_request_line, "%s %s HTTP/%1s.%1s\r\n", string_method_name, client->uri, number1, number2);
        handle_path(client, new_request_line);

        // set the new uri
        sprintf(new_request_line, "%s %s HTTP/%1s.%1s\r\n", string_method_name, client->uri, number1, number2);
        return 1;
}

/*
 * get_file_size:
 * get the file size by seeking to the end of the file.
 * We then return to beginning of the file
 */
int get_file_size(FILE *fp) {
        fseek(fp, 0, SEEK_END);
        int endpoint = (int)ftell(fp);
        fseek(fp, 0, SEEK_SET);

        return endpoint;
}

/*
 * reset_request_info_buffer:
 * reseting the headers and message body args without closing the socket
 */
void reset_request_info_buffer(client *client) {
        client->request_buffer[0] = 0;
        client->message_body_buffer[0] = 0;
        client->uri[0] = 0;
        strcpy(client->request_method, "");
        strcpy(client->headers[0], "");
        strcpy(client->headers[1], "");
        strcpy(client->headers[2], "");
        strcpy(client->headers[3], "");
        strcpy(client->headers[4], "");

        int temp_string_size = 64;
        char string_buffer[temp_string_size];
        get_str_from_int(-1, string_buffer);
        strcpy(client->bytes_to_be_read, string_buffer);
}

/*
 * get_last_modified:
 * return UTC time of the file's last modified date using ctime(),
 * and ANSI C's asctime() format
 * example: Sun Nov 6 08:49:37 1994
 */
char *get_last_modified(client *client, char *filepath) {
        // Sun Nov  6 08:49:37 1994 ; ANSI C's asctime() format
        struct stat fst;
        bzero(&fst, sizeof(fst));
        stat(filepath,&fst);
        /* Print the local time as a string */
        strcpy(client->headers[LAST_MODIFIED_INDEX], ctime(&fst.st_atime));
        return 0;
}

/*
 * get_path:
 * return FILE pointer to the URI requested by the client,
 * relative to the www folder
 */
FILE *get_path(client *client, char *www_folder_path) {
        // try to open the file provided by the URI
        FILE *fp;
        char *uri = client->uri;
        char uri_buffer[MAX_LINE];
        strcpy(uri_buffer, www_folder_path);

        if ( is_string_equal(&uri[strlen(uri)-1], "/") ) {
                strcat(uri_buffer, "/index.html");
        }
        else {
                strcat(uri_buffer, uri);
        }
        strcpy(client->uri, uri_buffer);
        fp = fopen(client->uri, "r");

        return fp;
}

/*
 * get_IP_from_hostname:
 * resolve the hostname against a DNS to get the IP
 */
char *get_IP_from_hostname(client *client, char *www_folder_path){
        // try to open the file provided by the URI
        char *uri = client->uri;
       // char uri_buffer[MAX_LINE];
        
        /* currently I will serve from one harcoded server */
        // TODO: get the ip of the "best" video web server

        return uri;
}

/*
 * handle_POST: handle requests by checking the URI's availability
 * and responding accordingly
 */
int handle_POST(client *client, char *www_folder_path) {

        FILE *fp;
        fp = get_path(client, www_folder_path);
        // if the file can't be found send 404
        if (fp == NULL) {
                send_response(client, 404, NULL, 0);
                return 1;
        }
        send_response(client, 200, NULL, 0);
        return 0;
}

/*
   get_content_type: get the content based on the file extension:
   currently supported types:
     text/css
     image/jpeg
     image/png
     audio/mpeg
     audio/ogg
     video/mp4

 */
char *get_content_type(char *uri, char *buff) {

        char *ext = strrchr(uri, '.');

        if ( is_string_equal(ext, ".css") ) {
                strcpy(buff, "text/css");
        }
        else if ( is_string_equal(ext, ".jpeg") ) {
                strcpy(buff, "image/jpeg");
        }
        else if ( is_string_equal(ext, ".jpg") ) {
                strcpy(buff, "image/jpg");
        }
        else if ( is_string_equal(ext, ".png") ) {
                strcpy(buff, "image/png");
        }
        else if ( is_string_equal(ext, ".mpeg") ) {
                strcpy(buff, "audio/mpeg");
        }
        else if ( is_string_equal(ext, ".ogg") ) {
                strcpy(buff, "audio/ogg");
        }
        else if ( is_string_equal(ext, ".mp4") ) {
                strcpy(buff, "video/mp4");
        }
        else {
                strcpy(buff, "text/html");
        }

        return buff;

}

/*
 * handle_HEAD: handle HEAD requests by checking the URI's availability
 * and responding accordingly with the content_length and without the body
 */
int handle_HEAD(client *client, char *www_folder_path) {

        int file_len;
        char *uri;
        FILE *fp;
        fp = get_path(client, www_folder_path);
        uri = client->uri;

        // if the file can't be found send 404
        if (fp == NULL) {
                send_response(client, 404, NULL, 0);
                return 1;
        }

        // Make a buffer of the size of the
        // file to contain the message body
        file_len = get_file_size(fp);

        char num_buff[20];
        get_str_from_int(file_len, num_buff);
        char *content_length = num_buff;

        /* server-side decided headers */
        char tmp_buff[20];
        char *content_type   = get_content_type(uri, tmp_buff);
        get_last_modified(client, uri);
        char *last_modified = client->headers[LAST_MODIFIED_INDEX];

        /*
           Carry a seek to know where to start writing
           while appending to "extra_message_info"
         */
        char extra_message_info[4 * BUF_SIZE]; // for '\0' each part
        int seek = 0;
        memcpy(extra_message_info + seek, "Content-Length: ", strlen("Content-Length: "));
        seek += strlen("Content-Length: ");

        memcpy(extra_message_info + seek, content_length, strlen(content_length));
        seek += strlen(content_length);


        memcpy(extra_message_info + seek, CRLF, strlen(CRLF));
        seek += strlen(CRLF);

        memcpy(extra_message_info + seek, "Content-Type: ", strlen("Content-Type: "));
        seek += strlen("Content-Type: ");

        memcpy(extra_message_info + seek, content_type, strlen(content_type));
        seek += strlen(content_type);

        memcpy(extra_message_info + seek, CRLF, strlen(CRLF));
        seek += strlen(CRLF);

        memcpy(extra_message_info + seek, "Last-Modified: ", strlen("Last-Modified: "));
        seek += strlen("Last-Modified: ");

        memcpy(extra_message_info + seek, last_modified, strlen(last_modified));
        seek += strlen(last_modified);

        memcpy(extra_message_info + seek, CRLF, strlen(CRLF));
        seek += strlen(CRLF);

        memcpy(extra_message_info + seek, CRLF, strlen(CRLF));
        seek += strlen(CRLF);

        send_response(client, 200, extra_message_info, seek);

        fclose(fp);

        return 0;
}

/*
 * app_serve: check the request type and,
 * respond to it according to the request method.
 * The request format:
 *
 *  Response  = Status-Line     ; Section 6.1
 *    *(( general-header       ; Section 4.5
 *    | response-header        ; Section 6.2
 *    | entity-header ) CRLF)  ; Section 7.1
 *     CRLF
 *     [ message-body ]        ; Section 7.2
 *
 */
void app_serve(client *client, char *www_folder_path) {

        // Status-Line = HTTP-Version SP Status-Code SP Reason-Phrase CRLF
        if ( is_string_equal(client->request_method, GET) || is_string_equal(client->request_method, "ET") ) {
            //    handle_GET(client, www_folder_path);
        }
        else if ( is_string_equal(client->request_method, HEAD) || is_string_equal(client->request_method, "EAD") ) {
                handle_HEAD(client, www_folder_path);
        }
        else {
                handle_POST(client, www_folder_path);
        }

}

void set_bit_rate(client *client, int bitrate_value) {
	int i;
	int found_bitrate_field = 0;
	for (i = 0; i < BITRATES_NUM && !found_bitrate_field; i++) {
		if (client->bitrates[i] == -1) {
			client->bitrates[i] = bitrate_value;
			found_bitrate_field = 1;
		}
	}
}

void get_bit_rates(client *client, char *buf) {
	// look for bitrates and add them to
	// the bitrate array
	char *bitrate_begin = strstr(buf, "bitrate");
	char *bitrate_end 	= NULL;
	while( bitrate_begin != NULL ) {
			bitrate_begin = bitrate_begin + strlen("bitrate") + strlen("=\"");
			bitrate_end   = strchr(bitrate_begin, '"');
			int bitrate_value = atoi(bitrate_begin);
			set_bit_rate(client, bitrate_value);
			bitrate_begin = strstr(bitrate_end, "bitrate");
	}
}
/*
* get_best_bitrate: 
* pick best bitrate according to t_curr 1.5x crit.
*/
int get_best_bitrate(client *client) {
	int supported_throughput = client->t_curr;
	int best_bitrate = client->bitrates[0];
	int i;
	for (i = 0; i < BITRATES_NUM; i++){
		// 2.5 * bitrate = bitrate 1.5x
		if (client->bitrates[i] < 0 || supported_throughput < (int)(client->bitrates[i] * 2.5)) {
			break;
		}
		best_bitrate = client->bitrates[i];
	}
	return best_bitrate;
}


/*
 * handle_path: if the path is request .f4m get the bitrates from the webserver
 * then request _nolist.f4m version instead and direct it to ther user
 */
void handle_path(client *client, char *new_request_line) {

	// store timestamp for bitrate adaptation
	client->timestamp = (int)time(NULL);

	char *extn = strrchr(client->uri, '.');
	if (extn != NULL && strcmp(extn, ".f4m") == 0) {
		// Perform the current request and store the bitrates
		// then modify the uri path so it requests "_nolist.f4m"
		static response respond_buffer;
		respond_buffer.size   = strlen(new_request_line);
		respond_buffer.buffer = new_request_line;
		send_response_helper(client, &respond_buffer);
		
		char buf[BUF_SIZE];

		// put all responses in the buffer readret = 0
		int seek = 0;
		int bytes_to_be_read = BUF_SIZE;
		int readret = 1;
		while(readret > 0) {
			readret = recv(client->clientfd, buf + seek, bytes_to_be_read, 0);
			seek += readret;
		}
		
		get_bit_rates(client, buf);
		int nolist_str_len = extn - (client->uri);
		char nolist_str[MAX_LINE];
		memcpy(nolist_str, client->uri, nolist_str_len);
		nolist_str[nolist_str_len] = '\0';
		sprintf(client->uri, "%s_nolist.f4m", nolist_str);
	}

	// for the case that you're modifying segment request
	// get the best bitrate and modify the uri
	char *path = strrchr(client->uri, '/');
	char *sub_path = strstr(client->uri, "seg"); //TODO: make sure it's lower case
	if (path != NULL && sub_path != NULL) {
		// Perform the current request and store the bitrates
		// then modify the uri path so it requests "_nolist.f4m"
		int path1_len = path - (client->uri);
		int best_supported_bitrate = get_best_bitrate(client);

		char new_uri[MAX_LINE];
		char new_sub_path[MAX_LINE];
		memcpy(new_sub_path, sub_path, strlen(sub_path)); // cpy in a buf to prevent overwrites from sprintf
		memcpy(new_uri, client->uri, path1_len);
		client->uri[0] = 0;
		sprintf(client->uri, "%s/%d%s", new_uri, best_supported_bitrate, new_sub_path);
	}

	return;
}

/*
 * handle_request_header: check client's headers'
 * line by line according to CRLF, and respond according
 * the format.
 */
int handle_request_header(client *client, int readret, char *www_folder_path, char *log_file) {

        // Setting the global variable for logging
        LOG_FILE = log_file;

        /* sepearate the bytes by CLRF & iterate through the lines
           are either request-line, headers, or empty-line */

        // if no message is recieved from the client return immedietly
        if ( is_string_equal(client->request_buffer, "") || strlen(client->request_buffer) < 1 ) {
                return 0;
        }

        char tmp_requst_buffer[BUF_SIZE];
        char line[BUF_SIZE];
        strcpy(tmp_requst_buffer, client->request_buffer);

      //  int close_socket_after_response = 0;

        //logger(LOG_FILE, "Handling Request:\n%s\n", tmp_requst_buffer, SHOW_LOG);

        // Check if the user sent empty-line or not (line with only CRLF)
        // and update the client's entry
        int has_empty_line = readret >= 4 && tmp_requst_buffer[readret - 4] == '\r' &&
                             tmp_requst_buffer[readret - 3] == '\n' &&
                             tmp_requst_buffer[readret - 2] == '\r' &&
                             tmp_requst_buffer[readret - 1] == '\n';

        static response response;
        client->sent_empty_line = has_empty_line;
        //char *request_end = &(tmp_requst_buffer[readret - 1]);
        char *begin = tmp_requst_buffer;
        char *end = strstr(tmp_requst_buffer, CRLF);
		char new_request_line[MAX_LINE];

        // point to lines by beginning and ending pointers
        while (end != NULL) {
 		    // if a request_line is encountered
    		// perform resolution on the uri
    		// then forward the request to the webserver
    		// for other requests foward without modification
    		memcpy(line, begin, (end + 2) - begin);
    		line[(end + 2) - begin] = '\0';

            if ( is_request_line(line) ) {
                    set_request_line(client, line, new_request_line);
                    // send the new request line
                    response.size = strlen(new_request_line);
                    response.buffer = new_request_line;
                    send_response_helper(client, &response);
            }
            else {
	            // send to ther user
                response.size = strlen(line);
                response.buffer = line;
                send_response_helper(client, &response);    	
            }

        	begin = end + 2;
        	end = strstr(begin, CRLF);
        }


        // Read headers line by line
        /*
        while (line != NULL && line < request_end) {
                
                // if a request_line is encountered
        		// perform resolution on the uri
        		// then forward the request to the webserver
        		// for other requests foward without modification
                if ( is_request_line(line) ) {
                        // if it's not a [GET, HEAD or POST] return unsupported request
                        // done: set_request_line
                        set_request_line(client, line);
                        handle_request_line()
                }
                else {
                    // send to the user directly without modification
                }

                // Check the next header
                line = strtok(NULL, CRLF);
        } // end of while loop
        */

        // TODO: not always close_connection
        return CLOSE_CONNECTION_CODE;
}
