/*      $OpenBSD: kern_watchdog.c,v 1.16 2022/08/14 01:58:27 jsg Exp $        */

/*
 * Copyright (c) 2003 Markus Friedl.  All rights reserved.
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
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/timeout.h>
#include <sys/sysctl.h>

void	wdog_tickle(void *arg);
int	(*wdog_ctl_cb)(void *, int) = NULL;
void	*wdog_ctl_cb_arg = NULL;
int	wdog_period = 0;
int	wdog_auto = 1;
struct	timeout wdog_timeout;

void
wdog_register(int (*cb)(void *, int), void *cb_arg)
{
	if (wdog_ctl_cb != NULL)
		return;

	wdog_ctl_cb = cb;
	wdog_ctl_cb_arg = cb_arg;
	timeout_set(&wdog_timeout, wdog_tickle, NULL);
}

void
wdog_tickle(void *arg)
{
	if (wdog_ctl_cb == NULL)
		return;
	(void) (*wdog_ctl_cb)(wdog_ctl_cb_arg, wdog_period);
	timeout_add_msec(&wdog_timeout, wdog_period * 1000 / 2);
}

void
wdog_shutdown(void *arg)
{
	if (wdog_ctl_cb == NULL || wdog_ctl_cb_arg != arg)
		return;
	timeout_del(&wdog_timeout);
	(void) (*wdog_ctl_cb)(wdog_ctl_cb_arg, 0);
	wdog_ctl_cb = NULL;
	wdog_period = 0;
	wdog_auto = 1;
}

int
sysctl_wdog(int *name, u_int namelen, void *oldp, size_t *oldlenp, void *newp,
    size_t newlen)
{
	int error, period;

	if (wdog_ctl_cb == NULL)
		return (EOPNOTSUPP);

	switch (name[0]) {
	case KERN_WATCHDOG_PERIOD:
		period = wdog_period;
		error = sysctl_int_bounded(oldp, oldlenp, newp, newlen,
		    &period, 0, INT_MAX);
		if (error)
			return (error);
		if (newp) {
			timeout_del(&wdog_timeout);
			wdog_period = (*wdog_ctl_cb)(wdog_ctl_cb_arg, period);
		}
		break;
	case KERN_WATCHDOG_AUTO:
		error = sysctl_int_bounded(oldp, oldlenp, newp, newlen,
		    &wdog_auto, 0, 1);
		if (error)
			return (error);
		break;
	default:
		return (EINVAL);
	}

	if (wdog_auto && wdog_period > 0) {
		(void) (*wdog_ctl_cb)(wdog_ctl_cb_arg, wdog_period);
		timeout_add_msec(&wdog_timeout, wdog_period * 1000 / 2);
	} else
		timeout_del(&wdog_timeout);

	return (error);
}
