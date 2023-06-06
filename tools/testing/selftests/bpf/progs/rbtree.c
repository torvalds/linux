// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2022 Meta Platforms, Inc. and affiliates. */

#include <vmlinux.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_core_read.h>
#include "bpf_experimental.h"

struct node_data {
	long key;
	long data;
	struct bpf_rb_node node;
};

long less_callback_ran = -1;
long removed_key = -1;
long first_data[2] = {-1, -1};

#define private(name) SEC(".data." #name) __hidden __attribute__((aligned(8)))
private(A) struct bpf_spin_lock glock;
private(A) struct bpf_rb_root groot __contains(node_data, node);

static bool less(struct bpf_rb_node *a, const struct bpf_rb_node *b)
{
	struct node_data *node_a;
	struct node_data *node_b;

	node_a = container_of(a, struct node_data, node);
	node_b = container_of(b, struct node_data, node);
	less_callback_ran = 1;

	return node_a->key < node_b->key;
}

static long __add_three(struct bpf_rb_root *root, struct bpf_spin_lock *lock)
{
	struct node_data *n, *m;

	n = bpf_obj_new(typeof(*n));
	if (!n)
		return 1;
	n->key = 5;

	m = bpf_obj_new(typeof(*m));
	if (!m) {
		bpf_obj_drop(n);
		return 2;
	}
	m->key = 1;

	bpf_spin_lock(&glock);
	bpf_rbtree_add(&groot, &n->node, less);
	bpf_rbtree_add(&groot, &m->node, less);
	bpf_spin_unlock(&glock);

	n = bpf_obj_new(typeof(*n));
	if (!n)
		return 3;
	n->key = 3;

	bpf_spin_lock(&glock);
	bpf_rbtree_add(&groot, &n->node, less);
	bpf_spin_unlock(&glock);
	return 0;
}

SEC("tc")
long rbtree_add_nodes(void *ctx)
{
	return __add_three(&groot, &glock);
}

SEC("tc")
long rbtree_add_and_remove(void *ctx)
{
	struct bpf_rb_node *res = NULL;
	struct node_data *n, *m = NULL;

	n = bpf_obj_new(typeof(*n));
	if (!n)
		goto err_out;
	n->key = 5;

	m = bpf_obj_new(typeof(*m));
	if (!m)
		goto err_out;
	m->key = 3;

	bpf_spin_lock(&glock);
	bpf_rbtree_add(&groot, &n->node, less);
	bpf_rbtree_add(&groot, &m->node, less);
	res = bpf_rbtree_remove(&groot, &n->node);
	bpf_spin_unlock(&glock);

	if (!res)
		return 1;

	n = container_of(res, struct node_data, node);
	removed_key = n->key;
	bpf_obj_drop(n);

	return 0;
err_out:
	if (n)
		bpf_obj_drop(n);
	if (m)
		bpf_obj_drop(m);
	return 1;
}

SEC("tc")
long rbtree_first_and_remove(void *ctx)
{
	struct bpf_rb_node *res = NULL;
	struct node_data *n, *m, *o;

	n = bpf_obj_new(typeof(*n));
	if (!n)
		return 1;
	n->key = 3;
	n->data = 4;

	m = bpf_obj_new(typeof(*m));
	if (!m)
		goto err_out;
	m->key = 5;
	m->data = 6;

	o = bpf_obj_new(typeof(*o));
	if (!o)
		goto err_out;
	o->key = 1;
	o->data = 2;

	bpf_spin_lock(&glock);
	bpf_rbtree_add(&groot, &n->node, less);
	bpf_rbtree_add(&groot, &m->node, less);
	bpf_rbtree_add(&groot, &o->node, less);

	res = bpf_rbtree_first(&groot);
	if (!res) {
		bpf_spin_unlock(&glock);
		return 2;
	}

	o = container_of(res, struct node_data, node);
	first_data[0] = o->data;

	res = bpf_rbtree_remove(&groot, &o->node);
	bpf_spin_unlock(&glock);

	if (!res)
		return 5;

	o = container_of(res, struct node_data, node);
	removed_key = o->key;
	bpf_obj_drop(o);

	bpf_spin_lock(&glock);
	res = bpf_rbtree_first(&groot);
	if (!res) {
		bpf_spin_unlock(&glock);
		return 3;
	}

	o = container_of(res, struct node_data, node);
	first_data[1] = o->data;
	bpf_spin_unlock(&glock);

	return 0;
err_out:
	if (n)
		bpf_obj_drop(n);
	if (m)
		bpf_obj_drop(m);
	return 1;
}

SEC("tc")
long rbtree_api_release_aliasing(void *ctx)
{
	struct node_data *n, *m, *o;
	struct bpf_rb_node *res, *res2;

	n = bpf_obj_new(typeof(*n));
	if (!n)
		return 1;
	n->key = 41;
	n->data = 42;

	bpf_spin_lock(&glock);
	bpf_rbtree_add(&groot, &n->node, less);
	bpf_spin_unlock(&glock);

	bpf_spin_lock(&glock);

	/* m and o point to the same node,
	 * but verifier doesn't know this
	 */
	res = bpf_rbtree_first(&groot);
	if (!res)
		goto err_out;
	o = container_of(res, struct node_data, node);

	res = bpf_rbtree_first(&groot);
	if (!res)
		goto err_out;
	m = container_of(res, struct node_data, node);

	res = bpf_rbtree_remove(&groot, &m->node);
	/* Retval of previous remove returns an owning reference to m,
	 * which is the same node non-owning ref o is pointing at.
	 * We can safely try to remove o as the second rbtree_remove will
	 * return NULL since the node isn't in a tree.
	 *
	 * Previously we relied on the verifier type system + rbtree_remove
	 * invalidating non-owning refs to ensure that rbtree_remove couldn't
	 * fail, but now rbtree_remove does runtime checking so we no longer
	 * invalidate non-owning refs after remove.
	 */
	res2 = bpf_rbtree_remove(&groot, &o->node);

	bpf_spin_unlock(&glock);

	if (res) {
		o = container_of(res, struct node_data, node);
		first_data[0] = o->data;
		bpf_obj_drop(o);
	}
	if (res2) {
		/* The second remove fails, so res2 is null and this doesn't
		 * execute
		 */
		m = container_of(res2, struct node_data, node);
		first_data[1] = m->data;
		bpf_obj_drop(m);
	}
	return 0;

err_out:
	bpf_spin_unlock(&glock);
	return 1;
}

char _license[] SEC("license") = "GPL";
