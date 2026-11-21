# TTWS — The Tiny Web Server

TTWS (The Tiny Web Server) is a minimal, iterative **HTTP/1.1** web server written in C that serves both static and dynamic content. It uses [`libpq`](https://www.postgresql.org/docs/current/libpq.html) to connect to a PostgreSQL-compatible database server and exposes simple CRUD-style user management URIs.

The server follows a CGI-like model: the main HTTP server (`web.server`) parses incoming requests and, for dynamic routes, forks a child process that execs a separate `db.client` program to handle database-backed requests. The child writes the HTTP response directly to the client socket (via `dup2` of `STDOUT_FILENO`).

---

## Features

- **HTTP/1.1** iterative server using BSD sockets (`getaddrinfo`, `socket`, `bind`, `listen`, `accept`)
- **Per-connection concurrency** via `fork()` — one child process handles each client connection
- **Static content**: serves `create_user.html` using `mmap()` for zero-copy file transfer
- **Dynamic content**: invokes `db.client` via `execve()` for database-backed routes, passing URI, method, and HTTP version as arguments plus PostgreSQL connection environment variables
- **Robust I/O**: custom buffered RIO package (`rio.c` / `rio.h`) for reliable socket reads/writes
- **Query string parsing**: `getQueryValue()` extracts `first_name` and `last_name` parameters from URIs
- **CRUD user management** backed by PostgreSQL (`libpq`)
- **HTML form** for creating users (`create_user.html`)

---

## Project Structure

```
TTWS/
├── README.md            # This file
├── web.server.c         # Main HTTP/1.1 server (listens, parses requests, forks children)
├── db.client.c          # CGI-style DB client (handles dynamic routes via libpq)
├── db.client            # Pre-compiled db.client binary (x86-64 ELF)
├── common.h             # Header for common utilities (getQueryValue)
├── common.c             # Query string parameter parsing
├── rio.h                # Header for Robust I/O (RIO) functions
├── rio.c                # Buffered, robust socket read/write implementation
└── create_user.html     # HTML form served for the /create_user endpoint
```

---

## Prerequisites

### System libraries

| Dependency | Purpose | Install (Debian/Ubuntu) |
|---|---|---|
| `libpq` / `libpq-dev` | PostgreSQL C client library + headers | `sudo apt-get install libpq-dev` |
| `gcc` | C compiler | `sudo apt-get install build-essential` |
| `make` | Build automation | `sudo apt-get install make` |
| PostgreSQL server | Database backend | `sudo apt-get install postgresql` |

The `db.client` binary is pre-compiled, but to rebuild it from source you need `libpq-dev` (provides `libpq-fe.h`).

---

## Database Setup

Before running the server, create the `users` table in PostgreSQL:

```sql
CREATE TABLE users (
    id SERIAL PRIMARY KEY,
    first_name TEXT NOT NULL,
    last_name  TEXT NOT NULL
);
```

The server expects the table to have columns: `id`, `first_name`, `last_name`.

---

## Build

Compile both the web server and the db client:

```bash
# Build the web server (no external dependencies beyond libc)
gcc -o web.server web.server.c rio.c common.c

# Build the db client (requires libpq)
gcc -o db.client db.client.c rio.c common.c -lpq
```

> **Note:** `libpq-dev` must be installed to compile `db.client.c` (it includes `<postgresql/libpq-fe.h>`). A pre-built `db.client` binary is already included in the repository.

---

## Running

### 1. Start the PostgreSQL server

```bash
sudo service postgresql start
# or: pg_ctlcluster 16 main start
```

Ensure a database and user exist matching the connection environment variables (see below).

### 2. Start the web server

```bash
./web.server <port>
```

Example:

```bash
./web.server 8080
```

The server prints `Socket <fd> listening on port <port>` and then accepts connections in an infinite loop.

### 3. Environment variables for the DB client

The web server passes the following environment variables to `db.client` for the PostgreSQL connection:

| Variable | Default value (hard-coded in web.server.c) |
|---|---|
| `PGHOST` | `localhost` |
| `PGUSER` | `postgres` |
| `PGPASSWORD` | `postgres` |
| `PGPORT` | `5432` |

These are set in `web.server.c` via the `envp` array passed to `execve()`.

---

## API Endpoints

| Method | URI | Description |
|---|---|---|
| `GET` | `/` | Root — currently returns **501 Not Implemented** (handled by `db.client`) |
| `GET` | `/create_user` | Serves the HTML user creation form (`create_user.html`) |
| `POST` | `/create_user` | Serves the HTML form (POST body handling is a TODO) |
| `GET` | `/users/<id>` | Fetches a user by ID from the database; returns HTML with first/last name or **404 Not Found** |
| `GET` | `/users/` (no ID) | Returns **400 Bad Request** |

### Example requests

```bash
# Serve the create user form
curl -i http://localhost:8080/create_user

# Fetch a user by ID
curl -i http://localhost:8080/users/1

# Non-existent user
curl -i http://localhost:8080/users/999

# Missing user ID
curl -i http://localhost:8080/users/
```

---

## How It Works

### `web.server.c` — HTTP Server

1. Parses command-line argument for the port to listen on.
2. Sets up a passive TCP socket using `getaddrinfo` / `socket` / `bind` / `listen` with a backlog of 1024.
3. Enters an `accept()` loop. For each connection:
   - Reads the request line using buffered RIO (`rio_readline`).
   - Parses the HTTP method, URI, and version from the request line.
   - Validates the URI against the supported list: `/`, `/create_user`, `/users/`.
   - For `/create_user`: serves `create_user.html` via `mmap()` with proper HTTP headers.
   - For other dynamic routes: forks a child process that:
     - Closes the listening socket.
     - Sets up environment variables (`PGHOST`, `PGUSER`, `PGPASSWORD`, `PGPORT`).
     - `dup2`s the client connection fd to `STDOUT_FILENO`.
     - `execve`s `./db.client` with arguments `(uri, method, version)`.
   - The parent closes the client connection fd and continues accepting.

### `db.client.c` — Database Client (CGI-style)

1. Receives `(uri, method, version)` as `argv[1..3]`.
2. Connects to PostgreSQL using `PQconnectdbParams()` (reads connection params from environment).
3. Routes based on the URI:
   - **`/`** (single character): returns **501 Not Implemented**.
   - **`/users/<id>`**: parses the numeric ID, runs `SELECT * FROM users WHERE id = <id>`, returns the user's first/last name in HTML or **404 Not Found** if no row matches.
   - **Otherwise** (e.g., `/create_user` with query string): runs `SELECT count(*) FROM users`, parses `first_name`/`last_name` query parameters via `getQueryValue()`, builds an `INSERT` statement, executes it, then returns **400 Bad Request**.
4. Writes the complete HTTP response (status line, headers, HTML body) to `stdout`, which is connected to the client socket by the parent server.
5. Cleans up: closes `STDOUT_FILENO`, calls `PQfinish()`.

### `rio.c` — Robust I/O

Provides buffered, interrupt-safe I/O primitives:
- `rio_write()` — write all bytes, retrying on `EINTR`
- `rio_read()` — read exactly `len` bytes (or return short count on EOF)
- `rio_readb()` — buffered read from an internal 1024-byte buffer
- `rio_readline()` — read a text line (up to `len-1` bytes, terminated by `\n`)

### `common.c` — Query Parsing

- `getQueryValue(url, key, value, n)` — extracts the value of a query parameter from a URL string (e.g., `?first_name=John&last_name=Doe`), copying up to `n-1` bytes into `value`.

---

## Limitations & Known TODOs

The following items are marked as TODO in the source code and represent known limitations:

- **Method checking**: URI method validation is not implemented (the `TODO: Check Allowed Methods on URIs` comment in `web.server.c`).
- **POST handling**: `POST /create_user` currently just serves the HTML form; form submission data is not processed.
- **Single DB connection per request**: Each `db.client` invocation opens a new PostgreSQL connection (`// TODO: Create a single open connection`).
- **HTML templates**: No template system; error/success HTML is embedded inline in `db.client.c` (`// TODO: Find a way to parse and generate HTML Pages from HTML templates`).
- **Premature client closure**: Not handled (`// TODO: handle premature client connection closure`).
- **SQL injection**: User input from query parameters is interpolated directly into SQL strings without parameterized queries.
- **Empty/whitespace validation**: Not checked for `first_name`/`last_name` (`// TODO: Check for empty strings or whitespace`).
- **Root path**: `GET /` returns 501 Not Implemented.

---

## License

This project is provided as-is for educational purposes.
