#ifndef __CO_TYPE_H__
#define __CO_TYPE_H__
#include <stdint.h>

#include "list.h"
#include "rb_tree.h"
enum CO_STATE {
	CO_READY,
	CO_RUNNING,
	CO_SLEEPING,
	CO_WAITING,
	CO_TIMEDWAIT,
	CO_EXITED,
};

struct co_context {
	void * sp;
};

struct co_struct
{
	long	id;
	//void (*sleep) (struct co_struct *, int64_t);
	//void (*yield) (struct co_struct *);
	void (*routine)(struct co_struct *);
	void * arg;
	size_t 	ssize;
	void *	stack;

	/*
	 * @state - values defined in enum CO_STATE.
	 */
	int		state;

	int64_t			 	sleep_expired_time;
	/* context */
	struct co_context 	 context;

	struct co_scheduler *scheduler;
	
	struct rb_node   sleep_node;
	//struct rb_node node;
	struct list_head list;
};

struct co_scheduler 
{
	long coid;
	//long tid;
	//const struct co_scheduler *next;

	void  *priv;

	/* 
	 * @schedret: Keep the latest scheduler->schedule() calling return 
	 */
	int   schedret;
	/* 
	 * prototype - int schedule(struct co_scheduler *scheduler) 
	 * returns: -1: error
	 *           0: timeout
	 *          >0: success
	 */
	int (*schedule)(struct co_scheduler *);
	//void (*resume)(struct co_struct *);
	//void (*yield) (struct co_struct *);
	struct co_context 	 	context;
	struct co_struct	   *runningco;
	struct rb_tree 			sleep_tree;
	struct list_head 		waits_list;
	struct list_head 		ready_list;
};
#endif
