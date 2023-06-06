// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2023 Meta Platforms, Inc. and affiliates. */

#include <vmlinux.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_core_read.h>
#include "bpf_misc.h"
#include "bpf_experimental.h"

struct node_data {
	long key;
	long list_data;
	struct bpf_rb_node r;
	struct bpf_list_node l;
	struct bpf_refcount ref;
};

struct map_value {
	struct node_data __kptr *node;
};

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__type(key, int);
	__type(value, struct map_value);
	__uint(max_entries, 1);
} stashed_nodes SEC(".maps");

struct node_acquire {
	long key;
	long data;
	struct bpf_rb_node node;
	struct bpf_refcount refcount;
};

#define private(name) SEC(".bss." #name) __hidden __attribute__((aligned(8)))
private(A) struct bpf_spin_lock lock;
private(A) struct bpf_rb_root root __contains(node_data, r);
private(A) struct bpf_list_head head __contains(node_data, l);

private(B) struct bpf_spin_lock alock;
private(B) struct bpf_rb_root aroot __contains(node_acquire, node);

static bool less(struct bpf_rb_node *node_a, const struct bpf_rb_node *node_b)
{
	struct node_data *a;
	struct node_data *b;

	a = container_of(node_a, struct node_data, r);
	b = container_of(node_b, struct node_data, r);

	return a->key < b->key;
}

static bool less_a(struct bpf_rb_node *a, const struct bpf_rb_node *b)
{
	struct node_acquire *node_a;
	struct node_acquire *node_b;

	node_a = container_of(a, struct node_acquire, node);
	node_b = container_of(b, struct node_acquire, node);

	return node_a->key < node_b->key;
}

static long __insert_in_tree_and_list(struct bpf_list_head *head,
				      struct bpf_rb_root *root,
				      struct bpf_spin_lock *lock)
{
	struct node_data *n, *m;

	n = bpf_obj_new(typeof(*n));
	if (!n)
		return -1;

	m = bpf_refcount_acquire(n);
	m->key = 123;
	m->list_data = 456;

	bpf_spin_lock(lock);
	if (bpf_rbtree_add(root, &n->r, less)) {
		/* Failure to insert - unexpected */
		bpf_spin_unlock(lock);
		bpf_obj_drop(m);
		return -2;
	}
	bpf_spin_unlock(lock);

	bpf_spin_lock(lock);
	if (bpf_list_push_front(head, &m->l)) {
		/* Failure to insert - unexpected */
		bpf_spin_unlock(lock);
		return -3;
	}
	bpf_spin_unlock(lock);
	return 0;
}

static long __stash_map_insert_tree(int idx, int val, struct bpf_rb_root *root,
				    struct bpf_spin_lock *lock)
{
	struct map_value *mapval;
	struct node_data *n, *m;

	mapval = bpf_map_lookup_elem(&stashed_nodes, &idx);
	if (!mapval)
		return -1;

	n = bpf_obj_new(typeof(*n));
	if (!n)
		return -2;

	n->key = val;
	m = bpf_refcount_acquire(n);

	n = bpf_kptr_xchg(&mapval->node, n);
	if (n) {
		bpf_obj_drop(n);
		bpf_obj_drop(m);
		return -3;
	}

	bpf_spin_lock(lock);
	if (bpf_rbtree_add(root, &m->r, less)) {
		/* Failure to insert - unexpected */
		bpf_spin_unlock(lock);
		return -4;
	}
	bpf_spin_unlock(lock);
	return 0;
}

static long __read_from_tree(struct bpf_rb_root *root,
			     struct bpf_spin_lock *lock,
			     bool remove_from_tree)
{
	struct bpf_rb_node *rb;
	struct node_data *n;
	long res = -99;

	bpf_spin_lock(lock);

	rb = bpf_rbtree_first(root);
	if (!rb) {
		bpf_spin_unlock(lock);
		return -1;
	}

	n = container_of(rb, struct node_data, r);
	res = n->key;

	if (!remove_from_tree) {
		bpf_spin_unlock(lock);
		return res;
	}

	rb = bpf_rbtree_remove(root, rb);
	bpf_spin_unlock(lock);
	if (!rb)
		return -2;
	n = container_of(rb, struct node_data, r);
	bpf_obj_drop(n);
	return res;
}

static long __read_from_list(struct bpf_list_head *head,
			     struct bpf_spin_lock *lock,
			     bool remove_from_list)
{
	struct bpf_list_node *l;
	struct node_data *n;
	long res = -99;

	bpf_spin_lock(lock);

	l = bpf_list_pop_front(head);
	if (!l) {
		bpf_spin_unlock(lock);
		return -1;
	}

	n = container_of(l, struct node_data, l);
	res = n->list_data;

	if (!remove_from_list) {
		if (bpf_list_push_back(head, &n->l)) {
			bpf_spin_unlock(lock);
			return -2;
		}
	}

	bpf_spin_unlock(lock);

	if (remove_from_list)
		bpf_obj_drop(n);
	return res;
}

static long __read_from_unstash(int idx)
{
	struct node_data *n = NULL;
	struct map_value *mapval;
	long val = -99;

	mapval = bpf_map_lookup_elem(&stashed_nodes, &idx);
	if (!mapval)
		return -1;

	n = bpf_kptr_xchg(&mapval->node, n);
	if (!n)
		return -2;

	val = n->key;
	bpf_obj_drop(n);
	return val;
}

