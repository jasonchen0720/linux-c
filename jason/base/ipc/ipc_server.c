/*
 * Copyright (c) 2017, <-Jason Chen->
 * Version: 1.1.1 - 20210917
 *                - Add ipc timing
 *                - Fix segment fault while doing core exit.
 *				  - Rename some definitions.
 *			1.1.0 - 20200520, update from  to 1.0.x
 *				  - What is new in 1.1.0? Works more efficiently and fix some extreme internal bugs.
 *			1.0.x - 20171101
 * Author: Jie Chen <jasonchen0720@163.com>
 * Note  : This program is used for libipc server core implementation.
 * Date  : Created at 2017/11/01
 */
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/un.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/time.h>
#include "ipc_server.h"
#include "ipc_log.h"
#include "ipc_base.h"

#define BACKLOG 5
#define IPC_MSG_BUFFER_SIZE 8192
struct ipc_core {
	struct list_head timing;
	struct list_head head;
	struct list_head *node_hb;
	int (*handler)(struct ipc_msg*);
	int (*filter) (struct ipc_notify *);
	int (*manager)(const struct ipc_server *, int);
	struct ipc_buf	 *buf;
	struct ipc_mutex *mutex;
	const char *path;
	const char *server;
	int terminated;
	int initialized;
};
static struct ipc_core global_core =
{
	.initialized = 0,
};
#define current_core() 	(&global_core)
#define uninitialized(c)	(!(c) || !(c)->initialized)
#define __LOGTAG__ (current_core()->server)
#define IPCLOG(format,...) IPC_LOG("[%s]:%s(%d): "format"\n", __LOGTAG__, __FUNCTION__, __LINE__, ##__VA_ARGS__)
#define ipc_mutex_lock(mutex)				\
do {								\
	if (mutex)(mutex)->lock();		\
} while (0)

#define ipc_mutex_unlock(mutex)			\
do {								\
	if (mutex)(mutex)->unlock();	\
} while (0)

#define ipc_manager(manager, sevr, cmd) \
do {									\
	if (manager)manager(sevr, cmd);		\
} while (0)
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
/**
 * peer_sync - the callback synchronize message from client, initialize |sevr->peer->node|
 * @core: ipc core of server
 * @sevr: handle to initialize
 */
static inline void peer_sync(struct ipc_core *core, struct ipc_server *sevr)
{
	int i;
	int j = 0;
	unsigned long mask = sevr->peer->mask;
	ipc_mutex_lock(core->mutex);
	for (i = 0; mask; mask >>= 1,i++) {
		if (mask & 0x01) {
			sevr->peer->node[j].sevr = sevr;
			list_add_tail(&sevr->peer->node[j++].list, &core->node_hb[i]);
		}
	}
	ipc_mutex_unlock(core->mutex);
}
/*
 * node_hb_init - to initialize hash bucket for supporting of asynchronous message to clients
 * @core: ipc core of server
 */
static inline int node_hb_init(struct ipc_core *core)
{
	core->node_hb = (struct list_head *)malloc((sizeof(unsigned long) << 3) * sizeof(struct list_head));
	if (!core->node_hb)
		return -1;
	int i;
	for (i = 0; i < sizeof(unsigned long) << 3; i++)
		init_list_head(&core->node_hb[i]);
	IPCLOG("node hash bucket initialized.\n");
	return 0;
}
/**
 * peer_create - to initialize a peer which is used to support asynchronous message to clients
 * @mask: mask, this means the events that clients interested
 */
static struct ipc_peer * peer_create(unsigned long mask)
{
	struct ipc_peer *peer;
	peer = (struct ipc_peer *)malloc(sizeof(struct ipc_peer));
	if (!peer)
		return NULL;
	peer->mask = mask;
	peer->private_data = NULL;
	peer->node = NULL;
	if (mask) {
		unsigned int node_size = bit_count(mask) * sizeof(struct ipc_node);
		peer->node = (struct ipc_node *)malloc(node_size);
		if (!peer->node) {
			free(peer);
			return NULL;
		}
		IPCLOG("create peer mask: %lu, node alloc size: %u\n", mask, node_size);
		/*
		 * Here only clean up all nodes, when the clients' callback is ready,
		 * will send syn message to server, then to initialize nodes,
		 * put all nodes into node hash bucket in ipc core.
		 */
		memset(peer->node, 0, node_size);
	}
	return peer;
}

/**
 * sock_opts - set socket options
 * @sock: socket to set
 */
