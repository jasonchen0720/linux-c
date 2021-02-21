#ifndef __IPC_BASE_H__
#define __IPC_BASE_H__
/*
 * Copyright (c) 2017, <-Jason Chen->
 * Version 1.1.0. modified at 2020/05/20
 * Author: Jie Chen <jasonchen@163.com>
 * Note  : This head file only used for libipc internal definitions, users could not include this file
 * Date  : Created at 2020/05/20
 */
#include <stdlib.h>
#include <sys/socket.h>
#include "ipc_common.h"

#define UNIX_SOCK_DIR "/tmp/"

#define IPC_RECEIVE_TMO			-IPC_ETIMEOUT
#define IPC_RECEIVE_EOF			-IPC_EOF
#define IPC_RECEIVE_EMEM		-IPC_EMEM
#define IPC_RECEIVE_ERR			-IPC_ERECV


#define IPC_MSG_TOKEN		0x56CE0000
#define IPC_MSG_TOKEN_MASK	0xFFFF0000
#define IPC_MSG_SDK			0x0000FF00
#define IPC_MSG_SDK_MASK	0xFFFFFF00

/* User's msg_id must be in range: 0x0000~0xff00, 0xff01~0xffff used for IPC SDK */
enum IPC_MSG_ID
{
	IPC_SDK_MSG_SUCCESS			= 0xff00,
	IPC_SDK_MSG_CONNECT  		= 0xff01,
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
	int identity;
	unsigned long mask;
};
struct ipc_buf
{
	unsigned int 	head;
	unsigned int 	tail;
	unsigned int 	size;
	char 			data[0];
};
struct ipc_negotiation
{
	int 			identity;
	unsigned int 	buf_size;
};
#define IPC_NOTIFY_MSG_MAX_SIZE 1024
#define msg_buf_abundant(max, size)	((max) >= sizeof(struct ipc_msg) + (size))
#define ntf_buf_abundant(max, size)	msg_buf_abundant(max, sizeof(struct ipc_notify) + size)
#define users_msg(msg)				((msg)->msg_id < IPC_MSG_SDK)
#define topic_isset(pr, topic) 		((pr)->mask & (topic))
#define __data_len(pr)					(sizeof(*(pr)) + (pr)->data_len)
#define __set_bit(nr, flag)			\
	do {							\
		(flag) |=  (1u << (nr));	\
	} while (0)
#define __clr_bit(nr, flag)			\
	do {							\
		(flag) &= ~(1u << (nr));	\
	} while (0)
#define __test_bit(nr, flag)	((flag) & (1u << (nr)))

#define expect_reply(msg) 	__test_bit(IPC_BIT_REPLY, (msg)->flags)
static inline int topic_check(unsigned long topic)
{
	return !(topic & (topic -1));
}
static inline void notify_pack(struct ipc_msg *msg, int to, unsigned long topic, int msg_id, void *data, int size)
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
static inline struct ipc_buf * alloc_buf(unsigned int size)
{
	struct ipc_buf *buf = (struct ipc_buf *)malloc(sizeof(struct ipc_buf) + size);
	if (buf) {
		buf->head = 0u;
		buf->tail = 0u;
		buf->size = size;
	}
	return buf;
}
const char * strerr(int err);
struct ipc_msg * find_msg(struct ipc_buf *buf);
int recv_msg(int sock,  char *buf, unsigned int size);
#endif