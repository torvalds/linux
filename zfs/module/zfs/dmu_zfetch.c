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
 * Copyright (c) 2013 by Delphix. All rights reserved.
 */

#include <sys/zfs_context.h>
#include <sys/dnode.h>
#include <sys/dmu_objset.h>
#include <sys/dmu_zfetch.h>
#include <sys/dmu.h>
#include <sys/dbuf.h>
#include <sys/kstat.h>

/*
 * I'm against tune-ables, but these should probably exist as tweakable globals
 * until we can get this working the way we want it to.
 */

int zfs_prefetch_disable = 0;

/* max # of streams per zfetch */
unsigned int	zfetch_max_streams = 8;
/* min time before stream reclaim */
unsigned int	zfetch_min_sec_reap = 2;
/* max number of blocks to fetch at a time */
unsigned int	zfetch_block_cap = 256;
/* number of bytes in a array_read at which we stop prefetching (1Mb) */
unsigned long	zfetch_array_rd_sz = 1024 * 1024;

/* forward decls for static routines */
static boolean_t	dmu_zfetch_colinear(zfetch_t *, zstream_t *);
static void		dmu_zfetch_dofetch(zfetch_t *, zstream_t *);
static uint64_t		dmu_zfetch_fetch(dnode_t *, uint64_t, uint64_t);
static uint64_t		dmu_zfetch_fetchsz(dnode_t *, uint64_t, uint64_t);
static boolean_t	dmu_zfetch_find(zfetch_t *, zstream_t *, int);
static int		dmu_zfetch_stream_insert(zfetch_t *, zstream_t *);
static zstream_t	*dmu_zfetch_stream_reclaim(zfetch_t *);
static void		dmu_zfetch_stream_remove(zfetch_t *, zstream_t *);
static int		dmu_zfetch_streams_equal(zstream_t *, zstream_t *);

typedef struct zfetch_stats {
	kstat_named_t zfetchstat_hits;
	kstat_named_t zfetchstat_misses;
	kstat_named_t zfetchstat_colinear_hits;
	kstat_named_t zfetchstat_colinear_misses;
	kstat_named_t zfetchstat_stride_hits;
	kstat_named_t zfetchstat_stride_misses;
	kstat_named_t zfetchstat_reclaim_successes;
	kstat_named_t zfetchstat_reclaim_failures;
	kstat_named_t zfetchstat_stream_resets;
	kstat_named_t zfetchstat_stream_noresets;
	kstat_named_t zfetchstat_bogus_streams;
} zfetch_stats_t;

static zfetch_stats_t zfetch_stats = {
	{ "hits",			KSTAT_DATA_UINT64 },
	{ "misses",			KSTAT_DATA_UINT64 },
	{ "colinear_hits",		KSTAT_DATA_UINT64 },
	{ "colinear_misses",		KSTAT_DATA_UINT64 },
	{ "stride_hits",		KSTAT_DATA_UINT64 },
	{ "stride_misses",		KSTAT_DATA_UINT64 },
	{ "reclaim_successes",		KSTAT_DATA_UINT64 },
	{ "reclaim_failures",		KSTAT_DATA_UINT64 },
	{ "streams_resets",		KSTAT_DATA_UINT64 },
	{ "streams_noresets",		KSTAT_DATA_UINT64 },
	{ "bogus_streams",		KSTAT_DATA_UINT64 },
};

#define	ZFETCHSTAT_INCR(stat, val) \
	atomic_add_64(&zfetch_stats.stat.value.ui64, (val));

#define	ZFETCHSTAT_BUMP(stat)		ZFETCHSTAT_INCR(stat, 1);

kstat_t		*zfetch_ksp;

/*
 * Given a zfetch structure and a zstream structure, determine whether the
 * blocks to be read are part of a co-linear pair of existing prefetch
 * streams.  If a set is found, coalesce the streams, removing one, and
 * configure the prefetch so it looks for a strided access pattern.
 *
 * In other words: if we find two sequential access streams that are
 * the same length and distance N appart, and this read is N from the
 * last stream, then we are probably in a strided access pattern.  So
 * combine the two sequential streams into a single strided stream.
 *
 * Returns whether co-linear streams were found.
 */
