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

#include <sys/cdefs.h>
#include <sys/types.h>
#include_next <sys/refcount.h>
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
#define	FTAG ((char *)(uintptr_t)__func__)

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
} refcount_t;

/* Note: refcount_t must be initialized with refcount_create[_untracked]() */

void refcount_create(refcount_t *rc);
void refcount_create_untracked(refcount_t *rc);
void refcount_create_tracked(refcount_t *rc);
void refcount_destroy(refcount_t *rc);
void refcount_destroy_many(refcount_t *rc, uint64_t number);
int refcount_is_zero(refcount_t *rc);
int64_t refcount_count(refcount_t *rc);
int64_t refcount_add(refcount_t *rc, void *holder_tag);
int64_t refcount_remove(refcount_t *rc, void *holder_tag);
int64_t refcount_add_many(refcount_t *rc, uint64_t number, void *holder_tag);
int64_t refcount_remove_many(refcount_t *rc, uint64_t number, void *holder_tag);
void refcount_transfer(refcount_t *dst, refcount_t *src);
void refcount_transfer_ownership(refcount_t *, void *, void *);
boolean_t refcount_held(refcount_t *, void *);
boolean_t refcount_not_held(refcount_t *, void *);

void refcount_sysinit(void);
void refcount_fini(void);

#else	/* ZFS_DEBUG */

typedef struct refcount {
	uint64_t rc_count;
} refcount_t;

#define	refcount_create(rc) ((rc)->rc_count = 0)
#define	refcount_create_untracked(rc) ((rc)->rc_count = 0)
#define	refcount_create_tracked(rc) ((rc)->rc_count = 0)
#define	refcount_destroy(rc) ((rc)->rc_count = 0)
#define	refcount_destroy_many(rc, number) ((rc)->rc_count = 0)
#define	refcount_is_zero(rc) ((rc)->rc_count == 0)
#define	refcount_count(rc) ((rc)->rc_count)
#define	refcount_add(rc, holder) atomic_inc_64_nv(&(rc)->rc_count)
#define	refcount_remove(rc, holder) atomic_dec_64_nv(&(rc)->rc_count)
#define	refcount_add_many(rc, number, holder) \
	atomic_add_64_nv(&(rc)->rc_count, number)
#define	refcount_remove_many(rc, number, holder) \
	atomic_add_64_nv(&(rc)->rc_count, -number)
#define	refcount_transfer(dst, src) { \
	uint64_t __tmp = (src)->rc_count; \
	atomic_add_64(&(src)->rc_count, -__tmp); \
	atomic_add_64(&(dst)->rc_count, __tmp); \
}
#define	refcount_transfer_ownership(rc, current_holder, new_holder)	(void)0
#define	refcount_held(rc, holder)		((rc)->rc_count > 0)
#define	refcount_not_held(rc, holder)		(B_TRUE)

#define	refcount_sysinit()
#define	refcount_fini()

#endif	/* ZFS_DEBUG */

#ifdef	__cplusplus
}
#endif

#endif /* _SYS_REFCOUNT_H */
