#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>
#include <time.h>
#include <sys/stat.h>

#include "ipc_server.h"

#include "bst.h"
#include "uapi_common.h"
#include "uapi_json.h"
#define __LOG_TAG				"uapib"
#define UAPI_SUBSCRIBER_COOKIE_TYPE	1
#define UAPI_BROKER_BST_HEIGHT		10
struct uapi_subscriber_node {
	struct uapi_event *event;
	struct uapi_subscriber *subscriber;
	struct list_head list;
};

struct uapi_subscriber {
	int type;
	const struct ipc_server *sevr;
	char *events;
	size_t count;
	struct uapi_subscriber_node *nodes;
};

struct uapi_event {
	const char *name;
	struct list_head head; /* list head for struct uapi_subscriber_node */
	struct bst_node node;
};

struct uapi_service {
	char *path;
	char *methods;
	struct bst_node   node;
	struct bst_table *table;

};

struct uapi_broker {
	int               etable_imbalance;
	struct bst_table *etable;
	struct bst_iterator *iterator;
};
static struct uapi_broker uapi_broker = {0};
static int uapi_event_compare_func(const struct bst_node *n1, const struct bst_node *n2)
{
	const struct uapi_event *a = bst_entry(n1, struct uapi_event, node);
	const struct uapi_event *b = bst_entry(n2, struct uapi_event, node);
	return strcmp(a->name, b->name);
}
static int uapi_event_search_func(const void *item, const struct bst_node *n)
{
	const struct uapi_event *entry = bst_entry(n, struct uapi_event, node);
	const char *name = (const char *)item;
	return strcmp(name, entry->name);
}
static void uapi_event_print_func(const struct bst_node *n)
{
	struct uapi_event *entry = bst_entry(n, struct uapi_event, node);
	printf("%s\n", entry->name);
}
void uapi_event_table_printf(struct uapi_broker *broker)
{
	int i = 0;
	struct bst_node *n;
	for (n = bst_iterator_first(broker->iterator, broker->etable); n; n = bst_iterator_next(broker->iterator)) {
		struct uapi_event *entry = bst_entry(n, struct uapi_event, node);
		LOGI("uapi %03d: event name:%s", i++, entry->name);
	}
}
static void uapi_broker_table_balance(struct uapi_broker *broker)
{
	if (broker->etable_imbalance >= broker->etable->bst_max_height) {
		broker->etable_imbalance = 0;
		bst_balance(broker->etable);
		uapi_event_table_printf(broker);
	}
}

static int uapi_broker_init(struct uapi_broker *broker, unsigned int height)
{
	broker->etable = bst_create(uapi_event_compare_func, 
									uapi_event_search_func, 
									uapi_event_print_func, height);
	if (broker->etable == NULL)
		goto error;
	broker->iterator = bst_iterator_init(broker->etable);
	if (broker->iterator == NULL)
		goto error;
	broker->etable_imbalance = 0;
	return 0;
error:
	
	if (broker->etable)
		bst_destroy(broker->etable, NULL, 1);
	if (broker->iterator)
		bst_iterator_free(broker->iterator);
	
	return -1;
}

static int uapi_broker_event_deliver(struct ipc_notify * notify, struct uapi_broker *broker)
{
	struct uapi_notify *unotify = (struct uapi_notify *)notify->data;
	struct bst_node *n = bst_search(broker->etable, unotify->event);
	if (n) {
		LOGI("recvd event '%s' , data: %s", unotify->event, unotify->json);
		const struct uapi_event *event_entry = bst_entry(n, struct uapi_event, node);
		struct uapi_subscriber_node *node;
		if (notify->to == IPC_TO_BROADCAST) {
			list_for_each_entry(node, &event_entry->head, list) {
				LOGI("deliver event to %d", node->subscriber->sevr->identity);
				ipc_server_forward(node->subscriber->sevr, notify);
			}
		} else {
			list_for_each_entry(node, &event_entry->head, list) {
				if (node->subscriber->sevr->identity != notify->to)
					continue;
				LOGI("notify event to %d", node->subscriber->sevr->identity);
				ipc_server_forward(node->subscriber->sevr, notify);
				break;
			}
		}
	} else
		LOGW("Not found any subscribers for event:%s", unotify->event);
	/* Always -1 is returned */
	return -1;
}

static void uapi_broker_subscriber_unregister(struct uapi_subscriber * subscriber)
{
	if (subscriber->nodes) {
		int i;
		for (i = 0 ; i < subscriber->count; i++) {
			if (subscriber->nodes[i].event)
				list_del(&subscriber->nodes[i].list);
			else
				break;
		}
		free(subscriber->nodes);
	}
	if (subscriber->events) {
		free(subscriber->events);
	}
	free(subscriber);
}
static int uapi_broker_subscriber_register(const struct ipc_server *sevr, 
	struct uapi_register *r, 
	struct uapi_broker *broker)
{
	int i = 0;

	struct uapi_subscriber *subscriber = malloc(sizeof(struct uapi_subscriber));
	if (subscriber == NULL)
		goto err;

	subscriber->nodes = calloc(r->count, sizeof(struct uapi_subscriber_node));
	if (subscriber->nodes == NULL) {
		goto err;
	}
	
