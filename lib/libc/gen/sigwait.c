/*	$OpenBSD: sigwait.c,v 1.1 2019/01/12 00:16:03 jca Exp $ */
/*
 * Copyright (c) 2005 Ted Unangst <tedu@openbsd.org>
 * All Rights Reserved.
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
/*
 * signals
 */

#include <signal.h>
#include <errno.h>

#include <pthread.h>

#include "thread/rthread.h"
#include "cancel.h"		/* in libc/include */

int
sigwait(const sigset_t *set, int *sig)
{
	sigset_t s = *set;
	int ret;

	sigdelset(&s, SIGTHR);
	do {
		ENTER_CANCEL_POINT(1);
		ret = __thrsigdivert(s, NULL, NULL);
		LEAVE_CANCEL_POINT(ret == -1);
	} while (ret == -1 && errno == EINTR);
	if (ret == -1)
		return (errno);
	*sig = ret;
	return (0);
}

#if 0		/* need kernel to fill in more siginfo_t bits first */
int
sigwaitinfo(const sigset_t *set, siginfo_t *info)
{
	sigset_t s = *set;
	int ret;

	sigdelset(&s, SIGTHR);
	ENTER_CANCEL_POINT(1);
	ret = __thrsigdivert(s, info, NULL);
	LEAVE_CANCEL_POINT(ret == -1);
	return (ret);
}

int
sigtimedwait(const sigset_t *set, siginfo_t *info,
    const struct timespec *timeout)
{
	sigset_t s = *set;
	int ret;

	sigdelset(&s, SIGTHR);
	ENTER_CANCEL_POINT(1);
	ret = __thrsigdivert(s, info, timeout);
	LEAVE_CANCEL_POINT(ret == -1);
	return (ret);
}
#endif
