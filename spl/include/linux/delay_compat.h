/*****************************************************************************\
 *  Copyright (C) 2007-2013 Lawrence Livermore National Security, LLC.
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

#ifndef _SPL_DELAY_COMPAT_H
#define _SPL_DELAY_COMPAT_H

#include <linux/delay.h>
#include <linux/time.h>

/* usleep_range() introduced in 2.6.36 */
#ifndef HAVE_USLEEP_RANGE

static inline void
usleep_range(unsigned long min, unsigned long max)
{
	unsigned int min_ms = min / USEC_PER_MSEC;

	if (min >= MAX_UDELAY_MS)
		msleep(min_ms);
	else
		udelay(min);
}

#endif /* HAVE_USLEEP_RANGE */

#endif /* _SPL_DELAY_COMPAT_H */
