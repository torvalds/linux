/*****************************************************************************\
 *  Copyright (C) 2007-2013 Lawrence Livermore National Security, LLC.
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

#ifndef _SPL_CALLO_H
#define	_SPL_CALLO_H

/*
 * Callout flags:
 *
 * CALLOUT_FLAG_ROUNDUP
 *      Roundup the expiration time to the next resolution boundary.
 *      If this flag is not specified, the expiration time is rounded down.
 * CALLOUT_FLAG_ABSOLUTE
 *      Normally, the expiration passed to the timeout API functions is an
 *      expiration interval. If this flag is specified, then it is
 *      interpreted as the expiration time itself.
 * CALLOUT_FLAG_HRESTIME
 *      Normally, callouts are not affected by changes to system time
 *      (hrestime). This flag is used to create a callout that is affected
 *      by system time. If system time changes, these timers must be
 *      handled in a special way (see callout.c). These are used by condition
 *      variables and LWP timers that need this behavior.
 * CALLOUT_FLAG_32BIT
 *      Legacy interfaces timeout() and realtime_timeout() pass this flag
 *      to timeout_generic() to indicate that a 32-bit ID should be allocated.
 */
#define	CALLOUT_FLAG_ROUNDUP		0x1
#define	CALLOUT_FLAG_ABSOLUTE		0x2
#define	CALLOUT_FLAG_HRESTIME		0x4
#define	CALLOUT_FLAG_32BIT		0x8

#endif  /* _SPL_CALLB_H */
