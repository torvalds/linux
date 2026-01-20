// SPDX-License-Identifier: GPL-2.0

#include "bpf_tracing_net.h"

char _license[] SEC("license") = "GPL";

int sk_index;
int redirect_idx;
int trace_port;
int helper_ret;
struct {
	__uint(type, BPF_MAP_TYPE_SOCKMAP);
	__uint(key_size, sizeof(__u32));
	__uint(value_size, sizeof(__u32));
	__uint(max_entries, 100);
} sock_map SEC(".maps");

SEC("sockops")
int mptcp_sockmap_inject(struct bpf_sock_ops *skops)
{
	struct bpf_sock *sk;

	/* only accept specified connection */
	if (skops->local_port != trace_port ||
	    skops->op != BPF_SOCK_OPS_PASSIVE_ESTABLISHED_CB)
		return 1;

	sk = skops->sk;
	if (!sk)
		return 1;

	/* update sk handler */
	helper_ret = bpf_sock_map_update(skops, &sock_map, &sk_index, BPF_NOEXIST);

	return 1;
}

SEC("sk_skb/stream_verdict")
int mptcp_sockmap_redirect(struct __sk_buff *skb)
{
	/* redirect skb to the sk under sock_map[redirect_idx] */
	return bpf_sk_redirect_map(skb, &sock_map, redirect_idx, 0);
}
