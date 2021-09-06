// SPDX-License-Identifier: GPL-2.0

/* In-place tunneling */

#include <stdbool.h>
#include <string.h>

#include <linux/stddef.h>
#include <linux/bpf.h>
#include <linux/if_ether.h>
#include <linux/in.h>
#include <linux/ip.h>
#include <linux/ipv6.h>
#include <linux/mpls.h>
#include <linux/tcp.h>
#include <linux/udp.h>
#include <linux/pkt_cls.h>
#include <linux/types.h>

#include <bpf/bpf_endian.h>
#include <bpf/bpf_helpers.h>

static const int cfg_port = 8000;

static const int cfg_udp_src = 20000;

#define	L2_PAD_SZ	(sizeof(struct vxlanhdr) + ETH_HLEN)

#define	UDP_PORT		5555
#define	MPLS_OVER_UDP_PORT	6635
#define	ETH_OVER_UDP_PORT	7777
#define	VXLAN_UDP_PORT		8472

#define	EXTPROTO_VXLAN	0x1

#define	VXLAN_N_VID     (1u << 24)
#define	VXLAN_VNI_MASK	bpf_htonl((VXLAN_N_VID - 1) << 8)
#define	VXLAN_FLAGS     0x8
#define	VXLAN_VNI       1

/* MPLS label 1000 with S bit (last label) set and ttl of 255. */
static const __u32 mpls_label = __bpf_constant_htonl(1000 << 12 |
						     MPLS_LS_S_MASK | 0xff);

struct vxlanhdr {
	__be32 vx_flags;
	__be32 vx_vni;
} __attribute__((packed));

struct gre_hdr {
	__be16 flags;
	__be16 protocol;
} __attribute__((packed));

union l4hdr {
	struct udphdr udp;
	struct gre_hdr gre;
};

struct v4hdr {
	struct iphdr ip;
	union l4hdr l4hdr;
	__u8 pad[L2_PAD_SZ];		/* space for L2 header / vxlan header ... */
} __attribute__((packed));

struct v6hdr {
	struct ipv6hdr ip;
	union l4hdr l4hdr;
	__u8 pad[L2_PAD_SZ];		/* space for L2 header / vxlan header ... */
} __attribute__((packed));

static __always_inline void set_ipv4_csum(struct iphdr *iph)
{
	__u16 *iph16 = (__u16 *)iph;
	__u32 csum;
	int i;

	iph->check = 0;

#pragma clang loop unroll(full)
	for (i = 0, csum = 0; i < sizeof(*iph) >> 1; i++)
		csum += *iph16++;

	iph->check = ~((csum & 0xffff) + (csum >> 16));
}

