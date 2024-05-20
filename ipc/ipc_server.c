/*
 * Copyright (c) 2017, <-Jason Chen->
 * Version: 1.2.3 - 20230410
 *				  - Add epoll support.
 * Version: 1.2.2 - 20230406
 *				  - Optimize and strengthen: ipc_timing_register()/ipc_timing_unregister():
 *                  Only allow them called in IPC core running context.       
 * Version: 1.2.1 - 20230316
 *				  - Optimize ipc_server_bind(IPC_COOKIE_ASYNC):
 *					Remove allocating fixed length of ipc_msg for ipc_async in ipc_server_bind(),
 *					and move the buffer allocating to ipc_async_execute().
 *				  - Optimize ipc_async_execute():
 *					Add arg @size for allocating specific length of ipc_msg for ipc_async.
 *				  - Modify log tag, using comm instead.
 *			1.2.0 - 20220216
 *				  - New feature: 
 *					1. Add struct ipc_async and related APIs named ipc_async_xxx().
 *                     With IPC async, server has the ability to response to client asynchronously.
 * 					2. Add data option(registering information) for subscriber registering - ipc_broker_register().
 *					3. Add option for ipc_client_manager() - IPC_CLIENT_UNREGISTER, IPC_CLIENT_SHUTDOWN.
 *					4. Modify ipc_server_manager(), supporting to manage client class: IPC_CLASS_REQUESTER.
 *			      - Optimizations: 
 *				    1. Add @arg and @cookie passed as the sole argument of handler().
 *				    2. Simplify ipc mutex option, Remove struct ipc_mutex, using __ipc_mutex internal instead.
 *				    3. Optimize struct ipc_peer, needing less Memory.
 *				    4. Optimize struct ipc_proxy, needing less Memory.
 *				    5. Rename notify_pack()->ipc_notify_pack() for conflict.
 *				    6. Remove private id for client of IPC_CLASS_SUBSCRIBER.
 *
 *			1.1.1 - 20210917
 *                - Add ipc timing
 *                - Fix segment fault while doing core exit.
 *				  - Rename some definitions.
 *			1.1.0 - 20200520, update from  to 1.0.x
 *				  - What is new in 1.1.0? Works more efficiently and fix some extreme internal bugs.
 *			1.0.x - 20171101
 *
 * Author: Jie Chen <jasonchen0720@163.com>
 *
 * Brief : This program is the implementation of IPC server core.
 * Date  : Created at 2017/11/01
 */
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <assert.h>
#include <signal.h>
#include <pthread.h>
#include <sys/un.h>
#include <sys/stat.h>
#include "ipc_server.h"
#include "ipc_log.h"
#include "ipc_base.h"
#define IPC_PERF	1
#define IPC_EPOLL	0
#define IPC_DEBUG	1
#if IPC_EPOLL
#include <limits.h>
#include <sys/epoll.h>
#else
#include <sys/select.h>
#endif
struct ipc_task
{
	pthread_t tid;
	struct ipc_async 	*async;
	struct ipc_pool 	*pool;
	struct list_head 	list;
};
struct ipc_pool
{
	int mintasks;
	int maxtasks;
	int ntasks;
	int idletasks;
	int linger;	
	int flags;
	struct list_head async_head;	
	struct list_head active_task_head;
	pthread_attr_t attr;
	pthread_mutex_t *mutex;
	pthread_cond_t		work_cond;
	pthread_cond_t  	exit_cond;
};

#define ASYNC_IDLE 		0
#define	ASYNC_PENDING	1
#define ASYNC_EXECUTING	2
#define ASYNC_EXITED	4
/* 
 * Cookie type, provide an ability for server to process request asynchronously.
 * Set through ipc_server_bind(const struct ipc_server *sevr, int type, void *cookie).
 */
struct ipc_async
{
	int		type;	/* Cookie type: Keep at the first field */
	void 	*arg;
	void (*func)(struct ipc_msg *, void *);
	void (*release)(struct ipc_msg *, void *);
	volatile int state;
	const struct ipc_server *sevr;
	struct ipc_msg 	*msg;
	struct list_head list;
};
enum {
	TASK_POOL_DESTROY,
};
#define BACKLOG 5
#define IPC_MSG_BUFFER_SIZE 8192
struct ipc_node
{
	struct list_head list;
	struct ipc_server *sevr;
};
struct ipc_peer
{
	struct ipc_node 	*node;
	unsigned long 		 mask;
	struct ipc_server	 sevr;
};
struct ipc_proxy
{
	void *arg;
	int (*handler)(int, void *);
	struct ipc_server	sevr;
};
struct ipc_core {
	struct list_head timing;
#if IPC_EPOLL
	int epfd;
#elif IPC_PERF
	int				nfds;
	fd_set 			rfds; /* In case of performance bottlenecks, using epoll instead. */
#endif	
	struct list_head head;
	struct list_head *node_hb;
	/**
	 * User's private argument, is passed as the sole argument of handler(),filter(),manager().
	 */
	void *arg;
	/**
	 * Prototype: int (*handler)(struct ipc_msg *msg, void *arg, void *priv); 
	 * Brief: Handler of normal ipc messages from clients.
	 */
	int (*handler)(struct ipc_msg *, void *, void *);
	/**
	 * Prototype: int (*filter)(struct ipc_notify *notify, void *arg);
	 * Brief: Filter hook for processing ipc notification messages from clients.
	 */
	int (*filter) (struct ipc_notify *, void *);
	/**
	 * Prototype: int (*manager)(const struct ipc_server *cli, int cmd, void *data, void *arg, void *priv);
	 * Brief: Hook for clients managing.
	 */
	int (*manager)(const struct ipc_server *, int, void *, void *, void *);
	struct ipc_server *dummy;
	struct ipc_buf	  *buf;
	struct ipc_msg 	  *clone;
	struct ipc_pool	  *pool;
	/* IPC Lock */
	void   *mutex;
	
	const char *path;
	const char *server;
	int flags;
	int tid;
};
#define IPC_CORE_F_INITED	0x1
#define IPC_CORE_F_RUN		0x2
static pthread_mutex_t 		__ipc_mutex = PTHREAD_MUTEX_INITIALIZER;
static struct ipc_server	__ipc_dummy = {
	.clazz 	 	= IPC_CLASS_DUMMY,
	.handler 	= NULL,
	.cookie 	= NULL,
	.identity	= -1,
};	 	
static struct ipc_core 		__ipc_core =
{
	.flags = 0,
};
#define current_core() 	(&__ipc_core)
#define __LOGTAG__ (current_core()->server)
#define ipc_core_inited(c)	((c) && ((c)->flags & IPC_CORE_F_INITED))
#define ipc_core_running(c)	((c) && ((c)->flags & IPC_CORE_F_RUN))
#define ipc_core_context(c)	((c)->tid == gettid())
#define ipc_mutex_lock(mutex)		\
do {								\
	if (mutex) 						\
		pthread_mutex_lock(mutex);	\
} while (0)

#define ipc_mutex_unlock(mutex)		\
do {								\
	if (mutex)						\
		pthread_mutex_unlock(mutex);\
} while (0)

