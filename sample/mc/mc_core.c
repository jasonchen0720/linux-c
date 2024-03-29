#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <assert.h>
#include <errno.h>
#include <ctype.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/socket.h>

#include "mc_system.h"
#include "mc_watchdog.h"
#include "mc_exception.h"

#include "generic_proc.h"

#define LOG_TAG 	"core"
enum MC_CLIENT_F_BITS 
{
	MC_CLIENT_F_EXIT	= 0,
	MC_CLIENT_F_KILL,
	MC_CLIENT_F_UNREG,
	
	MC_CLIENT_F_MAX = 15,

	/* Bit 16 - 32 for mc strategies see enum MC_STRATEGY */
};

#define set_client_state(client, s) do {(client)->state = (s);} while (0)
#define MC_CLIENT_RUNNING		1
#define MC_CLIENT_DEAD			2
#define MC_CLIENT_PRESTART		4
#define MC_CLIENT_STARTING		8
#define MC_CLIENT_BLOCKED		16
#define MC_CLIENT_DETACHED		32
/*
 * MC exception info.
 */
extern struct mc_einfo 	__mc_einfo;
/*
 * MC config.
 */
extern struct mc_config __mc_config;
static int __mc_evtfd = -1;
/*
 * MC ready clients.
 */
static struct mc_ready	__mc_ready;
static struct mc_timers __mc_timers;
static struct mc_struct __mc_struct;
/* Synchors for sleep / reboot / resume */
static struct mc_synchor __mc_synchors[3];
/* Referees for sleep / reboot */
static struct mc_referee __mc_referees[2];
static int mc_index_alloc(struct mc_struct *mc)
{
	int i = first_bit_zero(mc->index_stock);

	set_bit(i, mc->index_stock);
	LOGI("Alloc index:%d", i);
	return i;
}
static void mc_index_free(int index, struct mc_struct *mc)
{
	clear_bit(index, mc->index_stock);
	LOGI("Free index:%d", index);
}
static void mc_kill_guard(struct mc_struct *mc)
{
	struct mc_guard *guard;
	list_for_each_entry(guard, &mc->guard_head, list) {
		if (mc_guard_inspect(guard) == 0) {
			LOGP("System shutdown, Kill %s", guard->name);
			kill(guard->pid, SIGKILL);
		}
	}
}
static int mc_kill_client(struct mc_client *client)
{
	/* Kill Probe first. */
	if (kill(client->pid, 0) < 0) {
		LOGW("Kill probe: none existence, %s %d", client->name, client->pid);
		goto out;
	} 

	char name[64];
	memset(name, 0, sizeof(name));
	process_name(name, sizeof(name), client->pid);
	if (strcmp(name, client->name)) {
		LOGW("Kill %s %d: is not target.", client->name, client->pid);
		goto out;
	} 		
	if (kill(client->pid, SIGKILL)) {
		LOGE("SIGKILL to %s failed.", client->name);
		return -1;
	}
	LOGI("%s pid:%d killed.", client->name, client->pid);
out:
	client->flags |= BIT(MC_CLIENT_F_KILL);
	return 0;
}
static int mc_exit_client(struct mc_client *client)
{
	LOGI("Exit notify:%s.", client->name);
	if (ipc_server_notify(client->subscriber->sevr, 
							MC_GUARD_MASK, MC_IND_EXIT, NULL, 0) == 0)
		client->flags |= BIT(MC_CLIENT_F_EXIT);
	else
		LOGE("Exit notify error.");
	return 0;
}
static int device_reboot(struct ipc_timing *timing)
{
	struct mc_struct *mc = timing->arg;
	LOGP("Reboot timer expired.");
	mc_reboot_system(mc, 0);
	return 0;
}

