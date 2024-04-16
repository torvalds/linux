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

struct {
	__uint(type, BPF_MAP_TYPE_SOCKHASH);
	__uint(max_entries, 64);
	__type(key, __u32);
	__type(value, __u64);
} dst SEC(".maps");

__u32 elems = 0;
__u32 socks = 0;

SEC("iter/sockmap")
int copy(struct bpf_iter__sockmap *ctx)
{
	struct sock *sk = ctx->sk;
	__u32 tmp, *key = ctx->key;
	int ret;

	if (!key)
		return 0;

	elems++;

	/* We need a temporary buffer on the stack, since the verifier doesn't
	 * let us use the pointer from the context as an argument to the helper.
	 */
	tmp = *key;

	if (sk) {
		socks++;
		return bpf_map_update_elem(&dst, &tmp, sk, 0) != 0;
	}

	ret = bpf_map_delete_elem(&dst, &tmp);
	return ret && ret != -ENOENT;
}
