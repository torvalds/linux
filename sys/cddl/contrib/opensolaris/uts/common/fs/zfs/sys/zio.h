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
 * Copyright 2011 Nexenta Systems, Inc.  All rights reserved.
 * Copyright (c) 2012, 2018 by Delphix. All rights reserved.
 * Copyright (c) 2013 by Saso Kiselkov. All rights reserved.
 * Copyright 2016 Toomas Soome <tsoome@me.com>
 */

#ifndef _ZIO_H
#define	_ZIO_H

#include <sys/zio_priority.h>
#include <sys/zfs_context.h>
#include <sys/spa.h>
#include <sys/txg.h>
#include <sys/avl.h>
#include <sys/kstat.h>
#include <sys/fs/zfs.h>
#include <sys/zio_impl.h>

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * Embedded checksum
 */
#define	ZEC_MAGIC	0x210da7ab10c7a11ULL

typedef struct zio_eck {
	uint64_t	zec_magic;	/* for validation, endianness	*/
	zio_cksum_t	zec_cksum;	/* 256-bit checksum		*/
} zio_eck_t;

/*
 * Gang block headers are self-checksumming and contain an array
 * of block pointers.
 */
#define	SPA_GANGBLOCKSIZE	SPA_MINBLOCKSIZE
#define	SPA_GBH_NBLKPTRS	((SPA_GANGBLOCKSIZE - \
	sizeof (zio_eck_t)) / sizeof (blkptr_t))
#define	SPA_GBH_FILLER		((SPA_GANGBLOCKSIZE - \
	sizeof (zio_eck_t) - \
	(SPA_GBH_NBLKPTRS * sizeof (blkptr_t))) /\
	sizeof (uint64_t))

typedef struct zio_gbh {
	blkptr_t		zg_blkptr[SPA_GBH_NBLKPTRS];
	uint64_t		zg_filler[SPA_GBH_FILLER];
	zio_eck_t		zg_tail;
} zio_gbh_phys_t;

enum zio_checksum {
	ZIO_CHECKSUM_INHERIT = 0,
	ZIO_CHECKSUM_ON,
	ZIO_CHECKSUM_OFF,
	ZIO_CHECKSUM_LABEL,
	ZIO_CHECKSUM_GANG_HEADER,
	ZIO_CHECKSUM_ZILOG,
	ZIO_CHECKSUM_FLETCHER_2,
	ZIO_CHECKSUM_FLETCHER_4,
	ZIO_CHECKSUM_SHA256,
	ZIO_CHECKSUM_ZILOG2,
	ZIO_CHECKSUM_NOPARITY,
	ZIO_CHECKSUM_SHA512,
	ZIO_CHECKSUM_SKEIN,
#ifdef illumos
	ZIO_CHECKSUM_EDONR,
#endif
	ZIO_CHECKSUM_FUNCTIONS
};

/*
 * The number of "legacy" compression functions which can be set on individual
 * objects.
 */
#define	ZIO_CHECKSUM_LEGACY_FUNCTIONS ZIO_CHECKSUM_ZILOG2

#define	ZIO_CHECKSUM_ON_VALUE	ZIO_CHECKSUM_FLETCHER_4
#define	ZIO_CHECKSUM_DEFAULT	ZIO_CHECKSUM_ON

#define	ZIO_CHECKSUM_MASK	0xffULL
#define	ZIO_CHECKSUM_VERIFY	(1 << 8)

#define	ZIO_DEDUPCHECKSUM	ZIO_CHECKSUM_SHA256
#define	ZIO_DEDUPDITTO_MIN	100

/*
 * The number of "legacy" compression functions which can be set on individual
 * objects.
 */
#define	ZIO_COMPRESS_LEGACY_FUNCTIONS ZIO_COMPRESS_LZ4

/*
 * The meaning of "compress = on" selected by the compression features enabled
 * on a given pool.
 */
#define	ZIO_COMPRESS_LEGACY_ON_VALUE	ZIO_COMPRESS_LZJB
#define	ZIO_COMPRESS_LZ4_ON_VALUE	ZIO_COMPRESS_LZ4

