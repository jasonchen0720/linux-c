#ifndef __UAPI_CLIENT_H__
#define __UAPI_CLIENT_H__
#include <stddef.h>
#include "uapi_define.h"
#include "bst.h"
#define UAPI_BUF_MIN		64
#define UAPI_F_SAFE			1	/* The method callings may be happening in a multithread environment */
#define UAPI_F_ONESHOT		2	/* Long connection, but oneshot */
#define UAPI_F_MULTIPLEX	4	/* Long connection, Caller will call the path repeatedly */
#define UAPI_F_NOTIFY		8	/* None response expected */

char * uapi_method_list(const char *path, char *buff, size_t size, int tmo);
char * uapi_method_invoke(const char *path, const char *method, const char *json, char *buf, size_t size, int tmo, int flags);

#define UAPI_BROADCAST	0
struct uapic_event {
	const char *name; /* User defined, the length can not exceed UAPI_EVENT_MAX_LENGTH */
	void (*callback)(void *data, size_t size, void *arg);
	void *arg;
	struct bst_node node;
};

int uapi_event_register(struct uapic_event *events, int count,
	void (*callback)(const char *event, void *data, size_t size, void *arg),
	void *arg);
int uapi_event_push(const char *event, const char *json, int dest);

#endif

