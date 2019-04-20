#include <stdio.h>
#include <string.h>
#include <time.h>
#include <stdlib.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>

#define BITRATES_NUM 50
#define BUF_SIZE 9096
#define MAX_LINE 8192

/* indices of the supported headers */
#define ACCPETED_HEADERS_NUM 5
#define CONNECTION_INDEX 0
#define DATE_INDEX 1
#define CONTENT_LENGTH_INDEX 2
#define CONTENT_TYPE_INDEX 3
#define LAST_MODIFIED_INDEX 4

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
        int t_curr;

} client;

typedef struct client* client_ptr;

/* struct for preparing a response for a client*/
typedef struct {
        int size;
        char *buffer;
} response;


/* struct for messages of DNS */
struct dns_msg {

	// header
	uint16_t ID;
	uint16_t layer2; // values based on the range requested
	uint16_t QDCOUNT; // values based on the range requested
	uint16_t ANCOUNT; // values based on the range requested
	uint16_t NSCOUNT; // values based on the range requested
	uint16_t ARCOUNT; // values based on the range requested

	// body
	/*
		uint8_t QNAME; // usually 1 but not always
		uint16_t QTYPE; // usually 1 but not always
		uint16_t QCLASS; // should be always IN (for internet)
		{
			NAME // domain name of the resource, variable
			uint16_t TYPE // 1 in all
			uint16_t CLASS // always 1
			uint32_t TTL // always 0 in our case
			uint16_t RDLENGTH // length of RDATA
			uint32_t RDATA // TYPE is A and the CLASS is IN, the RDATA field is a 4 octet ARPA Internet address.
		}
	*/
	uint8_t body[1500]; // STANDARD UDP PACKET SIZE = 1500

};

typedef struct dns_msg dns_msg_s;

uint8_t QR_QUERY_CODE = 0;

uint8_t QR_RESPONSE_CODE = 1;

// query example

/*
* get_question_details: get the question section details from DNS message
*

      0  1  2  3  4  5  6  7  8  9  0  1  2  3  4  5
    +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
    |                                               |
    /                     QNAME                     /
    /                                               /
    +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
    |                     QTYPE                     |
    +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
    |                     QCLASS                    |
    +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+

*/
uint8_t *get_question_details(dns_msg_s *dns_msg, char *buffer) {

	// octet = 8-bits = 1 byte
	uint8_t *question_ptr;
	uint8_t  label_length;

	question_ptr = (uint8_t *)(dns_msg->body);

	while (question_ptr != NULL || question_ptr[0] == 0) {

		// length of octet
		label_length = ((uint8_t *)question_ptr)[0];

		// those label octets
		question_ptr = ((uint8_t *)question_ptr) + 1;

		memcpy(buffer, (char *)question_ptr, label_length);
		buffer[label_length] = '\0';
		//printf("Get buffer: %s\n", buffer);

		// for multi-label capability, uncomment
		/* strcat(buffer, '\n');
		*/
		question_ptr += label_length + 1;
		break; // only get the first label
	}


	uint16_t QTYPE = ((uint16_t *)question_ptr)[0];
	question_ptr = question_ptr + 2;

	uint16_t QCLASS = ((uint16_t *)question_ptr)[0];
	question_ptr = question_ptr + 2;


	//printf("\n[QNAME=%s] [QTYPE=%d] [QCLASS=%hu]\n", buffer, QTYPE, QCLASS);

	return question_ptr;
}

/*
* get_question_details: get the question section details from DNS message
*

      0  1  2  3  4  5  6  7  8  9  0  1  2  3  4  5
    +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
    |                                               |
    /                     QNAME                     /
    /                                               /
    +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
    |                     QTYPE                     |
    +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
    |                     QCLASS                    |
    +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+

*/
uint8_t *set_question_details(dns_msg_s *dns_msg, uint8_t *QNAME, uint16_t QTYPE, uint16_t QCLASS) {

	// question section is the first part of the DNS-Message body
	uint8_t *question_ptr = dns_msg->body;
	uint8_t QNAME_len = strlen((char *)QNAME);

	// put QNAME size
	memcpy(question_ptr, &QNAME_len, sizeof(uint8_t));
	question_ptr = question_ptr + 1;

	// put QNAME label
	strncpy((char *)question_ptr, (char *)QNAME, strlen((char *)QNAME) * sizeof(uint8_t));
	question_ptr = question_ptr + strlen((char *)QNAME) * sizeof(uint8_t);

	// put QTYPE (2 octet)
	memcpy((uint16_t *)question_ptr, (uint16_t *)(&QTYPE), sizeof(uint16_t));
	question_ptr = question_ptr + sizeof(uint16_t);

	// put QCLASS (2 octet)
	memcpy((uint16_t *)question_ptr, (uint16_t *)(&QCLASS), sizeof(uint16_t));
	question_ptr = question_ptr + sizeof(uint16_t);

	return question_ptr;
}