static int client_restart_review_timing(struct ipc_timing *timing)
{
	struct mc_client *client = timing->arg;

	switch (client->state) {
	case MC_CLIENT_RUNNING:
		ipc_timing_unregister(timing);
		break;
	case MC_CLIENT_STARTING:
		if (process_find(client->name) == 0)
			mc_process_latch(client->cmdline);
		break;
	default:
		LOGW("%s unexpected state:%d", client->name, client->state);
		break;
	}
	return 0;
}
static int client_restart_entry(struct ipc_timing *timing)
{
	struct mc_client *client = timing->arg;
	LOGI("Restarting client:%s state:%d", client->name, client->state);
	assert(client->state == MC_CLIENT_PRESTART);
	if (mc_process_restart(client->name, client->cmdline) < 0)
		return -1;
	set_client_state(client, MC_CLIENT_STARTING);
	ipc_server_publish(IPC_TO_BROADCAST,
	 					MC_SYNC_MASK, MC_IND_SYNC_RESTART, client->name, strlen(client->name) + 1);
	ipc_timing_unregister(timing);
	ipc_timing_init(&client->restart_timing, 1, client->latch_time, 0, 
							client, client_restart_review_timing);
	ipc_timing_register(&client->restart_timing);
	
	return 0;
}
static void mc_task_active(struct mc_task *task, struct mc_struct *mc)
{
	struct mc_task *task_tmp;
	list_del(&task->list);
	task->expire = mc_gettime() + mc->config->loss_max * task->interval;
	
	list_for_each_entry(task_tmp, &mc->task_head, list) {
		//LOGI("HT expire %s, interval:%d, delta:%ld - %ld", task->client->name, task->interval, task->expire, task_tmp->expire);
		if (task->expire >= task_tmp->expire)
			break;
	}

	list_add_tail(&task->list, &task_tmp->list);
	//list_for_each_entry(task_tmp, &mc->task_head, list) {LOGI("HT list %s:%ld", task_tmp->client->name, task_tmp->expire);}
}
static void mc_client_active(struct mc_client *client, struct mc_struct *mc, long probe_time)
{
	client->probe_expire = probe_time + mc->config->loss_max * mc->config->probe_interval;
	
	struct mc_client *client_tmp;
	list_del(&client->list);
	
	list_for_each_entry(client_tmp, &mc->work_head, list) {
		//LOGI("Probe expire %s, interval:%d, delta:%ld - %ld", client->name, mc->config->probe_interval, client->probe_expire, client_tmp->probe_expire);
		if (client->probe_expire >= client_tmp->probe_expire)
			break;
	}

	list_add_tail(&client->list, &client_tmp->list);
	//list_for_each_entry(client_tmp, &mc->work_head, list) {LOGI("Probe list %s:%ld", client_tmp->name, client_tmp->probe_expire);}
}
static int mc_client_restart(struct mc_client *client, struct mc_struct *mc)
{	
	ipc_timing_init(&client->restart_timing, 1, mc->config->delay_restart, 0, 
								client, client_restart_entry);
	ipc_timing_register(&client->restart_timing);	
	set_client_state(client, MC_CLIENT_PRESTART);
	LOGI("%s will restart %ds later.", client->name, mc->config->delay_restart);
	return 0;
}
static struct mc_client *mc_client_construct(
	struct mc_reginfo *reginfo, 
	struct mc_subscriber *subscriber,
	struct mc_struct *mc)
{
	int i;
	struct mc_client *client = NULL;
	
	list_for_each_entry(client, &mc->dead_head, list) {
		if (!strcmp(reginfo->name, client->name) && 
			!strcmp(reginfo->cmdline, client->cmdline) && reginfo->count == client->count) {
			assert(client->tasks);
			list_del_init(&client->list);
			LOGI("Client found, state:%d.", client->state);
			if (client->state & (MC_CLIENT_STARTING | MC_CLIENT_PRESTART))
				ipc_timing_unregister(&client->restart_timing);
			goto out;
		}
	}
	
	client = (struct mc_client *)malloc(sizeof(struct mc_client));
	if (!client) {
		LOGE("Client alloc:None Memory.");
		return NULL;
	}
	client->tasks = (struct mc_task *)calloc(reginfo->count, sizeof(struct mc_task));
	if (!client->tasks) {
		LOGE("Task alloc:None Memory.");
		free(client);
		return NULL;
	}
	client->count		= reginfo->count;
	client->latch_time	= reginfo->latch_time;
	strcpy(client->name, 	reginfo->name);
	strcpy(client->cmdline, reginfo->cmdline);
	INIT_LIST_HEAD(&client->list);
	struct mc_guard *guard = mc_guard_search(client->name);
	if (guard) {
		list_del_init(&guard->list);
		LOGW("Guard pid:%d, reginfo pid:%d", guard->pid, reginfo->pid);
	}
	client->guard 	= guard;
out:
	client->pid		= reginfo->pid;
	client->flags 	= 0;
	client->subscriber = subscriber;
	for (i = 0; i < reginfo->count; i++) {	
		client->tasks[i].tid 	  = reginfo->tasks[i].tid;
		client->tasks[i].strategy = reginfo->tasks[i].strategy;
		client->tasks[i].interval = reginfo->tasks[i].interval;
		client->tasks[i].client   = client;
		INIT_LIST_HEAD(&client->tasks[i].list);
		mc_task_active(&client->tasks[i], mc);
		LOGI("Register task(%s):%d, HT interval:%d, strategy:%d.", 
			client->name, client->tasks[i].tid, client->tasks[i].interval, client->tasks[i].strategy);
	}
	mc_client_active(client, mc, mc->probe_time);
	
