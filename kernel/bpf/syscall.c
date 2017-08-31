/* Copyright (c) 2011-2014 PLUMgrid, http://plumgrid.com
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 */
#include <linux/bpf.h>
#include <linux/bpf_trace.h>
#include <linux/syscalls.h>
#include <linux/slab.h>
#include <linux/sched/signal.h>
#include <linux/vmalloc.h>
#include <linux/mmzone.h>
#include <linux/anon_inodes.h>
#include <linux/file.h>
#include <linux/license.h>
#include <linux/filter.h>
#include <linux/version.h>
#include <linux/kernel.h>
#include <linux/idr.h>

#define IS_FD_ARRAY(map) ((map)->map_type == BPF_MAP_TYPE_PROG_ARRAY || \
			   (map)->map_type == BPF_MAP_TYPE_PERF_EVENT_ARRAY || \
			   (map)->map_type == BPF_MAP_TYPE_CGROUP_ARRAY || \
			   (map)->map_type == BPF_MAP_TYPE_ARRAY_OF_MAPS)
#define IS_FD_HASH(map) ((map)->map_type == BPF_MAP_TYPE_HASH_OF_MAPS)
#define IS_FD_MAP(map) (IS_FD_ARRAY(map) || IS_FD_HASH(map))

DEFINE_PER_CPU(int, bpf_prog_active);
static DEFINE_IDR(prog_idr);
static DEFINE_SPINLOCK(prog_idr_lock);
static DEFINE_IDR(map_idr);
static DEFINE_SPINLOCK(map_idr_lock);

int sysctl_unprivileged_bpf_disabled __read_mostly;

static const struct bpf_map_ops * const bpf_map_types[] = {
#define BPF_PROG_TYPE(_id, _ops)
#define BPF_MAP_TYPE(_id, _ops) \
	[_id] = &_ops,
#include <linux/bpf_types.h>
#undef BPF_PROG_TYPE
#undef BPF_MAP_TYPE
};

static struct bpf_map *find_and_alloc_map(union bpf_attr *attr)
{
	struct bpf_map *map;

	if (attr->map_type >= ARRAY_SIZE(bpf_map_types) ||
	    !bpf_map_types[attr->map_type])
		return ERR_PTR(-EINVAL);

	map = bpf_map_types[attr->map_type]->map_alloc(attr);
	if (IS_ERR(map))
		return map;
	map->ops = bpf_map_types[attr->map_type];
	map->map_type = attr->map_type;
	return map;
}

void *bpf_map_area_alloc(size_t size)
{
	/* We definitely need __GFP_NORETRY, so OOM killer doesn't
	 * trigger under memory pressure as we really just want to
	 * fail instead.
	 */
	const gfp_t flags = __GFP_NOWARN | __GFP_NORETRY | __GFP_ZERO;
	void *area;

	if (size <= (PAGE_SIZE << PAGE_ALLOC_COSTLY_ORDER)) {
		area = kmalloc(size, GFP_USER | flags);
		if (area != NULL)
			return area;
	}

	return __vmalloc(size, GFP_KERNEL | flags, PAGE_KERNEL);
}

void bpf_map_area_free(void *area)
{
	kvfree(area);
}

int bpf_map_precharge_memlock(u32 pages)
{
	struct user_struct *user = get_current_user();
	unsigned long memlock_limit, cur;

	memlock_limit = rlimit(RLIMIT_MEMLOCK) >> PAGE_SHIFT;
	cur = atomic_long_read(&user->locked_vm);
	free_uid(user);
	if (cur + pages > memlock_limit)
		return -EPERM;
	return 0;
}

static int bpf_map_charge_memlock(struct bpf_map *map)
{
	struct user_struct *user = get_current_user();
	unsigned long memlock_limit;

	memlock_limit = rlimit(RLIMIT_MEMLOCK) >> PAGE_SHIFT;

	atomic_long_add(map->pages, &user->locked_vm);

	if (atomic_long_read(&user->locked_vm) > memlock_limit) {
		atomic_long_sub(map->pages, &user->locked_vm);
		free_uid(user);
		return -EPERM;
	}
	map->user = user;
	return 0;
}

static void bpf_map_uncharge_memlock(struct bpf_map *map)
{
	struct user_struct *user = map->user;

	atomic_long_sub(map->pages, &user->locked_vm);
	free_uid(user);
}

static int bpf_map_alloc_id(struct bpf_map *map)
{
	int id;

	spin_lock_bh(&map_idr_lock);
	id = idr_alloc_cyclic(&map_idr, map, 1, INT_MAX, GFP_ATOMIC);
	if (id > 0)
		map->id = id;
	spin_unlock_bh(&map_idr_lock);

	if (WARN_ON_ONCE(!id))
		return -ENOSPC;

	return id > 0 ? 0 : id;
}

static void bpf_map_free_id(struct bpf_map *map, bool do_idr_lock)
{
	if (do_idr_lock)
		spin_lock_bh(&map_idr_lock);
	else
		__acquire(&map_idr_lock);

	idr_remove(&map_idr, map->id);

	if (do_idr_lock)
		spin_unlock_bh(&map_idr_lock);
	else
		__release(&map_idr_lock);
}

/* called from workqueue */
static void bpf_map_free_deferred(struct work_struct *work)
{
	struct bpf_map *map = container_of(work, struct bpf_map, work);

	bpf_map_uncharge_memlock(map);
	/* implementation dependent freeing */
	map->ops->map_free(map);
}

static void bpf_map_put_uref(struct bpf_map *map)
{
	if (atomic_dec_and_test(&map->usercnt)) {
		if (map->map_type == BPF_MAP_TYPE_PROG_ARRAY)
			bpf_fd_array_map_clear(map);
	}
}

