// SPDX-License-Identifier: GPL-2.0
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

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

#define AF_INET 2
#define AF_INET6 10

static __always_inline int fill_fib_params_v4(struct __sk_buff *skb,
					      struct bpf_fib_lookup *fib_params)
{
	void *data_end = ctx_ptr(skb->data_end);
	void *data = ctx_ptr(skb->data);
	struct iphdr *ip4h;

	if (data + sizeof(struct ethhdr) > data_end)
		return -1;

	ip4h = (struct iphdr *)(data + sizeof(struct ethhdr));
	if ((void *)(ip4h + 1) > data_end)
		return -1;

	fib_params->family = AF_INET;
	fib_params->tos = ip4h->tos;
	fib_params->l4_protocol = ip4h->protocol;
	fib_params->sport = 0;
	fib_params->dport = 0;
	fib_params->tot_len = bpf_ntohs(ip4h->tot_len);
	fib_params->ipv4_src = ip4h->saddr;
	fib_params->ipv4_dst = ip4h->daddr;

	return 0;
}

static __always_inline int fill_fib_params_v6(struct __sk_buff *skb,
					      struct bpf_fib_lookup *fib_params)
{
	struct in6_addr *src = (struct in6_addr *)fib_params->ipv6_src;
	struct in6_addr *dst = (struct in6_addr *)fib_params->ipv6_dst;
	void *data_end = ctx_ptr(skb->data_end);
	void *data = ctx_ptr(skb->data);
	struct ipv6hdr *ip6h;

	if (data + sizeof(struct ethhdr) > data_end)
		return -1;

	ip6h = (struct ipv6hdr *)(data + sizeof(struct ethhdr));
	if ((void *)(ip6h + 1) > data_end)
		return -1;

	fib_params->family = AF_INET6;
	fib_params->flowinfo = 0;
	fib_params->l4_protocol = ip6h->nexthdr;
	fib_params->sport = 0;
	fib_params->dport = 0;
	fib_params->tot_len = bpf_ntohs(ip6h->payload_len);
	*src = ip6h->saddr;
	*dst = ip6h->daddr;

	return 0;
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

static __always_inline int tc_redir(struct __sk_buff *skb)
{
	struct bpf_fib_lookup fib_params = { .ifindex = skb->ingress_ifindex };
	__u8 zero[ETH_ALEN * 2];
	int ret = -1;

	switch (skb->protocol) {
	case __bpf_constant_htons(ETH_P_IP):
		ret = fill_fib_params_v4(skb, &fib_params);
		break;
	case __bpf_constant_htons(ETH_P_IPV6):
		ret = fill_fib_params_v6(skb, &fib_params);
		break;
	}

	if (ret)
		return TC_ACT_OK;

	ret = bpf_fib_lookup(skb, &fib_params, sizeof(fib_params), 0);
	if (ret == BPF_FIB_LKUP_RET_NOT_FWDED || ret < 0)
		return TC_ACT_OK;

	__builtin_memset(&zero, 0, sizeof(zero));
	if (bpf_skb_store_bytes(skb, 0, &zero, sizeof(zero), 0) < 0)
		return TC_ACT_SHOT;

	if (ret == BPF_FIB_LKUP_RET_NO_NEIGH) {
		struct bpf_redir_neigh nh_params = {};

		nh_params.nh_family = fib_params.family;
		__builtin_memcpy(&nh_params.ipv6_nh, &fib_params.ipv6_dst,
				 sizeof(nh_params.ipv6_nh));

		return bpf_redirect_neigh(fib_params.ifindex, &nh_params,
					  sizeof(nh_params), 0);

	} else if (ret == BPF_FIB_LKUP_RET_SUCCESS) {
		void *data_end = ctx_ptr(skb->data_end);
		struct ethhdr *eth = ctx_ptr(skb->data);

		if (eth + 1 > data_end)
			return TC_ACT_SHOT;

		__builtin_memcpy(eth->h_dest, fib_params.dmac, ETH_ALEN);
		__builtin_memcpy(eth->h_source, fib_params.smac, ETH_ALEN);

		return bpf_redirect(fib_params.ifindex, 0);
	}

	return TC_ACT_SHOT;
}

/* these are identical, but keep them separate for compatibility with the
 * section names expected by test_tc_redirect.sh
 */
SEC("dst_ingress") int tc_dst(struct __sk_buff *skb)
{
	return tc_redir(skb);
}

SEC("src_ingress") int tc_src(struct __sk_buff *skb)
{
	return tc_redir(skb);
}

char __license[] SEC("license") = "GPL";
