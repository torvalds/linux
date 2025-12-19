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
#include <net/gso.h>
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
	if (tcph->syn && test_bit(NF_FLOW_CLOSING, &flow->flags)) {
		flow_offload_teardown(flow);
		return -1;
	}

	if ((tcph->fin || tcph->rst) &&
	    !test_bit(NF_FLOW_CLOSING, &flow->flags))
		set_bit(NF_FLOW_CLOSING, &flow->flags);

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
	__be16 inner_proto = skb->protocol;
	struct vlan_ethhdr *veth;
	struct pppoe_hdr *phdr;
	struct iphdr *iph;
	u16 offset = 0;
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
		inner_proto = veth->h_vlan_encapsulated_proto;
		offset += VLAN_HLEN;
		break;
	case htons(ETH_P_PPP_SES):
		phdr = (struct pppoe_hdr *)skb_network_header(skb);
		tuple->encap[i].id = ntohs(phdr->sid);
		tuple->encap[i].proto = skb->protocol;
		inner_proto = *((__be16 *)(phdr + 1));
		offset += PPPOE_SES_HLEN;
		break;
	}

	if (inner_proto == htons(ETH_P_IP)) {
		iph = (struct iphdr *)(skb_network_header(skb) + offset);
		if (iph->protocol == IPPROTO_IPIP) {
			tuple->tun.dst_v4.s_addr = iph->daddr;
			tuple->tun.src_v4.s_addr = iph->saddr;
			tuple->tun.l3_proto = IPPROTO_IPIP;
		}
	}
}

struct nf_flowtable_ctx {
	const struct net_device	*in;
	u32			offset;
	u32			hdrsize;
};

static int nf_flow_tuple_ip(struct nf_flowtable_ctx *ctx, struct sk_buff *skb,
			    struct flow_offload_tuple *tuple)
{
	struct flow_ports *ports;
	unsigned int thoff;
	struct iphdr *iph;
	u8 ipproto;

	if (!pskb_may_pull(skb, sizeof(*iph) + ctx->offset))
		return -1;

	iph = (struct iphdr *)(skb_network_header(skb) + ctx->offset);
	thoff = (iph->ihl * 4);

	if (ip_is_fragment(iph) ||
	    unlikely(ip_has_options(thoff)))
		return -1;

	thoff += ctx->offset;

	ipproto = iph->protocol;
	switch (ipproto) {
	case IPPROTO_TCP:
		ctx->hdrsize = sizeof(struct tcphdr);
		break;
	case IPPROTO_UDP:
		ctx->hdrsize = sizeof(struct udphdr);
		break;
#ifdef CONFIG_NF_CT_PROTO_GRE
	case IPPROTO_GRE:
		ctx->hdrsize = sizeof(struct gre_base_hdr);
		break;
#endif
	default:
		return -1;
	}

	if (iph->ttl <= 1)
		return -1;

	if (!pskb_may_pull(skb, thoff + ctx->hdrsize))
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

	iph = (struct iphdr *)(skb_network_header(skb) + ctx->offset);

	tuple->src_v4.s_addr	= iph->saddr;
	tuple->dst_v4.s_addr	= iph->daddr;
	tuple->l3proto		= AF_INET;
	tuple->l4proto		= ipproto;
	tuple->iifidx		= ctx->in->ifindex;
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

static bool nf_flow_ip4_tunnel_proto(struct sk_buff *skb, u32 *psize)
{
	struct iphdr *iph;
	u16 size;

	if (!pskb_may_pull(skb, sizeof(*iph) + *psize))
		return false;

	iph = (struct iphdr *)(skb_network_header(skb) + *psize);
	size = iph->ihl << 2;

	if (ip_is_fragment(iph) || unlikely(ip_has_options(size)))
		return false;

	if (iph->ttl <= 1)
		return false;

	if (iph->protocol == IPPROTO_IPIP)
		*psize += size;

	return true;
}

static void nf_flow_ip4_tunnel_pop(struct sk_buff *skb)
{
	struct iphdr *iph = (struct iphdr *)skb_network_header(skb);

	if (iph->protocol != IPPROTO_IPIP)
		return;

	skb_pull(skb, iph->ihl << 2);
	skb_reset_network_header(skb);
}

static bool nf_flow_skb_encap_protocol(struct sk_buff *skb, __be16 proto,
				       u32 *offset)
{
	__be16 inner_proto = skb->protocol;
	struct vlan_ethhdr *veth;
	bool ret = false;

	switch (skb->protocol) {
	case htons(ETH_P_8021Q):
		if (!pskb_may_pull(skb, skb_mac_offset(skb) + sizeof(*veth)))
			return false;

		veth = (struct vlan_ethhdr *)skb_mac_header(skb);
		if (veth->h_vlan_encapsulated_proto == proto) {
			*offset += VLAN_HLEN;
			inner_proto = proto;
			ret = true;
		}
		break;
	case htons(ETH_P_PPP_SES):
		if (nf_flow_pppoe_proto(skb, &inner_proto) &&
		    inner_proto == proto) {
			*offset += PPPOE_SES_HLEN;
			ret = true;
		}
		break;
	}

	if (inner_proto == htons(ETH_P_IP))
		ret = nf_flow_ip4_tunnel_proto(skb, offset);

	return ret;
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
			skb->protocol = __nf_flow_pppoe_proto(skb);
			skb_pull(skb, PPPOE_SES_HLEN);
			skb_reset_network_header(skb);
			break;
		}
	}

	if (skb->protocol == htons(ETH_P_IP))
		nf_flow_ip4_tunnel_pop(skb);
}

