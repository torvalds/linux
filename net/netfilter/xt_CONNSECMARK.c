/*
 * This module is used to copy security markings from packets
 * to connections, and restore security markings from connections
 * back to packets.  This would normally be performed in conjunction
 * with the SECMARK target and state match.
 *
 * Based somewhat on CONNMARK:
 *   Copyright (C) 2002,2004 MARA Systems AB <http://www.marasystems.com>
 *    by Henrik Nordstrom <hno@marasystems.com>
 *
 * (C) 2006,2008 Red Hat, Inc., James Morris <jmorris@redhat.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */
#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/netfilter/x_tables.h>
#include <linux/netfilter/xt_CONNSECMARK.h>
#include <net/netfilter/nf_conntrack.h>
#include <net/netfilter/nf_conntrack_ecache.h>

#define PFX "CONNSECMARK: "

MODULE_LICENSE("GPL");
MODULE_AUTHOR("James Morris <jmorris@redhat.com>");
MODULE_DESCRIPTION("Xtables: target for copying between connection and security mark");
MODULE_ALIAS("ipt_CONNSECMARK");
MODULE_ALIAS("ip6t_CONNSECMARK");

/*
 * If the packet has a security mark and the connection does not, copy
 * the security mark from the packet to the connection.
 */
static void secmark_save(const struct sk_buff *skb)
{
	if (skb->secmark) {
		struct nf_conn *ct;
		enum ip_conntrack_info ctinfo;

		ct = nf_ct_get(skb, &ctinfo);
		if (ct && !ct->secmark) {
			ct->secmark = skb->secmark;
			nf_conntrack_event_cache(IPCT_SECMARK, ct);
		}
	}
}

/*
 * If packet has no security mark, and the connection does, restore the
 * security mark from the connection to the packet.
 */
static void secmark_restore(struct sk_buff *skb)
{
	if (!skb->secmark) {
		const struct nf_conn *ct;
		enum ip_conntrack_info ctinfo;

		ct = nf_ct_get(skb, &ctinfo);
		if (ct && ct->secmark)
			skb->secmark = ct->secmark;
	}
}

static unsigned int
connsecmark_tg(struct sk_buff *skb, const struct xt_target_param *par)
{
	const struct xt_connsecmark_target_info *info = par->targinfo;

	switch (info->mode) {
	case CONNSECMARK_SAVE:
		secmark_save(skb);
		break;

	case CONNSECMARK_RESTORE:
		secmark_restore(skb);
		break;

	default:
		BUG();
	}

	return XT_CONTINUE;
}

static bool connsecmark_tg_check(const struct xt_tgchk_param *par)
{
	const struct xt_connsecmark_target_info *info = par->targinfo;

	if (strcmp(par->table, "mangle") != 0 &&
	    strcmp(par->table, "security") != 0) {
		printk(KERN_INFO PFX "target only valid in the \'mangle\' "
		       "or \'security\' tables, not \'%s\'.\n", par->table);
		return false;
	}

	switch (info->mode) {
	case CONNSECMARK_SAVE:
	case CONNSECMARK_RESTORE:
		break;

	default:
		printk(KERN_INFO PFX "invalid mode: %hu\n", info->mode);
		return false;
	}

	if (nf_ct_l3proto_try_module_get(par->family) < 0) {
		printk(KERN_WARNING "can't load conntrack support for "
				    "proto=%u\n", par->family);
		return false;
	}
	return true;
}

static void connsecmark_tg_destroy(const struct xt_tgdtor_param *par)
{
	nf_ct_l3proto_module_put(par->family);
}

static struct xt_target connsecmark_tg_reg __read_mostly = {
	.name       = "CONNSECMARK",
	.revision   = 0,
	.family     = NFPROTO_UNSPEC,
	.checkentry = connsecmark_tg_check,
	.destroy    = connsecmark_tg_destroy,
	.target     = connsecmark_tg,
	.targetsize = sizeof(struct xt_connsecmark_target_info),
	.me         = THIS_MODULE,
};

static int __init connsecmark_tg_init(void)
{
	return xt_register_target(&connsecmark_tg_reg);
}

static void __exit connsecmark_tg_exit(void)
{
	xt_unregister_target(&connsecmark_tg_reg);
}

module_init(connsecmark_tg_init);
module_exit(connsecmark_tg_exit);
