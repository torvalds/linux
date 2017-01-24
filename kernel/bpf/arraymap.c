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
#include <linux/err.h>
#include <linux/vmalloc.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/filter.h>
#include <linux/perf_event.h>

static void bpf_array_free_percpu(struct bpf_array *array)
{
	int i;

	for (i = 0; i < array->map.max_entries; i++)
		free_percpu(array->pptrs[i]);
}

static int bpf_array_alloc_percpu(struct bpf_array *array)
{
	void __percpu *ptr;
	int i;

	for (i = 0; i < array->map.max_entries; i++) {
		ptr = __alloc_percpu_gfp(array->elem_size, 8,
					 GFP_USER | __GFP_NOWARN);
		if (!ptr) {
			bpf_array_free_percpu(array);
			return -ENOMEM;
		}
		array->pptrs[i] = ptr;
	}

	return 0;
}

/* Called from syscall */
static struct bpf_map *array_map_alloc(union bpf_attr *attr)
{
	bool percpu = attr->map_type == BPF_MAP_TYPE_PERCPU_ARRAY;
	struct bpf_array *array;
	u64 array_size;
	u32 elem_size;

	/* check sanity of attributes */
	if (attr->max_entries == 0 || attr->key_size != 4 ||
	    attr->value_size == 0 || attr->map_flags)
		return ERR_PTR(-EINVAL);

	if (attr->value_size > KMALLOC_MAX_SIZE)
		/* if value_size is bigger, the user space won't be able to
		 * access the elements.
		 */
		return ERR_PTR(-E2BIG);

	elem_size = round_up(attr->value_size, 8);

	array_size = sizeof(*array);
	if (percpu)
		array_size += (u64) attr->max_entries * sizeof(void *);
	else
		array_size += (u64) attr->max_entries * elem_size;

	/* make sure there is no u32 overflow later in round_up() */
	if (array_size >= U32_MAX - PAGE_SIZE)
		return ERR_PTR(-ENOMEM);


	/* allocate all map elements and zero-initialize them */
	array = kzalloc(array_size, GFP_USER | __GFP_NOWARN);
	if (!array) {
		array = vzalloc(array_size);
		if (!array)
			return ERR_PTR(-ENOMEM);
	}

	/* copy mandatory map attributes */
	array->map.map_type = attr->map_type;
	array->map.key_size = attr->key_size;
	array->map.value_size = attr->value_size;
	array->map.max_entries = attr->max_entries;
	array->elem_size = elem_size;

	if (!percpu)
		goto out;

	array_size += (u64) attr->max_entries * elem_size * num_possible_cpus();

	if (array_size >= U32_MAX - PAGE_SIZE ||
	    elem_size > PCPU_MIN_UNIT_SIZE || bpf_array_alloc_percpu(array)) {
		kvfree(array);
		return ERR_PTR(-ENOMEM);
	}
out:
	array->map.pages = round_up(array_size, PAGE_SIZE) >> PAGE_SHIFT;

	return &array->map;
}

/* Called from syscall or from eBPF program */
static void *array_map_lookup_elem(struct bpf_map *map, void *key)
{
	struct bpf_array *array = container_of(map, struct bpf_array, map);
	u32 index = *(u32 *)key;

	if (unlikely(index >= array->map.max_entries))
		return NULL;

	return array->value + array->elem_size * index;
}

/* Called from eBPF program */
static void *percpu_array_map_lookup_elem(struct bpf_map *map, void *key)
{
	struct bpf_array *array = container_of(map, struct bpf_array, map);
	u32 index = *(u32 *)key;

	if (unlikely(index >= array->map.max_entries))
		return NULL;

	return this_cpu_ptr(array->pptrs[index]);
}

int bpf_percpu_array_copy(struct bpf_map *map, void *key, void *value)
{
	struct bpf_array *array = container_of(map, struct bpf_array, map);
	u32 index = *(u32 *)key;
	void __percpu *pptr;
	int cpu, off = 0;
	u32 size;

	if (unlikely(index >= array->map.max_entries))
		return -ENOENT;

	/* per_cpu areas are zero-filled and bpf programs can only
	 * access 'value_size' of them, so copying rounded areas
	 * will not leak any kernel data
	 */
	size = round_up(map->value_size, 8);
	rcu_read_lock();
	pptr = array->pptrs[index];
	for_each_possible_cpu(cpu) {
		bpf_long_memcpy(value + off, per_cpu_ptr(pptr, cpu), size);
		off += size;
	}
	rcu_read_unlock();
	return 0;
}

