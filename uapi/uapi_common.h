#ifndef __UAPI_COMMON_H__
#define __UAPI_COMMON_H__
#include "generic_log.h"
#define __LOG_FILE 	"/tmp/uapi.log"
#define __LOG_SIZE	(64 * 1024)
#define UAPI_SOCK_DIR	"/tmp"		/* Keep save as UNIX_SOCK_DIR defined in IPC ipc_base.h IPC Internal Use Only */

#define UAPI_BROKER	"UAPI:BROKER"

enum UAPI_MSG {
	UAPI_MSG_REQUEST 	= 6000,
	UAPI_MSG_LIST,

	UAPI_MSG_PRIV,
};

#define UAPI_INNER_MASK (1LU << ((sizeof(unsigned long) << 3) - 2))
#define UAPI_EVENT_MASK (1LU << ((sizeof(unsigned long) << 3) - 1))


enum UAPI_NOTIFY {
	/*
	 * UAPI_EVENT_MASK
	 */
	UAPI_EVENT 			= 1,
	UAPI_EVENT_DEBUG,
};
#define UAPI_METHOD_MAX_LENGTH	31

/*
 * UAPI_METHOD_ALIGN_LEN should adapt to changing of UAPI_METHOD_MAX_LENGTH.
 */
#define UAPI_METHOD_ALIGN_LEN	"32"
struct uapi_request 
{
	char 	method[UAPI_METHOD_MAX_LENGTH + 1];
	char 	json[0];
}__attribute__((packed));

#define UAPI_EVENT_DELIMITER	'\t'
#define UAPI_EVENT_MAX_LENGTH	31
struct uapi_notify 
{
	char 	event[UAPI_EVENT_MAX_LENGTH + 1];
	char 	json[0];
}__attribute__((packed));

struct uapi_register {
	int     count;
	char 	events[0];
}__attribute__((packed));
#endif