static boolean_t
dmu_zfetch_colinear(zfetch_t *zf, zstream_t *zh)
{
	zstream_t	*z_walk;
	zstream_t	*z_comp;

	if (! rw_tryenter(&zf->zf_rwlock, RW_WRITER))
		return (0);

	if (zh == NULL) {
		rw_exit(&zf->zf_rwlock);
		return (0);
	}

	for (z_walk = list_head(&zf->zf_stream); z_walk;
	    z_walk = list_next(&zf->zf_stream, z_walk)) {
		for (z_comp = list_next(&zf->zf_stream, z_walk); z_comp;
		    z_comp = list_next(&zf->zf_stream, z_comp)) {
			int64_t		diff;

			if (z_walk->zst_len != z_walk->zst_stride ||
			    z_comp->zst_len != z_comp->zst_stride) {
				continue;
			}

			diff = z_comp->zst_offset - z_walk->zst_offset;
			if (z_comp->zst_offset + diff == zh->zst_offset) {
				z_walk->zst_offset = zh->zst_offset;
				z_walk->zst_direction = diff < 0 ?
				    ZFETCH_BACKWARD : ZFETCH_FORWARD;
				z_walk->zst_stride =
				    diff * z_walk->zst_direction;
				z_walk->zst_ph_offset =
				    zh->zst_offset + z_walk->zst_stride;
				dmu_zfetch_stream_remove(zf, z_comp);
				mutex_destroy(&z_comp->zst_lock);
				kmem_free(z_comp, sizeof (zstream_t));

				dmu_zfetch_dofetch(zf, z_walk);

				rw_exit(&zf->zf_rwlock);
				return (1);
			}

			diff = z_walk->zst_offset - z_comp->zst_offset;
			if (z_walk->zst_offset + diff == zh->zst_offset) {
				z_walk->zst_offset = zh->zst_offset;
				z_walk->zst_direction = diff < 0 ?
				    ZFETCH_BACKWARD : ZFETCH_FORWARD;
				z_walk->zst_stride =
				    diff * z_walk->zst_direction;
				z_walk->zst_ph_offset =
				    zh->zst_offset + z_walk->zst_stride;
				dmu_zfetch_stream_remove(zf, z_comp);
				mutex_destroy(&z_comp->zst_lock);
				kmem_free(z_comp, sizeof (zstream_t));

				dmu_zfetch_dofetch(zf, z_walk);

				rw_exit(&zf->zf_rwlock);
				return (1);
			}
		}
	}

	rw_exit(&zf->zf_rwlock);
	return (0);
}

/*
 * Given a zstream_t, determine the bounds of the prefetch.  Then call the
 * routine that actually prefetches the individual blocks.
 */
static void
dmu_zfetch_dofetch(zfetch_t *zf, zstream_t *zs)
{
	uint64_t	prefetch_tail;
	uint64_t	prefetch_limit;
	uint64_t	prefetch_ofst;
	uint64_t	prefetch_len;
	uint64_t	blocks_fetched;

	zs->zst_stride = MAX((int64_t)zs->zst_stride, zs->zst_len);
	zs->zst_cap = MIN(zfetch_block_cap, 2 * zs->zst_cap);

	prefetch_tail = MAX((int64_t)zs->zst_ph_offset,
	    (int64_t)(zs->zst_offset + zs->zst_stride));
	/*
	 * XXX: use a faster division method?
	 */
	prefetch_limit = zs->zst_offset + zs->zst_len +
	    (zs->zst_cap * zs->zst_stride) / zs->zst_len;

	while (prefetch_tail < prefetch_limit) {
		prefetch_ofst = zs->zst_offset + zs->zst_direction *
		    (prefetch_tail - zs->zst_offset);

		prefetch_len = zs->zst_len;

		/*
		 * Don't prefetch beyond the end of the file, if working
		 * backwards.
		 */
		if ((zs->zst_direction == ZFETCH_BACKWARD) &&
		    (prefetch_ofst > prefetch_tail)) {
			prefetch_len += prefetch_ofst;
			prefetch_ofst = 0;
		}

		/* don't prefetch more than we're supposed to */
		if (prefetch_len > zs->zst_len)
			break;

		blocks_fetched = dmu_zfetch_fetch(zf->zf_dnode,
		    prefetch_ofst, zs->zst_len);

		prefetch_tail += zs->zst_stride;
		/* stop if we've run out of stuff to prefetch */
		if (blocks_fetched < zs->zst_len)
			break;
	}
	zs->zst_ph_offset = prefetch_tail;
	zs->zst_last = ddi_get_lbolt();
}

