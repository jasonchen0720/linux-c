#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>
#include <time.h>
#include <fcntl.h>
#include <sys/time.h>

#include "mc_log.h"
#include "mc_base.h"
#include "generic_proc.h"

#define LOG_TAG 	"util"
int mc_thread_create(void* (*entry)(void *), void *arg)
{
	sigset_t set, oset;
	sigfillset(&set);
	pthread_t thread_id;
	pthread_sigmask(SIG_SETMASK, &set, &oset);
	int error = pthread_create(&thread_id, NULL, entry, arg);
	pthread_sigmask(SIG_SETMASK, &oset, NULL);
	pthread_detach(thread_id);
	return error;
}
long mc_gettime()
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);

	return ts.tv_sec + (ts.tv_nsec + 500 * 1000 * 1000) / (1000 * 1000 * 1000);
}
int mc_process_latch(const char *cmdline)
{
	char command[MC_CLIENT_CMDLINE_LEN + 3];
	int ret = strlen(cmdline);
	if (ret > MC_CLIENT_CMDLINE_LEN) {
		LOGE("Cmdline is too long:%s.", cmdline);
		return -1;
	}
	strcpy(command, cmdline);
	command[ret + 0] = ' ';
	command[ret + 1] = '&';
	command[ret + 2] = '\0';
	ret = system(command);
	LOGI("Starting:%s ret:%d", cmdline, ret);
	return ret;
}
int mc_process_restart(const char *name, const char *cmdline)
{
	if (process_kill(name, SIGKILL) < 0) {
		LOGE("Kill process error.");
		return -1;
	}
	
	LOGI("Restarting client:%s", name);
	return mc_process_latch(cmdline);	
}
int mc_setfds(int fd)
{
	int flags = fcntl(fd, F_GETFL);
	if (flags < 0)
		return -1;
	
	if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1)
		return -1;
	
	if (fcntl(fd, F_SETFD, FD_CLOEXEC) == -1)
		return -1;

	return 0;
}
