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
 * Copyright (c) 2011, 2018 by Delphix. All rights reserved.
 * Copyright (c) 2011 Nexenta Systems, Inc. All rights reserved.
 * Copyright (c) 2014 Integros [integros.com]
 */

#include <sys/sysmacros.h>
#include <sys/zfs_context.h>
#include <sys/fm/fs/zfs.h>
#include <sys/spa.h>
#include <sys/txg.h>
#include <sys/spa_impl.h>
#include <sys/vdev_impl.h>
#include <sys/zio_impl.h>
#include <sys/zio_compress.h>
#include <sys/zio_checksum.h>
#include <sys/dmu_objset.h>
#include <sys/arc.h>
#include <sys/ddt.h>
#include <sys/trim_map.h>
#include <sys/blkptr.h>
#include <sys/zfeature.h>
#include <sys/dsl_scan.h>
#include <sys/metaslab_impl.h>
#include <sys/abd.h>
#include <sys/cityhash.h>

SYSCTL_DECL(_vfs_zfs);
SYSCTL_NODE(_vfs_zfs, OID_AUTO, zio, CTLFLAG_RW, 0, "ZFS ZIO");
#if defined(__amd64__)
static int zio_use_uma = 1;
#else
static int zio_use_uma = 0;
#endif
SYSCTL_INT(_vfs_zfs_zio, OID_AUTO, use_uma, CTLFLAG_RDTUN, &zio_use_uma, 0,
    "Use uma(9) for ZIO allocations");
static int zio_exclude_metadata = 0;
SYSCTL_INT(_vfs_zfs_zio, OID_AUTO, exclude_metadata, CTLFLAG_RDTUN, &zio_exclude_metadata, 0,
    "Exclude metadata buffers from dumps as well");

zio_trim_stats_t zio_trim_stats = {
	{ "bytes",		KSTAT_DATA_UINT64,
	  "Number of bytes successfully TRIMmed" },
	{ "success",		KSTAT_DATA_UINT64,
	  "Number of successful TRIM requests" },
	{ "unsupported",	KSTAT_DATA_UINT64,
	  "Number of TRIM requests that failed because TRIM is not supported" },
	{ "failed",		KSTAT_DATA_UINT64,
	  "Number of TRIM requests that failed for reasons other than not supported" },
};

static kstat_t *zio_trim_ksp;

/*
 * ==========================================================================
 * I/O type descriptions
 * ==========================================================================
 */
const char *zio_type_name[ZIO_TYPES] = {
	"zio_null", "zio_read", "zio_write", "zio_free", "zio_claim",
	"zio_ioctl"
};

boolean_t zio_dva_throttle_enabled = B_TRUE;
SYSCTL_INT(_vfs_zfs_zio, OID_AUTO, dva_throttle_enabled, CTLFLAG_RWTUN,
    &zio_dva_throttle_enabled, 0, "Enable allocation throttling");

/*
 * ==========================================================================
 * I/O kmem caches
 * ==========================================================================
 */
kmem_cache_t *zio_cache;
kmem_cache_t *zio_link_cache;
kmem_cache_t *zio_buf_cache[SPA_MAXBLOCKSIZE >> SPA_MINBLOCKSHIFT];
kmem_cache_t *zio_data_buf_cache[SPA_MAXBLOCKSIZE >> SPA_MINBLOCKSHIFT];

#ifdef _KERNEL
extern vmem_t *zio_alloc_arena;
#endif

#define	BP_SPANB(indblkshift, level) \
	(((uint64_t)1) << ((level) * ((indblkshift) - SPA_BLKPTRSHIFT)))
#define	COMPARE_META_LEVEL	0x80000000ul
/*
 * The following actions directly effect the spa's sync-to-convergence logic.
 * The values below define the sync pass when we start performing the action.
 * Care should be taken when changing these values as they directly impact
 * spa_sync() performance. Tuning these values may introduce subtle performance
 * pathologies and should only be done in the context of performance analysis.
 * These tunables will eventually be removed and replaced with #defines once
 * enough analysis has been done to determine optimal values.
 *
 * The 'zfs_sync_pass_deferred_free' pass must be greater than 1 to ensure that
 * regular blocks are not deferred.
 */
int zfs_sync_pass_deferred_free = 2; /* defer frees starting in this pass */
SYSCTL_INT(_vfs_zfs, OID_AUTO, sync_pass_deferred_free, CTLFLAG_RDTUN,
    &zfs_sync_pass_deferred_free, 0, "defer frees starting in this pass");
int zfs_sync_pass_dont_compress = 5; /* don't compress starting in this pass */
SYSCTL_INT(_vfs_zfs, OID_AUTO, sync_pass_dont_compress, CTLFLAG_RDTUN,
    &zfs_sync_pass_dont_compress, 0, "don't compress starting in this pass");
int zfs_sync_pass_rewrite = 2; /* rewrite new bps starting in this pass */
SYSCTL_INT(_vfs_zfs, OID_AUTO, sync_pass_rewrite, CTLFLAG_RDTUN,
    &zfs_sync_pass_rewrite, 0, "rewrite new bps starting in this pass");

/*
 * An allocating zio is one that either currently has the DVA allocate
 * stage set or will have it later in its lifetime.
 */
#define	IO_IS_ALLOCATING(zio) ((zio)->io_orig_pipeline & ZIO_STAGE_DVA_ALLOCATE)

boolean_t	zio_requeue_io_start_cut_in_line = B_TRUE;

#ifdef illumos
#ifdef ZFS_DEBUG
int zio_buf_debug_limit = 16384;
#else
int zio_buf_debug_limit = 0;
#endif
#endif

static void zio_taskq_dispatch(zio_t *, zio_taskq_type_t, boolean_t);

void
zio_init(void)
{
	size_t c;
	zio_cache = kmem_cache_create("zio_cache",
	    sizeof (zio_t), 0, NULL, NULL, NULL, NULL, NULL, 0);
	zio_link_cache = kmem_cache_create("zio_link_cache",
	    sizeof (zio_link_t), 0, NULL, NULL, NULL, NULL, NULL, 0);
	if (!zio_use_uma)
		goto out;

	/*
	 * For small buffers, we want a cache for each multiple of
	 * SPA_MINBLOCKSIZE.  For larger buffers, we want a cache
	 * for each quarter-power of 2.
	 */
	for (c = 0; c < SPA_MAXBLOCKSIZE >> SPA_MINBLOCKSHIFT; c++) {
		size_t size = (c + 1) << SPA_MINBLOCKSHIFT;
		size_t p2 = size;
		size_t align = 0;
		int cflags = zio_exclude_metadata ? KMC_NODEBUG : 0;

		while (!ISP2(p2))
			p2 &= p2 - 1;

#ifdef illumos
#ifndef _KERNEL
		/*
		 * If we are using watchpoints, put each buffer on its own page,
		 * to eliminate the performance overhead of trapping to the
		 * kernel when modifying a non-watched buffer that shares the
		 * page with a watched buffer.
		 */
		if (arc_watch && !IS_P2ALIGNED(size, PAGESIZE))
			continue;
#endif
#endif /* illumos */
		if (size <= 4 * SPA_MINBLOCKSIZE) {
			align = SPA_MINBLOCKSIZE;
		} else if (IS_P2ALIGNED(size, p2 >> 2)) {
			align = MIN(p2 >> 2, PAGESIZE);
		}

		if (align != 0) {
			char name[36];
			(void) sprintf(name, "zio_buf_%lu", (ulong_t)size);
			zio_buf_cache[c] = kmem_cache_create(name, size,
			    align, NULL, NULL, NULL, NULL, NULL, cflags);

			/*
			 * Since zio_data bufs do not appear in crash dumps, we
			 * pass KMC_NOTOUCH so that no allocator metadata is
			 * stored with the buffers.
			 */
			(void) sprintf(name, "zio_data_buf_%lu", (ulong_t)size);
			zio_data_buf_cache[c] = kmem_cache_create(name, size,
			    align, NULL, NULL, NULL, NULL, NULL,
			    cflags | KMC_NOTOUCH | KMC_NODEBUG);
		}
	}

	while (--c != 0) {
		ASSERT(zio_buf_cache[c] != NULL);
		if (zio_buf_cache[c - 1] == NULL)
			zio_buf_cache[c - 1] = zio_buf_cache[c];

		ASSERT(zio_data_buf_cache[c] != NULL);
		if (zio_data_buf_cache[c - 1] == NULL)
			zio_data_buf_cache[c - 1] = zio_data_buf_cache[c];
	}
out:

	zio_inject_init();

	zio_trim_ksp = kstat_create("zfs", 0, "zio_trim", "misc",
	    KSTAT_TYPE_NAMED,
	    sizeof(zio_trim_stats) / sizeof(kstat_named_t),
	    KSTAT_FLAG_VIRTUAL);

	if (zio_trim_ksp != NULL) {
		zio_trim_ksp->ks_data = &zio_trim_stats;
		kstat_install(zio_trim_ksp);
	}
}

void
zio_fini(void)
{
	size_t c;
	kmem_cache_t *last_cache = NULL;
	kmem_cache_t *last_data_cache = NULL;

	for (c = 0; c < SPA_MAXBLOCKSIZE >> SPA_MINBLOCKSHIFT; c++) {
		if (zio_buf_cache[c] != last_cache) {
			last_cache = zio_buf_cache[c];
			kmem_cache_destroy(zio_buf_cache[c]);
		}
		zio_buf_cache[c] = NULL;

		if (zio_data_buf_cache[c] != last_data_cache) {
			last_data_cache = zio_data_buf_cache[c];
			kmem_cache_destroy(zio_data_buf_cache[c]);
		}
		zio_data_buf_cache[c] = NULL;
	}

	kmem_cache_destroy(zio_link_cache);
	kmem_cache_destroy(zio_cache);

	zio_inject_fini();

	if (zio_trim_ksp != NULL) {
		kstat_delete(zio_trim_ksp);
		zio_trim_ksp = NULL;
	}
}

/*
 * ==========================================================================
 * Allocate and free I/O buffers
 * ==========================================================================
 */

/*
 * Use zio_buf_alloc to allocate ZFS metadata.  This data will appear in a
 * crashdump if the kernel panics, so use it judiciously.  Obviously, it's
 * useful to inspect ZFS metadata, but if possible, we should avoid keeping
 * excess / transient data in-core during a crashdump.
 */
void *
zio_buf_alloc(size_t size)
{
	size_t c = (size - 1) >> SPA_MINBLOCKSHIFT;
	int flags = zio_exclude_metadata ? KM_NODEBUG : 0;

	VERIFY3U(c, <, SPA_MAXBLOCKSIZE >> SPA_MINBLOCKSHIFT);

	if (zio_use_uma)
		return (kmem_cache_alloc(zio_buf_cache[c], KM_PUSHPAGE));
	else
		return (kmem_alloc(size, KM_SLEEP|flags));
}

/*
 * Use zio_data_buf_alloc to allocate data.  The data will not appear in a
 * crashdump if the kernel panics.  This exists so that we will limit the amount
 * of ZFS data that shows up in a kernel crashdump.  (Thus reducing the amount
 * of kernel heap dumped to disk when the kernel panics)
 */
void *
zio_data_buf_alloc(size_t size)
{
	size_t c = (size - 1) >> SPA_MINBLOCKSHIFT;

	VERIFY3U(c, <, SPA_MAXBLOCKSIZE >> SPA_MINBLOCKSHIFT);

	if (zio_use_uma)
		return (kmem_cache_alloc(zio_data_buf_cache[c], KM_PUSHPAGE));
	else
		return (kmem_alloc(size, KM_SLEEP | KM_NODEBUG));
}

void
zio_buf_free(void *buf, size_t size)
{
	size_t c = (size - 1) >> SPA_MINBLOCKSHIFT;

	VERIFY3U(c, <, SPA_MAXBLOCKSIZE >> SPA_MINBLOCKSHIFT);

	if (zio_use_uma)
		kmem_cache_free(zio_buf_cache[c], buf);
	else
		kmem_free(buf, size);
}

void
zio_data_buf_free(void *buf, size_t size)
{
	size_t c = (size - 1) >> SPA_MINBLOCKSHIFT;

	VERIFY3U(c, <, SPA_MAXBLOCKSIZE >> SPA_MINBLOCKSHIFT);

	if (zio_use_uma)
		kmem_cache_free(zio_data_buf_cache[c], buf);
	else
		kmem_free(buf, size);
}

/*
 * ==========================================================================
 * Push and pop I/O transform buffers
 * ==========================================================================
 */
void
zio_push_transform(zio_t *zio, abd_t *data, uint64_t size, uint64_t bufsize,
    zio_transform_func_t *transform)
{
	zio_transform_t *zt = kmem_alloc(sizeof (zio_transform_t), KM_SLEEP);

	/*
	 * Ensure that anyone expecting this zio to contain a linear ABD isn't
	 * going to get a nasty surprise when they try to access the data.
	 */
#ifdef illumos
	IMPLY(abd_is_linear(zio->io_abd), abd_is_linear(data));
#else
	IMPLY(zio->io_abd != NULL && abd_is_linear(zio->io_abd),
	    abd_is_linear(data));
#endif

	zt->zt_orig_abd = zio->io_abd;
	zt->zt_orig_size = zio->io_size;
	zt->zt_bufsize = bufsize;
	zt->zt_transform = transform;

	zt->zt_next = zio->io_transform_stack;
	zio->io_transform_stack = zt;

	zio->io_abd = data;
	zio->io_size = size;
}

void
zio_pop_transforms(zio_t *zio)
{
	zio_transform_t *zt;

	while ((zt = zio->io_transform_stack) != NULL) {
		if (zt->zt_transform != NULL)
			zt->zt_transform(zio,
			    zt->zt_orig_abd, zt->zt_orig_size);

		if (zt->zt_bufsize != 0)
			abd_free(zio->io_abd);

		zio->io_abd = zt->zt_orig_abd;
		zio->io_size = zt->zt_orig_size;
		zio->io_transform_stack = zt->zt_next;

		kmem_free(zt, sizeof (zio_transform_t));
	}
}

/*
 * ==========================================================================
 * I/O transform callbacks for subblocks and decompression
 * ==========================================================================
 */
static void
zio_subblock(zio_t *zio, abd_t *data, uint64_t size)
{
	ASSERT(zio->io_size > size);

	if (zio->io_type == ZIO_TYPE_READ)
		abd_copy(data, zio->io_abd, size);
}

static void
zio_decompress(zio_t *zio, abd_t *data, uint64_t size)
{
	if (zio->io_error == 0) {
		void *tmp = abd_borrow_buf(data, size);
		int ret = zio_decompress_data(BP_GET_COMPRESS(zio->io_bp),
		    zio->io_abd, tmp, zio->io_size, size);
		abd_return_buf_copy(data, tmp, size);

		if (ret != 0)
			zio->io_error = SET_ERROR(EIO);
	}
}

/*
 * ==========================================================================
 * I/O parent/child relationships and pipeline interlocks
 * ==========================================================================
 */
zio_t *
zio_walk_parents(zio_t *cio, zio_link_t **zl)
{
	list_t *pl = &cio->io_parent_list;

	*zl = (*zl == NULL) ? list_head(pl) : list_next(pl, *zl);
	if (*zl == NULL)
		return (NULL);

	ASSERT((*zl)->zl_child == cio);
	return ((*zl)->zl_parent);
}

zio_t *
zio_walk_children(zio_t *pio, zio_link_t **zl)
{
	list_t *cl = &pio->io_child_list;

	ASSERT(MUTEX_HELD(&pio->io_lock));

	*zl = (*zl == NULL) ? list_head(cl) : list_next(cl, *zl);
	if (*zl == NULL)
		return (NULL);

	ASSERT((*zl)->zl_parent == pio);
	return ((*zl)->zl_child);
}

zio_t *
zio_unique_parent(zio_t *cio)
{
	zio_link_t *zl = NULL;
	zio_t *pio = zio_walk_parents(cio, &zl);

	VERIFY3P(zio_walk_parents(cio, &zl), ==, NULL);
	return (pio);
}

void
zio_add_child(zio_t *pio, zio_t *cio)
{
	zio_link_t *zl = kmem_cache_alloc(zio_link_cache, KM_SLEEP);

	/*
	 * Logical I/Os can have logical, gang, or vdev children.
	 * Gang I/Os can have gang or vdev children.
	 * Vdev I/Os can only have vdev children.
	 * The following ASSERT captures all of these constraints.
	 */
	ASSERT3S(cio->io_child_type, <=, pio->io_child_type);

	zl->zl_parent = pio;
	zl->zl_child = cio;

	mutex_enter(&pio->io_lock);
	mutex_enter(&cio->io_lock);

	ASSERT(pio->io_state[ZIO_WAIT_DONE] == 0);

	for (int w = 0; w < ZIO_WAIT_TYPES; w++)
		pio->io_children[cio->io_child_type][w] += !cio->io_state[w];

	list_insert_head(&pio->io_child_list, zl);
	list_insert_head(&cio->io_parent_list, zl);

	pio->io_child_count++;
	cio->io_parent_count++;

	mutex_exit(&cio->io_lock);
	mutex_exit(&pio->io_lock);
}

static void
zio_remove_child(zio_t *pio, zio_t *cio, zio_link_t *zl)
{
	ASSERT(zl->zl_parent == pio);
	ASSERT(zl->zl_child == cio);

	mutex_enter(&pio->io_lock);
	mutex_enter(&cio->io_lock);

	list_remove(&pio->io_child_list, zl);
	list_remove(&cio->io_parent_list, zl);

	pio->io_child_count--;
	cio->io_parent_count--;

	mutex_exit(&cio->io_lock);
	mutex_exit(&pio->io_lock);
	kmem_cache_free(zio_link_cache, zl);
}

static boolean_t
zio_wait_for_children(zio_t *zio, uint8_t childbits, enum zio_wait_type wait)
{
	boolean_t waiting = B_FALSE;

	mutex_enter(&zio->io_lock);
	ASSERT(zio->io_stall == NULL);
	for (int c = 0; c < ZIO_CHILD_TYPES; c++) {
		if (!(ZIO_CHILD_BIT_IS_SET(childbits, c)))
			continue;

		uint64_t *countp = &zio->io_children[c][wait];
		if (*countp != 0) {
			zio->io_stage >>= 1;
			ASSERT3U(zio->io_stage, !=, ZIO_STAGE_OPEN);
			zio->io_stall = countp;
			waiting = B_TRUE;
			break;
		}
	}
	mutex_exit(&zio->io_lock);
	return (waiting);
}

static void
zio_notify_parent(zio_t *pio, zio_t *zio, enum zio_wait_type wait,
    zio_t **next_to_executep)
{
	uint64_t *countp = &pio->io_children[zio->io_child_type][wait];
	int *errorp = &pio->io_child_error[zio->io_child_type];

	mutex_enter(&pio->io_lock);
	if (zio->io_error && !(zio->io_flags & ZIO_FLAG_DONT_PROPAGATE))
		*errorp = zio_worst_error(*errorp, zio->io_error);
	pio->io_reexecute |= zio->io_reexecute;
	ASSERT3U(*countp, >, 0);

	(*countp)--;

	if (*countp == 0 && pio->io_stall == countp) {
		zio_taskq_type_t type =
		    pio->io_stage < ZIO_STAGE_VDEV_IO_START ? ZIO_TASKQ_ISSUE :
		    ZIO_TASKQ_INTERRUPT;
		pio->io_stall = NULL;
		mutex_exit(&pio->io_lock);

		/*
		 * If we can tell the caller to execute this parent next, do
		 * so.  Otherwise dispatch the parent zio as its own task.
		 *
		 * Having the caller execute the parent when possible reduces
		 * locking on the zio taskq's, reduces context switch
		 * overhead, and has no recursion penalty.  Note that one
		 * read from disk typically causes at least 3 zio's: a
		 * zio_null(), the logical zio_read(), and then a physical
		 * zio.  When the physical ZIO completes, we are able to call
		 * zio_done() on all 3 of these zio's from one invocation of
		 * zio_execute() by returning the parent back to
		 * zio_execute().  Since the parent isn't executed until this
		 * thread returns back to zio_execute(), the caller should do
		 * so promptly.
		 *
		 * In other cases, dispatching the parent prevents
		 * overflowing the stack when we have deeply nested
		 * parent-child relationships, as we do with the "mega zio"
		 * of writes for spa_sync(), and the chain of ZIL blocks.
		 */
		if (next_to_executep != NULL && *next_to_executep == NULL) {
			*next_to_executep = pio;
		} else {
			zio_taskq_dispatch(pio, type, B_FALSE);
		}
	} else {
		mutex_exit(&pio->io_lock);
	}
}

static void
zio_inherit_child_errors(zio_t *zio, enum zio_child c)
{
	if (zio->io_child_error[c] != 0 && zio->io_error == 0)
		zio->io_error = zio->io_child_error[c];
}

