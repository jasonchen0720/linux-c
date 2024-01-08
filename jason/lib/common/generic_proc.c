#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <ctype.h>
#include <dirent.h>
#include <signal.h>
#include <stdlib.h>
#include <time.h>
#include "generic_proc.h"
#define PROCESS_NAME_MAX	64

void msleep(long msec)
{
	struct timeval time;
	time.tv_sec   = (msec / 1000);
	time.tv_usec  = (msec % 1000) * 1000;
	int ret = 0;
	do {
		ret = select(0, NULL, NULL, NULL, &time);
	} while (ret < 0 && errno == EINTR);
}

int process_pidfile_acquire(const char *pidfile)
{
	int pid_fd;
	if (pidfile == NULL) return -1;

	pid_fd = open(pidfile, O_CREAT | O_WRONLY, 0644);
	if (pid_fd < 0) {
		fprintf(stderr, "Unable to open pidfile %s: %s\n", pidfile, strerror(errno));
	} else {
		lockf(pid_fd, F_LOCK, 0);
	}
	
	return pid_fd;
}


void process_pidfile_write_release(int pid_fd)
{
	FILE *out;

	if (pid_fd < 0) 
		return;

	if ((out = fdopen(pid_fd, "w")) != NULL) {
		fprintf(out, "%d\n", getpid());
		fclose(out);
	}
	lockf(pid_fd, F_UNLCK, 0);
	close(pid_fd);
}


void process_pidfile_delete(const char *pidfile)
{
	if (pidfile) 
		unlink(pidfile);
}

/*
 * On success, process_exe() returns the number of bytes placed in buf.  On error, -1 is returned
 */
int process_exe(char *buf, unsigned int size, pid_t pid)
{
	char path[32];
	sprintf(path, "/proc/%d/exe", pid);
	int ssize = readlink(path, buf, size);
	if (ssize < 0)
		return -1;

	if (size <= ssize)
		return -1;
	
	buf[ssize] = '\0';		
	return ssize;
}

/*
 * On success, process_exename() returns the number of bytes placed in buf.  On error, -1 is returned
 */
int process_exename(char *buf, unsigned int size, pid_t pid)
{
	char path[32];
	char buff[1024];
	char *exe = buff;
	char *ptr = NULL;
	sprintf(path, "/proc/%d/exe", pid);
	char* name;
	int bsize = sizeof(buff);
	int ssize = readlink(path, buff, sizeof(buff));
	if (ssize < 0)
		goto out;

	for (; bsize <= ssize;) {
		bsize += 80;

		ptr = realloc(ptr, bsize);
		
		ssize = readlink(path, ptr, bsize);
		if (ssize < 0)
			goto out;

		exe = ptr;
	}

	exe[ssize] = '\0';
	
	name = strrchr(exe,  '/');
	if (name == NULL)
		name = exe;

	ssize = snprintf(buf, size, "%s", ++name);

	if (ssize >= size)
		ssize = -1;
out:
	if (ptr)
		free(ptr);
	return ssize;
}
/*
 * On success, process_name() returns the number of bytes placed in buf.  On error, -1 is returned
 */
int process_name(char *buf, unsigned int size, pid_t pid)
{
	char path[32];
	char temp[1024];

	sprintf(path, "/proc/%d/cmdline", pid);

	int fd = open(path, O_RDONLY);
	if (fd < 0)
		return -1;
	
	int ssize = read(fd, temp,	sizeof(temp) - 1);
	close(fd);
	if (ssize > 0) {
		temp[ssize] = '\0';
		char *s = strchr(temp, ' ');
		if (s) 
			s[0] = '\0';
		s = strrchr(temp, '/');
		s = s ? s + 1 : temp;
		ssize = snprintf(buf, size, "%s", s);
		if (ssize >= size)
		 	ssize = -1;

		return ssize;
	}

	sprintf(path, "/proc/%d/comm", pid);

	fd = open(path, O_RDONLY);
	if (fd < 0)
		return -1;
	
	ssize = read(fd, temp,	sizeof(temp) - 1);
	close(fd);
	if (ssize < 0)
		return -1;

	if (ssize > 0) {
		if (temp[ssize - 1] == '\n')
			ssize--;
		
		if (size <= ssize)
			return -1;
		
		temp[ssize] = '\0';
		strcpy(buf, temp);
		/* ssize:15 synchronize with sizeof(task_struct.comm) - TASK_COMM_LEN in linux/sched.h */
		if (ssize > 0 && ssize < 15) 
			return ssize;
	}
	int exesize = process_exename(buf, size, pid);
	return exesize > 0 ? exesize : ssize;
}

static int is_digital(const char *str)
{
	int i;
	for (i = 0; str[i] != '\0'; i++) {
		if (!isdigit(str[i]))
			return 0;
	}
	return 1;
}
int for_each_pid(int (*action)(pid_t, void *), void *arg)
{
	int ret = 0;
	DIR *dir = NULL;
	struct dirent *dirent;
	dir = opendir("/proc");
	if (!dir) {
		printf("Open /proc failure:%d.", errno);
		return -1;
	}
	for (dirent = readdir(dir); dirent != NULL; dirent = readdir(dir)) {
		if (!is_digital(dirent->d_name))
			continue;
		
		if ((ret = action(atoi(dirent->d_name), arg)) == 0)
			continue;

		break;
	}
	closedir(dir);
	return ret;
}
int for_each_process(int (*action)(const char *, pid_t, void *), void *arg)
{
	int ret = 0;
	DIR *dir = NULL;
	struct dirent *dirent;
	char name[PROCESS_NAME_MAX];
	dir = opendir("/proc");
	if (!dir) {
		printf("Open /proc failure:%d.", errno);
		return -1;
	}
	for (dirent = readdir(dir); dirent != NULL; dirent = readdir(dir)) {
		if (!is_digital(dirent->d_name))
			continue;

		if (process_name(name, sizeof(name), atoi(dirent->d_name)) < 0)
			continue;
		
		if ((ret = action(name, atoi(dirent->d_name), arg)) == 0)
			continue;

		break;
	}
	closedir(dir);
	return ret;
}
static int __process_find(const char *comm, pid_t pid, void *arg) 
{
	return strcmp(comm, (const char *)arg) ? 0 : pid;
}
static int __process_kill(const char *comm, pid_t pid, void *arg)
{
	struct {
		const char *name;
		int sig;
	} *p = arg;
	int ret = 0;
	if (!strcmp(comm, p->name)) {
		ret = kill(pid, p->sig);
		printf("Process:%s is alive, Killed ret:%d.", comm, ret);
	}
	
	return ret;
}
static int __process_count(const char *comm, pid_t pid, void *arg) 
{
	struct {
		const char *name;
		int count;
	} *p = arg;
	if (!strcmp(comm, p->name))
		p->count += 1;
	return 0;
}

pid_t process_find(const char *name) 
{
	return for_each_process(__process_find, (void *)name);
}

int process_kill(const char *name, int sig)
{
	struct {
		const char *name;
		int sig;
	} arg = {name, sig};
	return for_each_process(__process_kill, &arg);
}

int process_count(const char *name)
{
	struct {
		const char *name;
		int count;
	} arg = {name, 0};
	for_each_process(__process_count, &arg);
	return arg.count;
}

