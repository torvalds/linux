// SPDX-License-Identifier: GPL-2.0-only
/*
 * Creates audit record for dropped/accepted packets
 *
 * (C) 2010-2011 Thomas Graf <tgraf@redhat.com>
 * (C) 2010-2011 Red Hat, Inc.
*/

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/audit.h>
#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/tcp.h>
#include <linux/udp.h>
#include <linux/if_arp.h>
#include <linux/netfilter/x_tables.h>
#include <linux/netfilter/xt_AUDIT.h>
#include <linux/netfilter_bridge/ebtables.h>
#include <net/ipv6.h>
#include <net/ip.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Thomas Graf <tgraf@redhat.com>");
MODULE_DESCRIPTION("Xtables: creates audit records for dropped/accepted packets");
MODULE_ALIAS("ipt_AUDIT");
MODULE_ALIAS("ip6t_AUDIT");
MODULE_ALIAS("ebt_AUDIT");
MODULE_ALIAS("arpt_AUDIT");

static unsigned int
audit_tg(struct sk_buff *skb, const struct xt_action_param *par)
{
	struct audit_buffer *ab;

	if (audit_enabled == AUDIT_OFF)
		goto errout;
	ab = audit_log_start(NULL, GFP_ATOMIC, AUDIT_NETFILTER_PKT);
	if (ab == NULL)
		goto errout;

	audit_log_format(ab, "mark=%#x", skb->mark);

	audit_log_nf_skb(ab, skb, xt_family(par));

	audit_log_end(ab);

errout:
	return XT_CONTINUE;
}

static unsigned int
audit_tg_ebt(struct sk_buff *skb, const struct xt_action_param *par)
{
	audit_tg(skb, par);
	return EBT_CONTINUE;
}

static int audit_tg_check(const struct xt_tgchk_param *par)
{
	const struct xt_audit_info *info = par->targinfo;

	if (info->type > XT_AUDIT_TYPE_MAX) {
		pr_info_ratelimited("Audit type out of range (valid range: 0..%u)\n",
				    XT_AUDIT_TYPE_MAX);
		return -ERANGE;
	}

	return 0;
}

static struct xt_target audit_tg_reg[] __read_mostly = {
	{
		.name		= "AUDIT",
		.family		= NFPROTO_UNSPEC,
		.target		= audit_tg,
		.targetsize	= sizeof(struct xt_audit_info),
		.checkentry	= audit_tg_check,
		.me		= THIS_MODULE,
	},
	{
		.name		= "AUDIT",
		.family		= NFPROTO_BRIDGE,
		.target		= audit_tg_ebt,
		.targetsize	= sizeof(struct xt_audit_info),
		.checkentry	= audit_tg_check,
		.me		= THIS_MODULE,
	},
};

static int __init audit_tg_init(void)
{
	return xt_register_targets(audit_tg_reg, ARRAY_SIZE(audit_tg_reg));
}

static void __exit audit_tg_exit(void)
{
	xt_unregister_targets(audit_tg_reg, ARRAY_SIZE(audit_tg_reg));
}

module_init(audit_tg_init);
module_exit(audit_tg_exit);