int
zio_bookmark_compare(const void *x1, const void *x2)
{
	const zio_t *z1 = x1;
	const zio_t *z2 = x2;

	if (z1->io_bookmark.zb_objset < z2->io_bookmark.zb_objset)
		return (-1);
	if (z1->io_bookmark.zb_objset > z2->io_bookmark.zb_objset)
		return (1);

	if (z1->io_bookmark.zb_object < z2->io_bookmark.zb_object)
		return (-1);
	if (z1->io_bookmark.zb_object > z2->io_bookmark.zb_object)
		return (1);

	if (z1->io_bookmark.zb_level < z2->io_bookmark.zb_level)
		return (-1);
	if (z1->io_bookmark.zb_level > z2->io_bookmark.zb_level)
		return (1);

	if (z1->io_bookmark.zb_blkid < z2->io_bookmark.zb_blkid)
		return (-1);
	if (z1->io_bookmark.zb_blkid > z2->io_bookmark.zb_blkid)
		return (1);

	if (z1 < z2)
		return (-1);
	if (z1 > z2)
		return (1);

	return (0);
}

/*
 * ==========================================================================
 * Create the various types of I/O (read, write, free, etc)
 * ==========================================================================
 */
static zio_t *
zio_create(zio_t *pio, spa_t *spa, uint64_t txg, const blkptr_t *bp,
    abd_t *data, uint64_t lsize, uint64_t psize, zio_done_func_t *done,
    void *private, zio_type_t type, zio_priority_t priority,
    enum zio_flag flags, vdev_t *vd, uint64_t offset,
    const zbookmark_phys_t *zb, enum zio_stage stage, enum zio_stage pipeline)
{
	zio_t *zio;

	ASSERT3U(type == ZIO_TYPE_FREE || psize, <=, SPA_MAXBLOCKSIZE);
	ASSERT(P2PHASE(psize, SPA_MINBLOCKSIZE) == 0);
	ASSERT(P2PHASE(offset, SPA_MINBLOCKSIZE) == 0);

	ASSERT(!vd || spa_config_held(spa, SCL_STATE_ALL, RW_READER));
	ASSERT(!bp || !(flags & ZIO_FLAG_CONFIG_WRITER));
	ASSERT(vd || stage == ZIO_STAGE_OPEN);

	IMPLY(lsize != psize, (flags & ZIO_FLAG_RAW) != 0);

	zio = kmem_cache_alloc(zio_cache, KM_SLEEP);
	bzero(zio, sizeof (zio_t));

	mutex_init(&zio->io_lock, NULL, MUTEX_DEFAULT, NULL);
	cv_init(&zio->io_cv, NULL, CV_DEFAULT, NULL);

	list_create(&zio->io_parent_list, sizeof (zio_link_t),
	    offsetof(zio_link_t, zl_parent_node));
	list_create(&zio->io_child_list, sizeof (zio_link_t),
	    offsetof(zio_link_t, zl_child_node));
	metaslab_trace_init(&zio->io_alloc_list);

	if (vd != NULL)
		zio->io_child_type = ZIO_CHILD_VDEV;
	else if (flags & ZIO_FLAG_GANG_CHILD)
		zio->io_child_type = ZIO_CHILD_GANG;
	else if (flags & ZIO_FLAG_DDT_CHILD)
		zio->io_child_type = ZIO_CHILD_DDT;
	else
		zio->io_child_type = ZIO_CHILD_LOGICAL;

	if (bp != NULL) {
		zio->io_bp = (blkptr_t *)bp;
		zio->io_bp_copy = *bp;
		zio->io_bp_orig = *bp;
		if (type != ZIO_TYPE_WRITE ||
		    zio->io_child_type == ZIO_CHILD_DDT)
			zio->io_bp = &zio->io_bp_copy;	/* so caller can free */
		if (zio->io_child_type == ZIO_CHILD_LOGICAL)
			zio->io_logical = zio;
		if (zio->io_child_type > ZIO_CHILD_GANG && BP_IS_GANG(bp))
			pipeline |= ZIO_GANG_STAGES;
	}

	zio->io_spa = spa;
	zio->io_txg = txg;
	zio->io_done = done;
	zio->io_private = private;
	zio->io_type = type;
	zio->io_priority = priority;
	zio->io_vd = vd;
	zio->io_offset = offset;
	zio->io_orig_abd = zio->io_abd = data;
	zio->io_orig_size = zio->io_size = psize;
	zio->io_lsize = lsize;
	zio->io_orig_flags = zio->io_flags = flags;
	zio->io_orig_stage = zio->io_stage = stage;
	zio->io_orig_pipeline = zio->io_pipeline = pipeline;
	zio->io_pipeline_trace = ZIO_STAGE_OPEN;

	zio->io_state[ZIO_WAIT_READY] = (stage >= ZIO_STAGE_READY);
	zio->io_state[ZIO_WAIT_DONE] = (stage >= ZIO_STAGE_DONE);

	if (zb != NULL)
		zio->io_bookmark = *zb;

	if (pio != NULL) {
		if (zio->io_logical == NULL)
			zio->io_logical = pio->io_logical;
		if (zio->io_child_type == ZIO_CHILD_GANG)
			zio->io_gang_leader = pio->io_gang_leader;
		zio_add_child(pio, zio);
	}

	return (zio);
}

static void
zio_destroy(zio_t *zio)
{
	metaslab_trace_fini(&zio->io_alloc_list);
	list_destroy(&zio->io_parent_list);
	list_destroy(&zio->io_child_list);
	mutex_destroy(&zio->io_lock);
	cv_destroy(&zio->io_cv);
	kmem_cache_free(zio_cache, zio);
}

zio_t *
zio_null(zio_t *pio, spa_t *spa, vdev_t *vd, zio_done_func_t *done,
    void *private, enum zio_flag flags)
{
	zio_t *zio;

	zio = zio_create(pio, spa, 0, NULL, NULL, 0, 0, done, private,
	    ZIO_TYPE_NULL, ZIO_PRIORITY_NOW, flags, vd, 0, NULL,
	    ZIO_STAGE_OPEN, ZIO_INTERLOCK_PIPELINE);

	return (zio);
}

zio_t *
zio_root(spa_t *spa, zio_done_func_t *done, void *private, enum zio_flag flags)
{
	return (zio_null(NULL, spa, NULL, done, private, flags));
}

void
zfs_blkptr_verify(spa_t *spa, const blkptr_t *bp)
{
	if (!DMU_OT_IS_VALID(BP_GET_TYPE(bp))) {
		zfs_panic_recover("blkptr at %p has invalid TYPE %llu",
		    bp, (longlong_t)BP_GET_TYPE(bp));
	}
	if (BP_GET_CHECKSUM(bp) >= ZIO_CHECKSUM_FUNCTIONS ||
	    BP_GET_CHECKSUM(bp) <= ZIO_CHECKSUM_ON) {
		zfs_panic_recover("blkptr at %p has invalid CHECKSUM %llu",
		    bp, (longlong_t)BP_GET_CHECKSUM(bp));
	}
	if (BP_GET_COMPRESS(bp) >= ZIO_COMPRESS_FUNCTIONS ||
	    BP_GET_COMPRESS(bp) <= ZIO_COMPRESS_ON) {
		zfs_panic_recover("blkptr at %p has invalid COMPRESS %llu",
		    bp, (longlong_t)BP_GET_COMPRESS(bp));
	}
	if (BP_GET_LSIZE(bp) > SPA_MAXBLOCKSIZE) {
		zfs_panic_recover("blkptr at %p has invalid LSIZE %llu",
		    bp, (longlong_t)BP_GET_LSIZE(bp));
	}
	if (BP_GET_PSIZE(bp) > SPA_MAXBLOCKSIZE) {
		zfs_panic_recover("blkptr at %p has invalid PSIZE %llu",
		    bp, (longlong_t)BP_GET_PSIZE(bp));
	}

	if (BP_IS_EMBEDDED(bp)) {
		if (BPE_GET_ETYPE(bp) > NUM_BP_EMBEDDED_TYPES) {
			zfs_panic_recover("blkptr at %p has invalid ETYPE %llu",
			    bp, (longlong_t)BPE_GET_ETYPE(bp));
		}
	}

	/*
	 * Do not verify individual DVAs if the config is not trusted. This
	 * will be done once the zio is executed in vdev_mirror_map_alloc.
	 */
	if (!spa->spa_trust_config)
		return;

	/*
	 * Pool-specific checks.
	 *
	 * Note: it would be nice to verify that the blk_birth and
	 * BP_PHYSICAL_BIRTH() are not too large.  However, spa_freeze()
	 * allows the birth time of log blocks (and dmu_sync()-ed blocks
	 * that are in the log) to be arbitrarily large.
	 */
	for (int i = 0; i < BP_GET_NDVAS(bp); i++) {
		uint64_t vdevid = DVA_GET_VDEV(&bp->blk_dva[i]);
		if (vdevid >= spa->spa_root_vdev->vdev_children) {
			zfs_panic_recover("blkptr at %p DVA %u has invalid "
			    "VDEV %llu",
			    bp, i, (longlong_t)vdevid);
			continue;
		}
		vdev_t *vd = spa->spa_root_vdev->vdev_child[vdevid];
		if (vd == NULL) {
			zfs_panic_recover("blkptr at %p DVA %u has invalid "
			    "VDEV %llu",
			    bp, i, (longlong_t)vdevid);
			continue;
		}
		if (vd->vdev_ops == &vdev_hole_ops) {
			zfs_panic_recover("blkptr at %p DVA %u has hole "
			    "VDEV %llu",
			    bp, i, (longlong_t)vdevid);
			continue;
		}
		if (vd->vdev_ops == &vdev_missing_ops) {
			/*
			 * "missing" vdevs are valid during import, but we
			 * don't have their detailed info (e.g. asize), so
			 * we can't perform any more checks on them.
			 */
			continue;
		}
		uint64_t offset = DVA_GET_OFFSET(&bp->blk_dva[i]);
		uint64_t asize = DVA_GET_ASIZE(&bp->blk_dva[i]);
		if (BP_IS_GANG(bp))
			asize = vdev_psize_to_asize(vd, SPA_GANGBLOCKSIZE);
		if (offset + asize > vd->vdev_asize) {
			zfs_panic_recover("blkptr at %p DVA %u has invalid "
			    "OFFSET %llu",
			    bp, i, (longlong_t)offset);
		}
	}
}

boolean_t
zfs_dva_valid(spa_t *spa, const dva_t *dva, const blkptr_t *bp)
{
	uint64_t vdevid = DVA_GET_VDEV(dva);

	if (vdevid >= spa->spa_root_vdev->vdev_children)
		return (B_FALSE);

	vdev_t *vd = spa->spa_root_vdev->vdev_child[vdevid];
	if (vd == NULL)
		return (B_FALSE);

	if (vd->vdev_ops == &vdev_hole_ops)
		return (B_FALSE);

	if (vd->vdev_ops == &vdev_missing_ops) {
		return (B_FALSE);
	}

	uint64_t offset = DVA_GET_OFFSET(dva);
	uint64_t asize = DVA_GET_ASIZE(dva);

	if (BP_IS_GANG(bp))
		asize = vdev_psize_to_asize(vd, SPA_GANGBLOCKSIZE);
	if (offset + asize > vd->vdev_asize)
		return (B_FALSE);

	return (B_TRUE);
}

zio_t *
zio_read(zio_t *pio, spa_t *spa, const blkptr_t *bp,
    abd_t *data, uint64_t size, zio_done_func_t *done, void *private,
    zio_priority_t priority, enum zio_flag flags, const zbookmark_phys_t *zb)
{
	zio_t *zio;

	zfs_blkptr_verify(spa, bp);

	zio = zio_create(pio, spa, BP_PHYSICAL_BIRTH(bp), bp,
	    data, size, size, done, private,
	    ZIO_TYPE_READ, priority, flags, NULL, 0, zb,
	    ZIO_STAGE_OPEN, (flags & ZIO_FLAG_DDT_CHILD) ?
	    ZIO_DDT_CHILD_READ_PIPELINE : ZIO_READ_PIPELINE);

	return (zio);
}

zio_t *
zio_write(zio_t *pio, spa_t *spa, uint64_t txg, blkptr_t *bp,
    abd_t *data, uint64_t lsize, uint64_t psize, const zio_prop_t *zp,
    zio_done_func_t *ready, zio_done_func_t *children_ready,
    zio_done_func_t *physdone, zio_done_func_t *done,
    void *private, zio_priority_t priority, enum zio_flag flags,
    const zbookmark_phys_t *zb)
{
	zio_t *zio;

	ASSERT(zp->zp_checksum >= ZIO_CHECKSUM_OFF &&
	    zp->zp_checksum < ZIO_CHECKSUM_FUNCTIONS &&
	    zp->zp_compress >= ZIO_COMPRESS_OFF &&
	    zp->zp_compress < ZIO_COMPRESS_FUNCTIONS &&
	    DMU_OT_IS_VALID(zp->zp_type) &&
	    zp->zp_level < 32 &&
	    zp->zp_copies > 0 &&
	    zp->zp_copies <= spa_max_replication(spa));

	zio = zio_create(pio, spa, txg, bp, data, lsize, psize, done, private,
	    ZIO_TYPE_WRITE, priority, flags, NULL, 0, zb,
	    ZIO_STAGE_OPEN, (flags & ZIO_FLAG_DDT_CHILD) ?
	    ZIO_DDT_CHILD_WRITE_PIPELINE : ZIO_WRITE_PIPELINE);

	zio->io_ready = ready;
	zio->io_children_ready = children_ready;
	zio->io_physdone = physdone;
	zio->io_prop = *zp;

	/*
	 * Data can be NULL if we are going to call zio_write_override() to
	 * provide the already-allocated BP.  But we may need the data to
	 * verify a dedup hit (if requested).  In this case, don't try to
	 * dedup (just take the already-allocated BP verbatim).
	 */
	if (data == NULL && zio->io_prop.zp_dedup_verify) {
		zio->io_prop.zp_dedup = zio->io_prop.zp_dedup_verify = B_FALSE;
	}

	return (zio);
}

zio_t *
zio_rewrite(zio_t *pio, spa_t *spa, uint64_t txg, blkptr_t *bp, abd_t *data,
    uint64_t size, zio_done_func_t *done, void *private,
    zio_priority_t priority, enum zio_flag flags, zbookmark_phys_t *zb)
{
	zio_t *zio;

	zio = zio_create(pio, spa, txg, bp, data, size, size, done, private,
	    ZIO_TYPE_WRITE, priority, flags | ZIO_FLAG_IO_REWRITE, NULL, 0, zb,
	    ZIO_STAGE_OPEN, ZIO_REWRITE_PIPELINE);

	return (zio);
}

void
zio_write_override(zio_t *zio, blkptr_t *bp, int copies, boolean_t nopwrite)
{
	ASSERT(zio->io_type == ZIO_TYPE_WRITE);
	ASSERT(zio->io_child_type == ZIO_CHILD_LOGICAL);
	ASSERT(zio->io_stage == ZIO_STAGE_OPEN);
	ASSERT(zio->io_txg == spa_syncing_txg(zio->io_spa));

	/*
	 * We must reset the io_prop to match the values that existed
	 * when the bp was first written by dmu_sync() keeping in mind
	 * that nopwrite and dedup are mutually exclusive.
	 */
	zio->io_prop.zp_dedup = nopwrite ? B_FALSE : zio->io_prop.zp_dedup;
	zio->io_prop.zp_nopwrite = nopwrite;
	zio->io_prop.zp_copies = copies;
	zio->io_bp_override = bp;
}

void
zio_free(spa_t *spa, uint64_t txg, const blkptr_t *bp)
{

	zfs_blkptr_verify(spa, bp);

	/*
	 * The check for EMBEDDED is a performance optimization.  We
	 * process the free here (by ignoring it) rather than
	 * putting it on the list and then processing it in zio_free_sync().
	 */
	if (BP_IS_EMBEDDED(bp))
		return;
	metaslab_check_free(spa, bp);

	/*
	 * Frees that are for the currently-syncing txg, are not going to be
	 * deferred, and which will not need to do a read (i.e. not GANG or
	 * DEDUP), can be processed immediately.  Otherwise, put them on the
	 * in-memory list for later processing.
	 */
	if (zfs_trim_enabled || BP_IS_GANG(bp) || BP_GET_DEDUP(bp) ||
	    txg != spa->spa_syncing_txg ||
	    spa_sync_pass(spa) >= zfs_sync_pass_deferred_free) {
		bplist_append(&spa->spa_free_bplist[txg & TXG_MASK], bp);
	} else {
		VERIFY0(zio_wait(zio_free_sync(NULL, spa, txg, bp,
		    BP_GET_PSIZE(bp), 0)));
	}
}

zio_t *
zio_free_sync(zio_t *pio, spa_t *spa, uint64_t txg, const blkptr_t *bp,
    uint64_t size, enum zio_flag flags)
{
	zio_t *zio;
	enum zio_stage stage = ZIO_FREE_PIPELINE;

	ASSERT(!BP_IS_HOLE(bp));
	ASSERT(spa_syncing_txg(spa) == txg);
	ASSERT(spa_sync_pass(spa) < zfs_sync_pass_deferred_free);

	if (BP_IS_EMBEDDED(bp))
		return (zio_null(pio, spa, NULL, NULL, NULL, 0));

	metaslab_check_free(spa, bp);
	arc_freed(spa, bp);
	dsl_scan_freed(spa, bp);

	if (zfs_trim_enabled)
		stage |= ZIO_STAGE_ISSUE_ASYNC | ZIO_STAGE_VDEV_IO_START |
		    ZIO_STAGE_VDEV_IO_ASSESS;
	/*
	 * GANG and DEDUP blocks can induce a read (for the gang block header,
	 * or the DDT), so issue them asynchronously so that this thread is
	 * not tied up.
	 */
	else if (BP_IS_GANG(bp) || BP_GET_DEDUP(bp))
		stage |= ZIO_STAGE_ISSUE_ASYNC;

	flags |= ZIO_FLAG_DONT_QUEUE;

	zio = zio_create(pio, spa, txg, bp, NULL, size,
	    size, NULL, NULL, ZIO_TYPE_FREE, ZIO_PRIORITY_NOW,
	    flags, NULL, 0, NULL, ZIO_STAGE_OPEN, stage);

	return (zio);
}

zio_t *
zio_claim(zio_t *pio, spa_t *spa, uint64_t txg, const blkptr_t *bp,
    zio_done_func_t *done, void *private, enum zio_flag flags)
{
	zio_t *zio;

	zfs_blkptr_verify(spa, bp);

	if (BP_IS_EMBEDDED(bp))
		return (zio_null(pio, spa, NULL, NULL, NULL, 0));

	/*
	 * A claim is an allocation of a specific block.  Claims are needed
	 * to support immediate writes in the intent log.  The issue is that
	 * immediate writes contain committed data, but in a txg that was
	 * *not* committed.  Upon opening the pool after an unclean shutdown,
	 * the intent log claims all blocks that contain immediate write data
	 * so that the SPA knows they're in use.
	 *
	 * All claims *must* be resolved in the first txg -- before the SPA
	 * starts allocating blocks -- so that nothing is allocated twice.
	 * If txg == 0 we just verify that the block is claimable.
	 */
	ASSERT3U(spa->spa_uberblock.ub_rootbp.blk_birth, <,
	    spa_min_claim_txg(spa));
	ASSERT(txg == spa_min_claim_txg(spa) || txg == 0);
	ASSERT(!BP_GET_DEDUP(bp) || !spa_writeable(spa));	/* zdb(1M) */

	zio = zio_create(pio, spa, txg, bp, NULL, BP_GET_PSIZE(bp),
	    BP_GET_PSIZE(bp), done, private, ZIO_TYPE_CLAIM, ZIO_PRIORITY_NOW,
	    flags, NULL, 0, NULL, ZIO_STAGE_OPEN, ZIO_CLAIM_PIPELINE);
	ASSERT0(zio->io_queued_timestamp);

	return (zio);
}

zio_t *
zio_ioctl(zio_t *pio, spa_t *spa, vdev_t *vd, int cmd, uint64_t offset,
    uint64_t size, zio_done_func_t *done, void *private,
    zio_priority_t priority, enum zio_flag flags)
{
	zio_t *zio;
	int c;

	if (vd->vdev_children == 0) {
		zio = zio_create(pio, spa, 0, NULL, NULL, 0, 0, done, private,
		    ZIO_TYPE_IOCTL, ZIO_PRIORITY_NOW, flags, vd, 0, NULL,
		    ZIO_STAGE_OPEN, ZIO_IOCTL_PIPELINE);

		zio->io_cmd = cmd;
	} else {
		zio = zio_null(pio, spa, NULL, NULL, NULL, flags);

		for (c = 0; c < vd->vdev_children; c++)
			zio_nowait(zio_ioctl(zio, spa, vd->vdev_child[c], cmd,
			    offset, size, done, private, priority, flags));
	}

	return (zio);
}

