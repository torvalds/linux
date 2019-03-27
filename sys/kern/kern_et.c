/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2010-2013 Alexander Motin <mav@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer,
 *    without modification, immediately at the beginning of the file.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/sbuf.h>
#include <sys/sysctl.h>
#include <sys/systm.h>
#include <sys/queue.h>
#include <sys/timeet.h>

#include "opt_timer.h"

SLIST_HEAD(et_eventtimers_list, eventtimer);
static struct et_eventtimers_list eventtimers = SLIST_HEAD_INITIALIZER(et_eventtimers);

struct mtx	et_eventtimers_mtx;
MTX_SYSINIT(et_eventtimers_init, &et_eventtimers_mtx, "et_mtx", MTX_DEF);

SYSCTL_NODE(_kern, OID_AUTO, eventtimer, CTLFLAG_RW, 0, "Event timers");
static SYSCTL_NODE(_kern_eventtimer, OID_AUTO, et, CTLFLAG_RW, 0, "");

/*
 * Register a new event timer hardware.
 */
int
et_register(struct eventtimer *et)
{
	struct eventtimer *tmp, *next;

	if (et->et_quality >= 0 || bootverbose) {
		if (et->et_frequency == 0) {
			printf("Event timer \"%s\" quality %d\n",
			    et->et_name, et->et_quality);
		} else {
			printf("Event timer \"%s\" "
			    "frequency %ju Hz quality %d\n",
			    et->et_name, (uintmax_t)et->et_frequency,
			    et->et_quality);
		}
	}
	KASSERT(et->et_start, ("et_register: timer has no start function"));
	et->et_sysctl = SYSCTL_ADD_NODE_WITH_LABEL(NULL,
	    SYSCTL_STATIC_CHILDREN(_kern_eventtimer_et), OID_AUTO, et->et_name,
	    CTLFLAG_RW, 0, "event timer description", "eventtimer");
	SYSCTL_ADD_INT(NULL, SYSCTL_CHILDREN(et->et_sysctl), OID_AUTO,
	    "flags", CTLFLAG_RD, &(et->et_flags), 0,
	    "Event timer capabilities");
	SYSCTL_ADD_UQUAD(NULL, SYSCTL_CHILDREN(et->et_sysctl), OID_AUTO,
	    "frequency", CTLFLAG_RD, &(et->et_frequency),
	    "Event timer base frequency");
	SYSCTL_ADD_INT(NULL, SYSCTL_CHILDREN(et->et_sysctl), OID_AUTO,
	    "quality", CTLFLAG_RD, &(et->et_quality), 0,
	    "Goodness of event timer");
	ET_LOCK();
	if (SLIST_EMPTY(&eventtimers) ||
	    SLIST_FIRST(&eventtimers)->et_quality < et->et_quality) {
		SLIST_INSERT_HEAD(&eventtimers, et, et_all);
	} else {
		SLIST_FOREACH(tmp, &eventtimers, et_all) {
			next = SLIST_NEXT(tmp, et_all);
			if (next == NULL || next->et_quality < et->et_quality) {
				SLIST_INSERT_AFTER(tmp, et, et_all);
				break;
			}
		}
	}
	ET_UNLOCK();
	return (0);
}

/*
 * Deregister event timer hardware.
 */
int
et_deregister(struct eventtimer *et)
{
	int err = 0;

	if (et->et_deregister_cb != NULL) {
		if ((err = et->et_deregister_cb(et, et->et_arg)) != 0)
			return (err);
	}

	ET_LOCK();
	SLIST_REMOVE(&eventtimers, et, eventtimer, et_all);
	ET_UNLOCK();
	sysctl_remove_oid(et->et_sysctl, 1, 1);
	return (0);
}

/*
 * Change the frequency of the given timer.  If it is the active timer,
 * reconfigure it on all CPUs (reschedules all current events based on the new
 * timer frequency).
 */
