#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/syscall.h>

#include <ps5/klog.h>

#include "proc_exec.h"

// Placeholder simple process exec that just attempts to run a builtin minimal
// program list (e.g., /usb0/bin/<prog> or /data/bin/<prog>). Real integration
// with elfldr could be added by importing elfldr_spawn/exec from existing shsrv.

static const char* SEARCH_PATHS[] = { "/data/bin", "/usb0/bin", "/user/home", 0 };

static int find_prog(const char* name, char* out, size_t outsz) {
  for(int i=0; SEARCH_PATHS[i]; i++) {
    snprintf(out, outsz, "%s/%s", SEARCH_PATHS[i], name);
    if(!access(out, R_OK | X_OK)) return 0;
  }
  return -1;
}

pid_t spawn_shell_like(int in_fd, int out_fd, int err_fd, char *const argv[]) {
  char path[512];
  if(find_prog(argv[0], path, sizeof(path))) {
    klog_printf("prog not found: %s\n", argv[0]);
    return -1;
  }
  pid_t pid = fork();
  if(pid < 0) {
    klog_perror("fork");
    return -1;
  }
  if(pid == 0) {
    syscall(SYS_thr_set_name, -1, argv[0]);
    dup2(in_fd, STDIN_FILENO);
    dup2(out_fd, STDOUT_FILENO);
    dup2(err_fd, STDERR_FILENO);
    char *envp[] = { "TERM=dumb", 0 };
    execve(path, argv, envp);
    klog_perror("execve");
    _exit(127);
  }
  return pid;
}
