#include "rio.h"
#include <postgresql/libpq-fe.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

// Create a single open connection (using environmental variables corresponding
// to the parameters ) to the database server (PgConn *)
int main(int argc, char *argv[]) {
  // fprintf(stderr, "Connecting to database\n");
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
    printf("%s%s", buf, res);
    fflush(stdout);
    close(STDOUT_FILENO);
    PQfinish(dbconn);
    return 1;
  }

  char buf[MAXLINE];
  char *res = "Connection to database successful\r\n";
  // response line
  sprintf(buf, "HTTP/1.1 200 OK\r\n");
  // response headers
  sprintf(buf, "%sContent-Type: text/plain\r\n", buf);
  sprintf(buf, "%sContent-Length: %lu\r\n", buf, strlen(res));
  // end of headers
  sprintf(buf, "%s\r\n", buf);
  rio_write(STDOUT_FILENO, buf, strlen(buf));
  rio_write(STDOUT_FILENO, res, strlen(res));
  close(STDOUT_FILENO);
  PQfinish(dbconn);
  return 0;
}
