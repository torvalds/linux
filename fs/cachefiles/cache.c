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
 * Bring a cache online.
 */
int cachefiles_add_cache(struct cachefiles_cache *cache)
{
	struct fscache_cache *cache_cookie;
	struct path path;
	struct kstatfs stats;
	struct dentry *graveyard, *cachedir, *root;
	const struct cred *saved_cred;
	int ret;

	_enter("");

	cache_cookie = fscache_acquire_cache(cache->tag);
	if (IS_ERR(cache_cookie))
		return PTR_ERR(cache_cookie);

	/* we want to work under the module's security ID */
	ret = cachefiles_get_security_ID(cache);
	if (ret < 0)
		goto error_getsec;

	cachefiles_begin_secure(cache, &saved_cred);

	/* look up the directory at the root of the cache */
	ret = kern_path(cache->rootdirname, LOOKUP_DIRECTORY, &path);
	if (ret < 0)
		goto error_open_root;

	cache->mnt = path.mnt;
	root = path.dentry;

	ret = -EINVAL;
	if (mnt_user_ns(path.mnt) != &init_user_ns) {
		pr_warn("File cache on idmapped mounts not supported");
		goto error_unsupported;
	}

	/* check parameters */
	ret = -EOPNOTSUPP;
	if (d_is_negative(root) ||
	    !d_backing_inode(root)->i_op->lookup ||
	    !d_backing_inode(root)->i_op->mkdir ||
	    !(d_backing_inode(root)->i_opflags & IOP_XATTR) ||
	    !root->d_sb->s_op->statfs ||
	    !root->d_sb->s_op->sync_fs ||
	    root->d_sb->s_blocksize > PAGE_SIZE)
		goto error_unsupported;

	ret = -EROFS;
	if (sb_rdonly(root->d_sb))
		goto error_unsupported;

	/* determine the security of the on-disk cache as this governs
	 * security ID of files we create */
	ret = cachefiles_determine_cache_security(cache, root, &saved_cred);
	if (ret < 0)
		goto error_unsupported;

	/* get the cache size and blocksize */
	ret = vfs_statfs(&path, &stats);
	if (ret < 0)
		goto error_unsupported;

	ret = -ERANGE;
	if (stats.f_bsize <= 0)
		goto error_unsupported;

	ret = -EOPNOTSUPP;
	if (stats.f_bsize > PAGE_SIZE)
		goto error_unsupported;

	cache->bsize = stats.f_bsize;
	cache->bshift = 0;
	if (stats.f_bsize < PAGE_SIZE)
		cache->bshift = PAGE_SHIFT - ilog2(stats.f_bsize);

	_debug("blksize %u (shift %u)",
	       cache->bsize, cache->bshift);

	_debug("size %llu, avail %llu",
	       (unsigned long long) stats.f_blocks,
	       (unsigned long long) stats.f_bavail);

	/* set up caching limits */
	do_div(stats.f_files, 100);
	cache->fstop = stats.f_files * cache->fstop_percent;
	cache->fcull = stats.f_files * cache->fcull_percent;
	cache->frun  = stats.f_files * cache->frun_percent;

	_debug("limits {%llu,%llu,%llu} files",
	       (unsigned long long) cache->frun,
	       (unsigned long long) cache->fcull,
	       (unsigned long long) cache->fstop);

	stats.f_blocks >>= cache->bshift;
	do_div(stats.f_blocks, 100);
	cache->bstop = stats.f_blocks * cache->bstop_percent;
	cache->bcull = stats.f_blocks * cache->bcull_percent;
	cache->brun  = stats.f_blocks * cache->brun_percent;

	_debug("limits {%llu,%llu,%llu} blocks",
	       (unsigned long long) cache->brun,
	       (unsigned long long) cache->bcull,
	       (unsigned long long) cache->bstop);

	/* get the cache directory and check its type */
	cachedir = cachefiles_get_directory(cache, root, "cache", NULL);
	if (IS_ERR(cachedir)) {
		ret = PTR_ERR(cachedir);
		goto error_unsupported;
	}

	cache->store = cachedir;

	/* get the graveyard directory */
	graveyard = cachefiles_get_directory(cache, root, "graveyard", NULL);
	if (IS_ERR(graveyard)) {
		ret = PTR_ERR(graveyard);
		goto error_unsupported;
	}

	cache->graveyard = graveyard;
	cache->cache = cache_cookie;

	ret = fscache_add_cache(cache_cookie, &cachefiles_cache_ops, cache);
	if (ret < 0)
		goto error_add_cache;

	/* done */
	set_bit(CACHEFILES_READY, &cache->flags);
	dput(root);

	pr_info("File cache on %s registered\n", cache_cookie->name);

	/* check how much space the cache has */
	cachefiles_has_space(cache, 0, 0);
	cachefiles_end_secure(cache, saved_cred);
	_leave(" = 0 [%px]", cache->cache);
	return 0;

error_add_cache:
	cachefiles_put_directory(cache->graveyard);
	cache->graveyard = NULL;
error_unsupported:
	cachefiles_put_directory(cache->store);
	cache->store = NULL;
	mntput(cache->mnt);
	cache->mnt = NULL;
	dput(root);
error_open_root:
	cachefiles_end_secure(cache, saved_cred);
error_getsec:
	fscache_relinquish_cache(cache_cookie);
	cache->cache = NULL;
	pr_err("Failed to register: %d\n", ret);
	return ret;
}

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

/*
 * Withdraw volumes.
 */
static void cachefiles_withdraw_volumes(struct cachefiles_cache *cache)
{
	_enter("");

	for (;;) {
		struct cachefiles_volume *volume = NULL;

		spin_lock(&cache->object_list_lock);
		if (!list_empty(&cache->volumes)) {
			volume = list_first_entry(&cache->volumes,
						  struct cachefiles_volume, cache_link);
			list_del_init(&volume->cache_link);
		}
		spin_unlock(&cache->object_list_lock);
		if (!volume)
			break;

		cachefiles_withdraw_volume(volume);
	}

	_leave("");
}

/*
 * Sync a cache to backing disk.
 */
static void cachefiles_sync_cache(struct cachefiles_cache *cache)
{
	const struct cred *saved_cred;
	int ret;

	_enter("%s", cache->cache->name);

	/* make sure all pages pinned by operations on behalf of the netfs are
	 * written to disc */
	cachefiles_begin_secure(cache, &saved_cred);
	down_read(&cache->mnt->mnt_sb->s_umount);
	ret = sync_filesystem(cache->mnt->mnt_sb);
	up_read(&cache->mnt->mnt_sb->s_umount);
	cachefiles_end_secure(cache, saved_cred);

	if (ret == -EIO)
		cachefiles_io_error(cache,
				    "Attempt to sync backing fs superblock returned error %d",
				    ret);
}

/*
 * Withdraw cache objects.
 */
void cachefiles_withdraw_cache(struct cachefiles_cache *cache)
{
	struct fscache_cache *fscache = cache->cache;

	pr_info("File cache on %s unregistering\n", fscache->name);

	fscache_withdraw_cache(fscache);

	/* we now have to destroy all the active objects pertaining to this
	 * cache - which we do by passing them off to thread pool to be
	 * disposed of */
	// PLACEHOLDER: Withdraw objects
	fscache_wait_for_objects(fscache);

	cachefiles_withdraw_volumes(cache);
	cachefiles_sync_cache(cache);
	cache->cache = NULL;
	fscache_relinquish_cache(fscache);
}
