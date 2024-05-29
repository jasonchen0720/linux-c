/*
 * Copyright (c) 2021, Jasonchen
 * Version: 0.0.1 - 20211103                
 * Author: Jie Chen <jasonchen0720@163.com>
 * brief : Implementation of JC-Posix timer.
 */
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
#include <stdlib.h>
#include <signal.h>
#include "generic_log.h"
#include "timer.h"

struct timer_base
{
	int 				state;
	int 				nthreads;
	int 				timerid;

	int 				idlethreads;
	pthread_cond_t 		exec_cond;
	pthread_cond_t 		work_cond;
	pthread_cond_t 		exit_cond;
	pthread_mutex_t 	mutex;

	struct list_head 	expired_timer;
	
	struct rb_tree 		*table;
	struct timer_struct *running_timer;
	struct timer_option *opt;
	pthread_attr_t 		tattr;
	pthread_condattr_t 	cattr;
};
struct timer_thread
{
	pthread_t			tid;
	struct timer_struct	*timer;
	struct timer_base 	*base;
};
enum TIMER_BASE_STATE {
	TMR_BASE_INITIAL,
	TMR_BASE_WAITING,
	TMR_BASE_TIMING,
	TMR_BASE_SHOOTING,
	TMR_BASE_DESTROY,
};

#define	TMR_F_ACTIVE 		1
#define	TMR_F_MOUNTED 		2
#define	TMR_F_PENDING		4
#define	TMR_F_EXECUTING		8
#define	TMR_F_WAITING		16
#define	TMR_F_DETACH		32
#define __LOG_FILE			"./timer.log"
#define __LOG_SIZE			1024 * 1024
#define __LOG_TAG			"timer"
static pthread_once_t __timer_init_once = PTHREAD_ONCE_INIT;
static struct timer_base __timer_base = {
	.state 		= TMR_BASE_INITIAL,
	.nthreads 	= 0,
	.mutex 		= PTHREAD_MUTEX_INITIALIZER,
};
#define current_base()	(&__timer_base)
#define working_base(base)	((base)->nthreads > 0 && (base)->state != TMR_BASE_DESTROY)
#define timespec_init(ts, ms)					\
do {											\
	(ts)->tv_sec  = (ms) / 1000;				\
	(ts)->tv_nsec = ((ms) % 1000) * 1000000;	\
} while (0)
#define timespec_add(a, b, r)					\
do {											\
	(r)->tv_sec  = (a)->tv_sec + (b)->tv_sec;	\
	(r)->tv_nsec = (a)->tv_nsec + (b)->tv_nsec;	\
	if ((r)->tv_nsec >= 1000000000) {			\
	    (r)->tv_sec++;							\
	    (r)->tv_nsec -= 1000000000;				\
	}											\
} while (0)

/* timespec_cmp()- compares the timer values in a and b using the comparison operator CMP, and returns true or false
 * depending on the result of the comparison. 
 * CMP of >=, <=, and == do not work; portable applications can instead use
 *
 *  !timespec_cmp(..., <)
 *  !timespec_cmp(..., >)
 *  !timespec_cmp(..., !=)
 */
#define timespec_cmp(a, b, CMP) ((a)->tv_sec CMP (b)->tv_sec || ((a)->tv_sec == (b)->tv_sec && (a)->tv_nsec CMP (b)->tv_nsec))
#define timespec_time(ts)				clock_gettime(CLOCK_MONOTONIC, ts) 
static int timer_comparator(const struct rb_node *n1, const struct rb_node *n2)
{
	const struct timer_struct *p1 = rb_entry(n1, struct timer_struct, node);
	const struct timer_struct *p2 = rb_entry(n2, struct timer_struct, node);

	if (timespec_cmp(&p1->expire, &p2->expire, <))
		return -1;
	if (timespec_cmp(&p1->expire, &p2->expire, >))
		return 1;
	return p1->timerid - p2->timerid;
}
static int timer_searcher(const void *data, const struct rb_node *n)
{
	const struct timer_struct *p1 = data;
	const struct timer_struct *p2 = rb_entry(n, struct timer_struct, node);

	if (timespec_cmp(&p1->expire, &p2->expire, <))
		return -1;
	if (timespec_cmp(&p1->expire, &p2->expire, >))
		return 1;
	return p1->timerid - p2->timerid;
}
static void timer_printer(const struct rb_node *n)
{
	if (!n)
		printf("\033[0mnil");
	else {
		const struct timer_struct *__pr = rb_entry(n, struct timer_struct, node);
		if (rb_is_red(n)) {
			printf("\033[31m%d-%ld", __pr->timerid, __pr->interval.tv_sec);
		} else {
			printf("\033[0m%d-%ld", __pr->timerid, __pr->interval.tv_sec);
		}
	}
	printf("\033[0m\n");
}
#define DEFAULT_TIMER_OPTION \
	{ /* linger */0, \
	  /* thread_min */2, \
	  /* thread_max */2, \
	  /* thread_stack_size 1M default */1024 * 1024, \
	}
