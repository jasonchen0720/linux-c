#ifndef __MC_UTILS_H__
#define __MC_UTILS_H__

#define EXEC_F_WAIT   1
#define EXEC_F_APEEND 2
int mc_exec(char *const argv[], const char *redirect, int flags);
int mc_exec_cmdline(char *cmdline, int flags) ;
int mc_thread_create(void* (*entry)(void *), void *arg);
long mc_gettime();
int mc_process_validate(const char *name, int pid);
int mc_process_latch(const char *cmdline);
int mc_process_restart(const char *name, const char *cmdline);
int mc_setfds(int fd);
#endif
