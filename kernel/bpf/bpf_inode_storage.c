// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2019 Facebook
 * Copyright 2020 Google LLC.
 */

#include <linux/rculist.h>
#include <linux/list.h>
#include <linux/hash.h>
#include <linux/types.h>
#include <linux/spinlock.h>
#include <linux/bpf.h>
#include <linux/bpf_local_storage.h>
#include <net/sock.h>
#include <uapi/linux/sock_diag.h>
#include <uapi/linux/btf.h>
#include <linux/bpf_lsm.h>
#include <linux/btf_ids.h>
#include <linux/fdtable.h>
#include <linux/rcupdate_trace.h>

DEFINE_BPF_STORAGE_CACHE(inode_cache);

static struct bpf_local_storage __rcu **
inode_storage_ptr(void *owner)
{
	struct inode *inode = owner;
	struct bpf_storage_blob *bsb;

	bsb = bpf_inode(inode);
	if (!bsb)
		return NULL;
	return &bsb->storage;
}

static struct bpf_local_storage_data *inode_storage_lookup(struct inode *inode,
							   struct bpf_map *map,
							   bool cacheit_lockit)
{
	struct bpf_local_storage *inode_storage;
	struct bpf_local_storage_map *smap;
	struct bpf_storage_blob *bsb;

	bsb = bpf_inode(inode);
	if (!bsb)
		return NULL;

	inode_storage =
		rcu_dereference_check(bsb->storage, bpf_rcu_lock_held());
	if (!inode_storage)
		return NULL;

	smap = (struct bpf_local_storage_map *)map;
	return bpf_local_storage_lookup(inode_storage, smap, cacheit_lockit);
}

void bpf_inode_storage_free(struct inode *inode)
{
	struct bpf_local_storage *local_storage;
	bool free_inode_storage = false;
	struct bpf_storage_blob *bsb;

	bsb = bpf_inode(inode);
	if (!bsb)
		return;

	rcu_read_lock();

	local_storage = rcu_dereference(bsb->storage);
	if (!local_storage) {
		rcu_read_unlock();
		return;
	}

	raw_spin_lock_bh(&local_storage->lock);
	free_inode_storage = bpf_local_storage_unlink_nolock(local_storage);
	raw_spin_unlock_bh(&local_storage->lock);
	rcu_read_unlock();

	if (free_inode_storage)
		kfree_rcu(local_storage, rcu);
}

static void *bpf_fd_inode_storage_lookup_elem(struct bpf_map *map, void *key)
{
	struct bpf_local_storage_data *sdata;
	struct file *f;
	int fd;

	fd = *(int *)key;
	f = fget_raw(fd);
	if (!f)
		return ERR_PTR(-EBADF);

	sdata = inode_storage_lookup(f->f_inode, map, true);
	fput(f);
	return sdata ? sdata->data : NULL;
}

static int bpf_fd_inode_storage_update_elem(struct bpf_map *map, void *key,
					 void *value, u64 map_flags)
{
	struct bpf_local_storage_data *sdata;
	struct file *f;
	int fd;

	fd = *(int *)key;
	f = fget_raw(fd);
	if (!f)
		return -EBADF;
	if (!inode_storage_ptr(f->f_inode)) {
		fput(f);
		return -EBADF;
	}

	sdata = bpf_local_storage_update(f->f_inode,
					 (struct bpf_local_storage_map *)map,
					 value, map_flags, GFP_ATOMIC);
	fput(f);
	return PTR_ERR_OR_ZERO(sdata);
}

static int inode_storage_delete(struct inode *inode, struct bpf_map *map)
{
	struct bpf_local_storage_data *sdata;

	sdata = inode_storage_lookup(inode, map, false);
	if (!sdata)
		return -ENOENT;

	bpf_selem_unlink(SELEM(sdata), true);

	return 0;
}

static int bpf_fd_inode_storage_delete_elem(struct bpf_map *map, void *key)
{
	struct file *f;
	int fd, err;

	fd = *(int *)key;
	f = fget_raw(fd);
	if (!f)
		return -EBADF;

	err = inode_storage_delete(f->f_inode, map);
	fput(f);
	return err;
}

