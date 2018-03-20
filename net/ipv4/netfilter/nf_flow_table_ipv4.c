#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/netfilter.h>
#include <linux/rhashtable.h>
#include <linux/ip.h>
#include <linux/netdevice.h>
#include <net/ip.h>
#include <net/neighbour.h>
#include <net/netfilter/nf_flow_table.h>
#include <net/netfilter/nf_tables.h>
/* For layer 4 checksum field offset. */
#include <linux/tcp.h>
#include <linux/udp.h>

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
			  enum flow_offload_tuple_dir dir)
{
	struct iphdr *iph = ip_hdr(skb);
	unsigned int thoff = iph->ihl * 4;

	if (flow->flags & FLOW_OFFLOAD_SNAT &&
	    (nf_flow_snat_port(flow, skb, thoff, iph->protocol, dir) < 0 ||
	     nf_flow_snat_ip(flow, skb, iph, thoff, dir) < 0))
		return -1;
	if (flow->flags & FLOW_OFFLOAD_DNAT &&
	    (nf_flow_dnat_port(flow, skb, thoff, iph->protocol, dir) < 0 ||
	     nf_flow_dnat_ip(flow, skb, iph, thoff, dir) < 0))
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

	thoff = iph->ihl * 4;
	if (!pskb_may_pull(skb, thoff + sizeof(*ports)))
		return -1;

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
static bool __nf_flow_exceeds_mtu(const struct sk_buff *skb, unsigned int mtu)
{
	if (skb->len <= mtu)
		return false;

	if ((ip_hdr(skb)->frag_off & htons(IP_DF)) == 0)
		return false;

	if (skb_is_gso(skb) && skb_gso_validate_network_len(skb, mtu))
		return false;

	return true;
}

static bool nf_flow_exceeds_mtu(struct sk_buff *skb, const struct rtable *rt)
{
	u32 mtu;

	mtu = ip_dst_mtu_maybe_forward(&rt->dst, true);
	if (__nf_flow_exceeds_mtu(skb, mtu))
		return true;

	return false;
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
	const struct rtable *rt;
	struct iphdr *iph;
	__be32 nexthop;

	if (skb->protocol != htons(ETH_P_IP))
		return NF_ACCEPT;

	if (nf_flow_tuple_ip(skb, state->in, &tuple) < 0)
		return NF_ACCEPT;

	tuplehash = flow_offload_lookup(flow_table, &tuple);
	if (tuplehash == NULL)
		return NF_ACCEPT;

	outdev = dev_get_by_index_rcu(state->net, tuplehash->tuple.oifidx);
	if (!outdev)
		return NF_ACCEPT;

	dir = tuplehash->tuple.dir;
	flow = container_of(tuplehash, struct flow_offload, tuplehash[dir]);

	rt = (const struct rtable *)flow->tuplehash[dir].tuple.dst_cache;
	if (unlikely(nf_flow_exceeds_mtu(skb, rt)))
		return NF_ACCEPT;

	if (skb_try_make_writable(skb, sizeof(*iph)))
		return NF_DROP;

	if (flow->flags & (FLOW_OFFLOAD_SNAT | FLOW_OFFLOAD_DNAT) &&
	    nf_flow_nat_ip(flow, skb, dir) < 0)
		return NF_DROP;

	flow->timeout = (u32)jiffies + NF_FLOW_TIMEOUT;
	iph = ip_hdr(skb);
	ip_decrease_ttl(iph);

	skb->dev = outdev;
	nexthop = rt_nexthop(rt, flow->tuplehash[!dir].tuple.src_v4.s_addr);
	neigh_xmit(NEIGH_ARP_TABLE, outdev, &nexthop, skb);

	return NF_STOLEN;
}
EXPORT_SYMBOL_GPL(nf_flow_offload_ip_hook);

static struct nf_flowtable_type flowtable_ipv4 = {
	.family		= NFPROTO_IPV4,
	.params		= &nf_flow_offload_rhash_params,
	.gc		= nf_flow_offload_work_gc,
	.free		= nf_flow_table_free,
	.hook		= nf_flow_offload_ip_hook,
	.owner		= THIS_MODULE,
};

static int __init nf_flow_ipv4_module_init(void)
{
	nft_register_flowtable_type(&flowtable_ipv4);

	return 0;
}

static void __exit nf_flow_ipv4_module_exit(void)
{
	nft_unregister_flowtable_type(&flowtable_ipv4);
}

module_init(nf_flow_ipv4_module_init);
module_exit(nf_flow_ipv4_module_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Pablo Neira Ayuso <pablo@netfilter.org>");
MODULE_ALIAS_NF_FLOWTABLE(AF_INET);