	set_client_state(client, MC_CLIENT_RUNNING);	
	LOGI("Alloc Client done.");
	return client;
}
static void mc_client_destroy(struct mc_client *client, struct mc_struct *mc)
{
	int i;
	LOGI("Client destroy, state:%d.", client->state);

	/* Delete from mc->loss_head or work_head or detach_head */
	list_del(&client->list);
	
	for (i = 0; i < client->count; i++)
		list_del_init(&client->tasks[i].list);
	
	if (client->guard)
		clear_bit(client->guard->id, mc->ready->bitmap);
	if (client->flags & BIT(MC_CLIENT_F_UNREG)) {
		if (client->guard && !(client->state & MC_CLIENT_DETACHED))
			list_add_tail(&client->guard->list, &mc->guard_head);
		free(client->tasks);
		free(client);
	} else {
		client->flags 		= 0;	
		client->pid 		= 0;
		client->subscriber	= NULL;
		set_client_state(client, MC_CLIENT_DEAD);
		list_add_tail(&client->list, &mc->dead_head);
	}
}
int mc_client_heartbeat_msg(struct ipc_msg *msg, void *cookie)
{
	struct mc_heartbeat *ht = (struct mc_heartbeat *)msg->data;
	if (ipc_class(msg) != IPC_CLASS_SUBSCRIBER)
		return -1;

	if (ipc_cookie_type(cookie) != MC_IPC_COOKIE_CLIENT) {
		LOGE("Invalid cookie.");
		return -1;
	}

	struct mc_client *client = get_client_from_cookie(cookie);
	
	assert(ipc_subscribed(client->subscriber->sevr, MC_GUARD_MASK));

	if (client->pid != ht->pid) {
		LOGE("Invalid pid.");
		return -1;
	}
	
	if (client->state & MC_CLIENT_DETACHED) {
		LOGH("HT(%s):%d-%d, detached.", client->name, ht->pid, ht->tid);
		return 0;
	}

	LOGH("HT(%s):%d-%d.", client->name, ht->pid, ht->tid);
		
	int i;
	struct mc_task *task;
	for (i = 0, task = client->tasks; i < client->count; i++, task++) {
		if (task->tid == ht->tid) {
			mc_task_active(task, client->subscriber->mc);
			if (task->client->flags & BIT(task->strategy))
				task->client->flags &= ~(BIT(task->strategy));
			return 0;
		}
	}
	LOGE("Task not found.");
	return -1;
}
int mc_client_probe_msg(struct ipc_msg *msg, void *cookie)
{
	if (ipc_class(msg) == IPC_CLASS_SUBSCRIBER && 
		ipc_cookie_type(cookie) == MC_IPC_COOKIE_CLIENT) {
		struct mc_client *client = get_client_from_cookie(cookie);
		assert(ipc_subscribed(client->subscriber->sevr, MC_GUARD_MASK));
		struct mc_probem *probe = (struct mc_probem *)msg->data;
		if (client->state & MC_CLIENT_DETACHED) {
			LOGH("Probe ack:%ld from detached client:%s", probe->time, client->name);
			return 0;
		}
		mc_client_active(client, client->subscriber->mc, probe->time);
		LOGH("Probe ack:%ld from client:%s", probe->time, client->name);
	}
	return 0;
}
int mc_client_ready_msg(struct ipc_msg *msg, void *cookie)
{
	if (ipc_class(msg) == IPC_CLASS_SUBSCRIBER && 
		ipc_cookie_type(cookie) == MC_IPC_COOKIE_CLIENT) {
		struct mc_client *client = get_client_from_cookie(cookie);
		assert(ipc_subscribed(client->subscriber->sevr, MC_GUARD_MASK));
		
		if (strcmp(msg->data, client->name))
			return -1;
		if (client->guard)
			set_bit(client->guard->id, client->subscriber->mc->ready->bitmap);

		strcpy(client->subscriber->mc->ready->name, client->name);
		return ipc_server_publish(IPC_TO_BROADCAST, 
								MC_SYNC_MASK, MC_IND_SYNC_READY, client->subscriber->mc->ready, sizeof(struct mc_ready));
	}
	return -1;
}
int mc_client_exit_msg(struct ipc_msg *msg, void *cookie)
{
	if (ipc_cookie_type(cookie) == MC_IPC_COOKIE_CLIENT) {
		struct mc_client *client = get_client_from_cookie(cookie);
		LOGI("Client:%s exit.", client->name);
	}
	return 0;
}
static int mc_client_attach(const char *name, int guard_id, struct mc_struct *mc)
{
	struct mc_client *temp, *client = NULL;
	list_for_each_entry(temp, &mc->detach_head, list) {
		if (!strcmp(temp->name, name)) {
			client = temp;
			break;
		}
	}
	if (client) {
		int i;
		for (i = 0; i < client->count; i++)
			mc_task_active(&client->tasks[i], mc);
		mc_client_active(client, mc, mc->probe_time);
		set_client_state(client, MC_CLIENT_RUNNING);
	} else {
		struct mc_guard *guard = mc_guard_search(name);
		if (guard && guard->id == guard_id) {
			list_del(&guard->list);
			list_add_tail(&guard->list, &mc->guard_head);
		}
	}
	return 0;
}

