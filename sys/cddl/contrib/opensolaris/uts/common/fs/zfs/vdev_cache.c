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
 * Copyright (c) 2013, 2017 by Delphix. All rights reserved.
 */

#include <sys/zfs_context.h>
#include <sys/spa.h>
#include <sys/vdev_impl.h>
#include <sys/zio.h>
#include <sys/kstat.h>
#include <sys/abd.h>

/*
 * Virtual device read-ahead caching.
 *
 * This file implements a simple LRU read-ahead cache.  When the DMU reads
 * a given block, it will often want other, nearby blocks soon thereafter.
 * We take advantage of this by reading a larger disk region and caching
 * the result.  In the best case, this can turn 128 back-to-back 512-byte
 * reads into a single 64k read followed by 127 cache hits; this reduces
 * latency dramatically.  In the worst case, it can turn an isolated 512-byte
 * read into a 64k read, which doesn't affect latency all that much but is
 * terribly wasteful of bandwidth.  A more intelligent version of the cache
 * could keep track of access patterns and not do read-ahead unless it sees
 * at least two temporally close I/Os to the same region.  Currently, only
 * metadata I/O is inflated.  A futher enhancement could take advantage of
 * more semantic information about the I/O.  And it could use something
 * faster than an AVL tree; that was chosen solely for convenience.
 *
 * There are five cache operations: allocate, fill, read, write, evict.
 *
 * (1) Allocate.  This reserves a cache entry for the specified region.
 *     We separate the allocate and fill operations so that multiple threads
 *     don't generate I/O for the same cache miss.
 *
 * (2) Fill.  When the I/O for a cache miss completes, the fill routine
 *     places the data in the previously allocated cache entry.
 *
 * (3) Read.  Read data from the cache.
 *
 * (4) Write.  Update cache contents after write completion.
 *
 * (5) Evict.  When allocating a new entry, we evict the oldest (LRU) entry
 *     if the total cache size exceeds zfs_vdev_cache_size.
 */

/*
 * These tunables are for performance analysis.
 */
/*
 * All i/os smaller than zfs_vdev_cache_max will be turned into
 * 1<<zfs_vdev_cache_bshift byte reads by the vdev_cache (aka software
 * track buffer).  At most zfs_vdev_cache_size bytes will be kept in each
 * vdev's vdev_cache.
 *
 * TODO: Note that with the current ZFS code, it turns out that the
 * vdev cache is not helpful, and in some cases actually harmful.  It
 * is better if we disable this.  Once some time has passed, we should
 * actually remove this to simplify the code.  For now we just disable
 * it by setting the zfs_vdev_cache_size to zero.  Note that Solaris 11
 * has made these same changes.
 */
int zfs_vdev_cache_max = 1<<14;			/* 16KB */
int zfs_vdev_cache_size = 0;
int zfs_vdev_cache_bshift = 16;

#define	VCBS (1 << zfs_vdev_cache_bshift)	/* 64KB */

SYSCTL_DECL(_vfs_zfs_vdev);
SYSCTL_NODE(_vfs_zfs_vdev, OID_AUTO, cache, CTLFLAG_RW, 0, "ZFS VDEV Cache");
SYSCTL_INT(_vfs_zfs_vdev_cache, OID_AUTO, max, CTLFLAG_RDTUN,
    &zfs_vdev_cache_max, 0, "Maximum I/O request size that increase read size");
SYSCTL_INT(_vfs_zfs_vdev_cache, OID_AUTO, size, CTLFLAG_RDTUN,
    &zfs_vdev_cache_size, 0, "Size of VDEV cache");
SYSCTL_INT(_vfs_zfs_vdev_cache, OID_AUTO, bshift, CTLFLAG_RDTUN,
    &zfs_vdev_cache_bshift, 0, "Turn too small requests into 1 << this value");

kstat_t	*vdc_ksp = NULL;

