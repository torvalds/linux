// SPDX-License-Identifier: GPL-2.0-only
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/netfilter.h>
#include <linux/rhashtable.h>
#include <linux/ip.h>
#include <linux/ipv6.h>
#include <linux/netdevice.h>
#include <net/ip.h>
#include <net/ipv6.h>
#include <net/ip6_route.h>
#include <net/neighbour.h>
#include <net/netfilter/nf_flow_table.h>
#include <net/netfilter/nf_conntrack_acct.h>
/* For layer 4 checksum field offset. */
#include <linux/tcp.h>
#include <linux/udp.h>

static int nf_flow_state_check(struct flow_offload *flow, int proto,
			       struct sk_buff *skb, unsigned int thoff)
{
	struct tcphdr *tcph;

	if (proto != IPPROTO_TCP)
		return 0;

	if (!pskb_may_pull(skb, thoff + sizeof(*tcph)))
		return -1;

	tcph = (void *)(skb_network_header(skb) + thoff);
	if (unlikely(tcph->fin || tcph->rst)) {
		flow_offload_teardown(flow);
		return -1;
	}

	return 0;
}

static int nf_flow_nat_ip_tcp(struct sk_buff *skb, unsigned int thoff,
			      __be32 addr, __be32 new_addr)
{
	struct tcphdr *tcph;

	if (!pskb_may_pull(skb, thoff + sizeof(*tcph)) ||
	    skb_try_make_writable(skb, thoff + sizeof(*tcph)))
		return -1;

	tcph = (void *)(skb_network_header(skb) + thoff);
	inet_proto_csum_replace4(&tcph->check, skb, addr, new_addr, true);

	return 0;
}

static int nf_flow_nat_ip_udp(struct sk_buff *skb, unsigned int thoff,
			      __be32 addr, __be32 new_addr)
{
	struct udphdr *udph;

	if (!pskb_may_pull(skb, thoff + sizeof(*udph)) ||
	    skb_try_make_writable(skb, thoff + sizeof(*udph)))
		return -1;

	udph = (void *)(skb_network_header(skb) + thoff);
	if (udph->check || skb->ip_summed == CHECKSUM_PARTIAL) {
		inet_proto_csum_replace4(&udph->check, skb, addr,
					 new_addr, true);
		if (!udph->check)
			udph->check = CSUM_MANGLED_0;
	}

	return 0;
}

static int nf_flow_nat_ip_l4proto(struct sk_buff *skb, struct iphdr *iph,
				  unsigned int thoff, __be32 addr,
				  __be32 new_addr)
{
	switch (iph->protocol) {
	case IPPROTO_TCP:
		if (nf_flow_nat_ip_tcp(skb, thoff, addr, new_addr) < 0)
			return NF_DROP;
		break;
	case IPPROTO_UDP:
		if (nf_flow_nat_ip_udp(skb, thoff, addr, new_addr) < 0)
			return NF_DROP;
		break;
	}

	return 0;
}

static int nf_flow_snat_ip(const struct flow_offload *flow, struct sk_buff *skb,
			   struct iphdr *iph, unsigned int thoff,
			   enum flow_offload_tuple_dir dir)
{
	__be32 addr, new_addr;

	switch (dir) {
	case FLOW_OFFLOAD_DIR_ORIGINAL:
		addr = iph->saddr;
		new_addr = flow->tuplehash[FLOW_OFFLOAD_DIR_REPLY].tuple.dst_v4.s_addr;
		iph->saddr = new_addr;
		break;
	case FLOW_OFFLOAD_DIR_REPLY:
		addr = iph->daddr;
		new_addr = flow->tuplehash[FLOW_OFFLOAD_DIR_ORIGINAL].tuple.src_v4.s_addr;
		iph->daddr = new_addr;
		break;
	default:
		return -1;
	}
	csum_replace4(&iph->check, addr, new_addr);

	return nf_flow_nat_ip_l4proto(skb, iph, thoff, addr, new_addr);
}

