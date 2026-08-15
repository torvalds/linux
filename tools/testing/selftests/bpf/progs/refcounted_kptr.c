// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2023 Meta Platforms, Inc. and affiliates. */

#include <vmlinux.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_core_read.h>
#include "bpf_misc.h"
#include "bpf_experimental.h"

extern void bpf_rcu_read_lock(void) __ksym;
extern void bpf_rcu_read_unlock(void) __ksym;

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
	__uint(max_entries, 2);
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

private(C) struct bpf_spin_lock block;
private(C) struct bpf_rb_root broot __contains(node_data, r);

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
__description("list_empty_test: list empty before add, non-empty after add")
__success __retval(0)
int list_empty_test(void *ctx)
{
	struct node_data *node_new;

	bpf_spin_lock(&lock);
	if (!bpf_list_empty(&head)) {
		bpf_spin_unlock(&lock);
		return -1;
	}
	bpf_spin_unlock(&lock);

	node_new = bpf_obj_new(typeof(*node_new));
	if (!node_new)
		return -2;

	bpf_spin_lock(&lock);
	bpf_list_push_front(&head, &node_new->l);

	if (bpf_list_empty(&head)) {
		bpf_spin_unlock(&lock);
		return -3;
	}
	bpf_spin_unlock(&lock);
	return 0;
}

static struct node_data *__add_in_list(struct bpf_list_head *head,
				       struct bpf_spin_lock *lock)
{
	struct node_data *node_new, *node_ref;

	node_new = bpf_obj_new(typeof(*node_new));
	if (!node_new)
		return NULL;

	node_ref = bpf_refcount_acquire(node_new);

	bpf_spin_lock(lock);
	bpf_list_push_front(head, &node_new->l);
	bpf_spin_unlock(lock);
	return node_ref;
}

SEC("tc")
__description("list_is_edge_test1: is_first on first node, is_last on last node")
__success __retval(0)
int list_is_edge_test1(void *ctx)
{
	struct node_data *node_first, *node_last;
	int err = 0;

	node_last = __add_in_list(&head, &lock);
	if (!node_last)
		return -1;

	node_first = __add_in_list(&head, &lock);
	if (!node_first) {
		bpf_obj_drop(node_last);
		return -2;
	}

	bpf_spin_lock(&lock);
	if (!bpf_list_is_first(&head, &node_first->l)) {
		err = -3;
		goto fail;
	}
	if (!bpf_list_is_last(&head, &node_last->l))
		err = -4;

fail:
	bpf_spin_unlock(&lock);
	bpf_obj_drop(node_first);
	bpf_obj_drop(node_last);
	return err;
}

SEC("tc")
__description("list_is_edge_test2: accept list_front/list_back return value")
__success __retval(0)
int list_is_edge_test2(void *ctx)
{
	struct bpf_list_node *front, *back;
	struct node_data *a, *b;
	long err = 0;

	a = __add_in_list(&head, &lock);
	if (!a)
		return -1;

	b = __add_in_list(&head, &lock);
	if (!b) {
		bpf_obj_drop(a);
		return -2;
	}

	bpf_spin_lock(&lock);
	front = bpf_list_front(&head);
	back = bpf_list_back(&head);
	if (!front || !back) {
		err = -3;
		goto out_unlock;
	}

	if (!bpf_list_is_first(&head, front) || bpf_list_is_last(&head, front)) {
		err = -4;
		goto out_unlock;
	}

	if (!bpf_list_is_last(&head, back) || bpf_list_is_first(&head, back)) {
		err = -5;
		goto out_unlock;
	}

out_unlock:
	bpf_spin_unlock(&lock);
	bpf_obj_drop(a);
	bpf_obj_drop(b);
	return err;
}

SEC("tc")
__description("list_is_edge_test3: single node is both first and last")
__success __retval(0)
int list_is_edge_test3(void *ctx)
{
	struct node_data *tmp;
	struct bpf_list_node *node;
	long err = 0;

	tmp = __add_in_list(&head, &lock);
	if (!tmp)
		return -1;

	bpf_spin_lock(&lock);
	node = bpf_list_front(&head);
	if (!node) {
		bpf_spin_unlock(&lock);
		bpf_obj_drop(tmp);
		return -2;
	}

	if (!bpf_list_is_first(&head, node) || !bpf_list_is_last(&head, node))
		err = -3;
	bpf_spin_unlock(&lock);

	bpf_obj_drop(tmp);
	return err;
}

