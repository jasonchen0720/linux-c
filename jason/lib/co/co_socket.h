#ifndef __CO_SOCKET_H__
#define __CO_SOCKET_H__
#include <sys/types.h>
#include <sys/socket.h>

int co_socket(int domain, int type, int protocol);
int co_accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen);
int co_connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen);
ssize_t co_send(int sockfd, const void *buf, size_t len, int flags);
ssize_t co_sendto(int sockfd, const void *buf, size_t len, int flags, const struct sockaddr *dest_addr, socklen_t addrlen);
ssize_t co_recv(int sockfd, void *buf, size_t len, int flags);
ssize_t co_recvfrom(int sockfd, void *buf, size_t len, int flags, struct sockaddr *src_addr, socklen_t *addrlen);
int co_socket_setup();
int co_socket_create(int sockfd, void (*func)(struct co_struct *), void *priv);
int co_socket_run();

#endif
