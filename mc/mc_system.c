#include <assert.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/reboot.h>
#include "mc_system.h"

#define LOG_TAG 	"system"
static int mc_synchor_wait(struct mc_struct *mc, struct mc_synchor *synchor, int indmsg, int timeout)
{
	LOGI("synchor@%p, indmsg:%d, timeout:%d.", synchor, indmsg, timeout);
	pthread_mutex_lock(&mc->mutex);
	if (!(synchor->flags & BIT(AF_WAIT))) {
		memset(synchor->ack_bitmap, 0, sizeof(synchor->ack_bitmap));
		ipc_server_publish(IPC_TO_BROADCAST, MC_SYSTEM_MASK, indmsg, NULL, 0);
	} else LOGW("synchor@%p already in progress.", synchor);
	
	struct timespec tspec;
	clock_gettime(CLOCK_MONOTONIC, &tspec);
	tspec.tv_sec += timeout;

	int err = 0;
	if (!is_bitmap_full(synchor->ack_bitmap, mc)) {
		synchor->flags |= BIT(AF_WAIT);
		err = pthread_cond_timedwait(&synchor->syn_cond, &mc->mutex, &tspec);
		synchor->flags &= ~(BIT(AF_WAIT));
		/* probably just err ETIMEDOUT */
		LOGI("Timedwait:%d", err);
	}
	LOGI("Waiting exit:%d.", err);
	if (indmsg == MC_IND_SYS_REBOOT) {
		LOGP("Reboot waiting exit:%d, ack_bitmap:%08x - full:%08x.", err, synchor->ack_bitmap[0], mc->full_bitmap[0]);
		mc_event(MC_EVENT_REBOOT);
	}
	pthread_mutex_unlock(&mc->mutex);
	return err;
}
static int mc_referee_wait(struct mc_struct *mc, struct mc_referee *referee,  int indmsg, int timeout)
{
	int err = 0;
	LOGI("referee@%p, indmsg:%d, timeout:%d.", referee, indmsg, timeout);
	pthread_mutex_lock(&mc->mutex);
	if (!(referee->flags & BIT(AF_WAIT))) {
		/* By default, we allow system sleep and device reboot */
		memcpy(referee->vote_approved, mc->full_bitmap, sizeof(mc->full_bitmap));
		memset(referee->vote_bitmap, 0, sizeof(referee->vote_bitmap));
		ipc_server_publish(IPC_TO_BROADCAST, MC_SYSTEM_MASK, indmsg, NULL, 0);
	} else LOGW("referee@%p already in progress.", referee);
	
	struct timespec tspec;
	clock_gettime(CLOCK_MONOTONIC, &tspec);
	tspec.tv_sec += timeout;
	if (!is_bitmap_full(referee->vote_bitmap, mc)) {
		referee->flags |= BIT(AF_WAIT);
		err = pthread_cond_timedwait(&referee->vote_cond, &mc->mutex, &tspec);
		referee->flags &= ~(BIT(AF_WAIT));
		/* probably just err ETIMEDOUT */
		LOGI("Timedwait:%d", err);
	}
		
	LOGI("Waiting exit:%d", err);
	err = is_bitmap_full(referee->vote_approved, mc) ? MC_APPLY_R_APPROVED : MC_APPLY_R_UNAPPROVED;
	pthread_mutex_unlock(&mc->mutex);
	return err;
}

