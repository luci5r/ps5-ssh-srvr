#pragma once
#include <stdint.h>

#define SSHSVR_PIDFILE "/data/tmp/sshsvr.pid"
#ifndef SSHSVR_DEFAULT_PORT
#define SSHSVR_DEFAULT_PORT 2222
#endif

void sshsvr_run(uint16_t port, int daemonize, int force_replace);