	subscriber->events = strdup(r->events);
	if (subscriber->events == NULL) {
		goto err;
	}
	
	subscriber->count = r->count;
	subscriber->type = UAPI_SUBSCRIBER_COOKIE_TYPE;
	subscriber->sevr = sevr;
	char *event;
	char *delimiter;

	LOGI("event register: %s.", r->events);
	for (event = subscriber->events, i = 0; i < r->count; i++, event = delimiter + 1) {
		delimiter = strchr(event, UAPI_EVENT_DELIMITER);

		if (delimiter)
			delimiter[0] = '\0';

		struct uapi_event *event_entry;
		struct bst_node *n = bst_search(broker->etable, event);
		if (n) {
			event_entry = bst_entry(n, struct uapi_event, node);
			list_add_tail(&subscriber->nodes[i].list, &event_entry->head);
		} else {
			event_entry = (struct uapi_event *)malloc(sizeof(*event_entry));
			if (event_entry == NULL) {
				goto err;
			}
			event_entry->name = strdup(event);
			if (event_entry->name == NULL) {
				free(event_entry);
				goto err;
			}
			INIT_LIST_HEAD(&event_entry->head);
			list_add_tail(&subscriber->nodes[i].list, &event_entry->head);
			bst_insert(broker->etable, &event_entry->node);
			broker->etable_imbalance++;
			uapi_broker_table_balance(broker);
			LOGI("created new event: '%s'", event);
		}
		subscriber->nodes[i].subscriber = subscriber;
		subscriber->nodes[i].event = event_entry;

		if (delimiter == NULL)
			break;
	}
	ipc_server_bind(sevr, IPC_COOKIE_USER, subscriber);
	return 0;
err:
	if (subscriber) {
		uapi_broker_subscriber_unregister(subscriber);
	}
	LOGE("uapi_subscriber create failed.");
	return -1;
}

static void daemonize(void) {
	pid_t pid;

	if ((pid = fork()) != 0)
		exit(0);

	setsid();
	signal(SIGHUP, SIG_IGN);

	if ((pid = fork()) != 0)
		exit(0);

	chdir("/");
	umask(0);
}
static int uapi_broker_ipc_handler(struct ipc_msg* ipc_msg, void *arg, void *cookie)
{
	LOGI("ipc_msg->msg_id[%04x]\n", ipc_msg->msg_id);
	ipc_msg->data_len = sprintf(ipc_msg->data, JSON_fmt2(KF_s("reply"), KF_ld("time")), "echo from broker", time(NULL)) + 1;
	return 0;
}

static int uapi_broker_ipc_notify_filter(struct ipc_notify *notify, void *arg)
{
	LOGI("notify message: %d", notify->msg_id);

	switch (notify->msg_id) {
	case UAPI_EVENT:
		return uapi_broker_event_deliver(notify, arg);
	default:
		break;
	}
	return 0;
}

static int uapi_broker_ipc_client_manager(const struct ipc_server *sevr, int cmd, 
	void *data, void *arg, void *cookie)
{
	LOGI("ipc manager cmd: %d", cmd);
	switch(cmd) {
	case IPC_CLIENT_RELEASE:
		break;
	case IPC_CLIENT_CONNECT:
		break;
	case IPC_CLIENT_REGISTER:
		LOGI("register mask: %lx", ipc_subscribed(sevr, ~0LU));
		if (ipc_subscribed(sevr, UAPI_EVENT_MASK)) {
			return uapi_broker_subscriber_register(sevr, data, arg);
		}
		break;
	case IPC_CLIENT_SYNC:
		break;
	case IPC_CLIENT_UNREGISTER:
	case IPC_CLIENT_SHUTDOWN:
		if (ipc_cookie_type(cookie) == UAPI_SUBSCRIBER_COOKIE_TYPE) {
			uapi_broker_subscriber_unregister(cookie);
		}
		break;
	default:
		return -1;
	}
	return 0;
}
int main(int argc, char **argv)
{
	int c = 0;
	int is_daemonize = 1;
	for (;;) {
		c = getopt(argc, argv, "f");
		if (c < 0)
			break;
		switch (c) {
		case 'f':
			is_daemonize = 0;
			break;
		default:
			exit(-1);
		}
	}

	if (is_daemonize) {
		daemonize();
	}

	struct uapi_broker *broker = &uapi_broker;
	
	if (uapi_broker_init(broker, UAPI_BROKER_BST_HEIGHT) < 0) {
		goto out;	
	}
	if (ipc_server_init(UAPI_BROKER, uapi_broker_ipc_handler) < 0)
		goto out;
	if (ipc_server_setopt(IPC_SEROPT_SET_FILTER,  uapi_broker_ipc_notify_filter) < 0)
		goto out;
	if (ipc_server_setopt(IPC_SEROPT_SET_MANAGER, uapi_broker_ipc_client_manager) < 0)
		goto out;
	if (ipc_server_setopt(IPC_SEROPT_SET_ARG, broker) < 0)
		goto out;
	if (ipc_server_run() < 0)
		goto out;
out:
	ipc_server_exit();
	return 0;
}
