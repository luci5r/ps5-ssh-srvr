#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <dirent.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <sys/wait.h>
#include <limits.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/user.h>
#include <sys/socket.h>     // added
#include <sys/user.h>       // for kinfo_proc
#include <netinet/in.h>     // added
#include <arpa/inet.h>      // added
#include <sys/select.h>  // for select()

#include "builtins.h"
#include "base64.h"
#include "../shsrv/elfldr.h"
#include "../shsrv/pt.h"
#include "util.h"   // added for dprintf
#include "sshsvr.h"   // add for sshsvr_run prototype / PIDFILE
#include <ps5/klog.h>  // for klog_printf

/* Prototype for cmd_install so the table can reference it */
static int cmd_install(int argc, char **argv);

static void print_error(const char* msg) { dprintf(1, "error: %s: %s\n", msg, strerror(errno)); }

static void fmt_mode(mode_t m, char* out) {
  out[0] = S_ISDIR(m)?'d':(S_ISLNK(m)?'l':'-');
  const char* rwx="rwx";
  for(int i=0;i<9;i++) out[i+1] = (m & (1 << (8-i))) ? rwx[i%3] : '-';
  out[10]=0;
}

static int do_ls_entry(const char* path, const char* name, int longf) {
  if(!longf) {
    dprintf(1, "%s\n", name);
    return 0;
  }
  char full[1024];
  snprintf(full, sizeof(full), "%s/%s", path, name);
  struct stat st;
  if(lstat(full, &st) < 0) return -1;
  char mode[11]; fmt_mode(st.st_mode, mode);
  struct tm tm; localtime_r(&st.st_mtime, &tm);
  char tbuf[32];
  strftime(tbuf, sizeof(tbuf), "%Y-%m-%d %H:%M", &tm);
  dprintf(1, "%s %6lu %6d %6d %8lld %s %s\n",
          mode, (unsigned long)st.st_nlink, (int)st.st_uid, (int)st.st_gid,
          (long long)st.st_size, tbuf, name);
  return 0;
}

static int ls_recursive(const char* path, int longf, int showall) {
  DIR* d = opendir(path);
  if(!d) { print_error(path); return -1; }
  dprintf(1, "%s:\n", path);
  struct dirent* de;
  while((de=readdir(d))) {
    if(!showall && de->d_name[0]=='.') continue;
    do_ls_entry(path, de->d_name, longf);
  }
  closedir(d);
  dprintf(1, "\n");
  return 0;
}

static int cmd_ls(int argc, char** argv) {
  int longf=0, all=0, rec=0;
  int start=1;
  for(int i=1;i<argc;i++) {
    if(argv[i][0]=='-') {
      if(strchr(argv[i],'l')) longf=1;
      if(strchr(argv[i],'a')) all=1;
      if(strchr(argv[i],'R')) rec=1;
      start++;
    } else break;
  }
  if(start==argc) {
    if(rec) return ls_recursive(".", longf, all);
    DIR* d=opendir(".");
    if(!d) { print_error("."); return -1; }
    struct dirent* de;
    while((de=readdir(d))) {
      if(!all && de->d_name[0]=='.') continue;
      do_ls_entry(".", de->d_name, longf);
    }
    closedir(d);
    return 0;
  }
  for(int i=start;i<argc;i++) {
    if(rec) ls_recursive(argv[i], longf, all);
    else {
      struct stat st;
      if(lstat(argv[i], &st)<0) { print_error(argv[i]); continue; }
      if(S_ISDIR(st.st_mode)) {
        DIR* d = opendir(argv[i]);
        if(!d) { print_error(argv[i]); continue; }
        struct dirent* de;
        while((de=readdir(d))) {
          if(!all && de->d_name[0]=='.') continue;
            do_ls_entry(argv[i], de->d_name, longf);
        }
        closedir(d);
      } else {
        do_ls_entry(".", argv[i], longf);
      }
    }
  }
  return 0;
}

static int recurse_rm(const char* path) {
  struct stat st;
  if(lstat(path,&st)<0) return -1;
  if(S_ISDIR(st.st_mode)) {
    DIR* d=opendir(path);
    if(!d) return -1;
    struct dirent* de;
    while((de=readdir(d))) {
      if(!strcmp(de->d_name,".")||!strcmp(de->d_name,"..")) continue;
      char buf[1024];
      snprintf(buf,sizeof(buf),"%s/%s",path,de->d_name);
      if(recurse_rm(buf)<0) { closedir(d); return -1; }
    }
    closedir(d);
    if(rmdir(path)<0) return -1;
  } else {
    if(unlink(path)<0) return -1;
  }
  return 0;
}

