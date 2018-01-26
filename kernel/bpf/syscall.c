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
#include <linux/syscalls.h>
#include <linux/slab.h>
#include <linux/anon_inodes.h>
#include <linux/file.h>
#include <linux/license.h>
#include <linux/filter.h>
#include <linux/version.h>

int sysctl_unprivileged_bpf_disabled __read_mostly;

static LIST_HEAD(bpf_map_types);

static struct bpf_map *find_and_alloc_map(union bpf_attr *attr)
{
	struct bpf_map_type_list *tl;
	struct bpf_map *map;

	list_for_each_entry(tl, &bpf_map_types, list_node) {
		if (tl->type == attr->map_type) {
			map = tl->ops->map_alloc(attr);
			if (IS_ERR(map))
				return map;
			map->ops = tl->ops;
			map->map_type = attr->map_type;
			return map;
		}
	}
	return ERR_PTR(-EINVAL);
}

/* boot time registration of different map implementations */
void bpf_register_map_type(struct bpf_map_type_list *tl)
{
	list_add(&tl->list_node, &bpf_map_types);
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
void bpf_map_put(struct bpf_map *map)
{
	if (atomic_dec_and_test(&map->refcnt)) {
		INIT_WORK(&map->work, bpf_map_free_deferred);
		schedule_work(&map->work);
	}
}

void bpf_map_put_with_uref(struct bpf_map *map)
{
	bpf_map_put_uref(map);
	bpf_map_put(map);
}

static int bpf_map_release(struct inode *inode, struct file *filp)
{
	bpf_map_put_with_uref(filp->private_data);
	return 0;
}

static const struct file_operations bpf_map_fops = {
	.release = bpf_map_release,
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

#define BPF_MAP_CREATE_LAST_FIELD max_entries
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
		goto free_map;

	err = bpf_map_new_fd(map);
	if (err < 0)
		/* failed to allocate fd */
		goto free_map;

	return err;

free_map:
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

/* helper to convert user pointers passed inside __aligned_u64 fields */
static void __user *u64_to_ptr(__u64 val)
{
	return (void __user *) (unsigned long) val;
}

/* last field in 'union bpf_attr' used by this command */
#define BPF_MAP_LOOKUP_ELEM_LAST_FIELD value

static int map_lookup_elem(union bpf_attr *attr)
{
	void __user *ukey = u64_to_ptr(attr->key);
	void __user *uvalue = u64_to_ptr(attr->value);
	int ufd = attr->map_fd;
	struct bpf_map *map;
	void *key, *value, *ptr;
	struct fd f;
	int err;

	if (CHECK_ATTR(BPF_MAP_LOOKUP_ELEM))
		return -EINVAL;

	f = fdget(ufd);
	map = __bpf_map_get(f);
	if (IS_ERR(map))
		return PTR_ERR(map);

	err = -ENOMEM;
	key = kmalloc(map->key_size, GFP_USER);
	if (!key)
		goto err_put;

	err = -EFAULT;
	if (copy_from_user(key, ukey, map->key_size) != 0)
		goto free_key;

	err = -ENOMEM;
	value = kmalloc(map->value_size, GFP_USER | __GFP_NOWARN);
	if (!value)
		goto free_key;

	rcu_read_lock();
	ptr = map->ops->map_lookup_elem(map, key);
	if (ptr)
		memcpy(value, ptr, map->value_size);
	rcu_read_unlock();

	err = -ENOENT;
	if (!ptr)
		goto free_value;

	err = -EFAULT;
	if (copy_to_user(uvalue, value, map->value_size) != 0)
		goto free_value;

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
	void __user *ukey = u64_to_ptr(attr->key);
	void __user *uvalue = u64_to_ptr(attr->value);
	int ufd = attr->map_fd;
	struct bpf_map *map;
	void *key, *value;
	struct fd f;
	int err;

	if (CHECK_ATTR(BPF_MAP_UPDATE_ELEM))
		return -EINVAL;

	f = fdget(ufd);
	map = __bpf_map_get(f);
	if (IS_ERR(map))
		return PTR_ERR(map);

	err = -ENOMEM;
	key = kmalloc(map->key_size, GFP_USER);
	if (!key)
		goto err_put;

	err = -EFAULT;
	if (copy_from_user(key, ukey, map->key_size) != 0)
		goto free_key;

	err = -ENOMEM;
	value = kmalloc(map->value_size, GFP_USER | __GFP_NOWARN);
	if (!value)
		goto free_key;

	err = -EFAULT;
	if (copy_from_user(value, uvalue, map->value_size) != 0)
		goto free_value;

	/* eBPF program that use maps are running under rcu_read_lock(),
	 * therefore all map accessors rely on this fact, so do the same here
	 */
	rcu_read_lock();
	err = map->ops->map_update_elem(map, key, value, attr->flags);
	rcu_read_unlock();

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
	void __user *ukey = u64_to_ptr(attr->key);
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

	err = -ENOMEM;
	key = kmalloc(map->key_size, GFP_USER);
	if (!key)
		goto err_put;

	err = -EFAULT;
	if (copy_from_user(key, ukey, map->key_size) != 0)
		goto free_key;

	rcu_read_lock();
	err = map->ops->map_delete_elem(map, key);
	rcu_read_unlock();

free_key:
	kfree(key);
err_put:
	fdput(f);
	return err;
}

/* last field in 'union bpf_attr' used by this command */
#define BPF_MAP_GET_NEXT_KEY_LAST_FIELD next_key

static int map_get_next_key(union bpf_attr *attr)
{
	void __user *ukey = u64_to_ptr(attr->key);
	void __user *unext_key = u64_to_ptr(attr->next_key);
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

	err = -ENOMEM;
	key = kmalloc(map->key_size, GFP_USER);
	if (!key)
		goto err_put;

	err = -EFAULT;
	if (copy_from_user(key, ukey, map->key_size) != 0)
		goto free_key;

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

	err = 0;

free_next_key:
	kfree(next_key);
free_key:
	kfree(key);
err_put:
	fdput(f);
	return err;
}

static LIST_HEAD(bpf_prog_types);

static int find_prog_type(enum bpf_prog_type type, struct bpf_prog *prog)
{
	struct bpf_prog_type_list *tl;

	list_for_each_entry(tl, &bpf_prog_types, list_node) {
		if (tl->type == type) {
			prog->aux->ops = tl->ops;
			prog->type = type;
			return 0;
		}
	}

	return -EINVAL;
}

void bpf_register_prog_type(struct bpf_prog_type_list *tl)
{
	list_add(&tl->list_node, &bpf_prog_types);
}

/* drop refcnt on maps used by eBPF program and free auxilary data */
static void free_used_maps(struct bpf_prog_aux *aux)
{
	int i;

	for (i = 0; i < aux->used_map_cnt; i++)
		bpf_map_put(aux->used_maps[i]);

	kfree(aux->used_maps);
}

static int bpf_prog_charge_memlock(struct bpf_prog *prog)
{
	struct user_struct *user = get_current_user();
	unsigned long memlock_limit;

	memlock_limit = rlimit(RLIMIT_MEMLOCK) >> PAGE_SHIFT;

	atomic_long_add(prog->pages, &user->locked_vm);
	if (atomic_long_read(&user->locked_vm) > memlock_limit) {
		atomic_long_sub(prog->pages, &user->locked_vm);
		free_uid(user);
		return -EPERM;
	}
	prog->aux->user = user;
	return 0;
}

static void bpf_prog_uncharge_memlock(struct bpf_prog *prog)
{
	struct user_struct *user = prog->aux->user;

	atomic_long_sub(prog->pages, &user->locked_vm);
	free_uid(user);
}

static void __prog_put_common(struct rcu_head *rcu)
{
	struct bpf_prog_aux *aux = container_of(rcu, struct bpf_prog_aux, rcu);

	free_used_maps(aux);
	bpf_prog_uncharge_memlock(aux->prog);
	bpf_prog_free(aux->prog);
}

/* version of bpf_prog_put() that is called after a grace period */
void bpf_prog_put_rcu(struct bpf_prog *prog)
{
	if (atomic_dec_and_test(&prog->aux->refcnt))
		call_rcu(&prog->aux->rcu, __prog_put_common);
}

void bpf_prog_put(struct bpf_prog *prog)
{
	if (atomic_dec_and_test(&prog->aux->refcnt))
		__prog_put_common(&prog->aux->rcu);
}
EXPORT_SYMBOL_GPL(bpf_prog_put);

static int bpf_prog_release(struct inode *inode, struct file *filp)
{
	struct bpf_prog *prog = filp->private_data;

	bpf_prog_put_rcu(prog);
	return 0;
}

static const struct file_operations bpf_prog_fops = {
        .release = bpf_prog_release,
};

int bpf_prog_new_fd(struct bpf_prog *prog)
{
	return anon_inode_getfd("bpf-prog", &bpf_prog_fops, prog,
				O_RDWR | O_CLOEXEC);
}

static struct bpf_prog *__bpf_prog_get(struct fd f)
{
	if (!f.file)
		return ERR_PTR(-EBADF);
	if (f.file->f_op != &bpf_prog_fops) {
		fdput(f);
		return ERR_PTR(-EINVAL);
	}

	return f.file->private_data;
}

struct bpf_prog *bpf_prog_inc(struct bpf_prog *prog)
{
	if (atomic_inc_return(&prog->aux->refcnt) > BPF_MAX_REFCNT) {
		atomic_dec(&prog->aux->refcnt);
		return ERR_PTR(-EBUSY);
	}
	return prog;
}

/* called by sockets/tracing/seccomp before attaching program to an event
 * pairs with bpf_prog_put()
 */
struct bpf_prog *bpf_prog_get(u32 ufd)
{
	struct fd f = fdget(ufd);
	struct bpf_prog *prog;

	prog = __bpf_prog_get(f);
	if (IS_ERR(prog))
		return prog;

	prog = bpf_prog_inc(prog);
	fdput(f);

	return prog;
}
EXPORT_SYMBOL_GPL(bpf_prog_get);

/* last field in 'union bpf_attr' used by this command */
#define	BPF_PROG_LOAD_LAST_FIELD kern_version

static int bpf_prog_load(union bpf_attr *attr)
{
	enum bpf_prog_type type = attr->prog_type;
	struct bpf_prog *prog;
	int err;
	char license[128];
	bool is_gpl;

	if (CHECK_ATTR(BPF_PROG_LOAD))
		return -EINVAL;

	/* copy eBPF program license from user space */
	if (strncpy_from_user(license, u64_to_ptr(attr->license),
			      sizeof(license) - 1) < 0)
		return -EFAULT;
	license[sizeof(license) - 1] = 0;

	/* eBPF programs must be GPL compatible to use GPL-ed functions */
	is_gpl = license_is_gpl_compatible(license);

	if (attr->insn_cnt >= BPF_MAXINSNS)
		return -EINVAL;

	if (type == BPF_PROG_TYPE_KPROBE &&
	    attr->kern_version != LINUX_VERSION_CODE)
		return -EINVAL;

	if (type != BPF_PROG_TYPE_SOCKET_FILTER && !capable(CAP_SYS_ADMIN))
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
	if (copy_from_user(prog->insns, u64_to_ptr(attr->insns),
			   prog->len * sizeof(struct bpf_insn)) != 0)
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
	err = bpf_prog_select_runtime(prog);
	if (err < 0)
		goto free_used_maps;

	err = bpf_prog_new_fd(prog);
	if (err < 0)
		/* failed to allocate fd */
		goto free_used_maps;

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

	return bpf_obj_pin_user(attr->bpf_fd, u64_to_ptr(attr->pathname));
}

static int bpf_obj_get(const union bpf_attr *attr)
{
	if (CHECK_ATTR(BPF_OBJ) || attr->bpf_fd != 0)
		return -EINVAL;

	return bpf_obj_get_user(u64_to_ptr(attr->pathname));
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
	if (size > sizeof(attr)) {
		unsigned char __user *addr;
		unsigned char __user *end;
		unsigned char val;

		addr = (void __user *)uattr + sizeof(attr);
		end  = (void __user *)uattr + size;

		for (; addr < end; addr++) {
			err = get_user(val, addr);
			if (err)
				return err;
			if (val)
				return -E2BIG;
		}
		size = sizeof(attr);
	}

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
	default:
		err = -EINVAL;
		break;
	}

	return err;
}
