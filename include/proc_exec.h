#pragma once
#include <unistd.h>

pid_t spawn_shell_like(int in_fd, int out_fd, int err_fd, char *const argv[]);
