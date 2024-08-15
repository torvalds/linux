// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2022 Meta

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <linux/bpf.h>
#include <linux/stddef.h>
#include <linux/pkt_cls.h>
#include <linux/if_ether.h>
#include <linux/in.h>
#include <linux/ip.h>
#include <linux/ipv6.h>
#include <linux/tcp.h>
#include <linux/udp.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_endian.h>

/* veth_src --- veth_src_fwd --- veth_det_fwd --- veth_dst
 *           |                                 |
 *  ns_src   |              ns_fwd             |   ns_dst
 *
 * ns_src and ns_dst: ENDHOST namespace
 *            ns_fwd: Fowarding namespace
 */

#define ctx_ptr(field)		(void *)(long)(field)

#define ip4_src			__bpf_htonl(0xac100164) /* 172.16.1.100 */
#define ip4_dst			__bpf_htonl(0xac100264) /* 172.16.2.100 */

#define ip6_src			{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, \
				  0x00, 0x01, 0xde, 0xad, 0xbe, 0xef, 0xca, 0xfe }
#define ip6_dst			{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, \
				  0x00, 0x02, 0xde, 0xad, 0xbe, 0xef, 0xca, 0xfe }

#define v6_equal(a, b)		(a.s6_addr32[0] == b.s6_addr32[0] && \
				 a.s6_addr32[1] == b.s6_addr32[1] && \
				 a.s6_addr32[2] == b.s6_addr32[2] && \
				 a.s6_addr32[3] == b.s6_addr32[3])

volatile const __u32 IFINDEX_SRC;
volatile const __u32 IFINDEX_DST;

#define EGRESS_ENDHOST_MAGIC	0x0b9fbeef
#define INGRESS_FWDNS_MAGIC	0x1b9fbeef
#define EGRESS_FWDNS_MAGIC	0x2b9fbeef

enum {
	INGRESS_FWDNS_P100,
	INGRESS_FWDNS_P101,
	EGRESS_FWDNS_P100,
	EGRESS_FWDNS_P101,
	INGRESS_ENDHOST,
	EGRESS_ENDHOST,
	SET_DTIME,
	__MAX_CNT,
};

enum {
	TCP_IP6_CLEAR_DTIME,
	TCP_IP4,
	TCP_IP6,
	UDP_IP4,
	UDP_IP6,
	TCP_IP4_RT_FWD,
	TCP_IP6_RT_FWD,
	UDP_IP4_RT_FWD,
	UDP_IP6_RT_FWD,
	UKN_TEST,
	__NR_TESTS,
};

enum {
	SRC_NS = 1,
	DST_NS,
};

__u32 dtimes[__NR_TESTS][__MAX_CNT] = {};
__u32 errs[__NR_TESTS][__MAX_CNT] = {};
__u32 test = 0;

static void inc_dtimes(__u32 idx)
{
	if (test < __NR_TESTS)
		dtimes[test][idx]++;
	else
		dtimes[UKN_TEST][idx]++;
}

static void inc_errs(__u32 idx)
{
	if (test < __NR_TESTS)
		errs[test][idx]++;
	else
		errs[UKN_TEST][idx]++;
}

static int skb_proto(int type)
{
	return type & 0xff;
}

static int skb_ns(int type)
{
	return (type >> 8) & 0xff;
}

static bool fwdns_clear_dtime(void)
{
	return test == TCP_IP6_CLEAR_DTIME;
}

static bool bpf_fwd(void)
{
	return test < TCP_IP4_RT_FWD;
}

static __u8 get_proto(void)
{
	switch (test) {
	case UDP_IP4:
	case UDP_IP6:
	case UDP_IP4_RT_FWD:
	case UDP_IP6_RT_FWD:
		return IPPROTO_UDP;
	default:
		return IPPROTO_TCP;
	}
}

/* -1: parse error: TC_ACT_SHOT
 *  0: not testing traffic: TC_ACT_OK
 * >0: first byte is the inet_proto, second byte has the netns
 *     of the sender
 */
