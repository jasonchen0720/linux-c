#ifndef __IPC_CLIENT_H__
#define __IPC_CLIENT_H__
#include "ipc_common.h"


#define IPC_REQUEST_SUCCESS  	 IPC_SUCCESS
#define IPC_REQUEST_TMO 	 	-IPC_ETIMEOUT
#define IPC_REQUEST_EOF  	 	-IPC_EOF
#define IPC_REQUEST_EMEM	 	-IPC_EMEM
#define IPC_REQUEST_EMT   		-IPC_ERECV
#define IPC_REQUEST_EMO    		-IPC_ESEND
#define IPC_REQUEST_ECN	 		-IPC_ECONN
#define IPC_REQUEST_EMSG	 	-IPC_EMSG
#define IPC_REQUEST_EVAL	 	-IPC_EVAL

struct ipc_client
{
	char server[64];
	int sock;
	int identity;
	volatile int state;
};
struct ipc_subscriber
{
	unsigned long task_id;
	unsigned long mask;
	int (*handler)(int, void *, int, void *);
	void *arg;
	void *buf;
	struct ipc_client client;
	unsigned int  data_len;
	char 		  data[];
};
typedef	int (*ipc_subscriber_handler)(int, void *, int, void *);
int ipc_client_init(const char *server, struct ipc_client *client);
int ipc_client_request(struct ipc_client* client, struct ipc_msg *msg, unsigned int size, int tmo);
struct ipc_client* ipc_client_create(const char *server);
void ipc_client_close(struct ipc_client* client);
void ipc_client_destroy(struct ipc_client* client);
int ipc_client_repair(struct ipc_client *client);
int ipc_client_publish(struct ipc_client *client, int to, unsigned long topic, int msg_id, const void *data, int size, int tmo);
struct ipc_subscriber *ipc_subscriber_register(const char *broker, 
										unsigned long mask, const void *data, unsigned int size,
										ipc_subscriber_handler handler, void *arg);
void ipc_subscriber_unregister(struct ipc_subscriber *subscriber);
int ipc_subscriber_report(struct ipc_subscriber *subscriber, struct ipc_msg *msg);
int ipc_subscriber_request(struct ipc_subscriber *subscriber, struct ipc_msg *msg, unsigned int size, int tmo);
#endif
