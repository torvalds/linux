// SPDX-License-Identifier: GPL-2.0
/*
 * FUSE passthrough to backing file.
 *
 * Copyright (c) 2023 CTERA Networks.
 */

#include "dev.h"
#include "fuse_i.h"

#include <linux/file.h>
#include <linux/dax.h>
#include <linux/rhashtable.h>

static struct fuse_backing *fuse_backing_get(struct fuse_backing *fb)
{
	if (fb && refcount_inc_not_zero(&fb->count))
		return fb;
	return NULL;
}

static void fuse_backing_free(struct fuse_backing *fb)
{
	pr_debug("%s: fb=0x%p\n", __func__, fb);

	switch (fb->type) {
	case FUSE_BACKING_PATH:
		path_put(&fb->path);
		put_cred(fb->cred);
		break;

	case FUSE_BACKING_DAXDEV:
		fs_put_dax(fb->dax_dev, fb);
		break;

	case FUSE_BACKING_EXTMAP:
		fuse_ext_map_destroy(fb);
		break;
	}
	kfree_rcu(fb, rcu);
}

void fuse_backing_put(struct fuse_backing *fb)
{
	if (fb && refcount_dec_and_test(&fb->count))
		fuse_backing_free(fb);
}

void fuse_backing_files_init(struct fuse_conn *fc)
{
	idr_init(&fc->backing_files_map);
}

static int fuse_backing_id_alloc(struct fuse_conn *fc, struct fuse_backing *fb)
{
	int id;

	idr_preload(GFP_KERNEL);
	spin_lock(&fc->lock);
	/* FIXME: xarray might be space inefficient */
	id = idr_alloc_cyclic(&fc->backing_files_map, fb, 1, 0, GFP_ATOMIC);
	spin_unlock(&fc->lock);
	idr_preload_end();
	if (id > 0)
		fb->backing_id = id;

	WARN_ON_ONCE(id == 0);
	return id;
}

static struct fuse_backing *fuse_backing_id_remove(struct fuse_conn *fc,
						   int id)
{
	struct fuse_backing *fb;

	spin_lock(&fc->lock);
	fb = idr_remove(&fc->backing_files_map, id);
	spin_unlock(&fc->lock);
	if (fb)
		fb->backing_id = 0;

	return fb;
}

static const struct rhashtable_params fuse_backing_prm = {
	.head_offset = offsetof(struct fuse_backing, hash_node),
	.key_offset = offsetof(struct fuse_backing, backing_id),
	.key_len = sizeof_field(struct fuse_backing, backing_id),
};

int fuse_backing_add_64(struct fuse_conn *fc, struct fuse_backing *fb)
{
	return rhashtable_lookup_insert_fast(&fc->backing_64_ht, &fb->hash_node, fuse_backing_prm);
}

int fuse_backing_close_64(struct fuse_conn *fc, u64 backing_id)
{
	struct fuse_backing *fb;
	int err;

	if (!fc->backing_id_64)
		return -EINVAL;

	scoped_guard(spinlock, &fc->lock) {
		fb = rhashtable_lookup_fast(&fc->backing_64_ht, &backing_id, fuse_backing_prm);
		if (!fb)
			return -ENOENT;

		err = rhashtable_remove_fast(&fc->backing_64_ht, &fb->hash_node, fuse_backing_prm);
		WARN_ON(err);
	}
	fb->backing_id = 0;
	fuse_backing_put(fb);

	return 0;
}

static int fuse_dax_notify_failure(struct dax_device *daxdev, u64 offset, u64 len, int mf_flags)
{
	struct fuse_backing *fb;

	guard(rcu)();

	fb = dax_holder(daxdev);
	if (fb)
		fb->dax_error = true;

	return 0;
}

static const struct dax_holder_operations fuse_dax_holder_ops = {
	.notify_failure		= fuse_dax_notify_failure,
};

static int fuse_backing_open_file(struct fuse_conn *fc, struct fuse_backing *fb, struct file *file)
{
	struct inode *inode = file_inode(file);
	struct dax_device *daxdev;
	int err;

	switch (inode->i_mode & S_IFMT) {
	case S_IFREG:
		/* TODO: relax CAP_SYS_ADMIN once backing files are visible to lsof */
		if (!fc->passthrough || !capable(CAP_SYS_ADMIN))
			return -EPERM;

		if (inode->i_sb->s_stack_depth >= fc->max_stack_depth)
			return -ELOOP;

		fb->type = FUSE_BACKING_PATH;
		fb->path = file->f_path;
		path_get(&fb->path);
		fb->cred = get_current_cred();
		return 0;

	case S_IFCHR:
		if (!fc->passthrough || !fc->backing_id_64)
			return -EINVAL;

		if (!IS_REACHABLE(CONFIG_DEV_DAX_FSDEV))
			return -EINVAL;

		daxdev = fsdev_dax_from_file(file);
		if (!daxdev)
			return -EINVAL;

		err = -EPERM;
		if (capable(CAP_SYS_RAWIO)) {
			err = fs_dax_get(daxdev, fb, &fuse_dax_holder_ops);
			if (!err) {
				fb->type = FUSE_BACKING_DAXDEV;
				fb->dax_dev = daxdev;
			}
		}
		put_dax(daxdev);
		return err;

	case S_IFDIR:
		return -EISDIR;

	default:
		return -EINVAL;
	}
}

