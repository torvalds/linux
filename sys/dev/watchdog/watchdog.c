/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2004 Poul-Henning Kamp
 * Copyright (c) 2013 iXsystems.com,
 *               author: Alfred Perlstein <alfred@freebsd.org>
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer
 *    in this position and unchanged.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include "opt_ddb.h"

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/types.h>
#include <sys/systm.h>
#include <sys/conf.h>
#include <sys/uio.h>
#include <sys/kernel.h>
#include <sys/kdb.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/sysctl.h>
#include <sys/syslog.h>
#include <sys/watchdog.h>
#include <sys/bus.h>
#include <machine/bus.h>

#include <sys/syscallsubr.h> /* kern_clock_gettime() */

static int wd_set_pretimeout(int newtimeout, int disableiftoolong);
static void wd_timeout_cb(void *arg);

static struct callout wd_pretimeo_handle;
static int wd_pretimeout;
static int wd_pretimeout_act = WD_SOFT_LOG;

static struct callout wd_softtimeo_handle;
static int wd_softtimer;	/* true = use softtimer instead of hardware
				   watchdog */
static int wd_softtimeout_act = WD_SOFT_LOG;	/* action for the software timeout */

static struct cdev *wd_dev;
static volatile u_int wd_last_u;    /* last timeout value set by kern_do_pat */
static u_int wd_last_u_sysctl;    /* last timeout value set by kern_do_pat */
static u_int wd_last_u_sysctl_secs;    /* wd_last_u in seconds */

SYSCTL_NODE(_hw, OID_AUTO, watchdog, CTLFLAG_RD, 0, "Main watchdog device");
SYSCTL_UINT(_hw_watchdog, OID_AUTO, wd_last_u, CTLFLAG_RD,
    &wd_last_u_sysctl, 0, "Watchdog last update time");
SYSCTL_UINT(_hw_watchdog, OID_AUTO, wd_last_u_secs, CTLFLAG_RD,
    &wd_last_u_sysctl_secs, 0, "Watchdog last update time");

static int wd_lastpat_valid = 0;
static time_t wd_lastpat = 0;	/* when the watchdog was last patted */

/* Hook for external software watchdog to register for use if needed */
void (*wdog_software_attach)(void);

static void
pow2ns_to_ts(int pow2ns, struct timespec *ts)
{
	uint64_t ns;

	ns = 1ULL << pow2ns;
	ts->tv_sec = ns / 1000000000ULL;
	ts->tv_nsec = ns % 1000000000ULL;
}

static int
pow2ns_to_ticks(int pow2ns)
{
	struct timeval tv;
	struct timespec ts;

	pow2ns_to_ts(pow2ns, &ts);
	TIMESPEC_TO_TIMEVAL(&tv, &ts);
	return (tvtohz(&tv));
}

static int
seconds_to_pow2ns(int seconds)
{
	uint64_t power;
	uint64_t ns;
	uint64_t shifted;

	ns = ((uint64_t)seconds) * 1000000000ULL;
	power = flsll(ns);
	shifted = 1ULL << power;
	if (shifted <= ns) {
		power++;
	}
	return (power);
}


