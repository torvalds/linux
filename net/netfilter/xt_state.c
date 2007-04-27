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

static int
match(const struct sk_buff *skb,
      const struct net_device *in,
      const struct net_device *out,
      const struct xt_match *match,
      const void *matchinfo,
      int offset,
      unsigned int protoff,
      int *hotdrop)
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

static int check(const char *tablename,
		 const void *inf,
		 const struct xt_match *match,
		 void *matchinfo,
		 unsigned int hook_mask)
{
	if (nf_ct_l3proto_try_module_get(match->family) < 0) {
		printk(KERN_WARNING "can't load conntrack support for "
				    "proto=%d\n", match->family);
		return 0;
	}
	return 1;
}

static void
destroy(const struct xt_match *match, void *matchinfo)
{
	nf_ct_l3proto_module_put(match->family);
}

static struct xt_match xt_state_match[] = {
	{
		.name		= "state",
		.family		= AF_INET,
		.checkentry	= check,
		.match		= match,
		.destroy	= destroy,
		.matchsize	= sizeof(struct xt_state_info),
		.me		= THIS_MODULE,
	},
	{
		.name		= "state",
		.family		= AF_INET6,
		.checkentry	= check,
		.match		= match,
		.destroy	= destroy,
		.matchsize	= sizeof(struct xt_state_info),
		.me		= THIS_MODULE,
	},
};

static int __init xt_state_init(void)
{
	return xt_register_matches(xt_state_match, ARRAY_SIZE(xt_state_match));
}

static void __exit xt_state_fini(void)
{
	xt_unregister_matches(xt_state_match, ARRAY_SIZE(xt_state_match));
}

module_init(xt_state_init);
module_exit(xt_state_fini);