/* decrement map refcnt and schedule it for freeing via workqueue
 * (unrelying map implementation ops->map_free() might sleep)
 */
static void __bpf_map_put(struct bpf_map *map, bool do_idr_lock)
{
	if (atomic_dec_and_test(&map->refcnt)) {
		/* bpf_map_free_id() must be called first */
		bpf_map_free_id(map, do_idr_lock);
		INIT_WORK(&map->work, bpf_map_free_deferred);
		schedule_work(&map->work);
	}
}

void bpf_map_put(struct bpf_map *map)
{
	__bpf_map_put(map, true);
}

void bpf_map_put_with_uref(struct bpf_map *map)
{
	bpf_map_put_uref(map);
	bpf_map_put(map);
}

static int bpf_map_release(struct inode *inode, struct file *filp)
{
	struct bpf_map *map = filp->private_data;

	if (map->ops->map_release)
		map->ops->map_release(map, filp);

	bpf_map_put_with_uref(map);
	return 0;
}

#ifdef CONFIG_PROC_FS
static void bpf_map_show_fdinfo(struct seq_file *m, struct file *filp)
{
	const struct bpf_map *map = filp->private_data;
	const struct bpf_array *array;
	u32 owner_prog_type = 0;
	u32 owner_jited = 0;

	if (map->map_type == BPF_MAP_TYPE_PROG_ARRAY) {
		array = container_of(map, struct bpf_array, map);
		owner_prog_type = array->owner_prog_type;
		owner_jited = array->owner_jited;
	}

	seq_printf(m,
		   "map_type:\t%u\n"
		   "key_size:\t%u\n"
		   "value_size:\t%u\n"
		   "max_entries:\t%u\n"
		   "map_flags:\t%#x\n"
		   "memlock:\t%llu\n",
		   map->map_type,
		   map->key_size,
		   map->value_size,
		   map->max_entries,
		   map->map_flags,
		   map->pages * 1ULL << PAGE_SHIFT);

	if (owner_prog_type) {
		seq_printf(m, "owner_prog_type:\t%u\n",
			   owner_prog_type);
		seq_printf(m, "owner_jited:\t%u\n",
			   owner_jited);
	}
}
#endif

static const struct file_operations bpf_map_fops = {
#ifdef CONFIG_PROC_FS
	.show_fdinfo	= bpf_map_show_fdinfo,
#endif
	.release	= bpf_map_release,
};

int bpf_map_new_fd(struct bpf_map *map)
{
	return anon_inode_getfd("bpf-map", &bpf_map_fops, map,
				O_RDWR | O_CLOEXEC);
}