static pthread_condattr_t __ipc_cond_attr;
static pthread_mutex_t __ipc_async_mutex = PTHREAD_MUTEX_INITIALIZER;
static struct ipc_pool __ipc_async_pool = {
	.mintasks	= 1,
	.maxtasks	= 1,
	.ntasks		= 0,
	.linger		= 0,
};
static void ipc_async_task_cleanup(void *arg)
{
	struct ipc_pool *pool = arg;

	pool->ntasks--;

	if (pool->flags & __bit(TASK_POOL_DESTROY)) {
		if (pool->ntasks == 0) {
			pthread_cond_broadcast(&pool->exit_cond);
		}
	} 
	pthread_mutex_unlock(pool->mutex);
	IPC_LOGI("Task exit cleanup ntasks:%d.", pool->ntasks);
}
static void ipc_async_cleanup(void *arg)
{
	struct ipc_task *task = arg;
	pthread_mutex_lock(task->pool->mutex);
	list_delete(&task->list);
	IPC_LOGI("IPC async state:%d", task->async->state);
	
	if (task->async->state & ASYNC_EXITED) {
		ipc_free_msg(task->async->msg);
		free(task->async);
		IPC_LOGI("IPC async free:%p", task->async);
	} else {
		IPC_LOGI("IPC async proc msg:%d, flags:%04x.", task->async->msg->msg_id, task->async->msg->flags);
		if (task->async->msg->flags & IPC_FLAG_REPLY)
			send_msg(task->async->sevr->sock, task->async->msg);
		
		ipc_free_msg(task->async->msg);
		task->async->msg 	 = NULL;
		task->async->state 	 = ASYNC_IDLE;
		task->async->func	 = NULL;
		task->async->release = NULL;
	}
}
static void *ipc_async_task(void *arg)
{
	int timedout = 0;
	struct ipc_pool *pool = arg;

	struct ipc_task task;

	task.tid = pthread_self();
	task.pool = pool;
	INIT_LIST_HEAD(&task.list);

	pthread_detach(task.tid);
	pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, NULL);
	pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
	
	pthread_mutex_lock(pool->mutex);
	pthread_cleanup_push(ipc_async_task_cleanup, pool);
	while (1) {

		pool->idletasks++;			
		while (list_empty(&pool->async_head)) {
			if (pool->flags & __bit(TASK_POOL_DESTROY))
				break;
			IPC_LOGI("idle tasks:%d, n tasks:%d, min tasks:%d.", pool->idletasks, pool->ntasks, pool->mintasks);
			if (pool->ntasks <= pool->mintasks) {
				pthread_cond_wait(&pool->work_cond, pool->mutex);
			} else {
				if (pool->linger > 0){
					struct timespec tspec;
					clock_gettime(CLOCK_MONOTONIC, &tspec);
					tspec.tv_sec += pool->linger;
					if (pthread_cond_timedwait(
						&pool->work_cond, pool->mutex, &tspec) != ETIMEDOUT)
						continue;
				}
				timedout = 1;
				break;
			}
		}
		pool->idletasks--;
		
		if (pool->flags & __bit(TASK_POOL_DESTROY))
			break;	
		
		if (!list_empty(&pool->async_head)) {
			struct ipc_async *async = list_first_entry(&pool->async_head, struct ipc_async, list);
			task.async = async;
			async->state = ASYNC_EXECUTING;
			list_del_init(&async->list);	
			list_add_tail(&task.list, &pool->active_task_head);
			pthread_mutex_unlock(pool->mutex);
			
			pthread_cleanup_push(ipc_async_cleanup, &task);
			async->func(async->msg, async->arg);
			pthread_cleanup_pop(1);
		}

		if (timedout && (pool->ntasks > pool->mintasks))
			break;
	}
	pthread_cleanup_pop(1);
	IPC_LOGI("Task exited ntasks: %d.", pool->ntasks);
	return NULL;	
}
static int ipc_async_task_create(struct ipc_pool *pool)
{
	sigset_t set, oset;
	sigfillset(&set);
	pthread_t thread_id;
	pthread_sigmask(SIG_SETMASK, &set, &oset);
	int error = pthread_create(&thread_id, &pool->attr, ipc_async_task, pool);
	pthread_sigmask(SIG_SETMASK, &oset, NULL);

	return error;
}
static void ipc_async_setup(struct ipc_pool * pool)
{
	assert(!pthread_condattr_init(&__ipc_cond_attr));
	assert(!pthread_condattr_setclock(&__ipc_cond_attr, CLOCK_MONOTONIC));
	pool->flags 		= 0;
	pool->ntasks		= 0;
	pool->idletasks 	= 0;
	pool->mutex 		= &__ipc_async_mutex;

	assert(!pthread_cond_init(&pool->work_cond, &__ipc_cond_attr));
	assert(!pthread_cond_init(&pool->exit_cond, &__ipc_cond_attr));
	assert(!pthread_attr_init(&pool->attr));	
	assert(!pthread_attr_setdetachstate(&pool->attr, PTHREAD_CREATE_DETACHED));
	INIT_LIST_HEAD(&pool->async_head);
	INIT_LIST_HEAD(&pool->active_task_head);
	IPC_LOGI("Task pool create done<%d-%d>.", pool->mintasks, pool->maxtasks);
}
static void ipc_async_exit(struct ipc_pool * pool)
{
	pthread_mutex_lock(pool->mutex);

	pool->flags |= __bit(TASK_POOL_DESTROY);
	if (pool->idletasks > 0)
		pthread_cond_broadcast(&pool->work_cond);

	if (pool->ntasks > 0) {
		struct timespec tspec;
		clock_gettime(CLOCK_MONOTONIC, &tspec);
		tspec.tv_sec += pool->linger;
		pthread_cond_timedwait(&pool->exit_cond, pool->mutex, &tspec);
	}
				
	struct ipc_task *t;
	list_for_each_entry(t, &pool->active_task_head, list) {
		pthread_cancel(t->tid);
	}
	
	while (pool->ntasks > 0)
		pthread_cond_wait(&pool->exit_cond, pool->mutex);

	pthread_mutex_unlock(pool->mutex);

	pthread_cond_destroy(&pool->work_cond);
	pthread_cond_destroy(&pool->exit_cond);
	pthread_attr_destroy(&pool->attr);
	pthread_condattr_destroy(&__ipc_cond_attr);
}
int ipc_async_execute(void *cookie,	/* @cookie: We assert that it  is type of struct ipc_async */
				struct ipc_msg *msg,		/* @msg : IPC request to execute asynchronously */
				unsigned int size,			/* @size: Max size of response */
				void (*func)(struct ipc_msg *, void *), 
				void (*release)(struct ipc_msg *, void *), void *arg)
{
	struct ipc_core *core = current_core();

	if (!ipc_core_running(core))
		return -1;

	if (!core->pool) {
		IPC_LOGE("IPC async not enabled.");
		return -1;
	} 
	if (ipc_cookie_type(cookie) != IPC_COOKIE_ASYNC) {
		IPC_LOGE("IPC async bind using ipc_server_bind().");
		return -1;
	}
	struct ipc_pool *pool = core->pool;
	struct ipc_async *async = cookie;
	pthread_mutex_lock(pool->mutex);
	if (async->state != ASYNC_IDLE) {
		IPC_LOGE("IPC async busy state:%d.", async->state);
		goto err;
	}
	assert(async->msg == NULL);
	async->func 	= func;
	async->release	= release;
	async->arg		= arg;
	async->state	= ASYNC_PENDING;
	async->msg = ipc_clone_msg(msg, size);
	if (!async->msg) {
		IPC_LOGE("IPC async none memory.");
		goto err;
	}
	msg->flags |= __bit(IPC_BIT_ASYNC);

	list_add_tail(&async->list, &pool->async_head);

	if (pool->idletasks > 0) {
		pthread_cond_signal(&pool->work_cond);
	} else if (pool->ntasks < pool->maxtasks && ipc_async_task_create(pool) == 0) {
		pool->ntasks++;
	}
	pthread_mutex_unlock(pool->mutex);
	return 0;
err:
	pthread_mutex_unlock(pool->mutex);
	return -1;
}
static struct ipc_async * ipc_async_alloc(const struct ipc_server *sevr)
{
	assert(sevr->cookie == NULL);
	struct ipc_async *async = (struct ipc_async *)malloc(sizeof(struct ipc_async));
	if (async) {
		memset(async, 0, sizeof(*async));	

		INIT_LIST_HEAD(&async->list);
		async->msg 		= NULL;
		async->sevr 	= sevr;
		async->state	= ASYNC_IDLE;
		async->type 	= IPC_COOKIE_ASYNC;
		IPC_LOGI("IPC async alloc:%p.", async);
	} else 
		IPC_LOGE("IPC async alloc failed.");
	return async;
}
static void ipc_async_release(struct ipc_pool *pool, struct ipc_async *async)
{
	pthread_mutex_lock(pool->mutex);
	IPC_LOGI("IPC async state:%d.", async->state);
	switch (async->state) {
	case ASYNC_PENDING:
		list_delete(&async->list);
	case ASYNC_IDLE:
		IPC_LOGI("IPC async free:%p.", async);
		if (async->release)
			async->release(async->msg, async->arg);
		if (async->msg)
			ipc_free_msg(async->msg);
		free(async);
		break;
	case ASYNC_EXECUTING:
		async->state |= ASYNC_EXITED;
		async->sevr = NULL;
		break;
	default:
		assert(0);
		break;
	}
	pthread_mutex_unlock(pool->mutex);
}
static inline int ipc_server_manager(
	struct ipc_core *core, 
	struct ipc_server *sevr, int cmd, void *data)
{
	return core->manager ? core->manager(sevr, cmd, data, core->arg, sevr->cookie) : 0;
}
/* bit_index - calculate the index of the highest bit 1 with the mask
 * @mask: mask to calculate
 */
