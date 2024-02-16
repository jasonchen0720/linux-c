#include <stdio.h>
#include <unistd.h>
#include <time.h>
#include <ctype.h>
#include <arpa/inet.h>
#include <string.h>

#include "co_socket.h"

#define LOG(format,...) printf("%s(%d): "format"\n\n",  __FUNCTION__, __LINE__, ##__VA_ARGS__)

static struct co_struct 	coroutine;
static struct co_scheduler 	scheduler;
static void func(struct co_struct *c)
{
	LOG("The program 1th enter.");
	LOG("co@%p  arg:%s", c, (const char *)c->arg);
	c->yield(c);
	LOG("The program 2th enter.");
}
static void test()
{
	struct co_struct 	*c = &coroutine;
	struct co_scheduler *s = &scheduler;
	LOG("co@%p test started.", c);
	LOG("The program 1th enter.");
	co_scheduler_init(s, NULL, NULL);
	co_init(c, s, func, "hello world");
	s->resume(c);
	LOG("The program 2th enter.");
	s->resume(c);
	LOG("The program 3th enter.");
	LOG("co@%p test exited.", c);
}

#define socket(a, b, c)		co_socket(a, b, c)
#define accept(a, b, c)		co_accept(a, b, c) 
#define send(a, b, c, d)	co_send(a, b, c, d)
#define recv(a, b, c, d)	co_recv(a, b, c, d)
#define connect(a, b, c)	co_connect(a, b, c)


static void server_proc(struct co_struct *co)
{
	char buf[1024];
	
	struct co_sock *sock = co->arg;
	LOG("recv co@%p enter, sockfd: %d", co, sock->sockfd);
	do {
		ssize_t rcvd = recv(sock->sockfd, buf, sizeof(buf), 0);

		if (rcvd <= 0)
			break;

		buf[rcvd] = '\0';
		
		LOG("Recvd from client: <-- %s", buf);

		
		size_t s = sprintf(buf, "Message from Jay Chan at %ld", (long)time(NULL));
	    send(sock->sockfd, buf, s, 0);
	} while (1);
	LOG("recv co@%p exit, sockfd: %d", co, sock->sockfd);
	co_socket_exit(sock);
}

static void server_func(struct co_struct *co)
{
	
	struct sockaddr_in clit_addr;
	socklen_t client_len = sizeof(clit_addr);

	struct co_sock *sock = co->arg;

	LOG("accept from listening sockfd: %d co@%p...", sock->sockfd, co);
	for (;;) {
		int sockfd = accept(sock->sockfd, (struct sockaddr *)&clit_addr, &client_len);

		LOG("accept return sockfd: %d from %s", sockfd, inet_ntoa(clit_addr.sin_addr));
		
		co_socket_exec(sockfd, server_proc, NULL);
	}
	co_socket_exit(sock);
}

int main(int argc, char *argv[])
{
	if (argc > 1 && strcmp(argv[1], "server") == 0) {
		if (co_socket_init() == -1)
			return -1;
		
	    int listenfd = socket(AF_INET, SOCK_STREAM, 0);

		if (listenfd == -1)
			return -1;
		
	    struct sockaddr_in serv_addr;
		
	    memset(&serv_addr, 0, sizeof(serv_addr));
	    
	    serv_addr.sin_family 		= AF_INET;
	    serv_addr.sin_port 			= htons(8080);
	    serv_addr.sin_addr.s_addr 	= htonl(INADDR_ANY);

	    bind(listenfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr));
	    listen(listenfd, 32);

		if (co_socket_exec(listenfd, server_func, NULL) == -1) {
			close(listenfd);
			return -1;
		}
			
		co_socket_run();
	} else {
		test();
	}
	return 0;
}

