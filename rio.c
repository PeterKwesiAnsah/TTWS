// Contains robust I/O functions for reading and writing to sockets
// rio_read 0(EOF) or n
// rio_readBuf 0(EOF) or n
// rio_readLine n-1
#include "rio.h"
#include <asm-generic/errno-base.h>
#include <assert.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

ssize_t rio_write(int fd, void *buf, size_t len) {
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
      // If count is zero   and fd refers to a file other than a regular file,
      // the results are not specified.
      numleft -= numread;
      cur += numread;
    }
  }
  return len;
}

ssize_t rio_read(int fd, void *buf, size_t len) {
  char *cur = (char *)buf;
  int tnumread = 0;
  int numread = 0;
  int numleft = len;
  while (numleft > 0) {
    numread = read(fd, cur, numleft);
    if (numread < 0) {
      // error
      if (errno == EINTR)
        continue;
      return -1;
    } else {
      // numread >= 0
      if (numread == 0 && numleft == len)
        return 0;
      else if (numread == 0)
        return len - numleft;
      numleft -= numread;
      cur += numread;
      tnumread += numread;
    }
  }
  assert(tnumread - len == 0);
  return len;
}

ssize_t rio_readb(rio *rp, void *buf, size_t len) {
  char *cur = (char *)buf;
  int numread = 0;
  ssize_t rio_bytes_read;
  if (!len || !buf)
    return 0;

READ_FROM_BUFFER:
  if (rp->bytesleft == 0)
    goto FILL_IN_BUFFER;
  numread = len > rp->bytesfill ? rp->bytesfill : len;
  memcpy(cur, rp->buf, numread);
  rp->buf = rp->buf + numread;
  rp->bytesleft = rp->bytesleft - numread;
  // short counts include 0, and rp->bytesfill
  return numread;

FILL_IN_BUFFER:
  rp->bytesfill = read(rp->fd, rp->rio_buf, RIO_BUFFER_SIZE);
  rio_bytes_read = rp->bytesfill;
  if (rio_bytes_read  < 0) {
    // error
    if (errno == EINTR)
      goto FILL_IN_BUFFER;
    return -1;
  } else {
    // rio_bytes_read  >= 0
    if (rio_bytes_read  == 0)
      return 0;
    rp->buf=rp->rio_buf;
    goto READ_FROM_BUFFER;
  }
}