static int nf_flow_dnat_ip(const struct flow_offload *flow, struct sk_buff *skb,
			   struct iphdr *iph, unsigned int thoff,
			   enum flow_offload_tuple_dir dir)
{
	__be32 addr, new_addr;

	switch (dir) {
	case FLOW_OFFLOAD_DIR_ORIGINAL:
		addr = iph->daddr;
		new_addr = flow->tuplehash[FLOW_OFFLOAD_DIR_REPLY].tuple.src_v4.s_addr;
		iph->daddr = new_addr;
		break;
	case FLOW_OFFLOAD_DIR_REPLY:
		addr = iph->saddr;
		new_addr = flow->tuplehash[FLOW_OFFLOAD_DIR_ORIGINAL].tuple.dst_v4.s_addr;
		iph->saddr = new_addr;
		break;
	default:
		return -1;
	}
	csum_replace4(&iph->check, addr, new_addr);

	return nf_flow_nat_ip_l4proto(skb, iph, thoff, addr, new_addr);
}

static int nf_flow_nat_ip(const struct flow_offload *flow, struct sk_buff *skb,
			  unsigned int thoff, enum flow_offload_tuple_dir dir)
{
	struct iphdr *iph = ip_hdr(skb);

	if (test_bit(NF_FLOW_SNAT, &flow->flags) &&
	    (nf_flow_snat_port(flow, skb, thoff, iph->protocol, dir) < 0 ||
	     nf_flow_snat_ip(flow, skb, ip_hdr(skb), thoff, dir) < 0))
		return -1;

	iph = ip_hdr(skb);
	if (test_bit(NF_FLOW_DNAT, &flow->flags) &&
	    (nf_flow_dnat_port(flow, skb, thoff, iph->protocol, dir) < 0 ||
	     nf_flow_dnat_ip(flow, skb, ip_hdr(skb), thoff, dir) < 0))
		return -1;

	return 0;
}

static bool ip_has_options(unsigned int thoff)
{
	return thoff != sizeof(struct iphdr);
}

static int nf_flow_tuple_ip(struct sk_buff *skb, const struct net_device *dev,
			    struct flow_offload_tuple *tuple)
{
	struct flow_ports *ports;
	unsigned int thoff;
	struct iphdr *iph;

	if (!pskb_may_pull(skb, sizeof(*iph)))
		return -1;

	iph = ip_hdr(skb);
	thoff = iph->ihl * 4;

	if (ip_is_fragment(iph) ||
	    unlikely(ip_has_options(thoff)))
		return -1;

	if (iph->protocol != IPPROTO_TCP &&
	    iph->protocol != IPPROTO_UDP)
		return -1;

	if (iph->ttl <= 1)
		return -1;

	thoff = iph->ihl * 4;
	if (!pskb_may_pull(skb, thoff + sizeof(*ports)))
		return -1;

	iph = ip_hdr(skb);
	ports = (struct flow_ports *)(skb_network_header(skb) + thoff);

	tuple->src_v4.s_addr	= iph->saddr;
	tuple->dst_v4.s_addr	= iph->daddr;
	tuple->src_port		= ports->source;
	tuple->dst_port		= ports->dest;
	tuple->l3proto		= AF_INET;
	tuple->l4proto		= iph->protocol;
	tuple->iifidx		= dev->ifindex;

	return 0;
}

/* Based on ip_exceeds_mtu(). */
static bool nf_flow_exceeds_mtu(const struct sk_buff *skb, unsigned int mtu)
{
	if (skb->len <= mtu)
		return false;

	if (skb_is_gso(skb) && skb_gso_validate_network_len(skb, mtu))
		return false;

	return true;
}

static int nf_flow_offload_dst_check(struct dst_entry *dst)
{
	if (unlikely(dst_xfrm(dst)))
		return dst_check(dst, 0) ? 0 : -1;

	return 0;
}

static unsigned int nf_flow_xmit_xfrm(struct sk_buff *skb,
				      const struct nf_hook_state *state,
				      struct dst_entry *dst)
{
	skb_orphan(skb);
	skb_dst_set_noref(skb, dst);
	dst_output(state->net, state->sk, skb);
	return NF_STOLEN;
}

