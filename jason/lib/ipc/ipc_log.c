/*
 * Copyright (c) 2017, <-Jason Chen->
 * Issue fix: - 20211126
 *            - (1). Mult-thread-safe: localtime() -> localtime_r() in ipc_log_time().
 *            - (2). Risk of null pointer: directly use the returning of localtime() in ipc_log_time().
 *
 * Author: Jie Chen <jasonchen@163.com>
 * Brief : This program is the implementation of IPC simple log print.
 * Date  : Created at 2017/11/01
 */
#include <stdio.h>
#include <stdarg.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <limits.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/sem.h>
#include "ipc_atomic.h"
#include "ipc_base.h"
#include "ipc_log.h"
#define IPC_LOG_PATH		"/tmp"
#define IPC_LOG_FILE		IPC_LOG_PATH"/ipclog"
#define IPC_LOG_SIZE 		256 * 1024
#define IPC_LOG_NUM 		4
#define IPC_LOG_LINE		256

#define IPC_LOCK_INVALID		-1
#define IPC_LOCK_INITING		-2
static int lock = IPC_LOCK_INVALID;
union semun {
	    int              val;    /* Value for SETVAL */
	    struct semid_ds *buf;    /* Buffer for IPC_STAT, IPC_SET */
	    unsigned short  *array;  /* Array for GETALL, SETALL */
	    struct seminfo  *__buf;  /* Buffer for IPC_INFO
	                                (Linux specific) */
};
#define lock_compare_and_swap(old_val, new_val)	ATOMIC_BCS(&lock, old_val, new_val)
static void lock_log(const char *info, int nerr)
{
	FILE *fp = fopen(IPC_LOG_PATH"/ipclog.lk", "a+");
	if (fp == NULL)
		return;
	
	if (ftell(fp) < IPC_LOG_SIZE) {
		char buf[64];
		if (info) {
			char err[32] = {0};
			if (nerr) sprintf(err, " errno:%d", nerr);
			snprintf(buf, sizeof(buf), "%d lock: %s%s\n",  gettid(), info, err);
		} else 
			snprintf(buf, sizeof(buf), "%d lock: %d\n",  gettid(), lock);
		fwrite(buf, strlen(buf), 1, fp);
	}
	fclose(fp);
}
int lock_init()
{
	int doing = !lock_compare_and_swap(IPC_LOCK_INVALID, IPC_LOCK_INITING);
	if (doing) {
		lock_log("init", EINPROGRESS);
		return -1;
	}
	int sem = -1;
    key_t key = ftok(IPC_LOG_PATH, 0x96);
    if (key == -1) {
		lock_log("ftok", errno);
        goto out;
    }
    sem = semget(key, 1, IPC_CREAT | IPC_EXCL | 0666);
    if (sem == -1) {
		if (errno == EEXIST)
			sem = semget(key, 1, 0666);
		else lock_log("semget", errno);
		goto out;
    }
	union semun arg = {.val = 1};
	if (semctl(sem, 0, SETVAL, arg) == -1) {
		lock_log("semctl", errno);
	    sem = -1;
	}
out:
	lock_compare_and_swap(IPC_LOCK_INITING, sem);
	lock_log(NULL, 0);
    return sem;
}

static int lock_hold()
{
	if (lock < 0) {
		if (lock_init() == -1)
			return -1;
	}
	struct sembuf sop[1];
	sop[0].sem_num = 0;
	sop[0].sem_op = -1;
	sop[0].sem_flg = SEM_UNDO;
	int ret = 0;
	/*
	 * If an operation specifies SEM_UNDO, it will be automatically undone when the process terminates.
	 */
#if 1
	do {
		ret = semop(lock, sop, 1);
	} while (ret < 0 && errno == EINTR);
#else
	struct timespec ts;
	ts.tv_sec	= 1; 
	ts.tv_nsec	= 0; 
	do {
		ret = semtimedop(lock, sop, 1, &ts);
	} while (ret < 0 && errno == EINTR);
#endif
	
	return ret;
}

static int lock_relase()
{
	if (lock < 0)
		return -1;
	
	struct sembuf sop[1];

	sop[0].sem_num = 0;
	sop[0].sem_op = 1;
	sop[0].sem_flg = SEM_UNDO;
	int ret = 0;

	do {
		ret = semop(lock, sop, 1);
	} while (ret < 0 && errno == EINTR);
	
	return ret;
}
static void log_backup(int fd)
{
	lock_hold();
	const char *log_file = IPC_LOG_FILE;
	char s[32];
	char file[2][PATH_MAX + 1] = {0};
	
	sprintf(s, "/proc/self/fd/%d", fd);
	/* File may by renamed by other process, get its link first */
	readlink(s, file[0], PATH_MAX);
	/* Get its absolute pathname */
	realpath(log_file, file[1]);
	
	if (strcmp(file[0], file[1])) {
		goto out;
	}
	
	int i = IPC_LOG_NUM - 1;
	int n = i & 1;
	
	sprintf(file[n], "%s.%d", log_file, i);
	for (i--; i >= 0; i--) {
		n = i & 1;
		sprintf(file[n], "%s.%d", log_file, i);
		if (access(file[n], F_OK) == 0)
			rename(file[n], file[!n]);
	}
	rename(log_file, file[n]);
out:
	lock_relase();
}
static int log_format(char buf[IPC_LOG_LINE], const char *format, va_list ap)
{
	int offs = 0;
	struct tm tm;
	time_t now_time;
	now_time = time(NULL);
	if (localtime_r(&now_time, &tm))
		offs = strftime(buf, 32, "%b %d %H:%M:%S ", &tm);
	else
		offs = snprintf(buf, 32, "%s ", "");

	offs += vsnprintf(buf + offs, IPC_LOG_LINE - offs, format, ap);
	if (offs >= IPC_LOG_LINE) {
		offs  = IPC_LOG_LINE;
		buf[offs - 1] = '\n';
	}
	return offs;
}
static void log_print(const char *format, va_list ap)
{
	FILE *fp = fopen(IPC_LOG_FILE, "a+");
	if (fp == NULL)
		return;
	char buf[IPC_LOG_LINE];
	int size = log_format(buf, format, ap);
	fwrite(buf, size, 1, fp);
	fflush(fp);
	if (ftell(fp) > IPC_LOG_SIZE)
		log_backup(fileno(fp));
	
	fclose(fp);
}
static void log_debug(const char *format, va_list ap)
{
	char time[32];
	char buf[IPC_LOG_LINE];
	log_format(buf, format, ap);
	printf("%s", buf);
}

/*
 *record the most critical error log
 */
void ipc_log(const char *format, ...)
{
	va_list ap;
	va_start(ap, format);
	log_print(format, ap);
	va_end(ap);
}
void ipc_dbg(const char *format, ...)
{
	va_list ap;
	va_start(ap, format);
	log_debug(format, ap);
	va_end(ap);
}

