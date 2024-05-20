/*
 * Copyright (c) 2017, <-Jason Chen->
 * Version: 1.2.1 - 20230316
 *				  - Modify log tag, using comm instead.
 * Version: 1.2.0 - 20220216
 *                - Add registering information option for client registering - See changes in ipc_subscriber_register(). 
 *				  - Code Robustness optimize: 
 *						Add error code - IPC_EVAL.
 *						Add global init routine.
 *						Add Callback Thread Signal mask.
 *						Add Callback Thread Cancel protection.
 *						Add protection in case of calling ipc_subscriber_unregister() in user's callback context. 
 *                      Add client state.
 *
 *				  - Fix at 20221103:
 *						Bug in macro client_valid(client), socket fd may be 0, which is checked in this macro function. 
 *			1.1.1 - 20210917
 *                - Add ipc timing
 *                - Fix segment fault while doing core exit.
 *				  - Rename some definitions.
 *			1.1.0 - 20200520, update from  to 1.0.x
 *				  - What is new in 1.1.0? Works more efficiently and fix some extreme internal bugs.
 *			1.0.x - 20171101
 * Author: Jie Chen <jasonchen0720@163.com>
 * Brief : This program is the implementation of IPC client interfaces.
 * Date  : Created at 2017/11/01
 */
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/un.h>
#include <pthread.h>
#include <assert.h>
#include <signal.h>
#include "ipc_client.h"
#include "ipc_log.h"
#include "ipc_base.h"
#define __LOGTAG__ __client_name
static pthread_once_t __client_once = PTHREAD_ONCE_INIT;
static pid_t 		__client_pid  = 0;
static const char * __client_name = DUMMY_NAME;
#define client_valid(client) (get_state(client) == IPC_S_CONNECTED && \
							  (client)->sock >= 0 && \
							  (client)->identity > 0 && \
							  (client)->identity == __client_pid)	
static void client_back (void)
{
	__client_once = PTHREAD_ONCE_INIT;
}
static void client_init(void) 
{
	__client_pid   = getpid();
	__client_name  = self_name();
	pthread_atfork(NULL, NULL, client_back);
}

/**
 * ipc_connect - connect to server
 */
static int ipc_connect(struct ipc_client *client)
{
	int retry = 0;
	int sock;
	struct sockaddr_un server_addr = {0};

	sock = socket(AF_UNIX, SOCK_STREAM, 0);
	if (sock < 0) {
        IPC_LOGE("Unable to create socket: %s", strerror(errno));
		return -1;
    }
	if (sock_opts(sock, 0) < 0) {
		close(sock);
		return -1;
	}
    server_addr.sun_family = AF_UNIX;
    strcpy(server_addr.sun_path, client->server);
	set_state(client, IPC_S_CONNECTING);
__retry:
	if (connect(sock, (struct sockaddr *)&server_addr,  (socklen_t)sizeof(struct sockaddr_un)) < 0) {
		if (errno == EINTR)
			goto __retry;
		if (errno == ECONNREFUSED || 
			errno == ENOENT ) {
			if (retry++ < 3) {
				sleep(1);
				goto __retry;
			}
		}
		IPC_LOGE("connect to %s errno:%d sk:%d", server_offset(client->server), errno, sock);
		set_state(client, IPC_S_DISCONNECTED);
		barrier();
		close(sock);
		return -1;
	}
	client->sock = sock;
	IPC_LOGD("connect to %s, sk:%d", server_offset(client->server), client->sock);
	return 0;
}
static int ipc_request(struct ipc_client* client, struct ipc_msg *msg, unsigned int size, int tmo)
{
	msg->from = client->identity;
	msg->flags &= IPC_FLAG_CLIENT_MASK;
	
	if (send_msg(client->sock, msg) < 0) {
		IPC_LOGE("request to %s error: %d, msg: %04x", server_offset(client->server), errno, msg->msg_id);
		return IPC_REQUEST_EMO;
	}
	if (msg->flags & __bit(IPC_BIT_REPLY)) {
		int rc;
		switch (recv_msg(client->sock, (char *)msg, size, tmo)) {
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
		case IPC_RECEIVE_EMSG:
			IPC_LOGE("Received error msg.");
			rc = IPC_REQUEST_EMSG;
			break;
		case IPC_RECEIVE_EVAL:
			rc = IPC_REQUEST_EVAL;
			break;
		default:
			return IPC_REQUEST_SUCCESS;
		}
		
		IPC_LOGE("receive from %s error: %s, messages ID: %04x", server_offset(client->server), strerr(-rc), msg->msg_id);
		return rc;
	}
	return IPC_REQUEST_SUCCESS;
}