struct nf_flow_xmit {
	const void		*dest;
	const void		*source;
	struct net_device	*outdev;
};

static unsigned int nf_flow_queue_xmit(struct net *net, struct sk_buff *skb,
				       struct nf_flow_xmit *xmit)
{
	skb->dev = xmit->outdev;
	dev_hard_header(skb, skb->dev, ntohs(skb->protocol),
			xmit->dest, xmit->source, skb->len);
	dev_queue_xmit(skb);

	return NF_STOLEN;
}

static struct flow_offload_tuple_rhash *
nf_flow_offload_lookup(struct nf_flowtable_ctx *ctx,
		       struct nf_flowtable *flow_table, struct sk_buff *skb)
{
	struct flow_offload_tuple tuple = {};

	if (!nf_flow_skb_encap_protocol(skb, htons(ETH_P_IP), &ctx->offset))
		return NULL;

	if (nf_flow_tuple_ip(ctx, skb, &tuple) < 0)
		return NULL;

	return flow_offload_lookup(flow_table, &tuple);
}

static int nf_flow_offload_forward(struct nf_flowtable_ctx *ctx,
				   struct nf_flowtable *flow_table,
				   struct flow_offload_tuple_rhash *tuplehash,
				   struct sk_buff *skb)
{
	enum flow_offload_tuple_dir dir;
	struct flow_offload *flow;
	unsigned int thoff, mtu;
	struct iphdr *iph;

	dir = tuplehash->tuple.dir;
	flow = container_of(tuplehash, struct flow_offload, tuplehash[dir]);

	mtu = flow->tuplehash[dir].tuple.mtu + ctx->offset;
	if (flow->tuplehash[!dir].tuple.tun_num)
		mtu -= sizeof(*iph);

	if (unlikely(nf_flow_exceeds_mtu(skb, mtu)))
		return 0;

	iph = (struct iphdr *)(skb_network_header(skb) + ctx->offset);
	thoff = (iph->ihl * 4) + ctx->offset;
	if (nf_flow_state_check(flow, iph->protocol, skb, thoff))
		return 0;

	if (!nf_flow_dst_check(&tuplehash->tuple)) {
		flow_offload_teardown(flow);
		return 0;
	}

	if (skb_try_make_writable(skb, thoff + ctx->hdrsize))
		return -1;

	flow_offload_refresh(flow_table, flow, false);

	nf_flow_encap_pop(skb, tuplehash);
	thoff -= ctx->offset;

	iph = ip_hdr(skb);
	nf_flow_nat_ip(flow, skb, thoff, dir, iph);

	ip_decrease_ttl(iph);
	skb_clear_tstamp(skb);

	if (flow_table->flags & NF_FLOWTABLE_COUNTER)
		nf_ct_acct_update(flow->ct, tuplehash->tuple.dir, skb->len);

	return 1;
}

static int nf_flow_pppoe_push(struct sk_buff *skb, u16 id)
{
	int data_len = skb->len + sizeof(__be16);
	struct ppp_hdr {
		struct pppoe_hdr hdr;
		__be16 proto;
	} *ph;
	__be16 proto;

	if (skb_cow_head(skb, PPPOE_SES_HLEN))
		return -1;

	switch (skb->protocol) {
	case htons(ETH_P_IP):
		proto = htons(PPP_IP);
		break;
	case htons(ETH_P_IPV6):
		proto = htons(PPP_IPV6);
		break;
	default:
		return -1;
	}

	__skb_push(skb, PPPOE_SES_HLEN);
	skb_reset_network_header(skb);

	ph = (struct ppp_hdr *)(skb->data);
	ph->hdr.ver	= 1;
	ph->hdr.type	= 1;
	ph->hdr.code	= 0;
	ph->hdr.sid	= htons(id);
	ph->hdr.length	= htons(data_len);
	ph->proto	= proto;
	skb->protocol	= htons(ETH_P_PPP_SES);

	return 0;
}

