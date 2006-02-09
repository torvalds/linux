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

#include <linux/netfilter_ipv6.h>
#include <linux/netfilter_ipv6/ip6_tables.h>
#include <linux/netfilter_ipv6/ip6t_policy.h>

MODULE_AUTHOR("Patrick McHardy <kaber@trash.net>");
MODULE_DESCRIPTION("IPtables IPsec policy matching module");
MODULE_LICENSE("GPL");


static inline int
match_xfrm_state(struct xfrm_state *x, const struct ip6t_policy_elem *e)
{
#define MATCH_ADDR(x,y,z)	(!e->match.x ||				       \
				 ((!ip6_masked_addrcmp(&e->x.a6, &e->y.a6, z)) \
				  ^ e->invert.x))
#define MATCH(x,y)		(!e->match.x || ((e->x == (y)) ^ e->invert.x))
	
	return MATCH_ADDR(saddr, smask, (struct in6_addr *)&x->props.saddr.a6) &&
	       MATCH_ADDR(daddr, dmask, (struct in6_addr *)&x->id.daddr.a6) &&
	       MATCH(proto, x->id.proto) &&
	       MATCH(mode, x->props.mode) &&
	       MATCH(spi, x->id.spi) &&
	       MATCH(reqid, x->props.reqid);
}

static int
match_policy_in(const struct sk_buff *skb, const struct ip6t_policy_info *info)
{
	const struct ip6t_policy_elem *e;
	struct sec_path *sp = skb->sp;
	int strict = info->flags & IP6T_POLICY_MATCH_STRICT;
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

		if (match_xfrm_state(sp->x[i].xvec, e)) {
			if (!strict)
				return 1;
		} else if (strict)
			return 0;
	}

	return strict ? 1 : 0;
}

static int
match_policy_out(const struct sk_buff *skb, const struct ip6t_policy_info *info)
{
	const struct ip6t_policy_elem *e;
	struct dst_entry *dst = skb->dst;
	int strict = info->flags & IP6T_POLICY_MATCH_STRICT;
	int i, pos;

	if (dst->xfrm == NULL)
		return -1;

	for (i = 0; dst && dst->xfrm; dst = dst->child, i++) {
		pos = strict ? i : 0;
		if (pos >= info->len)
			return 0;
		e = &info->pol[pos];

		if (match_xfrm_state(dst->xfrm, e)) {
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
                 const void *matchinfo,
		 int offset,
		 unsigned int protoff,
		 int *hotdrop)
{
	const struct ip6t_policy_info *info = matchinfo;
	int ret;

	if (info->flags & IP6T_POLICY_MATCH_IN)
		ret = match_policy_in(skb, info);
	else
		ret = match_policy_out(skb, info);

	if (ret < 0)
		ret = info->flags & IP6T_POLICY_MATCH_NONE ? 1 : 0;
	else if (info->flags & IP6T_POLICY_MATCH_NONE)
		ret = 0;

	return ret;
}

static int checkentry(const char *tablename, const void *ip_void,
                      void *matchinfo, unsigned int matchsize,
                      unsigned int hook_mask)
{
	struct ip6t_policy_info *info = matchinfo;

	if (matchsize != IP6T_ALIGN(sizeof(*info))) {
		printk(KERN_ERR "ip6t_policy: matchsize %u != %zu\n",
		       matchsize, IP6T_ALIGN(sizeof(*info)));
		return 0;
	}
	if (!(info->flags & (IP6T_POLICY_MATCH_IN|IP6T_POLICY_MATCH_OUT))) {
		printk(KERN_ERR "ip6t_policy: neither incoming nor "
		                "outgoing policy selected\n");
		return 0;
	}
	if (hook_mask & (1 << NF_IP6_PRE_ROUTING | 1 << NF_IP6_LOCAL_IN)
	    && info->flags & IP6T_POLICY_MATCH_OUT) {
		printk(KERN_ERR "ip6t_policy: output policy not valid in "
		                "PRE_ROUTING and INPUT\n");
		return 0;
	}
	if (hook_mask & (1 << NF_IP6_POST_ROUTING | 1 << NF_IP6_LOCAL_OUT)
	    && info->flags & IP6T_POLICY_MATCH_IN) {
		printk(KERN_ERR "ip6t_policy: input policy not valid in "
		                "POST_ROUTING and OUTPUT\n");
		return 0;
	}
	if (info->len > IP6T_POLICY_MAX_ELEM) {
		printk(KERN_ERR "ip6t_policy: too many policy elements\n");
		return 0;
	}

	return 1;
}

static struct ip6t_match policy_match = {
	.name		= "policy",
	.match		= match,
	.checkentry 	= checkentry,
	.me		= THIS_MODULE,
};

static int __init init(void)
{
	return ip6t_register_match(&policy_match);
}

static void __exit fini(void)
{
	ip6t_unregister_match(&policy_match);
}

module_init(init);
module_exit(fini);
