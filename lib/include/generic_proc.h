#ifndef __GENERIC_PROCESS_H__
#define __GENERIC_PROCESS_H__
#include <sys/types.h>
int process_pidfile_acquire(const char *pidfile);
void process_pidfile_write_release(int pid_fd);
void process_pidfile_delete(const char *pidfile);
int process_exe(char *buf, unsigned int size, pid_t pid);
int process_name(char *buf, unsigned int size, pid_t pid);
int process_exename(char *buf, unsigned int size, pid_t pid);
int for_each_pid(int (*action)(pid_t, void *), void *arg);
int for_each_process(int (*action)(const char *, pid_t, void *), void *arg);
pid_t process_find(const char *name) ;
int process_kill(const char *name, int sig);
int process_count(const char *name);
#endif
