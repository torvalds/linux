/*	$OpenBSD: sched_prio.c,v 1.1 2011/11/06 12:15:51 guenther Exp $	*/

/*
 * Copyright (c) 2010 Federico G. Schwindt <fgsch@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for
 * any purpose with or without fee is hereby granted, provided that
 * the above copyright notice and this permission notice appear in all
 * copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL
 * WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE
 * AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL
 * DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA
 * OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

#include <pthread.h>
#include <errno.h>
#include "rthread.h"

int
sched_get_priority_max(int policy)
{
	if (policy < SCHED_FIFO || policy > SCHED_RR) {
		errno = EINVAL;
		return (-1);
	}
	return (PTHREAD_MAX_PRIORITY);
}

int
sched_get_priority_min(int policy)
{
	if (policy < SCHED_FIFO || policy > SCHED_RR) {
		errno = EINVAL;
		return (-1);
	}
	return (PTHREAD_MIN_PRIORITY);
}

