/*
 * Copyright (c) 2021, Jasonchen
 * Version: 0.0.1 - 20211103                
 * Author: Jie Chen <jasonchen0720@163.com>
 * Note  : Implementation of JC easy thread pool.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <assert.h>
#include "thread_pool.h"
#include "logger.h"

static pthread_condattr_t __cond_attr;
static pthread_once_t __init_once = PTHREAD_ONCE_INIT;
static sigset_t __full_sigset;
#define LOG_FILE			"./thread-pool.log"
#define LOG_SIZE			1024 * 1024
#define LOG_TAG				"thread-pool"

struct thread_struct
{
	pthread_t tid;
	struct thread_pool *pool;
	struct list_head list;
};
struct thread_job
{
	void *arg;
	void (*func)(void *);
	struct list_head list;
};
enum {
	THREAD_POOL_DESTROY,
	THREAD_POOL_WAIT,
};
#define BIT(nr) (1U << (nr))


static void thread_attr_init(pthread_attr_t *new_attr, const pthread_attr_t *old_attr)
{
	assert(!pthread_attr_init(new_attr));	
	assert(!pthread_attr_setdetachstate(new_attr, PTHREAD_CREATE_DETACHED));
	if (old_attr) {
		size_t size;
		//void *stackaddr;
		//pthread_attr_getstack(old_attr, &stackaddr, &size);
		//pthread_attr_setstack(new_attr, stackaddr, size);
		pthread_attr_getstacksize(old_attr, &size);
		LOGI("stack size:%lu KB", size / 1024);
		pthread_attr_setstacksize(new_attr, size);
		
		pthread_attr_getguardsize(old_attr, &size);
		pthread_attr_setguardsize(new_attr, size);

		int value;
		pthread_attr_getinheritsched(old_attr, &value);
		pthread_attr_setinheritsched(new_attr, value);
		
		pthread_attr_getschedpolicy(old_attr, &value);
		pthread_attr_setschedpolicy(new_attr, value);

		pthread_attr_getscope(old_attr, &value);
		pthread_attr_setscope(new_attr, value);
	}
}
void thread_pool_wait(struct thread_pool *pool)
{
	pthread_mutex_lock(&pool->mutex);

	pthread_cleanup_push(pthread_mutex_unlock, &pool->mutex);
	
	while (!list_empty(&pool->job_head) || !list_empty(&pool->active_thread_head)) {
		pool->flags |= BIT(THREAD_POOL_WAIT);
		pthread_cond_wait(&pool->wait_cond, &pool->mutex);
	}
	pthread_cleanup_pop(1);
}
static void thread_pool_wakeup(struct thread_pool *pool) {
	
	if (list_empty(&pool->job_head) && list_empty(&pool->active_thread_head)) {
		pool->flags &= ~(BIT(THREAD_POOL_WAIT));
		pthread_cond_broadcast(&pool->wait_cond);
	}
}
static void thread_pool_setup()
{
	assert(!pthread_condattr_init(&__cond_attr));
	assert(!pthread_condattr_setclock(&__cond_attr, CLOCK_MONOTONIC));
	sigfillset(&__full_sigset);
	LOGI("Thread pool global init completely.");
}
int thread_pool_init(struct thread_pool * pool, int keepalive, 
	int minthreads, 
	int maxthreads, const pthread_attr_t *attr)
{
	if (pool == NULL ||
		minthreads < 0 || 
		minthreads > maxthreads) {
		LOGE("Invalid arg, minthreads:%d, maxthreads:%d.", minthreads, maxthreads);
		return -1;
	}
	assert(!pthread_once(&__init_once, thread_pool_setup));
	memset(pool, 0, sizeof(*pool));
	pool->minthreads 	= minthreads;
	pool->maxthreads 	= maxthreads;
	pool->keepalive 	= keepalive;
	pool->flags 		= 0;
	pool->nthreads 		= 0;
	pool->idlethreads 	= 0;

	assert(!pthread_mutex_init(&pool->mutex, NULL));
	assert(!pthread_cond_init(&pool->work_cond, &__cond_attr));
	assert(!pthread_cond_init(&pool->wait_cond, &__cond_attr));
	assert(!pthread_cond_init(&pool->exit_cond, &__cond_attr));
	thread_attr_init(&pool->attr, attr);
	INIT_LIST_HEAD(&pool->job_head);
	INIT_LIST_HEAD(&pool->active_thread_head);
	LOGI("Thread pool create done.");
	return 0;
}
void thread_pool_destroy(int defer, struct thread_pool *pool)
{
	pthread_mutex_lock(&pool->mutex);

	pool->flags |= BIT(THREAD_POOL_DESTROY);
	if (pool->idlethreads > 0)
		pthread_cond_broadcast(&pool->work_cond);

	if (!defer) {
		struct thread_struct *thr;
		list_for_each_entry(thr, &pool->active_thread_head, list) {
			pthread_cancel(thr->tid);
		}
	}
	while (pool->nthreads > 0)
		pthread_cond_wait(&pool->exit_cond, &pool->mutex);

	struct thread_job *job, *tmp;
	list_for_each_entry_safe(job, tmp, &pool->job_head, list) {
		free(job);
	}
	pthread_mutex_unlock(&pool->mutex);

	pthread_mutex_destroy(&pool->mutex);
	pthread_cond_destroy(&pool->work_cond);
	pthread_cond_destroy(&pool->wait_cond);
	pthread_cond_destroy(&pool->exit_cond);
}
static void thread_task_cleanup(void *arg)
{
	struct thread_pool *pool = arg;

	pool->nthreads--;

	if (pool->flags & BIT(THREAD_POOL_DESTROY)) {
		if (pool->nthreads == 0) {
			pthread_cond_broadcast(&pool->exit_cond);
		}
	} 
	pthread_mutex_unlock(&pool->mutex);
	LOGI("Thread cleanup: %d.", pool->nthreads);
}
static void thread_job_cleanup(void *arg)
{
	struct thread_struct *thread = arg;
	pthread_mutex_lock(&thread->pool->mutex);

	list_delete(&thread->list);

	if (thread->pool->flags & BIT(THREAD_POOL_WAIT)) 
		thread_pool_wakeup(thread->pool);
	
	//LOGI("Thread job cleanup: %d.", thread->pool->nthreads);
}
static void thread_idle_cleanup(void *arg)
{
	struct thread_pool *pool = arg;
	pool->idlethreads--;
}
static void *thread_task(void *arg)
{
	int timedout = 0;
	struct thread_pool *pool = arg;

	struct thread_struct thread;

	thread.tid = pthread_self();
	thread.pool = pool;
	INIT_LIST_HEAD(&thread.list);

	pthread_detach(thread.tid);
	pthread_sigmask(SIG_SETMASK, &__full_sigset, NULL);
	pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, NULL);
	pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
	
	pthread_mutex_lock(&pool->mutex);
	pthread_cleanup_push(thread_task_cleanup, pool);
	while (1) {

		pool->idlethreads++;
		
		pthread_cleanup_push(thread_idle_cleanup, pool);	
		
		while (list_empty(&pool->job_head)) {
			if (pool->flags & BIT(THREAD_POOL_DESTROY))
				break;
			LOGI("idlethreads:%d, nthreads:%d, minthreads:%d.", pool->idlethreads, pool->nthreads, pool->minthreads);
			if (pool->nthreads <= pool->minthreads) {
				pthread_cond_wait(&pool->work_cond, &pool->mutex);
			} else {
				if (pool->keepalive > 0){
					struct timespec tspec;
					clock_gettime(CLOCK_MONOTONIC, &tspec);
					tspec.tv_sec += pool->keepalive;
					if (pthread_cond_timedwait(
						&pool->work_cond, &pool->mutex, &tspec) != ETIMEDOUT)
						continue;
				}
				timedout = 1;
				break;
			}
		}
		
		pthread_cleanup_pop(0);
		
		pool->idlethreads--;
		
		if (pool->flags & BIT(THREAD_POOL_DESTROY))
			break;	
		
		if (!list_empty(&pool->job_head)) {
			struct thread_job *job = list_first_entry(&pool->job_head, struct thread_job, list);
			list_delete(&job->list);

			void (*func)(void *) = job->func;
			void * arg = job->arg;
			
			list_add_tail(&thread.list, &pool->active_thread_head);
			pthread_mutex_unlock(&pool->mutex);
			
			pthread_cleanup_push(thread_job_cleanup, &thread);
			free(job);
			func(arg);
			pthread_cleanup_pop(1);
		}

		if (timedout && (pool->nthreads > pool->minthreads))
			break;
	}
	pthread_cleanup_pop(1);
	LOGI("Thread returning: %d.", pool->nthreads);
	return NULL;	
}
static int thread_create(struct thread_pool *pool)
{
	sigset_t oset;
	pthread_t thread_id;
	pthread_sigmask(SIG_SETMASK, &__full_sigset, &oset);
	int error = pthread_create(&thread_id, &pool->attr, thread_task, pool);
	pthread_sigmask(SIG_SETMASK, &oset, NULL);

	return error;
}
int thread_pool_execute(struct thread_pool *pool, void (*func)(void *), void *arg)
{
	struct thread_job *job = (struct thread_job *)malloc(sizeof(struct thread_job));

	if (!job) {
		LOGE("No memory.");
		return -1;
	}

	job->func = func;
	job->arg  = arg;

	pthread_mutex_lock(&pool->mutex);
	list_add_tail(&job->list, &pool->job_head);

	if (pool->idlethreads > 0) {
		pthread_cond_signal(&pool->work_cond);
	} else if (pool->nthreads < pool->maxthreads && thread_create(pool) == 0) {
		pool->nthreads++;
	}
	pthread_mutex_unlock(&pool->mutex);
	return 0;
}
