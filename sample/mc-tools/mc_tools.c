#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include "ipc_client.h"
#include "mc_client.h"

#define MC_TOOLS_NAME	"mc_tools"
static int mct_test_restart()
{
	printf("Enter restart test\n");
	return sleep(10);
}
static int mct_test_callback(int msg, void *data, int size, void *arg)
{
	printf("MC Ind Message:%d.\n", msg);
	struct mc_evtval *evt = (struct mc_evtval *)data;
	switch (msg) {
	/* Below msg covered by MC_SYSTEM_MASK */
	case MC_IND_SYS_REBOOT:
		mc_client_reboot_ack(MC_IDENTITY_DUMMY);
		break;
	case MC_IND_SYS_RSTMODEM:
		printf("MC_IND_SYS_RSTMODEM.\n");
		break;
	case MC_IND_SYS_SLEEP:
		mc_client_sleep_ack(MC_IDENTITY_DUMMY);
		break;
	case MC_IND_SYS_WAKEUP:
		mc_client_resume_ack(MC_IDENTITY_DUMMY);
		break;
	case MC_IND_SYS_NETWORK:
		printf("MC_IND_SYS_NETWORK - %d\n", evt->value);
		break;
	case MC_IND_SYS_VOTE_SLEEP:
		mc_client_vote_sleep(MC_IDENTITY_DUMMY, 1);
		break;
	case MC_IND_SYS_VOTE_REBOOT:
		mc_client_vote_reboot(MC_IDENTITY_DUMMY, 1);
		break;
	}
	return 0;
}
static int mct_test(unsigned long mask)
{
	char buf[512];
	struct mc_reginfo *info = (struct mc_reginfo *)buf;
	info->tasks[0].interval = 10;
	info->tasks[0].strategy = MC_STRATEGY_RESTART;
	info->tasks[0].tid	   = mc_gettid();
	info->count			= 1;
	info->latch_time	= 5;
	info->pid			= getpid();
	strcpy(info->name, 	  MC_TOOLS_NAME);
	strcpy(info->cmdline, MC_TOOLS_NAME" restarting");
	if (mc_client_register(mask, info, mct_test_callback, NULL) < 0) {
		printf("Error exit!");
		return -1;
	}

	char input[512];
	do {
		memset(input, 0, sizeof(input));
		printf("Input 0: execute mc_client_detach().\n"
			   "Input 1: execute mc_client_heartbeat().\n"
			   "Input 2: execute mc_client_sleep_syn().\n"
			   "Input 3: execute mc_client_resume_syn().\n"
			   "Input 4: execute mc_client_reboot_syn().\n"
			   "Input 5: execute mc_client_apply_sleep().\n"
			   "Input 6: execute mc_client_apply_reboot().\n"
			   "Input 'exit' to quit.\n");
	    scanf("%s", input);
		if (!strcmp(input, "exit"))
			break;

		int c = atoi(input);

		switch (c) {
		case 0:
			mc_client_detach(MC_TOOLS_NAME, MC_IDENTITY_DUMMY, 1, 0);
			break;
		case 1:
			mc_client_heartbeat();
			break;
		case 2:
			mc_client_sleep_syn(5);
			break;
		case 3:
			mc_client_resume_syn(MC_RESUME_OTHER, 5);
			break;
		case 4:
			mc_client_reboot_syn(5);
			break;
		case 5:
			mc_client_apply_sleep(5);
			break;
		case 6:
			mc_client_apply_reboot(5);
			break;
		}
	} while (1);
	return 0;
}
static int mct_request(struct ipc_msg *msg, size_t size)
{	
	struct ipc_client client;
	if (ipc_client_init(MC_SERVER, &client) < 0) {
		printf("client init failure.\n");
		return -1;
	}
	if (ipc_client_request(&client, msg, size, 5) < 0) {
		printf("client request failure.\n");
		ipc_client_close(&client);
		return -1;
	}
	ipc_client_close(&client);
	return 0;
}
static int mct_detach(int state, const char *process, int exit, int identity)
{
	return	printf("%s\n", mc_client_detach(process, identity, state, exit) ? "Success" : "Failure");
}
static int mct_restart(const char *process)
{
	return printf("%s\n", mc_client_restart(process) == 1 ? "Success" : "Failure");
}

static int mct_show()
{
	char buf[8 * 1024];
	struct ipc_msg *msg = (struct ipc_msg *)buf;
	msg->flags = IPC_FLAG_REPLY;
	msg->msg_id = 12345;
	msg->data_len = sprintf(msg->data, "show") + 1;
	if (mct_request(msg, sizeof(buf)) < 0) {
		printf("Failure\n");
		return -1;
	}
	msg->data[msg->data_len] = '\0';
	printf("%s\n", msg->data);
	return 0;
}
static void usage()
{
	printf("\nUsage: mc_tools [parameters]:\n"
		"    show                  -- show all registered clients\n"
		"    test [mask]           -- enter test mode, test mc client APIs\n"
		"    restart [name]        -- restart process\n"
		"    detach [name] <exit>  -- detach process\n"
		"    attach [name] <exit>  -- attach process\n");
}

int main(int argc, char **argv)
{
	if (argc < 2) {
		goto out;
	}

	if (!strcmp(argv[1], "show"))
		return mct_show();

	if (!strcmp(argv[1], "restarting"))
		return mct_test_restart();
	
	if (argc < 3) {
		goto out;
	}
	
	if (!strcmp(argv[1], "test"))
		return mct_test(strtoul(argv[2], NULL, 16));

	if (!strcmp(argv[1], "restart"))
		return mct_restart(argv[2]);
	
	if (!strcmp(argv[1], "detach"))
		return mct_detach(1, argv[2], 
				argc > 3 ? atoi(argv[3]) : 0, 
				argc > 4 ? atoi(argv[4]) : MC_IDENTITY_DUMMY);
	
	if (!strcmp(argv[1], "attach"))
		return mct_detach(0, argv[2], 
				argc > 3 ? atoi(argv[3]) : 0, 
				argc > 4 ? atoi(argv[4]) : MC_IDENTITY_DUMMY);
out:	
	usage();
	return 0;
}
