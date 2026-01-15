#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/syscall.h>

#include <ps5/klog.h>

#include "util.h"

int set_proc_name(const char* name) {
  return syscall(SYS_thr_set_name, -1, name);
}

ssize_t safe_write(int fd, const void* buf, size_t len) {
  const char* p = (const char*)buf;
  size_t off = 0;
  while(off < len) {
    ssize_t w = write(fd, p + off, len - off);
    if(w < 0) {
      if(errno == EINTR) continue;
      return -1;
    }
    off += (size_t)w;
  }
  return (ssize_t)off;
}

ssize_t safe_read_line(int fd, char* buf, size_t maxlen) {
  size_t i = 0; char c;
  while(i + 1 < maxlen) {
    ssize_t r = read(fd, &c, 1);
    if(r == 0) break;
    if(r < 0) {
      if(errno == EINTR) continue; else return -1;
    }
    if(c == '\r') continue; // ignore CR
    if(c == '\n') break;
    buf[i++] = c;
  }
  buf[i] = 0;
  return (ssize_t)i;
}

int copy_to_fd(int dst_fd, int src_fd) {
  char b[1024];
  ssize_t r;
  while((r = read(src_fd, b, sizeof(b))) > 0) {
    if(safe_write(dst_fd, b, (size_t)r) < 0) return -1;
  }
  return r < 0 ? -1 : 0;
}
