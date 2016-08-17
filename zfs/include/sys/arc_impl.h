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
 * Copyright (c) 2013 by Delphix. All rights reserved.
 * Copyright (c) 2013 by Saso Kiselkov. All rights reserved.
 * Copyright 2013 Nexenta Systems, Inc.  All rights reserved.
 */

#ifndef _SYS_ARC_IMPL_H
#define	_SYS_ARC_IMPL_H

#include <sys/arc.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Note that buffers can be in one of 6 states:
 *	ARC_anon	- anonymous (discussed below)
 *	ARC_mru		- recently used, currently cached
 *	ARC_mru_ghost	- recentely used, no longer in cache
 *	ARC_mfu		- frequently used, currently cached
 *	ARC_mfu_ghost	- frequently used, no longer in cache
 *	ARC_l2c_only	- exists in L2ARC but not other states
 * When there are no active references to the buffer, they are
 * are linked onto a list in one of these arc states.  These are
 * the only buffers that can be evicted or deleted.  Within each
 * state there are multiple lists, one for meta-data and one for
 * non-meta-data.  Meta-data (indirect blocks, blocks of dnodes,
 * etc.) is tracked separately so that it can be managed more
 * explicitly: favored over data, limited explicitly.
 *
 * Anonymous buffers are buffers that are not associated with
 * a DVA.  These are buffers that hold dirty block copies
 * before they are written to stable storage.  By definition,
 * they are "ref'd" and are considered part of arc_mru
 * that cannot be freed.  Generally, they will aquire a DVA
 * as they are written and migrate onto the arc_mru list.
 *
 * The ARC_l2c_only state is for buffers that are in the second
 * level ARC but no longer in any of the ARC_m* lists.  The second
 * level ARC itself may also contain buffers that are in any of
 * the ARC_m* states - meaning that a buffer can exist in two
 * places.  The reason for the ARC_l2c_only state is to keep the
 * buffer header in the hash table, so that reads that hit the
 * second level ARC benefit from these fast lookups.
 */

typedef struct arc_state {
	/*
	 * list of evictable buffers
	 */
	multilist_t arcs_list[ARC_BUFC_NUMTYPES];
	/*
	 * total amount of evictable data in this state
	 */
	uint64_t arcs_lsize[ARC_BUFC_NUMTYPES];
	/*
	 * total amount of data in this state; this includes: evictable,
	 * non-evictable, ARC_BUFC_DATA, and ARC_BUFC_METADATA.
	 */
	refcount_t arcs_size;
	/*
	 * supports the "dbufs" kstat
	 */
	arc_state_type_t arcs_state;
} arc_state_t;

typedef struct arc_callback arc_callback_t;

struct arc_callback {
	void			*acb_private;
	arc_done_func_t		*acb_done;
	arc_buf_t		*acb_buf;
	zio_t			*acb_zio_dummy;
	arc_callback_t		*acb_next;
};

typedef struct arc_write_callback arc_write_callback_t;

struct arc_write_callback {
	void		*awcb_private;
	arc_done_func_t	*awcb_ready;
	arc_done_func_t	*awcb_physdone;
	arc_done_func_t	*awcb_done;
	arc_buf_t	*awcb_buf;
};

/*
 * ARC buffers are separated into multiple structs as a memory saving measure:
 *   - Common fields struct, always defined, and embedded within it:
 *       - L2-only fields, always allocated but undefined when not in L2ARC
 *       - L1-only fields, only allocated when in L1ARC
 *
 *           Buffer in L1                     Buffer only in L2
 *    +------------------------+          +------------------------+
 *    | arc_buf_hdr_t          |          | arc_buf_hdr_t          |
 *    |                        |          |                        |
 *    |                        |          |                        |
 *    |                        |          |                        |
 *    +------------------------+          +------------------------+
 *    | l2arc_buf_hdr_t        |          | l2arc_buf_hdr_t        |
 *    | (undefined if L1-only) |          |                        |
 *    +------------------------+          +------------------------+
 *    | l1arc_buf_hdr_t        |
 *    |                        |
 *    |                        |
 *    |                        |
 *    |                        |
 *    +------------------------+
 *
 * Because it's possible for the L2ARC to become extremely large, we can wind
 * up eating a lot of memory in L2ARC buffer headers, so the size of a header
 * is minimized by only allocating the fields necessary for an L1-cached buffer
 * when a header is actually in the L1 cache. The sub-headers (l1arc_buf_hdr and
 * l2arc_buf_hdr) are embedded rather than allocated separately to save a couple
 * words in pointers. arc_hdr_realloc() is used to switch a header between
 * these two allocation states.
 */
