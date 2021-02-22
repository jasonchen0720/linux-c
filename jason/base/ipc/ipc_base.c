/*
 * Copyright (c) 2017, <-Jason Chen->
 * Version 1.1.0. modified at 2020/05/20
 * Author: Jie Chen <jasonchen@163.com>
 * Note  : This program is used for libipc internal definitions
 * Date  : Created at 2020/05/20
 */

#include <string.h>
#include <errno.h>
#include <assert.h>
#include "ipc_log.h"
#include "ipc_base.h"
#include "ipc_common.h"
#define IPCLOG(format,...) IPC_LOG("%s(%d): "format"\n", __FUNCTION__, __LINE__, ##__VA_ARGS__)

/* 
 *	4-byte or 8-byte aligned 
 */
#define IPC_MSG_ALIGN		0x04

#define move_to_beginning(buf, left) \
	do {	\
		memcpy(buf->data, buf->data + buf->head, left);	\
		buf->head = 0;		\
		buf->tail = left;	\
	} while (0)

/**
 * friendly - check if the ipc meesage received is valid
 * @msg: message to check 
 */
static inline int friendly(struct ipc_msg *msg)
{
	if ((msg->msg_id & IPC_MSG_TOKEN_MASK) == IPC_MSG_TOKEN) {
		msg->msg_id &= ~IPC_MSG_TOKEN_MASK;
		return 1;
	}
	return 0;
}
const char * strerr(int err)
{
	static const char *errmsg[IPC_EMAX] = {
		"success",	  "timedout", 	"peer shutdown", "memory full",	  
		"recv error", "send error", "connect error", "bad message"
	};
	assert(err > IPC_EMIN && err < IPC_EMAX);
	return errmsg[err];
}
struct ipc_msg * find_msg(struct ipc_buf *buf)
{
	int left;
	int msg_len;
	IPC_INFO(__FILE__, "head: %d, tail: %d.\n", buf->head, buf->tail);
	for (left = buf->tail - buf->head; left > 0; buf->head += msg_len, left = buf->tail - buf->head){
		/* 
		 * If the left size is smaller than the head length of ipc msg,
		 * this case usually occurs when some bytes have not been read,
		 * return and continue to read.
		 */
		if (left < sizeof(struct ipc_msg))
			goto __move;
		
		struct ipc_msg *message = (struct ipc_msg *)(buf->data + buf->head);
		
		msg_len = __data_len(message);
		/* 
		 * If the left size is smaller than the total length of ipc msg,
		 * this case usually occurs when some bytes have not been read,
		 * return and continue to read.
		 */
		if (left < msg_len)
			goto __move;

		if (!friendly(message)) {
			IPCLOG("recv unfriendly message %08x\n", message->msg_id);
			continue;
		}
		/**
		 * If current position address is not byte aligned, 
		 * as the beginning address is always byte aligned, 
		 * move this message to the beginning of the buffer.
		 */
		if (buf->head & (IPC_MSG_ALIGN - 1)) {
			IPCLOG("Not aligned %u, move msg %d bytes\n", buf->head, msg_len);
			memcpy(buf->data, buf->data + buf->head, msg_len);
			message = (struct ipc_msg *)buf->data;
		}	
		buf->head += msg_len;
		IPC_INFO(__FILE__, "message found: %p, length: %d, head: %d, tail: %d, left %d bytes.\n", message, msg_len, buf->head, buf->head, left - msg_len);
		return message;
	}
	return NULL;
  __move:
  	IPCLOG("Incomplete msg, occupied %d bytes, buf size: %u bytes.\n", left, buf->size);
	if (buf->head)
		move_to_beginning(buf, left);
	return NULL;
}


int recv_msg(int sock,  char *buf, unsigned int size)
{
	int len;
	int offset = 0;
	struct ipc_msg *msg = (struct ipc_msg *)buf;
	if (!size)
		return 0;
  	do {
		len = recv(sock, buf + offset, size - offset, 0);
		if (len > 0) {
			offset += len;
			if (offset < sizeof(struct ipc_msg) || offset < __data_len(msg)) {
				IPCLOG("receive unfinished bytes: size: %d, offset: %d\n", size, offset);
				if (offset < size)
					continue;
				return IPC_RECEIVE_EMEM;
			}
			if (!friendly(msg))
				break;
			return offset;
		}
		if (len == 0)
			return IPC_RECEIVE_EOF;
		if (errno == EINTR)
			continue;
		if (errno == EAGAIN || errno == EWOULDBLOCK)
			return IPC_RECEIVE_TMO;
		break;
	} while (1);
	return IPC_RECEIVE_ERR;
}
struct ipc_msg * ipc_alloc_msg(unsigned int size)
{
	struct ipc_msg * msg = (struct ipc_msg *)malloc(sizeof(struct ipc_msg) + size);

	if (msg) {
		msg->msg_id = 0;
		msg->flags	= 0;
		msg->data_len = 0;
	}
	IPCLOG("alloc message buf: %p\n", msg);
	return msg;
}
void ipc_free_msg(struct ipc_msg *msg)
{
	IPCLOG("free message buf: %p\n", msg);
	free((void *)msg);
}
