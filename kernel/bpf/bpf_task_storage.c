// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2020 Facebook
 * Copyright 2020 Google LLC.
 */

#include <linux/pid.h>
#include <linux/sched.h>
#include <linux/rculist.h>
#include <linux/list.h>
#include <linux/hash.h>
#include <linux/types.h>
#include <linux/spinlock.h>
#include <linux/bpf.h>
#include <linux/bpf_local_storage.h>
#include <linux/filter.h>
#include <uapi/linux/btf.h>
#include <linux/bpf_lsm.h>
#include <linux/btf_ids.h>
#include <linux/fdtable.h>

DEFINE_BPF_STORAGE_CACHE(task_cache);

static struct bpf_local_storage __rcu **task_storage_ptr(void *owner)
{
	struct task_struct *task = owner;
	struct bpf_storage_blob *bsb;

	bsb = bpf_task(task);
	if (!bsb)
		return NULL;
	return &bsb->storage;
}

static struct bpf_local_storage_data *
task_storage_lookup(struct task_struct *task, struct bpf_map *map,
		    bool cacheit_lockit)
{
	struct bpf_local_storage *task_storage;
	struct bpf_local_storage_map *smap;
	struct bpf_storage_blob *bsb;

	bsb = bpf_task(task);
	if (!bsb)
		return NULL;

	task_storage = rcu_dereference(bsb->storage);
	if (!task_storage)
		return NULL;

	smap = (struct bpf_local_storage_map *)map;
	return bpf_local_storage_lookup(task_storage, smap, cacheit_lockit);
}

void bpf_task_storage_free(struct task_struct *task)
{
	struct bpf_local_storage_elem *selem;
	struct bpf_local_storage *local_storage;
	bool free_task_storage = false;
	struct bpf_storage_blob *bsb;
	struct hlist_node *n;

	bsb = bpf_task(task);
	if (!bsb)
		return;

	rcu_read_lock();

	local_storage = rcu_dereference(bsb->storage);
	if (!local_storage) {
		rcu_read_unlock();
		return;
	}

	/* Neither the bpf_prog nor the bpf-map's syscall
	 * could be modifying the local_storage->list now.
	 * Thus, no elem can be added-to or deleted-from the
	 * local_storage->list by the bpf_prog or by the bpf-map's syscall.
	 *
	 * It is racing with bpf_local_storage_map_free() alone
	 * when unlinking elem from the local_storage->list and
	 * the map's bucket->list.
	 */
	raw_spin_lock_bh(&local_storage->lock);
	hlist_for_each_entry_safe(selem, n, &local_storage->list, snode) {
		/* Always unlink from map before unlinking from
		 * local_storage.
		 */
		bpf_selem_unlink_map(selem);
		free_task_storage = bpf_selem_unlink_storage_nolock(
			local_storage, selem, false);
	}
	raw_spin_unlock_bh(&local_storage->lock);
	rcu_read_unlock();

	/* free_task_storage should always be true as long as
	 * local_storage->list was non-empty.
	 */
	if (free_task_storage)
		kfree_rcu(local_storage, rcu);
}

static void *bpf_pid_task_storage_lookup_elem(struct bpf_map *map, void *key)
{
	struct bpf_local_storage_data *sdata;
	struct task_struct *task;
	unsigned int f_flags;
	struct pid *pid;
	int fd, err;

	fd = *(int *)key;
	pid = pidfd_get_pid(fd, &f_flags);
	if (IS_ERR(pid))
		return ERR_CAST(pid);

	/* We should be in an RCU read side critical section, it should be safe
	 * to call pid_task.
	 */
	WARN_ON_ONCE(!rcu_read_lock_held());
	task = pid_task(pid, PIDTYPE_PID);
	if (!task) {
		err = -ENOENT;
		goto out;
	}

	sdata = task_storage_lookup(task, map, true);
	put_pid(pid);
	return sdata ? sdata->data : NULL;
out:
	put_pid(pid);
	return ERR_PTR(err);
}

static int bpf_pid_task_storage_update_elem(struct bpf_map *map, void *key,
					    void *value, u64 map_flags)
{
	struct bpf_local_storage_data *sdata;
	struct task_struct *task;
	unsigned int f_flags;
	struct pid *pid;
	int fd, err;

	fd = *(int *)key;
	pid = pidfd_get_pid(fd, &f_flags);
	if (IS_ERR(pid))
		return PTR_ERR(pid);

	/* We should be in an RCU read side critical section, it should be safe
	 * to call pid_task.
	 */
	WARN_ON_ONCE(!rcu_read_lock_held());
	task = pid_task(pid, PIDTYPE_PID);
	if (!task) {
		err = -ENOENT;
		goto out;
	}

	sdata = bpf_local_storage_update(
		task, (struct bpf_local_storage_map *)map, value, map_flags);

	err = PTR_ERR_OR_ZERO(sdata);
out:
	put_pid(pid);
	return err;
}

static int task_storage_delete(struct task_struct *task, struct bpf_map *map)
{
	struct bpf_local_storage_data *sdata;

	sdata = task_storage_lookup(task, map, false);
	if (!sdata)
		return -ENOENT;

	bpf_selem_unlink(SELEM(sdata));

	return 0;
}

