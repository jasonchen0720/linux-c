#ifndef __CO_CORE_H__
#define __CO_CORE_H__
#include "co_type.h"

int co_init(struct co_struct *co, struct co_scheduler *scheduler, 
			void (*routine)(struct co_struct *), void *arg, size_t stacksize);
int64_t co_time();
int64_t co_nexttmo(struct co_scheduler *scheduler);
/*
 * co_transfer() - transfer to the given coroutine state.
 * @co        - co
 * @state     - values defined in enum CO_STATE.
 */
void co_transfer(struct co_struct *co, int state);
void co_yield(struct co_struct *co);
void co_sleep(struct co_struct *co, int64_t usec);
int co_scheduler_init(struct co_scheduler *scheduler, 
		int  (*schedule)(struct co_scheduler *), void *priv, 
		void (*release)(struct co_struct *));
int co_scheduler_run(struct co_scheduler *scheduler);

#endif