zio_t *
zio_read_phys(zio_t *pio, vdev_t *vd, uint64_t offset, uint64_t size,
    abd_t *data, int checksum, zio_done_func_t *done, void *private,
    zio_priority_t priority, enum zio_flag flags, boolean_t labels)
{
	zio_t *zio;

	ASSERT(vd->vdev_children == 0);
	ASSERT(!labels || offset + size <= VDEV_LABEL_START_SIZE ||
	    offset >= vd->vdev_psize - VDEV_LABEL_END_SIZE);
	ASSERT3U(offset + size, <=, vd->vdev_psize);

	zio = zio_create(pio, vd->vdev_spa, 0, NULL, data, size, size, done,
	    private, ZIO_TYPE_READ, priority, flags | ZIO_FLAG_PHYSICAL, vd,
	    offset, NULL, ZIO_STAGE_OPEN, ZIO_READ_PHYS_PIPELINE);

	zio->io_prop.zp_checksum = checksum;

	return (zio);
}

zio_t *
zio_write_phys(zio_t *pio, vdev_t *vd, uint64_t offset, uint64_t size,
    abd_t *data, int checksum, zio_done_func_t *done, void *private,
    zio_priority_t priority, enum zio_flag flags, boolean_t labels)
{
	zio_t *zio;

	ASSERT(vd->vdev_children == 0);
	ASSERT(!labels || offset + size <= VDEV_LABEL_START_SIZE ||
	    offset >= vd->vdev_psize - VDEV_LABEL_END_SIZE);
	ASSERT3U(offset + size, <=, vd->vdev_psize);

	zio = zio_create(pio, vd->vdev_spa, 0, NULL, data, size, size, done,
	    private, ZIO_TYPE_WRITE, priority, flags | ZIO_FLAG_PHYSICAL, vd,
	    offset, NULL, ZIO_STAGE_OPEN, ZIO_WRITE_PHYS_PIPELINE);

	zio->io_prop.zp_checksum = checksum;

	if (zio_checksum_table[checksum].ci_flags & ZCHECKSUM_FLAG_EMBEDDED) {
		/*
		 * zec checksums are necessarily destructive -- they modify
		 * the end of the write buffer to hold the verifier/checksum.
		 * Therefore, we must make a local copy in case the data is
		 * being written to multiple places in parallel.
		 */
		abd_t *wbuf = abd_alloc_sametype(data, size);
		abd_copy(wbuf, data, size);

		zio_push_transform(zio, wbuf, size, size, NULL);
	}

	return (zio);
}

/*
 * Create a child I/O to do some work for us.
 */
zio_t *
zio_vdev_child_io(zio_t *pio, blkptr_t *bp, vdev_t *vd, uint64_t offset,
    abd_t *data, uint64_t size, int type, zio_priority_t priority,
    enum zio_flag flags, zio_done_func_t *done, void *private)
{
	enum zio_stage pipeline = ZIO_VDEV_CHILD_PIPELINE;
	zio_t *zio;

	/*
	 * vdev child I/Os do not propagate their error to the parent.
	 * Therefore, for correct operation the caller *must* check for
	 * and handle the error in the child i/o's done callback.
	 * The only exceptions are i/os that we don't care about
	 * (OPTIONAL or REPAIR).
	 */
	ASSERT((flags & ZIO_FLAG_OPTIONAL) || (flags & ZIO_FLAG_IO_REPAIR) ||
	    done != NULL);

	if (type == ZIO_TYPE_READ && bp != NULL) {
		/*
		 * If we have the bp, then the child should perform the
		 * checksum and the parent need not.  This pushes error
		 * detection as close to the leaves as possible and
		 * eliminates redundant checksums in the interior nodes.
		 */
		pipeline |= ZIO_STAGE_CHECKSUM_VERIFY;
		pio->io_pipeline &= ~ZIO_STAGE_CHECKSUM_VERIFY;
	}

	/* Not all IO types require vdev io done stage e.g. free */
	if (type == ZIO_TYPE_FREE &&
	    !(pio->io_pipeline & ZIO_STAGE_VDEV_IO_DONE))
		pipeline &= ~ZIO_STAGE_VDEV_IO_DONE;

	if (vd->vdev_ops->vdev_op_leaf) {
		ASSERT0(vd->vdev_children);
		offset += VDEV_LABEL_START_SIZE;
	}

	flags |= ZIO_VDEV_CHILD_FLAGS(pio);

	/*
	 * If we've decided to do a repair, the write is not speculative --
	 * even if the original read was.
	 */
	if (flags & ZIO_FLAG_IO_REPAIR)
		flags &= ~ZIO_FLAG_SPECULATIVE;

	/*
	 * If we're creating a child I/O that is not associated with a
	 * top-level vdev, then the child zio is not an allocating I/O.
	 * If this is a retried I/O then we ignore it since we will
	 * have already processed the original allocating I/O.
	 */
	if (flags & ZIO_FLAG_IO_ALLOCATING &&
	    (vd != vd->vdev_top || (flags & ZIO_FLAG_IO_RETRY))) {
		metaslab_class_t *mc = spa_normal_class(pio->io_spa);

		ASSERT(mc->mc_alloc_throttle_enabled);
		ASSERT(type == ZIO_TYPE_WRITE);
		ASSERT(priority == ZIO_PRIORITY_ASYNC_WRITE);
		ASSERT(!(flags & ZIO_FLAG_IO_REPAIR));
		ASSERT(!(pio->io_flags & ZIO_FLAG_IO_REWRITE) ||
		    pio->io_child_type == ZIO_CHILD_GANG);

		flags &= ~ZIO_FLAG_IO_ALLOCATING;
	}

	zio = zio_create(pio, pio->io_spa, pio->io_txg, bp, data, size, size,
	    done, private, type, priority, flags, vd, offset, &pio->io_bookmark,
	    ZIO_STAGE_VDEV_IO_START >> 1, pipeline);
	ASSERT3U(zio->io_child_type, ==, ZIO_CHILD_VDEV);

	zio->io_physdone = pio->io_physdone;
	if (vd->vdev_ops->vdev_op_leaf && zio->io_logical != NULL)
		zio->io_logical->io_phys_children++;

	return (zio);
}

zio_t *
zio_vdev_delegated_io(vdev_t *vd, uint64_t offset, abd_t *data, uint64_t size,
    zio_type_t type, zio_priority_t priority, enum zio_flag flags,
    zio_done_func_t *done, void *private)
{
	zio_t *zio;

	ASSERT(vd->vdev_ops->vdev_op_leaf);

	zio = zio_create(NULL, vd->vdev_spa, 0, NULL,
	    data, size, size, done, private, type, priority,
	    flags | ZIO_FLAG_CANFAIL | ZIO_FLAG_DONT_RETRY | ZIO_FLAG_DELEGATED,
	    vd, offset, NULL,
	    ZIO_STAGE_VDEV_IO_START >> 1, ZIO_VDEV_CHILD_PIPELINE);

	return (zio);
}

void
zio_flush(zio_t *zio, vdev_t *vd)
{
	zio_nowait(zio_ioctl(zio, zio->io_spa, vd, DKIOCFLUSHWRITECACHE, 0, 0,
	    NULL, NULL, ZIO_PRIORITY_NOW,
	    ZIO_FLAG_CANFAIL | ZIO_FLAG_DONT_PROPAGATE | ZIO_FLAG_DONT_RETRY));
}

zio_t *
zio_trim(zio_t *zio, spa_t *spa, vdev_t *vd, uint64_t offset, uint64_t size)
{

	ASSERT(vd->vdev_ops->vdev_op_leaf);

	return (zio_create(zio, spa, 0, NULL, NULL, size, size, NULL, NULL,
	    ZIO_TYPE_FREE, ZIO_PRIORITY_TRIM, ZIO_FLAG_DONT_AGGREGATE |
	    ZIO_FLAG_CANFAIL | ZIO_FLAG_DONT_PROPAGATE | ZIO_FLAG_DONT_RETRY,
	    vd, offset, NULL, ZIO_STAGE_OPEN, ZIO_FREE_PHYS_PIPELINE));
}

void
zio_shrink(zio_t *zio, uint64_t size)
{
	ASSERT3P(zio->io_executor, ==, NULL);
	ASSERT3P(zio->io_orig_size, ==, zio->io_size);
	ASSERT3U(size, <=, zio->io_size);

	/*
	 * We don't shrink for raidz because of problems with the
	 * reconstruction when reading back less than the block size.
	 * Note, BP_IS_RAIDZ() assumes no compression.
	 */
	ASSERT(BP_GET_COMPRESS(zio->io_bp) == ZIO_COMPRESS_OFF);
	if (!BP_IS_RAIDZ(zio->io_bp)) {
		/* we are not doing a raw write */
		ASSERT3U(zio->io_size, ==, zio->io_lsize);
		zio->io_orig_size = zio->io_size = zio->io_lsize = size;
	}
}

/*
 * ==========================================================================
 * Prepare to read and write logical blocks
 * ==========================================================================
 */

static zio_t *
zio_read_bp_init(zio_t *zio)
{
	blkptr_t *bp = zio->io_bp;

	ASSERT3P(zio->io_bp, ==, &zio->io_bp_copy);

	if (BP_GET_COMPRESS(bp) != ZIO_COMPRESS_OFF &&
	    zio->io_child_type == ZIO_CHILD_LOGICAL &&
	    !(zio->io_flags & ZIO_FLAG_RAW)) {
		uint64_t psize =
		    BP_IS_EMBEDDED(bp) ? BPE_GET_PSIZE(bp) : BP_GET_PSIZE(bp);
		zio_push_transform(zio, abd_alloc_sametype(zio->io_abd, psize),
		    psize, psize, zio_decompress);
	}

	if (BP_IS_EMBEDDED(bp) && BPE_GET_ETYPE(bp) == BP_EMBEDDED_TYPE_DATA) {
		zio->io_pipeline = ZIO_INTERLOCK_PIPELINE;

		int psize = BPE_GET_PSIZE(bp);
		void *data = abd_borrow_buf(zio->io_abd, psize);
		decode_embedded_bp_compressed(bp, data);
		abd_return_buf_copy(zio->io_abd, data, psize);
	} else {
		ASSERT(!BP_IS_EMBEDDED(bp));
		ASSERT3P(zio->io_bp, ==, &zio->io_bp_copy);
	}

	if (!DMU_OT_IS_METADATA(BP_GET_TYPE(bp)) && BP_GET_LEVEL(bp) == 0)
		zio->io_flags |= ZIO_FLAG_DONT_CACHE;

	if (BP_GET_TYPE(bp) == DMU_OT_DDT_ZAP)
		zio->io_flags |= ZIO_FLAG_DONT_CACHE;

	if (BP_GET_DEDUP(bp) && zio->io_child_type == ZIO_CHILD_LOGICAL)
		zio->io_pipeline = ZIO_DDT_READ_PIPELINE;

	return (zio);
}

static zio_t *
zio_write_bp_init(zio_t *zio)
{
	if (!IO_IS_ALLOCATING(zio))
		return (zio);

	ASSERT(zio->io_child_type != ZIO_CHILD_DDT);

	if (zio->io_bp_override) {
		blkptr_t *bp = zio->io_bp;
		zio_prop_t *zp = &zio->io_prop;

		ASSERT(bp->blk_birth != zio->io_txg);
		ASSERT(BP_GET_DEDUP(zio->io_bp_override) == 0);

		*bp = *zio->io_bp_override;
		zio->io_pipeline = ZIO_INTERLOCK_PIPELINE;

		if (BP_IS_EMBEDDED(bp))
			return (zio);

		/*
		 * If we've been overridden and nopwrite is set then
		 * set the flag accordingly to indicate that a nopwrite
		 * has already occurred.
		 */
		if (!BP_IS_HOLE(bp) && zp->zp_nopwrite) {
			ASSERT(!zp->zp_dedup);
			ASSERT3U(BP_GET_CHECKSUM(bp), ==, zp->zp_checksum);
			zio->io_flags |= ZIO_FLAG_NOPWRITE;
			return (zio);
		}

		ASSERT(!zp->zp_nopwrite);

		if (BP_IS_HOLE(bp) || !zp->zp_dedup)
			return (zio);

		ASSERT((zio_checksum_table[zp->zp_checksum].ci_flags &
		    ZCHECKSUM_FLAG_DEDUP) || zp->zp_dedup_verify);

		if (BP_GET_CHECKSUM(bp) == zp->zp_checksum) {
			BP_SET_DEDUP(bp, 1);
			zio->io_pipeline |= ZIO_STAGE_DDT_WRITE;
			return (zio);
		}

		/*
		 * We were unable to handle this as an override bp, treat
		 * it as a regular write I/O.
		 */
		zio->io_bp_override = NULL;
		*bp = zio->io_bp_orig;
		zio->io_pipeline = zio->io_orig_pipeline;
	}

	return (zio);
}

static zio_t *
zio_write_compress(zio_t *zio)
{
	spa_t *spa = zio->io_spa;
	zio_prop_t *zp = &zio->io_prop;
	enum zio_compress compress = zp->zp_compress;
	blkptr_t *bp = zio->io_bp;
	uint64_t lsize = zio->io_lsize;
	uint64_t psize = zio->io_size;
	int pass = 1;

	EQUIV(lsize != psize, (zio->io_flags & ZIO_FLAG_RAW) != 0);

	/*
	 * If our children haven't all reached the ready stage,
	 * wait for them and then repeat this pipeline stage.
	 */
	if (zio_wait_for_children(zio, ZIO_CHILD_LOGICAL_BIT |
	    ZIO_CHILD_GANG_BIT, ZIO_WAIT_READY)) {
		return (NULL);
	}

	if (!IO_IS_ALLOCATING(zio))
		return (zio);

	if (zio->io_children_ready != NULL) {
		/*
		 * Now that all our children are ready, run the callback
		 * associated with this zio in case it wants to modify the
		 * data to be written.
		 */
		ASSERT3U(zp->zp_level, >, 0);
		zio->io_children_ready(zio);
	}

	ASSERT(zio->io_child_type != ZIO_CHILD_DDT);
	ASSERT(zio->io_bp_override == NULL);

	if (!BP_IS_HOLE(bp) && bp->blk_birth == zio->io_txg) {
		/*
		 * We're rewriting an existing block, which means we're
		 * working on behalf of spa_sync().  For spa_sync() to
		 * converge, it must eventually be the case that we don't
		 * have to allocate new blocks.  But compression changes
		 * the blocksize, which forces a reallocate, and makes
		 * convergence take longer.  Therefore, after the first
		 * few passes, stop compressing to ensure convergence.
		 */
		pass = spa_sync_pass(spa);

		ASSERT(zio->io_txg == spa_syncing_txg(spa));
		ASSERT(zio->io_child_type == ZIO_CHILD_LOGICAL);
		ASSERT(!BP_GET_DEDUP(bp));

		if (pass >= zfs_sync_pass_dont_compress)
			compress = ZIO_COMPRESS_OFF;

		/* Make sure someone doesn't change their mind on overwrites */
		ASSERT(BP_IS_EMBEDDED(bp) || MIN(zp->zp_copies + BP_IS_GANG(bp),
		    spa_max_replication(spa)) == BP_GET_NDVAS(bp));
	}

	/* If it's a compressed write that is not raw, compress the buffer. */
	if (compress != ZIO_COMPRESS_OFF && psize == lsize) {
		void *cbuf = zio_buf_alloc(lsize);
		psize = zio_compress_data(compress, zio->io_abd, cbuf, lsize);
		if (psize == 0 || psize == lsize) {
			compress = ZIO_COMPRESS_OFF;
			zio_buf_free(cbuf, lsize);
		} else if (!zp->zp_dedup && psize <= BPE_PAYLOAD_SIZE &&
		    zp->zp_level == 0 && !DMU_OT_HAS_FILL(zp->zp_type) &&
		    spa_feature_is_enabled(spa, SPA_FEATURE_EMBEDDED_DATA)) {
			encode_embedded_bp_compressed(bp,
			    cbuf, compress, lsize, psize);
			BPE_SET_ETYPE(bp, BP_EMBEDDED_TYPE_DATA);
			BP_SET_TYPE(bp, zio->io_prop.zp_type);
			BP_SET_LEVEL(bp, zio->io_prop.zp_level);
			zio_buf_free(cbuf, lsize);
			bp->blk_birth = zio->io_txg;
			zio->io_pipeline = ZIO_INTERLOCK_PIPELINE;
			ASSERT(spa_feature_is_active(spa,
			    SPA_FEATURE_EMBEDDED_DATA));
			return (zio);
		} else {
			/*
			 * Round up compressed size up to the ashift
			 * of the smallest-ashift device, and zero the tail.
			 * This ensures that the compressed size of the BP
			 * (and thus compressratio property) are correct,
			 * in that we charge for the padding used to fill out
			 * the last sector.
			 */
			ASSERT3U(spa->spa_min_ashift, >=, SPA_MINBLOCKSHIFT);
			size_t rounded = (size_t)P2ROUNDUP(psize,
			    1ULL << spa->spa_min_ashift);
			if (rounded >= lsize) {
				compress = ZIO_COMPRESS_OFF;
				zio_buf_free(cbuf, lsize);
				psize = lsize;
			} else {
				abd_t *cdata = abd_get_from_buf(cbuf, lsize);
				abd_take_ownership_of_buf(cdata, B_TRUE);
				abd_zero_off(cdata, psize, rounded - psize);
				psize = rounded;
				zio_push_transform(zio, cdata,
				    psize, lsize, NULL);
			}
		}

		/*
		 * We were unable to handle this as an override bp, treat
		 * it as a regular write I/O.
		 */
		zio->io_bp_override = NULL;
		*bp = zio->io_bp_orig;
		zio->io_pipeline = zio->io_orig_pipeline;
	} else {
		ASSERT3U(psize, !=, 0);
	}

	/*
	 * The final pass of spa_sync() must be all rewrites, but the first
	 * few passes offer a trade-off: allocating blocks defers convergence,
	 * but newly allocated blocks are sequential, so they can be written
	 * to disk faster.  Therefore, we allow the first few passes of
	 * spa_sync() to allocate new blocks, but force rewrites after that.
	 * There should only be a handful of blocks after pass 1 in any case.
	 */
	if (!BP_IS_HOLE(bp) && bp->blk_birth == zio->io_txg &&
	    BP_GET_PSIZE(bp) == psize &&
	    pass >= zfs_sync_pass_rewrite) {
		ASSERT(psize != 0);
		enum zio_stage gang_stages = zio->io_pipeline & ZIO_GANG_STAGES;
		zio->io_pipeline = ZIO_REWRITE_PIPELINE | gang_stages;
		zio->io_flags |= ZIO_FLAG_IO_REWRITE;
	} else {
		BP_ZERO(bp);
		zio->io_pipeline = ZIO_WRITE_PIPELINE;
	}

	if (psize == 0) {
		if (zio->io_bp_orig.blk_birth != 0 &&
		    spa_feature_is_active(spa, SPA_FEATURE_HOLE_BIRTH)) {
			BP_SET_LSIZE(bp, lsize);
			BP_SET_TYPE(bp, zp->zp_type);
			BP_SET_LEVEL(bp, zp->zp_level);
			BP_SET_BIRTH(bp, zio->io_txg, 0);
		}
		zio->io_pipeline = ZIO_INTERLOCK_PIPELINE;
	} else {
		ASSERT(zp->zp_checksum != ZIO_CHECKSUM_GANG_HEADER);
		BP_SET_LSIZE(bp, lsize);
		BP_SET_TYPE(bp, zp->zp_type);
		BP_SET_LEVEL(bp, zp->zp_level);
		BP_SET_PSIZE(bp, psize);
		BP_SET_COMPRESS(bp, compress);
		BP_SET_CHECKSUM(bp, zp->zp_checksum);
		BP_SET_DEDUP(bp, zp->zp_dedup);
		BP_SET_BYTEORDER(bp, ZFS_HOST_BYTEORDER);
		if (zp->zp_dedup) {
			ASSERT(zio->io_child_type == ZIO_CHILD_LOGICAL);
			ASSERT(!(zio->io_flags & ZIO_FLAG_IO_REWRITE));
			zio->io_pipeline = ZIO_DDT_WRITE_PIPELINE;
		}
		if (zp->zp_nopwrite) {
			ASSERT(zio->io_child_type == ZIO_CHILD_LOGICAL);
			ASSERT(!(zio->io_flags & ZIO_FLAG_IO_REWRITE));
			zio->io_pipeline |= ZIO_STAGE_NOP_WRITE;
		}
	}
	return (zio);
}

static zio_t *
zio_free_bp_init(zio_t *zio)
{
	blkptr_t *bp = zio->io_bp;

	if (zio->io_child_type == ZIO_CHILD_LOGICAL) {
		if (BP_GET_DEDUP(bp))
			zio->io_pipeline = ZIO_DDT_FREE_PIPELINE;
	}

	ASSERT3P(zio->io_bp, ==, &zio->io_bp_copy);

	return (zio);
}