/**
 * ipc_client_init - init a temporary client handle to server
 * @server: server name
 * @client: client handle
 */
int ipc_client_init(const char *server, struct ipc_client *client)
{
	assert(pthread_once(&__client_once, client_init) == 0);
	
	int ssize = snprintf(client->server, sizeof(client->server), "%s%s", UNIX_SOCK_DIR, server);

	if (ssize >= sizeof(client->server)) {
		IPC_LOGE("Server name is too long.");
		return -1;
	}
	if (ipc_connect(client) < 0) {
		IPC_LOGE("IPC connecting failure.");
		return -1;
	}
	client->identity = __client_pid;

	/* 
	 * For this kind of temporary client, let it switch to CONNECTED directly. 
	 */
	set_state(client, IPC_S_CONNECTED);
	
	return 0; 
}
/**
 * ipc_client_connect - client connect to server
 * @client: client handle
 */
static int ipc_client_connect(struct ipc_client *client)
{
	char buffer[IPC_MSG_MINI_SIZE] = {0};
	struct ipc_msg *ipc_msg = (struct ipc_msg *)buffer;
		
	if (ipc_connect(client) < 0) {
		IPC_LOGE("IPC connecting failure.");
		return -1;
	}
	
	ipc_msg->msg_id = IPC_SDK_MSG_CONNECT;
	__set_bit(IPC_BIT_REPLY, ipc_msg->flags);
	struct ipc_identity *cid = (struct ipc_identity *)ipc_msg->data;
	cid->identity = client->identity;
	ipc_msg->data_len = sizeof(struct ipc_identity);
	if (IPC_REQUEST_SUCCESS != ipc_request(client, ipc_msg, sizeof(buffer), IPC_MSG_SDK_TIMEOUT))
		goto __error;
	
	if (ipc_msg->msg_id != IPC_SDK_MSG_SUCCESS)
		goto __error;

	set_state(client, IPC_S_CONNECTED);
	return 0;
__error:
	ipc_client_close(client);
	return -1;
}
/**
 * ipc_client_create - Allocate memory for new client handle and init the client handle.
 * 					 - Different from client initialized by ipc_client_init(), this client keeps long connection to server, it is reusable, 
 * 					   but user needs to take care thread-safe protection while calling ipc_client_request() using this client handle.
 * @server: server name
 */
struct ipc_client* ipc_client_create(const char *server)
{
	assert(pthread_once(&__client_once, client_init) == 0);
	struct ipc_client *client;

	client = (struct ipc_client *)malloc(sizeof(struct ipc_client));
	if (!client) {
		IPC_LOGE("None Memory.");
		return NULL;
	}
	memset(client, 0, sizeof(struct ipc_client));
	client->identity = __client_pid;
	int ssize = snprintf(client->server, sizeof(client->server), "%s%s", UNIX_SOCK_DIR, server);
	if (ssize >= sizeof(client->server)) {
		IPC_LOGE("Server name is too long.");
		goto err;
	}
    if (ipc_client_connect(client) < 0) {
		IPC_LOGE("Client connecting error.");
		goto err;
    }
	return client;
err:
	free(client);
	return NULL;
}
/**
 * ipc_client_request - client send a request message to server
 * @client: client handle
 * @msg: request message - must be users' message, users's messages ID should be smaller than IPC_MSG_SDK.
 * @size: message size
 * @tmo: time of send timeout
 */
int ipc_client_request(struct ipc_client* client, struct ipc_msg *msg, unsigned int size, int tmo)
{
	int rc = 0;
	/*
	 * Client handle sanity check.
	 */
	if (!client_valid(client))
		rc = IPC_REQUEST_EVAL;
	/*
	 * Users's messages ID should be smaller than IPC_MSG_SDK
	 */
	else if (!users_msg(msg))
		rc = IPC_REQUEST_EMSG;
	else
		rc = ipc_request(client, msg, size, tmo);

	if (rc)
		IPC_LOGE("Client request to %s error: %s, messages ID: %04x", server_offset(client->server), strerr(-rc), msg->msg_id);

	return rc;
}
/**
 * ipc_client_close - shutdown a connection
 * @client: client handle
 */
