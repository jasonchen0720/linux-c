#include <stdio.h>
#include <string.h>

#include "uapi_common.h"
#include "uapi_server.h"
#include "generic_bit.h"
#define __LOG_TAG	"uapis"
struct uapi_table
{
	struct bst_iterator *iterator;
	struct bst_table 	*uapi;
};
static int uapi_comparator(const struct bst_node *n1, const struct bst_node *n2)
{
	const struct uapi_entry *a = bst_entry(n1, struct uapi_entry, node);
	const struct uapi_entry *b = bst_entry(n2, struct uapi_entry, node);
	return strcmp(a->method, b->method);
}
static int uapi_searcher(const void *item, const struct bst_node *n)
{
	const struct uapi_entry *entry = bst_entry(n, struct uapi_entry, node);
	const char *method = (const char *)item;
	return strcmp(method, entry->method);
}
static void uapi_printer(const struct bst_node *n)
{
	const struct uapi_entry *entry = bst_entry(n, struct uapi_entry, node);
	printf("%s\n", entry->method);
}
void uapi_table_printf(struct uapi_table* t)
{
	int i = 0;
	struct uapi_entry *entry = NULL;
	struct bst_node *n;
	for (n = bst_iterator_first(t->iterator, t->uapi); n; n = bst_iterator_next(t->iterator)) {
		entry = bst_entry(n, struct uapi_entry, node);
		LOGI("uapi %03d: method:%s", i++, entry->method);
	}
}

static struct uapi_table *uapi_table_init(struct uapi_table *t, unsigned int height)
{
	memset(t, 0 , sizeof(struct uapi_table));
	t->uapi = bst_create(uapi_comparator, uapi_searcher, uapi_printer, height);
	if (t->uapi == NULL)
		goto error;
	t->iterator = bst_iterator_init(t->uapi);
	if (t->iterator == NULL)
		goto error;
	return t;
error:
	if (t) {
		if (t->uapi)
			bst_destroy(t->uapi, NULL, 1);
		if (t->iterator)
			bst_iterator_free(t->iterator);
	}
	return NULL;
}
static struct uapi_table __uapi_table;
static const char *__uapi_path;
static int (*__uapi_handler)(struct ipc_msg *, void *, void *);
static int uapi_request_handler(struct ipc_msg *msg, void *arg, void *cookie)
{
	struct uapi_request *request = (struct uapi_request *)msg->data;
	struct uapi_entry* entry = NULL;
	struct bst_node *n = bst_search(__uapi_table.uapi, request->method);
	if (n) {
		entry = bst_entry(n, struct uapi_entry, node);
		struct json_object *json = json_tokener_parse(request->json);
		entry->handler(msg, arg, cookie, json);
		msg->data[msg->data_len++] = '\0';
		if (json)
			json_object_put(json);
	} else {
		LOGE("Not found method:%s", request->method);
		msg->data_len = sprintf(msg->data, "Method not found") + 1;
	}
	return 0;
}
static int uapi_list_handler(struct ipc_msg *msg, void *arg, void *cookie)
{
	int offs = 0;
	struct uapi_entry *entry = NULL;
	struct bst_node *n;
	struct uapi_table *t = &__uapi_table;
	offs += sprintf(msg->data + offs, "Path [%s]:\n", __uapi_path);
	for (n = bst_iterator_first(t->iterator, t->uapi); n; n = bst_iterator_next(t->iterator)) {
		entry = bst_entry(n, struct uapi_entry, node);
		offs += sprintf(msg->data + offs, "\tmethod: %-"UAPI_METHOD_ALIGN_LEN"s param: %s\n", entry->method, entry->param);
	}
	msg->data_len = offs + 1;
	return 0;
}
static int uapi_ipc_handler(struct ipc_msg *msg, void *arg, void *cookie)
{
	switch (msg->msg_id) {
	case UAPI_MSG_REQUEST:
		uapi_request_handler(msg, arg, cookie);	
		break;
	case UAPI_MSG_LIST:
		uapi_list_handler(msg, arg, cookie);
		break;
	case UAPI_MSG_PRIV:
	default:
		if (__uapi_handler)
			__uapi_handler(msg, arg, cookie);
		break;
	}
	return 0;
}
int uapi_ipc_init(const char *path, struct uapi_entry *entries, int n, int (*priv_handler)(struct ipc_msg *, void *, void *))
{
	if (!is_uapi_path(path)) {
		LOGE("Please use UAPI_PATH(path) define uapi path.");
		return -1;
	}
	if (ipc_server_init(path, uapi_ipc_handler) < 0)
		return -1;
	
	struct uapi_table *t = &__uapi_table;
	if (uapi_table_init(t, fhs(n)) == NULL) {
		LOGE("Table init failed.");
		return -1;
	}
	int i = 0;
	for (i = 0; i < n; i++) {
		struct uapi_entry *entry = &entries[i];
		bst_insert(t->uapi, &entry->node);
	}
	LOGI("uapi(%s) table size: %d", path, i);
	bst_balance(t->uapi);
	uapi_table_printf(t);
	__uapi_path 	= path;
	__uapi_handler 	= priv_handler;
	return 0;
}

struct json_object * uapi_get_json(struct ipc_msg *msg)
{
	struct uapi_request *request = (struct uapi_request *)msg->data;
	msg->data[msg->data_len++] = '\0';
	return json_tokener_parse(request->json);
}
void uapi_put_json(struct json_object *json)
{
	json_object_put(json);
}