#define	ZIO_COMPRESS_DEFAULT		ZIO_COMPRESS_OFF

#define	BOOTFS_COMPRESS_VALID(compress)			\
	((compress) == ZIO_COMPRESS_LZJB ||		\
	(compress) == ZIO_COMPRESS_LZ4 ||		\
	(compress) == ZIO_COMPRESS_ON ||		\
	(compress) == ZIO_COMPRESS_OFF)

#define	ZIO_FAILURE_MODE_WAIT		0
#define	ZIO_FAILURE_MODE_CONTINUE	1
#define	ZIO_FAILURE_MODE_PANIC		2

enum zio_flag {
	/*
	 * Flags inherited by gang, ddt, and vdev children,
	 * and that must be equal for two zios to aggregate
	 */
	ZIO_FLAG_DONT_AGGREGATE	= 1 << 0,
	ZIO_FLAG_IO_REPAIR	= 1 << 1,
	ZIO_FLAG_SELF_HEAL	= 1 << 2,
	ZIO_FLAG_RESILVER	= 1 << 3,
	ZIO_FLAG_SCRUB		= 1 << 4,
	ZIO_FLAG_SCAN_THREAD	= 1 << 5,
	ZIO_FLAG_PHYSICAL	= 1 << 6,

#define	ZIO_FLAG_AGG_INHERIT	(ZIO_FLAG_CANFAIL - 1)

	/*
	 * Flags inherited by ddt, gang, and vdev children.
	 */
	ZIO_FLAG_CANFAIL	= 1 << 7,	/* must be first for INHERIT */
	ZIO_FLAG_SPECULATIVE	= 1 << 8,
	ZIO_FLAG_CONFIG_WRITER	= 1 << 9,
	ZIO_FLAG_DONT_RETRY	= 1 << 10,
	ZIO_FLAG_DONT_CACHE	= 1 << 11,
	ZIO_FLAG_NODATA		= 1 << 12,
	ZIO_FLAG_INDUCE_DAMAGE	= 1 << 13,
	ZIO_FLAG_IO_ALLOCATING	= 1 << 14,

#define	ZIO_FLAG_DDT_INHERIT	(ZIO_FLAG_IO_RETRY - 1)
#define	ZIO_FLAG_GANG_INHERIT	(ZIO_FLAG_IO_RETRY - 1)

	/*
	 * Flags inherited by vdev children.
	 */
	ZIO_FLAG_IO_RETRY	= 1 << 15,	/* must be first for INHERIT */
	ZIO_FLAG_PROBE		= 1 << 16,
	ZIO_FLAG_TRYHARD	= 1 << 17,
	ZIO_FLAG_OPTIONAL	= 1 << 18,

#define	ZIO_FLAG_VDEV_INHERIT	(ZIO_FLAG_DONT_QUEUE - 1)

	/*
	 * Flags not inherited by any children.
	 */
	ZIO_FLAG_DONT_QUEUE	= 1 << 19,	/* must be first for INHERIT */
	ZIO_FLAG_DONT_PROPAGATE	= 1 << 20,
	ZIO_FLAG_IO_BYPASS	= 1 << 21,
	ZIO_FLAG_IO_REWRITE	= 1 << 22,
	ZIO_FLAG_RAW		= 1 << 23,
	ZIO_FLAG_GANG_CHILD	= 1 << 24,
	ZIO_FLAG_DDT_CHILD	= 1 << 25,
	ZIO_FLAG_GODFATHER	= 1 << 26,
	ZIO_FLAG_NOPWRITE	= 1 << 27,
	ZIO_FLAG_REEXECUTED	= 1 << 28,
	ZIO_FLAG_DELEGATED	= 1 << 29,
};

#define	ZIO_FLAG_MUSTSUCCEED		0

#define	ZIO_DDT_CHILD_FLAGS(zio)				\
	(((zio)->io_flags & ZIO_FLAG_DDT_INHERIT) |		\
	ZIO_FLAG_DDT_CHILD | ZIO_FLAG_CANFAIL)