void
zfetch_init(void)
{

	zfetch_ksp = kstat_create("zfs", 0, "zfetchstats", "misc",
	    KSTAT_TYPE_NAMED, sizeof (zfetch_stats) / sizeof (kstat_named_t),
	    KSTAT_FLAG_VIRTUAL);

	if (zfetch_ksp != NULL) {
		zfetch_ksp->ks_data = &zfetch_stats;
		kstat_install(zfetch_ksp);
	}
}

void
zfetch_fini(void)
{
	if (zfetch_ksp != NULL) {
		kstat_delete(zfetch_ksp);
		zfetch_ksp = NULL;
	}
}

/*
 * This takes a pointer to a zfetch structure and a dnode.  It performs the
 * necessary setup for the zfetch structure, grokking data from the
 * associated dnode.
 */
void
dmu_zfetch_init(zfetch_t *zf, dnode_t *dno)
{
	if (zf == NULL) {
		return;
	}

	zf->zf_dnode = dno;
	zf->zf_stream_cnt = 0;
	zf->zf_alloc_fail = 0;

	list_create(&zf->zf_stream, sizeof (zstream_t),
	    offsetof(zstream_t, zst_node));

	rw_init(&zf->zf_rwlock, NULL, RW_DEFAULT, NULL);
}

/*
 * This function computes the actual size, in blocks, that can be prefetched,
 * and fetches it.
 */
static uint64_t
dmu_zfetch_fetch(dnode_t *dn, uint64_t blkid, uint64_t nblks)
{
	uint64_t	fetchsz;
	uint64_t	i;

	fetchsz = dmu_zfetch_fetchsz(dn, blkid, nblks);

	for (i = 0; i < fetchsz; i++) {
		dbuf_prefetch(dn, blkid + i, ZIO_PRIORITY_ASYNC_READ);
	}

	return (fetchsz);
}

/*
 * this function returns the number of blocks that would be prefetched, based
 * upon the supplied dnode, blockid, and nblks.  This is used so that we can
 * update streams in place, and then prefetch with their old value after the
 * fact.  This way, we can delay the prefetch, but subsequent accesses to the
 * stream won't result in the same data being prefetched multiple times.
 */
static uint64_t
dmu_zfetch_fetchsz(dnode_t *dn, uint64_t blkid, uint64_t nblks)
{
	uint64_t	fetchsz;

	if (blkid > dn->dn_maxblkid) {
		return (0);
	}

	/* compute fetch size */
	if (blkid + nblks + 1 > dn->dn_maxblkid) {
		fetchsz = (dn->dn_maxblkid - blkid) + 1;
		ASSERT(blkid + fetchsz - 1 <= dn->dn_maxblkid);
	} else {
		fetchsz = nblks;
	}


	return (fetchsz);
}

/*
 * given a zfetch and a zstream structure, see if there is an associated zstream
 * for this block read.  If so, it starts a prefetch for the stream it
 * located and returns true, otherwise it returns false
 */
