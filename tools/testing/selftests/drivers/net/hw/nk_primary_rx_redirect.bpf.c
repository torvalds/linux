// SPDX-License-Identifier: GPL-2.0
#include <linux/bpf.h>
#include <linux/pkt_cls.h>
#include <linux/if_ether.h>
#include <linux/in.h>
#include <linux/ipv6.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_endian.h>

#define ctx_ptr(field)		((void *)(long)(field))

volatile __u32 phys_ifindex;

SEC("tc/ingress")
int nk_primary_rx_redirect(struct __sk_buff *skb)
{
	void *data_end = ctx_ptr(skb->data_end);
	void *data = ctx_ptr(skb->data);
	struct ethhdr *eth;
	struct ipv6hdr *ip6h;

	eth = data;
	if ((void *)(eth + 1) > data_end)
		return TC_ACT_OK;

	if (eth->h_proto != bpf_htons(ETH_P_IPV6))
		return TC_ACT_OK;

	ip6h = data + sizeof(struct ethhdr);
	if ((void *)(ip6h + 1) > data_end)
		return TC_ACT_OK;

	if (ip6h->nexthdr == IPPROTO_ICMPV6)
		return TC_ACT_OK;

	return bpf_redirect_neigh(phys_ifindex, NULL, 0, 0);
}

char __license[] SEC("license") = "GPL";