static int cmd_rm(int argc, char** argv) {
  int rec=0, start=1;
  if(argc<2) { dprintf(1,"usage: rm [-r] path...\n"); return -1; }
  if(strcmp(argv[1],"-r")==0) { rec=1; start=2; }
  for(int i=start;i<argc;i++) {
    if(rec) {
      if(recurse_rm(argv[i])<0) print_error(argv[i]);
    } else if(unlink(argv[i])<0) print_error(argv[i]);
  }
  return 0;
}

static int ensure_dir(const char* path) {
  struct stat st;
  if(!stat(path,&st)) {
    if(S_ISDIR(st.st_mode)) return 0;
    errno=ENOTDIR; return -1;
  }
  if(mkdir(path, 0755)<0) return -1;
  return 0;
}

static int cmd_mkdir(int argc, char** argv) {
  if(argc<2) { dprintf(1,"usage: mkdir [-p] dir...\n"); return -1; }
  int pflag=0; int idx=1;
  if(strcmp(argv[1],"-p")==0) { pflag=1; idx=2; }
  for(int i=idx;i<argc;i++) {
    if(!pflag) {
      if(mkdir(argv[i],0755)<0) print_error(argv[i]);
    } else {
      char tmp[1024]; strncpy(tmp, argv[i], sizeof(tmp)); tmp[sizeof(tmp)-1]=0;
      char *s=tmp;
      if(*s=='/') s++;
      for(char* c=s; *c; c++) {
        if(*c=='/') {
          *c=0;
          if(*tmp) ensure_dir(tmp);
          *c='/';
        }
      }
      if(ensure_dir(tmp)<0 && errno!=EEXIST) print_error(tmp);
    }
  }
  return 0;
}

static int copy_file(const char* src, const char* dst) {
  int in=open(src,O_RDONLY);
  if(in<0) return -1;
  int out=open(dst,O_WRONLY|O_CREAT|O_TRUNC,0644);
  if(out<0) { close(in); return -1; }
  char buf[8192];
  ssize_t r;
  while((r=read(in,buf,sizeof(buf)))>0) {
    if(write(out,buf,r)!=r) { close(in); close(out); return -1; }
  }
  close(in); close(out);
  return r<0?-1:0;
}

static int recurse_cp(const char* src, const char* dst) {
  struct stat st;
  if(lstat(src,&st)<0) return -1;
  if(S_ISDIR(st.st_mode)) {
    mkdir(dst,0755);
    DIR* d=opendir(src);
    if(!d) return -1;
    struct dirent* de;
    while((de=readdir(d))) {
      if(!strcmp(de->d_name,".")||!strcmp(de->d_name,"..")) continue;
      char s[1024], t[1024];
      snprintf(s,sizeof(s),"%s/%s",src,de->d_name);
      snprintf(t,sizeof(t),"%s/%s",dst,de->d_name);
      if(recurse_cp(s,t)<0) { closedir(d); return -1; }
    }
    closedir(d);
    return 0;
  }
  return copy_file(src,dst);
}

static int cmd_cp(int argc, char** argv) {
  if(argc<3) { dprintf(1,"usage: cp [-r] src dst\n"); return -1; }
  int rec=0; int idx=1;
  if(strcmp(argv[1],"-r")==0) { rec=1; idx=2; }
  if(argc - idx != 2) { dprintf(1,"usage: cp [-r] src dst\n"); return -1; }
  const char* src=argv[idx];
  const char* dst=argv[idx+1];
  if(rec) {
    if(recurse_cp(src,dst)<0) print_error(src);
  } else {
    if(copy_file(src,dst)<0) print_error(src);
  }
  return 0;
}

static int cmd_mv(int argc, char** argv) {
  if(argc!=3) { dprintf(1,"usage: mv src dst\n"); return -1; }
  if(rename(argv[1],argv[2])<0) print_error(argv[1]);
  return 0;
}

