/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2011 The University of Melbourne
 * All rights reserved.
 *
 * This software was developed by Julien Ridoux at the University of Melbourne
 * under sponsorship from the FreeBSD Foundation.
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_ffclock.h"

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/module.h>
#include <sys/mutex.h>
#include <sys/priv.h>
#include <sys/proc.h>
#include <sys/sbuf.h>
#include <sys/sysent.h>
#include <sys/sysproto.h>
#include <sys/sysctl.h>
#include <sys/systm.h>
#include <sys/timeffc.h>

#ifdef FFCLOCK

FEATURE(ffclock, "Feed-forward clock support");

extern struct ffclock_estimate ffclock_estimate;
extern struct bintime ffclock_boottime;
extern int8_t ffclock_updated;
extern struct mtx ffclock_mtx;

/*
 * Feed-forward clock absolute time. This should be the preferred way to read
 * the feed-forward clock for "wall-clock" type time. The flags allow to compose
 * various flavours of absolute time (e.g. with or without leap seconds taken
 * into account). If valid pointers are provided, the ffcounter value and an
 * upper bound on clock error associated with the bintime are provided.
 * NOTE: use ffclock_convert_abs() to differ the conversion of a ffcounter value
 * read earlier.
 */
void
ffclock_abstime(ffcounter *ffcount, struct bintime *bt,
    struct bintime *error_bound, uint32_t flags)
{
	struct ffclock_estimate cest;
	ffcounter ffc;
	ffcounter update_ffcount;
	ffcounter ffdelta_error;

	/* Get counter and corresponding time. */
	if ((flags & FFCLOCK_FAST) == FFCLOCK_FAST)
		ffclock_last_tick(&ffc, bt, flags);
	else {
		ffclock_read_counter(&ffc);
		ffclock_convert_abs(ffc, bt, flags);
	}

	/* Current ffclock estimate, use update_ffcount as generation number. */
	do {
		update_ffcount = ffclock_estimate.update_ffcount;
		bcopy(&ffclock_estimate, &cest, sizeof(struct ffclock_estimate));
	} while (update_ffcount != ffclock_estimate.update_ffcount);

	/*
	 * Leap second adjustment. Total as seen by synchronisation algorithm
	 * since it started. cest.leapsec_next is the ffcounter prediction of
	 * when the next leapsecond occurs.
	 */
	if ((flags & FFCLOCK_LEAPSEC) == FFCLOCK_LEAPSEC) {
		bt->sec -= cest.leapsec_total;
		if (ffc > cest.leapsec_next)
			bt->sec -= cest.leapsec;
	}

	/* Boot time adjustment, for uptime/monotonic clocks. */
	if ((flags & FFCLOCK_UPTIME) == FFCLOCK_UPTIME) {
		bintime_sub(bt, &ffclock_boottime);
	}

	/* Compute error bound if a valid pointer has been passed. */
	if (error_bound) {
		ffdelta_error = ffc - cest.update_ffcount;
		ffclock_convert_diff(ffdelta_error, error_bound);
		/* 18446744073709 = int(2^64/1e12), err_bound_rate in [ps/s] */
		bintime_mul(error_bound, cest.errb_rate *
		    (uint64_t)18446744073709LL);
		/* 18446744073 = int(2^64 / 1e9), since err_abs in [ns] */
		bintime_addx(error_bound, cest.errb_abs *
		    (uint64_t)18446744073LL);
	}

	if (ffcount)
		*ffcount = ffc;
}

/*
 * Feed-forward difference clock. This should be the preferred way to convert a
 * time interval in ffcounter values into a time interval in seconds. If a valid
 * pointer is passed, an upper bound on the error in computing the time interval
 * in seconds is provided.
 */
void
ffclock_difftime(ffcounter ffdelta, struct bintime *bt,
    struct bintime *error_bound)
{
	ffcounter update_ffcount;
	uint32_t err_rate;

	ffclock_convert_diff(ffdelta, bt);

	if (error_bound) {
		do {
			update_ffcount = ffclock_estimate.update_ffcount;
			err_rate = ffclock_estimate.errb_rate;
		} while (update_ffcount != ffclock_estimate.update_ffcount);

		ffclock_convert_diff(ffdelta, error_bound);
		/* 18446744073709 = int(2^64/1e12), err_bound_rate in [ps/s] */
		bintime_mul(error_bound, err_rate * (uint64_t)18446744073709LL);
	}
}

/*
 * Create a new kern.sysclock sysctl node, which will be home to some generic
 * sysclock configuration variables. Feed-forward clock specific variables will
 * live under the ffclock subnode.
 */

SYSCTL_NODE(_kern, OID_AUTO, sysclock, CTLFLAG_RW, 0,
    "System clock related configuration");
SYSCTL_NODE(_kern_sysclock, OID_AUTO, ffclock, CTLFLAG_RW, 0,
    "Feed-forward clock configuration");

static char *sysclocks[] = {"feedback", "feed-forward"};
#define	MAX_SYSCLOCK_NAME_LEN 16
#define	NUM_SYSCLOCKS nitems(sysclocks)

