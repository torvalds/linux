// SPDX-License-Identifier: GPL-2.0-or-later
/* CacheFiles path walking and related routines
 *
 * Copyright (C) 2021 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */

#include <linux/fs.h>
#include <linux/namei.h>
#include "internal.h"

/*
 * Mark the backing file as being a cache file if it's not already in use.  The
 * mark tells the culling request command that it's not allowed to cull the
 * file or directory.  The caller must hold the inode lock.
 */
static bool __cachefiles_mark_inode_in_use(struct cachefiles_object *object,
					   struct dentry *dentry)
{
	struct inode *inode = d_backing_inode(dentry);
	bool can_use = false;

	if (!(inode->i_flags & S_KERNEL_FILE)) {
		inode->i_flags |= S_KERNEL_FILE;
		trace_cachefiles_mark_active(object, inode);
		can_use = true;
	} else {
		pr_notice("cachefiles: Inode already in use: %pd\n", dentry);
	}

	return can_use;
}

static bool cachefiles_mark_inode_in_use(struct cachefiles_object *object,
					 struct dentry *dentry)
{
	struct inode *inode = d_backing_inode(dentry);
	bool can_use;

	inode_lock(inode);
	can_use = __cachefiles_mark_inode_in_use(object, dentry);
	inode_unlock(inode);
	return can_use;
}

/*
 * Unmark a backing inode.  The caller must hold the inode lock.
 */
static void __cachefiles_unmark_inode_in_use(struct cachefiles_object *object,
					     struct dentry *dentry)
{
	struct inode *inode = d_backing_inode(dentry);

	inode->i_flags &= ~S_KERNEL_FILE;
	trace_cachefiles_mark_inactive(object, inode);
}

/*
 * Unmark a backing inode and tell cachefilesd that there's something that can
 * be culled.
 */
void cachefiles_unmark_inode_in_use(struct cachefiles_object *object,
				    struct file *file)
{
	struct cachefiles_cache *cache = object->volume->cache;
	struct inode *inode = file_inode(file);

	if (inode) {
		inode_lock(inode);
		__cachefiles_unmark_inode_in_use(object, file->f_path.dentry);
		inode_unlock(inode);

		if (!test_bit(CACHEFILES_OBJECT_USING_TMPFILE, &object->flags)) {
			atomic_long_add(inode->i_blocks, &cache->b_released);
			if (atomic_inc_return(&cache->f_released))
				cachefiles_state_changed(cache);
		}
	}
}

/*
 * get a subdirectory
 */
