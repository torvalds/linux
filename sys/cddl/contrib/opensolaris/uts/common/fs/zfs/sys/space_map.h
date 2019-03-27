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
 * Copyright (c) 2012, 2017 by Delphix. All rights reserved.
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
	int64_t		smp_alloc;	/* space allocated from the map */
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
 *
 * Note: the space_map may not be accessed concurrently; consumers
 * must provide external locking if required.
 */
typedef struct space_map {
	uint64_t	sm_start;	/* start of map */
	uint64_t	sm_size;	/* size of map */
	uint8_t		sm_shift;	/* unit shift */
	uint64_t	sm_length;	/* synced length */
	int64_t		sm_alloc;	/* synced space allocated */
	objset_t	*sm_os;		/* objset for this map */
	uint64_t	sm_object;	/* object id for this map */
	uint32_t	sm_blksz;	/* block size for space map */
	dmu_buf_t	*sm_dbuf;	/* space_map_phys_t dbuf */
	space_map_phys_t *sm_phys;	/* on-disk space map */
} space_map_t;

/*
 * debug entry
 *
 *     2     2        10                     50
 *  +-----+-----+------------+----------------------------------+
 *  | 1 0 | act |  syncpass  |        txg (lower bits)          |
 *  +-----+-----+------------+----------------------------------+
 *   63 62 61 60 59        50 49                                0
 *
 *
 * one-word entry
 *
 *    1               47                   1           15
 *  +-----------------------------------------------------------+
 *  | 0 |   offset (sm_shift units)    | type |       run       |
 *  +-----------------------------------------------------------+
 *   63  62                          16   15   14               0
 *
 *
 * two-word entry
 *
 *     2     2               36                      24
 *  +-----+-----+---------------------------+-------------------+
 *  | 1 1 | pad |            run            |       vdev        |
 *  +-----+-----+---------------------------+-------------------+
 *   63 62 61 60 59                       24 23                 0
 *
 *     1                            63
 *  +------+----------------------------------------------------+
 *  | type |                      offset                        |
 *  +------+----------------------------------------------------+
 *     63   62                                                  0
 *
 * Note that a two-word entry will not strandle a block boundary.
 * If necessary, the last word of a block will be padded with a
 * debug entry (with act = syncpass = txg = 0).
 */

typedef enum {
	SM_ALLOC,
	SM_FREE
} maptype_t;

typedef struct space_map_entry {
	maptype_t sme_type;
	uint32_t sme_vdev;	/* max is 2^24-1; SM_NO_VDEVID if not present */
	uint64_t sme_offset;	/* max is 2^63-1; units of sm_shift */
	uint64_t sme_run;	/* max is 2^36; units of sm_shift */
} space_map_entry_t;

#define	SM_NO_VDEVID	(1 << SPA_VDEVBITS)

/* one-word entry constants */
#define	SM_DEBUG_PREFIX	2
#define	SM_OFFSET_BITS	47
#define	SM_RUN_BITS	15

/* two-word entry constants */
#define	SM2_PREFIX	3
#define	SM2_OFFSET_BITS	63
#define	SM2_RUN_BITS	36

#define	SM_PREFIX_DECODE(x)	BF64_DECODE(x, 62, 2)
#define	SM_PREFIX_ENCODE(x)	BF64_ENCODE(x, 62, 2)

#define	SM_DEBUG_ACTION_DECODE(x)	BF64_DECODE(x, 60, 2)
#define	SM_DEBUG_ACTION_ENCODE(x)	BF64_ENCODE(x, 60, 2)
#define	SM_DEBUG_SYNCPASS_DECODE(x)	BF64_DECODE(x, 50, 10)
#define	SM_DEBUG_SYNCPASS_ENCODE(x)	BF64_ENCODE(x, 50, 10)
#define	SM_DEBUG_TXG_DECODE(x)		BF64_DECODE(x, 0, 50)
#define	SM_DEBUG_TXG_ENCODE(x)		BF64_ENCODE(x, 0, 50)

