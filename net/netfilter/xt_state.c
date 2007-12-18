/* Kernel module to match connection tracking information. */

/* (C) 1999-2001 Paul `Rusty' Russell
 * (C) 2002-2005 Netfilter Core Team <coreteam@netfilter.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/skbuff.h>
#include <net/netfilter/nf_conntrack.h>
#include <linux/netfilter/x_tables.h>
#include <linux/netfilter/xt_state.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Rusty Russell <rusty@rustcorp.com.au>");
MODULE_DESCRIPTION("ip[6]_tables connection tracking state match module");
MODULE_ALIAS("ipt_state");
MODULE_ALIAS("ip6t_state");

static bool
state_mt(const struct sk_buff *skb, const struct net_device *in,
         const struct net_device *out, const struct xt_match *match,
         const void *matchinfo, int offset, unsigned int protoff,
         bool *hotdrop)
{
	const struct xt_state_info *sinfo = matchinfo;
	enum ip_conntrack_info ctinfo;
	unsigned int statebit;

	if (nf_ct_is_untracked(skb))
		statebit = XT_STATE_UNTRACKED;
	else if (!nf_ct_get(skb, &ctinfo))
		statebit = XT_STATE_INVALID;
	else
		statebit = XT_STATE_BIT(ctinfo);

	return (sinfo->statemask & statebit);
}

static bool
state_mt_check(const char *tablename, const void *inf,
               const struct xt_match *match, void *matchinfo,
               unsigned int hook_mask)
{
	if (nf_ct_l3proto_try_module_get(match->family) < 0) {
		printk(KERN_WARNING "can't load conntrack support for "
				    "proto=%u\n", match->family);
		return false;
	}
	return true;
}

static void state_mt_destroy(const struct xt_match *match, void *matchinfo)
{
	nf_ct_l3proto_module_put(match->family);
}

static struct xt_match state_mt_reg[] __read_mostly = {
	{
		.name		= "state",
		.family		= AF_INET,
		.checkentry	= state_mt_check,
		.match		= state_mt,
		.destroy	= state_mt_destroy,
		.matchsize	= sizeof(struct xt_state_info),
		.me		= THIS_MODULE,
	},
	{
		.name		= "state",
		.family		= AF_INET6,
		.checkentry	= state_mt_check,
		.match		= state_mt,
		.destroy	= state_mt_destroy,
		.matchsize	= sizeof(struct xt_state_info),
		.me		= THIS_MODULE,
	},
};

static int __init state_mt_init(void)
{
	return xt_register_matches(state_mt_reg, ARRAY_SIZE(state_mt_reg));
}

static void __exit state_mt_exit(void)
{
	xt_unregister_matches(state_mt_reg, ARRAY_SIZE(state_mt_reg));
}

module_init(state_mt_init);
module_exit(state_mt_exit);