struct dentry *cachefiles_get_directory(struct cachefiles_cache *cache,
					struct dentry *dir,
					const char *dirname,
					bool *_is_new)
{
	struct dentry *subdir;
	struct path path;
	int ret;

	_enter(",,%s", dirname);

	/* search the current directory for the element name */
	inode_lock_nested(d_inode(dir), I_MUTEX_PARENT);

retry:
	ret = cachefiles_inject_read_error();
	if (ret == 0)
		subdir = lookup_one_len(dirname, dir, strlen(dirname));
	else
		subdir = ERR_PTR(ret);
	if (IS_ERR(subdir)) {
		trace_cachefiles_vfs_error(NULL, d_backing_inode(dir),
					   PTR_ERR(subdir),
					   cachefiles_trace_lookup_error);
		if (PTR_ERR(subdir) == -ENOMEM)
			goto nomem_d_alloc;
		goto lookup_error;
	}

	_debug("subdir -> %pd %s",
	       subdir, d_backing_inode(subdir) ? "positive" : "negative");

	/* we need to create the subdir if it doesn't exist yet */
	if (d_is_negative(subdir)) {
		ret = cachefiles_has_space(cache, 1, 0);
		if (ret < 0)
			goto mkdir_error;

		_debug("attempt mkdir");

		path.mnt = cache->mnt;
		path.dentry = dir;
		ret = security_path_mkdir(&path, subdir, 0700);
		if (ret < 0)
			goto mkdir_error;
		ret = cachefiles_inject_write_error();
		if (ret == 0)
			ret = vfs_mkdir(&init_user_ns, d_inode(dir), subdir, 0700);
		if (ret < 0) {
			trace_cachefiles_vfs_error(NULL, d_inode(dir), ret,
						   cachefiles_trace_mkdir_error);
			goto mkdir_error;
		}

		if (unlikely(d_unhashed(subdir))) {
			cachefiles_put_directory(subdir);
			goto retry;
		}
		ASSERT(d_backing_inode(subdir));

		_debug("mkdir -> %pd{ino=%lu}",
		       subdir, d_backing_inode(subdir)->i_ino);
		if (_is_new)
			*_is_new = true;
	}

	/* Tell rmdir() it's not allowed to delete the subdir */
	inode_lock(d_inode(subdir));
	inode_unlock(d_inode(dir));

	if (!__cachefiles_mark_inode_in_use(NULL, subdir))
		goto mark_error;

	inode_unlock(d_inode(subdir));

	/* we need to make sure the subdir is a directory */
	ASSERT(d_backing_inode(subdir));

	if (!d_can_lookup(subdir)) {
		pr_err("%s is not a directory\n", dirname);
		ret = -EIO;
		goto check_error;
	}

	ret = -EPERM;
	if (!(d_backing_inode(subdir)->i_opflags & IOP_XATTR) ||
	    !d_backing_inode(subdir)->i_op->lookup ||
	    !d_backing_inode(subdir)->i_op->mkdir ||
	    !d_backing_inode(subdir)->i_op->rename ||
	    !d_backing_inode(subdir)->i_op->rmdir ||
	    !d_backing_inode(subdir)->i_op->unlink)
		goto check_error;

	_leave(" = [%lu]", d_backing_inode(subdir)->i_ino);
	return subdir;

check_error:
	cachefiles_put_directory(subdir);
	_leave(" = %d [check]", ret);
	return ERR_PTR(ret);

mark_error:
	inode_unlock(d_inode(subdir));
	dput(subdir);
	return ERR_PTR(-EBUSY);

mkdir_error:
	inode_unlock(d_inode(dir));
	dput(subdir);
	pr_err("mkdir %s failed with error %d\n", dirname, ret);
	return ERR_PTR(ret);

lookup_error:
	inode_unlock(d_inode(dir));
	ret = PTR_ERR(subdir);
	pr_err("Lookup %s failed with error %d\n", dirname, ret);
	return ERR_PTR(ret);

nomem_d_alloc:
	inode_unlock(d_inode(dir));
	_leave(" = -ENOMEM");
	return ERR_PTR(-ENOMEM);
}

/*
 * Put a subdirectory.
 */
void cachefiles_put_directory(struct dentry *dir)
{
	if (dir) {
		inode_lock(dir->d_inode);
		__cachefiles_unmark_inode_in_use(NULL, dir);
		inode_unlock(dir->d_inode);
		dput(dir);
	}
}

/*
 * Remove a regular file from the cache.
 */
static int cachefiles_unlink(struct cachefiles_cache *cache,
			     struct cachefiles_object *object,
			     struct dentry *dir, struct dentry *dentry,
			     enum fscache_why_object_killed why)
{
	struct path path = {
		.mnt	= cache->mnt,
		.dentry	= dir,
	};
	int ret;

	trace_cachefiles_unlink(object, dentry, why);
	ret = security_path_unlink(&path, dentry);
	if (ret < 0) {
		cachefiles_io_error(cache, "Unlink security error");
		return ret;
	}

	ret = cachefiles_inject_remove_error();
	if (ret == 0) {
		ret = vfs_unlink(&init_user_ns, d_backing_inode(dir), dentry, NULL);
		if (ret == -EIO)
			cachefiles_io_error(cache, "Unlink failed");
	}
	if (ret != 0)
		trace_cachefiles_vfs_error(object, d_backing_inode(dir), ret,
					   cachefiles_trace_unlink_error);
	return ret;
}

/*
 * Delete an object representation from the cache
 * - File backed objects are unlinked
 * - Directory backed objects are stuffed into the graveyard for userspace to
 *   delete
 */