static int ffclock_version = 2;
SYSCTL_INT(_kern_sysclock_ffclock, OID_AUTO, version, CTLFLAG_RD,
    &ffclock_version, 0, "Feed-forward clock kernel version");

/* List available sysclocks. */
static int
sysctl_kern_sysclock_available(SYSCTL_HANDLER_ARGS)
{
	struct sbuf *s;
	int clk, error;

	s = sbuf_new_for_sysctl(NULL, NULL,
	    MAX_SYSCLOCK_NAME_LEN * NUM_SYSCLOCKS, req);
	if (s == NULL)
		return (ENOMEM);

	for (clk = 0; clk < NUM_SYSCLOCKS; clk++) {
		sbuf_cat(s, sysclocks[clk]);
		if (clk + 1 < NUM_SYSCLOCKS)
			sbuf_cat(s, " ");
	}
	error = sbuf_finish(s);
	sbuf_delete(s);

	return (error);
}

SYSCTL_PROC(_kern_sysclock, OID_AUTO, available, CTLTYPE_STRING | CTLFLAG_RD,
    0, 0, sysctl_kern_sysclock_available, "A",
    "List of available system clocks");

/*
 * Return the name of the active system clock if read, or attempt to change
 * the active system clock to the user specified one if written to. The active
 * system clock is read when calling any of the [get]{bin,nano,micro}[up]time()
 * functions.
 */
static int
sysctl_kern_sysclock_active(SYSCTL_HANDLER_ARGS)
{
	char newclock[MAX_SYSCLOCK_NAME_LEN];
	int error;
	int clk;

	/* Return the name of the current active sysclock. */
	strlcpy(newclock, sysclocks[sysclock_active], sizeof(newclock));
	error = sysctl_handle_string(oidp, newclock, sizeof(newclock), req);

	/* Check for error or no change */
	if (error != 0 || req->newptr == NULL)
		goto done;

	/* Change the active sysclock to the user specified one: */
	error = EINVAL;
	for (clk = 0; clk < NUM_SYSCLOCKS; clk++) {
		if (strncmp(newclock, sysclocks[clk],
		    MAX_SYSCLOCK_NAME_LEN - 1)) {
			continue;
		}
		sysclock_active = clk;
		error = 0;
		break;
	}
done:
	return (error);
}

SYSCTL_PROC(_kern_sysclock, OID_AUTO, active, CTLTYPE_STRING | CTLFLAG_RW,
    0, 0, sysctl_kern_sysclock_active, "A",
    "Name of the active system clock which is currently serving time");

static int sysctl_kern_ffclock_ffcounter_bypass = 0;
SYSCTL_INT(_kern_sysclock_ffclock, OID_AUTO, ffcounter_bypass, CTLFLAG_RW,
    &sysctl_kern_ffclock_ffcounter_bypass, 0,
    "Use reliable hardware timecounter as the feed-forward counter");

/*
 * High level functions to access the Feed-Forward Clock.
 */
void
ffclock_bintime(struct bintime *bt)
{

	ffclock_abstime(NULL, bt, NULL, FFCLOCK_LERP | FFCLOCK_LEAPSEC);
}

void
ffclock_nanotime(struct timespec *tsp)
{
	struct bintime bt;

	ffclock_abstime(NULL, &bt, NULL, FFCLOCK_LERP | FFCLOCK_LEAPSEC);
	bintime2timespec(&bt, tsp);
}

void
ffclock_microtime(struct timeval *tvp)
{
	struct bintime bt;

	ffclock_abstime(NULL, &bt, NULL, FFCLOCK_LERP | FFCLOCK_LEAPSEC);
	bintime2timeval(&bt, tvp);
}

void
ffclock_getbintime(struct bintime *bt)
{

	ffclock_abstime(NULL, bt, NULL,
	    FFCLOCK_LERP | FFCLOCK_LEAPSEC | FFCLOCK_FAST);
}

void
ffclock_getnanotime(struct timespec *tsp)
{
	struct bintime bt;

	ffclock_abstime(NULL, &bt, NULL,
	    FFCLOCK_LERP | FFCLOCK_LEAPSEC | FFCLOCK_FAST);
	bintime2timespec(&bt, tsp);
}

void
ffclock_getmicrotime(struct timeval *tvp)
{
	struct bintime bt;

	ffclock_abstime(NULL, &bt, NULL,
	    FFCLOCK_LERP | FFCLOCK_LEAPSEC | FFCLOCK_FAST);
	bintime2timeval(&bt, tvp);
}

void
ffclock_binuptime(struct bintime *bt)
{

	ffclock_abstime(NULL, bt, NULL, FFCLOCK_LERP | FFCLOCK_UPTIME);
}

void
ffclock_nanouptime(struct timespec *tsp)
{
	struct bintime bt;

	ffclock_abstime(NULL, &bt, NULL, FFCLOCK_LERP | FFCLOCK_UPTIME);
	bintime2timespec(&bt, tsp);
}

