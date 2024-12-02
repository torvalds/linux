// SPDX-License-Identifier: LGPL-2.1+
// Copyright (C) 2022, Linaro Ltd - Daniel Lezcano <daniel.lezcano@linaro.org>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <signal.h>
#include <sys/epoll.h>
#include "mainloop.h"
#include "log.h"

static int epfd = -1;
static unsigned short nrhandler;
static sig_atomic_t exit_mainloop;

struct mainloop_data {
	mainloop_callback_t cb;
	void *data;
	int fd;
};

static struct mainloop_data **mds;

#define MAX_EVENTS 10

int mainloop(unsigned int timeout)
{
	int i, nfds;
	struct epoll_event events[MAX_EVENTS];
	struct mainloop_data *md;

	if (epfd < 0)
		return -1;

	for (;;) {

		nfds = epoll_wait(epfd, events, MAX_EVENTS, timeout);

		if (exit_mainloop || !nfds)
			return 0;

		if (nfds < 0) {
			if (errno == EINTR)
				continue;
			return -1;
		}

		for (i = 0; i < nfds; i++) {
			md = events[i].data.ptr;

			if (md->cb(md->fd, md->data) > 0)
				return 0;
		}
	}
}

int mainloop_add(int fd, mainloop_callback_t cb, void *data)
{
	struct epoll_event ev = {
		.events = EPOLLIN,
	};

	struct mainloop_data *md;

	if (fd >= nrhandler) {
		mds = realloc(mds, sizeof(*mds) * (fd + 1));
		if (!mds)
			return -1;
		nrhandler = fd + 1;
	}

	md = malloc(sizeof(*md));
	if (!md)
		return -1;

	md->data = data;
	md->cb = cb;
	md->fd = fd;

	mds[fd] = md;
	ev.data.ptr = md;

	if (epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &ev) < 0) {
		free(md);
		return -1;
	}

	return 0;
}

int mainloop_del(int fd)
{
	if (fd >= nrhandler)
		return -1;

	if (epoll_ctl(epfd, EPOLL_CTL_DEL, fd, NULL) < 0)
		return -1;

	free(mds[fd]);

	return 0;
}

int mainloop_init(void)
{
	epfd = epoll_create(2);
	if (epfd < 0)
		return -1;

	return 0;
}

void mainloop_exit(void)
{
	exit_mainloop = 1;
}

void mainloop_fini(void)
{
	close(epfd);
}
