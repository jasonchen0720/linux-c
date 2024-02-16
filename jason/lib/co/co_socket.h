#ifndef __CO_SOCKET_H__
#define __CO_SOCKET_H__
#include <sys/types.h>
#include <sys/socket.h>
#include "co_define.h"
struct co_sock 
{
	struct co_struct co;
	void *priv;
	int sockfd;
};
int co_socket(int domain, int type, int protocol);
int co_accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen);
int co_connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen);
ssize_t co_send(int sockfd, const void *buf, size_t len, int flags);
ssize_t co_sendto(int sockfd, const void *buf, size_t len, int flags, const struct sockaddr *dest_addr, socklen_t addrlen);
ssize_t co_recv(int sockfd, void *buf, size_t len, int flags);
ssize_t co_recvfrom(int sockfd, void *buf, size_t len, int flags, struct sockaddr *src_addr, socklen_t *addrlen);
int co_socket_init();
int  co_socket_exec(int sockfd, void (*func)(struct co_struct *), void *priv);
void co_socket_exit(struct co_sock *sock);
int co_socket_run();

#endif