static boolean_t
dmu_zfetch_find(zfetch_t *zf, zstream_t *zh, int prefetched)
{
	zstream_t	*zs;
	int64_t		diff;
	int		reset = !prefetched;
	int		rc = 0;

	if (zh == NULL)
		return (0);

	/*
	 * XXX: This locking strategy is a bit coarse; however, it's impact has
	 * yet to be tested.  If this turns out to be an issue, it can be
	 * modified in a number of different ways.
	 */

	rw_enter(&zf->zf_rwlock, RW_READER);
top:

	for (zs = list_head(&zf->zf_stream); zs;
	    zs = list_next(&zf->zf_stream, zs)) {

		/*
		 * XXX - should this be an assert?
		 */
		if (zs->zst_len == 0) {
			/* bogus stream */
			ZFETCHSTAT_BUMP(zfetchstat_bogus_streams);
			continue;
		}

		/*
		 * We hit this case when we are in a strided prefetch stream:
		 * we will read "len" blocks before "striding".
		 */
		if (zh->zst_offset >= zs->zst_offset &&
		    zh->zst_offset < zs->zst_offset + zs->zst_len) {
			if (prefetched) {
				/* already fetched */
				ZFETCHSTAT_BUMP(zfetchstat_stride_hits);
				rc = 1;
				goto out;
			} else {
				ZFETCHSTAT_BUMP(zfetchstat_stride_misses);
			}
		}

		/*
		 * This is the forward sequential read case: we increment
		 * len by one each time we hit here, so we will enter this
		 * case on every read.
		 */
		if (zh->zst_offset == zs->zst_offset + zs->zst_len) {

			reset = !prefetched && zs->zst_len > 1;

			mutex_enter(&zs->zst_lock);

			if (zh->zst_offset != zs->zst_offset + zs->zst_len) {
				mutex_exit(&zs->zst_lock);
				goto top;
			}
			zs->zst_len += zh->zst_len;
			diff = zs->zst_len - zfetch_block_cap;
			if (diff > 0) {
				zs->zst_offset += diff;
				zs->zst_len = zs->zst_len > diff ?
				    zs->zst_len - diff : 0;
			}
			zs->zst_direction = ZFETCH_FORWARD;

			break;

		/*
		 * Same as above, but reading backwards through the file.
		 */
		} else if (zh->zst_offset == zs->zst_offset - zh->zst_len) {
			/* backwards sequential access */

			reset = !prefetched && zs->zst_len > 1;

			mutex_enter(&zs->zst_lock);

			if (zh->zst_offset != zs->zst_offset - zh->zst_len) {
				mutex_exit(&zs->zst_lock);
				goto top;
			}

			zs->zst_offset = zs->zst_offset > zh->zst_len ?
			    zs->zst_offset - zh->zst_len : 0;
			zs->zst_ph_offset = zs->zst_ph_offset > zh->zst_len ?
			    zs->zst_ph_offset - zh->zst_len : 0;
			zs->zst_len += zh->zst_len;

			diff = zs->zst_len - zfetch_block_cap;
			if (diff > 0) {
				zs->zst_ph_offset = zs->zst_ph_offset > diff ?
				    zs->zst_ph_offset - diff : 0;
				zs->zst_len = zs->zst_len > diff ?
				    zs->zst_len - diff : zs->zst_len;
			}
			zs->zst_direction = ZFETCH_BACKWARD;

			break;

		} else if ((zh->zst_offset - zs->zst_offset - zs->zst_stride <
		    zs->zst_len) && (zs->zst_len != zs->zst_stride)) {
			/* strided forward access */

			mutex_enter(&zs->zst_lock);

			if ((zh->zst_offset - zs->zst_offset - zs->zst_stride >=
			    zs->zst_len) || (zs->zst_len == zs->zst_stride)) {
				mutex_exit(&zs->zst_lock);
				goto top;
			}

			zs->zst_offset += zs->zst_stride;
			zs->zst_direction = ZFETCH_FORWARD;

			break;

		} else if ((zh->zst_offset - zs->zst_offset + zs->zst_stride <
		    zs->zst_len) && (zs->zst_len != zs->zst_stride)) {
			/* strided reverse access */

			mutex_enter(&zs->zst_lock);

			if ((zh->zst_offset - zs->zst_offset + zs->zst_stride >=
			    zs->zst_len) || (zs->zst_len == zs->zst_stride)) {
				mutex_exit(&zs->zst_lock);
				goto top;
			}

			zs->zst_offset = zs->zst_offset > zs->zst_stride ?
			    zs->zst_offset - zs->zst_stride : 0;
			zs->zst_ph_offset = (zs->zst_ph_offset >
			    (2 * zs->zst_stride)) ?
			    (zs->zst_ph_offset - (2 * zs->zst_stride)) : 0;
			zs->zst_direction = ZFETCH_BACKWARD;

			break;
		}
	}

	if (zs) {
		if (reset) {
			zstream_t *remove = zs;

			ZFETCHSTAT_BUMP(zfetchstat_stream_resets);
			rc = 0;
			mutex_exit(&zs->zst_lock);
			rw_exit(&zf->zf_rwlock);
			rw_enter(&zf->zf_rwlock, RW_WRITER);
			/*
			 * Relocate the stream, in case someone removes
			 * it while we were acquiring the WRITER lock.
			 */
			for (zs = list_head(&zf->zf_stream); zs;
			    zs = list_next(&zf->zf_stream, zs)) {
				if (zs == remove) {
					dmu_zfetch_stream_remove(zf, zs);
					mutex_destroy(&zs->zst_lock);
					kmem_free(zs, sizeof (zstream_t));
					break;
				}
			}
		} else {
			ZFETCHSTAT_BUMP(zfetchstat_stream_noresets);
			rc = 1;
			dmu_zfetch_dofetch(zf, zs);
			mutex_exit(&zs->zst_lock);
		}
	}
out:
	rw_exit(&zf->zf_rwlock);
	return (rc);
}

