/*	$OpenBSD: w_ppoll.c,v 1.1 2016/05/07 19:05:22 guenther Exp $ */
/*
 * Copyright (c) 2015 Philip Guenther <guenther@openbsd.org>
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

#include <poll.h>
#include <signal.h>
#include "cancel.h"

int
ppoll(struct pollfd *fds, nfds_t nfds,
    const struct timespec *timeout, const sigset_t *sigmask)
{
	sigset_t set;
	int ret;

	if (sigmask != NULL && sigismember(sigmask, SIGTHR)) {
		set = *sigmask;
		sigdelset(&set, SIGTHR);
		sigmask = &set;
	}

	ENTER_CANCEL_POINT(1);
	ret = HIDDEN(ppoll)(fds, nfds, timeout, sigmask);
	LEAVE_CANCEL_POINT(ret == -1);

	return (ret);
}
DEF_CANCEL(ppoll);
