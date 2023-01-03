// SPDX-License-Identifier: GPL-2.0-only
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/netfilter.h>
#include <linux/rhashtable.h>
#include <linux/ip.h>
#include <linux/ipv6.h>
#include <linux/netdevice.h>
#include <linux/if_ether.h>
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

	tcph = (void *)(skb_network_header(skb) + thoff);
	if (unlikely(tcph->fin || tcph->rst)) {
		flow_offload_teardown(flow);
		return -1;
	}

	return 0;
}

static void nf_flow_nat_ip_tcp(struct sk_buff *skb, unsigned int thoff,
			       __be32 addr, __be32 new_addr)
{
	struct tcphdr *tcph;

	tcph = (void *)(skb_network_header(skb) + thoff);
	inet_proto_csum_replace4(&tcph->check, skb, addr, new_addr, true);
}

static void nf_flow_nat_ip_udp(struct sk_buff *skb, unsigned int thoff,
			       __be32 addr, __be32 new_addr)
{
	struct udphdr *udph;

	udph = (void *)(skb_network_header(skb) + thoff);
	if (udph->check || skb->ip_summed == CHECKSUM_PARTIAL) {
		inet_proto_csum_replace4(&udph->check, skb, addr,
					 new_addr, true);
		if (!udph->check)
			udph->check = CSUM_MANGLED_0;
	}
}

static void nf_flow_nat_ip_l4proto(struct sk_buff *skb, struct iphdr *iph,
				   unsigned int thoff, __be32 addr,
				   __be32 new_addr)
{
	switch (iph->protocol) {
	case IPPROTO_TCP:
		nf_flow_nat_ip_tcp(skb, thoff, addr, new_addr);
		break;
	case IPPROTO_UDP:
		nf_flow_nat_ip_udp(skb, thoff, addr, new_addr);
		break;
	}
}

static void nf_flow_snat_ip(const struct flow_offload *flow,
			    struct sk_buff *skb, struct iphdr *iph,
			    unsigned int thoff, enum flow_offload_tuple_dir dir)
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
	}
	csum_replace4(&iph->check, addr, new_addr);

	nf_flow_nat_ip_l4proto(skb, iph, thoff, addr, new_addr);
}

static void nf_flow_dnat_ip(const struct flow_offload *flow,
			    struct sk_buff *skb, struct iphdr *iph,
			    unsigned int thoff, enum flow_offload_tuple_dir dir)
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
	}
	csum_replace4(&iph->check, addr, new_addr);

	nf_flow_nat_ip_l4proto(skb, iph, thoff, addr, new_addr);
}

static void nf_flow_nat_ip(const struct flow_offload *flow, struct sk_buff *skb,
			  unsigned int thoff, enum flow_offload_tuple_dir dir,
			  struct iphdr *iph)
{
	if (test_bit(NF_FLOW_SNAT, &flow->flags)) {
		nf_flow_snat_port(flow, skb, thoff, iph->protocol, dir);
		nf_flow_snat_ip(flow, skb, iph, thoff, dir);
	}
	if (test_bit(NF_FLOW_DNAT, &flow->flags)) {
		nf_flow_dnat_port(flow, skb, thoff, iph->protocol, dir);
		nf_flow_dnat_ip(flow, skb, iph, thoff, dir);
	}
}

static bool ip_has_options(unsigned int thoff)
{
	return thoff != sizeof(struct iphdr);
}

static void nf_flow_tuple_encap(struct sk_buff *skb,
				struct flow_offload_tuple *tuple)
{
	struct vlan_ethhdr *veth;
	struct pppoe_hdr *phdr;
	int i = 0;

	if (skb_vlan_tag_present(skb)) {
		tuple->encap[i].id = skb_vlan_tag_get(skb);
		tuple->encap[i].proto = skb->vlan_proto;
		i++;
	}
	switch (skb->protocol) {
	case htons(ETH_P_8021Q):
		veth = (struct vlan_ethhdr *)skb_mac_header(skb);
		tuple->encap[i].id = ntohs(veth->h_vlan_TCI);
		tuple->encap[i].proto = skb->protocol;
		break;
	case htons(ETH_P_PPP_SES):
		phdr = (struct pppoe_hdr *)skb_mac_header(skb);
		tuple->encap[i].id = ntohs(phdr->sid);
		tuple->encap[i].proto = skb->protocol;
		break;
	}
}

