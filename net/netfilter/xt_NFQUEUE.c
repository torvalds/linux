// SPDX-License-Identifier: GPL-2.0-only
/* iptables module for using new netfilter netlink queue
 *
 * (C) 2005 by Harald Welte <laforge@netfilter.org>
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/skbuff.h>

#include <linux/netfilter.h>
#include <linux/netfilter_arp.h>
#include <linux/netfilter/x_tables.h>
#include <linux/netfilter/xt_NFQUEUE.h>

#include <net/netfilter/nf_queue.h>

MODULE_AUTHOR("Harald Welte <laforge@netfilter.org>");
MODULE_DESCRIPTION("Xtables: packet forwarding to netlink");
MODULE_LICENSE("GPL");
MODULE_ALIAS("ipt_NFQUEUE");
MODULE_ALIAS("ip6t_NFQUEUE");
MODULE_ALIAS("arpt_NFQUEUE");

static u32 jhash_initval __read_mostly;

static unsigned int
nfqueue_tg(struct sk_buff *skb, const struct xt_action_param *par)
{
	const struct xt_NFQ_info *tinfo = par->targinfo;

	return NF_QUEUE_NR(tinfo->queuenum);
}

static unsigned int
nfqueue_tg_v1(struct sk_buff *skb, const struct xt_action_param *par)
{
	const struct xt_NFQ_info_v1 *info = par->targinfo;
	u32 queue = info->queuenum;

	if (info->queues_total > 1) {
		queue = nfqueue_hash(skb, queue, info->queues_total,
				     xt_family(par), jhash_initval);
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
	const struct xt_NFQ_info_v3 *info = par->targinfo;
	u32 maxid;

	init_hashrandom(&jhash_initval);

	if (info->queues_total == 0) {
		pr_info_ratelimited("number of total queues is 0\n");
		return -EINVAL;
	}
	maxid = info->queues_total - 1 + info->queuenum;
	if (maxid > 0xffff) {
		pr_info_ratelimited("number of queues (%u) out of range (got %u)\n",
				    info->queues_total, maxid);
		return -ERANGE;
	}
	if (par->target->revision == 2 && info->flags > 1)
		return -EINVAL;
	if (par->target->revision == 3 && info->flags & ~NFQ_FLAG_MASK)
		return -EINVAL;

	return 0;
}

static unsigned int
nfqueue_tg_v3(struct sk_buff *skb, const struct xt_action_param *par)
{
	const struct xt_NFQ_info_v3 *info = par->targinfo;
	u32 queue = info->queuenum;
	int ret;

	if (info->queues_total > 1) {
		if (info->flags & NFQ_FLAG_CPU_FANOUT) {
			int cpu = smp_processor_id();

			queue = info->queuenum + cpu % info->queues_total;
		} else {
			queue = nfqueue_hash(skb, queue, info->queues_total,
					     xt_family(par), jhash_initval);
		}
	}

	ret = NF_QUEUE_NR(queue);
	if (info->flags & NFQ_FLAG_BYPASS)
		ret |= NF_VERDICT_FLAG_QUEUE_BYPASS;

	return ret;
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
	{
		.name		= "NFQUEUE",
		.revision	= 3,
		.family		= NFPROTO_UNSPEC,
		.checkentry	= nfqueue_tg_check,
		.target		= nfqueue_tg_v3,
		.targetsize	= sizeof(struct xt_NFQ_info_v3),
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
