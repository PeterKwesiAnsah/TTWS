#define __GNU_SOURCE

#include "rio.h"
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define BACKLOG 1024
#define MAXLINE 1024

int main(int argc, char *argv[]) {
  if (argc != 2) {
    printf("Usage: %s <port>\n", argv[0]);
    return 1;
  }
  int gai_err;
  int sfd, listenfd;
  char *port = argv[1];
  struct addrinfo hint;
  struct addrinfo *result, *rp;

  memset(&hint, 0, sizeof(struct addrinfo));
  hint.ai_family = AF_UNSPEC;
  hint.ai_protocol = 0;
  hint.ai_socktype = SOCK_STREAM;
  hint.ai_flags = AI_ADDRCONFIG | AI_PASSIVE | AI_NUMERICSERV;

  if ((gai_err = getaddrinfo(NULL, port, &hint, &result) != 0)) {
    gai_strerror(gai_err);
    return 1;
  }
  rp = result;
  for (; rp; rp = rp->ai_next) {
    if ((sfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol)) < 0)
      continue;
    if (bind(sfd, rp->ai_addr, rp->ai_addrlen) != 0) {
      close(sfd);
      continue;
    }
    if (listen(sfd, BACKLOG) < 0) {
      close(sfd);
      continue;
    }
    break;
  }
  listenfd = sfd;
  if (!rp) {
    fprintf(stderr, "Invalid Hostname or port\n");
    exit(1);
  }
  freeaddrinfo(result);
  printf("Socket %d listening on port %s\n", listenfd, port);
  while (1) {
    struct sockaddr_storage client_addr;
    socklen_t addr_len = sizeof(struct sockaddr_storage);
    int connfd = accept(listenfd, (struct sockaddr *)&client_addr, &addr_len);
    if (connfd < 0) {
      fprintf(stderr, "failed to accept client connections");
      exit(1);
    }
    char host[NI_MAXHOST];
    char serv[NI_MAXSERV];
    if (getnameinfo((struct sockaddr *)&client_addr, addr_len, host, NI_MAXHOST,
                    serv, NI_MAXSERV, NI_NUMERICHOST | NI_NUMERICSERV) == 0)
      printf("Connection from (%s, %s) client\n", host, serv);
    else
      printf("Connection from unknown client");
    char *res = "Hello, World!\r\n";
    // response
    char buf[MAXLINE];
    //response line
    sprintf(buf, "HTTP/1.1 200 OK\r\n");
    // response headers
    sprintf(buf, "%sContent-Type: text/html\r\n", buf);
    sprintf(buf, "%sContent-Length: %lu\r\n", buf, strlen(res));
    // end of headers
    sprintf(buf, "%s\r\n", buf);
    rio_write(connfd, buf, strlen(buf));
    rio_write(connfd, res, strlen(res));
  }
  return 0;
}
