#ifndef __CO_EPOLL_H__
#define __CO_EPOLL_H__

#include <sys/epoll.h>

#include "co_type.h"

int co_epoll(struct co_struct *co, int epfd, int fd, uint32_t events, int timeout);
int co_epoll_schedule(struct co_scheduler *scheduler, int epfd, struct epoll_event *events,
                      int maxevents);

#endif
