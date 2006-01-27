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

#include <linux/netfilter_ipv4.h>
#include <linux/netfilter_ipv4/ip_tables.h>
#include <linux/netfilter_ipv4/ipt_policy.h>

MODULE_AUTHOR("Patrick McHardy <kaber@trash.net>");
MODULE_DESCRIPTION("IPtables IPsec policy matching module");
MODULE_LICENSE("GPL");


static inline int
match_xfrm_state(struct xfrm_state *x, const struct ipt_policy_elem *e)
{
#define MATCH(x,y)	(!e->match.x || ((e->x == (y)) ^ e->invert.x))

	return MATCH(saddr, x->props.saddr.a4 & e->smask) &&
	       MATCH(daddr, x->id.daddr.a4 & e->dmask) &&
	       MATCH(proto, x->id.proto) &&
	       MATCH(mode, x->props.mode) &&
	       MATCH(spi, x->id.spi) &&
	       MATCH(reqid, x->props.reqid);
}

static int
match_policy_in(const struct sk_buff *skb, const struct ipt_policy_info *info)
{
	const struct ipt_policy_elem *e;
	struct sec_path *sp = skb->sp;
	int strict = info->flags & IPT_POLICY_MATCH_STRICT;
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
match_policy_out(const struct sk_buff *skb, const struct ipt_policy_info *info)
{
	const struct ipt_policy_elem *e;
	struct dst_entry *dst = skb->dst;
	int strict = info->flags & IPT_POLICY_MATCH_STRICT;
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

	return strict ? 1 : 0;
}

static int match(const struct sk_buff *skb,
                 const struct net_device *in,
                 const struct net_device *out,
                 const void *matchinfo,
                 int offset,
                 unsigned int protoff,
                 int *hotdrop)
{
	const struct ipt_policy_info *info = matchinfo;
	int ret;

	if (info->flags & IPT_POLICY_MATCH_IN)
		ret = match_policy_in(skb, info);
	else
		ret = match_policy_out(skb, info);

	if (ret < 0)
		ret = info->flags & IPT_POLICY_MATCH_NONE ? 1 : 0;
	else if (info->flags & IPT_POLICY_MATCH_NONE)
		ret = 0;

	return ret;
}

static int checkentry(const char *tablename, const void *ip_void,
                      void *matchinfo, unsigned int matchsize,
                      unsigned int hook_mask)
{
	struct ipt_policy_info *info = matchinfo;

	if (matchsize != IPT_ALIGN(sizeof(*info))) {
		printk(KERN_ERR "ipt_policy: matchsize %u != %zu\n",
		       matchsize, IPT_ALIGN(sizeof(*info)));
		return 0;
	}
	if (!(info->flags & (IPT_POLICY_MATCH_IN|IPT_POLICY_MATCH_OUT))) {
		printk(KERN_ERR "ipt_policy: neither incoming nor "
		                "outgoing policy selected\n");
		return 0;
	}
	if (hook_mask & (1 << NF_IP_PRE_ROUTING | 1 << NF_IP_LOCAL_IN)
	    && info->flags & IPT_POLICY_MATCH_OUT) {
		printk(KERN_ERR "ipt_policy: output policy not valid in "
		                "PRE_ROUTING and INPUT\n");
		return 0;
	}
	if (hook_mask & (1 << NF_IP_POST_ROUTING | 1 << NF_IP_LOCAL_OUT)
	    && info->flags & IPT_POLICY_MATCH_IN) {
		printk(KERN_ERR "ipt_policy: input policy not valid in "
		                "POST_ROUTING and OUTPUT\n");
		return 0;
	}
	if (info->len > IPT_POLICY_MAX_ELEM) {
		printk(KERN_ERR "ipt_policy: too many policy elements\n");
		return 0;
	}

	return 1;
}

static struct ipt_match policy_match = {
	.name		= "policy",
	.match		= match,
	.checkentry 	= checkentry,
	.me		= THIS_MODULE,
};

static int __init init(void)
{
	return ipt_register_match(&policy_match);
}

static void __exit fini(void)
{
	ipt_unregister_match(&policy_match);
}

module_init(init);
module_exit(fini);
