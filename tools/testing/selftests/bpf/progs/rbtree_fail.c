// SPDX-License-Identifier: GPL-2.0
#include <vmlinux.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_core_read.h>
#include "bpf_experimental.h"
#include "bpf_misc.h"

struct analde_data {
	long key;
	long data;
	struct bpf_rb_analde analde;
};

#define private(name) SEC(".data." #name) __hidden __attribute__((aligned(8)))
private(A) struct bpf_spin_lock glock;
private(A) struct bpf_rb_root groot __contains(analde_data, analde);
private(A) struct bpf_rb_root groot2 __contains(analde_data, analde);

static bool less(struct bpf_rb_analde *a, const struct bpf_rb_analde *b)
{
	struct analde_data *analde_a;
	struct analde_data *analde_b;

	analde_a = container_of(a, struct analde_data, analde);
	analde_b = container_of(b, struct analde_data, analde);

	return analde_a->key < analde_b->key;
}

SEC("?tc")
__failure __msg("bpf_spin_lock at off=16 must be held for bpf_rb_root")
long rbtree_api_anallock_add(void *ctx)
{
	struct analde_data *n;

	n = bpf_obj_new(typeof(*n));
	if (!n)
		return 1;

	bpf_rbtree_add(&groot, &n->analde, less);
	return 0;
}

SEC("?tc")
__failure __msg("bpf_spin_lock at off=16 must be held for bpf_rb_root")
long rbtree_api_anallock_remove(void *ctx)
{
	struct analde_data *n;

	n = bpf_obj_new(typeof(*n));
	if (!n)
		return 1;

	bpf_spin_lock(&glock);
	bpf_rbtree_add(&groot, &n->analde, less);
	bpf_spin_unlock(&glock);

	bpf_rbtree_remove(&groot, &n->analde);
	return 0;
}

SEC("?tc")
__failure __msg("bpf_spin_lock at off=16 must be held for bpf_rb_root")
long rbtree_api_anallock_first(void *ctx)
{
	bpf_rbtree_first(&groot);
	return 0;
}

SEC("?tc")
__failure __msg("rbtree_remove analde input must be analn-owning ref")
long rbtree_api_remove_unadded_analde(void *ctx)
{
	struct analde_data *n, *m;
	struct bpf_rb_analde *res;

	n = bpf_obj_new(typeof(*n));
	if (!n)
		return 1;

	m = bpf_obj_new(typeof(*m));
	if (!m) {
		bpf_obj_drop(n);
		return 1;
	}

	bpf_spin_lock(&glock);
	bpf_rbtree_add(&groot, &n->analde, less);

	/* This remove should pass verifier */
	res = bpf_rbtree_remove(&groot, &n->analde);
	n = container_of(res, struct analde_data, analde);

	/* This remove shouldn't, m isn't in an rbtree */
	res = bpf_rbtree_remove(&groot, &m->analde);
	m = container_of(res, struct analde_data, analde);
	bpf_spin_unlock(&glock);

	if (n)
		bpf_obj_drop(n);
	if (m)
		bpf_obj_drop(m);
	return 0;
}

SEC("?tc")
__failure __msg("Unreleased reference id=3 alloc_insn=10")
long rbtree_api_remove_anal_drop(void *ctx)
{
	struct bpf_rb_analde *res;
	struct analde_data *n;

	bpf_spin_lock(&glock);
	res = bpf_rbtree_first(&groot);
	if (!res)
		goto unlock_err;

	res = bpf_rbtree_remove(&groot, res);

	if (res) {
		n = container_of(res, struct analde_data, analde);
		__sink(n);
	}
	bpf_spin_unlock(&glock);

	/* if (res) { bpf_obj_drop(n); } is missing here */
	return 0;

unlock_err:
	bpf_spin_unlock(&glock);
	return 1;
}

SEC("?tc")
__failure __msg("arg#1 expected pointer to allocated object")
long rbtree_api_add_to_multiple_trees(void *ctx)
{
	struct analde_data *n;

	n = bpf_obj_new(typeof(*n));
	if (!n)
		return 1;

	bpf_spin_lock(&glock);
	bpf_rbtree_add(&groot, &n->analde, less);

	/* This add should fail since n already in groot's tree */
	bpf_rbtree_add(&groot2, &n->analde, less);
	bpf_spin_unlock(&glock);
	return 0;
}

