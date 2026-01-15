#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>
#include <unistd.h>
#include <stdio.h>

int set_proc_name(const char* name);
int copy_to_fd(int dst_fd, int src_fd);
ssize_t safe_write(int fd, const void* buf, size_t len);
ssize_t safe_read_line(int fd, char* buf, size_t maxlen);

static inline int sshsvr_dprintf(int fd, const char* fmt, ...) {
  char buf[1024];
  va_list ap;
  va_start(ap, fmt);
  int n = vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  if(n < 0) return n;
  if(n > (int)sizeof(buf)) n = (int)sizeof(buf);
  if(write(fd, buf, n) < 0) return -1;
  return n;
}

#ifndef dprintf
#define dprintf sshsvr_dprintf
#endif
