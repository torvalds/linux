// SPDX-License-Identifier: GPL-2.0
#include "vmlinux.h"
#include <string.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_endian.h>
#include <bpf/bpf_tracing.h>

struct grehdr {
	__be16 flags;
	__be16 protocol;
};

SEC("encap_gre")
int bpf_lwt_encap_gre(struct __sk_buff *skb)
{
	struct encap_hdr {
		struct iphdr iph;
		struct grehdr greh;
	} hdr;
	int err;

	memset(&hdr, 0, sizeof(struct encap_hdr));

	hdr.iph.ihl = 5;
	hdr.iph.version = 4;
	hdr.iph.ttl = 0x40;
	hdr.iph.protocol = 47;  /* IPPROTO_GRE */
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
	hdr.iph.saddr = 0x640110ac;  /* 172.16.1.100 */
	hdr.iph.daddr = 0x641010ac;  /* 172.16.16.100 */
#elif __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
	hdr.iph.saddr = 0xac100164;  /* 172.16.1.100 */
	hdr.iph.daddr = 0xac101064;  /* 172.16.16.100 */
#else
#error "Fix your compiler's __BYTE_ORDER__?!"
#endif
	hdr.iph.tot_len = bpf_htons(skb->len + sizeof(struct encap_hdr));

	hdr.greh.protocol = skb->protocol;

	err = bpf_lwt_push_encap(skb, BPF_LWT_ENCAP_IP, &hdr,
				 sizeof(struct encap_hdr));
	if (err)
		return BPF_DROP;

	return BPF_LWT_REROUTE;
}

SEC("encap_gre6")
int bpf_lwt_encap_gre6(struct __sk_buff *skb)
{
	struct encap_hdr {
		struct ipv6hdr ip6hdr;
		struct grehdr greh;
	} hdr;
	int err;

	memset(&hdr, 0, sizeof(struct encap_hdr));

	hdr.ip6hdr.version = 6;
	hdr.ip6hdr.payload_len = bpf_htons(skb->len + sizeof(struct grehdr));
	hdr.ip6hdr.nexthdr = 47;  /* IPPROTO_GRE */
	hdr.ip6hdr.hop_limit = 0x40;
	/* fb01::1 */
	hdr.ip6hdr.saddr.in6_u.u6_addr8[0] = 0xfb;
	hdr.ip6hdr.saddr.in6_u.u6_addr8[1] = 1;
	hdr.ip6hdr.saddr.in6_u.u6_addr8[15] = 1;
	/* fb10::1 */
	hdr.ip6hdr.daddr.in6_u.u6_addr8[0] = 0xfb;
	hdr.ip6hdr.daddr.in6_u.u6_addr8[1] = 0x10;
	hdr.ip6hdr.daddr.in6_u.u6_addr8[15] = 1;

	hdr.greh.protocol = skb->protocol;

	err = bpf_lwt_push_encap(skb, BPF_LWT_ENCAP_IP, &hdr,
				 sizeof(struct encap_hdr));
	if (err)
		return BPF_DROP;

	return BPF_LWT_REROUTE;
}

#define VXLAN_PORT  4789
#define VXLAN_FLAGS 0x08000000
#define VXLAN_VNI   1

#define ETH_ALEN	6		/* Octets in one ethernet addr	 */
#define ETH_P_IP	0x0800		/* Internet Protocol packet	*/
#define ETH_P_IPV6	0x86DD		/* IPv6 over bluebook		*/

static const __u8 bcast[ETH_ALEN] = {
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
};

static const __u8 srcmac[ETH_ALEN] = {
	0x02, 0x00, 0x00, 0x00, 0x00, 0x01,
};

