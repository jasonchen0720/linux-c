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
#include <sys/prctl.h>
#include <fcntl.h>
#include <limits.h>
#include "ipc_client.h"
#include "ipc_server.h"
#include "ipcc.h"

#define IPC_TEST			"IPC_TEST"
static int sig_pipe = 0;
static time_t g_start;
static int test_ipc_callback(int msg_id, void *data, int size, void *arg)
{
	printf("--->ind msg coming : %04d: %s\n", msg_id, (char *)data);
	return 0;
}
static int test_ipc_handler(struct ipc_msg* msg, void *arg, void *cookie)
{
	printf("ipc_msg->msg_id[%04x], data:%s\n", msg->msg_id, msg->data);
	msg->data_len = sprintf(msg->data, "%s", "<-------------reply");
	msg->data_len++;
	return 0;
}
static int test_ipc_manager( struct ipc_server *cli, int cmd, void *data, void *arg, void *cookie)
{
	switch (cmd) {
	case IPC_CLIENT_RELEASE:
	case IPC_CLIENT_CONNECT:
		break;
	case IPC_CLIENT_REGISTER:
		if (cli->clazz == IPC_CLASS_SUBSCRIBER)
			if (ipc_subscribed(cli, 1))
				ipc_server_notify(cli, 1, 555, "welcome1", sizeof("welcome1"));
		break;
	case IPC_CLIENT_SYNC:
		if (ipc_subscribed(cli, 1))
			ipc_server_notify(cli, 1, 666, "welcome2", sizeof("welcome2"));
		break;
	default:
		return -1;
	}
	return 0;
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
			printf("Alrm...%ld \n", time(NULL) - g_start);
			//alarm(3);
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
static int timing_handler2(struct ipc_timing *t)
{
	printf(" ==================================== 2 Timing:%ld\n", time(NULL) - g_start);
	//ipc_timing_register(t);
	return 0;
}
static int system_monotonic_time()
{	
	struct timeval t;
	struct timeval *tv = &t;
	struct timespec ts;
	if (clock_gettime(CLOCK_MONOTONIC, &ts) < 0)
		return -1;
	tv->tv_sec 	= ts.tv_sec;
	tv->tv_usec = ts.tv_nsec / 1000000;
	printf("timeval tv_sec: %ld, tv_usec: %ld\n", tv->tv_sec ,tv->tv_usec);
	return 0;
}
static void uapi_func(const char *event, void *data, size_t size, void *arg)
{
	printf("event: %s recvd\n", event);
	printf("data: %s\n", (char *)data);
	printf("strlen(data): %lu, size: %lu\n", strlen((char *)data), size);
	printf("arg@%p\n", arg);
}

int main(int argc, char **argv)
{
	system_monotonic_time();
	char buf[1024] = {0};
	unsigned long mask;
	if (!strcmp(argv[1], "server")) {
		signal(SIGCHLD, SIG_IGN);
		pthread_t task_pid;
		//struct ipc_timing test_timing2 = ipc_timing_initializer(test_timing2, 1, 5,0,NULL,timing_handler2);
		if (ipc_server_init(IPC_TEST, test_ipc_handler) < 0)
		{
			printf("server init error\n");
			exit(-1);
		}
		unsigned int bz = 1000;
		ipc_server_setopt(IPC_SEROPT_SET_MANAGER, test_ipc_manager);
		ipc_server_setopt(IPC_SEROPT_SET_BUF_SIZE, &bz);
		pthread_create(&task_pid, NULL, monitor_task, NULL);
		int fd = sig_proxy_init();
		ipc_server_proxy(fd, sig_proxy_handler, NULL);
		//ipc_timing_register(&test_timing2);
		printf("Run ---- \n");
		g_start = time(NULL);
		if (ipc_server_run() < 0) {
			printf("run error\n");
			exit(-1);
		}
	} else if (!strcmp(argv[1], "client")) {
		mask = strtoul(argv[2], NULL, 10);
		const char *s = IPC_TEST;
		struct ipc_subscriber *subscriber = ipc_subscriber_register(s, mask, NULL, 0, test_ipc_callback, NULL);

		if (subscriber == NULL) {
			printf("init error\n");
			exit(-1);
		}
		while(1) {
			sleep(5);
			//ipcc_request_easy(IPC_TEST, 1003, test_seq2, sizeof(test_seq2), buf, 256);	
		}

	} else if (!strcmp(argv[1], "publisher")) {
		mask = strtoul(argv[2], NULL, 10);
		const char *s = IPC_TEST;
		while (1) {
			struct ipc_client *client = NULL;
			client = ipc_client_create(s);
			if (client) {
				ipc_client_publish(client, IPC_TO_BROADCAST, mask,  getpid(), test_seq1, sizeof(test_seq1), 3);
				ipc_client_publish(client, IPC_TO_BROADCAST, mask,  getpid(), test_seq2, sizeof(test_seq2), 3);
				ipc_client_publish(client, IPC_TO_BROADCAST, mask,  getpid(), test_seq3, sizeof(test_seq3), 3);
				ipc_client_publish(client, IPC_TO_BROADCAST, mask,  getpid(), test_seq4, sizeof(test_seq4), 3);
				buf[0] = '\0';
				ipcc_request(client, 0x102, "---->Hard", sizeof("---->Hard"), buf, sizeof(buf));
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

