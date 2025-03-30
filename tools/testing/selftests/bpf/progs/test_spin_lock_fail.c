// SPDX-License-Identifier: GPL-2.0
#include <vmlinux.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_helpers.h>
#include "bpf_experimental.h"

struct foo {
	struct bpf_spin_lock lock;
	int data;
};

struct array_map {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__type(key, int);
	__type(value, struct foo);
	__uint(max_entries, 1);
} array_map SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY_OF_MAPS);
	__uint(max_entries, 1);
	__type(key, int);
	__type(value, int);
	__array(values, struct array_map);
} map_of_maps SEC(".maps") = {
	.values = {
		[0] = &array_map,
	},
};

static struct bpf_spin_lock lockA SEC(".data.A");
static struct bpf_spin_lock lockB SEC(".data.B");

SEC("?tc")
int lock_id_kptr_preserve(void *ctx)
{
	struct foo *f;

	f = bpf_obj_new(typeof(*f));
	if (!f)
		return 0;
	bpf_this_cpu_ptr(f);
	return 0;
}

SEC("?tc")
int lock_id_global_zero(void *ctx)
{
	bpf_this_cpu_ptr(&lockA);
	return 0;
}

SEC("?tc")
int lock_id_mapval_preserve(void *ctx)
{
	struct foo *f;
	int key = 0;

	f = bpf_map_lookup_elem(&array_map, &key);
	if (!f)
		return 0;
	bpf_this_cpu_ptr(f);
	return 0;
}

SEC("?tc")
int lock_id_innermapval_preserve(void *ctx)
{
	struct foo *f;
	int key = 0;
	void *map;

	map = bpf_map_lookup_elem(&map_of_maps, &key);
	if (!map)
		return 0;
	f = bpf_map_lookup_elem(map, &key);
	if (!f)
		return 0;
	bpf_this_cpu_ptr(f);
	return 0;
}

#define CHECK(test, A, B)                                      \
	SEC("?tc")                                             \
	int lock_id_mismatch_##test(void *ctx)                 \
	{                                                      \
		struct foo *f1, *f2, *v, *iv;                  \
		int key = 0;                                   \
		void *map;                                     \
                                                               \
		map = bpf_map_lookup_elem(&map_of_maps, &key); \
		if (!map)                                      \
			return 0;                              \
		iv = bpf_map_lookup_elem(map, &key);           \
		if (!iv)                                       \
			return 0;                              \
		v = bpf_map_lookup_elem(&array_map, &key);     \
		if (!v)                                        \
			return 0;                              \
		f1 = bpf_obj_new(typeof(*f1));                 \
		if (!f1)                                       \
			return 0;                              \
		f2 = bpf_obj_new(typeof(*f2));                 \
		if (!f2) {                                     \
			bpf_obj_drop(f1);                      \
			return 0;                              \
		}                                              \
		bpf_spin_lock(A);                              \
		bpf_spin_unlock(B);                            \
		return 0;                                      \
	}

CHECK(kptr_kptr, &f1->lock, &f2->lock);
CHECK(kptr_global, &f1->lock, &lockA);
CHECK(kptr_mapval, &f1->lock, &v->lock);
CHECK(kptr_innermapval, &f1->lock, &iv->lock);

CHECK(global_global, &lockA, &lockB);
CHECK(global_kptr, &lockA, &f1->lock);
CHECK(global_mapval, &lockA, &v->lock);
CHECK(global_innermapval, &lockA, &iv->lock);

SEC("?tc")
int lock_id_mismatch_mapval_mapval(void *ctx)
{
	struct foo *f1, *f2;
	int key = 0;

	f1 = bpf_map_lookup_elem(&array_map, &key);
	if (!f1)
		return 0;
	f2 = bpf_map_lookup_elem(&array_map, &key);
	if (!f2)
		return 0;

	bpf_spin_lock(&f1->lock);
	f1->data = 42;
	bpf_spin_unlock(&f2->lock);

	return 0;
}

CHECK(mapval_kptr, &v->lock, &f1->lock);
CHECK(mapval_global, &v->lock, &lockB);
CHECK(mapval_innermapval, &v->lock, &iv->lock);