SEC("tc")
__description("list_del_test1: del returns removed nodes")
__success __retval(0)
int list_del_test1(void *ctx)
{
	struct node_data *node_first, *node_last;
	struct bpf_list_node *bpf_node_first, *bpf_node_last;
	int err = 0;

	node_last = __add_in_list(&head, &lock);
	if (!node_last)
		return -1;

	node_first = __add_in_list(&head, &lock);
	if (!node_first) {
		bpf_obj_drop(node_last);
		return -2;
	}

	bpf_spin_lock(&lock);
	bpf_node_last = bpf_list_del(&head, &node_last->l);
	bpf_node_first = bpf_list_del(&head, &node_first->l);
	bpf_spin_unlock(&lock);

	if (bpf_node_first)
		bpf_obj_drop(container_of(bpf_node_first, struct node_data, l));
	else
		err = -3;

	if (bpf_node_last)
		bpf_obj_drop(container_of(bpf_node_last, struct node_data, l));
	else
		err = -4;

	bpf_obj_drop(node_first);
	bpf_obj_drop(node_last);
	return err;
}

SEC("tc")
__description("list_del_test2: remove an arbitrary node from the list")
__success __retval(0)
int list_del_test2(void *ctx)
{
	struct bpf_rb_node *rb;
	struct bpf_list_node *l;
	struct node_data *n;
	long err;

	err = __insert_in_tree_and_list(&head, &root, &lock);
	if (err)
		return err;

	bpf_spin_lock(&lock);
	rb = bpf_rbtree_first(&root);
	if (!rb) {
		bpf_spin_unlock(&lock);
		return -4;
	}

	rb = bpf_rbtree_remove(&root, rb);
	if (!rb) {
		bpf_spin_unlock(&lock);
		return -5;
	}

	n = container_of(rb, struct node_data, r);
	l = bpf_list_del(&head, &n->l);
	bpf_spin_unlock(&lock);
	bpf_obj_drop(n);
	if (!l)
		return -6;

	bpf_obj_drop(container_of(l, struct node_data, l));
	return 0;
}

SEC("tc")
__description("list_del_test3: list_del accepts list_front return value as node")
__success __retval(0)
int list_del_test3(void *ctx)
{
	struct node_data *tmp;
	struct bpf_list_node *bpf_node, *l;
	long err = 0;

	tmp = __add_in_list(&head, &lock);
	if (!tmp)
		return -1;

	bpf_spin_lock(&lock);
	bpf_node = bpf_list_front(&head);
	if (!bpf_node) {
		bpf_spin_unlock(&lock);
		err = -2;
		goto fail;
	}

	l = bpf_list_del(&head, bpf_node);
	bpf_spin_unlock(&lock);
	if (!l) {
		err = -3;
		goto fail;
	}

	bpf_obj_drop(container_of(l, struct node_data, l));
	bpf_obj_drop(tmp);
	return 0;

fail:
	bpf_obj_drop(tmp);
	return err;
}

SEC("tc")
__description("list_add_test1: insert new node after prev")
__success __retval(0)
int list_add_test1(void *ctx)
{
	struct node_data *node_first;
	struct node_data *new_node;
	long err = 0;

	node_first = __add_in_list(&head, &lock);
	if (!node_first)
		return -1;

	new_node = bpf_obj_new(typeof(*new_node));
	if (!new_node) {
		err = -2;
		goto fail;
	}

	bpf_spin_lock(&lock);
	err = bpf_list_add(&head, &new_node->l, &node_first->l);
	bpf_spin_unlock(&lock);
	if (err) {
		err = -3;
		goto fail;
	}

fail:
	bpf_obj_drop(node_first);
	return err;
}

SEC("tc")
__description("list_add_test2: list_add accepts list_front return value as prev")
__success __retval(0)
int list_add_test2(void *ctx)
{
	struct node_data *new_node, *tmp;
	struct bpf_list_node *bpf_node;
	long err = 0;

	tmp = __add_in_list(&head, &lock);
	if (!tmp)
		return -1;

	new_node = bpf_obj_new(typeof(*new_node));
	if (!new_node) {
		err = -2;
		goto fail;
	}

	bpf_spin_lock(&lock);
	bpf_node = bpf_list_front(&head);
	if (!bpf_node) {
		bpf_spin_unlock(&lock);
		bpf_obj_drop(new_node);
		err = -3;
		goto fail;
	}

	err = bpf_list_add(&head, &new_node->l, bpf_node);
	bpf_spin_unlock(&lock);
	if (err) {
		err = -4;
		goto fail;
	}

fail:
	bpf_obj_drop(tmp);
	return err;
}

