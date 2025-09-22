/*	$OpenBSD: timer.h,v 1.9 2024/01/16 23:38:13 jsg Exp $	*/
/*
 * Copyright (c) 2013, 2014, 2015 Mark Kettenis
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

#ifndef _LINUX_TIMER_H
#define _LINUX_TIMER_H

#include <sys/types.h>
#include <sys/param.h>
#include <sys/timeout.h>
#include <sys/kernel.h>
#include <linux/ktime.h>

#define del_timer_sync(x)	timeout_del_barrier((x))
#define timer_shutdown_sync(x)	timeout_del_barrier((x))
#define del_timer(x)		timeout_del((x))
#define timer_pending(x)	timeout_pending((x))

static inline int
mod_timer(struct timeout *to, unsigned long j)
{
	if (j <= jiffies)
		return timeout_add(to, 1);
	return timeout_add(to, j - jiffies);
}

static inline unsigned long
round_jiffies_up(unsigned long j)
{
	return roundup(j, hz);
}

static inline unsigned long
round_jiffies_up_relative(unsigned long j)
{
	return roundup(j, hz);
}

#endif