SEC("?tc")
int lock_id_mismatch_innermapval_innermapval1(void *ctx)
{
	struct foo *f1, *f2;
	int key = 0;
	void *map;

	map = bpf_map_lookup_elem(&map_of_maps, &key);
	if (!map)
		return 0;
	f1 = bpf_map_lookup_elem(map, &key);
	if (!f1)
		return 0;
	f2 = bpf_map_lookup_elem(map, &key);
	if (!f2)
		return 0;

	bpf_spin_lock(&f1->lock);
	f1->data = 42;
	bpf_spin_unlock(&f2->lock);

	return 0;
}

SEC("?tc")
int lock_id_mismatch_innermapval_innermapval2(void *ctx)
{
	struct foo *f1, *f2;
	int key = 0;
	void *map;

	map = bpf_map_lookup_elem(&map_of_maps, &key);
	if (!map)
		return 0;
	f1 = bpf_map_lookup_elem(map, &key);
	if (!f1)
		return 0;
	map = bpf_map_lookup_elem(&map_of_maps, &key);
	if (!map)
		return 0;
	f2 = bpf_map_lookup_elem(map, &key);
	if (!f2)
		return 0;

	bpf_spin_lock(&f1->lock);
	f1->data = 42;
	bpf_spin_unlock(&f2->lock);

	return 0;
}

CHECK(innermapval_kptr, &iv->lock, &f1->lock);
CHECK(innermapval_global, &iv->lock, &lockA);
CHECK(innermapval_mapval, &iv->lock, &v->lock);

#undef CHECK

__noinline
int global_subprog(struct __sk_buff *ctx)
{
	volatile int ret = 0;

	if (ctx->protocol)
		ret += ctx->protocol;
	return ret + ctx->mark;
}

__noinline
static int static_subprog_call_global(struct __sk_buff *ctx)
{
	volatile int ret = 0;

	if (ctx->protocol)
		return ret;
	return ret + ctx->len + global_subprog(ctx);
}

SEC("?tc")
int lock_global_subprog_call1(struct __sk_buff *ctx)
{
	int ret = 0;

	bpf_spin_lock(&lockA);
	if (ctx->mark == 42)
		ret = global_subprog(ctx);
	bpf_spin_unlock(&lockA);
	return ret;
}

SEC("?tc")
int lock_global_subprog_call2(struct __sk_buff *ctx)
{
	int ret = 0;

	bpf_spin_lock(&lockA);
	if (ctx->mark == 42)
		ret = static_subprog_call_global(ctx);
	bpf_spin_unlock(&lockA);
	return ret;
}

int __noinline
global_subprog_int(int i)
{
	if (i)
		bpf_printk("%p", &i);
	return i;
}

int __noinline
global_sleepable_helper_subprog(int i)
{
	if (i)
		bpf_copy_from_user(&i, sizeof(i), NULL);
	return i;
}

int __noinline
global_sleepable_kfunc_subprog(int i)
{
	if (i)
		bpf_copy_from_user_str(&i, sizeof(i), NULL, 0);
	global_subprog_int(i);
	return i;
}

int __noinline
global_subprog_calling_sleepable_global(int i)
{
	if (!i)
		global_sleepable_kfunc_subprog(i);
	return i;
}

SEC("?syscall")
int lock_global_sleepable_helper_subprog(struct __sk_buff *ctx)
{
	int ret = 0;

	bpf_spin_lock(&lockA);
	if (ctx->mark == 42)
		ret = global_sleepable_helper_subprog(ctx->mark);
	bpf_spin_unlock(&lockA);
	return ret;
}

SEC("?syscall")
int lock_global_sleepable_kfunc_subprog(struct __sk_buff *ctx)
{
	int ret = 0;

	bpf_spin_lock(&lockA);
	if (ctx->mark == 42)
		ret = global_sleepable_kfunc_subprog(ctx->mark);
	bpf_spin_unlock(&lockA);
	return ret;
}

SEC("?syscall")
int lock_global_sleepable_subprog_indirect(struct __sk_buff *ctx)
{
	int ret = 0;

	bpf_spin_lock(&lockA);
	if (ctx->mark == 42)
		ret = global_subprog_calling_sleepable_global(ctx->mark);
	bpf_spin_unlock(&lockA);
	return ret;
}

char _license[] SEC("license") = "GPL";
