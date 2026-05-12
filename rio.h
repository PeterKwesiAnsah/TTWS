#include <stddef.h>
#include <sys/types.h>

#define RIO_BUFFER_SIZE 1024
struct rio {
  int fd;
  //number of bytes filled during our recent trip to the kernel
  size_t bytesfill;
  //number of bytes left to satisfy I/O request, when zero we go to the kernel
  size_t bytesleft;
  const char *buf;
  char rio_buf[RIO_BUFFER_SIZE];
};

typedef struct rio rio;

ssize_t rio_write(int fd, void *buf, size_t len);
ssize_t rio_read(int fd, void *buf, size_t len);
ssize_t rio_readb(rio *, void *buf, size_t len);
