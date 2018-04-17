#include "csapp.h"
#include<errno.h>
#include<fcntl.h>
#include<stdlib.h>
#include<stdio.h>
#include<sys/epoll.h>
#include<sys/socket.h>
#include<string.h>
#include"cache.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

/* $begin select */
#define MAXEVENTS 64

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";


void command(void);
void interrupt_handler(int);
int handle_client(int connfd);
int handle_new_client(int listenfd);
void write_log_entry(char* uri, size_t size);//, struct *sockaddr_storage addr);
void handler(int connection_fd);
void parse_uri(char *uri, char *hostname, char *path, int *port);
void build_http_header(char *http_header, char *hostname, char *path, int port, rio_t *rio_client);

CacheList* CACHE_LIST;
size_t written = 0;
FILE *log_file;

struct event_action {
	int (*callback)(int);
	void *arg;
};

int efd;

int main(int argc, char **argv)
{
	int listenfd;
	socklen_t clientlen;
	struct sockaddr_storage clientaddr;
	struct epoll_event event;
	struct epoll_event *events;
	int i;
	//int len;
	int *argptr;
	struct event_action *ea;

	size_t n;
	//char buf[MAXLINE];

	if (argc != 2) {
		fprintf(stderr, "usage: %s <port>\n", argv[0]);
		exit(0);
	}

	listenfd = Open_listenfd(argv[1]);

	// set fd to non-blocking (set flags while keeping existing flags)
	if (fcntl(listenfd, F_SETFL, fcntl(listenfd, F_GETFL, 0) | O_NONBLOCK) < 0) {
		fprintf(stderr, "error setting socket option\n");
		exit(1);
	}

	//Step 1: create epoll instance
	if ((efd = epoll_create1(0)) < 0) {
		fprintf(stderr, "error creating epoll fd\n");
		exit(1);
	}

	//Step 2: initialize cache.
	CACHE_LIST = (CacheList*)malloc(sizeof(CacheList));
  cache_init(CACHE_LIST);
  signal(SIGINT, interrupt_handler);


	ea = malloc(sizeof(struct event_action));
	ea->callback = handle_new_client;
	argptr = malloc(sizeof(int));
	*argptr = listenfd;

	ea->arg = argptr;
	event.data.ptr = ea;
	event.events = EPOLLIN | EPOLLET; // use edge-triggered monitoring
	if (epoll_ctl(efd, EPOLL_CTL_ADD, listenfd, &event) < 0) {
		fprintf(stderr, "error adding event\n");
		exit(1);
	}

	/* Buffer where events are returned */
	events = calloc(MAXEVENTS, sizeof(event));

	while (1) {
		// wait for event to happen (no timeout)
		n = epoll_wait(efd, events, MAXEVENTS, -1);
		//add checker for timeout and errors

		for (i = 0; i < n; i++) {
			ea = (struct event_action *)events[i].data.ptr;
			argptr = ea->arg;
			if (events[i].events & (EPOLLERR | EPOLLHUP | EPOLLRDHUP)) {
				/* An error has occured on this fd */
				fprintf (stderr, "epoll error on fd %d\n", *argptr);
				close(*argptr);
				free(ea->arg);
				free(ea);
				continue;
			}

			if (!ea->callback(*argptr)) {
				close(*argptr);
				free(ea->arg);
				free(ea);
			}

		}
	}
	free(events);
}

int handle_new_client(int listenfd) {
	printf("NEW CLIENT\n");
	socklen_t clientlen;
	int connfd;
	struct sockaddr_storage clientaddr;
	struct epoll_event event;
	int *argptr;
	struct event_action *ea;

	clientlen = sizeof(struct sockaddr_storage);

	// loop and get all the connections that are available
	while ((connfd = accept(listenfd, (struct sockaddr *)&clientaddr, &clientlen)) > 0) {

		// set fd to non-blocking (set flags while keeping existing flags)
		if (fcntl(connfd, F_SETFL, fcntl(connfd, F_GETFL, 0) | O_NONBLOCK) < 0) {
			fprintf(stderr, "error setting socket option\n");
			exit(1);
		}

		ea = malloc(sizeof(struct event_action));
		ea->callback = handle_client;
		argptr = malloc(sizeof(int));
		*argptr = connfd;

		// add event to epoll file descriptor
		ea->arg = argptr;
		event.data.ptr = ea;
		event.events = EPOLLIN | EPOLLET; // use edge-triggered monitoring
		if (epoll_ctl(efd, EPOLL_CTL_ADD, connfd, &event) < 0) {
			fprintf(stderr, "error adding event\n");
			exit(1);
		}
	}

	if (errno == EWOULDBLOCK || errno == EAGAIN) {
		// no more clients to accept()
		return 1;
	} else {
		perror("error accepting");
		return 0;
	}
}


