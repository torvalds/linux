// SPDX-License-Identifier: GPL-2.0
/*
 * FUSE passthrough to backing file.
 *
 * Copyright (c) 2023 CTERA Networks.
 */

#include "dev.h"
#include "fuse_i.h"

#include <linux/file.h>
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

	if (fb->file)
		fput(fb->file);
	put_cred(fb->cred);
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

static int fuse_backing_add_64(struct fuse_conn *fc, struct fuse_backing *fb)
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

static struct fuse_backing *fuse_backing_new(struct fuse_conn *fc, int fd)
{
	struct fuse_backing *fb;
	struct super_block *backing_sb;
	struct file *file;

	/* TODO: relax CAP_SYS_ADMIN once backing files are visible to lsof */
	if (!fc->passthrough || !capable(CAP_SYS_ADMIN))
		return ERR_PTR(-EPERM);

	CLASS(fd_raw, f)(fd);
	if (fd_empty(f))
		return ERR_PTR(-EBADF);

	file = fd_file(f);

	/* read/write/splice/mmap passthrough only relevant for regular files */
	if (!d_is_reg(file->f_path.dentry))
		return d_is_dir(file->f_path.dentry) ? ERR_PTR(-EISDIR) : ERR_PTR(-EINVAL);

	backing_sb = file_inode(file)->i_sb;
	if (backing_sb->s_stack_depth >= fc->max_stack_depth)
		return ERR_PTR(-ELOOP);

	fb = kmalloc_obj(struct fuse_backing);
	if (!fb)
		return ERR_PTR(-ENOMEM);

	fb->file = get_file(file);
	fb->cred = get_current_cred();
	refcount_set(&fb->count, 1);

	return fb;
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
	if (fc->backing_id_64) {
		rhashtable_free_and_destroy(&fc->backing_64_ht, fuse_backing_rht_free, NULL);
	} else {
		idr_for_each(&fc->backing_files_map, fuse_backing_idr_free, NULL);
		idr_destroy(&fc->backing_files_map);
	}
}

void fuse_backing_files_init_64(struct fuse_conn *fc)
{
	rhashtable_init(&fc->backing_64_ht, &fuse_backing_prm);
}
