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
 * Copyright (c) 2005, 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2012, 2015 by Delphix. All rights reserved.
 */

#ifndef	_SYS_REFCOUNT_H
#define	_SYS_REFCOUNT_H

#include <sys/inttypes.h>
#include <sys/list.h>
#include <sys/zfs_context.h>

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * If the reference is held only by the calling function and not any
 * particular object, use FTAG (which is a string) for the holder_tag.
 * Otherwise, use the object that holds the reference.
 */
#define	FTAG ((char *)__func__)

#ifdef	ZFS_DEBUG
typedef struct reference {
	list_node_t ref_link;
	void *ref_holder;
	uint64_t ref_number;
	uint8_t *ref_removed;
} reference_t;

typedef struct refcount {
	kmutex_t rc_mtx;
	boolean_t rc_tracked;
	list_t rc_list;
	list_t rc_removed;
	uint64_t rc_count;
	uint64_t rc_removed_count;
} zfs_refcount_t;

/*
 * Note: zfs_refcount_t must be initialized with
 * refcount_create[_untracked]()
 */

void zfs_refcount_create(zfs_refcount_t *);
void zfs_refcount_create_untracked(zfs_refcount_t *);
void zfs_refcount_create_tracked(zfs_refcount_t *);
void zfs_refcount_destroy(zfs_refcount_t *);
void zfs_refcount_destroy_many(zfs_refcount_t *, uint64_t);
int zfs_refcount_is_zero(zfs_refcount_t *);
int64_t zfs_refcount_count(zfs_refcount_t *);
int64_t zfs_refcount_add(zfs_refcount_t *, void *);
int64_t zfs_refcount_remove(zfs_refcount_t *, void *);
int64_t zfs_refcount_add_many(zfs_refcount_t *, uint64_t, void *);
int64_t zfs_refcount_remove_many(zfs_refcount_t *, uint64_t, void *);
void zfs_refcount_transfer(zfs_refcount_t *, zfs_refcount_t *);
void zfs_refcount_transfer_ownership(zfs_refcount_t *, void *, void *);
boolean_t zfs_refcount_held(zfs_refcount_t *, void *);
boolean_t zfs_refcount_not_held(zfs_refcount_t *, void *);

void zfs_refcount_init(void);
void zfs_refcount_fini(void);

#else	/* ZFS_DEBUG */

typedef struct refcount {
	uint64_t rc_count;
} zfs_refcount_t;

#define	zfs_refcount_create(rc) ((rc)->rc_count = 0)
#define	zfs_refcount_create_untracked(rc) ((rc)->rc_count = 0)
#define	zfs_refcount_create_tracked(rc) ((rc)->rc_count = 0)
#define	zfs_refcount_destroy(rc) ((rc)->rc_count = 0)
#define	zfs_refcount_destroy_many(rc, number) ((rc)->rc_count = 0)
#define	zfs_refcount_is_zero(rc) ((rc)->rc_count == 0)
#define	zfs_refcount_count(rc) ((rc)->rc_count)
#define	zfs_refcount_add(rc, holder) atomic_inc_64_nv(&(rc)->rc_count)
#define	zfs_refcount_remove(rc, holder) atomic_dec_64_nv(&(rc)->rc_count)
#define	zfs_refcount_add_many(rc, number, holder) \
	atomic_add_64_nv(&(rc)->rc_count, number)
#define	zfs_refcount_remove_many(rc, number, holder) \
	atomic_add_64_nv(&(rc)->rc_count, -number)
#define	zfs_refcount_transfer(dst, src) { \
	uint64_t __tmp = (src)->rc_count; \
	atomic_add_64(&(src)->rc_count, -__tmp); \
	atomic_add_64(&(dst)->rc_count, __tmp); \
}
#define	zfs_refcount_transfer_ownership(rc, current_holder, new_holder)	(void)0
#define	zfs_refcount_held(rc, holder)		((rc)->rc_count > 0)
#define	zfs_refcount_not_held(rc, holder)		(B_TRUE)

#define	zfs_refcount_init()
#define	zfs_refcount_fini()

#endif	/* ZFS_DEBUG */

#ifdef	__cplusplus
}
#endif

#endif /* _SYS_REFCOUNT_H */