static struct timer_option __timer_opts = DEFAULT_TIMER_OPTION;
static const struct timer_option *__users_opts = NULL;
static struct rb_tree __timer_table = {
	.root		= NULL,
	.comparator = timer_comparator,
	.searcher	= timer_searcher,
	.printer	= timer_printer,
	.rb_count	= 0,
};
static void thread_cleanup(void *arg)
{
	struct timer_thread *thread = arg;
	struct timer_base *base = thread->base;

	base->nthreads--;
	LOGI("thread:%lx cleanup nthreads:%d.", thread->tid, base->nthreads);
	if (base->nthreads == 0) {
		if (base->state == TMR_BASE_DESTROY) {
			pthread_cond_broadcast(&base->exit_cond);
		}
	}
	pthread_mutex_unlock(&base->mutex);
	free(thread);
}
static int timer_thread_create(struct timer_base *base, void *(*routine)(void *))
{
	struct timer_thread *thread = (struct timer_thread *)malloc(sizeof(struct timer_thread));

	if (!thread)
		return ENOMEM;
	thread->base = base;
	thread->timer = NULL;
	sigset_t set, oset;
	sigfillset(&set);
	pthread_sigmask(SIG_SETMASK, &set, &oset);
	int err = pthread_create(&thread->tid, &base->tattr, routine, thread);
	pthread_sigmask (SIG_SETMASK, &oset, NULL);
	if (err)
		free(thread);
	else
		base->nthreads++;
	LOGI("Create timer thread:%d, nthreads:%d.", err, base->nthreads);
	return err;
}
static inline void timer_insert(struct timer_struct *timer, struct timer_base *base)
{
	timespec_add(&timer->expire, &timer->interval, &timer->expire);
	/*
	 * Adding timeid to avoid conflict due to the same expiration time.
	 */
	timer->timerid = base->timerid++;
	//rb_print(base->table);
	rb_insert(&timer->node, base->table);
	timer->flags |= TMR_F_MOUNTED;

	LOGI("Inserted Timer-%d, interval:%ld", timer->timerid, timer->interval.tv_sec);
	//rb_print(base->table);
}
static inline void timer_remove(struct timer_struct *timer, struct timer_base *base)
{
	//rb_print(base->table);
	rb_remove(&timer->node, base->table);
	timer->flags &= ~TMR_F_MOUNTED;
	LOGI("Removed Timer-%d", timer->timerid);
	//rb_print(base->table);
}
static inline int timer_search(struct timer_struct *timer, struct timer_base *base)
{
	return rb_search(timer, base->table) == &timer->node;
}
static void timer_wait_cleanup(void *arg)
{
	struct timer_struct *timer = arg;
	struct timer_base	*base  = timer->base;

	pthread_cond_destroy(&timer->cond);
	pthread_mutex_unlock(&base->mutex);
}
static void timer_exit(struct timer_struct *timer, struct timer_base *base)
{
	timer->flags &= ~TMR_F_ACTIVE;
	
	if (timer->flags & TMR_F_EXECUTING) {
		if (pthread_equal(pthread_self(), ((struct timer_thread *)timer->thread)->tid)) {
			LOGI("Self delete, Timer:%d is executing, detached.", timer->timerid);
			timer->flags |= TMR_F_DETACH;
			goto out;
		}
	}
	/* Waiting */
	if (timer->flags & (TMR_F_PENDING | TMR_F_EXECUTING)) {
		pthread_cleanup_push(timer_wait_cleanup, timer);
		do {
			LOGI("I Waiting for timer...");
			timer->flags |= TMR_F_WAITING;
			pthread_cond_wait(&timer->cond, &base->mutex);
			LOGI("O Waiting for timer, flags:%04x...", timer->flags);
		} while (timer->flags & (TMR_F_PENDING | TMR_F_EXECUTING));
		pthread_cleanup_pop(0);
	}

	pthread_cond_destroy(&timer->cond);
out:
	pthread_mutex_unlock(&base->mutex);
}
static void timer_cleanup(void *arg)
{
	struct timer_thread *thread = arg;

	pthread_mutex_lock(&thread->base->mutex);

	thread->timer->flags &= ~TMR_F_EXECUTING;
	
	if (thread->timer->flags & TMR_F_WAITING) {
		LOGI("Wakeup waiting processes.");
		thread->timer->flags &= ~TMR_F_WAITING;
		pthread_cond_broadcast(&thread->timer->cond);
	}
	if (thread->timer->flags & TMR_F_DETACH) {
		thread->timer->flags &= ~TMR_F_ACTIVE;
		pthread_cond_destroy(&thread->timer->cond);
		LOGI("Detached Timer-%d deleted.", thread->timer->timerid);
	}
	
}
static void timer_execute(struct timer_struct *timer, struct timer_thread *thread)
{
	struct timer_base *base = thread->base;
	thread->timer = timer;
	timer->thread = thread;
	timer->flags |= TMR_F_EXECUTING;
	pthread_mutex_unlock(&base->mutex);
	
	pthread_cleanup_push(timer_cleanup, thread);
	LOGI("Executing timer-%d, flags:%04x, thread:%lx...", 
		timer->timerid, timer->flags, thread->tid);
	timer->func(timer->arg);
	pthread_cleanup_pop(1);
	
	LOGI("Executed timer-%d, flags:%04x, thread:%lx...", 
		timer->timerid, timer->flags, thread->tid);
	
}
static void *timer_executor(void *arg)
{
	int timedout = 0;
	struct timer_thread *self = arg;
	struct timer_base 	*base = self->base;

	assert(pthread_equal(self->tid, pthread_self()));
	
	pthread_mutex_lock(&base->mutex);
	
	pthread_cleanup_push(thread_cleanup, self);
	for (; base->state != TMR_BASE_DESTROY; ) {
		base->idlethreads++;

		if (list_empty(&base->expired_timer)) {
			LOGI("Expire Waiting, idlethreads:%d nthreads:%d, minthreads:%d, maxthreads:%d...", 
				base->idlethreads, base->nthreads, 
				base->opt->thread_min, base->opt->thread_max);
			if (base->nthreads <= base->opt->thread_min) {
				pthread_cond_wait(&base->exec_cond, &base->mutex);
			} else {
				if (base->opt->linger > 0) {
					struct timespec tspec;
					clock_gettime(CLOCK_MONOTONIC, &tspec);
					tspec.tv_sec += base->opt->linger;
					if (pthread_cond_timedwait(&base->exec_cond, &base->mutex, &tspec) == ETIMEDOUT)
						timedout = 1;
				} else 
					timedout = 1;
			}
		}
		
		base->idlethreads--;
		
		while (!list_empty(&base->expired_timer)) {
			struct timer_struct *timer = list_first_entry(&base->expired_timer, struct timer_struct, list);
			list_delete(&timer->list);
			assert(timer->flags & TMR_F_PENDING);
			timer->flags &= ~TMR_F_PENDING;
			timer_execute(timer, self);
		}
		if (timedout && 
			(base->nthreads > base->opt->thread_min))
			break;
	}
	LOGI("thread:%lx exiting...", self->tid);
	pthread_cleanup_pop(1);
	return NULL;
}
static int timer_thread_execute(struct timer_struct *timer, struct timer_base *base)
{
	int err = 0;
	timer->flags |= TMR_F_PENDING;
	
	list_add_tail(&timer->list, &base->expired_timer);
	LOGI("idlethreads:%d", base->idlethreads);
	if (base->idlethreads > 0)
		err = pthread_cond_signal(&base->exec_cond);
	else if (base->nthreads < base->opt->thread_max)
		err = timer_thread_create(base, timer_executor);
	return err;
}