static struct fuse_backing *fuse_backing_new(struct fuse_conn *fc, int fd)
{
	struct fuse_backing *fb __free(kfree) = kzalloc_obj(*fb);
	int err;

	if (!fb)
		return ERR_PTR(-ENOMEM);

	CLASS(fd_raw, f)(fd);
	if (fd_empty(f))
		return ERR_PTR(-EBADF);

	err = fuse_backing_open_file(fc, fb, fd_file(f));
	if (err)
		return ERR_PTR(err);

	refcount_set(&fb->count, 1);

	return_ptr(fb);
}

int fuse_backing_open_64(struct fuse_conn *fc, struct fuse_backing_create_in *map)
{
	struct fuse_backing *fb;
	int res;

	if (map->padding || map->spare[0] || map->spare[1] || !map->backing_id)
		return -EINVAL;

	if (!fc->backing_id_64)
		return -EINVAL;

	fb = fuse_backing_new(fc, map->fd);
	if (IS_ERR(fb))
		return PTR_ERR(fb);

	fb->backing_id = map->backing_id;
	res = fuse_backing_add_64(fc, fb);
	if (res < 0)
		fuse_backing_free(fb);

	return res;
}

int fuse_backing_open(struct fuse_conn *fc, struct fuse_backing_map *map)
{
	struct fuse_backing *fb = NULL;
	int res;

	pr_debug("%s: fd=%d flags=0x%x\n", __func__, map->fd, map->flags);

	res = -EINVAL;
	if (map->flags || map->padding)
		goto out;

	if (fc->backing_id_64)
		goto out;

	fb = fuse_backing_new(fc, map->fd);
	res = PTR_ERR(fb);
	if (!IS_ERR(fb)) {
		res = fuse_backing_id_alloc(fc, fb);
		if (res < 0) {
			fuse_backing_free(fb);
			fb = NULL;
		}
	}
out:
	pr_debug("%s: fb=0x%p, ret=%i\n", __func__, fb, res);

	return res;
}

int fuse_backing_close(struct fuse_conn *fc, int backing_id)
{
	struct fuse_backing *fb = NULL;
	int err;

	pr_debug("%s: backing_id=%d\n", __func__, backing_id);

	if (fc->backing_id_64)
		return -EINVAL;

	/* TODO: relax CAP_SYS_ADMIN once backing files are visible to lsof */
	err = -EPERM;
	if (!fc->passthrough || !capable(CAP_SYS_ADMIN))
		goto out;

	err = -EINVAL;
	if (backing_id <= 0)
		goto out;

	err = -ENOENT;
	fb = fuse_backing_id_remove(fc, backing_id);
	if (!fb)
		goto out;

	fuse_backing_put(fb);
	err = 0;
out:
	pr_debug("%s: fb=0x%p, err=%i\n", __func__, fb, err);

	return err;
}

bool fuse_backing_is_dax(struct fuse_backing *fb)
{
	switch (fb->type) {
	case FUSE_BACKING_PATH:
		return false;
	case FUSE_BACKING_DAXDEV:
		return true;
	case FUSE_BACKING_EXTMAP:
		return fuse_ext_map_is_dax(fb);
	default:
		WARN_ON(1);
		return false;
	}
}

struct fuse_backing *fuse_backing_lookup(struct fuse_conn *fc, u64 backing_id)
{
	struct fuse_backing *fb;

	guard(rcu)();
	if (!fc->backing_id_64)
		fb = idr_find(&fc->backing_files_map, backing_id);
	else
		fb = rhashtable_lookup(&fc->backing_64_ht, &backing_id, fuse_backing_prm);

	return fuse_backing_get(fb);
}

static void fuse_backing_check_free(struct fuse_backing *fb)
{
	WARN_ON_ONCE(refcount_read(&fb->count) != 1);
	fuse_backing_free(fb);
}

static int fuse_backing_idr_free(int id, void *p, void *data)
{
	fuse_backing_check_free(p);
	return 0;
}

static void fuse_backing_rht_free(void *p, void *data)
{
	fuse_backing_check_free(p);
}

void fuse_backing_files_free(struct fuse_conn *fc)
{
	struct rhashtable_iter iter;
	struct fuse_backing *fb;

	if (!fc->backing_id_64) {
		idr_for_each(&fc->backing_files_map, fuse_backing_idr_free, NULL);
		idr_destroy(&fc->backing_files_map);
		return;
	}

	/*
	 * extents are referencing other backings, put these refs before
	 * destroying the backings themselves
	 */
	rhashtable_walk_enter(&fc->backing_64_ht, &iter);
	rhashtable_walk_start(&iter);
	while ((fb = rhashtable_walk_next(&iter))) {
		if (IS_ERR(fb)) {
			if (PTR_ERR(fb) == -EAGAIN)
				continue;
			break;
		}
		if (fb->type == FUSE_BACKING_EXTMAP && fb->extents) {
			rhashtable_walk_stop(&iter);
			fuse_ext_map_destroy(fb);
			rhashtable_walk_start(&iter);
		}
	}
	rhashtable_walk_stop(&iter);
	rhashtable_walk_exit(&iter);

	rhashtable_free_and_destroy(&fc->backing_64_ht, fuse_backing_rht_free, NULL);
}

void fuse_backing_files_init_64(struct fuse_conn *fc)
{
	rhashtable_init(&fc->backing_64_ht, &fuse_backing_prm);
}
