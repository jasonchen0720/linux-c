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
#include <sys/wait.h>
#include "mc_guard.h"
#include "mc_system.h"
#include "mc_watchdog.h"
#include "mc_exception.h"

#include "generic_proc.h"

#define __LOG_TAG 	"core"
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
static void mc_kill_guard(struct mc_struct *mc, int sig)
{
	struct mc_guard *guard;
	list_for_each_entry(guard, &mc->guard_head, list) {
		if (mc_process_validate(guard->name, guard->pid)) {
			LOGP("system shutdown, Kill(-%d) %s", sig, guard->name);
			kill(guard->pid, sig);
		}
	}
}
static int mc_kill_client(struct mc_client *client, int sig)
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
		LOGW("Kill(-%d) %s %d: is not target.", sig, client->name, client->pid);
		goto out;
	} 		
	if (kill(client->pid, sig)) {
		LOGE("SIG(-%d)to %s failed.", sig, client->name);
		return -1;
	}
	LOGI("%s pid:%d killed(-%d).", client->name, client->pid, sig);
out:
	client->flags |= BIT(MC_CLIENT_F_KILL);
	return 0;
}
static int mc_exit_client(struct mc_client *client)
{
	LOGI("Exit notify to client: %s, state: %d.", client->name, client->state);
	if (client->state & MC_CLIENT_RUNNING) {
		assert(client->subscriber);
		if (ipc_server_notify(client->subscriber->sevr, MC_GUARD_MASK, MC_IND_EXIT, NULL, 0) == 0)
			client->flags |= BIT(MC_CLIENT_F_EXIT);
		else LOGE("Exit notify error.");
	}
	return 0;
}
static int device_reboot(struct ipc_timing *timing)
{
	struct mc_struct *mc = timing->arg;
	LOGP("Reboot timer expired.");
	mc_reboot_system(mc, 0, 0);
	return 0;
}