static int mc_client_detach(const char *name, int guard_id, struct mc_struct *mc)
{
	struct mc_client *temp, *client = NULL;
	
	list_for_each_entry(temp, &mc->work_head, list) {
		if (!strcmp(temp->name, name)) {
			client = temp;
			break;
		}
	}
	if (client) {
		int i;
		for (i = 0; i < client->count; i++)
			list_del_init(&client->tasks[i].list);
		
		list_del(&client->list);
		list_add_tail(&client->list, &mc->detach_head);
		set_client_state(client, MC_CLIENT_DETACHED);
		LOGI("Detach %s successfully.", name);
	} else {
		struct mc_guard *guard = mc_guard_search(name);
		LOGI("Detach from guard list, guard ID: %d.", guard_id);
		if (guard && guard->id == guard_id)
			/* Delete from mc->guard_head */
			list_del_init(&guard->list);
	}
	return 0;
}
int mc_client_detach_msg(struct ipc_msg *msg,  void *arg, void *cookie)
{
	struct mc_struct *mc = arg;
	struct mc_detach *detach = (struct mc_detach *)msg->data;
	LOGI("%s Message from %s", detach->state ? "Detach" : "Attach", detach->name);
	return detach->state ? 
		mc_client_detach(detach->name, detach->guard_id, mc) : mc_client_attach(detach->name, detach->guard_id, mc);
	
}
static int mc_tools_msg_process(struct ipc_msg *msg,  struct mc_struct *mc)
{
	const char *command = msg->data;

	int offs = 0;
	if (!strcmp(command, "show")) {
		struct mc_client *client;
		list_for_each_entry(client, &mc->work_head, list) {
			offs += sprintf(msg->data + offs, "%-16s(R) \tpid:%-6d \tID:%d\n", client->name, client->pid, client->guard ? client->guard->id : -1);
		}
		list_for_each_entry(client, &mc->detach_head, list) {
			offs += sprintf(msg->data + offs, "%-16s(D) \tpid:%-6d \tID:%d\n", client->name, client->pid, client->guard ? client->guard->id : -1);
		}

		list_for_each_entry(client, &mc->loss_head, list) {
			offs += sprintf(msg->data + offs, "%-16s(L) \tpid:%-6d \tID:%d\n", client->name, client->pid, client->guard ? client->guard->id : -1);
		}

		list_for_each_entry(client, &mc->dead_head, list) {
			offs += sprintf(msg->data + offs, "%-16s(X) \tpid:%-6d \tID:%d\n", client->name, client->pid, client->guard ? client->guard->id : -1);
		}

		struct mc_guard *guard;
		list_for_each_entry(guard, &mc->guard_head, list) {
			offs += sprintf(msg->data + offs, "%-16s(G) \tpid:%-6d \tID:%d\n", guard->name, guard->pid, guard->id);
		}

		msg->data_len = offs + 1;
	} else {
		msg->data_len = sprintf(msg->data, "Unkonwn command\n") + 1;
	}
	LOGI("command:%s, response length:%d", command, msg->data_len);
	return 0;
}
static int mc_probe(struct ipc_timing *timing)
{
	struct mc_struct *mc = timing->arg;
	struct mc_probem probe; 
	probe.time = mc_gettime();
	
	LOGH("Probe Message: %ld.", probe.time);
	mc->probe_time = probe.time;
	return ipc_server_publish(IPC_TO_BROADCAST, 
						MC_GUARD_MASK, MC_IND_PROBE, &probe, sizeof(probe));
}
static int mc_inspect(struct ipc_timing *timing)
{
	struct mc_struct *mc = timing->arg; 
	struct mc_task *task, *tmp;
	
	long current = mc_gettime();
	long action = 0;
	list_for_each_entry_safe_reverse(task, tmp, &mc->task_head, list) {
		//LOGI("HT time loss: %ld - %ld", current, task->expire);
		if (current >= task->expire) {	
			LOGE("HT(%s) miss from %ld to %ld.", task->client->name, task->expire, current);
			if (!(task->client->flags & BIT(task->strategy))) {
				task->client->flags |= BIT(task->strategy);
				action |= BIT(task->strategy);
				if (task->strategy == MC_STRATEGY_RESTART) {
					list_del(&task->client->list);
					list_add(&task->client->list, &mc->loss_head);
				} else if (task->strategy == MC_STRATEGY_REBOOT) {
                    LOGP("%s need to reboot\n", task->client->name);
                }
			}
		} else break;
	}
	
	struct mc_client *client, *t;
	list_for_each_entry_safe_reverse(client, t, &mc->work_head, list) {
		//LOGI("probe time loss: %ld - %ld", mc->probe_time, client->probe_expire);
		/*
		 * Check probe time, in case client's callback blocked.
		 */
		if (mc->probe_time >= client->probe_expire) {
			LOGE("Probe(%s) miss from %ld to %ld.", client->name, client->probe_expire, mc->probe_time);
			client->flags |= BIT(MC_STRATEGY_RESTART);
			list_del(&client->list);
			list_add(&client->list, &mc->loss_head);
		} else break;
	}

	list_for_each_entry(client, &mc->loss_head, list) {
		LOGI("client:%s flags:%08x.", client->name, client->flags);	
		if (!(client->flags & BIT(MC_CLIENT_F_EXIT)))
			mc_exit_client(client);
		else if (!(client->flags & BIT(MC_CLIENT_F_KILL)))
			mc_kill_client(client);
		else
			set_client_state(client, MC_CLIENT_BLOCKED);
	}

	/* Inspect if Device rebooting triggered */
	if (action & BIT(MC_STRATEGY_REBOOT)) {
        LOGP("reboot action %ld!\n", action);
		mc_reboot_system(mc, 0);
    }
	
	
	return 0;
}
static void mc_timers_init(struct mc_struct *mc)
{
	struct mc_timers *tms = mc->timers;
	
	assert(tms != NULL);
	
	/* Cyclic Timer for feeding watchdog */
	ipc_timing_init(&tms->feedwd_timing, 1, 
						mc->config->watchdog_interval, 0, mc, mc_feedwd);
	assert(ipc_timing_register(&tms->feedwd_timing) == 0);


	/* Cyclic Timer for inspecting clients */
	ipc_timing_init(&tms->patrol_timing, 1, 
						mc->config->patrol_interval, 0, mc, mc_inspect);
	assert(ipc_timing_register(&tms->patrol_timing) == 0);


	/* Cyclic Timer for probe message to clients */
	ipc_timing_init(&tms->probe_timing, 1, 
						mc->config->probe_interval, 0, mc, mc_probe);
	assert(ipc_timing_register(&tms->probe_timing) == 0);


	/* Cyclic Timer for guarding */
	ipc_timing_init(&tms->guard_timing, 0, 
						mc->config->guard_delay, 0, mc, mc_guard_delay);
	assert(ipc_timing_register(&tms->guard_timing) == 0);

	if (mc->config->auto_reboot_timeout > 0) {
		/* Cyclic Timer for auto reboot */
		ipc_timing_init(&tms->auto_reboot_timing, 0, 
							mc->config->auto_reboot_timeout, 0, mc, device_reboot);
		assert(ipc_timing_register(&tms->auto_reboot_timing) == 0);
	}
}
static int mc_init(const char *conf, struct mc_struct *mc)
{	
	memset(mc, 0, sizeof(struct mc_struct))	;
	mc->einfo  = &__mc_einfo;
	mc->ready  = &__mc_ready;
	mc->config = &__mc_config;
	mc->timers = &__mc_timers;

	mc->sync_sleep 	= &__mc_synchors[0];
	mc->sync_resume = &__mc_synchors[1];
	mc->sync_reboot = &__mc_synchors[2];
	
	mc->vote_sleep 	= &__mc_referees[0];
	mc->vote_reboot = &__mc_referees[1];
	
	memset(mc->ready, 0, sizeof(struct mc_ready));
	memset(mc->config, 0, sizeof(struct mc_config));
	memset(mc->timers, 0, sizeof(struct mc_timers));

	memset(mc->sync_sleep, 0, sizeof(struct mc_synchor));
	memset(mc->sync_resume, 0, sizeof(struct mc_synchor));
	memset(mc->sync_reboot, 0, sizeof(struct mc_synchor));
	
	memset(mc->vote_sleep, 0, sizeof(struct mc_referee));
	memset(mc->vote_reboot, 0, sizeof(struct mc_referee));
	
	INIT_LIST_HEAD(&mc->dead_head);
	INIT_LIST_HEAD(&mc->loss_head);
	INIT_LIST_HEAD(&mc->work_head);
	INIT_LIST_HEAD(&mc->detach_head);
	INIT_LIST_HEAD(&mc->task_head);
	INIT_LIST_HEAD(&mc->guard_head);

	mc_config_init(conf);
	
	if (mc_watchdog_init(mc->config->watchdog_timeout) < 0)
		return -1;
	
	mc_guard_init(&mc->guard_head);

	mc_timers_init(mc);
	
	assert(!pthread_condattr_setclock(&mc->cattr, CLOCK_MONOTONIC));
	assert(!pthread_cond_init(&mc->sync_sleep->syn_cond, &mc->cattr));
	assert(!pthread_cond_init(&mc->sync_resume->syn_cond, &mc->cattr));
	assert(!pthread_cond_init(&mc->sync_reboot->syn_cond, &mc->cattr));
	assert(!pthread_cond_init(&mc->vote_sleep->vote_cond, &mc->cattr));
	assert(!pthread_cond_init(&mc->vote_reboot->vote_cond, &mc->cattr));
	assert(!pthread_mutex_init(&mc->mutex, NULL));
	
	mc->probe_time = mc_gettime();
	LOGI("MC core init done@%p.", mc);
	return 0;
}
static int mc_ipc_connect_process(const struct ipc_server *sevr, struct mc_struct *mc)
{
	assert(sevr->clazz == IPC_CLASS_REQUESTER);
	if (ipc_server_bind(sevr, IPC_COOKIE_ASYNC, NULL) < 0) {
		LOGE("async bind error.");
		return -1;
	}
	return 0;
}
static int mc_ipc_register_process(const struct ipc_server *sevr,  struct mc_struct *mc, struct mc_reginfo *reginfo)
{
	char name[64] = {0};
	if (process_name(name, sizeof(name), sevr->identity) < 0) {
		LOGE("MC get process:%d(%s) name fail.", sevr->identity, reginfo->name);
		return -1;
	}
	
	LOGI("%s:%d register mask:%04x, MC core@%p.", name, sevr->identity, ipc_subscribed(sevr, BITS_MSK_LONG), mc);
	
	if (mc->subscribers >= MC_CLIENT_MAX) {
		LOGE("MC register overload.");
		return -1;
	}

	unsigned long supported_mask = MC_GUARD_MASK | MC_SYNC_MASK | MC_SYSTEM_MASK | MC_EXCEPTION_MASK;
	
	if (!ipc_subscribed(sevr, supported_mask) || ipc_subscribed(sevr, ~supported_mask)) {
		LOGE("MC Event Bad Mask included.");
		return -1;
	}
	char *prname = NULL;
	struct mc_client *client = NULL;
	struct mc_subscriber *subscriber = (struct mc_subscriber *)malloc(sizeof(struct mc_subscriber));

	if (!subscriber) {
		LOGE("Registrant alloc:None Memory.");
		return -1;
	}
	if (ipc_subscribed(sevr, MC_GUARD_MASK)) {
		LOGI("%s:%d registering, latch cmdline:%s.", reginfo->name, reginfo->pid, reginfo->cmdline);
		if (sevr->identity != reginfo->pid || strcmp(name, reginfo->name)) {
			LOGE("reginfo mismatch.");
			goto err;
		}
	
		client = mc_client_construct(reginfo, subscriber, mc);
		if (!client)
			goto err;
		
		subscriber->client = client;
		subscriber->cookie_type = MC_IPC_COOKIE_CLIENT;
	} else {
		prname = strdup(name);
		if (!prname)
			goto err;
		
		subscriber->name = prname;
		subscriber->cookie_type = MC_IPC_COOKIE_REGONLY;
	}

	subscriber->sevr = sevr;
	subscriber->mc	 = mc;
	subscriber->index  = mc_index_alloc(mc);
	if (ipc_server_bind(sevr, IPC_COOKIE_USER, subscriber) < 0)
		goto err;

	if (ipc_subscribed(sevr, MC_SYSTEM_MASK)) {
		pthread_mutex_lock(&mc->mutex);
		set_bit(subscriber->index, mc->full_bitmap);
		set_bit(subscriber->index, mc->sync_sleep->ack_bitmap);
		set_bit(subscriber->index, mc->sync_resume->ack_bitmap);
		set_bit(subscriber->index, mc->sync_reboot->ack_bitmap);
		set_bit(subscriber->index, mc->vote_sleep->vote_bitmap);
		set_bit(subscriber->index, mc->vote_sleep->vote_approved);
		set_bit(subscriber->index, mc->vote_reboot->vote_bitmap);
		set_bit(subscriber->index, mc->vote_reboot->vote_approved);
		pthread_mutex_unlock(&mc->mutex);
		int i;
		for (i = 0; i < MC_BITMAP_SIZE; i++)
			LOGI("full_bitmap[%d]:%lx", i, mc->full_bitmap[i]);
	}
	mc->subscribers++;
	LOGI("Register done, current subscribers:%d.", mc->subscribers);
	return 0;

err:
	if (client)
		mc_client_destroy(client, mc);
	if (prname)
		free(prname);
	if (subscriber)
		free(subscriber);
	LOGE("MC register exit error.");
	return -1;
}
static int mc_ipc_release_process(const struct ipc_server *sevr, struct mc_struct *mc, void *cookie)
{
	if (sevr->clazz == IPC_CLASS_SUBSCRIBER) {
		if (ipc_cookie_type(cookie) != IPC_COOKIE_NONE) { 
			struct mc_subscriber *subscriber = cookie;
			if (ipc_subscribed(sevr, MC_SYSTEM_MASK)) {
				pthread_mutex_lock(&mc->mutex);
				clear_bit(subscriber->index, mc->full_bitmap);
				clear_bit(subscriber->index, mc->sync_sleep->ack_bitmap);
				clear_bit(subscriber->index, mc->sync_resume->ack_bitmap);
				clear_bit(subscriber->index, mc->sync_reboot->ack_bitmap);
				clear_bit(subscriber->index, mc->vote_sleep->vote_bitmap);
				clear_bit(subscriber->index, mc->vote_sleep->vote_approved);
				clear_bit(subscriber->index, mc->vote_reboot->vote_bitmap);
				clear_bit(subscriber->index, mc->vote_reboot->vote_approved);
				if (mc->sync_sleep->flags & BIT(AF_WAIT)) {
					if (is_bitmap_full(mc->sync_sleep->ack_bitmap, mc))
						pthread_cond_broadcast(&mc->sync_sleep->syn_cond);
				}

				if (mc->sync_resume->flags & BIT(AF_WAIT)) {
					if (is_bitmap_full(mc->sync_reboot->ack_bitmap, mc))
						pthread_cond_broadcast(&mc->sync_resume->syn_cond);
				}
				
				if (mc->sync_reboot->flags & BIT(AF_WAIT)) {	
					if (is_bitmap_full(mc->sync_reboot->ack_bitmap, mc))
						pthread_cond_broadcast(&mc->sync_reboot->syn_cond);
				}

				if (mc->vote_sleep->flags & BIT(AF_WAIT)) {
					if (is_bitmap_full(mc->vote_sleep->vote_bitmap, mc))
						pthread_cond_broadcast(&mc->vote_sleep->vote_cond);
				}
				
				if (mc->vote_reboot->flags & BIT(AF_WAIT)) {
					if (is_bitmap_full(mc->vote_reboot->vote_bitmap, mc))
						pthread_cond_broadcast(&mc->vote_reboot->vote_cond);
				}
				
				pthread_mutex_unlock(&mc->mutex);
				int i;
				for (i = 0; i < MC_BITMAP_SIZE; i++) {
					LOGI("full_bitmap[%d]:%lx", i, mc->full_bitmap[i]);
				}
			}
			mc_index_free(subscriber->index, mc);
			
			if (subscriber->cookie_type == MC_IPC_COOKIE_REGONLY) {
				LOGI("%s release.", subscriber->name);
				free((void *)subscriber->name);
			}
			
			free(subscriber);
			mc->subscribers--;
		}
	}
	return 0;
}
static int mc_ipc_unregister_process(const struct ipc_server *sevr, struct mc_struct *mc, void *cookie)
{
	if (ipc_cookie_type(cookie) == MC_IPC_COOKIE_CLIENT) {
		struct mc_client *client = get_client_from_cookie(cookie);
		client->flags |= BIT(MC_CLIENT_F_UNREG);
	}
	return 0;
}

