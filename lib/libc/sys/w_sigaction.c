/*	$OpenBSD: w_sigaction.c,v 1.1 2015/10/23 04:39:24 guenther Exp $ */
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

int
WRAP(sigaction)(int sig, const struct sigaction *act, struct sigaction *oact)
{
	struct sigaction sa;

	if (sig == SIGTHR) {
		errno = EINVAL;
		return (-1);
	}
	if (act != NULL && sigismember(&act->sa_mask, SIGTHR)) {
		sa = *act;
		sigdelset(&sa.sa_mask, SIGTHR);
		act = &sa;
	}
	return (sigaction(sig, act, oact));
}
DEF_WRAP(sigaction);