static void mc_reboot(struct mc_struct *mc)
{
	const char *desc = mc->flags & BIT(MC_F_POWER_OFF) ? "mcd-pwroff" : "mcd-reboot";
	mc_event(MC_EVENT_SHUTDOWN);
	LOGI("Device rebooting(%s)...", desc);
	ipc_server_publish(IPC_TO_BROADCAST, 
						MC_SYSTEM_MASK, MC_IND_SYS_SHUTDOWN, desc, strlen(desc) + 1);
	sync();
	usleep(1000 * 1000);
	if (mc->flags & BIT(MC_F_POWER_OFF))
		reboot(RB_POWER_OFF);
	else
		reboot(RB_AUTOBOOT);

	/* Never reach here */
	LOGP("Device reboot failure:%d", errno);
}
static void *mc_reboot_entry(void *arg)
{
	struct mc_struct *mc = arg;
	mc_synchor_wait(mc, mc->sync_reboot, MC_IND_SYS_REBOOT, mc->config->delay_reboot);
	mc_reboot(mc);
	return NULL;
}
static void *mc_reboot_poll(void *arg)
{
	int ret;
	struct mc_struct *mc = arg;
	do {
		ret = mc_referee_wait(mc, mc->vote_reboot, MC_IND_SYS_VOTE_REBOOT, mc->config->delay_reboot);
	} while (ret == MC_APPLY_R_UNAPPROVED);
	
	return mc_reboot_entry(arg);
}
void mc_reboot_system(struct mc_struct *mc, int force)
{
	if (mc_thread_create(force ? mc_reboot_entry : mc_reboot_poll, mc)) {
		LOGP("Create task error, reboot directly.");
		mc_reboot(mc);
	}
}
void mc_pwroff_system(struct mc_struct *mc, int force)
{
	mc->flags |= BIT(MC_F_POWER_OFF);
	if (mc_thread_create(force ? mc_reboot_entry : mc_reboot_poll, mc)) {
		LOGP("Create task error, reboot directly.");
		mc_reboot(mc);
	}
}
static void sync_waiting(struct ipc_msg *msg, void *arg)
{
	int err = 0;
	struct mc_struct *mc = arg;
	struct mc_syn *syn = (struct mc_syn *)msg->data;
	LOGI("Sync(%d) proc, from:%s, tmo:%d.", syn->synid, syn->sponsor, syn->timeout);
	switch (syn->synid) {
	case MC_SYN_SLEEP:
		err = mc_synchor_wait(mc, mc->sync_sleep, MC_IND_SYS_SLEEP, syn->timeout);
		break;
	case MC_SYN_RESUME:
		err = mc_synchor_wait(mc, mc->sync_resume, MC_IND_SYS_WAKEUP, syn->timeout);
		break;
	case MC_SYN_REBOOT:
		err = mc_synchor_wait(mc, mc->sync_reboot, MC_IND_SYS_REBOOT, syn->timeout);
		break;
	default:
		LOGE("Invalid syn msg.");
		return;
	}
	struct mc_synrsp *rsp = (struct mc_synrsp *)msg->data;
	rsp->code = err ? MC_SYN_TMO : MC_SYN_DONE;
	msg->data_len = sizeof(*rsp);
}
int mc_client_syn_msg(struct ipc_msg *msg, struct mc_struct *mc, void *cookie)
{
	if (ipc_class(msg) != IPC_CLASS_REQUESTER) 
		return -1;
	
	/*
	 * In case rogue application 
	 */
	if (ipc_cookie_type(cookie) != IPC_COOKIE_ASYNC) {
		LOGW("rogue request.");
		return -1;
	}
	struct mc_syn 	 *syn = (struct mc_syn 	  *)msg->data;
	struct mc_synrsp *rsp = (struct mc_synrsp *)msg->data;
	LOGI("Sync(%d) proc, from:%s, tmo:%d.", syn->synid, syn->sponsor, syn->timeout);
	if (ipc_async_execute(cookie, msg, sizeof(struct mc_synrsp), sync_waiting, NULL, mc) < 0) {
		LOGE("syn from:%s error.", syn->sponsor);
		goto err;
	}
	return 0;
err:
	msg->data_len = sizeof(struct mc_synrsp);
	rsp->code = MC_SYN_ERR;
	return 0;
}

