/*	$OpenBSD: w_sigprocmask.c,v 1.1 2015/10/23 04:39:24 guenther Exp $ */
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
WRAP(sigprocmask)(int how, const sigset_t *set, sigset_t *oset)
{
	sigset_t s;

	if (set != NULL && how != SIG_UNBLOCK && sigismember(set, SIGTHR)) {
		s = *set;
		sigdelset(&s, SIGTHR);
		set = &s;
	}
	return (sigprocmask(how, set, oset));
}
DEF_WRAP(sigprocmask);