/* *gfp_flags* is a hidden argument provided by the verifier */
BPF_CALL_5(bpf_inode_storage_get, struct bpf_map *, map, struct inode *, inode,
	   void *, value, u64, flags, gfp_t, gfp_flags)
{
	struct bpf_local_storage_data *sdata;

	WARN_ON_ONCE(!bpf_rcu_lock_held());
	if (flags & ~(BPF_LOCAL_STORAGE_GET_F_CREATE))
		return (unsigned long)NULL;

	/* explicitly check that the inode_storage_ptr is not
	 * NULL as inode_storage_lookup returns NULL in this case and
	 * bpf_local_storage_update expects the owner to have a
	 * valid storage pointer.
	 */
	if (!inode || !inode_storage_ptr(inode))
		return (unsigned long)NULL;

	sdata = inode_storage_lookup(inode, map, true);
	if (sdata)
		return (unsigned long)sdata->data;

	/* This helper must only called from where the inode is guaranteed
	 * to have a refcount and cannot be freed.
	 */
	if (flags & BPF_LOCAL_STORAGE_GET_F_CREATE) {
		sdata = bpf_local_storage_update(
			inode, (struct bpf_local_storage_map *)map, value,
			BPF_NOEXIST, gfp_flags);
		return IS_ERR(sdata) ? (unsigned long)NULL :
					     (unsigned long)sdata->data;
	}

	return (unsigned long)NULL;
}

BPF_CALL_2(bpf_inode_storage_delete,
	   struct bpf_map *, map, struct inode *, inode)
{
	WARN_ON_ONCE(!bpf_rcu_lock_held());
	if (!inode)
		return -EINVAL;

	/* This helper must only called from where the inode is guaranteed
	 * to have a refcount and cannot be freed.
	 */
	return inode_storage_delete(inode, map);
}

static int notsupp_get_next_key(struct bpf_map *map, void *key,
				void *next_key)
{
	return -ENOTSUPP;
}

static struct bpf_map *inode_storage_map_alloc(union bpf_attr *attr)
{
	return bpf_local_storage_map_alloc(attr, &inode_cache);
}

static void inode_storage_map_free(struct bpf_map *map)
{
	bpf_local_storage_map_free(map, &inode_cache, NULL);
}

BTF_ID_LIST_SINGLE(inode_storage_map_btf_ids, struct,
		   bpf_local_storage_map)
const struct bpf_map_ops inode_storage_map_ops = {
	.map_meta_equal = bpf_map_meta_equal,
	.map_alloc_check = bpf_local_storage_map_alloc_check,
	.map_alloc = inode_storage_map_alloc,
	.map_free = inode_storage_map_free,
	.map_get_next_key = notsupp_get_next_key,
	.map_lookup_elem = bpf_fd_inode_storage_lookup_elem,
	.map_update_elem = bpf_fd_inode_storage_update_elem,
	.map_delete_elem = bpf_fd_inode_storage_delete_elem,
	.map_check_btf = bpf_local_storage_map_check_btf,
	.map_btf_id = &inode_storage_map_btf_ids[0],
	.map_owner_storage_ptr = inode_storage_ptr,
};

BTF_ID_LIST_SINGLE(bpf_inode_storage_btf_ids, struct, inode)

const struct bpf_func_proto bpf_inode_storage_get_proto = {
	.func		= bpf_inode_storage_get,
	.gpl_only	= false,
	.ret_type	= RET_PTR_TO_MAP_VALUE_OR_NULL,
	.arg1_type	= ARG_CONST_MAP_PTR,
	.arg2_type	= ARG_PTR_TO_BTF_ID,
	.arg2_btf_id	= &bpf_inode_storage_btf_ids[0],
	.arg3_type	= ARG_PTR_TO_MAP_VALUE_OR_NULL,
	.arg4_type	= ARG_ANYTHING,
};

const struct bpf_func_proto bpf_inode_storage_delete_proto = {
	.func		= bpf_inode_storage_delete,
	.gpl_only	= false,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_CONST_MAP_PTR,
	.arg2_type	= ARG_PTR_TO_BTF_ID,
	.arg2_btf_id	= &bpf_inode_storage_btf_ids[0],
};
