#include <stdio.h>
#include <unistd.h>
#include <time.h>
#include <ctype.h>
#include <arpa/inet.h>
#include <string.h>

#include "co_log.h"
#include "co_core.h"
#include "co_socket.h"
#undef  LOG_TAG
#define LOG_TAG "co-sample"
#define CO_STACKSIZE	(16 * 1024)

#define socket(a, b, c)		co_socket(a, b, c)
#define accept(a, b, c)		co_accept(a, b, c) 
#define send(a, b, c, d)	co_send(a, b, c, d)
#define recv(a, b, c, d)	co_recv(a, b, c, d)
#define connect(a, b, c)	co_connect(a, b, c)


static void server_proc(struct co_struct *co)
{
	char buf[1024];
	struct co_sock *sock = co->arg;
	TRACE(rolec, co, "recv enter, sockfd: %d", sock->sockfd);
	do {
		ssize_t rcvd = recv(sock->sockfd, buf, sizeof(buf), 0);

		if (rcvd <= 0)
			break;

		buf[rcvd] = '\0';
		
		TRACE(rolec, co, "Recvd from client: <-- %s", buf);

		//co_sleep(co, 1000 * 1000);
		
		size_t s = sprintf(buf, "Message from Jay Chan at %ld", (long)time(NULL));
	    send(sock->sockfd, buf, s, 0);
	} while (1);
	TRACE(rolec, co, "recv exit, sockfd: %d", sock->sockfd);
}

static void server_func(struct co_struct *co)
{
	
	struct sockaddr_in clit_addr;
	socklen_t client_len = sizeof(clit_addr);

	struct co_sock *sock = co->arg;

	TRACE(rolec, co, "accept from listening sockfd: %d", sock->sockfd);
	for (;;) {
		int sockfd = accept(sock->sockfd, (struct sockaddr *)&clit_addr, &client_len);

		TRACE(rolec, co, "accept return sockfd: %d from %s", sockfd, inet_ntoa(clit_addr.sin_addr));
		
		co_socket_exec(sockfd, server_proc, NULL, CO_STACKSIZE);
	}
}

int main(int argc, char *argv[])
{
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

	if (co_socket_exec(listenfd, server_func, NULL, CO_STACKSIZE) == -1) {
		close(listenfd);
		return -1;
	}
		
	return co_socket_run();
}