static int mc_ipc_shutdown_process(const struct ipc_server *sevr, struct mc_struct *mc, void *cookie)
{
	if (ipc_cookie_type(cookie) == MC_IPC_COOKIE_CLIENT) {
		struct mc_client *client = get_client_from_cookie(cookie);	
		assert(ipc_subscribed(sevr, MC_GUARD_MASK));
		LOGI("%s:%d shutdown, state:%d.", client->name, client->pid, client->state);
		ipc_server_publish(IPC_TO_BROADCAST, 
							MC_SYNC_MASK, 
							MC_IND_SYNC_DEAD, client->name, strlen(client->name) + 1);

		int need_restart = client->state & MC_CLIENT_DETACHED ? 0 : 1;

		if (mc->flags & (BIT(MC_F_REBOOT) | BIT(MC_F_SHUTDOWN))) {
			LOGW("Reboot in progress, stop client restart.");
			need_restart = 0;
		} 
		mc_client_destroy(client, mc);

		if (need_restart)
			mc_client_restart(client, mc);
	}
	return 0;
}
static int mc_ipc_sync_process(const struct ipc_server *sevr, struct mc_struct *mc, void *cookie, void *data)
{
	if (ipc_cookie_type(cookie) == MC_IPC_COOKIE_CLIENT) {
		struct mc_client *client = get_client_from_cookie(cookie);
		int *tid = data;
		LOGI("Receive sync from %s, task:%d.", client->name, *tid);
		assert(ipc_subscribed(client->subscriber->sevr, MC_GUARD_MASK));
		if (ipc_subscribed(client->subscriber->sevr, MC_SYNC_MASK))
			ipc_server_notify(sevr, MC_SYNC_MASK, MC_IND_SYNC_READY, mc->ready, sizeof(struct mc_ready));
	}
	return 0;
}

