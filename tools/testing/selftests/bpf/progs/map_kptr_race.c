// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2026 Meta Platforms, Inc. and affiliates. */
#include <vmlinux.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include "../test_kmods/bpf_testmod_kfunc.h"

struct map_value {
	struct prog_test_ref_kfunc __kptr *ref_ptr;
};

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(map_flags, BPF_F_NO_PREALLOC);
	__type(key, int);
	__type(value, struct map_value);
	__uint(max_entries, 1);
} race_hash_map SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_PERCPU_HASH);
	__uint(map_flags, BPF_F_NO_PREALLOC);
	__type(key, int);
	__type(value, struct map_value);
	__uint(max_entries, 1);
} race_percpu_hash_map SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_SK_STORAGE);
	__uint(map_flags, BPF_F_NO_PREALLOC);
	__type(key, int);
	__type(value, struct map_value);
} race_sk_ls_map SEC(".maps");

int num_of_refs;
int sk_ls_leak_done;
int target_map_id;
int map_freed;
const volatile int nr_cpus;

SEC("tc")
int test_htab_leak(struct __sk_buff *skb)
{
	struct prog_test_ref_kfunc *p, *old;
	struct map_value val = {};
	struct map_value *v;
	int key = 0;

	if (bpf_map_update_elem(&race_hash_map, &key, &val, BPF_ANY))
		return 1;

	v = bpf_map_lookup_elem(&race_hash_map, &key);
	if (!v)
		return 2;

	p = bpf_kfunc_call_test_acquire(&(unsigned long){0});
	if (!p)
		return 3;
	old = bpf_kptr_xchg(&v->ref_ptr, p);
	if (old)
		bpf_kfunc_call_test_release(old);

	bpf_map_delete_elem(&race_hash_map, &key);

	p = bpf_kfunc_call_test_acquire(&(unsigned long){0});
	if (!p)
		return 4;
	old = bpf_kptr_xchg(&v->ref_ptr, p);
	if (old)
		bpf_kfunc_call_test_release(old);

	return 0;
}

static int fill_percpu_kptr(struct map_value *v)
{
	struct prog_test_ref_kfunc *p, *old;

	p = bpf_kfunc_call_test_acquire(&(unsigned long){0});
	if (!p)
		return 1;
	old = bpf_kptr_xchg(&v->ref_ptr, p);
	if (old)
		bpf_kfunc_call_test_release(old);
	return 0;
}

SEC("tc")
int test_percpu_htab_leak(struct __sk_buff *skb)
{
	struct map_value *v, *arr[16] = {};
	struct map_value val = {};
	int key = 0;
	int err = 0;

	if (bpf_map_update_elem(&race_percpu_hash_map, &key, &val, BPF_ANY))
		return 1;

	for (int i = 0; i < nr_cpus; i++) {
		v = bpf_map_lookup_percpu_elem(&race_percpu_hash_map, &key, i);
		if (!v)
			return 2;
		arr[i] = v;
	}

	bpf_map_delete_elem(&race_percpu_hash_map, &key);

	for (int i = 0; i < nr_cpus; i++) {
		v = arr[i];
		err = fill_percpu_kptr(v);
		if (err)
			return 3;
	}

	return 0;
}

SEC("tp_btf/inet_sock_set_state")
int BPF_PROG(test_sk_ls_leak, struct sock *sk, int oldstate, int newstate)
{
	struct prog_test_ref_kfunc *p, *old;
	struct map_value *v;

	if (newstate != BPF_TCP_SYN_SENT)
		return 0;

	if (sk_ls_leak_done)
		return 0;

	v = bpf_sk_storage_get(&race_sk_ls_map, sk, NULL,
				BPF_SK_STORAGE_GET_F_CREATE);
	if (!v)
		return 0;

	p = bpf_kfunc_call_test_acquire(&(unsigned long){0});
	if (!p)
		return 0;
	old = bpf_kptr_xchg(&v->ref_ptr, p);
	if (old)
		bpf_kfunc_call_test_release(old);

	bpf_sk_storage_delete(&race_sk_ls_map, sk);

	p = bpf_kfunc_call_test_acquire(&(unsigned long){0});
	if (!p)
		return 0;
	old = bpf_kptr_xchg(&v->ref_ptr, p);
	if (old)
		bpf_kfunc_call_test_release(old);

	sk_ls_leak_done = 1;
	return 0;
}

long target_map_ptr;

SEC("fentry/bpf_map_put")
int BPF_PROG(map_put, struct bpf_map *map)
{
	if (target_map_id && map->id == (u32)target_map_id)
		target_map_ptr = (long)map;
	return 0;
}

SEC("fexit/htab_map_free")
int BPF_PROG(htab_map_free, struct bpf_map *map)
{
	if (target_map_ptr && (long)map == target_map_ptr)
		map_freed = 1;
	return 0;
}

SEC("fexit/bpf_sk_storage_map_free")
int BPF_PROG(sk_map_free, struct bpf_map *map)
{
	if (target_map_ptr && (long)map == target_map_ptr)
		map_freed = 1;
	return 0;
}

SEC("syscall")
int count_ref(void *ctx)
{
	struct prog_test_ref_kfunc *p;
	unsigned long arg = 0;

	p = bpf_kfunc_call_test_acquire(&arg);
	if (!p)
		return 1;

	num_of_refs = p->cnt.refs.counter;

	bpf_kfunc_call_test_release(p);
	return 0;
}

char _license[] SEC("license") = "GPL";