void
ffclock_microuptime(struct timeval *tvp)
{
	struct bintime bt;

	ffclock_abstime(NULL, &bt, NULL, FFCLOCK_LERP | FFCLOCK_UPTIME);
	bintime2timeval(&bt, tvp);
}

void
ffclock_getbinuptime(struct bintime *bt)
{

	ffclock_abstime(NULL, bt, NULL,
	    FFCLOCK_LERP | FFCLOCK_UPTIME | FFCLOCK_FAST);
}

void
ffclock_getnanouptime(struct timespec *tsp)
{
	struct bintime bt;

	ffclock_abstime(NULL, &bt, NULL,
	    FFCLOCK_LERP | FFCLOCK_UPTIME | FFCLOCK_FAST);
	bintime2timespec(&bt, tsp);
}

void
ffclock_getmicrouptime(struct timeval *tvp)
{
	struct bintime bt;

	ffclock_abstime(NULL, &bt, NULL,
	    FFCLOCK_LERP | FFCLOCK_UPTIME | FFCLOCK_FAST);
	bintime2timeval(&bt, tvp);
}

void
ffclock_bindifftime(ffcounter ffdelta, struct bintime *bt)
{

	ffclock_difftime(ffdelta, bt, NULL);
}

void
ffclock_nanodifftime(ffcounter ffdelta, struct timespec *tsp)
{
	struct bintime bt;

	ffclock_difftime(ffdelta, &bt, NULL);
	bintime2timespec(&bt, tsp);
}

void
ffclock_microdifftime(ffcounter ffdelta, struct timeval *tvp)
{
	struct bintime bt;

	ffclock_difftime(ffdelta, &bt, NULL);
	bintime2timeval(&bt, tvp);
}

/*
 * System call allowing userland applications to retrieve the current value of
 * the Feed-Forward Clock counter.
 */
#ifndef _SYS_SYSPROTO_H_
struct ffclock_getcounter_args {
	ffcounter *ffcount;
};
#endif
/* ARGSUSED */
int
sys_ffclock_getcounter(struct thread *td, struct ffclock_getcounter_args *uap)
{
	ffcounter ffcount;
	int error;

	ffcount = 0;
	ffclock_read_counter(&ffcount);
	if (ffcount == 0)
		return (EAGAIN);
	error = copyout(&ffcount, uap->ffcount, sizeof(ffcounter));

	return (error);
}

/*
 * System call allowing the synchronisation daemon to push new feed-foward clock
 * estimates to the kernel. Acquire ffclock_mtx to prevent concurrent updates
 * and ensure data consistency.
 * NOTE: ffclock_updated signals the fftimehands that new estimates are
 * available. The updated estimates are picked up by the fftimehands on next
 * tick, which could take as long as 1/hz seconds (if ticks are not missed).
 */
#ifndef _SYS_SYSPROTO_H_
struct ffclock_setestimate_args {
	struct ffclock_estimate *cest;
};
#endif
/* ARGSUSED */
int
sys_ffclock_setestimate(struct thread *td, struct ffclock_setestimate_args *uap)
{
	struct ffclock_estimate cest;
	int error;

	/* Reuse of PRIV_CLOCK_SETTIME. */
	if ((error = priv_check(td, PRIV_CLOCK_SETTIME)) != 0)
		return (error);

	if ((error = copyin(uap->cest, &cest, sizeof(struct ffclock_estimate)))
	    != 0)
		return (error);

	mtx_lock(&ffclock_mtx);
	memcpy(&ffclock_estimate, &cest, sizeof(struct ffclock_estimate));
	ffclock_updated++;
	mtx_unlock(&ffclock_mtx);
	return (error);
}

/*
 * System call allowing userland applications to retrieve the clock estimates
 * stored within the kernel. It is useful to kickstart the synchronisation
 * daemon with the kernel's knowledge of hardware timecounter.
 */
#ifndef _SYS_SYSPROTO_H_
struct ffclock_getestimate_args {
	struct ffclock_estimate *cest;
};
#endif
/* ARGSUSED */
int
sys_ffclock_getestimate(struct thread *td, struct ffclock_getestimate_args *uap)
{
	struct ffclock_estimate cest;
	int error;

	mtx_lock(&ffclock_mtx);
	memcpy(&cest, &ffclock_estimate, sizeof(struct ffclock_estimate));
	mtx_unlock(&ffclock_mtx);
	error = copyout(&cest, uap->cest, sizeof(struct ffclock_estimate));
	return (error);
}

#else /* !FFCLOCK */

int
sys_ffclock_getcounter(struct thread *td, struct ffclock_getcounter_args *uap)
{

	return (ENOSYS);
}

int
sys_ffclock_setestimate(struct thread *td, struct ffclock_setestimate_args *uap)
{

	return (ENOSYS);
}

int
sys_ffclock_getestimate(struct thread *td, struct ffclock_getestimate_args *uap)
{

	return (ENOSYS);
}

#endif /* FFCLOCK */
