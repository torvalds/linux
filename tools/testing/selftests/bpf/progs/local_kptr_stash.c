// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2023 Meta Platforms, Inc. and affiliates. */

#include <vmlinux.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_core_read.h>
#include "../bpf_experimental.h"
#include "../bpf_testmod/bpf_testmod_kfunc.h"

struct node_data {
	long key;
	long data;
	struct bpf_rb_node node;
};

struct map_value {
	struct prog_test_ref_kfunc *not_kptr;
	struct prog_test_ref_kfunc __kptr *val;
	struct node_data __kptr *node;
};

/* This is necessary so that LLVM generates BTF for node_data struct
 * If it's not included, a fwd reference for node_data will be generated but
 * no struct. Example BTF of "node" field in map_value when not included:
 *
 * [10] PTR '(anon)' type_id=35
 * [34] FWD 'node_data' fwd_kind=struct
 * [35] TYPE_TAG 'kptr_ref' type_id=34
 *
 * (with no node_data struct defined)
 * Had to do the same w/ bpf_kfunc_call_test_release below
 */
struct node_data *just_here_because_btf_bug;

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__type(key, int);
	__type(value, struct map_value);
	__uint(max_entries, 2);
} some_nodes SEC(".maps");

static int create_and_stash(int idx, int val)
{
	struct map_value *mapval;
	struct node_data *res;

	mapval = bpf_map_lookup_elem(&some_nodes, &idx);
	if (!mapval)
		return 1;

	res = bpf_obj_new(typeof(*res));
	if (!res)
		return 1;
	res->key = val;

	res = bpf_kptr_xchg(&mapval->node, res);
	if (res)
		bpf_obj_drop(res);
	return 0;
}

SEC("tc")
long stash_rb_nodes(void *ctx)
{
	return create_and_stash(0, 41) ?: create_and_stash(1, 42);
}

SEC("tc")
long unstash_rb_node(void *ctx)
{
	struct map_value *mapval;
	struct node_data *res;
	long retval;
	int key = 1;

	mapval = bpf_map_lookup_elem(&some_nodes, &key);
	if (!mapval)
		return 1;

	res = bpf_kptr_xchg(&mapval->node, NULL);
	if (res) {
		retval = res->key;
		bpf_obj_drop(res);
		return retval;
	}
	return 1;
}

SEC("tc")
long stash_test_ref_kfunc(void *ctx)
{
	struct prog_test_ref_kfunc *res;
	struct map_value *mapval;
	int key = 0;

	mapval = bpf_map_lookup_elem(&some_nodes, &key);
	if (!mapval)
		return 1;

	res = bpf_kptr_xchg(&mapval->val, NULL);
	if (res)
		bpf_kfunc_call_test_release(res);
	return 0;
}

char _license[] SEC("license") = "GPL";