#define	ZIO_GANG_CHILD_FLAGS(zio)				\
	(((zio)->io_flags & ZIO_FLAG_GANG_INHERIT) |		\
	ZIO_FLAG_GANG_CHILD | ZIO_FLAG_CANFAIL)

#define	ZIO_VDEV_CHILD_FLAGS(zio)				\
	(((zio)->io_flags & ZIO_FLAG_VDEV_INHERIT) |		\
	ZIO_FLAG_DONT_PROPAGATE | ZIO_FLAG_CANFAIL)

#define	ZIO_CHILD_BIT(x)		(1 << (x))
#define	ZIO_CHILD_BIT_IS_SET(val, x)	((val) & (1 << (x)))

enum zio_child {
	ZIO_CHILD_VDEV = 0,
	ZIO_CHILD_GANG,
	ZIO_CHILD_DDT,
	ZIO_CHILD_LOGICAL,
	ZIO_CHILD_TYPES
};

#define	ZIO_CHILD_VDEV_BIT		ZIO_CHILD_BIT(ZIO_CHILD_VDEV)
#define	ZIO_CHILD_GANG_BIT		ZIO_CHILD_BIT(ZIO_CHILD_GANG)
#define	ZIO_CHILD_DDT_BIT		ZIO_CHILD_BIT(ZIO_CHILD_DDT)
#define	ZIO_CHILD_LOGICAL_BIT		ZIO_CHILD_BIT(ZIO_CHILD_LOGICAL)
#define	ZIO_CHILD_ALL_BITS					\
	(ZIO_CHILD_VDEV_BIT | ZIO_CHILD_GANG_BIT |		\
	ZIO_CHILD_DDT_BIT | ZIO_CHILD_LOGICAL_BIT)

enum zio_wait_type {
	ZIO_WAIT_READY = 0,
	ZIO_WAIT_DONE,
	ZIO_WAIT_TYPES
};

/*
 * We'll take the number 122 and 123 to indicate checksum errors and
 * fragmentation. Those doesn't collide with any errno values as they
 * are greater than ELAST.
 */
#define	ECKSUM	122
#define	EFRAGS	123

typedef void zio_done_func_t(zio_t *zio);

extern boolean_t zio_dva_throttle_enabled;
extern const char *zio_type_name[ZIO_TYPES];

/*
 * A bookmark is a four-tuple <objset, object, level, blkid> that uniquely
 * identifies any block in the pool.  By convention, the meta-objset (MOS)
 * is objset 0, and the meta-dnode is object 0.  This covers all blocks
 * except root blocks and ZIL blocks, which are defined as follows:
 *
 * Root blocks (objset_phys_t) are object 0, level -1:  <objset, 0, -1, 0>.
 * ZIL blocks are bookmarked <objset, 0, -2, blkid == ZIL sequence number>.
 * dmu_sync()ed ZIL data blocks are bookmarked <objset, object, -2, blkid>.
 * dnode visit bookmarks are <objset, object id of dnode, -3, 0>.
 *
 * Note: this structure is called a bookmark because its original purpose
 * was to remember where to resume a pool-wide traverse.
 *
 * Note: this structure is passed between userland and the kernel, and is
 * stored on disk (by virtue of being incorporated into other on-disk
 * structures, e.g. dsl_scan_phys_t).
 */
typedef struct zbookmark_phys {
	uint64_t	zb_objset;
	uint64_t	zb_object;
	int64_t		zb_level;
	uint64_t	zb_blkid;
} zbookmark_phys_t;

#define	SET_BOOKMARK(zb, objset, object, level, blkid)  \
{                                                       \
	(zb)->zb_objset = objset;                       \
	(zb)->zb_object = object;                       \
	(zb)->zb_level = level;                         \
	(zb)->zb_blkid = blkid;                         \
}

#define	ZB_DESTROYED_OBJSET	(-1ULL)

#define	ZB_ROOT_OBJECT		(0ULL)
#define	ZB_ROOT_LEVEL		(-1LL)
#define	ZB_ROOT_BLKID		(0ULL)

