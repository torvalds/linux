// SPDX-License-Identifier: GPL-2.0
#include <linux/bpf.h>
#include <linux/pkt_cls.h>
#include <linux/if_ether.h>
#include <linux/ipv6.h>
#include <linux/in6.h>
#include <bpf/bpf_endian.h>
#include <bpf/bpf_helpers.h>

#define TC_ACT_OK 0
#define ETH_P_IPV6 0x86DD

#define ctx_ptr(field)		((void *)(long)(field))

#define v6_p64_equal(a, b)	(a.s6_addr32[0] == b.s6_addr32[0] && \
				 a.s6_addr32[1] == b.s6_addr32[1])

volatile __u32 netkit_ifindex;
volatile __u8 ipv6_prefix[16];

SEC("tc/ingress")
int tc_redirect_peer(struct __sk_buff *skb)
{
	void *data_end = ctx_ptr(skb->data_end);
	void *data = ctx_ptr(skb->data);
	struct in6_addr *peer_addr;
	struct ipv6hdr *ip6h;
	struct ethhdr *eth;

	peer_addr = (struct in6_addr *)ipv6_prefix;

	if (skb->protocol != bpf_htons(ETH_P_IPV6))
		return TC_ACT_OK;

	eth = data;
	if ((void *)(eth + 1) > data_end)
		return TC_ACT_OK;

	ip6h = data + sizeof(struct ethhdr);
	if ((void *)(ip6h + 1) > data_end)
		return TC_ACT_OK;

	if (!v6_p64_equal(ip6h->daddr, (*peer_addr)))
		return TC_ACT_OK;

	return bpf_redirect_peer(netkit_ifindex, 0);
}

char __license[] SEC("license") = "GPL";