static int client_restart_review(struct ipc_timing *timing)
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
static int client_restart(struct mc_client *client)
{
	pid_t pid = mc_process_restart(client->name, client->cmdline);
	LOGI("Restarting client:%s state:%d pid:%d", client->name, client->state, pid);
	if (pid < 0)
		return -1;
	/*
	 * We don't set client->pid here as the peer will carry pid while doing register.
	 */
	set_client_state(client, MC_CLIENT_STARTING);
	ipc_server_publish(IPC_TO_BROADCAST, MC_SYNC_MASK, MC_IND_SYNC_RESTART, client->name, strlen(client->name) + 1);
	ipc_timing_init(&client->restart_timing, 1, client->latch_time, 0, client, client_restart_review);
	ipc_timing_register(&client->restart_timing);
	return 0;
}
static int client_restart_delay(struct ipc_timing *timing)
{
	return client_restart((struct mc_client *)timing->arg);
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
static void mcd_client_active(struct mc_client *client, struct mc_struct *mc, long probe_time)
{
	client->probe_expire = probe_time + mc->config->loss_max * mc->config->probe_interval;
	
	struct mc_client *client_tmp;
	list_del(&client->list);
	
	list_for_each_entry(client_tmp, &mc->client_runningq, list) {
		//LOGI("Probe expire %s, interval:%d, delta:%ld - %ld", client->name, mc->config->probe_interval, client->probe_expire, client_tmp->probe_expire);
		if (client->probe_expire >= client_tmp->probe_expire)
			break;
	}

	list_add_tail(&client->list, &client_tmp->list);
	//list_for_each_entry(client_tmp, &mc->client_runningq, list) {LOGI("Probe list %s:%ld", client_tmp->name, client_tmp->probe_expire);}
}
static int mcd_client_restart(struct mc_client *client, int delay)
{	
	long mature_time = client->birth_time + client->latch_time;
	if (mc_gettime() < mature_time) {
		ipc_timing_init(&client->restart_timing, 0, delay, 0, client, client_restart_delay);
		ipc_timing_register(&client->restart_timing);	
		set_client_state(client, MC_CLIENT_PRESTART);
		LOGI("%s will restart %ds later.", client->name, delay);
	} else 
		client_restart(client);
	return 0;
}
static struct mc_client *mcd_client_construct(
	struct mc_reginfo *reginfo, 
	struct mc_subscriber *subscriber,
	struct mc_struct *mc)
{
	int i;
	struct mc_client *client = NULL;
	
	list_for_each_entry(client, &mc->client_deadq, list) {
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
	client->birth_time = mc_gettime();
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
	mcd_client_active(client, mc, mc->probe_time);
	set_client_state(client, MC_CLIENT_RUNNING);	
	LOGI("Alloc Client done.");
	return client;
}
static void mcd_client_destroy(struct mc_client *client, struct mc_struct *mc)
{
	int i;
	LOGI("Client destroy, state:%d.", client->state);	
	if (client->flags & BIT(MC_CLIENT_F_UNREG)) {
		/* 
		 * Delete from mc->client_lossq or client_runningq or client_detachdq 
		 */
		list_del(&client->list);
		for (i = 0; i < client->count; i++)
			list_del(&client->tasks[i].list);
		if (client->guard) {
			clear_bit(client->guard->id, mc->ready->bitmap);
			client->guard->pid = client->pid;
			client->guard->state = client->state & MC_CLIENT_DETACHED ? MC_GUARD_STATE_DETACH : MC_GUARD_STATE_NEW;
			list_add_tail(&client->guard->list, &mc->guard_head);
		}
		free(client->tasks);
		free(client);
	} else {
		for (i = 0; i < client->count; i++)
			list_del_init(&client->tasks[i].list);
		if (client->guard)
			clear_bit(client->guard->id, mc->ready->bitmap);
		client->flags 		= 0;	
		client->pid 		= 0;
		client->subscriber	= NULL;
		if (client->state & MC_CLIENT_DETACHED) {
			LOGW("Detached client:%s dead.", client->name);
			/*
			 * Need to keep 'detached' state
			 */
			set_client_state(client, MC_CLIENT_DEAD | MC_CLIENT_DETACHED);
		} else {
			set_client_state(client, MC_CLIENT_DEAD);
			/*
			 * Move from mc->client_lossq or client_runningq to client_deadq 
			 */
			list_move(&client->list, &mc->client_deadq);
		}
	}
}
int mcd_client_heartbeat_msg(struct ipc_msg *msg, void *cookie)
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
int mcd_client_probe_msg(struct ipc_msg *msg, void *cookie)
{
	if (ipc_class(msg) == IPC_CLASS_SUBSCRIBER && 
		ipc_cookie_type(cookie) == MC_IPC_COOKIE_CLIENT) {
		struct mc_client *client = get_client_from_cookie(cookie);
		assert(ipc_subscribed(client->subscriber->sevr, MC_GUARD_MASK));
		struct mc_probe *probe = (struct mc_probe *)msg->data;
		if (client->state & MC_CLIENT_DETACHED) {
			LOGH("Probe ack:%ld from detached client:%s", probe->time, client->name);
			return 0;
		}
		mcd_client_active(client, client->subscriber->mc, probe->time);
		LOGH("Probe ack:%ld from client:%s", probe->time, client->name);
	}
	return 0;
}
int mcd_client_ready_msg(struct ipc_msg *msg, void *cookie)
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
int mcd_client_exit_msg(struct ipc_msg *msg, void *cookie)
{
	if (ipc_cookie_type(cookie) == MC_IPC_COOKIE_CLIENT) {
		struct mc_client *client = get_client_from_cookie(cookie);
		LOGI("Client:%s exit.", client->name);
	}
	return 0;
}
static int mcd_client_attach(const struct mc_detach *det, struct mc_struct *mc)
{
	struct mc_client *client = NULL;
	list_for_each_entry(client, &mc->client_detachdq, list) {
		if (!strcmp(client->name, det->name)) {
			if (client->state & MC_CLIENT_RUNNING) {
				int i;
				for (i = 0; i < client->count; i++)
					mc_task_active(&client->tasks[i], mc);
				mcd_client_active(client, mc, mc->probe_time);
				client->state &= ~MC_CLIENT_DETACHED;
				LOGI("client running, detach state cleared.");
				list_move(&client->list, &mc->client_runningq);
			} else if (client->state & MC_CLIENT_DEAD) {
				client->state &= ~MC_CLIENT_DETACHED;
				list_move(&client->list, &mc->client_deadq);
				mcd_client_restart(client, mc->config->delay_restart);
				LOGI("client dead, restart directly.");
			} else {
				LOGE("client unexpected state: %d.", client->state);
				return 0;
			}
			return 1;
		}
	}
	
	struct mc_guard *guard;
	list_for_each_entry(guard, &mc->guard_head, list) {
		if (guard->state == MC_GUARD_STATE_DETACH && !strcmp(guard->name, det->name)) {
			guard->state = MC_GUARD_STATE_NEW;
			return 1;
		}
	}
	LOGW("%s not found, attach failure.", det->name);
	return 0;
}

static int mcd_client_detach(const struct mc_detach *det, struct mc_struct *mc)
{
	struct mc_client *client = NULL;
	
	list_for_each_entry(client, &mc->client_runningq, list) {
		if (!strcmp(client->name, det->name)) {
			int i;
			for (i = 0; i < client->count; i++)
				list_del_init(&client->tasks[i].list);
			
			list_move(&client->list, &mc->client_detachdq);
			client->state |= MC_CLIENT_DETACHED;
			if (det->exit && (client->flags & BIT(MC_CLIENT_F_EXIT)) == 0) {
				mc_exit_client(client);
			}
			LOGI("Detach %s successfully.", det->name);
			return 1;
		}
	}
	
	struct mc_guard *guard = NULL;
	list_for_each_entry(guard, &mc->guard_head, list) {
		if (guard->state != MC_GUARD_STATE_DETACH && !strcmp(guard->name, det->name)) {
			LOGI("%s detached from guard list, guard ID: %d.", det->name, guard->id);
			guard->state = MC_GUARD_STATE_DETACH;
			if (det->exit) {
				kill(guard->pid, SIGKILL);
			}
			return 1;
		}
	}
	LOGW("%s not found, detach failure.", det->name);
	return 0;
}
int mcd_client_restart_msg(struct ipc_msg *msg,  void *arg, void *cookie)
{
	int found = 1;
	struct mc_struct *mc = arg;
	const char *name = msg->data;
	struct mc_client *client = NULL;
	list_for_each_entry(client, &mc->client_runningq, list) {
		if (!strcmp(client->name, name)) {
			/* Only exit the client, mc will start it again */
			mc_exit_client(client);
			goto out;
		}
	}
	struct mc_guard *guard = NULL;
	list_for_each_entry(guard, &mc->guard_head, list) {
		if (guard->state != MC_GUARD_STATE_DETACH && !strcmp(guard->name, name)) {
			if (mc_process_validate(guard->name, guard->pid))
				kill(guard->pid, SIGTERM);
			LOGI("restart %s.", name);
			guard->pid = mc_process_restart(guard->name, guard->cmdline);
			guard->state = MC_GUARD_STATE_NEW;
			goto out;
		}
	}
	found = 0;
	LOGW("%s not found, restart failure.", name);
out:
	msg->data_len = sprintf(msg->data, "%s", found ? MC_STRING_TRUE : MC_STRING_FALSE) + 1;
	return 0;
}
int mcd_client_detach_msg(struct ipc_msg *msg,  void *arg, void *cookie)
{
	struct mc_struct *mc = arg;
	struct mc_detach *detach = (struct mc_detach *)msg->data;
	LOGI("%s Message for %s", detach->state ? "Detach" : "Attach", detach->name);
	int ret = detach->state ? mcd_client_detach(detach, mc) : mcd_client_attach(detach, mc);
	msg->data_len = sprintf(msg->data, "%s", ret ? MC_STRING_TRUE : MC_STRING_FALSE) + 1;
	return 0;
}
static int mcd_tools_general_msg(struct ipc_msg *msg,  struct mc_struct *mc)
{
	const char *command = msg->data;

	int offs = 0;
	if (!strcmp(command, "show")) {
		struct mc_client *client;
		list_for_each_entry(client, &mc->client_runningq, list) {
			offs += sprintf(msg->data + offs, "%-16s(R) \tpid:%-6d \tID:%d\n", client->name, client->pid, client->guard ? client->guard->id : -1);
		}
		list_for_each_entry(client, &mc->client_detachdq, list) {
			offs += sprintf(msg->data + offs, "%-16s(D) \tpid:%-6d \tID:%d\n", client->name, client->pid, client->guard ? client->guard->id : -1);
		}

		list_for_each_entry(client, &mc->client_lossq, list) {
			offs += sprintf(msg->data + offs, "%-16s(L) \tpid:%-6d \tID:%d\n", client->name, client->pid, client->guard ? client->guard->id : -1);
		}

		list_for_each_entry(client, &mc->client_deadq, list) {
			offs += sprintf(msg->data + offs, "%-16s(X) \tpid:%-6d \tID:%d\n", client->name, client->pid, client->guard ? client->guard->id : -1);
		}
		#define __guard_state(s) ({char __c = 'd'; if (s == MC_GUARD_STATE_NEW) __c = 'n'; else if (s == MC_GUARD_STATE_RUN)__c = 'r'; __c;})
		struct mc_guard *guard;
		list_for_each_entry(guard, &mc->guard_head, list) {
			offs += sprintf(msg->data + offs, "%-16s(%c) \tpid:%-6d \tID:%d\n", guard->name, __guard_state(guard->state), guard->pid, guard->id);
		}
		#undef __guard_state
		msg->data_len = offs + 1;
	} else {
		msg->data_len = sprintf(msg->data, "Unkonwn command\n") + 1;
	}
	return 0;
}
static int mcd_probe(struct ipc_timing *timing)
{
	struct mc_struct *mc = timing->arg;
	struct mc_probe probe; 
	probe.time = mc_gettime();
	
	LOGH("Probe Message: %ld.", probe.time);
	mc->probe_time = probe.time;
	return ipc_server_publish(IPC_TO_BROADCAST, 
						MC_GUARD_MASK, MC_IND_PROBE, &probe, sizeof(probe));
}
static int mcd_inspect(struct ipc_timing *timing)
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
					list_move(&task->client->list, &mc->client_lossq);
				} else if (task->strategy == MC_STRATEGY_REBOOT) {
                    LOGP("%s need to reboot\n", task->client->name);
                }
			}
		} else break;
	}
	
	struct mc_client *client, *t;
	list_for_each_entry_safe_reverse(client, t, &mc->client_runningq, list) {
		//LOGI("probe time loss: %ld - %ld", mc->probe_time, client->probe_expire);
		/*
		 * Check probe time, in case client's callback blocked.
		 */
		if (mc->probe_time >= client->probe_expire) {
			LOGE("Probe(%s) miss from %ld to %ld.", client->name, client->probe_expire, mc->probe_time);
			client->flags |= BIT(MC_STRATEGY_RESTART);
			list_move(&client->list, &mc->client_lossq);
		} else break;
	}

	list_for_each_entry(client, &mc->client_lossq, list) {
		LOGI("client:%s flags:%08x.", client->name, client->flags);	
		if (!(client->flags & BIT(MC_CLIENT_F_EXIT)))
			mc_exit_client(client);
		else if (!(client->flags & BIT(MC_CLIENT_F_KILL)))
			mc_kill_client(client, SIGKILL);
		else
			set_client_state(client, MC_CLIENT_BLOCKED);
	}

	/* Inspect if Device rebooting triggered */
	if (action & BIT(MC_STRATEGY_REBOOT)) {
        LOGP("reboot action %ld!\n", action);
		mc_reboot_system(mc, 0, 0);
    }
	
	
	return 0;
}
static int mc_shutdown_fn(struct ipc_timing *timing)
{
	struct mc_struct *mc = timing->arg;
	
	if (mc->flags & BIT(MC_F_SHUTDOWN)) {
		LOGP("system shutdown, mcd exit guard force.");
		mc_kill_guard(mc, SIGKILL);
	}
	if (mc->flags & BIT(MC_F_REBOOT)) {
		LOGP("reboot / poweroff failure?");
		mc_reboot(1, mc->af & BIT(MC_AF_PWROFF));
	}
	exit(0);
	return 0;
}
static void mc_shutdown(struct mc_struct *mc)
{
	mc->flags |= BIT(MC_F_SHUTDOWN);
	/*
	 * Only care guards, as all registered client will exit in @MC_IND_SYS_SHUTDOWN callback.
	 */
	mc_kill_guard(mc, SIGTERM);
	ipc_timing_unregister(&mc->timers->patrol_timing);
	ipc_timing_init(&mc->timers->patrol_timing, 1, 5, 0, mc, mc_shutdown_fn);
	ipc_timing_register(&mc->timers->patrol_timing);
}
static void mc_timers_init(struct mc_struct *mc)
{
	struct mc_timers *tms = mc->timers;
	
	assert(tms != NULL);
	if (mc->config->watchdog_interval > 0) {
		/* Cyclic Timer for feeding watchdog */
		ipc_timing_init(&tms->feedwd_timing, 1, 
							mc->config->watchdog_interval, 0, mc, mc_feedwd);
		assert(ipc_timing_register(&tms->feedwd_timing) == 0);
	}

	if (mc->config->patrol_interval > 0) {
		/* Cyclic Timer for inspecting clients */
		ipc_timing_init(&tms->patrol_timing, 1, 
							mc->config->patrol_interval, 0, mc, mcd_inspect);
		assert(ipc_timing_register(&tms->patrol_timing) == 0);
	}

	if (mc->config->probe_interval > 0) {
		/* Cyclic Timer for probe message to clients */
		ipc_timing_init(&tms->probe_timing, 1, 
							mc->config->probe_interval, 0, mc, mcd_probe);
		assert(ipc_timing_register(&tms->probe_timing) == 0);
	}

	if (mc->config->guard_delay > 0) {
		/* Cyclic Timer for guarding */
		ipc_timing_init(&tms->guard_timing, 0, 
							mc->config->guard_delay, 0, mc, mcd_guard_delay);
		assert(ipc_timing_register(&tms->guard_timing) == 0);
	}
	if (mc->config->auto_reboot_timeout > 0) {
		/* Cyclic Timer for auto reboot */
		ipc_timing_init(&tms->auto_reboot_timing, 0, 
							mc->config->auto_reboot_timeout, 0, mc, device_reboot);
		assert(ipc_timing_register(&tms->auto_reboot_timing) == 0);
	}
}

