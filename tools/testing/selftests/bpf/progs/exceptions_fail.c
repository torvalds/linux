// SPDX-License-Identifier: GPL-2.0
#include <vmlinux.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_core_read.h>

#include "bpf_misc.h"
#include "bpf_experimental.h"

extern void bpf_rcu_read_lock(void) __ksym;

#define private(name) SEC(".bss." #name) __hidden __attribute__((aligned(8)))

struct foo {
	struct bpf_rb_node node;
};

struct hmap_elem {
	struct bpf_timer timer;
};

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(max_entries, 64);
	__type(key, int);
	__type(value, struct hmap_elem);
} hmap SEC(".maps");

private(A) struct bpf_spin_lock lock;
private(A) struct bpf_rb_root rbtree __contains(foo, node);

__noinline void *exception_cb_bad_ret_type(u64 cookie)
{
	return NULL;
}

__noinline int exception_cb_bad_arg_0(void)
{
	return 0;
}

__noinline int exception_cb_bad_arg_2(int a, int b)
{
	return 0;
}

__noinline int exception_cb_ok_arg_small(int a)
{
	return 0;
}

SEC("?tc")
__exception_cb(exception_cb_bad_ret_type)
__failure __msg("Global function exception_cb_bad_ret_type() doesn't return scalar.")
int reject_exception_cb_type_1(struct __sk_buff *ctx)
{
	bpf_throw(0);
	return 0;
}

SEC("?tc")
__exception_cb(exception_cb_bad_arg_0)
__failure __msg("exception cb only supports single integer argument")
int reject_exception_cb_type_2(struct __sk_buff *ctx)
{
	bpf_throw(0);
	return 0;
}

SEC("?tc")
__exception_cb(exception_cb_bad_arg_2)
__failure __msg("exception cb only supports single integer argument")
int reject_exception_cb_type_3(struct __sk_buff *ctx)
{
	bpf_throw(0);
	return 0;
}

SEC("?tc")
__exception_cb(exception_cb_ok_arg_small)
__success
int reject_exception_cb_type_4(struct __sk_buff *ctx)
{
	bpf_throw(0);
	return 0;
}

__noinline
static int timer_cb(void *map, int *key, struct bpf_timer *timer)
{
	bpf_throw(0);
	return 0;
}

SEC("?tc")
__failure __msg("cannot be called from callback subprog")
int reject_async_callback_throw(struct __sk_buff *ctx)
{
	struct hmap_elem *elem;

	elem = bpf_map_lookup_elem(&hmap, &(int){0});
	if (!elem)
		return 0;
	return bpf_timer_set_callback(&elem->timer, timer_cb);
}

__noinline static int subprog_lock(struct __sk_buff *ctx)
{
	volatile int ret = 0;

	bpf_spin_lock(&lock);
	if (ctx->len)
		bpf_throw(0);
	return ret;
}

SEC("?tc")
__failure __msg("function calls are not allowed while holding a lock")
int reject_with_lock(void *ctx)
{
	bpf_spin_lock(&lock);
	bpf_throw(0);
	return 0;
}

SEC("?tc")
__failure __msg("function calls are not allowed while holding a lock")
int reject_subprog_with_lock(void *ctx)
{
	return subprog_lock(ctx);
}

SEC("?tc")
__failure __msg("BPF_EXIT instruction cannot be used inside bpf_rcu_read_lock-ed region")
int reject_with_rcu_read_lock(void *ctx)
{
	bpf_rcu_read_lock();
	bpf_throw(0);
	return 0;
}

__noinline static int throwing_subprog(struct __sk_buff *ctx)
{
	if (ctx->len)
		bpf_throw(0);
	return 0;
}

SEC("?tc")
__failure __msg("BPF_EXIT instruction cannot be used inside bpf_rcu_read_lock-ed region")
int reject_subprog_with_rcu_read_lock(void *ctx)
{
	bpf_rcu_read_lock();
	return throwing_subprog(ctx);
}

static bool rbless(struct bpf_rb_node *n1, const struct bpf_rb_node *n2)
{
	bpf_throw(0);
	return true;
}

SEC("?tc")
__failure __msg("function calls are not allowed while holding a lock")
int reject_with_rbtree_add_throw(void *ctx)
{
	struct foo *f;

	f = bpf_obj_new(typeof(*f));
	if (!f)
		return 0;
	bpf_spin_lock(&lock);
	bpf_rbtree_add(&rbtree, &f->node, rbless);
	bpf_spin_unlock(&lock);
	return 0;
}