/* Called from syscall */
static int array_map_get_next_key(struct bpf_map *map, void *key, void *next_key)
{
	struct bpf_array *array = container_of(map, struct bpf_array, map);
	u32 index = *(u32 *)key;
	u32 *next = (u32 *)next_key;

	if (index >= array->map.max_entries) {
		*next = 0;
		return 0;
	}

	if (index == array->map.max_entries - 1)
		return -ENOENT;

	*next = index + 1;
	return 0;
}

/* Called from syscall or from eBPF program */
static int array_map_update_elem(struct bpf_map *map, void *key, void *value,
				 u64 map_flags)
{
	struct bpf_array *array = container_of(map, struct bpf_array, map);
	u32 index = *(u32 *)key;

	if (unlikely(map_flags > BPF_EXIST))
		/* unknown flags */
		return -EINVAL;

	if (unlikely(index >= array->map.max_entries))
		/* all elements were pre-allocated, cannot insert a new one */
		return -E2BIG;

	if (unlikely(map_flags == BPF_NOEXIST))
		/* all elements already exist */
		return -EEXIST;

	if (array->map.map_type == BPF_MAP_TYPE_PERCPU_ARRAY)
		memcpy(this_cpu_ptr(array->pptrs[index]),
		       value, map->value_size);
	else
		memcpy(array->value + array->elem_size * index,
		       value, map->value_size);
	return 0;
}

int bpf_percpu_array_update(struct bpf_map *map, void *key, void *value,
			    u64 map_flags)
{
	struct bpf_array *array = container_of(map, struct bpf_array, map);
	u32 index = *(u32 *)key;
	void __percpu *pptr;
	int cpu, off = 0;
	u32 size;

	if (unlikely(map_flags > BPF_EXIST))
		/* unknown flags */
		return -EINVAL;

	if (unlikely(index >= array->map.max_entries))
		/* all elements were pre-allocated, cannot insert a new one */
		return -E2BIG;

	if (unlikely(map_flags == BPF_NOEXIST))
		/* all elements already exist */
		return -EEXIST;

	/* the user space will provide round_up(value_size, 8) bytes that
	 * will be copied into per-cpu area. bpf programs can only access
	 * value_size of it. During lookup the same extra bytes will be
	 * returned or zeros which were zero-filled by percpu_alloc,
	 * so no kernel data leaks possible
	 */
	size = round_up(map->value_size, 8);
	rcu_read_lock();
	pptr = array->pptrs[index];
	for_each_possible_cpu(cpu) {
		bpf_long_memcpy(per_cpu_ptr(pptr, cpu), value + off, size);
		off += size;
	}
	rcu_read_unlock();
	return 0;
}

/* Called from syscall or from eBPF program */
static int array_map_delete_elem(struct bpf_map *map, void *key)
{
	return -EINVAL;
}

/* Called when map->refcnt goes to zero, either from workqueue or from syscall */
static void array_map_free(struct bpf_map *map)
{
	struct bpf_array *array = container_of(map, struct bpf_array, map);

	/* at this point bpf_prog->aux->refcnt == 0 and this map->refcnt == 0,
	 * so the programs (can be more than one that used this map) were
	 * disconnected from events. Wait for outstanding programs to complete
	 * and free the array
	 */
	synchronize_rcu();

	if (array->map.map_type == BPF_MAP_TYPE_PERCPU_ARRAY)
		bpf_array_free_percpu(array);

	kvfree(array);
}

static const struct bpf_map_ops array_ops = {
	.map_alloc = array_map_alloc,
	.map_free = array_map_free,
	.map_get_next_key = array_map_get_next_key,
	.map_lookup_elem = array_map_lookup_elem,
	.map_update_elem = array_map_update_elem,
	.map_delete_elem = array_map_delete_elem,
};

static struct bpf_map_type_list array_type __read_mostly = {
	.ops = &array_ops,
	.type = BPF_MAP_TYPE_ARRAY,
};

static const struct bpf_map_ops percpu_array_ops = {
	.map_alloc = array_map_alloc,
	.map_free = array_map_free,
	.map_get_next_key = array_map_get_next_key,
	.map_lookup_elem = percpu_array_map_lookup_elem,
	.map_update_elem = array_map_update_elem,
	.map_delete_elem = array_map_delete_elem,
};

static struct bpf_map_type_list percpu_array_type __read_mostly = {
	.ops = &percpu_array_ops,
	.type = BPF_MAP_TYPE_PERCPU_ARRAY,
};

