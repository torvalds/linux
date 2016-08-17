/*
 * CDDL HEADER START
 *
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source.  A copy of the CDDL is also available via the Internet at
 * http://www.illumos.org/license/CDDL.
 *
 * CDDL HEADER END
 */

/*
 * Copyright (c) 2016, Lawrence Livermore National Security, LLC.
 */

#ifndef _SYS_ZFS_RATELIMIT_H
#define	_SYS_ZFS_RATELIMIT_H

#include <sys/zfs_context.h>

typedef struct {
	hrtime_t start;
	unsigned int count;

	/*
	 * Pointer to number of events per interval.  We do this to
	 * allow the burst to be a (changeable) module parameter.
	 */
	unsigned int *burst;

	unsigned int interval;	/* Interval length in seconds */
	kmutex_t lock;
} zfs_ratelimit_t;

int zfs_ratelimit(zfs_ratelimit_t *rl);
void zfs_ratelimit_init(zfs_ratelimit_t *rl, unsigned int *burst,
    unsigned int interval);
void zfs_ratelimit_fini(zfs_ratelimit_t *rl);

#endif	/* _SYS_ZFS_RATELIMIT_H */
