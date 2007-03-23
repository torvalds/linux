/* IP tables module for matching the value of the IPv4 and TCP ECN bits
 *
 * (C) 2002 by Harald Welte <laforge@gnumonks.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/in.h>
#include <linux/ip.h>
#include <net/ip.h>
#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/tcp.h>

#include <linux/netfilter/x_tables.h>
#include <linux/netfilter_ipv4/ip_tables.h>
#include <linux/netfilter_ipv4/ipt_ecn.h>

MODULE_AUTHOR("Harald Welte <laforge@netfilter.org>");
MODULE_DESCRIPTION("iptables ECN matching module");
MODULE_LICENSE("GPL");

static inline int match_ip(const struct sk_buff *skb,
			   const struct ipt_ecn_info *einfo)
{
	return (ip_hdr(skb)->tos & IPT_ECN_IP_MASK) == einfo->ip_ect;
}

static inline int match_tcp(const struct sk_buff *skb,
			    const struct ipt_ecn_info *einfo,
			    int *hotdrop)
{
	struct tcphdr _tcph, *th;

	/* In practice, TCP match does this, so can't fail.  But let's
	 * be good citizens.
	 */
	th = skb_header_pointer(skb, ip_hdrlen(skb), sizeof(_tcph), &_tcph);
	if (th == NULL) {
		*hotdrop = 0;
		return 0;
	}

	if (einfo->operation & IPT_ECN_OP_MATCH_ECE) {
		if (einfo->invert & IPT_ECN_OP_MATCH_ECE) {
			if (th->ece == 1)
				return 0;
		} else {
			if (th->ece == 0)
				return 0;
		}
	}

	if (einfo->operation & IPT_ECN_OP_MATCH_CWR) {
		if (einfo->invert & IPT_ECN_OP_MATCH_CWR) {
			if (th->cwr == 1)
				return 0;
		} else {
			if (th->cwr == 0)
				return 0;
		}
	}

	return 1;
}

static int match(const struct sk_buff *skb,
		 const struct net_device *in, const struct net_device *out,
		 const struct xt_match *match, const void *matchinfo,
		 int offset, unsigned int protoff, int *hotdrop)
{
	const struct ipt_ecn_info *info = matchinfo;

	if (info->operation & IPT_ECN_OP_MATCH_IP)
		if (!match_ip(skb, info))
			return 0;

	if (info->operation & (IPT_ECN_OP_MATCH_ECE|IPT_ECN_OP_MATCH_CWR)) {
		if (ip_hdr(skb)->protocol != IPPROTO_TCP)
			return 0;
		if (!match_tcp(skb, info, hotdrop))
			return 0;
	}

	return 1;
}

static int checkentry(const char *tablename, const void *ip_void,
		      const struct xt_match *match,
		      void *matchinfo, unsigned int hook_mask)
{
	const struct ipt_ecn_info *info = matchinfo;
	const struct ipt_ip *ip = ip_void;

	if (info->operation & IPT_ECN_OP_MATCH_MASK)
		return 0;

	if (info->invert & IPT_ECN_OP_MATCH_MASK)
		return 0;

	if (info->operation & (IPT_ECN_OP_MATCH_ECE|IPT_ECN_OP_MATCH_CWR)
	    && ip->proto != IPPROTO_TCP) {
		printk(KERN_WARNING "ipt_ecn: can't match TCP bits in rule for"
		       " non-tcp packets\n");
		return 0;
	}

	return 1;
}

static struct xt_match ecn_match = {
	.name		= "ecn",
	.family		= AF_INET,
	.match		= match,
	.matchsize	= sizeof(struct ipt_ecn_info),
	.checkentry	= checkentry,
	.me		= THIS_MODULE,
};

static int __init ipt_ecn_init(void)
{
	return xt_register_match(&ecn_match);
}

static void __exit ipt_ecn_fini(void)
{
	xt_unregister_match(&ecn_match);
}

module_init(ipt_ecn_init);
module_exit(ipt_ecn_fini);
