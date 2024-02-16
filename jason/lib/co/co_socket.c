#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/epoll.h>

#include "co_log.h"
#include "co_define.h"
#include "mem_pool.h"

struct sock_struct 
{
	struct co_struct co;
	void *priv;
	int sockfd;
};
struct poll_struct
{
	int 				epfd;
	int					eventsize;
	struct epoll_event *eventlist;
};
#define sock_skfd(coroutine) 	(((struct sock_struct *)coroutine->arg)->sockfd)
#define sock_epfd(scheduler) 	(((struct poll_struct *)scheduler->priv)->epfd)
static struct poll_struct 	socket_poll;
static struct co_scheduler 	socket_sched;
static struct mem_cache *	socket_cache = NULL;
int sock_cache_init()
{
	size_t chunk_size  = sizeof(struct sock_struct);
	size_t chunk_count = mem_chunk_count(2, chunk_size);
	
	LOG("co: chunk_size: %lu, chunk_count: %lu", chunk_size, chunk_count);
	
	socket_cache = mem_cache_create(chunk_count, chunk_size);
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
static inline struct sock_struct * sock_cache_alloc()
{
	return mem_cache_alloc(socket_cache);
}
static inline void sock_cache_free(struct sock_struct *sock)
{
	return mem_cache_free(socket_cache, sock);
}

static inline struct co_scheduler *sock_scheduler()
{
	return &socket_sched;
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
static int sock_epoll(struct co_struct *co, uint32_t evt)
{
	struct epoll_event ev;

	struct sock_struct *sock = co->arg;
	
	ev.events 	= evt | EPOLLERR | EPOLLHUP;
	ev.data.ptr = co;

	epoll_ctl(sock_epfd(co->scheduler), EPOLL_CTL_ADD, sock->sockfd, &ev);

	LOG("co@%p yield.", co);
	co->yield(co);
	LOG("co@%p resume.", co);
	
	epoll_ctl(sock_epfd(co->scheduler), EPOLL_CTL_DEL, sock->sockfd, &ev);
	return 0;
}
static void sock_poll(struct co_scheduler *scheduler)
{
	int ret;
	int timeout = -1;
	struct co_struct *co;
	struct epoll_event events[32];
	do {
		LOG("epoll wait....");
		ret = epoll_wait(sock_epfd(scheduler), events, 32, timeout);
		if (ret > 0) {
			int n = 0;
			for (n = 0; n < ret; n++) {
				co = events[n].data.ptr;
				LOG("sockfd:%d.", sock_skfd(co));
				LOG("resume co@%p.", co);
				scheduler->resume(co);
			}
			break;
		} else if (ret < 0 && errno == EINTR) {
			continue;
		}
		LOG("epoll_wait ret: %d errno: %d", ret, errno);
	} while (1);
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
		sock_epoll(sock_scheduler()->runningco, EPOLLIN);
	retry:
		sock = accept(sockfd, addr, addrlen);
		if (sock == -1) {
			LOG("accept sockfd:%d errno:%d.", sockfd, errno);
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
			LOG("connect errno:%d.", errno);
			switch (errno) {
			case EINTR:
				continue;
			case EAGAIN: /* On unix, same as EWOULDBLOCK */
			case EINPROGRESS:
				sock_epoll(sock_scheduler()->runningco, EPOLLOUT);
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
			LOG("send errno:%d.", errno);
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
			sock_epoll(sock_scheduler()->runningco, EPOLLIN);
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
			LOG("sendto errno:%d.", errno);
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
			sock_epoll(sock_scheduler()->runningco, EPOLLOUT);
		else
			break;
	}

	return sent;
}


ssize_t co_recv(int sockfd, void *buf, size_t len, int flags)
{
	sock_epoll(sock_scheduler()->runningco, EPOLLIN);

	do {
		ssize_t rcvd = recv(sockfd, buf, len, flags);

		if (rcvd < 0) {
			LOG("recv errno:%d.", errno);
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
	sock_epoll(sock_scheduler()->runningco, EPOLLIN);

	do {
		ssize_t rcvd = recvfrom(sockfd, buf, len, flags, src_addr, addrlen);

		if (rcvd < 0) {
			LOG("recvfrom errno:%d.", errno);
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

int co_socket_setup()
{
	struct poll_struct  *so_poll = &socket_poll;

	so_poll->epfd = epoll_create(1024);
	LOG("socket poller epfd: %d", so_poll->epfd);
	if (so_poll->epfd < 0)
		return -1;
	so_poll->eventsize = 0;
	so_poll->eventlist = NULL;

	if (sock_cache_init() < 0) {
		close(so_poll->epfd);
		return -1;
	}
	co_scheduler_init(sock_scheduler(), sock_poll, so_poll);
	
	return 0;
}

int co_socket_create(int sockfd, void (*func)(struct co_struct *), void *priv)
{
	struct sock_struct *sock = sock_cache_alloc();
	if (sock == NULL)
		return -1;

	sock->sockfd = sockfd;
	sock->priv	 = priv;
	
	if (co_init(&sock->co, sock_scheduler(), func, sock) == -1) {
		sock_cache_free(sock);
		return -1;
	}
	return 0;
}

int co_socket_run()
{
	return co_scheduler_run(sock_scheduler());
}
