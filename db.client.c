#include "common.h"
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
#define MAX_COMMAND_LENGTH 1024
#define BODY_BUFFER_SIZE 1024

void client_error(int connfd, int status, const char *msg) {
  char res[MAXLINE];
  sprintf(res, "HTTP/1.1 %d %s\r\n\r\n", status, msg);
  rio_write(connfd, res, strlen(res));
  close(connfd);
}
// TODO: handle premature client connection closure
// TODO: Find a way to parse and generate HTML Pages from HTML templates like showing error or success messages
//  Create a single open connection (using environmental variables corresponding
//  to the parameters ) to the database server (PgConn *)
int main(int argc, char *argv[]) {
  if (argc != 4) {
    fprintf(stderr, "Usage: %s <uri> <method> <version>\n", argv[0]);
    client_error(STDOUT_FILENO, 400, "Bad Request");
    exit(1);
  }

  PGconn *dbconn =
      PQconnectdbParams((const char *[]){NULL}, (const char *[]){NULL}, 0);
  if (PQstatus(dbconn) != CONNECTION_OK) {
    char html_res[MAXLINE];
    char body[BODY_BUFFER_SIZE];
    snprintf(body, BODY_BUFFER_SIZE,
             "<!doctype html>"
             "<html>"
             "<head><title>Database Error</title></head>"
             "<body><span style='color:red;'>Server could not connect to the "
             "database server: %s</span></body>"
             "</html>",
             PQerrorMessage(dbconn));
    snprintf(html_res, MAXLINE,
             "HTTP/1.1 500 Internal Server\r\n"
             "Content-Type: text/html\r\n"
             "Content-Length: %lu\r\n"
             "\r\n"
             "%s",
             strlen(body), body);
    // response headers
    rio_write(STDOUT_FILENO, html_res, strlen(html_res));
    goto CLEAN_UP;
  }

  // uri between /users/<userid> and /
  // users table have id | first_name | last_name columns
  if (argv[URI_ARG_INDEX] && strlen(argv[URI_ARG_INDEX]) == 1) {
    // /
    client_error(STDOUT_FILENO, 501, "Not Implemented");
    PQfinish(dbconn);
    exit(1);
  } else if (strncmp(argv[URI_ARG_INDEX], "/users/", 7) == 0) {
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
    char command[MAX_COMMAND_LENGTH];
    snprintf(command, MAX_COMMAND_LENGTH, "SELECT * from users WHERE id = %lu;",
             user_id);

    PGresult *qresult = PQexec(dbconn, command);
    ExecStatusType execstype = PQresultStatus(qresult);
    if (execstype != PGRES_TUPLES_OK) {
      // send error to client
      char *html_res = command;
      char body[BODY_BUFFER_SIZE];
      snprintf(body, BODY_BUFFER_SIZE,
               "<!doctype html>"
               "<html>"
               "<head><title>Database Error</title></head>"
               "<body><span style='color:red;'>Error %d: %s</span></body>"
               "</html>",
               execstype, PQresStatus(execstype));
      snprintf(html_res, MAX_COMMAND_LENGTH,
               "HTTP/1.1 500 Internal Server\r\n"
               "Content-Type: text/html\r\n"
               "Content-Length: %lu\r\n"
               "\r\n"
               "%s",
               strlen(html_res), html_res);
      rio_write(STDOUT_FILENO, body, strlen(body));
      PQclear(qresult);
      goto CLEAN_UP;
    }
    int rowscount = PQntuples(qresult);
    // if rowscount is 0, user not found
    if (rowscount == 0) {
      char *html_res = command;
      char body[BODY_BUFFER_SIZE];
      snprintf(body, BODY_BUFFER_SIZE,
               "<!doctype html>"
               "<html>"
               "<head><title>User: Not Found</title></head>"
               "<body><span style='color:red;'>No User Found</span></body>"
               "</html>");
      snprintf(html_res, MAX_COMMAND_LENGTH,
               "HTTP/1.1 404 Not Found\r\n"
               "Content-Type: text/html\r\n"
               "Content-Length: %lu\r\n"
               "\r\n"
               "%s",
               strlen(body), body);
      rio_write(STDOUT_FILENO, html_res, strlen(html_res));
      PQclear(qresult);
      goto CLEAN_UP;
    }

    char *html_res = command;
    char body[BODY_BUFFER_SIZE];

    snprintf(body, BODY_BUFFER_SIZE,
             "<!doctype html>"
             "<html>"
             "<head><title>User: %lu</title></head>"
             "<body>"
             "<span>First Name: %s</span></br>"
             "<span>Last Name: %s</span></br>"
             "<button><a href='/create_user'>Add New User</a></button>"
             "</body>"
             "</html>",
             user_id, PQgetvalue(qresult, 0, 1), PQgetvalue(qresult, 0, 2));

    snprintf(html_res, MAX_COMMAND_LENGTH,
             "HTTP/1.1 200 OK\r\n"
             "Content-Type: text/html\r\n"
             "Content-Length: %zu\r\n"
             "\r\n"
             "%s",
             strlen(body), body);
    rio_write(STDOUT_FILENO, html_res, strlen(html_res));
    PQclear(qresult);
    goto CLEAN_UP;
  } else {
    PGresult *qresult = PQexec(dbconn, "SELECT count(*) from users;");
    ExecStatusType execstype = PQresultStatus(qresult);
    // TODO: put database error inside a function
    if (execstype != PGRES_COMMAND_OK) {
      // send error to client
      char html_res[MAX_COMMAND_LENGTH];
      char body[BODY_BUFFER_SIZE];
      snprintf(body, BODY_BUFFER_SIZE,
               "<!doctype html>"
               "<html>"
               "<head><title>Database Error</title></head>"
               "<body><span style='color:red;'>Error %d: %s</span></body>"
               "</html>",
               execstype, PQresStatus(execstype));
      snprintf(html_res, MAX_COMMAND_LENGTH,
               "HTTP/1.1 500 Internal Server\r\n"
               "Content-Type: text/html\r\n"
               "Content-Length: %lu\r\n"
               "\r\n"
               "%s",
               strlen(body), body);
      rio_write(STDOUT_FILENO, html_res, strlen(html_res));
      PQclear(qresult);
      goto CLEAN_UP;
    }
    int user_count = atoi(PQgetvalue(qresult, 0, 0));
    PQclear(qresult);

    //?first_name=&last_name=
    char fn_queryValue[64];
    char ln_queryValue[64];

    if (getQueryValue(argv[URI_ARG_INDEX], "first_name", fn_queryValue,
                      sizeof(fn_queryValue))) {
      // we can 1) send a 400 or 2) serve /create_user but with an error message
      // for the sake of simplicity, we will do 1
    };
    if (getQueryValue(argv[URI_ARG_INDEX], "last_name", ln_queryValue,
                      sizeof(ln_queryValue))) {
    };
    // TODO: Check for empty strings or whitespace
    char command[MAX_COMMAND_LENGTH];
    snprintf(command, MAX_COMMAND_LENGTH,
             "INSERT INTO users VALUES ( %d, %s, %s);", user_count + 1,
             fn_queryValue, ln_queryValue);

    qresult = PQexec(dbconn, command);
    execstype = PQresultStatus(qresult);
    // TODO: put database error inside a function
    if (execstype != PGRES_COMMAND_OK) {
      // send error to client
      char *html_res = command;
      char body[BODY_BUFFER_SIZE];
      snprintf(body, BODY_BUFFER_SIZE,
               "<!doctype html>"
               "<html>"
               "<head><title>Database Error</title></head>"
               "<body><span style='color:red;'>Error %d: %s</span></body>"
               "</html>",
               execstype, PQresStatus(execstype));
      snprintf(html_res, MAX_COMMAND_LENGTH,
               "HTTP/1.1 500 Internal Server\r\n"
               "Content-Type: text/html\r\n"
               "Content-Length: %lu\r\n"
               "\r\n"
               "%s",
               strlen(body), body);
      rio_write(STDOUT_FILENO, html_res, strlen(html_res));
      PQclear(qresult);
      goto CLEAN_UP;
    }

    // POST create_user
    client_error(STDOUT_FILENO, 400, "Bad Request");
    PQfinish(dbconn);
    goto CLEAN_UP;
  }
CLEAN_UP:
  close(STDOUT_FILENO);
  PQfinish(dbconn);
  return 0;
}
