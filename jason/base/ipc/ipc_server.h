#ifndef __IPC_SERVER_H__

#define __IPC_SERVER_H__
#include <sys/time.h>
#include "list.h"
#include "ipc_common.h"

/* 
 * User needs to define private data type ID if they want to bind different data type to sevr.
 * User's private cookie type values need to be defined less than IPC_COOKIE_USER(0x8000).
 */
enum IPC_COOKIE_TYPE 
{
	IPC_COOKIE_USER = 0x8000,
	IPC_COOKIE_NONE,
	IPC_COOKIE_ASYNC,
};
/* 
 * Template of cookie, must defined with field |type| of int as the first member
 */
struct ipc_cookie
{
	int type;
};
struct ipc_core;
struct ipc_server
{
	int clazz;
	int sock;
	int (*handler)(struct ipc_core *, struct ipc_server *);
	int identity;
	/*
	 * Different type of cookie, must be defined as:
	 * struct priv_data_xxx {
	 *		int type;  ---> type used to distinguish from different private data type if any.
	 *		Other members.
	 * };
	 */
	void *cookie;
	struct list_head list;
};
struct ipc_timing
{
	int cycle;
	struct timeval tv;
	struct timeval expire;
	void *arg;
	int (*handler)(struct ipc_timing *);
	struct list_head list;
};
/* Option setting for IPC_SEROPT_ENABLE_ASYNC*/
struct ipc_aopts 
{
	int mintasks;
	int maxtasks;
	int linger;
};
/*
 * ipc_timing_init() - initialize struct ipc_timing.
 * @timing: ipc timing handle.
 * @cyclic: Indicate if this timing event is cyclic.
 * @sec:  Timeout time - Seconds part.
 * @usec: Timeout time - Microsecond part.
 * @argv: Private data or argument.
 * @fn:   Private handler to process the timing event.
 */
#define ipc_timing_init(timing, cyclic, sec, usec, argv, fn) \
	do {															\
		(timing)->cycle 	 = (cyclic);							\
		(timing)->tv.tv_sec  = (sec);								\
		(timing)->tv.tv_usec = (usec);								\
		(timing)->arg 		 = (argv);								\
		(timing)->handler 	 = (fn);								\
		init_list_head(&(timing)->list);							\
	} while (0)
#define ipc_timing_initializer(timing, cyclic, sec, usec, argv, fn) \
{ /* cycle */cyclic, \
  /* tv */{sec, usec}, \
  /* expire */{0, 0}, \
  /* arg */argv, \
  /* handler */fn, \
  /* list */list_head_initializer(timing.list) \
}
/*
 * int ipc_server_handler(struct ipc_msg *msg, void *arg);
 * The handler is used to process ipc messages.
 * @msg : ipc msg to the server.
 * @arg : private argument, see option - IPC_SEROPT_SET_ARG. 
 * On success, zero is returned.  On error, -1 is returned.
 */
typedef int (*ipc_server_handler)(struct ipc_msg *, void *);
/* 
 * int ipc_notify_filter(struct ipc_notify *notify, void *arg);
 * @notify :  every notify msg transferred by the server
 * @arg : private argument, see option - IPC_SEROPT_SET_ARG. 
 * if filter return -1, this notify msg will be ignored.
 * if filter return  0, this notify msg will be delivered.
 */
typedef int (*ipc_notify_filter)(struct ipc_notify *, void *);
enum IPC_MANAGER_CMD
{
	 IPC_CLIENT_RELEASE = 1,	/* For subscriber / connector, do your own release work */
	 IPC_CLIENT_REGISTER,		/* For subscriber / connector, registering stage, do your own register work*/
	 IPC_CLIENT_SYNC,		    /* For subscriber, indicates client's callback is ready */
	 IPC_CLIENT_UNREGISTER,		/* For subscriber, unregister normally */
	 IPC_CLIENT_SHUTDOWN,		/* For subscriber, shutdown abnormally */
};
/* 
 * int ipc_client_manager(const struct ipc_server *sevr, int cmd, void *data, void *arg, void *cookie);
 * manage the client registered to the server.
 * Server can do some managements by implementing ipc_client_manager callback.
 * @sevr: client handle.If you want to report message, need this handle 
 * @cmd:  defined in enum IPC_MANAGER_CMD
 * @data: Data from client, for IPC_CLIENT_REGISTER, may carry register information.
 * @arg:  Private argument, see option - IPC_SEROPT_SET_ARG.
 * @cookie: client cookie to be used in callbacks.
 * On success, zero is returned. On error, -1 is returned, for IPC_CLIENT_REGISTER this client will fail to register.
 */
typedef int (*ipc_client_manager)(const struct ipc_server *, int, void *, void *, void *);
enum IPC_SERVER_OPTION
{									/* for ipc_server_setopt(int opt, void *arg) */
	IPC_SEROPT_SET_FILTER,			/* arg must be ipc_notify_filter type */
	IPC_SEROPT_SET_MANAGER,			/* arg must be ipc_client_manager type */
	IPC_SEROPT_SET_MUTEX,			/* arg: Boolean Type */
	IPC_SEROPT_SET_BUF_SIZE,		/* arg must be unsigned int* type */
	IPC_SEROPT_SET_ARG,				/* arg: Argument of handler(),filter(),manager() */
	IPC_SEROPT_ENABLE_ASYNC,
};
int ipc_server_init(const char *server, int (*handler)(struct ipc_msg *, void *, void *));
int ipc_server_run();
int ipc_server_exit();
int ipc_server_publish(int to, unsigned long topic, int msg_id, void *data, int size);
int ipc_server_notify(const struct ipc_server *sevr, unsigned long topic, int msg_id, void *data, int size);
int ipc_server_setopt(int opt, void *arg);
int ipc_server_proxy(int fd, int (*proxy)(int, void *), void *arg);
/*
 * ipc_timing_idle() - Checking if @timing is running.
 *@timing: Must have been initialized.
 */
static inline int ipc_timing_idle(struct ipc_timing *timing)
{
	return timing->list.next == &timing->list;
}
int ipc_timing_register(struct ipc_timing *timing);
int ipc_timing_unregister(struct ipc_timing *timing);
int ipc_timing_refresh(struct ipc_timing *timing, const struct timeval *tv);
int ipc_timing_release();
enum IPC_CLASS 
{
	IPC_CLASS_DUMMY			= 0,
	IPC_CLASS_CONNECTOR		= 1,
	IPC_CLASS_SUBSCRIBER	= 2,
	IPC_CLASS_MASTER,
	IPC_CLASS_PROXY,
};
/**
 * Return the handle type which received this msg.
 * Return values define in enum IPC_CLASS.
 */
#define ipc_class(msg)		(((msg)->flags & ((1 << IPC_BIT_CONNECTOR) | (1 << IPC_BIT_SUBSCRIBER))) >> IPC_BIT_SERVER)

/*
 * Return the private data type, values may be IPC_COOKIE_ASYNC or different values defined by user. 
 */
#define ipc_cookie_type(cookie) ({ struct ipc_cookie *__c = (struct ipc_cookie *)cookie; __c ? __c->type : IPC_COOKIE_NONE; })
int ipc_server_bind(const struct ipc_server *sevr, int type, void *cookie);
int ipc_subscribed(const struct ipc_server *sevr, unsigned long mask);
int ipc_async_execute(void *cookie, struct ipc_msg *msg, 
				void (*func)(struct ipc_msg *, void *), 
				void (*release)(struct ipc_msg *, void *), void *arg);

#endif