uint8_t *set_answer_details(uint8_t *answer_original_ptr, uint8_t *NAME, uint16_t TYPE, 
	uint16_t CLASS, uint32_t TTL, uint16_t RDLENGTH, uint8_t *RDATA) {

	// question section is the first part of the DNS-Message body
	uint8_t *answer_ptr = answer_original_ptr;
	uint8_t NAME_len	= strlen((char *)NAME);

	// put NAME size
	memcpy(answer_ptr, &NAME_len, sizeof(uint8_t));
	answer_ptr = answer_ptr + 1;

	// put NAME label
	strncpy((char *)answer_ptr, (char *)NAME, strlen((char *)NAME) * sizeof(uint8_t));
	answer_ptr = answer_ptr + strlen((char *)NAME) * sizeof(uint8_t);

	// put TYPE (2 octet)
	memcpy((uint16_t *)answer_ptr, &TYPE, sizeof(uint16_t));
	answer_ptr = answer_ptr + sizeof(uint16_t);

	// put CLASS (2 octet)
	memcpy((uint16_t *)answer_ptr, &CLASS, sizeof(uint16_t));
	answer_ptr = answer_ptr + sizeof(uint16_t);

	// put TTL (2 octet)
	memcpy((uint32_t *)answer_ptr, &TTL, sizeof(uint32_t));
	answer_ptr = answer_ptr + sizeof(uint32_t);

	// put RDLENGTH (2 octet)
	memcpy((uint16_t *)answer_ptr, &RDLENGTH, sizeof(uint16_t));
	answer_ptr = answer_ptr + sizeof(uint16_t);

	// put RDATA (2 octet)
	memcpy((uint8_t *)answer_ptr, RDATA, RDLENGTH); // +1 for '\0'
	answer_ptr = answer_ptr + sizeof(uint8_t) + 1;

	return answer_ptr;

}
/*
* return end ptr
*/
uint8_t *get_answer_details(uint8_t *answer_original_ptr, char *buffer) {

	// question section is the first part of the DNS-Message body
	uint8_t *answer_ptr = answer_original_ptr;

	// put NAME size
	uint8_t NAME_len = answer_ptr[0];
	answer_ptr = answer_ptr + 1;

	// put NAME label
	answer_ptr = answer_ptr + NAME_len * sizeof(uint8_t);

	// put TYPE (2 octet)
	answer_ptr = answer_ptr + sizeof(uint16_t);

	// put CLASS (2 octet)
	answer_ptr = answer_ptr + sizeof(uint16_t);

	// put TTL (2 octet)
	answer_ptr = answer_ptr + sizeof(uint32_t);

	// put RDLENGTH (2 octet)
	uint16_t RDLENGTH = 0;
	RDLENGTH = ((uint16_t *)answer_ptr)[0] ;
	answer_ptr = answer_ptr + sizeof(uint16_t);

	// put RDATA (2 octet)
	memcpy(buffer, (uint8_t *)answer_ptr, RDLENGTH);
	answer_ptr = answer_ptr + sizeof(uint8_t);

	return answer_ptr;

}

/*	
			 (--- DNS Message structure ---)

      0  1  2  3  4  5  6  7  8  9  0  1  2  3  4  5
    +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
    |                      ID                       |
    +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
    |QR|   Opcode  |AA|TC|RD|RA|   Z    |   RCODE   |
    +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
    |                    QDCOUNT                    |
    +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
    |                    ANCOUNT                    |
    +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
    |                    NSCOUNT                    |
    +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
    |                    ARCOUNT                    |
    +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+


*/
uint8_t *generate_dns_query(dns_msg_s *dns_msg, uint8_t *QNAME) {

	dns_msg->ID = 0; // id is always 0
	dns_msg->layer2 =  0;
	dns_msg->QDCOUNT = 1;
	dns_msg->ANCOUNT = 0;
	dns_msg->NSCOUNT = 0;
	dns_msg->ARCOUNT = 0;
	uint8_t QTYPE  = 1;
	uint8_t QCLASS = 1;

	uint8_t *end_ptr = set_question_details(dns_msg, QNAME, QTYPE, QCLASS);
	return end_ptr;
}