void
et_change_frequency(struct eventtimer *et, uint64_t newfreq)
{

#ifndef NO_EVENTTIMERS
	cpu_et_frequency(et, newfreq);
#endif
}

/*
 * Find free event timer hardware with specified parameters.
 */
struct eventtimer *
et_find(const char *name, int check, int want)
{
	struct eventtimer *et = NULL;

	SLIST_FOREACH(et, &eventtimers, et_all) {
		if (et->et_active)
			continue;
		if (name != NULL && strcasecmp(et->et_name, name) != 0)
			continue;
		if (name == NULL && et->et_quality < 0)
			continue;
		if ((et->et_flags & check) != want)
			continue;
		break;
	}
	return (et);
}

/*
 * Initialize event timer hardware. Set callbacks.
 */
int
et_init(struct eventtimer *et, et_event_cb_t *event,
    et_deregister_cb_t *deregister, void *arg)
{

	if (event == NULL)
		return (EINVAL);
	if (et->et_active)
		return (EBUSY);

	et->et_active = 1;
	et->et_event_cb = event;
	et->et_deregister_cb = deregister;
	et->et_arg = arg;
	return (0);
}

/*
 * Start event timer hardware.
 * first - delay before first tick.
 * period - period of subsequent periodic ticks.
 */
int
et_start(struct eventtimer *et, sbintime_t first, sbintime_t period)
{

	if (!et->et_active)
		return (ENXIO);
	KASSERT(period >= 0, ("et_start: negative period"));
	KASSERT((et->et_flags & ET_FLAGS_PERIODIC) || period == 0,
		("et_start: period specified for oneshot-only timer"));
	KASSERT((et->et_flags & ET_FLAGS_ONESHOT) || period != 0,
		("et_start: period not specified for periodic-only timer"));
	if (period != 0) {
		if (period < et->et_min_period)
		        period = et->et_min_period;
		else if (period > et->et_max_period)
		        period = et->et_max_period;
	}
	if (period == 0 || first != 0) {
		if (first < et->et_min_period)
		        first = et->et_min_period;
		else if (first > et->et_max_period)
		        first = et->et_max_period;
	}
	return (et->et_start(et, first, period));
}

/* Stop event timer hardware. */
int
et_stop(struct eventtimer *et)
{

	if (!et->et_active)
		return (ENXIO);
	if (et->et_stop)
		return (et->et_stop(et));
	return (0);
}

/* Mark event timer hardware as broken. */
int
et_ban(struct eventtimer *et)
{

	et->et_flags &= ~(ET_FLAGS_PERIODIC | ET_FLAGS_ONESHOT);
	return (0);
}

/* Free event timer hardware. */
int
et_free(struct eventtimer *et)
{

	if (!et->et_active)
		return (ENXIO);

	et->et_active = 0;
	return (0);
}

/* Report list of supported event timer hardware via sysctl. */
static int
sysctl_kern_eventtimer_choice(SYSCTL_HANDLER_ARGS)
{
	struct sbuf sb;
	struct eventtimer *et;
	int error;

	sbuf_new(&sb, NULL, 256, SBUF_AUTOEXTEND | SBUF_INCLUDENUL);

	ET_LOCK();
	SLIST_FOREACH(et, &eventtimers, et_all) {
		if (et != SLIST_FIRST(&eventtimers))
			sbuf_putc(&sb, ' ');
		sbuf_printf(&sb, "%s(%d)", et->et_name, et->et_quality);
	}
	ET_UNLOCK();

	error = sbuf_finish(&sb);
	if (error == 0)
		error = SYSCTL_OUT(req, sbuf_data(&sb), sbuf_len(&sb));
	sbuf_delete(&sb);
	return (error);
}
SYSCTL_PROC(_kern_eventtimer, OID_AUTO, choice,
    CTLTYPE_STRING | CTLFLAG_RD | CTLFLAG_MPSAFE,
    0, 0, sysctl_kern_eventtimer_choice, "A", "Present event timers");