static inline int sock_opts(int sock)
{
	struct timeval timeout;
	timeout.tv_sec = 1;
	timeout.tv_usec = 0;
	if (setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) < 0) {
		IPCLOG("setsockopt: sk[%d], errno:%d\n", sock, errno);
		return -1;
	}
	if (setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0) {
		IPCLOG("setsockopt: sk[%d], errno:%d\n", sock, errno);
		return -1;
	}
	return 0;
}
int timing_time(struct timeval *tv)
{	
	struct timespec ts;
	if (clock_gettime(CLOCK_MONOTONIC, &ts) < 0)
		return -1;
	tv->tv_sec 	= ts.tv_sec;
	tv->tv_usec = ts.tv_nsec / 1000;
	return 0;
}
int timing_refresh(struct ipc_timing *timing, const struct timeval *tv)
{	
	struct timespec ts;
	if (clock_gettime(CLOCK_MONOTONIC, &ts) < 0)
		return -1;
	timing->expire.tv_sec 	= ts.tv_sec;
	timing->expire.tv_usec 	= ts.tv_nsec / 1000;
	timing->expire.tv_sec 	+= tv->tv_sec;
	timing->expire.tv_usec 	+= tv->tv_usec;
	while (timing->expire.tv_usec >= 1000000) {
		timing->expire.tv_sec++;
		timing->expire.tv_usec -= 1000000;
	}
	return 0;
}
static int ipc_handler_invoke(struct ipc_core *core, int sock, struct ipc_msg *msg)
{
	/* invoke the user's specific ipc message process handler */
	if (core->handler(msg) < 0)
		return 0;
	/* check if this message with a response */
	if (!expect_reply(msg))
		return 0;
	if (send_msg(sock, msg) < 0) {
		IPCLOG("reply error: %s\n", strerror(errno));
		return -1;
	}
	return 0;
}
#define msg_report(sevr, msg) 		\
do {								\
	if (send((sevr)->sock, (void *)(msg),  __data_len(msg), MSG_NOSIGNAL | MSG_DONTWAIT) < 0)	\
		IPCLOG("send error:c[%d],s:%d, errno:%d\n", (sevr)->identity, (sevr)->sock, errno);	 	\
} while (0)
#define msg_broadcast(node, head, msg)		\
 do {											\
 	(msg)->msg_id |= IPC_MSG_TOKEN;				\
	list_for_each_entry(node, head, list) {		\
		msg_report(node->sevr, msg);			\
	}											\
} while (0) 
#define msg_unicast(node, head, msg, to)		\
do {											\
	(msg)->msg_id |= IPC_MSG_TOKEN;				\
	list_for_each_entry(node, head, list) {		\
		if (node->sevr->identity == (to)) {		\
			msg_report(node->sevr, msg);		\
			break;								\
		}										\
	}											\
} while (0)
#define msg_notify(sevr, msg)		\
do {									\
	(msg)->msg_id |= IPC_MSG_TOKEN;		\
	msg_report(sevr, msg);				\
} while (0)
/**
 * ipc_release - release resources occupied by ipc handle
 * @core: ipc core
 * @ipc: ipc handle to release
 */
static void ipc_release(struct ipc_core *core, struct ipc_server *ipc)
{
	if (ipc) {
		if (ipc->peer) {
			ipc_manager(core->manager, ipc, IPC_CLIENT_RELEASE);
		}
		ipc_mutex_lock(core->mutex);
		if (ipc->peer) {
			int i;
			IPC_INFO(__LOGTAG__, "peer: %p, node: %p\n", ipc->peer, ipc->peer->node);
			for (i = 0; i < bit_count(ipc->peer->mask); i++)
				/*
				 * Only when |sevr| is set does this node need to been deleted from the list
			     * because node is initialized when the server side receive the syn message from clients' callback
			     */
				if (ipc->peer->node[i].sevr)
					list_delete(&ipc->peer->node[i].list);
		}
		ipc_mutex_unlock(core->mutex);
		list_delete(&ipc->list);
		close(ipc->sock);
		if (ipc->peer) {
			free(ipc->peer->node);
			free(ipc->peer);
		}
		free(ipc);
	}
}

/**
 * ipc_create - allocate memory for new client handle
 * @core: ipc core of server
 * @mask: the mask of asynchronous messages, if this client handle used to support asynchronous messages
 * @identity: client identity, it is client's pid by default, can be set by core->manage()
 * @sock: socket fd
 * @handler: handler hook for this client handle
 */
