#ifndef __UTIL_PROCESS_H__
#define __UTIL_PROCESS_H__
#include <sys/types.h>

int get_pid(char *filename);
int process_running_check();
int process_running_trylock(const char *name, int killtime, int try_times, int interval_ms);
int process_running_unlock(const char *name, int locked_fd);
int process_generate_pid_file();
int process_exe(char *buf, unsigned int size, pid_t pid);
int process_name(char *buf, unsigned int size, pid_t pid);
int process_exename(char *buf, unsigned int size, pid_t pid);
int for_each_pid(int (*action)(pid_t, void *), void *arg);
int for_each_process(int (*action)(const char *, pid_t, void *), void *arg);
pid_t process_find(const char *name) ;
int process_kill(const char *name, int sig);
int process_count(const char *name);
#endif
