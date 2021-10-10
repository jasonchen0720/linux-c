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
 * Note  : This program is used for libipc client interface implementation
 * Date  : Created at 2017/11/01
 */
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/un.h>
#include <sys/time.h>
#include <sys/select.h>
#include <pthread.h>
#include "ipc_client.h"
#include "ipc_log.h"
#include "ipc_base.h"
#define __LOGTAG__ comm_name()
#define IPCLOG(format,...) IPC_LOG("[%s]:%s(%d): "format"\n", comm_name(), __FUNCTION__, __LINE__, ##__VA_ARGS__)
static const char * comm_name()
{
	static char name[32]= {0};
	if (name[0])
		return (const char *)name;
	int fd = open("/proc/self/comm", O_RDONLY);
	if (fd < 0)
		return "dummy";
	int size = read(fd, name, sizeof(name) -1);
	if (size <= 0) {
		close(fd);
		return "dummy";
	}
	close(fd);
	if (name[size - 1] == '\n')
		name[size - 1] = '\0';
	else
		name[size] = '\0';
	return (const char *)name;
}
/**
 * ipc_receive - receive some bytes from socket
 * @sock: connected socket fd
 * @buffer: buffer used to store message
 * @size: buffer size
 * @tmo: time of receive timeout
 */
static int ipc_receive(int sock, void *buffer, unsigned int size, int tmo)
{
	int rc;
	int len;
	fd_set rfds;
	struct timeval timeout;
	FD_ZERO(&rfds);
	FD_SET(sock, &rfds);
	timeout.tv_sec  = tmo;
	timeout.tv_usec = 0;
  __select:
	rc = select(sock + 1, &rfds, NULL, NULL, &timeout);
	if (rc > 0)
	{
	  __recv:
		len = recv(sock, buffer, size, 0);
		if (len > 0)
			return len;
		if (len == 0)
			return IPC_RECEIVE_EOF;
		if (errno == EINTR)
			goto __recv;
	}
	else if (rc == 0)
	{
		return IPC_RECEIVE_TMO;
	}
	else
	{
		if (errno == EINTR)
			goto __select;
	}
	return IPC_RECEIVE_ERR;
}
/**
 * ipc_connect - connect to server
 * @addr: server's address, it is the path of unix-domain socket
 */
static int ipc_connect(const char *addr)
{
	int sock;
	int retry = 0;
	struct sockaddr_un server_addr = {0};
	if ((sock = socket(AF_UNIX, SOCK_STREAM, 0)) < 0)
	{
        IPC_ERR(__LOGTAG__,"Unable to create socket: %s", strerror(errno));
		return -1;
    }
    server_addr.sun_family = AF_UNIX;
    strcpy(server_addr.sun_path, addr);
  __retry:
	if (connect(sock, (struct sockaddr *)&server_addr,  (socklen_t)sizeof(struct sockaddr_un)) < 0)
	{
		if (errno == ECONNREFUSED || errno == ENOENT || errno == EINTR)
		{
			if (retry++ < 3)
			{
				sleep(1);
				goto __retry;
			}
		}
		IPC_ERR(__LOGTAG__,"connect to %s error[%s]\n", addr, strerror(errno));
		close(sock);
		return -1;
	}
	return sock;
}
static int ipc_request(struct ipc_client* client, struct ipc_msg *msg, unsigned int size, int tmo)
{
	msg->from = client->identity;
	if (send_msg(client->sock, msg) < 0) {
		IPCLOG("request to %s error: %d, msg: %04x\n", client->server, errno, msg->msg_id);
		return IPC_REQUEST_EMO;
	}
	if (expect_reply(msg)) {
		int rc;
		fd_set rfds;
		struct timeval timeout;
		FD_ZERO(&rfds);
		FD_SET(client->sock, &rfds);
		timeout.tv_sec  = tmo;
		timeout.tv_usec = 0;
	  __select:
		rc = select(client->sock + 1, &rfds, NULL, NULL, &timeout);
		if (rc > 0) {
			switch (recv_msg(client->sock, (char *)msg, size)) {
			case IPC_RECEIVE_TMO:
				rc = IPC_REQUEST_TMO;
				break;
			case IPC_RECEIVE_EOF:
				rc = IPC_REQUEST_EOF;
				break;
			case IPC_RECEIVE_EMEM:
				rc = IPC_REQUEST_EMEM;
				break;
			case IPC_RECEIVE_ERR:
				rc = IPC_REQUEST_EMT;
				break;
			default:
				return IPC_REQUEST_SUCCESS;
			}
		} else if (rc == 0) {
			rc = IPC_REQUEST_TMO;
		} else {
			if (errno == EINTR)
				goto __select;
			rc = IPC_REQUEST_EMT;
		}
		IPCLOG("receive from %s error: %s, messages ID: %04x, \n", client->server, strerr(-rc), msg->msg_id);
		return rc;
	}
	return IPC_REQUEST_SUCCESS;
}