/*
 * Call this after ipc_server_init(), as internal timers depend on IPC
 */
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
	
	INIT_LIST_HEAD(&mc->client_deadq);
	INIT_LIST_HEAD(&mc->client_lossq);
	INIT_LIST_HEAD(&mc->client_runningq);
	INIT_LIST_HEAD(&mc->client_detachdq);
	INIT_LIST_HEAD(&mc->task_head);
	INIT_LIST_HEAD(&mc->guard_head);
	mc_config_init(conf);
	
	if (mc_watchdog_init(mc->config->watchdog_timeout) < 0)
		return -1;
	
	mcd_guard_init(mc);

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
	if (mc->flags & (BIT(MC_F_REBOOT) | BIT(MC_F_SHUTDOWN))) {
		LOGW("Reboot in progress, stop client register.");
		return -1;
	} 
	
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
	
		client = mcd_client_construct(reginfo, subscriber, mc);
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
		mcd_client_destroy(client, mc);
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
		mcd_client_destroy(client, mc);

		if (need_restart)
			mcd_client_restart(client, mc->config->delay_restart);
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
		ret = mcd_client_probe_msg(msg, cookie);
		break;
	case MC_MSG_READY:
		ret = mcd_client_ready_msg(msg, cookie);
		break;
	case MC_MSG_EXIT:
		ret = mcd_client_exit_msg(msg, cookie);
		break;
	case MC_MSG_HEARTBEAT:
		ret = mcd_client_heartbeat_msg(msg, cookie);
		break;
	case MC_MSG_DETACH:
		ret = mcd_client_detach_msg(msg, arg, cookie);
		break;
	case MC_MSG_EXCEPTION:
		ret = mcd_client_exception_msg(msg, arg, cookie);
		break;
	case MC_MSG_SYN:
		ret = mcd_client_syn_msg(msg, arg, cookie);
		break;
	case MC_MSG_ACK:
		ret = mcd_client_ack_msg(msg, arg, cookie);
		break;
	case MC_MSG_APPLY:
		ret = mcd_client_apply_msg(msg, arg, cookie);
		break;
	case MC_MSG_VOTE:
		ret = mcd_client_vote_msg(msg, arg, cookie);
		break;
	case MC_MSG_REBOOT:
		ret = mcd_client_reboot_msg(msg, arg, cookie);
		break;
	case MC_MSG_RESTART:
		ret = mcd_client_restart_msg(msg, arg, cookie);
		break;
	default:
		if (msg->msg_id == 12345) {
			return mcd_tools_general_msg(msg, arg);
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
		mc_shutdown(mc);
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
static void sigterm()
{
	LOGP("Event: SIGTERM recvd.");
	printf("mcd exit.\n");
	exit(0);
}
static void sigchid()
{
	int status;
    pid_t pid;
    while((pid = waitpid(-1, &status, WNOHANG)) > 0) {
		if (WIFEXITED(status)) {
			LOGI("PID:%d exit status: %d", pid, WEXITSTATUS(status));
		} else if (WIFSIGNALED(status)) {
			LOGI("PID:%d exit sig: %d", pid, WTERMSIG(status));
		} else if (WIFSTOPPED(status)) {
			LOGI("PID:%d stop sig: %d", pid, WSTOPSIG(status));
		} else if (WIFCONTINUED(status)) {
			LOGI("PID:%d SIGCONT recvd", pid);
		} else {
			LOGI("PID:%d recycle", pid);
		}
    }
	LOGI("PID[%d] errno:%d", pid, errno);
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
		LOGI("SIGUSR1 recvd.");
		mcd_guard_start(mc);
		break;
	case SIGUSR2:
		break;
	case SIGTERM:
		sigterm();
		break;
	case SIGCHLD:
		sigchid();
		break;
	case MC_EVENT_REBOOT:
		mc->flags |= BIT(MC_F_REBOOT);
		LOGP("Event: system reboot.");
		break;
	case MC_EVENT_SHUTDOWN:
		LOGP("Event: system shutdown.");
		mc_shutdown(mc);
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
	signal(SIGCHLD, mc_event);
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

