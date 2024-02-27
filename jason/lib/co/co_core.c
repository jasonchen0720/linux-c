#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <assert.h>
#include <stdint.h>
#include <time.h>


#include "co_log.h"
#include "co_type.h"

/*
 * CO state queue
 */
#define stq_any(q)     (!list_empty(q))
#define stq_init(q)    INIT_LIST_HEAD(q);
#define stq_del(co)    list_del_init(&(co)->list)
#define stq_add(co, q) list_add_tail(&(co)->list, q)
#define stq_first(q)   list_first_entry(q, struct co_struct, list)

void co_switch(struct co_context *cur_ctx, struct co_context *new_ctx);

#if defined(__i386__)
/*
 * x86 callee save registers: ebp ebx edi esi 
 */
__asm__ (
	".text                        \n"
	".p2align 2,,3                \n"
	".globl co_switch             \n"
	".type	co_switch, @function  \n"
	"co_switch:                   \n"
	"    pushl %ebp               \n" /* save ebp */
	"    pushl %ebx               \n" /* save ebx */
	"    pushl %edi               \n" /* save edi */
	"    pushl %esi               \n" /* save esi */
	"    movl 0x14(%esp), %edx    \n" /* @cur_ctx -> %edx*/
	"    movl %esp, (%edx)        \n"
	"    movl 0x18(%esp), %edx    \n" /* @new_ctx -> %edx*/
	"    movl (%edx), %esp        \n"
	"    popl %esi                \n" /* restore esi */
	"    popl %edi                \n" /* restore edi */
	"    popl %ebx                \n" /* restore ebx */
	"    popl %ebp                \n" /* restore ebp */
	"    ret                      \n"
);

#elif defined(__x86_64__)
/*
 * x64 callee save registers: rbp rbx r12 r13 r14 r15 
 */
__asm__ (
	".text                        \n"
	".p2align 4,,15               \n"
	".globl co_switch             \n"
	".type	co_switch, @function  \n"
	"co_switch:                   \n"
#if !defined(__argoverstack__)
	"    pushq %rdi               \n" /* Not really needed, just for initial arg */
#endif
	"    pushq %rbp               \n" /* save rbp */
	"    pushq %rbx               \n" /* save rbx */
	"    pushq %r12               \n" /* save r12 */
	"    pushq %r13               \n" /* save r13 */
	"    pushq %r14               \n" /* save r14 */
	"    pushq %r15               \n" /* save r15 */
	"    movq %rsp, (%rdi)        \n" /* @cur_ctx */
	"    movq (%rsi), %rsp        \n" /* @new_ctx */
	"    popq %r15                \n" /* restore r15 */
	"    popq %r14                \n" /* restore r14 */
	"    popq %r13                \n" /* restore r13 */
	"    popq %r12                \n" /* restore r12 */
	"    popq %rbx                \n" /* restore rbx */
	"    popq %rbp                \n" /* restore rbp */
#if !defined(__argoverstack__)
	"    popq %rdi                \n" /* Not really needed, just for initial arg */
#endif
	"    retq                     \n" 
);
#elif defined(__arm__)
/*
 * Thumb Mode:
 *            Registers R4~R7 are usually used.
 *            In different standards, R9~R11 may have other functions
 *
 * r7:  FP - Frame pointer
 * r13: SP - Stack pointer
 * r14: LR - Link register
 * r15: PC - Program counter
 *
 * ARM Mode:
 * r11: FP - Frame pointer
 * r12: IP - Intra-Procedure-call scratch register
 * r13: SP - Stack pointer
 * r14: LR - Link register
 * r15: PC - Program counter
 *
 */
__asm__ (
	".text                        \n"
#if defined(__thumb__)
	".align 1                     \n"
#else
	".align 2                     \n"
#endif
	".globl co_switch             \n"
	".type	co_switch, %function  \n"
	"co_switch:                   \n"
	"    push {r14}               \n"
#if !defined(__argoverstack__)
	"    push {r0}                \n"
#endif
	"    stmfd r13!, {r4-r11}     \n" /* push {r4-r11} */
	"    str r13, [r0]            \n"
	"    ldr r13, [r1]            \n"
	"    ldmfd r13!, {r4-r11}     \n" /* pop {r4-r11} */
#if !defined(__argoverstack__)
	"    pop {r0}                 \n"
#endif
	"    pop {r15}                \n"
);
#else
	#error "Not supported on this arch."
#endif

