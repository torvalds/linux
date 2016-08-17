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
 * Copyright 2011 Nexenta Systems, Inc. All rights reserved.
 * Copyright (c) 2012, 2014 by Delphix. All rights reserved.
 * Copyright (c) 2013 by Saso Kiselkov. All rights reserved.
 */

#ifndef _ZIO_H
#define	_ZIO_H

#include <sys/zfs_context.h>
#include <sys/spa.h>
#include <sys/txg.h>
#include <sys/avl.h>
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

enum zio_compress {
	ZIO_COMPRESS_INHERIT = 0,
	ZIO_COMPRESS_ON,
	ZIO_COMPRESS_OFF,
	ZIO_COMPRESS_LZJB,
	ZIO_COMPRESS_EMPTY,
	ZIO_COMPRESS_GZIP_1,
	ZIO_COMPRESS_GZIP_2,
	ZIO_COMPRESS_GZIP_3,
	ZIO_COMPRESS_GZIP_4,
	ZIO_COMPRESS_GZIP_5,
	ZIO_COMPRESS_GZIP_6,
	ZIO_COMPRESS_GZIP_7,
	ZIO_COMPRESS_GZIP_8,
	ZIO_COMPRESS_GZIP_9,
	ZIO_COMPRESS_ZLE,
	ZIO_COMPRESS_LZ4,
	ZIO_COMPRESS_FUNCTIONS
};

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

/*
 * Default Linux timeout for a sd device.
 */
#define	ZIO_DELAY_MAX			(30 * MILLISEC)

#define	ZIO_FAILURE_MODE_WAIT		0
#define	ZIO_FAILURE_MODE_CONTINUE	1
#define	ZIO_FAILURE_MODE_PANIC		2

typedef enum zio_priority {
	ZIO_PRIORITY_SYNC_READ,
	ZIO_PRIORITY_SYNC_WRITE,	/* ZIL */
	ZIO_PRIORITY_ASYNC_READ,	/* prefetch */
	ZIO_PRIORITY_ASYNC_WRITE,	/* spa_sync() */
	ZIO_PRIORITY_SCRUB,		/* asynchronous scrub/resilver reads */
	ZIO_PRIORITY_NUM_QUEUEABLE,

	ZIO_PRIORITY_NOW		/* non-queued i/os (e.g. free) */
} zio_priority_t;

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

#define	ZIO_FLAG_DDT_INHERIT	(ZIO_FLAG_IO_RETRY - 1)
#define	ZIO_FLAG_GANG_INHERIT	(ZIO_FLAG_IO_RETRY - 1)

	/*
	 * Flags inherited by vdev children.
	 */
	ZIO_FLAG_IO_RETRY	= 1 << 14,	/* must be first for INHERIT */
	ZIO_FLAG_PROBE		= 1 << 15,
	ZIO_FLAG_TRYHARD	= 1 << 16,
	ZIO_FLAG_OPTIONAL	= 1 << 17,

#define	ZIO_FLAG_VDEV_INHERIT	(ZIO_FLAG_DONT_QUEUE - 1)

	/*
	 * Flags not inherited by any children.
	 */
	ZIO_FLAG_DONT_QUEUE	= 1 << 18,	/* must be first for INHERIT */
	ZIO_FLAG_DONT_PROPAGATE	= 1 << 19,
	ZIO_FLAG_IO_BYPASS	= 1 << 20,
	ZIO_FLAG_IO_REWRITE	= 1 << 21,
	ZIO_FLAG_RAW		= 1 << 22,
	ZIO_FLAG_GANG_CHILD	= 1 << 23,
	ZIO_FLAG_DDT_CHILD	= 1 << 24,
	ZIO_FLAG_GODFATHER	= 1 << 25,
	ZIO_FLAG_NOPWRITE	= 1 << 26,
	ZIO_FLAG_REEXECUTED	= 1 << 27,
	ZIO_FLAG_DELEGATED	= 1 << 28,
	ZIO_FLAG_FASTWRITE	= 1 << 29,
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
	ZIO_FLAG_CANFAIL)

enum zio_child {
	ZIO_CHILD_VDEV = 0,
	ZIO_CHILD_GANG,
	ZIO_CHILD_DDT,
	ZIO_CHILD_LOGICAL,
	ZIO_CHILD_TYPES
};

