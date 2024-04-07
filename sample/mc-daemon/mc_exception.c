#include <assert.h>
#include <unistd.h>
#include "mc_exception.h"
#include "generic_util.h"
#include "generic_bit.h"
#include "mc_log.h"


#define LOG_TAG 	"exeception"

struct mc_einfo __mc_einfo = {{0}, {0}};

int mc_exception_test(int e, struct mc_einfo *info)
{
	assert(e > MC_EXCEPTION_MIN && e < MC_EXCEPTION_MAX);
	
	if (test_bit(e, info->exception)) {
		clear_bit(e, info->exception);
		return 1;
	} 
	
	return 0;
}

int mc_exception_test_only(int e, struct mc_einfo *info)
{
	assert(e > MC_EXCEPTION_MIN && e < MC_EXCEPTION_MAX);
	return test_bit(e, info->exception);
}

int mcd_client_exception_msg(struct ipc_msg *msg, struct mc_struct *mc, void *cookie)
{
	if (ipc_class(msg) != IPC_CLASS_SUBSCRIBER) {
		LOGE("Bad exeception notification.");
		return -1;
	}

	if (ipc_cookie_type(cookie) != MC_IPC_COOKIE_CLIENT) {
		LOGE("Invalid cookie.");
		return -1;
	}
	
	struct mc_subscriber *subscriber = cookie;
		
	if (!ipc_subscribed(subscriber->sevr, MC_EXCEPTION_MASK)) {
		LOGE("Bad exeception notification.");
		return -1;
	}
	struct mc_exception *e = (struct mc_exception *)msg->data;
	LOGI("Exeception notification:%d.", e->eid);
	switch (e->eid) {
	case MC_EXCEPTION_CPU_USAGE:	/* Excessive usage of CPU */
	case MC_EXCEPTION_MEM_USAGE:	/* Excessive usage of Memory */
        LOGP("exception(%d) reboot: %s\n", e->eid, e->info);
	#if 0
       mc_reboot_system(mc, 1);
	#endif
        break;
	case MC_EXCEPTION_CPU_TEMP:		/* Temperature of cpu too high */
	case MC_EXCEPTION_DISK_USAGE:	/* Excessive usage of disk */
		set_bit(e->eid, mc->einfo->exception);
		mc->einfo->count[e->eid]++;
		break;
	case MC_EXCEPTION_NONE_CONN:	/* None connection for long time */
		set_bit(e->eid, mc->einfo->exception);
		mc->einfo->count[e->eid]++;
		LOGI("Connection exeception(%d) received:%d times.", e->eid, mc->einfo->count[e->eid]);
		break;
	default:
		break;
	}
	
	return 0;
}

