#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <assert.h>
#include "ipc_client.h"
#include "ipcc.h"

int ipcc_request(struct ipc_client *client, 
			int msg_id, void *data, int size, void *response, int rsplen)
{
	int  ret = -1;
	char buf[1024] = {0};
	struct ipc_msg *msg = (struct ipc_msg *)buf;
	size_t bufsize = rsplen > size ? rsplen : size;
	if (!ipc_msg_space_check(sizeof(buf), bufsize)) {
		msg = ipc_alloc_msg(bufsize);
		if (msg == NULL) {
			fprintf(stderr, "No memory\n");
			return ret;
		}
		bufsize += sizeof(struct ipc_msg);
	} else
		bufsize  = sizeof(buf);
	
	msg->msg_id = msg_id;
	if (response) {
		msg->flags = IPC_FLAG_REPLY;
	}
	if (data) {
		memcpy(msg->data, data, size);
		msg->data_len = size;
	}
	if (IPC_REQUEST_SUCCESS == ipc_client_request(client, msg, bufsize, 5)) {
		if (response) {
			if (msg->data_len > rsplen) {
				msg->data_len = rsplen;
				fprintf(stderr, "No enough space for response, truncated.\n");
			}
			memcpy(response, msg->data, msg->data_len);
		}
		ret = 0;
	}
	if ((void *)msg != (void *)buf) {
		ipc_free_msg(msg);
	}
	return ret;
}

/*
 * function:common interface for short tcp connection
 */
int ipcc_request_easy(const char *server, 
			int msg_id, void *data, int size, void *response, int rsplen)
{
	struct ipc_client client;
	if (ipc_client_init(server, &client) < 0) {
		printf("%s:error: client init error\n",__FUNCTION__);
		return -1;
	}
	int ret = ipcc_request(&client, msg_id, data, size, response, rsplen);

	ipc_client_close(&client);

	return ret;
}
int ipcs_request(struct ipc_subscriber *subscriber, 
				int msg_id, void *data, int size, void *response, int rsplen)
{
	int  ret = -1;
	char buf[1024] = {0};
	struct ipc_msg *msg = (struct ipc_msg *)buf;

	size_t bufsize = rsplen > size ? rsplen : size;

	if (!ipc_msg_space_check(sizeof(buf), rsplen)) {
		msg = ipc_alloc_msg(bufsize);
		if (msg == NULL) {
			fprintf(stderr, "No memory\n");
			return ret;
		}
		bufsize += sizeof(struct ipc_msg);
	} else
		bufsize  = sizeof(buf);
	
	msg->msg_id = msg_id;
	if (response) {
		msg->flags = IPC_FLAG_REPLY;
	}
	if (data) {
		memcpy(msg->data, data, size);
		msg->data_len = size;
	}
	if (IPC_REQUEST_SUCCESS == ipc_subscriber_request(subscriber, msg, bufsize, 5)) {
		if (response) {
			if (msg->data_len > rsplen) {
				msg->data_len = rsplen;
				fprintf(stderr, "No enough space for response, truncated.\n");
			}
			memcpy(response, msg->data, msg->data_len);
		}
		ret = 0;
	}
	if ((void *)msg != (void *)buf) {
		ipc_free_msg(msg);
	}
	return ret;
}

/* 
 * publisher/client publish topic
 * function:recommend reporting low frequency event to broker with this interface
 */
int ipcc_publish(const char *broker, unsigned long topic, int msg_id, const void *data, int size)
{
	struct ipc_client client;
	if (ipc_client_init(broker, &client) < 0) {
		printf("%s:error: client init error\n",__FUNCTION__);
		return -1;
	}
	int ret =  ipc_client_publish(&client, IPC_TO_BROADCAST, topic, msg_id, data, size, 3);

	ipc_client_close(&client);

	return ret == IPC_REQUEST_SUCCESS ? 0 : -1;
}
