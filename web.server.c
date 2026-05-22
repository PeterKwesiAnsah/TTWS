#define __GNU_SOURCE
#include <stddef.h>
#include <strings.h>

#include "rio.h"
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#define BACKLOG 1024
#define MAXLINE 1024
#define METHOD_LENGTH 24
#define HTTP_VERSION 24

const char *supported_urls[] = {"/", "/create_user", "/users/", NULL};
void client_error(int connfd, int status, const char *msg) {
  char res[MAXLINE];
  sprintf(res, "HTTP/1.1 %d %s\r\n\r\n", status, msg);
  rio_write(connfd, res, strlen(res));
  close(connfd);
}

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
LISTENING_LOOP:
  while (1) {
    struct sockaddr_storage client_addr;
    socklen_t addr_len = sizeof(struct sockaddr_storage);
    int connfd = accept(listenfd, (struct sockaddr *)&client_addr, &addr_len);
    if (connfd < 0) {
      fprintf(stderr, "failed to accept client connections\n");
      exit(1);
    }
    char host[NI_MAXHOST];
    char serv[NI_MAXSERV];
    if (getnameinfo((struct sockaddr *)&client_addr, addr_len, host, NI_MAXHOST,
                    serv, NI_MAXSERV, NI_NUMERICHOST | NI_NUMERICSERV) == 0)
      printf("Connection from (%s, %s) client\n", host, serv);
    else
      printf("Connection from unknown client");
    char buf[MAXLINE];
    char method[METHOD_LENGTH];
    char uri[MAXLINE];
    char version[HTTP_VERSION];
    // read packets from client
    rio rp;
    memset(&rp, 0, sizeof(rp));
    rp.fd = connfd;
    rio_readline(&rp, buf, MAXLINE);
    sscanf(buf, "%s %s %s", method, uri, version);

    if (strcasecmp(method, "GET")) {
      client_error(connfd, 501, "Method Not Implemented");
      continue;
    }

    {
      int i = 0;
      const char *cur_uri = supported_urls[i++];
      size_t uri_len = strlen(uri);
      while (cur_uri) {
        size_t cur_uri_len = strlen(cur_uri);
        if (uri_len == 1 && !strcasecmp(cur_uri, uri))
          break;

        if (strncmp(cur_uri, uri, cur_uri_len) == 0) {
          // /create_user or /users
          if (*(cur_uri + 1) == 'c') {
            // /create_user
            if (uri_len > cur_uri_len + 1) {
              // we send 404 Not Found
              client_error(connfd, 404, "Not Found");
              goto LISTENING_LOOP;
            }
            // supporting trailing slashes
            char c = *(char *)(uri + cur_uri_len);
            if (c != '\0' || c != '/') {
              // we send 404 Not Found
              client_error(connfd, 404, "Not Found");
              goto LISTENING_LOOP;
            }
          } else {
            // /users/
            if (uri_len == cur_uri_len) {
              // we send 400 Bad Request
              client_error(connfd, 400, "Bad Request");
              goto LISTENING_LOOP;
            }
            char *path_params = uri + cur_uri_len;
            // if there's another slash it should be trailing
            if ((path_params = strchr(path_params, '/')) == NULL)
              break;
            if (!path_params && *(uri + (uri_len - 1)) != '/') {
              // we send 404 Not Found
              client_error(connfd, 404, "Not Found");
              goto LISTENING_LOOP;
            }
            break;
          }
        }
        cur_uri = supported_urls[i++];
      }
      if (cur_uri == NULL) {
        client_error(connfd, 404, "Not Found");
        goto LISTENING_LOOP;
      }
    }
    // invoke the db client program
    if (fork() == 0) {
      close(listenfd);
      if (*(uri + 1) == 'c') {
        int fd = open("create_user.html", O_RDONLY);
        if (fd < 0) {
          client_error(connfd, 500, "Internal Server Error");
          exit(1);
        }
        struct stat st;
        fstat(fd, &st);
        sprintf(buf, "HTTP/1.1 200 OK\r\n");
        // response headers
        sprintf(buf, "%sContent-Type: text/html\r\n", buf);
        sprintf(buf, "%sContent-Length: %lu\r\n", buf, st.st_size);
        sprintf(buf, "%s\r\n", buf);

        rio_write(connfd, buf, strlen(buf));
        char *fbuf = mmap(NULL, st.st_size, PROT_READ, MAP_SHARED, fd, 0);
        if (!fbuf) {
          client_error(connfd, 500, "Internal Server Error");
          exit(1);
        }
        rio_write(connfd, fbuf, st.st_size);
        exit(1);
      }
      char *envp[] = {"PGHOST=localhost", "PGUSER=postgres",
                      "PGPASSWORD=postgres", "PGPORT=5432", NULL};
      char *argp[] = {NULL};

      dup2(connfd, STDOUT_FILENO);
      if (execve("./db.client", argp, envp) < 0) {
        perror("execve");
        exit(1);
      };
    }
    close(connfd);
  }
  return 0;
}
