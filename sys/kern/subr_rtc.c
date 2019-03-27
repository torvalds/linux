/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1988 University of Utah.
 * Copyright (c) 1982, 1990, 1993
 *	The Regents of the University of California.
 * Copyright (c) 2011 The FreeBSD Foundation
 * All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * the Systems Programming Group of the University of Utah Computer
 * Science Department.
 *
 * Portions of this software were developed by Julien Ridoux at the University
 * of Melbourne under sponsorship from the FreeBSD Foundation.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	from: Utah $Hdr: clock.c 1.18 91/01/21$
 *	from: @(#)clock.c	8.2 (Berkeley) 1/12/94
 *	from: NetBSD: clock_subr.c,v 1.6 2001/07/07 17:04:02 thorpej Exp
 *	and
 *	from: src/sys/i386/isa/clock.c,v 1.176 2001/09/04
 */

/*
 * Helpers for time-of-day clocks. This is useful for architectures that need
 * support multiple models of such clocks, and generally serves to make the
 * code more machine-independent.
 * If the clock in question can also be used as a time counter, the driver
 * needs to initiate this.
 * This code is not yet used by all architectures.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_ffclock.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/bus.h>
#include <sys/clock.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/sx.h>
#include <sys/sysctl.h>
#include <sys/taskqueue.h>
#ifdef FFCLOCK
#include <sys/timeffc.h>
#endif
#include <sys/timetc.h>

#include "clock_if.h"

static int show_io;
SYSCTL_INT(_debug, OID_AUTO, clock_show_io, CTLFLAG_RWTUN, &show_io, 0,
    "Enable debug printing of RTC clock I/O; 1=reads, 2=writes, 3=both.");

static int sysctl_clock_do_io(SYSCTL_HANDLER_ARGS);
SYSCTL_PROC(_debug, OID_AUTO, clock_do_io, CTLTYPE_INT | CTLFLAG_RW,
    0, 0, sysctl_clock_do_io, "I",
    "Trigger one-time IO on RTC clocks; 1=read (and discard), 2=write");

/* XXX: should be kern. now, it's no longer machdep.  */
static int disable_rtc_set;
SYSCTL_INT(_machdep, OID_AUTO, disable_rtc_set, CTLFLAG_RW, &disable_rtc_set,
    0, "Disallow adjusting time-of-day clock");

/*
 * An instance of a realtime clock.  A list of these tracks all the registered
 * clocks in the system.
 *
 * The resadj member is used to apply a "resolution adjustment" equal to half
 * the clock's resolution, which is useful mainly on clocks with a whole-second
 * resolution.  Because the clock truncates the fractional part, adding half the
 * resolution performs 4/5 rounding.  The same adjustment is applied to the
 * times returned from clock_gettime(), because the fraction returned will
 * always be zero, but on average the actual fraction at the time of the call
 * should be about .5.
 */
struct rtc_instance {
	device_t	clockdev;
	int		resolution;
	int		flags;
	u_int		schedns;
	struct timespec resadj;
	struct timeout_task
			stask;
	LIST_ENTRY(rtc_instance)
			rtc_entries;
};

/*
 * Clocks are updated using a task running on taskqueue_thread.
 */
static void settime_task_func(void *arg, int pending);

/*
 * Registered clocks are kept in a list which is sorted by resolution; the more
 * accurate clocks get the first shot at providing the time.
 */
LIST_HEAD(rtc_listhead, rtc_instance);
static struct rtc_listhead rtc_list = LIST_HEAD_INITIALIZER(rtc_list);
static struct sx rtc_list_lock;
SX_SYSINIT(rtc_list_lock_init, &rtc_list_lock, "rtc list");

/*
 * On the task thread, invoke the clock_settime() method of the clock.  Do so
 * holding no locks, so that clock drivers are free to do whatever kind of
 * locking or sleeping they need to.
 */
static void
settime_task_func(void *arg, int pending)
{
	struct timespec ts;
	struct rtc_instance *rtc;
	int error;

	rtc = arg;
	if (!(rtc->flags & CLOCKF_SETTIME_NO_TS)) {
		getnanotime(&ts);
		if (!(rtc->flags & CLOCKF_SETTIME_NO_ADJ)) {
			ts.tv_sec -= utc_offset();
			timespecadd(&ts, &rtc->resadj, &ts);
		}
	} else {
		ts.tv_sec  = 0;
		ts.tv_nsec = 0;
	}
	error = CLOCK_SETTIME(rtc->clockdev, &ts);
	if (error != 0 && bootverbose)
		device_printf(rtc->clockdev, "CLOCK_SETTIME error %d\n", error);
}

static void
clock_dbgprint_hdr(device_t dev, int rw)
{
	struct timespec now;

	getnanotime(&now);
	device_printf(dev, "%s at ", (rw & CLOCK_DBG_READ) ? "read " : "write");
	clock_print_ts(&now, 9);
	printf(": "); 
}

void
clock_dbgprint_bcd(device_t dev, int rw, const struct bcd_clocktime *bct)
{

	if (show_io & rw) {
		clock_dbgprint_hdr(dev, rw);
		clock_print_bcd(bct, 9);
		printf("\n");
	}
}

void
clock_dbgprint_ct(device_t dev, int rw, const struct clocktime *ct)
{

	if (show_io & rw) {
		clock_dbgprint_hdr(dev, rw);
		clock_print_ct(ct, 9);
		printf("\n");
	}
}

void
clock_dbgprint_err(device_t dev, int rw, int err)
{

	if (show_io & rw) {
		clock_dbgprint_hdr(dev, rw);
		printf("error = %d\n", err);
	}
}

void
clock_dbgprint_ts(device_t dev, int rw, const struct timespec *ts)
{

	if (show_io & rw) {
		clock_dbgprint_hdr(dev, rw);
		clock_print_ts(ts, 9);
		printf("\n");
	}
}

void
clock_register_flags(device_t clockdev, long resolution, int flags)
{
	struct rtc_instance *rtc, *newrtc;

	newrtc = malloc(sizeof(*newrtc), M_DEVBUF, M_WAITOK);
	newrtc->clockdev = clockdev;
	newrtc->resolution = (int)resolution;
	newrtc->flags = flags;
	newrtc->schedns = 0;
	newrtc->resadj.tv_sec  = newrtc->resolution / 2 / 1000000;
	newrtc->resadj.tv_nsec = newrtc->resolution / 2 % 1000000 * 1000;
	TIMEOUT_TASK_INIT(taskqueue_thread, &newrtc->stask, 0,
		    settime_task_func, newrtc);

	sx_xlock(&rtc_list_lock);
	if (LIST_EMPTY(&rtc_list)) {
		LIST_INSERT_HEAD(&rtc_list, newrtc, rtc_entries);
	} else {
		LIST_FOREACH(rtc, &rtc_list, rtc_entries) {
			if (rtc->resolution > newrtc->resolution) {
				LIST_INSERT_BEFORE(rtc, newrtc, rtc_entries);
				break;
			} else if (LIST_NEXT(rtc, rtc_entries) == NULL) {
				LIST_INSERT_AFTER(rtc, newrtc, rtc_entries);
				break;
			}
		}
	}
	sx_xunlock(&rtc_list_lock);

	device_printf(clockdev, 
	    "registered as a time-of-day clock, resolution %d.%6.6ds\n",
	    newrtc->resolution / 1000000, newrtc->resolution % 1000000);
}

void
clock_register(device_t dev, long res)
{

	clock_register_flags(dev, res, 0);
}

void
clock_unregister(device_t clockdev)
{
	struct rtc_instance *rtc, *tmp;

	sx_xlock(&rtc_list_lock);
	LIST_FOREACH_SAFE(rtc, &rtc_list, rtc_entries, tmp) {
		if (rtc->clockdev == clockdev) {
			LIST_REMOVE(rtc, rtc_entries);
			break;
		}
	}
	sx_xunlock(&rtc_list_lock);
	if (rtc != NULL) {
		taskqueue_cancel_timeout(taskqueue_thread, &rtc->stask, NULL);
		taskqueue_drain_timeout(taskqueue_thread, &rtc->stask);
		free(rtc, M_DEVBUF);
	}
}

void
clock_schedule(device_t clockdev, u_int offsetns)
{
	struct rtc_instance *rtc;

	sx_xlock(&rtc_list_lock);
	LIST_FOREACH(rtc, &rtc_list, rtc_entries) {
		if (rtc->clockdev == clockdev) {
			rtc->schedns = offsetns;
			break;
		}
	}
	sx_xunlock(&rtc_list_lock);
}

static int
read_clocks(struct timespec *ts, bool debug_read)
{
	struct rtc_instance *rtc;
	int error;

	error = ENXIO;
	sx_xlock(&rtc_list_lock);
	LIST_FOREACH(rtc, &rtc_list, rtc_entries) {
		if ((error = CLOCK_GETTIME(rtc->clockdev, ts)) != 0)
			continue;
		if (ts->tv_sec < 0 || ts->tv_nsec < 0) {
			error = EINVAL;
			continue;
		}
		if (!(rtc->flags & CLOCKF_GETTIME_NO_ADJ)) {
			timespecadd(ts, &rtc->resadj, ts);
			ts->tv_sec += utc_offset();
		}
		if (!debug_read) {
			if (bootverbose)
				device_printf(rtc->clockdev,
				    "providing initial system time\n");
			break;
		}
	}
	sx_xunlock(&rtc_list_lock);
	return (error);
}

/*
 * Initialize the system time.  Must be called from a context which does not
 * restrict any locking or sleeping that clock drivers may need to do.
 *
 * First attempt to get the time from a registered realtime clock.  The clocks
 * are queried in order of resolution until one provides the time.  If no clock
 * can provide the current time, use the 'base' time provided by the caller, if
 * non-zero.  The 'base' time is potentially highly inaccurate, such as the last
 * known good value of the system clock, or even a filesystem last-updated
 * timestamp.  It is used to prevent system time from appearing to move
 * backwards in logs.
 */
void
inittodr(time_t base)
{
	struct timespec ts;
	int error;

	error = read_clocks(&ts, false);

	/*
	 * Do not report errors from each clock; it is expected that some clocks
	 * cannot provide results in some situations.  Only report problems when
	 * no clocks could provide the time.
	 */
	if (error != 0) {
		switch (error) {
		case ENXIO:
			printf("Warning: no time-of-day clock registered, ");
			break;
		case EINVAL:
			printf("Warning: bad time from time-of-day clock, ");
			break;
		default:
			printf("Error reading time-of-day clock (%d), ", error);
			break;
		}
		printf("system time will not be set accurately\n");
		ts.tv_sec  = (base > 0) ? base : -1;
		ts.tv_nsec = 0;
	}

	if (ts.tv_sec >= 0) {
		tc_setclock(&ts);
#ifdef FFCLOCK
		ffclock_reset_clock(&ts);
#endif
	}
}

/*
 * Write system time back to all registered clocks, unless disabled by admin.
 * This can be called from a context that restricts locking and/or sleeping; the
 * actual updating is done asynchronously on a task thread.
 */
void
resettodr(void)
{
	struct timespec now;
	struct rtc_instance *rtc;
	sbintime_t sbt;
	long waitns;

	if (disable_rtc_set)
		return;

	sx_xlock(&rtc_list_lock);
	LIST_FOREACH(rtc, &rtc_list, rtc_entries) {
		if (rtc->schedns != 0) {
			getnanotime(&now);
			waitns = rtc->schedns - now.tv_nsec;
			if (waitns < 0)
				waitns += 1000000000;
			sbt = nstosbt(waitns);
		} else
			sbt = 0;
		taskqueue_enqueue_timeout_sbt(taskqueue_thread,
		    &rtc->stask, -sbt, 0, C_PREL(31));
	}
	sx_xunlock(&rtc_list_lock);
}

static int
sysctl_clock_do_io(SYSCTL_HANDLER_ARGS)
{
	struct timespec ts_discard;
	int error, value;

	value = 0;
	error = sysctl_handle_int(oidp, &value, 0, req);
	if (error != 0 || req->newptr == NULL)
		return (error);

	switch (value) {
	case CLOCK_DBG_READ:
		if (read_clocks(&ts_discard, true) == ENXIO)
			printf("No registered RTC clocks\n");
		break;
	case CLOCK_DBG_WRITE:
		resettodr();
		break;
	default:
                return (EINVAL);
	}

	return (0);
}