static int cmd_pwd(int argc, char** argv) {
  (void)argc;(void)argv;
  char buf[PATH_MAX];
  if(getcwd(buf,sizeof(buf))) dprintf(1,"%s\n",buf);
  else print_error("pwd");
  return 0;
}

static int cmd_cd(int argc, char** argv) {
  if(argc<2) return chdir("/") == 0 ? 0 : -1;
  if(chdir(argv[1])<0) print_error(argv[1]);
  return 0;
}

static int cmd_cat(int argc, char** argv) {
  if(argc<2) { dprintf(1,"usage: cat file...\n"); return -1; }
  for(int i=1;i<argc;i++) {
    int fd=open(argv[i],O_RDONLY);
    if(fd<0) { print_error(argv[i]); continue; }
    char buf[8192]; ssize_t r;
    while((r=read(fd,buf,sizeof(buf)))>0) write(1,buf,r);
    close(fd);
  }
  return 0;
}

static int cmd_ps(int argc, char** argv) {
  (void)argc;(void)argv;
  int mib[4] = { CTL_KERN, KERN_PROC, KERN_PROC_PROC, 0 };
  size_t len=0;
  if(sysctl(mib,4,NULL,&len,NULL,0)<0) { print_error("sysctl size"); return -1; }
  char* buf = malloc(len);
  if(!buf) return -1;
  if(sysctl(mib,4,buf,&len,NULL,0)<0) { print_error("sysctl data"); free(buf); return -1; }
  struct kinfo_proc* p = (struct kinfo_proc*)buf;
  int count = len / sizeof(struct kinfo_proc);
  dprintf(1,"  PID  PPID  PGID   SID   UID State    COMMAND\n");
  for(int i=0;i<count;i++) {
    if(!p[i].ki_comm[0]) continue; // skip processes without command name
    const char* state_str;
    switch(p[i].ki_stat) {
      case 1: state_str = "IDL"; break;
      case 2: state_str = "RUN"; break;
      case 3: state_str = "SLEEP"; break;
      case 4: state_str = "STOP"; break;
      case 5: state_str = "ZOMB"; break;
      default: state_str = "?"; break;
    }
    dprintf(1,"%5d %5d %5d %5d %5d %6s %s\n",
            p[i].ki_pid, p[i].ki_ppid, p[i].ki_pgid, p[i].ki_sid, p[i].ki_uid,
            state_str, p[i].ki_comm);
  }
  free(buf);
  return 0;
}

static int cmd_put(int argc, char** argv) {
  if(argc!=2) { dprintf(1,"usage: put <dest>\n"); return -1; }
  int fd = open(argv[1], O_WRONLY|O_CREAT|O_TRUNC, 0644);
  if(fd<0) { print_error(argv[1]); return -1; }
  char line[4096];
  unsigned char bin[3072];
  dprintf(1,".\n");
  while(1) {
    ssize_t n = read(0,line,sizeof(line)-1);
    if(n<=0) break;
    line[n]=0;
    char* nl=strchr(line,'\n'); if(nl) *nl=0;
    if(strcmp(line,".")==0) break;
    int linelen=strlen(line);
    int dec = b64_decode_block(line, linelen, bin);
    if(dec<0) { dprintf(1,"decode error\n"); break; }
    if(write(fd,bin,dec)!=dec) { dprintf(1,"write error\n"); break; }
  }
  close(fd);
  return 0;
}

static int cmd_get(int argc, char** argv) {
  if(argc!=2) { dprintf(1,"usage: get <src>\n"); return -1; }
  int fd=open(argv[1], O_RDONLY);
  if(fd<0) { print_error(argv[1]); return -1; }
  unsigned char buf[3072];
  char out[4096];
  ssize_t r;
  while((r=read(fd,buf,sizeof(buf)))>0) {
    int enc = b64_encode_block(buf, r, out);
    write(1,out,enc);
    write(1,"\n",1);
  }
  write(1,".\n",2);
  close(fd);
  return 0;
}

static int cmd_klogtail(int argc, char** argv) {
  (void)argc;(void)argv;
  dprintf(1,"klogtail (stub) Ctrl-C to exit\n");
  for(;;) sleep(1);
  return 0;
}

static size_t parse_size(const char* s) {
  char* end;
  unsigned long long v = strtoull(s,&end,10);
  if(*end=='K'||*end=='k') v*=1024ULL;
  else if(*end=='M'||*end=='m') v*=1024ULL*1024ULL;
  return (size_t)v;
}

