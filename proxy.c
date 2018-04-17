#define MAXEVENTS 64
/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400
#define READ_REQ 1
#define SEND_REQ 2
#define READ_RESP 3
#define SEND_RESP 4

#include "csapp.h"
#include <errno.h>
#include<fcntl.h>
#include<stdlib.h>
#include<stdio.h>
#include<sys/epoll.h>
#include<sys/socket.h>
#include<string.h>
#include"cache.h"

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";


struct event_activity{
  int state;
  int listen_fd;
  int conn_fd;
  int server_fd;            //Destination server fd
  char buf[MAXLINE];
  int n_read;
  int n_written;
  int nl_to_write;
  int n_to_read;
};

struct event_action{
  int(*callback)(struct event_activity*);
  //void *arg;
  struct event_activity* arg;
};

struct epoll_event event;
struct epoll_event *events;
int efd;

CacheList* CACHE_LIST;
size_t written = 0;
FILE *log_file;

void command(void);
void write_log_entry(char* uri);//, struct *sockaddr_storage addr);
void interrupt_handler(int);
int handle_new_client(struct event_activity*);
int handle_client(struct event_activity*);
void handler(int connection_fd);
void parse_uri(char *uri, char *hostname, char *path, int *port);
void build_http_header(char *http_header, char *hostname, char *path, int port, int connfd);
int send_req(struct event_activity*);
int recv_resp(struct event_activity*);
int send_resp(struct event_activity*);
void while_check_error(struct event_activity *argptr, struct event_action *ea);

int main(int argc, char **argv){

  int listenfd, connfd;
  socklen_t clientlen;
  struct sockaddr_storage clientaddr;
  struct epoll_event event;
  struct epoll_event *events;
  int i;
  int len;
  struct event_activity *argptr;
  struct event_action *ea;
  struct event_activity *act;

  size_t n;
  char buf[MAX_OBJECT_SIZE];

  if (argc != 2){
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(0);
  }

  listenfd = Open_listenfd(argv[1]);

  //Step 1: Set fd to non-blocking
  if (fcntl(listenfd, F_SETFL, fcntl(listenfd, F_GETFL, 0) | O_NONBLOCK) < 0){
    fprintf(stderr, "error setting socket option to non-blocking\n");
    exit(1);
  }

  //Step 2: Create epoll instance
  if((efd = epoll_create1(0)) < 0){
    fprintf(stderr, "error creating epoll fd\n");
    exit(1);
  }

  //Step 3: Register listenfd with epoll instance
  ea = malloc(sizeof(struct event_action));
  ea->callback = handle_new_client;
  act = (struct event_activity*)malloc(sizeof(struct event_activity));
  act->listen_fd = listenfd;
  argptr = (struct event_activity*)malloc(sizeof(struct event_activity));
  argptr = act;
  //*argptr = listenfd;

  ea->arg = argptr;
  event.data.ptr = ea;
  event.events = EPOLLIN | EPOLLET;
  if (epoll_ctl(efd, EPOLL_CTL_ADD, listenfd, &event) < 0){
    fprintf(stderr, "error adding event\n");
    exit(1);
  }


  //Step 4: Open log file
  //This is done every time a client connects it also closes right after

  //Step 5: initialize cache
  CACHE_LIST = (CacheList*)malloc(sizeof(CacheList));
  cache_init(CACHE_LIST);
  signal(SIGINT, interrupt_handler);

  //Buffer where events are returned
  events = calloc(MAXEVENTS, sizeof(event));

  int num_ready = 0;
  //Step 6: Start an epoll wait loop w/ timeout 1 sec
  printf("before\n");
  while(1){

    num_ready = epoll_wait(efd, events, MAXEVENTS, 1);

    if(num_ready < 0){
      //no events ready
      perror("epoll_wait error");
      break;
    }
    for (int i = 0; i < num_ready; i++) {

      ea = (struct event_action *)events[i].data.ptr;
      argptr = ea->arg;
      if (events[i].events & (EPOLLERR | EPOLLHUP | EPOLLRDHUP)){
        printf("epoll error in execution\n");
        //Check which state to see where error is
        /*if(*argptr->state == READ_REQ){
          fprintf(stderr, "epoll error on fd %d\n", *argptr->conn_fd);
          close(*argptr->conn_fd);
          free(ea->arg);
          free(ea);
        }
        else if(*argptr->state == SEND_REQ){
          fprintf(stderr, "epoll error on fd %d\n", *argptr->conn_fd);
          close(*argptr->conn_fd);
          free(ea->arg);
          free(ea);
        }
        else if(*argptr->state == READ_RESP){
          fprintf(stderr, "epoll error on fd %d\n", *argptr->server_fd);
          close(*argptr->server_fd);
          free(ea->arg);
          free(ea);
        }
        else if(*argptr->state == SEND_REQ){
          fprintf(stderr, "epoll error on fd %d\n", *argptr->src_fd);
          close(*argptr->src_fd);
          free(ea->arg);
          free(ea);
        }
        else{
          fprintf(stderr, "epoll error on fd %d\n", *argptr->listen_fd);
          close(*argptr->listen_fd);
          free(ea->arg);
          free(ea);
        }*/
      }
      if(!ea->callback(argptr)){
        close(argptr->conn_fd);
        free(ea->arg);
        free(ea);
      }
    }
  }//End while
  free(events);
return 0;

}// End main


