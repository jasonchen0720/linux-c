#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>
#include <time.h>
#include <fcntl.h>
#include <errno.h>
#include <stdarg.h>
#include <sys/time.h>
#include <sys/wait.h>

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
/*
 * @argv	: See The exec() family.
 * @redirect: Standard output file. 
 * @flags	: EXEC_F_WAIT | EXEC_F_APEEND
 * returns: 
 *			Without flag EXEC_F_WAIT
 *				<0: errno
 *				>0: the process's new pid
 *			With EXEC_F_WAIT
 *				rerturn child process executing status.
 */
int mc_exec(char *const argv[], const char *redirect, int flags)
{
	pid_t pid = fork();
	if (pid == 0) {
		/* child */
		signal(SIGINT, SIG_IGN);
#if MCD_DEBUG
		if (redirect == NULL)
			redirect = LOG_FILE;
#endif
		if (redirect) {
			flags = flags & EXEC_F_APEEND ? O_APPEND : O_TRUNC;
			int fd = open(redirect, O_RDWR | O_CREAT | flags , 0666);
			if (fd == -1) {
				exit(1);
			}
			dup2(fd, STDOUT_FILENO);
			dup2(fd, STDERR_FILENO);
			close(fd);
		} else {
			/*
			 * Keep all child process' stdout / stderr 
			 */
		#if 0
			close(STDOUT_FILENO);
			close(STDERR_FILENO);
		#endif
		}
		LOGI("executing: %s, pid: %d", argv[0], getpid());
		execvp(argv[0], argv);
		perror(argv[0]);
		exit(127);
	} else if (pid > 0) {
		/* parent */
		if (flags & EXEC_F_WAIT) {
			int status;
			LOGW("waiting: %s, pid: %d", argv[0], getpid());
			waitpid(pid, &status, 0);
			return WIFEXITED(status) ? WEXITSTATUS(status) : status;
		}
		return pid;
	} else {
		/* error */
		int err = errno;
		LOGI("fork errno:%d", err);
		return -err;
	}
}

#define trunc_space(s) do { while (*s == ' ') *s++ = '\0'; } while (0)
/*
 * @cmdline: 
 *			cmdline to be parsed and executed. 
 * 			@cmdline will be modified internally, caller should take care of its backup if needed.
 *
 * returns: See mc_exec() returns
 */
int mc_exec_cmdline(char *cmdline, int flags) 
{
	#define ARGC_MAX 63
	LOGI("executing cmdline:%s", cmdline);
	trunc_space(cmdline);
	char *p;
	char *argv[ARGC_MAX + 1];
	const char *redirect = NULL;
	int x = 1;
	int err = -EINVAL;
	if (cmdline[0] == '\0')
		goto out;
	
	argv[0] = cmdline;
	for (p = strchr(cmdline, ' '); p; p = strchr(p, ' ')) {
		trunc_space(p);
		switch (*p) {
		case '\0':
			continue;
		case '"':
		case '\'':
			argv[x] = p + 1;
			/* Find the end |"| or |'| */
			p = strchr(argv[x], *p);
			if (p == NULL)
				goto out;
			/* Eat |"| or |'| */
			*p++ = '\0';
			break;
		case '>':
			p++;
			if (p[0] == '>') {
				p++;
				flags |=  EXEC_F_APEEND;
			} 
			trunc_space(p);
			redirect = p;
			if (*redirect == '\0')
				goto out;
			
			argv[x] = NULL; 
			break;
		default:
			argv[x] = p;
			break;
		}
		if (++x > ARGC_MAX) {
			err = -ENOMEM;
			goto out;
		}
	}	
	argv[x] = NULL;
#if 1
	for (x = 0; argv[x]; x++) LOGI("argv[%d]: %s", x, argv[x]);
	LOGI("redirect file: %s, flags: %d", redirect, flags);
#endif
	err = mc_exec(argv, redirect, flags);
out:
	if (err < 0) LOGE("latch cmdline: %s, errno: %d", cmdline, x);
	return err;
	#undef ARGC_MAX
}

/*
 * @As optional, cmdline can contain 3 commands: 
 *		pre-command; command; post-command
 *		pre-command && command && post-command
 *
 */
int mc_process_latch(const char *cmdline) 
{
	char *commands = strdup(cmdline);
	if (commands == NULL) {
		LOGE("No memory: %s.", cmdline);
		return -ENOMEM;
	}
	int i = 0;
	int flags;
	char *command;
	char *outer;
	int pid[3];
	if (commands[0] == ';') {
		i = 1;
		pid[0] = -ENOENT;
	}
	for (command = strtok_r(commands, ";&", &outer); command; command = strtok_r(NULL, ";&", &outer)) {
		flags = *outer == '&' ? EXEC_F_WAIT : 0;
		pid[i] = mc_exec_cmdline(command, flags);
		LOGI("pid[%d] / status[%d]: %d, flags: %d", i, i, pid[i], flags);
		if (i++ >= 2)
			break;
	}
	free(commands);
	/*
	 * return process latch pid - exe-command.
	 */
	return i > 1 ? pid[1] : pid[0];
}
/*
 * Return: bool
 */
int mc_process_validate(const char *name, int pid)
{
	char cmdline[MC_CLIENT_CMDLINE_LEN + 1];
	/*
	 * Sequential matching: 
	 * 						/proc/{pid}/cmdline
	 *						/proc/{pid}/comm
	 *						/proc/{pid}/exe
	 */
	if (process_name(cmdline, sizeof(cmdline), pid) < 0) {
		LOGE("process %s[%d] not found.", name, pid);
		return 0;
	}
	if (strcmp(cmdline, name)) {
		LOGE("process %s[%d] but cmdline: %s.", name, pid, cmdline);
		return 0;
	}
	return 1;
}
/*
 * return: <0: errno
 *         >0: the process's new pid
 */
int mc_process_restart(const char *name, const char *cmdline)
{
	process_kill(name, SIGKILL);
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
