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

static const struct bpf_func_proto *test_func_proto(enum bpf_func_id func_id)
{
	switch (func_id) {
	case BPF_FUNC_map_lookup_elem:
		return &bpf_map_lookup_elem_proto;
	case BPF_FUNC_map_update_elem:
		return &bpf_map_update_elem_proto;
	case BPF_FUNC_map_delete_elem:
		return &bpf_map_delete_elem_proto;
	default:
		return NULL;
	}
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

static int __init register_test_ops(void)
{
	bpf_register_prog_type(&tl_prog);
	return 0;
}
late_initcall(register_test_ops);