static int load_file(const char* path, unsigned char** out, size_t* outlen) {
  int fd=open(path,O_RDONLY);
  if(fd<0) return -1;
  off_t sz=lseek(fd,0,SEEK_END);
  lseek(fd,0,SEEK_SET);
  unsigned char* buf=malloc(sz);
  if(!buf) { close(fd); return -1; }
  if(read(fd,buf,sz)!=sz) { free(buf); close(fd); return -1; }
  close(fd);
  *out=buf; *outlen=sz;
  return 0;
}

static int do_exec_common(int argc, char** argv, int debug_mode) {
  if(argc<2) {
    dprintf(1,"usage: %s [--heap SIZE] [--env KEY=VAL]... <elf> [args...]\n",
            debug_mode?"debugelf":"execelf");
    return -1;
  }
  size_t heap=0;
  char* envs[64]; int envc=0;
  int idx=1;
  while(idx<argc) {
    if(strcmp(argv[idx],"--heap")==0 && idx+1<argc) {
      heap = parse_size(argv[idx+1]);
      idx+=2; continue;
    }
    if(strcmp(argv[idx],"--env")==0 && idx+1<argc) {
      if(envc < (int)(sizeof(envs)/sizeof(envs[0])-1)) envs[envc++]=argv[idx+1];
      idx+=2; continue;
    }
    break;
  }
  if(idx>=argc) { dprintf(1,"missing elf path\n"); return -1; }
  const char* elfpath = argv[idx++];
  unsigned char* data; size_t len;
  if(load_file(elfpath,&data,&len)<0) { print_error(elfpath); return -1; }

  char* prog_argv[64];
  int carg=0;
  prog_argv[carg++]=(char*)elfpath;
  while(idx<argc && carg < 63) prog_argv[carg++]=argv[idx++];
  prog_argv[carg]=NULL;

  (void)heap;
  for(int i=0;i<envc;i++) putenv(envs[i]);

  pid_t pid = elfldr_spawn(0,1,2,data,(char**)prog_argv);
  if(pid<0) { dprintf(1,"elf spawn failed\n"); free(data); return -1; }

  if(debug_mode) {
    dprintf(1,"(debug stub) pid=%d\n", pid);
  } else {
    int status;
    waitpid(pid,&status,0);
    if(WIFEXITED(status))
      dprintf(1,"exit %d\n", WEXITSTATUS(status));
  }
  free(data);
  return 0;
}

static int cmd_execelf(int argc, char** argv) { return do_exec_common(argc, argv, 0); }
static int cmd_debugelf(int argc, char** argv) { return do_exec_common(argc, argv, 1); }

/* ---- Added implementations for exit/kill/serverctl + help + table ---- */

/* cmd_exit: end session loop (pipeline single builtin case returns -2 mapped from 255) */
static int cmd_exit(int argc, char** argv) {
  (void)argc; (void)argv;
  return 255;
}

/* kill <pid> [sig] */
static int cmd_kill(int argc, char** argv) {
  if(argc < 2) {
    dprintf(1,"usage: kill <pid> [sig]\n");
    return -1;
  }
  int pid = atoi(argv[1]);
  int sig = (argc >=3) ? atoi(argv[2]) : SIGTERM;
  if(kill(pid, sig) < 0) {
    dprintf(1,"kill: %s\n", strerror(errno));
    return -1;
  }
  return 0;
}

/* ---- serverctl helpers ---- */
static int read_pidfile_builtin(pid_t* pid_out) {
  FILE* f = fopen(SSHSVR_PIDFILE, "r");
  if(!f) return -1;
  int pid=0;
  if(fscanf(f, "%d", &pid)!=1) { fclose(f); return -1; }
  fclose(f);
  if(pid <= 1) return -1;
  *pid_out = (pid_t)pid;
  return 0;
}

static int wait_for_exit(pid_t pid, int ms_total) {
  int loops = ms_total / 50;
  for(int i=0;i<loops;i++) {
    if(kill(pid,0)!=0) return 0;
    usleep(50*1000);
  }
  return (kill(pid,0)==0) ? -1 : 0;
}

