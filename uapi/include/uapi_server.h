#ifndef __UAPI_SERVER_H__
#define __UAPI_SERVER_H__
#include <json-c/json_object.h>
#include <json-c/json_tokener.h>

#include "bst.h"
#include "ipc_server.h"
#include "uapi_define.h"
#include "uapi_json.h"

struct uapi_entry 
{
	const char 	*method;
	/* 
	 * int handler(struct ipc_msg *msg, void *arg, void *cookie);
	 */
	int (*handler)(struct ipc_msg *, void *, void *, struct json_object *);

	const char 	*param;
	
	struct bst_node node;
};
int uapi_ipc_init(const char *path, struct uapi_entry *entries, int n, int (*priv_handler)(struct ipc_msg *, void *, void *));
struct json_object * uapi_get_json(struct ipc_msg *msg);
void uapi_put_json(struct json_object *json);
#endif
