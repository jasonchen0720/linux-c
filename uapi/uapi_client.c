#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>
#include "list.h"
#include "ipc_client.h"
#include "uapi_common.h"
#include "uapi_client.h"
#include "generic_bit.h"
#define __LOG_TAG	"uapic"
#define UAPIC_DBG(format, ...) 	do { if (uapic_debug) LOGI("DBG "format, ##__VA_ARGS__); } while (0)
#define uapic_lock(flags)	do {if (flags & UAPI_F_SAFE) pthread_mutex_lock(&uapic_mutex);} while (0)
#define uapic_unlock(flags)	do {if (flags & UAPI_F_SAFE) pthread_mutex_unlock(&uapic_mutex);} while (0)

#define uapic_lock_acquire() pthread_mutex_lock(&uapic_mutex)
#define uapic_lock_release() pthread_mutex_unlock(&uapic_mutex)
static int uapic_debug = 0;
static pthread_mutex_t uapic_mutex = PTHREAD_MUTEX_INITIALIZER;

#define server_of(uapic) ((uapic)->handle->server + sizeof(UAPI_SOCK_DIR))
//#define UAPIC_PERF 1
#define VLA_SUPPORTED 1
struct uapic_struct {
	struct ipc_client *handle;
	union {
		struct bst_node node;
		struct list_head list;
	};
};
#if defined(UAPIC_PERF)
#define UAPIC_BST_HEIGHT		10
struct uapic_manager
{
	int imbalance;
	struct bst_table *table;
	struct bst_iterator *iterator;
};
static struct uapic_manager *uapic_mgr = NULL;
static int uapic_compare_func(const struct bst_node *n1, const struct bst_node *n2)
{
	const struct uapic_struct *a = bst_entry(n1, struct uapic_struct, node);
	const struct uapic_struct *b = bst_entry(n2, struct uapic_struct, node);
	return strcmp(server_of(a), server_of(b));
}
static int uapic_search_func(const void *item, const struct bst_node *n)
{
	const struct uapic_struct *entry = bst_entry(n, struct uapic_struct, node);
	const char *server = (const char *)item;
	return strcmp(server, server_of(entry));
}
static void uapic_print_func(const struct bst_node *n)
{
	struct uapic_struct *entry = bst_entry(n, struct uapic_struct, node);
	printf("%s\n", server_of(entry));
}

static int uapic_manager_create(struct uapic_manager **uapicm)
{
	static struct uapic_manager singleton;
	struct uapic_manager *manager = &singleton;
	manager->table = bst_create(uapic_compare_func, 
								uapic_search_func, 
								uapic_print_func, UAPIC_BST_HEIGHT);
	if (manager->table == NULL)
		goto error;
	manager->iterator = bst_iterator_init(manager->table);
	if (manager->iterator == NULL)
		goto error;
	manager->imbalance = 0;
	*uapicm = manager;
	return 0;
error:
	
	if (manager->table)
		bst_destroy(manager->table, NULL, 1);
	if (manager->iterator)
		bst_iterator_free(manager->iterator);
	return -1;
}
#else
static struct list_head uapic_head = list_head_initializer(uapic_head);
#endif
static struct uapic_struct *uapi_client_create(const char *server)
{
	struct uapic_struct *uapic = NULL;
#if defined(UAPIC_PERF)
	if (uapic_mgr) {
		struct bst_node *n = bst_search(uapic_mgr->table, server);
		if (n) {
			uapic = bst_entry(n, struct uapic_struct, node);
			UAPIC_DBG("Found uapic: %s", server_of(uapic));
			return uapic;
		}
	} else if (uapic_manager_create(&uapic_mgr) < 0) {
		LOGE("uapic manager init failure.");
		return NULL;
	} else
		LOGI("uapic manager init done.");
#else
	list_for_each_entry(uapic, &uapic_head, list) {
		if (!strcmp(server_of(uapic), server))
			return uapic;
	}
#endif	
	uapic = malloc(sizeof(*uapic));
	if (uapic == NULL) {
		LOGE("No memory");
		return NULL;
	}

	uapic->handle = ipc_client_create(server);

	if (uapic->handle == NULL) {
		LOGE("Request client create failure: %s.", server);
		free(uapic);
		return NULL;
	}
	LOGI("create client for %s @%d", server, getpid());
#if defined(UAPIC_PERF)
	bst_insert(uapic_mgr->table, &uapic->node);
	if (uapic_mgr->imbalance++ > UAPIC_BST_HEIGHT) {
		uapic_mgr->imbalance = 0;
		bst_balance(uapic_mgr->table);
	}
#else
	list_add(&uapic->list, &uapic_head);
#endif	
	return uapic;
}