typedef struct vdc_stats {
	kstat_named_t vdc_stat_delegations;
	kstat_named_t vdc_stat_hits;
	kstat_named_t vdc_stat_misses;
} vdc_stats_t;

static vdc_stats_t vdc_stats = {
	{ "delegations",	KSTAT_DATA_UINT64 },
	{ "hits",		KSTAT_DATA_UINT64 },
	{ "misses",		KSTAT_DATA_UINT64 }
};

#define	VDCSTAT_BUMP(stat)	atomic_inc_64(&vdc_stats.stat.value.ui64);

static int
vdev_cache_offset_compare(const void *a1, const void *a2)
{
	const vdev_cache_entry_t *ve1 = a1;
	const vdev_cache_entry_t *ve2 = a2;

	if (ve1->ve_offset < ve2->ve_offset)
		return (-1);
	if (ve1->ve_offset > ve2->ve_offset)
		return (1);
	return (0);
}

static int
vdev_cache_lastused_compare(const void *a1, const void *a2)
{
	const vdev_cache_entry_t *ve1 = a1;
	const vdev_cache_entry_t *ve2 = a2;

	if (ve1->ve_lastused < ve2->ve_lastused)
		return (-1);
	if (ve1->ve_lastused > ve2->ve_lastused)
		return (1);

	/*
	 * Among equally old entries, sort by offset to ensure uniqueness.
	 */
	return (vdev_cache_offset_compare(a1, a2));
}

/*
 * Evict the specified entry from the cache.
 */
static void
vdev_cache_evict(vdev_cache_t *vc, vdev_cache_entry_t *ve)
{
	ASSERT(MUTEX_HELD(&vc->vc_lock));
	ASSERT3P(ve->ve_fill_io, ==, NULL);
	ASSERT3P(ve->ve_abd, !=, NULL);

	avl_remove(&vc->vc_lastused_tree, ve);
	avl_remove(&vc->vc_offset_tree, ve);
	abd_free(ve->ve_abd);
	kmem_free(ve, sizeof (vdev_cache_entry_t));
}

/*
 * Allocate an entry in the cache.  At the point we don't have the data,
 * we're just creating a placeholder so that multiple threads don't all
 * go off and read the same blocks.
 */
static vdev_cache_entry_t *
vdev_cache_allocate(zio_t *zio)
{
	vdev_cache_t *vc = &zio->io_vd->vdev_cache;
	uint64_t offset = P2ALIGN(zio->io_offset, VCBS);
	vdev_cache_entry_t *ve;

	ASSERT(MUTEX_HELD(&vc->vc_lock));

	if (zfs_vdev_cache_size == 0)
		return (NULL);

	/*
	 * If adding a new entry would exceed the cache size,
	 * evict the oldest entry (LRU).
	 */
	if ((avl_numnodes(&vc->vc_lastused_tree) << zfs_vdev_cache_bshift) >
	    zfs_vdev_cache_size) {
		ve = avl_first(&vc->vc_lastused_tree);
		if (ve->ve_fill_io != NULL)
			return (NULL);
		ASSERT3U(ve->ve_hits, !=, 0);
		vdev_cache_evict(vc, ve);
	}

	ve = kmem_zalloc(sizeof (vdev_cache_entry_t), KM_SLEEP);
	ve->ve_offset = offset;
	ve->ve_lastused = ddi_get_lbolt();
	ve->ve_abd = abd_alloc_for_io(VCBS, B_TRUE);

	avl_add(&vc->vc_offset_tree, ve);
	avl_add(&vc->vc_lastused_tree, ve);

	return (ve);
}