enum zio_wait_type {
	ZIO_WAIT_READY = 0,
	ZIO_WAIT_DONE,
	ZIO_WAIT_TYPES
};

/*
 * We'll take the unused errnos, 'EBADE' and 'EBADR' (from the Convergent
 * graveyard) to indicate checksum errors and fragmentation.
 */
#define	ECKSUM	EBADE
#define	EFRAGS	EBADR

typedef void zio_done_func_t(zio_t *zio);

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
 *
 * Note: this structure is called a bookmark because its original purpose
 * was to remember where to resume a pool-wide traverse.
 *
 * Note: this structure is passed between userland and the kernel, and is
 * stored on disk (by virtue of being incorporated into other on-disk
 * structures, e.g. dsl_scan_phys_t).
 */
struct zbookmark_phys {
	uint64_t	zb_objset;
	uint64_t	zb_object;
	int64_t		zb_level;
	uint64_t	zb_blkid;
};

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
    zio_gang_node_t *gn, void *data);

typedef void zio_transform_func_t(zio_t *zio, void *data, uint64_t size);

typedef struct zio_transform {
	void			*zt_orig_data;
	uint64_t		zt_orig_size;
	uint64_t		zt_bufsize;
	zio_transform_func_t	*zt_transform;
	struct zio_transform	*zt_next;
} zio_transform_t;

typedef int zio_pipe_stage_t(zio_t *zio);

/*
 * The io_reexecute flags are distinct from io_flags because the child must
 * be able to propagate them to the parent.  The normal io_flags are local
 * to the zio, not protected by any lock, and not modifiable by children;
 * the reexecute flags are protected by io_lock, modifiable by children,
 * and always propagated -- even when ZIO_FLAG_DONT_PROPAGATE is set.
 */
#define	ZIO_REEXECUTE_NOW	0x01
#define	ZIO_REEXECUTE_SUSPEND	0x02

typedef struct zio_link {
	zio_t		*zl_parent;
	zio_t		*zl_child;
	list_node_t	zl_parent_node;
	list_node_t	zl_child_node;
} zio_link_t;

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
	zio_link_t	*io_walk_link;
	zio_t		*io_logical;
	zio_transform_t *io_transform_stack;

	/* Callback info */
	zio_done_func_t *io_ready;
	zio_done_func_t	*io_physdone;
	zio_done_func_t	*io_done;
	void		*io_private;
	int64_t		io_prev_space_delta;	/* DMU private */
	blkptr_t	io_bp_orig;

	/* Data represented by this I/O */
	void		*io_data;
	void		*io_orig_data;
	uint64_t	io_size;
	uint64_t	io_orig_size;

	/* Stuff for the vdev stack */
	vdev_t		*io_vd;
	void		*io_vsd;
	const zio_vsd_ops_t *io_vsd_ops;

	uint64_t	io_offset;
	hrtime_t	io_timestamp;	/* submitted at */
	hrtime_t	io_delta;	/* vdev queue service delta */
	uint64_t	io_delay;	/* vdev disk service delta (ticks) */
	avl_node_t	io_queue_node;
	avl_node_t	io_offset_node;

	/* Internal pipeline state */
	enum zio_flag	io_flags;
	enum zio_stage	io_stage;
	enum zio_stage	io_pipeline;
	enum zio_flag	io_orig_flags;
	enum zio_stage	io_orig_stage;
	enum zio_stage	io_orig_pipeline;
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

	/* FMA state */
	zio_cksum_report_t *io_cksum_report;
	uint64_t	io_ena;

	/* Taskq dispatching state */
	taskq_ent_t	io_tqent;
};

extern zio_t *zio_null(zio_t *pio, spa_t *spa, vdev_t *vd,
    zio_done_func_t *done, void *private, enum zio_flag flags);

extern zio_t *zio_root(spa_t *spa,
    zio_done_func_t *done, void *private, enum zio_flag flags);

extern zio_t *zio_read(zio_t *pio, spa_t *spa, const blkptr_t *bp, void *data,
    uint64_t size, zio_done_func_t *done, void *private,
    zio_priority_t priority, enum zio_flag flags, const zbookmark_phys_t *zb);