static int serverctl_stop(void) {
  pid_t pid;
  if(read_pidfile_builtin(&pid) < 0) {
    dprintf(1,"serverctl: not running\n");
    return 1;
  }
  if(kill(pid, SIGTERM)<0) {
    dprintf(1,"serverctl: kill failed: %s\n", strerror(errno));
    return -1;
  }
  if(wait_for_exit(pid, 2000) < 0) {
    dprintf(1,"serverctl: timeout waiting for pid %d\n", pid);
    return -1;
  }
  unlink(SSHSVR_PIDFILE);
  dprintf(1,"serverctl: stopped (pid=%d)\n", pid);
  return 0;
}

static int serverctl_start(int argc, char** argv, int offset, int do_force) {
  int port = SSHSVR_DEFAULT_PORT;
  const char* envp = getenv("SSHSVR_PORT");
  if(envp) port = atoi(envp);
  for(int i=offset;i<argc;i++) {
    if(strcmp(argv[i],"-p")==0 && i+1<argc) {
      port = atoi(argv[++i]);
    }
  }
  pid_t pid;
  if(read_pidfile_builtin(&pid)==0 && kill(pid,0)==0 && !do_force) {
    dprintf(1,"serverctl: already running (pid=%d). Use restart or start --force.\n", pid);
    return 1;
  }
  pid_t child = fork();
  if(child < 0) {
    dprintf(1,"serverctl: fork failed\n");
    return -1;
  }
  if(child == 0) {
    sshsvr_run((uint16_t)port, 1, do_force ? 1 : 0);
    _exit(0);
  }
  dprintf(1,"serverctl: launching (port=%d)\n", port);
  usleep(200*1000);
  if(read_pidfile_builtin(&pid)==0 && kill(pid,0)==0) {
    dprintf(1,"serverctl: running pid=%d\n", pid);
  } else {
    dprintf(1,"serverctl: start verification failed\n");
  }
  return 0;
}

static int serverctl_status(void) {
  pid_t pid;
  if(read_pidfile_builtin(&pid)<0) {
    dprintf(1,"serverctl: not running\n");
    return 1;
  }
  if(kill(pid,0)==0) {
    dprintf(1,"serverctl: running (pid=%d)\n", pid);
    return 0;
  }
  dprintf(1,"serverctl: stale pidfile (pid=%d)\n", pid);
  return 2;
}

static int cmd_serverctl(int argc, char** argv) {
  if(argc < 2) {
    dprintf(1,"usage: serverctl <start|stop|restart|status> [options]\n"
              " start   [-p port] [--force]\n"
              " stop\n"
              " restart [-p port] [--force]\n"
              " status\n");
    return -1;
  }
  int do_force = 0;
  for(int i=2;i<argc;i++) {
    if(strcmp(argv[i],"--force")==0) do_force=1;
  }
  if(strcmp(argv[1],"stop")==0) {
    return serverctl_stop();
  } else if(strcmp(argv[1],"start")==0) {
    return serverctl_start(argc, argv, 2, do_force);
  } else if(strcmp(argv[1],"restart")==0) {
    serverctl_stop();
    return serverctl_start(argc, argv, 2, do_force);
  } else if(strcmp(argv[1],"status")==0) {
    return serverctl_status();
  }
  dprintf(1,"serverctl: unknown subcommand '%s'\n", argv[1]);
  return -1;
}

/* cmd_help placed before table usage now */
static int cmd_help(int argc, char** argv) {
  (void)argc;(void)argv;
  size_t n;
  const builtin_t* tbl = builtin_table(&n); // will use table definition below (needs forward)
  for(size_t i=0;i<n;i++)
    dprintf(1,"%-10s - %s\n", tbl[i].name, tbl[i].help);
  return 0;
}

/* --- install helpers and cmd_install (move this block above the table) --- */
// Minimal URL-encode for spaces only (good enough for local paths)
static void enc_spaces(const char* in, char* out, size_t outsz) {
  size_t oi = 0;
  for(size_t i=0; in[i] && oi+3 < outsz; i++) {
    if(in[i] == ' ') {
      out[oi++] = '%'; out[oi++] = '2'; out[oi++] = '0';
    } else {
      out[oi++] = in[i];
    }
  }
  if(oi < outsz) out[oi] = 0; else out[outsz-1] = 0;
}