static void
vdev_cache_hit(vdev_cache_t *vc, vdev_cache_entry_t *ve, zio_t *zio)
{
	uint64_t cache_phase = P2PHASE(zio->io_offset, VCBS);

	ASSERT(MUTEX_HELD(&vc->vc_lock));
	ASSERT3P(ve->ve_fill_io, ==, NULL);

	if (ve->ve_lastused != ddi_get_lbolt()) {
		avl_remove(&vc->vc_lastused_tree, ve);
		ve->ve_lastused = ddi_get_lbolt();
		avl_add(&vc->vc_lastused_tree, ve);
	}

	ve->ve_hits++;
	abd_copy_off(zio->io_abd, ve->ve_abd, 0, cache_phase, zio->io_size);
}

/*
 * Fill a previously allocated cache entry with data.
 */
static void
vdev_cache_fill(zio_t *fio)
{
	vdev_t *vd = fio->io_vd;
	vdev_cache_t *vc = &vd->vdev_cache;
	vdev_cache_entry_t *ve = fio->io_private;
	zio_t *pio;

	ASSERT3U(fio->io_size, ==, VCBS);

	/*
	 * Add data to the cache.
	 */
	mutex_enter(&vc->vc_lock);

	ASSERT3P(ve->ve_fill_io, ==, fio);
	ASSERT3U(ve->ve_offset, ==, fio->io_offset);
	ASSERT3P(ve->ve_abd, ==, fio->io_abd);

	ve->ve_fill_io = NULL;

	/*
	 * Even if this cache line was invalidated by a missed write update,
	 * any reads that were queued up before the missed update are still
	 * valid, so we can satisfy them from this line before we evict it.
	 */
	zio_link_t *zl = NULL;
	while ((pio = zio_walk_parents(fio, &zl)) != NULL)
		vdev_cache_hit(vc, ve, pio);

	if (fio->io_error || ve->ve_missed_update)
		vdev_cache_evict(vc, ve);

	mutex_exit(&vc->vc_lock);
}

/*
 * Read data from the cache.  Returns B_TRUE cache hit, B_FALSE on miss.
 */
boolean_t
vdev_cache_read(zio_t *zio)
{
	vdev_cache_t *vc = &zio->io_vd->vdev_cache;
	vdev_cache_entry_t *ve, ve_search;
	uint64_t cache_offset = P2ALIGN(zio->io_offset, VCBS);
	uint64_t cache_phase = P2PHASE(zio->io_offset, VCBS);
	zio_t *fio;

	ASSERT3U(zio->io_type, ==, ZIO_TYPE_READ);

	if (zio->io_flags & ZIO_FLAG_DONT_CACHE)
		return (B_FALSE);

	if (zio->io_size > zfs_vdev_cache_max)
		return (B_FALSE);

	/*
	 * If the I/O straddles two or more cache blocks, don't cache it.
	 */
	if (P2BOUNDARY(zio->io_offset, zio->io_size, VCBS))
		return (B_FALSE);

	ASSERT3U(cache_phase + zio->io_size, <=, VCBS);

	mutex_enter(&vc->vc_lock);

	ve_search.ve_offset = cache_offset;
	ve = avl_find(&vc->vc_offset_tree, &ve_search, NULL);

	if (ve != NULL) {
		if (ve->ve_missed_update) {
			mutex_exit(&vc->vc_lock);
			return (B_FALSE);
		}

		if ((fio = ve->ve_fill_io) != NULL) {
			zio_vdev_io_bypass(zio);
			zio_add_child(zio, fio);
			mutex_exit(&vc->vc_lock);
			VDCSTAT_BUMP(vdc_stat_delegations);
			return (B_TRUE);
		}

		vdev_cache_hit(vc, ve, zio);
		zio_vdev_io_bypass(zio);

		mutex_exit(&vc->vc_lock);
		VDCSTAT_BUMP(vdc_stat_hits);
		return (B_TRUE);
	}

	ve = vdev_cache_allocate(zio);

	if (ve == NULL) {
		mutex_exit(&vc->vc_lock);
		return (B_FALSE);
	}

	fio = zio_vdev_delegated_io(zio->io_vd, cache_offset,
	    ve->ve_abd, VCBS, ZIO_TYPE_READ, ZIO_PRIORITY_NOW,
	    ZIO_FLAG_DONT_CACHE, vdev_cache_fill, ve);

	ve->ve_fill_io = fio;
	zio_vdev_io_bypass(zio);
	zio_add_child(zio, fio);

	mutex_exit(&vc->vc_lock);
	zio_nowait(fio);
	VDCSTAT_BUMP(vdc_stat_misses);

	return (B_TRUE);
}

