#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <assert.h>
#include "ipc_client.h"
#include "broker_client.h"
#include "broker_define.h"
#define BROKER_MSG_MAX_SIZE		1024
static struct ipc_subscriber *event_subscriber = NULL;
int broker_client_publish(unsigned long mask, int msg_id, void *data, int size)
{
	int rc;
	struct ipc_client client;
	if (ipc_client_init(IPC_BROKER, &client) < 0) {
		printf("%s:error: client init error\n",__FUNCTION__);
		return -1;
	}
	rc =  ipc_client_publish(&client, IPC_TO_BROADCAST, mask, msg_id, data, size, 3);

	ipc_client_close(&client);

	return rc == IPC_REQUEST_SUCCESS ? 0 : -1;
}
int broker_client_request(int msg_id, void *data, int size, void *response, int rsplen)
{
	int rc = -1;
	char buffer[BROKER_MSG_MAX_SIZE] = {0};
	struct ipc_client client;
	struct ipc_msg *ipc_msg = (struct ipc_msg *)buffer;
	ipc_msg->msg_id = msg_id;
	if (response)
		ipc_msg->flags |= IPC_FLAG_REPLY;
	if (data) {
		memcpy(ipc_msg->data, data, size);
		ipc_msg->data_len = size;
	}
	if (ipc_client_init(IPC_BROKER, &client) < 0) {
		printf("%s:error: client init error\n",__FUNCTION__);
		return -1;
	}
	if (IPC_REQUEST_SUCCESS == ipc_client_request(&client, ipc_msg, sizeof(buffer), 5)) {
		if (response) {
			if (ipc_msg->data_len > rsplen)
				ipc_msg->data_len = rsplen;
			memcpy(response, ipc_msg->data, ipc_msg->data_len);
		}
		rc = 0;
	}
	ipc_client_close(&client);
	return rc;
}
int broker_client_register(unsigned long mask, event_callback cb, void *arg)
{
	if (mask != 0ul && cb != NULL) {
		struct ipc_subscriber *subscriber = ipc_subscriber_register(IPC_BROKER, mask, NULL, 0u, cb, arg);
		event_subscriber = subscriber;
		return subscriber ? 0 : -1;
	}
	
	return -1;
}
void broker_client_unregister()
{
	if (event_subscriber) {
		ipc_subscriber_unregister(event_subscriber);
		event_subscriber = NULL;
	}
}