#ifndef __JC_TIMER_H__
#define __JC_TIMER_H__
#include <time.h>
#include "rb_tree.h"
#ifndef TMR_USE_THREAD_POOL
#include <pthread.h>
#include "list.h"
#endif
struct timer_struct
{
	struct timespec 	interval;
	struct timespec 	expire;
	int 				timerid;
	int					option;
	int 				flags;
	void 				*arg;
	void (*func)(void *);
#ifndef TMR_USE_THREAD_POOL
	void				*thread;
	pthread_cond_t		cond;
	struct list_head	list;
#endif
	struct rb_node 		node;
};
struct timer_option 
{
	int 	linger;
	int 	thread_min;
	int 	thread_max;
	size_t 	thread_stack_size;
};
enum TIMER_OPTION {
	TMR_O_CYCLE,
	TMR_O_THREAD,
};
#define TMR_OPT_CYCLE 		(1 << TMR_O_CYCLE)
#define TMR_OPT_THREAD 		(1 << TMR_O_THREAD)

/* Timer struct initializer */
#ifdef TMR_USE_THREAD_POOL
#define TIMER_INITIALIZER \
	{ /* interval */{0, 0}, \
	  /* expire */{0, 0}, \
	  /* timerid */0, \
	  /* option */0, \
	  /* flags */0, \
	  /* arg */(void *) 0, \
	  /* func */(void *) 0, \
	  /* node */{(void *) 0, (void *) 0, (void *) 0} \
	}
#else
#define TIMER_INITIALIZER \
	{ /* interval */{0, 0}, \
	  /* expire */{0, 0}, \
	  /* timerid */0, \
	  /* option */0, \
	  /* flags */0, \
	  /* arg */(void *) 0, \
	  /* func */(void *) 0, \
	  /* thread */(void *) 0, \
	  /* cond */PTHREAD_COND_INITIALIZER, \
	  /* list */{(void *) 0, (void *) 0}, \
	  /* node */{(void *) 0, (void *) 0, (void *) 0} \
	}
#endif

void timer_setopt(const struct timer_option *opt);
int timer_setup();
int timer_destroy();
int timer_add(struct timer_struct *timer, int option, long msec,
	void (*func)(void *), void *arg);
int timer_del(struct timer_struct *timer);
int timer_mod(struct timer_struct *timer, long msec);

#endif
