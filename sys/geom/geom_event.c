/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2002 Poul-Henning Kamp
 * Copyright (c) 2002 Networks Associates Technology, Inc.
 * All rights reserved.
 *
 * This software was developed for the FreeBSD Project by Poul-Henning Kamp
 * and NAI Labs, the Security Research Division of Network Associates, Inc.
 * under DARPA/SPAWAR contract N66001-01-C-8035 ("CBOSS"), as part of the
 * DARPA CHATS research program.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The names of the authors may not be used to endorse or promote
 *    products derived from this software without specific prior written
 *    permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * XXX: How do we in general know that objects referenced in events
 * have not been destroyed before we get around to handle the event ?
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/malloc.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/proc.h>
#include <sys/errno.h>
#include <sys/time.h>
#include <geom/geom.h>
#include <geom/geom_int.h>

#include <machine/stdarg.h>

TAILQ_HEAD(event_tailq_head, g_event);

static struct event_tailq_head g_events = TAILQ_HEAD_INITIALIZER(g_events);
static u_int g_pending_events;
static TAILQ_HEAD(,g_provider) g_doorstep = TAILQ_HEAD_INITIALIZER(g_doorstep);
static struct mtx g_eventlock;
static int g_wither_work;

#define G_N_EVENTREFS		20

struct g_event {
	TAILQ_ENTRY(g_event)	events;
	g_event_t		*func;
	void			*arg;
	int			flag;
	void			*ref[G_N_EVENTREFS];
};

#define EV_DONE		0x80000
#define EV_WAKEUP	0x40000
#define EV_CANCELED	0x20000
#define EV_INPROGRESS	0x10000

void
g_waitidle(void)
{

	g_topology_assert_not();

	mtx_lock(&g_eventlock);
	TSWAIT("GEOM events");
	while (!TAILQ_EMPTY(&g_events))
		msleep(&g_pending_events, &g_eventlock, PPAUSE,
		    "g_waitidle", hz/5);
	TSUNWAIT("GEOM events");
	mtx_unlock(&g_eventlock);
	curthread->td_pflags &= ~TDP_GEOM;
}

#if 0
void
g_waitidlelock(void)
{

	g_topology_assert();
	mtx_lock(&g_eventlock);
	while (!TAILQ_EMPTY(&g_events)) {
		g_topology_unlock();
		msleep(&g_pending_events, &g_eventlock, PPAUSE,
		    "g_waitidlel", hz/5);
		g_topology_lock();
	}
	mtx_unlock(&g_eventlock);
}
#endif

struct g_attrchanged_args {
	struct g_provider *pp;
	const char *attr;
};

static void
g_attr_changed_event(void *arg, int flag)
{
	struct g_attrchanged_args *args;
	struct g_provider *pp;
	struct g_consumer *cp;
	struct g_consumer *next_cp;

	args = arg;
	pp = args->pp;

	g_topology_assert();
	if (flag != EV_CANCEL && g_shutdown == 0) {

		/*
		 * Tell all consumers of the change.
		 */
		LIST_FOREACH_SAFE(cp, &pp->consumers, consumers, next_cp) {
			if (cp->geom->attrchanged != NULL)
				cp->geom->attrchanged(cp, args->attr);
		}
	}
	g_free(args);
}

int
g_attr_changed(struct g_provider *pp, const char *attr, int flag)
{
	struct g_attrchanged_args *args;
	int error;

	args = g_malloc(sizeof *args, flag);
	if (args == NULL)
		return (ENOMEM);
	args->pp = pp;
	args->attr = attr;
	error = g_post_event(g_attr_changed_event, args, flag, pp, NULL);
	if (error != 0)
		g_free(args);
	return (error);
}

void
g_orphan_provider(struct g_provider *pp, int error)
{

	/* G_VALID_PROVIDER(pp)  We likely lack topology lock */
	g_trace(G_T_TOPOLOGY, "g_orphan_provider(%p(%s), %d)",
	    pp, pp->name, error);
	KASSERT(error != 0,
	    ("g_orphan_provider(%p(%s), 0) error must be non-zero\n",
	     pp, pp->name));
	
	pp->error = error;
	mtx_lock(&g_eventlock);
	KASSERT(!(pp->flags & G_PF_ORPHAN),
	    ("g_orphan_provider(%p(%s)), already an orphan", pp, pp->name));
	pp->flags |= G_PF_ORPHAN;
	TAILQ_INSERT_TAIL(&g_doorstep, pp, orphan);
	mtx_unlock(&g_eventlock);
	wakeup(&g_wait_event);
}