/*
* generate_dns_answer:
* notice: assumes the ID is set, and one question
*/
uint8_t *generate_dns_answer(dns_msg_s *dns_msg, uint8_t *RDATA, uint16_t RDLENGTH) {
	dns_msg->layer2 =  0;
	dns_msg->layer2 =  dns_msg->layer2 | (1 << 15); // set QR for response
	dns_msg->layer2 =  dns_msg->layer2 | (1 << 10); // set AA for response
	dns_msg->ANCOUNT = 1;
	uint16_t TYPE = 1; // A records 
	uint16_t CLASS = 1; // returning IPs
	uint32_t TTL = 0;
	char NAME[1500];

	// QNAME, QTYPE, QCLASS
	uint8_t *answer_ptr = get_question_details(dns_msg, NAME);
	uint8_t *end_ptr	= set_answer_details(answer_ptr, (uint8_t *)NAME, TYPE, CLASS, TTL, RDLENGTH, RDATA);
	return end_ptr;
}

char *get_dns_answer_RDATA(dns_msg_s *dns_msg, char *answer_buffer) {

	char dummy_buffer[1500];

	// pass question
	uint8_t *answer_ptr = get_question_details(dns_msg, dummy_buffer);
	
	// pass answer
	uint8_t *end_ptr	= get_answer_details(answer_ptr, answer_buffer);
	
	// return answer
	printf("answer_buffer: %s\n", answer_buffer);
	return answer_buffer;
}

