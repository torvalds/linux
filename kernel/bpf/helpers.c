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
#include <linux/rcupdate.h>

/* If kernel subsystem is allowing eBPF programs to call this function,
 * inside its own verifier_ops->get_func_proto() callback it should return
 * bpf_map_lookup_elem_proto, so that verifier can properly check the arguments
 *
 * Different map implementations will rely on rcu in map methods
 * lookup/update/delete, therefore eBPF programs must run under rcu lock
 * if program is allowed to access maps, so check rcu_read_lock_held in
 * all three functions.
 */
static u64 bpf_map_lookup_elem(u64 r1, u64 r2, u64 r3, u64 r4, u64 r5)
{
	/* verifier checked that R1 contains a valid pointer to bpf_map
	 * and R2 points to a program stack and map->key_size bytes were
	 * initialized
	 */
	struct bpf_map *map = (struct bpf_map *) (unsigned long) r1;
	void *key = (void *) (unsigned long) r2;
	void *value;

	WARN_ON_ONCE(!rcu_read_lock_held());

	value = map->ops->map_lookup_elem(map, key);

	/* lookup() returns either pointer to element value or NULL
	 * which is the meaning of PTR_TO_MAP_VALUE_OR_NULL type
	 */
	return (unsigned long) value;
}

struct bpf_func_proto bpf_map_lookup_elem_proto = {
	.func = bpf_map_lookup_elem,
	.gpl_only = false,
	.ret_type = RET_PTR_TO_MAP_VALUE_OR_NULL,
	.arg1_type = ARG_CONST_MAP_PTR,
	.arg2_type = ARG_PTR_TO_MAP_KEY,
};

static u64 bpf_map_update_elem(u64 r1, u64 r2, u64 r3, u64 r4, u64 r5)
{
	struct bpf_map *map = (struct bpf_map *) (unsigned long) r1;
	void *key = (void *) (unsigned long) r2;
	void *value = (void *) (unsigned long) r3;

	WARN_ON_ONCE(!rcu_read_lock_held());

	return map->ops->map_update_elem(map, key, value, r4);
}

struct bpf_func_proto bpf_map_update_elem_proto = {
	.func = bpf_map_update_elem,
	.gpl_only = false,
	.ret_type = RET_INTEGER,
	.arg1_type = ARG_CONST_MAP_PTR,
	.arg2_type = ARG_PTR_TO_MAP_KEY,
	.arg3_type = ARG_PTR_TO_MAP_VALUE,
	.arg4_type = ARG_ANYTHING,
};

static u64 bpf_map_delete_elem(u64 r1, u64 r2, u64 r3, u64 r4, u64 r5)
{
	struct bpf_map *map = (struct bpf_map *) (unsigned long) r1;
	void *key = (void *) (unsigned long) r2;

	WARN_ON_ONCE(!rcu_read_lock_held());

	return map->ops->map_delete_elem(map, key);
}

struct bpf_func_proto bpf_map_delete_elem_proto = {
	.func = bpf_map_delete_elem,
	.gpl_only = false,
	.ret_type = RET_INTEGER,
	.arg1_type = ARG_CONST_MAP_PTR,
	.arg2_type = ARG_PTR_TO_MAP_KEY,
};
