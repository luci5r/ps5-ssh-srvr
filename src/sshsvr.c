/* Pseudo-SSH server (unencrypted) draft for PS5 payload environment.
 * This intentionally does NOT implement the SSH protocol. It just offers
 * a slightly structured command channel reminiscent of SSH sessions.
 */
#include <arpa/inet.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdarg.h>   // for fallback dprintf
#include <errno.h>
#include <sys/sysctl.h>
#include <ps5/klog.h>

#if __has_include(<sys/user.h>)
#include <sys/user.h>
#define HAVE_KINFO_PROC 1
#endif

#include <ps5/klog.h>
#include <ps5/kernel.h>
#include <libgen.h>

#include "sshsvr.h"
#include "session.h"
#include "util.h"   // added

#include <sys/types.h>

static volatile int g_running = 1;
static pid_t g_listener_pid = 0;
static const char *PIDFILE = SSHSVR_PIDFILE;  // replace old literal

static void sigterm_handler(int sig) {
  (void)sig;
  g_running = 0;
}

static int write_pidfile(pid_t pid) {
  char path[PATH_MAX];
  strcpy(path, PIDFILE);
  char *d = dirname(path);
  if (d && d[0] && strcmp(d, ".") != 0 && strcmp(d, "/") != 0) {
    mkdir(d, 0755);
  }
  FILE *f = fopen(PIDFILE, "wb");
  if(!f) return -1;
  fprintf(f, "%d\n", pid);
  fclose(f);
  return 0;
}

static void remove_pidfile(void) {
  unlink(PIDFILE);
}

static int read_pidfile(pid_t* pid_out) {
  FILE* f = fopen(PIDFILE, "r");
  if(!f) return -1;
  int pid=0;
  if(fscanf(f, "%d", &pid)!=1) { fclose(f); return -1; }
  fclose(f);
  if(pid <= 1) return -1;
  *pid_out = (pid_t)pid;
  return 0;
}

#ifndef HAVE_KINFO_PROC
/* Fallback stub: process enumeration not available */
static int kill_processes_named(const char* name, pid_t self) {
  (void)name; (void)self;
  return 0;
}
#else
static int kill_processes_named(const char* name, pid_t self) {
  int mib[4] = { CTL_KERN, KERN_PROC, KERN_PROC_PROC, 0 };
  size_t len=0;
  if(sysctl(mib,4,NULL,&len,NULL,0)<0) return 0;
  char* buf = malloc(len);
  if(!buf) return 0;
  if(sysctl(mib,4,buf,&len,NULL,0)<0) { free(buf); return 0; }
  struct kinfo_proc* p = (struct kinfo_proc*)buf;
  int count = (int)(len / sizeof(*p));
  int killed = 0;
  for(int i=0;i<count;i++) {
    if(p[i].ki_pid == self) continue;
    if(strcmp(p[i].ki_comm, name)==0) {
      kill(p[i].ki_pid, SIGTERM);
      killed++;
    }
  }
  free(buf);
  return killed;
}
#endif

static int ensure_single_instance(int force) {
  pid_t oldpid;
  if(read_pidfile(&oldpid) < 0) {
    if(force) {
      int k = kill_processes_named("sshsvr", getpid());
      if(k>0) {
        klog_printf("sshsvr: terminated %d existing instance(s) (no pidfile)\n", k);
        // wait a bit for sockets to release
        usleep(200*1000);
      }
    }
    return 0;
  }

  if(kill(oldpid, 0) != 0) {
    remove_pidfile();
    return 0;
  }

  if(!force) {
    klog_printf("sshsvr already running (pid=%d). Use -F to replace.\n", oldpid);
    return -1;
  }

  klog_printf("sshsvr: terminating existing instance pid=%d\n", oldpid);
  kill(oldpid, SIGTERM);
  for(int i=0;i<200;i++) {
    if(kill(oldpid,0)!=0) { klog_printf("old instance stopped\n"); break; }
    usleep(10*1000);
  }
  if(kill(oldpid,0)==0) {
    klog_printf("warning: old instance still alive\n");
    return -1;
  }
  remove_pidfile();
  // small grace period for port release
  usleep(150*1000);
  return 0;
}

