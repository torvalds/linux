/*-
 * Copyright (c) 2010 Isilon Systems, Inc.
 * Copyright (c) 2010 iX Systems, Inc.
 * Copyright (c) 2010 Panasas, Inc.
 * Copyright (c) 2013-2015 Mellanox Technologies, Ltd.
 * Copyright (c) 2014 François Tigeot
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
#ifndef _LINUX_DELAY_H_
#define	_LINUX_DELAY_H_

#include <linux/jiffies.h>
#include <sys/systm.h>

static inline void
linux_msleep(unsigned int ms)
{
	/* guard against invalid values */
	if (ms == 0)
		ms = 1;
	pause_sbt("lnxsleep", mstosbt(ms), 0, C_HARDCLOCK);
}

#undef msleep
#define	msleep(ms) linux_msleep(ms)

#undef msleep_interruptible
#define	msleep_interruptible(ms) linux_msleep_interruptible(ms)

#define	udelay(t)	DELAY(t)

static inline void
mdelay(unsigned long msecs)
{
	while (msecs--)
		DELAY(1000);
}

static inline void
ndelay(unsigned long x)
{
	DELAY(howmany(x, 1000));
}

static inline void
usleep_range(unsigned long min, unsigned long max)
{
	DELAY(min);
}

extern unsigned int linux_msleep_interruptible(unsigned int ms);

#endif	/* _LINUX_DELAY_H_ */
