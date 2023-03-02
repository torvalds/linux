// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2022 Meta Platforms, Inc. and affiliates. */

#include <vmlinux.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_core_read.h>
#include "bpf_experimental.h"

struct node_data {
	int key;
	int data;
	struct bpf_rb_node node;
};

struct node_data2 {
	int key;
	struct bpf_rb_node node;
	int data;
};

static bool less2(struct bpf_rb_node *a, const struct bpf_rb_node *b)
{
	struct node_data2 *node_a;
	struct node_data2 *node_b;

	node_a = container_of(a, struct node_data2, node);
	node_b = container_of(b, struct node_data2, node);

	return node_a->key < node_b->key;
}

#define private(name) SEC(".data." #name) __hidden __attribute__((aligned(8)))
private(A) struct bpf_spin_lock glock;
private(A) struct bpf_rb_root groot __contains(node_data, node);

SEC("tc")
long rbtree_api_add__add_wrong_type(void *ctx)
{
	struct node_data2 *n;

	n = bpf_obj_new(typeof(*n));
	if (!n)
		return 1;

	bpf_spin_lock(&glock);
	bpf_rbtree_add(&groot, &n->node, less2);
	bpf_spin_unlock(&glock);
	return 0;
}

char _license[] SEC("license") = "GPL";