#define	ZB_ZIL_OBJECT		(0ULL)
#define	ZB_ZIL_LEVEL		(-2LL)

#define	ZB_DNODE_LEVEL		(-3LL)
#define	ZB_DNODE_BLKID		(0ULL)

#define	ZB_IS_ZERO(zb)						\
	((zb)->zb_objset == 0 && (zb)->zb_object == 0 &&	\
	(zb)->zb_level == 0 && (zb)->zb_blkid == 0)
#define	ZB_IS_ROOT(zb)				\
	((zb)->zb_object == ZB_ROOT_OBJECT &&	\
	(zb)->zb_level == ZB_ROOT_LEVEL &&	\
	(zb)->zb_blkid == ZB_ROOT_BLKID)

typedef struct zio_prop {
	enum zio_checksum	zp_checksum;
	enum zio_compress	zp_compress;
	dmu_object_type_t	zp_type;
	uint8_t			zp_level;
	uint8_t			zp_copies;
	boolean_t		zp_dedup;
	boolean_t		zp_dedup_verify;
	boolean_t		zp_nopwrite;
} zio_prop_t;

typedef struct zio_cksum_report zio_cksum_report_t;

typedef void zio_cksum_finish_f(zio_cksum_report_t *rep,
    const void *good_data);
typedef void zio_cksum_free_f(void *cbdata, size_t size);

struct zio_bad_cksum;				/* defined in zio_checksum.h */
struct dnode_phys;
struct abd;

struct zio_cksum_report {
	struct zio_cksum_report *zcr_next;
	nvlist_t		*zcr_ereport;
	nvlist_t		*zcr_detector;
	void			*zcr_cbdata;
	size_t			zcr_cbinfo;	/* passed to zcr_free() */
	uint64_t		zcr_align;
	uint64_t		zcr_length;
	zio_cksum_finish_f	*zcr_finish;
	zio_cksum_free_f	*zcr_free;

	/* internal use only */
	struct zio_bad_cksum	*zcr_ckinfo;	/* information from failure */
};

typedef void zio_vsd_cksum_report_f(zio_t *zio, zio_cksum_report_t *zcr,
    void *arg);

zio_vsd_cksum_report_f	zio_vsd_default_cksum_report;

typedef struct zio_vsd_ops {
	zio_done_func_t		*vsd_free;
	zio_vsd_cksum_report_f	*vsd_cksum_report;
} zio_vsd_ops_t;

typedef struct zio_gang_node {
	zio_gbh_phys_t		*gn_gbh;
	struct zio_gang_node	*gn_child[SPA_GBH_NBLKPTRS];
} zio_gang_node_t;

typedef zio_t *zio_gang_issue_func_t(zio_t *zio, blkptr_t *bp,
    zio_gang_node_t *gn, struct abd *data, uint64_t offset);

typedef void zio_transform_func_t(zio_t *zio, struct abd *data, uint64_t size);

typedef struct zio_transform {
	struct abd		*zt_orig_abd;
	uint64_t		zt_orig_size;
	uint64_t		zt_bufsize;
	zio_transform_func_t	*zt_transform;
	struct zio_transform	*zt_next;
} zio_transform_t;

typedef zio_t *zio_pipe_stage_t(zio_t *zio);

/*
 * The io_reexecute flags are distinct from io_flags because the child must
 * be able to propagate them to the parent.  The normal io_flags are local
 * to the zio, not protected by any lock, and not modifiable by children;
 * the reexecute flags are protected by io_lock, modifiable by children,
 * and always propagated -- even when ZIO_FLAG_DONT_PROPAGATE is set.
 */
#define	ZIO_REEXECUTE_NOW	0x01
#define	ZIO_REEXECUTE_SUSPEND	0x02

typedef struct zio_alloc_list {
	list_t  zal_list;
	uint64_t zal_size;
} zio_alloc_list_t;

typedef struct zio_link {
	zio_t		*zl_parent;
	zio_t		*zl_child;
	list_node_t	zl_parent_node;
	list_node_t	zl_child_node;
} zio_link_t;

