#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <assert.h>
#include "ipc_client.h"
#include "client.h"

#define MSG_MAX_SIZE 4096
int ipcc_common_request(struct ipc_client *client, 
		int msg_id, void *data, int size, void *response, int rsplen)
{
	char buffer[MSG_MAX_SIZE]={0};
	struct ipc_msg *ipc_msg = (struct ipc_msg *)buffer;
	ipc_msg->msg_id = msg_id;
	if (response)
		ipc_msg->flags |= IPC_FLAG_REPLY;
	if (data) {
		memcpy(ipc_msg->data, data, size);
		ipc_msg->data_len = size;
	}
	if (IPC_REQUEST_SUCCESS == ipc_client_request(client, ipc_msg, sizeof(buffer), 5)) {
		if (response) {
			if (ipc_msg->data_len > rsplen)
				ipc_msg->data_len = rsplen;
			memcpy(response, ipc_msg->data, ipc_msg->data_len);
		}
		return 0;
	}
	return -1;
}

/*
 * function:common interface for short tcp connection
 */
int ipcc_common_request_easy(const char *server, int msg_id, void *data, int size, void *response, int rsplen)
{
	int rc = -1;
	struct ipc_client client;
	if (ipc_client_init(server, &client) < 0) {
		printf("%s:error: client init error\n",__FUNCTION__);
		return -1;
	}
	rc = ipcc_common_request(&client, msg_id, data, size, response, rsplen);

	ipc_client_close(&client);

	return rc;
}
int ipcs_common_request(struct ipc_subscriber *subscriber, 
				int msg_id, void *data, int size, void *response, int rsplen)
{
	char buffer[MSG_MAX_SIZE]={0};
	struct ipc_msg *msg = (struct ipc_msg *)buffer;
	msg->msg_id = msg_id;
	if (response)
		msg->flags |= IPC_FLAG_REPLY;
	if (data) {
		memcpy(msg->data, data, size);
		msg->data_len = size;
	}
	if (IPC_REQUEST_SUCCESS == ipc_subscriber_request(subscriber, msg, sizeof(buffer), 5)) {
		if (response) {
			if (msg->data_len > rsplen)
				msg->data_len = rsplen;
			memcpy(response, msg->data, msg->data_len);
		}
		return 0;
	}
	return -1;
}

/* 
 * publisher/client publish topic
 * function:recommend reporting low frequency event to broker with this interface
 */
int ipcc_common_topic_publish(const char *broker, unsigned long topic, int msg_id, void *data, int size)
{
	struct ipc_client client;
	if (ipc_client_init(broker, &client) < 0) {
		printf("%s:error: client init error\n",__FUNCTION__);
		return -1;
	}
	int rc =  ipc_client_publish(&client, IPC_TO_BROADCAST, topic, msg_id, data, size, 3);

	ipc_client_close(&client);

	return rc == IPC_REQUEST_SUCCESS ? 0 : -1;
}
client_handle *client_subscriber_register(int service, unsigned long topic_set, ind_msg_callback cb, void *arg)
{	
	const char *broker = NULL;
	switch(service) {
		case WW_SERVICE_TEST:
			broker = IPC_SERVER_TEST;
			break;
		default:
			return NULL;
	}
	return (client_handle *)ipc_subscriber_register(broker, topic_set, NULL, 0, cb, NULL);
}

void client_subscriber_unregister(client_handle *handle)
{
	if (handle)
		ipc_subscriber_unregister((struct ipc_subscriber *)handle);
}
int client_send_request_msg(client_handle *handle, int msg_id, 
					void *request, int reqlen, void *response, int rsplen)
{
	if (!handle) {
		printf("error: invalid handle\n");
		return -1;
	}
	return ipcs_common_request((struct ipc_subscriber *)handle,msg_id, request, reqlen, response, rsplen);
}