typedef struct l1arc_buf_hdr {
	kmutex_t		b_freeze_lock;

	arc_buf_t		*b_buf;
	uint32_t		b_datacnt;
	/* for waiting on writes to complete */
	kcondvar_t		b_cv;


	/* protected by arc state mutex */
	arc_state_t		*b_state;
	multilist_node_t	b_arc_node;

	/* updated atomically */
	clock_t			b_arc_access;
	uint32_t		b_mru_hits;
	uint32_t		b_mru_ghost_hits;
	uint32_t		b_mfu_hits;
	uint32_t		b_mfu_ghost_hits;
	uint32_t		b_l2_hits;

	/* self protecting */
	refcount_t		b_refcnt;

	arc_callback_t		*b_acb;
	/* temporary buffer holder for in-flight compressed data */
	void			*b_tmp_cdata;
} l1arc_buf_hdr_t;

typedef struct l2arc_dev {
	vdev_t			*l2ad_vdev;	/* vdev */
	spa_t			*l2ad_spa;	/* spa */
	uint64_t		l2ad_hand;	/* next write location */
	uint64_t		l2ad_start;	/* first addr on device */
	uint64_t		l2ad_end;	/* last addr on device */
	boolean_t		l2ad_first;	/* first sweep through */
	boolean_t		l2ad_writing;	/* currently writing */
	kmutex_t		l2ad_mtx;	/* lock for buffer list */
	list_t			l2ad_buflist;	/* buffer list */
	list_node_t		l2ad_node;	/* device list node */
	refcount_t		l2ad_alloc;	/* allocated bytes */
} l2arc_dev_t;

typedef struct l2arc_buf_hdr {
	/* protected by arc_buf_hdr mutex */
	l2arc_dev_t		*b_dev;		/* L2ARC device */
	uint64_t		b_daddr;	/* disk address, offset byte */
	/* real alloc'd buffer size depending on b_compress applied */
	uint32_t		b_hits;
	int32_t			b_asize;
	uint8_t			b_compress;

	list_node_t		b_l2node;
} l2arc_buf_hdr_t;

typedef struct l2arc_write_callback {
	l2arc_dev_t	*l2wcb_dev;		/* device info */
	arc_buf_hdr_t	*l2wcb_head;		/* head of write buflist */
} l2arc_write_callback_t;

struct arc_buf_hdr {
	/* protected by hash lock */
	dva_t			b_dva;
	uint64_t		b_birth;
	/*
	 * Even though this checksum is only set/verified when a buffer is in
	 * the L1 cache, it needs to be in the set of common fields because it
	 * must be preserved from the time before a buffer is written out to
	 * L2ARC until after it is read back in.
	 */
	zio_cksum_t		*b_freeze_cksum;

	arc_buf_hdr_t		*b_hash_next;
	arc_flags_t		b_flags;

	/* immutable */
	int32_t			b_size;
	uint64_t		b_spa;

	/* L2ARC fields. Undefined when not in L2ARC. */
	l2arc_buf_hdr_t		b_l2hdr;
	/* L1ARC fields. Undefined when in l2arc_only state */
	l1arc_buf_hdr_t		b_l1hdr;
};
#ifdef __cplusplus
}
#endif

#endif /* _SYS_ARC_IMPL_H */