static int __init register_array_map(void)
{
	bpf_register_map_type(&array_type);
	bpf_register_map_type(&percpu_array_type);
	return 0;
}
late_initcall(register_array_map);

static struct bpf_map *fd_array_map_alloc(union bpf_attr *attr)
{
	/* only file descriptors can be stored in this type of map */
	if (attr->value_size != sizeof(u32))
		return ERR_PTR(-EINVAL);
	return array_map_alloc(attr);
}

static void fd_array_map_free(struct bpf_map *map)
{
	struct bpf_array *array = container_of(map, struct bpf_array, map);
	int i;

	synchronize_rcu();

	/* make sure it's empty */
	for (i = 0; i < array->map.max_entries; i++)
		BUG_ON(array->ptrs[i] != NULL);
	kvfree(array);
}

static void *fd_array_map_lookup_elem(struct bpf_map *map, void *key)
{
	return NULL;
}

/* only called from syscall */
int bpf_fd_array_map_update_elem(struct bpf_map *map, struct file *map_file,
				 void *key, void *value, u64 map_flags)
{
	struct bpf_array *array = container_of(map, struct bpf_array, map);
	void *new_ptr, *old_ptr;
	u32 index = *(u32 *)key, ufd;

	if (map_flags != BPF_ANY)
		return -EINVAL;

	if (index >= array->map.max_entries)
		return -E2BIG;

	ufd = *(u32 *)value;
	new_ptr = map->ops->map_fd_get_ptr(map, map_file, ufd);
	if (IS_ERR(new_ptr))
		return PTR_ERR(new_ptr);

	old_ptr = xchg(array->ptrs + index, new_ptr);
	if (old_ptr)
		map->ops->map_fd_put_ptr(old_ptr);

	return 0;
}

static int fd_array_map_delete_elem(struct bpf_map *map, void *key)
{
	struct bpf_array *array = container_of(map, struct bpf_array, map);
	void *old_ptr;
	u32 index = *(u32 *)key;

	if (index >= array->map.max_entries)
		return -E2BIG;

	old_ptr = xchg(array->ptrs + index, NULL);
	if (old_ptr) {
		map->ops->map_fd_put_ptr(old_ptr);
		return 0;
	} else {
		return -ENOENT;
	}
}

static void *prog_fd_array_get_ptr(struct bpf_map *map,
				   struct file *map_file, int fd)
{
	struct bpf_array *array = container_of(map, struct bpf_array, map);
	struct bpf_prog *prog = bpf_prog_get(fd);

	if (IS_ERR(prog))
		return prog;

	if (!bpf_prog_array_compatible(array, prog)) {
		bpf_prog_put(prog);
		return ERR_PTR(-EINVAL);
	}

	return prog;
}

static void prog_fd_array_put_ptr(void *ptr)
{
	bpf_prog_put(ptr);
}

/* decrement refcnt of all bpf_progs that are stored in this map */
void bpf_fd_array_map_clear(struct bpf_map *map)
{
	struct bpf_array *array = container_of(map, struct bpf_array, map);
	int i;

	for (i = 0; i < array->map.max_entries; i++)
		fd_array_map_delete_elem(map, &i);
}

static const struct bpf_map_ops prog_array_ops = {
	.map_alloc = fd_array_map_alloc,
	.map_free = fd_array_map_free,
	.map_get_next_key = array_map_get_next_key,
	.map_lookup_elem = fd_array_map_lookup_elem,
	.map_delete_elem = fd_array_map_delete_elem,
	.map_fd_get_ptr = prog_fd_array_get_ptr,
	.map_fd_put_ptr = prog_fd_array_put_ptr,
};

static struct bpf_map_type_list prog_array_type __read_mostly = {
	.ops = &prog_array_ops,
	.type = BPF_MAP_TYPE_PROG_ARRAY,
};

static int __init register_prog_array_map(void)
{
	bpf_register_map_type(&prog_array_type);
	return 0;
}
late_initcall(register_prog_array_map);

static struct bpf_event_entry *bpf_event_entry_gen(struct file *perf_file,
						   struct file *map_file)
{
	struct bpf_event_entry *ee;

	ee = kzalloc(sizeof(*ee), GFP_ATOMIC);
	if (ee) {
		ee->event = perf_file->private_data;
		ee->perf_file = perf_file;
		ee->map_file = map_file;
	}

	return ee;
}

