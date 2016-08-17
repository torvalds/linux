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
/*
 * Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */
/*
 * Copyright (c) 2012 by Delphix. All rights reserved.
 */

#ifndef	_SYS_RR_RW_LOCK_H
#define	_SYS_RR_RW_LOCK_H



#ifdef	__cplusplus
extern "C" {
#endif

#include <sys/inttypes.h>
#include <sys/zfs_context.h>
#include <sys/refcount.h>

/*
 * A reader-writer lock implementation that allows re-entrant reads, but
 * still gives writers priority on "new" reads.
 *
 * See rrwlock.c for more details about the implementation.
 *
 * Fields of the rrwlock_t structure:
 * - rr_lock: protects modification and reading of rrwlock_t fields
 * - rr_cv: cv for waking up readers or waiting writers
 * - rr_writer: thread id of the current writer
 * - rr_anon_rount: number of active anonymous readers
 * - rr_linked_rcount: total number of non-anonymous active readers
 * - rr_writer_wanted: a writer wants the lock
 */
typedef struct rrwlock {
	kmutex_t	rr_lock;
	kcondvar_t	rr_cv;
	kthread_t	*rr_writer;
	refcount_t	rr_anon_rcount;
	refcount_t	rr_linked_rcount;
	boolean_t	rr_writer_wanted;
	boolean_t	rr_track_all;
} rrwlock_t;

/*
 * 'tag' is used in reference counting tracking.  The
 * 'tag' must be the same in a rrw_enter() as in its
 * corresponding rrw_exit().
 */
void rrw_init(rrwlock_t *rrl, boolean_t track_all);
void rrw_destroy(rrwlock_t *rrl);
void rrw_enter(rrwlock_t *rrl, krw_t rw, void *tag);
void rrw_enter_read(rrwlock_t *rrl, void *tag);
void rrw_enter_read_prio(rrwlock_t *rrl, void *tag);
void rrw_enter_write(rrwlock_t *rrl);
void rrw_exit(rrwlock_t *rrl, void *tag);
boolean_t rrw_held(rrwlock_t *rrl, krw_t rw);
void rrw_tsd_destroy(void *arg);

#define	RRW_READ_HELD(x)	rrw_held(x, RW_READER)
#define	RRW_WRITE_HELD(x)	rrw_held(x, RW_WRITER)
#define	RRW_LOCK_HELD(x) \
	(rrw_held(x, RW_WRITER) || rrw_held(x, RW_READER))

/*
 * A reader-mostly lock implementation, tuning above reader-writer locks
 * for hightly parallel read acquisitions, pessimizing write acquisitions.
 *
 * This should be a prime number.  See comment in rrwlock.c near
 * RRM_TD_LOCK() for details.
 */
#define	RRM_NUM_LOCKS		17
typedef struct rrmlock {
	rrwlock_t	locks[RRM_NUM_LOCKS];
} rrmlock_t;

void rrm_init(rrmlock_t *rrl, boolean_t track_all);
void rrm_destroy(rrmlock_t *rrl);
void rrm_enter(rrmlock_t *rrl, krw_t rw, void *tag);
void rrm_enter_read(rrmlock_t *rrl, void *tag);
void rrm_enter_write(rrmlock_t *rrl);
void rrm_exit(rrmlock_t *rrl, void *tag);
boolean_t rrm_held(rrmlock_t *rrl, krw_t rw);

#define	RRM_READ_HELD(x)	rrm_held(x, RW_READER)
#define	RRM_WRITE_HELD(x)	rrm_held(x, RW_WRITER)
#define	RRM_LOCK_HELD(x) \
	(rrm_held(x, RW_WRITER) || rrm_held(x, RW_READER))

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_RR_RW_LOCK_H */
