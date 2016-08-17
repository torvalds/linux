/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */

#ifndef	_SYS_FS_ZFS_DELAY_H
#define	_SYS_FS_ZFS_DELAY_H

#include <linux/delay_compat.h>

/*
 * Generic wrapper to sleep until a given time.
 */
#define	zfs_sleep_until(wakeup)						\
	do {								\
		hrtime_t delta = wakeup - gethrtime();			\
									\
		if (delta > 0) {					\
			unsigned long delta_us;				\
			delta_us = delta / (NANOSEC / MICROSEC);	\
			usleep_range(delta_us, delta_us + 100);		\
		}							\
	} while (0)

#endif	/* _SYS_FS_ZFS_DELAY_H */
