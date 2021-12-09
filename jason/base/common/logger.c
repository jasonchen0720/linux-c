#include <stdio.h>
#include <stdarg.h>
#include <fcntl.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>			 
#define LOG_FILE_PATH_BUF_SIZE	256
#define LOG_FILE_PATH_MAX_SIZE	(LOG_FILE_PATH_BUF_SIZE - 16)
struct logger_struct
{
	int		file_size;
	int		file_bakup;
	int		lock;
	char 	file_path[LOG_FILE_PATH_BUF_SIZE];
};
static struct logger_struct __logger = {
	.file_size 	= 0,
	.file_bakup	= 0,
	.lock		= 0,
	.file_path	= {0},
};
#define LOGGER() 		(&__logger)
#define logger_ready()	(LOGGER()->lock > 0)
static const char *__log_file(const char *path)
{
	const char *slash = path;
	const char *pfile = path;
	for (;;) {
		slash = strchr(slash, '/');
		if (slash) 
			pfile = ++slash;
		else break;
	}
	return pfile;
}
static int __log_lock_init(struct logger_struct *logger)
{
	char lockf[LOG_FILE_PATH_BUF_SIZE];
	const char *file_name = __log_file(logger->file_path);
	size_t path_len = strlen(logger->file_path) - strlen(file_name);
	memcpy(lockf, logger->file_path, path_len);
	sprintf(lockf + path_len, ".%s", file_name);
	int fd = open(lockf, O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
	if (fd < 0) {
	    printf("Can't open file:%s, errno:%d\n", lockf, errno);
	    return -1;
	}
	return fd;
}
static void __log_backup(struct logger_struct *logger)
{
	char file[2][LOG_FILE_PATH_BUF_SIZE];
	int i = logger->file_bakup - 1;
	int n = i & 1;
	
	sprintf(file[n], "%s.%d", logger->file_path, i);
	for (i--; i >= 0; i--) {
		n = i & 1;
		sprintf(file[n], "%s.%d", logger->file_path, i);
		if (access(file[n], F_OK) == 0)
			rename(file[n], file[!n]);
	}
	rename(logger->file_path, file[n]);
}
static void __log_copy(FILE *io, const char *file)
{
	char buf[32 * 1024];
	sprintf(buf, "%s.%d", file, 0);

	FILE *fp = fopen(buf, "w");
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
int __log_lock(int fd) 
{
	struct flock lock;
	lock.l_type = F_WRLCK;
	lock.l_start = 0;
	lock.l_len = 0;
	lock.l_whence = SEEK_SET;
  try_again: 
	if (fcntl(fd, F_SETLKW, &lock) < 0) {
		if (errno == EINTR) {
			goto try_again;
		}
		return -1;
	}
	return 0;
}

int __log_unlock(int fd) 
{
	struct flock lock;
	lock.l_type = F_UNLCK;
	lock.l_start = 0;
	lock.l_len = 0;
	lock.l_whence = SEEK_SET;
  try_again: 
	if (fcntl(fd, F_SETLK, &lock) < 0)  {
		if (errno == EINTR) {
			goto try_again;
		}
		return -1;
	}
	return 0;
}
/*
 * __log_time() -
 * Internal calling, 
 * caller should guarantee that the size of buffer @time_info is enough(size > 25 bytes).
 */
static inline int __log_time(char *time_info, size_t size)
{
	struct tm tm;
	time_t now = time(NULL);
	/*
	 * Format: Thu Jan 01 00:00:00 1970
	 */
	if (localtime_r(&now, &tm))
		return strftime(time_info, size, "%c ", &tm);
	else
		return snprintf(time_info, size, "%s ", "");
}
/*
 * generic_print() -
 * In case caller doesn't call or fail to call GENERIC_LOG_INIT().
 * Temporarily, only print 255 bytes instead.
 */
static inline void generic_print(const char *format, va_list ap)
{
	char buf[256];
	int size = __log_time(buf, 32);
	vsnprintf(buf + size, sizeof(buf) - size, format, ap);
	printf("%s", buf);
}
static void generic_log_print(const char *format, va_list ap)
{
	struct logger_struct *logger = LOGGER();
	__log_lock(logger->lock);
	FILE *io = fopen(logger->file_path, "a+");
	if (!io) {
		printf("Logger IO error.\n");
		goto out;
	}
	static char time[32];
	int size = __log_time(time, sizeof(time));
	
	fwrite(time, size, 1, io);
	vfprintf(io, format, ap);
	fflush(io);
	
	if (ftell(io) > logger->file_size) {
		if (logger->file_bakup > 0)
			__log_backup(logger);
		else
			ftruncate(fileno(io), 0);
	}
	
	fclose(io);
out:
	__log_unlock(logger->lock);
}
int GENERIC_LOG_INIT(const char *file_path, int file_size, int file_count)
{
	int lock = 0;
	struct logger_struct *logger = LOGGER();
	memset(logger, 0, sizeof(*logger));
	if (strlen(file_path) > LOG_FILE_PATH_MAX_SIZE) {
		printf("Log path is too long.\n");
		goto err;
	}
	strcpy(logger->file_path, file_path);
	lock = __log_lock_init(logger);
	if (lock < 0) {
		printf("Logger lock init failed.\n");
		goto err;
	}
	logger->file_size  = file_size;
	logger->file_bakup = file_count - 1;
	logger->lock	   = lock;
	printf("Logger init done, Log file path:%s, Log file size:%d.\n", 
		logger->file_path, logger->file_size);
	return 0;
err:
	if (lock > 0)
		close(lock);
	return -1;
}
void GENERIC_LOG(const char *format, ...)
{
	va_list ap;
	va_start(ap, format);
	if (logger_ready())
		generic_log_print(format, ap);
	else
		generic_print(format, ap);
	va_end(ap);
}
static void simple_log_print(const char *file_path, int file_size, const char *format, va_list ap)
{
	int fd;
	FILE *io = fopen(file_path, "a+");
	if (!io) {
		printf("Logger IO error.\n");
		return;
	}
	fd = fileno(io);
	__log_lock(fd);
	char time[32];
	int size = __log_time(time, sizeof(time));
	fwrite(time, size, 1, io);
	vfprintf(io, format, ap);
	fflush(io);

	if (ftell(io) > file_size) {
		__log_copy(io, file_path);
		ftruncate(fd, 0);
	}
	__log_unlock(fd);
	fclose(io);
}
void SIMPLE_LOG(const char *file_path, int file_size, const char *format, ...)
{
	va_list ap;
	va_start(ap, format);
	simple_log_print(file_path, file_size, format, ap);
	va_end(ap);
}
