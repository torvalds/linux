/*-
 * Copyright (c) 2010 Isilon Systems, Inc.
 * Copyright (c) 2010 iX Systems, Inc.
 * Copyright (c) 2010 Panasas, Inc.
 * Copyright (c) 2013, 2014 Mellanox Technologies, Ltd.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
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
 *
 * $FreeBSD$
 */
#ifndef _LINUX_TIMER_H_
#define	_LINUX_TIMER_H_

#include <linux/types.h>

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/callout.h>

struct timer_list {
	struct callout callout;
	union {
		void (*function) (unsigned long);	/* < v4.15 */
		void (*function_415) (struct timer_list *);
	};
	unsigned long data;
	int expires;
};

extern unsigned long linux_timer_hz_mask;

#define	TIMER_IRQSAFE	0x0001

#define	from_timer(var, arg, field)					\
        container_of(arg, typeof(*(var)), field)

#define	timer_setup(timer, func, flags) do {				\
	CTASSERT(((flags) & ~TIMER_IRQSAFE) == 0);			\
	(timer)->function_415 = (func);					\
	(timer)->data = (unsigned long)(timer);				\
	callout_init(&(timer)->callout, 1);				\
} while (0)

#define	setup_timer(timer, func, dat) do {				\
	(timer)->function = (func);					\
	(timer)->data = (dat);						\
	callout_init(&(timer)->callout, 1);			\
} while (0)

#define	__setup_timer(timer, func, dat, flags) do {			\
	CTASSERT(((flags) & ~TIMER_IRQSAFE) == 0);			\
	setup_timer(timer, func, dat);					\
} while (0)

#define	init_timer(timer) do {						\
	(timer)->function = NULL;					\
	(timer)->data = 0;						\
	callout_init(&(timer)->callout, 1);			\
} while (0)

extern void mod_timer(struct timer_list *, int);
extern void add_timer(struct timer_list *);
extern void add_timer_on(struct timer_list *, int cpu);

#define	del_timer(timer)	(void)callout_stop(&(timer)->callout)
#define	del_timer_sync(timer)	(void)callout_drain(&(timer)->callout)
#define	timer_pending(timer)	callout_pending(&(timer)->callout)
#define	round_jiffies(j)	\
	((int)(((j) + linux_timer_hz_mask) & ~linux_timer_hz_mask))
#define	round_jiffies_relative(j) round_jiffies(j)
#define	round_jiffies_up(j)	round_jiffies(j)
#define	round_jiffies_up_relative(j) round_jiffies_up(j)

#endif					/* _LINUX_TIMER_H_ */