/*
 * Update cache contents upon write completion.
 */
void
vdev_cache_write(zio_t *zio)
{
	vdev_cache_t *vc = &zio->io_vd->vdev_cache;
	vdev_cache_entry_t *ve, ve_search;
	uint64_t io_start = zio->io_offset;
	uint64_t io_end = io_start + zio->io_size;
	uint64_t min_offset = P2ALIGN(io_start, VCBS);
	uint64_t max_offset = P2ROUNDUP(io_end, VCBS);
	avl_index_t where;

	ASSERT3U(zio->io_type, ==, ZIO_TYPE_WRITE);

	mutex_enter(&vc->vc_lock);

	ve_search.ve_offset = min_offset;
	ve = avl_find(&vc->vc_offset_tree, &ve_search, &where);

	if (ve == NULL)
		ve = avl_nearest(&vc->vc_offset_tree, where, AVL_AFTER);

	while (ve != NULL && ve->ve_offset < max_offset) {
		uint64_t start = MAX(ve->ve_offset, io_start);
		uint64_t end = MIN(ve->ve_offset + VCBS, io_end);

		if (ve->ve_fill_io != NULL) {
			ve->ve_missed_update = 1;
		} else {
			abd_copy_off(ve->ve_abd, zio->io_abd,
			    start - ve->ve_offset, start - io_start,
			    end - start);
		}
		ve = AVL_NEXT(&vc->vc_offset_tree, ve);
	}
	mutex_exit(&vc->vc_lock);
}

void
vdev_cache_purge(vdev_t *vd)
{
	vdev_cache_t *vc = &vd->vdev_cache;
	vdev_cache_entry_t *ve;

	mutex_enter(&vc->vc_lock);
	while ((ve = avl_first(&vc->vc_offset_tree)) != NULL)
		vdev_cache_evict(vc, ve);
	mutex_exit(&vc->vc_lock);
}

void
vdev_cache_init(vdev_t *vd)
{
	vdev_cache_t *vc = &vd->vdev_cache;

	mutex_init(&vc->vc_lock, NULL, MUTEX_DEFAULT, NULL);

	avl_create(&vc->vc_offset_tree, vdev_cache_offset_compare,
	    sizeof (vdev_cache_entry_t),
	    offsetof(struct vdev_cache_entry, ve_offset_node));

	avl_create(&vc->vc_lastused_tree, vdev_cache_lastused_compare,
	    sizeof (vdev_cache_entry_t),
	    offsetof(struct vdev_cache_entry, ve_lastused_node));
}

void
vdev_cache_fini(vdev_t *vd)
{
	vdev_cache_t *vc = &vd->vdev_cache;

	vdev_cache_purge(vd);

	avl_destroy(&vc->vc_offset_tree);
	avl_destroy(&vc->vc_lastused_tree);

	mutex_destroy(&vc->vc_lock);
}

void
vdev_cache_stat_init(void)
{
	vdc_ksp = kstat_create("zfs", 0, "vdev_cache_stats", "misc",
	    KSTAT_TYPE_NAMED, sizeof (vdc_stats) / sizeof (kstat_named_t),
	    KSTAT_FLAG_VIRTUAL);
	if (vdc_ksp != NULL) {
		vdc_ksp->ks_data = &vdc_stats;
		kstat_install(vdc_ksp);
	}
}

void
vdev_cache_stat_fini(void)
{
	if (vdc_ksp != NULL) {
		kstat_delete(vdc_ksp);
		vdc_ksp = NULL;
	}
}
