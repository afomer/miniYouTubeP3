#include <stdio.h>
#include "clients.h"

// ANDREW ID: afomer

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";

/*
 * * * * * * * * * * * * * * * *
 * The Web Proxy Main Function *
 * * * * * * * * * * * * * * * *
 */
int main(int argc, char **argv) 
{
	return run_server(argc, argv);
}

/*
 * doit - handles the client's GET HTTP requests
 *
 */
void doit(void* fdp) 
{    
    
    pthread_detach(pthread_self());

    int fd = *((int *)(fdp));
    
    free(fdp);

    /* initlizing different buffers */
    // method -> method recieved (Only accept GET)
    // uri -> uri from the user (for parsing port, domain and path)
    // proxy_request -> for making the main get request 
    char buf[MAXLINE], method[MAXLINE],version[MAXLINE],
    uri[MAXLINE], proxy_request[MAXLINE],
    server_response[MAXLINE], cache_buff[MAX_OBJECT_SIZE],
    domain[MAXLINE],  // buffers for the domain, port and path
    port[MAXLINE], path[MAXLINE];
    
    // Setting the default path to '/'
    path[0] = '/'; 
    path[1] = '\0';

    // Setting the default port to 80    
    sprintf(port, "%d", 80); 
    

    // variables used in the case of reading and writing to the client
    int bytes_read = 0;
    int buff_size = 0;
   
    
    // setting rio structs
    rio_t rio_proxy;
    rio_t rio_client;
          

    // initiliazing the proxy request and server_response buffers
    // (because strcat() is used, and want avoid '\0' errors)
    int i;
    for (i = 0; i < MAXLINE; ++i)
    {
        proxy_request[i] = 0;
    }

    for (i = 0; i < MAXLINE; ++i)
    {
        server_response[i] = 0;
    }
   
     
    Rio_readinitb(&rio_client, fd);
    
    // if the fd is bad, abort
    if ( fd < 0 )
    {
        return;
    }

    // if the request is not GET, reject it
    // otherwise retrieve the uri
    if (!Rio_readlineb(&rio_client, buf, MAXLINE)) 
    {
        Close(fd);
        return;
    }
    sscanf(buf, "%s %s %s", method, uri, version);       
    if (strcasecmp(method, "GET")) {                     
        clienterror(fd, method, "501", "Not Implemented",
                    "The proxy does not implement this method");
        Close(fd);
        return;
    }                                                    

    // parse the uri to domain, path and port
    parse_uri(uri, domain, path, port);
      

    /* look for the GET request in the cache, if it's available serve
       from the cache, otherwise just request it from the user */

    entry_t tmp_entry = cache_lookup(domain, path, port);

    // if you find an entry then use the cached data to server the client
    if ( tmp_entry != NULL )
    {
       Rio_writen(fd, tmp_entry->buff, tmp_entry->buff_size);

       Close(fd);
       
       return;
    }

   /* creating the sockets and making the
       conncetion with the web server */
    
    // Connect to the web server the client requested
    int proxy_clientfd = open_clientfd(domain, port);

    // if the domain couldn't be identinfied, 
    // write bad GET Request to the user
    if ( proxy_clientfd < 0 )
    {
        Rio_writen(fd, "The Bad GET request \n",strlen("The Bad GET request \n")+1);
        return;
    }

    // for the client of the proxy
    Rio_readinitb(&rio_proxy, proxy_clientfd); 

    // Creat the GET request and send it to the web server
    strcat(strcat(strcat(proxy_request,"GET "),path), " HTTP/1.0\r\n");
  
    rio_writen(proxy_clientfd, proxy_request, strlen(proxy_request));

    // Read the request headers by the client, send the appropraite ones
    // to the web server
    read_requesthdrs(&rio_client, &rio_proxy, domain, proxy_clientfd);                              //line:netp:doit:readrequesthdrs
      
    // Read the response from the web server, copy it in the cache buffer
    // then send it to the client
    while( (bytes_read = 
        Rio_readn(proxy_clientfd, server_response, MAX_OBJECT_SIZE)) > 0)
    {
       memcpy(cache_buff , server_response, bytes_read);
       
       buff_size += bytes_read;

    }

    // After the data is read put it in the cache
    cache_insert(domain, path, port, cache_buff, buff_size);

    // Send the web server response to the user, and close 
    // the conncetion with him/her and the web server
    Rio_writen(fd, cache_buff, buff_size);

    Close(proxy_clientfd);
    Close(fd);
    
   return;
}

/*
 * read_requesthdrs - read HTTP request headers
 */