/*
 * Used for TRIM kstat.
 */
typedef struct zio_trim_stats {
	/*
	 * Number of bytes successfully TRIMmed.
	 */
	kstat_named_t bytes;

	/*
	 * Number of successful TRIM requests.
	 */
	kstat_named_t success;

	/*
	 * Number of TRIM requests that failed because TRIM is not
	 * supported.
	 */
	kstat_named_t unsupported;

	/*
	 * Number of TRIM requests that failed for other reasons.
	 */
	kstat_named_t failed;
} zio_trim_stats_t;

extern zio_trim_stats_t zio_trim_stats;

#define ZIO_TRIM_STAT_INCR(stat, val) \
	atomic_add_64(&zio_trim_stats.stat.value.ui64, (val));
#define ZIO_TRIM_STAT_BUMP(stat) \
	ZIO_TRIM_STAT_INCR(stat, 1);

struct zio {
	/* Core information about this I/O */
	zbookmark_phys_t	io_bookmark;
	zio_prop_t	io_prop;
	zio_type_t	io_type;
	enum zio_child	io_child_type;
	int		io_cmd;
	zio_priority_t	io_priority;
	uint8_t		io_reexecute;
	uint8_t		io_state[ZIO_WAIT_TYPES];
	uint64_t	io_txg;
	spa_t		*io_spa;
	blkptr_t	*io_bp;
	blkptr_t	*io_bp_override;
	blkptr_t	io_bp_copy;
	list_t		io_parent_list;
	list_t		io_child_list;
	zio_t		*io_logical;
	zio_transform_t *io_transform_stack;

	/* Callback info */
	zio_done_func_t	*io_ready;
	zio_done_func_t	*io_children_ready;
	zio_done_func_t	*io_physdone;
	zio_done_func_t	*io_done;
	void		*io_private;
	int64_t		io_prev_space_delta;	/* DMU private */
	blkptr_t	io_bp_orig;

	/* Data represented by this I/O */
	struct abd	*io_abd;
	struct abd	*io_orig_abd;
	uint64_t	io_size;
	uint64_t	io_orig_size;
	/* io_lsize != io_orig_size iff this is a raw write */
	uint64_t	io_lsize;

	/* Stuff for the vdev stack */
	vdev_t		*io_vd;
	void		*io_vsd;
	const zio_vsd_ops_t *io_vsd_ops;

	uint64_t	io_offset;
	hrtime_t	io_timestamp;
	hrtime_t	io_queued_timestamp;
	hrtime_t	io_target_timestamp;
	avl_node_t	io_queue_node;
	avl_node_t	io_offset_node;
	avl_node_t	io_alloc_node;
	zio_alloc_list_t 	io_alloc_list;

#ifdef __FreeBSD__
	struct bio	*io_bio;
#endif

	/* Internal pipeline state */
	enum zio_flag	io_flags;
	enum zio_stage	io_stage;
	enum zio_stage	io_pipeline;
	enum zio_flag	io_orig_flags;
	enum zio_stage	io_orig_stage;
	enum zio_stage	io_orig_pipeline;
	enum zio_stage	io_pipeline_trace;
	int		io_error;
	int		io_child_error[ZIO_CHILD_TYPES];
	uint64_t	io_children[ZIO_CHILD_TYPES][ZIO_WAIT_TYPES];
	uint64_t	io_child_count;
	uint64_t	io_phys_children;
	uint64_t	io_parent_count;
	uint64_t	*io_stall;
	zio_t		*io_gang_leader;
	zio_gang_node_t	*io_gang_tree;
	void		*io_executor;
	void		*io_waiter;
	kmutex_t	io_lock;
	kcondvar_t	io_cv;
	int		io_allocator;

	/* FMA state */
	zio_cksum_report_t *io_cksum_report;
	uint64_t	io_ena;

	/* Taskq dispatching state */
	taskq_ent_t	io_tqent;

	avl_node_t	io_trim_node;
	list_node_t	io_trim_link;
};

extern int zio_bookmark_compare(const void *, const void *);

