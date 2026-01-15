#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/sysctl.h>
#if __has_include(<sys/user.h>)
#include <sys/user.h>
#endif
#include "util.h"

int main(void) {
#if !__has_include(<sys/user.h>)
  dprintf(1,"kill_sshsvr: sys/user.h not available\n");
  return 1;
#else
  int mib[4] = { CTL_KERN, KERN_PROC, KERN_PROC_PROC, 0 };
  size_t len=0;
  if(sysctl(mib,4,NULL,&len,NULL,0)<0) {
    dprintf(1,"kill_sshsvr: sysctl size failed\n");
    return 1;
  }
  char* buf = malloc(len);
  if(!buf) return 1;
  if(sysctl(mib,4,buf,&len,NULL,0)<0) {
    dprintf(1,"kill_sshsvr: sysctl data failed\n");
    free(buf);
    return 1;
  }
  struct kinfo_proc* p = (struct kinfo_proc*)buf;
  int count = (int)(len / sizeof(*p));
  int killed=0;
  pid_t self = getpid();
  for(int i=0;i<count;i++) {
    if(p[i].ki_pid == self) continue;
    if(strcmp(p[i].ki_comm,"sshsvr")==0) {
      if(kill(p[i].ki_pid,SIGTERM)==0) {
        dprintf(1,"kill_sshsvr: sent SIGTERM to pid %d\n", p[i].ki_pid);
        killed++;
      }
    }
  }
  free(buf);
  if(killed==0) dprintf(1,"kill_sshsvr: no sshsvr found\n");
  else dprintf(1,"kill_sshsvr: done (%d)\n", killed);
  unlink("/data/sshsvr.pid");
  return 0;
#endif
}