int64_t co_time() 
{
	struct timespec ts = {0};
	assert(clock_gettime(CLOCK_MONOTONIC, &ts) == 0);
	return (int64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}
int64_t co_nexttmo(struct co_scheduler *scheduler)
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
 * co_transfer() - co state transfer.
 */
void co_transfer(struct co_struct *co, int state)
{
	stq_del(co);
	switch (state) {
	case CO_READY:
		stq_add(co, &co->scheduler->readyq);
		break;
	case CO_RUNNING:
		break;
	case CO_SLEEPING:
		break;
	case CO_WAITING:
	case CO_TIMEDWAIT:
		stq_add(co, &co->scheduler->waitsq);
		break;
	case CO_EXITED:
		break;
	default:
		assert(0);
	}
	co->state = state;
}
void co_yield(struct co_struct *co)
{
	TRACE(rolec, co, "co@%p yield.", co);
	assert(co->scheduler->runningco == co);
	co_switch(&co->context,	&co->scheduler->context);
}

void co_sleep(struct co_struct *co, int64_t usec)
{
	TRACE(rolec, co, "co@%p sleep %lld us", co, usec);
	assert(co->scheduler->runningco == co);
	if (usec > 0) {
		co->sleep_expired_time = co_time() + usec;
		TRACE(rolec, co, "will sleep %lld us, expire at %lld", usec, co->sleep_expired_time);
		while (&co->sleep_node != rb_insert(&co->sleep_node, &co->scheduler->sleep_tree))
			co->sleep_expired_time++;
	}
	co_transfer(co, CO_SLEEPING);
	co_yield(co);
}

static int expt_compare(const struct rb_node *n1, const struct rb_node *n2)
{
	struct co_struct *c1 = rb_entry(n1, struct co_struct, sleep_node);
	struct co_struct *c2 = rb_entry(n2, struct co_struct, sleep_node);

	return c1->sleep_expired_time > c2->sleep_expired_time;
}
static int expt_search(const void *data, const struct rb_node *n)
{
	const struct co_struct *c1 = rb_entry(n, struct co_struct, sleep_node);
	const struct co_struct *c2 = (const struct co_struct *)data;
	return c1->sleep_expired_time > c2->sleep_expired_time;
}
static void expt_print(const struct rb_node *n)
{
	if (n) {
		const struct co_struct *co = rb_entry(n, struct co_struct, sleep_node);
		printf(rb_is_red(n) ? "\033[31m%lld" : "\033[0m%lld", (long long)co->sleep_expired_time);
	} else
		printf("\033[0mnil");
	
	printf("\033[0m\n");
}

/*
 * schedule_expired - return next timeout duration
 */
static void expire(struct co_scheduler *scheduler)
{
	struct rb_node *n;
	struct co_struct *co;
	for (n = rb_first(&scheduler->sleep_tree); n ; n = rb_next(n)) {
		co = rb_entry(n, struct co_struct, sleep_node);
		int64_t now = co_time();
		int64_t dif = co->sleep_expired_time - now;
		TRACE(roles, scheduler, "co@%p expired time: %lld, now: %lld, dif: %lld", co, co->sleep_expired_time, now, dif);
		if (dif <= 0) {
			rb_remove(n, &scheduler->sleep_tree);
			co_transfer(co, CO_READY);
		}
	}
}

static inline void release(struct co_struct *co)
{
	TRACE(roles, co->scheduler, "call scheduler->release(), co@%p exited.", co);
	free(co->stack);
	co->scheduler->release(co);
}

static void schedule(struct co_scheduler *scheduler)
{
	struct co_struct *co;
	while (stq_any(&scheduler->readyq)) {
		co = stq_first(&scheduler->readyq);
		TRACE(roles, scheduler, "resume co@%p.", co);
		co->scheduler->runningco = co;
		co_transfer(co, CO_RUNNING);
		co_switch(&co->scheduler->context, &co->context);
		if (co->scheduler->runningco->state == CO_EXITED) {
			release(co->scheduler->runningco);
		}
		co->scheduler->runningco = NULL;
	}
}

int co_scheduler_init(struct co_scheduler *scheduler, 
		int  (*schedule)(struct co_scheduler *), void *priv, 
		void (*release)(struct co_struct *))
{	
	stq_init(&scheduler->readyq);
	stq_init(&scheduler->waitsq);
	scheduler->coid      = 0L;
	scheduler->runningco = NULL;
	scheduler->priv	     = priv;
	scheduler->schedule  = schedule;
	scheduler->release   = release;
	rb_tree_init(&scheduler->sleep_tree, expt_compare, expt_search, expt_print);
	return 0;
}
int co_scheduler_run(struct co_scheduler *scheduler)
{
	while (stq_any(&scheduler->waitsq) || 
		   stq_any(&scheduler->readyq) || rb_count(&scheduler->sleep_tree)) {
		expire(scheduler);
		schedule(scheduler);
		TRACE(roles, scheduler, "calling scheduler->schedule()...");
		scheduler->schedret = scheduler->schedule(scheduler);
	}
	return 0;
}

static void co_dummy_ret()
{
	assert(0);
}

static void co_entry(struct co_struct *co)
{
#if defined(__argoverstack__)
	struct co_struct *arg = co;
	#if defined(__x86_64__)
	__asm__ volatile("movq (%%rbp), %0;" : "=r"(co));
	#elif defined(__arm__)
	__asm__ volatile("ldr %0, [fp]" : "=r"(co));
	#endif
	TRACE(rolec, co, "co argument: %p", arg);
#endif
	TRACE(rolec, co, "co@%p ID[%ld] running.", co, co->id);
	co->routine(co);
	TRACE(rolec, co, "co@%p ID[%ld] exiting.", co, co->id);

	co_transfer(co, CO_EXITED);
	co_yield(co);
}

int co_init(struct co_struct *co, struct co_scheduler *scheduler, 
			void (*routine)(struct co_struct *), void *arg, size_t stacksize)
{
	void *stack = NULL;
	void **new_stack = NULL;
	int err = posix_memalign(&stack, getpagesize(), stacksize);
	if (err) {
		LOG("Failed to allocate stack for new coroutine");
		return -1;
	}
	
	new_stack = (void **)(stack + stacksize);

	/* 
	 * Below are register values stored on its stack, see co_switch()..
	 */
#if defined(__i386__)
	/*
	 *  i386 :
	 *  new_stack[-1] = arg
	 *  new_stack[-2] = ret 
	 *  new_stack[-3] = eip
	 *  new_stack[-4] = %ebp
	 *  new_stack[-5] = %ebx 
	 *  new_stack[-6] = %edi 
	 *  new_stack[-7] = %esi 
	 */
	new_stack[-1]  = (void *)co;           /* arg */
	new_stack[-2]  = (void *)co_dummy_ret; /* ret */
	new_stack[-3]  = (void *)co_entry;     /* eip */
	new_stack[-4]  = (void *)new_stack - (2 * sizeof(void *)); /* ebp */
	co->context.sp = (void *)new_stack - (7 * sizeof(void *)); /* esp */
#elif defined(__x86_64__)
	#if defined(__argoverstack__)
	/*
	 *  x86_64 :
	 *  new_stack[-1] = arg
	 *  new_stack[-2] = eip
	 *  new_stack[-3] = %rbp
	 *  new_stack[-4] = %rbx 
	 *  new_stack[-5] = %r12 
	 *  new_stack[-6] = %r13 
	 *  new_stack[-7] = %r14 
	 *  new_stack[-8] = %r15 
	 */
	new_stack[-1]  = (void *)co;       /* arg */
	new_stack[-2]  = (void *)co_entry; /* eip */
	new_stack[-3]  = (void *)new_stack - (1 * sizeof(void *)); /* rbp */
	co->context.sp = (void *)new_stack - (8 * sizeof(void *)); /* rsp */
	#else
	/*
	 *  x86_64 :
	 *  new_stack[-1] = eip
	 *  new_stack[-2] = %rdi <- arg
	 *  new_stack[-3] = %rbp
	 *  new_stack[-4] = %rbx 
	 *  new_stack[-5] = %r12 
	 *  new_stack[-6] = %r13 
	 *  new_stack[-7] = %r14 
	 *  new_stack[-8] = %r15 
	 */
	new_stack[-1]  = (void *)co_entry;		/* eip */
	new_stack[-2]  = (void *)co; 			/* rdi */
	new_stack[-3]  = (void *)new_stack;		/* rbp */
	co->context.sp = (void *)new_stack - (8 * sizeof(void *));
	#endif
#elif defined(__arm__)
	/*
	 *  __argoverstack__:
	 *  new_stack[-1] = arg --> co
	 *  new_stack[-2] = lr  --> co_entry
	 *
	 *  __argoverregister__:
	 *  new_stack[-1] = lr  --> co_entry
	 *  new_stack[-2] = co  --> r0
	 *
	 *  new_stack[-3] = r11
	 *  new_stack[-4] = r10
	 *  new_stack[-5] = r9
	 *  new_stack[-6] = r8
	 *  new_stack[-7] = r7 
	 *  new_stack[-8] = r6
	 *  new_stack[-9] = r5
	 *  new_stack[-10]= r4
	 *
	 */
	#if defined(__argoverstack__)
	new_stack[-1] = (void *)co;
	new_stack[-2] = (void *)co_entry;
	new_stack[-3] = (void *)new_stack - (1 * sizeof(void *));
	#else
	new_stack[-1] = (void *)co_entry;
	new_stack[-2] = (void *)co;
	new_stack[-3] = (void *)new_stack;
	#endif
	/* 
	 * FP(Frame pointer):
	 *     Thumb: r7
	 *     ARM  : r11
	 */
	#if defined(__thumb__)
	new_stack[-7] = new_stack[-3];
	#endif
	co->context.sp  = (void *)new_stack - (10 * sizeof(void *));
#else
	#error "Not supported on this arch."
#endif
	INIT_LIST_HEAD(&co->list);
	co->id 			= scheduler->coid++;
	co->scheduler 	= scheduler;
	co->ssize		= stacksize;
	co->stack 		= stack;
	co->arg			= arg;
	co->routine		= routine;
	co_transfer(co, CO_READY);
	LOG("co@%p initialized, stack top: %p.", co, new_stack);
	return 0;
}
