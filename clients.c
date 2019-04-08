/* libs */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <sys/select.h>
#include <time.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>
#include <syslog.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "parser.h"

/** Constants **/

#define SERVER_LISTENING_QUEUE_SIZE 5
#define DEBUG 1
#define NULL_TERMINATOR '\0'
#define SERVER_EXIT_ERROR 1
#define SHOW_LOG 1
#define BITRATES_NUM 50
#define SERVER_TOP_TITLE "\n\033[0;32m**********************************************\n*********** LISOD - HTTP/1.1 Server **********\n**********************************************\033[0m\n\n"
/** Global Variables **/

char *LOG_FILE;
char *FAKE_IP;

/****/

/** Structs **/

typedef struct {
        int maxfd; // largest fd in read_set
        fd_set read_set; // set of all active descriptors
        fd_set ready_set;
        int nready; // Number of ready FDs from select
        int maxi; // the highest index with non -1 number
        client clients[FD_SETSIZE]; // set of active descriptors
}
pool;

/** Function Headers **/

int run_server(int argc, char *argv[]);
void init_pool(int listenfd, pool * p);
int create_listening_socket(struct sockaddr_in addr);
void add_client(int connfd, pool * p, char * log_file);
void check_clients(pool * p, char * www_folder_path, char * log_file);
void remove_client(pool * p, int i);
int daemonize(char * lock_file);
void signal_handler(int sig);

/****/

/**
 * signal_handler: internal signal handler for daemonizeing
 */
void signal_handler(int sig) {
        switch (sig) {
        case SIGHUP:
                /* rehash the server */
                break;
        case SIGTERM:
                /* finalize and shutdown the server */
                // TODO: liso_shutdown(NULL, EXIT_SUCCESS);
                break;
        default:
                break;
                /* unhandled signal */
        }
}

/**
 * daemonize: internal function daemonizing the process
 */
int daemonize(char * lock_file) {
        /* drop to having init() as parent */
        int i, lfp, pid = fork();
        char str[256] = {
                0
        };
        if (pid < 0) exit(EXIT_FAILURE);
        if (pid > 0) exit(EXIT_SUCCESS);

        setsid();

        for (i = getdtablesize(); i >= 0; i--)
                close(i);

        i = open("/dev/null", O_RDWR);
        dup(i); /* stdout */
        dup(i); /* stderr */
        umask(027);

        lfp = open(lock_file, O_RDWR | O_CREAT, 0640);

        if (lfp < 0)
                exit(EXIT_FAILURE); /* can not open */

        if (lockf(lfp, F_TLOCK, 0) < 0)
                exit(EXIT_SUCCESS); /* can not lock */

        /* only first instance continues */
        sprintf(str, "%d\n", getpid());
        write(lfp, str, strlen(str)); /* record pid to lockfile */

        signal(SIGCHLD, SIG_IGN); /* child terminate signal */

        signal(SIGHUP, signal_handler); /* hangup signal */
        signal(SIGTERM, signal_handler); /* software termination signal from kill */

        return EXIT_SUCCESS;
}

/*
 * remove_client: client from the server clients table,
 * and reset his/her struct's entries
 */
void remove_client(pool * p, int i) {

        FD_CLR(p->clients[i].clientfd, &p->read_set);

        close_socket(p->clients[i].clientfd);

        // remove clientfd from pool
        p->clients[i].clientfd = -1;
        p->clients[i].client_context = NULL;

        reset_request_info_buffer( &(p->clients[i]));

}

/*
 * check_clients: retrieve data in the reading buffer
 * depnding if its a secure or insecure connection
 */