static int mc_ipc_handler(struct ipc_msg *msg, void *arg, void *cookie)
{	
	int ret = -1;
	switch (msg->msg_id) {
	case MC_MSG_PROBE:
		ret = mc_client_probe_msg(msg, cookie);
		break;
	case MC_MSG_READY:
		ret = mc_client_ready_msg(msg, cookie);
		break;
	case MC_MSG_EXIT:
		ret = mc_client_exit_msg(msg, cookie);
		break;
	case MC_MSG_HEARTBEAT:
		ret = mc_client_heartbeat_msg(msg, cookie);
		break;
	case MC_MSG_DETACH:
		ret = mc_client_detach_msg(msg, arg, cookie);
		break;
	case MC_MSG_EXCEPTION:
		ret = mc_client_exception_msg(msg, arg, cookie);
		break;
	case MC_MSG_SYN:
		ret = mc_client_syn_msg(msg, arg, cookie);
		break;
	case MC_MSG_ACK:
		ret = mc_client_ack_msg(msg, arg, cookie);
		break;
	case MC_MSG_APPLY:
		ret = mc_client_apply_msg(msg, arg, cookie);
		break;
	case MC_MSG_VOTE:
		ret = mc_client_vote_msg(msg, arg, cookie);
		break;
	case MC_MSG_REBOOT:
		ret = mc_client_reboot_msg(msg, arg, cookie);
		break;
	default:
		if (msg->msg_id == 12345) {
			return mc_tools_msg_process(msg, arg);
		}
		printf("Unexpected messages.");
		break;
	}
	return ret;
}