static void * timer_inspector(void *arg)
{
	int err;
	struct rb_node *node;
	struct timespec current;
	
	struct timer_thread *self = arg;
	struct timer_base 	*base = self->base;

	assert(pthread_equal(self->tid, pthread_self()));
	pthread_mutex_lock(&base->mutex);
	
	pthread_cleanup_push(thread_cleanup, self);
	for (; base->state != TMR_BASE_DESTROY;) {
		node = rb_first(base->table);

		if (node == NULL) {
			LOGI("Waiting timer...");
			base->state = TMR_BASE_WAITING;
			pthread_cond_wait(&base->work_cond, &base->mutex);
			continue;
		}
		
		struct timer_struct *timer = rb_entry(node, struct timer_struct, node);
		timespec_time(&current);
		if (timespec_cmp(&current, &timer->expire, <)) {
			LOGI("timer-%d timedwaiting...", timer->timerid);
			base->state = TMR_BASE_TIMING;
			base->running_timer = timer;
			/*
			 * You donot have to check if the current time has already been passed. 
			 * Because pthread_cond_timedwait() Will return immediately, 
	   		 * if the absolute time specified by abstime has already been passed at the time of the call.
			 */
			err = pthread_cond_timedwait(&base->work_cond, 
										 &base->mutex, 
										 &base->running_timer->expire);
			/*
		 	 * In this case, Timer must have been removed.
		 	 */
			if (!base->running_timer) {
				LOGI("Removing err:%d...", err);
				continue;
			}
			base->running_timer = NULL;
			
			if (err != ETIMEDOUT) {
				LOGI("timedwaiting err:%d...", err);
				continue;
			}
		}
		LOGI("timer-%d expired, executing...", timer->timerid);
		base->state = TMR_BASE_SHOOTING;
		timer_remove(timer, base);
		if (timer->option & TMR_OPT_CYCLE)
			timer_insert(timer, base);
	
		if (timer->flags & (TMR_F_PENDING | TMR_F_EXECUTING))
			continue;

		
		if (timer->option & TMR_OPT_THREAD)
			timer_thread_execute(timer, base);
		else
			timer_execute(timer, self);
		
	}
	pthread_cleanup_pop(1);
	return NULL;
}

