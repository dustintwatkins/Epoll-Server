#define MAXEVENTS 64

#include "csapp.h"
#include <errno.h>
#include<fcntl.h>
#include<stdlib.h>
#include<stdio.h>
#include<sys/epoll.h>
#include<sys/socket.h>
#include<string.h>

void command(void);
int handle_client(int connfd);
int handle_new_client(int listenfd);

struct event_action{
  int(*callback)(void *);
  void *arg;

};

struct event_activity{
  int state;
  int client_fd;
  int src_fd;
  char buf[MAXLINE];
  int n_read;
  int n_written;
  int nl_to_write;
  int n_to_read;
};

int efd;

int main(int argc, char **argv){

  int listenfd, connfd;
  socklen_t clientlen;
  struct sockaddr_storage clientaddr;
  struct epoll_event event;
  struct epoll_event *events;
  int i;
  int len;
  int *argptr;
  struct event_action *ea;

  size_t n;
  char buf[MAXLINE];

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
  argptr = malloc(sizeof(int));
  *argptr = listenfd;

  ea->arg = argptr;
  event.data.ptr = ea;
  event.events = EPOLLIN | EPOLLET;
  if (epoll_ctl(efd, EPOLL_CTL_ADD, listenfd, &event) < 0){
    fprintf(stderr, "error adding event\n");
    exit(1);
  }

return 0;

}// End main
