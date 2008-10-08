/*
 * ebt_nflog
 *
 *	Author:
 *	Peter Warasin <peter@endian.com>
 *
 *  February, 2008
 *
 * Based on:
 *  xt_NFLOG.c, (C) 2006 by Patrick McHardy <kaber@trash.net>
 *  ebt_ulog.c, (C) 2004 by Bart De Schuymer <bdschuym@pandora.be>
 *
 */

#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/netfilter/x_tables.h>
#include <linux/netfilter_bridge/ebtables.h>
#include <linux/netfilter_bridge/ebt_nflog.h>
#include <net/netfilter/nf_log.h>

static unsigned int
ebt_nflog_tg(struct sk_buff *skb, const struct xt_target_param *par)
{
	const struct ebt_nflog_info *info = par->targinfo;
	struct nf_loginfo li;

	li.type = NF_LOG_TYPE_ULOG;
	li.u.ulog.copy_len = info->len;
	li.u.ulog.group = info->group;
	li.u.ulog.qthreshold = info->threshold;

	nf_log_packet(PF_BRIDGE, par->hooknum, skb, par->in, par->out,
	              &li, "%s", info->prefix);
	return EBT_CONTINUE;
}

static bool
ebt_nflog_tg_check(const char *table, const void *e,
		   const struct xt_target *target, void *data,
		   unsigned int hookmask)
{
	struct ebt_nflog_info *info = data;

	if (info->flags & ~EBT_NFLOG_MASK)
		return false;
	info->prefix[EBT_NFLOG_PREFIX_SIZE - 1] = '\0';
	return true;
}

static struct xt_target ebt_nflog_tg_reg __read_mostly = {
	.name       = "nflog",
	.revision   = 0,
	.family     = NFPROTO_BRIDGE,
	.target     = ebt_nflog_tg,
	.checkentry = ebt_nflog_tg_check,
	.targetsize = XT_ALIGN(sizeof(struct ebt_nflog_info)),
	.me         = THIS_MODULE,
};

static int __init ebt_nflog_init(void)
{
	return xt_register_target(&ebt_nflog_tg_reg);
}

static void __exit ebt_nflog_fini(void)
{
	xt_unregister_target(&ebt_nflog_tg_reg);
}

module_init(ebt_nflog_init);
module_exit(ebt_nflog_fini);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Peter Warasin <peter@endian.com>");
MODULE_DESCRIPTION("ebtables NFLOG netfilter logging module");