extern zio_t *zio_null(zio_t *pio, spa_t *spa, vdev_t *vd,
    zio_done_func_t *done, void *priv, enum zio_flag flags);

extern zio_t *zio_root(spa_t *spa,
    zio_done_func_t *done, void *priv, enum zio_flag flags);

extern zio_t *zio_read(zio_t *pio, spa_t *spa, const blkptr_t *bp,
    struct abd *data, uint64_t lsize, zio_done_func_t *done, void *priv,
    zio_priority_t priority, enum zio_flag flags, const zbookmark_phys_t *zb);

extern zio_t *zio_write(zio_t *pio, spa_t *spa, uint64_t txg, blkptr_t *bp,
    struct abd *data, uint64_t size, uint64_t psize, const zio_prop_t *zp,
    zio_done_func_t *ready, zio_done_func_t *children_ready,
    zio_done_func_t *physdone, zio_done_func_t *done,
    void *priv, zio_priority_t priority, enum zio_flag flags,
    const zbookmark_phys_t *zb);

extern zio_t *zio_rewrite(zio_t *pio, spa_t *spa, uint64_t txg, blkptr_t *bp,
    struct abd *data, uint64_t size, zio_done_func_t *done, void *priv,
    zio_priority_t priority, enum zio_flag flags, zbookmark_phys_t *zb);

extern void zio_write_override(zio_t *zio, blkptr_t *bp, int copies,
    boolean_t nopwrite);

extern void zio_free(spa_t *spa, uint64_t txg, const blkptr_t *bp);

extern zio_t *zio_claim(zio_t *pio, spa_t *spa, uint64_t txg,
    const blkptr_t *bp,
    zio_done_func_t *done, void *priv, enum zio_flag flags);

extern zio_t *zio_ioctl(zio_t *pio, spa_t *spa, vdev_t *vd, int cmd,
    uint64_t offset, uint64_t size, zio_done_func_t *done, void *priv,
    zio_priority_t priority, enum zio_flag flags);

extern zio_t *zio_read_phys(zio_t *pio, vdev_t *vd, uint64_t offset,
    uint64_t size, struct abd *data, int checksum,
    zio_done_func_t *done, void *priv, zio_priority_t priority,
    enum zio_flag flags, boolean_t labels);

extern zio_t *zio_write_phys(zio_t *pio, vdev_t *vd, uint64_t offset,
    uint64_t size, struct abd *data, int checksum,
    zio_done_func_t *done, void *priv, zio_priority_t priority,
    enum zio_flag flags, boolean_t labels);

extern zio_t *zio_free_sync(zio_t *pio, spa_t *spa, uint64_t txg,
    const blkptr_t *bp, uint64_t size, enum zio_flag flags);

extern int zio_alloc_zil(spa_t *spa, uint64_t objset, uint64_t txg,
    blkptr_t *new_bp, blkptr_t *old_bp, uint64_t size, boolean_t *slog);
extern void zio_flush(zio_t *zio, vdev_t *vd);
extern zio_t *zio_trim(zio_t *zio, spa_t *spa, vdev_t *vd, uint64_t offset,
    uint64_t size);
extern void zio_shrink(zio_t *zio, uint64_t size);

extern int zio_wait(zio_t *zio);
extern void zio_nowait(zio_t *zio);
extern void zio_execute(zio_t *zio);
extern void zio_interrupt(zio_t *zio);
extern void zio_delay_init(zio_t *zio);
extern void zio_delay_interrupt(zio_t *zio);

extern zio_t *zio_walk_parents(zio_t *cio, zio_link_t **);
extern zio_t *zio_walk_children(zio_t *pio, zio_link_t **);
extern zio_t *zio_unique_parent(zio_t *cio);
extern void zio_add_child(zio_t *pio, zio_t *cio);

extern void *zio_buf_alloc(size_t size);
extern void zio_buf_free(void *buf, size_t size);
extern void *zio_data_buf_alloc(size_t size);
extern void zio_data_buf_free(void *buf, size_t size);