/*
 * ==========================================================================
 * Execute the I/O pipeline
 * ==========================================================================
 */

static void
zio_taskq_dispatch(zio_t *zio, zio_taskq_type_t q, boolean_t cutinline)
{
	spa_t *spa = zio->io_spa;
	zio_type_t t = zio->io_type;
	int flags = (cutinline ? TQ_FRONT : 0);

	ASSERT(q == ZIO_TASKQ_ISSUE || q == ZIO_TASKQ_INTERRUPT);

	/*
	 * If we're a config writer or a probe, the normal issue and
	 * interrupt threads may all be blocked waiting for the config lock.
	 * In this case, select the otherwise-unused taskq for ZIO_TYPE_NULL.
	 */
	if (zio->io_flags & (ZIO_FLAG_CONFIG_WRITER | ZIO_FLAG_PROBE))
		t = ZIO_TYPE_NULL;

	/*
	 * A similar issue exists for the L2ARC write thread until L2ARC 2.0.
	 */
	if (t == ZIO_TYPE_WRITE && zio->io_vd && zio->io_vd->vdev_aux)
		t = ZIO_TYPE_NULL;

	/*
	 * If this is a high priority I/O, then use the high priority taskq if
	 * available.
	 */
	if (zio->io_priority == ZIO_PRIORITY_NOW &&
	    spa->spa_zio_taskq[t][q + 1].stqs_count != 0)
		q++;

	ASSERT3U(q, <, ZIO_TASKQ_TYPES);

	/*
	 * NB: We are assuming that the zio can only be dispatched
	 * to a single taskq at a time.  It would be a grievous error
	 * to dispatch the zio to another taskq at the same time.
	 */
#if defined(illumos) || !defined(_KERNEL)
	ASSERT(zio->io_tqent.tqent_next == NULL);
#else
	ASSERT(zio->io_tqent.tqent_task.ta_pending == 0);
#endif
	spa_taskq_dispatch_ent(spa, t, q, (task_func_t *)zio_execute, zio,
	    flags, &zio->io_tqent);
}

static boolean_t
zio_taskq_member(zio_t *zio, zio_taskq_type_t q)
{
	kthread_t *executor = zio->io_executor;
	spa_t *spa = zio->io_spa;

	for (zio_type_t t = 0; t < ZIO_TYPES; t++) {
		spa_taskqs_t *tqs = &spa->spa_zio_taskq[t][q];
		uint_t i;
		for (i = 0; i < tqs->stqs_count; i++) {
			if (taskq_member(tqs->stqs_taskq[i], executor))
				return (B_TRUE);
		}
	}

	return (B_FALSE);
}

static zio_t *
zio_issue_async(zio_t *zio)
{
	zio_taskq_dispatch(zio, ZIO_TASKQ_ISSUE, B_FALSE);

	return (NULL);
}

void
zio_interrupt(zio_t *zio)
{
	zio_taskq_dispatch(zio, ZIO_TASKQ_INTERRUPT, B_FALSE);
}

void
zio_delay_interrupt(zio_t *zio)
{
	/*
	 * The timeout_generic() function isn't defined in userspace, so
	 * rather than trying to implement the function, the zio delay
	 * functionality has been disabled for userspace builds.
	 */

#ifdef _KERNEL
	/*
	 * If io_target_timestamp is zero, then no delay has been registered
	 * for this IO, thus jump to the end of this function and "skip" the
	 * delay; issuing it directly to the zio layer.
	 */
	if (zio->io_target_timestamp != 0) {
		hrtime_t now = gethrtime();

		if (now >= zio->io_target_timestamp) {
			/*
			 * This IO has already taken longer than the target
			 * delay to complete, so we don't want to delay it
			 * any longer; we "miss" the delay and issue it
			 * directly to the zio layer. This is likely due to
			 * the target latency being set to a value less than
			 * the underlying hardware can satisfy (e.g. delay
			 * set to 1ms, but the disks take 10ms to complete an
			 * IO request).
			 */

			DTRACE_PROBE2(zio__delay__miss, zio_t *, zio,
			    hrtime_t, now);

			zio_interrupt(zio);
		} else {
			hrtime_t diff = zio->io_target_timestamp - now;

			DTRACE_PROBE3(zio__delay__hit, zio_t *, zio,
			    hrtime_t, now, hrtime_t, diff);

			(void) timeout_generic(CALLOUT_NORMAL,
			    (void (*)(void *))zio_interrupt, zio, diff, 1, 0);
		}

		return;
	}
#endif

	DTRACE_PROBE1(zio__delay__skip, zio_t *, zio);
	zio_interrupt(zio);
}

/*
 * Execute the I/O pipeline until one of the following occurs:
 *
 *	(1) the I/O completes
 *	(2) the pipeline stalls waiting for dependent child I/Os
 *	(3) the I/O issues, so we're waiting for an I/O completion interrupt
 *	(4) the I/O is delegated by vdev-level caching or aggregation
 *	(5) the I/O is deferred due to vdev-level queueing
 *	(6) the I/O is handed off to another thread.
 *
 * In all cases, the pipeline stops whenever there's no CPU work; it never
 * burns a thread in cv_wait().
 *
 * There's no locking on io_stage because there's no legitimate way
 * for multiple threads to be attempting to process the same I/O.
 */
static zio_pipe_stage_t *zio_pipeline[];

void
zio_execute(zio_t *zio)
{
	ASSERT3U(zio->io_queued_timestamp, >, 0);

	while (zio->io_stage < ZIO_STAGE_DONE) {
		enum zio_stage pipeline = zio->io_pipeline;
		enum zio_stage stage = zio->io_stage;

		zio->io_executor = curthread;

		ASSERT(!MUTEX_HELD(&zio->io_lock));
		ASSERT(ISP2(stage));
		ASSERT(zio->io_stall == NULL);

		do {
			stage <<= 1;
		} while ((stage & pipeline) == 0);

		ASSERT(stage <= ZIO_STAGE_DONE);

		/*
		 * If we are in interrupt context and this pipeline stage
		 * will grab a config lock that is held across I/O,
		 * or may wait for an I/O that needs an interrupt thread
		 * to complete, issue async to avoid deadlock.
		 *
		 * For VDEV_IO_START, we cut in line so that the io will
		 * be sent to disk promptly.
		 */
		if ((stage & ZIO_BLOCKING_STAGES) && zio->io_vd == NULL &&
		    zio_taskq_member(zio, ZIO_TASKQ_INTERRUPT)) {
			boolean_t cut = (stage == ZIO_STAGE_VDEV_IO_START) ?
			    zio_requeue_io_start_cut_in_line : B_FALSE;
			zio_taskq_dispatch(zio, ZIO_TASKQ_ISSUE, cut);
			return;
		}

		zio->io_stage = stage;
		zio->io_pipeline_trace |= zio->io_stage;

		/*
		 * The zio pipeline stage returns the next zio to execute
		 * (typically the same as this one), or NULL if we should
		 * stop.
		 */
		zio = zio_pipeline[highbit64(stage) - 1](zio);

		if (zio == NULL)
			return;
	}
}

/*
 * ==========================================================================
 * Initiate I/O, either sync or async
 * ==========================================================================
 */
int
zio_wait(zio_t *zio)
{
	int error;

	ASSERT3P(zio->io_stage, ==, ZIO_STAGE_OPEN);
	ASSERT3P(zio->io_executor, ==, NULL);

	zio->io_waiter = curthread;
	ASSERT0(zio->io_queued_timestamp);
	zio->io_queued_timestamp = gethrtime();

	zio_execute(zio);

	mutex_enter(&zio->io_lock);
	while (zio->io_executor != NULL)
		cv_wait(&zio->io_cv, &zio->io_lock);
	mutex_exit(&zio->io_lock);

	error = zio->io_error;
	zio_destroy(zio);

	return (error);
}

void
zio_nowait(zio_t *zio)
{
	ASSERT3P(zio->io_executor, ==, NULL);

	if (zio->io_child_type == ZIO_CHILD_LOGICAL &&
	    zio_unique_parent(zio) == NULL) {
		/*
		 * This is a logical async I/O with no parent to wait for it.
		 * We add it to the spa_async_root_zio "Godfather" I/O which
		 * will ensure they complete prior to unloading the pool.
		 */
		spa_t *spa = zio->io_spa;

		zio_add_child(spa->spa_async_zio_root[CPU_SEQID], zio);
	}

	ASSERT0(zio->io_queued_timestamp);
	zio->io_queued_timestamp = gethrtime();
	zio_execute(zio);
}

/*
 * ==========================================================================
 * Reexecute, cancel, or suspend/resume failed I/O
 * ==========================================================================
 */

static void
zio_reexecute(zio_t *pio)
{
	zio_t *cio, *cio_next;

	ASSERT(pio->io_child_type == ZIO_CHILD_LOGICAL);
	ASSERT(pio->io_orig_stage == ZIO_STAGE_OPEN);
	ASSERT(pio->io_gang_leader == NULL);
	ASSERT(pio->io_gang_tree == NULL);

	pio->io_flags = pio->io_orig_flags;
	pio->io_stage = pio->io_orig_stage;
	pio->io_pipeline = pio->io_orig_pipeline;
	pio->io_reexecute = 0;
	pio->io_flags |= ZIO_FLAG_REEXECUTED;
	pio->io_pipeline_trace = 0;
	pio->io_error = 0;
	for (int w = 0; w < ZIO_WAIT_TYPES; w++)
		pio->io_state[w] = 0;
	for (int c = 0; c < ZIO_CHILD_TYPES; c++)
		pio->io_child_error[c] = 0;

	if (IO_IS_ALLOCATING(pio))
		BP_ZERO(pio->io_bp);

	/*
	 * As we reexecute pio's children, new children could be created.
	 * New children go to the head of pio's io_child_list, however,
	 * so we will (correctly) not reexecute them.  The key is that
	 * the remainder of pio's io_child_list, from 'cio_next' onward,
	 * cannot be affected by any side effects of reexecuting 'cio'.
	 */
	zio_link_t *zl = NULL;
	mutex_enter(&pio->io_lock);
	for (cio = zio_walk_children(pio, &zl); cio != NULL; cio = cio_next) {
		cio_next = zio_walk_children(pio, &zl);
		for (int w = 0; w < ZIO_WAIT_TYPES; w++)
			pio->io_children[cio->io_child_type][w]++;
		mutex_exit(&pio->io_lock);
		zio_reexecute(cio);
		mutex_enter(&pio->io_lock);
	}
	mutex_exit(&pio->io_lock);

	/*
	 * Now that all children have been reexecuted, execute the parent.
	 * We don't reexecute "The Godfather" I/O here as it's the
	 * responsibility of the caller to wait on it.
	 */
	if (!(pio->io_flags & ZIO_FLAG_GODFATHER)) {
		pio->io_queued_timestamp = gethrtime();
		zio_execute(pio);
	}
}

void
zio_suspend(spa_t *spa, zio_t *zio)
{
	if (spa_get_failmode(spa) == ZIO_FAILURE_MODE_PANIC)
		fm_panic("Pool '%s' has encountered an uncorrectable I/O "
		    "failure and the failure mode property for this pool "
		    "is set to panic.", spa_name(spa));

	zfs_ereport_post(FM_EREPORT_ZFS_IO_FAILURE, spa, NULL, NULL, 0, 0);

	mutex_enter(&spa->spa_suspend_lock);

	if (spa->spa_suspend_zio_root == NULL)
		spa->spa_suspend_zio_root = zio_root(spa, NULL, NULL,
		    ZIO_FLAG_CANFAIL | ZIO_FLAG_SPECULATIVE |
		    ZIO_FLAG_GODFATHER);

	spa->spa_suspended = B_TRUE;

	if (zio != NULL) {
		ASSERT(!(zio->io_flags & ZIO_FLAG_GODFATHER));
		ASSERT(zio != spa->spa_suspend_zio_root);
		ASSERT(zio->io_child_type == ZIO_CHILD_LOGICAL);
		ASSERT(zio_unique_parent(zio) == NULL);
		ASSERT(zio->io_stage == ZIO_STAGE_DONE);
		zio_add_child(spa->spa_suspend_zio_root, zio);
	}

	mutex_exit(&spa->spa_suspend_lock);
}

int
zio_resume(spa_t *spa)
{
	zio_t *pio;

	/*
	 * Reexecute all previously suspended i/o.
	 */
	mutex_enter(&spa->spa_suspend_lock);
	spa->spa_suspended = B_FALSE;
	cv_broadcast(&spa->spa_suspend_cv);
	pio = spa->spa_suspend_zio_root;
	spa->spa_suspend_zio_root = NULL;
	mutex_exit(&spa->spa_suspend_lock);

	if (pio == NULL)
		return (0);

	zio_reexecute(pio);
	return (zio_wait(pio));
}

void
zio_resume_wait(spa_t *spa)
{
	mutex_enter(&spa->spa_suspend_lock);
	while (spa_suspended(spa))
		cv_wait(&spa->spa_suspend_cv, &spa->spa_suspend_lock);
	mutex_exit(&spa->spa_suspend_lock);
}

/*
 * ==========================================================================
 * Gang blocks.
 *
 * A gang block is a collection of small blocks that looks to the DMU
 * like one large block.  When zio_dva_allocate() cannot find a block
 * of the requested size, due to either severe fragmentation or the pool
 * being nearly full, it calls zio_write_gang_block() to construct the
 * block from smaller fragments.
 *
 * A gang block consists of a gang header (zio_gbh_phys_t) and up to
 * three (SPA_GBH_NBLKPTRS) gang members.  The gang header is just like
 * an indirect block: it's an array of block pointers.  It consumes
 * only one sector and hence is allocatable regardless of fragmentation.
 * The gang header's bps point to its gang members, which hold the data.
 *
 * Gang blocks are self-checksumming, using the bp's <vdev, offset, txg>
 * as the verifier to ensure uniqueness of the SHA256 checksum.
 * Critically, the gang block bp's blk_cksum is the checksum of the data,
 * not the gang header.  This ensures that data block signatures (needed for
 * deduplication) are independent of how the block is physically stored.
 *
 * Gang blocks can be nested: a gang member may itself be a gang block.
 * Thus every gang block is a tree in which root and all interior nodes are
 * gang headers, and the leaves are normal blocks that contain user data.
 * The root of the gang tree is called the gang leader.
 *
 * To perform any operation (read, rewrite, free, claim) on a gang block,
 * zio_gang_assemble() first assembles the gang tree (minus data leaves)
 * in the io_gang_tree field of the original logical i/o by recursively
 * reading the gang leader and all gang headers below it.  This yields
 * an in-core tree containing the contents of every gang header and the
 * bps for every constituent of the gang block.
 *
 * With the gang tree now assembled, zio_gang_issue() just walks the gang tree
 * and invokes a callback on each bp.  To free a gang block, zio_gang_issue()
 * calls zio_free_gang() -- a trivial wrapper around zio_free() -- for each bp.
 * zio_claim_gang() provides a similarly trivial wrapper for zio_claim().
 * zio_read_gang() is a wrapper around zio_read() that omits reading gang
 * headers, since we already have those in io_gang_tree.  zio_rewrite_gang()
 * performs a zio_rewrite() of the data or, for gang headers, a zio_rewrite()
 * of the gang header plus zio_checksum_compute() of the data to update the
 * gang header's blk_cksum as described above.
 *
 * The two-phase assemble/issue model solves the problem of partial failure --
 * what if you'd freed part of a gang block but then couldn't read the
 * gang header for another part?  Assembling the entire gang tree first
 * ensures that all the necessary gang header I/O has succeeded before
 * starting the actual work of free, claim, or write.  Once the gang tree
 * is assembled, free and claim are in-memory operations that cannot fail.
 *
 * In the event that a gang write fails, zio_dva_unallocate() walks the
 * gang tree to immediately free (i.e. insert back into the space map)
 * everything we've allocated.  This ensures that we don't get ENOSPC
 * errors during repeated suspend/resume cycles due to a flaky device.
 *
 * Gang rewrites only happen during sync-to-convergence.  If we can't assemble
 * the gang tree, we won't modify the block, so we can safely defer the free
 * (knowing that the block is still intact).  If we *can* assemble the gang
 * tree, then even if some of the rewrites fail, zio_dva_unallocate() will free
 * each constituent bp and we can allocate a new block on the next sync pass.
 *
 * In all cases, the gang tree allows complete recovery from partial failure.
 * ==========================================================================
 */

static void
zio_gang_issue_func_done(zio_t *zio)
{
	abd_put(zio->io_abd);
}

static zio_t *
zio_read_gang(zio_t *pio, blkptr_t *bp, zio_gang_node_t *gn, abd_t *data,
    uint64_t offset)
{
	if (gn != NULL)
		return (pio);

	return (zio_read(pio, pio->io_spa, bp, abd_get_offset(data, offset),
	    BP_GET_PSIZE(bp), zio_gang_issue_func_done,
	    NULL, pio->io_priority, ZIO_GANG_CHILD_FLAGS(pio),
	    &pio->io_bookmark));
}

static zio_t *
zio_rewrite_gang(zio_t *pio, blkptr_t *bp, zio_gang_node_t *gn, abd_t *data,
    uint64_t offset)
{
	zio_t *zio;

	if (gn != NULL) {
		abd_t *gbh_abd =
		    abd_get_from_buf(gn->gn_gbh, SPA_GANGBLOCKSIZE);
		zio = zio_rewrite(pio, pio->io_spa, pio->io_txg, bp,
		    gbh_abd, SPA_GANGBLOCKSIZE, zio_gang_issue_func_done, NULL,
		    pio->io_priority, ZIO_GANG_CHILD_FLAGS(pio),
		    &pio->io_bookmark);
		/*
		 * As we rewrite each gang header, the pipeline will compute
		 * a new gang block header checksum for it; but no one will
		 * compute a new data checksum, so we do that here.  The one
		 * exception is the gang leader: the pipeline already computed
		 * its data checksum because that stage precedes gang assembly.
		 * (Presently, nothing actually uses interior data checksums;
		 * this is just good hygiene.)
		 */
		if (gn != pio->io_gang_leader->io_gang_tree) {
			abd_t *buf = abd_get_offset(data, offset);

			zio_checksum_compute(zio, BP_GET_CHECKSUM(bp),
			    buf, BP_GET_PSIZE(bp));

			abd_put(buf);
		}
		/*
		 * If we are here to damage data for testing purposes,
		 * leave the GBH alone so that we can detect the damage.
		 */
		if (pio->io_gang_leader->io_flags & ZIO_FLAG_INDUCE_DAMAGE)
			zio->io_pipeline &= ~ZIO_VDEV_IO_STAGES;
	} else {
		zio = zio_rewrite(pio, pio->io_spa, pio->io_txg, bp,
		    abd_get_offset(data, offset), BP_GET_PSIZE(bp),
		    zio_gang_issue_func_done, NULL, pio->io_priority,
		    ZIO_GANG_CHILD_FLAGS(pio), &pio->io_bookmark);
	}

	return (zio);
}

/* ARGSUSED */
static zio_t *
zio_free_gang(zio_t *pio, blkptr_t *bp, zio_gang_node_t *gn, abd_t *data,
    uint64_t offset)
{
	return (zio_free_sync(pio, pio->io_spa, pio->io_txg, bp,
	    BP_IS_GANG(bp) ? SPA_GANGBLOCKSIZE : BP_GET_PSIZE(bp),
	    ZIO_GANG_CHILD_FLAGS(pio)));
}

/* ARGSUSED */
static zio_t *
zio_claim_gang(zio_t *pio, blkptr_t *bp, zio_gang_node_t *gn, abd_t *data,
    uint64_t offset)
{
	return (zio_claim(pio, pio->io_spa, pio->io_txg, bp,
	    NULL, NULL, ZIO_GANG_CHILD_FLAGS(pio)));
}

static zio_gang_issue_func_t *zio_gang_issue_func[ZIO_TYPES] = {
	NULL,
	zio_read_gang,
	zio_rewrite_gang,
	zio_free_gang,
	zio_claim_gang,
	NULL
};

static void zio_gang_tree_assemble_done(zio_t *zio);

static zio_gang_node_t *
zio_gang_node_alloc(zio_gang_node_t **gnpp)
{
	zio_gang_node_t *gn;

	ASSERT(*gnpp == NULL);

	gn = kmem_zalloc(sizeof (*gn), KM_SLEEP);
	gn->gn_gbh = zio_buf_alloc(SPA_GANGBLOCKSIZE);
	*gnpp = gn;

	return (gn);
}

static void
zio_gang_node_free(zio_gang_node_t **gnpp)
{
	zio_gang_node_t *gn = *gnpp;

	for (int g = 0; g < SPA_GBH_NBLKPTRS; g++)
		ASSERT(gn->gn_child[g] == NULL);

	zio_buf_free(gn->gn_gbh, SPA_GANGBLOCKSIZE);
	kmem_free(gn, sizeof (*gn));
	*gnpp = NULL;
}

