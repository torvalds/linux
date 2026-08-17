// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2023 Meta Platforms, Inc. and affiliates. */

#include <linux/types.h>
#include <linux/bpf.h>
#include <linux/pkt_cls.h>
#include <linux/if_ether.h>
#include <linux/ip.h>
#include <linux/in.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_endian.h>

struct bpf_fib_lookup fib_params = {};
int fib_lookup_ret = 0;
int lookup_flags = 0;

SEC("tc")
int fib_lookup(struct __sk_buff *skb)
{
	fib_lookup_ret = bpf_fib_lookup(skb, &fib_params, sizeof(fib_params),
					lookup_flags);

	return TC_ACT_SHOT;
}

SEC("xdp")
int fib_lookup_xdp(struct xdp_md *ctx)
{
	fib_lookup_ret = bpf_fib_lookup(ctx, &fib_params, sizeof(fib_params),
					lookup_flags);

	return XDP_DROP;
}

int redirected = 0;
int passed = 0;
int delivered = 0;

SEC("xdp")
int fib_lookup_redirect(struct xdp_md *ctx)
{
	struct bpf_fib_lookup params = fib_params;
	long ret;

	ret = bpf_fib_lookup(ctx, &params, sizeof(params), lookup_flags);
	if (ret == BPF_FIB_LKUP_RET_SUCCESS) {
		redirected++;
		return bpf_redirect(params.ifindex, 0);
	}

	passed++;
	return XDP_PASS;
}

SEC("xdp")
int xdp_count(struct xdp_md *ctx)
{
	void *data = (void *)(long)ctx->data;
	void *data_end = (void *)(long)ctx->data_end;
	struct ethhdr *eth = data;
	struct iphdr *iph;

	/*
	 * count only the test's TCP frames: the netns has live
	 * link-local traffic (DAD, MLD) that would satisfy a bare
	 * counter
	 */
	if ((void *)(eth + 1) > data_end ||
	    eth->h_proto != bpf_htons(ETH_P_IP))
		return XDP_DROP;
	iph = (void *)(eth + 1);
	if ((void *)(iph + 1) > data_end || iph->protocol != IPPROTO_TCP)
		return XDP_DROP;

	delivered++;
	return XDP_DROP;
}

char _license[] SEC("license") = "GPL";
