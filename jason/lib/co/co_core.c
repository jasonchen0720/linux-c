#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <assert.h>
#include <stdint.h>
#include <time.h>


#include "co_log.h"
#include "co_define.h"

#define CO_MAX_STACKSIZE	(16 * 1024) 

#if defined(__i386__)
__asm__ (
	".text                                  \n"
	".p2align 2,,3                          \n"
	".globl co_switch                       \n"
	".type	co_switch, @function            \n"
	"co_switch:                             \n"
	"       pushl %ebp                      \n" /* save ebp */
	"       pushl %ebx                      \n" /* save ebx */
	"       pushl %edi                      \n" /* save edi */
	"       pushl %esi                      \n" /* save esi */
	"       movl 0x14(%esp), %edx           \n" /* @cur_ctx -> %edx*/
	"       movl %esp, (%edx)               \n"
	"       movl 0x18(%esp), %edx           \n" /* @new_ctx -> %edx*/
	"       movl (%edx), %esp               \n"
	"       popl %esi                       \n" /* restore esi */
	"       popl %edi                       \n" /* restore edi */
	"       popl %ebx                       \n" /* restore ebx */
	"       popl %ebp                       \n" /* restore ebp */
	"       ret                             \n"
);
/* ebp ebx edi esi */
#define CALLEE_SAVE_REGISTERS	4
#elif defined(__x86_64__) && defined(__x64_stack_arg__)
__asm__ (
	".text                                  \n"
	".p2align 4,,15                         \n"
	".globl co_switch                       \n"
	".type	co_switch, @function            \n"
	"co_switch:                             \n"
	"       pushq %rbp                      \n" /* save rbp */
	"       pushq %rbx                      \n" /* save rbx */
	"       pushq %r12                      \n" /* save r12 */
	"       pushq %r13                      \n" /* save r13 */
	"       pushq %r14                      \n" /* save r14 */
	"       pushq %r15                      \n" /* save r15 */
	"       movq %rsp, (%rdi)               \n" /* @cur_ctx */
	"       movq (%rsi), %rsp               \n" /* @new_ctx */
	"       popq %r15                       \n" /* restore r15 */
	"       popq %r14                       \n" /* restore r14 */
	"       popq %r13                       \n" /* restore r13 */
	"       popq %r12                       \n" /* restore r12 */
	"       popq %rbx                       \n" /* restore rbx */
	"       popq %rbp                       \n" /* restore rbp */
	"       ret                             \n"
);
/* rbp rbx r12 r13 r14 r15 */
#define CALLEE_SAVE_REGISTERS	6
#elif defined(__x86_64__)
__asm__ (
	".text                                  \n"
	".p2align 4,,15                         \n"
	".globl co_switch                       \n"
	".type	co_switch, @function            \n"
	"co_switch:                             \n"
	"       pushq %rdi                      \n" /* Not really needed, just for initial arg */
	"       pushq %rbp                      \n" /* save rbp */
	"       pushq %rbx                      \n" /* save rbx */
	"       pushq %r12                      \n" /* save r12 */
	"       pushq %r13                      \n" /* save r13 */
	"       pushq %r14                      \n" /* save r14 */
	"       pushq %r15                      \n" /* save r15 */
	"       movq %rsp, (%rdi)               \n" /* @cur_ctx */
	"       movq (%rsi), %rsp               \n" /* @new_ctx */
	"       popq %r15                       \n" /* restore r15 */
	"       popq %r14                       \n" /* restore r14 */
	"       popq %r13                       \n" /* restore r13 */
	"       popq %r12                       \n" /* restore r12 */
	"       popq %rbx                       \n" /* restore rbx */
	"       popq %rbp                       \n" /* restore rbp */
	"       popq %rdi                       \n" /* Not really needed, just for initial arg */
	"       ret                             \n" 
);
/* rdi(for initial arg) rbp rbx r12 r13 r14 r15 */
#define CALLEE_SAVE_REGISTERS	7
#elif defined(__arm__)
__asm__ (
);
#else
#error "Not supported on this arch."
#endif
static int64_t co_time() 
{
	struct timespec ts = {0};
	assert(clock_gettime(CLOCK_MONOTONIC, &ts) == 0);
	return ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}
