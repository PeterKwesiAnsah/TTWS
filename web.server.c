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
#define METHOD_LENGTH 36
#define HTTP_VERSION 36

const char *supported_urls[] = {"/", "/create_user", "/users/", NULL};
void client_error(int connfd, int status, const char *msg) {
  char res[MAXLINE];
  snprintf(res, MAXLINE, "HTTP/1.1 %d %s\r\n\r\n", status, msg);
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
    char *method, *uri, *version;

    // read packets from client
    rio rp;
    memset(&rp, 0, sizeof(rp));
    rp.fd = connfd;
    rio_readline(&rp, buf, MAXLINE);
    method = buf;
    uri = strchr(buf, ' ');
    if (!uri) {
      // invalid request line
      close(connfd);
      continue;
    }
    *uri = '\0';
    uri = uri + 1;
    version = strchr(uri, ' ');
    if (!version) {
      // invalid request line
      close(connfd);
      continue;
    }
    *version = '\0';
    version = version + 1;

    {
      int i = 0;
      // TODO: Check Allowed Methods on URIs
      const char *cur_uri = supported_urls[i];
      size_t uri_len = strlen(uri);
      while (cur_uri) {
        size_t cur_uri_len = strlen(cur_uri);
        if (uri_len == 1 && !strcasecmp(cur_uri, uri))
          break;

        printf("request uri: %s (%lu) application uri: %s (%lu)\n", uri,
               uri_len, cur_uri, cur_uri_len);
        if (strlen(cur_uri) == 1) {
          cur_uri = supported_urls[++i];
          continue;
        }
        if (strncmp(cur_uri, uri, cur_uri_len) == 0) {
          // /create_user or /users
          if (*(uri + 1) == 'c') {
            printf("Create_User\n");
            // /create_user
            if (*(uri + cur_uri_len) == '/' || *(uri + cur_uri_len) == '?' ||
                *(uri + cur_uri_len) == '\0') {

              if (*(uri + cur_uri_len) == '\0')
                break;

              if (*(uri + cur_uri_len) == '?') {
                // trailing ?
                if (uri_len == cur_uri_len + 1)
                  break;
              }

              if (*(uri + cur_uri_len) == '/') {
                // trailing /
                if (uri_len == cur_uri_len + 1)
                  break;
              }
            }

            client_error(connfd, 404, "Not Found");
            goto LISTENING_LOOP;

          } else if (strncmp(uri, "/users/", 7) == 0) {
            printf("Users\n");
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
        cur_uri = supported_urls[++i];
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
        // Handle POST requests
        int fd = open("create_user.html", O_RDONLY);
        if (fd < 0) {
          client_error(connfd, 500, "Internal Server Error");
          exit(1);
        }
        struct stat st;
        fstat(fd, &st);
        snprintf(buf, MAXLINE,
                 "HTTP/1.1 200 OK\r\n"
                 "Content-Type: text/html\r\n"
                 "Content-Length: %lu\r\n"
                 "\r\n",
                 st.st_size);

        rio_write(connfd, buf, strlen(buf));
        char *fbuf = mmap(NULL, st.st_size, PROT_READ, MAP_SHARED, fd, 0);
        if (!fbuf) {
          client_error(connfd, 500, "Internal Server Error");
          exit(1);
        }
        rio_write(connfd, fbuf, st.st_size);
        exit(1);
      }

      char *envp[] = {"PGHOST=localhost",
                      "PGUSER=postgres",
                      "PGPASSWORD=postgres",
                      "PGPORT=5432",
                      uri,
                      method,
                      version,
                      NULL};
      char *argp[] = {"./db.client", uri, method, version, NULL};

      dup2(connfd, STDOUT_FILENO);
      if (execve(argp[0], argp, envp) < 0) {
        perror("execve");
        exit(1);
      };
    }
    close(connfd);
  }
  return 0;
}