static void uapi_client_destroy(struct uapic_struct *uapic)
{
#if defined(UAPIC_PERF)
	bst_remove(uapic_mgr->table, &uapic->node, 1);
#else
	list_del(&uapic->list);
#endif
	ipc_client_destroy(uapic->handle);
	free(uapic);
}
static int uapi_request_easily(const char *server, struct ipc_msg *msg, size_t size, int tmo)
{
	
	struct ipc_client client;
	if (ipc_client_init(server, &client) != 0) {
		LOGE("Request client create failure: %s.", server);
		return -1;
	}
	int ret = ipc_client_request(&client, msg, size, tmo);
	ipc_client_close(&client);

	if (ret != IPC_REQUEST_SUCCESS) {
		LOGE("Request msg:%d failure:%d.", msg->msg_id, ret);
		return -1;
	} 

	return 0;
}
static int uapi_request_oneshot(const char *server, struct ipc_msg *msg, size_t size, int tmo)
{
	struct ipc_client *client = ipc_client_create(server);
	if (client == NULL) {
		LOGE("Request client create failure: %s.", server);
		return -1;
	}

	int ret = ipc_client_request(client, msg, size, tmo);
	ipc_client_destroy(client);
	
	if (ret != IPC_REQUEST_SUCCESS) {
		LOGE("Request msg:%d failure:%d.", msg->msg_id, ret);
		return -1;
	}
	
	return 0;
}

static int uapi_request_multiplex(const char *server, struct ipc_msg *msg, size_t size, int tmo)
{
	struct uapic_struct *uapic = uapi_client_create(server);

	if (uapic == NULL)
		return -1;
	
	int ret = ipc_client_request(uapic->handle, msg, size, tmo);

 	/* 
	 * Anyway, release the client handle whenever got an error.
	 */
	if (ret != IPC_REQUEST_SUCCESS) {
		LOGE("Request msg:%d failure:%d.", msg->msg_id, ret);
		uapi_client_destroy(uapic);
		return -1;
	}
	
	return 0;
}
static int uapi_request_msg(const char *server, struct ipc_msg *msg, size_t size, int tmo, const int flags)
{
	int ret;
	/* 
	 * Left one byte for the terminating null byte ('\0') 
	 */
	size -= 1;
	msg->msg_id = UAPI_MSG_REQUEST;
	msg->flags = flags & UAPI_F_NOTIFY ? 0 : IPC_FLAG_REPLY;

	if (flags & UAPI_F_ONESHOT) {
		ret = uapi_request_oneshot(server, msg, size, tmo);
	} else if (flags & UAPI_F_MULTIPLEX) {
		uapic_lock(flags);		
		ret = uapi_request_multiplex(server, msg, size, tmo);	
		uapic_unlock(flags);
	} else {
		ret = uapi_request_easily(server, msg, size, tmo);
	}
	if (ret < 0)
		return -1;
	if (flags & UAPI_F_NOTIFY)
		msg->data_len = 0;
		
	msg->data[msg->data_len] = '\0';
	return 0; 
}

char *uapi_method_list(const char *path, char *buff, size_t size, int tmo)
{
	struct ipc_msg *msg = (struct ipc_msg *)buff;
	msg->msg_id = UAPI_MSG_LIST;
	msg->flags = IPC_FLAG_REPLY;
	msg->data_len = 0;
	return uapi_request_easily(path, msg, size, tmo) == 0 ? msg->data : NULL;
}
/*
 * @path:   Path serving for this method.
 * @method: Method name, its length needs to be smaller than 32-byte.
 * @json:   Parameter of JSON string type.
 * @buf:    This buffer provided by caller, used to generate method request internally and also store response.
 *          Be ensure its size is bigger than max[64 + strlen(json), max length of response]. 
 *          Note : (64 : sizeof(struct ipc_msg) + sizeof(struct uapi_request))
 * @size:   Buffer size
 * @tmo:	Timeout, unit sec.
 * @flags:  Flags:
 *			NONE				0 - Temporary connection will be used for the method invoking.
 *			UAPI_F_SAFE			1 - The method callings may be happening in a multithread environment
 *          UAPI_F_ONESHOT		2 - Long connection, but oneshot.
 *			UAPI_F_MULTIPLEX	4 - Long connection, Caller will call the path repeatedly.
 *          UAPI_F_NOTIFY		8 - None response expected
 *
 * Return:	return a JSON response using the space of @buf. The returned pointer points to a position of @buf(Not the beginning).
 *          <!- Caller should not take care of deleting JSON response memory returned.
 *              Caller should only take care of deleting memory of @buf if needed.     -!>			
 */