unsigned int
nf_flow_offload_ip_hook(void *priv, struct sk_buff *skb,
			const struct nf_hook_state *state)
{
	struct flow_offload_tuple_rhash *tuplehash;
	struct nf_flowtable *flow_table = priv;
	struct flow_offload_tuple tuple = {};
	enum flow_offload_tuple_dir dir;
	struct flow_offload *flow;
	struct net_device *outdev;
	struct rtable *rt;
	unsigned int thoff;
	struct iphdr *iph;
	__be32 nexthop;

	if (skb->protocol != htons(ETH_P_IP))
		return NF_ACCEPT;

	if (nf_flow_tuple_ip(skb, state->in, &tuple) < 0)
		return NF_ACCEPT;

	tuplehash = flow_offload_lookup(flow_table, &tuple);
	if (tuplehash == NULL)
		return NF_ACCEPT;

	dir = tuplehash->tuple.dir;
	flow = container_of(tuplehash, struct flow_offload, tuplehash[dir]);
	rt = (struct rtable *)flow->tuplehash[dir].tuple.dst_cache;
	outdev = rt->dst.dev;

	if (unlikely(nf_flow_exceeds_mtu(skb, flow->tuplehash[dir].tuple.mtu)))
		return NF_ACCEPT;

	if (skb_try_make_writable(skb, sizeof(*iph)))
		return NF_DROP;

	thoff = ip_hdr(skb)->ihl * 4;
	if (nf_flow_state_check(flow, ip_hdr(skb)->protocol, skb, thoff))
		return NF_ACCEPT;

	flow_offload_refresh(flow_table, flow);

	if (nf_flow_offload_dst_check(&rt->dst)) {
		flow_offload_teardown(flow);
		return NF_ACCEPT;
	}

	if (nf_flow_nat_ip(flow, skb, thoff, dir) < 0)
		return NF_DROP;

	iph = ip_hdr(skb);
	ip_decrease_ttl(iph);
	skb->tstamp = 0;

	if (flow_table->flags & NF_FLOWTABLE_COUNTER)
		nf_ct_acct_update(flow->ct, tuplehash->tuple.dir, skb->len);

	if (unlikely(dst_xfrm(&rt->dst))) {
		memset(skb->cb, 0, sizeof(struct inet_skb_parm));
		IPCB(skb)->iif = skb->dev->ifindex;
		IPCB(skb)->flags = IPSKB_FORWARDED;
		return nf_flow_xmit_xfrm(skb, state, &rt->dst);
	}

	skb->dev = outdev;
	nexthop = rt_nexthop(rt, flow->tuplehash[!dir].tuple.src_v4.s_addr);
	skb_dst_set_noref(skb, &rt->dst);
	neigh_xmit(NEIGH_ARP_TABLE, outdev, &nexthop, skb);

	return NF_STOLEN;
}
EXPORT_SYMBOL_GPL(nf_flow_offload_ip_hook);

static int nf_flow_nat_ipv6_tcp(struct sk_buff *skb, unsigned int thoff,
				struct in6_addr *addr,
				struct in6_addr *new_addr)
{
	struct tcphdr *tcph;

	if (!pskb_may_pull(skb, thoff + sizeof(*tcph)) ||
	    skb_try_make_writable(skb, thoff + sizeof(*tcph)))
		return -1;

	tcph = (void *)(skb_network_header(skb) + thoff);
	inet_proto_csum_replace16(&tcph->check, skb, addr->s6_addr32,
				  new_addr->s6_addr32, true);

	return 0;
}

static int nf_flow_nat_ipv6_udp(struct sk_buff *skb, unsigned int thoff,
				struct in6_addr *addr,
				struct in6_addr *new_addr)
{
	struct udphdr *udph;

	if (!pskb_may_pull(skb, thoff + sizeof(*udph)) ||
	    skb_try_make_writable(skb, thoff + sizeof(*udph)))
		return -1;

