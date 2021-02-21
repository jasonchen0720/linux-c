#ifndef _IPC_COMMON_CMD_H_
#define _IPC_COMMON_CMD_H_


#define IPC_SERVER_TEST			"IPC_TEST"
#define IPC_SERVER_BROKER	    "IPC_BROKER"

int client_sendto_server_easy(const char *server, 
		int msg_id, void *data, int size, void *response, int rsplen);
int client_sendto_server(struct ipc_client *client, 
		int msg_id, void *data, int size, void *response, int rsplen);
int subscriber_sendto_server(struct ipc_subscriber *subscriber, 
		int msg_id, void *data, int size, void *response, int rsplen);
#endif