int cachefiles_bury_object(struct cachefiles_cache *cache,
			   struct cachefiles_object *object,
			   struct dentry *dir,
			   struct dentry *rep,
			   enum fscache_why_object_killed why)
{
	struct dentry *grave, *trap;
	struct path path, path_to_graveyard;
	char nbuffer[8 + 8 + 1];
	int ret;

	_enter(",'%pd','%pd'", dir, rep);

	if (rep->d_parent != dir) {
		inode_unlock(d_inode(dir));
		_leave(" = -ESTALE");
		return -ESTALE;
	}

	/* non-directories can just be unlinked */
	if (!d_is_dir(rep)) {
		dget(rep); /* Stop the dentry being negated if it's only pinned
			    * by a file struct.
			    */
		ret = cachefiles_unlink(cache, object, dir, rep, why);
		dput(rep);

		inode_unlock(d_inode(dir));
		_leave(" = %d", ret);
		return ret;
	}

	/* directories have to be moved to the graveyard */
	_debug("move stale object to graveyard");
	inode_unlock(d_inode(dir));

try_again:
	/* first step is to make up a grave dentry in the graveyard */
	sprintf(nbuffer, "%08x%08x",
		(uint32_t) ktime_get_real_seconds(),
		(uint32_t) atomic_inc_return(&cache->gravecounter));

	/* do the multiway lock magic */
	trap = lock_rename(cache->graveyard, dir);

	/* do some checks before getting the grave dentry */
	if (rep->d_parent != dir || IS_DEADDIR(d_inode(rep))) {
		/* the entry was probably culled when we dropped the parent dir
		 * lock */
		unlock_rename(cache->graveyard, dir);
		_leave(" = 0 [culled?]");
		return 0;
	}

	if (!d_can_lookup(cache->graveyard)) {
		unlock_rename(cache->graveyard, dir);
		cachefiles_io_error(cache, "Graveyard no longer a directory");
		return -EIO;
	}

	if (trap == rep) {
		unlock_rename(cache->graveyard, dir);
		cachefiles_io_error(cache, "May not make directory loop");
		return -EIO;
	}

	if (d_mountpoint(rep)) {
		unlock_rename(cache->graveyard, dir);
		cachefiles_io_error(cache, "Mountpoint in cache");
		return -EIO;
	}

	grave = lookup_one_len(nbuffer, cache->graveyard, strlen(nbuffer));
	if (IS_ERR(grave)) {
		unlock_rename(cache->graveyard, dir);
		trace_cachefiles_vfs_error(object, d_inode(cache->graveyard),
					   PTR_ERR(grave),
					   cachefiles_trace_lookup_error);

		if (PTR_ERR(grave) == -ENOMEM) {
			_leave(" = -ENOMEM");
			return -ENOMEM;
		}

		cachefiles_io_error(cache, "Lookup error %ld", PTR_ERR(grave));
		return -EIO;
	}

	if (d_is_positive(grave)) {
		unlock_rename(cache->graveyard, dir);
		dput(grave);
		grave = NULL;
		cond_resched();
		goto try_again;
	}

	if (d_mountpoint(grave)) {
		unlock_rename(cache->graveyard, dir);
		dput(grave);
		cachefiles_io_error(cache, "Mountpoint in graveyard");
		return -EIO;
	}

	/* target should not be an ancestor of source */
	if (trap == grave) {
		unlock_rename(cache->graveyard, dir);
		dput(grave);
		cachefiles_io_error(cache, "May not make directory loop");
		return -EIO;
	}

	/* attempt the rename */
	path.mnt = cache->mnt;
	path.dentry = dir;
	path_to_graveyard.mnt = cache->mnt;
	path_to_graveyard.dentry = cache->graveyard;
	ret = security_path_rename(&path, rep, &path_to_graveyard, grave, 0);
	if (ret < 0) {
		cachefiles_io_error(cache, "Rename security error %d", ret);
	} else {
		struct renamedata rd = {
			.old_mnt_userns	= &init_user_ns,
			.old_dir	= d_inode(dir),
			.old_dentry	= rep,
			.new_mnt_userns	= &init_user_ns,
			.new_dir	= d_inode(cache->graveyard),
			.new_dentry	= grave,
		};
		trace_cachefiles_rename(object, rep, grave, why);
		ret = cachefiles_inject_read_error();
		if (ret == 0)
			ret = vfs_rename(&rd);
		if (ret != 0)
			trace_cachefiles_vfs_error(object, d_inode(dir), ret,
						   cachefiles_trace_rename_error);
		if (ret != 0 && ret != -ENOMEM)
			cachefiles_io_error(cache,
					    "Rename failed with error %d", ret);
	}