	udph = (void *)(skb_network_header(skb) + thoff);
	if (udph->check || skb->ip_summed == CHECKSUM_PARTIAL) {
		inet_proto_csum_replace16(&udph->check, skb, addr->s6_addr32,
					  new_addr->s6_addr32, true);
		if (!udph->check)
			udph->check = CSUM_MANGLED_0;
	}

	return 0;
}

static int nf_flow_nat_ipv6_l4proto(struct sk_buff *skb, struct ipv6hdr *ip6h,
				    unsigned int thoff, struct in6_addr *addr,
				    struct in6_addr *new_addr)
{
	switch (ip6h->nexthdr) {
	case IPPROTO_TCP:
		if (nf_flow_nat_ipv6_tcp(skb, thoff, addr, new_addr) < 0)
			return NF_DROP;
		break;
	case IPPROTO_UDP:
		if (nf_flow_nat_ipv6_udp(skb, thoff, addr, new_addr) < 0)
			return NF_DROP;
		break;
	}

	return 0;
}

static int nf_flow_snat_ipv6(const struct flow_offload *flow,
			     struct sk_buff *skb, struct ipv6hdr *ip6h,
			     unsigned int thoff,
			     enum flow_offload_tuple_dir dir)
{
	struct in6_addr addr, new_addr;

	switch (dir) {
	case FLOW_OFFLOAD_DIR_ORIGINAL:
		addr = ip6h->saddr;
		new_addr = flow->tuplehash[FLOW_OFFLOAD_DIR_REPLY].tuple.dst_v6;
		ip6h->saddr = new_addr;
		break;
	case FLOW_OFFLOAD_DIR_REPLY:
		addr = ip6h->daddr;
		new_addr = flow->tuplehash[FLOW_OFFLOAD_DIR_ORIGINAL].tuple.src_v6;
		ip6h->daddr = new_addr;
		break;
	default:
		return -1;
	}

	return nf_flow_nat_ipv6_l4proto(skb, ip6h, thoff, &addr, &new_addr);
}

static int nf_flow_dnat_ipv6(const struct flow_offload *flow,
			     struct sk_buff *skb, struct ipv6hdr *ip6h,
			     unsigned int thoff,
			     enum flow_offload_tuple_dir dir)
{
	struct in6_addr addr, new_addr;

	switch (dir) {
	case FLOW_OFFLOAD_DIR_ORIGINAL:
		addr = ip6h->daddr;
		new_addr = flow->tuplehash[FLOW_OFFLOAD_DIR_REPLY].tuple.src_v6;
		ip6h->daddr = new_addr;
		break;
	case FLOW_OFFLOAD_DIR_REPLY:
		addr = ip6h->saddr;
		new_addr = flow->tuplehash[FLOW_OFFLOAD_DIR_ORIGINAL].tuple.dst_v6;
		ip6h->saddr = new_addr;
		break;
	default:
		return -1;
	}

	return nf_flow_nat_ipv6_l4proto(skb, ip6h, thoff, &addr, &new_addr);
}

static int nf_flow_nat_ipv6(const struct flow_offload *flow,
			    struct sk_buff *skb,
			    enum flow_offload_tuple_dir dir)
{
	struct ipv6hdr *ip6h = ipv6_hdr(skb);
	unsigned int thoff = sizeof(*ip6h);

	if (test_bit(NF_FLOW_SNAT, &flow->flags) &&
	    (nf_flow_snat_port(flow, skb, thoff, ip6h->nexthdr, dir) < 0 ||
	     nf_flow_snat_ipv6(flow, skb, ipv6_hdr(skb), thoff, dir) < 0))
		return -1;

	ip6h = ipv6_hdr(skb);
	if (test_bit(NF_FLOW_DNAT, &flow->flags) &&
	    (nf_flow_dnat_port(flow, skb, thoff, ip6h->nexthdr, dir) < 0 ||
	     nf_flow_dnat_ipv6(flow, skb, ipv6_hdr(skb), thoff, dir) < 0))
		return -1;

	return 0;
}

