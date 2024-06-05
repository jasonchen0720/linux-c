#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/reboot.h>
#include "mc_log.h"
#include "mc_client.h"
#include "ipc_client.h"
#include "generic_proc.h"

#define __LOG_TAG 		"client"
struct mc_cltinfo
{
	char name[MC_CLIENT_NAME_LEN + 1];
	int (*fn)(int, void *, int, void *);
	void *arg;
	struct ipc_subscriber 	*subscriber;
};
static struct mc_cltinfo *__mc_client = NULL;
static pthread_mutex_t __mc_mutex = PTHREAD_MUTEX_INITIALIZER;

int mc_client_push(unsigned long mask, int msgid, const void *data, int size)
{
	int ret;
	if (__mc_client) {
		ret = ipc_subscriber_publish(__mc_client->subscriber, IPC_TO_BROADCAST, mask, msgid, data, size);
	} else {
		struct ipc_client client;
		if (ipc_client_init(MC_SERVER, &client) < 0) {
			LOGE("client init failure.");
			return -1;
		}
		ret = ipc_client_publish(&client, IPC_TO_BROADCAST, mask, msgid, data, size, 0);
		ipc_client_close(&client);
	}
	return ret == IPC_REQUEST_SUCCESS ? 0 : -1;
}

static int mc_client_request_easy(struct ipc_msg *msg, size_t size, int tmo)
{
	struct ipc_client client;
	if (ipc_client_init(MC_SERVER, &client) < 0) {
		LOGE("client init failure.");
		return -1;
	}
	if (ipc_client_request(&client, msg, size, tmo) < 0) {
		LOGE("client request failure.");
		ipc_client_close(&client);
		return -1;
	}
	ipc_client_close(&client);
	return 0;
}

static int mc_client_request(struct ipc_msg *msg, size_t size, int tmo)
{
	static struct ipc_client *client = NULL;
	pthread_mutex_lock(&__mc_mutex);

	if (client == NULL) {
		client = ipc_client_create(MC_SERVER);
		
		if (!client) {
			LOGE("Request client create failure.");
			goto err;
		}
	}

	int ret = ipc_client_request(client, msg, size, tmo);
	if (ret != IPC_REQUEST_SUCCESS) {
		ipc_client_destroy(client);
		client = NULL;
		LOGE("Request msg:%d failure:%d.", msg->msg_id, ret);
		goto err;
	}

	pthread_mutex_unlock(&__mc_mutex);
	return 0;
err:
	pthread_mutex_unlock(&__mc_mutex);
	return -1;
}
static int mc_client_probe(struct mc_cltinfo *c, struct mc_probe *probe)
{
	char buf[64];
	struct ipc_msg *msg = (struct ipc_msg *)buf;
	msg->msg_id 	= MC_MSG_PROBE;
	msg->data_len	= sizeof(struct mc_probe);
	LOGH("Probe(%s):%ld", c->name, probe->time);
	memcpy(msg->data, probe, sizeof(struct mc_probe));
	return ipc_subscriber_report(c->subscriber, msg) == IPC_REQUEST_SUCCESS ? 0 : -1;
}
static int mc_client_exit(struct mc_cltinfo *c)
{
	char buf[64];
	struct ipc_msg *msg = (struct ipc_msg *)buf;
	msg->msg_id 	= MC_MSG_EXIT;
	msg->data_len	= 0;
	LOGI("Exit:%s", c->name);
	ipc_subscriber_report(c->subscriber, msg) == IPC_REQUEST_SUCCESS ? 0 : -1;
	abort();
	exit(-1);
	return 0;
}
static int mc_client_callback(int msg_id, void *data, int size, void *arg)
{
	struct mc_cltinfo *client = arg;
	
	client->fn(msg_id, data, size, client->arg);
	switch (msg_id) {
	case MC_IND_PROBE:
		mc_client_probe(client, data);
		break;
	case MC_IND_EXIT:
		mc_client_exit(client);
		break;
	case MC_IND_SYNC_READY:
		LOGI("%s Received: %s ready.", client->name, ((struct mc_ready *)data)->name);
		break;
	case MC_IND_SYNC_DEAD:
		LOGI("%s Received: %s dead.", client->name, (const char *)data);
		break;
	case MC_IND_SYNC_RESTART:
		LOGI("%s Received: %s restart.", client->name, (const char *)data);
		break;
	case MC_IND_SYS_REBOOT:
		LOGP("system is going to reboot >> %s.", client->name);
		break;
	case MC_IND_SYS_RSTMODEM:
		break;
	case MC_IND_SYS_SLEEP:
		break;
	case MC_IND_SYS_WAKEUP:
		break;
	case MC_IND_SYS_NETWORK:
		break;
	case MC_IND_SYS_VOTE_SLEEP:
		break;
	case MC_IND_SYS_VOTE_REBOOT:
		break;
	case MC_IND_SYS_SHUTDOWN:
		LOGP("%s >>: system shutdown, %s exited.", data, client->name);
		mc_client_exit(client);
		break;
	default:
		LOGW("Unknown:%d\n", msg_id);
		break;
	}
	return 0;
}
int mc_client_register(unsigned long mask, struct mc_reginfo *info, 
	int (*fn)(int, void *, int, void *), 
	void *arg)
{
	pthread_mutex_lock(&__mc_mutex);

	if (__mc_client) {
		LOGE("Registered.");
		goto err;
	}
	struct mc_cltinfo *client = (struct mc_cltinfo *)malloc(sizeof(struct mc_cltinfo));

	if (!client) {
		LOGE("None Memory.");
		goto err;
	}
	size_t size = info ? sizeof(struct mc_reginfo) + info->count * sizeof(struct mc_tskinfo) : 0U;
	struct ipc_subscriber *subscriber = ipc_subscriber_register(MC_SERVER, 
						 mask, info, size, mc_client_callback, client);
	if (!subscriber) {
		LOGE("MC subscriber register failure.");
		free(client);
		goto err;
	}
	client->arg			= arg;
	client->fn			= fn;
	client->subscriber 	= subscriber;
	
	if (info)
		strcpy(client->name, info->name);
	else
		process_name(client->name, sizeof(client->name), getpid());
	
	__mc_client = client;
	pthread_mutex_unlock(&__mc_mutex);
	return 0;
err:
	pthread_mutex_unlock(&__mc_mutex);
	return -1;
}
/*
 * @identity: defined in enum MC_STATIC_IDENTITY, if none please use MC_IDENTITY_DUMMY instead.
 * @state: 1:detach 0:attach
 */
