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

/* Called from syscall */
static struct bpf_map *array_map_alloc(union bpf_attr *attr)
{
	struct bpf_array *array;
	u32 elem_size, array_size;

	/* check sanity of attributes */
	if (attr->max_entries == 0 || attr->key_size != 4 ||
	    attr->value_size == 0)
		return ERR_PTR(-EINVAL);

	elem_size = round_up(attr->value_size, 8);

	/* check round_up into zero and u32 overflow */
	if (elem_size == 0 ||
	    attr->max_entries > (U32_MAX - sizeof(*array)) / elem_size)
		return ERR_PTR(-ENOMEM);

	array_size = sizeof(*array) + attr->max_entries * elem_size;

	/* allocate all map elements and zero-initialize them */
	array = kzalloc(array_size, GFP_USER | __GFP_NOWARN);
	if (!array) {
		array = vzalloc(array_size);
		if (!array)
			return ERR_PTR(-ENOMEM);
	}

	/* copy mandatory map attributes */
	array->map.key_size = attr->key_size;
	array->map.value_size = attr->value_size;
	array->map.max_entries = attr->max_entries;

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

	return array->value + array->elem_size * index;
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

	memcpy(array->value + array->elem_size * index, value, array->elem_size);
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

static struct bpf_map *prog_array_map_alloc(union bpf_attr *attr)
{
	/* only bpf_prog file descriptors can be stored in prog_array map */
	if (attr->value_size != sizeof(u32))
		return ERR_PTR(-EINVAL);
	return array_map_alloc(attr);
}

static void prog_array_map_free(struct bpf_map *map)
{
	struct bpf_array *array = container_of(map, struct bpf_array, map);
	int i;

	synchronize_rcu();

	/* make sure it's empty */
	for (i = 0; i < array->map.max_entries; i++)
		BUG_ON(array->prog[i] != NULL);
	kvfree(array);
}

static void *prog_array_map_lookup_elem(struct bpf_map *map, void *key)
{
	return NULL;
}

/* only called from syscall */
static int prog_array_map_update_elem(struct bpf_map *map, void *key,
				      void *value, u64 map_flags)
{
	struct bpf_array *array = container_of(map, struct bpf_array, map);
	struct bpf_prog *prog, *old_prog;
	u32 index = *(u32 *)key, ufd;

	if (map_flags != BPF_ANY)
		return -EINVAL;

	if (index >= array->map.max_entries)
		return -E2BIG;

	ufd = *(u32 *)value;
	prog = bpf_prog_get(ufd);
	if (IS_ERR(prog))
		return PTR_ERR(prog);

	if (!bpf_prog_array_compatible(array, prog)) {
		bpf_prog_put(prog);
		return -EINVAL;
	}

	old_prog = xchg(array->prog + index, prog);
	if (old_prog)
		bpf_prog_put(old_prog);

	return 0;
}

static int prog_array_map_delete_elem(struct bpf_map *map, void *key)
{
	struct bpf_array *array = container_of(map, struct bpf_array, map);
	struct bpf_prog *old_prog;
	u32 index = *(u32 *)key;

	if (index >= array->map.max_entries)
		return -E2BIG;

	old_prog = xchg(array->prog + index, NULL);
	if (old_prog) {
		bpf_prog_put(old_prog);
		return 0;
	} else {
		return -ENOENT;
	}
}

/* decrement refcnt of all bpf_progs that are stored in this map */
void bpf_prog_array_map_clear(struct bpf_map *map)
{
	struct bpf_array *array = container_of(map, struct bpf_array, map);
	int i;

	for (i = 0; i < array->map.max_entries; i++)
		prog_array_map_delete_elem(map, &i);
}

static const struct bpf_map_ops prog_array_ops = {
	.map_alloc = prog_array_map_alloc,
	.map_free = prog_array_map_free,
	.map_get_next_key = array_map_get_next_key,
	.map_lookup_elem = prog_array_map_lookup_elem,
	.map_update_elem = prog_array_map_update_elem,
	.map_delete_elem = prog_array_map_delete_elem,
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