static int skb_get_type(struct __sk_buff *skb)
{
	__u16 dst_ns_port = __bpf_htons(50000 + test);
	void *data_end = ctx_ptr(skb->data_end);
	void *data = ctx_ptr(skb->data);
	__u8 inet_proto = 0, ns = 0;
	struct ipv6hdr *ip6h;
	__u16 sport, dport;
	struct iphdr *iph;
	struct tcphdr *th;
	struct udphdr *uh;
	void *trans;

	switch (skb->protocol) {
	case __bpf_htons(ETH_P_IP):
		iph = data + sizeof(struct ethhdr);
		if (iph + 1 > data_end)
			return -1;
		if (iph->saddr == ip4_src)
			ns = SRC_NS;
		else if (iph->saddr == ip4_dst)
			ns = DST_NS;
		inet_proto = iph->protocol;
		trans = iph + 1;
		break;
	case __bpf_htons(ETH_P_IPV6):
		ip6h = data + sizeof(struct ethhdr);
		if (ip6h + 1 > data_end)
			return -1;
		if (v6_equal(ip6h->saddr, (struct in6_addr)ip6_src))
			ns = SRC_NS;
		else if (v6_equal(ip6h->saddr, (struct in6_addr)ip6_dst))
			ns = DST_NS;
		inet_proto = ip6h->nexthdr;
		trans = ip6h + 1;
		break;
	default:
		return 0;
	}

	/* skb is not from src_ns or dst_ns.
	 * skb is not the testing IPPROTO.
	 */
	if (!ns || inet_proto != get_proto())
		return 0;

	switch (inet_proto) {
	case IPPROTO_TCP:
		th = trans;
		if (th + 1 > data_end)
			return -1;
		sport = th->source;
		dport = th->dest;
		break;
	case IPPROTO_UDP:
		uh = trans;
		if (uh + 1 > data_end)
			return -1;
		sport = uh->source;
		dport = uh->dest;
		break;
	default:
		return 0;
	}

	/* The skb is the testing traffic */
	if ((ns == SRC_NS && dport == dst_ns_port) ||
	    (ns == DST_NS && sport == dst_ns_port))
		return (ns << 8 | inet_proto);

	return 0;
}

/* format: direction@iface@netns
 * egress@veth_(src|dst)@ns_(src|dst)
 */
SEC("tc")
int egress_host(struct __sk_buff *skb)
{
	int skb_type;

	skb_type = skb_get_type(skb);
	if (skb_type == -1)
		return TC_ACT_SHOT;
	if (!skb_type)
		return TC_ACT_OK;

	if (skb_proto(skb_type) == IPPROTO_TCP) {
		if (skb->tstamp_type == BPF_SKB_TSTAMP_DELIVERY_MONO &&
		    skb->tstamp)
			inc_dtimes(EGRESS_ENDHOST);
		else
			inc_errs(EGRESS_ENDHOST);
	} else {
		if (skb->tstamp_type == BPF_SKB_TSTAMP_UNSPEC &&
		    skb->tstamp)
			inc_dtimes(EGRESS_ENDHOST);
		else
			inc_errs(EGRESS_ENDHOST);
	}

	skb->tstamp = EGRESS_ENDHOST_MAGIC;

	return TC_ACT_OK;
}

/* ingress@veth_(src|dst)@ns_(src|dst) */
SEC("tc")
int ingress_host(struct __sk_buff *skb)
{
	int skb_type;

	skb_type = skb_get_type(skb);
	if (skb_type == -1)
		return TC_ACT_SHOT;
	if (!skb_type)
		return TC_ACT_OK;

	if (skb->tstamp_type == BPF_SKB_TSTAMP_DELIVERY_MONO &&
	    skb->tstamp == EGRESS_FWDNS_MAGIC)
		inc_dtimes(INGRESS_ENDHOST);
	else
		inc_errs(INGRESS_ENDHOST);

	return TC_ACT_OK;
}

/* ingress@veth_(src|dst)_fwd@ns_fwd priority 100 */
SEC("tc")
int ingress_fwdns_prio100(struct __sk_buff *skb)
{
	int skb_type;

	skb_type = skb_get_type(skb);
	if (skb_type == -1)
		return TC_ACT_SHOT;
	if (!skb_type)
		return TC_ACT_OK;

	/* delivery_time is only available to the ingress
	 * if the tc-bpf checks the skb->tstamp_type.
	 */
	if (skb->tstamp == EGRESS_ENDHOST_MAGIC)
		inc_errs(INGRESS_FWDNS_P100);

	if (fwdns_clear_dtime())
		skb->tstamp = 0;

	return TC_ACT_UNSPEC;
}

