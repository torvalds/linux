/* iptables module for using new netfilter netlink queue
 *
 * (C) 2005 by Harald Welte <laforge@netfilter.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/module.h>
#include <linux/skbuff.h>

#include <linux/ip.h>
#include <linux/ipv6.h>
#include <linux/jhash.h>

#include <linux/netfilter.h>
#include <linux/netfilter_arp.h>
#include <linux/netfilter/x_tables.h>
#include <linux/netfilter/xt_NFQUEUE.h>

MODULE_AUTHOR("Harald Welte <laforge@netfilter.org>");
MODULE_DESCRIPTION("Xtables: packet forwarding to netlink");
MODULE_LICENSE("GPL");
MODULE_ALIAS("ipt_NFQUEUE");
MODULE_ALIAS("ip6t_NFQUEUE");
MODULE_ALIAS("arpt_NFQUEUE");

static u32 jhash_initval __read_mostly;
static bool rnd_inited __read_mostly;

static unsigned int
nfqueue_tg(struct sk_buff *skb, const struct xt_target_param *par)
{
	const struct xt_NFQ_info *tinfo = par->targinfo;

	return NF_QUEUE_NR(tinfo->queuenum);
}

static u32 hash_v4(const struct sk_buff *skb)
{
	const struct iphdr *iph = ip_hdr(skb);
	__be32 ipaddr;

	/* packets in either direction go into same queue */
	ipaddr = iph->saddr ^ iph->daddr;

	return jhash_2words((__force u32)ipaddr, iph->protocol, jhash_initval);
}

static unsigned int
nfqueue_tg4_v1(struct sk_buff *skb, const struct xt_target_param *par)
{
	const struct xt_NFQ_info_v1 *info = par->targinfo;
	u32 queue = info->queuenum;

	if (info->queues_total > 1)
		queue = hash_v4(skb) % info->queues_total + queue;
	return NF_QUEUE_NR(queue);
}

#if defined(CONFIG_IP6_NF_IPTABLES) || defined(CONFIG_IP6_NF_IPTABLES_MODULE)
static u32 hash_v6(const struct sk_buff *skb)
{
	const struct ipv6hdr *ip6h = ipv6_hdr(skb);
	__be32 addr[4];

	addr[0] = ip6h->saddr.s6_addr32[0] ^ ip6h->daddr.s6_addr32[0];
	addr[1] = ip6h->saddr.s6_addr32[1] ^ ip6h->daddr.s6_addr32[1];
	addr[2] = ip6h->saddr.s6_addr32[2] ^ ip6h->daddr.s6_addr32[2];
	addr[3] = ip6h->saddr.s6_addr32[3] ^ ip6h->daddr.s6_addr32[3];

	return jhash2((__force u32 *)addr, ARRAY_SIZE(addr), jhash_initval);
}

static unsigned int
nfqueue_tg6_v1(struct sk_buff *skb, const struct xt_target_param *par)
{
	const struct xt_NFQ_info_v1 *info = par->targinfo;
	u32 queue = info->queuenum;

	if (info->queues_total > 1)
		queue = hash_v6(skb) % info->queues_total + queue;
	return NF_QUEUE_NR(queue);
}
#endif

static bool nfqueue_tg_v1_check(const struct xt_tgchk_param *par)
{
	const struct xt_NFQ_info_v1 *info = par->targinfo;
	u32 maxid;

	if (unlikely(!rnd_inited)) {
		get_random_bytes(&jhash_initval, sizeof(jhash_initval));
		rnd_inited = true;
	}
	if (info->queues_total == 0) {
		pr_err("NFQUEUE: number of total queues is 0\n");
		return false;
	}
	maxid = info->queues_total - 1 + info->queuenum;
	if (maxid > 0xffff) {
		pr_err("NFQUEUE: number of queues (%u) out of range (got %u)\n",
		       info->queues_total, maxid);
		return false;
	}
	return true;
}

static struct xt_target nfqueue_tg_reg[] __read_mostly = {
	{
		.name		= "NFQUEUE",
		.family		= NFPROTO_UNSPEC,
		.target		= nfqueue_tg,
		.targetsize	= sizeof(struct xt_NFQ_info),
		.me		= THIS_MODULE,
	},
	{
		.name		= "NFQUEUE",
		.revision	= 1,
		.family		= NFPROTO_IPV4,
		.checkentry	= nfqueue_tg_v1_check,
		.target		= nfqueue_tg4_v1,
		.targetsize	= sizeof(struct xt_NFQ_info_v1),
		.me		= THIS_MODULE,
	},
#if defined(CONFIG_IP6_NF_IPTABLES) || defined(CONFIG_IP6_NF_IPTABLES_MODULE)
	{
		.name		= "NFQUEUE",
		.revision	= 1,
		.family		= NFPROTO_IPV6,
		.checkentry	= nfqueue_tg_v1_check,
		.target		= nfqueue_tg6_v1,
		.targetsize	= sizeof(struct xt_NFQ_info_v1),
		.me		= THIS_MODULE,
	},
#endif
};

static int __init nfqueue_tg_init(void)
{
	return xt_register_targets(nfqueue_tg_reg, ARRAY_SIZE(nfqueue_tg_reg));
}

static void __exit nfqueue_tg_exit(void)
{
	xt_unregister_targets(nfqueue_tg_reg, ARRAY_SIZE(nfqueue_tg_reg));
}

module_init(nfqueue_tg_init);
module_exit(nfqueue_tg_exit);
