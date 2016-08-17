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
 * Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

/*
 * Copyright (c) 2012, 2014 by Delphix. All rights reserved.
 */

#ifndef _SYS_SPACE_MAP_H
#define	_SYS_SPACE_MAP_H

#include <sys/avl.h>
#include <sys/range_tree.h>
#include <sys/dmu.h>

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * The size of the space map object has increased to include a histogram.
 * The SPACE_MAP_SIZE_V0 designates the original size and is used to
 * maintain backward compatibility.
 */
#define	SPACE_MAP_SIZE_V0	(3 * sizeof (uint64_t))
#define	SPACE_MAP_HISTOGRAM_SIZE	32

/*
 * The space_map_phys is the on-disk representation of the space map.
 * Consumers of space maps should never reference any of the members of this
 * structure directly. These members may only be updated in syncing context.
 *
 * Note the smp_object is no longer used but remains in the structure
 * for backward compatibility.
 */
typedef struct space_map_phys {
	uint64_t	smp_object;	/* on-disk space map object */
	uint64_t	smp_objsize;	/* size of the object */
	uint64_t	smp_alloc;	/* space allocated from the map */
	uint64_t	smp_pad[5];	/* reserved */

	/*
	 * The smp_histogram maintains a histogram of free regions. Each
	 * bucket, smp_histogram[i], contains the number of free regions
	 * whose size is:
	 * 2^(i+sm_shift) <= size of free region in bytes < 2^(i+sm_shift+1)
	 */
	uint64_t	smp_histogram[SPACE_MAP_HISTOGRAM_SIZE];
} space_map_phys_t;

/*
 * The space map object defines a region of space, its size, how much is
 * allocated, and the on-disk object that stores this information.
 * Consumers of space maps may only access the members of this structure.
 */
typedef struct space_map {
	uint64_t	sm_start;	/* start of map */
	uint64_t	sm_size;	/* size of map */
	uint8_t		sm_shift;	/* unit shift */
	uint64_t	sm_length;	/* synced length */
	uint64_t	sm_alloc;	/* synced space allocated */
	objset_t	*sm_os;		/* objset for this map */
	uint64_t	sm_object;	/* object id for this map */
	uint32_t	sm_blksz;	/* block size for space map */
	dmu_buf_t	*sm_dbuf;	/* space_map_phys_t dbuf */
	space_map_phys_t *sm_phys;	/* on-disk space map */
	kmutex_t	*sm_lock;	/* pointer to lock that protects map */
} space_map_t;

/*
 * debug entry
 *
 *    1      3         10                     50
 *  ,---+--------+------------+---------------------------------.
 *  | 1 | action |  syncpass  |        txg (lower bits)         |
 *  `---+--------+------------+---------------------------------'
 *   63  62    60 59        50 49                               0
 *
 *
 * non-debug entry
 *
 *    1               47                   1           15
 *  ,-----------------------------------------------------------.
 *  | 0 |   offset (sm_shift units)    | type |       run       |
 *  `-----------------------------------------------------------'
 *   63  62                          17   16   15               0
 */

/* All this stuff takes and returns bytes */
#define	SM_RUN_DECODE(x)	(BF64_DECODE(x, 0, 15) + 1)
#define	SM_RUN_ENCODE(x)	BF64_ENCODE((x) - 1, 0, 15)
#define	SM_TYPE_DECODE(x)	BF64_DECODE(x, 15, 1)
#define	SM_TYPE_ENCODE(x)	BF64_ENCODE(x, 15, 1)
#define	SM_OFFSET_DECODE(x)	BF64_DECODE(x, 16, 47)
#define	SM_OFFSET_ENCODE(x)	BF64_ENCODE(x, 16, 47)
#define	SM_DEBUG_DECODE(x)	BF64_DECODE(x, 63, 1)
#define	SM_DEBUG_ENCODE(x)	BF64_ENCODE(x, 63, 1)

#define	SM_DEBUG_ACTION_DECODE(x)	BF64_DECODE(x, 60, 3)
#define	SM_DEBUG_ACTION_ENCODE(x)	BF64_ENCODE(x, 60, 3)

#define	SM_DEBUG_SYNCPASS_DECODE(x)	BF64_DECODE(x, 50, 10)
#define	SM_DEBUG_SYNCPASS_ENCODE(x)	BF64_ENCODE(x, 50, 10)

#define	SM_DEBUG_TXG_DECODE(x)		BF64_DECODE(x, 0, 50)
#define	SM_DEBUG_TXG_ENCODE(x)		BF64_ENCODE(x, 0, 50)

#define	SM_RUN_MAX			SM_RUN_DECODE(~0ULL)

typedef enum {
	SM_ALLOC,
	SM_FREE
} maptype_t;

int space_map_load(space_map_t *sm, range_tree_t *rt, maptype_t maptype);

void space_map_histogram_clear(space_map_t *sm);
void space_map_histogram_add(space_map_t *sm, range_tree_t *rt,
    dmu_tx_t *tx);

void space_map_update(space_map_t *sm);

uint64_t space_map_object(space_map_t *sm);
uint64_t space_map_allocated(space_map_t *sm);
uint64_t space_map_length(space_map_t *sm);

void space_map_write(space_map_t *sm, range_tree_t *rt, maptype_t maptype,
    dmu_tx_t *tx);
void space_map_truncate(space_map_t *sm, dmu_tx_t *tx);
uint64_t space_map_alloc(objset_t *os, dmu_tx_t *tx);
void space_map_free(space_map_t *sm, dmu_tx_t *tx);

int space_map_open(space_map_t **smp, objset_t *os, uint64_t object,
    uint64_t start, uint64_t size, uint8_t shift, kmutex_t *lp);
void space_map_close(space_map_t *sm);

int64_t space_map_alloc_delta(space_map_t *sm);

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_SPACE_MAP_H */
