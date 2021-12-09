#include <fcntl.h>
#include <errno.h>

#include <sys/sem.h>

union semun 
{
    int              val;    /* Value for SETVAL */
    struct semid_ds *buf;    /* Buffer for IPC_STAT, IPC_SET */
    unsigned short  *array;  /* Array for GETALL, SETALL */
    struct seminfo  *__buf;  /* Buffer for IPC_INFO
                                (Linux specific) */
};

int fd_lock(int fd) 
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

int fd_unlock(int fd) 
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
int sem_create(const char *path)
{
    key_t key = ftok(path, 0x80);
    if (key == -1)
        return -1;

    int sem = semget(key, 1, IPC_CREAT | IPC_EXCL | 0666);
    if (sem == -1)
		return errno == EEXIST ? semget(key, 1, 0666) : -1;
	union semun arg;
	arg.val = 1;
	if (semctl(sem, 0, SETVAL, arg) == -1)
	    return -1;
    
    return sem;
}

int sem_lock(int sem)
{
	struct sembuf sop[1];
	sop[0].sem_num = 0;
	sop[0].sem_op = -1;
	sop[0].sem_flg = SEM_UNDO;
	int ret = 0;
	/*
	 * If an operation specifies SEM_UNDO, it will be automatically undone when the process terminates.
	 */
	do {
		ret = semop(sem, sop, 1);
	} while (ret < 0 && errno == EINTR);
	
	return ret;
}

int sem_unlock(int sem)
{
	struct sembuf sop[1];

	sop[0].sem_num = 0;
	sop[0].sem_op = 1;
	sop[0].sem_flg = SEM_UNDO;
	int ret = 0;

	do {
		ret = semop(sem, sop, 1);
	} while (ret < 0 && errno == EINTR);
	
	return ret;
}

