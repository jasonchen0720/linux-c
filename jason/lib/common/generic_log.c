#include <stdio.h>
#include <stdarg.h>
#include <fcntl.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>	
#include <limits.h>
#include <stdlib.h>
#include <sys/sem.h>
#include <sys/syscall.h>
#define gettid()				syscall(__NR_gettid)
#define LOG_PATH				"/var/volatile/log"
#define LOG_FILE_PATH_MAX		255
#define LOG_LINE_MAX			1024
union semun {
    int              val;    /* Value for SETVAL */
    struct semid_ds *buf;    /* Buffer for IPC_STAT, IPC_SET */
    unsigned short  *array;  /* Array for GETALL, SETALL */
    struct seminfo  *__buf;  /* Buffer for IPC_INFO
                                (Linux specific) */
};
struct logger_struct
{
	int		file_size;
	int		file_bakup;
	char 	file_path[LOG_FILE_PATH_MAX + 1];
};
static struct logger_struct __logger = {
	.file_size 	= 0,
	.file_bakup	= 0,
	.file_path	= {0},
};
#define LOCK_INVALID		-1
#define LOCK_INITING		-2
static int __lock = LOCK_INVALID;
#define LOGGER() 	(&__logger)
#if 0
#define lock_cas(old_val, new_val) ({int __b = __lock == (old_val); if (__b) __lock = (new_val); __b; })
#else
#define lock_cas(old_val, new_val) (__sync_bool_compare_and_swap((&__lock), (old_val), (new_val)))
#endif

/*
 * log_format() -
 * Internal calling, 
 * caller should guarantee that the size of buffer @buf is enough(size > 25 bytes).
 */
static int log_format(char buf[LOG_LINE_MAX], const char *format, va_list ap)
{
	int offs = 0;
	struct tm tm;
	time_t now = time(NULL);
	if (localtime_r(&now, &tm))
		offs = strftime(buf, 32, "%c ", &tm);
	else
		offs = snprintf(buf, 32, "%s ", "");
	
	offs += vsnprintf(buf + offs, LOG_LINE_MAX - offs, format, ap);
	if (offs >= LOG_LINE_MAX) {
		offs  = LOG_LINE_MAX;
		buf[offs - 1] = '\n';
	}
	return offs;
}

static const char * log_pname(char name[17])
{
	int fd = open("/proc/self/comm", O_RDONLY);
	if (fd < 0)
		goto err;
	int size = read(fd, name, 16);
	if (size <= 0) {
		close(fd);
		goto err;
	}
	close(fd);
	if (name[size - 1] == '\n')
		name[size - 1] = '\0';
	else
		name[size] = '\0';
	return (const char *)name;
err:
	sprintf(name, "%ld", gettid());
	return (const char *)name; 
}

static void log_debug(const char *format, ...)
{
	FILE *io = fopen(LOG_PATH"/samlog.lk", "a+");
	if (!io) {
		printf("samlog.log IO error.\n");
		return;
	}
	if (ftell(io) < 64 * 1024) {
		va_list ap;
		va_start(ap, format);
		char buf[LOG_LINE_MAX];
		int size = log_format(buf, format, ap);
		fwrite(buf, size, 1, io);
		fflush(io);
		va_end(ap);
	}
	fclose(io);
}
#define LOG(format,...) log_debug("tid:%d "format"\n", gettid(), ##__VA_ARGS__)
static int lock_init()
{
	char comm[17];
	/* Process name which is trying to hold lock. */
	log_pname(comm);

	/* Thread-safe protection, set the lock status as initializing(LOCK_INITING) atomically. */
	int doing = !lock_cas(LOCK_INVALID, LOCK_INITING);
	if (doing) {
		LOG("%s init:%d", comm, EINPROGRESS);
		return -1;
	}
	int sem = -1;
    key_t key = ftok(LOG_PATH, 0x80);
    if (key == -1) {
		LOG("%s ftok:%d", comm, errno);
        goto out;
    }
    sem = semget(key, 1, IPC_CREAT | IPC_EXCL | 0666);
    if (sem == -1) {
		if (errno == EEXIST) {
			sem = semget(key, 1, 0666);
			LOG("%s key:0x%x sem:%d", comm, key, sem);
		} else 
			LOG("%s semget:%d", comm, errno);
		goto out;
    }
	union semun arg = {.val = 1};
	if (semctl(sem, 0, SETVAL, arg) == -1) {
		LOG("%s semctl:%d", comm, errno);
	    sem = -1;
	} else
		LOG("%s key:0x%x sem:%d", comm, key, sem);
out:
	lock_cas(LOCK_INITING, sem);
	LOG("%s lock:%d", comm, __lock);
    return sem;
}