/**
 * ipc_client_init - init a client handle to server
 * @server: server name
 * @client: client handle
 */
int ipc_client_init(const char *server, struct ipc_client *client)
{
	snprintf(client->server, sizeof(client->server), "%s%s", UNIX_SOCK_DIR, server);
    client->sock = ipc_connect(client->server);
	client->identity = getpid();
    if (client->sock > 0)
    {
		return 0;
    }
    return -1;
}
/**
 * ipc_client_connect - client connect to server
 * @client: client handle
 */
static int ipc_client_connect(struct ipc_client *client)
{
	char buffer[IPC_MSG_MINI_SIZE] = {0};
	struct ipc_msg *ipc_msg = (struct ipc_msg *)buffer;
	client->sock = ipc_connect(client->server);
		
	if (client->sock < 0)
		return -1;
	
	ipc_msg->msg_id = IPC_SDK_MSG_CONNECT;
	__set_bit(IPC_BIT_REPLY, ipc_msg->flags);
	struct ipc_identity *cid = (struct ipc_identity *)ipc_msg->data;
	cid->identity = client->identity;
	ipc_msg->data_len = sizeof(struct ipc_identity);
	if (IPC_REQUEST_SUCCESS != ipc_request(client, ipc_msg, sizeof(buffer), 1))
		goto __error;
	
	if (ipc_msg->msg_id != IPC_SDK_MSG_SUCCESS)
		goto __error;
	return 0;
__error:
	ipc_client_close(client);
	return -1;
}
/**
 * ipc_client_create - allocate memory for new client handle and init the client handle
 * @server: server name
 */
struct ipc_client* ipc_client_create(const char *server)
{
	struct ipc_client *client;

	client = (struct ipc_client *)malloc(sizeof(struct ipc_client));
	if (!client)
		return NULL;
	
	memset(client, 0, sizeof(struct ipc_client));
	client->identity = getpid();
	snprintf(client->server, sizeof(client->server), "%s%s", UNIX_SOCK_DIR, server);
    if (ipc_client_connect(client) == 0)
		return client;
	free(client);
	return NULL;
}
/**
 * ipc_client_request - client send a request message to server
 * @client: client handle
 * @msg: request message - must be users' message
 * @size: message size
 * @tmo: time of send timeout
 */
int ipc_client_request(struct ipc_client* client, struct ipc_msg *msg, unsigned int size, int tmo)
{
	/*
	 * Users's messages ID should be smaller than IPC_MSG_SDK
	 */
	return users_msg(msg) ? ipc_request(client, msg, size, tmo) : IPC_REQUEST_EMSG;
}
/**
 * ipc_client_close - shutdown a connection
 * @client: client handle
 */
void ipc_client_close(struct ipc_client* client)
{
	if (client->sock > 0)
	{
		close(client->sock);
	}
	client->sock = -1;
}
/**
 * ipc_client_destroy - shutdown a connection and free the memory occupied by ipc_client structure 
 * @client: client handle to be destroyed
 */
void ipc_client_destroy(struct ipc_client* client)
{
	if (client)
	{
		if (client->sock > 0)
		{
			close(client->sock);
		}
		free(client);
	}
}
/**
 * ipc_client_repair - client repair the connection if disconnect
 * @client: client handle
 */
