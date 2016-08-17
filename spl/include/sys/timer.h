/*****************************************************************************\
 *  Copyright (C) 2007-2010 Lawrence Livermore National Security, LLC.
 *  Copyright (C) 2007 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Brian Behlendorf <behlendorf1@llnl.gov>.
 *  UCRL-CODE-235197
 *
 *  This file is part of the SPL, Solaris Porting Layer.
 *  For details, see <http://zfsonlinux.org/>.
 *
 *  The SPL is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the
 *  Free Software Foundation; either version 2 of the License, or (at your
 *  option) any later version.
 *
 *  The SPL is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with the SPL.  If not, see <http://www.gnu.org/licenses/>.
\*****************************************************************************/

#ifndef _SPL_TIMER_H
#define	_SPL_TIMER_H

#include <linux/module.h>
#include <linux/sched.h>
#include <linux/timer.h>

#define	lbolt				((clock_t)jiffies)
#define	lbolt64				((int64_t)get_jiffies_64())

#define	ddi_get_lbolt()			((clock_t)jiffies)
#define	ddi_get_lbolt64()		((int64_t)get_jiffies_64())

#define	ddi_time_before(a, b)		(typecheck(clock_t, a) && \
					typecheck(clock_t, b) && \
					((a) - (b) < 0))
#define	ddi_time_after(a, b)		ddi_time_before(b, a)
#define	ddi_time_before_eq(a, b)	(!ddi_time_after(a, b))
#define	ddi_time_after_eq(a, b)		ddi_time_before_eq(b, a)

#define	ddi_time_before64(a, b)		(typecheck(int64_t, a) && \
					typecheck(int64_t, b) && \
					((a) - (b) < 0))
#define	ddi_time_after64(a, b)		ddi_time_before64(b, a)
#define	ddi_time_before_eq64(a, b)	(!ddi_time_after64(a, b))
#define	ddi_time_after_eq64(a, b)	ddi_time_before_eq64(b, a)

#define	delay(ticks)			schedule_timeout_uninterruptible(ticks)

#define	SEC_TO_TICK(sec)		((sec) * HZ)
#define	MSEC_TO_TICK(ms)		msecs_to_jiffies(ms)
#define	USEC_TO_TICK(us)		usecs_to_jiffies(us)
#define	NSEC_TO_TICK(ns)		usecs_to_jiffies(ns / NSEC_PER_USEC)

#endif  /* _SPL_TIMER_H */