static int log_lock() 
{
	if (__lock < 0) {
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
		ret = semop(__lock, sop, 1);
	} while (ret < 0 && errno == EINTR);
#else
	struct timespec ts;
	ts.tv_sec	= 1; 
	ts.tv_nsec	= 0; 
	do {
		ret = semtimedop(__lock, sop, 1, &ts);
	} while (ret < 0 && errno == EINTR);
#endif
	
	return ret;
}

static int log_unlock() 
{
	if (__lock < 0)
		return -1;
	
	struct sembuf sop[1];

	sop[0].sem_num = 0;
	sop[0].sem_op = 1;
	sop[0].sem_flg = SEM_UNDO;
	int ret = 0;

	do {
		ret = semop(__lock, sop, 1);
	} while (ret < 0 && errno == EINTR);
	
	return ret;
}
static void log_backup(int fd, int file_bakup, const char *file_path)
{
	if (log_lock() == -1)
		return;
	
	char s[32];
	char file[2][PATH_MAX + 1] = {{0}, {0}};
	
	sprintf(s, "/proc/self/fd/%d", fd);
	/* File may by renamed by other process, get its link first */
	readlink(s, file[0], PATH_MAX);
	/* Get its absolute pathname */
	realpath(file_path, file[1]);
	
	if (strcmp(file[0], file[1])) {
		goto out;
	}
	
	int i = file_bakup - 1;
	int n = i & 1;
	
	sprintf(file[n], "%s.%d", file_path, i);
	for (i--; i >= 0; i--) {
		n = i & 1;
		sprintf(file[n], "%s.%d", file_path, i);
		if (access(file[n], F_OK) == 0)
			rename(file[n], file[!n]);
	}
	rename(file_path, file[n]);
out:
	log_unlock();
}

/*
 * generic_print() -
 * In case caller doesn't call or fail to call GENERIC_LOG_INIT().
 * Temporarily, only print 255 bytes instead.
 */
static void generic_print(const char *format, va_list ap)
{
	char buf[LOG_LINE_MAX];
	log_format(buf, format, ap);
	printf("%s", buf);
}
static void generic_log_print(const char *format, va_list ap)
{
	struct logger_struct *logger = LOGGER();
	FILE *io = fopen(logger->file_path, "a+");
	if (!io) {
		printf("Logger IO error.\n");
		return;
	}
	char buf[LOG_LINE_MAX];
	int size = log_format(buf, format, ap);
	fwrite(buf, size, 1, io);
	fflush(io);
	
	if (ftell(io) > logger->file_size) {
		if (logger->file_bakup > 0)
			log_backup(fileno(io), logger->file_bakup, logger->file_path);
		else
			ftruncate(fileno(io), 0);
	}
	
	fclose(io);
}
int GENERIC_LOG_INIT(const char *file_path, int file_size, int file_count)
{
	struct logger_struct *logger = LOGGER();
	if (strlen(file_path) > LOG_FILE_PATH_MAX) {
		printf("Log path is too long.\n");
		goto err;
	}
	strcpy(logger->file_path, file_path);
	logger->file_size  = file_size;
	logger->file_bakup = file_count - 1;
	printf("Logger init done, Log file path:%s, Log file size:%d.\n", 
		logger->file_path, logger->file_size);
	return 0;
err:
	return -1;
}
void GENERIC_LOG(const char *format, ...)
{
	va_list ap;
	va_start(ap, format);
	if (LOGGER()->file_size > 0)
		generic_log_print(format, ap);
	else
		generic_print(format, ap);
	va_end(ap);
}

void SIMPLE_LOG(const char *file_path, int file_size, const char *format, ...)
{
	FILE *io = fopen(file_path, "a+");
	if (!io) {
		printf("Logger IO error.\n");
		return;
	}
	va_list ap;
	va_start(ap, format);
	char buf[LOG_LINE_MAX];
	int size = log_format(buf, format, ap);
	fwrite(buf, size, 1, io);
	fflush(io);

	if (ftell(io) > file_size) {
		log_backup(fileno(io), 1, file_path);
	}
	fclose(io);
	va_end(ap);
}