/* helper macro to check that unused fields 'union bpf_attr' are zero */
#define CHECK_ATTR(CMD) \
	memchr_inv((void *) &attr->CMD##_LAST_FIELD + \
		   sizeof(attr->CMD##_LAST_FIELD), 0, \
		   sizeof(*attr) - \
		   offsetof(union bpf_attr, CMD##_LAST_FIELD) - \
		   sizeof(attr->CMD##_LAST_FIELD)) != NULL

#define BPF_MAP_CREATE_LAST_FIELD inner_map_fd
/* called via syscall */
static int map_create(union bpf_attr *attr)
{
	struct bpf_map *map;
	int err;

	err = CHECK_ATTR(BPF_MAP_CREATE);
	if (err)
		return -EINVAL;

	/* find map type and init map: hashtable vs rbtree vs bloom vs ... */
	map = find_and_alloc_map(attr);
	if (IS_ERR(map))
		return PTR_ERR(map);

	atomic_set(&map->refcnt, 1);
	atomic_set(&map->usercnt, 1);

	err = bpf_map_charge_memlock(map);
	if (err)
		goto free_map_nouncharge;

	err = bpf_map_alloc_id(map);
	if (err)
		goto free_map;

	err = bpf_map_new_fd(map);
	if (err < 0) {
		/* failed to allocate fd.
		 * bpf_map_put() is needed because the above
		 * bpf_map_alloc_id() has published the map
		 * to the userspace and the userspace may
		 * have refcnt-ed it through BPF_MAP_GET_FD_BY_ID.
		 */
		bpf_map_put(map);
		return err;
	}

	trace_bpf_map_create(map, err);
	return err;

free_map:
	bpf_map_uncharge_memlock(map);
free_map_nouncharge:
	map->ops->map_free(map);
	return err;
}

/* if error is returned, fd is released.
 * On success caller should complete fd access with matching fdput()
 */
struct bpf_map *__bpf_map_get(struct fd f)
{
	if (!f.file)
		return ERR_PTR(-EBADF);
	if (f.file->f_op != &bpf_map_fops) {
		fdput(f);
		return ERR_PTR(-EINVAL);
	}

	return f.file->private_data;
}

/* prog's and map's refcnt limit */
#define BPF_MAX_REFCNT 32768

struct bpf_map *bpf_map_inc(struct bpf_map *map, bool uref)
{
	if (atomic_inc_return(&map->refcnt) > BPF_MAX_REFCNT) {
		atomic_dec(&map->refcnt);
		return ERR_PTR(-EBUSY);
	}
	if (uref)
		atomic_inc(&map->usercnt);
	return map;
}

struct bpf_map *bpf_map_get_with_uref(u32 ufd)
{
	struct fd f = fdget(ufd);
	struct bpf_map *map;

	map = __bpf_map_get(f);
	if (IS_ERR(map))
		return map;

	map = bpf_map_inc(map, true);
	fdput(f);

	return map;
}

/* map_idr_lock should have been held */
static struct bpf_map *bpf_map_inc_not_zero(struct bpf_map *map,
					    bool uref)
{
	int refold;

	refold = __atomic_add_unless(&map->refcnt, 1, 0);

	if (refold >= BPF_MAX_REFCNT) {
		__bpf_map_put(map, false);
		return ERR_PTR(-EBUSY);
	}

	if (!refold)
		return ERR_PTR(-ENOENT);

	if (uref)
		atomic_inc(&map->usercnt);

	return map;
}

int __weak bpf_stackmap_copy(struct bpf_map *map, void *key, void *value)
{
	return -ENOTSUPP;
}

/* last field in 'union bpf_attr' used by this command */
#define BPF_MAP_LOOKUP_ELEM_LAST_FIELD value

static int map_lookup_elem(union bpf_attr *attr)
{
	void __user *ukey = u64_to_user_ptr(attr->key);
	void __user *uvalue = u64_to_user_ptr(attr->value);
	int ufd = attr->map_fd;
	struct bpf_map *map;
	void *key, *value, *ptr;
	u32 value_size;
	struct fd f;
	int err;

	if (CHECK_ATTR(BPF_MAP_LOOKUP_ELEM))
		return -EINVAL;

	f = fdget(ufd);
	map = __bpf_map_get(f);
	if (IS_ERR(map))
		return PTR_ERR(map);

	key = memdup_user(ukey, map->key_size);
	if (IS_ERR(key)) {
		err = PTR_ERR(key);
		goto err_put;
	}

	if (map->map_type == BPF_MAP_TYPE_PERCPU_HASH ||
	    map->map_type == BPF_MAP_TYPE_LRU_PERCPU_HASH ||
	    map->map_type == BPF_MAP_TYPE_PERCPU_ARRAY)
		value_size = round_up(map->value_size, 8) * num_possible_cpus();
	else if (IS_FD_MAP(map))
		value_size = sizeof(u32);
	else
		value_size = map->value_size;

	err = -ENOMEM;
	value = kmalloc(value_size, GFP_USER | __GFP_NOWARN);
	if (!value)
		goto free_key;

	if (map->map_type == BPF_MAP_TYPE_PERCPU_HASH ||
	    map->map_type == BPF_MAP_TYPE_LRU_PERCPU_HASH) {
		err = bpf_percpu_hash_copy(map, key, value);
	} else if (map->map_type == BPF_MAP_TYPE_PERCPU_ARRAY) {
		err = bpf_percpu_array_copy(map, key, value);
	} else if (map->map_type == BPF_MAP_TYPE_STACK_TRACE) {
		err = bpf_stackmap_copy(map, key, value);
	} else if (IS_FD_ARRAY(map)) {
		err = bpf_fd_array_map_lookup_elem(map, key, value);
	} else if (IS_FD_HASH(map)) {
		err = bpf_fd_htab_map_lookup_elem(map, key, value);
	} else {
		rcu_read_lock();
		ptr = map->ops->map_lookup_elem(map, key);
		if (ptr)
			memcpy(value, ptr, value_size);
		rcu_read_unlock();
		err = ptr ? 0 : -ENOENT;
	}

	if (err)
		goto free_value;

	err = -EFAULT;
	if (copy_to_user(uvalue, value, value_size) != 0)
		goto free_value;

	trace_bpf_map_lookup_elem(map, ufd, key, value);
	err = 0;

free_value:
	kfree(value);
free_key:
	kfree(key);
err_put:
	fdput(f);
	return err;
}

#define BPF_MAP_UPDATE_ELEM_LAST_FIELD flags

static int map_update_elem(union bpf_attr *attr)
{
	void __user *ukey = u64_to_user_ptr(attr->key);
	void __user *uvalue = u64_to_user_ptr(attr->value);
	int ufd = attr->map_fd;
	struct bpf_map *map;
	void *key, *value;
	u32 value_size;
	struct fd f;
	int err;

	if (CHECK_ATTR(BPF_MAP_UPDATE_ELEM))
		return -EINVAL;

	f = fdget(ufd);
	map = __bpf_map_get(f);
	if (IS_ERR(map))
		return PTR_ERR(map);

	key = memdup_user(ukey, map->key_size);
	if (IS_ERR(key)) {
		err = PTR_ERR(key);
		goto err_put;
	}

	if (map->map_type == BPF_MAP_TYPE_PERCPU_HASH ||
	    map->map_type == BPF_MAP_TYPE_LRU_PERCPU_HASH ||
	    map->map_type == BPF_MAP_TYPE_PERCPU_ARRAY)
		value_size = round_up(map->value_size, 8) * num_possible_cpus();
	else
		value_size = map->value_size;

	err = -ENOMEM;
	value = kmalloc(value_size, GFP_USER | __GFP_NOWARN);
	if (!value)
		goto free_key;

	err = -EFAULT;
	if (copy_from_user(value, uvalue, value_size) != 0)
		goto free_value;

	/* must increment bpf_prog_active to avoid kprobe+bpf triggering from
	 * inside bpf map update or delete otherwise deadlocks are possible
	 */
	preempt_disable();
	__this_cpu_inc(bpf_prog_active);
	if (map->map_type == BPF_MAP_TYPE_PERCPU_HASH ||
	    map->map_type == BPF_MAP_TYPE_LRU_PERCPU_HASH) {
		err = bpf_percpu_hash_update(map, key, value, attr->flags);
	} else if (map->map_type == BPF_MAP_TYPE_PERCPU_ARRAY) {
		err = bpf_percpu_array_update(map, key, value, attr->flags);
	} else if (map->map_type == BPF_MAP_TYPE_PERF_EVENT_ARRAY ||
		   map->map_type == BPF_MAP_TYPE_PROG_ARRAY ||
		   map->map_type == BPF_MAP_TYPE_CGROUP_ARRAY ||
		   map->map_type == BPF_MAP_TYPE_ARRAY_OF_MAPS) {
		rcu_read_lock();
		err = bpf_fd_array_map_update_elem(map, f.file, key, value,
						   attr->flags);
		rcu_read_unlock();
	} else if (map->map_type == BPF_MAP_TYPE_HASH_OF_MAPS) {
		rcu_read_lock();
		err = bpf_fd_htab_map_update_elem(map, f.file, key, value,
						  attr->flags);
		rcu_read_unlock();
	} else {
		rcu_read_lock();
		err = map->ops->map_update_elem(map, key, value, attr->flags);
		rcu_read_unlock();
	}
	__this_cpu_dec(bpf_prog_active);
	preempt_enable();

	if (!err)
		trace_bpf_map_update_elem(map, ufd, key, value);
free_value:
	kfree(value);
free_key:
	kfree(key);
err_put:
	fdput(f);
	return err;
}

#define BPF_MAP_DELETE_ELEM_LAST_FIELD key

static int map_delete_elem(union bpf_attr *attr)
{
	void __user *ukey = u64_to_user_ptr(attr->key);
	int ufd = attr->map_fd;
	struct bpf_map *map;
	struct fd f;
	void *key;
	int err;

	if (CHECK_ATTR(BPF_MAP_DELETE_ELEM))
		return -EINVAL;

	f = fdget(ufd);
	map = __bpf_map_get(f);
	if (IS_ERR(map))
		return PTR_ERR(map);

	key = memdup_user(ukey, map->key_size);
	if (IS_ERR(key)) {
		err = PTR_ERR(key);
		goto err_put;
	}

	preempt_disable();
	__this_cpu_inc(bpf_prog_active);
	rcu_read_lock();
	err = map->ops->map_delete_elem(map, key);
	rcu_read_unlock();
	__this_cpu_dec(bpf_prog_active);
	preempt_enable();

	if (!err)
		trace_bpf_map_delete_elem(map, ufd, key);
	kfree(key);
err_put:
	fdput(f);
	return err;
}

/* last field in 'union bpf_attr' used by this command */
#define BPF_MAP_GET_NEXT_KEY_LAST_FIELD next_key

static int map_get_next_key(union bpf_attr *attr)
{
	void __user *ukey = u64_to_user_ptr(attr->key);
	void __user *unext_key = u64_to_user_ptr(attr->next_key);
	int ufd = attr->map_fd;
	struct bpf_map *map;
	void *key, *next_key;
	struct fd f;
	int err;

	if (CHECK_ATTR(BPF_MAP_GET_NEXT_KEY))
		return -EINVAL;

	f = fdget(ufd);
	map = __bpf_map_get(f);
	if (IS_ERR(map))
		return PTR_ERR(map);

	if (ukey) {
		key = memdup_user(ukey, map->key_size);
		if (IS_ERR(key)) {
			err = PTR_ERR(key);
			goto err_put;
		}
	} else {
		key = NULL;
	}

	err = -ENOMEM;
	next_key = kmalloc(map->key_size, GFP_USER);
	if (!next_key)
		goto free_key;

	rcu_read_lock();
	err = map->ops->map_get_next_key(map, key, next_key);
	rcu_read_unlock();
	if (err)
		goto free_next_key;

	err = -EFAULT;
	if (copy_to_user(unext_key, next_key, map->key_size) != 0)
		goto free_next_key;

	trace_bpf_map_next_key(map, ufd, key, next_key);
	err = 0;

free_next_key:
	kfree(next_key);
free_key:
	kfree(key);
err_put:
	fdput(f);
	return err;
}

static const struct bpf_verifier_ops * const bpf_prog_types[] = {
#define BPF_PROG_TYPE(_id, _ops) \
	[_id] = &_ops,
#define BPF_MAP_TYPE(_id, _ops)
#include <linux/bpf_types.h>
#undef BPF_PROG_TYPE
#undef BPF_MAP_TYPE
};

static int find_prog_type(enum bpf_prog_type type, struct bpf_prog *prog)
{
	if (type >= ARRAY_SIZE(bpf_prog_types) || !bpf_prog_types[type])
		return -EINVAL;

	prog->aux->ops = bpf_prog_types[type];
	prog->type = type;
	return 0;
}

/* drop refcnt on maps used by eBPF program and free auxilary data */
static void free_used_maps(struct bpf_prog_aux *aux)
{
	int i;

	for (i = 0; i < aux->used_map_cnt; i++)
		bpf_map_put(aux->used_maps[i]);

	kfree(aux->used_maps);
}

int __bpf_prog_charge(struct user_struct *user, u32 pages)
{
	unsigned long memlock_limit = rlimit(RLIMIT_MEMLOCK) >> PAGE_SHIFT;
	unsigned long user_bufs;

	if (user) {
		user_bufs = atomic_long_add_return(pages, &user->locked_vm);
		if (user_bufs > memlock_limit) {
			atomic_long_sub(pages, &user->locked_vm);
			return -EPERM;
		}
	}

	return 0;
}

void __bpf_prog_uncharge(struct user_struct *user, u32 pages)
{
	if (user)
		atomic_long_sub(pages, &user->locked_vm);
}

static int bpf_prog_charge_memlock(struct bpf_prog *prog)
{
	struct user_struct *user = get_current_user();
	int ret;

	ret = __bpf_prog_charge(user, prog->pages);
	if (ret) {
		free_uid(user);
		return ret;
	}

	prog->aux->user = user;
	return 0;
}

static void bpf_prog_uncharge_memlock(struct bpf_prog *prog)
{
	struct user_struct *user = prog->aux->user;

	__bpf_prog_uncharge(user, prog->pages);
	free_uid(user);
}

static int bpf_prog_alloc_id(struct bpf_prog *prog)
{
	int id;

	spin_lock_bh(&prog_idr_lock);
	id = idr_alloc_cyclic(&prog_idr, prog, 1, INT_MAX, GFP_ATOMIC);
	if (id > 0)
		prog->aux->id = id;
	spin_unlock_bh(&prog_idr_lock);

	/* id is in [1, INT_MAX) */
	if (WARN_ON_ONCE(!id))
		return -ENOSPC;

	return id > 0 ? 0 : id;
}

static void bpf_prog_free_id(struct bpf_prog *prog, bool do_idr_lock)
{
	/* cBPF to eBPF migrations are currently not in the idr store. */
	if (!prog->aux->id)
		return;

	if (do_idr_lock)
		spin_lock_bh(&prog_idr_lock);
	else
		__acquire(&prog_idr_lock);

	idr_remove(&prog_idr, prog->aux->id);

	if (do_idr_lock)
		spin_unlock_bh(&prog_idr_lock);
	else
		__release(&prog_idr_lock);
}

static void __bpf_prog_put_rcu(struct rcu_head *rcu)
{
	struct bpf_prog_aux *aux = container_of(rcu, struct bpf_prog_aux, rcu);

	free_used_maps(aux);
	bpf_prog_uncharge_memlock(aux->prog);
	bpf_prog_free(aux->prog);
}

static void __bpf_prog_put(struct bpf_prog *prog, bool do_idr_lock)
{
	if (atomic_dec_and_test(&prog->aux->refcnt)) {
		trace_bpf_prog_put_rcu(prog);
		/* bpf_prog_free_id() must be called first */
		bpf_prog_free_id(prog, do_idr_lock);
		bpf_prog_kallsyms_del(prog);
		call_rcu(&prog->aux->rcu, __bpf_prog_put_rcu);
	}
}

void bpf_prog_put(struct bpf_prog *prog)
{
	__bpf_prog_put(prog, true);
}
EXPORT_SYMBOL_GPL(bpf_prog_put);

static int bpf_prog_release(struct inode *inode, struct file *filp)
{
	struct bpf_prog *prog = filp->private_data;

	bpf_prog_put(prog);
	return 0;
}

#ifdef CONFIG_PROC_FS
static void bpf_prog_show_fdinfo(struct seq_file *m, struct file *filp)
{
	const struct bpf_prog *prog = filp->private_data;
	char prog_tag[sizeof(prog->tag) * 2 + 1] = { };

	bin2hex(prog_tag, prog->tag, sizeof(prog->tag));
	seq_printf(m,
		   "prog_type:\t%u\n"
		   "prog_jited:\t%u\n"
		   "prog_tag:\t%s\n"
		   "memlock:\t%llu\n",
		   prog->type,
		   prog->jited,
		   prog_tag,
		   prog->pages * 1ULL << PAGE_SHIFT);
}
#endif

static const struct file_operations bpf_prog_fops = {
#ifdef CONFIG_PROC_FS
	.show_fdinfo	= bpf_prog_show_fdinfo,
#endif
	.release	= bpf_prog_release,
};

int bpf_prog_new_fd(struct bpf_prog *prog)
{
	return anon_inode_getfd("bpf-prog", &bpf_prog_fops, prog,
				O_RDWR | O_CLOEXEC);
}

static struct bpf_prog *____bpf_prog_get(struct fd f)
{
	if (!f.file)
		return ERR_PTR(-EBADF);
	if (f.file->f_op != &bpf_prog_fops) {
		fdput(f);
		return ERR_PTR(-EINVAL);
	}

	return f.file->private_data;
}

struct bpf_prog *bpf_prog_add(struct bpf_prog *prog, int i)
{
	if (atomic_add_return(i, &prog->aux->refcnt) > BPF_MAX_REFCNT) {
		atomic_sub(i, &prog->aux->refcnt);
		return ERR_PTR(-EBUSY);
	}
	return prog;
}
EXPORT_SYMBOL_GPL(bpf_prog_add);

void bpf_prog_sub(struct bpf_prog *prog, int i)
{
	/* Only to be used for undoing previous bpf_prog_add() in some
	 * error path. We still know that another entity in our call
	 * path holds a reference to the program, thus atomic_sub() can
	 * be safely used in such cases!
	 */
	WARN_ON(atomic_sub_return(i, &prog->aux->refcnt) == 0);
}
EXPORT_SYMBOL_GPL(bpf_prog_sub);

struct bpf_prog *bpf_prog_inc(struct bpf_prog *prog)
{
	return bpf_prog_add(prog, 1);
}
EXPORT_SYMBOL_GPL(bpf_prog_inc);

/* prog_idr_lock should have been held */
static struct bpf_prog *bpf_prog_inc_not_zero(struct bpf_prog *prog)
{
	int refold;

	refold = __atomic_add_unless(&prog->aux->refcnt, 1, 0);

	if (refold >= BPF_MAX_REFCNT) {
		__bpf_prog_put(prog, false);
		return ERR_PTR(-EBUSY);
	}

	if (!refold)
		return ERR_PTR(-ENOENT);

	return prog;
}

static struct bpf_prog *__bpf_prog_get(u32 ufd, enum bpf_prog_type *type)
{
	struct fd f = fdget(ufd);
	struct bpf_prog *prog;

	prog = ____bpf_prog_get(f);
	if (IS_ERR(prog))
		return prog;
	if (type && prog->type != *type) {
		prog = ERR_PTR(-EINVAL);
		goto out;
	}

	prog = bpf_prog_inc(prog);
out:
	fdput(f);
	return prog;
}

struct bpf_prog *bpf_prog_get(u32 ufd)
{
	return __bpf_prog_get(ufd, NULL);
}

struct bpf_prog *bpf_prog_get_type(u32 ufd, enum bpf_prog_type type)
{
	struct bpf_prog *prog = __bpf_prog_get(ufd, &type);

	if (!IS_ERR(prog))
		trace_bpf_prog_get_type(prog);
	return prog;
}
EXPORT_SYMBOL_GPL(bpf_prog_get_type);

/* last field in 'union bpf_attr' used by this command */
#define	BPF_PROG_LOAD_LAST_FIELD prog_flags

static int bpf_prog_load(union bpf_attr *attr)
{
	enum bpf_prog_type type = attr->prog_type;
	struct bpf_prog *prog;
	int err;
	char license[128];
	bool is_gpl;

	if (CHECK_ATTR(BPF_PROG_LOAD))
		return -EINVAL;

	if (attr->prog_flags & ~BPF_F_STRICT_ALIGNMENT)
		return -EINVAL;

	/* copy eBPF program license from user space */
	if (strncpy_from_user(license, u64_to_user_ptr(attr->license),
			      sizeof(license) - 1) < 0)
		return -EFAULT;
	license[sizeof(license) - 1] = 0;

	/* eBPF programs must be GPL compatible to use GPL-ed functions */
	is_gpl = license_is_gpl_compatible(license);

	if (attr->insn_cnt == 0 || attr->insn_cnt > BPF_MAXINSNS)
		return -E2BIG;

	if (type == BPF_PROG_TYPE_KPROBE &&
	    attr->kern_version != LINUX_VERSION_CODE)
		return -EINVAL;

	if (type != BPF_PROG_TYPE_SOCKET_FILTER &&
	    type != BPF_PROG_TYPE_CGROUP_SKB &&
	    !capable(CAP_SYS_ADMIN))
		return -EPERM;

	/* plain bpf_prog allocation */
	prog = bpf_prog_alloc(bpf_prog_size(attr->insn_cnt), GFP_USER);
	if (!prog)
		return -ENOMEM;

	err = bpf_prog_charge_memlock(prog);
	if (err)
		goto free_prog_nouncharge;

	prog->len = attr->insn_cnt;

	err = -EFAULT;
	if (copy_from_user(prog->insns, u64_to_user_ptr(attr->insns),
			   bpf_prog_insn_size(prog)) != 0)
		goto free_prog;

	prog->orig_prog = NULL;
	prog->jited = 0;

	atomic_set(&prog->aux->refcnt, 1);
	prog->gpl_compatible = is_gpl ? 1 : 0;

	/* find program type: socket_filter vs tracing_filter */
	err = find_prog_type(type, prog);
	if (err < 0)
		goto free_prog;

	/* run eBPF verifier */
	err = bpf_check(&prog, attr);
	if (err < 0)
		goto free_used_maps;

	/* eBPF program is ready to be JITed */
	prog = bpf_prog_select_runtime(prog, &err);
	if (err < 0)
		goto free_used_maps;

	err = bpf_prog_alloc_id(prog);
	if (err)
		goto free_used_maps;

	err = bpf_prog_new_fd(prog);
	if (err < 0) {
		/* failed to allocate fd.
		 * bpf_prog_put() is needed because the above
		 * bpf_prog_alloc_id() has published the prog
		 * to the userspace and the userspace may
		 * have refcnt-ed it through BPF_PROG_GET_FD_BY_ID.
		 */
		bpf_prog_put(prog);
		return err;
	}

	bpf_prog_kallsyms_add(prog);
	trace_bpf_prog_load(prog, err);
	return err;

free_used_maps:
	free_used_maps(prog->aux);
free_prog:
	bpf_prog_uncharge_memlock(prog);
free_prog_nouncharge:
	bpf_prog_free(prog);
	return err;
}

#define BPF_OBJ_LAST_FIELD bpf_fd

static int bpf_obj_pin(const union bpf_attr *attr)
{
	if (CHECK_ATTR(BPF_OBJ))
		return -EINVAL;

	return bpf_obj_pin_user(attr->bpf_fd, u64_to_user_ptr(attr->pathname));
}

static int bpf_obj_get(const union bpf_attr *attr)
{
	if (CHECK_ATTR(BPF_OBJ) || attr->bpf_fd != 0)
		return -EINVAL;

	return bpf_obj_get_user(u64_to_user_ptr(attr->pathname));
}

#ifdef CONFIG_CGROUP_BPF

#define BPF_PROG_ATTACH_LAST_FIELD attach_flags

static int bpf_prog_attach(const union bpf_attr *attr)
{
	enum bpf_prog_type ptype;
	struct bpf_prog *prog;
	struct cgroup *cgrp;
	int ret;

	if (!capable(CAP_NET_ADMIN))
		return -EPERM;

	if (CHECK_ATTR(BPF_PROG_ATTACH))
		return -EINVAL;

	if (attr->attach_flags & ~BPF_F_ALLOW_OVERRIDE)
		return -EINVAL;

	switch (attr->attach_type) {
	case BPF_CGROUP_INET_INGRESS:
	case BPF_CGROUP_INET_EGRESS:
		ptype = BPF_PROG_TYPE_CGROUP_SKB;
		break;
	case BPF_CGROUP_INET_SOCK_CREATE:
		ptype = BPF_PROG_TYPE_CGROUP_SOCK;
		break;
	case BPF_CGROUP_SOCK_OPS:
		ptype = BPF_PROG_TYPE_SOCK_OPS;
		break;
	default:
		return -EINVAL;
	}

	prog = bpf_prog_get_type(attr->attach_bpf_fd, ptype);
	if (IS_ERR(prog))
		return PTR_ERR(prog);

	cgrp = cgroup_get_from_fd(attr->target_fd);
	if (IS_ERR(cgrp)) {
		bpf_prog_put(prog);
		return PTR_ERR(cgrp);
	}

	ret = cgroup_bpf_update(cgrp, prog, attr->attach_type,
				attr->attach_flags & BPF_F_ALLOW_OVERRIDE);
	if (ret)
		bpf_prog_put(prog);
	cgroup_put(cgrp);

	return ret;
}

#define BPF_PROG_DETACH_LAST_FIELD attach_type

static int bpf_prog_detach(const union bpf_attr *attr)
{
	struct cgroup *cgrp;
	int ret;

	if (!capable(CAP_NET_ADMIN))
		return -EPERM;

	if (CHECK_ATTR(BPF_PROG_DETACH))
		return -EINVAL;

	switch (attr->attach_type) {
	case BPF_CGROUP_INET_INGRESS:
	case BPF_CGROUP_INET_EGRESS:
	case BPF_CGROUP_INET_SOCK_CREATE:
	case BPF_CGROUP_SOCK_OPS:
		cgrp = cgroup_get_from_fd(attr->target_fd);
		if (IS_ERR(cgrp))
			return PTR_ERR(cgrp);

		ret = cgroup_bpf_update(cgrp, NULL, attr->attach_type, false);
		cgroup_put(cgrp);
		break;

	default:
		return -EINVAL;
	}

	return ret;
}

#endif /* CONFIG_CGROUP_BPF */

#define BPF_PROG_TEST_RUN_LAST_FIELD test.duration

static int bpf_prog_test_run(const union bpf_attr *attr,
			     union bpf_attr __user *uattr)
{
	struct bpf_prog *prog;
	int ret = -ENOTSUPP;

	if (CHECK_ATTR(BPF_PROG_TEST_RUN))
		return -EINVAL;

	prog = bpf_prog_get(attr->test.prog_fd);
	if (IS_ERR(prog))
		return PTR_ERR(prog);

	if (prog->aux->ops->test_run)
		ret = prog->aux->ops->test_run(prog, attr, uattr);

	bpf_prog_put(prog);
	return ret;
}

#define BPF_OBJ_GET_NEXT_ID_LAST_FIELD next_id

static int bpf_obj_get_next_id(const union bpf_attr *attr,
			       union bpf_attr __user *uattr,
			       struct idr *idr,
			       spinlock_t *lock)
{
	u32 next_id = attr->start_id;
	int err = 0;

	if (CHECK_ATTR(BPF_OBJ_GET_NEXT_ID) || next_id >= INT_MAX)
		return -EINVAL;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	next_id++;
	spin_lock_bh(lock);
	if (!idr_get_next(idr, &next_id))
		err = -ENOENT;
	spin_unlock_bh(lock);

	if (!err)
		err = put_user(next_id, &uattr->next_id);

	return err;
}

#define BPF_PROG_GET_FD_BY_ID_LAST_FIELD prog_id

static int bpf_prog_get_fd_by_id(const union bpf_attr *attr)
{
	struct bpf_prog *prog;
	u32 id = attr->prog_id;
	int fd;

	if (CHECK_ATTR(BPF_PROG_GET_FD_BY_ID))
		return -EINVAL;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	spin_lock_bh(&prog_idr_lock);
	prog = idr_find(&prog_idr, id);
	if (prog)
		prog = bpf_prog_inc_not_zero(prog);
	else
		prog = ERR_PTR(-ENOENT);
	spin_unlock_bh(&prog_idr_lock);

	if (IS_ERR(prog))
		return PTR_ERR(prog);

	fd = bpf_prog_new_fd(prog);
	if (fd < 0)
		bpf_prog_put(prog);

	return fd;
}

#define BPF_MAP_GET_FD_BY_ID_LAST_FIELD map_id

static int bpf_map_get_fd_by_id(const union bpf_attr *attr)
{
	struct bpf_map *map;
	u32 id = attr->map_id;
	int fd;

	if (CHECK_ATTR(BPF_MAP_GET_FD_BY_ID))
		return -EINVAL;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	spin_lock_bh(&map_idr_lock);
	map = idr_find(&map_idr, id);
	if (map)
		map = bpf_map_inc_not_zero(map, true);
	else
		map = ERR_PTR(-ENOENT);
	spin_unlock_bh(&map_idr_lock);

	if (IS_ERR(map))
		return PTR_ERR(map);

	fd = bpf_map_new_fd(map);
	if (fd < 0)
		bpf_map_put(map);

	return fd;
}

static int check_uarg_tail_zero(void __user *uaddr,
				size_t expected_size,
				size_t actual_size)
{
	unsigned char __user *addr;
	unsigned char __user *end;
	unsigned char val;
	int err;

	if (actual_size <= expected_size)
		return 0;

	addr = uaddr + expected_size;
	end  = uaddr + actual_size;

	for (; addr < end; addr++) {
		err = get_user(val, addr);
		if (err)
			return err;
		if (val)
			return -E2BIG;
	}

	return 0;
}

static int bpf_prog_get_info_by_fd(struct bpf_prog *prog,
				   const union bpf_attr *attr,
				   union bpf_attr __user *uattr)
{
	struct bpf_prog_info __user *uinfo = u64_to_user_ptr(attr->info.info);
	struct bpf_prog_info info = {};
	u32 info_len = attr->info.info_len;
	char __user *uinsns;
	u32 ulen;
	int err;

	err = check_uarg_tail_zero(uinfo, sizeof(info), info_len);
	if (err)
		return err;
	info_len = min_t(u32, sizeof(info), info_len);

	if (copy_from_user(&info, uinfo, info_len))
		return -EFAULT;

	info.type = prog->type;
	info.id = prog->aux->id;

	memcpy(info.tag, prog->tag, sizeof(prog->tag));

	if (!capable(CAP_SYS_ADMIN)) {
		info.jited_prog_len = 0;
		info.xlated_prog_len = 0;
		goto done;
	}

	ulen = info.jited_prog_len;
	info.jited_prog_len = prog->jited_len;
	if (info.jited_prog_len && ulen) {
		uinsns = u64_to_user_ptr(info.jited_prog_insns);
		ulen = min_t(u32, info.jited_prog_len, ulen);
		if (copy_to_user(uinsns, prog->bpf_func, ulen))
			return -EFAULT;
	}

	ulen = info.xlated_prog_len;
	info.xlated_prog_len = bpf_prog_insn_size(prog);
	if (info.xlated_prog_len && ulen) {
		uinsns = u64_to_user_ptr(info.xlated_prog_insns);
		ulen = min_t(u32, info.xlated_prog_len, ulen);
		if (copy_to_user(uinsns, prog->insnsi, ulen))
			return -EFAULT;
	}

done:
	if (copy_to_user(uinfo, &info, info_len) ||
	    put_user(info_len, &uattr->info.info_len))
		return -EFAULT;

	return 0;
}

static int bpf_map_get_info_by_fd(struct bpf_map *map,
				  const union bpf_attr *attr,
				  union bpf_attr __user *uattr)
{
	struct bpf_map_info __user *uinfo = u64_to_user_ptr(attr->info.info);
	struct bpf_map_info info = {};
	u32 info_len = attr->info.info_len;
	int err;

	err = check_uarg_tail_zero(uinfo, sizeof(info), info_len);
	if (err)
		return err;
	info_len = min_t(u32, sizeof(info), info_len);

	info.type = map->map_type;
	info.id = map->id;
	info.key_size = map->key_size;
	info.value_size = map->value_size;
	info.max_entries = map->max_entries;
	info.map_flags = map->map_flags;

	if (copy_to_user(uinfo, &info, info_len) ||
	    put_user(info_len, &uattr->info.info_len))
		return -EFAULT;

	return 0;
}

#define BPF_OBJ_GET_INFO_BY_FD_LAST_FIELD info.info

static int bpf_obj_get_info_by_fd(const union bpf_attr *attr,
				  union bpf_attr __user *uattr)
{
	int ufd = attr->info.bpf_fd;
	struct fd f;
	int err;

	if (CHECK_ATTR(BPF_OBJ_GET_INFO_BY_FD))
		return -EINVAL;

	f = fdget(ufd);
	if (!f.file)
		return -EBADFD;

	if (f.file->f_op == &bpf_prog_fops)
		err = bpf_prog_get_info_by_fd(f.file->private_data, attr,
					      uattr);
	else if (f.file->f_op == &bpf_map_fops)
		err = bpf_map_get_info_by_fd(f.file->private_data, attr,
					     uattr);
	else
		err = -EINVAL;

	fdput(f);
	return err;
}

SYSCALL_DEFINE3(bpf, int, cmd, union bpf_attr __user *, uattr, unsigned int, size)
{
	union bpf_attr attr = {};
	int err;

	if (!capable(CAP_SYS_ADMIN) && sysctl_unprivileged_bpf_disabled)
		return -EPERM;

	if (!access_ok(VERIFY_READ, uattr, 1))
		return -EFAULT;

	if (size > PAGE_SIZE)	/* silly large */
		return -E2BIG;

	/* If we're handed a bigger struct than we know of,
	 * ensure all the unknown bits are 0 - i.e. new
	 * user-space does not rely on any kernel feature
	 * extensions we dont know about yet.
	 */
	err = check_uarg_tail_zero(uattr, sizeof(attr), size);
	if (err)
		return err;
	size = min_t(u32, size, sizeof(attr));

	/* copy attributes from user space, may be less than sizeof(bpf_attr) */
	if (copy_from_user(&attr, uattr, size) != 0)
		return -EFAULT;

	switch (cmd) {
	case BPF_MAP_CREATE:
		err = map_create(&attr);
		break;
	case BPF_MAP_LOOKUP_ELEM:
		err = map_lookup_elem(&attr);
		break;
	case BPF_MAP_UPDATE_ELEM:
		err = map_update_elem(&attr);
		break;
	case BPF_MAP_DELETE_ELEM:
		err = map_delete_elem(&attr);
		break;
	case BPF_MAP_GET_NEXT_KEY:
		err = map_get_next_key(&attr);
		break;
	case BPF_PROG_LOAD:
		err = bpf_prog_load(&attr);
		break;
	case BPF_OBJ_PIN:
		err = bpf_obj_pin(&attr);
		break;
	case BPF_OBJ_GET:
		err = bpf_obj_get(&attr);
		break;
#ifdef CONFIG_CGROUP_BPF
	case BPF_PROG_ATTACH:
		err = bpf_prog_attach(&attr);
		break;
	case BPF_PROG_DETACH:
		err = bpf_prog_detach(&attr);
		break;
#endif
	case BPF_PROG_TEST_RUN:
		err = bpf_prog_test_run(&attr, uattr);
		break;
	case BPF_PROG_GET_NEXT_ID:
		err = bpf_obj_get_next_id(&attr, uattr,
					  &prog_idr, &prog_idr_lock);
		break;
	case BPF_MAP_GET_NEXT_ID:
		err = bpf_obj_get_next_id(&attr, uattr,
					  &map_idr, &map_idr_lock);
		break;
	case BPF_PROG_GET_FD_BY_ID:
		err = bpf_prog_get_fd_by_id(&attr);
		break;
	case BPF_MAP_GET_FD_BY_ID:
		err = bpf_map_get_fd_by_id(&attr);
		break;
	case BPF_OBJ_GET_INFO_BY_FD:
		err = bpf_obj_get_info_by_fd(&attr, uattr);
		break;
	default:
		err = -EINVAL;
		break;
	}

	return err;
}
