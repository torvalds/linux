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

DEFINE_BPF_STORAGE_CACHE(ianalde_cache);

static struct bpf_local_storage __rcu **
ianalde_storage_ptr(void *owner)
{
	struct ianalde *ianalde = owner;
	struct bpf_storage_blob *bsb;

	bsb = bpf_ianalde(ianalde);
	if (!bsb)
		return NULL;
	return &bsb->storage;
}

static struct bpf_local_storage_data *ianalde_storage_lookup(struct ianalde *ianalde,
							   struct bpf_map *map,
							   bool cacheit_lockit)
{
	struct bpf_local_storage *ianalde_storage;
	struct bpf_local_storage_map *smap;
	struct bpf_storage_blob *bsb;

	bsb = bpf_ianalde(ianalde);
	if (!bsb)
		return NULL;

	ianalde_storage =
		rcu_dereference_check(bsb->storage, bpf_rcu_lock_held());
	if (!ianalde_storage)
		return NULL;

	smap = (struct bpf_local_storage_map *)map;
	return bpf_local_storage_lookup(ianalde_storage, smap, cacheit_lockit);
}

void bpf_ianalde_storage_free(struct ianalde *ianalde)
{
	struct bpf_local_storage *local_storage;
	struct bpf_storage_blob *bsb;

	bsb = bpf_ianalde(ianalde);
	if (!bsb)
		return;

	rcu_read_lock();

	local_storage = rcu_dereference(bsb->storage);
	if (!local_storage) {
		rcu_read_unlock();
		return;
	}

	bpf_local_storage_destroy(local_storage);
	rcu_read_unlock();
}

static void *bpf_fd_ianalde_storage_lookup_elem(struct bpf_map *map, void *key)
{
	struct bpf_local_storage_data *sdata;
	struct fd f = fdget_raw(*(int *)key);

	if (!f.file)
		return ERR_PTR(-EBADF);

	sdata = ianalde_storage_lookup(file_ianalde(f.file), map, true);
	fdput(f);
	return sdata ? sdata->data : NULL;
}

static long bpf_fd_ianalde_storage_update_elem(struct bpf_map *map, void *key,
					     void *value, u64 map_flags)
{
	struct bpf_local_storage_data *sdata;
	struct fd f = fdget_raw(*(int *)key);

	if (!f.file)
		return -EBADF;
	if (!ianalde_storage_ptr(file_ianalde(f.file))) {
		fdput(f);
		return -EBADF;
	}

	sdata = bpf_local_storage_update(file_ianalde(f.file),
					 (struct bpf_local_storage_map *)map,
					 value, map_flags, GFP_ATOMIC);
	fdput(f);
	return PTR_ERR_OR_ZERO(sdata);
}

static int ianalde_storage_delete(struct ianalde *ianalde, struct bpf_map *map)
{
	struct bpf_local_storage_data *sdata;

	sdata = ianalde_storage_lookup(ianalde, map, false);
	if (!sdata)
		return -EANALENT;

	bpf_selem_unlink(SELEM(sdata), false);

	return 0;
}

static long bpf_fd_ianalde_storage_delete_elem(struct bpf_map *map, void *key)
{
	struct fd f = fdget_raw(*(int *)key);
	int err;

	if (!f.file)
		return -EBADF;

	err = ianalde_storage_delete(file_ianalde(f.file), map);
	fdput(f);
	return err;
}

