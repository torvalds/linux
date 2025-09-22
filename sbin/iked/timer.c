/*	$OpenBSD: timer.c,v 1.13 2016/09/13 10:49:52 mikeb Exp $	*/

/*
 * Copyright (c) 2010-2013 Reyk Floeter <reyk@openbsd.org>
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

#include <sys/queue.h>
#include <sys/socket.h>
#include <sys/uio.h>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <ctype.h>
#include <event.h>

#include "iked.h"

void	 timer_callback(int, short, void *);

void
timer_set(struct iked *env, struct iked_timer *tmr,
    void (*cb)(struct iked *, void *), void *arg)
{
	if (evtimer_initialized(&tmr->tmr_ev) &&
	    evtimer_pending(&tmr->tmr_ev, NULL))
		evtimer_del(&tmr->tmr_ev);

	tmr->tmr_env = env;
	tmr->tmr_cb = cb;
	tmr->tmr_cbarg = arg;
	evtimer_set(&tmr->tmr_ev, timer_callback, tmr);
}

void
timer_add(struct iked *env, struct iked_timer *tmr, int timeout)
{
	struct timeval		 tv = { timeout };

	evtimer_add(&tmr->tmr_ev, &tv);
}

void
timer_del(struct iked *env, struct iked_timer *tmr)
{
	if (tmr->tmr_env == env && tmr->tmr_cb &&
	    evtimer_initialized(&tmr->tmr_ev))
		evtimer_del(&tmr->tmr_ev);
}

void
timer_callback(int fd, short event, void *arg)
{
	struct iked_timer	*tmr = arg;

	if (tmr->tmr_cb)
		tmr->tmr_cb(tmr->tmr_env, tmr->tmr_cbarg);
}