static void
zio_gang_tree_free(zio_gang_node_t **gnpp)
{
	zio_gang_node_t *gn = *gnpp;

	if (gn == NULL)
		return;

	for (int g = 0; g < SPA_GBH_NBLKPTRS; g++)
		zio_gang_tree_free(&gn->gn_child[g]);

	zio_gang_node_free(gnpp);
}

static void
zio_gang_tree_assemble(zio_t *gio, blkptr_t *bp, zio_gang_node_t **gnpp)
{
	zio_gang_node_t *gn = zio_gang_node_alloc(gnpp);
	abd_t *gbh_abd = abd_get_from_buf(gn->gn_gbh, SPA_GANGBLOCKSIZE);

	ASSERT(gio->io_gang_leader == gio);
	ASSERT(BP_IS_GANG(bp));

	zio_nowait(zio_read(gio, gio->io_spa, bp, gbh_abd, SPA_GANGBLOCKSIZE,
	    zio_gang_tree_assemble_done, gn, gio->io_priority,
	    ZIO_GANG_CHILD_FLAGS(gio), &gio->io_bookmark));
}

static void
zio_gang_tree_assemble_done(zio_t *zio)
{
	zio_t *gio = zio->io_gang_leader;
	zio_gang_node_t *gn = zio->io_private;
	blkptr_t *bp = zio->io_bp;

	ASSERT(gio == zio_unique_parent(zio));
	ASSERT(zio->io_child_count == 0);

	if (zio->io_error)
		return;

	/* this ABD was created from a linear buf in zio_gang_tree_assemble */
	if (BP_SHOULD_BYTESWAP(bp))
		byteswap_uint64_array(abd_to_buf(zio->io_abd), zio->io_size);

	ASSERT3P(abd_to_buf(zio->io_abd), ==, gn->gn_gbh);
	ASSERT(zio->io_size == SPA_GANGBLOCKSIZE);
	ASSERT(gn->gn_gbh->zg_tail.zec_magic == ZEC_MAGIC);

	abd_put(zio->io_abd);

	for (int g = 0; g < SPA_GBH_NBLKPTRS; g++) {
		blkptr_t *gbp = &gn->gn_gbh->zg_blkptr[g];
		if (!BP_IS_GANG(gbp))
			continue;
		zio_gang_tree_assemble(gio, gbp, &gn->gn_child[g]);
	}
}

static void
zio_gang_tree_issue(zio_t *pio, zio_gang_node_t *gn, blkptr_t *bp, abd_t *data,
    uint64_t offset)
{
	zio_t *gio = pio->io_gang_leader;
	zio_t *zio;

	ASSERT(BP_IS_GANG(bp) == !!gn);
	ASSERT(BP_GET_CHECKSUM(bp) == BP_GET_CHECKSUM(gio->io_bp));
	ASSERT(BP_GET_LSIZE(bp) == BP_GET_PSIZE(bp) || gn == gio->io_gang_tree);

	/*
	 * If you're a gang header, your data is in gn->gn_gbh.
	 * If you're a gang member, your data is in 'data' and gn == NULL.
	 */
	zio = zio_gang_issue_func[gio->io_type](pio, bp, gn, data, offset);

	if (gn != NULL) {
		ASSERT(gn->gn_gbh->zg_tail.zec_magic == ZEC_MAGIC);

		for (int g = 0; g < SPA_GBH_NBLKPTRS; g++) {
			blkptr_t *gbp = &gn->gn_gbh->zg_blkptr[g];
			if (BP_IS_HOLE(gbp))
				continue;
			zio_gang_tree_issue(zio, gn->gn_child[g], gbp, data,
			    offset);
			offset += BP_GET_PSIZE(gbp);
		}
	}

	if (gn == gio->io_gang_tree && gio->io_abd != NULL)
		ASSERT3U(gio->io_size, ==, offset);

	if (zio != pio)
		zio_nowait(zio);
}

static zio_t *
zio_gang_assemble(zio_t *zio)
{
	blkptr_t *bp = zio->io_bp;

	ASSERT(BP_IS_GANG(bp) && zio->io_gang_leader == NULL);
	ASSERT(zio->io_child_type > ZIO_CHILD_GANG);

	zio->io_gang_leader = zio;

	zio_gang_tree_assemble(zio, bp, &zio->io_gang_tree);

	return (zio);
}

static zio_t *
zio_gang_issue(zio_t *zio)
{
	blkptr_t *bp = zio->io_bp;

	if (zio_wait_for_children(zio, ZIO_CHILD_GANG_BIT, ZIO_WAIT_DONE)) {
		return (NULL);
	}

	ASSERT(BP_IS_GANG(bp) && zio->io_gang_leader == zio);
	ASSERT(zio->io_child_type > ZIO_CHILD_GANG);

	if (zio->io_child_error[ZIO_CHILD_GANG] == 0)
		zio_gang_tree_issue(zio, zio->io_gang_tree, bp, zio->io_abd,
		    0);
	else
		zio_gang_tree_free(&zio->io_gang_tree);

	zio->io_pipeline = ZIO_INTERLOCK_PIPELINE;

	return (zio);
}

static void
zio_write_gang_member_ready(zio_t *zio)
{
	zio_t *pio = zio_unique_parent(zio);
	zio_t *gio = zio->io_gang_leader;
	dva_t *cdva = zio->io_bp->blk_dva;
	dva_t *pdva = pio->io_bp->blk_dva;
	uint64_t asize;

	if (BP_IS_HOLE(zio->io_bp))
		return;

	ASSERT(BP_IS_HOLE(&zio->io_bp_orig));

	ASSERT(zio->io_child_type == ZIO_CHILD_GANG);
	ASSERT3U(zio->io_prop.zp_copies, ==, gio->io_prop.zp_copies);
	ASSERT3U(zio->io_prop.zp_copies, <=, BP_GET_NDVAS(zio->io_bp));
	ASSERT3U(pio->io_prop.zp_copies, <=, BP_GET_NDVAS(pio->io_bp));
	ASSERT3U(BP_GET_NDVAS(zio->io_bp), <=, BP_GET_NDVAS(pio->io_bp));

	mutex_enter(&pio->io_lock);
	for (int d = 0; d < BP_GET_NDVAS(zio->io_bp); d++) {
		ASSERT(DVA_GET_GANG(&pdva[d]));
		asize = DVA_GET_ASIZE(&pdva[d]);
		asize += DVA_GET_ASIZE(&cdva[d]);
		DVA_SET_ASIZE(&pdva[d], asize);
	}
	mutex_exit(&pio->io_lock);
}

static void
zio_write_gang_done(zio_t *zio)
{
	abd_put(zio->io_abd);
}

static zio_t *
zio_write_gang_block(zio_t *pio)
{
	spa_t *spa = pio->io_spa;
	metaslab_class_t *mc = spa_normal_class(spa);
	blkptr_t *bp = pio->io_bp;
	zio_t *gio = pio->io_gang_leader;
	zio_t *zio;
	zio_gang_node_t *gn, **gnpp;
	zio_gbh_phys_t *gbh;
	abd_t *gbh_abd;
	uint64_t txg = pio->io_txg;
	uint64_t resid = pio->io_size;
	uint64_t lsize;
	int copies = gio->io_prop.zp_copies;
	int gbh_copies = MIN(copies + 1, spa_max_replication(spa));
	zio_prop_t zp;
	int error;

	int flags = METASLAB_HINTBP_FAVOR | METASLAB_GANG_HEADER;
	if (pio->io_flags & ZIO_FLAG_IO_ALLOCATING) {
		ASSERT(pio->io_priority == ZIO_PRIORITY_ASYNC_WRITE);
		ASSERT(!(pio->io_flags & ZIO_FLAG_NODATA));

		flags |= METASLAB_ASYNC_ALLOC;
		VERIFY(refcount_held(&mc->mc_alloc_slots[pio->io_allocator],
		    pio));

		/*
		 * The logical zio has already placed a reservation for
		 * 'copies' allocation slots but gang blocks may require
		 * additional copies. These additional copies
		 * (i.e. gbh_copies - copies) are guaranteed to succeed
		 * since metaslab_class_throttle_reserve() always allows
		 * additional reservations for gang blocks.
		 */
		VERIFY(metaslab_class_throttle_reserve(mc, gbh_copies - copies,
		    pio->io_allocator, pio, flags));
	}

	error = metaslab_alloc(spa, mc, SPA_GANGBLOCKSIZE,
	    bp, gbh_copies, txg, pio == gio ? NULL : gio->io_bp, flags,
	    &pio->io_alloc_list, pio, pio->io_allocator);
	if (error) {
		if (pio->io_flags & ZIO_FLAG_IO_ALLOCATING) {
			ASSERT(pio->io_priority == ZIO_PRIORITY_ASYNC_WRITE);
			ASSERT(!(pio->io_flags & ZIO_FLAG_NODATA));

			/*
			 * If we failed to allocate the gang block header then
			 * we remove any additional allocation reservations that
			 * we placed here. The original reservation will
			 * be removed when the logical I/O goes to the ready
			 * stage.
			 */
			metaslab_class_throttle_unreserve(mc,
			    gbh_copies - copies, pio->io_allocator, pio);
		}
		pio->io_error = error;
		return (pio);
	}

	if (pio == gio) {
		gnpp = &gio->io_gang_tree;
	} else {
		gnpp = pio->io_private;
		ASSERT(pio->io_ready == zio_write_gang_member_ready);
	}

	gn = zio_gang_node_alloc(gnpp);
	gbh = gn->gn_gbh;
	bzero(gbh, SPA_GANGBLOCKSIZE);
	gbh_abd = abd_get_from_buf(gbh, SPA_GANGBLOCKSIZE);

	/*
	 * Create the gang header.
	 */
	zio = zio_rewrite(pio, spa, txg, bp, gbh_abd, SPA_GANGBLOCKSIZE,
	    zio_write_gang_done, NULL, pio->io_priority,
	    ZIO_GANG_CHILD_FLAGS(pio), &pio->io_bookmark);

	/*
	 * Create and nowait the gang children.
	 */
	for (int g = 0; resid != 0; resid -= lsize, g++) {
		lsize = P2ROUNDUP(resid / (SPA_GBH_NBLKPTRS - g),
		    SPA_MINBLOCKSIZE);
		ASSERT(lsize >= SPA_MINBLOCKSIZE && lsize <= resid);

		zp.zp_checksum = gio->io_prop.zp_checksum;
		zp.zp_compress = ZIO_COMPRESS_OFF;
		zp.zp_type = DMU_OT_NONE;
		zp.zp_level = 0;
		zp.zp_copies = gio->io_prop.zp_copies;
		zp.zp_dedup = B_FALSE;
		zp.zp_dedup_verify = B_FALSE;
		zp.zp_nopwrite = B_FALSE;

		zio_t *cio = zio_write(zio, spa, txg, &gbh->zg_blkptr[g],
		    abd_get_offset(pio->io_abd, pio->io_size - resid), lsize,
		    lsize, &zp, zio_write_gang_member_ready, NULL, NULL,
		    zio_write_gang_done, &gn->gn_child[g], pio->io_priority,
		    ZIO_GANG_CHILD_FLAGS(pio), &pio->io_bookmark);

		if (pio->io_flags & ZIO_FLAG_IO_ALLOCATING) {
			ASSERT(pio->io_priority == ZIO_PRIORITY_ASYNC_WRITE);
			ASSERT(!(pio->io_flags & ZIO_FLAG_NODATA));

			/*
			 * Gang children won't throttle but we should
			 * account for their work, so reserve an allocation
			 * slot for them here.
			 */
			VERIFY(metaslab_class_throttle_reserve(mc,
			    zp.zp_copies, cio->io_allocator, cio, flags));
		}
		zio_nowait(cio);
	}

	/*
	 * Set pio's pipeline to just wait for zio to finish.
	 */
	pio->io_pipeline = ZIO_INTERLOCK_PIPELINE;

	zio_nowait(zio);

	return (pio);
}

/*
 * The zio_nop_write stage in the pipeline determines if allocating a
 * new bp is necessary.  The nopwrite feature can handle writes in
 * either syncing or open context (i.e. zil writes) and as a result is
 * mutually exclusive with dedup.
 *
 * By leveraging a cryptographically secure checksum, such as SHA256, we
 * can compare the checksums of the new data and the old to determine if
 * allocating a new block is required.  Note that our requirements for
 * cryptographic strength are fairly weak: there can't be any accidental
 * hash collisions, but we don't need to be secure against intentional
 * (malicious) collisions.  To trigger a nopwrite, you have to be able
 * to write the file to begin with, and triggering an incorrect (hash
 * collision) nopwrite is no worse than simply writing to the file.
 * That said, there are no known attacks against the checksum algorithms
 * used for nopwrite, assuming that the salt and the checksums
 * themselves remain secret.
 */
static zio_t *
zio_nop_write(zio_t *zio)
{
	blkptr_t *bp = zio->io_bp;
	blkptr_t *bp_orig = &zio->io_bp_orig;
	zio_prop_t *zp = &zio->io_prop;

	ASSERT(BP_GET_LEVEL(bp) == 0);
	ASSERT(!(zio->io_flags & ZIO_FLAG_IO_REWRITE));
	ASSERT(zp->zp_nopwrite);
	ASSERT(!zp->zp_dedup);
	ASSERT(zio->io_bp_override == NULL);
	ASSERT(IO_IS_ALLOCATING(zio));

	/*
	 * Check to see if the original bp and the new bp have matching
	 * characteristics (i.e. same checksum, compression algorithms, etc).
	 * If they don't then just continue with the pipeline which will
	 * allocate a new bp.
	 */
	if (BP_IS_HOLE(bp_orig) ||
	    !(zio_checksum_table[BP_GET_CHECKSUM(bp)].ci_flags &
	    ZCHECKSUM_FLAG_NOPWRITE) ||
	    BP_GET_CHECKSUM(bp) != BP_GET_CHECKSUM(bp_orig) ||
	    BP_GET_COMPRESS(bp) != BP_GET_COMPRESS(bp_orig) ||
	    BP_GET_DEDUP(bp) != BP_GET_DEDUP(bp_orig) ||
	    zp->zp_copies != BP_GET_NDVAS(bp_orig))
		return (zio);

	/*
	 * If the checksums match then reset the pipeline so that we
	 * avoid allocating a new bp and issuing any I/O.
	 */
	if (ZIO_CHECKSUM_EQUAL(bp->blk_cksum, bp_orig->blk_cksum)) {
		ASSERT(zio_checksum_table[zp->zp_checksum].ci_flags &
		    ZCHECKSUM_FLAG_NOPWRITE);
		ASSERT3U(BP_GET_PSIZE(bp), ==, BP_GET_PSIZE(bp_orig));
		ASSERT3U(BP_GET_LSIZE(bp), ==, BP_GET_LSIZE(bp_orig));
		ASSERT(zp->zp_compress != ZIO_COMPRESS_OFF);
		ASSERT(bcmp(&bp->blk_prop, &bp_orig->blk_prop,
		    sizeof (uint64_t)) == 0);

		*bp = *bp_orig;
		zio->io_pipeline = ZIO_INTERLOCK_PIPELINE;
		zio->io_flags |= ZIO_FLAG_NOPWRITE;
	}

	return (zio);
}

/*
 * ==========================================================================
 * Dedup
 * ==========================================================================
 */
static void
zio_ddt_child_read_done(zio_t *zio)
{
	blkptr_t *bp = zio->io_bp;
	ddt_entry_t *dde = zio->io_private;
	ddt_phys_t *ddp;
	zio_t *pio = zio_unique_parent(zio);

	mutex_enter(&pio->io_lock);
	ddp = ddt_phys_select(dde, bp);
	if (zio->io_error == 0)
		ddt_phys_clear(ddp);	/* this ddp doesn't need repair */

	if (zio->io_error == 0 && dde->dde_repair_abd == NULL)
		dde->dde_repair_abd = zio->io_abd;
	else
		abd_free(zio->io_abd);
	mutex_exit(&pio->io_lock);
}

static zio_t *
zio_ddt_read_start(zio_t *zio)
{
	blkptr_t *bp = zio->io_bp;

	ASSERT(BP_GET_DEDUP(bp));
	ASSERT(BP_GET_PSIZE(bp) == zio->io_size);
	ASSERT(zio->io_child_type == ZIO_CHILD_LOGICAL);

	if (zio->io_child_error[ZIO_CHILD_DDT]) {
		ddt_t *ddt = ddt_select(zio->io_spa, bp);
		ddt_entry_t *dde = ddt_repair_start(ddt, bp);
		ddt_phys_t *ddp = dde->dde_phys;
		ddt_phys_t *ddp_self = ddt_phys_select(dde, bp);
		blkptr_t blk;

		ASSERT(zio->io_vsd == NULL);
		zio->io_vsd = dde;

		if (ddp_self == NULL)
			return (zio);

		for (int p = 0; p < DDT_PHYS_TYPES; p++, ddp++) {
			if (ddp->ddp_phys_birth == 0 || ddp == ddp_self)
				continue;
			ddt_bp_create(ddt->ddt_checksum, &dde->dde_key, ddp,
			    &blk);
			zio_nowait(zio_read(zio, zio->io_spa, &blk,
			    abd_alloc_for_io(zio->io_size, B_TRUE),
			    zio->io_size, zio_ddt_child_read_done, dde,
			    zio->io_priority, ZIO_DDT_CHILD_FLAGS(zio) |
			    ZIO_FLAG_DONT_PROPAGATE, &zio->io_bookmark));
		}
		return (zio);
	}

	zio_nowait(zio_read(zio, zio->io_spa, bp,
	    zio->io_abd, zio->io_size, NULL, NULL, zio->io_priority,
	    ZIO_DDT_CHILD_FLAGS(zio), &zio->io_bookmark));

	return (zio);
}

static zio_t *
zio_ddt_read_done(zio_t *zio)
{
	blkptr_t *bp = zio->io_bp;

	if (zio_wait_for_children(zio, ZIO_CHILD_DDT_BIT, ZIO_WAIT_DONE)) {
		return (NULL);
	}

	ASSERT(BP_GET_DEDUP(bp));
	ASSERT(BP_GET_PSIZE(bp) == zio->io_size);
	ASSERT(zio->io_child_type == ZIO_CHILD_LOGICAL);

	if (zio->io_child_error[ZIO_CHILD_DDT]) {
		ddt_t *ddt = ddt_select(zio->io_spa, bp);
		ddt_entry_t *dde = zio->io_vsd;
		if (ddt == NULL) {
			ASSERT(spa_load_state(zio->io_spa) != SPA_LOAD_NONE);
			return (zio);
		}
		if (dde == NULL) {
			zio->io_stage = ZIO_STAGE_DDT_READ_START >> 1;
			zio_taskq_dispatch(zio, ZIO_TASKQ_ISSUE, B_FALSE);
			return (NULL);
		}
		if (dde->dde_repair_abd != NULL) {
			abd_copy(zio->io_abd, dde->dde_repair_abd,
			    zio->io_size);
			zio->io_child_error[ZIO_CHILD_DDT] = 0;
		}
		ddt_repair_done(ddt, dde);
		zio->io_vsd = NULL;
	}

	ASSERT(zio->io_vsd == NULL);

	return (zio);
}

static boolean_t
zio_ddt_collision(zio_t *zio, ddt_t *ddt, ddt_entry_t *dde)
{
	spa_t *spa = zio->io_spa;
	boolean_t do_raw = (zio->io_flags & ZIO_FLAG_RAW);

	/* We should never get a raw, override zio */
	ASSERT(!(zio->io_bp_override && do_raw));

	/*
	 * Note: we compare the original data, not the transformed data,
	 * because when zio->io_bp is an override bp, we will not have
	 * pushed the I/O transforms.  That's an important optimization
	 * because otherwise we'd compress/encrypt all dmu_sync() data twice.
	 */
	for (int p = DDT_PHYS_SINGLE; p <= DDT_PHYS_TRIPLE; p++) {
		zio_t *lio = dde->dde_lead_zio[p];

		if (lio != NULL) {
			return (lio->io_orig_size != zio->io_orig_size ||
			    abd_cmp(zio->io_orig_abd, lio->io_orig_abd,
			    zio->io_orig_size) != 0);
		}
	}

	for (int p = DDT_PHYS_SINGLE; p <= DDT_PHYS_TRIPLE; p++) {
		ddt_phys_t *ddp = &dde->dde_phys[p];

		if (ddp->ddp_phys_birth != 0) {
			arc_buf_t *abuf = NULL;
			arc_flags_t aflags = ARC_FLAG_WAIT;
			int zio_flags = ZIO_FLAG_CANFAIL | ZIO_FLAG_SPECULATIVE;
			blkptr_t blk = *zio->io_bp;
			int error;

			ddt_bp_fill(ddp, &blk, ddp->ddp_phys_birth);

			ddt_exit(ddt);

			/*
			 * Intuitively, it would make more sense to compare
			 * io_abd than io_orig_abd in the raw case since you
			 * don't want to look at any transformations that have
			 * happened to the data. However, for raw I/Os the
			 * data will actually be the same in io_abd and
			 * io_orig_abd, so all we have to do is issue this as
			 * a raw ARC read.
			 */
			if (do_raw) {
				zio_flags |= ZIO_FLAG_RAW;
				ASSERT3U(zio->io_size, ==, zio->io_orig_size);
				ASSERT0(abd_cmp(zio->io_abd, zio->io_orig_abd,
				    zio->io_size));
				ASSERT3P(zio->io_transform_stack, ==, NULL);
			}

			error = arc_read(NULL, spa, &blk,
			    arc_getbuf_func, &abuf, ZIO_PRIORITY_SYNC_READ,
			    zio_flags, &aflags, &zio->io_bookmark);

			if (error == 0) {
				if (arc_buf_size(abuf) != zio->io_orig_size ||
				    abd_cmp_buf(zio->io_orig_abd, abuf->b_data,
				    zio->io_orig_size) != 0)
					error = SET_ERROR(EEXIST);
				arc_buf_destroy(abuf, &abuf);
			}

			ddt_enter(ddt);
			return (error != 0);
		}
	}

	return (B_FALSE);
}

