#ifndef __IPC_SERVER_H__

#define __IPC_SERVER_H__
#include <time.h>
#include "list.h"
#include "ipc_common.h"
struct ipc_core;
struct ipc_server
{
	int sock;
	void *arg;
	int (*proxy)(int, void *);
	int (*handler)(struct ipc_core *, struct ipc_server *);
	int identity;
	struct ipc_peer *peer;
	struct list_head list;
};
struct ipc_mutex
{
	int (*lock)();
	int (*unlock)();
};
struct ipc_node
{
	struct list_head list;
	struct ipc_server *sevr;
};
struct ipc_peer
{
	struct ipc_node 	*node;
	unsigned long 		 mask;
	int					 private_identity;
	void 				*private_data;
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
/*
 * ipc_timing_initializer() - initialize struct ipc_timing.
 * @t: client handle.If you want to report message, need this handle.
 * @c: Indicate if this timing event is cyclic.
 * @s: Timeout time - Seconds part.
 * @u: Timeout time - Microsecond part.
 * @a: Private data or argument.
 * @h: Private handler to process the timing event.
 */
#define ipc_timing_initializer(t, c, s, u, a, h) 	\
	do {											\
		(t)->cycle 		= (c);						\
		(t)->tv.tv_sec 	= (s);						\
		(t)->tv.tv_usec = (u);						\
		(t)->arg 		= (a);						\
		(t)->handler 	= (h);						\
		init_list_head(&(t)->list);					\
	} while (0)
/*
 * int ipc_server_handler(struct ipc_msg *msg);
 * The handler is used to process ipc messages.
 * @msg : ipc msg to the server
 * On success, zero is returned.  On error, -1 is returned.
 */
typedef int (*ipc_server_handler)(struct ipc_msg*);
/* 
 * int ipc_notify_filter(struct ipc_notify *notify);
 * @notify :  every notify msg transferred by the server
 * if filter return -1, this notify msg will be ignored
 * if filter return  0, this notify msg will be delivered
 */
typedef int (*ipc_notify_filter)(struct ipc_notify *);
enum IPC_MANAGER_CMD
{
	 IPC_CLIENT_BRELEASE = 0,	/* before release, do some special work if needed before release */
	 IPC_CLIENT_RELEASE = 1,	/* do release, do your own release work */
	 IPC_CLIENT_REGISTER,		/* do register,  do your own register work*/
	 IPC_CLIENT_SYNC,		    /* when client's callback begin to work, client will push this msg to server */
};
/* 
 * int ipc_client_manager(const struct ipc_server *cli, int cmd);
 * manage the client registered to the server.
 * Server can do some managements by implementing ipc_client_manager callback.
 * @cli: client handle.If you want to report message, need this handle 
 * @cmd: defined in enum IPC_MANAGER_CMD
 * On success, zero is returned. On error, -1 is returned, for IPC_CLIENT_REGISTER this client will fail to register.
 */
typedef int (*ipc_client_manager)(const struct ipc_server *, int);
enum IPC_SERVER_OPTION
{									/* for ipc_server_setopt(int opt, void *arg) */
	IPC_SEROPT_SET_FILTER,			/* arg must be ipc_notify_filter type */
	IPC_SEROPT_SET_MANAGER,			/* arg must be ipc_client_manager type */
	IPC_SEROPT_SET_MUTEX,			/* arg must be struct ipc_mutex* type */
	IPC_SEROPT_SET_BUF_SIZE,		/* arg must be unsigned int* type */
};
int ipc_server_init(const char *server, int (*handler)(struct ipc_msg*));

int ipc_server_run();

int ipc_server_exit();

int ipc_server_publish(int to, unsigned long topic, int msg_id, void *data, int size);

int ipc_server_notify(const struct ipc_server *cli, unsigned long topic, int msg_id, void *data, int size);

int ipc_server_setopt(int opt, void *arg);

int ipc_server_proxy(int fd, int (*proxy)(int, void *), void *arg);
int ipc_timing_register(struct ipc_timing *timing);
int ipc_timing_unregister(struct ipc_timing *timing);
int ipc_timing_refresh(struct ipc_timing *timing, const struct timeval *tv);
int ipc_timing_release();
#endif