extern zio_t *zio_write(zio_t *pio, spa_t *spa, uint64_t txg, blkptr_t *bp,
    void *data, uint64_t size, const zio_prop_t *zp,
    zio_done_func_t *ready, zio_done_func_t *physdone, zio_done_func_t *done,
    void *private,
    zio_priority_t priority, enum zio_flag flags, const zbookmark_phys_t *zb);

extern zio_t *zio_rewrite(zio_t *pio, spa_t *spa, uint64_t txg, blkptr_t *bp,
    void *data, uint64_t size, zio_done_func_t *done, void *private,
    zio_priority_t priority, enum zio_flag flags, zbookmark_phys_t *zb);

extern void zio_write_override(zio_t *zio, blkptr_t *bp, int copies,
    boolean_t nopwrite);

extern void zio_free(spa_t *spa, uint64_t txg, const blkptr_t *bp);

extern zio_t *zio_claim(zio_t *pio, spa_t *spa, uint64_t txg,
    const blkptr_t *bp,
    zio_done_func_t *done, void *private, enum zio_flag flags);

extern zio_t *zio_ioctl(zio_t *pio, spa_t *spa, vdev_t *vd, int cmd,
    zio_done_func_t *done, void *private, enum zio_flag flags);

extern zio_t *zio_read_phys(zio_t *pio, vdev_t *vd, uint64_t offset,
    uint64_t size, void *data, int checksum,
    zio_done_func_t *done, void *private, zio_priority_t priority,
    enum zio_flag flags, boolean_t labels);

extern zio_t *zio_write_phys(zio_t *pio, vdev_t *vd, uint64_t offset,
    uint64_t size, void *data, int checksum,
    zio_done_func_t *done, void *private, zio_priority_t priority,
    enum zio_flag flags, boolean_t labels);

extern zio_t *zio_free_sync(zio_t *pio, spa_t *spa, uint64_t txg,
    const blkptr_t *bp, enum zio_flag flags);

extern int zio_alloc_zil(spa_t *spa, uint64_t txg, blkptr_t *new_bp,
    uint64_t size, boolean_t use_slog);
extern void zio_free_zil(spa_t *spa, uint64_t txg, blkptr_t *bp);
extern void zio_flush(zio_t *zio, vdev_t *vd);
extern void zio_shrink(zio_t *zio, uint64_t size);

extern int zio_wait(zio_t *zio);
extern void zio_nowait(zio_t *zio);
extern void zio_execute(zio_t *zio);
extern void zio_interrupt(zio_t *zio);

extern zio_t *zio_walk_parents(zio_t *cio);
extern zio_t *zio_walk_children(zio_t *pio);
extern zio_t *zio_unique_parent(zio_t *cio);
extern void zio_add_child(zio_t *pio, zio_t *cio);

extern void *zio_buf_alloc(size_t size);
extern void zio_buf_free(void *buf, size_t size);
extern void *zio_data_buf_alloc(size_t size);
extern void zio_data_buf_free(void *buf, size_t size);
extern void *zio_buf_alloc_flags(size_t size, int flags);

extern void zio_resubmit_stage_async(void *);

extern zio_t *zio_vdev_child_io(zio_t *zio, blkptr_t *bp, vdev_t *vd,
    uint64_t offset, void *data, uint64_t size, int type,
    zio_priority_t priority, enum zio_flag flags,
    zio_done_func_t *done, void *private);

extern zio_t *zio_vdev_delegated_io(vdev_t *vd, uint64_t offset,
    void *data, uint64_t size, int type, zio_priority_t priority,
    enum zio_flag flags, zio_done_func_t *done, void *private);

extern void zio_vdev_io_bypass(zio_t *zio);
extern void zio_vdev_io_reissue(zio_t *zio);
extern void zio_vdev_io_redone(zio_t *zio);

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
extern uint64_t zio_handle_io_delay(zio_t *zio);

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
boolean_t zbookmark_is_before(const struct dnode_phys *dnp,
    const zbookmark_phys_t *zb1, const zbookmark_phys_t *zb2);

#ifdef	__cplusplus
}
#endif

#endif	/* _ZIO_H */
