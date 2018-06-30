#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/netfilter.h>
#include <linux/rhashtable.h>
#include <linux/ipv6.h>
#include <linux/netdevice.h>
#include <net/ipv6.h>
#include <net/ip6_route.h>
#include <net/neighbour.h>
#include <net/netfilter/nf_flow_table.h>
#include <net/netfilter/nf_tables.h>
/* For layer 4 checksum field offset. */
#include <linux/tcp.h>
#include <linux/udp.h>

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

	if (flow->flags & FLOW_OFFLOAD_SNAT &&
	    (nf_flow_snat_port(flow, skb, thoff, ip6h->nexthdr, dir) < 0 ||
	     nf_flow_snat_ipv6(flow, skb, ip6h, thoff, dir) < 0))
		return -1;
	if (flow->flags & FLOW_OFFLOAD_DNAT &&
	    (nf_flow_dnat_port(flow, skb, thoff, ip6h->nexthdr, dir) < 0 ||
	     nf_flow_dnat_ipv6(flow, skb, ip6h, thoff, dir) < 0))
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

	thoff = sizeof(*ip6h);
	if (!pskb_may_pull(skb, thoff + sizeof(*ports)))
		return -1;

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

/* Based on ip_exceeds_mtu(). */
static bool __nf_flow_exceeds_mtu(const struct sk_buff *skb, unsigned int mtu)
{
	if (skb->len <= mtu)
		return false;

	if (skb_is_gso(skb) && skb_gso_validate_network_len(skb, mtu))
		return false;

	return true;
}

static bool nf_flow_exceeds_mtu(struct sk_buff *skb, const struct rt6_info *rt)
{
	u32 mtu;

	mtu = ip6_dst_mtu_forward(&rt->dst);
	if (__nf_flow_exceeds_mtu(skb, mtu))
		return true;

	return false;
}

unsigned int
nf_flow_offload_ipv6_hook(void *priv, struct sk_buff *skb,
			  const struct nf_hook_state *state)
{
	struct flow_offload_tuple_rhash *tuplehash;
	struct nf_flowtable *flow_table = priv;
	struct flow_offload_tuple tuple = {};
	enum flow_offload_tuple_dir dir;
	struct flow_offload *flow;
	struct net_device *outdev;
	struct in6_addr *nexthop;
	struct ipv6hdr *ip6h;
	struct rt6_info *rt;

	if (skb->protocol != htons(ETH_P_IPV6))
		return NF_ACCEPT;

	if (nf_flow_tuple_ipv6(skb, state->in, &tuple) < 0)
		return NF_ACCEPT;

	tuplehash = flow_offload_lookup(flow_table, &tuple);
	if (tuplehash == NULL)
		return NF_ACCEPT;

	outdev = dev_get_by_index_rcu(state->net, tuplehash->tuple.oifidx);
	if (!outdev)
		return NF_ACCEPT;

	dir = tuplehash->tuple.dir;
	flow = container_of(tuplehash, struct flow_offload, tuplehash[dir]);

	rt = (struct rt6_info *)flow->tuplehash[dir].tuple.dst_cache;
	if (unlikely(nf_flow_exceeds_mtu(skb, rt)))
		return NF_ACCEPT;

	if (skb_try_make_writable(skb, sizeof(*ip6h)))
		return NF_DROP;

	if (flow->flags & (FLOW_OFFLOAD_SNAT | FLOW_OFFLOAD_DNAT) &&
	    nf_flow_nat_ipv6(flow, skb, dir) < 0)
		return NF_DROP;

	flow->timeout = (u32)jiffies + NF_FLOW_TIMEOUT;
	ip6h = ipv6_hdr(skb);
	ip6h->hop_limit--;

	skb->dev = outdev;
	nexthop = rt6_nexthop(rt, &flow->tuplehash[!dir].tuple.src_v6);
	skb_dst_set_noref(skb, &rt->dst);
	neigh_xmit(NEIGH_ND_TABLE, outdev, nexthop, skb);

	return NF_STOLEN;
}
EXPORT_SYMBOL_GPL(nf_flow_offload_ipv6_hook);

static struct nf_flowtable_type flowtable_ipv6 = {
	.family		= NFPROTO_IPV6,
	.params		= &nf_flow_offload_rhash_params,
	.gc		= nf_flow_offload_work_gc,
	.free		= nf_flow_table_free,
	.hook		= nf_flow_offload_ipv6_hook,
	.owner		= THIS_MODULE,
};

static int __init nf_flow_ipv6_module_init(void)
{
	nft_register_flowtable_type(&flowtable_ipv6);

	return 0;
}

static void __exit nf_flow_ipv6_module_exit(void)
{
	nft_unregister_flowtable_type(&flowtable_ipv6);
}

module_init(nf_flow_ipv6_module_init);
module_exit(nf_flow_ipv6_module_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Pablo Neira Ayuso <pablo@netfilter.org>");
MODULE_ALIAS_NF_FLOWTABLE(AF_INET6);
