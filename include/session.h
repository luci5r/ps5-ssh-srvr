#pragma once
#include <stdint.h>
#include <sys/types.h>
void session_handle(int fd, const char* remote_ip, pid_t listener_pid);