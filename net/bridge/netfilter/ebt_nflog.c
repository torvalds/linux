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

static void ebt_nflog(const struct sk_buff *skb,
		      unsigned int hooknr,
		      const struct net_device *in,
		      const struct net_device *out,
		      const void *data, unsigned int datalen)
{
	struct ebt_nflog_info *info = (struct ebt_nflog_info *)data;
	struct nf_loginfo li;

	li.type = NF_LOG_TYPE_ULOG;
	li.u.ulog.copy_len = info->len;
	li.u.ulog.group = info->group;
	li.u.ulog.qthreshold = info->threshold;

	nf_log_packet(PF_BRIDGE, hooknr, skb, in, out, &li, "%s", info->prefix);
}

static int ebt_nflog_check(const char *tablename,
			   unsigned int hookmask,
			   const struct ebt_entry *e,
			   void *data, unsigned int datalen)
{
	struct ebt_nflog_info *info = (struct ebt_nflog_info *)data;

	if (info->flags & ~EBT_NFLOG_MASK)
		return -EINVAL;
	info->prefix[EBT_NFLOG_PREFIX_SIZE - 1] = '\0';
	return 0;
}

static struct ebt_watcher nflog __read_mostly = {
	.name = EBT_NFLOG_WATCHER,
	.watcher = ebt_nflog,
	.check = ebt_nflog_check,
	.targetsize = XT_ALIGN(sizeof(struct ebt_nflog_info)),
	.me = THIS_MODULE,
};

static int __init ebt_nflog_init(void)
{
	return ebt_register_watcher(&nflog);
}

static void __exit ebt_nflog_fini(void)
{
	ebt_unregister_watcher(&nflog);
}

module_init(ebt_nflog_init);
module_exit(ebt_nflog_fini);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Peter Warasin <peter@endian.com>");
MODULE_DESCRIPTION("ebtables NFLOG netfilter logging module");