SEC("?tc")
__failure __msg("Unreleased reference")
int reject_with_reference(void *ctx)
{
	struct foo *f;

	f = bpf_obj_new(typeof(*f));
	if (!f)
		return 0;
	bpf_throw(0);
	return 0;
}

__noinline static int subprog_ref(struct __sk_buff *ctx)
{
	struct foo *f;

	f = bpf_obj_new(typeof(*f));
	if (!f)
		return 0;
	bpf_throw(0);
	return 0;
}

__noinline static int subprog_cb_ref(u32 i, void *ctx)
{
	bpf_throw(0);
	return 0;
}

SEC("?tc")
__failure __msg("Unreleased reference")
int reject_with_cb_reference(void *ctx)
{
	struct foo *f;

	f = bpf_obj_new(typeof(*f));
	if (!f)
		return 0;
	bpf_loop(5, subprog_cb_ref, NULL, 0);
	bpf_obj_drop(f);
	return 0;
}

SEC("?tc")
__failure __msg("cannot be called from callback")
int reject_with_cb(void *ctx)
{
	bpf_loop(5, subprog_cb_ref, NULL, 0);
	return 0;
}

SEC("?tc")
__failure __msg("Unreleased reference")
int reject_with_subprog_reference(void *ctx)
{
	return subprog_ref(ctx) + 1;
}

__noinline int throwing_exception_cb(u64 c)
{
	bpf_throw(0);
	return c;
}

__noinline int exception_cb1(u64 c)
{
	return c;
}

__noinline int exception_cb2(u64 c)
{
	return c;
}

static __noinline int static_func(struct __sk_buff *ctx)
{
	return exception_cb1(ctx->tstamp);
}

__noinline int global_func(struct __sk_buff *ctx)
{
	return exception_cb1(ctx->tstamp);
}

SEC("?tc")
__exception_cb(throwing_exception_cb)
__failure __msg("cannot be called from callback subprog")
int reject_throwing_exception_cb(struct __sk_buff *ctx)
{
	return 0;
}

SEC("?tc")
__exception_cb(exception_cb1)
__failure __msg("cannot call exception cb directly")
int reject_exception_cb_call_global_func(struct __sk_buff *ctx)
{
	return global_func(ctx);
}

SEC("?tc")
__exception_cb(exception_cb1)
__failure __msg("cannot call exception cb directly")
int reject_exception_cb_call_static_func(struct __sk_buff *ctx)
{
	return static_func(ctx);
}

SEC("?tc")
__exception_cb(exception_cb1)
__exception_cb(exception_cb2)
__failure __msg("multiple exception callback tags for main subprog")
int reject_multiple_exception_cb(struct __sk_buff *ctx)
{
	bpf_throw(0);
	return 16;
}

__noinline int exception_cb_bad_ret(u64 c)
{
	return c;
}

SEC("?fentry/bpf_check")
__exception_cb(exception_cb_bad_ret)
__failure __msg("At program exit the register R0 has unknown scalar value should")
int reject_set_exception_cb_bad_ret1(void *ctx)
{
	return 0;
}

SEC("?fentry/bpf_check")
__failure __msg("At program exit the register R1 has smin=64 smax=64 should")
int reject_set_exception_cb_bad_ret2(void *ctx)
{
	bpf_throw(64);
	return 0;
}

__noinline static int loop_cb1(u32 index, int *ctx)
{
	bpf_throw(0);
	return 0;
}

__noinline static int loop_cb2(u32 index, int *ctx)
{
	bpf_throw(0);
	return 0;
}

SEC("?tc")
__failure __msg("cannot be called from callback")
int reject_exception_throw_cb(struct __sk_buff *ctx)
{
	bpf_loop(5, loop_cb1, NULL, 0);
	return 0;
}

SEC("?tc")
__failure __msg("cannot be called from callback")
int reject_exception_throw_cb_diff(struct __sk_buff *ctx)
{
	if (ctx->protocol)
		bpf_loop(5, loop_cb1, NULL, 0);
	else
		bpf_loop(5, loop_cb2, NULL, 0);
	return 0;
}

char _license[] SEC("license") = "GPL";