static inline int bit_index(unsigned long mask)
{
    int i;
    i = -1;
    do {
        mask >>= 1;
        i++;
    } while (mask);
    return i;
}
/* bit_count - calculate how many bits of 1 with the mask
 * @mask: mask to calculate
 */
static inline int bit_count(unsigned long mask)
{
	int i = 0;
	while (mask){
		i++;
		mask &= (mask - 1);
	}
	return i;
}
#if IPC_EPOLL
static inline void epoll_add(struct ipc_server *sevr, struct ipc_core *core)
{
	struct epoll_event ev;
	ev.events = EPOLLIN; /* | EPOLLET */
	ev.data.fd 	= sevr->sock;
	ev.data.ptr = sevr;
	if (epoll_ctl(core->epfd, EPOLL_CTL_ADD, sevr->sock, &ev) < 0)
		IPC_LOGE("epoll_ctl(%d) err.", sevr->sock);
	
	IPC_LOGI("Add socket:%d @%p.", sevr->sock, sevr);
}

static inline void epoll_del(struct ipc_server *sevr, struct ipc_core *core)
{
#if 0
	struct epoll_event ev;
	ev.events = EPOLLIN;
	ev.data.fd	= sevr->sock;
	ev.data.ptr = sevr;
#endif
	if (epoll_ctl(core->epfd, EPOLL_CTL_DEL, sevr->sock, NULL) < 0)
		IPC_LOGE("epoll_ctl(%d) err.", sevr->sock);
	
	IPC_LOGI("Del socket:%d @%p.", sevr->sock, sevr);
}
#elif IPC_PERF
static void fds_add(int sock, struct ipc_core *core)
{
	FD_SET(sock, &core->rfds);
	if (core->nfds < sock)
		core->nfds = sock;

	IPC_LOGI("add socket:%d current nfds:%d.", sock,  core->nfds);
#if IPC_DEBUG
	/* Print for debug */
	struct ipc_server *s;
	list_for_each_entry(s, &core->head, list) {
		IPC_LOGI("socket:%d ISSET:%d.", s->sock, !!FD_ISSET(s->sock, &core->rfds));
	}
#endif
}

static void fds_del(int sock, struct ipc_core *core)
{
	FD_CLR(sock, &core->rfds);
	IPC_LOGI("del socket:%d ISSET:%d.", sock, !!FD_ISSET(sock, &core->rfds));
	if (core->nfds == sock) {
		core->nfds = 0;
		struct ipc_server *s;
		list_for_each_entry(s, &core->head, list) {
			if (s->sock > core->nfds) 
				core->nfds = s->sock;
		}
		IPC_LOGI("recalc nfds:%d.", core->nfds);
	}
#if IPC_DEBUG
	/* Print for debug */
	struct ipc_server *s;
	list_for_each_entry(s, &core->head, list) {
		IPC_LOGI("socket:%d ISSET:%d.", s->sock, !!FD_ISSET(s->sock, &core->rfds));
	}
#endif
}
#endif
static inline void sevr_init(struct ipc_core *core, struct ipc_server *sevr,
							int clazz,
							int identity,
							int sock,
							int (*handler)(struct ipc_core *, struct ipc_server *))
{
	sevr->clazz 	= clazz;
	sevr->sock 		= sock;
	sevr->identity	= identity;
	sevr->handler	= handler;
	sevr->cookie 	= NULL;
	list_add_tail(&sevr->list, &core->head);
#if IPC_EPOLL
	epoll_add(sevr, core);
#elif IPC_PERF
	fds_add(sock, core);
#endif
}
static const char * peer_name(const struct ipc_server *sevr)
{
	static char name[32];
	char path[32];
	sprintf(path, "/proc/%d/comm", sevr->identity);
	int fd = open(path, O_RDONLY);
	if (fd < 0)
		return DUMMY_NAME;
	int size = read(fd, name, sizeof(name) -1);
	if (size <= 0) {
		close(fd);
		return DUMMY_NAME;
	}
	close(fd);
	if (name[size - 1] == '\n')
		name[size - 1] = '\0';
	else
		name[size] = '\0';
	return (const char *)name;
}
/**
 * peer_sync - the callback synchronize message from client, initialize |sevr->peer->node|
 * @core: ipc core of server
 * @sevr: handle to initialize
 */
static inline void peer_sync(struct ipc_core *core, struct ipc_peer *peer)
{
	int i;
	int j = 0;
	unsigned long mask = peer->mask;
	ipc_mutex_lock(core->mutex);
	for (i = 0; mask; mask >>= 1,i++) {
		if (mask & 0x01) {
			peer->node[j].sevr = &peer->sevr;
			list_add_tail(&peer->node[j++].list, &core->node_hb[i]);
		}
	}
	ipc_mutex_unlock(core->mutex);
}
/*
 * node_hash_bucket_init - to initialize hash bucket for supporting of asynchronous message to clients
 * @core: ipc core of server
 */
static inline int node_hash_bucket_init(struct ipc_core *core)
{
	core->node_hb = (struct list_head *)malloc((sizeof(unsigned long) << 3) * sizeof(struct list_head));
	if (!core->node_hb)
		return -1;
	int i;
	for (i = 0; i < sizeof(unsigned long) << 3; i++)
		init_list_head(&core->node_hb[i]);
	IPC_LOGI("node hash bucket initialized.");
	return 0;
}

static inline void timing_time(struct timeval *tv)
{	
	struct timespec ts = {0};
	clock_gettime(CLOCK_MONOTONIC, &ts);
	tv->tv_sec 	= ts.tv_sec;
#if IPC_EPOLL
	tv->tv_usec = ts.tv_nsec / 1000000;	/* Convert to millisecond */
#else
	tv->tv_usec = ts.tv_nsec / 1000;	/* Convert to microsecond */
#endif
}

static int timing_refresh(struct ipc_timing *timing, const struct timeval *now)
{	
	if (now) {
		timing->expire.tv_sec  = now->tv_sec;
		timing->expire.tv_usec = now->tv_usec;
	} else
		timing_time(&timing->expire);
	timing->expire.tv_sec  += timing->tv.tv_sec;
#if IPC_EPOLL
	/* timing->expire->tv_usec is used to save the millisecond part */
	timing->expire.tv_usec += timing->tv.tv_usec / 1000;
	while (timing->expire.tv_usec >= 1000) {
		timing->expire.tv_sec++;
		timing->expire.tv_usec -= 1000;
	}
#else
	/* timing->expire->tv_usec is used to save the microsecond part */
	timing->expire.tv_usec += timing->tv.tv_usec;
	while (timing->expire.tv_usec >= 1000000) {
		timing->expire.tv_sec++;
		timing->expire.tv_usec -= 1000000;
	}
#endif
	return 0;
}