int ipc_client_repair(struct ipc_client *client)
{
	ipc_client_close(client);

	if (ipc_client_connect(client) == 0)
	{
		IPCLOG("client reconnect success sk:%d\n", client->sock);
		return 0;
	}
    return -1;
}
/**
 * ipc_client_publish - publisher/client publish notification message
 * @client: client handle
 * @to: indicate who the notify message is sent to. 
 * @topic: message type
 * @msg_id: message id
 * @data: message data, if no data carried, set NULL
 * @size: the length of message data
 * @tmo: time of send timeout
 */
int ipc_client_publish(struct ipc_client *client, 
		int to, unsigned long topic, int msg_id, void *data, int size, int tmo)
{
	int dynamic = 0;
	char buffer[IPC_NOTIFY_MSG_MAX_SIZE] = {0};
	struct ipc_msg *msg = (struct ipc_msg *)buffer;
	if (!ipc_notify_space_check(sizeof(buffer), size)) {
		msg = ipc_alloc_msg(sizeof(struct ipc_notify) + size);
		if (!msg) {
			IPCLOG("Client publish memory failed.\n");
			return -1;
		} else dynamic = 1;
	}
	notify_pack(msg, to, topic, msg_id, data, size);
	
	int rc = ipc_request(client, msg, sizeof(buffer), tmo);
	if (dynamic)
		ipc_free_msg(msg);
	return rc;
}
/**
 * ipc_subscriber_report - subscriber report a event message to server without response
 * @subscriber: subscriber handle
 * @msg: event message - must be users' message.
 */
int ipc_subscriber_report(struct ipc_subscriber *subscriber, struct ipc_msg *msg)
{
	/*
	 * Users's messages ID should be smaller than IPC_MSG_SDK
	 */
	if (!users_msg(msg))
		return IPC_REQUEST_EMSG;

	/*
	 * Report a msg to server.
	 * If a response expected, please call ipc_subscriber_request() instead.
	 */
	__clr_bit(IPC_BIT_REPLY, msg->flags);
	msg->from = subscriber->client.identity;
	return send_msg(subscriber->client.sock, msg) > 0 ? IPC_REQUEST_SUCCESS : IPC_REQUEST_EMO;
}
/**
 * ipc_subscriber_request - subscriber send a request message to server
 * @subscriber: subscriber handle
 * @msg: request message - must be users' message
 * @size: message size
 * @tmo: time of send timeout
 */
int ipc_subscriber_request(struct ipc_subscriber *subscriber, struct ipc_msg *msg, unsigned int size, int tmo)
{
	int rc;
	struct ipc_client client;
	strcpy(client.server, subscriber->client.server);
	client.identity = subscriber->client.identity;
	client.sock = ipc_connect(client.server);
	if (client.sock < 0)
		return IPC_REQUEST_ECN;
	rc = ipc_client_request(&client, msg, size, tmo);
	close(client.sock);
	IPCLOG("subscriber[%d] request:%04x %s\n", subscriber->client.identity, msg->msg_id, strerr(-rc));
	return rc;
}
/**
 * ipc_subscriber_destroy - shutdown a connection and free the memory occupied by ipc_subscriber structure 
 * @subscriber: subscriber handle to be destroyed
 */
void ipc_subscriber_destroy(struct ipc_subscriber *subscriber)
{
	if (subscriber)
	{
		if (subscriber->client.sock > 0)
		{
			close(subscriber->client.sock);
		}
		if (subscriber->buf)
			free(subscriber->buf);
		free(subscriber);
	}
}
/**
 * ipc_subscriber_connect - subscriber connect to server
 * @subscriber: subscriber handle
 */