static void timer_reinit_atfork (void)
{
	struct timer_base *base = current_base();
	pthread_mutex_init(&base->mutex, NULL);
}

static void timer_init_once(void)
{
	struct timer_base *base = current_base();
	assert(!pthread_condattr_init(&base->cattr));
	assert(!pthread_condattr_setclock(&base->cattr, CLOCK_MONOTONIC));	
	assert(!pthread_cond_init(&base->work_cond, &base->cattr));
	assert(!pthread_cond_init(&base->exit_cond, &base->cattr));

	assert(!pthread_cond_init(&base->exec_cond, &base->cattr));
	INIT_LIST_HEAD(&base->expired_timer);
	base->table = &__timer_table;

	const struct timer_option *users_opt = __users_opts;
	struct timer_option *timer_opt = &__timer_opts;
	if (users_opt) {
		if (users_opt->linger >= 0)
			timer_opt->linger = users_opt->linger;
		if (users_opt->thread_min > 0)
			/* +1: Task timer_inspector() should not be counted. */
			timer_opt->thread_min = users_opt->thread_min + 1; 
		if (users_opt->thread_max > 0)
			/* +1: Task timer_inspector() should not be counted. */
			timer_opt->thread_max = users_opt->thread_max + 1; 
		if (users_opt->thread_stack_size > 0)
			timer_opt->thread_stack_size = users_opt->thread_stack_size;
		
		if (timer_opt->thread_min > timer_opt->thread_max)
			timer_opt->thread_min = timer_opt->thread_max;
	}
	
	base->opt = timer_opt;
	pthread_atfork(NULL, NULL, timer_reinit_atfork);
}
static void timer_destroy_cleanup(void *arg)
{
	struct timer_base *base = arg;
	struct timer_option opts = DEFAULT_TIMER_OPTION;
	memcpy(base->opt, &opts, sizeof(opts));

	pthread_attr_destroy(&base->tattr);
	base->table->root 		= NULL;
	base->table->rb_count 	= 0;
	base->state 			= TMR_BASE_INITIAL;
	pthread_mutex_unlock(&base->mutex);
	LOGI("timer destroy cleanup done.");
}
/*
 * timer_setup() - Setup TRE - Timer Runtime Environment.
 * @opt: Caller can set the attributes of thread pool using by timer.
 *		 default options will be used if null @opt passed.
 */
int timer_setup(const struct timer_option *opt)
{
	struct timer_base *base = current_base();

	pthread_mutex_lock(&base->mutex);

	__users_opts = opt;
	
	pthread_once(&__timer_init_once, timer_init_once);
	
	assert(base->nthreads == 0);
	assert(!pthread_attr_init(&base->tattr));
	assert(!pthread_attr_setstacksize(&base->tattr, base->opt->thread_stack_size));
	assert(!pthread_attr_setdetachstate(&base->tattr, PTHREAD_CREATE_DETACHED));
	
	base->timerid 		= 0;
	base->running_timer = NULL;
	base->nthreads		= 0;
	base->idlethreads	= 0;	
	if (timer_thread_create(base, timer_inspector) != 0) 
		goto err;
	LOGI("Timer setup done, options: %d-%d-%d-%u.", base->opt->linger, 
		base->opt->thread_min, 
		base->opt->thread_max, 
		base->opt->thread_stack_size / 1024);
	pthread_mutex_unlock(&base->mutex);
	return 0;
err:
	pthread_attr_destroy(&base->tattr);
	pthread_mutex_unlock(&base->mutex);
	return -1;
}