int mc_client_detach(const char *name, int identity, int state, int exit)
{
	char buf[128] = {0};
	struct ipc_msg 	 *msg = (struct ipc_msg   *)buf;
	struct mc_detach *det = (struct mc_detach *)msg->data;
	det->guard_id	= identity;
	det->state		= state;
	det->exit		= exit;
	snprintf(det->name, sizeof(det->name), name);
	msg->flags 	 	= IPC_FLAG_REPLY;
	msg->msg_id		= MC_MSG_DETACH;
	msg->data_len	= sizeof(struct mc_detach);
	if (mc_client_request_easy(msg, sizeof(buf), 1) < 0) {
		LOGE("client request failure.");
		return -1;
	}
	return !strcmp(msg->data, MC_STRING_TRUE);
}
int mc_client_restart(const char *name)
{
	char buf[MC_CLIENT_NAME_LEN + 17] = {0};
	struct ipc_msg *msg = (struct ipc_msg *)buf;
	size_t len = strlen(name);
	if (len > MC_CLIENT_NAME_LEN) {
		LOGE("name too long.");
		return -1;
	}
	strcpy(msg->data, name);
	msg->flags 	= IPC_FLAG_REPLY;
	msg->msg_id = MC_MSG_RESTART;
	msg->data_len = len + 1;
	if (mc_client_request_easy(msg, sizeof(buf), 5) < 0) {
		LOGE("client request failure.");
		return -1;
	}
	return !strcmp(msg->data, MC_STRING_TRUE);
}

int mc_client_ready()
{	
	struct mc_cltinfo *pc = __mc_client;
	
	assert(pc != NULL);
	
	char buf[128];
	struct ipc_msg *msg = (struct ipc_msg *)buf;

	msg->msg_id 	= MC_MSG_READY;
	/*
	 * Including the terminating null byte ('\0') 
	 */
	msg->data_len	= strlen(pc->name) + 1;
	strcpy(msg->data, pc->name);
	int ret = ipc_subscriber_report(pc->subscriber, msg);
	if (ret) {
		LOGE("errno:%d", ret);
		return -1;
	}
	return 0;
}
int mc_client_ready_test(int id, struct mc_ready *rdy)
{
	assert(id < MC_IDENTITY_MAX && id >= 0);
	return test_bit(id, rdy->bitmap);
}
int mc_client_heartbeat()
{

	struct mc_cltinfo *pc = __mc_client;
	
	assert(pc != NULL);

	char buf[64];
	struct ipc_msg *msg = (struct ipc_msg *)buf;

	msg->msg_id 	= MC_MSG_HEARTBEAT;
	msg->data_len	= sizeof(struct mc_heartbeat);
	
	struct mc_heartbeat *ht = (struct mc_heartbeat *)msg->data;
	ht->pid		 = pc->subscriber->client.identity;
	ht->tid		 = mc_gettid();
	LOGH("HT(%s):%d-%d", pc->name, ht->pid, ht->tid);
	int ret = ipc_subscriber_report(pc->subscriber, msg);
	if (ret) {
		LOGE("errno:%d subscriber @%p sk:%d", ret, pc->subscriber, pc->subscriber->client.sock);
		return -1;
	}
	return 0;
}

