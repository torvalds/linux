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
nfqueue_tg(struct sk_buff *skb, const struct xt_action_param *par)
{
	const struct xt_NFQ_info *tinfo = par->targinfo;

	return NF_QUEUE_NR(tinfo->queuenum);
}

static u32 hash_v4(const struct sk_buff *skb)
{
	const struct iphdr *iph = ip_hdr(skb);

	/* packets in either direction go into same queue */
	if (iph->saddr < iph->daddr)
		return jhash_3words((__force u32)iph->saddr,
			(__force u32)iph->daddr, iph->protocol, jhash_initval);

	return jhash_3words((__force u32)iph->daddr,
			(__force u32)iph->saddr, iph->protocol, jhash_initval);
}

#if IS_ENABLED(CONFIG_IP6_NF_IPTABLES)
static u32 hash_v6(const struct sk_buff *skb)
{
	const struct ipv6hdr *ip6h = ipv6_hdr(skb);
	u32 a, b, c;

	if (ip6h->saddr.s6_addr32[3] < ip6h->daddr.s6_addr32[3]) {
		a = (__force u32) ip6h->saddr.s6_addr32[3];
		b = (__force u32) ip6h->daddr.s6_addr32[3];
	} else {
		b = (__force u32) ip6h->saddr.s6_addr32[3];
		a = (__force u32) ip6h->daddr.s6_addr32[3];
	}

	if (ip6h->saddr.s6_addr32[1] < ip6h->daddr.s6_addr32[1])
		c = (__force u32) ip6h->saddr.s6_addr32[1];
	else
		c = (__force u32) ip6h->daddr.s6_addr32[1];

	return jhash_3words(a, b, c, jhash_initval);
}
#endif

static unsigned int
nfqueue_tg_v1(struct sk_buff *skb, const struct xt_action_param *par)
{
	const struct xt_NFQ_info_v1 *info = par->targinfo;
	u32 queue = info->queuenum;

	if (info->queues_total > 1) {
		if (par->family == NFPROTO_IPV4)
			queue = (((u64) hash_v4(skb) * info->queues_total) >>
				 32) + queue;
#if IS_ENABLED(CONFIG_IP6_NF_IPTABLES)
		else if (par->family == NFPROTO_IPV6)
			queue = (((u64) hash_v6(skb) * info->queues_total) >>
				 32) + queue;
#endif
	}
	return NF_QUEUE_NR(queue);
}

static unsigned int
nfqueue_tg_v2(struct sk_buff *skb, const struct xt_action_param *par)
{
	const struct xt_NFQ_info_v2 *info = par->targinfo;
	unsigned int ret = nfqueue_tg_v1(skb, par);

	if (info->bypass)
		ret |= NF_VERDICT_FLAG_QUEUE_BYPASS;
	return ret;
}

static int nfqueue_tg_check(const struct xt_tgchk_param *par)
{
	const struct xt_NFQ_info_v2 *info = par->targinfo;
	u32 maxid;

	if (unlikely(!rnd_inited)) {
		get_random_bytes(&jhash_initval, sizeof(jhash_initval));
		rnd_inited = true;
	}
	if (info->queues_total == 0) {
		pr_err("NFQUEUE: number of total queues is 0\n");
		return -EINVAL;
	}
	maxid = info->queues_total - 1 + info->queuenum;
	if (maxid > 0xffff) {
		pr_err("NFQUEUE: number of queues (%u) out of range (got %u)\n",
		       info->queues_total, maxid);
		return -ERANGE;
	}
	if (par->target->revision == 2 && info->bypass > 1)
		return -EINVAL;
	return 0;
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
		.family		= NFPROTO_UNSPEC,
		.checkentry	= nfqueue_tg_check,
		.target		= nfqueue_tg_v1,
		.targetsize	= sizeof(struct xt_NFQ_info_v1),
		.me		= THIS_MODULE,
	},
	{
		.name		= "NFQUEUE",
		.revision	= 2,
		.family		= NFPROTO_UNSPEC,
		.checkentry	= nfqueue_tg_check,
		.target		= nfqueue_tg_v2,
		.targetsize	= sizeof(struct xt_NFQ_info_v2),
		.me		= THIS_MODULE,
	},
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