int co_switch(struct co_context *cur_ctx, struct co_context *new_ctx);
void co_yield(struct co_struct *co)
{
	assert(co->scheduler->runningco == co);
	
	co_switch(&co->context,	&co->scheduler->context);
}
void co_resume(struct co_struct *co)
{
	co->scheduler->runningco = co;
	
	co_switch(&co->scheduler->context, &co->context);

	co->scheduler->runningco = NULL;
}
void co_sleep(struct co_struct *co, int64_t usec)
{
	assert(co->scheduler->runningco == co);
	if (usec > 0) {
		co->sleep_expired_time = co_time() + usec;
		struct rb_node *n = &co->sleep_node;
		while (n != rb_insert(n, &co->scheduler->sleep_tree))
			co->sleep_expired_time++;
	}
	co_yield(co);
}

static int exptime_comparator(const struct rb_node *n1, const struct rb_node *n2)
{
	struct co_struct *c1 = rb_entry(n1, struct co_struct, sleep_node);
	struct co_struct *c2 = rb_entry(n2, struct co_struct, sleep_node);

	return c1->sleep_expired_time > c2->sleep_expired_time;
}
static int exptime_searcher(const void *data, const struct rb_node *n)
{
	const struct co_struct *c1 = rb_entry(n, struct co_struct, sleep_node);
	const struct co_struct *c2 = (const struct co_struct *)data;
	return c1->sleep_expired_time > c2->sleep_expired_time;
}
static void exptime_printer(const struct rb_node *n)
{
	if (n) {
		const struct co_struct *co = rb_entry(n, struct co_struct, sleep_node);
		printf(rb_is_red(n) ? "\033[31m%lld" : "\033[0m%lld", (long long)co->sleep_expired_time);
	} else
		printf("\033[0mnil");
	
	printf("\033[0m\n");
}
static int64_t next_timeout(struct co_scheduler *scheduler)
{
	struct rb_node * n = rb_first(&scheduler->sleep_tree);
	if (n) {
		struct co_struct * co = rb_entry(n, struct co_struct, sleep_node);
		int64_t tmo = co->sleep_expired_time - co_time();
		return tmo > 0 ? tmo : 0;
	}
	return -1;
}
/*
 * schedule_expired - return next timeout duration
 */
static void schedule_expired(struct co_scheduler *scheduler)
{
	struct rb_node *n;
	struct co_struct *co;
	for (n = rb_first(&scheduler->sleep_tree); n ; n = rb_next(n)) {
		co = rb_entry(n, struct co_struct, sleep_node);
		int64_t diff = co->sleep_expired_time - co_time();
		LOG("co->sleep_expired_time: %ld, diff: %ld", co->sleep_expired_time, diff);
		if (diff <= 0) {
			rb_remove(n, &scheduler->sleep_tree);
			co_resume(co);
		}
	}
}
static void schedule_ready(struct co_scheduler *scheduler)
{
	struct co_struct *co, *t;
	list_for_each_entry_safe(co, t, &scheduler->ready_head, ready_list) {
		list_del(&co->ready_list);
		co_resume(co);
	}
}

int co_scheduler_init(struct co_scheduler *scheduler, int (*schedule)(struct co_scheduler *, int64_t), void *priv)
{	
	INIT_LIST_HEAD(&scheduler->ready_head);
	scheduler->coid 		= 0L;
	scheduler->runningco 	= NULL;
	//scheduler->yield		= co_yield;
	//scheduler->resume		= co_resume;
	scheduler->priv			= priv;
	scheduler->schedule		= schedule;
	rb_tree_init(&scheduler->sleep_tree, exptime_comparator, exptime_searcher, exptime_printer);
	return 0;
}
int co_scheduler_run(struct co_scheduler *scheduler)
{
	while (1) {
		schedule_expired(scheduler);
		schedule_ready(scheduler);
		scheduler->schedule(scheduler, next_timeout(scheduler));
	}
	return 0;
}

