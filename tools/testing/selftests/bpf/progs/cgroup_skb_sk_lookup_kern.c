// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2020 Facebook

#include <linux/bpf.h>
#include <bpf/bpf_endian.h>
#include <bpf/bpf_helpers.h>

#include <linux/if_ether.h>
#include <linux/in.h>
#include <linux/in6.h>
#include <linux/ipv6.h>
#include <linux/tcp.h>

#include <sys/types.h>
#include <sys/socket.h>

int _version SEC("version") = 1;
char _license[] SEC("license") = "GPL";

__u16 g_serv_port = 0;

static inline void set_ip(__u32 *dst, const struct in6_addr *src)
{
	dst[0] = src->in6_u.u6_addr32[0];
	dst[1] = src->in6_u.u6_addr32[1];
	dst[2] = src->in6_u.u6_addr32[2];
	dst[3] = src->in6_u.u6_addr32[3];
}

static inline void set_tuple(struct bpf_sock_tuple *tuple,
			     const struct ipv6hdr *ip6h,
			     const struct tcphdr *tcph)
{
	set_ip(tuple->ipv6.saddr, &ip6h->daddr);
	set_ip(tuple->ipv6.daddr, &ip6h->saddr);
	tuple->ipv6.sport = tcph->dest;
	tuple->ipv6.dport = tcph->source;
}

static inline int is_allowed_peer_cg(struct __sk_buff *skb,
				     const struct ipv6hdr *ip6h,
				     const struct tcphdr *tcph)
{
	__u64 cgid, acgid, peer_cgid, peer_acgid;
	struct bpf_sock_tuple tuple;
	size_t tuple_len = sizeof(tuple.ipv6);
	struct bpf_sock *peer_sk;

	set_tuple(&tuple, ip6h, tcph);

	peer_sk = bpf_sk_lookup_tcp(skb, &tuple, tuple_len,
				    BPF_F_CURRENT_NETNS, 0);
	if (!peer_sk)
		return 0;

	cgid = bpf_skb_cgroup_id(skb);
	peer_cgid = bpf_sk_cgroup_id(peer_sk);

	acgid = bpf_skb_ancestor_cgroup_id(skb, 2);
	peer_acgid = bpf_sk_ancestor_cgroup_id(peer_sk, 2);

	bpf_sk_release(peer_sk);

	return cgid && cgid == peer_cgid && acgid && acgid == peer_acgid;
}

SEC("cgroup_skb/ingress")
int ingress_lookup(struct __sk_buff *skb)
{
	__u32 serv_port_key = 0;
	struct ipv6hdr ip6h;
	struct tcphdr tcph;

	if (skb->protocol != bpf_htons(ETH_P_IPV6))
		return 1;

	/* For SYN packets coming to listening socket skb->remote_port will be
	 * zero, so IPv6/TCP headers are loaded to identify remote peer
	 * instead.
	 */
	if (bpf_skb_load_bytes(skb, 0, &ip6h, sizeof(ip6h)))
		return 1;

	if (ip6h.nexthdr != IPPROTO_TCP)
		return 1;

	if (bpf_skb_load_bytes(skb, sizeof(ip6h), &tcph, sizeof(tcph)))
		return 1;

	if (!g_serv_port)
		return 0;

	if (tcph.dest != g_serv_port)
		return 1;

	return is_allowed_peer_cg(skb, &ip6h, &tcph);
}