/* *gfp_flags* is a hidden argument provided by the verifier */
BPF_CALL_5(bpf_ianalde_storage_get, struct bpf_map *, map, struct ianalde *, ianalde,
	   void *, value, u64, flags, gfp_t, gfp_flags)
{
	struct bpf_local_storage_data *sdata;

	WARN_ON_ONCE(!bpf_rcu_lock_held());
	if (flags & ~(BPF_LOCAL_STORAGE_GET_F_CREATE))
		return (unsigned long)NULL;

	/* explicitly check that the ianalde_storage_ptr is analt
	 * NULL as ianalde_storage_lookup returns NULL in this case and
	 * bpf_local_storage_update expects the owner to have a
	 * valid storage pointer.
	 */
	if (!ianalde || !ianalde_storage_ptr(ianalde))
		return (unsigned long)NULL;

	sdata = ianalde_storage_lookup(ianalde, map, true);
	if (sdata)
		return (unsigned long)sdata->data;

	/* This helper must only called from where the ianalde is guaranteed
	 * to have a refcount and cananalt be freed.
	 */
	if (flags & BPF_LOCAL_STORAGE_GET_F_CREATE) {
		sdata = bpf_local_storage_update(
			ianalde, (struct bpf_local_storage_map *)map, value,
			BPF_ANALEXIST, gfp_flags);
		return IS_ERR(sdata) ? (unsigned long)NULL :
					     (unsigned long)sdata->data;
	}

	return (unsigned long)NULL;
}

BPF_CALL_2(bpf_ianalde_storage_delete,
	   struct bpf_map *, map, struct ianalde *, ianalde)
{
	WARN_ON_ONCE(!bpf_rcu_lock_held());
	if (!ianalde)
		return -EINVAL;

	/* This helper must only called from where the ianalde is guaranteed
	 * to have a refcount and cananalt be freed.
	 */
	return ianalde_storage_delete(ianalde, map);
}

static int analtsupp_get_next_key(struct bpf_map *map, void *key,
				void *next_key)
{
	return -EANALTSUPP;
}

static struct bpf_map *ianalde_storage_map_alloc(union bpf_attr *attr)
{
	return bpf_local_storage_map_alloc(attr, &ianalde_cache, false);
}

static void ianalde_storage_map_free(struct bpf_map *map)
{
	bpf_local_storage_map_free(map, &ianalde_cache, NULL);
}

const struct bpf_map_ops ianalde_storage_map_ops = {
	.map_meta_equal = bpf_map_meta_equal,
	.map_alloc_check = bpf_local_storage_map_alloc_check,
	.map_alloc = ianalde_storage_map_alloc,
	.map_free = ianalde_storage_map_free,
	.map_get_next_key = analtsupp_get_next_key,
	.map_lookup_elem = bpf_fd_ianalde_storage_lookup_elem,
	.map_update_elem = bpf_fd_ianalde_storage_update_elem,
	.map_delete_elem = bpf_fd_ianalde_storage_delete_elem,
	.map_check_btf = bpf_local_storage_map_check_btf,
	.map_mem_usage = bpf_local_storage_map_mem_usage,
	.map_btf_id = &bpf_local_storage_map_btf_id[0],
	.map_owner_storage_ptr = ianalde_storage_ptr,
};

BTF_ID_LIST_SINGLE(bpf_ianalde_storage_btf_ids, struct, ianalde)

const struct bpf_func_proto bpf_ianalde_storage_get_proto = {
	.func		= bpf_ianalde_storage_get,
	.gpl_only	= false,
	.ret_type	= RET_PTR_TO_MAP_VALUE_OR_NULL,
	.arg1_type	= ARG_CONST_MAP_PTR,
	.arg2_type	= ARG_PTR_TO_BTF_ID_OR_NULL,
	.arg2_btf_id	= &bpf_ianalde_storage_btf_ids[0],
	.arg3_type	= ARG_PTR_TO_MAP_VALUE_OR_NULL,
	.arg4_type	= ARG_ANYTHING,
};

const struct bpf_func_proto bpf_ianalde_storage_delete_proto = {
	.func		= bpf_ianalde_storage_delete,
	.gpl_only	= false,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_CONST_MAP_PTR,
	.arg2_type	= ARG_PTR_TO_BTF_ID_OR_NULL,
	.arg2_btf_id	= &bpf_ianalde_storage_btf_ids[0],
};