/* egress@veth_(src|dst)_fwd@ns_fwd priority 100 */
SEC("tc")
int egress_fwdns_prio100(struct __sk_buff *skb)
{
	int skb_type;

	skb_type = skb_get_type(skb);
	if (skb_type == -1)
		return TC_ACT_SHOT;
	if (!skb_type)
		return TC_ACT_OK;

	/* delivery_time is always available to egress even
	 * the tc-bpf did not use the tstamp_type.
	 */
	if (skb->tstamp == INGRESS_FWDNS_MAGIC)
		inc_dtimes(EGRESS_FWDNS_P100);
	else
		inc_errs(EGRESS_FWDNS_P100);

	if (fwdns_clear_dtime())
		skb->tstamp = 0;

	return TC_ACT_UNSPEC;
}

/* ingress@veth_(src|dst)_fwd@ns_fwd priority 101 */
SEC("tc")
int ingress_fwdns_prio101(struct __sk_buff *skb)
{
	__u64 expected_dtime = EGRESS_ENDHOST_MAGIC;
	int skb_type;

	skb_type = skb_get_type(skb);
	if (skb_type == -1 || !skb_type)
		/* Should have handled in prio100 */
		return TC_ACT_SHOT;

	if (skb_proto(skb_type) == IPPROTO_UDP)
		expected_dtime = 0;

	if (skb->tstamp_type) {
		if (fwdns_clear_dtime() ||
		    skb->tstamp_type != BPF_SKB_TSTAMP_DELIVERY_MONO ||
		    skb->tstamp != expected_dtime)
			inc_errs(INGRESS_FWDNS_P101);
		else
			inc_dtimes(INGRESS_FWDNS_P101);
	} else {
		if (!fwdns_clear_dtime() && expected_dtime)
			inc_errs(INGRESS_FWDNS_P101);
	}

	if (skb->tstamp_type == BPF_SKB_TSTAMP_DELIVERY_MONO) {
		skb->tstamp = INGRESS_FWDNS_MAGIC;
	} else {
		if (bpf_skb_set_tstamp(skb, INGRESS_FWDNS_MAGIC,
				       BPF_SKB_TSTAMP_DELIVERY_MONO))
			inc_errs(SET_DTIME);
		if (!bpf_skb_set_tstamp(skb, INGRESS_FWDNS_MAGIC,
					BPF_SKB_TSTAMP_UNSPEC))
			inc_errs(SET_DTIME);
	}

	if (skb_ns(skb_type) == SRC_NS)
		return bpf_fwd() ?
			bpf_redirect_neigh(IFINDEX_DST, NULL, 0, 0) : TC_ACT_OK;
	else
		return bpf_fwd() ?
			bpf_redirect_neigh(IFINDEX_SRC, NULL, 0, 0) : TC_ACT_OK;
}

/* egress@veth_(src|dst)_fwd@ns_fwd priority 101 */
SEC("tc")
int egress_fwdns_prio101(struct __sk_buff *skb)
{
	int skb_type;

	skb_type = skb_get_type(skb);
	if (skb_type == -1 || !skb_type)
		/* Should have handled in prio100 */
		return TC_ACT_SHOT;

	if (skb->tstamp_type) {
		if (fwdns_clear_dtime() ||
		    skb->tstamp_type != BPF_SKB_TSTAMP_DELIVERY_MONO ||
		    skb->tstamp != INGRESS_FWDNS_MAGIC)
			inc_errs(EGRESS_FWDNS_P101);
		else
			inc_dtimes(EGRESS_FWDNS_P101);
	} else {
		if (!fwdns_clear_dtime())
			inc_errs(EGRESS_FWDNS_P101);
	}

	if (skb->tstamp_type == BPF_SKB_TSTAMP_DELIVERY_MONO) {
		skb->tstamp = EGRESS_FWDNS_MAGIC;
	} else {
		if (bpf_skb_set_tstamp(skb, EGRESS_FWDNS_MAGIC,
				       BPF_SKB_TSTAMP_DELIVERY_MONO))
			inc_errs(SET_DTIME);
		if (!bpf_skb_set_tstamp(skb, INGRESS_FWDNS_MAGIC,
					BPF_SKB_TSTAMP_UNSPEC))
			inc_errs(SET_DTIME);
	}

	return TC_ACT_OK;
}

char __license[] SEC("license") = "GPL";
