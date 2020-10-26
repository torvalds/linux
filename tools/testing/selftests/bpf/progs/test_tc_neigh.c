// SPDX-License-Identifier: GPL-2.0
#include <stdint.h>
#include <stdbool.h>

#include <linux/bpf.h>
#include <linux/stddef.h>
#include <linux/pkt_cls.h>
#include <linux/if_ether.h>
#include <linux/in.h>
#include <linux/ip.h>
#include <linux/ipv6.h>

#include <bpf/bpf_helpers.h>
#include <bpf/bpf_endian.h>

#ifndef ctx_ptr
# define ctx_ptr(field)		(void *)(long)(field)
#endif

#define ip4_src			0xac100164 /* 172.16.1.100 */
#define ip4_dst			0xac100264 /* 172.16.2.100 */

#define ip6_src			{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, \
				  0x00, 0x01, 0xde, 0xad, 0xbe, 0xef, 0xca, 0xfe }
#define ip6_dst			{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, \
				  0x00, 0x02, 0xde, 0xad, 0xbe, 0xef, 0xca, 0xfe }

#ifndef v6_equal
# define v6_equal(a, b)		(a.s6_addr32[0] == b.s6_addr32[0] && \
				 a.s6_addr32[1] == b.s6_addr32[1] && \
				 a.s6_addr32[2] == b.s6_addr32[2] && \
				 a.s6_addr32[3] == b.s6_addr32[3])
#endif

enum {
	dev_src,
	dev_dst,
};

struct bpf_map_def SEC("maps") ifindex_map = {
	.type		= BPF_MAP_TYPE_ARRAY,
	.key_size	= sizeof(int),
	.value_size	= sizeof(int),
	.max_entries	= 2,
};

static __always_inline bool is_remote_ep_v4(struct __sk_buff *skb,
					    __be32 addr)
{
	void *data_end = ctx_ptr(skb->data_end);
	void *data = ctx_ptr(skb->data);
	struct iphdr *ip4h;

	if (data + sizeof(struct ethhdr) > data_end)
		return false;

	ip4h = (struct iphdr *)(data + sizeof(struct ethhdr));
	if ((void *)(ip4h + 1) > data_end)
		return false;

	return ip4h->daddr == addr;
}

static __always_inline bool is_remote_ep_v6(struct __sk_buff *skb,
					    struct in6_addr addr)
{
	void *data_end = ctx_ptr(skb->data_end);
	void *data = ctx_ptr(skb->data);
	struct ipv6hdr *ip6h;

	if (data + sizeof(struct ethhdr) > data_end)
		return false;

	ip6h = (struct ipv6hdr *)(data + sizeof(struct ethhdr));
	if ((void *)(ip6h + 1) > data_end)
		return false;

	return v6_equal(ip6h->daddr, addr);
}

static __always_inline int get_dev_ifindex(int which)
{
	int *ifindex = bpf_map_lookup_elem(&ifindex_map, &which);

	return ifindex ? *ifindex : 0;
}

SEC("chk_egress") int tc_chk(struct __sk_buff *skb)
{
	void *data_end = ctx_ptr(skb->data_end);
	void *data = ctx_ptr(skb->data);
	__u32 *raw = data;

	if (data + sizeof(struct ethhdr) > data_end)
		return TC_ACT_SHOT;

	return !raw[0] && !raw[1] && !raw[2] ? TC_ACT_SHOT : TC_ACT_OK;
}

SEC("dst_ingress") int tc_dst(struct __sk_buff *skb)
{
	__u8 zero[ETH_ALEN * 2];
	bool redirect = false;

	switch (skb->protocol) {
	case __bpf_constant_htons(ETH_P_IP):
		redirect = is_remote_ep_v4(skb, __bpf_constant_htonl(ip4_src));
		break;
	case __bpf_constant_htons(ETH_P_IPV6):
		redirect = is_remote_ep_v6(skb, (struct in6_addr)ip6_src);
		break;
	}

	if (!redirect)
		return TC_ACT_OK;

	__builtin_memset(&zero, 0, sizeof(zero));
	if (bpf_skb_store_bytes(skb, 0, &zero, sizeof(zero), 0) < 0)
		return TC_ACT_SHOT;

	return bpf_redirect_neigh(get_dev_ifindex(dev_src), 0);
}

SEC("src_ingress") int tc_src(struct __sk_buff *skb)
{
	__u8 zero[ETH_ALEN * 2];
	bool redirect = false;

	switch (skb->protocol) {
	case __bpf_constant_htons(ETH_P_IP):
		redirect = is_remote_ep_v4(skb, __bpf_constant_htonl(ip4_dst));
		break;
	case __bpf_constant_htons(ETH_P_IPV6):
		redirect = is_remote_ep_v6(skb, (struct in6_addr)ip6_dst);
		break;
	}

	if (!redirect)
		return TC_ACT_OK;

	__builtin_memset(&zero, 0, sizeof(zero));
	if (bpf_skb_store_bytes(skb, 0, &zero, sizeof(zero), 0) < 0)
		return TC_ACT_SHOT;

	return bpf_redirect_neigh(get_dev_ifindex(dev_dst), 0);
}

char __license[] SEC("license") = "GPL";
