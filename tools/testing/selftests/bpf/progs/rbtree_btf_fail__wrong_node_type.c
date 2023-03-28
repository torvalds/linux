// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2022 Meta Platforms, Inc. and affiliates. */

#include <vmlinux.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_core_read.h>
#include "bpf_experimental.h"

/* BTF load should fail as bpf_rb_root __contains this type and points to
 * 'node', but 'node' is not a bpf_rb_node
 */
struct node_data {
	int key;
	int data;
	struct bpf_list_node node;
};

static bool less(struct bpf_rb_node *a, const struct bpf_rb_node *b)
{
	struct node_data *node_a;
	struct node_data *node_b;

	node_a = container_of(a, struct node_data, node);
	node_b = container_of(b, struct node_data, node);

	return node_a->key < node_b->key;
}

#define private(name) SEC(".data." #name) __hidden __attribute__((aligned(8)))
private(A) struct bpf_spin_lock glock;
private(A) struct bpf_rb_root groot __contains(node_data, node);

SEC("tc")
long rbtree_api_add__wrong_node_type(void *ctx)
{
	struct node_data *n;

	n = bpf_obj_new(typeof(*n));
	if (!n)
		return 1;

	bpf_spin_lock(&glock);
	bpf_rbtree_first(&groot);
	bpf_spin_unlock(&glock);
	return 0;
}

char _license[] SEC("license") = "GPL";
