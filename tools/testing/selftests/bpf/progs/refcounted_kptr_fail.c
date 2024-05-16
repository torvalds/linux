// SPDX-License-Identifier: GPL-2.0
#include <vmlinux.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_core_read.h>
#include "bpf_experimental.h"
#include "bpf_misc.h"

struct node_acquire {
	long key;
	long data;
	struct bpf_rb_node node;
	struct bpf_refcount refcount;
};

extern void bpf_rcu_read_lock(void) __ksym;
extern void bpf_rcu_read_unlock(void) __ksym;

#define private(name) SEC(".data." #name) __hidden __attribute__((aligned(8)))
private(A) struct bpf_spin_lock glock;
private(A) struct bpf_rb_root groot __contains(node_acquire, node);

static bool less(struct bpf_rb_node *a, const struct bpf_rb_node *b)
{
	struct node_acquire *node_a;
	struct node_acquire *node_b;

	node_a = container_of(a, struct node_acquire, node);
	node_b = container_of(b, struct node_acquire, node);

	return node_a->key < node_b->key;
}

SEC("?tc")
__failure __msg("Unreleased reference id=4 alloc_insn=21")
long rbtree_refcounted_node_ref_escapes(void *ctx)
{
	struct node_acquire *n, *m;

	n = bpf_obj_new(typeof(*n));
	if (!n)
		return 1;

	bpf_spin_lock(&glock);
	bpf_rbtree_add(&groot, &n->node, less);
	/* m becomes an owning ref but is never drop'd or added to a tree */
	m = bpf_refcount_acquire(n);
	bpf_spin_unlock(&glock);
	if (!m)
		return 2;

	m->key = 2;
	return 0;
}

SEC("?tc")
__failure __msg("Possibly NULL pointer passed to trusted arg0")
long refcount_acquire_maybe_null(void *ctx)
{
	struct node_acquire *n, *m;

	n = bpf_obj_new(typeof(*n));
	/* Intentionally not testing !n
	 * it's MAYBE_NULL for refcount_acquire
	 */
	m = bpf_refcount_acquire(n);
	if (m)
		bpf_obj_drop(m);
	if (n)
		bpf_obj_drop(n);

	return 0;
}

SEC("?tc")
__failure __msg("Unreleased reference id=3 alloc_insn=9")
long rbtree_refcounted_node_ref_escapes_owning_input(void *ctx)
{
	struct node_acquire *n, *m;

	n = bpf_obj_new(typeof(*n));
	if (!n)
		return 1;

	/* m becomes an owning ref but is never drop'd or added to a tree */
	m = bpf_refcount_acquire(n);
	m->key = 2;

	bpf_spin_lock(&glock);
	bpf_rbtree_add(&groot, &n->node, less);
	bpf_spin_unlock(&glock);

	return 0;
}

SEC("?fentry.s/bpf_testmod_test_read")
__failure __msg("function calls are not allowed while holding a lock")
int BPF_PROG(rbtree_fail_sleepable_lock_across_rcu,
	     struct file *file, struct kobject *kobj,
	     struct bin_attribute *bin_attr, char *buf, loff_t off, size_t len)
{
	struct node_acquire *n;

	n = bpf_obj_new(typeof(*n));
	if (!n)
		return 0;

	/* spin_{lock,unlock} are in different RCU CS */
	bpf_rcu_read_lock();
	bpf_spin_lock(&glock);
	bpf_rbtree_add(&groot, &n->node, less);
	bpf_rcu_read_unlock();

	bpf_rcu_read_lock();
	bpf_spin_unlock(&glock);
	bpf_rcu_read_unlock();

	return 0;
}

char _license[] SEC("license") = "GPL";
