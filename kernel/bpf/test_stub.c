/* Copyright (c) 2011-2014 PLUMgrid, http://plumgrid.com
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 */
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/bpf.h>

/* test stubs for BPF_MAP_TYPE_UNSPEC and for BPF_PROG_TYPE_UNSPEC
 * to be used by user space verifier testsuite
 */
struct bpf_context {
	u64 arg1;
	u64 arg2;
};

static u64 test_func(u64 r1, u64 r2, u64 r3, u64 r4, u64 r5)
{
	return 0;
}

static struct bpf_func_proto test_funcs[] = {
	[BPF_FUNC_unspec] = {
		.func = test_func,
		.gpl_only = true,
		.ret_type = RET_PTR_TO_MAP_VALUE_OR_NULL,
		.arg1_type = ARG_CONST_MAP_PTR,
		.arg2_type = ARG_PTR_TO_MAP_KEY,
	},
};

static const struct bpf_func_proto *test_func_proto(enum bpf_func_id func_id)
{
	if (func_id < 0 || func_id >= ARRAY_SIZE(test_funcs))
		return NULL;
	return &test_funcs[func_id];
}

static const struct bpf_context_access {
	int size;
	enum bpf_access_type type;
} test_ctx_access[] = {
	[offsetof(struct bpf_context, arg1)] = {
		FIELD_SIZEOF(struct bpf_context, arg1),
		BPF_READ
	},
	[offsetof(struct bpf_context, arg2)] = {
		FIELD_SIZEOF(struct bpf_context, arg2),
		BPF_READ
	},
};

static bool test_is_valid_access(int off, int size, enum bpf_access_type type)
{
	const struct bpf_context_access *access;

	if (off < 0 || off >= ARRAY_SIZE(test_ctx_access))
		return false;

	access = &test_ctx_access[off];
	if (access->size == size && (access->type & type))
		return true;

	return false;
}

static struct bpf_verifier_ops test_ops = {
	.get_func_proto = test_func_proto,
	.is_valid_access = test_is_valid_access,
};

static struct bpf_prog_type_list tl_prog = {
	.ops = &test_ops,
	.type = BPF_PROG_TYPE_UNSPEC,
};

static struct bpf_map *test_map_alloc(union bpf_attr *attr)
{
	struct bpf_map *map;

	map = kzalloc(sizeof(*map), GFP_USER);
	if (!map)
		return ERR_PTR(-ENOMEM);

	map->key_size = attr->key_size;
	map->value_size = attr->value_size;
	map->max_entries = attr->max_entries;
	return map;
}

static void test_map_free(struct bpf_map *map)
{
	kfree(map);
}

static struct bpf_map_ops test_map_ops = {
	.map_alloc = test_map_alloc,
	.map_free = test_map_free,
};

static struct bpf_map_type_list tl_map = {
	.ops = &test_map_ops,
	.type = BPF_MAP_TYPE_UNSPEC,
};

static int __init register_test_ops(void)
{
	bpf_register_map_type(&tl_map);
	bpf_register_prog_type(&tl_prog);
	return 0;
}
late_initcall(register_test_ops);
