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
#include "generic_file.h"
#include "util_process.h"
#define PROCESS_NAME_MAX	64
#define PROCESS_FPID_PATH	"/var/volatile/run/"
int get_pid(char *filename) {
	int pid = utils_file_read_integer(filename, -1);
	if (pid < 0) {
		printf("%s(%d): get failed, filename[%s]\n", __FUNCTION__, __LINE__,
				filename);
		return -1;
	}
	return pid;
}
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

int process_running_check()
{
	char name[PROCESS_NAME_MAX] = {0};
	char buff[PROCESS_NAME_MAX + sizeof(PROCESS_FPID_PATH) + 4] = {0};
	
	struct flock lock;
	
	pid_t pid = getpid();
	
	process_name(name, sizeof(name), pid);
	
	sprintf(buff, PROCESS_FPID_PATH"%s.pid", name);
	int fd = open(buff, O_RDWR | O_CREAT, 0644);
	if (fd < 0){
		printf("open:%s failed:%d.", buff, errno);
		return -1;
	}

	lock.l_type = F_WRLCK;
	lock.l_whence = 0;
	lock.l_start = 0;
	lock.l_len = 0;
	int ret = fcntl(fd, F_SETLK, &lock);
	if (ret < 0){
		printf("errno:%d.", errno);
		if (errno == EAGAIN || errno == EWOULDBLOCK) {
			printf("%s is running.", name);
			close(fd);
			return 1;
		}
		close(fd);
		return -1;
	}
	ftruncate(fd, 0);
	lseek(fd, 0, SEEK_SET);
	ret = sprintf(buff, "%d", pid);
	
	write(fd, buff, ret);

	/* Do not close fd and hold the flock for the whole life cycle. */

	return 0;
}

/* 
 * Caller should ensure the name is unique.
 * Return locked fd.
 */
int process_running_trylock(const char *name, int killtime, int try_times, int interval_ms)
{
	struct flock lock;
	
	pid_t pid = getpid();

	char s[128];
	int ret, fd_pid, fd_lock;
	int ssize = strlen(name) + sizeof(PROCESS_FPID_PATH) + 5; /* 5: max size of ".pid" and ".lock" */

	if (ssize >= sizeof(s)) {
		printf("Buf overflow, locking failed:%s.", name);
		return -1;
	}
	sprintf(s, "%s%s.pid", PROCESS_FPID_PATH, name);
	
	fd_pid = open(s, O_RDWR | O_CREAT | O_SYNC , 0644);
	if (fd_pid < 0)
	{
		printf("open:%s failed:%d.", s, errno);
		return -1;
	}

	sprintf(s, "%s%s.lock", PROCESS_FPID_PATH, name);
	
	fd_lock = open(s, O_RDWR | O_CREAT, 0644);
	if (fd_lock < 0)
	{
		printf("open:%s failed:%d.", s, errno);
		close(fd_pid);
		return -1;
	}

	sys_file_lock(fd_pid);
	
	lseek(fd_pid, 0, SEEK_SET);
	ssize = read(fd_pid, s, sizeof(s) - 1);
	s[ssize] = '\0';
	int running_pid = atoi(s);
	
	lock.l_type   = F_WRLCK;
	lock.l_whence = SEEK_SET;
	lock.l_start  = 0;
	lock.l_len	  = 0;

	int locking_times = 0;
	
	do {
		ret = fcntl(fd_lock, F_SETLK, &lock);
		if (ret == 0)	
			break;

		if (errno == EAGAIN || errno == EWOULDBLOCK) {
			locking_times++;
			printf("Trylock %d, process is running, pid:%d.", locking_times, running_pid);
			msleep(interval_ms);	
			if (killtime >= 0 && 
				killtime <= locking_times && running_pid > 0)
				kill(running_pid, SIGKILL);
			continue;
		}
		goto err;
	} while (locking_times < try_times);

	if (locking_times >= try_times)
	   goto err;

	ftruncate(fd_pid, 0);
	lseek(fd_pid, 0, SEEK_SET);
	ssize = sprintf(s, "%d", pid);
	
	write(fd_pid, s, ssize);
	
	sys_file_unlock(fd_pid);
	close(fd_pid);

	printf("%d lock done at %ld", pid, (long)time(NULL));
	return fd_lock;
err:
	printf("%d lock fail at %ld", pid, (long)time(NULL));
	sys_file_unlock(fd_pid);
	close(fd_pid);
	close(fd_lock);
	return -1;
}

int process_running_unlock(const char *name, int locked_fd)
{
    struct flock lock;

    if (locked_fd < 0){
        printf("Invalid locked fd");
        return -1;
    }

	char s[128];

	int ssize = strlen(name) + sizeof(PROCESS_FPID_PATH) + 5; /* 5: max size of ".lock" */

	if (ssize >= sizeof(s)) {
		printf("Buf overflow, unlocking failed:%s.", name);
		return -1;
	}
	
	sprintf(s, "%s%s.lock", PROCESS_FPID_PATH, name);
	
	pid_t pid = getpid();
	
	char rel[256] = {0};
	char buf[256] = {0};
	
	realpath(s, rel);
	
	sprintf(s, "/proc/%d/fd/%d", pid, locked_fd);
	readlink(s, buf, sizeof(buf) - 1);

	if (strcmp(buf, rel)) {
		printf("Invalid locked fd: %s %s", buf, rel);
		return -1;
	}
	
    lock.l_type   = F_UNLCK;
	lock.l_whence = SEEK_SET;
    lock.l_start  = 0;
    lock.l_len    = 0;

    fcntl(locked_fd, F_SETLK, &lock);
    close(locked_fd);

    printf("%d unlock done at %ld.", pid, (long)time(NULL));
    return 0;
}


int process_generate_pid_file()
{
	FILE *fp = NULL;

	char name[PROCESS_NAME_MAX] = {0};
	char fpid[PROCESS_NAME_MAX + 16] = {0};
	
	pid_t pid = getpid();	
	process_name(name, sizeof(name), pid);	
	sprintf(fpid, PROCESS_FPID_PATH"%s.pid", name);
	
	fp = fopen(fpid, "w");
    if (fp != NULL) {
        int ssize = fprintf(fp, "%d\n", pid);
        fclose(fp);
		return ssize > 0 ? 0 : -1;
    }
	return -1;
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