static struct ipc_server * ipc_create(struct ipc_core *core, unsigned long mask,
							int identity,
							int sock,
							int (*handler)(struct ipc_core *, struct ipc_server *))
{
	struct ipc_server *sevr;
	sevr = (struct ipc_server *)malloc(sizeof(struct ipc_server));
	if (sevr == NULL)
		return NULL;

	sevr->sock = sock;
	sevr->identity = identity;
	sevr->handler = handler;
	if (mask) {
		sevr->peer = peer_create(mask);
		if (!sevr->peer) {
			free(sevr);
			return NULL;
		}
		ipc_mutex_lock(core->mutex);
		if (!core->node_hb) {
			if (node_hb_init(core) < 0) {
				free(sevr);
				ipc_mutex_unlock(core->mutex);
				return NULL;
			}
		}
		ipc_mutex_unlock(core->mutex);
	} else
		sevr->peer = NULL;
	list_add_tail(&sevr->list, &core->head);
	return sevr;
}
/**
 * ipc_broker_sync - handler for the syn message from client's callback
 * @core: ipc core of server
 * @cli: client handle
 * @msg: synchronize message
 */
static int ipc_broker_sync(struct ipc_core *core,
								struct ipc_server *sevr,
								struct ipc_msg *msg)
{
	if (!sevr->peer)
		return -1;
	if (!sevr->peer->node)
		return -1;
	IPCLOG("sync from client %d ,sk: %d, mask:%04lx\n",
							sevr->identity, sevr->sock, sevr->peer->mask);
	peer_sync(core, sevr);
	ipc_manager(core->manager, sevr, IPC_CLIENT_SYNC);
	return 0;
}
/**
 * ipc_broker_unreg - handler for the callback unregister message from client
 * @core: ipc core of server
 * @cli: client handle
 * @msg: unregister message
 */
static int ipc_broker_unreg(struct ipc_core *core, struct ipc_server *sevr, struct ipc_msg *msg)
{
	if (!sevr->peer)
		return -1;
	if (!sevr->peer->node)
		return -1;
	IPCLOG("client %d exit, sk:%d, mask:%04lx\n",
								sevr->identity,sevr->sock, sevr->peer->mask);
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
        core->filter(notify) < 0)
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
	return ipc->proxy(ipc->sock, ipc->arg);
}
/**
 * ipc_common_socket_handler - handler for client handle that use continuous stream socket connection
 * @core:ipc core of server
 * @ipc: client handle/ipc handle
 */
static int ipc_common_socket_handler(struct ipc_core *core, struct ipc_server *ipc)
{
	int len;
	struct ipc_buf *buf = core->buf;
	struct ipc_msg *msg = (struct ipc_msg *)buf->data;
	buf->head = 0u;
	buf->tail = 0u;
  __recv:
	len = recv(ipc->sock, buf->data + buf->tail, buf->size - buf->tail, 0);
	if (len > 0) {
		buf->tail += len;
		do {
			msg = find_msg(buf);
			if (!msg) {
				if (buf->tail >= buf->size) goto __error;
				goto __recv;
			}
			switch (msg->msg_id) {
			case IPC_SDK_MSG_CONNECT:
				goto __error;	/* This kind of msg is not allowed for this ipc handle */
			case IPC_SDK_MSG_REGISTER:
				goto __error;	/* This kind of msg is not allowed for this ipc handle */
			case IPC_SDK_MSG_SYNC:
				if (ipc_broker_sync(core, ipc, msg) < 0)
					goto __error;
				break;
			case IPC_SDK_MSG_UNREGISTER:
				if (ipc_broker_unreg(core, ipc, msg) < 0)
					goto __error;
				break;
			case IPC_SDK_MSG_NOTIFY:
				if (ipc_broker_publish(core, msg) < 0)
					IPCLOG("broker dispatch notify error.\n");
				break;
			default:
				if (ipc_handler_invoke(core, ipc->sock, msg) < 0)
					goto __error;
				break;
			}
		} while (buf->head < buf->tail);
		return 0;
	}
	if (len == 0) {
		IPCLOG("client: %d shutdown sk:%d\n", ipc->identity, ipc->sock);
		goto __error;
	} else if (errno == EINTR) {
		goto __recv;
	}
	IPCLOG("error: recv length: %d, errno: %d", len, errno);
  __error:
	ipc_release(core, ipc);
	return -1;
}

/**
 * ipc_broker_register - handler for the callback register message from client
 * @core: ipc core of server
 * @sock: socket fd
 * @msg: callback register message
 */