extern void zio_push_transform(zio_t *zio, struct abd *abd, uint64_t size,
    uint64_t bufsize, zio_transform_func_t *transform);
extern void zio_pop_transforms(zio_t *zio);

extern void zio_resubmit_stage_async(void *);

extern zio_t *zio_vdev_child_io(zio_t *zio, blkptr_t *bp, vdev_t *vd,
    uint64_t offset, struct abd *data, uint64_t size, int type,
    zio_priority_t priority, enum zio_flag flags,
    zio_done_func_t *done, void *priv);

extern zio_t *zio_vdev_delegated_io(vdev_t *vd, uint64_t offset,
    struct abd *data, uint64_t size, zio_type_t type, zio_priority_t priority,
    enum zio_flag flags, zio_done_func_t *done, void *priv);

extern void zio_vdev_io_bypass(zio_t *zio);
extern void zio_vdev_io_reissue(zio_t *zio);
extern void zio_vdev_io_redone(zio_t *zio);

extern void zio_change_priority(zio_t *pio, zio_priority_t priority);

extern void zio_checksum_verified(zio_t *zio);
extern int zio_worst_error(int e1, int e2);

extern enum zio_checksum zio_checksum_select(enum zio_checksum child,
    enum zio_checksum parent);
extern enum zio_checksum zio_checksum_dedup_select(spa_t *spa,
    enum zio_checksum child, enum zio_checksum parent);
extern enum zio_compress zio_compress_select(spa_t *spa,
    enum zio_compress child, enum zio_compress parent);

extern void zio_suspend(spa_t *spa, zio_t *zio);
extern int zio_resume(spa_t *spa);
extern void zio_resume_wait(spa_t *spa);

/*
 * Initial setup and teardown.
 */
extern void zio_init(void);
extern void zio_fini(void);

/*
 * Fault injection
 */
struct zinject_record;
extern uint32_t zio_injection_enabled;
extern int zio_inject_fault(char *name, int flags, int *id,
    struct zinject_record *record);
extern int zio_inject_list_next(int *id, char *name, size_t buflen,
    struct zinject_record *record);
extern int zio_clear_fault(int id);
extern void zio_handle_panic_injection(spa_t *spa, char *tag, uint64_t type);
extern int zio_handle_fault_injection(zio_t *zio, int error);
extern int zio_handle_device_injection(vdev_t *vd, zio_t *zio, int error);
extern int zio_handle_label_injection(zio_t *zio, int error);
extern void zio_handle_ignored_writes(zio_t *zio);
extern hrtime_t zio_handle_io_delay(zio_t *zio);

/*
 * Checksum ereport functions
 */
extern void zfs_ereport_start_checksum(spa_t *spa, vdev_t *vd, struct zio *zio,
    uint64_t offset, uint64_t length, void *arg, struct zio_bad_cksum *info);
extern void zfs_ereport_finish_checksum(zio_cksum_report_t *report,
    const void *good_data, const void *bad_data, boolean_t drop_if_identical);

extern void zfs_ereport_send_interim_checksum(zio_cksum_report_t *report);
extern void zfs_ereport_free_checksum(zio_cksum_report_t *report);

/* If we have the good data in hand, this function can be used */
extern void zfs_ereport_post_checksum(spa_t *spa, vdev_t *vd,
    struct zio *zio, uint64_t offset, uint64_t length,
    const void *good_data, const void *bad_data, struct zio_bad_cksum *info);

/* Called from spa_sync(), but primarily an injection handler */
extern void spa_handle_ignored_writes(spa_t *spa);

/* zbookmark_phys functions */
boolean_t zbookmark_subtree_completed(const struct dnode_phys *dnp,
    const zbookmark_phys_t *subtree_root, const zbookmark_phys_t *last_block);
int zbookmark_compare(uint16_t dbss1, uint8_t ibs1, uint16_t dbss2,
    uint8_t ibs2, const zbookmark_phys_t *zb1, const zbookmark_phys_t *zb2);

#ifdef	__cplusplus
}
#endif

#endif	/* _ZIO_H */