/*
 * Clean-up state associated with a zfetch structure.  This frees allocated
 * structure members, empties the zf_stream tree, and generally makes things
 * nice.  This doesn't free the zfetch_t itself, that's left to the caller.
 */
void
dmu_zfetch_rele(zfetch_t *zf)
{
	zstream_t	*zs;
	zstream_t	*zs_next;

	ASSERT(!RW_LOCK_HELD(&zf->zf_rwlock));

	for (zs = list_head(&zf->zf_stream); zs; zs = zs_next) {
		zs_next = list_next(&zf->zf_stream, zs);

		list_remove(&zf->zf_stream, zs);
		mutex_destroy(&zs->zst_lock);
		kmem_free(zs, sizeof (zstream_t));
	}
	list_destroy(&zf->zf_stream);
	rw_destroy(&zf->zf_rwlock);

	zf->zf_dnode = NULL;
}

/*
 * Given a zfetch and zstream structure, insert the zstream structure into the
 * AVL tree contained within the zfetch structure.  Peform the appropriate
 * book-keeping.  It is possible that another thread has inserted a stream which
 * matches one that we are about to insert, so we must be sure to check for this
 * case.  If one is found, return failure, and let the caller cleanup the
 * duplicates.
 */
static int
dmu_zfetch_stream_insert(zfetch_t *zf, zstream_t *zs)
{
	zstream_t	*zs_walk;
	zstream_t	*zs_next;

	ASSERT(RW_WRITE_HELD(&zf->zf_rwlock));

	for (zs_walk = list_head(&zf->zf_stream); zs_walk; zs_walk = zs_next) {
		zs_next = list_next(&zf->zf_stream, zs_walk);

		if (dmu_zfetch_streams_equal(zs_walk, zs)) {
			return (0);
		}
	}

	list_insert_head(&zf->zf_stream, zs);
	zf->zf_stream_cnt++;
	return (1);
}


/*
 * Walk the list of zstreams in the given zfetch, find an old one (by time), and
 * reclaim it for use by the caller.
 */
static zstream_t *
dmu_zfetch_stream_reclaim(zfetch_t *zf)
{
	zstream_t	*zs;

	if (! rw_tryenter(&zf->zf_rwlock, RW_WRITER))
		return (0);

	for (zs = list_head(&zf->zf_stream); zs;
	    zs = list_next(&zf->zf_stream, zs)) {

		if (((ddi_get_lbolt() - zs->zst_last)/hz) > zfetch_min_sec_reap)
			break;
	}

	if (zs) {
		dmu_zfetch_stream_remove(zf, zs);
		mutex_destroy(&zs->zst_lock);
		bzero(zs, sizeof (zstream_t));
	} else {
		zf->zf_alloc_fail++;
	}
	rw_exit(&zf->zf_rwlock);

	return (zs);
}

/*
 * Given a zfetch and zstream structure, remove the zstream structure from its
 * container in the zfetch structure.  Perform the appropriate book-keeping.
 */
static void
dmu_zfetch_stream_remove(zfetch_t *zf, zstream_t *zs)
{
	ASSERT(RW_WRITE_HELD(&zf->zf_rwlock));

	list_remove(&zf->zf_stream, zs);
	zf->zf_stream_cnt--;
}

static int
dmu_zfetch_streams_equal(zstream_t *zs1, zstream_t *zs2)
{
	if (zs1->zst_offset != zs2->zst_offset)
		return (0);

	if (zs1->zst_len != zs2->zst_len)
		return (0);

	if (zs1->zst_stride != zs2->zst_stride)
		return (0);

	if (zs1->zst_ph_offset != zs2->zst_ph_offset)
		return (0);

	if (zs1->zst_cap != zs2->zst_cap)
		return (0);

	if (zs1->zst_direction != zs2->zst_direction)
		return (0);

	return (1);
}