void check_clients(pool * p, char * www_folder_path, char * log_file) {

        int i, readret, connfd;
        char buf[BUF_SIZE];
        static client client;

        /* loop over all clients unless you hit the hightest index,
           or there are no active clients available */
        for (i = 0;
             (p->maxi >= i) && (p->nready > 0); i++) {

                client = p->clients[i];
                connfd = client.clientfd;

                if ((connfd > 0) && (FD_ISSET(connfd, &p->ready_set))) {

                        p->nready--;


                        // if it's a web_server, echo the result to the target client
                        if ( client.is_web_server ) {
	                        	readret = recv(client.clientfd, buf, BUF_SIZE, 0);

	                        	if (readret == 0 || (client.sent_empty_line && client.clientfd > 0)) {
		                        		// EOF detected break the connection
		                        	//	remove_client(p, i);

		                        }
		                        else {
	                        			send(client.serving_clientfd, buf, readret, 0);	
                        		}
                        }
                        else {

		                        // if client has bytes_to_be_read > 0 (potentially as part of message-body)
		                        // and sent empty-line (end of headers). Surely this is end of message.
		                        // concat those bytes to reading buffer and decrement the counter

		                        // else it's potentially a request header (or malformed request)
		                        // concat it to the data in the request_buffer

		                        // BUF_SIZE-1 to account for '\0' char later in processing
		                        int bytes_to_be_read = get_int_from_str(client.bytes_to_be_read);
		                        bytes_to_be_read = bytes_to_be_read > 0 && BUF_SIZE > bytes_to_be_read ? bytes_to_be_read : BUF_SIZE - 1;

		                        // buffer the request if another request is not completely processed

		                        readret = recv(client.clientfd, buf, bytes_to_be_read, 0);

		                        // incase of EOF, close the connection
		                        if (readret < 0) {
		                              //  remove_client(p, i);
		                                continue;
		                        }

		                        if (client.request_buffer[0] == 0) {
		                                memcpy(client.request_buffer, buf, readret);
		                                client.request_buffer[readret] = '\0';
		                        } else {
		                             //logger(LOG_FILE, "Appending to the end of the request: %s\n", client.request_buffer, SHOW_LOG);
		                                strcat(client.request_buffer, buf);
		                        }

		                     //logger(LOG_FILE, "client.request_buffer: %s\n", client.request_buffer, SHOW_LOG);

		                        //int close_socket_after_response = 0 close_socket_after_response =
		                        handle_request_header(&client, readret, www_folder_path, LOG_FILE);
		                     //logger(LOG_FILE, "\n request_buffer after handle_request_header:%s\n", client.request_buffer, SHOW_LOG);

		                        // After serving the client, decide whether to close the connection or not.
		                        // Check if it's "connection: close" in that case, close the connection
		                        // TODO: KEEP-ALIVE
		                        if (readret == 0 || (client.sent_empty_line && client.clientfd > 0)) {
		                                // EOF detected break the connection
		                        		// TODO: remove the outgoing connection client too
		                               // remove_client(p, i);

		                        }
                     	}
                }

        }

}

TODO: add bitrate field for bitrate adpatation
/*
 * add_client: add the connection to the server's
 * clients's table
 */