static int ipc_subscriber_connect(struct ipc_subscriber *subscriber)
{
	char buffer[IPC_MSG_MINI_SIZE] = {0};
	struct ipc_msg *ipc_msg = (struct ipc_msg *)buffer;
	struct ipc_reg *reg = (struct ipc_reg *)ipc_msg->data;

	subscriber->client.sock = ipc_connect(subscriber->client.server);
	if (subscriber->client.sock < 0)
		return -1;
	
	ipc_msg->msg_id = IPC_SDK_MSG_REGISTER;
	__set_bit(IPC_BIT_REPLY, ipc_msg->flags);
	reg->mask = subscriber->mask;
	reg->identity = subscriber->client.identity;
	ipc_msg->data_len = sizeof(struct ipc_reg);
	if (IPC_REQUEST_SUCCESS != ipc_request(&subscriber->client, ipc_msg, sizeof(buffer),1))
		goto __error;
	
	if (ipc_msg->msg_id != IPC_SDK_MSG_SUCCESS)
		goto __error;
	
	struct ipc_negotiation *neg = (struct ipc_negotiation *)ipc_msg->data;
	/* 
	 * negotiation information.
	 * identity server allocate a new identity for the subscriber.
	 * buf_size must be required, it tells the client the max IPC msg size of buffer to allocate.
	 */
	subscriber->identity = neg->identity;
	if (!subscriber->buf)
		subscriber->buf = (void *)alloc_buf(neg->buf_size);
	if (!subscriber->buf)
		goto __error;
	IPCLOG("ipc buf size: %u.\n",neg->buf_size);
	return 0;
__error:
	ipc_client_close(&subscriber->client);
	return -1;
}
/**
 * ipc_subscriber_sync - subscriber send the callback synchronize message to server
 * @subscriber: subscriber handle
 */
static int ipc_subscriber_sync(struct ipc_subscriber *subscriber)
{
	char buffer[IPC_MSG_MINI_SIZE] = {0};
	struct ipc_msg *msg = (struct ipc_msg *)buffer;
	msg->msg_id = IPC_SDK_MSG_SYNC;
	msg->data_len = 0;
	msg->from = subscriber->client.identity;
	IPCLOG("sync to server, client: %d, mask: %04lx\n", subscriber->client.identity, subscriber->mask);
	return send_msg(subscriber->client.sock, msg) > 0 ? 0 : -1;
}
/**
 * ipc_subscriber_repair - subscriber repair the connection if disconnect
 * @subscriber: subscriber handle
 */
static int ipc_subscriber_repair(struct ipc_subscriber *subscriber)
{
	ipc_client_close(&subscriber->client);

	if (ipc_subscriber_connect(subscriber) == 0)
	{
		IPCLOG("subscriber rebuild success sk:%d, client %d\n", subscriber->client.sock,  subscriber->client.identity);
		ipc_subscriber_sync(subscriber);
		return 0;
	}
	return -1;
}
/**
 * ipc_subscriber_process - subscriber process asynchronous messages from server
 * @subscriber: subscriber handle
 * @buf: asynchronous messages
 * @length: total length of asynchronous messages store in %buf
 */
static void ipc_subscriber_process(struct ipc_subscriber *subscriber, struct ipc_buf *buf)
{
	struct ipc_msg *msg;
	struct ipc_notify *notify;
	do {
		msg = find_msg(buf);
		if (!msg)
			return;
		if (msg->msg_id == IPC_SDK_MSG_NOTIFY) {
			notify = (struct ipc_notify *)msg->data;
			if (topic_isset(subscriber, notify->topic) && 
				topic_check(notify->topic))
				subscriber->handler(notify->msg_id, notify->data, notify->data_len, subscriber->arg);
		} else if (msg->msg_id == IPC_SDK_MSG_UNREGISTER) {
			IPCLOG("subscriber task exit\n");
			subscriber->mask = 0ul;
			pthread_exit(NULL);
		} else {
			IPCLOG("unexpected msg:%04x, %d:%d\n",msg->msg_id, buf->head, buf->tail);
			break;
		}
	} while (buf->head < buf->tail);
	IPC_INFO(__LOGTAG__, "tail: %d, head: %d\n", buf->tail, buf->head);
	buf->tail = 0;
	buf->head = 0;
}

/**
 * ipc_subscriber_task - subscriber callbacks entry
 */