static void __bpf_event_entry_free(struct rcu_head *rcu)
{
	struct bpf_event_entry *ee;

	ee = container_of(rcu, struct bpf_event_entry, rcu);
	fput(ee->perf_file);
	kfree(ee);
}

static void bpf_event_entry_free_rcu(struct bpf_event_entry *ee)
{
	call_rcu(&ee->rcu, __bpf_event_entry_free);
}

static void *perf_event_fd_array_get_ptr(struct bpf_map *map,
					 struct file *map_file, int fd)
{
	const struct perf_event_attr *attr;
	struct bpf_event_entry *ee;
	struct perf_event *event;
	struct file *perf_file;

	perf_file = perf_event_get(fd);
	if (IS_ERR(perf_file))
		return perf_file;

	event = perf_file->private_data;
	ee = ERR_PTR(-EINVAL);

	attr = perf_event_attrs(event);
	if (IS_ERR(attr) || attr->inherit)
		goto err_out;

	switch (attr->type) {
	case PERF_TYPE_SOFTWARE:
		if (attr->config != PERF_COUNT_SW_BPF_OUTPUT)
			goto err_out;
		/* fall-through */
	case PERF_TYPE_RAW:
	case PERF_TYPE_HARDWARE:
		ee = bpf_event_entry_gen(perf_file, map_file);
		if (ee)
			return ee;
		ee = ERR_PTR(-ENOMEM);
		/* fall-through */
	default:
		break;
	}

err_out:
	fput(perf_file);
	return ee;
}

static void perf_event_fd_array_put_ptr(void *ptr)
{
	bpf_event_entry_free_rcu(ptr);
}

static void perf_event_fd_array_release(struct bpf_map *map,
					struct file *map_file)
{
	struct bpf_array *array = container_of(map, struct bpf_array, map);
	struct bpf_event_entry *ee;
	int i;

	rcu_read_lock();
	for (i = 0; i < array->map.max_entries; i++) {
		ee = READ_ONCE(array->ptrs[i]);
		if (ee && ee->map_file == map_file)
			fd_array_map_delete_elem(map, &i);
	}
	rcu_read_unlock();
}

static const struct bpf_map_ops perf_event_array_ops = {
	.map_alloc = fd_array_map_alloc,
	.map_free = fd_array_map_free,
	.map_get_next_key = array_map_get_next_key,
	.map_lookup_elem = fd_array_map_lookup_elem,
	.map_delete_elem = fd_array_map_delete_elem,
	.map_fd_get_ptr = perf_event_fd_array_get_ptr,
	.map_fd_put_ptr = perf_event_fd_array_put_ptr,
	.map_release = perf_event_fd_array_release,
};

static struct bpf_map_type_list perf_event_array_type __read_mostly = {
	.ops = &perf_event_array_ops,
	.type = BPF_MAP_TYPE_PERF_EVENT_ARRAY,
};

static int __init register_perf_event_array_map(void)
{
	bpf_register_map_type(&perf_event_array_type);
	return 0;
}
late_initcall(register_perf_event_array_map);

#ifdef CONFIG_CGROUPS
static void *cgroup_fd_array_get_ptr(struct bpf_map *map,
				     struct file *map_file /* not used */,
				     int fd)
{
	return cgroup_get_from_fd(fd);
}

static void cgroup_fd_array_put_ptr(void *ptr)
{
	/* cgroup_put free cgrp after a rcu grace period */
	cgroup_put(ptr);
}

static void cgroup_fd_array_free(struct bpf_map *map)
{
	bpf_fd_array_map_clear(map);
	fd_array_map_free(map);
}

static const struct bpf_map_ops cgroup_array_ops = {
	.map_alloc = fd_array_map_alloc,
	.map_free = cgroup_fd_array_free,
	.map_get_next_key = array_map_get_next_key,
	.map_lookup_elem = fd_array_map_lookup_elem,
	.map_delete_elem = fd_array_map_delete_elem,
	.map_fd_get_ptr = cgroup_fd_array_get_ptr,
	.map_fd_put_ptr = cgroup_fd_array_put_ptr,
};

static struct bpf_map_type_list cgroup_array_type __read_mostly = {
	.ops = &cgroup_array_ops,
	.type = BPF_MAP_TYPE_CGROUP_ARRAY,
};

static int __init register_cgroup_array_map(void)
{
	bpf_register_map_type(&cgroup_array_type);
	return 0;
}
late_initcall(register_cgroup_array_map);
#endif
