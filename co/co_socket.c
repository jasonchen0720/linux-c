#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <assert.h>
#include <sys/ioctl.h>

#include "co_core.h"
#include "co_log.h"
#include "co_epoll.h"
#include "co_socket.h"

#include "mem_pool.h"
#define SK_MAX_EVENTS		1024
#define SK_CACHE_MAX_PAGES	2 
struct sk_epoll
{
	int 			   epfd;
	struct epoll_event events[SK_MAX_EVENTS];
};
#define sock_skfd(coroutine) 	(((struct co_sock *)coroutine->arg)->sockfd)
#define sock_epfd(scheduler) 	(((struct sk_epoll *)scheduler->priv)->epfd)
#define sock_sched()			(&socket_sched)
static struct sk_epoll 		socket_epoll;
static struct co_scheduler 	socket_sched;
static struct mem_cache *	socket_cache = NULL;
int sock_cache_init()
{
	size_t chunk_size  = sizeof(struct co_sock);
	size_t chunk_count = mem_chunk_count(SK_CACHE_MAX_PAGES, chunk_size);
	
	LOG("co: chunk_size: %lu, chunk_count: %lu", chunk_size, chunk_count);
	
	socket_cache = mem_cache_create(chunk_count, chunk_size, 0);
	if (socket_cache == NULL) {
		LOG("cache alloc failed.");
		return -1;
	}
	return 0;
}

static inline void sock_cache_deinit()
{
	mem_cache_destroy(socket_cache);
}
static inline struct co_sock * sock_cache_alloc()
{
	return mem_cache_alloc(socket_cache);
}
static inline void sock_cache_free(struct co_sock *sock)
{
	return mem_cache_free(socket_cache, sock);
}
static int sock_options(int sockfd)
{
	int opt = 1;
    if (ioctl(sockfd, FIONBIO, &opt) == -1) {
		LOG("ioctl errno:%d.", errno);
		return -1;
    }
	if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1) {
		LOG("setsockopt errno:%d.", errno);
		return -1;
	}
	return 0;
}
static inline int sock_epoll(struct co_struct *co, uint32_t evt)
{
	return co_epoll(co, sock_epfd(co->scheduler), sock_skfd(co), evt | EPOLLERR | EPOLLHUP, -1);
		
}
static int sock_schedule(struct co_scheduler *scheduler)
{
	struct sk_epoll *sk = scheduler->priv;
	return co_epoll_schedule(scheduler, sk->epfd, sk->events, SK_MAX_EVENTS);	
}
static void sock_release(struct co_struct *co)
{
	struct co_sock *sock = co->arg;
	close(sock->sockfd);
	sock_cache_free(sock);
}
int co_socket(int domain, int type, int protocol)
{
	int sock = socket(domain, type, protocol);
	if (sock == -1) {
		LOG("Failed to create a new socket.");
		return -1;
	}
    if (sock_options(sock) == -1) {
		close(sock);
		LOG("Failed to set socket options.");
		return -1;
    }
	LOG("created sock:%d", sock);
	return sock;
}

int  co_accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen)
{
	int sock;
	do {
		sock_epoll(sock_sched()->runningco, EPOLLIN);
	retry:
		sock = accept(sockfd, addr, addrlen);
		if (sock == -1) {
			TRACE(rolec, sock_sched()->runningco, "accept sockfd:%d errno:%d.", sockfd, errno);
			switch (errno) {
			case EINTR:
				goto retry;
			case EAGAIN:
				continue;
			default:
				return -1;
			}
		}
		break;
	} while (1);
	
	if (sock_options(sock) == -1) {
		close(sock);
		return -1;
    }
	return sock;
}

int co_connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen)
{
	int ret;

	for (;;) {
		ret = connect(sockfd, addr, addrlen);

		if (ret < 0) {
			TRACE(rolec, sock_sched()->runningco, "connect errno:%d.", errno);
			switch (errno) {
			case EINTR:
				continue;
			case EAGAIN: /* On unix, same as EWOULDBLOCK */
			case EINPROGRESS:
				sock_epoll(sock_sched()->runningco, EPOLLOUT);
				continue;
			default:
				break;
			}
			break;
		}
	}
	return ret;
}