static void * ipc_subscriber_task(void *arg)
{
	int rc;
	struct ipc_subscriber *subscriber = (struct ipc_subscriber *)arg;
	struct ipc_buf *buf = (struct ipc_buf *)subscriber->buf;
	ipc_subscriber_sync(subscriber);
	while (subscriber->mask) {
		rc = ipc_receive(subscriber->client.sock, 
						buf->data + buf->tail, 
						buf->size - buf->tail, 30);
		if (rc > 0) {
			IPC_INFO(__LOGTAG__, "receive %d bytes, offset: %d.\n", rc, buf->head);
			buf->tail += rc;
			ipc_subscriber_process(subscriber, buf);
			if (buf->tail >= buf->size)
				IPCLOG("subscriber buffer full, size: %u.%u\n", buf->tail, buf->size);
		} else {
			switch (rc) {
			case IPC_RECEIVE_TMO:
				break;
			case IPC_RECEIVE_EOF:
			case IPC_RECEIVE_ERR:
				IPCLOG("receive from %s, error: %s\n", subscriber->client.server, strerr(-rc));
				while(subscriber->mask && (ipc_subscriber_repair(subscriber) < 0))
					sleep(5);
				buf->head = 0u;
				buf->tail = 0u;
				break;
			default:
				break;
			}
		}
	}
	return NULL;
}
static int ipc_subscriber_run(struct ipc_subscriber *subscriber)
{
	return pthread_create(&subscriber->task_id, NULL, ipc_subscriber_task, (void *)subscriber);
}
/**
 * ipc_subscriber_create - allocate memory for new subscriber handle and init the subscriber handle
 * @broker: broker/server name
 * @mask: the mask of asynchronous messages interested
 * @handle: callback to process asynchronous messages
 */
static struct ipc_subscriber *ipc_subscriber_create(const char *broker, 
													unsigned long mask, 
													ipc_subscriber_handler handler, void *arg)
{
	struct ipc_subscriber *subscriber;
	subscriber = (struct ipc_subscriber *)malloc(sizeof(struct ipc_subscriber));
	if (!subscriber)
		return NULL;
	memset(subscriber, 0, sizeof(struct ipc_subscriber));
	snprintf(subscriber->client.server, sizeof(subscriber->client.server), "%s%s", UNIX_SOCK_DIR, broker);
	subscriber->mask = handler != NULL ? mask : 0L;
	subscriber->arg = arg;
	subscriber->client.identity = getpid();
	if (!subscriber->mask)
		return subscriber;
    if (ipc_subscriber_connect(subscriber) == 0) {
		IPCLOG("subscriber sk: %d, client: %d\n", subscriber->client.sock, subscriber->client.identity);
		subscriber->handler = handler;
		return subscriber;
	}
	free(subscriber);
	return NULL;
}
/**
 * ipc_subscriber_register - allocate a new subscriber handle
 * @broker: broker/server name
 * @mask: the mask of asynchronous messages interested
 * @handler: callback to process asynchronous messages
 * @arg: user's private arg, which is passed as the last argument of ipc_subscriber_handler
 */
struct ipc_subscriber *ipc_subscriber_register(const char *broker, 
										unsigned long mask, 
										ipc_subscriber_handler handler, void *arg)
{
	struct ipc_subscriber *subscriber;
	subscriber = ipc_subscriber_create(broker, mask, handler, arg);
	if (!subscriber) {
		IPCLOG("subscriber init error[topic %lx]\n",mask);
		return NULL;
	}
	if (subscriber->mask) {
		if (ipc_subscriber_run(subscriber) != 0) {
			ipc_subscriber_destroy(subscriber);
			return NULL;
		}
	}
	return subscriber;
}
/**
 * ipc_subscriber_unregister - release a subscriber handle
 * @subscriber: subscriber handle to release
 */
void ipc_subscriber_unregister(struct ipc_subscriber *subscriber)
{
	if (subscriber->task_id > 0 && subscriber->mask)
	{
		char buffer[IPC_MSG_MINI_SIZE] = {0};
		struct ipc_msg *msg = (struct ipc_msg *)buffer;
		msg->msg_id = IPC_SDK_MSG_UNREGISTER;
		msg->from = subscriber->client.identity;
		__clr_bit(IPC_BIT_REPLY, msg->flags);
		send_msg(subscriber->client.sock, msg);
		usleep(1000);
		if (subscriber->mask)
			pthread_cancel(subscriber->task_id);	
		pthread_join(subscriber->task_id, NULL);
	}
	ipc_subscriber_destroy(subscriber);
	IPC_INFO(__LOGTAG__,"subscriber unregister success\n");
}

