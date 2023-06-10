#ifndef __IPC_BASE_H__
#define __IPC_BASE_H__
/*
 * Copyright (c) 2017, <-Jason Chen->
 * Version 1.1.0. modified at 2020/05/20
 * Author: Jie Chen <jasonchen@163.com>
 * Brief : This header file only used for IPC internal definitions, users needn't include this header file.
 * Date  : Created at 2020/05/20
 */
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include "ipc_common.h"
#include "ipc_atomic.h"
#define UNIX_SOCK_DIR "/tmp/"

#define IPC_RECEIVE_TMO			-IPC_ETIMEOUT
#define IPC_RECEIVE_EOF			-IPC_EOF
#define IPC_RECEIVE_EMEM		-IPC_EMEM
#define IPC_RECEIVE_ERR			-IPC_ERECV
#define IPC_RECEIVE_EMSG 		-IPC_EMSG
#define IPC_RECEIVE_EVAL		-IPC_EVAL

#define IPC_MSG_TOKEN		0x56CE0000
#define IPC_MSG_TOKEN_MASK	0xFFFF0000
#define IPC_MSG_SDK			0x0000FF00
#define IPC_MSG_SDK_MASK	0xFFFFFF00
#define IPC_MSG_SDK_TIMEOUT	5
enum IPC_STATE {
	IPC_S_CONNECTING,
	IPC_S_CONNECTED,
	IPC_S_DISCONNECTING,
	IPC_S_DISCONNECTED,
};
#define set_state(c, s)	ATOMIC_SET(&(c)->state, s)
#define get_state(c)	ATOMIC_GET(&(c)->state)
/* User's msg_id must be in range: 0x0000~0xff00, 0xff01~0xffff used for IPC SDK */
enum IPC_MSG_ID
{
	IPC_SDK_MSG_SUCCESS			= IPC_MSG_SDK,
	IPC_SDK_MSG_CONNECT,
	IPC_SDK_MSG_REGISTER,    	/* client send this msg, launch a callback register */
	IPC_SDK_MSG_SYNC,			/* client send this msg to notify server that client is ready and callback begin to work */
	IPC_SDK_MSG_UNREGISTER,		/* server push this msg to client, release the callback */
	IPC_SDK_MSG_NOTIFY			/* server push this msg to client's callback */
};
struct ipc_identity
{
	int identity;
};
struct ipc_reg
{
	unsigned long 	mask;
	unsigned int 	data_len;
	char 			data[0];
};
struct ipc_buf
{
	unsigned int 	head;
	unsigned int 	tail;
	unsigned int 	size;
	char 			data[0];
};
#define ipc_buf_full(buf)			((buf)->tail >= (buf)->size)
#define ipc_buf_pending(buf)		((buf)->head < (buf)->tail)
#define ipc_buf_reset(buf)			((buf)->tail = (buf)->head = 0u)
struct ipc_negotiation
{
	unsigned int 	buf_size;
};
#define gettid()					syscall(__NR_gettid)
#define IPC_NOTIFY_MSG_MAX_SIZE 1024
#define server_offset(path)			((path) + sizeof(UNIX_SOCK_DIR) - 1)
#define users_msg(msg)				((msg)->msg_id < IPC_MSG_SDK)
#define topic_isset(pr, topic) 		((pr)->mask & (topic))
#define __data_len(pr)				(sizeof(*(pr)) + (pr)->data_len)
#define __bit(nr) 					(1 << (nr))
#define __set_bit(nr, flag)		\
	do {						\
		(flag) |=  __bit(nr);	\
	} while (0)
#define __clr_bit(nr, flag)		\
	do {						\
		(flag) &= ~(__bit(nr));	\
	} while (0)
#define __test_bit(nr, flag)	((flag) & __bit(nr))
#define DUMMY_NAME	"dummy"
const char * self_name();
static inline int topic_check(unsigned long topic)
{
	return !(topic & (topic -1));
}
static inline void ipc_notify_pack(struct ipc_msg *msg, int to, unsigned long topic, int msg_id, void *data, int size)
{
	struct ipc_notify *notify = (struct ipc_notify *)msg->data;
	
	notify->topic = topic;
	notify->msg_id = msg_id;
	notify->to = to;
	if (data) {
		memcpy(notify->data, data, size);
		notify->data_len = size;
	} else {
		notify->data_len = 0;
	}
	msg->msg_id = IPC_SDK_MSG_NOTIFY;
	msg->data_len = __data_len(notify);
}
static inline int send_msg(int sock, struct ipc_msg *msg)
{
	/*
	 * messages ids should be in range: 0x0000~0xffff, 
	 * 0x0000~0xff00 used for user's specific definitions, 0xff01~0xffff used for IPC SDK
	 */
	msg->msg_id |= IPC_MSG_TOKEN;
	return send(sock, (void *)msg,  __data_len(msg), MSG_NOSIGNAL | MSG_DONTWAIT);
}
struct ipc_buf * alloc_buf(unsigned int size);
const char * strerr(int err);
struct ipc_msg * find_msg(struct ipc_buf *buf, struct ipc_msg *clone);
int recv_msg(int sock,  char *buf, unsigned int size, int tmo);
int sock_opts(int sock, int tmo);
#endif