static void
zio_ddt_child_write_ready(zio_t *zio)
{
	int p = zio->io_prop.zp_copies;
	ddt_t *ddt = ddt_select(zio->io_spa, zio->io_bp);
	ddt_entry_t *dde = zio->io_private;
	ddt_phys_t *ddp = &dde->dde_phys[p];
	zio_t *pio;

	if (zio->io_error)
		return;

	ddt_enter(ddt);

	ASSERT(dde->dde_lead_zio[p] == zio);

	ddt_phys_fill(ddp, zio->io_bp);

	zio_link_t *zl = NULL;
	while ((pio = zio_walk_parents(zio, &zl)) != NULL)
		ddt_bp_fill(ddp, pio->io_bp, zio->io_txg);

	ddt_exit(ddt);
}

static void
zio_ddt_child_write_done(zio_t *zio)
{
	int p = zio->io_prop.zp_copies;
	ddt_t *ddt = ddt_select(zio->io_spa, zio->io_bp);
	ddt_entry_t *dde = zio->io_private;
	ddt_phys_t *ddp = &dde->dde_phys[p];

	ddt_enter(ddt);

	ASSERT(ddp->ddp_refcnt == 0);
	ASSERT(dde->dde_lead_zio[p] == zio);
	dde->dde_lead_zio[p] = NULL;

	if (zio->io_error == 0) {
		zio_link_t *zl = NULL;
		while (zio_walk_parents(zio, &zl) != NULL)
			ddt_phys_addref(ddp);
	} else {
		ddt_phys_clear(ddp);
	}

	ddt_exit(ddt);
}

static void
zio_ddt_ditto_write_done(zio_t *zio)
{
	int p = DDT_PHYS_DITTO;
	zio_prop_t *zp = &zio->io_prop;
	blkptr_t *bp = zio->io_bp;
	ddt_t *ddt = ddt_select(zio->io_spa, bp);
	ddt_entry_t *dde = zio->io_private;
	ddt_phys_t *ddp = &dde->dde_phys[p];
	ddt_key_t *ddk = &dde->dde_key;

	ddt_enter(ddt);

	ASSERT(ddp->ddp_refcnt == 0);
	ASSERT(dde->dde_lead_zio[p] == zio);
	dde->dde_lead_zio[p] = NULL;

	if (zio->io_error == 0) {
		ASSERT(ZIO_CHECKSUM_EQUAL(bp->blk_cksum, ddk->ddk_cksum));
		ASSERT(zp->zp_copies < SPA_DVAS_PER_BP);
		ASSERT(zp->zp_copies == BP_GET_NDVAS(bp) - BP_IS_GANG(bp));
		if (ddp->ddp_phys_birth != 0)
			ddt_phys_free(ddt, ddk, ddp, zio->io_txg);
		ddt_phys_fill(ddp, bp);
	}

	ddt_exit(ddt);
}

static zio_t *
zio_ddt_write(zio_t *zio)
{
	spa_t *spa = zio->io_spa;
	blkptr_t *bp = zio->io_bp;
	uint64_t txg = zio->io_txg;
	zio_prop_t *zp = &zio->io_prop;
	int p = zp->zp_copies;
	int ditto_copies;
	zio_t *cio = NULL;
	zio_t *dio = NULL;
	ddt_t *ddt = ddt_select(spa, bp);
	ddt_entry_t *dde;
	ddt_phys_t *ddp;

	ASSERT(BP_GET_DEDUP(bp));
	ASSERT(BP_GET_CHECKSUM(bp) == zp->zp_checksum);
	ASSERT(BP_IS_HOLE(bp) || zio->io_bp_override);
	ASSERT(!(zio->io_bp_override && (zio->io_flags & ZIO_FLAG_RAW)));

	ddt_enter(ddt);
	dde = ddt_lookup(ddt, bp, B_TRUE);
	ddp = &dde->dde_phys[p];

	if (zp->zp_dedup_verify && zio_ddt_collision(zio, ddt, dde)) {
		/*
		 * If we're using a weak checksum, upgrade to a strong checksum
		 * and try again.  If we're already using a strong checksum,
		 * we can't resolve it, so just convert to an ordinary write.
		 * (And automatically e-mail a paper to Nature?)
		 */
		if (!(zio_checksum_table[zp->zp_checksum].ci_flags &
		    ZCHECKSUM_FLAG_DEDUP)) {
			zp->zp_checksum = spa_dedup_checksum(spa);
			zio_pop_transforms(zio);
			zio->io_stage = ZIO_STAGE_OPEN;
			BP_ZERO(bp);
		} else {
			zp->zp_dedup = B_FALSE;
			BP_SET_DEDUP(bp, B_FALSE);
		}
		ASSERT(!BP_GET_DEDUP(bp));
		zio->io_pipeline = ZIO_WRITE_PIPELINE;
		ddt_exit(ddt);
		return (zio);
	}

	ditto_copies = ddt_ditto_copies_needed(ddt, dde, ddp);
	ASSERT(ditto_copies < SPA_DVAS_PER_BP);

	if (ditto_copies > ddt_ditto_copies_present(dde) &&
	    dde->dde_lead_zio[DDT_PHYS_DITTO] == NULL) {
		zio_prop_t czp = *zp;

		czp.zp_copies = ditto_copies;

		/*
		 * If we arrived here with an override bp, we won't have run
		 * the transform stack, so we won't have the data we need to
		 * generate a child i/o.  So, toss the override bp and restart.
		 * This is safe, because using the override bp is just an
		 * optimization; and it's rare, so the cost doesn't matter.
		 */
		if (zio->io_bp_override) {
			zio_pop_transforms(zio);
			zio->io_stage = ZIO_STAGE_OPEN;
			zio->io_pipeline = ZIO_WRITE_PIPELINE;
			zio->io_bp_override = NULL;
			BP_ZERO(bp);
			ddt_exit(ddt);
			return (zio);
		}

		dio = zio_write(zio, spa, txg, bp, zio->io_orig_abd,
		    zio->io_orig_size, zio->io_orig_size, &czp, NULL, NULL,
		    NULL, zio_ddt_ditto_write_done, dde, zio->io_priority,
		    ZIO_DDT_CHILD_FLAGS(zio), &zio->io_bookmark);

		zio_push_transform(dio, zio->io_abd, zio->io_size, 0, NULL);
		dde->dde_lead_zio[DDT_PHYS_DITTO] = dio;
	}

	if (ddp->ddp_phys_birth != 0 || dde->dde_lead_zio[p] != NULL) {
		if (ddp->ddp_phys_birth != 0)
			ddt_bp_fill(ddp, bp, txg);
		if (dde->dde_lead_zio[p] != NULL)
			zio_add_child(zio, dde->dde_lead_zio[p]);
		else
			ddt_phys_addref(ddp);
	} else if (zio->io_bp_override) {
		ASSERT(bp->blk_birth == txg);
		ASSERT(BP_EQUAL(bp, zio->io_bp_override));
		ddt_phys_fill(ddp, bp);
		ddt_phys_addref(ddp);
	} else {
		cio = zio_write(zio, spa, txg, bp, zio->io_orig_abd,
		    zio->io_orig_size, zio->io_orig_size, zp,
		    zio_ddt_child_write_ready, NULL, NULL,
		    zio_ddt_child_write_done, dde, zio->io_priority,
		    ZIO_DDT_CHILD_FLAGS(zio), &zio->io_bookmark);

		zio_push_transform(cio, zio->io_abd, zio->io_size, 0, NULL);
		dde->dde_lead_zio[p] = cio;
	}

	ddt_exit(ddt);

	if (cio)
		zio_nowait(cio);
	if (dio)
		zio_nowait(dio);

	return (zio);
}

ddt_entry_t *freedde; /* for debugging */

static zio_t *
zio_ddt_free(zio_t *zio)
{
	spa_t *spa = zio->io_spa;
	blkptr_t *bp = zio->io_bp;
	ddt_t *ddt = ddt_select(spa, bp);
	ddt_entry_t *dde;
	ddt_phys_t *ddp;

	ASSERT(BP_GET_DEDUP(bp));
	ASSERT(zio->io_child_type == ZIO_CHILD_LOGICAL);

	ddt_enter(ddt);
	freedde = dde = ddt_lookup(ddt, bp, B_TRUE);
	ddp = ddt_phys_select(dde, bp);
	ddt_phys_decref(ddp);
	ddt_exit(ddt);

	return (zio);
}

/*
 * ==========================================================================
 * Allocate and free blocks
 * ==========================================================================
 */

static zio_t *
zio_io_to_allocate(spa_t *spa, int allocator)
{
	zio_t *zio;

	ASSERT(MUTEX_HELD(&spa->spa_alloc_locks[allocator]));

	zio = avl_first(&spa->spa_alloc_trees[allocator]);
	if (zio == NULL)
		return (NULL);

	ASSERT(IO_IS_ALLOCATING(zio));

	/*
	 * Try to place a reservation for this zio. If we're unable to
	 * reserve then we throttle.
	 */
	ASSERT3U(zio->io_allocator, ==, allocator);
	if (!metaslab_class_throttle_reserve(spa_normal_class(spa),
	    zio->io_prop.zp_copies, zio->io_allocator, zio, 0)) {
		return (NULL);
	}

	avl_remove(&spa->spa_alloc_trees[allocator], zio);
	ASSERT3U(zio->io_stage, <, ZIO_STAGE_DVA_ALLOCATE);

	return (zio);
}

static zio_t *
zio_dva_throttle(zio_t *zio)
{
	spa_t *spa = zio->io_spa;
	zio_t *nio;

	if (zio->io_priority == ZIO_PRIORITY_SYNC_WRITE ||
	    !spa_normal_class(zio->io_spa)->mc_alloc_throttle_enabled ||
	    zio->io_child_type == ZIO_CHILD_GANG ||
	    zio->io_flags & ZIO_FLAG_NODATA) {
		return (zio);
	}

	ASSERT(zio->io_child_type > ZIO_CHILD_GANG);

	ASSERT3U(zio->io_queued_timestamp, >, 0);
	ASSERT(zio->io_stage == ZIO_STAGE_DVA_THROTTLE);

	zbookmark_phys_t *bm = &zio->io_bookmark;
	/*
	 * We want to try to use as many allocators as possible to help improve
	 * performance, but we also want logically adjacent IOs to be physically
	 * adjacent to improve sequential read performance. We chunk each object
	 * into 2^20 block regions, and then hash based on the objset, object,
	 * level, and region to accomplish both of these goals.
	 */
	zio->io_allocator = cityhash4(bm->zb_objset, bm->zb_object,
	    bm->zb_level, bm->zb_blkid >> 20) % spa->spa_alloc_count;
	mutex_enter(&spa->spa_alloc_locks[zio->io_allocator]);

	ASSERT(zio->io_type == ZIO_TYPE_WRITE);
	avl_add(&spa->spa_alloc_trees[zio->io_allocator], zio);

	nio = zio_io_to_allocate(zio->io_spa, zio->io_allocator);
	mutex_exit(&spa->spa_alloc_locks[zio->io_allocator]);

	return (nio);
}

void
zio_allocate_dispatch(spa_t *spa, int allocator)
{
	zio_t *zio;

	mutex_enter(&spa->spa_alloc_locks[allocator]);
	zio = zio_io_to_allocate(spa, allocator);
	mutex_exit(&spa->spa_alloc_locks[allocator]);
	if (zio == NULL)
		return;

	ASSERT3U(zio->io_stage, ==, ZIO_STAGE_DVA_THROTTLE);
	ASSERT0(zio->io_error);
	zio_taskq_dispatch(zio, ZIO_TASKQ_ISSUE, B_TRUE);
}

static zio_t *
zio_dva_allocate(zio_t *zio)
{
	spa_t *spa = zio->io_spa;
	metaslab_class_t *mc = spa_normal_class(spa);
	blkptr_t *bp = zio->io_bp;
	int error;
	int flags = 0;

	if (zio->io_gang_leader == NULL) {
		ASSERT(zio->io_child_type > ZIO_CHILD_GANG);
		zio->io_gang_leader = zio;
	}

	ASSERT(BP_IS_HOLE(bp));
	ASSERT0(BP_GET_NDVAS(bp));
	ASSERT3U(zio->io_prop.zp_copies, >, 0);
	ASSERT3U(zio->io_prop.zp_copies, <=, spa_max_replication(spa));
	ASSERT3U(zio->io_size, ==, BP_GET_PSIZE(bp));

	if (zio->io_flags & ZIO_FLAG_NODATA) {
		flags |= METASLAB_DONT_THROTTLE;
	}
	if (zio->io_flags & ZIO_FLAG_GANG_CHILD) {
		flags |= METASLAB_GANG_CHILD;
	}
	if (zio->io_priority == ZIO_PRIORITY_ASYNC_WRITE) {
		flags |= METASLAB_ASYNC_ALLOC;
	}

	error = metaslab_alloc(spa, mc, zio->io_size, bp,
	    zio->io_prop.zp_copies, zio->io_txg, NULL, flags,
	    &zio->io_alloc_list, zio, zio->io_allocator);

	if (error != 0) {
		zfs_dbgmsg("%s: metaslab allocation failure: zio %p, "
		    "size %llu, error %d", spa_name(spa), zio, zio->io_size,
		    error);
		if (error == ENOSPC && zio->io_size > SPA_MINBLOCKSIZE)
			return (zio_write_gang_block(zio));
		zio->io_error = error;
	}

	return (zio);
}

static zio_t *
zio_dva_free(zio_t *zio)
{
	metaslab_free(zio->io_spa, zio->io_bp, zio->io_txg, B_FALSE);

	return (zio);
}

static zio_t *
zio_dva_claim(zio_t *zio)
{
	int error;

	error = metaslab_claim(zio->io_spa, zio->io_bp, zio->io_txg);
	if (error)
		zio->io_error = error;

	return (zio);
}

/*
 * Undo an allocation.  This is used by zio_done() when an I/O fails
 * and we want to give back the block we just allocated.
 * This handles both normal blocks and gang blocks.
 */
static void
zio_dva_unallocate(zio_t *zio, zio_gang_node_t *gn, blkptr_t *bp)
{
	ASSERT(bp->blk_birth == zio->io_txg || BP_IS_HOLE(bp));
	ASSERT(zio->io_bp_override == NULL);

	if (!BP_IS_HOLE(bp))
		metaslab_free(zio->io_spa, bp, bp->blk_birth, B_TRUE);

	if (gn != NULL) {
		for (int g = 0; g < SPA_GBH_NBLKPTRS; g++) {
			zio_dva_unallocate(zio, gn->gn_child[g],
			    &gn->gn_gbh->zg_blkptr[g]);
		}
	}
}

/*
 * Try to allocate an intent log block.  Return 0 on success, errno on failure.
 */
int
zio_alloc_zil(spa_t *spa, uint64_t objset, uint64_t txg, blkptr_t *new_bp,
    blkptr_t *old_bp, uint64_t size, boolean_t *slog)
{
	int error = 1;
	zio_alloc_list_t io_alloc_list;

	ASSERT(txg > spa_syncing_txg(spa));

	metaslab_trace_init(&io_alloc_list);
	/*
	 * When allocating a zil block, we don't have information about
	 * the final destination of the block except the objset it's part
	 * of, so we just hash the objset ID to pick the allocator to get
	 * some parallelism.
	 */
	error = metaslab_alloc(spa, spa_log_class(spa), size, new_bp, 1,
	    txg, old_bp, METASLAB_HINTBP_AVOID, &io_alloc_list, NULL,
	    cityhash4(0, 0, 0, objset) % spa->spa_alloc_count);
	if (error == 0) {
		*slog = TRUE;
	} else {
		error = metaslab_alloc(spa, spa_normal_class(spa), size,
		    new_bp, 1, txg, old_bp, METASLAB_HINTBP_AVOID,
		    &io_alloc_list, NULL, cityhash4(0, 0, 0, objset) %
		    spa->spa_alloc_count);
		if (error == 0)
			*slog = FALSE;
	}
	metaslab_trace_fini(&io_alloc_list);

	if (error == 0) {
		BP_SET_LSIZE(new_bp, size);
		BP_SET_PSIZE(new_bp, size);
		BP_SET_COMPRESS(new_bp, ZIO_COMPRESS_OFF);
		BP_SET_CHECKSUM(new_bp,
		    spa_version(spa) >= SPA_VERSION_SLIM_ZIL
		    ? ZIO_CHECKSUM_ZILOG2 : ZIO_CHECKSUM_ZILOG);
		BP_SET_TYPE(new_bp, DMU_OT_INTENT_LOG);
		BP_SET_LEVEL(new_bp, 0);
		BP_SET_DEDUP(new_bp, 0);
		BP_SET_BYTEORDER(new_bp, ZFS_HOST_BYTEORDER);
	} else {
		zfs_dbgmsg("%s: zil block allocation failure: "
		    "size %llu, error %d", spa_name(spa), size, error);
	}

	return (error);
}

/*
 * ==========================================================================
 * Read, write and delete to physical devices
 * ==========================================================================
 */


/*
 * Issue an I/O to the underlying vdev. Typically the issue pipeline
 * stops after this stage and will resume upon I/O completion.
 * However, there are instances where the vdev layer may need to
 * continue the pipeline when an I/O was not issued. Since the I/O
 * that was sent to the vdev layer might be different than the one
 * currently active in the pipeline (see vdev_queue_io()), we explicitly
 * force the underlying vdev layers to call either zio_execute() or
 * zio_interrupt() to ensure that the pipeline continues with the correct I/O.
 */
