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
MODULE_DESCRIPTION("Xtables: Explicit Congestion Notification (ECN) flag match for IPv4");
MODULE_LICENSE("GPL");

static inline bool match_ip(const struct sk_buff *skb,
			    const struct ipt_ecn_info *einfo)
{
	return (ip_hdr(skb)->tos & IPT_ECN_IP_MASK) == einfo->ip_ect;
}

static inline bool match_tcp(const struct sk_buff *skb,
			     const struct ipt_ecn_info *einfo,
			     bool *hotdrop)
{
	struct tcphdr _tcph;
	const struct tcphdr *th;

	/* In practice, TCP match does this, so can't fail.  But let's
	 * be good citizens.
	 */
	th = skb_header_pointer(skb, ip_hdrlen(skb), sizeof(_tcph), &_tcph);
	if (th == NULL) {
		*hotdrop = false;
		return false;
	}

	if (einfo->operation & IPT_ECN_OP_MATCH_ECE) {
		if (einfo->invert & IPT_ECN_OP_MATCH_ECE) {
			if (th->ece == 1)
				return false;
		} else {
			if (th->ece == 0)
				return false;
		}
	}

	if (einfo->operation & IPT_ECN_OP_MATCH_CWR) {
		if (einfo->invert & IPT_ECN_OP_MATCH_CWR) {
			if (th->cwr == 1)
				return false;
		} else {
			if (th->cwr == 0)
				return false;
		}
	}

	return true;
}

static bool ecn_mt(const struct sk_buff *skb, const struct xt_match_param *par)
{
	const struct ipt_ecn_info *info = par->matchinfo;

	if (info->operation & IPT_ECN_OP_MATCH_IP)
		if (!match_ip(skb, info))
			return false;

	if (info->operation & (IPT_ECN_OP_MATCH_ECE|IPT_ECN_OP_MATCH_CWR)) {
		if (ip_hdr(skb)->protocol != IPPROTO_TCP)
			return false;
		if (!match_tcp(skb, info, par->hotdrop))
			return false;
	}

	return true;
}

static bool ecn_mt_check(const struct xt_mtchk_param *par)
{
	const struct ipt_ecn_info *info = par->matchinfo;
	const struct ipt_ip *ip = par->entryinfo;

	if (info->operation & IPT_ECN_OP_MATCH_MASK)
		return false;

	if (info->invert & IPT_ECN_OP_MATCH_MASK)
		return false;

	if (info->operation & (IPT_ECN_OP_MATCH_ECE|IPT_ECN_OP_MATCH_CWR) &&
	    ip->proto != IPPROTO_TCP) {
		printk(KERN_WARNING "ipt_ecn: can't match TCP bits in rule for"
		       " non-tcp packets\n");
		return false;
	}

	return true;
}

static struct xt_match ecn_mt_reg __read_mostly = {
	.name		= "ecn",
	.family		= NFPROTO_IPV4,
	.match		= ecn_mt,
	.matchsize	= sizeof(struct ipt_ecn_info),
	.checkentry	= ecn_mt_check,
	.me		= THIS_MODULE,
};

static int __init ecn_mt_init(void)
{
	return xt_register_match(&ecn_mt_reg);
}

static void __exit ecn_mt_exit(void)
{
	xt_unregister_match(&ecn_mt_reg);
}

module_init(ecn_mt_init);
module_exit(ecn_mt_exit);
