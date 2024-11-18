// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2023 Meta Platforms, Inc. and affiliates. */

#include <vmlinux.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_core_read.h>
#include "../bpf_experimental.h"
#include "../bpf_testmod/bpf_testmod_kfunc.h"

struct plain_local;

struct node_data {
	long key;
	long data;
	struct plain_local __kptr * stashed_in_local_kptr;
	struct bpf_rb_node node;
};

struct refcounted_node {
	long data;
	struct bpf_rb_node rb_node;
	struct bpf_refcount refcount;
};

struct stash {
	struct bpf_spin_lock l;
	struct refcounted_node __kptr *stashed;
};

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__type(key, int);
	__type(value, struct stash);
	__uint(max_entries, 10);
} refcounted_node_stash SEC(".maps");

struct plain_local {
	long key;
	long data;
};

struct local_with_root {
	long key;
	struct bpf_spin_lock l;
	struct bpf_rb_root r __contains(node_data, node);
};

struct map_value {
	struct prog_test_ref_kfunc *not_kptr;
	struct prog_test_ref_kfunc __kptr *val;
	struct node_data __kptr *node;
	struct plain_local __kptr *plain;
	struct local_with_root __kptr *local_root;
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
struct refcounted_node *just_here_because_btf_bug2;

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__type(key, int);
	__type(value, struct map_value);
	__uint(max_entries, 2);
} some_nodes SEC(".maps");

static bool less(struct bpf_rb_node *a, const struct bpf_rb_node *b)
{
	struct node_data *node_a;
	struct node_data *node_b;

	node_a = container_of(a, struct node_data, node);
	node_b = container_of(b, struct node_data, node);

	return node_a->key < node_b->key;
}

static int create_and_stash(int idx, int val)
{
	struct plain_local *inner_local_kptr;
	struct map_value *mapval;
	struct node_data *res;

	mapval = bpf_map_lookup_elem(&some_nodes, &idx);
	if (!mapval)
		return 1;

	inner_local_kptr = bpf_obj_new(typeof(*inner_local_kptr));
	if (!inner_local_kptr)
		return 2;

	res = bpf_obj_new(typeof(*res));
	if (!res) {
		bpf_obj_drop(inner_local_kptr);
		return 3;
	}
	res->key = val;

	inner_local_kptr = bpf_kptr_xchg(&res->stashed_in_local_kptr, inner_local_kptr);
	if (inner_local_kptr) {
		/* Should never happen, we just obj_new'd res */
		bpf_obj_drop(inner_local_kptr);
		bpf_obj_drop(res);
		return 4;
	}

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
long stash_plain(void *ctx)
{
	struct map_value *mapval;
	struct plain_local *res;
	int idx = 0;

	mapval = bpf_map_lookup_elem(&some_nodes, &idx);
	if (!mapval)
		return 1;

	res = bpf_obj_new(typeof(*res));
	if (!res)
		return 1;
	res->key = 41;

	res = bpf_kptr_xchg(&mapval->plain, res);
	if (res)
		bpf_obj_drop(res);
	return 0;
}

SEC("tc")
long stash_local_with_root(void *ctx)
{
	struct local_with_root *res;
	struct map_value *mapval;
	struct node_data *n;
	int idx = 0;

	mapval = bpf_map_lookup_elem(&some_nodes, &idx);
	if (!mapval)
		return 1;

	res = bpf_obj_new(typeof(*res));
	if (!res)
		return 2;
	res->key = 41;

	n = bpf_obj_new(typeof(*n));
	if (!n) {
		bpf_obj_drop(res);
		return 3;
	}

	bpf_spin_lock(&res->l);
	bpf_rbtree_add(&res->r, &n->node, less);
	bpf_spin_unlock(&res->l);

	res = bpf_kptr_xchg(&mapval->local_root, res);
	if (res) {
		bpf_obj_drop(res);
		return 4;
	}
	return 0;
}

SEC("tc")
long unstash_rb_node(void *ctx)
{
	struct plain_local *inner_local_kptr = NULL;
	struct map_value *mapval;
	struct node_data *res;
	long retval;
	int key = 1;

	mapval = bpf_map_lookup_elem(&some_nodes, &key);
	if (!mapval)
		return 1;

	res = bpf_kptr_xchg(&mapval->node, NULL);
	if (res) {
		inner_local_kptr = bpf_kptr_xchg(&res->stashed_in_local_kptr, inner_local_kptr);
		if (!inner_local_kptr) {
			bpf_obj_drop(res);
			return 1;
		}
		bpf_obj_drop(inner_local_kptr);

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

SEC("tc")
long refcount_acquire_without_unstash(void *ctx)
{
	struct refcounted_node *p;
	struct stash *s;
	int ret = 0;

	s = bpf_map_lookup_elem(&refcounted_node_stash, &ret);
	if (!s)
		return 1;

	if (!s->stashed)
		/* refcount_acquire failure is expected when no refcounted_node
		 * has been stashed before this program executes
		 */
		return 2;

	p = bpf_refcount_acquire(s->stashed);
	if (!p)
		return 3;

	ret = s->stashed ? s->stashed->data : -1;
	bpf_obj_drop(p);
	return ret;
}

/* Helper for refcount_acquire_without_unstash test */
SEC("tc")
long stash_refcounted_node(void *ctx)
{
	struct refcounted_node *p;
	struct stash *s;
	int key = 0;

	s = bpf_map_lookup_elem(&refcounted_node_stash, &key);
	if (!s)
		return 1;

	p = bpf_obj_new(typeof(*p));
	if (!p)
		return 2;
	p->data = 42;

	p = bpf_kptr_xchg(&s->stashed, p);
	if (p) {
		bpf_obj_drop(p);
		return 3;
	}

	return 0;
}

char _license[] SEC("license") = "GPL";
