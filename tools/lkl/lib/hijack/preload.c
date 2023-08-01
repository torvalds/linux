// SPDX-License-Identifier: GPL-2.0
/*
 * system calls hook by LD_PRELOAD
 * Copyright (c) 2023 Hajime Tazaki
 *
 * Author: Hajime Tazaki <thehajime@gmail.com>
 *
 */

#include "hijack.h"

int setsockopt(int fd, int level, int optname, const void *optval,
	       socklen_t optlen)
{
	return  hijack_setsockopt(fd, level, optname, optval, optlen);
}

int getsockopt(int fd, int level, int optname, void *optval, socklen_t *optlen)
{
	return hijack_getsockopt(fd, level, optname, optval, optlen);
}

int poll(struct pollfd *fds, nfds_t nfds, int timeout)
{
	return  hijack_poll(fds, nfds, timeout);
}
int __poll(struct pollfd *, nfds_t, int) __attribute__((alias("poll")));

int select(int nfds, fd_set *r, fd_set *w, fd_set *e, struct timeval *t)
{
	return hijack_select(nfds, r, w, e, t);
}

int epoll_create(int size)
{
	return hijack_epoll_create(size);
}

int epoll_create1(int flags)
{
	return hijack_epoll_create1(flags);
}

int epoll_ctl(int epollfd, int op, int fd, struct epoll_event *event)
{
	return hijack_epoll_ctl(epollfd, op, fd, event);
}

int epoll_wait(int epfd, struct epoll_event *events,
	       int maxevents, int timeout)
{
	return hijack_epoll_wait(epfd, events, maxevents, timeout);
}

int eventfd(unsigned int count, int flags)
{
	return hijack_eventfd(count, flags);
}

int eventfd_read(int fd, uint64_t *value)
{
	return hijack_eventfd_read(fd, value);
}

int eventfd_write(int fd, uint64_t value)
{
	return hijack_eventfd_write(fd, value);
}