// TODO: lunch a new connection for each new client that connects and add a field in the struct
void add_client(int connfd, pool * p, char * log_file) {

        /* You have in p_clientfd FD_SETSIZE positions,
           find the nearest slot for the new client */
        int i;
        client *client = NULL;
        client *server_client = NULL;

        /* main listening socket is ones of the nready.
           So, we decrement 1 to get the right number of clients */
        p->nready--;

        for (i = 0; i < FD_SETSIZE; i++) {

                client = &(p->clients[i]);

                // Check for open slot for the new connection
                if (p->clients[i].clientfd < 0) {

                        /* Add the new client to the active clients pool
                           & its FD to read set */
                        p->clients[i].clientfd = connfd;
						p->clients[i]->outgoing_port = -1;
						p->clinets[i]->serving_clientfd = -1;
						p->clients[i]->is_web_server = 0;

                        // Malloc space for headers (maximum chars is MAX_LINE)
                        p->clients[i].bytes_to_be_read[0] = 0;

                        int j;
                        for (j = 0; j < 5; j++) {
                                p->clients[i].headers[j][0] = 0;
                        }
                        
                        for (j = 0; j < BITRATES_NUM; j++) {
                                p->clients[i].bitrates[j] = -1;
                        }

                        reset_request_info_buffer( &(p->clients[i]));

                        // add connfd and sock to web server to the active clients' descriptors
                        FD_SET(connfd, &p->read_set);

                        // update the max index of the fdset
                        if (connfd > p->maxfd) {
                                p->maxfd = connfd
                        }
                        if (i > p->maxi) {
                                p->maxi = i;
                        }
                        break;
                }
        }

		/* initializing the socket for Apache Web Server connections, 
			and resolving based on DNS */
		struct sockaddr_in outgoing_addr;
		int outgoing_port;
		struct sockaddr_int actual_addr;
		int actual_addr_size = sizeof(actual_addr);
		outgoing_addr.sin_family = AF_INET;
		// 0 tells the OS to pick the port for us
		outgoing_addr.sin_port = htons(0);
		outgoing_addr.sin_addr.s_addr = FAKE_IP;
		outgoing_sock = create_listening_socket(outgoing_addr);
		getsockname(outgoing_sock, (struct sockaddr*)&actual_addr, &actual_addr_size);

        for (i = 0; i < FD_SETSIZE; i++) {

                server_client = &(p->clients[i]);

                // Check for open slot for the new connection
                if (p->clients[i].clientfd < 0) {
						
						server_client->outgoing_port = ntohs(actual_addr.sin_port);
						server_client->serving_clientfd = connfd;
						server_client->is_web_server = 1;
                        
                        /* Add the new client to the active clients pool
                           & its FD to read set */
                        p->clients[i].clientfd = server_client->outgoing_port;

                        // Malloc space for headers (maximum chars is MAX_LINE)
                        p->clients[i].bytes_to_be_read[0] = 0;

                        int j;
                        for (j = 0; j < 5; j++) {
                                p->clients[i].headers[j][0] = 0;
                        }
                        
                        for (j = 0; j < BITRATES_NUM; j++) {
                                p->clients[i].bitrates[j] = -1;
                        }

                        reset_request_info_buffer( &(p->clients[i]));


                        // add connfd and sock to web server to the active clients' descriptors
                        FD_SET(server_client->outgoing_port, &p->read_set);

                        // update the max index of the fdset
                        if (outgoing_sock > p->maxfd) {
                                p->maxfd = outgoing_sock;
                        }
                        if (i > p->maxi) {
                                p->maxi = i;
                        }
                        break;
                }
        }

        // if looped all the array & didn't find empty slot
        //TODO: review condition logic
        if (i == FD_SETSIZE && (client != NULL || server_client != NULL) ) {
                // Tell the client if s/he online, the server is full
                send_err(client, "add_client error: max num of clients reached");
        }

}


/*
 * create_listening_socket: Create a socket, bind it to the ports and start listening
 */
int create_listening_socket(struct sockaddr_in addr) {

        int sock;

        /* all networked programs must create a socket */
        if ((sock = socket(PF_INET, SOCK_STREAM, 0)) == -1) {
                fprintf(stderr, "Failed creating socket.\nShutting the server down");
                exit(EXIT_FAILURE);
        }

        /* servers bind sockets to ports---notify the OS they accept connections */
        if (bind(sock, (struct sockaddr * ) &addr, sizeof(addr))) {
                close_socket(sock);
                fprintf(stderr, "Failed binding socket.\nShutting the server down");
                exit(EXIT_FAILURE);
        }

        /* set the listening socket then exit*/
        if (listen(sock, SERVER_LISTENING_QUEUE_SIZE)) {
                close_socket(sock);
                fprintf(stderr, "Error listening on socket.\nShutting the server down");
                exit(EXIT_FAILURE);
        }

        return sock;
}

/*
 * create_socket_and_bind: Create a socket, and bind it to the port
 */
int create_socket_and_bind(struct sockaddr_in addr) {

        int sock;

        /* create TCP socket */
        if ((sock = socket(PF_INET, SOCK_STREAM, 0)) == -1) {
                fprintf(stderr, "Failed creating socket.\nShutting the server down");
                exit(EXIT_FAILURE);
        }

        /* bind socket to ports---notify the OS they accept connections */
        if (bind(sock, (struct sockaddr * ) &addr, sizeof(addr))) {
                close_socket(sock);
                fprintf(stderr, "Failed binding socket.\nShutting the server down");
                exit(EXIT_FAILURE);
        }

        return sock;
}

/*
 * init_pool: init the finite state machine for tracking clients' status'
 */
void init_pool(int listenfd, pool * p) {

        // All empty array entries are set to -1
        int i;
        p->maxi = -1;

        for (i = 0; i < FD_SETSIZE; i++) {
                p->clients[i].clientfd = -1;

                // Malloc space for headers (maximum chars is MAX_LINE)
                p->clients[i].bytes_to_be_read = malloc(sizeof(char) * MAX_LINE);
                int j;
                for (j = 0; j < 5; j++) {
                        p->clients[i].headers[j] = malloc(sizeof(char) * MAX_LINE);
                }

        }

        /* setting-up the read set for select() */
        p->maxfd = listenfd;
        FD_ZERO( &p->read_set);
        FD_SET(listenfd, &p->read_set);
}