int handle_new_client(struct event_activity* activity) {
	printf("NEW CLIENT\n");
	socklen_t clientlen;
  int listenfd = activity->listen_fd;
	int connfd;
	struct sockaddr_storage clientaddr;
	struct epoll_event event;
  int *argptr;
	struct event_action *ea;
  struct event_activity *act;

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
    act = (struct event_activity*)malloc(sizeof(struct event_activity));
    act->conn_fd = connfd;
    act->state = READ_REQ;
    argptr = (struct event_activity*)malloc(sizeof(struct event_activity));
    argptr = act;

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

//STATE 1: READ_REQUEST
int handle_client(struct event_activity* activity){
  printf("HANDLE_CLIENT\n");
  int dest_server_fd;
  int connfd = activity->conn_fd;
  char *buf;
  buf = malloc(sizeof(char)*MAXLINE);
  //memset(&buf[0], 0, sizeof(buf));
	char usrbuf[MAXLINE];                                                       //Buffer to read from
  //memset(usrbuf, )
  char *method;                                                      //Method, should be "GET" we don't handle anything else
  method = malloc(sizeof(char)*MAXLINE);
  char *uri;                                                          //The address we are going to i.e(https://www.example.com/)
  uri = malloc(sizeof(char)*MAXLINE);
  char *version;                                                    //Will be changing version to 1.0 always
  version = malloc(sizeof(char)*MAXLINE);
  char *hostname;                                                    //i.e. www.example.com
  hostname = malloc(sizeof(char)*MAXLINE);
  //memset(&hostname[0], 0, sizeof(hostname));
  char *path;                                                       //destinationin server i.e /home/index.html
  path = malloc(sizeof(char)* MAXLINE);
  //memset(&path[0], 0, sizeof(path));
  char *http_header;
  http_header = malloc(sizeof(char)*MAXLINE);

  int port;

  //read first line
  //read(connfd, buf, MAXLINE);

  //rio_t rio_client;                                                           //Client rio_t
  //rio_t rio_server;                                                           //Server rio_t

	//Rio_readinitb(&rio_client, connfd);
	//Rio_readlineb(&rio_client, buf, MAXLINE);
  int nread = 0;
  while( read(connfd, buf, MAXLINE) > 0){
    //Just keep reading, just keep reading...
  }

  //printf("buf\n %s\n", buf);
  sscanf(buf,"%s %s %s", method, uri, version);

  //printf("METHOD: %s\n", method);
  //printf("URI: %s\n", uri);
  //printf("VERSION: %s\n", version);

  if (strcasecmp(method, "GET")){
    printf("Proxy server only implements GET method\n");
    return;
  }

  CachedItem* cached_item = find(buf, CACHE_LIST);
	if(cached_item != NULL){
    printf("cached item found\n");
		move_to_front(cached_item->url, CACHE_LIST);
		size_t to_be_written = cached_item->size;
		written = 0;
		char* item_buf = cached_item->item_p;
		while((written = write(connfd, item_buf, to_be_written)) != to_be_written){
			item_buf += written;
			to_be_written -= written;
		}
		return;
	}

  //Parse the URI to get hostname, path and port
  parse_uri(uri, hostname, path, &port);
	printf("DONE PARSING...\n");
	printf("PATH: %s\n", path);
	printf("PORT: %d\n", port);
	printf("HOSTNAME: %s\n", hostname);


  build_http_header(http_header, hostname, path, port, connfd);
  printf("DONE WITH HEADER\n");
  printf("%s\n", http_header);

  //Write to log_file
  write_log_entry(uri);

  //Unregister connfd
  epoll_ctl(efd, EPOLL_CTL_DEL, connfd, NULL);

  //Get dest_server_fd
  char port_string[100];
  sprintf(port_string, "%d", port);
  dest_server_fd = Open_clientfd(hostname, port_string);

  if(dest_server_fd < 0){
			printf("Connection to %s on port %d unsuccessful\n", hostname, port);
      return;
  }
	printf("CONNECTED!\n");

  //Now register dest_server_fd to epoll
  struct event_activity *argptr;
  struct event_action *ea;
  struct event_activity *act;

  ea = malloc(sizeof(struct event_action));
  ea->callback = send_req;
  act = (struct event_activity*)malloc(sizeof(struct event_activity));
  act->server_fd = dest_server_fd;
  act->state = SEND_REQ;
  act->conn_fd = connfd;
  act->server_fd = dest_server_fd;
  memcpy(act->buf, http_header, sizeof(http_header));
  printf("MEMMOVE\n %s", act->buf);
  argptr = act;

/*  ea->arg = argptr;
  event.data.ptr = ea;
  event.events = EPOLLOUT;
  if (epoll_ctl(efd, EPOLL_CTL_ADD, dest_server_fd, &event) < 0){
    fprintf(stderr, "error adding event at send_req\n");
    exit(1);
  }*/

  free(uri);
  free(hostname);
  free(path);
  free(http_header);
  free(buf);
  free(version);
  free(method);
  return 1;
}


//State 2: SEND_REQ
int send_req(struct event_activity* activity){

  printf("entered send_req\n");
  printf("HTTP_HEADER:\n, %s", activity->buf);

/*  int server_dest_fd = act.server_fd;
  int connfd = act.conn_fd;
  char buf[MAXLINE];
  buf = act.buf;

  int len;
  int total_bytes = 0;
  char usrbuf[MAXLINE];
  char obj[MAX_OBJECT_SIZE];
  while (len = read(server_dest_fd, http_header, MAXLINE, 0) > 0){
    total_bytes += len;
    write(connfd, usrbuf, len);
    if(total_bytes < MAX_OBJECT_SIZE)
      strcat(obj, usrbuf);
  }

  if(total_bytes < MAX_OBJECT_SIZE){
    char* to_be_cached = (char*) malloc(total_bytes);
    strcpy(to_be_cached, obj);
    cache_URL(buf, to_be_cached, total_bytes, CACHE_LIST);
  }

  //Unregister server_dest_fd
  //Can I just close to unregister?
  close(server_dest_fd);
  epoll_ctl(efd, EPOLL_CTL_DEL, server_dest_fd, NULL);

  //Register new event of RECV_RESP
  //Now register dest_server_fd to epoll
  struct event_action *ea;
  struct event_activity *activty;
  struct event_activity *argptr;
  ea = malloc(sizeof(struct event_action));
  ea->callback = recv_resp;
  activty = malloc(sizeof(struct event_activity));
  activity->server_fd = server_dest_fd;
  activity->state = SEND_REQ;
  activity->conn_fd = connfd;
  activty->buf =

  argptr = malloc(sizeof(struct event_activity));
  *argptr = activty;

  //add event to epoll file descriptor
  ea->arg = argptr;
  event.data.ptr = ea;
  event.events = EPOLLOUT;
  if (epoll_ctl(efd, EPOLL_CTL_ADD, dest_server_fd, &event) < 0){
    fprintf(stderr, "error adding event at send_req\n");
    exit(1);
  }*/


  return 1;

}

//State 3: RECV_RESP
int recv_resp(struct event_activity* act){

  printf("entered recv_res\n");



  return 1;
}

//State 4: SEND_RESP
int send_resp(struct event_activity* act){

  printf("entered send_resp\n");

  return 1;
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

void build_http_header(char *http_header, char *hostname, char *path, int port, int connfd){

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

    while(read(connfd, buf, MAXLINE) > 0){

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
void write_log_entry(char* uri){//, struct sockaddr_storage *addr){

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

}// End write_log_entry

/*
void while_check_error(struct event_activity *argptr, struct event_action *ea){
  if(*argptr->state == READ_REQ){
    fprintf(stderr, "epoll error on fd %d\n", *argptr->conn_fd);
    close(*argptr->conn_fd);
    free(ea->arg);
    free(ea);
    return;
  }
  else if(*argptr->state == SEND_REQ){
    fprintf(stderr, "epoll error on fd %d\n", *argptr->conn_fd);
    close(*argptr->conn_fd);
    free(ea->arg);
    free(ea);
    return;
  }
  else if(*argptr->state == READ_RESP){
    fprintf(stderr, "epoll error on fd %d\n", *argptr->server_fd);
    close(*argptr->server_fd);
    free(ea->arg);
    free(ea);
    return;
  }
  else if(*argptr->state == SEND_REQ){
    fprintf(stderr, "epoll error on fd %d\n", *argptr->src_fd);
    close(*argptr->src_fd);
    free(ea->arg);
    free(ea);
    return;
  }
  else{
    fprintf(stderr, "epoll error on fd %d\n", *argptr->listen_fd);
    close(*argptr->listen_fd);
    free(ea->arg);
    free(ea);
    return;
  }
}*/

void interrupt_handler(int num){
    printf("RECEIVED INTERRUPT!\n");
    printf("FREEING DATA\n");
    cache_destruct(CACHE_LIST);
    free(CACHE_LIST);
    free(events);
	//Will also need to free ea
    exit(0);
}// End interrupt_handler
