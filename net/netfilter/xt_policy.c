/* IP tables module for matching IPsec policy
 *
 * Copyright (c) 2004,2005 Patrick McHardy, <kaber@trash.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/config.h>
#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/init.h>
#include <net/xfrm.h>

#include <linux/netfilter/xt_policy.h>
#include <linux/netfilter/x_tables.h>

MODULE_AUTHOR("Patrick McHardy <kaber@trash.net>");
MODULE_DESCRIPTION("Xtables IPsec policy matching module");
MODULE_LICENSE("GPL");

static inline int
xt_addr_cmp(const union xt_policy_addr *a1, const union xt_policy_addr *m,
	    const union xt_policy_addr *a2, unsigned short family)
{
	switch (family) {
	case AF_INET:
		return !((a1->a4.s_addr ^ a2->a4.s_addr) & m->a4.s_addr);
	case AF_INET6:
		return !ipv6_masked_addr_cmp(&a1->a6, &m->a6, &a2->a6);
	}
	return 0;
}

static inline int
match_xfrm_state(struct xfrm_state *x, const struct xt_policy_elem *e,
		 unsigned short family)
{
#define MATCH_ADDR(x,y,z)	(!e->match.x ||			       \
				 (xt_addr_cmp(&e->x, &e->y, z, family) \
				  ^ e->invert.x))
#define MATCH(x,y)		(!e->match.x || ((e->x == (y)) ^ e->invert.x))

	return MATCH_ADDR(saddr, smask, (union xt_policy_addr *)&x->props.saddr) &&
	       MATCH_ADDR(daddr, dmask, (union xt_policy_addr *)&x->id.daddr) &&
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
	struct sec_path *sp = skb->sp;
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

		if (match_xfrm_state(sp->x[i].xvec, e, family)) {
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
	struct dst_entry *dst = skb->dst;
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

static int match(const struct sk_buff *skb,
                 const struct net_device *in,
                 const struct net_device *out,
                 const struct xt_match *match,
                 const void *matchinfo,
                 int offset,
                 unsigned int protoff,
                 int *hotdrop)
{
	const struct xt_policy_info *info = matchinfo;
	int ret;

	if (info->flags & XT_POLICY_MATCH_IN)
		ret = match_policy_in(skb, info, match->family);
	else
		ret = match_policy_out(skb, info, match->family);

	if (ret < 0)
		ret = info->flags & XT_POLICY_MATCH_NONE ? 1 : 0;
	else if (info->flags & XT_POLICY_MATCH_NONE)
		ret = 0;

	return ret;
}

static int checkentry(const char *tablename, const void *ip_void,
                      const struct xt_match *match,
                      void *matchinfo, unsigned int matchsize,
                      unsigned int hook_mask)
{
	struct xt_policy_info *info = matchinfo;

	if (!(info->flags & (XT_POLICY_MATCH_IN|XT_POLICY_MATCH_OUT))) {
		printk(KERN_ERR "xt_policy: neither incoming nor "
		                "outgoing policy selected\n");
		return 0;
	}
	/* hook values are equal for IPv4 and IPv6 */
	if (hook_mask & (1 << NF_IP_PRE_ROUTING | 1 << NF_IP_LOCAL_IN)
	    && info->flags & XT_POLICY_MATCH_OUT) {
		printk(KERN_ERR "xt_policy: output policy not valid in "
		                "PRE_ROUTING and INPUT\n");
		return 0;
	}
	if (hook_mask & (1 << NF_IP_POST_ROUTING | 1 << NF_IP_LOCAL_OUT)
	    && info->flags & XT_POLICY_MATCH_IN) {
		printk(KERN_ERR "xt_policy: input policy not valid in "
		                "POST_ROUTING and OUTPUT\n");
		return 0;
	}
	if (info->len > XT_POLICY_MAX_ELEM) {
		printk(KERN_ERR "xt_policy: too many policy elements\n");
		return 0;
	}
	return 1;
}

static struct xt_match policy_match = {
	.name		= "policy",
	.family		= AF_INET,
	.match		= match,
	.matchsize	= sizeof(struct xt_policy_info),
	.checkentry 	= checkentry,
	.family		= AF_INET,
	.me		= THIS_MODULE,
};

static struct xt_match policy6_match = {
	.name		= "policy",
	.family		= AF_INET6,
	.match		= match,
	.matchsize	= sizeof(struct xt_policy_info),
	.checkentry	= checkentry,
	.family		= AF_INET6,
	.me		= THIS_MODULE,
};

static int __init init(void)
{
	int ret;

	ret = xt_register_match(&policy_match);
	if (ret)
		return ret;
	ret = xt_register_match(&policy6_match);
	if (ret)
		xt_unregister_match(&policy_match);
	return ret;
}

static void __exit fini(void)
{
	xt_unregister_match(&policy6_match);
	xt_unregister_match(&policy_match);
}

module_init(init);
module_exit(fini);
MODULE_ALIAS("ipt_policy");
MODULE_ALIAS("ip6t_policy");