static void timing_insert(struct ipc_core *core, struct ipc_timing *timing)
{
	struct ipc_timing *tmp;
	struct list_head *p;
	list_for_each(p, &core->timing) {
		tmp = list_entry(p, struct ipc_timing, list);
		if (timercmp(&timing->expire, &tmp->expire, <))
			break;
	}
	list_add_tail(&timing->list, p);
}
static int ipc_handler_invoke(struct ipc_core *core, struct ipc_server *s, struct ipc_msg *msg)
{
	struct ipc_peer *peer = NULL;
	msg->flags &= IPC_FLAG_CLIENT_MASK;
	switch (s->clazz) {
	case IPC_CLASS_DUMMY:
		break;
	case IPC_CLASS_REQUESTER:
		msg->flags |= __bit(IPC_BIT_REQUESTER);
		break;
	case IPC_CLASS_SUBSCRIBER:
		msg->flags |= __bit(IPC_BIT_SUBSCRIBER);
		peer = container_of(s, struct ipc_peer, sevr);
		break;
	case IPC_CLASS_MASTER:
	case IPC_CLASS_PROXY:
	default:
		return -1;
	}
	assert(s->clazz == ipc_class(msg));
	/* invoke the user's specific ipc message process handler */
	if (core->handler(msg, core->arg, s->cookie) < 0)
		return 0;
	if (msg->flags & __bit(IPC_BIT_ASYNC)) {
		IPC_LOGI("IPC async message:%d.", msg->msg_id);
		return 0;
	}
	/* check if this message with a response */
	if (!(msg->flags & __bit(IPC_BIT_REPLY)) || peer)
		return 0;
	if (send_msg(s->sock, msg) < 0) {
		IPC_LOGE("reply error: %s.", strerror(errno));
		return -1;
	}
	return 0;
}
#define msg_report(sevr, msg) 	\
do {							\
	if (send((sevr)->sock, (void *)(msg),  __data_len(msg), MSG_NOSIGNAL | MSG_DONTWAIT) < 0)	\
		IPC_LOGE("send error:%s[%d],sk:%d,errno:%d.", peer_name(sevr), (sevr)->identity, (sevr)->sock, errno);	 \
} while (0)
#define msg_broadcast(node, head, msg)		\
 do {										\
	(msg)->msg_id |= IPC_MSG_TOKEN;			\
	list_for_each_entry(node, head, list) {	\
		msg_report(node->sevr, msg);		\
	}										\
} while (0)
#define msg_unicast(node, head, msg, to)	\
do {										\
	(msg)->msg_id |= IPC_MSG_TOKEN;			\
	list_for_each_entry(node, head, list) {	\
		if (node->sevr->identity == (to)) {	\
			msg_report(node->sevr, msg);	\
			break;							\
		}									\
	}										\
} while (0)
#define msg_notify(sevr, msg)		\
do {								\
	(msg)->msg_id |= IPC_MSG_TOKEN;	\
	msg_report(sevr, msg);			\
} while (0)
/**
 * ipc_release - release resources occupied by ipc handle
 * @core: ipc core
 * @ipc: ipc handle to release
 */
static void ipc_release(struct ipc_core *core, struct ipc_server *sevr)
{
	assert(sevr != NULL);
	void *p = sevr;
	switch (sevr->clazz) {
	case IPC_CLASS_DUMMY:
		assert(0);
		break;
	case IPC_CLASS_REQUESTER:
		ipc_server_manager(core, sevr, IPC_CLIENT_RELEASE, NULL);
		if (sevr->cookie) {
			if (ipc_cookie_type(sevr->cookie) == IPC_COOKIE_ASYNC)
				ipc_async_release(core->pool, sevr->cookie);
		}
		IPC_LOGI("Free ipc:%p.", sevr);
		break;
	case IPC_CLASS_SUBSCRIBER: {
		struct ipc_peer *peer = container_of(sevr, struct ipc_peer, sevr);
		
		ipc_server_manager(core, sevr, IPC_CLIENT_RELEASE, NULL);
		ipc_mutex_lock(core->mutex);
		
		int i;
		IPC_LOGD("peer: %p, node: %p", peer, peer->node);
		for (i = 0; i < bit_count(peer->mask); i++)
			/*
			 * Only when |sevr| is set does this node need to been deleted from the list
		     * because node is initialized when the server side receive the syn message from clients' callback
		     */
			if (peer->node[i].sevr)
				list_delete(&peer->node[i].list);
		
		if (peer->node)
			free(peer->node);
		
		ipc_mutex_unlock(core->mutex);
		p = peer;
		IPC_LOGI("Free peer:%p.", p);
		break;
	}
	case IPC_CLASS_MASTER:
		IPC_LOGI("Free master ipc:%p.", sevr);
		break;
	case IPC_CLASS_PROXY:
		p = container_of(sevr, struct ipc_proxy, sevr);
		IPC_LOGI("Free proxy:%p.", p);
		break;
	default:
		assert(0);
		break;
	}
	list_delete(&sevr->list);
#if IPC_EPOLL
	epoll_del(sevr, core);
#elif IPC_PERF
	fds_del(sevr->sock, core);
#endif	
	close(sevr->sock);
	free(p);
}
/**
 * ipc_server_create - allocate memory for new client handle
 * @core: ipc core of server
 * @clazz: ipc handle type, defined in enum IPC_CLASS.
 * @identity: client identity, it is client's pid.
 * @sock: socket fd
 * @handler: handler hook for this client handle
 */
static struct ipc_server * ipc_server_create(struct ipc_core *core,
							int clazz,
							int identity,
							int sock,
							int (*handler)(struct ipc_core *, struct ipc_server *))
{
	struct ipc_server *sevr;
	sevr = (struct ipc_server *)malloc(sizeof(struct ipc_server));
	if (sevr == NULL)
		return NULL;

	sevr_init(core, sevr, clazz, identity, sock, handler);
	IPC_LOGI("Alloc ipc: %p.", sevr);
	return sevr;
}
/**
 * ipc_server_sync - handler for the syn message from client's callback
 * @core: ipc core of server.
 * @sevr: ipc handle.
 * @msg: synchronize message.
 */
static int ipc_server_sync(struct ipc_core *core,
								struct ipc_server *sevr,
								struct ipc_msg *msg)
{
	assert(sevr->clazz == IPC_CLASS_SUBSCRIBER);
	
	struct ipc_peer *peer = container_of(sevr, struct ipc_peer, sevr);
	
	assert(peer->node);

	struct ipc_identity *tid = (struct ipc_identity *)msg->data;
	IPC_LOGI("sync from client %d(%d) ,sk: %d, mask:%04lx",
							sevr->identity, tid->identity, sevr->sock, peer->mask);
	peer_sync(core, peer);
	ipc_server_manager(core, &peer->sevr, IPC_CLIENT_SYNC, msg->data);
	return 0;
}
/**
 * ipc_server_unregister - handler for the callback unregister message from client
 * @core: ipc core of server
 * @sevr: ipc handle
 * @msg: unregister message
 */
static int ipc_server_unregister(struct ipc_core *core, struct ipc_server *sevr, struct ipc_msg *msg)
{
	if (sevr->clazz != IPC_CLASS_SUBSCRIBER)
		return -1;
	
	struct ipc_peer *peer = container_of(sevr, struct ipc_peer, sevr);
	
	if (!peer->node)
		return -1;
	IPC_LOGI("client %d:%s exit, sk:%d, mask:%04lx",
								sevr->identity,  peer_name(sevr), sevr->sock, peer->mask);

	ipc_server_manager(core, sevr, IPC_CLIENT_UNREGISTER, NULL);
	
	send_msg(sevr->sock, msg);
	ipc_release(core, sevr);
	return 0;
}
/**
 * ipc_broker_publish - server as a broker, dispatch the messages from the client to all subscribers
 * @core: ipc core of server
 * @msg: message to dispatch
 */
static int ipc_broker_publish(struct ipc_core *core, struct ipc_msg * msg)
{
	struct ipc_node *node;
	struct ipc_notify *notify = (struct ipc_notify *)msg->data;
	if (!topic_check(notify->topic))
		return -1;
    /**
     * Every notify msg transferred by the server needs to be filtered by filter hook.
     * If filter hook returns a negative number, this notify will be ignored and not be dispatched.
     */
    if (core->filter &&
        core->filter(notify, core->arg) < 0)
        return -1;

	/**
	 * Node hash bucket is null, this indicates that no clients register to the server
	 */
	if (!core->node_hb)
		return 0;


	int i = bit_index(notify->topic);
	if (notify->to == IPC_TO_BROADCAST)
		msg_broadcast(node, &core->node_hb[i], msg);
	else
		msg_unicast(node, &core->node_hb[i], msg, notify->to);
	return 0;
}
static int ipc_proxy_socket_handler(struct ipc_core *core, struct ipc_server *ipc)
{
	assert(ipc->clazz == IPC_CLASS_PROXY);
	
	struct ipc_proxy *proxy = container_of(ipc, struct ipc_proxy, sevr);
	
	return proxy->handler(ipc->sock, proxy->arg);
}
/**
 * ipc_common_socket_handler - handler for client handle that use continuous stream socket connection
 * @core:ipc core of server
 * @ipc: ipc handle
 */
