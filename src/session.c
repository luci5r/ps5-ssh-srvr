#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <signal.h>

#include "session.h"
#include "builtins.h"
#include "util.h"   // added for dprintf

#define MAX_LINE 2048
#define MAX_TOKS 256
#define MAX_PIPE 16

typedef struct {
  char** argv;
  int argc;
  int is_builtin;
} command_t;

static char* trim(char* s) {
  while(isspace((unsigned char)*s)) s++;
  char* e = s + strlen(s);
  while(e>s && isspace((unsigned char)e[-1])) e--;
  *e=0;
  return s;
}

static int tokenize(char* line, char** toks) {
  int ntok=0;
  char* p=line;
  while(*p) {
    while(isspace((unsigned char)*p)) p++;
    if(!*p) break;
    if(*p=='|' || *p=='>' ) {
      if(ntok<MAX_TOKS) {
        if(*p=='>' && p[1]=='>') {
          toks[ntok++]=strdup(">>");
          p+=2;
          continue;
        }
        char sym[2]={*p,0};
        toks[ntok++]=strdup(sym);
        p++;
      } else break;
      continue;
    }
    char* start=p;
    int inq=0;
    while(*p && (inq || (!isspace((unsigned char)*p) && *p!='|' && *p!='>'))) {
      if(*p=='"') inq=!inq;
      p++;
    }
    size_t len = p-start;
    char* t = malloc(len+1);
    memcpy(t,start,len);
    t[len]=0;
    if(t[0]=='"' && t[len-1]=='"' && len>=2) {
      t[len-1]=0;
      memmove(t,t+1,len-1);
    }
    if(ntok<MAX_TOKS) toks[ntok++]=t;
  }
  toks[ntok]=NULL;
  return ntok;
}

static void free_tokens(char** toks, int ntok) {
  for(int i=0;i<ntok;i++) free(toks[i]);
}

static int build_pipeline(char** toks, int ntok, command_t* cmds, int* ncmds,
                          char** redir_file, int* append_mode) {
  int ci=0;
  int i=0;
  while(i<ntok) {
    if(ci>=MAX_PIPE) return -1;
    cmds[ci].argv = malloc(sizeof(char*)*(MAX_TOKS));
    cmds[ci].argc = 0;
    cmds[ci].is_builtin = 0;
    while(i<ntok) {
      if(strcmp(toks[i],"|")==0) { i++; break; }
      if(strcmp(toks[i],">")==0 || strcmp(toks[i],">>")==0) {
        *append_mode = (toks[i][1]=='>');
        i++;
        if(i>=ntok) return -1;
        *redir_file = strdup(toks[i]);
        i++;
        break;
      }
      cmds[ci].argv[cmds[ci].argc++] = strdup(toks[i]);
      i++;
    }
    if(cmds[ci].argc==0) continue;
    cmds[ci].argv[cmds[ci].argc]=NULL;
    size_t bc;
    const builtin_t* tbl = builtin_table(&bc);
    for(size_t b=0;b<bc;b++) {
      if(strcmp(tbl[b].name, cmds[ci].argv[0])==0 ||
         (strcmp(cmds[ci].argv[0],"ll")==0 && strcmp(tbl[b].name,"ls")==0)) {
        cmds[ci].is_builtin = 1;
        break;
      }
    }
    ci++;
  }
  *ncmds = ci;
  return 0;
}

static void free_pipeline(command_t* cmds, int ncmds, char* redir_file) {
  for(int i=0;i<ncmds;i++) {
    for(int j=0;j<cmds[i].argc;j++) free(cmds[i].argv[j]);
    free(cmds[i].argv);
  }
  if(redir_file) free(redir_file);
}

static int execute_pipeline(command_t* cmds, int ncmds, char* redir_file,
                            int append_mode, pid_t listener_pid) {
  if(ncmds==0) return 0;
  if(ncmds==1 && cmds[0].is_builtin && !redir_file) {
    int rc = run_builtin_in_current(cmds[0].argc, cmds[0].argv, listener_pid);
    if(rc==255) return -2;
    return rc;
  }

  int pipes[MAX_PIPE-1][2];
  for(int i=0;i<ncmds-1;i++) {
    if(pipe(pipes[i])<0) { dprintf(1,"pipe error\n"); return -1; }
  }

  for(int i=0;i<ncmds;i++) {
    pid_t pid = fork();
    if(pid==0) {
      if(i>0) {
        dup2(pipes[i-1][0],0);
      }
      if(i<ncmds-1) {
        dup2(pipes[i][1],1);
      } else if(redir_file) {
        int fd = open(redir_file,
                      O_WRONLY|O_CREAT|(append_mode?O_APPEND|O_WRONLY:O_TRUNC),
                      0644);
        if(fd<0) { dprintf(1,"redir open failed\n"); _exit(1); }
        dup2(fd,1);
        close(fd);
      }
      for(int k=0;k<ncmds-1;k++) {
        close(pipes[k][0]); close(pipes[k][1]);
      }
      char lbuf[32];
      snprintf(lbuf,sizeof(lbuf),"%d", listener_pid);
      setenv("SESSION_LISTENER_PID", lbuf, 1);
      if(cmds[i].is_builtin) {
        int rc = run_builtin_in_current(cmds[i].argc, cmds[i].argv, listener_pid);
        _exit(rc<0?1:(rc==255?0:rc));
      } else {
        execvp(cmds[i].argv[0], cmds[i].argv);
        dprintf(1,"exec failed: %s\n", cmds[i].argv[0]);
        _exit(127);
      }
    }
  }
  for(int i=0;i<ncmds-1;i++) { close(pipes[i][0]); close(pipes[i][1]); }

  int status;
  int exitcode=0;
  for(int i=0;i<ncmds;i++) {
    wait(&status);
    if(WIFEXITED(status)) exitcode = WEXITSTATUS(status);
  }
  return exitcode;
}

static int read_line(int fd, char* buf, size_t max) {
  size_t off=0;
  while(off+1<max) {
    char c;
    ssize_t r = read(fd,&c,1);
    if(r<=0) return -1;
    if(c=='\r') continue;
    if(c=='\n') break;
    buf[off++]=c;
  }
  buf[off]=0;
  return (int)off;
}

void session_handle(int fd, const char* remote_ip, pid_t listener_pid) {
  dprintf(fd,"Pseudo-SSH (unencrypted) - remote %s\n", remote_ip);
  dprintf(fd,"Type 'help' for builtins.\n");
  dup2(fd,0);
  dup2(fd,1);
  dup2(fd,2);

  char line[MAX_LINE];
  while(1) {
    dprintf(1,"$ ");
    if(read_line(0,line,sizeof(line))<0) break;
    char* ln = trim(line);
    if(!*ln) continue;
    char* toks[MAX_TOKS];
    int ntok = tokenize(ln, toks);
    if(ntok<=0) continue;
    command_t cmds[MAX_PIPE];
    int ncmds=0;
    char* redir_file=NULL;
    int append_mode=0;
    if(build_pipeline(toks, ntok, cmds, &ncmds, &redir_file, &append_mode)<0) {
      dprintf(1,"parse error\n");
      free_tokens(toks, ntok);
      continue;
    }
    int rc = execute_pipeline(cmds, ncmds, redir_file, append_mode, listener_pid);
    free_pipeline(cmds,ncmds,redir_file);
    free_tokens(toks, ntok);
    if(rc==-2) break;
  }
}
