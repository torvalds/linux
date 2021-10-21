// SPDX-License-Identifier: GPL-2.0-or-later
/* Manage high-level VFS aspects of a cache.
 *
 * Copyright (C) 2007, 2021 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */

#include <linux/slab.h>
#include <linux/statfs.h>
#include <linux/namei.h>
#include "internal.h"

/*
 * See if we have space for a number of pages and/or a number of files in the
 * cache
 */
int cachefiles_has_space(struct cachefiles_cache *cache,
			 unsigned fnr, unsigned bnr)
{
	struct kstatfs stats;
	u64 b_avail, b_writing;
	int ret;

	struct path path = {
		.mnt	= cache->mnt,
		.dentry	= cache->mnt->mnt_root,
	};

	//_enter("{%llu,%llu,%llu,%llu,%llu,%llu},%u,%u",
	//       (unsigned long long) cache->frun,
	//       (unsigned long long) cache->fcull,
	//       (unsigned long long) cache->fstop,
	//       (unsigned long long) cache->brun,
	//       (unsigned long long) cache->bcull,
	//       (unsigned long long) cache->bstop,
	//       fnr, bnr);

	/* find out how many pages of blockdev are available */
	memset(&stats, 0, sizeof(stats));

	ret = vfs_statfs(&path, &stats);
	if (ret < 0) {
		trace_cachefiles_vfs_error(NULL, d_inode(path.dentry), ret,
					   cachefiles_trace_statfs_error);
		if (ret == -EIO)
			cachefiles_io_error(cache, "statfs failed");
		_leave(" = %d", ret);
		return ret;
	}

	b_avail = stats.f_bavail >> cache->bshift;
	b_writing = atomic_long_read(&cache->b_writing);
	if (b_avail > b_writing)
		b_avail -= b_writing;
	else
		b_avail = 0;

	//_debug("avail %llu,%llu",
	//       (unsigned long long)stats.f_ffree,
	//       (unsigned long long)b_avail);

	/* see if there is sufficient space */
	if (stats.f_ffree > fnr)
		stats.f_ffree -= fnr;
	else
		stats.f_ffree = 0;

	if (b_avail > bnr)
		b_avail -= bnr;
	else
		b_avail = 0;

	ret = -ENOBUFS;
	if (stats.f_ffree < cache->fstop ||
	    b_avail < cache->bstop)
		goto begin_cull;

	ret = 0;
	if (stats.f_ffree < cache->fcull ||
	    b_avail < cache->bcull)
		goto begin_cull;

	if (test_bit(CACHEFILES_CULLING, &cache->flags) &&
	    stats.f_ffree >= cache->frun &&
	    b_avail >= cache->brun &&
	    test_and_clear_bit(CACHEFILES_CULLING, &cache->flags)
	    ) {
		_debug("cease culling");
		cachefiles_state_changed(cache);
	}

	//_leave(" = 0");
	return 0;

begin_cull:
	if (!test_and_set_bit(CACHEFILES_CULLING, &cache->flags)) {
		_debug("### CULL CACHE ###");
		cachefiles_state_changed(cache);
	}

	_leave(" = %d", ret);
	return ret;
}