static int ipc_common_socket_handler(struct ipc_core *core, struct ipc_server *ipc)
{
	int len;
	struct ipc_buf *buf = core->buf;
	struct ipc_msg *msg = (struct ipc_msg *)buf->data;
	struct timeval timeout = {.tv_sec  = 0, .tv_usec = 500 * 1000};
	ipc_buf_reset(buf);
  __recv:
	len = recv(ipc->sock, buf->data + buf->tail, buf->size - buf->tail, 0);
	if (len > 0) {
		IPC_LOGD("receive %d bytes, head:%u, tail:%u.", len, buf->head, buf->tail);
		buf->tail += len;
		do {
			msg = find_msg(buf, core->clone);
			if (!msg) {
				if (ipc_buf_full(buf)) {
					IPC_LOGE("buffer full.");
					goto __error;
				}
				if (recv_wait(ipc->sock, &timeout) <= 0) {
					IPC_LOGE("recv wait timeout.");
					goto __error;
				}
				goto __recv;
			}
			switch (msg->msg_id) {
			case IPC_SDK_MSG_CONNECT:
				goto __error;	/* This kind of msg is not allowed for this ipc handle */
			case IPC_SDK_MSG_REGISTER:
				goto __error;	/* This kind of msg is not allowed for this ipc handle */
			case IPC_SDK_MSG_SYNC:
				if (ipc_server_sync(core, ipc, msg) < 0)
					goto __error;
				break;
			case IPC_SDK_MSG_UNREGISTER:
				if (ipc_server_unregister(core, ipc, msg) < 0)
					goto __error;
				break;
			case IPC_SDK_MSG_NOTIFY:
				if (ipc_broker_publish(core, msg) < 0)
					IPC_LOGE("broker dispatch notify error.");
				break;
			default:
				if (ipc_handler_invoke(core, ipc, msg) < 0)
					goto __error;
				break;
			}
		} while (ipc_buf_pending(buf));
		return 0;
	}
	if (len == 0) {
		IPC_LOGW("client %d:%d:%s shutdown sk:%d", ipc->clazz, ipc->identity, peer_name(ipc), ipc->sock);
		if (ipc->clazz == IPC_CLASS_SUBSCRIBER)
			ipc_server_manager(core, ipc, IPC_CLIENT_SHUTDOWN, NULL);
	} else {
		/* recv calling errors */
		if (errno == EINTR)
			goto __recv;
		IPC_LOGE("error: recv length: %d, errno: %d", len, errno);
	#if 0
		if (errno == EAGAIN || errno == EWOULDBLOCK)
			return 0;
	#endif
		if (errno == ECONNRESET)
			IPC_LOGE("client %d:%d:%s crash sk:%d", ipc->clazz, ipc->identity, peer_name(ipc), ipc->sock);
			
		if (ipc->clazz == IPC_CLASS_SUBSCRIBER)
			ipc_server_manager(core, ipc, IPC_CLIENT_SHUTDOWN, NULL);
	}
  __error:
	ipc_release(core, ipc);
	return -1;
}
static inline struct ipc_proxy * ipc_proxy_create(struct ipc_core *core, int fd, int (*handler)(int, void *), void *arg)
{
	struct ipc_proxy * proxy = (struct ipc_proxy *)malloc(sizeof(struct ipc_proxy));

	if (proxy) {
		proxy->handler 	= handler;
		proxy->arg		= arg;

		sevr_init(core, &proxy->sevr, IPC_CLASS_PROXY, 0, fd, ipc_proxy_socket_handler);
	}
	IPC_LOGI("Alloc proxy: %p.", proxy);
	return proxy;
}
/**
 * ipc_peer_create - to initialize a peer which is used to support asynchronous message to clients
 * @core: ipc core of server
 * @mask: event mask clients interested
 * @clazz: ipc handle type, defined in enum IPC_CLASS.
 * @identity: client identity, it is client's pid.
 * @sock: socket fd
 */
static struct ipc_peer * ipc_peer_create(struct ipc_core *core, unsigned long mask,
							int clazz,
							int identity,
							int sock)
{
	assert(mask);
	
	struct ipc_peer *peer;
	peer = (struct ipc_peer *)malloc(sizeof(struct ipc_peer));
	if (!peer)
		return NULL;
	peer->mask = mask;
	peer->node = NULL;

	/*
	 * Here only clean up all nodes, when the clients' callback is ready,
	 * will send syn message to server, then to initialize nodes,
	 * put all nodes into node hash bucket in ipc core.
	 */
	peer->node = (struct ipc_node *)calloc(bit_count(mask), sizeof(struct ipc_node));
	if (!peer->node)
		goto err;
	
	IPC_LOGI("create peer mask: %lu, node n:%d", mask, bit_count(mask));
	
	ipc_mutex_lock(core->mutex);
	if (!core->node_hb) {
		if (node_hash_bucket_init(core) < 0) {
			ipc_mutex_unlock(core->mutex);
			IPC_LOGE("hash bucket init failure.");
			goto err;
		}
	}
	ipc_mutex_unlock(core->mutex);
	sevr_init(core, &peer->sevr, clazz, identity, sock, ipc_common_socket_handler);
	IPC_LOGI("Alloc peer: %p.", peer);
	return peer;
err:
	if (peer) {
		if (peer->node)
			free(peer->node);
		free(peer);
	}
	return NULL;
}
/**
 * ipc_server_register - handler for the callback register message from client
 * @core: ipc core of server
 * @sock: socket fd
 * @msg: callback register message
 */
static int ipc_server_register(struct ipc_core *core, int sock, struct ipc_msg * msg)
{
	struct ipc_peer *peer;
	struct ipc_reg *reg = (struct ipc_reg *)msg->data;
	if (!reg->mask) {
		IPC_LOGE("Incorrect mask.");
		goto __fatal;
	}
	/* Identity always be client's pid */
	peer = ipc_peer_create(core, reg->mask, IPC_CLASS_SUBSCRIBER, msg->from, sock);
	if (!peer)
		goto __fatal;
	/*
	 * Manager hook provide a possibility to manage the clients registered to the server.
	 * This is the only hook which expose the IPC handle, user can set client cookies through this.
	 */
	if (ipc_server_manager(core, &peer->sevr, IPC_CLIENT_REGISTER, reg->data) < 0) {
		msg->data_len = 0;
		send_msg(sock, msg);
		IPC_LOGW("Manager refused.");
		goto __error;
	}
	IPC_LOGI("%d register, client %d:%s, sk: %d, mask: %04lx",
					msg->from, peer->sevr.identity, peer_name(&peer->sevr), peer->sevr.sock, peer->mask);
	/*
	 * In this period, do some negotiation.
	 * max IPC buffer size is always required
	 * identity allocated by user is optional.
	 */
	struct ipc_negotiation *neg = (struct ipc_negotiation *)msg->data;
	neg->buf_size = core->buf->size;
	msg->msg_id = IPC_SDK_MSG_SUCCESS;
	msg->data_len = sizeof(struct ipc_negotiation);
	if (send_msg(sock, msg) < 0) {
		/* Client probably just exited or crashed. */
		IPC_LOGE("Send SDK success error.");
		ipc_server_manager(core, &peer->sevr, IPC_CLIENT_SHUTDOWN, NULL);
		goto __error;
	}
	return 0;
__error:
	ipc_release(core, &peer->sevr);
	return -1;
__fatal:
	msg->data_len = 0;
	send_msg(sock, msg);
	close(sock);
	return -1;
}
/**
 * ipc_server_connect - handler for the client connecting message from client
 * @core: ipc core of server
 * @sock: socket fd
 * @msg: channel register message
 */
