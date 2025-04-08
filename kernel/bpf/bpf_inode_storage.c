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
	struct bpf_storage_blob *bsb;

	bsb = bpf_inode(inode);
	if (!bsb)
		return;

	migrate_disable();
	rcu_read_lock();

	local_storage = rcu_dereference(bsb->storage);
	if (!local_storage)
		goto out;

	bpf_local_storage_destroy(local_storage);
out:
	rcu_read_unlock();
	migrate_enable();
}

static void *bpf_fd_inode_storage_lookup_elem(struct bpf_map *map, void *key)
{
	struct bpf_local_storage_data *sdata;
	CLASS(fd_raw, f)(*(int *)key);

	if (fd_empty(f))
		return ERR_PTR(-EBADF);

	sdata = inode_storage_lookup(file_inode(fd_file(f)), map, true);
	return sdata ? sdata->data : NULL;
}

static long bpf_fd_inode_storage_update_elem(struct bpf_map *map, void *key,
					     void *value, u64 map_flags)
{
	struct bpf_local_storage_data *sdata;
	CLASS(fd_raw, f)(*(int *)key);

	if (fd_empty(f))
		return -EBADF;
	if (!inode_storage_ptr(file_inode(fd_file(f))))
		return -EBADF;

	sdata = bpf_local_storage_update(file_inode(fd_file(f)),
					 (struct bpf_local_storage_map *)map,
					 value, map_flags, false, GFP_ATOMIC);
	return PTR_ERR_OR_ZERO(sdata);
}

static int inode_storage_delete(struct inode *inode, struct bpf_map *map)
{
	struct bpf_local_storage_data *sdata;

	sdata = inode_storage_lookup(inode, map, false);
	if (!sdata)
		return -ENOENT;

	bpf_selem_unlink(SELEM(sdata), false);

	return 0;
}

static long bpf_fd_inode_storage_delete_elem(struct bpf_map *map, void *key)
{
	CLASS(fd_raw, f)(*(int *)key);

	if (fd_empty(f))
		return -EBADF;
	return inode_storage_delete(file_inode(fd_file(f)), map);
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
			BPF_NOEXIST, false, gfp_flags);
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
	return bpf_local_storage_map_alloc(attr, &inode_cache, false);
}

static void inode_storage_map_free(struct bpf_map *map)
{
	bpf_local_storage_map_free(map, &inode_cache, NULL);
}

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
	.map_mem_usage = bpf_local_storage_map_mem_usage,
	.map_btf_id = &bpf_local_storage_map_btf_id[0],
	.map_owner_storage_ptr = inode_storage_ptr,
};

BTF_ID_LIST_SINGLE(bpf_inode_storage_btf_ids, struct, inode)

const struct bpf_func_proto bpf_inode_storage_get_proto = {
	.func		= bpf_inode_storage_get,
	.gpl_only	= false,
	.ret_type	= RET_PTR_TO_MAP_VALUE_OR_NULL,
	.arg1_type	= ARG_CONST_MAP_PTR,
	.arg2_type	= ARG_PTR_TO_BTF_ID_OR_NULL,
	.arg2_btf_id	= &bpf_inode_storage_btf_ids[0],
	.arg3_type	= ARG_PTR_TO_MAP_VALUE_OR_NULL,
	.arg4_type	= ARG_ANYTHING,
};

const struct bpf_func_proto bpf_inode_storage_delete_proto = {
	.func		= bpf_inode_storage_delete,
	.gpl_only	= false,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_CONST_MAP_PTR,
	.arg2_type	= ARG_PTR_TO_BTF_ID_OR_NULL,
	.arg2_btf_id	= &bpf_inode_storage_btf_ids[0],
};