#define INSERT_READ_BOTH(rem_tree, rem_list, desc)			\
SEC("tc")								\
__description(desc)							\
__success __retval(579)							\
long insert_and_remove_tree_##rem_tree##_list_##rem_list(void *ctx)	\
{									\
	long err, tree_data, list_data;					\
									\
	err = __insert_in_tree_and_list(&head, &root, &lock);		\
	if (err)							\
		return err;						\
									\
	err = __read_from_tree(&root, &lock, rem_tree);			\
	if (err < 0)							\
		return err;						\
	else								\
		tree_data = err;					\
									\
	err = __read_from_list(&head, &lock, rem_list);			\
	if (err < 0)							\
		return err;						\
	else								\
		list_data = err;					\
									\
	return tree_data + list_data;					\
}

/* After successful insert of struct node_data into both collections:
 *   - it should have refcount = 2
 *   - removing / not removing the node_data from a collection after
 *     reading should have no effect on ability to read / remove from
 *     the other collection
 */
INSERT_READ_BOTH(true, true, "insert_read_both: remove from tree + list");
INSERT_READ_BOTH(false, false, "insert_read_both: remove from neither");
INSERT_READ_BOTH(true, false, "insert_read_both: remove from tree");
INSERT_READ_BOTH(false, true, "insert_read_both: remove from list");

#undef INSERT_READ_BOTH
#define INSERT_READ_BOTH(rem_tree, rem_list, desc)			\
SEC("tc")								\
__description(desc)							\
__success __retval(579)							\
long insert_and_remove_lf_tree_##rem_tree##_list_##rem_list(void *ctx)	\
{									\
	long err, tree_data, list_data;					\
									\
	err = __insert_in_tree_and_list(&head, &root, &lock);		\
	if (err)							\
		return err;						\
									\
	err = __read_from_list(&head, &lock, rem_list);			\
	if (err < 0)							\
		return err;						\
	else								\
		list_data = err;					\
									\
	err = __read_from_tree(&root, &lock, rem_tree);			\
	if (err < 0)							\
		return err;						\
	else								\
		tree_data = err;					\
									\
	return tree_data + list_data;					\
}

/* Similar to insert_read_both, but list data is read and possibly removed
 * first
 *
 * Results should be no different than reading and possibly removing rbtree
 * node first
 */
INSERT_READ_BOTH(true, true, "insert_read_both_list_first: remove from tree + list");
INSERT_READ_BOTH(false, false, "insert_read_both_list_first: remove from neither");
INSERT_READ_BOTH(true, false, "insert_read_both_list_first: remove from tree");
INSERT_READ_BOTH(false, true, "insert_read_both_list_first: remove from list");

#define INSERT_DOUBLE_READ_AND_DEL(read_fn, read_root, desc)		\
SEC("tc")								\
__description(desc)							\
__success __retval(-1)							\
long insert_double_##read_fn##_and_del_##read_root(void *ctx)		\
{									\
	long err, list_data;						\
									\
	err = __insert_in_tree_and_list(&head, &root, &lock);		\
	if (err)							\
		return err;						\
									\
	err = read_fn(&read_root, &lock, true);				\
	if (err < 0)							\
		return err;						\
	else								\
		list_data = err;					\
									\
	err = read_fn(&read_root, &lock, true);				\
	if (err < 0)							\
		return err;						\
									\
	return err + list_data;						\
}

/* Insert into both tree and list, then try reading-and-removing from either twice
 *
 * The second read-and-remove should fail on read step since the node has
 * already been removed
 */
INSERT_DOUBLE_READ_AND_DEL(__read_from_tree, root, "insert_double_del: 2x read-and-del from tree");
INSERT_DOUBLE_READ_AND_DEL(__read_from_list, head, "insert_double_del: 2x read-and-del from list");

#define INSERT_STASH_READ(rem_tree, desc)				\
SEC("tc")								\
__description(desc)							\
__success __retval(84)							\
long insert_rbtree_and_stash__del_tree_##rem_tree(void *ctx)		\
{									\
	long err, tree_data, map_data;					\
									\
	err = __stash_map_insert_tree(0, 42, &root, &lock);		\
	if (err)							\
		return err;						\
									\
	err = __read_from_tree(&root, &lock, rem_tree);			\
	if (err < 0)							\
		return err;						\
	else								\
		tree_data = err;					\
									\
	err = __read_from_unstash(0);					\
	if (err < 0)							\
		return err;						\
	else								\
		map_data = err;						\
									\
	return tree_data + map_data;					\
}

/* Stash a refcounted node in map_val, insert same node into tree, then try
 * reading data from tree then unstashed map_val, possibly removing from tree
 *
 * Removing from tree should have no effect on map_val kptr validity
 */
INSERT_STASH_READ(true, "insert_stash_read: remove from tree");
INSERT_STASH_READ(false, "insert_stash_read: don't remove from tree");

SEC("tc")
__success
long rbtree_refcounted_node_ref_escapes(void *ctx)
{
	struct node_acquire *n, *m;

	n = bpf_obj_new(typeof(*n));
	if (!n)
		return 1;

	bpf_spin_lock(&alock);
	bpf_rbtree_add(&aroot, &n->node, less_a);
	m = bpf_refcount_acquire(n);
	bpf_spin_unlock(&alock);

	m->key = 2;
	bpf_obj_drop(m);
	return 0;
}

SEC("tc")
__success
long rbtree_refcounted_node_ref_escapes_owning_input(void *ctx)
{
	struct node_acquire *n, *m;

	n = bpf_obj_new(typeof(*n));
	if (!n)
		return 1;

	m = bpf_refcount_acquire(n);
	m->key = 2;

	bpf_spin_lock(&alock);
	bpf_rbtree_add(&aroot, &n->node, less_a);
	bpf_spin_unlock(&alock);

	bpf_obj_drop(m);

	return 0;
}

char _license[] SEC("license") = "GPL";
