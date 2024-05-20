#ifndef _IPC_COMMON_H_
#define _IPC_COMMON_H_

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
	IPC_EVAL,
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

#define IPC_MSG_HDRLEN		(sizeof(struct ipc_msg))
#define IPC_MSG_MINI_SIZE	(IPC_MSG_HDRLEN + 16) 		/* this must adpter to the size of struct ipc_msg */

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
 	* Bits:0 - 7 used by Client.
 	*/
 	IPC_BIT_CLIENT	= 0,
   /*
 	* This bit indicates the message has a response.
 	*/
	IPC_BIT_REPLY 	= IPC_BIT_CLIENT,


   /*
 	* Following bits 8 - 15 used by Server.
 	*/
	IPC_BIT_SERVER		= 8,
   /*
 	* Bit 8-9 indicate the message class.
 	*/
	IPC_BIT_REQUESTER 	= IPC_BIT_SERVER,
	IPC_BIT_SUBSCRIBER	= 9,
	
	IPC_BIT_ASYNC		= 15,
	IPC_BIT_MAX 		= 16
};
#define IPC_FLAG_EXPECT_REPLY	(1u << IPC_BIT_REPLY)
#define IPC_FLAG_REPLY			(1u << IPC_BIT_REPLY)
#define IPC_FLAG_CLIENT_MASK	((1u << IPC_BIT_SERVER) - 1)
/*
 * ipc_msg_space_check() - Used to check if the buffer space %max is enough for the ipc message. 
 */
#define ipc_msg_space_check(max, size)		((max) >= IPC_MSG_HDRLEN + (size))
/*
 * ipc_notify_space_check() - Used to check if the buffer space %max is enough for the notify message. 
 */
#define ipc_notify_space_check(max, size)	ipc_msg_space_check(max, sizeof(struct ipc_notify) + size)
struct ipc_msg * ipc_clone_msg(const struct ipc_msg *msg, unsigned int size);
struct ipc_msg * ipc_alloc_msg(unsigned int size);
void ipc_free_msg(struct ipc_msg *msg);
#endif