int handle_client(int connection_fd) {

	//printf("entered handle client\n");

	int dest_server_fd;
	char buf[MAXLINE];
	memset(&buf[0], 0, sizeof(buf));
	char usrbuf[MAXLINE];                                                       //Buffer to read from
  char method[MAXLINE];                                                       //Method, should be "GET" we don't handle anything else
  char uri[MAXLINE];                                                          //The address we are going to i.e(https://www.example.com/)
  char version[MAXLINE];                                                      //Will be changing version to 1.0 always
	char hostname[MAXLINE];                                                     //i.e. www.example.com
	memset(&hostname[0], 0, sizeof(hostname));
  char path[MAXLINE];                                                         //destinationin server i.e /home/index.html
	memset(&path[0], 0, sizeof(path));
  char http_header[MAXLINE];
	//memset(&http_header[0], 0, sizeof(http_header));
	//char* http_header = malloc(MAXLINE * sizeof(char));

  int port;

	rio_t rio_client;                                                           //Client rio_t
  rio_t rio_server;                                                           //Server rio_t

	Rio_readinitb(&rio_client, connection_fd);
	Rio_readlineb(&rio_client, buf, MAXLINE);                                   //Read first line
	sscanf(buf,"%s %s %s", method, uri, version);

	if(strcasecmp(method, "GET")){                                              //If somethinge besides "GET", disregard
			printf("Proxy server only implements GET method\n");
			return;
	}

	//printf("URI: %s\n", uri);
	//printf("VERSION: %s\n", version);
	//printf("METHOD: %s\n", method);

	//printf("Checking cache...\n");
	//Check if request exists in cache
	CachedItem* cached_item = find(buf, CACHE_LIST);
	if(cached_item != NULL){
		move_to_front(cached_item->url, CACHE_LIST);
		size_t to_be_written = cached_item->size;
		written = 0;
		char* item_buf = cached_item->item_p;
		while((written = rio_writen(connection_fd, item_buf, to_be_written)) != to_be_written){
			item_buf += written;
			to_be_written -= written;
		}
		return;
	}

	//printf("DNE in cache...\n");

	/*  PARSE_URI
	*   get the hostname
	*   check if desired port is input or set to default port 80
	*   get the path from URI
	*/

	memset(&path[0], 0, sizeof(path));                                          //Reset the memeory of path
	memset(&hostname[0], 0, sizeof(hostname));                                  //Reset the memory of hostname
	//memset(&uri[0], 0, sizeof(uri));

	//Parse the URI to get hostname, path and port
	//printf("PARSING URI...\n");
  parse_uri(uri, hostname, path, &port);
	//printf("DONE PARSING...\n");
	//printf("PATH: %s\n", path);
	//printf("PORT: %d\n", port);
	//printf("HOSTNAME: %s\n", hostname);

	//Build the http header from the parsed_uri to send to server
	//printf("build_http_header\n");
  build_http_header(http_header, hostname, path, port, &rio_client);
	//printf("DONE WITH HEADER\n");
  printf("%s\n", http_header);

	//printf("ATTEMPTING CONNECTION TO DESTINATION SERVER\n");
	//Establish connection to destination server
  char port_string[100];
  sprintf(port_string, "%d", port);
  dest_server_fd = Open_clientfd(hostname, port_string);

  if(dest_server_fd < 0){
			printf("Connection to %s on port %d unsuccessful\n", hostname, port);
      return;
  }

	printf("CONNECTED!\n");

	//Send and receive info to and from destination server
  Rio_readinitb(&rio_server, dest_server_fd);
  rio_writen(dest_server_fd, http_header, sizeof(http_header));

	printf("after writen\n");

  size_t size = 0;
  size_t total_bytes = 0;
  char obj[MAX_OBJECT_SIZE];
  while((size = rio_readlineb(&rio_server, usrbuf, MAXLINE)) != 0){
          total_bytes += size;
          rio_writen(connection_fd, usrbuf, size);
          if(total_bytes < MAX_OBJECT_SIZE)
            strcat(obj, usrbuf);
  }

	printf("buf: %s\n", buf);

  if(total_bytes < MAX_OBJECT_SIZE){
    char* to_be_cached = (char*) malloc(total_bytes);
    strcpy(to_be_cached, obj);
    cache_URL(buf, to_be_cached, total_bytes, CACHE_LIST);
  }

	printf("CACHED: %s\n", CACHE_LIST->first->url);

	write_log_entry(buf, total_bytes);
	//free(http_header);

	// int len;
	// while ((len = recv(connfd, buf, MAXLINE, 0)) > 0) {
	// 	printf("Received %d bytes\n", len);
	// 	send(connfd, buf, len, 0);
	// }
	// if (len == 0) {
	// 	// EOF received.
	// 	// Closing the fd will automatically unregister the fd
	// 	// from the efd
	// 	return 0;
	// } else if (errno == EWOULDBLOCK || errno == EAGAIN) {
	// 	return 1;
	// 	// no more data to read()
	// } else {
	// 	perror("error reading");
	// 	return 0;
	// }
}

void interrupt_handler(int num){
    cache_destruct(CACHE_LIST);
    free(CACHE_LIST);
	//Will also need to free ea
    exit(0);
}