/*
 * This function is called once on each provider which the event handler
 * finds on its g_doorstep.
 */

static void
g_orphan_register(struct g_provider *pp)
{
	struct g_consumer *cp, *cp2;
	int wf;

	g_topology_assert();
	G_VALID_PROVIDER(pp);
	g_trace(G_T_TOPOLOGY, "g_orphan_register(%s)", pp->name);

	g_cancel_event(pp);

	wf = pp->flags & G_PF_WITHER;
	pp->flags &= ~G_PF_WITHER;

	/*
	 * Tell all consumers the bad news.
	 * Don't be surprised if they self-destruct.
	 */
	LIST_FOREACH_SAFE(cp, &pp->consumers, consumers, cp2) {
		KASSERT(cp->geom->orphan != NULL,
		    ("geom %s has no orphan, class %s",
		    cp->geom->name, cp->geom->class->name));
		/*
		 * XXX: g_dev_orphan method does deferred destroying
		 * and it is possible, that other event could already
		 * call the orphan method. Check consumer's flags to
		 * do not schedule it twice.
		 */
		if (cp->flags & G_CF_ORPHAN)
			continue;
		cp->flags |= G_CF_ORPHAN;
		cp->geom->orphan(cp);
	}
	if (LIST_EMPTY(&pp->consumers) && wf)
		g_destroy_provider(pp);
	else
		pp->flags |= wf;
#ifdef notyet
	cp = LIST_FIRST(&pp->consumers);
	if (cp != NULL)
		return;
	if (pp->geom->flags & G_GEOM_WITHER)
		g_destroy_provider(pp);
#endif
}

static int
one_event(void)
{
	struct g_event *ep;
	struct g_provider *pp;

	g_topology_assert();
	mtx_lock(&g_eventlock);
	TAILQ_FOREACH(pp, &g_doorstep, orphan) {
		if (pp->nstart == pp->nend)
			break;
	}
	if (pp != NULL) {
		G_VALID_PROVIDER(pp);
		TAILQ_REMOVE(&g_doorstep, pp, orphan);
		mtx_unlock(&g_eventlock);
		g_orphan_register(pp);
		return (1);
	}

	ep = TAILQ_FIRST(&g_events);
	if (ep == NULL) {
		wakeup(&g_pending_events);
		return (0);
	}
	if (ep->flag & EV_INPROGRESS) {
		mtx_unlock(&g_eventlock);
		return (1);
	}
	ep->flag |= EV_INPROGRESS;
	mtx_unlock(&g_eventlock);
	g_topology_assert();
	ep->func(ep->arg, 0);
	g_topology_assert();
	mtx_lock(&g_eventlock);
	TSRELEASE("GEOM events");
	TAILQ_REMOVE(&g_events, ep, events);
	ep->flag &= ~EV_INPROGRESS;
	if (ep->flag & EV_WAKEUP) {
		ep->flag |= EV_DONE;
		mtx_unlock(&g_eventlock);
		wakeup(ep);
	} else {
		mtx_unlock(&g_eventlock);
		g_free(ep);
	}
	return (1);
}

void
g_run_events()
{

	for (;;) {
		g_topology_lock();
		while (one_event())
			;
		mtx_assert(&g_eventlock, MA_OWNED);
		if (g_wither_work) {
			g_wither_work = 0;
			mtx_unlock(&g_eventlock);
			g_wither_washer();
			g_topology_unlock();
		} else {
			g_topology_unlock();
			msleep(&g_wait_event, &g_eventlock, PRIBIO | PDROP,
			    "-", TAILQ_EMPTY(&g_doorstep) ? 0 : hz / 10);
		}
	}
	/* NOTREACHED */
}

