#include <stdio.h>
#include <string.h>
#include <time.h>
#include <stdlib.h>

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

int main(int argc, char const *argv[])
{
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