void write_log_entry(char* uri, size_t size){//, struct sockaddr_storage *addr){

	//Format current time string
	time_t t;
	char t_string[MAXLINE];
	t = time(NULL);
	strftime(t_string, MAXLINE, "%a %d %b %Y %H:%M %S %Z", localtime(&t));
	//printf("time: %s\n", t_string);

	//Open log.txt
	//"a" - open for writing
	log_file = fopen("log.txt", "a");
	fprintf(log_file, "REQUEST ON: %s\n", t_string);
	//fprintf(log_file, "FROM: %s\n", host);
	fprintf(log_file, "URI: %s\n\n", uri);
	fclose(log_file);

}

void parse_uri(char *uri, char *hostname, char *path, int *port){

    char* sub_str1 = strstr(uri, "//");
    char my_sub[MAXLINE];
    memset(&my_sub[0], 0, sizeof(my_sub));
    char* sub = my_sub;
    char num[MAXLINE];
    int hostname_set = 0;

    *port = 80;                                                                 //Default port is 80

    if(sub_str1 != NULL){
        int i = 2;                                                              //advance past the '//'
        int j = 0;
        for(; i < strlen(sub_str1); i++)
            sub[j++] = sub_str1[i];
    }
    //printf("sub: %s\n", sub);                                                 //sub contains everything after http://

    /*  Check if colon exists in sub-string
    *   if it exists, we have a designated port
    *   else port is already set to default port 80
    */
    char* port_substring = strstr(sub, ":");
    if(port_substring != NULL){
        int x = 1;
        int y = 0;
        while(1){                                                               //Get port numbers
            if(port_substring[x] == '/')
                break;
            num[y++] = port_substring[x++];
        }
        *port = atoi(num);                                                      //Set port

        x = 0;
        y = 0;
        while(1){
            if(sub[y] == ':')
                break;
            hostname[x++] = sub[y++];
        }
        hostname_set = 1;
    }
    //printf("PORT: %d\n", *port);

    //Get Path
    char *sub_path = strstr(sub, "/");
    //printf("sub_path: %s\n", sub_path);
    if(sub_path != NULL){
        int a = 0;
        int b = 0;
        while(1){
            if(sub_path[b] == '\0')
                break;
            path[a++] = sub_path[b++];
        }
        if(!hostname_set){                                                      //If the hostname is not set
            a = 0;                                                              //Set it...
            b = 0;
            while(1){
                if(sub[b] == '/')
                    break;
                hostname[a++] = sub[b++];
            }
        }
    }
}

void build_http_header(char *http_header, char *hostname, char *path, int port, rio_t *rio_client){

    char buf[MAXLINE];
    char request_header[MAXLINE];
    char host_header[MAXLINE];
    char other_headers[MAXLINE];

    char *connection_header = "Connection: close\r\n";
    char *prox_header = "Proxy-Connection: close\r\n";
    char *host_header_format = "Host: %s\r\n";
    char *request_header_format = "GET %s HTTP/1.0\r\n";
    char *carriage_return = "\r\n";

    char *connection_key = "Connection";
    char *user_agent_key= "User-Agent";
    char *proxy_connection_key = "Proxy-Connection";
    char *host_key = "Host";

    int connection_len = strlen(connection_key);
    int user_len = strlen(user_agent_key);
    int proxy_len = strlen(proxy_connection_key);
    int host_len = strlen(host_key);

    sprintf(request_header, request_header_format, path);

    while(Rio_readlineb(rio_client, buf, MAXLINE) > 0){

            //Check for EOF first
            if(!strcmp(buf, carriage_return))
                break;

            //Check for host_key in buf
            //strncasecmp is not case sensitive
            //compares host_len chars in buf to host_key
            if(!strncasecmp(buf, host_key, host_len)){
                strcpy(host_header, buf);
                continue;
            }

            //Check for any headers that are other_headers
            if( !strncasecmp(buf, connection_key, connection_len) &&
                !strncasecmp(buf, proxy_connection_key, proxy_len) &&
                !strncasecmp(buf, user_agent_key, user_len)){
                    strcat(other_headers, buf);
                }
    }

    if(strlen(host_header) == 0)                                                //If host header is not set, set it here
        sprintf(host_header, host_header_format, hostname);

    //Build the http header string
    sprintf(http_header, "%s%s%s%s%s%s%s", request_header, host_header, connection_header,
                             prox_header, user_agent_hdr, other_headers,
                             carriage_return);
}
/* $end select */


//how to delete
//epoll_ctl(efd, EPOLL_DEL, fd, NULL)

/*
	ordering
	read request
		cfd, IN
		getaddrinfo here
	sendreq
		sfd, OUT
	RecRes
		sfd, IN
	SendRes
		cfd, OUT

	struct with the following stuff
		{
		int state
		int clientfd
		int serverfd
		unsigned char buffer[MAXLINE]
		int nread																													//
		int nwritten
		int nl_to_write
		int n_to_read
	} event_activity
*/
