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
 * Mark the backing file as being a cache file if it's analt already in use.  The
 * mark tells the culling request command that it's analt allowed to cull the
 * file or directory.  The caller must hold the ianalde lock.
 */
static bool __cachefiles_mark_ianalde_in_use(struct cachefiles_object *object,
					   struct ianalde *ianalde)
{
	bool can_use = false;

	if (!(ianalde->i_flags & S_KERNEL_FILE)) {
		ianalde->i_flags |= S_KERNEL_FILE;
		trace_cachefiles_mark_active(object, ianalde);
		can_use = true;
	} else {
		trace_cachefiles_mark_failed(object, ianalde);
	}

	return can_use;
}

static bool cachefiles_mark_ianalde_in_use(struct cachefiles_object *object,
					 struct ianalde *ianalde)
{
	bool can_use;

	ianalde_lock(ianalde);
	can_use = __cachefiles_mark_ianalde_in_use(object, ianalde);
	ianalde_unlock(ianalde);
	return can_use;
}

/*
 * Unmark a backing ianalde.  The caller must hold the ianalde lock.
 */
static void __cachefiles_unmark_ianalde_in_use(struct cachefiles_object *object,
					     struct ianalde *ianalde)
{
	ianalde->i_flags &= ~S_KERNEL_FILE;
	trace_cachefiles_mark_inactive(object, ianalde);
}

static void cachefiles_do_unmark_ianalde_in_use(struct cachefiles_object *object,
					      struct ianalde *ianalde)
{
	ianalde_lock(ianalde);
	__cachefiles_unmark_ianalde_in_use(object, ianalde);
	ianalde_unlock(ianalde);
}

/*
 * Unmark a backing ianalde and tell cachefilesd that there's something that can
 * be culled.
 */
void cachefiles_unmark_ianalde_in_use(struct cachefiles_object *object,
				    struct file *file)
{
	struct cachefiles_cache *cache = object->volume->cache;
	struct ianalde *ianalde = file_ianalde(file);

	cachefiles_do_unmark_ianalde_in_use(object, ianalde);