static int dpi_send_json_9090(const char* url_or_path) {
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  if(fd < 0) return -1;

  struct sockaddr_in addr; memset(&addr,0,sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons(9090);
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

  if(connect(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
    close(fd);
    return -1;
  }

  char json[PATH_MAX + 64];
  // DirectPKGInstaller accepts plain file path or http(s) URL in "url"
  snprintf(json, sizeof(json), "{\"url\":\"%s\"}", url_or_path);

  if(write(fd, json, strlen(json)) < 0) { close(fd); return -1; }

  char resp[256] = {0};
  // Use select for timeout
  fd_set rf; FD_ZERO(&rf); FD_SET(fd, &rf);
  struct timeval tv = { .tv_sec = 5, .tv_usec = 0 };
  int rc = select(fd+1, &rf, NULL, NULL, &tv);
  if(rc <= 0) { close(fd); return -1; }
  ssize_t r = read(fd, resp, sizeof(resp)-1);
  close(fd);
  if(r <= 0) return -1;

  // Expect {"res":"0"} on success
  int res = -1;
  const char* key = "\"res\":\"";
  char* p = strstr(resp, key);
  if(p) res = atoi(p + (int)strlen(key));

  if(res == 0) return 0;
  dprintf(1, "DPI v1 response: %s\n", resp);
  return -1;
}

static int dpi_v2_post_url_12800(const char* url_or_path) {
  // Build simple urlencoded body: url=...
  char enc[PATH_MAX*3];
  enc_spaces(url_or_path, enc, sizeof(enc));

  char body[PATH_MAX*3 + 8];
  snprintf(body, sizeof(body), "url=%s", enc);
  size_t blen = strlen(body);

  int fd = socket(AF_INET, SOCK_STREAM, 0);
  if(fd < 0) return -1;
  struct sockaddr_in a; memset(&a,0,sizeof(a));
  a.sin_family = AF_INET; a.sin_port = htons(12800);
  a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  if(connect(fd, (struct sockaddr*)&a, sizeof(a)) < 0) {
    close(fd);
    return -1;
  }

  char req[4096];
  int n = snprintf(req, sizeof(req),
                   "POST /upload HTTP/1.1\r\n"
                   "Host: 127.0.0.1\r\n"
                   "Content-Type: application/x-www-form-urlencoded\r\n"
                   "Content-Length: %zu\r\n"
                   "Connection: close\r\n\r\n", blen);
  if(n <= 0 || (size_t)n >= sizeof(req)) { close(fd); return -1; }

  if(write(fd, req, n) < 0) { close(fd); return -1; }
  if(write(fd, body, blen) < 0) { close(fd); return -1; }

  char resp[1024] = {0};
  // Use select for timeout
  fd_set rf; FD_ZERO(&rf); FD_SET(fd, &rf);
  struct timeval tv = { .tv_sec = 5, .tv_usec = 0 };
  int rc = select(fd+1, &rf, NULL, NULL, &tv);
  if(rc <= 0) { close(fd); return -1; }
  ssize_t r = read(fd, resp, sizeof(resp)-1);
  close(fd);
  if(r <= 0) return -1;

  // Look for SUCCESS in body
  if(strstr(resp, "SUCCESS")) return 0;
  dprintf(1, "DPI v2 response:\n%s\n", resp);
  return -1;
}

// GROK Code
static int klog_wait_install(int timeout_sec) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;

    struct sockaddr_in a;
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_port = htons(9081);
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    if (connect(fd, (struct sockaddr*)&a, sizeof(a)) < 0) {
        close(fd);
        dprintf(1, "[install] KLOG monitoring not available, install may still succeed\n");
        return 0;
    }

    dprintf(1, "[install] watching KLOG for completion (up to %d s)...\n", timeout_sec);

    char buffer[4096];          // larger buffer
    char line[1024] = {0};      // for line accumulation
    int line_len = 0;
    int elapsed = 0;
    int result = -1;
    int final_state = -1;
    unsigned final_err = 0xFFFFFFFF;

    while (1) {
        fd_set rf;
        FD_ZERO(&rf);
        FD_SET(fd, &rf);

        struct timeval tv = { .tv_sec = 1, .tv_usec = 0 };
        int rc = select(fd + 1, &rf, NULL, NULL, &tv);

        if (rc < 0) {
            close(fd);
            return -1;
        }

        if (rc == 0) {  // timeout tick
            if (++elapsed >= timeout_sec) {
                dprintf(1, "[install] timeout waiting for completion\n");
                break;
            }
            continue;
        }

        ssize_t r = read(fd, buffer, sizeof(buffer) - 1);
        if (r <= 0) {
            // connection closed or error
            break;
        }
        buffer[r] = '\0';

        char *p = buffer;
        while (p < buffer + r) {
            char *nl = strchr(p, '\n');
            size_t chunk_len;

            if (nl) {
                chunk_len = nl - p;
            } else {
                chunk_len = (buffer + r) - p;
            }

            // Append to current line (protect against overflow)
            size_t space_left = sizeof(line) - 1 - line_len;
            size_t to_copy = chunk_len < space_left ? chunk_len : space_left;
            memcpy(line + line_len, p, to_copy);
            line_len += to_copy;
            line[line_len] = '\0';

            p += chunk_len;

            if (nl) {
                // We have a complete line → process it
                char *s = line;

                // Trim leading/trailing whitespace if you want (optional)
                while (*s == ' ' || *s == '\t') s++;

                // ────────────────────────────────────────────────
                // Your desired outputs (same as before, but on full lines)
                // ────────────────────────────────────────────────

                if (strstr(s, "Staring Pre-allocation transfer")) {
                    dprintf(1, "[install] pre-allocation transfer started.\n");
                }

                if (strstr(s, "[PlayGoCore][RequestInstall] begin")) {
                    dprintf(1, "[install] Installation requested\n");
                }

                if (strstr(s, "application data size (")) {
                    int bytes = 0;
                    if (sscanf(strstr(s, "application data size (") + 23, "%d", &bytes) == 1) {
                        double mb = bytes / 1048576.0;
                        dprintf(1, "[install] Game Size: %.2f MB\n", mb);
                    }
                }

                if (strstr(s, "transfer started")) {
                    dprintf(1, "[install] transfer started\n");
                }

                // Detect completion from progress line like "started (1572864/1572864)"
                char *prog = strstr(s, "started (");
                if (prog) {
                    int cur = 0, tot = 0;
                    if (sscanf(prog + 9, "%d/%d", &cur, &tot) == 2 && cur == tot && tot > 0) {
                        dprintf(1, "[install] Transfer completed\n");
                    }
                }

                if (strstr(s, "Whole Process    : ")) {
                    char *time_str = strstr(s, "Whole Process    : ") + 19;
                    // remove trailing newline/spaces if needed
                    char *end = time_str + strlen(time_str) - 1;
                    while (end >= time_str && (*end == '\n' || *end == ' ')) *end-- = '\0';
                    dprintf(1, "[install] Completed in: %s\n", time_str);
                }

                // ────────────────────────────────────────────────
                // More robust success detection
                // Look for patterns that indicate final status
                // ────────────────────────────────────────────────
                if (strstr(s, "request ended") &&
                    strstr(s, "state =") &&
                    strstr(s, "error = 0x")) {

                    char *st_pos = strstr(s, "state = ");
                    char *err_pos = strstr(s, "error = 0x");

                    if (st_pos && err_pos) {
                        final_state = atoi(st_pos + 8);
                        sscanf(err_pos + 9, "%x", &final_err);

                        if (final_state == 7 && final_err == 0) {
                            result = 0;
                        } else {
                            result = -1;
                        }
                        // We found the ending line → can exit early
                        goto finished;
                    }
                }

                // Also accept the playgo.progress line as fallback
                if (strstr(s, "playgo.progress.state=")) {
                    int st = -1;
                    unsigned er = 0xFFFFFFFF;
                    if (sscanf(strstr(s, "state=") + 6, "%d", &st) == 1 &&
                        strstr(s, "error_code=0x")) {
                        sscanf(strstr(s, "error_code=0x") + 12, "%x", &er);
                        if (st == 7 && er == 0) result = 0;
                        else result = -1;
                        goto finished;
                    }
                }

                // Reset for next line
                line_len = 0;
                line[0] = '\0';
                p++;  // skip the \n
            }
        }
    }

finished:
    close(fd);

    if (result == 0) {
        dprintf(1, "[install] Installation completed successfully\n");
        klog_printf("[install] Installation completed successfully\n");
    } else if (result == -1) {
        dprintf(1, "[install] Installation failed (state=%d, error=0x%x)\n",
                final_state, final_err);
        klog_printf("[install] Installation failed (state=%d, error=0x%x)\n",
                final_state, final_err);
    } else {
        dprintf(1, "[install] Did not detect completion status — check manually\n");
    }

    return result;
}

static int cmd_install(int argc, char **argv) {
    int wait_flag = 0;
    int i = 1;
    for (; i < argc; i++) {
        if (strcmp(argv[i], "-w") == 0) {
            wait_flag = 1;
            continue;
        }
        break;
    }
    if (i >= argc) {
        dprintf(1, "usage: install [-w] <pkgfile or http(s) URL>\n");
        return -1;
    }

    char target[PATH_MAX];
    const char *arg = argv[i];
    int is_url = (!strncmp(arg, "http://", 7) || !strncmp(arg, "https://", 8));
    if (is_url || arg[0] == '/') {
        strncpy(target, arg, sizeof(target) - 1);
        target[sizeof(target) - 1] = '\0';
    } else {
        snprintf(target, sizeof(target), "/mnt/usb0/%s", arg);
    }

    if (!is_url) {
        struct stat st;
        if (stat(target, &st) < 0) {
            print_error(target);
            return -1;
        }
    }

    dprintf(1, "Installing: %s\n", target);
    dprintf(1, "Starting installation sequence for %s\n", target);

    if (dpi_send_json_9090(target) == 0 || dpi_v2_post_url_12800(target) == 0) {
        dprintf(1, "Install started. See on-screen notifications.\n");
        if (wait_flag) {
            int rc = klog_wait_install(600);  // up to 10 min
            return rc;
        }
        return 0;
    }

    dprintf(1, "DirectPKGInstaller not reachable (9090/12800). Enable it and retry.\n");
    return -1;
}
/* --- end install block --- */

/* Builtin command dispatch table — must be BEFORE builtin_table() */
static const builtin_t g_builtins[] = {
  {"help",      cmd_help,      "Show help"},
  {"exit",      cmd_exit,      "Exit session"},
  {"ls",        cmd_ls,        "List directory"},
  {"ll",        cmd_ls,        "Alias for ls -l"},
  {"rm",        cmd_rm,        "Remove files (-r)"},
  {"cp",        cmd_cp,        "Copy files (-r)"},
  {"mv",        cmd_mv,        "Move/rename"},
  {"mkdir",     cmd_mkdir,     "Create directories (-p)"},
  {"pwd",       cmd_pwd,       "Print working directory"},
  {"cd",        cmd_cd,        "Change directory"},
  {"cat",       cmd_cat,       "Show file contents"},
  {"ps",        cmd_ps,        "List processes"},
  {"put",       cmd_put,       "Receive base64 file"},
  {"get",       cmd_get,       "Send base64 file"},
  {"install",   cmd_install,   "Install PKG via etaHEN DPI (9090/12800)"},
  {"klogtail",  cmd_klogtail,  "Tail kernel log (stub)"},
  {"execelf",   cmd_execelf,   "Execute ELF payload"},
  {"debugelf",  cmd_debugelf,  "Execute ELF (debug mode)"},
  {"kill",      cmd_kill,      "Send signal (kill <pid> [sig])"},
  {"serverctl", cmd_serverctl, "Control server (start/stop/restart/status)"}
};

// Ensure builtin_table() is located AFTER the g_builtins[] definition.
const builtin_t* builtin_table(size_t* count) {
  if(count) *count = sizeof(g_builtins)/sizeof(g_builtins[0]);
  return g_builtins;
}

int builtin_is_pipeline_safe(const char* name) {
  (void)name;
  return 1;
}

int run_builtin_in_current(int argc, char** argv, pid_t listener_pid) {
  (void)listener_pid;
  size_t n;
  const builtin_t* tbl = builtin_table(&n);
  for(size_t i=0;i<n;i++) {
    if(strcmp(argv[0], tbl[i].name)==0) {
      if(strcmp(argv[0],"ll")==0) {
        char* nargs[argc+2];
        nargs[0]="ls";
        nargs[1]="-l";
        for(int j=1;j<argc;j++) nargs[j+1]=argv[j];
        nargs[argc+1]=NULL;
        return tbl[i].fn(argc+1,nargs);
      }
      return tbl[i].fn(argc, argv);
    }
  }
  return -2;
}
/* ---- end additions ---- */