/*
 * This is the prefetch entry point.  It calls all of the other dmu_zfetch
 * routines to create, delete, find, or operate upon prefetch streams.
 */
void
dmu_zfetch(zfetch_t *zf, uint64_t offset, uint64_t size, int prefetched)
{
	zstream_t	zst;
	zstream_t	*newstream;
	boolean_t	fetched;
	int		inserted;
	unsigned int	blkshft;
	uint64_t	blksz;

	if (zfs_prefetch_disable)
		return;

	/* files that aren't ln2 blocksz are only one block -- nothing to do */
	if (!zf->zf_dnode->dn_datablkshift)
		return;

	/* convert offset and size, into blockid and nblocks */
	blkshft = zf->zf_dnode->dn_datablkshift;
	blksz = (1 << blkshft);

	bzero(&zst, sizeof (zstream_t));
	zst.zst_offset = offset >> blkshft;
	zst.zst_len = (P2ROUNDUP(offset + size, blksz) -
	    P2ALIGN(offset, blksz)) >> blkshft;

	fetched = dmu_zfetch_find(zf, &zst, prefetched);
	if (fetched) {
		ZFETCHSTAT_BUMP(zfetchstat_hits);
	} else {
		ZFETCHSTAT_BUMP(zfetchstat_misses);
		if ((fetched = dmu_zfetch_colinear(zf, &zst))) {
			ZFETCHSTAT_BUMP(zfetchstat_colinear_hits);
		} else {
			ZFETCHSTAT_BUMP(zfetchstat_colinear_misses);
		}
	}

	if (!fetched) {
		newstream = dmu_zfetch_stream_reclaim(zf);

		/*
		 * we still couldn't find a stream, drop the lock, and allocate
		 * one if possible.  Otherwise, give up and go home.
		 */
		if (newstream) {
			ZFETCHSTAT_BUMP(zfetchstat_reclaim_successes);
		} else {
			uint64_t	maxblocks;
			uint32_t	max_streams;
			uint32_t	cur_streams;

			ZFETCHSTAT_BUMP(zfetchstat_reclaim_failures);
			cur_streams = zf->zf_stream_cnt;
			maxblocks = zf->zf_dnode->dn_maxblkid;

			max_streams = MIN(zfetch_max_streams,
			    (maxblocks / zfetch_block_cap));
			if (max_streams == 0) {
				max_streams++;
			}

			if (cur_streams >= max_streams) {
				return;
			}
			newstream =
			    kmem_zalloc(sizeof (zstream_t), KM_SLEEP);
		}

		newstream->zst_offset = zst.zst_offset;
		newstream->zst_len = zst.zst_len;
		newstream->zst_stride = zst.zst_len;
		newstream->zst_ph_offset = zst.zst_len + zst.zst_offset;
		newstream->zst_cap = zst.zst_len;
		newstream->zst_direction = ZFETCH_FORWARD;
		newstream->zst_last = ddi_get_lbolt();

		mutex_init(&newstream->zst_lock, NULL, MUTEX_DEFAULT, NULL);

		rw_enter(&zf->zf_rwlock, RW_WRITER);
		inserted = dmu_zfetch_stream_insert(zf, newstream);
		rw_exit(&zf->zf_rwlock);

		if (!inserted) {
			mutex_destroy(&newstream->zst_lock);
			kmem_free(newstream, sizeof (zstream_t));
		}
	}
}

#if defined(_KERNEL) && defined(HAVE_SPL)
module_param(zfs_prefetch_disable, int, 0644);
MODULE_PARM_DESC(zfs_prefetch_disable, "Disable all ZFS prefetching");

module_param(zfetch_max_streams, uint, 0644);
MODULE_PARM_DESC(zfetch_max_streams, "Max number of streams per zfetch");

module_param(zfetch_min_sec_reap, uint, 0644);
MODULE_PARM_DESC(zfetch_min_sec_reap, "Min time before stream reclaim");

module_param(zfetch_block_cap, uint, 0644);
MODULE_PARM_DESC(zfetch_block_cap, "Max number of blocks to fetch at a time");

module_param(zfetch_array_rd_sz, ulong, 0644);
MODULE_PARM_DESC(zfetch_array_rd_sz, "Number of bytes in a array_read");
#endif