/*
 *
 ################################################################################
 #            run_server: The Main Function to Start The Server                 #
 ################################################################################
 *
 */
int run_server(int argc, char *argv[]) {

        int sock, client_sock;
        int listen_port;
        int dns_port;
        float alpha;
        char *log_file;
        char *dns_ip;
        char *optional_www_ip;
        socklen_t cli_size;
        struct sockaddr_in addr, cli_addr;
        static pool pool;
        int is_secure_connection;
        int port_len = 10;
        char listen_port_buff[port_len];
        char dns_port_buff[port_len];

        fprintf(stdout, SERVER_TOP_TITLE);

        /**
           Expected paramaters in argv[]

           ./proxy <log> <alpha> <listen-port> <fake-ip>
           		<dns-ip> <dns-port> [<www-ip>]
         **/

        LOG_FILE = argv[1]; // set a global variable for the log_file
        log_file = LOG_FILE;
        alpha = atoi(argv[2]); // for smooth averaging
        listen_port = atoi(argv[3]); // listening for the browser
        FAKE_IP  = argv[4]; // binding for connecting with Web Server
        dns_ip   = argv[5]; // DNS server IP
        dns_port = argv[6]; // DNS server port
        optional_www_ip = argv[7]; // if unavialable DNS resolve to video.cs.cmu.edu
        get_str_from_int(listen_port, listen_port_buff);
        get_str_from_int(dns_port, dns_port_buff);

        // log the provided args
       // print_args(argv, argc);

        // if failed to get the port
        if (listen_port < 1) {
                fprintf(stderr, "Failed to get the port\n");
                return EXIT_FAILURE;
        }

        /* initializing the server for browser connections, and retrieving the listening fd */
        addr.sin_family = AF_INET;
        addr.sin_port = htons(listen_port);
        addr.sin_addr.s_addr = INADDR_ANY;
        sock = create_listening_socket(addr);

        /* setting-up the read set for select() */
        // inititialize the clients' pool
        init_pool(sock, &pool);

        /* finally, loop infinitely, waiting for input and serve clients/browsers */
        while (1) {

                pool.ready_set = pool.read_set;
               //logger(LOG_FILE, "%s", "Blocking Select()...\n", SHOW_LOG);

                // wait for an event ( unread bytes, new connections )
                pool.nready = select(pool.maxfd + 1, &pool.ready_set, NULL, NULL, NULL);
               //logger(LOG_FILE, "%s", "check & add clients\n", SHOW_LOG);

                /*** Check for connection requests & add corresponding clients ***/
                /* Checking HTTP connections */
                if (FD_ISSET(sock, &pool.ready_set)) {

                        cli_size = sizeof(cli_addr);
                        // Accept new connections
                        if ((client_sock = accept(sock, (struct sockaddr * ) &cli_addr, &
                                                  cli_size)) == -1) {
                                close(sock);
                                fprintf(stderr, "Error accepting connection.\n");
                                return EXIT_FAILURE;
                        }
                     //logger(LOG_FILE, "%s", "new Client\n----\n", SHOW_LOG);
                        is_secure_connection = 0;

                        // Add to active client pool
                        add_client(client_sock, &pool, log_file);

                }

                /* Checking HTTPS connections */
                /*if (FD_ISSET(secure_sock, &pool.ready_set)) {
                        secure_cli_size = sizeof(secure_cli_addr);
                        // Accept new connections
                        if ((client_sock = accept(secure_sock, (struct sockaddr * ) &secure_cli_addr, &
                                                  secure_cli_size)) == -1) {
                                close(secure_sock);
                                fprintf(stderr, "Error accepting connection.\n");
                                return EXIT_FAILURE;
                        }

                     //logger(LOG_FILE, "%s", "new Client\n----\n", SHOW_LOG);
                        is_secure_connection = 1;

                        // Add to active client pool
                        add_client(client_sock, &pool, log_file, is_secure_connection);
                }*/

                /* Check events for connected clients */
                check_clients(&pool, www_folder_path, log_file);
        }

}