static int ipc_server_connect(struct ipc_core *core, int sock, struct ipc_msg * msg)
{
	struct ipc_server *sevr;
	struct ipc_identity *cid = (struct ipc_identity *)msg->data;
	sevr = ipc_server_create(core, IPC_CLASS_REQUESTER, cid->identity, sock, ipc_common_socket_handler);
	if (!sevr) {
		send_msg(sock, msg);
		close(sock);
		return -1;
	}
	if (ipc_server_manager(core, sevr, IPC_CLIENT_CONNECT, NULL) < 0) {
		send_msg(sock, msg);
		IPC_LOGW("Manager refused.");
		goto err;
	}
	IPC_LOGI("%d:%s connecting sevr:sk:%d", sevr->identity, peer_name(sevr), sevr->sock);
	msg->msg_id = IPC_SDK_MSG_SUCCESS;
	msg->data_len = 0;
	
	if (send_msg(sock, msg) < 0) {
		IPC_LOGE("Send SDK success error.");
		goto err;
	}
	return 0;
err:
	ipc_release(core, sevr);
	return -1;
}
/**
 * ipc_master_socket_handler - handler for client handle that use temporary stream socket connection
 * @core:ipc core of server
 * @ipc: master ipc handle
 */
static int ipc_master_socket_handler(struct ipc_core *core, struct ipc_server *ipc)
{
	int sock;
	char *buffer = core->buf->data;

	struct sockaddr_un client_addr = {0};
	socklen_t addrlen = (socklen_t)sizeof(struct sockaddr_un);
  __accept:
	sock = accept(ipc->sock, (struct sockaddr *)&client_addr, &addrlen);
	if (sock > 0) {
		if (sock_opts(sock, 0) < 0)
			goto __error;
		if (recv_msg(sock, buffer, core->buf->size, 1) < 0)
			goto __error;
		struct ipc_msg *msg = (struct ipc_msg *)buffer;
		switch (msg->msg_id){
		case IPC_SDK_MSG_CONNECT:
			return ipc_server_connect(core, sock, msg);
		case IPC_SDK_MSG_REGISTER:
			return ipc_server_register(core, sock, msg);
		case IPC_SDK_MSG_SYNC:
			goto __error;	/* This kind of msg is not allowed for this ipc handle */
		case IPC_SDK_MSG_UNREGISTER:
			goto __error;	/* This kind of msg is not allowed for this ipc handle */
		case IPC_SDK_MSG_NOTIFY:
			if (ipc_broker_publish(core, msg) < 0)
				IPC_LOGE("broker dispatch notify error.");
			break;
		default:
			core->dummy->sock = sock;
			if (ipc_handler_invoke(core, core->dummy, msg) < 0)
				goto __error;
			break;
		}
		close(sock);
		return 0;
	  __error:
		IPC_LOGE("error");
		close(sock);
		return -1;
	} else {
		if (errno == EINTR)
			goto __accept;
		if (errno == EAGAIN || errno == EWOULDBLOCK)
			return 0;
		return -1;
	}
}

/**
 * ipc_socket_create - create a unix-domain stream socket
 * @path: the path of unix-domain socket
 */
static int ipc_socket_create(const char *path)
{
	int sock;
    struct sockaddr_un serv_adr;
    if ((sock = socket(AF_UNIX, SOCK_STREAM, 0)) < 0)
	{
        IPC_LOGE("Unable to create socket: %s", strerror(errno));
        return -1;
    }
    serv_adr.sun_family = AF_UNIX;

    strncpy(serv_adr.sun_path, path, sizeof(serv_adr.sun_path));

    unlink(path);

    if (bind(sock, (struct sockaddr *)&serv_adr, (socklen_t)sizeof(struct sockaddr_un)) < 0) {
        IPC_LOGE("Unable to bind socket: %s", strerror(errno));
		close(sock);
        return -1;
    }
	if (listen(sock, BACKLOG) < 0) {
		IPC_LOGE("Unable to listen socket: %s", strerror(errno));
		close(sock);
        return -1;
	}
    chmod(path, S_IRWXO|S_IRWXG |S_IRWXU);
	return sock;
}
/**
 * ipc_master_init - init master socket for the ipc core
 * @core: ipc core to be initialized
 */
static int ipc_master_init(struct ipc_core *core)
{
	int sock;
	struct ipc_server *master;
	sock = ipc_socket_create(core->path);
	if (sock < 0)
		return -1;

	if (sock_opts(sock, 0) == -1) {
		close(sock);
		return -1;
	}
	master = ipc_server_create(core, IPC_CLASS_MASTER, 0, sock,
		ipc_master_socket_handler);
	if (master == NULL) {
		close(sock);
		return -1;
	}

	IPC_LOGI("master sock:%d", master->sock);
	return 0;
}
/**
 * ipc_timing_register - Register a timing-event.
 * Recommend to be called in the context of %handler callback passed in ipc_server_init().
 * @timing: timing handle initialized via ipc_timing_init().
 */
int ipc_timing_register(struct ipc_timing *timing)
{
	struct ipc_core *core = current_core();
	if (!ipc_core_inited(core))
		return -1;
	if (!ipc_core_context(core)) {
		IPC_LOGE("Not IPC context@%d.", gettid());
		return -1;
	}
	timing_refresh(timing, NULL);
	list_del_init(&timing->list);
	timing_insert(core, timing);
	return 0;	
}
int ipc_timing_unregister(struct ipc_timing *timing)
{
	struct ipc_core *core = current_core();
	if (!ipc_core_inited(core))
		return -1;
	if (!ipc_core_context(core)) {
		IPC_LOGE("Not IPC context%d.", gettid());
		return -1;
	}
	list_del_init(&timing->list);
	return 0;
}
int ipc_timing_refresh(struct ipc_timing *timing, const struct timeval *tv)
{	
	if (tv) {
		timing->tv.tv_sec  = tv->tv_sec;
		timing->tv.tv_usec = tv->tv_usec;
	} 
	return ipc_timing_register(timing);
}
int ipc_timing_release()
{
	struct ipc_core *core = current_core();
	if (!ipc_core_inited(core))
		return -1;
	struct ipc_timing *timing, *tmp;
	ipc_mutex_lock(core->mutex);
	list_for_each_entry_safe(timing, tmp, &core->timing, list) {
		list_del_init(&timing->list);
	}
	ipc_mutex_unlock(core->mutex);
	return 0;
}
/**
 * ipc_loop - run the ipc core
 * @core: ipc core to run
 */