int
wdog_kern_pat(u_int utim)
{
	int error;
	static int first = 1;

	if ((utim & WD_LASTVAL) != 0 && (utim & WD_INTERVAL) > 0)
		return (EINVAL);

	if ((utim & WD_LASTVAL) != 0) {
		/*
		 * if WD_LASTVAL is set, fill in the bits for timeout
		 * from the saved value in wd_last_u.
		 */
		MPASS((wd_last_u & ~WD_INTERVAL) == 0);
		utim &= ~WD_LASTVAL;
		utim |= wd_last_u;
	} else {
		/*
		 * Otherwise save the new interval.
		 * This can be zero (to disable the watchdog)
		 */
		wd_last_u = (utim & WD_INTERVAL);
		wd_last_u_sysctl = wd_last_u;
		wd_last_u_sysctl_secs = pow2ns_to_ticks(wd_last_u) / hz;
	}
	if ((utim & WD_INTERVAL) == WD_TO_NEVER) {
		utim = 0;

		/* Assume all is well; watchdog signals failure. */
		error = 0;
	} else {
		/* Assume no watchdog available; watchdog flags success */
		error = EOPNOTSUPP;
	}
	if (wd_softtimer) {
		if (utim == 0) {
			callout_stop(&wd_softtimeo_handle);
		} else {
			(void) callout_reset(&wd_softtimeo_handle,
			    pow2ns_to_ticks(utim), wd_timeout_cb, "soft");
		}
		error = 0;
	} else {
		EVENTHANDLER_INVOKE(watchdog_list, utim, &error);
	}
	/*
	 * If we no hardware watchdog responded, we have not tried to
	 * attach an external software watchdog, and one is available,
	 * attach it now and retry.
	 */
	if (error == EOPNOTSUPP && first && *wdog_software_attach != NULL) {
		(*wdog_software_attach)();
		EVENTHANDLER_INVOKE(watchdog_list, utim, &error);
	}
	first = 0;

	wd_set_pretimeout(wd_pretimeout, true);
	/*
	 * If we were able to arm/strobe the watchdog, then
	 * update the last time it was strobed for WDIOC_GETTIMELEFT
	 */
	if (!error) {
		struct timespec ts;

		error = kern_clock_gettime(curthread /* XXX */,
		    CLOCK_MONOTONIC_FAST, &ts);
		if (!error) {
			wd_lastpat = ts.tv_sec;
			wd_lastpat_valid = 1;
		}
	}
	return (error);
}

static int
wd_valid_act(int act)
{

	if ((act & ~(WD_SOFT_MASK)) != 0)
		return false;
	return true;
}

static int
wd_ioctl_patpat(caddr_t data)
{
	u_int u;

	u = *(u_int *)data;
	if (u & ~(WD_ACTIVE | WD_PASSIVE | WD_LASTVAL | WD_INTERVAL))
		return (EINVAL);
	if ((u & (WD_ACTIVE | WD_PASSIVE)) == (WD_ACTIVE | WD_PASSIVE))
		return (EINVAL);
	if ((u & (WD_ACTIVE | WD_PASSIVE)) == 0 && ((u & WD_INTERVAL) > 0 ||
	    (u & WD_LASTVAL) != 0))
		return (EINVAL);
	if (u & WD_PASSIVE)
		return (ENOSYS);	/* XXX Not implemented yet */
	u &= ~(WD_ACTIVE | WD_PASSIVE);

	return (wdog_kern_pat(u));
}

static int
wd_get_time_left(struct thread *td, time_t *remainp)
{
	struct timespec ts;
	int error;

	error = kern_clock_gettime(td, CLOCK_MONOTONIC_FAST, &ts);
	if (error)
		return (error);
	if (!wd_lastpat_valid)
		return (ENOENT);
	*remainp = ts.tv_sec - wd_lastpat;
	return (0);
}

static void
wd_timeout_cb(void *arg)
{
	const char *type = arg;

#ifdef DDB
	if ((wd_pretimeout_act & WD_SOFT_DDB)) {
		char kdb_why[80];
		snprintf(kdb_why, sizeof(kdb_why), "watchdog %s-timeout", type);
		kdb_backtrace();
		kdb_enter(KDB_WHY_WATCHDOG, kdb_why);
	}
#endif
	if ((wd_pretimeout_act & WD_SOFT_LOG))
		log(LOG_EMERG, "watchdog %s-timeout, WD_SOFT_LOG\n", type);
	if ((wd_pretimeout_act & WD_SOFT_PRINTF))
		printf("watchdog %s-timeout, WD_SOFT_PRINTF\n", type);
	if ((wd_pretimeout_act & WD_SOFT_PANIC))
		panic("watchdog %s-timeout, WD_SOFT_PANIC set", type);
}

/*
 * Called to manage timeouts.
 * newtimeout needs to be in the range of 0 to actual watchdog timeout.
 * if 0, we disable the pre-timeout.
 * otherwise we set the pre-timeout provided it's not greater than the
 * current actual watchdog timeout.
 */