int get_best_bitrate(client *client) {
	//TODO: pick best bitrate according to t_curr 1.5x crit.
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

// Testing the parse of .f4m files, by trying to get all bitrates
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

char* get_str_from_int(int number, char *buffer) {
        sprintf(buffer, "%d", number);
        return buffer;
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
		int nolist_str_len = extn - (client->uri);
		char nolist_str[MAX_LINE];
		memcpy(nolist_str, client->uri, nolist_str_len);
		sprintf(client->uri, "%s_nolist.f4m", nolist_str);
	}

	// for the case that you're modifying segment request
	// get the best bitrate and modify the uri
	char *path = strrchr(client->uri, '/');
	char *sub_path = strstr(client->uri, "Seg"); //TODO: make sure it's lower case
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

typedef struct server_list server_list_s;
typedef struct node node_s;

struct node {
	int seq_num;
	char ip[128];
	server_list_s *neighbours;
	struct node *next;
};

struct server_list {
	struct node *head;
	struct node *tail;
	struct node *curr;
	int node_num;
};


server_list_s *create_server_list() {
	server_list_s *server_list = malloc(sizeof(struct server_list));
	server_list->curr = NULL;
	server_list->node_num = 0;
	server_list->head = NULL;
	server_list->tail = NULL;

	return server_list;
}

node_s *print_list(server_list_s *servers) {
	
	node_s *tmp_ptr = servers->head;
	
	int node_i = 0;
	int nodes_max = servers->node_num;
	printf("----(%d)\n", nodes_max);

	while (tmp_ptr != NULL) {
		node_i  = node_i + 1;
		printf("ip: %s - seq_num: %d\n", tmp_ptr->ip, tmp_ptr->seq_num);
		tmp_ptr = tmp_ptr->next;
	}
	printf("----\n");
	return NULL;

}

node_s *set_server(server_list_s *server_list, char *new_server_ip) {
	// TODO: make sure there are no duplicates
	// else add it
	
	node_s *node = malloc(sizeof(struct node));
	strcpy(node->ip, new_server_ip);
	node->next = server_list->head;
	node->neighbours = NULL;
	node->seq_num = 0;
	server_list->head = node;
	server_list->node_num = server_list->node_num + 1;

	return node;
}

node_s *deqeue_server(server_list_s *servers) {
	
	if ( servers == NULL || servers->head == NULL) {
		return NULL;
	}
	else if (servers->head->next == NULL) {
		node_s *tmp_ptr = servers->head;
		servers->node_num = servers->node_num - 1;
		servers->head = NULL;
		return tmp_ptr;
	}

	node_s *tmp_ptr  = servers->head;
	node_s *prev_ptr = NULL;

	while (tmp_ptr->next != NULL){
		prev_ptr = tmp_ptr;
		tmp_ptr = tmp_ptr->next;
	}
	
	servers->node_num = servers->node_num - 1;
	prev_ptr->next = NULL;
	return tmp_ptr;
}

node_s *get_server_by_ip(char *source_ip, server_list_s *servers) {
	node_s *tmp_ptr;

	for (tmp_ptr = servers->head; tmp_ptr != NULL; tmp_ptr = tmp_ptr->next) {
		if ( strcmp(tmp_ptr->ip, source_ip) == 0 ) {
			return tmp_ptr;
		}
	}

	return NULL;
}

server_list_s *get_neighbours_from_str(char *list) {
	
	server_list_s *neighbours = create_server_list();

	//printf("list: %s - head: %d\n", list, neighbours->head == NULL);

	// if there is only one entry recognize it
	if (strchr(list, ',') == NULL) {
		set_server(neighbours, list);
	}

	char line[BUF_SIZE];
	char *begin = list;
	char *end = strchr(list, ',');

	// point to lines by beginning and ending pointers
	while (end != NULL) {
		// if a request_line is encountered
		// perform resolution on the uri
		// then forward the request to the webserver
		// for other requests foward without modification
		strncpy(line, begin, (int)(end - begin) );
		set_server(neighbours, line);
		begin = end + 1;
		end = strchr(begin, ',');
	}

	// take care of the last entry in multi-neighbour case
	begin = strrchr(list, ',');
	end = strrchr(list, '\0');

	if ( begin != NULL && end != NULL) {
		// if a request_line is encountered
		// perform resolution on the uri
		// then forward the request to the webserver
		// for other requests foward without modification
		memcpy(line, begin + 1, end - begin);
		line[end - begin] = '\0';

		//printf("line: %s\n", line);
		set_server(neighbours, line);
	}

	return neighbours;
}

/*
* get_best_server_round_robin: servers is a linked list, in which the head is
* refered to when the end of the list is reached
*/
char *get_best_server_round_robin(server_list_s *servers) {
	node_s *server_p;
	char   *ip = NULL;

	if (servers->curr == NULL) {
		servers->curr = servers->head;
		ip = servers->curr != NULL ? servers->curr->ip : NULL;
	} else {
		ip = servers->curr != NULL ? servers->curr->ip : NULL;
		servers->curr = servers->curr->next == NULL ? servers->head : servers->curr->next;
	}

	return ip;
}

node_s *is_in_server_list(char *source_ip, server_list_s *servers) {
	node_s *tmp_ptr;
	//print_list(servers);
	for (tmp_ptr = servers->head; tmp_ptr; tmp_ptr = tmp_ptr->next) {
	//	printf("ip = %s\n", tmp_ptr->ip);
		if ( strcmp(tmp_ptr->ip, source_ip) == 0 ) {
			return tmp_ptr;
		}
	}
	//printf("SHOOT\n");
	return NULL;
}

/*
	handle_LSAs: and populate server_list
*/
server_list_s *handle_LSAs(char *LSAs_file) {

  // Read the file line by line
  char *line = NULL;
  char sender_ip[BUF_SIZE];
  char neighbours[BUF_SIZE];
  size_t len = 0;
  int seq_num;

  FILE *fp = fopen(LSAs_file, "r");

  if (fp == NULL) {
  	printf("Problem with LSAs file\n");
  	exit(0);
  }

  // all IPs/routes seen
  server_list_s *servers = create_server_list();
  node_s *found_server;
  node_s *new_server;
  
  // getline will allocate a space for the lines that should be freed
  // (that happens because line is NULL and len is 0)
	while (getline(&line, &len, fp) != -1) {
		sscanf(line, "%s %d %s", sender_ip, &seq_num, neighbours);
		//printf("line: %s %d %s\n", sender_ip, seq_num, neighbours);
		
		found_server = is_in_server_list(sender_ip, servers);
		if ( found_server != NULL) {
			if ( seq_num > found_server->seq_num ) {
				found_server->seq_num = seq_num;
				found_server->neighbours = get_neighbours_from_str(neighbours);
				// TODO: free old neighbours
			}
		}
		else {
			new_server = set_server(servers, sender_ip);
			new_server->seq_num = seq_num;			
			new_server->neighbours = get_neighbours_from_str(neighbours);
		}

		// print
		//printf("get neighbours\n");
	}

	printf("\nLSA Sources (%d)\n", servers->node_num);
	print_list(servers);
	fclose(fp);
	return servers;
}

char *get_best_server_dijkstra(char *source_ip, server_list_s *providing_servers, server_list_s *servers_with_LSAs) {

	// since, all links are weighted at 1 BFS == Dijkstra
	// distance to all others as INFINITY
	// Q - queue of vertices vertices
	server_list_s *queue = create_server_list();
	server_list_s *visited_vertices = create_server_list();
	set_server(queue, source_ip);
	set_server(visited_vertices, source_ip);

	printf("Client SOURCE: %s\n", source_ip);
	node_s *node = queue->head;
	int remove_status = 1;

	// while Q is not empty
	while ( queue->head != NULL && node != NULL) {
		// remove the element u with the min dis
		node_s *node = deqeue_server(queue);

		// if reached any server (non-router then return it)
		// TODO: node is a server (not DNS nor client)
		if ( node != NULL && is_in_server_list(node->ip, providing_servers) != NULL && strstr(node->ip, source_ip) == NULL && strstr(node->ip, "router") == NULL ) {
			printf("Best server: %s\n", node->ip);
			return node->ip;
		}

		// for all neighbours of u
		printf("original node ip: %s\n", node->ip);
		node_s *found_node = is_in_server_list(node->ip, servers_with_LSAs);
		node_s *neighbour;

		printf("----[%s] node ip: %s\n", node->ip, found_node ? found_node->ip : "NO Entry");

		for (neighbour = found_node && found_node->neighbours ? found_node->neighbours->head : NULL; neighbour; neighbour = neighbour->next) {
			// if N(s) is not visisted
			//printf("is_in_server_list \n\n");
			//node_s *x = queue->head != NULL ? print_list(queue) : NULL;
			
			if ( is_in_server_list(neighbour->ip, visited_vertices) == NULL ) {
				// label it as visited then add it to visited nodes
			//	printf("addd to visited nodes\n");
			//	printf("neighbour: %s (%d, %d) \n", neighbour->ip, queue == NULL, visited_vertices == NULL);
				set_server(visited_vertices, neighbour->ip);
				//printf("add to queue\n");
				set_server(queue, neighbour->ip);
			}
			else {
				//printf("visited already\n");
			}
		}

	}

	exit(0);
	return NULL;

}

server_list_s *handle_servers(char *servers_file) {

  // Read the file line by line
  char *line = NULL;
  char server_ip_buff[BUF_SIZE];
  node_s *new_server;
  size_t len = 0;

  FILE *fp = fopen(servers_file, "r");
  if (fp == NULL) {
  	printf("Problem with Servers file\n");
  	exit(0);
  }

  // all IPs/routes seen
  server_list_s *servers = create_server_list();

  // getline will allocate a space for the lines that should be freed
  // (that happens because line is NULL and len is 0)
	while (getline(&line, &len, fp) != -1) {
		sscanf(line, "%s", server_ip_buff);
		//printf("line: %s %d %s\n", server_ip_buff, seq_num, neighbours);
				
		new_server = set_server(servers, server_ip_buff);
		new_server->seq_num = 0;			
		new_server->neighbours = NULL;
	}

	printf("\nServers (%d)\n", servers->node_num);
	print_list(servers);
	fclose(fp);
	return servers;
}

int main(int argc, char **argv)
{
	/** Test DNS Messages **/
	// TODO: if I don't have a domain name, say I don't have it
	// RCODE = 3 (Name Error)

	// create listening socket
	int sock;
	char data_received[1500];
	socklen_t fromlen;

	if ((sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP)) == -1) {
    	perror("could not create socket");
    	exit(-1);
  	}

  	server_list_s *servers_by_LSAs= handle_LSAs(argv[1]);

  	server_list_s *providing_servers = handle_servers(argv[2]);

  	get_best_server_dijkstra("1.0.0.1", providing_servers, servers_by_LSAs);

  	exit(0);

	// sendto listening socket, read the data
	struct sockaddr_in myaddr;
	bzero(&myaddr, sizeof(myaddr));
	fromlen = sizeof(myaddr);
	myaddr.sin_family = AF_INET;
	myaddr.sin_addr.s_addr = htonl(INADDR_ANY);
	myaddr.sin_port = htons(8080);
	
	bind(sock, (struct sockaddr *) &myaddr, sizeof(myaddr));

	// Make servers
	server_list_s *servers = create_server_list();
	
	// before calling for 
	set_server(servers, "128.2.9.1");
	set_server(servers, "2.9.1.8");
	set_server(servers, "12.9.1.8");
	set_server(servers, "2456.9.1.8");
	servers->curr = servers->head;

	int q = 0;
	while (q < 10) {
	static dns_msg_s dns_msg;
	char str_buf[1500];
	uint8_t *QNAME = (uint8_t *)"localhost";
	generate_dns_query(&dns_msg, QNAME);
	sendto(sock, data_received, 1500, 0, (struct sockaddr *) &myaddr, (fromlen) );
	recvfrom(sock, data_received, 1500, 0, (struct sockaddr *) &myaddr, &(fromlen) );
	get_question_details( (dns_msg_s *)(&data_received), str_buf);
	printf("extracted name from DNS Query: %s\n", str_buf);

	// always RDLENGTH takes into account the '\0'
	char *best_server = get_best_server_round_robin(servers);
	generate_dns_answer(&dns_msg, (uint8_t *)best_server, strlen(best_server) + 1);
	get_dns_answer_RDATA(&dns_msg, str_buf);
	printf("extracted RDATA from DNS Answer: %s [best_server=%s]\n", str_buf, best_server);
	q++;}
	exit(0);

	/*****/
	static client client;
	char buf[BUF_SIZE*125];
	int i;
	for (i = 0; i < BITRATES_NUM; i++) {
		client.bitrates[i] = -1;
	}

	FILE *fp = fopen("./static/example.f4m", "r");
	fseek(fp, 0, SEEK_END);
	long fsize = ftell(fp);
	fseek(fp, 0, SEEK_SET);  /* same as rewind(f); */
	fread(buf, 1, fsize, fp);
	fclose(fp);
	printf("Getting the bitrates...\n");
	get_bit_rates(&client, buf);
	printf("new bitrates: %s\n", client.uri);

	printf("Fetched bitrates:\n");
	printf("[Stored] bitrate_1=%d\n", 400);
	printf("[Stored] bitrate_2=%d\n", 700);
	printf("[Stored] bitrate_3=%d\n", 872);
	printf("[Stored] bitrate_4=%d\n", 1072);
	printf("[Stored] bitrate_5=%d\n", 1372);

	char *example_uri = "/vod/big_buck_bunny.f4m";
	strcpy(client.uri, example_uri);
	handle_path(&client, buf);
	int bitrate_str_mod_test_1 = strcmp(client.uri, "/vod/big_buck_bunny_nolist.f4m") == 0;
	printf("[example_uri=%s] [genereated_uri=%s]\n", example_uri, client.uri);

	char *seq_uri = "/vod/10Seg2-Frag3";
	strcpy(client.uri, seq_uri);	
	client.t_curr = 2679;
	handle_path(&client, example_uri);
	int bitrate_adapt_test1 = strcmp(client.uri, "/vod/10Seg2-Frag3") == 0;
	printf("[seq_uri=%s] [genereated_uri=%s]\n", seq_uri, client.uri);

	char *seq2_uri = "/path/to/video/-1209500Seg2-Frag3";
	strcpy(client.uri, seq_uri);	
	client.t_curr = 2680;
	handle_path(&client, seq2_uri);
	int bitrate_adapt_test2 = strcmp(client.uri, "/path/to/video/1072Seg2-Frag3") == 0;
	printf("[seq_uri=%s] [genereated_uri=%s]\n", seq2_uri, client.uri);

	if (bitrate_str_mod_test_1 && bitrate_adapt_test1 && bitrate_adapt_test2 && client.bitrates[0] == 400 && client.bitrates[1] == 700 && 
		client.bitrates[2] == 872 && client.bitrates[3] == 1072 && 
		client.bitrates[4] == 1372) {
		printf("\nPASSED ALL TESTS\n");
	}
	else {
		printf("FAILED TEST\n");
	}

	return 0;
}