void read_requesthdrs(rio_t *rio_client, rio_t *rio_proxy, char* domain,
 int proxy_clientfd) 
{   
    // setting the buffers for, the host, buf and header title
    char buf[MAXLINE];
    char writing_buf[MAXLINE];
    char header_title[MAXLINE]; /* the name of the header */
    char possible_host[MAXLINE];

    // initilaizing, the possible-host
    // (to avoid strcat errors)
    int i;
    for (i = 0; i < MAXLINE; i++)
    {
        possible_host[i] = 0;
    }
    
    // by default, there is no host header sent by the user
    int there_is_host = 0;
    
    // Setting the possible host
    strcat(strcat(strcat(writing_buf, "Host: "), domain), "\r\n");
  
    // setting the connection headers
    char* connection_header1 = "Connection: close\r\n";
    char* connection_header2 = "Proxy-Connection: close\r\n";
    
    // Reading a header from from the user
    Rio_readlineb(rio_client, buf, MAXLINE);

    /* Store client headers in a buffer 
    (except connection and proxy-conncetion) */
    while(strcmp(buf, "\r\n") != 0)
    {
        // extract the name of the header with sscanf
        sscanf(buf, "%[^:]", header_title);
        
        // if the header_title is connection or proxy-connection,
        // ignore the the header, 
        // otherwise write it to the headers buffer
        if ( strcmp(header_title,"Connection") != 0 &&
             strcmp(header_title,"Proxy-Connection") != 0 )
            
        {
           strcat(writing_buf, buf);

           // if what you just wrote is a host
           // set host = 1 
           // ( don't write the set host header later)
           if ( strcmp(header_title,"Host") == 0)     
           {
             there_is_host = 1;
           }


        }

        // read a line, from the client and put it in the read buffer
        Rio_readlineb(rio_client, buf, MAXLINE);
        
    }
    
    // Write the host if the user didn't specify a host
    if ( there_is_host == 0)
    {
        strcat(writing_buf, possible_host);
    }
    
    // Store the user-agent and connection headers
    strcat(writing_buf, user_agent_hdr);
    strcat(writing_buf, connection_header1);
    strcat(writing_buf, connection_header2);
    
    // Storing the empty charachter, at the end,
    // declaring the end of the request
    strcat(writing_buf, "\r\n\r\n");
    
    
    // writing everything from the writing buffer to the web server
    Rio_writen(proxy_clientfd, buf ,strlen(buf));

    return;
}


/*
 * parse_uri - parse the URI into domain, path and port
 */
void parse_uri(char *uri, char* domain, char* path, char* port) 
{   

    // initializing the domain and path to 0 to avoid
    // possible data corruption (because of '\0')
    int j;
    for (j = 0; j < MAXLINE; ++j)
    {
        domain[j] = 0;
    }

    for (j = 0; j < MAXLINE; ++j)
    {
        path[j] = 0;
    }


    /* Get the case finding the first / and last / or ;
      after this point, a port is decided if there is one
      and then the port is retrieved */    
    int start = 0;
    int end = 0;

    while ( uri[start] != '/')
    {
        start++;
    }
    
    end = start+=2;

    while ( uri[end] != '/' && uri[end] != ':')
    {
        end++;
    }
    
    int i;
    
    for (i = start; i < end ;i++)
    {
        domain[i - start] = uri[i];
    }

    domain[i - start] = '\0';
    
    // looking for the path starting and ending points
    if(uri[end] == ':')
    {

      for (i = end++; uri[i] != '/' ;i++)
      {
        port[i - end] = uri[i];
      }

      port[i - end] = '\0';

    }
    
    int slash = i;
    int n = strlen(uri);

    // Retrieving the full path from the uri
    for (; i < n;i++)
    {
      path[i - slash] = uri[i];
    }

      path[i] = '\0';
    
    return;
   
}


/*
 * clienterror - returns an error message to the client
 */
void clienterror(int fd, char *cause, char *errnum, 
         char *shortmsg, char *longmsg) 
{
    char buf[MAXLINE], body[MAXBUF];

    /* Build the HTTP response body */
    sprintf(body, "<html><title>Proxy Error</title>");
    sprintf(body, "%s<body bgcolor=""ffffff"">\r\n", body);
    sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
    sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
    sprintf(body, "%s<hr><em>Proxy Web server</em>\r\n", body);

    /* Print the HTTP response */
    sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-type: text/html\r\n");
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
    Rio_writen(fd, buf, strlen(buf));
    Rio_writen(fd, body, strlen(body));
}

/****************/
/*** END HERE ***/
/****************/