void ipc_client_close(struct ipc_client* client)
{
	if (client->sock > 0)
	{
		set_state(client, IPC_S_DISCONNECTED);
		barrier();
		close(client->sock);	
		client->sock = -1;
	}
}
/**
 * ipc_client_destroy - shutdown a connection and free the memory occupied by ipc_client structure 
 * @client: client handle to be destroyed
 */
void ipc_client_destroy(struct ipc_client* client)
{
	if (client)
	{
		if (client->sock > 0) {
			set_state(client, IPC_S_DISCONNECTED);
			barrier();
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
		IPC_LOGI("client reconnect success sk:%d", client->sock);
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
		int to, unsigned long topic, int msg_id, const void *data, int size, int tmo)
{
	int rc = 0;
	int dynamic = 0;
	/*
	 * Client handle sanity check.
	 */
	if (!client_valid(client)) {
		rc = IPC_REQUEST_EVAL;
		goto out;
	}
	char buffer[IPC_NOTIFY_MSG_MAX_SIZE] = {0};
	struct ipc_msg *msg = (struct ipc_msg *)buffer;
	if (!ipc_notify_space_check(sizeof(buffer), size)) {
		msg = ipc_alloc_msg(sizeof(struct ipc_notify) + size);
		if (!msg) {
			rc = IPC_RECEIVE_EMEM;
			goto out;
		} else dynamic = 1;
	}
	ipc_notify_pack(msg, to, topic, msg_id, data, size);
	
	rc = ipc_request(client, msg, sizeof(buffer), tmo);
	if (dynamic)
		ipc_free_msg(msg);
out:
	if (rc)
		IPC_LOGE("Client publish to %s error: %s, messages ID: %04x", server_offset(client->server), strerr(-rc), msg_id);
	
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
	 * Client handle sanity check.
	 */
	if (!client_valid(&subscriber->client))
		return IPC_REQUEST_EVAL;
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
	msg->flags &= IPC_FLAG_CLIENT_MASK;
	msg->from = subscriber->client.identity;
	return send_msg(subscriber->client.sock, msg) > 0 ? IPC_REQUEST_SUCCESS : IPC_REQUEST_EMO;
}
/**
 * ipc_subscriber_request - subscriber send a request message to server
 * @subscriber: subscriber handle
 * @msg: request message - must be users' message, users's messages ID should be smaller than IPC_MSG_SDK.
 * @size: message size
 * @tmo: time of send timeout
 */
int ipc_subscriber_request(struct ipc_subscriber *subscriber, struct ipc_msg *msg, unsigned int size, int tmo)
{
	int rc = 0;
	if (subscriber->client.identity <= 0 || 
		subscriber->client.identity != __client_pid) {
		rc = IPC_REQUEST_EVAL;
		goto out;
	}
	struct ipc_client dummy_client;
	strcpy(dummy_client.server, subscriber->client.server);
	dummy_client.identity = subscriber->client.identity;
	if (ipc_connect(&dummy_client) < 0) {
		rc = IPC_REQUEST_ECN;
		goto out;
	}
	
	rc = ipc_client_request(&dummy_client, msg, size, tmo);

	ipc_client_close(&dummy_client);
 out:
 	if (rc) 
		IPC_LOGE("Subscriber request error: %s, messages ID: %04x", strerr(-rc), msg->msg_id);
	return rc;
}
/**
 * ipc_subscriber_destroy - shutdown a connection and free the memory occupied by ipc_subscriber structure 
 * @subscriber: subscriber handle to be destroyed
 */
static void ipc_subscriber_destroy(struct ipc_subscriber *subscriber)
{
	if (subscriber)
	{
		if (subscriber->client.sock > 0) {
			set_state(&subscriber->client, IPC_S_DISCONNECTED);
			barrier();
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
	int dynamic = 0;
	char buffer[256] = {0};
	unsigned int sbuf = sizeof(buffer);
	unsigned int size = sizeof(struct ipc_reg) + subscriber->data_len;
	struct ipc_msg *ipc_msg = (struct ipc_msg *)buffer;
	if (ipc_msg_space_check(sbuf, size) == 0) {
		ipc_msg = ipc_alloc_msg(size);
		if (!ipc_msg)
			return -1;
		else dynamic = 1;
		sbuf = size + IPC_MSG_HDRLEN;
	}
	struct ipc_reg *reg = (struct ipc_reg *)ipc_msg->data;

	set_state(&subscriber->client, IPC_S_CONNECTING);
	
	if (ipc_connect(&subscriber->client) < 0)
		goto __error;
	
	ipc_msg->msg_id = IPC_SDK_MSG_REGISTER;
	__set_bit(IPC_BIT_REPLY, ipc_msg->flags);
	reg->mask 	  = subscriber->mask;
	reg->data_len = subscriber->data_len;
	if (subscriber->data_len > 0)
		memcpy(reg->data, subscriber->data, subscriber->data_len);
	ipc_msg->data_len = size;

	if (IPC_REQUEST_SUCCESS != ipc_request(&subscriber->client, ipc_msg, sbuf, IPC_MSG_SDK_TIMEOUT))
		goto __error;
	
	if (ipc_msg->msg_id != IPC_SDK_MSG_SUCCESS)
		goto __error;
	
	struct ipc_negotiation *neg = (struct ipc_negotiation *)ipc_msg->data;
	/* 
	 * negotiation information.
	 * buf_size must be required, it tells the client the max IPC msg size of buffer to allocate.
	 */
	if (!subscriber->buf)
		subscriber->buf = (void *)alloc_buf(neg->buf_size);
	if (!subscriber->buf)
		goto __error;
	IPC_LOGI("ipc buf size: %u.",neg->buf_size);
	if (dynamic)
		ipc_free_msg(ipc_msg);

	set_state(&subscriber->client, IPC_S_CONNECTED);
	return 0;
__error:
	if (dynamic)
		ipc_free_msg(ipc_msg);
	ipc_client_close(&subscriber->client);
	IPC_LOGI("subscriber connect error, sk: %d.", subscriber->client.sock);
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
	msg->data_len = sizeof(struct ipc_identity);
	msg->from = subscriber->client.identity;
	struct ipc_identity *tid = (struct ipc_identity *)msg->data;
	tid->identity = gettid();
	IPC_LOGI("sync to server, client:%d(%d), mask: %04lx", subscriber->client.identity, tid->identity, subscriber->mask);
	return send_msg(subscriber->client.sock, msg) > 0 ? 0 : -1;
}
/**
 * ipc_subscriber_repair - subscriber repair the connection if disconnected.
 * @subscriber: subscriber handle
 */
static int ipc_subscriber_repair(struct ipc_subscriber *subscriber)
{
	ipc_client_close(&subscriber->client);

	if (ipc_subscriber_connect(subscriber) == 0)
	{
		IPC_LOGI("subscriber rebuild success sk:%d, client %d", subscriber->client.sock,  subscriber->client.identity);
		ipc_subscriber_sync(subscriber);
		return 0;
	}
	IPC_LOGE("subscriber @%p repair failure, sk: %d", subscriber, subscriber->client.sock);
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
		msg = find_msg(buf, NULL);
		if (!msg) {
			if (ipc_buf_full(buf)) {
				IPC_LOGE("subscriber buffer full, size: %u.%u", buf->tail, buf->size);
				break;
			}
			return;
		}
		if (msg->msg_id == IPC_SDK_MSG_NOTIFY) {
			notify = (struct ipc_notify *)msg->data;
			if (topic_isset(subscriber, notify->topic) && 
				topic_check(notify->topic))
				subscriber->handler(notify->msg_id, notify->data, notify->data_len, subscriber->arg);
		} else if (msg->msg_id == IPC_SDK_MSG_UNREGISTER) {
			IPC_LOGI("subscriber task exit");
			subscriber->mask = 0ul;
			pthread_exit(NULL);
		} else {
			IPC_LOGE("unexpected msg:%04x, %d:%d",msg->msg_id, buf->head, buf->tail);
			break;
		}
	} while (ipc_buf_pending(buf));
	IPC_LOGD("tail: %d, head: %d", buf->tail, buf->head);
	ipc_buf_reset(buf);
}

/**
 * ipc_subscriber_task - subscriber callback entry
 */
static void * ipc_subscriber_task(void *arg)
{
	int rc;
	struct ipc_subscriber *subscriber = (struct ipc_subscriber *)arg;
	struct ipc_buf *buf = (struct ipc_buf *)subscriber->buf;
	pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, NULL);
	pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
	ipc_subscriber_sync(subscriber);
	while (subscriber->mask) {
		struct timeval timeout;
		timeout.tv_sec 	= 30;
		timeout.tv_usec = 0;
		rc = recv_stream(subscriber->client.sock, 
						buf->data + buf->tail, 
						buf->size - buf->tail, &timeout);
		if (rc > 0) {
			IPC_LOGD("receive %d bytes, offset: %u.", rc, buf->head);
			buf->tail += rc;
			ipc_subscriber_process(subscriber, buf);
		} else {
			switch (rc) {
			case IPC_RECEIVE_TMO:
				break;
			case IPC_RECEIVE_EOF:
			case IPC_RECEIVE_ERR:
				IPC_LOGE("receive from %s, error: %s", subscriber->client.server, strerr(-rc));
				while(subscriber->mask && (ipc_subscriber_repair(subscriber) < 0))
					sleep(5);
				ipc_buf_reset(buf);
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
	sigset_t set, oset;
	sigfillset(&set);
	pthread_sigmask(SIG_SETMASK, &set, &oset);
	int err = pthread_create(&subscriber->task_id, NULL, ipc_subscriber_task, (void *)subscriber);
	pthread_sigmask(SIG_SETMASK, &oset, NULL);
	return err;
}
/**
 * ipc_subscriber_create - allocate memory for new subscriber handle and init the subscriber handle
 * @param: See annotation of ipc_subscriber_register()
 */
static struct ipc_subscriber *ipc_subscriber_create(const char *broker, 
													unsigned long mask, const void *data, unsigned int size,
													ipc_subscriber_handler handler, void *arg)
{
	if (handler == NULL || mask == 0ul || data == NULL)
		size = 0u;
	struct ipc_subscriber *subscriber;
	subscriber = (struct ipc_subscriber *)malloc(sizeof(struct ipc_subscriber) + size);
	if (!subscriber) {
		IPC_LOGE("None Memory.");
		return NULL;
	}
	memset(subscriber, 0, sizeof(struct ipc_subscriber));
	int ssize = snprintf(subscriber->client.server, sizeof(subscriber->client.server), "%s%s", UNIX_SOCK_DIR, broker);
	if (ssize >= sizeof(subscriber->client.server)) {
		IPC_LOGE("Server name is too long.");
		goto err;
	}
	subscriber->mask = handler != NULL ? mask : 0ul;
	subscriber->arg = arg;
	subscriber->client.identity = __client_pid;
	if (!subscriber->mask)
		return subscriber;
	if (data) {
		subscriber->data_len = size;
		memcpy(subscriber->data, data, size);
	} else
		subscriber->data_len = 0u;
    if (ipc_subscriber_connect(subscriber) < 0)
		goto err;
	
	IPC_LOGI("subscriber sk: %d, client: %d", subscriber->client.sock, subscriber->client.identity);
	subscriber->handler = handler;
	return subscriber;
err:
	free(subscriber);
	return NULL;
}
/**
 * ipc_subscriber_register - allocate a new subscriber handle
 * @broker: broker/server name
 * @mask: the mask of asynchronous messages interested
 * @data: Data sent to server while doing register.
 * @size: Data length.
 * @handler: callback to process asynchronous messages
 * @arg: user's private arg, which is passed as the last argument of ipc_subscriber_handler()
 */
struct ipc_subscriber *ipc_subscriber_register(const char *broker, 
										unsigned long mask, const void *data, unsigned int size,
										ipc_subscriber_handler handler, void *arg)
{
	assert(pthread_once(&__client_once, client_init) == 0);
	
	struct ipc_subscriber *subscriber;
	subscriber = ipc_subscriber_create(broker, mask, data, size, handler, arg);
	if (!subscriber) {
		IPC_LOGE("subscriber init error[mask %lx]", mask);
		return NULL;
	}
	if (subscriber->mask) {
		if (ipc_subscriber_run(subscriber) != 0) {
			IPC_LOGE("subscriber run error");
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

		set_state(&subscriber->client, IPC_S_DISCONNECTING);
		
		send_msg(subscriber->client.sock, msg);
		/*
		 * In case of calling in user's callback context.
		 */
		if (pthread_equal(pthread_self(), subscriber->task_id)) {
			pthread_detach(pthread_self());
			ipc_subscriber_destroy(subscriber);
			IPC_LOGW("subscriber callback exit.");
			pthread_exit(NULL);
		}
		usleep(1000);
		if (subscriber->mask)
			pthread_cancel(subscriber->task_id);	
		pthread_join(subscriber->task_id, NULL);
	}
	ipc_subscriber_destroy(subscriber);
	IPC_LOGI("subscriber unregister success");
}

