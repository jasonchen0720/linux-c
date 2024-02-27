#ifndef __IPCC_H__
#define __IPCC_H__

int ipcc_request_easy(const char *server, 
				int msg_id, void *data, int size, void *response, int rsplen);
int ipcc_request(struct ipc_client *client, 
				int msg_id, void *data, int size, void *response, int rsplen);
int ipcs_request(struct ipc_subscriber *subscriber, 
				int msg_id, void *data, int size, void *response, int rsplen);
int ipcc_publish(const char *broker, unsigned long topic, int msg_id, const void *data, int size);

#endif
