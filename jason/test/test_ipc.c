#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>
#include <errno.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include "ipc_client.h"
#include "ipc_server.h"
#include "client.h"
static int sig_pipe = 0;
static int indication_msg_callback(int msg_id, void *data, int size, void *arg)
{
	printf("--->ind msg coming : %04d: %s\n", msg_id, (char *)data);
	return 0;
}
static int ipcServerHandler(struct ipc_msg* msg, void *arg, void *cookie)
{
	printf("ipc_msg->msg_id[%04x], data:%s\n", msg->msg_id, msg->data);
	msg->data_len = sprintf(msg->data, "%s", "<-------------reply");
	msg->data_len++;
	return 0;
}
static int ipcClientManager( struct ipc_server *cli, int cmd, void *data, void *arg, void *cookie)
{
	switch (cmd) {
	case IPC_CLIENT_RELEASE:
		return 0;
	case IPC_CLIENT_REGISTER:
		if (ipc_subscribed(cli, 1))
			ipc_server_notify(cli, 1, 555, "welcome1", sizeof("welcome1"));
		return 0;
	case IPC_CLIENT_SYNC:
		if (ipc_subscribed(cli, 1))
			ipc_server_notify(cli, 1, 666, "welcome2", sizeof("welcome2"));
		return 0;
	default:
		return -1;
	}
}
#define test_seq1	"if the server as broker, server can call this function to notify a client"

#define test_seq2	"nice to meet you,server can call this function to notify a client"

#define test_seq3	"nice to meet you"
#define test_seq4	"hello world!"
#define test_seq5	"This is the messages from server pushing, if you received, please reply me with a string key word."


static void *  monitor_task(void *arg)
{
	
	while(1) {
		ipc_server_publish(IPC_TO_BROADCAST, 0x08, 123, test_seq5, sizeof(test_seq5));
		usleep(1000*1000);
	}
	return NULL;
}
static void signal_handler(int sig)
{
	send(sig_pipe, &sig, sizeof(sig), MSG_DONTWAIT | MSG_NOSIGNAL);
}
static int sig_proxy_handler(int fd, void *arg)
{
	int sig;
	do {
		ev_read:
		if (read(fd, &sig, sizeof(sig)) > 0)
			break;
		if (errno == EINTR)
			goto ev_read;
		return -1;
	} while (1);
	switch (sig) {
		case SIGALRM:
			//printf("Alrm... \n");
			alarm(1);
			break;
		default:
			break;
	}
	return 0;
}
int sig_proxy_init()
{
	int pipe[2];
	if (socketpair(AF_UNIX, SOCK_STREAM, 0, pipe))
		return -1;
	sig_pipe = pipe[1];
	signal(SIGALRM, signal_handler);
	//alarm(1);
	printf("event proxy init success. \n");
	return pipe[0];
}
static int timing_handler(struct ipc_timing *t)
{
	static time_t ti = 0;
	if (!ti)
		ti = time(NULL);
	printf("Timing:%ld\n", time(NULL) - ti);
	return 0;
}
int test_entry_for_ipc(int argc, char **argv)
{
	char buf[1024] = {0};
	unsigned long mask;
	if (!strcmp(argv[0], "server")) {

		//signal(SIGCHLD, SIG_IGN);
		pthread_t task_pid;
		struct ipc_timing test_timing = ipc_timing_initializer(test_timing,1,6,0,NULL,timing_handler);
		if (ipc_server_init(IPC_SERVER_TEST, ipcServerHandler) < 0)
		{
			printf("server init error\n");
			exit(-1);
		}
		ipc_server_setopt(IPC_SEROPT_SET_MANAGER, ipcClientManager);
	
		pthread_create(&task_pid, NULL, monitor_task, NULL);
		int fd = sig_proxy_init();
		ipc_server_proxy(fd, sig_proxy_handler, NULL);
		ipc_timing_register(&test_timing);
		
		printf("Run ---- \n");
		if (ipc_server_run() < 0) {
			printf("run error\n");
			exit(-1);
		}

		ipc_server_exit();
	} else if (!strcmp(argv[0], "client")) {
		mask = strtoul(argv[1], NULL, 10);
		struct ipc_subscriber *subscriber = ipc_subscriber_register(IPC_SERVER_TEST, mask, NULL, 0, indication_msg_callback, NULL);

		if (subscriber == NULL) {
			printf("init error\n");
			exit(-1);
		}
		while(1) {
			sleep(5);
			//client_sendto_server_easy(IPC_SERVER_TEST, 1003, test_seq2, sizeof(test_seq2), buf, 256);	
		}

	} else if (!strcmp(argv[0], "publisher")) {
		mask = strtoul(argv[1], NULL, 10);

		while (1) {
			struct ipc_client *client = NULL;
			client = ipc_client_create(IPC_SERVER_TEST);
			if (client) {
				ipc_client_publish(client, IPC_TO_BROADCAST, mask,  getpid(), test_seq1, sizeof(test_seq1), 3);
				//ipc_client_publish(client, IPC_TO_BROADCAST, mask,  getpid(), test_seq2, sizeof(test_seq2), 3);
				//ipc_client_publish(client, IPC_TO_BROADCAST, mask,  getpid(), test_seq3, sizeof(test_seq3), 3);
				ipc_client_publish(client, IPC_TO_BROADCAST, mask,  getpid(), test_seq4, sizeof(test_seq4), 3);
				buf[0] = '\0';
				client_sendto_server(client, 0x102, "---->Hard", sizeof("---->Hard"), buf, sizeof(buf));
				printf("%s\n", buf);
				ipc_client_destroy(client);
			}
			sleep(5);
		}
	} else {
		printf("Unknown test command:%s", argv[0]);
	}
	return 0;
}