static int create_listener(uint16_t port) {
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  if(fd < 0) {
    klog_perror("socket");
    return -1;
  }
  int yes = 1;
  setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

  struct sockaddr_in addr; memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET; addr.sin_addr.s_addr = htonl(INADDR_ANY); addr.sin_port = htons(port);
  if(bind(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
    klog_perror("bind");
    close(fd); return -1;
  }
  if(listen(fd, 8) < 0) { klog_perror("listen"); close(fd); return -1; }
  return fd;
}

static int bind_with_retry(uint16_t port, int force) {
  int lfd = -1;
  for(int attempt=0; attempt<30; attempt++) {
    lfd = create_listener(port);
    if(lfd >= 0) return lfd;
    if(errno == EADDRINUSE && force) {
      // Try killing straggler processes once more (maybe race)
      kill_processes_named("sshsvr", getpid());
      usleep(100*1000);
      continue;
    }
    break;
  }
  return lfd;
}

void sshsvr_run(uint16_t port, int daemonize, int force_replace) {
  if(ensure_single_instance(force_replace) < 0)
    return;

  syscall(SYS_thr_set_name, -1, "sshsvr");
  g_listener_pid = getpid();

  if(daemonize) {
    pid_t pid = fork();
    if(pid < 0) {
      klog_perror("fork");
      return;
    }
    if(pid > 0) {
      // parent exits, child continues
      return;
    }
    setsid();
    int devnull = open("/dev/null", O_RDWR);
    if(devnull >= 0) {
      dup2(devnull, 0);
      dup2(devnull, 1);
      dup2(devnull, 2);
      if(devnull > 2) close(devnull);
    }
    g_listener_pid = getpid();
    if(write_pidfile(g_listener_pid) < 0) {
      klog_printf("warn: cannot write pid file\n");
    }
  }

  struct sigaction sa;
  memset(&sa, 0, sizeof(sa));
  sa.sa_handler = sigterm_handler;
  sigaction(SIGTERM, &sa, NULL);
  sigaction(SIGINT, &sa, NULL);

  int lfd = bind_with_retry(port, force_replace);
  if(lfd < 0) {
    klog_perror("bind");
    if(daemonize) remove_pidfile();
    return;
  }
  klog_printf("sshsvr listening on port %d (pid=%d)\n", port, g_listener_pid);
  write_pidfile(g_listener_pid);
  klog_printf("pid written");

  while(g_running) {
    struct sockaddr_in caddr; socklen_t clen = sizeof(caddr);
    int cfd = accept(lfd, (struct sockaddr*)&caddr, &clen);
    if(cfd < 0) {
      if(!g_running) break;
      klog_perror("accept");
      continue;
    }
    char ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &caddr.sin_addr, ip, sizeof(ip));
    klog_printf("connection from %s\n", ip);

    pid_t child = fork();
    if(child == 0) {
      close(lfd);
      session_handle(cfd, ip, g_listener_pid);
      close(cfd);
      _exit(0);
    }
    close(cfd);
  }

  close(lfd);
  if(daemonize) remove_pidfile();
  klog_printf("sshsvr shutting down\n");
}

static void usage(const char* prog) {
  dprintf(1,
    "Usage: %s [-p port] [-d] [-F]\n"
    "  -p <port>  listen port (default %d)\n"
    "  -d         daemonize\n"
    "  -F         force replace existing instance\n",
    prog, SSHSVR_DEFAULT_PORT);
}

int main(int argc, char** argv) {
  int port = SSHSVR_DEFAULT_PORT;
  int daemonize = 0;
  int force = 0;
  for(int i=1;i<argc;i++) {
    if(strcmp(argv[i], "-p")==0 && i+1<argc) {
      port = atoi(argv[++i]);
    } else if(strcmp(argv[i], "-d")==0) {
      daemonize = 1;
    } else if(strcmp(argv[i], "-F")==0) {
      force = 1;
    } else if(strcmp(argv[i], "-h")==0 || strcmp(argv[i], "--help")==0) {
      usage(argv[0]);
      return 0;
    }
  }
  if(!force && access(SSHSVR_PIDFILE, R_OK)==0) force=1;

  char pbuf[16];
  snprintf(pbuf,sizeof(pbuf),"%d",port);
  setenv("SSHSVR_PORT", pbuf, 1);

  sshsvr_run(port, daemonize, force);
  return 0;
}