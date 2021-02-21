#ifndef _IPC_COMMON_H_
#define _IPC_COMMON_H_

#define IPC_MSG_MINI_SIZE	32 /* this must adpter to the size of struct ipc_msg */
enum ERR_CODE
{
	IPC_EMIN	= -1,
	IPC_SUCCESS = 0,
	IPC_ETIMEOUT,
	IPC_EOF,
	IPC_EMEM,
	IPC_ERECV,
	IPC_ESEND,
	IPC_ECONN,
	IPC_EMSG,
	IPC_EMAX,
};
struct ipc_msg
{
	int 			from;
	int 			msg_id;
	unsigned short 	flags;
	unsigned short 	data_len;
	char 			data[0];
}__attribute__((packed));
/* This identity is used to indicate thst the notify message needs to be dispatched to all the related clients */
#define IPC_TO_BROADCAST	0x7FFFFFFF
#define IPC_TO_NOTIFY		0x7FFFFFFE
struct ipc_notify
{
	int 			to;
	unsigned long 	topic;
	int 			msg_id;
	int 			data_len;
	char 			data[0];
};
enum {
   /*
 	* This bit indicates the message has a response.
 	*/
	IPC_BIT_REPLY 	= 0,

	IPC_BIT_MAX 	= 16
};
#define IPC_FLAG_EXPECT_REPLY	(1u << IPC_BIT_REPLY)
#define ipc_subscribed(sr,t)		((sr)->peer->mask & (t))
struct ipc_msg * ipc_alloc_msg(unsigned int size);
void ipc_free_msg(struct ipc_msg *msg);
#endif