char *uapi_method_invoke(const char *path, 
								const char *method, 
								const char *json, char *buf, size_t size, int tmo, int flags)
{
	struct ipc_msg 		*msg = (struct ipc_msg *)buf;
	struct uapi_request *req = (struct uapi_request *)msg->data;
	size_t length;
	if (path == NULL || method == NULL) {
		LOGE("Path / Method null.");
		return NULL;
	}
	if (json == NULL)
		json = "";
		
	length = strlen(method);
	if (length > UAPI_METHOD_MAX_LENGTH) {
		LOGE("Method too long:%s", method);
		return NULL;
	}
	
	length = strlen(json);
	if ((sizeof(*msg) + sizeof(*req) + length) >= size) {
		LOGE("Buffer not enough.");
		return NULL;
	}
	strcpy(req->method, method);
	strcpy(req->json, 	json);
	msg->data_len = sizeof(struct uapi_request) + length + 1;
	return uapi_request_msg(path, msg, size, tmo, flags) < 0 ? NULL : msg->data;
}

struct uapic_subscriber 
{
	void *arg;
	void (*callback)(const char *event, void *data, size_t size, void *arg);
	struct bst_table *table;
	struct bst_iterator *iterator;
	struct ipc_subscriber * subscriber;
};
static struct uapic_subscriber *uapic_subs = NULL;
#define uapi_notify_jsonlen(notify_len) (notify_len - sizeof(struct uapi_notify))
static int uapic_event_compare_func(const struct bst_node *n1, const struct bst_node *n2)
{
	const struct uapic_event *a = bst_entry(n1, struct uapic_event, node);
	const struct uapic_event *b = bst_entry(n2, struct uapic_event, node);
	return strcmp(a->name, b->name);
}
static int uapic_event_search_func(const void *item, const struct bst_node *n)
{
	const struct uapic_event *entry = bst_entry(n, struct uapic_event, node);
	const char *name = (const char *)item;
	return strcmp(name, entry->name);
}
static void uapic_event_print_func(const struct bst_node *n)
{
	struct uapic_event *entry = bst_entry(n, struct uapic_event, node);
	printf("%s\n", entry->name);
}
static int uapic_event_table_create(struct uapic_subscriber *subs, unsigned int height)
{
	subs->table = bst_create(uapic_event_compare_func, 
							 uapic_event_search_func, 
							 uapic_event_print_func, height);
	if (subs->table == NULL) 
		goto error;
	subs->iterator = bst_iterator_init(subs->table);
	if (subs->iterator == NULL) 
		goto error;
	return 0;
error:
	
	if (subs->table)
		bst_destroy(subs->table, NULL, 1);
	if (subs->iterator)
		bst_iterator_free(subs->iterator);
	return -1;
}

void uapic_event_table_printf(struct uapic_subscriber * subs)
{
	int i = 0;
	struct bst_node *n;
	//LOGI("printf uapic event table: @%p", subs->table);
	for (n = bst_iterator_first(subs->iterator, subs->table); n; n = bst_iterator_next(subs->iterator)) {
		struct uapic_event *entry = bst_entry(n, struct uapic_event, node);
		LOGI("uapic %03d: event: %s", i++, entry->name);
	}
}