static __always_inline int __encap_ipv4(struct __sk_buff *skb, __u8 encap_proto,
					__u16 l2_proto, __u16 ext_proto)
{
	__u16 udp_dst = UDP_PORT;
	struct iphdr iph_inner;
	struct v4hdr h_outer;
	struct tcphdr tcph;
	int olen, l2_len;
	__u8 *l2_hdr = NULL;
	int tcp_off;
	__u64 flags;

	/* Most tests encapsulate a packet into a tunnel with the same
	 * network protocol, and derive the outer header fields from
	 * the inner header.
	 *
	 * The 6in4 case tests different inner and outer protocols. As
	 * the inner is ipv6, but the outer expects an ipv4 header as
	 * input, manually build a struct iphdr based on the ipv6hdr.
	 */
	if (encap_proto == IPPROTO_IPV6) {
		const __u32 saddr = (192 << 24) | (168 << 16) | (1 << 8) | 1;
		const __u32 daddr = (192 << 24) | (168 << 16) | (1 << 8) | 2;
		struct ipv6hdr iph6_inner;

		/* Read the IPv6 header */
		if (bpf_skb_load_bytes(skb, ETH_HLEN, &iph6_inner,
				       sizeof(iph6_inner)) < 0)
			return TC_ACT_OK;

		/* Derive the IPv4 header fields from the IPv6 header */
		memset(&iph_inner, 0, sizeof(iph_inner));
		iph_inner.version = 4;
		iph_inner.ihl = 5;
		iph_inner.tot_len = bpf_htons(sizeof(iph6_inner) +
				    bpf_ntohs(iph6_inner.payload_len));
		iph_inner.ttl = iph6_inner.hop_limit - 1;
		iph_inner.protocol = iph6_inner.nexthdr;
		iph_inner.saddr = __bpf_constant_htonl(saddr);
		iph_inner.daddr = __bpf_constant_htonl(daddr);

		tcp_off = sizeof(iph6_inner);
	} else {
		if (bpf_skb_load_bytes(skb, ETH_HLEN, &iph_inner,
				       sizeof(iph_inner)) < 0)
			return TC_ACT_OK;

		tcp_off = sizeof(iph_inner);
	}

	/* filter only packets we want */
	if (iph_inner.ihl != 5 || iph_inner.protocol != IPPROTO_TCP)
		return TC_ACT_OK;

	if (bpf_skb_load_bytes(skb, ETH_HLEN + tcp_off,
			       &tcph, sizeof(tcph)) < 0)
		return TC_ACT_OK;

	if (tcph.dest != __bpf_constant_htons(cfg_port))
		return TC_ACT_OK;

	olen = sizeof(h_outer.ip);
	l2_len = 0;

	flags = BPF_F_ADJ_ROOM_FIXED_GSO | BPF_F_ADJ_ROOM_ENCAP_L3_IPV4;

	switch (l2_proto) {
	case ETH_P_MPLS_UC:
		l2_len = sizeof(mpls_label);
		udp_dst = MPLS_OVER_UDP_PORT;
		break;
	case ETH_P_TEB:
		l2_len = ETH_HLEN;
		if (ext_proto & EXTPROTO_VXLAN) {
			udp_dst = VXLAN_UDP_PORT;
			l2_len += sizeof(struct vxlanhdr);
		} else
			udp_dst = ETH_OVER_UDP_PORT;
		break;
	}
	flags |= BPF_F_ADJ_ROOM_ENCAP_L2(l2_len);

	switch (encap_proto) {
	case IPPROTO_GRE:
		flags |= BPF_F_ADJ_ROOM_ENCAP_L4_GRE;
		olen += sizeof(h_outer.l4hdr.gre);
		h_outer.l4hdr.gre.protocol = bpf_htons(l2_proto);
		h_outer.l4hdr.gre.flags = 0;
		break;
	case IPPROTO_UDP:
		flags |= BPF_F_ADJ_ROOM_ENCAP_L4_UDP;
		olen += sizeof(h_outer.l4hdr.udp);
		h_outer.l4hdr.udp.source = __bpf_constant_htons(cfg_udp_src);
		h_outer.l4hdr.udp.dest = bpf_htons(udp_dst);
		h_outer.l4hdr.udp.check = 0;
		h_outer.l4hdr.udp.len = bpf_htons(bpf_ntohs(iph_inner.tot_len) +
						  sizeof(h_outer.l4hdr.udp) +
						  l2_len);
		break;
	case IPPROTO_IPIP:
	case IPPROTO_IPV6:
		break;
	default:
		return TC_ACT_OK;
	}

	/* add L2 encap (if specified) */
	l2_hdr = (__u8 *)&h_outer + olen;
	switch (l2_proto) {
	case ETH_P_MPLS_UC:
		*(__u32 *)l2_hdr = mpls_label;
		break;
	case ETH_P_TEB:
		flags |= BPF_F_ADJ_ROOM_ENCAP_L2_ETH;

		if (ext_proto & EXTPROTO_VXLAN) {
			struct vxlanhdr *vxlan_hdr = (struct vxlanhdr *)l2_hdr;

			vxlan_hdr->vx_flags = VXLAN_FLAGS;
			vxlan_hdr->vx_vni = bpf_htonl((VXLAN_VNI & VXLAN_VNI_MASK) << 8);

			l2_hdr += sizeof(struct vxlanhdr);
		}

		if (bpf_skb_load_bytes(skb, 0, l2_hdr, ETH_HLEN))
			return TC_ACT_SHOT;

		break;
	}
	olen += l2_len;

	/* add room between mac and network header */
	if (bpf_skb_adjust_room(skb, olen, BPF_ADJ_ROOM_MAC, flags))
		return TC_ACT_SHOT;

	/* prepare new outer network header */
	h_outer.ip = iph_inner;
	h_outer.ip.tot_len = bpf_htons(olen +
				       bpf_ntohs(h_outer.ip.tot_len));
	h_outer.ip.protocol = encap_proto;

	set_ipv4_csum((void *)&h_outer.ip);

	/* store new outer network header */
	if (bpf_skb_store_bytes(skb, ETH_HLEN, &h_outer, olen,
				BPF_F_INVALIDATE_HASH) < 0)
		return TC_ACT_SHOT;

	/* if changing outer proto type, update eth->h_proto */
	if (encap_proto == IPPROTO_IPV6) {
		struct ethhdr eth;

		if (bpf_skb_load_bytes(skb, 0, &eth, sizeof(eth)) < 0)
			return TC_ACT_SHOT;
		eth.h_proto = bpf_htons(ETH_P_IP);
		if (bpf_skb_store_bytes(skb, 0, &eth, sizeof(eth), 0) < 0)
			return TC_ACT_SHOT;
	}

	return TC_ACT_OK;
}

