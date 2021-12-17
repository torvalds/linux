// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2020 Cloudflare
#include "vmlinux.h"
#include <bpf/bpf_helpers.h>

struct {
	__uint(type, BPF_MAP_TYPE_SOCKMAP);
	__uint(max_entries, 1);
	__type(key, __u32);
	__type(value, __u64);
} src SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_SOCKMAP);
	__uint(max_entries, 1);
	__type(key, __u32);
	__type(value, __u64);
} dst_sock_map SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_SOCKHASH);
	__uint(max_entries, 1);
	__type(key, __u32);
	__type(value, __u64);
} dst_sock_hash SEC(".maps");

SEC("tc")
int copy_sock_map(void *ctx)
{
	struct bpf_sock *sk;
	bool failed = false;
	__u32 key = 0;

	sk = bpf_map_lookup_elem(&src, &key);
	if (!sk)
		return SK_DROP;

	if (bpf_map_update_elem(&dst_sock_map, &key, sk, 0))
		failed = true;

	if (bpf_map_update_elem(&dst_sock_hash, &key, sk, 0))
		failed = true;

	bpf_sk_release(sk);
	return failed ? SK_DROP : SK_PASS;
}

char _license[] SEC("license") = "GPL";
