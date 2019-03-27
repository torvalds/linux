/*
 * event.h
 */

/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2009 Maksim Yevmenkin <m_evmenkin@yahoo.com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/* $FreeBSD$ */

/*
 * Hack to provide libevent (see devel/libevent port) like API.
 * Should be removed if FreeBSD ever decides to import libevent into base.
 */

#include <sys/select.h>
#include <sys/time.h>
#include <sys/queue.h>
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <syslog.h>

#include "event.h"
#define L2CAP_SOCKET_CHECKED
#include "btpand.h"

#define		__event_link(ev) \
do { \
		TAILQ_INSERT_TAIL(&pending, ev, next); \
		ev->flags |= EV_PENDING; \
} while (0)

static void	tv_add(struct timeval *, struct timeval const *);
static void	tv_sub(struct timeval *, struct timeval const *);
static int	tv_cmp(struct timeval const *, struct timeval const *);
static int	__event_dispatch(void);
static void	__event_add_current(struct event *);
static void	__event_del_current(struct event *);


static TAILQ_HEAD(, event)	pending;
static TAILQ_HEAD(, event)	current;

void
event_init(void)
{
	TAILQ_INIT(&pending);
}

int
event_dispatch(void)
{
	while (__event_dispatch() == 0)
		;

	return (-1);
}

static int
__event_dispatch(void)
{
	fd_set			r, w;
	int			nfd;
	struct event		*ev;
	struct timeval		now, timeout, t;

	FD_ZERO(&r);
	FD_ZERO(&w);

	nfd = 0;

	gettimeofday(&now, NULL);

	timeout.tv_sec = 10;	/* arbitrary */
	timeout.tv_usec = 0;

	TAILQ_INIT(&current);

	/*
	 * Build fd_set's
	 */

	event_log_debug("%s: building fd set...", __func__);

	while (!TAILQ_EMPTY(&pending)) {
		ev = TAILQ_FIRST(&pending);
		event_del(ev);

		if (ev->flags & EV_HAS_TIMEOUT) {
			if (tv_cmp(&now, &ev->expire) >= 0)
				t.tv_sec = t.tv_usec = 0;
			else {
				t = ev->expire;
				tv_sub(&t, &now);
			}

			if (tv_cmp(&t, &timeout) < 0)
				timeout = t;
		}

		if (ev->fd >= 0) {
			if (ev->flags & EV_READ) {
				FD_SET(ev->fd, &r);
				nfd = (nfd > ev->fd) ? nfd : ev->fd;
			}

			if (ev->flags & EV_WRITE) {
				FD_SET(ev->fd, &w);
				nfd = (nfd > ev->fd) ? nfd : ev->fd;
			}
		}

		__event_add_current(ev);
	}

	event_log_debug("%s: waiting for events...", __func__);

	nfd = select(nfd + 1, &r, &w, NULL, &timeout);
	if (nfd < 0)
		return (-1);

	/*
	 * Process current pending
	 */

	event_log_debug("%s: processing events...", __func__);

	gettimeofday(&now, NULL);

	while (!TAILQ_EMPTY(&current)) {
		ev = TAILQ_FIRST(&current);
		__event_del_current(ev);

		/* check if fd is ready for reading/writing */
		if (nfd > 0 && ev->fd >= 0) {
			if (FD_ISSET(ev->fd, &r) || FD_ISSET(ev->fd, &w)) {
				if (ev->flags & EV_PERSIST) {
					if (ev->flags & EV_HAS_TIMEOUT)
						event_add(ev, &ev->timeout);
					else
						event_add(ev, NULL);
				}

				nfd --;

				event_log_debug("%s: calling %p(%d, %p), " \
					"ev=%p", __func__, ev->cb, ev->fd,
					ev->cbarg, ev);

				(ev->cb)(ev->fd,
					(ev->flags & (EV_READ|EV_WRITE)),
					ev->cbarg);

				continue;
			}
		}

		/* if event has no timeout - just requeue */
		if ((ev->flags & EV_HAS_TIMEOUT) == 0) {
			event_add(ev, NULL);
			continue;
		}

		/* check if event has expired */
		if (tv_cmp(&now, &ev->expire) >= 0) {
			if (ev->flags & EV_PERSIST) 
				event_add(ev, &ev->timeout);

			event_log_debug("%s: calling %p(%d, %p), ev=%p",
				__func__, ev->cb, ev->fd, ev->cbarg, ev);

			(ev->cb)(ev->fd,
				(ev->flags & (EV_READ|EV_WRITE)),
				ev->cbarg);

			continue;
		}

		assert((ev->flags & (EV_PENDING|EV_CURRENT)) == 0);
		__event_link(ev);
	}

	return (0);
}

void
__event_set(struct event *ev, int fd, short flags,
	void (*cb)(int, short, void *), void *cbarg)
{
	ev->fd = fd;
	ev->flags = flags;
	ev->cb = cb;
	ev->cbarg = cbarg;
}

int
__event_add(struct event *ev, const struct timeval *timeout)
{
	assert((ev->flags & (EV_PENDING|EV_CURRENT)) == 0);

	if (timeout != NULL) {
		gettimeofday(&ev->expire, NULL);
		tv_add(&ev->expire, timeout);
		ev->timeout = *timeout;
		ev->flags |= EV_HAS_TIMEOUT;
	} else
		ev->flags &= ~EV_HAS_TIMEOUT;

	__event_link(ev);

	return (0);
}

int
__event_del(struct event *ev)
{
	assert((ev->flags & EV_CURRENT) == 0);

	if ((ev->flags & EV_PENDING) != 0) {
		TAILQ_REMOVE(&pending, ev, next);
		ev->flags &= ~EV_PENDING;
	}

	return (0);
}

static void
__event_add_current(struct event *ev)
{
	assert((ev->flags & (EV_PENDING|EV_CURRENT)) == 0);

	TAILQ_INSERT_TAIL(&current, ev, next);
	ev->flags |= EV_CURRENT;
}

static void
__event_del_current(struct event *ev)
{
	assert((ev->flags & (EV_CURRENT|EV_PENDING)) == EV_CURRENT);

	TAILQ_REMOVE(&current, ev, next);
	ev->flags &= ~EV_CURRENT;
}

static void
tv_add(struct timeval *a, struct timeval const *b)
{
	a->tv_sec += b->tv_sec;
	a->tv_usec += b->tv_usec;

	if(a->tv_usec >= 1000000) {
		a->tv_usec -= 1000000;
		a->tv_sec += 1;
	}
}

static void
tv_sub(struct timeval *a, struct timeval const *b)
{
	if (a->tv_usec < b->tv_usec) {
		a->tv_usec += 1000000;
		a->tv_sec -= 1;
	}

	a->tv_usec -= b->tv_usec;
	a->tv_sec -= b->tv_sec;
}

static int
tv_cmp(struct timeval const *a, struct timeval const *b)
{
 	if (a->tv_sec > b->tv_sec)
		return (1);

	if (a->tv_sec < b->tv_sec)
		return (-1);

	if (a->tv_usec > b->tv_usec)
		return (1);

	if (a->tv_usec < b->tv_usec)
		return (-1);

	return (0);
}