ssize_t co_send(int sockfd, const void *buf, size_t len, int flags)
{
	size_t sent = 0;
	size_t ret;

	for (;;){
		ret = send(sockfd, (const char *)buf + sent, len - sent, flags);
		if (ret > 0) {
			sent += ret;
		} else if (ret < 0) {
			TRACE(rolec, sock_sched()->runningco, "send errno:%d.", errno);
			switch (errno) {
			case EINTR:
				continue;
			case EAGAIN:
				break;
			default:
				goto out;
			}
		}
		if (sent < len)
			sock_epoll(sock_sched()->runningco, EPOLLIN);
		else break;
	}
out:
	return sent > 0 ? sent : ret;
}

ssize_t co_sendto(int sockfd, const void *buf, size_t len, int flags, const struct sockaddr *dest_addr, socklen_t addrlen)
{
	size_t sent = 0;
	size_t ret;

	for (;;){
		ret = sendto(sockfd, (const char *)buf + sent, len - sent, flags, dest_addr, addrlen);
		if (ret > 0) {
			sent += ret;	
		} else if (ret < 0) {
			TRACE(rolec, sock_sched()->runningco, "sendto errno:%d.", errno);
			switch (errno) {
			case EINTR:
				continue;
			case EAGAIN:
				break;
			default:
				return -1;
			}
		}
		if (sent < len)
			sock_epoll(sock_sched()->runningco, EPOLLOUT);
		else
			break;
	}

	return sent;
}


ssize_t co_recv(int sockfd, void *buf, size_t len, int flags)
{
	sock_epoll(sock_sched()->runningco, EPOLLIN);

	do {
		ssize_t rcvd = recv(sockfd, buf, len, flags);

		if (rcvd < 0) {
			TRACE(rolec, sock_sched()->runningco, "recv errno:%d.", errno);
			switch (errno) {
			case EINTR:
				continue;
			case EAGAIN:
			case ECONNRESET:
				break;
			default:
				break;
			}
		}
		return rcvd;
	} while (1);
}

ssize_t co_recvfrom(int sockfd, void *buf, size_t len, int flags, struct sockaddr *src_addr, socklen_t *addrlen)
{
	sock_epoll(sock_sched()->runningco, EPOLLIN);

	do {
		ssize_t rcvd = recvfrom(sockfd, buf, len, flags, src_addr, addrlen);

		if (rcvd < 0) {
			TRACE(rolec, sock_sched()->runningco, "recvfrom errno:%d.", errno);
			switch (errno) {
			case EINTR:
				continue;
			case EAGAIN:
			case ECONNRESET:
				break;
			default:
				break;
			}
		}
		return rcvd;
	} while (1);
}
int co_socket_init()
{
	struct sk_epoll *epoll = &socket_epoll;

	epoll->epfd = epoll_create(1024);
	LOG("socket poller epfd: %d", epoll->epfd);
	if (epoll->epfd < 0)
		return -1;
	
	if (sock_cache_init() < 0) {
		close(epoll->epfd);
		return -1;
	}
	co_scheduler_init(sock_sched(), sock_schedule, epoll, sock_release);
	
	return 0;
}

int co_socket_exec(int sockfd, void (*func)(struct co_struct *), void *priv, size_t stacksize)
{
	struct co_sock *sock = sock_cache_alloc();
	if (sock == NULL)
		return -1;

	sock->sockfd = sockfd;
	sock->priv	 = priv;
	
	if (co_init(&sock->co, sock_sched(), func, sock, stacksize) == -1) {
		sock_cache_free(sock);
		return -1;
	}
	return 0;
}
int co_socket_run()
{
	return co_scheduler_run(sock_sched());
}
