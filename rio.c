// Contains robust I/O functions for reading and writing to sockets
// rio_write (no short count) ,-1 on error
// rio_read 0(EOF) or n
// rio_readBuf 0(EOF) or n
// rio_readLine n-1

#include "rio.h"
#include <asm-generic/errno-base.h>
#include <errno.h>
#include <unistd.h>

int rio_write(int fd, void *buf, size_t len) {
  char *cur = (char *)buf;
  int numread = 0;
  int numleft = len;
  while (numleft > 0) {
    numread = write(fd, cur, numleft);
    if (numread < 0) {
      // error
      if (errno == EINTR)
        continue;
      return -1;
    } else {
      // numread > 0
      // writes don't EOF
      numleft -= numread;
      cur += numread;
    }
  }
  return len;
}