static int ipc_broker_register(struct ipc_core *core, int sock, struct ipc_msg * msg)
{
	struct ipc_server *broker;
	struct ipc_reg *reg = (struct ipc_reg *)msg->data;
	if (!reg->mask) {
		IPCLOG("Incorrect mask.\n");
		goto __fatal;
	}
	/* reg identity always be client's pid */
	broker = ipc_create(core, reg->mask, reg->identity, sock, ipc_common_socket_handler);
	if (!broker)
		goto __fatal;
	/*
	 * Manager hook provide a possibility to manage the clients registered to the server.
	 * user can modify the peer->private_identity filed or fill peer->private_data filed.
	 */
	if (core->manager &&
		core->manager(broker, IPC_CLIENT_REGISTER) < 0) {
		send_msg(sock, msg);
		goto __error;
	}
	IPCLOG("%d register: client %d ,sk: %d, mask: %04lx\n",
					reg->identity, broker->identity, broker->sock, broker->peer->mask);
	/*
	 * In this period, do some negotiation.
	 * max IPC buffer size is always required
	 * identity allocated by user is optional.
	 */
	struct ipc_negotiation *neg = (struct ipc_negotiation *)msg->data;
	neg->identity = broker->peer->private_identity;
	neg->buf_size = core->buf->size;
	msg->msg_id = IPC_SDK_MSG_SUCCESS;
	msg->data_len = sizeof(struct ipc_negotiation);
	if (send_msg(sock, msg) > 0)
		return 0;
__error:
	ipc_release(core, broker);
	return -1;
__fatal:
	send_msg(sock, msg);
	close(sock);
	return -1;
}
/**
 * ipc_channel_register - handler for the channel register message from client
 * @core: ipc core of server
 * @sock: socket fd
 * @msg: channel register message
 */
static int ipc_channel_register(struct ipc_core *core, int sock, struct ipc_msg * msg)
{
	struct ipc_server *ipc;
	struct ipc_identity *cid = (struct ipc_identity *)msg->data;
	ipc = ipc_create(core, 0ul, cid->identity, sock, ipc_common_socket_handler);
	if (ipc) {
		IPCLOG("%d reg channel:sk:%d\n", ipc->identity, ipc->sock);
		msg->msg_id = IPC_SDK_MSG_SUCCESS;
		msg->data_len = 0;
		if (send_msg(sock, msg) < 0) {
			ipc_release(core, ipc);
			return -1;
		}
		return 0;
	}
	send_msg(sock, msg);
	close(sock);
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
		if (sock_opts(sock) < 0)
			goto __error;
		if (recv_msg(sock, buffer, core->buf->size) < 0)
			goto __error;
		struct ipc_msg *msg = (struct ipc_msg *)buffer;
		switch (msg->msg_id){
		case IPC_SDK_MSG_CONNECT:
			return ipc_channel_register(core, sock, msg);
		case IPC_SDK_MSG_REGISTER:
			return ipc_broker_register (core, sock, msg);
		case IPC_SDK_MSG_SYNC:
			goto __error;	/* This kind of msg is not allowed for this ipc handle */
		case IPC_SDK_MSG_UNREGISTER:
			goto __error;	/* This kind of msg is not allowed for this ipc handle */
		case IPC_SDK_MSG_NOTIFY:
			if (ipc_broker_publish(core, msg) < 0)
				IPCLOG("broker dispatch notify error.\n");
			break;
		default:
			if (ipc_handler_invoke(core, sock, msg) < 0)
				goto __error;
			break;
		}
		close(sock);
		return 0;
	  __error:
	  	IPCLOG("error\n");
		close(sock);
		return -1;
	} else {
		if (errno == EINTR)
			goto __accept;
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
        IPC_ERR(__LOGTAG__,"Unable to create socket: %s", strerror(errno));
        return -1;
    }
    serv_adr.sun_family = AF_UNIX;

    strncpy(serv_adr.sun_path, path, sizeof(serv_adr.sun_path));

    unlink(path);

    if (bind(sock, (struct sockaddr *)&serv_adr, (socklen_t)sizeof(struct sockaddr_un)) < 0) {
        IPC_ERR(__LOGTAG__,"Unable to bind socket: %s", strerror(errno));
		close(sock);
        return -1;
    }
	if (listen(sock, BACKLOG) < 0) {
		IPC_ERR(__LOGTAG__,"Unable to listen socket: %s", strerror(errno));
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
	master = ipc_create(core, 0ul, 0, sock,
		ipc_master_socket_handler);
	if (master == NULL) {
		close(sock);
		return -1;
	}
	IPCLOG("master sock:%d\n", master->sock);
	return 0;
}
/**
 * ipc_timing_register - Register a timing-event.
 * Recommend to be called in the context of %handler callback passed in ipc_server_init().
 * @timing: timing handle initialized via ipc_timing_initializer().
 */