void
g_cancel_event(void *ref)
{
	struct g_event *ep, *epn;
	struct g_provider *pp;
	u_int n;

	mtx_lock(&g_eventlock);
	TAILQ_FOREACH(pp, &g_doorstep, orphan) {
		if (pp != ref)
			continue;
		TAILQ_REMOVE(&g_doorstep, pp, orphan);
		break;
	}
	TAILQ_FOREACH_SAFE(ep, &g_events, events, epn) {
		if (ep->flag & EV_INPROGRESS)
			continue;
		for (n = 0; n < G_N_EVENTREFS; n++) {
			if (ep->ref[n] == NULL)
				break;
			if (ep->ref[n] != ref)
				continue;
			TSRELEASE("GEOM events");
			TAILQ_REMOVE(&g_events, ep, events);
			ep->func(ep->arg, EV_CANCEL);
			mtx_assert(&g_eventlock, MA_OWNED);
			if (ep->flag & EV_WAKEUP) {
				ep->flag |= (EV_DONE|EV_CANCELED);
				wakeup(ep);
			} else {
				g_free(ep);
			}
			break;
		}
	}
	if (TAILQ_EMPTY(&g_events))
		wakeup(&g_pending_events);
	mtx_unlock(&g_eventlock);
}

static int
g_post_event_x(g_event_t *func, void *arg, int flag, int wuflag, struct g_event **epp, va_list ap)
{
	struct g_event *ep;
	void *p;
	u_int n;

	g_trace(G_T_TOPOLOGY, "g_post_event_x(%p, %p, %d, %d)",
	    func, arg, flag, wuflag);
	KASSERT(wuflag == 0 || wuflag == EV_WAKEUP,
	    ("Wrong wuflag in g_post_event_x(0x%x)", wuflag));
	ep = g_malloc(sizeof *ep, flag | M_ZERO);
	if (ep == NULL)
		return (ENOMEM);
	ep->flag = wuflag;
	for (n = 0; n < G_N_EVENTREFS; n++) {
		p = va_arg(ap, void *);
		if (p == NULL)
			break;
		g_trace(G_T_TOPOLOGY, "  ref %p", p);
		ep->ref[n] = p;
	}
	KASSERT(p == NULL, ("Too many references to event"));
	ep->func = func;
	ep->arg = arg;
	mtx_lock(&g_eventlock);
	TSHOLD("GEOM events");
	TAILQ_INSERT_TAIL(&g_events, ep, events);
	mtx_unlock(&g_eventlock);
	wakeup(&g_wait_event);
	if (epp != NULL)
		*epp = ep;
	curthread->td_pflags |= TDP_GEOM;
	return (0);
}

int
g_post_event(g_event_t *func, void *arg, int flag, ...)
{
	va_list ap;
	int i;

	KASSERT(flag == M_WAITOK || flag == M_NOWAIT,
	    ("Wrong flag to g_post_event"));
	va_start(ap, flag);
	i = g_post_event_x(func, arg, flag, 0, NULL, ap);
	va_end(ap);
	return (i);
}

void
g_do_wither()
{

	mtx_lock(&g_eventlock);
	g_wither_work = 1;
	mtx_unlock(&g_eventlock);
	wakeup(&g_wait_event);
}

/*
 * XXX: It might actually be useful to call this function with topology held.
 * XXX: This would ensure that the event gets created before anything else
 * XXX: changes.  At present all users have a handle on things in some other
 * XXX: way, so this remains an XXX for now.
 */

int
g_waitfor_event(g_event_t *func, void *arg, int flag, ...)
{
	va_list ap;
	struct g_event *ep;
	int error;

	g_topology_assert_not();
	KASSERT(flag == M_WAITOK || flag == M_NOWAIT,
	    ("Wrong flag to g_post_event"));
	va_start(ap, flag);
	error = g_post_event_x(func, arg, flag, EV_WAKEUP, &ep, ap);
	va_end(ap);
	if (error)
		return (error);

	mtx_lock(&g_eventlock);
	while (!(ep->flag & EV_DONE))
		msleep(ep, &g_eventlock, PRIBIO, "g_waitfor_event", hz);
	if (ep->flag & EV_CANCELED)
		error = EAGAIN;
	mtx_unlock(&g_eventlock);

	g_free(ep);
	return (error);
}

void
g_event_init()
{

	mtx_init(&g_eventlock, "GEOM orphanage", NULL, MTX_DEF);
}