SEC("encap_vxlan")
int bpf_lwt_encap_vxlan(struct __sk_buff *skb)
{
	struct encap_hdr {
		struct iphdr    iph;
		struct udphdr   udph;
		struct vxlanhdr vxh;
		struct ethhdr   eth;
	} __attribute__((__packed__)) hdr;
	int err;

	memset(&hdr, 0, sizeof(hdr));

	hdr.iph.ihl      = 5;
	hdr.iph.version  = 4;
	hdr.iph.ttl      = 0x40;
	hdr.iph.protocol = 17; /* IPPROTO_UDP */
	hdr.iph.tot_len  = bpf_htons(skb->len + sizeof(hdr));
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
	hdr.iph.saddr = 0x640510ac;  /* 172.16.5.100  */
	hdr.iph.daddr = 0x641110ac;  /* 172.16.17.100 */
#elif __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
	hdr.iph.saddr = 0xac100564;  /* 172.16.5.100 */
	hdr.iph.daddr = 0xac101164;  /* 172.16.17.100 */
#else
#error "Fix your compiler's __BYTE_ORDER__?!"
#endif

	hdr.udph.source = bpf_htons(VXLAN_PORT);
	hdr.udph.dest   = bpf_htons(VXLAN_PORT);
	hdr.udph.len    = bpf_htons(skb->len + sizeof(hdr.udph) + sizeof(hdr.vxh) +
				    sizeof(hdr.eth));

	hdr.vxh.vx_flags = bpf_htonl(VXLAN_FLAGS);
	hdr.vxh.vx_vni   = bpf_htonl(VXLAN_VNI << 8);

	__builtin_memcpy(hdr.eth.h_dest, bcast, ETH_ALEN);
	__builtin_memcpy(hdr.eth.h_source, srcmac, ETH_ALEN);
	hdr.eth.h_proto = bpf_htons(ETH_P_IP);

	err = bpf_lwt_push_encap(skb, BPF_LWT_ENCAP_IP, &hdr, sizeof(hdr));
	if (err)
		return BPF_DROP;

	return BPF_LWT_REROUTE;
}

SEC("encap_vxlan6")
int bpf_lwt_encap_vxlan6(struct __sk_buff *skb)
{
	struct encap_hdr {
		struct ipv6hdr  ip6hdr;
		struct udphdr   udph;
		struct vxlanhdr vxh;
		struct ethhdr   eth;
	} __attribute__((__packed__)) hdr;
	int err;

	memset(&hdr, 0, sizeof(hdr));

	hdr.ip6hdr.version     = 6;
	hdr.ip6hdr.nexthdr     = 17; /* IPPROTO_UDP */
	hdr.ip6hdr.hop_limit   = 0x40;
	hdr.ip6hdr.payload_len = bpf_htons(skb->len + sizeof(hdr.udph) + sizeof(hdr.vxh) +
					   sizeof(hdr.eth));
	/* fb05::1 */
	hdr.ip6hdr.saddr.in6_u.u6_addr8[0]  = 0xfb;
	hdr.ip6hdr.saddr.in6_u.u6_addr8[1]  = 0x05;
	hdr.ip6hdr.saddr.in6_u.u6_addr8[15] = 1;
	/* fb11::1 */
	hdr.ip6hdr.daddr.in6_u.u6_addr8[0]  = 0xfb;
	hdr.ip6hdr.daddr.in6_u.u6_addr8[1]  = 0x11;
	hdr.ip6hdr.daddr.in6_u.u6_addr8[15] = 1;

	hdr.udph.source = bpf_htons(VXLAN_PORT);
	hdr.udph.dest   = bpf_htons(VXLAN_PORT);
	hdr.udph.len    = bpf_htons(skb->len + sizeof(hdr.udph) + sizeof(hdr.vxh) +
				    sizeof(hdr.eth));

	hdr.vxh.vx_flags = bpf_htonl(VXLAN_FLAGS);
	hdr.vxh.vx_vni   = bpf_htonl(VXLAN_VNI << 8);

	__builtin_memcpy(hdr.eth.h_dest, bcast, ETH_ALEN);
	__builtin_memcpy(hdr.eth.h_source, srcmac, ETH_ALEN);
	hdr.eth.h_proto = bpf_htons(ETH_P_IPV6);

	err = bpf_lwt_push_encap(skb, BPF_LWT_ENCAP_IP, &hdr, sizeof(hdr));
	if (err)
		return BPF_DROP;

	return BPF_LWT_REROUTE;
}

volatile const int tgt_ip_version;

__u16 transport_hdr = 0;
__u16 network_hdr = 0;
bool fexit_triggered = false;

SEC("?fexit/bpf_lwt_push_ip_encap")
int BPF_PROG(fexit_lwt_push_ip_encap, struct sk_buff *skb, void *hdr, u32 len, bool ingress,
	     int retval)
{
	struct iphdr *iph;

	if (retval || fexit_triggered)
		return 0;

	iph = (typeof(iph)) (skb->head + skb->network_header);
	if (iph->version != tgt_ip_version)
		return 0;

	if ((iph->version == 4 && iph->protocol == 17 /* IPPROTO_UDP */) ||
	    (iph->version == 6 && ((struct ipv6hdr *)iph)->nexthdr == 17 /* IPPROTO_UDP */)) {
		fexit_triggered = true;
		transport_hdr   = skb->transport_header;
		network_hdr     = skb->network_header;
	}
	return 0;
}

char _license[] SEC("license") = "GPL";
