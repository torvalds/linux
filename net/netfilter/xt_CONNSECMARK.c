// SPDX-License-Identifier: GPL-2.0-only
/*
 * This module is used to copy security markings from packets
 * to connections, and restore security markings from connections
 * back to packets.  This would normally be performed in conjunction
 * with the SECMARK target and state match.
 *
 * Based somewhat on CONNMARK:
 *   Copyright (C) 2002,2004 MARA Systems AB <https://www.marasystems.com>
 *    by Henrik Nordstrom <hno@marasystems.com>
 *
 * (C) 2006,2008 Red Hat, Inc., James Morris <jmorris@redhat.com>
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt
#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/netfilter/x_tables.h>
#include <linux/netfilter/xt_CONNSECMARK.h>
#include <net/netfilter/nf_conntrack.h>
#include <net/netfilter/nf_conntrack_ecache.h>

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
connsecmark_tg(struct sk_buff *skb, const struct xt_action_param *par)
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

static int connsecmark_tg_check(const struct xt_tgchk_param *par)
{
	const struct xt_connsecmark_target_info *info = par->targinfo;
	int ret;

	if (strcmp(par->table, "mangle") != 0 &&
	    strcmp(par->table, "security") != 0) {
		pr_info_ratelimited("only valid in \'mangle\' or \'security\' table, not \'%s\'\n",
				    par->table);
		return -EINVAL;
	}

	switch (info->mode) {
	case CONNSECMARK_SAVE:
	case CONNSECMARK_RESTORE:
		break;

	default:
		pr_info_ratelimited("invalid mode: %hu\n", info->mode);
		return -EINVAL;
	}

	ret = nf_ct_netns_get(par->net, par->family);
	if (ret < 0)
		pr_info_ratelimited("cannot load conntrack support for proto=%u\n",
				    par->family);
	return ret;
}

static void connsecmark_tg_destroy(const struct xt_tgdtor_param *par)
{
	nf_ct_netns_put(par->net, par->family);
}

static struct xt_target connsecmark_tg_reg[] __read_mostly = {
	{
		.name       = "CONNSECMARK",
		.revision   = 0,
		.family     = NFPROTO_IPV4,
		.checkentry = connsecmark_tg_check,
		.destroy    = connsecmark_tg_destroy,
		.target     = connsecmark_tg,
		.targetsize = sizeof(struct xt_connsecmark_target_info),
		.me         = THIS_MODULE,
	},
#if IS_ENABLED(CONFIG_IP6_NF_IPTABLES)
	{
		.name       = "CONNSECMARK",
		.revision   = 0,
		.family     = NFPROTO_IPV6,
		.checkentry = connsecmark_tg_check,
		.destroy    = connsecmark_tg_destroy,
		.target     = connsecmark_tg,
		.targetsize = sizeof(struct xt_connsecmark_target_info),
		.me         = THIS_MODULE,
	},
#endif
};

static int __init connsecmark_tg_init(void)
{
	return xt_register_targets(connsecmark_tg_reg, ARRAY_SIZE(connsecmark_tg_reg));
}

static void __exit connsecmark_tg_exit(void)
{
	xt_unregister_targets(connsecmark_tg_reg, ARRAY_SIZE(connsecmark_tg_reg));
}

module_init(connsecmark_tg_init);
module_exit(connsecmark_tg_exit);
