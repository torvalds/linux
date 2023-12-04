// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2017 Facebook
 */
#include <linux/slab.h>
#include <linux/bpf.h>
#include <linux/btf.h>

#include "map_in_map.h"

struct bpf_map *bpf_map_meta_alloc(int inner_map_ufd)
{
	struct bpf_map *inner_map, *inner_map_meta;
	u32 inner_map_meta_size;
	struct fd f;

	f = fdget(inner_map_ufd);
	inner_map = __bpf_map_get(f);
	if (IS_ERR(inner_map))
		return inner_map;

	/* Does not support >1 level map-in-map */
	if (inner_map->inner_map_meta) {
		fdput(f);
		return ERR_PTR(-EINVAL);
	}

	if (!inner_map->ops->map_meta_equal) {
		fdput(f);
		return ERR_PTR(-ENOTSUPP);
	}

	if (map_value_has_spin_lock(inner_map)) {
		fdput(f);
		return ERR_PTR(-ENOTSUPP);
	}

	inner_map_meta_size = sizeof(*inner_map_meta);
	/* In some cases verifier needs to access beyond just base map. */
	if (inner_map->ops == &array_map_ops)
		inner_map_meta_size = sizeof(struct bpf_array);

	inner_map_meta = kzalloc(inner_map_meta_size, GFP_USER);
	if (!inner_map_meta) {
		fdput(f);
		return ERR_PTR(-ENOMEM);
	}

	inner_map_meta->map_type = inner_map->map_type;
	inner_map_meta->key_size = inner_map->key_size;
	inner_map_meta->value_size = inner_map->value_size;
	inner_map_meta->map_flags = inner_map->map_flags;
	inner_map_meta->max_entries = inner_map->max_entries;
	inner_map_meta->spin_lock_off = inner_map->spin_lock_off;
	inner_map_meta->timer_off = inner_map->timer_off;
	inner_map_meta->kptr_off_tab = bpf_map_copy_kptr_off_tab(inner_map);
	if (inner_map->btf) {
		btf_get(inner_map->btf);
		inner_map_meta->btf = inner_map->btf;
	}

	/* Misc members not needed in bpf_map_meta_equal() check. */
	inner_map_meta->ops = inner_map->ops;
	if (inner_map->ops == &array_map_ops) {
		struct bpf_array *inner_array_meta =
			container_of(inner_map_meta, struct bpf_array, map);
		struct bpf_array *inner_array = container_of(inner_map, struct bpf_array, map);

		inner_array_meta->index_mask = inner_array->index_mask;
		inner_array_meta->elem_size = inner_array->elem_size;
		inner_map_meta->bypass_spec_v1 = inner_map->bypass_spec_v1;
	}

	fdput(f);
	return inner_map_meta;
}

void bpf_map_meta_free(struct bpf_map *map_meta)
{
	bpf_map_free_kptr_off_tab(map_meta);
	btf_put(map_meta->btf);
	kfree(map_meta);
}

bool bpf_map_meta_equal(const struct bpf_map *meta0,
			const struct bpf_map *meta1)
{
	/* No need to compare ops because it is covered by map_type */
	return meta0->map_type == meta1->map_type &&
		meta0->key_size == meta1->key_size &&
		meta0->value_size == meta1->value_size &&
		meta0->timer_off == meta1->timer_off &&
		meta0->map_flags == meta1->map_flags &&
		bpf_map_equal_kptr_off_tab(meta0, meta1);
}

void *bpf_map_fd_get_ptr(struct bpf_map *map,
			 struct file *map_file /* not used */,
			 int ufd)
{
	struct bpf_map *inner_map, *inner_map_meta;
	struct fd f;

	f = fdget(ufd);
	inner_map = __bpf_map_get(f);
	if (IS_ERR(inner_map))
		return inner_map;

	inner_map_meta = map->inner_map_meta;
	if (inner_map_meta->ops->map_meta_equal(inner_map_meta, inner_map))
		bpf_map_inc(inner_map);
	else
		inner_map = ERR_PTR(-EINVAL);

	fdput(f);
	return inner_map;
}

void bpf_map_fd_put_ptr(struct bpf_map *map, void *ptr, bool need_defer)
{
	struct bpf_map *inner_map = ptr;

	/* The inner map may still be used by both non-sleepable and sleepable
	 * bpf program, so free it after one RCU grace period and one tasks
	 * trace RCU grace period.
	 */
	if (need_defer)
		WRITE_ONCE(inner_map->free_after_mult_rcu_gp, true);
	bpf_map_put(inner_map);
}

u32 bpf_map_fd_sys_lookup_elem(void *ptr)
{
	return ((struct bpf_map *)ptr)->id;
}