int mc_client_ack_msg(struct ipc_msg *msg, struct mc_struct *mc, void *cookie)
{
	if (ipc_class(msg) != IPC_CLASS_SUBSCRIBER)
		return -1;

	if (ipc_cookie_type(cookie) != MC_IPC_COOKIE_CLIENT) {
		LOGE("Invalid cookie.");
		return -1;
	}
	struct mc_subscriber *subscriber = cookie;

	struct mc_ack *ack = (struct mc_ack *)msg->data;
	
	if (!ipc_subscribed(subscriber->sevr, MC_SYSTEM_MASK))
		return -1;

	LOGI("Ack(%d) from %s:%d, subscriber:%d", ack->synid, ack->sponsor, msg->from, subscriber->index);
	struct mc_synchor *synchor;
	switch (ack->synid) {
	case MC_SYN_SLEEP:
		synchor = mc->sync_sleep;
		break;
	case MC_SYN_RESUME:
		synchor = mc->sync_resume;
		break;
	case MC_SYN_REBOOT:
		synchor = mc->sync_reboot;
		break;
	default:
		LOGE("Invalid syn msg.");
		return -1;
	}
	pthread_mutex_lock(&mc->mutex);	
	set_bit(subscriber->index, synchor->ack_bitmap);
	LOGI("ack_bitmap:%08x - full:%08x.", synchor->ack_bitmap[0], mc->full_bitmap[0]);
	if (is_bitmap_full(synchor->ack_bitmap, mc)) {
		if (synchor->flags & BIT(AF_WAIT))
			pthread_cond_broadcast(&synchor->syn_cond);
	}
	pthread_mutex_unlock(&mc->mutex);
	
	return 0;
}
static void apply_waiting(struct ipc_msg *msg, void *arg)
{
	int ret = 0;
	struct mc_struct *mc = arg;
	struct mc_apply *apply = (struct mc_apply *)msg->data;
	switch (apply->applyid) {
	case MC_APPLY_SLEEP:
		ret = mc_referee_wait(mc, mc->vote_sleep, MC_IND_SYS_VOTE_SLEEP, apply->timeout);
		break;
	case MC_APPLY_REBOOT:
		ret = mc_referee_wait(mc, mc->vote_reboot, MC_IND_SYS_VOTE_REBOOT,apply->timeout);
		break;
	default:
		LOGE("Invalid apply msg.");
		return;
		
	}
	struct mc_applyrsp *rsp = (struct mc_applyrsp *)msg->data;
	msg->data_len = sizeof(*rsp);
	rsp->approved = ret;
}
int mc_client_apply_msg(struct ipc_msg *msg, struct mc_struct *mc, void *cookie)
{
	if (ipc_class(msg) != IPC_CLASS_REQUESTER)
		return -1;

	/*
	 * In case rogue application 
	 */
	if (ipc_cookie_type(cookie) != IPC_COOKIE_ASYNC) {
		LOGW("rogue request.");
		return -1;
	}
		
	struct mc_apply	 *apply = (struct mc_apply    *)msg->data;
	struct mc_applyrsp *rsp = (struct mc_applyrsp *)msg->data;
	
	LOGI("Apply(%d) msg from:%s, tmo:%d.", apply->applyid, apply->sponsor, apply->timeout);
	if (ipc_async_execute(cookie, msg, sizeof(struct mc_applyrsp), apply_waiting, NULL, mc) < 0) {
		LOGE("Apply(%d) from:%s error.", apply->applyid, apply->sponsor);	
		goto err;
		
	}
	return 0;
err:
	msg->data_len = sizeof(struct mc_applyrsp);
	rsp->approved = MC_APPLY_R_ERR;
	return 0;
}
int mc_client_vote_msg(struct ipc_msg *msg, struct mc_struct *mc, void *cookie)
{
	if (ipc_class(msg) != IPC_CLASS_SUBSCRIBER)
		return -1;

	if (ipc_cookie_type(cookie) != MC_IPC_COOKIE_CLIENT) {
		LOGE("Invalid cookie.");
		return -1;
	}
	struct mc_subscriber *subscriber = cookie;
	struct mc_vote *vote = (struct mc_vote *)msg->data;
	
	if (!ipc_subscribed(subscriber->sevr, MC_SYSTEM_MASK))
		return -1;
	
	LOGI("Vote sleep from %s:%d, approved:%d, subscriber:%d", vote->sponsor, msg->from, vote->approved, subscriber->index);
	struct mc_referee *referee;

	switch (vote->applyid) {
	case MC_APPLY_SLEEP:
		referee = mc->vote_sleep;
		break;
	case MC_APPLY_REBOOT:
		referee = mc->vote_reboot;
		break;
	default:
		LOGE("Invalid apply msg.");
		return -1;
		
	}
	pthread_mutex_lock(&mc->mutex); 
	set_bit(subscriber->index, referee->vote_bitmap);
	LOGI("vote_bitmap:%08x - full:%08x.", referee->vote_bitmap[0], mc->full_bitmap[0]);
	if (!vote->approved)
		clear_bit(subscriber->index, referee->vote_approved); 
	
	if (is_bitmap_full(referee->vote_bitmap, mc)) {
		if (referee->flags & BIT(AF_WAIT))
			pthread_cond_broadcast(&referee->vote_cond);
	}
	pthread_mutex_unlock(&mc->mutex);
	
	return 0;
}
static void reboot_waiting(struct ipc_msg *msg, void *arg)
{
	struct mc_struct *mc = arg;
	struct mc_syn *syn = (struct mc_syn *)msg->data;
	LOGP("Reboot sync(%d) proc, from:%s, tmo:%d.", syn->synid, syn->sponsor, syn->timeout);
	mc_synchor_wait(mc, mc->sync_reboot, MC_IND_SYS_REBOOT, syn->timeout);
	mc_reboot(mc);
}

int mc_client_reboot_msg(struct ipc_msg *msg, struct mc_struct *mc, void *cookie)
{
	if (ipc_class(msg) != IPC_CLASS_REQUESTER)
		return -1;

	/*
	 * In case rogue application 
	 */
	if (ipc_cookie_type(cookie) != IPC_COOKIE_ASYNC) {
		LOGW("rogue request.");
		return -1;
	}
	
	struct mc_syn *syn = (struct mc_syn *)msg->data;
	struct mc_val *val = (struct mc_val *)syn->data;
	if (val->value)
		mc->flags |= BIT(MC_F_POWER_OFF);
	if (ipc_async_execute(cookie, msg, sizeof(struct mc_synrsp), reboot_waiting, NULL, mc) < 0) {
		LOGE("syn from:%s error.", syn->sponsor);
		goto err;
	}
	return 0;
err:
	msg->data_len = sizeof(struct mc_synrsp);
	pthread_mutex_unlock(&mc->mutex);
	mc_reboot_system(mc, 1);
	return 0;
}