static zio_t *
zio_vdev_io_start(zio_t *zio)
{
	vdev_t *vd = zio->io_vd;
	uint64_t align;
	spa_t *spa = zio->io_spa;
	int ret;

	ASSERT(zio->io_error == 0);
	ASSERT(zio->io_child_error[ZIO_CHILD_VDEV] == 0);

	if (vd == NULL) {
		if (!(zio->io_flags & ZIO_FLAG_CONFIG_WRITER))
			spa_config_enter(spa, SCL_ZIO, zio, RW_READER);

		/*
		 * The mirror_ops handle multiple DVAs in a single BP.
		 */
		vdev_mirror_ops.vdev_op_io_start(zio);
		return (NULL);
	}

	if (vd->vdev_ops->vdev_op_leaf && zio->io_type == ZIO_TYPE_FREE &&
	    zio->io_priority == ZIO_PRIORITY_NOW) {
		trim_map_free(vd, zio->io_offset, zio->io_size, zio->io_txg);
		return (zio);
	}

	ASSERT3P(zio->io_logical, !=, zio);
	if (zio->io_type == ZIO_TYPE_WRITE) {
		ASSERT(spa->spa_trust_config);

		if (zio->io_vd->vdev_removing) {
			/*
			 * Note: the code can handle other kinds of writes,
			 * but we don't expect them.
			 */
			ASSERT(zio->io_flags &
			    (ZIO_FLAG_PHYSICAL | ZIO_FLAG_SELF_HEAL |
			    ZIO_FLAG_RESILVER | ZIO_FLAG_INDUCE_DAMAGE));
		}
	}

        /*
         * We keep track of time-sensitive I/Os so that the scan thread
         * can quickly react to certain workloads.  In particular, we care
         * about non-scrubbing, top-level reads and writes with the following
         * characteristics:
         *      - synchronous writes of user data to non-slog devices
         *      - any reads of user data
         * When these conditions are met, adjust the timestamp of spa_last_io
         * which allows the scan thread to adjust its workload accordingly.
         */
        if (!(zio->io_flags & ZIO_FLAG_SCAN_THREAD) && zio->io_bp != NULL &&
            vd == vd->vdev_top && !vd->vdev_islog &&
            zio->io_bookmark.zb_objset != DMU_META_OBJSET &&
            zio->io_txg != spa_syncing_txg(spa)) {
                uint64_t old = spa->spa_last_io;
                uint64_t new = ddi_get_lbolt64();
                if (old != new)
                        (void) atomic_cas_64(&spa->spa_last_io, old, new);
        }
	align = 1ULL << vd->vdev_top->vdev_ashift;

	if (!(zio->io_flags & ZIO_FLAG_PHYSICAL) &&
	    P2PHASE(zio->io_size, align) != 0) {
		/* Transform logical writes to be a full physical block size. */
		uint64_t asize = P2ROUNDUP(zio->io_size, align);
		abd_t *abuf = NULL;
		if (zio->io_type == ZIO_TYPE_READ ||
		    zio->io_type == ZIO_TYPE_WRITE)
			abuf = abd_alloc_sametype(zio->io_abd, asize);
		ASSERT(vd == vd->vdev_top);
		if (zio->io_type == ZIO_TYPE_WRITE) {
			abd_copy(abuf, zio->io_abd, zio->io_size);
			abd_zero_off(abuf, zio->io_size, asize - zio->io_size);
		}
		zio_push_transform(zio, abuf, asize, abuf ? asize : 0,
		    zio_subblock);
	}

	/*
	 * If this is not a physical io, make sure that it is properly aligned
	 * before proceeding.
	 */
	if (!(zio->io_flags & ZIO_FLAG_PHYSICAL)) {
		ASSERT0(P2PHASE(zio->io_offset, align));
		ASSERT0(P2PHASE(zio->io_size, align));
	} else {
		/*
		 * For the physical io we allow alignment
		 * to a logical block size.
		 */
		uint64_t log_align =
		    1ULL << vd->vdev_top->vdev_logical_ashift;
		ASSERT0(P2PHASE(zio->io_offset, log_align));
		ASSERT0(P2PHASE(zio->io_size, log_align));
	}

	VERIFY(zio->io_type == ZIO_TYPE_READ || spa_writeable(spa));

	/*
	 * If this is a repair I/O, and there's no self-healing involved --
	 * that is, we're just resilvering what we expect to resilver --
	 * then don't do the I/O unless zio's txg is actually in vd's DTL.
	 * This prevents spurious resilvering.
	 *
	 * There are a few ways that we can end up creating these spurious
	 * resilver i/os:
	 *
	 * 1. A resilver i/o will be issued if any DVA in the BP has a
	 * dirty DTL.  The mirror code will issue resilver writes to
	 * each DVA, including the one(s) that are not on vdevs with dirty
	 * DTLs.
	 *
	 * 2. With nested replication, which happens when we have a
	 * "replacing" or "spare" vdev that's a child of a mirror or raidz.
	 * For example, given mirror(replacing(A+B), C), it's likely that
	 * only A is out of date (it's the new device). In this case, we'll
	 * read from C, then use the data to resilver A+B -- but we don't
	 * actually want to resilver B, just A. The top-level mirror has no
	 * way to know this, so instead we just discard unnecessary repairs
	 * as we work our way down the vdev tree.
	 *
	 * 3. ZTEST also creates mirrors of mirrors, mirrors of raidz, etc.
	 * The same logic applies to any form of nested replication: ditto
	 * + mirror, RAID-Z + replacing, etc.
	 *
	 * However, indirect vdevs point off to other vdevs which may have
	 * DTL's, so we never bypass them.  The child i/os on concrete vdevs
	 * will be properly bypassed instead.
	 */
	if ((zio->io_flags & ZIO_FLAG_IO_REPAIR) &&
	    !(zio->io_flags & ZIO_FLAG_SELF_HEAL) &&
	    zio->io_txg != 0 &&	/* not a delegated i/o */
	    vd->vdev_ops != &vdev_indirect_ops &&
	    !vdev_dtl_contains(vd, DTL_PARTIAL, zio->io_txg, 1)) {
		ASSERT(zio->io_type == ZIO_TYPE_WRITE);
		zio_vdev_io_bypass(zio);
		return (zio);
	}

	if (vd->vdev_ops->vdev_op_leaf) {
		switch (zio->io_type) {
		case ZIO_TYPE_READ:
			if (vdev_cache_read(zio))
				return (zio);
			/* FALLTHROUGH */
		case ZIO_TYPE_WRITE:
		case ZIO_TYPE_FREE:
			if ((zio = vdev_queue_io(zio)) == NULL)
				return (NULL);

			if (!vdev_accessible(vd, zio)) {
				zio->io_error = SET_ERROR(ENXIO);
				zio_interrupt(zio);
				return (NULL);
			}
			break;
		}
		/*
		 * Note that we ignore repair writes for TRIM because they can
		 * conflict with normal writes. This isn't an issue because, by
		 * definition, we only repair blocks that aren't freed.
		 */
		if (zio->io_type == ZIO_TYPE_WRITE &&
		    !(zio->io_flags & ZIO_FLAG_IO_REPAIR) &&
		    !trim_map_write_start(zio))
			return (NULL);
	}

	vd->vdev_ops->vdev_op_io_start(zio);
	return (NULL);
}

static zio_t *
zio_vdev_io_done(zio_t *zio)
{
	vdev_t *vd = zio->io_vd;
	vdev_ops_t *ops = vd ? vd->vdev_ops : &vdev_mirror_ops;
	boolean_t unexpected_error = B_FALSE;

	if (zio_wait_for_children(zio, ZIO_CHILD_VDEV_BIT, ZIO_WAIT_DONE)) {
		return (NULL);
	}

	ASSERT(zio->io_type == ZIO_TYPE_READ ||
	    zio->io_type == ZIO_TYPE_WRITE || zio->io_type == ZIO_TYPE_FREE);

	if (vd != NULL && vd->vdev_ops->vdev_op_leaf &&
	    (zio->io_type == ZIO_TYPE_READ || zio->io_type == ZIO_TYPE_WRITE ||
	    zio->io_type == ZIO_TYPE_FREE)) {

		if (zio->io_type == ZIO_TYPE_WRITE &&
		    !(zio->io_flags & ZIO_FLAG_IO_REPAIR))
			trim_map_write_done(zio);

		vdev_queue_io_done(zio);

		if (zio->io_type == ZIO_TYPE_WRITE)
			vdev_cache_write(zio);

		if (zio_injection_enabled && zio->io_error == 0)
			zio->io_error = zio_handle_device_injection(vd,
			    zio, EIO);

		if (zio_injection_enabled && zio->io_error == 0)
			zio->io_error = zio_handle_label_injection(zio, EIO);

		if (zio->io_error) {
			if (zio->io_error == ENOTSUP &&
			    zio->io_type == ZIO_TYPE_FREE) {
				/* Not all devices support TRIM. */
			} else if (!vdev_accessible(vd, zio)) {
				zio->io_error = SET_ERROR(ENXIO);
			} else {
				unexpected_error = B_TRUE;
			}
		}
	}

	ops->vdev_op_io_done(zio);

	if (unexpected_error)
		VERIFY(vdev_probe(vd, zio) == NULL);

	return (zio);
}

/*
 * This function is used to change the priority of an existing zio that is
 * currently in-flight. This is used by the arc to upgrade priority in the
 * event that a demand read is made for a block that is currently queued
 * as a scrub or async read IO. Otherwise, the high priority read request
 * would end up having to wait for the lower priority IO.
 */
void
zio_change_priority(zio_t *pio, zio_priority_t priority)
{
	zio_t *cio, *cio_next;
	zio_link_t *zl = NULL;

	ASSERT3U(priority, <, ZIO_PRIORITY_NUM_QUEUEABLE);

	if (pio->io_vd != NULL && pio->io_vd->vdev_ops->vdev_op_leaf) {
		vdev_queue_change_io_priority(pio, priority);
	} else {
		pio->io_priority = priority;
	}

	mutex_enter(&pio->io_lock);
	for (cio = zio_walk_children(pio, &zl); cio != NULL; cio = cio_next) {
		cio_next = zio_walk_children(pio, &zl);
		zio_change_priority(cio, priority);
	}
	mutex_exit(&pio->io_lock);
}

/*
 * For non-raidz ZIOs, we can just copy aside the bad data read from the
 * disk, and use that to finish the checksum ereport later.
 */
static void
zio_vsd_default_cksum_finish(zio_cksum_report_t *zcr,
    const void *good_buf)
{
	/* no processing needed */
	zfs_ereport_finish_checksum(zcr, good_buf, zcr->zcr_cbdata, B_FALSE);
}

/*ARGSUSED*/
void
zio_vsd_default_cksum_report(zio_t *zio, zio_cksum_report_t *zcr, void *ignored)
{
	void *buf = zio_buf_alloc(zio->io_size);

	abd_copy_to_buf(buf, zio->io_abd, zio->io_size);

	zcr->zcr_cbinfo = zio->io_size;
	zcr->zcr_cbdata = buf;
	zcr->zcr_finish = zio_vsd_default_cksum_finish;
	zcr->zcr_free = zio_buf_free;
}

static zio_t *
zio_vdev_io_assess(zio_t *zio)
{
	vdev_t *vd = zio->io_vd;

	if (zio_wait_for_children(zio, ZIO_CHILD_VDEV_BIT, ZIO_WAIT_DONE)) {
		return (NULL);
	}

	if (vd == NULL && !(zio->io_flags & ZIO_FLAG_CONFIG_WRITER))
		spa_config_exit(zio->io_spa, SCL_ZIO, zio);

	if (zio->io_vsd != NULL) {
		zio->io_vsd_ops->vsd_free(zio);
		zio->io_vsd = NULL;
	}

	if (zio_injection_enabled && zio->io_error == 0)
		zio->io_error = zio_handle_fault_injection(zio, EIO);

	if (zio->io_type == ZIO_TYPE_FREE &&
	    zio->io_priority != ZIO_PRIORITY_NOW) {
		switch (zio->io_error) {
		case 0:
			ZIO_TRIM_STAT_INCR(bytes, zio->io_size);
			ZIO_TRIM_STAT_BUMP(success);
			break;
		case EOPNOTSUPP:
			ZIO_TRIM_STAT_BUMP(unsupported);
			break;
		default:
			ZIO_TRIM_STAT_BUMP(failed);
			break;
		}
	}

	/*
	 * If the I/O failed, determine whether we should attempt to retry it.
	 *
	 * On retry, we cut in line in the issue queue, since we don't want
	 * compression/checksumming/etc. work to prevent our (cheap) IO reissue.
	 */
	if (zio->io_error && vd == NULL &&
	    !(zio->io_flags & (ZIO_FLAG_DONT_RETRY | ZIO_FLAG_IO_RETRY))) {
		ASSERT(!(zio->io_flags & ZIO_FLAG_DONT_QUEUE));	/* not a leaf */
		ASSERT(!(zio->io_flags & ZIO_FLAG_IO_BYPASS));	/* not a leaf */
		zio->io_error = 0;
		zio->io_flags |= ZIO_FLAG_IO_RETRY |
		    ZIO_FLAG_DONT_CACHE | ZIO_FLAG_DONT_AGGREGATE;
		zio->io_stage = ZIO_STAGE_VDEV_IO_START >> 1;
		zio_taskq_dispatch(zio, ZIO_TASKQ_ISSUE,
		    zio_requeue_io_start_cut_in_line);
		return (NULL);
	}

	/*
	 * If we got an error on a leaf device, convert it to ENXIO
	 * if the device is not accessible at all.
	 */
	if (zio->io_error && vd != NULL && vd->vdev_ops->vdev_op_leaf &&
	    !vdev_accessible(vd, zio))
		zio->io_error = SET_ERROR(ENXIO);

	/*
	 * If we can't write to an interior vdev (mirror or RAID-Z),
	 * set vdev_cant_write so that we stop trying to allocate from it.
	 */
	if (zio->io_error == ENXIO && zio->io_type == ZIO_TYPE_WRITE &&
	    vd != NULL && !vd->vdev_ops->vdev_op_leaf) {
		vd->vdev_cant_write = B_TRUE;
	}

	/*
	 * If a cache flush returns ENOTSUP or ENOTTY, we know that no future
	 * attempts will ever succeed. In this case we set a persistent bit so
	 * that we don't bother with it in the future.
	 */
	if ((zio->io_error == ENOTSUP || zio->io_error == ENOTTY) &&
	    zio->io_type == ZIO_TYPE_IOCTL &&
	    zio->io_cmd == DKIOCFLUSHWRITECACHE && vd != NULL)
		vd->vdev_nowritecache = B_TRUE;

	if (zio->io_error)
		zio->io_pipeline = ZIO_INTERLOCK_PIPELINE;

	if (vd != NULL && vd->vdev_ops->vdev_op_leaf &&
	    zio->io_physdone != NULL) {
		ASSERT(!(zio->io_flags & ZIO_FLAG_DELEGATED));
		ASSERT(zio->io_child_type == ZIO_CHILD_VDEV);
		zio->io_physdone(zio->io_logical);
	}

	return (zio);
}

void
zio_vdev_io_reissue(zio_t *zio)
{
	ASSERT(zio->io_stage == ZIO_STAGE_VDEV_IO_START);
	ASSERT(zio->io_error == 0);

	zio->io_stage >>= 1;
}

void
zio_vdev_io_redone(zio_t *zio)
{
	ASSERT(zio->io_stage == ZIO_STAGE_VDEV_IO_DONE);

	zio->io_stage >>= 1;
}

void
zio_vdev_io_bypass(zio_t *zio)
{
	ASSERT(zio->io_stage == ZIO_STAGE_VDEV_IO_START);
	ASSERT(zio->io_error == 0);

	zio->io_flags |= ZIO_FLAG_IO_BYPASS;
	zio->io_stage = ZIO_STAGE_VDEV_IO_ASSESS >> 1;
}

/*
 * ==========================================================================
 * Generate and verify checksums
 * ==========================================================================
 */
static zio_t *
zio_checksum_generate(zio_t *zio)
{
	blkptr_t *bp = zio->io_bp;
	enum zio_checksum checksum;

	if (bp == NULL) {
		/*
		 * This is zio_write_phys().
		 * We're either generating a label checksum, or none at all.
		 */
		checksum = zio->io_prop.zp_checksum;

		if (checksum == ZIO_CHECKSUM_OFF)
			return (zio);

		ASSERT(checksum == ZIO_CHECKSUM_LABEL);
	} else {
		if (BP_IS_GANG(bp) && zio->io_child_type == ZIO_CHILD_GANG) {
			ASSERT(!IO_IS_ALLOCATING(zio));
			checksum = ZIO_CHECKSUM_GANG_HEADER;
		} else {
			checksum = BP_GET_CHECKSUM(bp);
		}
	}

	zio_checksum_compute(zio, checksum, zio->io_abd, zio->io_size);

	return (zio);
}

static zio_t *
zio_checksum_verify(zio_t *zio)
{
	zio_bad_cksum_t info;
	blkptr_t *bp = zio->io_bp;
	int error;

	ASSERT(zio->io_vd != NULL);

	if (bp == NULL) {
		/*
		 * This is zio_read_phys().
		 * We're either verifying a label checksum, or nothing at all.
		 */
		if (zio->io_prop.zp_checksum == ZIO_CHECKSUM_OFF)
			return (zio);

		ASSERT(zio->io_prop.zp_checksum == ZIO_CHECKSUM_LABEL);
	}

	if ((error = zio_checksum_error(zio, &info)) != 0) {
		zio->io_error = error;
		if (error == ECKSUM &&
		    !(zio->io_flags & ZIO_FLAG_SPECULATIVE)) {
			zfs_ereport_start_checksum(zio->io_spa,
			    zio->io_vd, zio, zio->io_offset,
			    zio->io_size, NULL, &info);
		}
	}

	return (zio);
}

/*
 * Called by RAID-Z to ensure we don't compute the checksum twice.
 */
void
zio_checksum_verified(zio_t *zio)
{
	zio->io_pipeline &= ~ZIO_STAGE_CHECKSUM_VERIFY;
}

/*
 * ==========================================================================
 * Error rank.  Error are ranked in the order 0, ENXIO, ECKSUM, EIO, other.
 * An error of 0 indicates success.  ENXIO indicates whole-device failure,
 * which may be transient (e.g. unplugged) or permament.  ECKSUM and EIO
 * indicate errors that are specific to one I/O, and most likely permanent.
 * Any other error is presumed to be worse because we weren't expecting it.
 * ==========================================================================
 */
int
zio_worst_error(int e1, int e2)
{
	static int zio_error_rank[] = { 0, ENXIO, ECKSUM, EIO };
	int r1, r2;

	for (r1 = 0; r1 < sizeof (zio_error_rank) / sizeof (int); r1++)
		if (e1 == zio_error_rank[r1])
			break;

	for (r2 = 0; r2 < sizeof (zio_error_rank) / sizeof (int); r2++)
		if (e2 == zio_error_rank[r2])
			break;

	return (r1 > r2 ? e1 : e2);
}

/*
 * ==========================================================================
 * I/O completion
 * ==========================================================================
 */
static zio_t *
zio_ready(zio_t *zio)
{
	blkptr_t *bp = zio->io_bp;
	zio_t *pio, *pio_next;
	zio_link_t *zl = NULL;

	if (zio_wait_for_children(zio, ZIO_CHILD_GANG_BIT | ZIO_CHILD_DDT_BIT,
	    ZIO_WAIT_READY)) {
		return (NULL);
	}

	if (zio->io_ready) {
		ASSERT(IO_IS_ALLOCATING(zio));
		ASSERT(bp->blk_birth == zio->io_txg || BP_IS_HOLE(bp) ||
		    (zio->io_flags & ZIO_FLAG_NOPWRITE));
		ASSERT(zio->io_children[ZIO_CHILD_GANG][ZIO_WAIT_READY] == 0);

		zio->io_ready(zio);
	}

	if (bp != NULL && bp != &zio->io_bp_copy)
		zio->io_bp_copy = *bp;

	if (zio->io_error != 0) {
		zio->io_pipeline = ZIO_INTERLOCK_PIPELINE;

		if (zio->io_flags & ZIO_FLAG_IO_ALLOCATING) {
			ASSERT(IO_IS_ALLOCATING(zio));
			ASSERT(zio->io_priority == ZIO_PRIORITY_ASYNC_WRITE);
			/*
			 * We were unable to allocate anything, unreserve and
			 * issue the next I/O to allocate.
			 */
			metaslab_class_throttle_unreserve(
			    spa_normal_class(zio->io_spa),
			    zio->io_prop.zp_copies, zio->io_allocator, zio);
			zio_allocate_dispatch(zio->io_spa, zio->io_allocator);
		}
	}

	mutex_enter(&zio->io_lock);
	zio->io_state[ZIO_WAIT_READY] = 1;
	pio = zio_walk_parents(zio, &zl);
	mutex_exit(&zio->io_lock);

	/*
	 * As we notify zio's parents, new parents could be added.
	 * New parents go to the head of zio's io_parent_list, however,
	 * so we will (correctly) not notify them.  The remainder of zio's
	 * io_parent_list, from 'pio_next' onward, cannot change because
	 * all parents must wait for us to be done before they can be done.
	 */
	for (; pio != NULL; pio = pio_next) {
		pio_next = zio_walk_parents(zio, &zl);
		zio_notify_parent(pio, zio, ZIO_WAIT_READY, NULL);
	}

	if (zio->io_flags & ZIO_FLAG_NODATA) {
		if (BP_IS_GANG(bp)) {
			zio->io_flags &= ~ZIO_FLAG_NODATA;
		} else {
			ASSERT((uintptr_t)zio->io_abd < SPA_MAXBLOCKSIZE);
			zio->io_pipeline &= ~ZIO_VDEV_IO_STAGES;
		}
	}

	if (zio_injection_enabled &&
	    zio->io_spa->spa_syncing_txg == zio->io_txg)
		zio_handle_ignored_writes(zio);

	return (zio);
}

/*
 * Update the allocation throttle accounting.
 */
