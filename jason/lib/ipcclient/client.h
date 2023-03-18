#ifndef __CLIENT_H__
#define __CLIENT_H__

#define IPC_SERVER_TEST			"IPC_TEST"
#define IPC_SERVER_BROKER	    "IPC_BROKER"

int client_sendto_server_easy(const char *server, 
		int msg_id, void *data, int size, void *response, int rsplen);
int client_sendto_server(struct ipc_client *client, 
		int msg_id, void *data, int size, void *response, int rsplen);
int subscriber_sendto_server(struct ipc_subscriber *subscriber, 
		int msg_id, void *data, int size, void *response, int rsplen);

typedef void client_handle;
enum WW_SERVICE_ID
{
	WW_SERVICE_BROKER = 0x0001,
};
/**
 * ind_msg_callback(int msg_id, void *data, int size, void *arg)
 * @msg_id: indication message ID defined in enum INDICATION_MSG_ID
 * @data: data of indication message if existed
 * @size: data length, if none of data, size is zero
 * @arg: the sole argument of ind_msg_callback(), which is passed by client_subscriber_regester().
 */
typedef	int (*ind_msg_callback)(int, void *, int, void *);
/**
 * function: client_subscriber_register() - create a client handle for a callback
 * 				This handle can be also used for sending synchronous request, see description of client_send_request_msg_v2()
 * parameters:
 * @service: service ID defined in enum WW_SERVICE_ID
 * @topic_set: event mask that you are interested in (mask value defined in this header file)
 * @cb: event trigger callback. This function will be triggered when topic event is ready,
 *              you'd better not do blocked actions or process which can cost a long time in your callback
 * @arg: arg is passed as the sole argument of ind_msg_callback().
 * return: if success, return a pointer of client handle, if not, return NULL
 * note: every client handle need free by client_subscriber_unregester(), after work
 */
client_handle *client_subscriber_register(int service, unsigned long topic_set, ind_msg_callback cb, void *arg);
/**
 * function: client_subscriber_unregister() - free a client handle
 * parameters:
 * @handle: client handle to be released
 * return: None
 */
void client_subscriber_unregister(client_handle *handle);

/**
 * function: client_send_request_msg() -  used to send a request via handle of subscriber_handle
 * parameters:
 * @handle: client handle returned by client_subscriber_regester()
 * @msg_id: message ID defined in enum CLIENT_MSG_ID
 * @request: request parameter, if no request data, set NULL
 * @reqlen : the length of parameter request
 * @response: response buffer, if no response expected, set NULL
 * @rsplen: the size of response buffer
 * return: On success, zero is returned. On error, -1 is returned
 */
int client_send_request_msg(client_handle *handle, int msg_id, void *request, int reqlen, void *response, int rsplen);

#endif