static int nf_flow_tuple_ip(struct sk_buff *skb, const struct net_device *dev,
			    struct flow_offload_tuple *tuple, u32 *hdrsize,
			    u32 offset)
{
	struct flow_ports *ports;
	unsigned int thoff;
	struct iphdr *iph;
	u8 ipproto;

	if (!pskb_may_pull(skb, sizeof(*iph) + offset))
		return -1;

	iph = (struct iphdr *)(skb_network_header(skb) + offset);
	thoff = (iph->ihl * 4);

	if (ip_is_fragment(iph) ||
	    unlikely(ip_has_options(thoff)))
		return -1;

	thoff += offset;

	ipproto = iph->protocol;
	switch (ipproto) {
	case IPPROTO_TCP:
		*hdrsize = sizeof(struct tcphdr);
		break;
	case IPPROTO_UDP:
		*hdrsize = sizeof(struct udphdr);
		break;
#ifdef CONFIG_NF_CT_PROTO_GRE
	case IPPROTO_GRE:
		*hdrsize = sizeof(struct gre_base_hdr);
		break;
#endif
	default:
		return -1;
	}

	if (iph->ttl <= 1)
		return -1;

	if (!pskb_may_pull(skb, thoff + *hdrsize))
		return -1;

	switch (ipproto) {
	case IPPROTO_TCP:
	case IPPROTO_UDP:
		ports = (struct flow_ports *)(skb_network_header(skb) + thoff);
		tuple->src_port		= ports->source;
		tuple->dst_port		= ports->dest;
		break;
	case IPPROTO_GRE: {
		struct gre_base_hdr *greh;

		greh = (struct gre_base_hdr *)(skb_network_header(skb) + thoff);
		if ((greh->flags & GRE_VERSION) != GRE_VERSION_0)
			return -1;
		break;
	}
	}

	iph = (struct iphdr *)(skb_network_header(skb) + offset);

	tuple->src_v4.s_addr	= iph->saddr;
	tuple->dst_v4.s_addr	= iph->daddr;
	tuple->l3proto		= AF_INET;
	tuple->l4proto		= ipproto;
	tuple->iifidx		= dev->ifindex;
	nf_flow_tuple_encap(skb, tuple);

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

static inline bool nf_flow_dst_check(struct flow_offload_tuple *tuple)
{
	if (tuple->xmit_type != FLOW_OFFLOAD_XMIT_NEIGH &&
	    tuple->xmit_type != FLOW_OFFLOAD_XMIT_XFRM)
		return true;

	return dst_check(tuple->dst_cache, tuple->dst_cookie);
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

static bool nf_flow_skb_encap_protocol(const struct sk_buff *skb, __be16 proto,
				       u32 *offset)
{
	struct vlan_ethhdr *veth;

	switch (skb->protocol) {
	case htons(ETH_P_8021Q):
		veth = (struct vlan_ethhdr *)skb_mac_header(skb);
		if (veth->h_vlan_encapsulated_proto == proto) {
			*offset += VLAN_HLEN;
			return true;
		}
		break;
	case htons(ETH_P_PPP_SES):
		if (nf_flow_pppoe_proto(skb) == proto) {
			*offset += PPPOE_SES_HLEN;
			return true;
		}
		break;
	}

	return false;
}

static void nf_flow_encap_pop(struct sk_buff *skb,
			      struct flow_offload_tuple_rhash *tuplehash)
{
	struct vlan_hdr *vlan_hdr;
	int i;

	for (i = 0; i < tuplehash->tuple.encap_num; i++) {
		if (skb_vlan_tag_present(skb)) {
			__vlan_hwaccel_clear_tag(skb);
			continue;
		}
		switch (skb->protocol) {
		case htons(ETH_P_8021Q):
			vlan_hdr = (struct vlan_hdr *)skb->data;
			__skb_pull(skb, VLAN_HLEN);
			vlan_set_encap_proto(skb, vlan_hdr);
			skb_reset_network_header(skb);
			break;
		case htons(ETH_P_PPP_SES):
			skb->protocol = nf_flow_pppoe_proto(skb);
			skb_pull(skb, PPPOE_SES_HLEN);
			skb_reset_network_header(skb);
			break;
		}
	}
}

static unsigned int nf_flow_queue_xmit(struct net *net, struct sk_buff *skb,
				       const struct flow_offload_tuple_rhash *tuplehash,
				       unsigned short type)
{
	struct net_device *outdev;

	outdev = dev_get_by_index_rcu(net, tuplehash->tuple.out.ifidx);
	if (!outdev)
		return NF_DROP;

	skb->dev = outdev;
	dev_hard_header(skb, skb->dev, type, tuplehash->tuple.out.h_dest,
			tuplehash->tuple.out.h_source, skb->len);
	dev_queue_xmit(skb);

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
	u32 hdrsize, offset = 0;
	unsigned int thoff, mtu;
	struct rtable *rt;
	struct iphdr *iph;
	__be32 nexthop;
	int ret;

	if (skb->protocol != htons(ETH_P_IP) &&
	    !nf_flow_skb_encap_protocol(skb, htons(ETH_P_IP), &offset))
		return NF_ACCEPT;

	if (nf_flow_tuple_ip(skb, state->in, &tuple, &hdrsize, offset) < 0)
		return NF_ACCEPT;

	tuplehash = flow_offload_lookup(flow_table, &tuple);
	if (tuplehash == NULL)
		return NF_ACCEPT;

	dir = tuplehash->tuple.dir;
	flow = container_of(tuplehash, struct flow_offload, tuplehash[dir]);

	mtu = flow->tuplehash[dir].tuple.mtu + offset;
	if (unlikely(nf_flow_exceeds_mtu(skb, mtu)))
		return NF_ACCEPT;

	iph = (struct iphdr *)(skb_network_header(skb) + offset);
	thoff = (iph->ihl * 4) + offset;
	if (nf_flow_state_check(flow, iph->protocol, skb, thoff))
		return NF_ACCEPT;

	if (!nf_flow_dst_check(&tuplehash->tuple)) {
		flow_offload_teardown(flow);
		return NF_ACCEPT;
	}

	if (skb_try_make_writable(skb, thoff + hdrsize))
		return NF_DROP;

	flow_offload_refresh(flow_table, flow);

	nf_flow_encap_pop(skb, tuplehash);
	thoff -= offset;

	iph = ip_hdr(skb);
	nf_flow_nat_ip(flow, skb, thoff, dir, iph);

	ip_decrease_ttl(iph);
	skb_clear_tstamp(skb);

	if (flow_table->flags & NF_FLOWTABLE_COUNTER)
		nf_ct_acct_update(flow->ct, tuplehash->tuple.dir, skb->len);

	if (unlikely(tuplehash->tuple.xmit_type == FLOW_OFFLOAD_XMIT_XFRM)) {
		rt = (struct rtable *)tuplehash->tuple.dst_cache;
		memset(skb->cb, 0, sizeof(struct inet_skb_parm));
		IPCB(skb)->iif = skb->dev->ifindex;
		IPCB(skb)->flags = IPSKB_FORWARDED;
		return nf_flow_xmit_xfrm(skb, state, &rt->dst);
	}

	switch (tuplehash->tuple.xmit_type) {
	case FLOW_OFFLOAD_XMIT_NEIGH:
		rt = (struct rtable *)tuplehash->tuple.dst_cache;
		outdev = rt->dst.dev;
		skb->dev = outdev;
		nexthop = rt_nexthop(rt, flow->tuplehash[!dir].tuple.src_v4.s_addr);
		skb_dst_set_noref(skb, &rt->dst);
		neigh_xmit(NEIGH_ARP_TABLE, outdev, &nexthop, skb);
		ret = NF_STOLEN;
		break;
	case FLOW_OFFLOAD_XMIT_DIRECT:
		ret = nf_flow_queue_xmit(state->net, skb, tuplehash, ETH_P_IP);
		if (ret == NF_DROP)
			flow_offload_teardown(flow);
		break;
	default:
		WARN_ON_ONCE(1);
		ret = NF_DROP;
		break;
	}

	return ret;
}
EXPORT_SYMBOL_GPL(nf_flow_offload_ip_hook);

static void nf_flow_nat_ipv6_tcp(struct sk_buff *skb, unsigned int thoff,
				 struct in6_addr *addr,
				 struct in6_addr *new_addr,
				 struct ipv6hdr *ip6h)
{
	struct tcphdr *tcph;

	tcph = (void *)(skb_network_header(skb) + thoff);
	inet_proto_csum_replace16(&tcph->check, skb, addr->s6_addr32,
				  new_addr->s6_addr32, true);
}

static void nf_flow_nat_ipv6_udp(struct sk_buff *skb, unsigned int thoff,
				 struct in6_addr *addr,
				 struct in6_addr *new_addr)
{
	struct udphdr *udph;

	udph = (void *)(skb_network_header(skb) + thoff);
	if (udph->check || skb->ip_summed == CHECKSUM_PARTIAL) {
		inet_proto_csum_replace16(&udph->check, skb, addr->s6_addr32,
					  new_addr->s6_addr32, true);
		if (!udph->check)
			udph->check = CSUM_MANGLED_0;
	}
}

static void nf_flow_nat_ipv6_l4proto(struct sk_buff *skb, struct ipv6hdr *ip6h,
				     unsigned int thoff, struct in6_addr *addr,
				     struct in6_addr *new_addr)
{
	switch (ip6h->nexthdr) {
	case IPPROTO_TCP:
		nf_flow_nat_ipv6_tcp(skb, thoff, addr, new_addr, ip6h);
		break;
	case IPPROTO_UDP:
		nf_flow_nat_ipv6_udp(skb, thoff, addr, new_addr);
		break;
	}
}

static void nf_flow_snat_ipv6(const struct flow_offload *flow,
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
	}

	nf_flow_nat_ipv6_l4proto(skb, ip6h, thoff, &addr, &new_addr);
}

static void nf_flow_dnat_ipv6(const struct flow_offload *flow,
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
	}

	nf_flow_nat_ipv6_l4proto(skb, ip6h, thoff, &addr, &new_addr);
}