static int nf_flow_tunnel_ipip_push(struct net *net, struct sk_buff *skb,
				    struct flow_offload_tuple *tuple,
				    __be32 *ip_daddr)
{
	struct iphdr *iph = (struct iphdr *)skb_network_header(skb);
	struct rtable *rt = dst_rtable(tuple->dst_cache);
	u8 tos = iph->tos, ttl = iph->ttl;
	__be16 frag_off = iph->frag_off;
	u32 headroom = sizeof(*iph);
	int err;

	err = iptunnel_handle_offloads(skb, SKB_GSO_IPXIP4);
	if (err)
		return err;

	skb_set_inner_ipproto(skb, IPPROTO_IPIP);
	headroom += LL_RESERVED_SPACE(rt->dst.dev) + rt->dst.header_len;
	err = skb_cow_head(skb, headroom);
	if (err)
		return err;

	skb_scrub_packet(skb, true);
	skb_clear_hash_if_not_l4(skb);

	/* Push down and install the IP header. */
	skb_push(skb, sizeof(*iph));
	skb_reset_network_header(skb);

	iph = ip_hdr(skb);
	iph->version	= 4;
	iph->ihl	= sizeof(*iph) >> 2;
	iph->frag_off	= ip_mtu_locked(&rt->dst) ? 0 : frag_off;
	iph->protocol	= tuple->tun.l3_proto;
	iph->tos	= tos;
	iph->daddr	= tuple->tun.src_v4.s_addr;
	iph->saddr	= tuple->tun.dst_v4.s_addr;
	iph->ttl	= ttl;
	iph->tot_len	= htons(skb->len);
	__ip_select_ident(net, iph, skb_shinfo(skb)->gso_segs ?: 1);
	ip_send_check(iph);

	*ip_daddr = tuple->tun.src_v4.s_addr;

	return 0;
}

static int nf_flow_tunnel_v4_push(struct net *net, struct sk_buff *skb,
				  struct flow_offload_tuple *tuple,
				  __be32 *ip_daddr)
{
	if (tuple->tun_num)
		return nf_flow_tunnel_ipip_push(net, skb, tuple, ip_daddr);

	return 0;
}

static int nf_flow_encap_push(struct sk_buff *skb,
			      struct flow_offload_tuple *tuple)
{
	int i;

	for (i = 0; i < tuple->encap_num; i++) {
		switch (tuple->encap[i].proto) {
		case htons(ETH_P_8021Q):
		case htons(ETH_P_8021AD):
			if (skb_vlan_push(skb, tuple->encap[i].proto,
					  tuple->encap[i].id) < 0)
				return -1;
			break;
		case htons(ETH_P_PPP_SES):
			if (nf_flow_pppoe_push(skb, tuple->encap[i].id) < 0)
				return -1;
			break;
		}
	}

	return 0;
}