struct uninit_head_val {
	struct bpf_spin_lock lock;
	struct bpf_list_head head __contains(node_data, l);
};

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__type(key, int);
	__type(value, struct uninit_head_val);
	__uint(max_entries, 1);
} uninit_head_map SEC(".maps");

SEC("tc")
__description("list_push_back_uninit_head: push_back on 0-initialized list head")
__success __retval(0)
int list_push_back_uninit_head(void *ctx)
{
	struct uninit_head_val *st;
	struct node_data *node;
	int ret = -1, key = 0;

	st = bpf_map_lookup_elem(&uninit_head_map, &key);
	if (!st)
		return -1;

	node = bpf_obj_new(typeof(*node));
	if (!node)
		return -1;

	bpf_spin_lock(&st->lock);
	ret = bpf_list_push_back(&st->head, &node->l);
	bpf_spin_unlock(&st->lock);

	return ret;
}

SEC("?tc")
__failure __msg("bpf_spin_lock at off=32 must be held for bpf_list_head")
long list_del_without_lock_fail(void *ctx)
{
	struct node_data *n;
	struct bpf_list_node *l;

	n = bpf_obj_new(typeof(*n));
	if (!n)
		return -1;

	/* Error case: delete list node without holding lock */
	l = bpf_list_del(&head, &n->l);
	bpf_obj_drop(n);
	if (!l)
		return -2;
	bpf_obj_drop(container_of(l, struct node_data, l));

	return 0;
}

SEC("?tc")
__failure __msg("bpf_spin_lock at off=32 must be held for bpf_list_head")
long list_add_without_lock_fail(void *ctx)
{
	struct node_data *n, *prev;
	long err;

	n = bpf_obj_new(typeof(*n));
	if (!n)
		return -1;

	prev = bpf_obj_new(typeof(*prev));
	if (!prev) {
		bpf_obj_drop(n);
		return -1;
	}

	/* Error case: add list node without holding lock */
	err = bpf_list_add(&head, &n->l, &prev->l);
	bpf_obj_drop(prev);
	if (err)
		return -2;

	return 0;
}

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
	if (!m)
		return 2;

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

static long __stash_map_empty_xchg(struct node_data *n, int idx)
{
	struct map_value *mapval = bpf_map_lookup_elem(&stashed_nodes, &idx);

	if (!mapval) {
		bpf_obj_drop(n);
		return 1;
	}
	n = bpf_kptr_xchg(&mapval->node, n);
	if (n) {
		bpf_obj_drop(n);
		return 2;
	}
	return 0;
}

SEC("tc")
long rbtree_wrong_owner_remove_fail_a1(void *ctx)
{
	struct node_data *n, *m;

	n = bpf_obj_new(typeof(*n));
	if (!n)
		return 1;
	m = bpf_refcount_acquire(n);

	if (__stash_map_empty_xchg(n, 0)) {
		bpf_obj_drop(m);
		return 2;
	}

	if (__stash_map_empty_xchg(m, 1))
		return 3;

	return 0;
}

SEC("tc")
long rbtree_wrong_owner_remove_fail_b(void *ctx)
{
	struct map_value *mapval;
	struct node_data *n;
	int idx = 0;

	mapval = bpf_map_lookup_elem(&stashed_nodes, &idx);
	if (!mapval)
		return 1;

	n = bpf_kptr_xchg(&mapval->node, NULL);
	if (!n)
		return 2;

	bpf_spin_lock(&block);

	bpf_rbtree_add(&broot, &n->r, less);

	bpf_spin_unlock(&block);
	return 0;
}

SEC("tc")
long rbtree_wrong_owner_remove_fail_a2(void *ctx)
{
	struct map_value *mapval;
	struct bpf_rb_node *res;
	struct node_data *m;
	int idx = 1;

	mapval = bpf_map_lookup_elem(&stashed_nodes, &idx);
	if (!mapval)
		return 1;

	m = bpf_kptr_xchg(&mapval->node, NULL);
	if (!m)
		return 2;
	bpf_spin_lock(&lock);

	/* make m non-owning ref */
	bpf_list_push_back(&head, &m->l);
	res = bpf_rbtree_remove(&root, &m->r);

	bpf_spin_unlock(&lock);
	if (res) {
		bpf_obj_drop(container_of(res, struct node_data, r));
		return 3;
	}
	return 0;
}