static int
wd_set_pretimeout(int newtimeout, int disableiftoolong)
{
	u_int utime;
	struct timespec utime_ts;
	int timeout_ticks;

	utime = wdog_kern_last_timeout();
	pow2ns_to_ts(utime, &utime_ts);
	/* do not permit a pre-timeout >= than the timeout. */
	if (newtimeout >= utime_ts.tv_sec) {
		/*
		 * If 'disableiftoolong' then just fall through
		 * so as to disable the pre-watchdog
		 */
		if (disableiftoolong)
			newtimeout = 0;
		else
			return EINVAL;
	}

	/* disable the pre-timeout */
	if (newtimeout == 0) {
		wd_pretimeout = 0;
		callout_stop(&wd_pretimeo_handle);
		return 0;
	}

	timeout_ticks = pow2ns_to_ticks(utime) - (hz*newtimeout);
#if 0
	printf("wd_set_pretimeout: "
	    "newtimeout: %d, "
	    "utime: %d -> utime_ticks: %d, "
	    "hz*newtimeout: %d, "
	    "timeout_ticks: %d -> sec: %d\n",
	    newtimeout,
	    utime, pow2ns_to_ticks(utime),
	    hz*newtimeout,
	    timeout_ticks, timeout_ticks / hz);
#endif

	/* We determined the value is sane, so reset the callout */
	(void) callout_reset(&wd_pretimeo_handle,
	    timeout_ticks, wd_timeout_cb, "pre");
	wd_pretimeout = newtimeout;
	return 0;
}

static int
wd_ioctl(struct cdev *dev __unused, u_long cmd, caddr_t data,
    int flags __unused, struct thread *td)
{
	u_int u;
	time_t timeleft;
	int error;

	error = 0;

	switch (cmd) {
	case WDIOC_SETSOFT:
		u = *(int *)data;
		/* do nothing? */
		if (u == wd_softtimer)
			break;
		/* If there is a pending timeout disallow this ioctl */
		if (wd_last_u != 0) {
			error = EINVAL;
			break;
		}
		wd_softtimer = u;
		break;
	case WDIOC_SETSOFTTIMEOUTACT:
		u = *(int *)data;
		if (wd_valid_act(u)) {
			wd_softtimeout_act = u;
		} else {
			error = EINVAL;
		}
		break;
	case WDIOC_SETPRETIMEOUTACT:
		u = *(int *)data;
		if (wd_valid_act(u)) {
			wd_pretimeout_act = u;
		} else {
			error = EINVAL;
		}
		break;
	case WDIOC_GETPRETIMEOUT:
		*(int *)data = (int)wd_pretimeout;
		break;
	case WDIOC_SETPRETIMEOUT:
		error = wd_set_pretimeout(*(int *)data, false);
		break;
	case WDIOC_GETTIMELEFT:
		error = wd_get_time_left(td, &timeleft);
		if (error)
			break;
		*(int *)data = (int)timeleft;
		break;
	case WDIOC_SETTIMEOUT:
		u = *(u_int *)data;
		error = wdog_kern_pat(seconds_to_pow2ns(u));
		break;
	case WDIOC_GETTIMEOUT:
		u = wdog_kern_last_timeout();
		*(u_int *)data = u;
		break;
	case WDIOCPATPAT:
		error = wd_ioctl_patpat(data);
		break;
	default:
		error = ENOIOCTL;
		break;
	}
	return (error);
}

/*
 * Return the last timeout set, this is NOT the seconds from NOW until timeout,
 * rather it is the amount of seconds passed to WDIOCPATPAT/WDIOC_SETTIMEOUT.
 */
u_int
wdog_kern_last_timeout(void)
{

	return (wd_last_u);
}

static struct cdevsw wd_cdevsw = {
	.d_version =	D_VERSION,
	.d_ioctl =	wd_ioctl,
	.d_name =	"watchdog",
};

static int
watchdog_modevent(module_t mod __unused, int type, void *data __unused)
{
	switch(type) {
	case MOD_LOAD:
		callout_init(&wd_pretimeo_handle, 1);
		callout_init(&wd_softtimeo_handle, 1);
		wd_dev = make_dev(&wd_cdevsw, 0,
		    UID_ROOT, GID_WHEEL, 0600, _PATH_WATCHDOG);
		return 0;
	case MOD_UNLOAD:
		callout_stop(&wd_pretimeo_handle);
		callout_stop(&wd_softtimeo_handle);
		callout_drain(&wd_pretimeo_handle);
		callout_drain(&wd_softtimeo_handle);
		destroy_dev(wd_dev);
		return 0;
	case MOD_SHUTDOWN:
		return 0;
	default:
		return EOPNOTSUPP;
	}
}

DEV_MODULE(watchdog, watchdog_modevent, NULL);