static void nf_flow_nat_ipv6(const struct flow_offload *flow,
			     struct sk_buff *skb,
			     enum flow_offload_tuple_dir dir,
			     struct ipv6hdr *ip6h)
{
	unsigned int thoff = sizeof(*ip6h);

	if (test_bit(NF_FLOW_SNAT, &flow->flags)) {
		nf_flow_snat_port(flow, skb, thoff, ip6h->nexthdr, dir);
		nf_flow_snat_ipv6(flow, skb, ip6h, thoff, dir);
	}
	if (test_bit(NF_FLOW_DNAT, &flow->flags)) {
		nf_flow_dnat_port(flow, skb, thoff, ip6h->nexthdr, dir);
		nf_flow_dnat_ipv6(flow, skb, ip6h, thoff, dir);
	}
}

static int nf_flow_tuple_ipv6(struct sk_buff *skb, const struct net_device *dev,
			      struct flow_offload_tuple *tuple, u32 *hdrsize,
			      u32 offset)
{
	struct flow_ports *ports;
	struct ipv6hdr *ip6h;
	unsigned int thoff;
	u8 nexthdr;

	thoff = sizeof(*ip6h) + offset;
	if (!pskb_may_pull(skb, thoff))
		return -1;

	ip6h = (struct ipv6hdr *)(skb_network_header(skb) + offset);

	nexthdr = ip6h->nexthdr;
	switch (nexthdr) {
	case IPPROTO_TCP:
		*hdrsize = sizeof(struct tcphdr);
		break;
	case IPPROTO_UDP:
		*hdrsize = sizeof(struct udphdr);
		break;
#ifdef CONFIG_NF_CT_PROTO_GRE
	case IPPROTO_GRE:
		*hdrsize = sizeof(struct gre_base_hdr);
		break;
#endif
	default:
		return -1;
	}

	if (ip6h->hop_limit <= 1)
		return -1;

	if (!pskb_may_pull(skb, thoff + *hdrsize))
		return -1;

	switch (nexthdr) {
	case IPPROTO_TCP:
	case IPPROTO_UDP:
		ports = (struct flow_ports *)(skb_network_header(skb) + thoff);
		tuple->src_port		= ports->source;
		tuple->dst_port		= ports->dest;
		break;
	case IPPROTO_GRE: {
		struct gre_base_hdr *greh;

		greh = (struct gre_base_hdr *)(skb_network_header(skb) + thoff);
		if ((greh->flags & GRE_VERSION) != GRE_VERSION_0)
			return -1;
		break;
	}
	}

	ip6h = (struct ipv6hdr *)(skb_network_header(skb) + offset);

	tuple->src_v6		= ip6h->saddr;
	tuple->dst_v6		= ip6h->daddr;
	tuple->l3proto		= AF_INET6;
	tuple->l4proto		= nexthdr;
	tuple->iifidx		= dev->ifindex;
	nf_flow_tuple_encap(skb, tuple);

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
	unsigned int thoff, mtu;
	u32 hdrsize, offset = 0;
	struct ipv6hdr *ip6h;
	struct rt6_info *rt;
	int ret;

	if (skb->protocol != htons(ETH_P_IPV6) &&
	    !nf_flow_skb_encap_protocol(skb, htons(ETH_P_IPV6), &offset))
		return NF_ACCEPT;

	if (nf_flow_tuple_ipv6(skb, state->in, &tuple, &hdrsize, offset) < 0)
		return NF_ACCEPT;

	tuplehash = flow_offload_lookup(flow_table, &tuple);
	if (tuplehash == NULL)
		return NF_ACCEPT;

	dir = tuplehash->tuple.dir;
	flow = container_of(tuplehash, struct flow_offload, tuplehash[dir]);

	mtu = flow->tuplehash[dir].tuple.mtu + offset;
	if (unlikely(nf_flow_exceeds_mtu(skb, mtu)))
		return NF_ACCEPT;

	ip6h = (struct ipv6hdr *)(skb_network_header(skb) + offset);
	thoff = sizeof(*ip6h) + offset;
	if (nf_flow_state_check(flow, ip6h->nexthdr, skb, thoff))
		return NF_ACCEPT;

	if (!nf_flow_dst_check(&tuplehash->tuple)) {
		flow_offload_teardown(flow);
		return NF_ACCEPT;
	}

	if (skb_try_make_writable(skb, thoff + hdrsize))
		return NF_DROP;

	flow_offload_refresh(flow_table, flow);

	nf_flow_encap_pop(skb, tuplehash);

	ip6h = ipv6_hdr(skb);
	nf_flow_nat_ipv6(flow, skb, dir, ip6h);

	ip6h->hop_limit--;
	skb_clear_tstamp(skb);

	if (flow_table->flags & NF_FLOWTABLE_COUNTER)
		nf_ct_acct_update(flow->ct, tuplehash->tuple.dir, skb->len);

	if (unlikely(tuplehash->tuple.xmit_type == FLOW_OFFLOAD_XMIT_XFRM)) {
		rt = (struct rt6_info *)tuplehash->tuple.dst_cache;
		memset(skb->cb, 0, sizeof(struct inet6_skb_parm));
		IP6CB(skb)->iif = skb->dev->ifindex;
		IP6CB(skb)->flags = IP6SKB_FORWARDED;
		return nf_flow_xmit_xfrm(skb, state, &rt->dst);
	}

	switch (tuplehash->tuple.xmit_type) {
	case FLOW_OFFLOAD_XMIT_NEIGH:
		rt = (struct rt6_info *)tuplehash->tuple.dst_cache;
		outdev = rt->dst.dev;
		skb->dev = outdev;
		nexthop = rt6_nexthop(rt, &flow->tuplehash[!dir].tuple.src_v6);
		skb_dst_set_noref(skb, &rt->dst);
		neigh_xmit(NEIGH_ND_TABLE, outdev, nexthop, skb);
		ret = NF_STOLEN;
		break;
	case FLOW_OFFLOAD_XMIT_DIRECT:
		ret = nf_flow_queue_xmit(state->net, skb, tuplehash, ETH_P_IPV6);
		if (ret == NF_DROP)
			flow_offload_teardown(flow);
		break;
	default:
		WARN_ON_ONCE(1);
		ret = NF_DROP;
		break;
	}

	return ret;
}
EXPORT_SYMBOL_GPL(nf_flow_offload_ipv6_hook);
