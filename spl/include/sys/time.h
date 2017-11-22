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

#ifndef _SPL_TIME_H
#define	_SPL_TIME_H

#include <linux/module.h>
#include <linux/time.h>
#include <sys/types.h>
#include <sys/timer.h>

#if defined(CONFIG_64BIT)
#define	TIME_MAX			INT64_MAX
#define	TIME_MIN			INT64_MIN
#else
#define	TIME_MAX			INT32_MAX
#define	TIME_MIN			INT32_MIN
#endif

#define	SEC				1
#define	MILLISEC			1000
#define	MICROSEC			1000000
#define	NANOSEC				1000000000

#define	MSEC2NSEC(m)	((hrtime_t)(m) * (NANOSEC / MILLISEC))
#define	NSEC2MSEC(n)	((n) / (NANOSEC / MILLISEC))

#define	USEC2NSEC(m)	((hrtime_t)(m) * (NANOSEC / MICROSEC))
#define	NSEC2USEC(n)	((n) / (NANOSEC / MICROSEC))

#define	NSEC2SEC(n)	((n) / (NANOSEC / SEC))
#define	SEC2NSEC(m)	((hrtime_t)(m) * (NANOSEC / SEC))

static const int hz = HZ;

#define	TIMESPEC_OVERFLOW(ts)		\
	((ts)->tv_sec < TIME_MIN || (ts)->tv_sec > TIME_MAX)

static inline void
gethrestime(timestruc_t *now)
{
	*now = current_kernel_time();
}

static inline time_t
gethrestime_sec(void)
{
	struct timespec ts;
	ts = current_kernel_time();
	return (ts.tv_sec);
}

static inline hrtime_t
gethrtime(void)
{
	struct timespec now;
	getrawmonotonic(&now);
	return (((hrtime_t)now.tv_sec * NSEC_PER_SEC) + now.tv_nsec);
}

#endif  /* _SPL_TIME_H */