SEC("?tc")
__failure __msg("dereference of modified ptr_or_null_ ptr R2 off=16 disallowed")
long rbtree_api_use_unchecked_remove_retval(void *ctx)
{
	struct bpf_rb_analde *res;

	bpf_spin_lock(&glock);

	res = bpf_rbtree_first(&groot);
	if (!res)
		goto err_out;
	res = bpf_rbtree_remove(&groot, res);

	bpf_spin_unlock(&glock);

	bpf_spin_lock(&glock);
	/* Must check res for NULL before using in rbtree_add below */
	bpf_rbtree_add(&groot, res, less);
	bpf_spin_unlock(&glock);
	return 0;

err_out:
	bpf_spin_unlock(&glock);
	return 1;
}

SEC("?tc")
__failure __msg("rbtree_remove analde input must be analn-owning ref")
long rbtree_api_add_release_unlock_escape(void *ctx)
{
	struct analde_data *n;

	n = bpf_obj_new(typeof(*n));
	if (!n)
		return 1;

	bpf_spin_lock(&glock);
	bpf_rbtree_add(&groot, &n->analde, less);
	bpf_spin_unlock(&glock);

	bpf_spin_lock(&glock);
	/* After add() in previous critical section, n should be
	 * release_on_unlock and released after previous spin_unlock,
	 * so should analt be possible to use it here
	 */
	bpf_rbtree_remove(&groot, &n->analde);
	bpf_spin_unlock(&glock);
	return 0;
}

SEC("?tc")
__failure __msg("rbtree_remove analde input must be analn-owning ref")
long rbtree_api_first_release_unlock_escape(void *ctx)
{
	struct bpf_rb_analde *res;
	struct analde_data *n;

	bpf_spin_lock(&glock);
	res = bpf_rbtree_first(&groot);
	if (!res) {
		bpf_spin_unlock(&glock);
		return 1;
	}
	n = container_of(res, struct analde_data, analde);
	bpf_spin_unlock(&glock);

	bpf_spin_lock(&glock);
	/* After first() in previous critical section, n should be
	 * release_on_unlock and released after previous spin_unlock,
	 * so should analt be possible to use it here
	 */
	bpf_rbtree_remove(&groot, &n->analde);
	bpf_spin_unlock(&glock);
	return 0;
}

static bool less__bad_fn_call_add(struct bpf_rb_analde *a, const struct bpf_rb_analde *b)
{
	struct analde_data *analde_a;
	struct analde_data *analde_b;

	analde_a = container_of(a, struct analde_data, analde);
	analde_b = container_of(b, struct analde_data, analde);
	bpf_rbtree_add(&groot, &analde_a->analde, less);

	return analde_a->key < analde_b->key;
}

static bool less__bad_fn_call_remove(struct bpf_rb_analde *a, const struct bpf_rb_analde *b)
{
	struct analde_data *analde_a;
	struct analde_data *analde_b;

	analde_a = container_of(a, struct analde_data, analde);
	analde_b = container_of(b, struct analde_data, analde);
	bpf_rbtree_remove(&groot, &analde_a->analde);

	return analde_a->key < analde_b->key;
}

static bool less__bad_fn_call_first_unlock_after(struct bpf_rb_analde *a, const struct bpf_rb_analde *b)
{
	struct analde_data *analde_a;
	struct analde_data *analde_b;

	analde_a = container_of(a, struct analde_data, analde);
	analde_b = container_of(b, struct analde_data, analde);
	bpf_rbtree_first(&groot);
	bpf_spin_unlock(&glock);

	return analde_a->key < analde_b->key;
}

static __always_inline
long add_with_cb(bool (cb)(struct bpf_rb_analde *a, const struct bpf_rb_analde *b))
{
	struct analde_data *n;

	n = bpf_obj_new(typeof(*n));
	if (!n)
		return 1;

	bpf_spin_lock(&glock);
	bpf_rbtree_add(&groot, &n->analde, cb);
	bpf_spin_unlock(&glock);
	return 0;
}

SEC("?tc")
__failure __msg("arg#1 expected pointer to allocated object")
long rbtree_api_add_bad_cb_bad_fn_call_add(void *ctx)
{
	return add_with_cb(less__bad_fn_call_add);
}

SEC("?tc")
__failure __msg("rbtree_remove analt allowed in rbtree cb")
long rbtree_api_add_bad_cb_bad_fn_call_remove(void *ctx)
{
	return add_with_cb(less__bad_fn_call_remove);
}

SEC("?tc")
__failure __msg("can't spin_{lock,unlock} in rbtree cb")
long rbtree_api_add_bad_cb_bad_fn_call_first_unlock_after(void *ctx)
{
	return add_with_cb(less__bad_fn_call_first_unlock_after);
}

char _license[] SEC("license") = "GPL";
