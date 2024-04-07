#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/stat.h>

#include "ipc_server.h"
#include "broker.h"
#include "broker_define.h"

#define LOG_TAG				"broker-main"

struct broker_arg {
	int padding;
};

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
static void *brk_arg()
{
	static struct broker_arg brkargs = {0};
	return &brkargs;
}
static int brk_ipc_handler(struct ipc_msg* ipc_msg, void *arg, void *cookie)
{
	LOGI("ipc_msg->msg_id[%04x]\n", ipc_msg->msg_id);
	return 0;
}

static int brk_ipc_notify_filter(struct ipc_notify *notify, void *arg)
{
	struct broker_arg *brkarg = arg;
	LOGI("notify message:%d arg@%p", notify->msg_id, brkarg);
	return 0;
}
static int brk_ipc_client_manager(const struct ipc_server *sevr, int cmd, 
	void *data, void *arg, void *cookie)
{
	struct broker_arg *brkarg = arg;
	LOGI("cmd:%d arg@%p", cmd, brkarg);
	switch(cmd) {
	case IPC_CLIENT_RELEASE:
		return 0;
	case IPC_CLIENT_REGISTER:
		return 0;
	case IPC_CLIENT_SYNC:
		return 0;
	default:
		return -1;
	}
}
#if 1
#include "mc_client.h"

static struct ipc_timing ht_timing;
static int at_mc_heartbeat(struct ipc_timing *t)
{
	return mc_client_heartbeat();
}
static int at_mc_callback(int msg, void *data, int size, void *arg)
{
	return 0;
}

static int at_mc_register(const char *name)
{
	char buf[512];
	struct mc_reginfo *info = (struct mc_reginfo *)buf;
	info->tasks[0].interval = 10;
	info->tasks[0].strategy = MC_STRATEGY_RESTART;
	info->tasks[0].tid	   = mc_gettid();
	info->count			= 1;
	info->latch_time	= 5;
	info->pid			= getpid();
	strcpy(info->name, 	name);
	strcpy(info->cmdline, name);
	int ret = mc_client_register(MC_GUARD_MASK | MC_SYSTEM_MASK, info, at_mc_callback, NULL);
	if (!ret) {
		ipc_timing_init(&ht_timing, 1, 5, 0, NULL, at_mc_heartbeat);
		ipc_timing_register(&ht_timing);
	}
	return ret;
}

#endif
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
		
	if (ipc_server_init(IPC_BROKER, brk_ipc_handler) < 0)
		goto out;
	if (ipc_server_setopt(IPC_SEROPT_SET_FILTER,  brk_ipc_notify_filter) < 0)
		goto out;
	if (ipc_server_setopt(IPC_SEROPT_SET_MANAGER, brk_ipc_client_manager) < 0)
		goto out;
	if (ipc_server_setopt(IPC_SEROPT_SET_ARG, 	  brk_arg()) < 0)
		goto out;
	if (at_mc_register("broker") < 0)
		goto out;
	if (ipc_server_run() < 0)
		goto out;
out:
	ipc_server_exit();
	return 0;
}