int mc_client_exeception(int eid, const void *einfo, unsigned int elen)
{
	struct mc_cltinfo *pc = __mc_client;
	
	assert(pc != NULL);
	assert(pc->subscriber->mask & MC_EXCEPTION_MASK);
	
	char buf[MC_EXCEPTION_R_SIZE] = {0};
	
	size_t size = sizeof(struct ipc_msg) + sizeof(struct mc_exception) + elen;

	struct ipc_msg *msg = NULL;
	
	if (size > MC_EXCEPTION_R_SIZE) {
		msg = (struct ipc_msg *)malloc(size);
		if (!msg) 
			return -1;
	} else
		msg = (struct ipc_msg *)buf;

	struct mc_exception *e = (struct mc_exception *)msg->data;

	memcpy(e->info, einfo, elen);
	e->len = elen;
	e->eid = eid;
	msg->msg_id 	= MC_MSG_EXCEPTION;
	msg->data_len	= sizeof(struct mc_exception) + elen;
	
	
	int ret = ipc_subscriber_report(pc->subscriber, msg);
	LOGI("EXECEPTION(%s):%d, ret:%d.", pc->name, e->eid, ret);
	
	if (size > MC_EXCEPTION_R_SIZE)
		free(msg);
	
	return ret ? -1 : 0;
}
int mc_client_network_syn(int status)
{

	char buf[8] = {0};
	struct mc_evtval *evt = (struct mc_evtval *)buf;
	evt->value = status;
	return mc_client_push(MC_SYSTEM_MASK, MC_IND_SYS_NETWORK, evt, sizeof(*evt));
}

static int __mc_client_syn(int synid, void *data, int data_len, int tmo)
{
	struct mc_cltinfo *pc = __mc_client;
	
	assert(pc != NULL);
	assert(pc->subscriber->mask & MC_SYSTEM_MASK);

	char buf[128] = {0};
	struct ipc_msg *m = (struct ipc_msg *)buf;
	
	struct mc_syn *syn = (struct mc_syn *)m->data;
	syn->synid	 = synid;
	syn->timeout = tmo;
	strcpy(syn->sponsor, pc->name);

	if (data) {
		syn->data_len = data_len;
		memcpy(syn->data, data, data_len);
	} else syn->data_len = 0;
	
	m->flags	= IPC_FLAG_REPLY;
	m->msg_id 	= MC_MSG_SYN;
	m->data_len	= mc_syn_len(syn);
	
	if (mc_client_request(m, sizeof(buf), tmo + 1) < 0) {
		LOGE("syn(%d) request failure.", syn->synid);
		return -1;
	}

	struct mc_synrsp *rsp = (struct mc_synrsp *)m->data;

	LOGI("Sync(%d), return code[%d].", synid, rsp->code);
	return rsp->code;
	
}
static int __mc_client_ack(int identity, int synid)
{

	struct mc_cltinfo *pc = __mc_client;
	
	assert(pc != NULL);

	char buf[128] = {0};
	struct ipc_msg *m = (struct ipc_msg *)buf;

	struct mc_ack *ack = (struct mc_ack *)m->data;
	strcpy(ack->sponsor, pc->name);
	ack->synid	  = synid;
	ack->identity = identity;
	
	m->msg_id 	= MC_MSG_ACK;
	m->data_len = sizeof(struct mc_ack);
	
	int ret = ipc_subscriber_report(pc->subscriber, m);
	if (ret) {
		LOGE("errno:%d subscriber @%p sk:%d", ret, pc->subscriber, pc->subscriber->client.sock);
		return -1;
	}
	return 0;
}
int mc_client_sleep_syn(int tmo)
{
	return __mc_client_syn(MC_SYN_SLEEP, NULL, 0, tmo);
}
/*
 * @identity: defined in enum MC_STATIC_IDENTITY, if none please use MC_IDENTITY_DUMMY instead.
 */
int mc_client_sleep_ack(int identity)
{
	return __mc_client_ack(identity, MC_SYN_SLEEP);
}