static int mc_ipc_filter(struct ipc_notify *notify, void *arg)
{
	struct mc_struct *mc = arg;
	if (notify->msg_id == MC_IND_SYS_SHUTDOWN) {
		LOGP("%s >>: system shutdown.", notify->data);
		mc->flags |= BIT(MC_F_SHUTDOWN);
		mc_kill_guard(mc);
	}
	return 0;
}

static int mc_ipc_manager(const struct ipc_server *sevr, int cmd, 
	void *data, void *arg, void *cookie)
{
	struct mc_struct *mc = arg;
	switch (cmd) {
	case IPC_CLIENT_RELEASE:
		return mc_ipc_release_process(sevr, mc, cookie);
	case IPC_CLIENT_CONNECT:
		return mc_ipc_connect_process(sevr, mc);
	case IPC_CLIENT_REGISTER:
		return mc_ipc_register_process(sevr, mc, data);
	case IPC_CLIENT_SYNC:
		return mc_ipc_sync_process(sevr, mc, cookie, data);
	case IPC_CLIENT_UNREGISTER:
		return mc_ipc_unregister_process(sevr, mc, cookie);
	case IPC_CLIENT_SHUTDOWN:
		return mc_ipc_shutdown_process(sevr, mc, cookie);
	default:
		return -1;
	}
}
static int mc_event_handler(int fd, void *arg)
{
	int event;
	struct mc_struct *mc = arg;
	do {
		ev_read:
		if (read(fd, &event, sizeof(event)) > 0)
			break;
		if (errno == EINTR)
			goto ev_read;
		return -1;
	} while (1);
	switch (event) {
	case SIGUSR1:
		break;
	case SIGUSR2:
		break;
	case SIGTERM:
		LOGP("Event: SIGTERM recvd.");
		exit(0);
		break;
	case MC_EVENT_REBOOT:
		mc->flags |= BIT(MC_F_REBOOT);
		LOGP("Event: system reboot.");
		break;
	case MC_EVENT_SHUTDOWN:
		mc->flags |= BIT(MC_F_SHUTDOWN);
		LOGP("Event: system shutdown.");
		mc_kill_guard(mc);
		break;
	default:
		break;
	}
	return 0;
}
void mc_event(int event)
{
	send(__mc_evtfd, &event, sizeof(event), MSG_DONTWAIT | MSG_NOSIGNAL);
}
static int mc_evtfd()
{
	int pipe[2];
	if (socketpair(AF_UNIX, SOCK_STREAM, 0, pipe))
		return -1;
	mc_setfds(pipe[0]);
	mc_setfds(pipe[1]);
	__mc_evtfd = pipe[1];
	signal(SIGUSR1, mc_event);
	signal(SIGUSR2, mc_event);
	signal(SIGTERM, mc_event);
	LOGI("mc event fd init done.");
	return pipe[0];
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
int main(int argc, char **argv)
{
	int c = 0;
	int is_daemonize = 1;
	const char *conf = "/app/config/mc.conf";
	for (;;) {
		c = getopt(argc, argv, "fc:");
		if (c < 0)
			break;
		switch (c) {
		case 'c':
			conf = optarg;
			break;
		case 'f':
			is_daemonize = 0;
			break;
		default:
			exit(-1);
		}
	}

	if (is_daemonize)
		daemonize();
	
	if (ipc_server_init(MC_SERVER, mc_ipc_handler) < 0)
		return -1;

	struct mc_struct *mc = &__mc_struct;
	if (mc_init(conf, mc) < 0) {
		LOGE("MC init failure.");
		goto out;
	}
	if (ipc_server_proxy(mc_evtfd(), mc_event_handler, mc) < 0)
		goto out;
	if (ipc_server_setopt(IPC_SEROPT_SET_FILTER, mc_ipc_filter) < 0)
		goto out;
	if (ipc_server_setopt(IPC_SEROPT_SET_MANAGER, mc_ipc_manager) < 0)
		goto out;
	if (ipc_server_setopt(IPC_SEROPT_SET_ARG, mc) < 0)
		goto out;
	if (ipc_server_setopt(IPC_SEROPT_ENABLE_ASYNC, NULL) < 0)
		goto out;
	
	ipc_server_run();
out:	
	LOGE("mcd exit.");
	ipc_server_exit();	
	return 0;
}