static int bpf_pid_task_storage_delete_elem(struct bpf_map *map, void *key)
{
	struct task_struct *task;
	unsigned int f_flags;
	struct pid *pid;
	int fd, err;

	fd = *(int *)key;
	pid = pidfd_get_pid(fd, &f_flags);
	if (IS_ERR(pid))
		return PTR_ERR(pid);

	/* We should be in an RCU read side critical section, it should be safe
	 * to call pid_task.
	 */
	WARN_ON_ONCE(!rcu_read_lock_held());
	task = pid_task(pid, PIDTYPE_PID);
	if (!task) {
		err = -ENOENT;
		goto out;
	}

	err = task_storage_delete(task, map);
out:
	put_pid(pid);
	return err;
}

BPF_CALL_4(bpf_task_storage_get, struct bpf_map *, map, struct task_struct *,
	   task, void *, value, u64, flags)
{
	struct bpf_local_storage_data *sdata;

	if (flags & ~(BPF_LOCAL_STORAGE_GET_F_CREATE))
		return (unsigned long)NULL;

	/* explicitly check that the task_storage_ptr is not
	 * NULL as task_storage_lookup returns NULL in this case and
	 * bpf_local_storage_update expects the owner to have a
	 * valid storage pointer.
	 */
	if (!task_storage_ptr(task))
		return (unsigned long)NULL;

	sdata = task_storage_lookup(task, map, true);
	if (sdata)
		return (unsigned long)sdata->data;

	/* This helper must only be called from places where the lifetime of the task
	 * is guaranteed. Either by being refcounted or by being protected
	 * by an RCU read-side critical section.
	 */
	if (flags & BPF_LOCAL_STORAGE_GET_F_CREATE) {
		sdata = bpf_local_storage_update(
			task, (struct bpf_local_storage_map *)map, value,
			BPF_NOEXIST);
		return IS_ERR(sdata) ? (unsigned long)NULL :
					     (unsigned long)sdata->data;
	}

	return (unsigned long)NULL;
}

BPF_CALL_2(bpf_task_storage_delete, struct bpf_map *, map, struct task_struct *,
	   task)
{
	/* This helper must only be called from places where the lifetime of the task
	 * is guaranteed. Either by being refcounted or by being protected
	 * by an RCU read-side critical section.
	 */
	return task_storage_delete(task, map);
}

static int notsupp_get_next_key(struct bpf_map *map, void *key, void *next_key)
{
	return -ENOTSUPP;
}

static struct bpf_map *task_storage_map_alloc(union bpf_attr *attr)
{
	struct bpf_local_storage_map *smap;

	smap = bpf_local_storage_map_alloc(attr);
	if (IS_ERR(smap))
		return ERR_CAST(smap);

	smap->cache_idx = bpf_local_storage_cache_idx_get(&task_cache);
	return &smap->map;
}

static void task_storage_map_free(struct bpf_map *map)
{
	struct bpf_local_storage_map *smap;

	smap = (struct bpf_local_storage_map *)map;
	bpf_local_storage_cache_idx_free(&task_cache, smap->cache_idx);
	bpf_local_storage_map_free(smap);
}

static int task_storage_map_btf_id;
const struct bpf_map_ops task_storage_map_ops = {
	.map_meta_equal = bpf_map_meta_equal,
	.map_alloc_check = bpf_local_storage_map_alloc_check,
	.map_alloc = task_storage_map_alloc,
	.map_free = task_storage_map_free,
	.map_get_next_key = notsupp_get_next_key,
	.map_lookup_elem = bpf_pid_task_storage_lookup_elem,
	.map_update_elem = bpf_pid_task_storage_update_elem,
	.map_delete_elem = bpf_pid_task_storage_delete_elem,
	.map_check_btf = bpf_local_storage_map_check_btf,
	.map_btf_name = "bpf_local_storage_map",
	.map_btf_id = &task_storage_map_btf_id,
	.map_owner_storage_ptr = task_storage_ptr,
};

BTF_ID_LIST_SINGLE(bpf_task_storage_btf_ids, struct, task_struct)

const struct bpf_func_proto bpf_task_storage_get_proto = {
	.func = bpf_task_storage_get,
	.gpl_only = false,
	.ret_type = RET_PTR_TO_MAP_VALUE_OR_NULL,
	.arg1_type = ARG_CONST_MAP_PTR,
	.arg2_type = ARG_PTR_TO_BTF_ID,
	.arg2_btf_id = &bpf_task_storage_btf_ids[0],
	.arg3_type = ARG_PTR_TO_MAP_VALUE_OR_NULL,
	.arg4_type = ARG_ANYTHING,
};

const struct bpf_func_proto bpf_task_storage_delete_proto = {
	.func = bpf_task_storage_delete,
	.gpl_only = false,
	.ret_type = RET_INTEGER,
	.arg1_type = ARG_CONST_MAP_PTR,
	.arg2_type = ARG_PTR_TO_BTF_ID,
	.arg2_btf_id = &bpf_task_storage_btf_ids[0],
};