static int uapic_event_callback(int msg, void *data, int size, void *arg)
{
	struct bst_node *node;
	struct uapi_notify   * notify   = (struct uapi_notify   *)data;
	struct uapic_subscriber * subs = (struct uapic_subscriber *)arg;
	switch (msg) {
	case UAPI_EVENT:
		LOGI("%d recvd event '%s', data: %s", subs->subscriber->client.identity, notify->event, notify->json);
		node = bst_search(subs->table, notify->event);
		if (node) {
			struct uapic_event *event = bst_entry(node, struct uapic_event, node);
			if (event->callback)
				event->callback(notify->json, uapi_notify_jsonlen(size), event->arg);
			else if (subs->callback)
				subs->callback(notify->event, notify->json, uapi_notify_jsonlen(size), subs->arg);
			else
				LOGE("No handler for event: %s", event->name);
		} else {
			LOGE("Not found uapic_event: %s", notify->event);
			uapic_event_table_printf(subs);
		}
		
		break;
	case UAPI_EVENT_DEBUG:
		uapic_debug = 1;
		LOGI("uapic debug enabled.");
		break;
	}
	return 0;
}
int uapi_event_register(struct uapic_event *events, int count,
	void (*callback)(const char *event, void *data, size_t size, void *arg),
	void *arg)
{
	static struct uapic_subscriber singleton;
	char tmp[1024];
	char *alloc = NULL;
	char *buf = tmp;
	size_t size = sizeof(tmp);
	int i = 0, ssize, offs = sizeof(struct uapi_register);
	struct uapi_register *regevent = NULL;
	struct uapic_subscriber *subs = &singleton;
	uapic_lock_acquire();

	if (uapic_subs) {
		LOGE("Already registered.");
		goto err;
	}

	if (uapic_event_table_create(subs, fhs(count)) < 0) {
		LOGE("event table create failure.");
		goto err;
	}
	regevent = (struct uapi_register *)buf;
	regevent->count = count;
	while (i < count) {
		if (strlen(events[i].name) > UAPI_EVENT_MAX_LENGTH) {
			goto err;
		}
		ssize = snprintf(buf + offs, size - offs, "%s%c", events[i].name, UAPI_EVENT_DELIMITER);
		if (offs + ssize < size) {
			bst_insert(subs->table, &events[i].node);
			offs += ssize;
			LOGI("included event: %s", events[i].name);
			i++;
			continue;
		}
		size <<= 1;
		char *p = (char *)realloc(alloc, size);
		if (p) {
			alloc = p;
			buf = alloc;
			regevent = (struct uapi_register *)buf;
			LOGI("realloc: %lu", size);
			continue;
		}
		LOGE("realloc failed, alloc@%p", alloc);
		goto err;
	}

	buf[offs - 1] = '\0';
	subs->arg = arg;
	subs->callback = callback;
	LOGI("reg mask: %lx, event count: %d, size: %d, events: %s.", UAPI_EVENT_MASK, regevent->count, offs, regevent->events);
	subs->subscriber = ipc_subscriber_register(UAPI_BROKER, UAPI_EVENT_MASK, buf, offs, uapic_event_callback, subs);
	if (subs->subscriber == NULL) {
		goto err;
	}
	bst_balance(subs->table);
	uapic_event_table_printf(subs);
	uapic_subs = subs;
	uapic_lock_release();
	if (alloc)
		free(alloc);
	return 0;
err:
	uapic_lock_release();
	if (alloc)
		free(alloc);
	while (i)
		bst_remove(subs->table, &events[--i].node, 1);
	return -1;
}
static int uapi_publish(struct ipc_msg *msg, int notify_len)
{
	int ret;
	if (uapic_subs) {
		uapic_lock_acquire();
		ret = ipc_client_publishx(&uapic_subs->subscriber->client, msg, IPC_TO_BROADCAST, 
				UAPI_EVENT_MASK, 
				UAPI_EVENT, notify_len);
		uapic_lock_release();
		UAPIC_DBG("ret: %d", ret);
		return ret == IPC_SUCCESS ? 0 : -1;
	} else {
		struct ipc_client client;
		if (ipc_client_init(UAPI_BROKER, &client) < 0) {
			return -1;
		}
		ret = ipc_client_publishx(&client, msg, IPC_TO_BROADCAST, 
				UAPI_EVENT_MASK, 
				UAPI_EVENT, notify_len);
		ipc_client_close(&client);
		UAPIC_DBG("ret: %d", ret);
		return ret == IPC_SUCCESS ? 0 : -1;
	}
}


int uapi_event_push(const char *event, const char *json)
{
	if (strlen(event) > UAPI_EVENT_MAX_LENGTH) {
		return -1;
	}
	if (json == NULL) {
		json = "";
	}
	/* 
	 * total length of uapi_notify, including a terminating null byte ('\0') at the end of json string
	 */
	int l = strlen(json) + 1 + sizeof(struct uapi_notify);
	int s = ipc_notify_buffer_size(l);
#if defined(VLA_SUPPORTED)
	char b[s]; /* supported on GCC since C99 */
#else
	char *b = malloc(s);
	if (b == NULL) {
		return -1;
	}
#endif
	struct uapi_notify *notify = ipc_notify_payload_of(b, struct uapi_notify);
	strcpy(notify->event, event);
	strcpy(notify->json, json);
#if defined(VLA_SUPPORTED)
	return uapi_publish((struct ipc_msg *)b, l);
#else
	int ret = uapi_publish((struct ipc_msg *)b, l);
	free(b);
	return ret;
#endif
}