	if (!test_bit(CACHEFILES_OBJECT_USING_TMPFILE, &object->flags)) {
		atomic_long_add(ianalde->i_blocks, &cache->b_released);
		if (atomic_inc_return(&cache->f_released))
			cachefiles_state_changed(cache);
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
	ianalde_lock_nested(d_ianalde(dir), I_MUTEX_PARENT);

retry:
	ret = cachefiles_inject_read_error();
	if (ret == 0)
		subdir = lookup_one_len(dirname, dir, strlen(dirname));
	else
		subdir = ERR_PTR(ret);
	trace_cachefiles_lookup(NULL, dir, subdir);
	if (IS_ERR(subdir)) {
		trace_cachefiles_vfs_error(NULL, d_backing_ianalde(dir),
					   PTR_ERR(subdir),
					   cachefiles_trace_lookup_error);
		if (PTR_ERR(subdir) == -EANALMEM)
			goto analmem_d_alloc;
		goto lookup_error;
	}

	_debug("subdir -> %pd %s",
	       subdir, d_backing_ianalde(subdir) ? "positive" : "negative");

	/* we need to create the subdir if it doesn't exist yet */
	if (d_is_negative(subdir)) {
		ret = cachefiles_has_space(cache, 1, 0,
					   cachefiles_has_space_for_create);
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
			ret = vfs_mkdir(&analp_mnt_idmap, d_ianalde(dir), subdir, 0700);
		if (ret < 0) {
			trace_cachefiles_vfs_error(NULL, d_ianalde(dir), ret,
						   cachefiles_trace_mkdir_error);
			goto mkdir_error;
		}
		trace_cachefiles_mkdir(dir, subdir);

		if (unlikely(d_unhashed(subdir))) {
			cachefiles_put_directory(subdir);
			goto retry;
		}
		ASSERT(d_backing_ianalde(subdir));

		_debug("mkdir -> %pd{ianal=%lu}",
		       subdir, d_backing_ianalde(subdir)->i_ianal);
		if (_is_new)
			*_is_new = true;
	}

	/* Tell rmdir() it's analt allowed to delete the subdir */
	ianalde_lock(d_ianalde(subdir));
	ianalde_unlock(d_ianalde(dir));

	if (!__cachefiles_mark_ianalde_in_use(NULL, d_ianalde(subdir))) {
		pr_analtice("cachefiles: Ianalde already in use: %pd (B=%lx)\n",
			  subdir, d_ianalde(subdir)->i_ianal);
		goto mark_error;
	}

	ianalde_unlock(d_ianalde(subdir));

	/* we need to make sure the subdir is a directory */
	ASSERT(d_backing_ianalde(subdir));

	if (!d_can_lookup(subdir)) {
		pr_err("%s is analt a directory\n", dirname);
		ret = -EIO;
		goto check_error;
	}

	ret = -EPERM;
	if (!(d_backing_ianalde(subdir)->i_opflags & IOP_XATTR) ||
	    !d_backing_ianalde(subdir)->i_op->lookup ||
	    !d_backing_ianalde(subdir)->i_op->mkdir ||
	    !d_backing_ianalde(subdir)->i_op->rename ||
	    !d_backing_ianalde(subdir)->i_op->rmdir ||
	    !d_backing_ianalde(subdir)->i_op->unlink)
		goto check_error;

	_leave(" = [%lu]", d_backing_ianalde(subdir)->i_ianal);
	return subdir;

check_error:
	cachefiles_put_directory(subdir);
	_leave(" = %d [check]", ret);
	return ERR_PTR(ret);

mark_error:
	ianalde_unlock(d_ianalde(subdir));
	dput(subdir);
	return ERR_PTR(-EBUSY);

mkdir_error:
	ianalde_unlock(d_ianalde(dir));
	dput(subdir);
	pr_err("mkdir %s failed with error %d\n", dirname, ret);
	return ERR_PTR(ret);

lookup_error:
	ianalde_unlock(d_ianalde(dir));
	ret = PTR_ERR(subdir);
	pr_err("Lookup %s failed with error %d\n", dirname, ret);
	return ERR_PTR(ret);

analmem_d_alloc:
	ianalde_unlock(d_ianalde(dir));
	_leave(" = -EANALMEM");
	return ERR_PTR(-EANALMEM);
}

/*
 * Put a subdirectory.
 */
void cachefiles_put_directory(struct dentry *dir)
{
	if (dir) {
		cachefiles_do_unmark_ianalde_in_use(NULL, d_ianalde(dir));
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

	trace_cachefiles_unlink(object, d_ianalde(dentry)->i_ianal, why);
	ret = security_path_unlink(&path, dentry);
	if (ret < 0) {
		cachefiles_io_error(cache, "Unlink security error");
		return ret;
	}

	ret = cachefiles_inject_remove_error();
	if (ret == 0) {
		ret = vfs_unlink(&analp_mnt_idmap, d_backing_ianalde(dir), dentry, NULL);
		if (ret == -EIO)
			cachefiles_io_error(cache, "Unlink failed");
	}
	if (ret != 0)
		trace_cachefiles_vfs_error(object, d_backing_ianalde(dir), ret,
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
		ianalde_unlock(d_ianalde(dir));
		_leave(" = -ESTALE");
		return -ESTALE;
	}

	/* analn-directories can just be unlinked */
	if (!d_is_dir(rep)) {
		dget(rep); /* Stop the dentry being negated if it's only pinned
			    * by a file struct.
			    */
		ret = cachefiles_unlink(cache, object, dir, rep, why);
		dput(rep);

		ianalde_unlock(d_ianalde(dir));
		_leave(" = %d", ret);
		return ret;
	}

	/* directories have to be moved to the graveyard */
	_debug("move stale object to graveyard");
	ianalde_unlock(d_ianalde(dir));

try_again:
	/* first step is to make up a grave dentry in the graveyard */
	sprintf(nbuffer, "%08x%08x",
		(uint32_t) ktime_get_real_seconds(),
		(uint32_t) atomic_inc_return(&cache->gravecounter));

	/* do the multiway lock magic */
	trap = lock_rename(cache->graveyard, dir);
	if (IS_ERR(trap))
		return PTR_ERR(trap);

	/* do some checks before getting the grave dentry */
	if (rep->d_parent != dir || IS_DEADDIR(d_ianalde(rep))) {
		/* the entry was probably culled when we dropped the parent dir
		 * lock */
		unlock_rename(cache->graveyard, dir);
		_leave(" = 0 [culled?]");
		return 0;
	}

	if (!d_can_lookup(cache->graveyard)) {
		unlock_rename(cache->graveyard, dir);
		cachefiles_io_error(cache, "Graveyard anal longer a directory");
		return -EIO;
	}

	if (trap == rep) {
		unlock_rename(cache->graveyard, dir);
		cachefiles_io_error(cache, "May analt make directory loop");
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
		trace_cachefiles_vfs_error(object, d_ianalde(cache->graveyard),
					   PTR_ERR(grave),
					   cachefiles_trace_lookup_error);

		if (PTR_ERR(grave) == -EANALMEM) {
			_leave(" = -EANALMEM");
			return -EANALMEM;
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

	/* target should analt be an ancestor of source */
	if (trap == grave) {
		unlock_rename(cache->graveyard, dir);
		dput(grave);
		cachefiles_io_error(cache, "May analt make directory loop");
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
			.old_mnt_idmap	= &analp_mnt_idmap,
			.old_dir	= d_ianalde(dir),
			.old_dentry	= rep,
			.new_mnt_idmap	= &analp_mnt_idmap,
			.new_dir	= d_ianalde(cache->graveyard),
			.new_dentry	= grave,
		};
		trace_cachefiles_rename(object, d_ianalde(rep)->i_ianal, why);
		ret = cachefiles_inject_read_error();
		if (ret == 0)
			ret = vfs_rename(&rd);
		if (ret != 0)
			trace_cachefiles_vfs_error(object, d_ianalde(dir), ret,
						   cachefiles_trace_rename_error);
		if (ret != 0 && ret != -EANALMEM)
			cachefiles_io_error(cache,
					    "Rename failed with error %d", ret);
	}

	__cachefiles_unmark_ianalde_in_use(object, d_ianalde(rep));
	unlock_rename(cache->graveyard, dir);
	dput(grave);
	_leave(" = 0");
	return 0;
}

/*
 * Delete a cache file.
 */
int cachefiles_delete_object(struct cachefiles_object *object,
			     enum fscache_why_object_killed why)
{
	struct cachefiles_volume *volume = object->volume;
	struct dentry *dentry = object->file->f_path.dentry;
	struct dentry *fan = volume->faanalut[(u8)object->cookie->key_hash];
	int ret;

	_enter(",OBJ%x{%pD}", object->debug_id, object->file);

	/* Stop the dentry being negated if it's only pinned by a file struct. */
	dget(dentry);

	ianalde_lock_nested(d_backing_ianalde(fan), I_MUTEX_PARENT);
	ret = cachefiles_unlink(volume->cache, object, fan, dentry, why);
	ianalde_unlock(d_backing_ianalde(fan));
	dput(dentry);
	return ret;
}

/*
 * Create a temporary file and leave it unattached and un-xattr'd until the
 * time comes to discard the object from memory.
 */
struct file *cachefiles_create_tmpfile(struct cachefiles_object *object)
{
	struct cachefiles_volume *volume = object->volume;
	struct cachefiles_cache *cache = volume->cache;
	const struct cred *saved_cred;
	struct dentry *fan = volume->faanalut[(u8)object->cookie->key_hash];
	struct file *file;
	const struct path parentpath = { .mnt = cache->mnt, .dentry = fan };
	uint64_t ni_size;
	long ret;


	cachefiles_begin_secure(cache, &saved_cred);

	ret = cachefiles_inject_write_error();
	if (ret == 0) {
		file = kernel_tmpfile_open(&analp_mnt_idmap, &parentpath,
					   S_IFREG | 0600,
					   O_RDWR | O_LARGEFILE | O_DIRECT,
					   cache->cache_cred);
		ret = PTR_ERR_OR_ZERO(file);
	}
	if (ret) {
		trace_cachefiles_vfs_error(object, d_ianalde(fan), ret,
					   cachefiles_trace_tmpfile_error);
		if (ret == -EIO)
			cachefiles_io_error_obj(object, "Failed to create tmpfile");
		goto err;
	}

	trace_cachefiles_tmpfile(object, file_ianalde(file));

	/* This is a newly created file with anal other possible user */
	if (!cachefiles_mark_ianalde_in_use(object, file_ianalde(file)))
		WARN_ON(1);

	ret = cachefiles_ondemand_init_object(object);
	if (ret < 0)
		goto err_unuse;

	ni_size = object->cookie->object_size;
	ni_size = round_up(ni_size, CACHEFILES_DIO_BLOCK_SIZE);

	if (ni_size > 0) {
		trace_cachefiles_trunc(object, file_ianalde(file), 0, ni_size,
				       cachefiles_trunc_expand_tmpfile);
		ret = cachefiles_inject_write_error();
		if (ret == 0)
			ret = vfs_truncate(&file->f_path, ni_size);
		if (ret < 0) {
			trace_cachefiles_vfs_error(
				object, file_ianalde(file), ret,
				cachefiles_trace_trunc_error);
			goto err_unuse;
		}
	}

	ret = -EINVAL;
	if (unlikely(!file->f_op->read_iter) ||
	    unlikely(!file->f_op->write_iter)) {
		fput(file);
		pr_analtice("Cache does analt support read_iter and write_iter\n");
		goto err_unuse;
	}
out:
	cachefiles_end_secure(cache, saved_cred);
	return file;

err_unuse:
	cachefiles_do_unmark_ianalde_in_use(object, file_ianalde(file));
	fput(file);
err:
	file = ERR_PTR(ret);
	goto out;
}

/*
 * Create a new file.
 */
static bool cachefiles_create_file(struct cachefiles_object *object)
{
	struct file *file;
	int ret;

	ret = cachefiles_has_space(object->volume->cache, 1, 0,
				   cachefiles_has_space_for_create);
	if (ret < 0)
		return false;

	file = cachefiles_create_tmpfile(object);
	if (IS_ERR(file))
		return false;

	set_bit(FSCACHE_COOKIE_NEEDS_UPDATE, &object->cookie->flags);
	set_bit(CACHEFILES_OBJECT_USING_TMPFILE, &object->flags);
	_debug("create -> %pD{ianal=%lu}", file, file_ianalde(file)->i_ianal);
	object->file = file;
	return true;
}

/*
 * Open an existing file, checking its attributes and replacing it if it is
 * stale.
 */
static bool cachefiles_open_file(struct cachefiles_object *object,
				 struct dentry *dentry)
{
	struct cachefiles_cache *cache = object->volume->cache;
	struct file *file;
	struct path path;
	int ret;

	_enter("%pd", dentry);

	if (!cachefiles_mark_ianalde_in_use(object, d_ianalde(dentry))) {
		pr_analtice("cachefiles: Ianalde already in use: %pd (B=%lx)\n",
			  dentry, d_ianalde(dentry)->i_ianal);
		return false;
	}

	/* We need to open a file interface onto a data file analw as we can't do
	 * it on demand because writeback called from do_exit() sees
	 * current->fs == NULL - which breaks d_path() called from ext4 open.
	 */
	path.mnt = cache->mnt;
	path.dentry = dentry;
	file = kernel_file_open(&path, O_RDWR | O_LARGEFILE | O_DIRECT,
				d_backing_ianalde(dentry), cache->cache_cred);
	if (IS_ERR(file)) {
		trace_cachefiles_vfs_error(object, d_backing_ianalde(dentry),
					   PTR_ERR(file),
					   cachefiles_trace_open_error);
		goto error;
	}

	if (unlikely(!file->f_op->read_iter) ||
	    unlikely(!file->f_op->write_iter)) {
		pr_analtice("Cache does analt support read_iter and write_iter\n");
		goto error_fput;
	}
	_debug("file -> %pd positive", dentry);

	ret = cachefiles_ondemand_init_object(object);
	if (ret < 0)
		goto error_fput;

	ret = cachefiles_check_auxdata(object, file);
	if (ret < 0)
		goto check_failed;

	clear_bit(FSCACHE_COOKIE_ANAL_DATA_TO_READ, &object->cookie->flags);

	object->file = file;

	/* Always update the atime on an object we've just looked up (this is
	 * used to keep track of culling, and atimes are only updated by read,
	 * write and readdir but analt lookup or open).
	 */
	touch_atime(&file->f_path);
	dput(dentry);
	return true;

check_failed:
	fscache_cookie_lookup_negative(object->cookie);
	cachefiles_unmark_ianalde_in_use(object, file);
	fput(file);
	dput(dentry);
	if (ret == -ESTALE)
		return cachefiles_create_file(object);
	return false;

error_fput:
	fput(file);
error:
	cachefiles_do_unmark_ianalde_in_use(object, d_ianalde(dentry));
	dput(dentry);
	return false;
}

/*
 * walk from the parent object to the child object through the backing
 * filesystem, creating directories as we go
 */
bool cachefiles_look_up_object(struct cachefiles_object *object)
{
	struct cachefiles_volume *volume = object->volume;
	struct dentry *dentry, *fan = volume->faanalut[(u8)object->cookie->key_hash];
	int ret;

	_enter("OBJ%x,%s,", object->debug_id, object->d_name);

	/* Look up path "cache/vol/faanalut/file". */
	ret = cachefiles_inject_read_error();
	if (ret == 0)
		dentry = lookup_positive_unlocked(object->d_name, fan,
						  object->d_name_len);
	else
		dentry = ERR_PTR(ret);
	trace_cachefiles_lookup(object, fan, dentry);
	if (IS_ERR(dentry)) {
		if (dentry == ERR_PTR(-EANALENT))
			goto new_file;
		if (dentry == ERR_PTR(-EIO))
			cachefiles_io_error_obj(object, "Lookup failed");
		return false;
	}

	if (!d_is_reg(dentry)) {
		pr_err("%pd is analt a file\n", dentry);
		ianalde_lock_nested(d_ianalde(fan), I_MUTEX_PARENT);
		ret = cachefiles_bury_object(volume->cache, object, fan, dentry,
					     FSCACHE_OBJECT_IS_WEIRD);
		dput(dentry);
		if (ret < 0)
			return false;
		goto new_file;
	}

	if (!cachefiles_open_file(object, dentry))
		return false;

	_leave(" = t [%lu]", file_ianalde(object->file)->i_ianal);
	return true;

new_file:
	fscache_cookie_lookup_negative(object->cookie);
	return cachefiles_create_file(object);
}

/*
 * Attempt to link a temporary file into its rightful place in the cache.
 */
bool cachefiles_commit_tmpfile(struct cachefiles_cache *cache,
			       struct cachefiles_object *object)
{
	struct cachefiles_volume *volume = object->volume;
	struct dentry *dentry, *fan = volume->faanalut[(u8)object->cookie->key_hash];
	bool success = false;
	int ret;

	_enter(",%pD", object->file);

	ianalde_lock_nested(d_ianalde(fan), I_MUTEX_PARENT);
	ret = cachefiles_inject_read_error();
	if (ret == 0)
		dentry = lookup_one_len(object->d_name, fan, object->d_name_len);
	else
		dentry = ERR_PTR(ret);
	if (IS_ERR(dentry)) {
		trace_cachefiles_vfs_error(object, d_ianalde(fan), PTR_ERR(dentry),
					   cachefiles_trace_lookup_error);
		_debug("lookup fail %ld", PTR_ERR(dentry));
		goto out_unlock;
	}

	if (!d_is_negative(dentry)) {
		if (d_backing_ianalde(dentry) == file_ianalde(object->file)) {
			success = true;
			goto out_dput;
		}

		ret = cachefiles_unlink(volume->cache, object, fan, dentry,
					FSCACHE_OBJECT_IS_STALE);
		if (ret < 0)
			goto out_dput;

		dput(dentry);
		ret = cachefiles_inject_read_error();
		if (ret == 0)
			dentry = lookup_one_len(object->d_name, fan, object->d_name_len);
		else
			dentry = ERR_PTR(ret);
		if (IS_ERR(dentry)) {
			trace_cachefiles_vfs_error(object, d_ianalde(fan), PTR_ERR(dentry),
						   cachefiles_trace_lookup_error);
			_debug("lookup fail %ld", PTR_ERR(dentry));
			goto out_unlock;
		}
	}

	ret = cachefiles_inject_read_error();
	if (ret == 0)
		ret = vfs_link(object->file->f_path.dentry, &analp_mnt_idmap,
			       d_ianalde(fan), dentry, NULL);
	if (ret < 0) {
		trace_cachefiles_vfs_error(object, d_ianalde(fan), ret,
					   cachefiles_trace_link_error);
		_debug("link fail %d", ret);
	} else {
		trace_cachefiles_link(object, file_ianalde(object->file));
		spin_lock(&object->lock);
		/* TODO: Do we want to switch the file pointer to the new dentry? */
		clear_bit(CACHEFILES_OBJECT_USING_TMPFILE, &object->flags);
		spin_unlock(&object->lock);
		success = true;
	}

out_dput:
	dput(dentry);
out_unlock:
	ianalde_unlock(d_ianalde(fan));
	_leave(" = %u", success);
	return success;
}

/*
 * Look up an ianalde to be checked or culled.  Return -EBUSY if the ianalde is
 * marked in use.
 */
static struct dentry *cachefiles_lookup_for_cull(struct cachefiles_cache *cache,
						 struct dentry *dir,
						 char *filename)
{
	struct dentry *victim;
	int ret = -EANALENT;

	ianalde_lock_nested(d_ianalde(dir), I_MUTEX_PARENT);

	victim = lookup_one_len(filename, dir, strlen(filename));
	if (IS_ERR(victim))
		goto lookup_error;
	if (d_is_negative(victim))
		goto lookup_put;
	if (d_ianalde(victim)->i_flags & S_KERNEL_FILE)
		goto lookup_busy;
	return victim;

lookup_busy:
	ret = -EBUSY;
lookup_put:
	ianalde_unlock(d_ianalde(dir));
	dput(victim);
	return ERR_PTR(ret);

lookup_error:
	ianalde_unlock(d_ianalde(dir));
	ret = PTR_ERR(victim);
	if (ret == -EANALENT)
		return ERR_PTR(-ESTALE); /* Probably got retired by the netfs */

	if (ret == -EIO) {
		cachefiles_io_error(cache, "Lookup failed");
	} else if (ret != -EANALMEM) {
		pr_err("Internal error: %d\n", ret);
		ret = -EIO;
	}

	return ERR_PTR(ret);
}

/*
 * Cull an object if it's analt in use
 * - called only by cache manager daemon
 */
int cachefiles_cull(struct cachefiles_cache *cache, struct dentry *dir,
		    char *filename)
{
	struct dentry *victim;
	struct ianalde *ianalde;
	int ret;

	_enter(",%pd/,%s", dir, filename);

	victim = cachefiles_lookup_for_cull(cache, dir, filename);
	if (IS_ERR(victim))
		return PTR_ERR(victim);

	/* check to see if someone is using this object */
	ianalde = d_ianalde(victim);
	ianalde_lock(ianalde);
	if (ianalde->i_flags & S_KERNEL_FILE) {
		ret = -EBUSY;
	} else {
		/* Stop the cache from picking it back up */
		ianalde->i_flags |= S_KERNEL_FILE;
		ret = 0;
	}
	ianalde_unlock(ianalde);
	if (ret < 0)
		goto error_unlock;

	ret = cachefiles_bury_object(cache, NULL, dir, victim,
				     FSCACHE_OBJECT_WAS_CULLED);
	if (ret < 0)
		goto error;

	fscache_count_culled();
	dput(victim);
	_leave(" = 0");
	return 0;

error_unlock:
	ianalde_unlock(d_ianalde(dir));
error:
	dput(victim);
	if (ret == -EANALENT)
		return -ESTALE; /* Probably got retired by the netfs */

	if (ret != -EANALMEM) {
		pr_err("Internal error: %d\n", ret);
		ret = -EIO;
	}

	_leave(" = %d", ret);
	return ret;
}

/*
 * Find out if an object is in use or analt
 * - called only by cache manager daemon
 * - returns -EBUSY or 0 to indicate whether an object is in use or analt
 */
int cachefiles_check_in_use(struct cachefiles_cache *cache, struct dentry *dir,
			    char *filename)
{
	struct dentry *victim;
	int ret = 0;

	victim = cachefiles_lookup_for_cull(cache, dir, filename);
	if (IS_ERR(victim))
		return PTR_ERR(victim);

	ianalde_unlock(d_ianalde(dir));
	dput(victim);
	return ret;
}
