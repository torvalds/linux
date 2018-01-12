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

/* Called from syscall */
static struct bpf_map *array_map_alloc(union bpf_attr *attr)
{
	u32 elem_size, array_size, index_mask, max_entries;
	bool unpriv = !capable(CAP_SYS_ADMIN);
	struct bpf_array *array;
	u64 mask64;

	/* check sanity of attributes */
	if (attr->max_entries == 0 || attr->key_size != 4 ||
	    attr->value_size == 0)
		return ERR_PTR(-EINVAL);

	if (attr->value_size >= 1 << (KMALLOC_SHIFT_MAX - 1))
		/* if value_size is bigger, the user space won't be able to
		 * access the elements.
		 */
		return ERR_PTR(-E2BIG);

	elem_size = round_up(attr->value_size, 8);

	max_entries = attr->max_entries;

	/* On 32 bit archs roundup_pow_of_two() with max_entries that has
	 * upper most bit set in u32 space is undefined behavior due to
	 * resulting 1U << 32, so do it manually here in u64 space.
	 */
	mask64 = fls_long(max_entries - 1);
	mask64 = 1ULL << mask64;
	mask64 -= 1;

	index_mask = mask64;
	if (unpriv) {
		/* round up array size to nearest power of 2,
		 * since cpu will speculate within index_mask limits
		 */
		max_entries = index_mask + 1;
		/* Check for overflows. */
		if (max_entries < attr->max_entries)
			return ERR_PTR(-E2BIG);
	}

	/* check round_up into zero and u32 overflow */
	if (elem_size == 0 ||
	    max_entries > (U32_MAX - PAGE_SIZE - sizeof(*array)) / elem_size)
		return ERR_PTR(-ENOMEM);

	array_size = sizeof(*array) + max_entries * elem_size;

	/* allocate all map elements and zero-initialize them */
	array = kzalloc(array_size, GFP_USER | __GFP_NOWARN);
	if (!array) {
		array = vzalloc(array_size);
		if (!array)
			return ERR_PTR(-ENOMEM);
	}
	array->index_mask = index_mask;
	array->map.unpriv_array = unpriv;

	/* copy mandatory map attributes */
	array->map.key_size = attr->key_size;
	array->map.value_size = attr->value_size;
	array->map.max_entries = attr->max_entries;
	array->map.pages = round_up(array_size, PAGE_SIZE) >> PAGE_SHIFT;
	array->elem_size = elem_size;

	return &array->map;
}

/* Called from syscall or from eBPF program */
static void *array_map_lookup_elem(struct bpf_map *map, void *key)
{
	struct bpf_array *array = container_of(map, struct bpf_array, map);
	u32 index = *(u32 *)key;

	if (index >= array->map.max_entries)
		return NULL;

	return array->value + array->elem_size * (index & array->index_mask);
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

	if (map_flags > BPF_EXIST)
		/* unknown flags */
		return -EINVAL;

	if (index >= array->map.max_entries)
		/* all elements were pre-allocated, cannot insert a new one */
		return -E2BIG;

	if (map_flags == BPF_NOEXIST)
		/* all elements already exist */
		return -EEXIST;

	memcpy(array->value +
	       array->elem_size * (index & array->index_mask),
	       value, map->value_size);
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

static int __init register_array_map(void)
{
	bpf_register_map_type(&array_type);
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
static int fd_array_map_update_elem(struct bpf_map *map, void *key,
				    void *value, u64 map_flags)
{
	struct bpf_array *array = container_of(map, struct bpf_array, map);
	void *new_ptr, *old_ptr;
	u32 index = *(u32 *)key, ufd;

	if (map_flags != BPF_ANY)
		return -EINVAL;

	if (index >= array->map.max_entries)
		return -E2BIG;

	ufd = *(u32 *)value;
	new_ptr = map->ops->map_fd_get_ptr(map, ufd);
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

static void *prog_fd_array_get_ptr(struct bpf_map *map, int fd)
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
	struct bpf_prog *prog = ptr;

	bpf_prog_put_rcu(prog);
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
	.map_update_elem = fd_array_map_update_elem,
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

static void perf_event_array_map_free(struct bpf_map *map)
{
	bpf_fd_array_map_clear(map);
	fd_array_map_free(map);
}

static void *perf_event_fd_array_get_ptr(struct bpf_map *map, int fd)
{
	struct perf_event *event;
	const struct perf_event_attr *attr;

	event = perf_event_get(fd);
	if (IS_ERR(event))
		return event;

	attr = perf_event_attrs(event);
	if (IS_ERR(attr))
		goto err;

	if (attr->inherit)
		goto err;

	if (attr->type == PERF_TYPE_RAW)
		return event;

	if (attr->type == PERF_TYPE_HARDWARE)
		return event;

	if (attr->type == PERF_TYPE_SOFTWARE &&
	    attr->config == PERF_COUNT_SW_BPF_OUTPUT)
		return event;
err:
	perf_event_release_kernel(event);
	return ERR_PTR(-EINVAL);
}

static void perf_event_fd_array_put_ptr(void *ptr)
{
	struct perf_event *event = ptr;

	perf_event_release_kernel(event);
}

static const struct bpf_map_ops perf_event_array_ops = {
	.map_alloc = fd_array_map_alloc,
	.map_free = perf_event_array_map_free,
	.map_get_next_key = array_map_get_next_key,
	.map_lookup_elem = fd_array_map_lookup_elem,
	.map_update_elem = fd_array_map_update_elem,
	.map_delete_elem = fd_array_map_delete_elem,
	.map_fd_get_ptr = perf_event_fd_array_get_ptr,
	.map_fd_put_ptr = perf_event_fd_array_put_ptr,
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
