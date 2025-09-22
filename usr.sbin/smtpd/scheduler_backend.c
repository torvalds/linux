/*	$OpenBSD: scheduler_backend.c,v 1.18 2021/06/14 17:58:16 eric Exp $	*/

/*
 * Copyright (c) 2012 Gilles Chehade <gilles@poolp.org>
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

#include <string.h>

#include "smtpd.h"

extern struct scheduler_backend scheduler_backend_null;
extern struct scheduler_backend scheduler_backend_proc;
extern struct scheduler_backend scheduler_backend_ramqueue;

struct scheduler_backend *
scheduler_backend_lookup(const char *name)
{
	if (!strcmp(name, "null"))
		return &scheduler_backend_null;
	if (!strcmp(name, "ramqueue"))
		return &scheduler_backend_ramqueue;

	return &scheduler_backend_proc;
}

void
scheduler_info(struct scheduler_info *sched, struct envelope *evp)
{
	struct dispatcher *disp;

	disp = evp->type == D_BOUNCE ?
	    env->sc_dispatcher_bounce :
	    dict_xget(env->sc_dispatchers, evp->dispatcher);

	switch (disp->type) {
	case DISPATCHER_LOCAL:
		sched->type = D_MDA;
		break;
	case DISPATCHER_REMOTE:
		sched->type = D_MTA;
		break;
	case DISPATCHER_BOUNCE:
		sched->type = D_BOUNCE;
		break;
	}
	sched->ttl = disp->ttl ? disp->ttl : env->sc_ttl;

	sched->evpid = evp->id;
	sched->creation = evp->creation;
	sched->retry = evp->retry;
	sched->lasttry = evp->lasttry;
	sched->lastbounce = evp->lastbounce;
	sched->nexttry	= 0;
}
