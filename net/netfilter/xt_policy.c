/* IP tables module for matching IPsec policy
 *
 * Copyright (c) 2004,2005 Patrick McHardy, <kaber@trash.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/init.h>
#include <net/xfrm.h>

#include <linux/netfilter.h>
#include <linux/netfilter/xt_policy.h>
#include <linux/netfilter/x_tables.h>

MODULE_AUTHOR("Patrick McHardy <kaber@trash.net>");
MODULE_DESCRIPTION("Xtables: IPsec policy match");
MODULE_LICENSE("GPL");

static inline bool
xt_addr_cmp(const union nf_inet_addr *a1, const union nf_inet_addr *m,
	    const union nf_inet_addr *a2, unsigned short family)
{
	switch (family) {
	case NFPROTO_IPV4:
		return ((a1->ip ^ a2->ip) & m->ip) == 0;
	case NFPROTO_IPV6:
		return ipv6_masked_addr_cmp(&a1->in6, &m->in6, &a2->in6) == 0;
	}
	return false;
}

static bool
match_xfrm_state(const struct xfrm_state *x, const struct xt_policy_elem *e,
		 unsigned short family)
{
#define MATCH_ADDR(x,y,z)	(!e->match.x ||			       \
				 (xt_addr_cmp(&e->x, &e->y, (const union nf_inet_addr *)(z), family) \
				  ^ e->invert.x))
#define MATCH(x,y)		(!e->match.x || ((e->x == (y)) ^ e->invert.x))

	return MATCH_ADDR(saddr, smask, &x->props.saddr) &&
	       MATCH_ADDR(daddr, dmask, &x->id.daddr) &&
	       MATCH(proto, x->id.proto) &&
	       MATCH(mode, x->props.mode) &&
	       MATCH(spi, x->id.spi) &&
	       MATCH(reqid, x->props.reqid);
}

static int
match_policy_in(const struct sk_buff *skb, const struct xt_policy_info *info,
		unsigned short family)
{
	const struct xt_policy_elem *e;
	const struct sec_path *sp = skb->sp;
	int strict = info->flags & XT_POLICY_MATCH_STRICT;
	int i, pos;

	if (sp == NULL)
		return -1;
	if (strict && info->len != sp->len)
		return 0;

	for (i = sp->len - 1; i >= 0; i--) {
		pos = strict ? i - sp->len + 1 : 0;
		if (pos >= info->len)
			return 0;
		e = &info->pol[pos];

		if (match_xfrm_state(sp->xvec[i], e, family)) {
			if (!strict)
				return 1;
		} else if (strict)
			return 0;
	}

	return strict ? 1 : 0;
}

static int
match_policy_out(const struct sk_buff *skb, const struct xt_policy_info *info,
		 unsigned short family)
{
	const struct xt_policy_elem *e;
	const struct dst_entry *dst = skb->dst;
	int strict = info->flags & XT_POLICY_MATCH_STRICT;
	int i, pos;

	if (dst->xfrm == NULL)
		return -1;

	for (i = 0; dst && dst->xfrm; dst = dst->child, i++) {
		pos = strict ? i : 0;
		if (pos >= info->len)
			return 0;
		e = &info->pol[pos];

		if (match_xfrm_state(dst->xfrm, e, family)) {
			if (!strict)
				return 1;
		} else if (strict)
			return 0;
	}

	return strict ? i == info->len : 0;
}

static bool
policy_mt(const struct sk_buff *skb, const struct xt_match_param *par)
{
	const struct xt_policy_info *info = par->matchinfo;
	int ret;

	if (info->flags & XT_POLICY_MATCH_IN)
		ret = match_policy_in(skb, info, par->match->family);
	else
		ret = match_policy_out(skb, info, par->match->family);

	if (ret < 0)
		ret = info->flags & XT_POLICY_MATCH_NONE ? true : false;
	else if (info->flags & XT_POLICY_MATCH_NONE)
		ret = false;

	return ret;
}

static bool policy_mt_check(const struct xt_mtchk_param *par)
{
	const struct xt_policy_info *info = par->matchinfo;

	if (!(info->flags & (XT_POLICY_MATCH_IN|XT_POLICY_MATCH_OUT))) {
		printk(KERN_ERR "xt_policy: neither incoming nor "
				"outgoing policy selected\n");
		return false;
	}
	if (par->hook_mask & ((1 << NF_INET_PRE_ROUTING) |
	    (1 << NF_INET_LOCAL_IN)) && info->flags & XT_POLICY_MATCH_OUT) {
		printk(KERN_ERR "xt_policy: output policy not valid in "
				"PRE_ROUTING and INPUT\n");
		return false;
	}
	if (par->hook_mask & ((1 << NF_INET_POST_ROUTING) |
	    (1 << NF_INET_LOCAL_OUT)) && info->flags & XT_POLICY_MATCH_IN) {
		printk(KERN_ERR "xt_policy: input policy not valid in "
				"POST_ROUTING and OUTPUT\n");
		return false;
	}
	if (info->len > XT_POLICY_MAX_ELEM) {
		printk(KERN_ERR "xt_policy: too many policy elements\n");
		return false;
	}
	return true;
}

static struct xt_match policy_mt_reg[] __read_mostly = {
	{
		.name		= "policy",
		.family		= NFPROTO_IPV4,
		.checkentry 	= policy_mt_check,
		.match		= policy_mt,
		.matchsize	= sizeof(struct xt_policy_info),
		.me		= THIS_MODULE,
	},
	{
		.name		= "policy",
		.family		= NFPROTO_IPV6,
		.checkentry	= policy_mt_check,
		.match		= policy_mt,
		.matchsize	= sizeof(struct xt_policy_info),
		.me		= THIS_MODULE,
	},
};

static int __init policy_mt_init(void)
{
	return xt_register_matches(policy_mt_reg, ARRAY_SIZE(policy_mt_reg));
}

static void __exit policy_mt_exit(void)
{
	xt_unregister_matches(policy_mt_reg, ARRAY_SIZE(policy_mt_reg));
}

module_init(policy_mt_init);
module_exit(policy_mt_exit);
MODULE_ALIAS("ipt_policy");
MODULE_ALIAS("ip6t_policy");
