/*
 * Copyright (c) 2017, <-Jason Chen->
 * Author: Jie Chen <jasonchen@163.com>
 * Note  : This program is used for libipc simple log print implementation
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
static int ipc_log_time(char *time_info)
{
	struct tm *datetime;
	time_t now_time;
	now_time = time(NULL);
	datetime = localtime(&now_time);
	return strftime(time_info, 80, "%b %d %H:%M:%S ", datetime);
}
static int ipc_log_size(int fd)
{
    struct stat st_buf;
	
	return 0 == fstat(fd, &st_buf) ? st_buf.st_size : 0;
}

static void ipc_log_print(const char *format, va_list ap)
{
	int fd;
	int size;
	char buff[128];
	FILE *fp;
	if((fp = fopen(IPC_LOG_FILE, "a+")) == NULL)	
		return;
	fd = fileno(fp);
	ipc_flock(fd);
	size = ipc_log_size(fd);
	if(size > IPC_LOG_MAX_SIZE)
	{
		snprintf(buff, sizeof(buff), "cp %s %s", IPC_LOG_FILE, IPC_LOG_FILE_BAK);
		system(buff);
		ftruncate(fd, 0); 
		lseek(fd, 0, SEEK_SET);
	}
	size = ipc_log_time(buff);
	fwrite(buff, size, 1, fp);
	vfprintf(fp, format, ap);
	fflush(fp);
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
