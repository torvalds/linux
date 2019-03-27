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
 * Copyright (c) 2017 by Delphix. All rights reserved.
 */

#ifndef	_SYS_AGGSUM_H
#define	_SYS_AGGSUM_H

#include <sys/zfs_context.h>

#ifdef	__cplusplus
extern "C" {
#endif

typedef struct aggsum_bucket {
	kmutex_t asc_lock;
	int64_t asc_delta;
	uint64_t asc_borrowed;
	uint64_t asc_pad[2]; /* pad out to cache line (64 bytes) */
} aggsum_bucket_t __aligned(CACHE_LINE_SIZE);

/*
 * Fan out over FANOUT cpus.
 */
typedef struct aggsum {
	kmutex_t as_lock;
	int64_t as_lower_bound;
	int64_t as_upper_bound;
	uint64_t as_numbuckets;
	aggsum_bucket_t *as_buckets;
} aggsum_t;

void aggsum_init(aggsum_t *, uint64_t);
void aggsum_fini(aggsum_t *);
int64_t aggsum_lower_bound(aggsum_t *);
int64_t aggsum_upper_bound(aggsum_t *);
int aggsum_compare(aggsum_t *, uint64_t);
uint64_t aggsum_value(aggsum_t *);
void aggsum_add(aggsum_t *, int64_t);

#ifdef	__cplusplus
}
#endif

#endif /* _SYS_AGGSUM_H */