static __always_inline int encap_ipv4(struct __sk_buff *skb, __u8 encap_proto,
				      __u16 l2_proto)
{
	return __encap_ipv4(skb, encap_proto, l2_proto, 0);
}

static __always_inline int __encap_ipv6(struct __sk_buff *skb, __u8 encap_proto,
					__u16 l2_proto, __u16 ext_proto)
{
	__u16 udp_dst = UDP_PORT;
	struct ipv6hdr iph_inner;
	struct v6hdr h_outer;
	struct tcphdr tcph;
	int olen, l2_len;
	__u8 *l2_hdr = NULL;
	__u16 tot_len;
	__u64 flags;

	if (bpf_skb_load_bytes(skb, ETH_HLEN, &iph_inner,
			       sizeof(iph_inner)) < 0)
		return TC_ACT_OK;

	/* filter only packets we want */
	if (bpf_skb_load_bytes(skb, ETH_HLEN + sizeof(iph_inner),
			       &tcph, sizeof(tcph)) < 0)
		return TC_ACT_OK;

	if (tcph.dest != __bpf_constant_htons(cfg_port))
		return TC_ACT_OK;

	olen = sizeof(h_outer.ip);
	l2_len = 0;

	flags = BPF_F_ADJ_ROOM_FIXED_GSO | BPF_F_ADJ_ROOM_ENCAP_L3_IPV6;

	switch (l2_proto) {
	case ETH_P_MPLS_UC:
		l2_len = sizeof(mpls_label);
		udp_dst = MPLS_OVER_UDP_PORT;
		break;
	case ETH_P_TEB:
		l2_len = ETH_HLEN;
		if (ext_proto & EXTPROTO_VXLAN) {
			udp_dst = VXLAN_UDP_PORT;
			l2_len += sizeof(struct vxlanhdr);
		} else
			udp_dst = ETH_OVER_UDP_PORT;
		break;
	}
	flags |= BPF_F_ADJ_ROOM_ENCAP_L2(l2_len);

	switch (encap_proto) {
	case IPPROTO_GRE:
		flags |= BPF_F_ADJ_ROOM_ENCAP_L4_GRE;
		olen += sizeof(h_outer.l4hdr.gre);
		h_outer.l4hdr.gre.protocol = bpf_htons(l2_proto);
		h_outer.l4hdr.gre.flags = 0;
		break;
	case IPPROTO_UDP:
		flags |= BPF_F_ADJ_ROOM_ENCAP_L4_UDP;
		olen += sizeof(h_outer.l4hdr.udp);
		h_outer.l4hdr.udp.source = __bpf_constant_htons(cfg_udp_src);
		h_outer.l4hdr.udp.dest = bpf_htons(udp_dst);
		tot_len = bpf_ntohs(iph_inner.payload_len) + sizeof(iph_inner) +
			  sizeof(h_outer.l4hdr.udp) + l2_len;
		h_outer.l4hdr.udp.check = 0;
		h_outer.l4hdr.udp.len = bpf_htons(tot_len);
		break;
	case IPPROTO_IPV6:
		break;
	default:
		return TC_ACT_OK;
	}

	/* add L2 encap (if specified) */
	l2_hdr = (__u8 *)&h_outer + olen;
	switch (l2_proto) {
	case ETH_P_MPLS_UC:
		*(__u32 *)l2_hdr = mpls_label;
		break;
	case ETH_P_TEB:
		flags |= BPF_F_ADJ_ROOM_ENCAP_L2_ETH;

		if (ext_proto & EXTPROTO_VXLAN) {
			struct vxlanhdr *vxlan_hdr = (struct vxlanhdr *)l2_hdr;

			vxlan_hdr->vx_flags = VXLAN_FLAGS;
			vxlan_hdr->vx_vni = bpf_htonl((VXLAN_VNI & VXLAN_VNI_MASK) << 8);

			l2_hdr += sizeof(struct vxlanhdr);
		}

		if (bpf_skb_load_bytes(skb, 0, l2_hdr, ETH_HLEN))
			return TC_ACT_SHOT;
		break;
	}
	olen += l2_len;

	/* add room between mac and network header */
	if (bpf_skb_adjust_room(skb, olen, BPF_ADJ_ROOM_MAC, flags))
		return TC_ACT_SHOT;

	/* prepare new outer network header */
	h_outer.ip = iph_inner;
	h_outer.ip.payload_len = bpf_htons(olen +
					   bpf_ntohs(h_outer.ip.payload_len));

	h_outer.ip.nexthdr = encap_proto;

	/* store new outer network header */
	if (bpf_skb_store_bytes(skb, ETH_HLEN, &h_outer, olen,
				BPF_F_INVALIDATE_HASH) < 0)
		return TC_ACT_SHOT;

	return TC_ACT_OK;
}

