/*
 * Copyright (c) 2020, <-Jason Chen->
 * Version: 1.2.1 - 20230316
 *				  - Optimize ipc_server_bind(IPC_COOKIE_ASYNC):
 *					Remove allocating fixed length of ipc_msg for ipc_async in ipc_server_bind(),
 *					and move the buffer allocating to ipc_async_execute().
 *				  - Optimize ipc_async_execute():
 *					Add arg @size for allocating specific length of ipc_msg for ipc_async.
 *				  - Modify log tag, using comm instead.
 * Version 1.1.0. modified at 2020/05/20
 * Author: Jie Chen <jasonchen0720@163.com>
 * Brief : IPC internal definitions
 * Date  : Created at 2020/05/20
 */

#include <string.h>
#include <errno.h>
#include <assert.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <sys/select.h>
#include <sys/ioctl.h>
#include "ipc_log.h"
#include "ipc_base.h"
#include "ipc_common.h"
static const char *__base_tag = DUMMY_NAME;
#define __LOGTAG__ __base_tag
const char * self_name()
{
	static char name[17] = DUMMY_NAME;
	const char **p = &__base_tag;
	int done = !ATOMIC_BCS(p, DUMMY_NAME, name);
	if (done)
		return name;
	int fd = open("/proc/self/comm", O_RDONLY);
	if (fd < 0)
		goto err;
	int size = read(fd, name, sizeof(name) - 1);
	if (size <= 0) {
		close(fd);
		goto err;
	}
	close(fd);
	if (name[size - 1] == '\n')
		name[size - 1] = '\0';
	else
		name[size] = '\0';
	return (const char *)name;
err:
	sprintf(name, "%d", getpid());
	return (const char *)name; 
}	
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
		"recv error", "send error", "connect error", "bad message", "Invalid argument"
	};
	assert(err > IPC_EMIN && err < IPC_EMAX);
	return errmsg[err];
}
struct ipc_msg * find_msg(struct ipc_buf *buf)
{
	int left;
	int msg_len;
	IPC_INFO("head: %d, tail: %d.", buf->head, buf->tail);
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
			IPC_LOGE("recv unfriendly message %08x", message->msg_id);
			continue;
		}
		/**
		 * If current position address is not byte aligned, 
		 * as the beginning address is always byte aligned, 
		 * move this message to the beginning of the buffer.
		 */
		if (buf->head & (IPC_MSG_ALIGN - 1)) {
			IPC_LOGW("Not aligned %u, move msg %d bytes", buf->head, msg_len);
			memcpy(buf->data, buf->data + buf->head, msg_len);
			message = (struct ipc_msg *)buf->data;
		}	
		buf->head += msg_len;
		IPC_INFO("message found: %p, length: %d, head: %d, tail: %d, left %d bytes.", message, msg_len, buf->head, buf->head, left - msg_len);
		return message;
	}
	return NULL;
  __move:
  	IPC_LOGW("Incomplete msg, occupied %d bytes, buf size: %u bytes.", left, buf->size);
	if (buf->head)
		move_to_beginning(buf, left);
	return NULL;
}
/**
 * sock_opts - set socket options
 * @sock: socket to set
 */
int sock_opts(int sock, int tmo)
{
	if (tmo > 0) {
		struct timeval timeout;
		timeout.tv_sec 	= tmo;
		timeout.tv_usec = 0;
		if (setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) < 0) {
			IPC_LOGE("setsockopt: sk[%d], errno:%d", sock, errno);
			return -1;
		}
		if (setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0) {
			IPC_LOGE("setsockopt: sk[%d], errno:%d", sock, errno);
			return -1;
		}
	} else {
		int nb = 1;
	    if (ioctl(sock, FIONBIO, &nb) == -1)
			return -1;
	}

	if (fcntl(sock, F_SETFD, FD_CLOEXEC) == -1) {
		return -1;
	}
	return 0;
}

int recv_msg(int sock,  char *buf, unsigned int size, int tmo)
{
	fd_set rfds;
	FD_ZERO(&rfds);
	FD_SET(sock, &rfds);
	struct timeval timeout = {.tv_sec  = tmo, .tv_usec = 0};
	
	int len;
	int offset = 0;
	struct ipc_msg *msg = (struct ipc_msg *)buf;
	if (!size)
		return IPC_RECEIVE_EVAL;
retry:
	len = select(sock + 1, &rfds, NULL, NULL, &timeout);

	if (len == 0) {
		IPC_LOGE("Select timedout with %d secs.", tmo);
		return IPC_RECEIVE_TMO;
	}
	if (len < 0) {
		if (errno == EINTR)
			goto retry;
		IPC_LOGE("Select errno:%d", errno);
		return IPC_RECEIVE_ERR;
	}
  	do {
		len = recv(sock, buf + offset, size - offset, 0);
		if (len > 0) {
			offset += len;
			if (offset < sizeof(struct ipc_msg) || offset < __data_len(msg)) {
				IPC_LOGW("receive unfinished bytes: size: %d, offset: %d", size, offset);
				if (offset < size)
					continue;
				return IPC_RECEIVE_EMEM;
			}
			return friendly(msg) ? offset : IPC_RECEIVE_EMSG;
		}
		if (len == 0)
			return IPC_RECEIVE_EOF;
		if (errno == EINTR)
			continue;
		if (errno == EAGAIN || errno == EWOULDBLOCK)
			return IPC_RECEIVE_TMO;
		IPC_LOGE("Recv errno:%d", errno);			
		break;
	} while (1);
	return IPC_RECEIVE_ERR;
}
struct ipc_msg * ipc_clone_msg(const struct ipc_msg *msg, unsigned int size)
{
	size_t s = __data_len(msg);
	size += sizeof(struct ipc_msg);
	struct ipc_msg * clone_msg = (struct ipc_msg *)malloc(size > s ? size : s);

	if (clone_msg) {
		memcpy(clone_msg, msg, s);
	}
	IPC_LOGI("clone message: %p", clone_msg);
	return clone_msg;
}
struct ipc_msg * ipc_alloc_msg(unsigned int size)
{
	struct ipc_msg * msg = (struct ipc_msg *)malloc(sizeof(struct ipc_msg) + size);

	if (msg) {
		msg->msg_id = 0;
		msg->flags	= 0;
		msg->data_len = 0;
	}
	IPC_LOGI("alloc message: %p", msg);
	return msg;
}
void ipc_free_msg(struct ipc_msg *msg)
{
	IPC_LOGI("free message: %p", msg);
	free((void *)msg);
}
