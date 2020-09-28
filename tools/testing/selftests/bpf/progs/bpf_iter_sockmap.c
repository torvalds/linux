// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2020 Cloudflare */
#include "bpf_iter.h"
#include "bpf_tracing_net.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include <errno.h>

char _license[] SEC("license") = "GPL";

struct {
	__uint(type, BPF_MAP_TYPE_SOCKMAP);
	__uint(max_entries, 64);
	__type(key, __u32);
	__type(value, __u64);
} sockmap SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_SOCKHASH);
	__uint(max_entries, 64);
	__type(key, __u32);
	__type(value, __u64);
} sockhash SEC(".maps");

__u32 elems = 0;
__u32 socks = 0;

SEC("iter/sockmap")
int count_elems(struct bpf_iter__sockmap *ctx)
{
	struct sock *sk = ctx->sk;
	__u32 tmp, *key = ctx->key;
	int ret;

	if (key)
		elems++;

	if (sk)
		socks++;

	return 0;
}