static __always_inline int encap_ipv6(struct __sk_buff *skb, __u8 encap_proto,
				      __u16 l2_proto)
{
	return __encap_ipv6(skb, encap_proto, l2_proto, 0);
}

SEC("encap_ipip_none")
int __encap_ipip_none(struct __sk_buff *skb)
{
	if (skb->protocol == __bpf_constant_htons(ETH_P_IP))
		return encap_ipv4(skb, IPPROTO_IPIP, ETH_P_IP);
	else
		return TC_ACT_OK;
}

SEC("encap_gre_none")
int __encap_gre_none(struct __sk_buff *skb)
{
	if (skb->protocol == __bpf_constant_htons(ETH_P_IP))
		return encap_ipv4(skb, IPPROTO_GRE, ETH_P_IP);
	else
		return TC_ACT_OK;
}

SEC("encap_gre_mpls")
int __encap_gre_mpls(struct __sk_buff *skb)
{
	if (skb->protocol == __bpf_constant_htons(ETH_P_IP))
		return encap_ipv4(skb, IPPROTO_GRE, ETH_P_MPLS_UC);
	else
		return TC_ACT_OK;
}

SEC("encap_gre_eth")
int __encap_gre_eth(struct __sk_buff *skb)
{
	if (skb->protocol == __bpf_constant_htons(ETH_P_IP))
		return encap_ipv4(skb, IPPROTO_GRE, ETH_P_TEB);
	else
		return TC_ACT_OK;
}

SEC("encap_udp_none")
int __encap_udp_none(struct __sk_buff *skb)
{
	if (skb->protocol == __bpf_constant_htons(ETH_P_IP))
		return encap_ipv4(skb, IPPROTO_UDP, ETH_P_IP);
	else
		return TC_ACT_OK;
}

SEC("encap_udp_mpls")
int __encap_udp_mpls(struct __sk_buff *skb)
{
	if (skb->protocol == __bpf_constant_htons(ETH_P_IP))
		return encap_ipv4(skb, IPPROTO_UDP, ETH_P_MPLS_UC);
	else
		return TC_ACT_OK;
}

SEC("encap_udp_eth")
int __encap_udp_eth(struct __sk_buff *skb)
{
	if (skb->protocol == __bpf_constant_htons(ETH_P_IP))
		return encap_ipv4(skb, IPPROTO_UDP, ETH_P_TEB);
	else
		return TC_ACT_OK;
}

SEC("encap_vxlan_eth")
int __encap_vxlan_eth(struct __sk_buff *skb)
{
	if (skb->protocol == __bpf_constant_htons(ETH_P_IP))
		return __encap_ipv4(skb, IPPROTO_UDP,
				    ETH_P_TEB,
				    EXTPROTO_VXLAN);
	else
		return TC_ACT_OK;
}

SEC("encap_sit_none")
int __encap_sit_none(struct __sk_buff *skb)
{
	if (skb->protocol == __bpf_constant_htons(ETH_P_IPV6))
		return encap_ipv4(skb, IPPROTO_IPV6, ETH_P_IP);
	else
		return TC_ACT_OK;
}

SEC("encap_ip6tnl_none")
int __encap_ip6tnl_none(struct __sk_buff *skb)
{
	if (skb->protocol == __bpf_constant_htons(ETH_P_IPV6))
		return encap_ipv6(skb, IPPROTO_IPV6, ETH_P_IPV6);
	else
		return TC_ACT_OK;
}

SEC("encap_ip6gre_none")
int __encap_ip6gre_none(struct __sk_buff *skb)
{
	if (skb->protocol == __bpf_constant_htons(ETH_P_IPV6))
		return encap_ipv6(skb, IPPROTO_GRE, ETH_P_IPV6);
	else
		return TC_ACT_OK;
}

SEC("encap_ip6gre_mpls")
int __encap_ip6gre_mpls(struct __sk_buff *skb)
{
	if (skb->protocol == __bpf_constant_htons(ETH_P_IPV6))
		return encap_ipv6(skb, IPPROTO_GRE, ETH_P_MPLS_UC);
	else
		return TC_ACT_OK;
}

