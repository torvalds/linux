// SPDX-License-Identifier: GPL-2.0-only
/*
 * (C) 2008-2009 Pablo Neira Ayuso <pablo@netfilter.org>
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt
#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/jhash.h>
#include <linux/ip.h>
#include <net/ipv6.h>

#include <linux/netfilter/x_tables.h>
#include <net/netfilter/nf_conntrack.h>
#include <linux/netfilter/xt_cluster.h>

static inline u32 nf_ct_orig_ipv4_src(const struct nf_conn *ct)
{
	return (__force u32)ct->tuplehash[IP_CT_DIR_ORIGINAL].tuple.src.u3.ip;
}

static inline const u32 *nf_ct_orig_ipv6_src(const struct nf_conn *ct)
{
	return (__force u32 *)ct->tuplehash[IP_CT_DIR_ORIGINAL].tuple.src.u3.ip6;
}

static inline u_int32_t
xt_cluster_hash_ipv4(u_int32_t ip, const struct xt_cluster_match_info *info)
{
	return jhash_1word(ip, info->hash_seed);
}

static inline u_int32_t
xt_cluster_hash_ipv6(const void *ip, const struct xt_cluster_match_info *info)
{
	return jhash2(ip, NF_CT_TUPLE_L3SIZE / sizeof(__u32), info->hash_seed);
}

static inline u_int32_t
xt_cluster_hash(const struct nf_conn *ct,
		const struct xt_cluster_match_info *info)
{
	u_int32_t hash = 0;

	switch(nf_ct_l3num(ct)) {
	case AF_INET:
		hash = xt_cluster_hash_ipv4(nf_ct_orig_ipv4_src(ct), info);
		break;
	case AF_INET6:
		hash = xt_cluster_hash_ipv6(nf_ct_orig_ipv6_src(ct), info);
		break;
	default:
		WARN_ON(1);
		break;
	}

	return reciprocal_scale(hash, info->total_nodes);
}

static inline bool
xt_cluster_is_multicast_addr(const struct sk_buff *skb, u_int8_t family)
{
	bool is_multicast = false;

	switch(family) {
	case NFPROTO_IPV4:
		is_multicast = ipv4_is_multicast(ip_hdr(skb)->daddr);
		break;
	case NFPROTO_IPV6:
		is_multicast = ipv6_addr_is_multicast(&ipv6_hdr(skb)->daddr);
		break;
	default:
		WARN_ON(1);
		break;
	}
	return is_multicast;
}

static bool
xt_cluster_mt(const struct sk_buff *skb, struct xt_action_param *par)
{
	struct sk_buff *pskb = (struct sk_buff *)skb;
	const struct xt_cluster_match_info *info = par->matchinfo;
	const struct nf_conn *ct;
	enum ip_conntrack_info ctinfo;
	unsigned long hash;

	/* This match assumes that all nodes see the same packets. This can be
	 * achieved if the switch that connects the cluster nodes support some
	 * sort of 'port mirroring'. However, if your switch does not support
	 * this, your cluster nodes can reply ARP request using a multicast MAC
	 * address. Thus, your switch will flood the same packets to the
	 * cluster nodes with the same multicast MAC address. Using a multicast
	 * link address is a RFC 1812 (section 3.3.2) violation, but this works
	 * fine in practise.
	 *
	 * Unfortunately, if you use the multicast MAC address, the link layer
	 * sets skbuff's pkt_type to PACKET_MULTICAST, which is not accepted
	 * by TCP and others for packets coming to this node. For that reason,
	 * this match mangles skbuff's pkt_type if it detects a packet
	 * addressed to a unicast address but using PACKET_MULTICAST. Yes, I
	 * know, matches should not alter packets, but we are doing this here
	 * because we would need to add a PKTTYPE target for this sole purpose.
	 */
	if (!xt_cluster_is_multicast_addr(skb, xt_family(par)) &&
	    skb->pkt_type == PACKET_MULTICAST) {
	    	pskb->pkt_type = PACKET_HOST;
	}

	ct = nf_ct_get(skb, &ctinfo);
	if (ct == NULL)
		return false;

	if (ct->master)
		hash = xt_cluster_hash(ct->master, info);
	else
		hash = xt_cluster_hash(ct, info);

	return !!((1 << hash) & info->node_mask) ^
	       !!(info->flags & XT_CLUSTER_F_INV);
}

static int xt_cluster_mt_checkentry(const struct xt_mtchk_param *par)
{
	struct xt_cluster_match_info *info = par->matchinfo;
	int ret;

	if (info->total_nodes > XT_CLUSTER_NODES_MAX) {
		pr_info_ratelimited("you have exceeded the maximum number of cluster nodes (%u > %u)\n",
				    info->total_nodes, XT_CLUSTER_NODES_MAX);
		return -EINVAL;
	}
	if (info->node_mask >= (1ULL << info->total_nodes)) {
		pr_info_ratelimited("node mask cannot exceed total number of nodes\n");
		return -EDOM;
	}

	ret = nf_ct_netns_get(par->net, par->family);
	if (ret < 0)
		pr_info_ratelimited("cannot load conntrack support for proto=%u\n",
				    par->family);
	return ret;
}

static void xt_cluster_mt_destroy(const struct xt_mtdtor_param *par)
{
	nf_ct_netns_put(par->net, par->family);
}

static struct xt_match xt_cluster_match[] __read_mostly = {
	{
		.name		= "cluster",
		.family		= NFPROTO_IPV4,
		.match		= xt_cluster_mt,
		.checkentry	= xt_cluster_mt_checkentry,
		.matchsize	= sizeof(struct xt_cluster_match_info),
		.destroy	= xt_cluster_mt_destroy,
		.me		= THIS_MODULE,
	},
#if IS_ENABLED(CONFIG_IP6_NF_IPTABLES)
	{
		.name		= "cluster",
		.family		= NFPROTO_IPV6,
		.match		= xt_cluster_mt,
		.checkentry	= xt_cluster_mt_checkentry,
		.matchsize	= sizeof(struct xt_cluster_match_info),
		.destroy	= xt_cluster_mt_destroy,
		.me		= THIS_MODULE,
	},
#endif
};

static int __init xt_cluster_mt_init(void)
{
	return xt_register_matches(xt_cluster_match, ARRAY_SIZE(xt_cluster_match));
}

static void __exit xt_cluster_mt_fini(void)
{
	xt_unregister_matches(xt_cluster_match, ARRAY_SIZE(xt_cluster_match));
}

MODULE_AUTHOR("Pablo Neira Ayuso <pablo@netfilter.org>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Xtables: hash-based cluster match");
MODULE_ALIAS("ipt_cluster");
MODULE_ALIAS("ip6t_cluster");
module_init(xt_cluster_mt_init);
module_exit(xt_cluster_mt_fini);