	__cachefiles_unmark_inode_in_use(object, rep);
	unlock_rename(cache->graveyard, dir);
	dput(grave);
	_leave(" = 0");
	return 0;
}

/*
 * Look up an inode to be checked or culled.  Return -EBUSY if the inode is
 * marked in use.
 */
static struct dentry *cachefiles_lookup_for_cull(struct cachefiles_cache *cache,
						 struct dentry *dir,
						 char *filename)
{
	struct dentry *victim;
	int ret = -ENOENT;

	inode_lock_nested(d_inode(dir), I_MUTEX_PARENT);

	victim = lookup_one_len(filename, dir, strlen(filename));
	if (IS_ERR(victim))
		goto lookup_error;
	if (d_is_negative(victim))
		goto lookup_put;
	if (d_inode(victim)->i_flags & S_KERNEL_FILE)
		goto lookup_busy;
	return victim;

lookup_busy:
	ret = -EBUSY;
lookup_put:
	inode_unlock(d_inode(dir));
	dput(victim);
	return ERR_PTR(ret);

lookup_error:
	inode_unlock(d_inode(dir));
	ret = PTR_ERR(victim);
	if (ret == -ENOENT)
		return ERR_PTR(-ESTALE); /* Probably got retired by the netfs */

	if (ret == -EIO) {
		cachefiles_io_error(cache, "Lookup failed");
	} else if (ret != -ENOMEM) {
		pr_err("Internal error: %d\n", ret);
		ret = -EIO;
	}

	return ERR_PTR(ret);
}

/*
 * Cull an object if it's not in use
 * - called only by cache manager daemon
 */
int cachefiles_cull(struct cachefiles_cache *cache, struct dentry *dir,
		    char *filename)
{
	struct dentry *victim;
	struct inode *inode;
	int ret;

	_enter(",%pd/,%s", dir, filename);

	victim = cachefiles_lookup_for_cull(cache, dir, filename);
	if (IS_ERR(victim))
		return PTR_ERR(victim);

	/* check to see if someone is using this object */
	inode = d_inode(victim);
	inode_lock(inode);
	if (inode->i_flags & S_KERNEL_FILE) {
		ret = -EBUSY;
	} else {
		/* Stop the cache from picking it back up */
		inode->i_flags |= S_KERNEL_FILE;
		ret = 0;
	}
	inode_unlock(inode);
	if (ret < 0)
		goto error_unlock;

	ret = cachefiles_bury_object(cache, NULL, dir, victim,
				     FSCACHE_OBJECT_WAS_CULLED);
	if (ret < 0)
		goto error;

	dput(victim);
	_leave(" = 0");
	return 0;

error_unlock:
	inode_unlock(d_inode(dir));
error:
	dput(victim);
	if (ret == -ENOENT)
		return -ESTALE; /* Probably got retired by the netfs */

	if (ret != -ENOMEM) {
		pr_err("Internal error: %d\n", ret);
		ret = -EIO;
	}

	_leave(" = %d", ret);
	return ret;
}

/*
 * Find out if an object is in use or not
 * - called only by cache manager daemon
 * - returns -EBUSY or 0 to indicate whether an object is in use or not
 */
int cachefiles_check_in_use(struct cachefiles_cache *cache, struct dentry *dir,
			    char *filename)
{
	struct dentry *victim;
	int ret = 0;

	victim = cachefiles_lookup_for_cull(cache, dir, filename);
	if (IS_ERR(victim))
		return PTR_ERR(victim);

	inode_unlock(d_inode(dir));
	dput(victim);
	return ret;
}