#if IPC_EPOLL
static int ipc_loop(struct ipc_core *core)
{
	int res;
	int timeout;
	struct timeval now;
	struct ipc_timing *timing;
	struct ipc_server *sevr;
	struct epoll_event events[32];
	while ((core->flags & IPC_CORE_F_RUN) && !list_empty(&core->head)) {
		if (!list_empty(&core->timing)) {
			timing_time(&now);
			timing = list_first_entry(&core->timing, struct ipc_timing, list);
			if (timercmp(&now, &timing->expire, <)) {
				/* timing->expire->tv_usec is used to save the millisecond part */
				int64_t ms = (timing->expire.tv_sec - now.tv_sec) * 1000 + (timing->expire.tv_usec - now.tv_usec);
				timeout = ms > INT_MAX ? INT_MAX : (int)ms;
				IPC_LOGD("{%ld %ld} {%ld %ld} timeout:%d", timing->expire.tv_sec, timing->expire.tv_usec, now.tv_sec, now.tv_usec, timeout);
			} else {
				timeout = 0;
			}
		} else timeout = -1;

		res = epoll_wait(core->epfd, events, 32, timeout);
		if (res > 0) {
			int n = 0;
			for (n = 0; n < res; n++) {
				sevr = events[n].data.ptr;
				sevr->handler(core, sevr);
				IPC_LOGD("event socket:%d @%p.", sevr->sock, sevr);
			}
		} else if (res < 0){
			if (errno == EINTR) {
				continue;
			}
			IPC_LOGE("epoll_wait error:%d", errno);
			break;
		}
		if (!list_empty(&core->timing)) {
			struct ipc_timing *t;
			timing_time(&now);
			list_for_each_entry_safe(timing, t, &core->timing, list) {
				if (timercmp(&now, &timing->expire, <))
					break;
			
				IPC_LOGD("{%ld %ld} {%ld %ld} expired!", timing->expire.tv_sec, timing->expire.tv_usec, now.tv_sec, now.tv_usec);
				list_del_init(&timing->list);
				if (timing->cycle) {
					timing_refresh(timing, &now);
					timing_insert(core, timing);
				}
				timing->handler(timing);
			}
		}
	}
	return -1;
}
#else
static int ipc_loop(struct ipc_core *core)
{
	int res;
	int nfds;
	fd_set readfds;
	struct timeval tv, now, *timeout;
	struct ipc_timing *timing;
	struct ipc_server *ipc, *tmp;
	while ((core->flags & IPC_CORE_F_RUN)
		&& !list_empty(&core->head)) {
		if (!list_empty(&core->timing)) {
			timing_time(&now);
			timeout = &tv;
			timing = list_entry(core->timing.next, struct ipc_timing, list);
			if (timercmp(&now, &timing->expire, <))
				timersub(&timing->expire, &now, &tv);
			else
				tv.tv_sec = tv.tv_usec = 0;
		} else timeout = NULL;
#ifdef IPC_PERF
		nfds	= core->nfds;
		readfds = core->rfds;
#else
		FD_ZERO(&readfds);
		nfds = 0;
		list_for_each_entry(ipc, &core->head, list) {
			IPC_LOGD("socket:%d", ipc->sock);
			FD_SET(ipc->sock, &readfds);
			if (ipc->sock > nfds) nfds = ipc->sock;
		}
#endif
	 __select:
		res = select(nfds + 1, &readfds, NULL, NULL, timeout); /* In case of performance bottlenecks, using epoll instead. */
		if (res < 0) {
			/* On Linux, the function select modifies timeout to reflect the amount of time not slept */
			if (errno == EINTR)
				goto __select;
			IPC_LOGE("select error:%d", errno);
			break;
		}
		if (!list_empty(&core->timing)) {
			struct ipc_timing *tt;
			timing_time(&now);
			list_for_each_entry_safe(timing, tt, &core->timing, list) {
				if (!timercmp(&now, &timing->expire, <)) {
					list_del_init(&timing->list);
					if (timing->cycle) {
						timing_refresh(timing, &now);
						timing_insert(core, timing);
					}
					timing->handler(timing);
				} else break;
			}
		}	
		if (res > 0) {
		#ifdef IPC_PERF
			int n = 0;
		#endif
			list_for_each_entry_safe(ipc, tmp, &core->head, list) {
				if (FD_ISSET(ipc->sock, &readfds)) {
					ipc->handler(core, ipc);
				#ifdef IPC_PERF
					if (++n == res)
						break;
				#endif
				}
			}
		} 
	}
	return -1;
}
#endif
/**
 * ipc_server_init - init the ipc core
 * @server: the name of ipc server
 * @handler: int handler(struct ipc_msg *msg, void *arg, void *cookie)
 *           @msg: IPC message received.
 *           @arg: Private argument used in callback, see option - IPC_SEROPT_SET_ARG.
 * 			 @cookie:  client cookie to be used in callbacks, see ipc_server_bind().
 *			 callback used to process messages,
 *			 The handler should return -1 on failure, 0 on success
 *			 If success, the server core will give the expected response to the client
 */
int ipc_server_init(const char *server, int (*handler)(struct ipc_msg *, void *, void *))
{
	char *path = NULL;
	if (!server || !handler)
		return -1;
	path = (char *)malloc(sizeof(UNIX_SOCK_DIR) + strlen(server));
	if (!path)
		return -1;
	struct ipc_core *core = current_core();
	sprintf(path, "%s%s", UNIX_SOCK_DIR, server);
	init_list_head(&core->head);
	init_list_head(&core->timing);
	core->handler = handler;
	core->filter  = NULL;
	core->manager = NULL;
	core->mutex   = NULL;
	core->node_hb = NULL;
	core->buf 	  = NULL;
	core->arg	  = NULL;
	core->pool	  = NULL;
	core->path 	  = (const char *)path;
	core->server  = self_name();
#if IPC_EPOLL
	core->epfd = epoll_create(1024);
	if (core->epfd < 0) {
		goto err;
	}
	if (fcntl(core->epfd, F_SETFD, FD_CLOEXEC) < 0) {
		close(core->epfd);
		goto err;
	}
#elif IPC_PERF
	core->nfds	  = 0;
	FD_ZERO(&core->rfds);
#endif
	if (ipc_master_init(core) < 0) {
#if IPC_EPOLL
		close(core->epfd);
#endif
		goto err;
	}
	core->dummy	 = &__ipc_dummy;
	core->tid 	 = gettid();
	core->flags |= IPC_CORE_F_INITED;
	IPC_LOGI("%s init done.", server);
	return 0;
err:
	core->path 	  = NULL;
	core->server  = NULL;
	free(path);
	return -1;
}
/**
 * ipc_server_run - after init ipc core successfully, call this to run ipc core
 */
int ipc_server_run()
{
	struct ipc_core *core = current_core();
	if (!ipc_core_inited(core))
		return -1;
	/*
	 * The IPC buffer can be set by ipc_server_setopt(),
	 * if null, user IPC_MSG_BUFFER_SIZE as the default size.
	 */
	if (!core->buf)
		core->buf = alloc_buf(IPC_MSG_BUFFER_SIZE);
	if (!core->buf)
		return -1;
	core->clone = ipc_alloc_msg(core->buf->size);
	if (!core->clone)
		return -1;
	core->tid = gettid();
	IPC_LOGI("mutex:%p,filter:%p,manager:%p, buf size:%u, context@%d", core->mutex,
			core->filter,
			core->manager,
			core->buf->size, core->tid);
	core->flags |= IPC_CORE_F_RUN;
	return ipc_loop(core);
}
/**
 * ipc_server_exit - release all the resources allocated during ipc core work
 */
int ipc_server_exit()
{
	struct ipc_server *pos;
	struct ipc_server *tmp;
	struct ipc_timing *timing, *ttmp;
	struct ipc_core *core = current_core();
	if (!ipc_core_inited(core))
		return -1;
	core->flags &= ~IPC_CORE_F_RUN;
	list_for_each_entry_safe(pos, tmp, &core->head, list) {
		ipc_release(core, pos);
	}
	ipc_mutex_lock(core->mutex);
	list_for_each_entry_safe(timing, ttmp, &core->timing, list) {
		list_del_init(&timing->list);
	}
	if (core->path) {
		free((void *)core->path);
		core->path = NULL;
	}
#if 0
	if (core->server) {
		free((void *)core->server);
		core->server = NULL;
	}
#endif
	if (core->node_hb) {
		free(core->node_hb);
		core->node_hb = NULL;
	}
	if (core->buf) {
		free(core->buf);
		core->buf = NULL;
	}
	if (core->clone) {
		ipc_free_msg(core->clone);
		core->clone = NULL;
	}
	core->flags &= ~IPC_CORE_F_INITED;
	ipc_mutex_unlock(core->mutex);
#if IPC_EPOLL
	close(core->epfd);
#endif
	if (core->pool)
		ipc_async_exit(core->pool);
	
	IPC_LOGI("IPC exit.");
	return 0;
}
/**
 * ipc_server_publish - if the server as broker, server can call this function to publish a message.
 * This function is non-thread-safe can be called only in your %handler callback passed by ipc_server_init(),
 * if calling in mult-thread-environment, please set IPC_SEROPT_SET_BROKER_MUTEX option.
 * @to: indicate who the notify message is sent to.
 * @topic: message type
 * @msg_id: message id
 * @data: message data, if no data carried, set NULL
 * @size: the length of message data
 *
 */
