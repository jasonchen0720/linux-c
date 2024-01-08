#ifndef __MC_UTILS_H__
#define __MC_UTILS_H__
int mc_thread_create(void* (*entry)(void *), void *arg);
long mc_gettime();
int mc_process_latch(const char *cmdline);
int mc_process_restart(const char *name, const char *cmdline);
int mc_setfds(int fd);
#endif