SEC("?fentry.s/" SYS_PREFIX "sys_getpgid")
__success
int BPF_PROG(rbtree_sleepable_rcu,
	     struct file *file, struct kobject *kobj,
	     struct bin_attribute *bin_attr, char *buf, loff_t off, size_t len)
{
	struct bpf_rb_node *rb;
	struct node_data *n, *m = NULL;

	n = bpf_obj_new(typeof(*n));
	if (!n)
		return 0;

	bpf_rcu_read_lock();
	bpf_spin_lock(&lock);
	bpf_rbtree_add(&root, &n->r, less);
	rb = bpf_rbtree_first(&root);
	if (!rb)
		goto err_out;

	rb = bpf_rbtree_remove(&root, rb);
	if (!rb)
		goto err_out;

	m = container_of(rb, struct node_data, r);

err_out:
	bpf_spin_unlock(&lock);
	bpf_rcu_read_unlock();
	if (m)
		bpf_obj_drop(m);
	return 0;
}

SEC("?fentry.s/" SYS_PREFIX "sys_getpgid")
__success
int BPF_PROG(rbtree_sleepable_rcu_no_explicit_rcu_lock,
	     struct file *file, struct kobject *kobj,
	     struct bin_attribute *bin_attr, char *buf, loff_t off, size_t len)
{
	struct bpf_rb_node *rb;
	struct node_data *n, *m = NULL;

	n = bpf_obj_new(typeof(*n));
	if (!n)
		return 0;

	/* No explicit bpf_rcu_read_lock */
	bpf_spin_lock(&lock);
	bpf_rbtree_add(&root, &n->r, less);
	rb = bpf_rbtree_first(&root);
	if (!rb)
		goto err_out;

	rb = bpf_rbtree_remove(&root, rb);
	if (!rb)
		goto err_out;

	m = container_of(rb, struct node_data, r);

err_out:
	bpf_spin_unlock(&lock);
	/* No explicit bpf_rcu_read_unlock */
	if (m)
		bpf_obj_drop(m);
	return 0;
}

private(kptr_ref) u64 ref;

static int probe_read_refcount(void)
{
	u32 refcount;

	bpf_probe_read_kernel(&refcount, sizeof(refcount), (void *) ref);
	return refcount;
}

static int __insert_in_list(struct bpf_list_head *head, struct bpf_spin_lock *lock,
			    struct node_data __kptr **node)
{
	struct node_data *node_new, *node_ref, *node_old;

	node_new = bpf_obj_new(typeof(*node_new));
	if (!node_new)
		return -1;

	node_ref = bpf_refcount_acquire(node_new);
	node_old = bpf_kptr_xchg(node, node_new);
	if (node_old) {
		bpf_obj_drop(node_old);
		bpf_obj_drop(node_ref);
		return -2;
	}

	bpf_spin_lock(lock);
	bpf_list_push_front(head, &node_ref->l);
	ref = (u64)(void *) &node_ref->ref;
	bpf_spin_unlock(lock);
	return probe_read_refcount();
}

struct {
	__uint(type, BPF_MAP_TYPE_PERCPU_HASH);
	__type(key, int);
	__type(value, struct map_value);
	__uint(max_entries, 1);
} percpu_hash SEC(".maps");

SEC("tc")
int percpu_hash_refcount_leak(void *ctx)
{
	struct map_value *v;
	int key = 0;

	v = bpf_map_lookup_percpu_elem(&percpu_hash, &key, 0);
	if (!v)
		return 0;

	return __insert_in_list(&head, &lock, &v->node);
}

SEC("syscall")
int clear_percpu_hash_kptr(void *ctx)
{
	struct node_data *n;
	struct map_value *v;
	int key = 0;

	v = bpf_map_lookup_percpu_elem(&percpu_hash, &key, 0);
	if (!v)
		return 0;

	n = bpf_kptr_xchg(&v->node, NULL);
	if (!n)
		return 0;
	bpf_obj_drop(n);
	return probe_read_refcount();
}

SEC("tc")
int check_percpu_hash_refcount(void *ctx)
{
	return probe_read_refcount();
}

char _license[] SEC("license") = "GPL";