static int nf_flow_tuple_ipv6(struct sk_buff *skb, const struct net_device *dev,
			      struct flow_offload_tuple *tuple)
{
	struct flow_ports *ports;
	struct ipv6hdr *ip6h;
	unsigned int thoff;

	if (!pskb_may_pull(skb, sizeof(*ip6h)))
		return -1;

	ip6h = ipv6_hdr(skb);

	if (ip6h->nexthdr != IPPROTO_TCP &&
	    ip6h->nexthdr != IPPROTO_UDP)
		return -1;

	if (ip6h->hop_limit <= 1)
		return -1;

	thoff = sizeof(*ip6h);
	if (!pskb_may_pull(skb, thoff + sizeof(*ports)))
		return -1;

	ip6h = ipv6_hdr(skb);
	ports = (struct flow_ports *)(skb_network_header(skb) + thoff);

	tuple->src_v6		= ip6h->saddr;
	tuple->dst_v6		= ip6h->daddr;
	tuple->src_port		= ports->source;
	tuple->dst_port		= ports->dest;
	tuple->l3proto		= AF_INET6;
	tuple->l4proto		= ip6h->nexthdr;
	tuple->iifidx		= dev->ifindex;

	return 0;
}

unsigned int
nf_flow_offload_ipv6_hook(void *priv, struct sk_buff *skb,
			  const struct nf_hook_state *state)
{
	struct flow_offload_tuple_rhash *tuplehash;
	struct nf_flowtable *flow_table = priv;
	struct flow_offload_tuple tuple = {};
	enum flow_offload_tuple_dir dir;
	const struct in6_addr *nexthop;
	struct flow_offload *flow;
	struct net_device *outdev;
	struct ipv6hdr *ip6h;
	struct rt6_info *rt;

	if (skb->protocol != htons(ETH_P_IPV6))
		return NF_ACCEPT;

	if (nf_flow_tuple_ipv6(skb, state->in, &tuple) < 0)
		return NF_ACCEPT;

	tuplehash = flow_offload_lookup(flow_table, &tuple);
	if (tuplehash == NULL)
		return NF_ACCEPT;

	dir = tuplehash->tuple.dir;
	flow = container_of(tuplehash, struct flow_offload, tuplehash[dir]);
	rt = (struct rt6_info *)flow->tuplehash[dir].tuple.dst_cache;
	outdev = rt->dst.dev;

	if (unlikely(nf_flow_exceeds_mtu(skb, flow->tuplehash[dir].tuple.mtu)))
		return NF_ACCEPT;

	if (nf_flow_state_check(flow, ipv6_hdr(skb)->nexthdr, skb,
				sizeof(*ip6h)))
		return NF_ACCEPT;

	flow_offload_refresh(flow_table, flow);

	if (nf_flow_offload_dst_check(&rt->dst)) {
		flow_offload_teardown(flow);
		return NF_ACCEPT;
	}

	if (skb_try_make_writable(skb, sizeof(*ip6h)))
		return NF_DROP;

	if (nf_flow_nat_ipv6(flow, skb, dir) < 0)
		return NF_DROP;

	ip6h = ipv6_hdr(skb);
	ip6h->hop_limit--;
	skb->tstamp = 0;

	if (flow_table->flags & NF_FLOWTABLE_COUNTER)
		nf_ct_acct_update(flow->ct, tuplehash->tuple.dir, skb->len);

	if (unlikely(dst_xfrm(&rt->dst))) {
		memset(skb->cb, 0, sizeof(struct inet6_skb_parm));
		IP6CB(skb)->iif = skb->dev->ifindex;
		IP6CB(skb)->flags = IP6SKB_FORWARDED;
		return nf_flow_xmit_xfrm(skb, state, &rt->dst);
	}

	skb->dev = outdev;
	nexthop = rt6_nexthop(rt, &flow->tuplehash[!dir].tuple.src_v6);
	skb_dst_set_noref(skb, &rt->dst);
	neigh_xmit(NEIGH_ND_TABLE, outdev, nexthop, skb);

	return NF_STOLEN;
}
EXPORT_SYMBOL_GPL(nf_flow_offload_ipv6_hook);