SEC("encap_ip6gre_eth")
int __encap_ip6gre_eth(struct __sk_buff *skb)
{
	if (skb->protocol == __bpf_constant_htons(ETH_P_IPV6))
		return encap_ipv6(skb, IPPROTO_GRE, ETH_P_TEB);
	else
		return TC_ACT_OK;
}

SEC("encap_ip6udp_none")
int __encap_ip6udp_none(struct __sk_buff *skb)
{
	if (skb->protocol == __bpf_constant_htons(ETH_P_IPV6))
		return encap_ipv6(skb, IPPROTO_UDP, ETH_P_IPV6);
	else
		return TC_ACT_OK;
}

SEC("encap_ip6udp_mpls")
int __encap_ip6udp_mpls(struct __sk_buff *skb)
{
	if (skb->protocol == __bpf_constant_htons(ETH_P_IPV6))
		return encap_ipv6(skb, IPPROTO_UDP, ETH_P_MPLS_UC);
	else
		return TC_ACT_OK;
}

SEC("encap_ip6udp_eth")
int __encap_ip6udp_eth(struct __sk_buff *skb)
{
	if (skb->protocol == __bpf_constant_htons(ETH_P_IPV6))
		return encap_ipv6(skb, IPPROTO_UDP, ETH_P_TEB);
	else
		return TC_ACT_OK;
}

SEC("encap_ip6vxlan_eth")
int __encap_ip6vxlan_eth(struct __sk_buff *skb)
{
	if (skb->protocol == __bpf_constant_htons(ETH_P_IPV6))
		return __encap_ipv6(skb, IPPROTO_UDP,
				    ETH_P_TEB,
				    EXTPROTO_VXLAN);
	else
		return TC_ACT_OK;
}

static int decap_internal(struct __sk_buff *skb, int off, int len, char proto)
{
	char buf[sizeof(struct v6hdr)];
	struct gre_hdr greh;
	struct udphdr udph;
	int olen = len;

	switch (proto) {
	case IPPROTO_IPIP:
	case IPPROTO_IPV6:
		break;
	case IPPROTO_GRE:
		olen += sizeof(struct gre_hdr);
		if (bpf_skb_load_bytes(skb, off + len, &greh, sizeof(greh)) < 0)
			return TC_ACT_OK;
		switch (bpf_ntohs(greh.protocol)) {
		case ETH_P_MPLS_UC:
			olen += sizeof(mpls_label);
			break;
		case ETH_P_TEB:
			olen += ETH_HLEN;
			break;
		}
		break;
	case IPPROTO_UDP:
		olen += sizeof(struct udphdr);
		if (bpf_skb_load_bytes(skb, off + len, &udph, sizeof(udph)) < 0)
			return TC_ACT_OK;
		switch (bpf_ntohs(udph.dest)) {
		case MPLS_OVER_UDP_PORT:
			olen += sizeof(mpls_label);
			break;
		case ETH_OVER_UDP_PORT:
			olen += ETH_HLEN;
			break;
		case VXLAN_UDP_PORT:
			olen += ETH_HLEN + sizeof(struct vxlanhdr);
			break;
		}
		break;
	default:
		return TC_ACT_OK;
	}

	if (bpf_skb_adjust_room(skb, -olen, BPF_ADJ_ROOM_MAC,
				BPF_F_ADJ_ROOM_FIXED_GSO))
		return TC_ACT_SHOT;

	return TC_ACT_OK;
}

static int decap_ipv4(struct __sk_buff *skb)
{
	struct iphdr iph_outer;

	if (bpf_skb_load_bytes(skb, ETH_HLEN, &iph_outer,
			       sizeof(iph_outer)) < 0)
		return TC_ACT_OK;

	if (iph_outer.ihl != 5)
		return TC_ACT_OK;

	return decap_internal(skb, ETH_HLEN, sizeof(iph_outer),
			      iph_outer.protocol);
}

static int decap_ipv6(struct __sk_buff *skb)
{
	struct ipv6hdr iph_outer;

	if (bpf_skb_load_bytes(skb, ETH_HLEN, &iph_outer,
			       sizeof(iph_outer)) < 0)
		return TC_ACT_OK;

	return decap_internal(skb, ETH_HLEN, sizeof(iph_outer),
			      iph_outer.nexthdr);
}

SEC("decap")
int decap_f(struct __sk_buff *skb)
{
	switch (skb->protocol) {
	case __bpf_constant_htons(ETH_P_IP):
		return decap_ipv4(skb);
	case __bpf_constant_htons(ETH_P_IPV6):
		return decap_ipv6(skb);
	default:
		/* does not match, ignore */
		return TC_ACT_OK;
	}
}

char __license[] SEC("license") = "GPL";
