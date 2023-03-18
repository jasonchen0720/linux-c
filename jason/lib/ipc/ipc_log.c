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
#include <sys/types.h>
#include <sys/stat.h>
#include "ipc_log.h"
static void ipc_flock(int fd) 
{
	struct flock lock;
	lock.l_type = F_WRLCK;
	lock.l_start = 0;
	lock.l_len = 0;
	lock.l_whence = SEEK_SET;
  try_again: 
	if(fcntl(fd, F_SETLKW, &lock) < 0)
	{
		if (errno == EINTR) 
		{
			goto try_again;
		}
	}
}

static void ipc_funlock(int fd) 
{
	struct flock lock;
	lock.l_type = F_UNLCK;
	lock.l_start = 0;
	lock.l_len = 0;
	lock.l_whence = SEEK_SET;
  try_again: 
	if(fcntl(fd, F_SETLK, &lock) < 0) 
	{
		if (errno == EINTR) 
		{
			goto try_again;
		}
	}
}
static void ipc_log_copy(FILE *io, const char *file)
{
	char buf[32 * 1024];

	FILE *fp = fopen(file, "w");
	if (!fp) {
		printf("Copy error.\n");
		return;
	}
	size_t size;
	fseek(io, 0, SEEK_SET);
	while ((size = fread(buf, 1, sizeof(buf), io))) {
		fwrite(buf, 1, size, fp);
	}
	fflush(fp);
	fclose(fp);
}
static inline int ipc_log_time(char *time_info, size_t size)
{
	struct tm tm;
	time_t now_time;
	now_time = time(NULL);
	if (localtime_r(&now_time, &tm))
		return strftime(time_info, size, "%b %d %H:%M:%S ", &tm);
	else
		return snprintf(time_info, size, "%s ", "");
}
static void ipc_log_print(const char *format, va_list ap)
{
	int fd;
	int size;
	char time[32];
	FILE *fp = fopen(IPC_LOG_FILE, "a+");
	if(fp == NULL)
		return;
	fd = fileno(fp);
	ipc_flock(fd);
	size = ipc_log_time(time, sizeof(time));
	fwrite(time, size, 1, fp);
	vfprintf(fp, format, ap);
	fflush(fp);

	if (ftell(fp) > IPC_LOG_MAX_SIZE) {
		ipc_log_copy(fp, IPC_LOG_FILE_BAK);
		ftruncate(fd, 0);
	}

	ipc_funlock(fd);
	fclose(fp);
}

/*
 *record the most critical error log
 */
void IPC_LOG(const char *format, ...)
{
	va_list ap;
	va_start(ap, format);
	ipc_log_print(format, ap);
	va_end(ap);
}