static void
zio_dva_throttle_done(zio_t *zio)
{
	zio_t *lio = zio->io_logical;
	zio_t *pio = zio_unique_parent(zio);
	vdev_t *vd = zio->io_vd;
	int flags = METASLAB_ASYNC_ALLOC;

	ASSERT3P(zio->io_bp, !=, NULL);
	ASSERT3U(zio->io_type, ==, ZIO_TYPE_WRITE);
	ASSERT3U(zio->io_priority, ==, ZIO_PRIORITY_ASYNC_WRITE);
	ASSERT3U(zio->io_child_type, ==, ZIO_CHILD_VDEV);
	ASSERT(vd != NULL);
	ASSERT3P(vd, ==, vd->vdev_top);
	ASSERT(!(zio->io_flags & (ZIO_FLAG_IO_REPAIR | ZIO_FLAG_IO_RETRY)));
	ASSERT(zio->io_flags & ZIO_FLAG_IO_ALLOCATING);
	ASSERT(!(lio->io_flags & ZIO_FLAG_IO_REWRITE));
	ASSERT(!(lio->io_orig_flags & ZIO_FLAG_NODATA));

	/*
	 * Parents of gang children can have two flavors -- ones that
	 * allocated the gang header (will have ZIO_FLAG_IO_REWRITE set)
	 * and ones that allocated the constituent blocks. The allocation
	 * throttle needs to know the allocating parent zio so we must find
	 * it here.
	 */
	if (pio->io_child_type == ZIO_CHILD_GANG) {
		/*
		 * If our parent is a rewrite gang child then our grandparent
		 * would have been the one that performed the allocation.
		 */
		if (pio->io_flags & ZIO_FLAG_IO_REWRITE)
			pio = zio_unique_parent(pio);
		flags |= METASLAB_GANG_CHILD;
	}

	ASSERT(IO_IS_ALLOCATING(pio));
	ASSERT3P(zio, !=, zio->io_logical);
	ASSERT(zio->io_logical != NULL);
	ASSERT(!(zio->io_flags & ZIO_FLAG_IO_REPAIR));
	ASSERT0(zio->io_flags & ZIO_FLAG_NOPWRITE);

	mutex_enter(&pio->io_lock);
	metaslab_group_alloc_decrement(zio->io_spa, vd->vdev_id, pio, flags,
	    pio->io_allocator, B_TRUE);
	mutex_exit(&pio->io_lock);

	metaslab_class_throttle_unreserve(spa_normal_class(zio->io_spa),
	    1, pio->io_allocator, pio);

	/*
	 * Call into the pipeline to see if there is more work that
	 * needs to be done. If there is work to be done it will be
	 * dispatched to another taskq thread.
	 */
	zio_allocate_dispatch(zio->io_spa, pio->io_allocator);
}

static zio_t *
zio_done(zio_t *zio)
{
	spa_t *spa = zio->io_spa;
	zio_t *lio = zio->io_logical;
	blkptr_t *bp = zio->io_bp;
	vdev_t *vd = zio->io_vd;
	uint64_t psize = zio->io_size;
	zio_t *pio, *pio_next;
	metaslab_class_t *mc = spa_normal_class(spa);
	zio_link_t *zl = NULL;

	/*
	 * If our children haven't all completed,
	 * wait for them and then repeat this pipeline stage.
	 */
	if (zio_wait_for_children(zio, ZIO_CHILD_ALL_BITS, ZIO_WAIT_DONE)) {
		return (NULL);
	}

	/*
	 * If the allocation throttle is enabled, then update the accounting.
	 * We only track child I/Os that are part of an allocating async
	 * write. We must do this since the allocation is performed
	 * by the logical I/O but the actual write is done by child I/Os.
	 */
	if (zio->io_flags & ZIO_FLAG_IO_ALLOCATING &&
	    zio->io_child_type == ZIO_CHILD_VDEV) {
		ASSERT(mc->mc_alloc_throttle_enabled);
		zio_dva_throttle_done(zio);
	}

	/*
	 * If the allocation throttle is enabled, verify that
	 * we have decremented the refcounts for every I/O that was throttled.
	 */
	if (zio->io_flags & ZIO_FLAG_IO_ALLOCATING) {
		ASSERT(zio->io_type == ZIO_TYPE_WRITE);
		ASSERT(zio->io_priority == ZIO_PRIORITY_ASYNC_WRITE);
		ASSERT(bp != NULL);
		metaslab_group_alloc_verify(spa, zio->io_bp, zio,
		    zio->io_allocator);
		VERIFY(refcount_not_held(&mc->mc_alloc_slots[zio->io_allocator],
		    zio));
	}

	for (int c = 0; c < ZIO_CHILD_TYPES; c++)
		for (int w = 0; w < ZIO_WAIT_TYPES; w++)
			ASSERT(zio->io_children[c][w] == 0);

	if (bp != NULL && !BP_IS_EMBEDDED(bp)) {
		ASSERT(bp->blk_pad[0] == 0);
		ASSERT(bp->blk_pad[1] == 0);
		ASSERT(bcmp(bp, &zio->io_bp_copy, sizeof (blkptr_t)) == 0 ||
		    (bp == zio_unique_parent(zio)->io_bp));
		if (zio->io_type == ZIO_TYPE_WRITE && !BP_IS_HOLE(bp) &&
		    zio->io_bp_override == NULL &&
		    !(zio->io_flags & ZIO_FLAG_IO_REPAIR)) {
			ASSERT(!BP_SHOULD_BYTESWAP(bp));
			ASSERT3U(zio->io_prop.zp_copies, <=, BP_GET_NDVAS(bp));
			ASSERT(BP_COUNT_GANG(bp) == 0 ||
			    (BP_COUNT_GANG(bp) == BP_GET_NDVAS(bp)));
		}
		if (zio->io_flags & ZIO_FLAG_NOPWRITE)
			VERIFY(BP_EQUAL(bp, &zio->io_bp_orig));
	}

	/*
	 * If there were child vdev/gang/ddt errors, they apply to us now.
	 */
	zio_inherit_child_errors(zio, ZIO_CHILD_VDEV);
	zio_inherit_child_errors(zio, ZIO_CHILD_GANG);
	zio_inherit_child_errors(zio, ZIO_CHILD_DDT);

	/*
	 * If the I/O on the transformed data was successful, generate any
	 * checksum reports now while we still have the transformed data.
	 */
	if (zio->io_error == 0) {
		while (zio->io_cksum_report != NULL) {
			zio_cksum_report_t *zcr = zio->io_cksum_report;
			uint64_t align = zcr->zcr_align;
			uint64_t asize = P2ROUNDUP(psize, align);
			char *abuf = NULL;
			abd_t *adata = zio->io_abd;

			if (asize != psize) {
				adata = abd_alloc_linear(asize, B_TRUE);
				abd_copy(adata, zio->io_abd, psize);
				abd_zero_off(adata, psize, asize - psize);
			}

			if (adata != NULL)
				abuf = abd_borrow_buf_copy(adata, asize);

			zio->io_cksum_report = zcr->zcr_next;
			zcr->zcr_next = NULL;
			zcr->zcr_finish(zcr, abuf);
			zfs_ereport_free_checksum(zcr);

			if (adata != NULL)
				abd_return_buf(adata, abuf, asize);

			if (asize != psize)
				abd_free(adata);
		}
	}

	zio_pop_transforms(zio);	/* note: may set zio->io_error */

	vdev_stat_update(zio, psize);

	if (zio->io_error) {
		/*
		 * If this I/O is attached to a particular vdev,
		 * generate an error message describing the I/O failure
		 * at the block level.  We ignore these errors if the
		 * device is currently unavailable.
		 */
		if (zio->io_error != ECKSUM && vd != NULL && !vdev_is_dead(vd))
			zfs_ereport_post(FM_EREPORT_ZFS_IO, spa, vd, zio, 0, 0);

		if ((zio->io_error == EIO || !(zio->io_flags &
		    (ZIO_FLAG_SPECULATIVE | ZIO_FLAG_DONT_PROPAGATE))) &&
		    zio == lio) {
			/*
			 * For logical I/O requests, tell the SPA to log the
			 * error and generate a logical data ereport.
			 */
			spa_log_error(spa, zio);
			zfs_ereport_post(FM_EREPORT_ZFS_DATA, spa, NULL, zio,
			    0, 0);
		}
	}

	if (zio->io_error && zio == lio) {
		/*
		 * Determine whether zio should be reexecuted.  This will
		 * propagate all the way to the root via zio_notify_parent().
		 */
		ASSERT(vd == NULL && bp != NULL);
		ASSERT(zio->io_child_type == ZIO_CHILD_LOGICAL);

		if (IO_IS_ALLOCATING(zio) &&
		    !(zio->io_flags & ZIO_FLAG_CANFAIL)) {
			if (zio->io_error != ENOSPC)
				zio->io_reexecute |= ZIO_REEXECUTE_NOW;
			else
				zio->io_reexecute |= ZIO_REEXECUTE_SUSPEND;
		}

		if ((zio->io_type == ZIO_TYPE_READ ||
		    zio->io_type == ZIO_TYPE_FREE) &&
		    !(zio->io_flags & ZIO_FLAG_SCAN_THREAD) &&
		    zio->io_error == ENXIO &&
		    spa_load_state(spa) == SPA_LOAD_NONE &&
		    spa_get_failmode(spa) != ZIO_FAILURE_MODE_CONTINUE)
			zio->io_reexecute |= ZIO_REEXECUTE_SUSPEND;

		if (!(zio->io_flags & ZIO_FLAG_CANFAIL) && !zio->io_reexecute)
			zio->io_reexecute |= ZIO_REEXECUTE_SUSPEND;

		/*
		 * Here is a possibly good place to attempt to do
		 * either combinatorial reconstruction or error correction
		 * based on checksums.  It also might be a good place
		 * to send out preliminary ereports before we suspend
		 * processing.
		 */
	}

	/*
	 * If there were logical child errors, they apply to us now.
	 * We defer this until now to avoid conflating logical child
	 * errors with errors that happened to the zio itself when
	 * updating vdev stats and reporting FMA events above.
	 */
	zio_inherit_child_errors(zio, ZIO_CHILD_LOGICAL);

	if ((zio->io_error || zio->io_reexecute) &&
	    IO_IS_ALLOCATING(zio) && zio->io_gang_leader == zio &&
	    !(zio->io_flags & (ZIO_FLAG_IO_REWRITE | ZIO_FLAG_NOPWRITE)))
		zio_dva_unallocate(zio, zio->io_gang_tree, bp);

	zio_gang_tree_free(&zio->io_gang_tree);

	/*
	 * Godfather I/Os should never suspend.
	 */
	if ((zio->io_flags & ZIO_FLAG_GODFATHER) &&
	    (zio->io_reexecute & ZIO_REEXECUTE_SUSPEND))
		zio->io_reexecute = 0;

	if (zio->io_reexecute) {
		/*
		 * This is a logical I/O that wants to reexecute.
		 *
		 * Reexecute is top-down.  When an i/o fails, if it's not
		 * the root, it simply notifies its parent and sticks around.
		 * The parent, seeing that it still has children in zio_done(),
		 * does the same.  This percolates all the way up to the root.
		 * The root i/o will reexecute or suspend the entire tree.
		 *
		 * This approach ensures that zio_reexecute() honors
		 * all the original i/o dependency relationships, e.g.
		 * parents not executing until children are ready.
		 */
		ASSERT(zio->io_child_type == ZIO_CHILD_LOGICAL);

		zio->io_gang_leader = NULL;

		mutex_enter(&zio->io_lock);
		zio->io_state[ZIO_WAIT_DONE] = 1;
		mutex_exit(&zio->io_lock);

		/*
		 * "The Godfather" I/O monitors its children but is
		 * not a true parent to them. It will track them through
		 * the pipeline but severs its ties whenever they get into
		 * trouble (e.g. suspended). This allows "The Godfather"
		 * I/O to return status without blocking.
		 */
		zl = NULL;
		for (pio = zio_walk_parents(zio, &zl); pio != NULL;
		    pio = pio_next) {
			zio_link_t *remove_zl = zl;
			pio_next = zio_walk_parents(zio, &zl);

			if ((pio->io_flags & ZIO_FLAG_GODFATHER) &&
			    (zio->io_reexecute & ZIO_REEXECUTE_SUSPEND)) {
				zio_remove_child(pio, zio, remove_zl);
				/*
				 * This is a rare code path, so we don't
				 * bother with "next_to_execute".
				 */
				zio_notify_parent(pio, zio, ZIO_WAIT_DONE,
				    NULL);
			}
		}

		if ((pio = zio_unique_parent(zio)) != NULL) {
			/*
			 * We're not a root i/o, so there's nothing to do
			 * but notify our parent.  Don't propagate errors
			 * upward since we haven't permanently failed yet.
			 */
			ASSERT(!(zio->io_flags & ZIO_FLAG_GODFATHER));
			zio->io_flags |= ZIO_FLAG_DONT_PROPAGATE;
			/*
			 * This is a rare code path, so we don't bother with
			 * "next_to_execute".
			 */
			zio_notify_parent(pio, zio, ZIO_WAIT_DONE, NULL);
		} else if (zio->io_reexecute & ZIO_REEXECUTE_SUSPEND) {
			/*
			 * We'd fail again if we reexecuted now, so suspend
			 * until conditions improve (e.g. device comes online).
			 */
			zio_suspend(spa, zio);
		} else {
			/*
			 * Reexecution is potentially a huge amount of work.
			 * Hand it off to the otherwise-unused claim taskq.
			 */
#if defined(illumos) || !defined(_KERNEL)
			ASSERT(zio->io_tqent.tqent_next == NULL);
#else
			ASSERT(zio->io_tqent.tqent_task.ta_pending == 0);
#endif
			spa_taskq_dispatch_ent(spa, ZIO_TYPE_CLAIM,
			    ZIO_TASKQ_ISSUE, (task_func_t *)zio_reexecute, zio,
			    0, &zio->io_tqent);
		}
		return (NULL);
	}

	ASSERT(zio->io_child_count == 0);
	ASSERT(zio->io_reexecute == 0);
	ASSERT(zio->io_error == 0 || (zio->io_flags & ZIO_FLAG_CANFAIL));

	/*
	 * Report any checksum errors, since the I/O is complete.
	 */
	while (zio->io_cksum_report != NULL) {
		zio_cksum_report_t *zcr = zio->io_cksum_report;
		zio->io_cksum_report = zcr->zcr_next;
		zcr->zcr_next = NULL;
		zcr->zcr_finish(zcr, NULL);
		zfs_ereport_free_checksum(zcr);
	}

	/*
	 * It is the responsibility of the done callback to ensure that this
	 * particular zio is no longer discoverable for adoption, and as
	 * such, cannot acquire any new parents.
	 */
	if (zio->io_done)
		zio->io_done(zio);

	mutex_enter(&zio->io_lock);
	zio->io_state[ZIO_WAIT_DONE] = 1;
	mutex_exit(&zio->io_lock);

	/*
	 * We are done executing this zio.  We may want to execute a parent
	 * next.  See the comment in zio_notify_parent().
	 */
	zio_t *next_to_execute = NULL;
	zl = NULL;
	for (pio = zio_walk_parents(zio, &zl); pio != NULL; pio = pio_next) {
		zio_link_t *remove_zl = zl;
		pio_next = zio_walk_parents(zio, &zl);
		zio_remove_child(pio, zio, remove_zl);
		zio_notify_parent(pio, zio, ZIO_WAIT_DONE, &next_to_execute);
	}

	if (zio->io_waiter != NULL) {
		mutex_enter(&zio->io_lock);
		zio->io_executor = NULL;
		cv_broadcast(&zio->io_cv);
		mutex_exit(&zio->io_lock);
	} else {
		zio_destroy(zio);
	}

	return (next_to_execute);
}

/*
 * ==========================================================================
 * I/O pipeline definition
 * ==========================================================================
 */
static zio_pipe_stage_t *zio_pipeline[] = {
	NULL,
	zio_read_bp_init,
	zio_write_bp_init,
	zio_free_bp_init,
	zio_issue_async,
	zio_write_compress,
	zio_checksum_generate,
	zio_nop_write,
	zio_ddt_read_start,
	zio_ddt_read_done,
	zio_ddt_write,
	zio_ddt_free,
	zio_gang_assemble,
	zio_gang_issue,
	zio_dva_throttle,
	zio_dva_allocate,
	zio_dva_free,
	zio_dva_claim,
	zio_ready,
	zio_vdev_io_start,
	zio_vdev_io_done,
	zio_vdev_io_assess,
	zio_checksum_verify,
	zio_done
};




/*
 * Compare two zbookmark_phys_t's to see which we would reach first in a
 * pre-order traversal of the object tree.
 *
 * This is simple in every case aside from the meta-dnode object. For all other
 * objects, we traverse them in order (object 1 before object 2, and so on).
 * However, all of these objects are traversed while traversing object 0, since
 * the data it points to is the list of objects.  Thus, we need to convert to a
 * canonical representation so we can compare meta-dnode bookmarks to
 * non-meta-dnode bookmarks.
 *
 * We do this by calculating "equivalents" for each field of the zbookmark.
 * zbookmarks outside of the meta-dnode use their own object and level, and
 * calculate the level 0 equivalent (the first L0 blkid that is contained in the
 * blocks this bookmark refers to) by multiplying their blkid by their span
 * (the number of L0 blocks contained within one block at their level).
 * zbookmarks inside the meta-dnode calculate their object equivalent
 * (which is L0equiv * dnodes per data block), use 0 for their L0equiv, and use
 * level + 1<<31 (any value larger than a level could ever be) for their level.
 * This causes them to always compare before a bookmark in their object
 * equivalent, compare appropriately to bookmarks in other objects, and to
 * compare appropriately to other bookmarks in the meta-dnode.
 */
int
zbookmark_compare(uint16_t dbss1, uint8_t ibs1, uint16_t dbss2, uint8_t ibs2,
    const zbookmark_phys_t *zb1, const zbookmark_phys_t *zb2)
{
	/*
	 * These variables represent the "equivalent" values for the zbookmark,
	 * after converting zbookmarks inside the meta dnode to their
	 * normal-object equivalents.
	 */
	uint64_t zb1obj, zb2obj;
	uint64_t zb1L0, zb2L0;
	uint64_t zb1level, zb2level;

	if (zb1->zb_object == zb2->zb_object &&
	    zb1->zb_level == zb2->zb_level &&
	    zb1->zb_blkid == zb2->zb_blkid)
		return (0);

	/*
	 * BP_SPANB calculates the span in blocks.
	 */
	zb1L0 = (zb1->zb_blkid) * BP_SPANB(ibs1, zb1->zb_level);
	zb2L0 = (zb2->zb_blkid) * BP_SPANB(ibs2, zb2->zb_level);

	if (zb1->zb_object == DMU_META_DNODE_OBJECT) {
		zb1obj = zb1L0 * (dbss1 << (SPA_MINBLOCKSHIFT - DNODE_SHIFT));
		zb1L0 = 0;
		zb1level = zb1->zb_level + COMPARE_META_LEVEL;
	} else {
		zb1obj = zb1->zb_object;
		zb1level = zb1->zb_level;
	}

	if (zb2->zb_object == DMU_META_DNODE_OBJECT) {
		zb2obj = zb2L0 * (dbss2 << (SPA_MINBLOCKSHIFT - DNODE_SHIFT));
		zb2L0 = 0;
		zb2level = zb2->zb_level + COMPARE_META_LEVEL;
	} else {
		zb2obj = zb2->zb_object;
		zb2level = zb2->zb_level;
	}

	/* Now that we have a canonical representation, do the comparison. */
	if (zb1obj != zb2obj)
		return (zb1obj < zb2obj ? -1 : 1);
	else if (zb1L0 != zb2L0)
		return (zb1L0 < zb2L0 ? -1 : 1);
	else if (zb1level != zb2level)
		return (zb1level > zb2level ? -1 : 1);
	/*
	 * This can (theoretically) happen if the bookmarks have the same object
	 * and level, but different blkids, if the block sizes are not the same.
	 * There is presently no way to change the indirect block sizes
	 */
	return (0);
}

/*
 *  This function checks the following: given that last_block is the place that
 *  our traversal stopped last time, does that guarantee that we've visited
 *  every node under subtree_root?  Therefore, we can't just use the raw output
 *  of zbookmark_compare.  We have to pass in a modified version of
 *  subtree_root; by incrementing the block id, and then checking whether
 *  last_block is before or equal to that, we can tell whether or not having
 *  visited last_block implies that all of subtree_root's children have been
 *  visited.
 */
boolean_t
zbookmark_subtree_completed(const dnode_phys_t *dnp,
    const zbookmark_phys_t *subtree_root, const zbookmark_phys_t *last_block)
{
	zbookmark_phys_t mod_zb = *subtree_root;
	mod_zb.zb_blkid++;
	ASSERT(last_block->zb_level == 0);

	/* The objset_phys_t isn't before anything. */
	if (dnp == NULL)
		return (B_FALSE);

	/*
	 * We pass in 1ULL << (DNODE_BLOCK_SHIFT - SPA_MINBLOCKSHIFT) for the
	 * data block size in sectors, because that variable is only used if
	 * the bookmark refers to a block in the meta-dnode.  Since we don't
	 * know without examining it what object it refers to, and there's no
	 * harm in passing in this value in other cases, we always pass it in.
	 *
	 * We pass in 0 for the indirect block size shift because zb2 must be
	 * level 0.  The indirect block size is only used to calculate the span
	 * of the bookmark, but since the bookmark must be level 0, the span is
	 * always 1, so the math works out.
	 *
	 * If you make changes to how the zbookmark_compare code works, be sure
	 * to make sure that this code still works afterwards.
	 */
	return (zbookmark_compare(dnp->dn_datablkszsec, dnp->dn_indblkshift,
	    1ULL << (DNODE_BLOCK_SHIFT - SPA_MINBLOCKSHIFT), 0, &mod_zb,
	    last_block) <= 0);
}
