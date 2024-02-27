#include <stdio.h>
#include <errno.h>

#include "co_log.h"
#include "co_core.h"
#include "co_epoll.h"

int co_epoll(struct co_struct *co, int epfd, int fd, uint32_t events, 
			int timeout)
{
	struct epoll_event ev;
	ev.events = events;
	ev.data.ptr = co;

	epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &ev);

	if (timeout >= 0) {
		co->sleep_expired_time = co_time() + 1000 * timeout;
		while (&co->sleep_node != rb_insert(&co->sleep_node, &co->scheduler->sleep_tree))
			co->sleep_expired_time++;
		LOG("co@%p will wait %d ms, resume at %lld", co, timeout, co->sleep_expired_time);
		co_transfer(co, CO_TIMEDWAIT);
	} else
		co_transfer(co, CO_WAITING);
	
	co_yield(co);
	TRACE(rolec, co, "co@%p resumed.", co);
	
	epoll_ctl(epfd, EPOLL_CTL_DEL, fd, &ev);
	
	return co->scheduler->schedret;
}
static int co_epoll_nexttmo(struct co_scheduler *scheduler)
{
	struct rb_node * n = rb_first(&scheduler->sleep_tree);
	if (n == NULL)
		return -1;
	
	struct co_struct * co = rb_entry(n, struct co_struct, sleep_node);
	int64_t tmo = co->sleep_expired_time - co_time();
	if (tmo > 0)
		return (tmo + 500) / 1000;
	
	return tmo < 0 ? -1 : 0;
}

int co_epoll_schedule(struct co_scheduler *scheduler, int epfd, struct epoll_event *events,
                      int maxevents)
{
	do {
		int tmo = co_epoll_nexttmo(scheduler);
		TRACE(roles, scheduler, "epoll waiting..., timedout: %d.", tmo);
		int ret = epoll_wait(epfd, events, maxevents, tmo);
		if (ret > 0) {
			int n = 0;
			for (n = 0; n < ret; n++) {
				co_transfer((struct co_struct *)events[n].data.ptr, CO_READY);
			}
			return n;
		} else if (ret == 0) {
			TRACE(roles, scheduler, "epoll %d ms timedout.", tmo);
			return 0;
		} else if (errno == EINTR) {
			continue;
		} else {
			TRACE(roles, scheduler, "epoll ret: %d errno: %d", ret, errno);
			return -1;
		}
	} while (1);
}