/*
 * Do not call timer_destroy() at the runtime execution context of the timer function.
 */
int timer_destroy()
{
	struct timer_base *base = current_base();

	pthread_mutex_lock(&base->mutex);
	
	if (!working_base(base))
		goto err;

	if (base->state == TMR_BASE_WAITING ||
		base->state == TMR_BASE_TIMING)
		pthread_cond_signal(&base->work_cond);

	if (base->idlethreads > 0)
		pthread_cond_broadcast(&base->exec_cond);

	base->state = TMR_BASE_DESTROY;
	pthread_cleanup_push(timer_destroy_cleanup, base);
	LOGI("Waiting Timer task to exit, state:%d...", base->state);
	while (base->nthreads > 0)
		pthread_cond_wait(&base->exit_cond, &base->mutex);
	
	pthread_cleanup_pop(1);

	LOGI("Timer destroy done, options: %d-%d-%d-%u.", base->opt->linger, 
												base->opt->thread_min, 
												base->opt->thread_max, 
												base->opt->thread_stack_size / 1024);
	return 0;
err:
	pthread_mutex_unlock(&base->mutex);
	return -1;

}
int timer_add(struct timer_struct *timer, int option, long msec,
	void (*func)(void *), void *arg)
{
	struct timer_base *base = current_base();
	pthread_mutex_lock(&base->mutex);
	if (!working_base(base))
		goto err;
	
	if (timer->flags & TMR_F_ACTIVE)
		goto err;
	
	if (timer->flags & TMR_F_MOUNTED && 
		timer_search(timer, base)) {
		LOGW("Timer already exited.");
		goto err;	
	}
	timer->option	= option;
	timer->func 	= func;
	timer->arg		= arg;
	timer->flags	= 0;
	timespec_init(&timer->interval, msec);
	timespec_time(&timer->expire);
	timer_insert(timer, base);
	timer->flags |= TMR_F_ACTIVE;

	timer->base		= base;//TODO
	pthread_cond_init(&timer->cond, &base->cattr);

	if (base->state == TMR_BASE_WAITING || 
		(base->running_timer != NULL && timespec_cmp(&timer->expire, &base->running_timer->expire, <))) {
		LOGI("Signal for adding...");
		pthread_cond_signal(&base->work_cond);
	}
	pthread_mutex_unlock(&base->mutex);
	return 0;
err:
	LOGI("Adding timer failed, base state:%d timer flags:%04x.", base->state, timer->flags);
	pthread_mutex_unlock(&base->mutex);
	return -1;
}
int timer_del(struct timer_struct *timer)
{
	struct timer_base *base = current_base();
	
	pthread_mutex_lock(&base->mutex);

	if (!working_base(base))
		goto err;
	
	/*
	 * In case of reentering when it is still waiting in last deleting operation. 
	 */
	if (!(timer->flags & TMR_F_ACTIVE))
		goto err;

	if (timer->flags & TMR_F_MOUNTED) {
		if (!timer_search(timer, base))
			goto err;
		timer_remove(timer, base);
		if (base->running_timer == timer) {
			base->running_timer = NULL;
			LOGI("Signal for removing timer-%d...", timer->timerid);
			pthread_cond_signal(&base->work_cond);
		}
	} 
	LOGI("Removing timer-%d flags:%04x.", timer->timerid, timer->flags);
	timer_exit(timer, base);
	LOGI("Remove timer-%d exit, flags:%04x", timer->timerid, timer->flags);
	return 0;
err:
	LOGW("Remove timer-%d error, flags:%04x", timer->timerid, timer->flags);
	pthread_mutex_unlock(&base->mutex);
	return -1;
}
int timer_mod(struct timer_struct *timer, long msec)
{
	struct timer_base *base = current_base();
	
	pthread_mutex_lock(&base->mutex);

	if (!working_base(base))
		goto err;
	
	if (!(timer->flags & TMR_F_ACTIVE))
		goto err;
	
	if (timer->flags & TMR_F_MOUNTED) {
		if (!timer_search(timer, base))
			goto err;
		timer_remove(timer, base);
	}
	
	timespec_init(&timer->interval, msec);
	timespec_time(&timer->expire);
	timer_insert(timer, base);
	if (base->running_timer == timer ||
		(base->running_timer != NULL && timespec_cmp(&timer->expire, &base->running_timer->expire, <)))
		pthread_cond_signal(&base->work_cond);
	
	pthread_mutex_unlock(&base->mutex);
	return 0;
err:
	pthread_mutex_unlock(&base->mutex);
	return -1;
}