#if defined(__i386__)
static void co_dummy_ret()
{
	assert(0);
}
#endif

static void co_entry(struct co_struct *co)
{
#if defined(__x64_stack_arg__)
	__asm__ volatile("movq 8(%%rbp), %0;" : "=r"(co));
#endif
	LOG("co@%p ID[%ld] running.", co, co->id);
	co->routine(co);
	LOG("co@%p ID[%ld] exited.", co, co->id);

	co_yield(co);
}

int co_init(struct co_struct *co, struct co_scheduler *scheduler, void (*routine)(struct co_struct *), void *arg)
{
	void *stack = NULL;
	void **new_stack = NULL;
	int err = posix_memalign(&stack, getpagesize(), CO_MAX_STACKSIZE);
	if (err) {
		printf("Failed to allocate stack for new coroutine\n");
		return -1;
	}
	
	new_stack = (void **)(stack + CO_MAX_STACKSIZE);

	/* 
	 * Below are register values stored on its stack.
	 */
#if defined(__i386__)
	/*
	 *  i386 :
	 *      new_stack[-1] = arg
	 * 		new_stack[-2] = ret 
	 * 		new_stack[-3] = eip
	 *      new_stack[-4] = %ebp
	 * 		new_stack[-5] = %ebx 
	 * 		new_stack[-6] = %edi 
	 * 		new_stack[-7] = %esi 
	 */
	new_stack[-1] = (void *)co;				/* arg */
	new_stack[-2] = (void *)co_dummy_ret;	/* ret */
	new_stack[-3] = (void *)co_entry;		/* eip */
	new_stack[-4] = (void *)new_stack;		/* ebp */ /* new_stack[-4] = (void *)new_stack - (3 * sizeof(void*)); */
	co->context.sp  = (void *)new_stack - ((CALLEE_SAVE_REGISTERS + 3) * sizeof(void *));
#elif defined(__x86_64__) && defined(__x64_stack_arg__)
	/*
	 *  x86_64 :
	 *      new_stack[-1] = arg
	 * 		new_stack[-2] = eip
	 *      new_stack[-3] = %rbp
	 * 		new_stack[-4] = %rbx 
	 * 		new_stack[-5] = %r12 
	 * 		new_stack[-6] = %r13 
	 * 		new_stack[-7] = %r14 
	 * 		new_stack[-8] = %r15 
	 */
	new_stack[-1] = (void *)co;				/* arg */
	new_stack[-2] = (void *)co_entry;		/* eip */
	new_stack[-3] = (void *)new_stack;		/* rbp */ /* new_stack[-3] = (void *)new_stack - (2 * sizeof(void*)); */
	co->context.sp  = (void *)new_stack - ((CALLEE_SAVE_REGISTERS + 2) * sizeof(void *));
#elif defined(__x86_64__)
	/*
	 *  x86_64 :
	 *      new_stack[-1] = eip
	 * 		new_stack[-2] = %rdi <- arg
	 *      new_stack[-3] = %rbp
	 * 		new_stack[-4] = %rbx 
	 * 		new_stack[-5] = %r12 
	 * 		new_stack[-6] = %r13 
	 * 		new_stack[-7] = %r14 
	 * 		new_stack[-8] = %r15 
	 */
	new_stack[-1] = (void *)co_entry;		/* eip */
	new_stack[-2] = (void *)co; 			/* rdi */
	new_stack[-3] = (void *)new_stack;		/* rbp */ /* new_stack[-3] = (void *)new_stack - (1 * sizeof(void*)); */
	co->context.sp  = (void *)new_stack - ((CALLEE_SAVE_REGISTERS + 1) * sizeof(void *));
#else
	#error "Not supported on this arch."
#endif	
	co->id 			= scheduler->coid++;
	co->scheduler 	= scheduler;
	co->ssize		= CO_MAX_STACKSIZE;
	co->stack 		= stack;
	co->arg			= arg;
	co->routine		= routine;
	//co->yield		= co_yield;
	//co->sleep		= co_sleep;
	list_add_tail(&co->ready_list, &scheduler->ready_head);
	LOG("co@%p initialized.", co);
	return 0;
}
