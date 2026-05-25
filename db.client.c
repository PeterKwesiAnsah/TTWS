#include "rio.h"
#include <postgresql/libpq-fe.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define URI_ARG_INDEX 1
#define METHOD_ARG_INDEX 2
#define VERSION_ARG_INDEX 3

void client_error(int connfd, int status, const char *msg) {
  char res[MAXLINE];
  sprintf(res, "HTTP/1.1 %d %s\r\n\r\n", status, msg);
  rio_write(connfd, res, strlen(res));
  close(connfd);
}
// Create a single open connection (using environmental variables corresponding
// to the parameters ) to the database server (PgConn *)
int main(int argc, char *argv[]) {
  if (argc != 4) {
    fprintf(stderr, "Usage: %s <uri> <method> <version>\n", argv[0]);
    client_error(STDOUT_FILENO, 400, "Bad Request");
    exit(1);
  }

  PGconn *dbconn =
      PQconnectdbParams((const char *[]){NULL}, (const char *[]){NULL}, 0);
  if (PQstatus(dbconn) != CONNECTION_OK) {
    // fprintf(stderr, "Connection to database failed\n");
    char buf[MAXLINE];
    char res[MAXLINE];
    sprintf(res, "Server could not connect to the database server:%s\r\n",
            PQerrorMessage(dbconn));
    sprintf(buf, "HTTP/1.1 500 Internal Server Error\r\n");
    // response headers
    sprintf(buf, "%sContent-Type: text/plain\r\n", buf);
    sprintf(buf, "%sContent-Length: %lu\r\n", buf, strlen(res));
    sprintf(buf, "%s\r\n", buf);

    rio_write(STDOUT_FILENO, buf, strlen(buf));
    rio_write(STDOUT_FILENO, res, strlen(res));
    close(STDOUT_FILENO);
    PQfinish(dbconn);
    return 1;
  }

  // uri between /users/<userid> and /
  // users table have id | first_name | last_name columns
  if (argv[URI_ARG_INDEX] && strlen(argv[URI_ARG_INDEX]) == 1) {
    // /
  } else {
    // /users/<user-id
    char *suser_id = argv[URI_ARG_INDEX] + strlen("/users/");
    char *endptr;
    // check for non-valid string
    size_t user_id = strtol(suser_id, &endptr, 10);
    if (*endptr != '\0' || suser_id == endptr) {
      client_error(STDOUT_FILENO, 400, "Bad Request");
      PQfinish(dbconn);
      exit(1);
    }
  }
  close(STDOUT_FILENO);
  PQfinish(dbconn);
  return 0;
}
