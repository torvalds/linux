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
 * Copyright (c) 2014 by Delphix. All rights reserved.
 */
#ifndef	_ZIO_PRIORITY_H
#define	_ZIO_PRIORITY_H

#ifdef	__cplusplus
extern "C" {
#endif

typedef enum zio_priority {
	ZIO_PRIORITY_SYNC_READ,
	ZIO_PRIORITY_SYNC_WRITE,	/* ZIL */
	ZIO_PRIORITY_ASYNC_READ,	/* prefetch */
	ZIO_PRIORITY_ASYNC_WRITE,	/* spa_sync() */
	ZIO_PRIORITY_SCRUB,		/* asynchronous scrub/resilver reads */
	ZIO_PRIORITY_NUM_QUEUEABLE,
	ZIO_PRIORITY_NOW,		/* non-queued i/os (e.g. free) */
} zio_priority_t;

#ifdef	__cplusplus
}
#endif

#endif	/* _ZIO_PRIORITY_H */
