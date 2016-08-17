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

typedef longlong_t		hrtime_t;
typedef struct timespec		timespec_t;

static const int hz = HZ;

#define	TIMESPEC_OVERFLOW(ts)		\
	((ts)->tv_sec < TIME_MIN || (ts)->tv_sec > TIME_MAX)

#if defined(HAVE_INODE_TIMESPEC64_TIMES)
typedef struct timespec64	inode_timespec_t;
#else
typedef struct timespec		inode_timespec_t;
#endif

/* Include for Lustre compatibility */
#define        timestruc_t     inode_timespec_t

static inline void
gethrestime(inode_timespec_t *ts)
 {
#if defined(HAVE_INODE_TIMESPEC64_TIMES)
	*ts = current_kernel_time64();
#else
	*ts = current_kernel_time();
#endif
}

static inline time_t
gethrestime_sec(void)
{
#if defined(HAVE_INODE_TIMESPEC64_TIMES)
	inode_timespec_t ts = current_kernel_time64();
#else
	inode_timespec_t ts = current_kernel_time();
#endif
	return (ts.tv_sec);
}

static inline hrtime_t
gethrtime(void)
{
	struct timespec ts;
	getrawmonotonic(&ts);
	return (((hrtime_t)ts.tv_sec * NSEC_PER_SEC) + ts.tv_nsec);
}

#endif  /* _SPL_TIME_H */