int mc_client_resume_syn(int source, int tmo)
{
	return __mc_client_syn(MC_SYN_RESUME, &source, sizeof(source), tmo);
}
/*
 * @identity: defined in enum MC_STATIC_IDENTITY, if none please use MC_IDENTITY_DUMMY instead.
 */
int mc_client_resume_ack(int identity)
{
	return __mc_client_ack(identity, MC_SYN_RESUME);
}

int mc_client_reboot_syn(int tmo)
{
	return __mc_client_syn(MC_SYN_REBOOT, NULL, 0, tmo);
}
/*
 * @identity: defined in enum MC_STATIC_IDENTITY, if none please use MC_IDENTITY_DUMMY instead.
 */
int mc_client_reboot_ack(int identity)
{
	return __mc_client_ack(identity, MC_SYN_REBOOT);
}
static int __mc_client_apply(int applyid, int tmo)
{
	struct mc_cltinfo *pc = __mc_client;
	
	assert(pc != NULL);
	assert(pc->subscriber->mask & MC_SYSTEM_MASK);

	char buf[128] = {0};
	struct ipc_msg *m = (struct ipc_msg *)buf;
	
	struct mc_apply *apply = (struct mc_apply *)m->data;
	apply->applyid		= applyid;
	apply->timeout 		= tmo;
	strcpy(apply->sponsor, pc->name);
	m->flags	= IPC_FLAG_REPLY;
	m->msg_id	= MC_MSG_APPLY;
	m->data_len = sizeof(struct mc_apply);
	
	if (mc_client_request(m, sizeof(buf), tmo + 1) < 0) {
		LOGE("Apply(%d) request failure.", applyid);
		return -1;
	}

	struct mc_applyrsp *rsp = (struct mc_applyrsp *)m->data;

	LOGI("MC Apply(%d), approved:%d.", applyid, rsp->approved);
	return rsp->approved;
}
static int __mc_client_vote(int applyid, int identity, int approved)
{
	struct mc_cltinfo *pc = __mc_client;
	
	assert(pc != NULL);

	char buf[128] = {0};
	struct ipc_msg *m = (struct ipc_msg *)buf;

	struct mc_vote *vote = (struct mc_vote *)m->data;
	strcpy(vote->sponsor, pc->name);
	vote->applyid	= applyid;
	vote->identity 	= identity;
	vote->approved 	= approved;
	m->msg_id 		= MC_MSG_VOTE;
	m->data_len  	= sizeof(struct mc_vote);
	
	int ret = ipc_subscriber_report(pc->subscriber, m);
	if (ret) {
		LOGE("errno:%d subscriber @%p sk:%d", ret, pc->subscriber, pc->subscriber->client.sock);
		return -1;
	}
	return 0;
}
int mc_client_apply_sleep(int tmo)
{
	return __mc_client_apply(MC_APPLY_SLEEP, tmo);
}

int mc_client_apply_reboot(int tmo)
{
	return __mc_client_apply(MC_APPLY_REBOOT, tmo);
}
int mc_client_vote_sleep(int identity, int approved)
{
	return __mc_client_vote(MC_APPLY_SLEEP, identity, approved);
}
int mc_client_vote_reboot(int identity, int approved)
{
	return __mc_client_vote(MC_APPLY_REBOOT, identity, approved);
}
int mc_client_reboot(int poweroff, int block, int wait_tmo, const char *reason)
{
	char buf[128] = {0};
	struct ipc_msg *m = (struct ipc_msg *)buf;
	
	struct mc_syn *syn = (struct mc_syn *)m->data;
	struct mc_val *val = (struct mc_val *)syn->data;
	syn->synid	 	= MC_SYN_REBOOT;
	syn->timeout 	= wait_tmo;
	syn->data_len 	= sizeof(*val);
	val->value 		= !!poweroff;
	process_name(syn->sponsor, sizeof(syn->sponsor), getpid());

	m->flags	= block ? IPC_FLAG_REPLY : 0;
	m->msg_id 	= MC_MSG_REBOOT;
	m->data_len	= mc_syn_len(syn);

	if (reason == NULL)
		reason = "null";
		
	LOGP("Reboot request from:%s, reason:%s.", syn->sponsor, reason);
	
	if (mc_client_request(m, sizeof(buf), wait_tmo + 30) < 0) {
		LOGP("%s Reboot request failure, reboot directly.", syn->sponsor);
		sync();
		if (poweroff)
			reboot(RB_POWER_OFF);
		else
			reboot(RB_AUTOBOOT);
		LOGP("%s Reboot returned failure.", syn->sponsor);
		return -1;
	}
	LOGP("%s Reboot request done.", syn->sponsor);
	return 0;
}