int ipc_server_publish(int to, unsigned long topic, int msg_id, const void *data, int size)
{
	int dynamic = 0;
	char buffer[IPC_NOTIFY_MSG_MAX_SIZE];
	struct ipc_node *node;
	struct ipc_msg *ipc_msg = (struct ipc_msg *)buffer;
	struct ipc_core *core = current_core();
	if (!ipc_core_running(core))
		return -1;
	if (!topic_check(topic))
		return -1;
	if (!ipc_notify_space_check(sizeof(buffer), size)) {
		ipc_msg = ipc_alloc_msg(sizeof(struct ipc_notify) + size);
		if (!ipc_msg) {
			IPC_LOGE("Server publish memory failed.");
			return -1;
		} else dynamic = 1;
	}
	ipc_notify_pack(ipc_msg, to, topic, msg_id, data, size);
	int i = bit_index(topic);
	ipc_mutex_lock(core->mutex);
	/*
	 * Node hash bucket has not been initialized.
	 * This indicates that no clients register to server.
	 */
	if (core->node_hb) {
		if (to == IPC_TO_BROADCAST)
			msg_broadcast(node, &core->node_hb[i], ipc_msg);
		else
			msg_unicast(node, &core->node_hb[i], ipc_msg, to);
	}
	ipc_mutex_unlock(core->mutex);
	if (dynamic)
		ipc_free_msg(ipc_msg);
	return 0;
}
/**
 * ipc_server_notify - if the server as broker, server can call this function to notify a client
 *  <!- Non-thread-safe, it is safely calling in context of handler passed by ipc_server_init(). Calling in other thread, 
 *      caller needs to do resource competition protection in command:IPC_CLIENT_RELEASE of ipc_client_manager() -!>
 * @sevr: client the message sent to
 * @topic: message type
 * @msg_id: message id
 * @data: message data, if no data carried, set NULL
 * @size: the length of message data
 *
 */
int ipc_server_notify(const struct ipc_server *sevr, unsigned long topic, int msg_id, const void *data, int size)
{
	int dynamic = 0;
	char buffer[IPC_NOTIFY_MSG_MAX_SIZE];
	struct ipc_msg *ipc_msg = (struct ipc_msg *)buffer;
	struct ipc_core *core = current_core();
	
	if (!ipc_core_running(core))
		return -1;
	
	struct ipc_peer *peer = container_of(sevr, struct ipc_peer, sevr);
	if (sevr->clazz != IPC_CLASS_SUBSCRIBER ||
		!topic_check(topic) ||
		!topic_isset(peer, topic))
		return -1;	

	/* Without client SYNC, client's callback may not get ready */
	if (!peer->node[bit_count(peer->mask & (topic -1))].sevr) {
		IPC_LOGE("client has not synchronized.");
		return -1;
	}	
	if (!ipc_notify_space_check(sizeof(buffer), size)) {
		ipc_msg = ipc_alloc_msg(sizeof(struct ipc_notify) + size);
		if (!ipc_msg) {
			IPC_LOGE("Server notify memory failed.");
			return -1;
		} else dynamic = 1;
	}	
	ipc_notify_pack(ipc_msg, IPC_TO_NOTIFY, topic, msg_id, data, size);	
	msg_notify(sevr, ipc_msg);	
	if (dynamic)
		ipc_free_msg(ipc_msg);
	return 0;
}
static inline int set_opt_flt(struct ipc_core *core, void *arg)
{
	ipc_notify_filter filter = (ipc_notify_filter)arg;
	if (!filter)
		return -1;
	core->filter = filter;
	return 0;
}
static inline int set_opt_mng(struct ipc_core *core, void *arg)
{
	ipc_client_manager manager = (ipc_client_manager)arg;
	if (!manager)
		return -1;
	core->manager = manager;
	return 0;

}
static inline int set_opt_mtx(struct ipc_core *core, void *arg)
{
	core->mutex = arg ? &__ipc_mutex : NULL;
	return 0;
}
static inline int set_opt_buf(struct ipc_core *core, void *arg)
{
	unsigned int *size = (unsigned int *)arg;

	if (!size)
		return -1;
	if (core->buf)
		return -1;
	core->buf = alloc_buf(*size);
	return core->buf ? 0: -1;
}
static inline int set_opt_arg(struct ipc_core *core, void *arg)
{
	core->arg = arg;
	return 0;
}
static inline int set_opt_async(struct ipc_core *core, void *arg)
{
	core->pool = &__ipc_async_pool;
	if (arg) {
		struct ipc_aopts *opts = arg;
		if (opts->mintasks < 0 || 
			opts->mintasks > opts->maxtasks) {
			IPC_LOGE("Async option invalid arg, min tasks:%d, max tasks:%d.", opts->mintasks, opts->maxtasks);
			core->pool = NULL;
			return -1;
		}
		
		if (opts->mintasks > 0)
			core->pool->mintasks = opts->mintasks;
		if (opts->maxtasks > 0)
			core->pool->maxtasks = opts->maxtasks;
		if (opts->linger > 0)
			core->pool->linger = opts->linger;
	}
	ipc_async_setup(core->pool);
	return 0;
}
/**
 * ipc_server_proxy - Calling this function, ipc core will manage this fd.
 * @fd: fd to be trusted.
 * @handler: fd handler
 * @arg: arg passed as the sole argument of handler().
 */
int ipc_server_proxy(int fd, int (*handler)(int, void *), void *arg)
{
	struct ipc_core *core = current_core();
	if (!ipc_core_inited(core))
		return -1;
	if (fd < 0)
		return -1;
	if (!handler)
		return -1;

	return ipc_proxy_create(core, fd, handler, arg) ? 0 : -1;
}
/**
 * ipc_server_setopt - used to set ipc core options,
 * always call this after calling ipc_server_init() and before ipc_server_run().
 * @opt: defined in enum IPC_SERVER_OPTION
 * @arg: the option parameter
 */
int ipc_server_setopt(int opt, void *arg)
{
	struct ipc_core *core = current_core();
	if (!ipc_core_inited(core))
		return -1;
	switch (opt) {
	case IPC_SEROPT_SET_FILTER:
		return set_opt_flt(core, arg);
	case IPC_SEROPT_SET_MANAGER:
		return set_opt_mng(core, arg);
	case IPC_SEROPT_SET_MUTEX:
		return set_opt_mtx(core, arg);
	case IPC_SEROPT_SET_BUF_SIZE:
		return set_opt_buf(core, arg);
	case IPC_SEROPT_SET_ARG:
		return set_opt_arg(core, arg);
	case IPC_SEROPT_ENABLE_ASYNC:
		return set_opt_async(core, arg);
	default:
		return -1;
	}
}
/**
 * ipc_server_bind - used to bind private cookie to the registering client.
 * @sevr: ipc handle for registering client.
 * @type: Indicate private data type, defined in enum IPC_COOKIE_TYPE.
 *		  IPC_COOKIE_USER:
 *		   - Callers can define their own cookie type, but must match with the template of cookie, see struct ipc_cookie.
 *	 	  IPC_COOKIE_ASYNC:
 *		   - @cookie passed as null, IPC will create a cookie of type ipc_async internally.
 * @cookie: Client cookie to be used in callbacks.
 *			Private cookie type values need to be defined less than IPC_COOKIE_USER(0x8000).
 *          
 */
int ipc_server_bind(const struct ipc_server *sevr, int type, void *cookie)
{
	if (sevr->clazz != IPC_CLASS_REQUESTER &&
		sevr->clazz != IPC_CLASS_SUBSCRIBER)
		return -1;
	
	if (sevr->cookie) {
		IPC_LOGE("Repeatedly Priv bind.");
		return -1;
	}

	if (type == IPC_COOKIE_ASYNC) {
		struct ipc_async *async = ipc_async_alloc(sevr);
		if (!async)
			return -1;

		cookie = async;
	} else {
		assert(type == IPC_COOKIE_USER);

		if (ipc_cookie_type(cookie) >= IPC_COOKIE_USER) {
			IPC_LOGE("Invalid cookie type.");
			return -1;
		}
	}
	struct ipc_server *t = (struct ipc_server *)sevr; 
	t->cookie = cookie;
	IPC_LOGI("Bind priv type:%x @%p.", type, cookie);
	return 0;
	 
}
/**
 * ipc_subscribed - used to check if the given mask is concerned.
 * @sevr: ipc handle for registered client.
 * return: zero or none zero.
 */
int ipc_subscribed(const struct ipc_server *sevr, unsigned long mask)
{
	assert(sevr->clazz == IPC_CLASS_SUBSCRIBER);
	struct ipc_peer *peer = container_of(sevr, struct ipc_peer, sevr);
	return peer->mask & mask;
}
