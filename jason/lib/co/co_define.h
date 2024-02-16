#ifndef __CO_DEFINE_H__
#define __CO_DEFINE_H__
#include "list.h"
#include "rb_tree.h"

struct co_context {
	void * sp;
};

struct co_struct
{
	long	id;
	void (*sleep) (struct co_struct *, int64_t);
	void (*yield) (struct co_struct *);
	void (*routine)(struct co_struct *);
	void * arg;
	size_t 	ssize;
	void *	stack;

	int		status;

	int64_t			 	sleep_expired_time;
	/* context */
	struct co_context 	 context;

	struct co_scheduler *scheduler;
	
	struct rb_node 		sleep_node;
	struct rb_node 		waits_node;
	struct list_head 	ready_list;
};

struct co_scheduler 
{
	long coid;
	long tid;
	const struct co_scheduler *next;

	void  *priv;
	void (*poll)(struct co_scheduler *);
	void (*resume)(struct co_struct *);
	//void (*yield) (struct co_struct *);
#if 0	
	int 				epfd;
	int					eventsize;
	struct epoll_event *eventlist;
#endif
	struct co_context 	 	context;
	struct co_struct	   *runningco;
	struct rb_tree 			sleep_tree;
	struct rb_tree 			waits_tree;
	struct list_head 		ready_head;
};
int co_init(struct co_struct *co, struct co_scheduler *scheduler, void (*routine)(struct co_struct *), void *arg);

int co_scheduler_init(struct co_scheduler *scheduler, void (*poll)(struct co_scheduler *), void *priv);
int co_scheduler_run(struct co_scheduler *scheduler);
#endif
