/*	$OpenBSD: accept.c,v 1.7 2016/05/23 19:11:42 renato Exp $ */

/*
 * Copyright (c) 2012 Claudio Jeker <claudio@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/types.h>
#include <stdlib.h>

#include "ldpd.h"
#include "ldpe.h"
#include "log.h"

struct accept_ev {
	LIST_ENTRY(accept_ev)	 entry;
	struct event		 ev;
	void			(*accept_cb)(int, short, void *);
	void			*arg;
	int			 fd;
};

struct {
	LIST_HEAD(, accept_ev)	queue;
	struct event		evt;
} accept_queue;

static void	accept_arm(void);
static void	accept_unarm(void);
static void	accept_cb(int, short, void *);
static void	accept_timeout(int, short, void *);

void
accept_init(void)
{
	LIST_INIT(&accept_queue.queue);
	evtimer_set(&accept_queue.evt, accept_timeout, NULL);
}

int
accept_add(int fd, void (*cb)(int, short, void *), void *arg)
{
	struct accept_ev	*av;

	if ((av = calloc(1, sizeof(*av))) == NULL)
		return (-1);
	av->fd = fd;
	av->accept_cb = cb;
	av->arg = arg;
	LIST_INSERT_HEAD(&accept_queue.queue, av, entry);

	event_set(&av->ev, av->fd, EV_READ, accept_cb, av);
	event_add(&av->ev, NULL);

	log_debug("%s: accepting on fd %d", __func__, fd);

	return (0);
}

void
accept_del(int fd)
{
	struct accept_ev	*av;

	LIST_FOREACH(av, &accept_queue.queue, entry)
		if (av->fd == fd) {
			log_debug("%s: %d removed from queue", __func__, fd);
			event_del(&av->ev);
			LIST_REMOVE(av, entry);
			free(av);
			return;
		}
}

void
accept_pause(void)
{
	struct timeval evtpause = { 1, 0 };

	log_debug(__func__);
	accept_unarm();
	evtimer_add(&accept_queue.evt, &evtpause);
}

void
accept_unpause(void)
{
	if (evtimer_pending(&accept_queue.evt, NULL)) {
		log_debug(__func__);
		evtimer_del(&accept_queue.evt);
		accept_arm();
	}
}

static void
accept_arm(void)
{
	struct accept_ev	*av;
	LIST_FOREACH(av, &accept_queue.queue, entry)
		event_add(&av->ev, NULL);
}

static void
accept_unarm(void)
{
	struct accept_ev	*av;
	LIST_FOREACH(av, &accept_queue.queue, entry)
		event_del(&av->ev);
}

static void
accept_cb(int fd, short event, void *arg)
{
	struct accept_ev	*av = arg;
	event_add(&av->ev, NULL);
	av->accept_cb(fd, event, av->arg);
}

static void
accept_timeout(int fd, short event, void *bula)
{
	log_debug(__func__);
	accept_arm();
}