unsigned int
nf_flow_offload_ip_hook(void *priv, struct sk_buff *skb,
			const struct nf_hook_state *state)
{
	struct flow_offload_tuple_rhash *tuplehash;
	struct nf_flowtable *flow_table = priv;
	struct flow_offload_tuple *other_tuple;
	enum flow_offload_tuple_dir dir;
	struct nf_flowtable_ctx ctx = {
		.in	= state->in,
	};
	struct nf_flow_xmit xmit = {};
	struct flow_offload *flow;
	struct neighbour *neigh;
	struct rtable *rt;
	__be32 ip_daddr;
	int ret;

	tuplehash = nf_flow_offload_lookup(&ctx, flow_table, skb);
	if (!tuplehash)
		return NF_ACCEPT;

	ret = nf_flow_offload_forward(&ctx, flow_table, tuplehash, skb);
	if (ret < 0)
		return NF_DROP;
	else if (ret == 0)
		return NF_ACCEPT;

	if (unlikely(tuplehash->tuple.xmit_type == FLOW_OFFLOAD_XMIT_XFRM)) {
		rt = dst_rtable(tuplehash->tuple.dst_cache);
		memset(skb->cb, 0, sizeof(struct inet_skb_parm));
		IPCB(skb)->iif = skb->dev->ifindex;
		IPCB(skb)->flags = IPSKB_FORWARDED;
		return nf_flow_xmit_xfrm(skb, state, &rt->dst);
	}

	dir = tuplehash->tuple.dir;
	flow = container_of(tuplehash, struct flow_offload, tuplehash[dir]);
	other_tuple = &flow->tuplehash[!dir].tuple;
	ip_daddr = other_tuple->src_v4.s_addr;

	if (nf_flow_tunnel_v4_push(state->net, skb, other_tuple, &ip_daddr) < 0)
		return NF_DROP;

	if (nf_flow_encap_push(skb, other_tuple) < 0)
		return NF_DROP;

	switch (tuplehash->tuple.xmit_type) {
	case FLOW_OFFLOAD_XMIT_NEIGH:
		rt = dst_rtable(tuplehash->tuple.dst_cache);
		xmit.outdev = dev_get_by_index_rcu(state->net, tuplehash->tuple.ifidx);
		if (!xmit.outdev) {
			flow_offload_teardown(flow);
			return NF_DROP;
		}
		neigh = ip_neigh_gw4(rt->dst.dev, rt_nexthop(rt, ip_daddr));
		if (IS_ERR(neigh)) {
			flow_offload_teardown(flow);
			return NF_DROP;
		}
		xmit.dest = neigh->ha;
		skb_dst_set_noref(skb, &rt->dst);
		break;
	case FLOW_OFFLOAD_XMIT_DIRECT:
		xmit.outdev = dev_get_by_index_rcu(state->net, tuplehash->tuple.out.ifidx);
		if (!xmit.outdev) {
			flow_offload_teardown(flow);
			return NF_DROP;
		}
		xmit.dest = tuplehash->tuple.out.h_dest;
		xmit.source = tuplehash->tuple.out.h_source;
		break;
	default:
		WARN_ON_ONCE(1);
		return NF_DROP;
	}

	return nf_flow_queue_xmit(state->net, skb, &xmit);
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

static int nf_flow_tuple_ipv6(struct nf_flowtable_ctx *ctx, struct sk_buff *skb,
			      struct flow_offload_tuple *tuple)
{
	struct flow_ports *ports;
	struct ipv6hdr *ip6h;
	unsigned int thoff;
	u8 nexthdr;

	thoff = sizeof(*ip6h) + ctx->offset;
	if (!pskb_may_pull(skb, thoff))
		return -1;

	ip6h = (struct ipv6hdr *)(skb_network_header(skb) + ctx->offset);

	nexthdr = ip6h->nexthdr;
	switch (nexthdr) {
	case IPPROTO_TCP:
		ctx->hdrsize = sizeof(struct tcphdr);
		break;
	case IPPROTO_UDP:
		ctx->hdrsize = sizeof(struct udphdr);
		break;
#ifdef CONFIG_NF_CT_PROTO_GRE
	case IPPROTO_GRE:
		ctx->hdrsize = sizeof(struct gre_base_hdr);
		break;
#endif
	default:
		return -1;
	}

	if (ip6h->hop_limit <= 1)
		return -1;

	if (!pskb_may_pull(skb, thoff + ctx->hdrsize))
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

	ip6h = (struct ipv6hdr *)(skb_network_header(skb) + ctx->offset);

	tuple->src_v6		= ip6h->saddr;
	tuple->dst_v6		= ip6h->daddr;
	tuple->l3proto		= AF_INET6;
	tuple->l4proto		= nexthdr;
	tuple->iifidx		= ctx->in->ifindex;
	nf_flow_tuple_encap(skb, tuple);

	return 0;
}

static int nf_flow_offload_ipv6_forward(struct nf_flowtable_ctx *ctx,
					struct nf_flowtable *flow_table,
					struct flow_offload_tuple_rhash *tuplehash,
					struct sk_buff *skb)
{
	enum flow_offload_tuple_dir dir;
	struct flow_offload *flow;
	unsigned int thoff, mtu;
	struct ipv6hdr *ip6h;

	dir = tuplehash->tuple.dir;
	flow = container_of(tuplehash, struct flow_offload, tuplehash[dir]);

	mtu = flow->tuplehash[dir].tuple.mtu + ctx->offset;
	if (unlikely(nf_flow_exceeds_mtu(skb, mtu)))
		return 0;

	ip6h = (struct ipv6hdr *)(skb_network_header(skb) + ctx->offset);
	thoff = sizeof(*ip6h) + ctx->offset;
	if (nf_flow_state_check(flow, ip6h->nexthdr, skb, thoff))
		return 0;

	if (!nf_flow_dst_check(&tuplehash->tuple)) {
		flow_offload_teardown(flow);
		return 0;
	}

	if (skb_try_make_writable(skb, thoff + ctx->hdrsize))
		return -1;

	flow_offload_refresh(flow_table, flow, false);

	nf_flow_encap_pop(skb, tuplehash);

	ip6h = ipv6_hdr(skb);
	nf_flow_nat_ipv6(flow, skb, dir, ip6h);

	ip6h->hop_limit--;
	skb_clear_tstamp(skb);

	if (flow_table->flags & NF_FLOWTABLE_COUNTER)
		nf_ct_acct_update(flow->ct, tuplehash->tuple.dir, skb->len);

	return 1;
}

static struct flow_offload_tuple_rhash *
nf_flow_offload_ipv6_lookup(struct nf_flowtable_ctx *ctx,
			    struct nf_flowtable *flow_table,
			    struct sk_buff *skb)
{
	struct flow_offload_tuple tuple = {};

	if (skb->protocol != htons(ETH_P_IPV6) &&
	    !nf_flow_skb_encap_protocol(skb, htons(ETH_P_IPV6), &ctx->offset))
		return NULL;

	if (nf_flow_tuple_ipv6(ctx, skb, &tuple) < 0)
		return NULL;

	return flow_offload_lookup(flow_table, &tuple);
}

unsigned int
nf_flow_offload_ipv6_hook(void *priv, struct sk_buff *skb,
			  const struct nf_hook_state *state)
{
	struct flow_offload_tuple_rhash *tuplehash;
	struct nf_flowtable *flow_table = priv;
	struct flow_offload_tuple *other_tuple;
	enum flow_offload_tuple_dir dir;
	struct nf_flowtable_ctx ctx = {
		.in	= state->in,
	};
	struct nf_flow_xmit xmit = {};
	struct in6_addr *ip6_daddr;
	struct flow_offload *flow;
	struct neighbour *neigh;
	struct rt6_info *rt;
	int ret;

	tuplehash = nf_flow_offload_ipv6_lookup(&ctx, flow_table, skb);
	if (tuplehash == NULL)
		return NF_ACCEPT;

	ret = nf_flow_offload_ipv6_forward(&ctx, flow_table, tuplehash, skb);
	if (ret < 0)
		return NF_DROP;
	else if (ret == 0)
		return NF_ACCEPT;

	if (unlikely(tuplehash->tuple.xmit_type == FLOW_OFFLOAD_XMIT_XFRM)) {
		rt = dst_rt6_info(tuplehash->tuple.dst_cache);
		memset(skb->cb, 0, sizeof(struct inet6_skb_parm));
		IP6CB(skb)->iif = skb->dev->ifindex;
		IP6CB(skb)->flags = IP6SKB_FORWARDED;
		return nf_flow_xmit_xfrm(skb, state, &rt->dst);
	}

	dir = tuplehash->tuple.dir;
	flow = container_of(tuplehash, struct flow_offload, tuplehash[dir]);
	other_tuple = &flow->tuplehash[!dir].tuple;
	ip6_daddr = &other_tuple->src_v6;

	if (nf_flow_encap_push(skb, other_tuple) < 0)
		return NF_DROP;

	switch (tuplehash->tuple.xmit_type) {
	case FLOW_OFFLOAD_XMIT_NEIGH:
		rt = dst_rt6_info(tuplehash->tuple.dst_cache);
		xmit.outdev = dev_get_by_index_rcu(state->net, tuplehash->tuple.ifidx);
		if (!xmit.outdev) {
			flow_offload_teardown(flow);
			return NF_DROP;
		}
		neigh = ip_neigh_gw6(rt->dst.dev, rt6_nexthop(rt, ip6_daddr));
		if (IS_ERR(neigh)) {
			flow_offload_teardown(flow);
			return NF_DROP;
		}
		xmit.dest = neigh->ha;
		skb_dst_set_noref(skb, &rt->dst);
		break;
	case FLOW_OFFLOAD_XMIT_DIRECT:
		xmit.outdev = dev_get_by_index_rcu(state->net, tuplehash->tuple.out.ifidx);
		if (!xmit.outdev) {
			flow_offload_teardown(flow);
			return NF_DROP;
		}
		xmit.dest = tuplehash->tuple.out.h_dest;
		xmit.source = tuplehash->tuple.out.h_source;
		break;
	default:
		WARN_ON_ONCE(1);
		return NF_DROP;
	}

	return nf_flow_queue_xmit(state->net, skb, &xmit);
}
EXPORT_SYMBOL_GPL(nf_flow_offload_ipv6_hook);
