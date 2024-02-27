#ifndef __THREAD_POOL_H__
#define __THREAD_POOL_H__
#include <pthread.h>
#include "list.h"

struct thread_pool
{
	int maxthreads;
	int minthreads;

	int nthreads;
	int idlethreads;

	int keepalive;
	
	int flags;

	struct list_head job_head;	
	struct list_head active_thread_head;

	pthread_attr_t attr;
	
	pthread_mutex_t mutex;
	
	pthread_cond_t		work_cond;
	pthread_cond_t  	wait_cond;
	pthread_cond_t  	exit_cond;
};
int thread_pool_init(struct thread_pool * pool, int keepalive, 
	int minthreads, 
	int maxthreads, const pthread_attr_t *attr);
void thread_pool_destroy(int defer, struct thread_pool *pool);
int thread_pool_execute(struct thread_pool *pool, void (*func)(void *), void *arg);

#endif