#define	SM_OFFSET_DECODE(x)	BF64_DECODE(x, 16, SM_OFFSET_BITS)
#define	SM_OFFSET_ENCODE(x)	BF64_ENCODE(x, 16, SM_OFFSET_BITS)
#define	SM_TYPE_DECODE(x)	BF64_DECODE(x, 15, 1)
#define	SM_TYPE_ENCODE(x)	BF64_ENCODE(x, 15, 1)
#define	SM_RUN_DECODE(x)	(BF64_DECODE(x, 0, SM_RUN_BITS) + 1)
#define	SM_RUN_ENCODE(x)	BF64_ENCODE((x) - 1, 0, SM_RUN_BITS)
#define	SM_RUN_MAX		SM_RUN_DECODE(~0ULL)
#define	SM_OFFSET_MAX		SM_OFFSET_DECODE(~0ULL)

#define	SM2_RUN_DECODE(x)	(BF64_DECODE(x, SPA_VDEVBITS, SM2_RUN_BITS) + 1)
#define	SM2_RUN_ENCODE(x)	BF64_ENCODE((x) - 1, SPA_VDEVBITS, SM2_RUN_BITS)
#define	SM2_VDEV_DECODE(x)	BF64_DECODE(x, 0, SPA_VDEVBITS)
#define	SM2_VDEV_ENCODE(x)	BF64_ENCODE(x, 0, SPA_VDEVBITS)
#define	SM2_TYPE_DECODE(x)	BF64_DECODE(x, SM2_OFFSET_BITS, 1)
#define	SM2_TYPE_ENCODE(x)	BF64_ENCODE(x, SM2_OFFSET_BITS, 1)
#define	SM2_OFFSET_DECODE(x)	BF64_DECODE(x, 0, SM2_OFFSET_BITS)
#define	SM2_OFFSET_ENCODE(x)	BF64_ENCODE(x, 0, SM2_OFFSET_BITS)
#define	SM2_RUN_MAX		SM2_RUN_DECODE(~0ULL)
#define	SM2_OFFSET_MAX		SM2_OFFSET_DECODE(~0ULL)

boolean_t sm_entry_is_debug(uint64_t e);
boolean_t sm_entry_is_single_word(uint64_t e);
boolean_t sm_entry_is_double_word(uint64_t e);

typedef int (*sm_cb_t)(space_map_entry_t *sme, void *arg);

int space_map_load(space_map_t *sm, range_tree_t *rt, maptype_t maptype);
int space_map_iterate(space_map_t *sm, sm_cb_t callback, void *arg);
int space_map_incremental_destroy(space_map_t *sm, sm_cb_t callback, void *arg,
    dmu_tx_t *tx);

void space_map_histogram_clear(space_map_t *sm);
void space_map_histogram_add(space_map_t *sm, range_tree_t *rt,
    dmu_tx_t *tx);

void space_map_update(space_map_t *sm);

uint64_t space_map_object(space_map_t *sm);
uint64_t space_map_allocated(space_map_t *sm);
uint64_t space_map_length(space_map_t *sm);

void space_map_write(space_map_t *sm, range_tree_t *rt, maptype_t maptype,
    uint64_t vdev_id, dmu_tx_t *tx);
uint64_t space_map_estimate_optimal_size(space_map_t *sm, range_tree_t *rt,
    uint64_t vdev_id);
void space_map_truncate(space_map_t *sm, int blocksize, dmu_tx_t *tx);
uint64_t space_map_alloc(objset_t *os, int blocksize, dmu_tx_t *tx);
void space_map_free(space_map_t *sm, dmu_tx_t *tx);
void space_map_free_obj(objset_t *os, uint64_t smobj, dmu_tx_t *tx);

int space_map_open(space_map_t **smp, objset_t *os, uint64_t object,
    uint64_t start, uint64_t size, uint8_t shift);
void space_map_close(space_map_t *sm);

int64_t space_map_alloc_delta(space_map_t *sm);

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_SPACE_MAP_H */