int ipc_timing_register(struct ipc_timing *timing)
{
	struct ipc_timing *tmp;
	struct list_head *p;
	struct ipc_core *core = current_core();
	if (uninitialized(core))
		return -1;
	timing_refresh(timing, &timing->tv);
	ipc_mutex_lock(core->mutex);
	list_del_init(&timing->list);
	list_for_each(p, &core->timing) {
		tmp = list_entry(p, struct ipc_timing, list);
		if (timercmp(&timing->expire, &tmp->expire, <))
			break;
	}
	list_add_tail(&timing->list, p);
	ipc_mutex_unlock(core->mutex);
	return 0;	
}
int ipc_timing_unregister(struct ipc_timing *timing)
{
	struct ipc_core *core = current_core();
	if (uninitialized(core))
		return -1;
	ipc_mutex_lock(core->mutex);
	list_del_init(&timing->list);
	ipc_mutex_unlock(core->mutex);
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
	if (uninitialized(core))
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
static int ipc_loop(struct ipc_core *core)
{
	int res;
	int nfds;
	fd_set readfds;
	struct timeval tv, now, *timeout;
	struct ipc_timing *timing;
	struct ipc_server *ipc,*tmp;
	while (!core->terminated
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
		FD_ZERO(&readfds);
		nfds = 0;
		list_for_each_entry(ipc, &core->head, list) {
			IPC_INFO(__LOGTAG__,"socket:%d\n", ipc->sock);
			FD_SET(ipc->sock, &readfds);
			if (ipc->sock > nfds) nfds = ipc->sock;
		}
	 __select:
		res = select(nfds + 1, &readfds, NULL, NULL, timeout);
		if (res < 0) {
			/* On Linux, the function select modifies timeout to reflect the amount of time not slept */
			if (errno == EINTR)
				goto __select;
			IPCLOG("select error:%d\n", errno);
			break;
		}
		if (!list_empty(&core->timing)) {
			struct ipc_timing *tt;
			timing_time(&now);
			list_for_each_entry_safe(timing, tt, &core->timing, list) {
				if (!timercmp(&now, &timing->expire, <)) {
					if (timing->cycle)
						ipc_timing_register(timing);
					else
						ipc_timing_unregister(timing);
					timing->handler(timing);
				} else break;
			}
		}	
		if (res > 0) {
			list_for_each_entry_safe(ipc, tmp, &core->head, list) {
				if (FD_ISSET(ipc->sock, &readfds)) {
					ipc->handler(core, ipc);
				}
			}
		} 
	}
	return -1;
}
/**
 * ipc_server_init - init the ipc core
 * @server: the name of ipc server
 * @handler: callback used to process messages,
 *			 The handler should return -1 on failure, 0 on success
 *			 If success, the server core will give the expected response to the client
 */
int ipc_server_init(const char *server, int (*handler)(struct ipc_msg*))
{
	char *path = NULL;
	char *serv = NULL;
	if (!server || !handler)
		return -1;
	path = (char *)malloc(sizeof(UNIX_SOCK_DIR) + strlen(server));
	if (!path)
		return -1;
	serv = strdup(server);
	if (!serv) {
		free(path);
		return -1;
	}
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
	core->path 	  = (const char *)path;
	core->server  = (const char *)serv;
	core->terminated = 0;
	core->initialized = 1;
	return ipc_master_init(core);
}
/**
 * ipc_server_run - after init ipc core successfully, call this to run ipc core
 */
int ipc_server_run()
{
	struct ipc_core *core = current_core();
	if (uninitialized(core))
		return -1;
	/*
	 * The IPC buffer can be set by ipc_server_setopt(),
	 * if null, user IPC_MSG_BUFFER_SIZE as the default size.
	 */
	if (!core->buf)
		core->buf = alloc_buf(IPC_MSG_BUFFER_SIZE);
	if (!core->buf)
		return -1;
	IPCLOG("mutex:%p,filter:%p,manager:%p, buf size:%u\n", core->mutex,
			core->filter,
			core->manager,
			core->buf->size);
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
	if (uninitialized(core))
		return -1;
	core->terminated = 1;
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
	if (core->server) {
		free((void *)core->server);
		core->server = NULL;
	}
	if (core->node_hb) {
		free(core->node_hb);
		core->node_hb = NULL;
	}
	if (core->buf) {
		free(core->buf);
		core->buf = NULL;
	}
	core->initialized = 0;
	ipc_mutex_unlock(core->mutex);
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
int ipc_server_publish(int to, unsigned long topic, int msg_id, void *data, int size)
{
	int dynamic = 0;
	char buffer[IPC_NOTIFY_MSG_MAX_SIZE];
	struct ipc_node *node;
	struct ipc_msg *ipc_msg = (struct ipc_msg *)buffer;
	struct ipc_core *core = current_core();
	if (uninitialized(core))
		return -1;
	if (!topic_check(topic))
		return -1;
	if (!ipc_notify_space_check(sizeof(buffer), size)) {
		ipc_msg = ipc_alloc_msg(sizeof(struct ipc_notify) + size);
		if (!ipc_msg) {
			IPCLOG("Server publish memory failed.\n");
			return -1;
		} else dynamic = 1;
	}
	notify_pack(ipc_msg, to, topic, msg_id, data, size);
	int i = bit_index(topic);
	ipc_mutex_lock(core->mutex);
	/*
	 * Node hash bucket has not been initialized.
	 * This indicates that no clients register to server.
	 */
	if (!core->node_hb)
		goto unlock;
	if (to == IPC_TO_BROADCAST)
		msg_broadcast(node, &core->node_hb[i], ipc_msg);
	else
		msg_unicast(node, &core->node_hb[i], ipc_msg, to);
unlock:
	ipc_mutex_unlock(core->mutex);
	if (dynamic)
		ipc_free_msg(ipc_msg);
	return 0;
}
/**
 * ipc_server_notify - if the server as broker, server can call this function to notify a client
 * This function is non-thread-safe can be called only in your %handler callback passed by ipc_server_init().
 * @cli: client the message sent to
 * @topic: message type
 * @msg_id: message id
 * @data: message data, if no data carried, set NULL
 * @size: the length of message data
 *
 */
int ipc_server_notify(const struct ipc_server *cli, unsigned long topic, int msg_id, void *data, int size)
{
	int dynamic = 0;
	char buffer[IPC_NOTIFY_MSG_MAX_SIZE];
	struct ipc_msg *ipc_msg = (struct ipc_msg *)buffer;
	if (!cli->peer ||
		!topic_check(topic) ||
		!topic_isset(cli->peer, topic))
		return -1;
	if (!cli->peer->node[bit_count(cli->peer->mask & (topic -1))].sevr) {
		IPCLOG("client has not synchronized.\n");
		return -1;
	}
	if (!ipc_notify_space_check(sizeof(buffer), size)) {
		ipc_msg = ipc_alloc_msg(sizeof(struct ipc_notify) + size);
		if (!ipc_msg) {
			IPCLOG("Server notify memory failed.\n");
			return -1;
		} else dynamic = 1;
	}
	notify_pack(ipc_msg, IPC_TO_NOTIFY, topic, msg_id, data, size);
	msg_notify(cli, ipc_msg);
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
	struct ipc_mutex *mutex = (struct ipc_mutex *)arg;
	if (!mutex)
		return -1;
	if (mutex->lock &&
		mutex->unlock) {
		core->mutex = mutex;
		return 0;
	}
	return -1;
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
int ipc_server_proxy(int fd, int (*proxy)(int, void *), void *arg)
{
	struct ipc_core *core = current_core();
	if (uninitialized(core))
		return -1;
	if (fd <= 0)
		return -1;
	if (!proxy)
		return -1;
	struct ipc_server *ipc = ipc_create(core, 0ul, 0, fd, ipc_proxy_socket_handler);
	if (!ipc)
		return -1;
	ipc->arg = arg;
	ipc->proxy = proxy;
	return 0;
}
/**
 * ipc_server_setopt - used to set ipc core options,
 * always call this after calling ipc_server_init() and before ipc_server_run().
 * @opt: defined in enum IPC_SERVER_OPTION
 * @arg: the option parameter
 */
int ipc_server_setopt(int opt, void *arg)
{
	if (!arg)
		return -1;
	struct ipc_core *core = current_core();
	if (uninitialized(core))
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
	default:
		return -1;
	}
}
