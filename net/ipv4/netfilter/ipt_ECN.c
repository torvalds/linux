/* iptables module for the IPv4 and TCP ECN bits, Version 1.5
 *
 * (C) 2002 by Harald Welte <laforge@netfilter.org>
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as 
 * published by the Free Software Foundation.
 *
 * ipt_ECN.c,v 1.5 2002/08/18 19:36:51 laforge Exp
*/

#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/ip.h>
#include <linux/tcp.h>
#include <net/checksum.h>

#include <linux/netfilter_ipv4/ip_tables.h>
#include <linux/netfilter_ipv4/ipt_ECN.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Harald Welte <laforge@netfilter.org>");
MODULE_DESCRIPTION("iptables ECN modification module");

/* set ECT codepoint from IP header.
 * 	return 0 if there was an error. */
static inline int
set_ect_ip(struct sk_buff **pskb, const struct ipt_ECN_info *einfo)
{
	if (((*pskb)->nh.iph->tos & IPT_ECN_IP_MASK)
	    != (einfo->ip_ect & IPT_ECN_IP_MASK)) {
		u_int16_t diffs[2];

		if (!skb_make_writable(pskb, sizeof(struct iphdr)))
			return 0;

		diffs[0] = htons((*pskb)->nh.iph->tos) ^ 0xFFFF;
		(*pskb)->nh.iph->tos &= ~IPT_ECN_IP_MASK;
		(*pskb)->nh.iph->tos |= (einfo->ip_ect & IPT_ECN_IP_MASK);
		diffs[1] = htons((*pskb)->nh.iph->tos);
		(*pskb)->nh.iph->check
			= csum_fold(csum_partial((char *)diffs,
						 sizeof(diffs),
						 (*pskb)->nh.iph->check
						 ^0xFFFF));
	} 
	return 1;
}

/* Return 0 if there was an error. */
static inline int
set_ect_tcp(struct sk_buff **pskb, const struct ipt_ECN_info *einfo, int inward)
{
	struct tcphdr _tcph, *tcph;
	u_int16_t diffs[2];

	/* Not enought header? */
	tcph = skb_header_pointer(*pskb, (*pskb)->nh.iph->ihl*4,
				  sizeof(_tcph), &_tcph);
	if (!tcph)
		return 0;

	if ((!(einfo->operation & IPT_ECN_OP_SET_ECE) ||
	     tcph->ece == einfo->proto.tcp.ece) &&
	    ((!(einfo->operation & IPT_ECN_OP_SET_CWR) ||
	     tcph->cwr == einfo->proto.tcp.cwr)))
		return 1;

	if (!skb_make_writable(pskb, (*pskb)->nh.iph->ihl*4+sizeof(*tcph)))
		return 0;
	tcph = (void *)(*pskb)->nh.iph + (*pskb)->nh.iph->ihl*4;

	if ((*pskb)->ip_summed == CHECKSUM_HW &&
	    skb_checksum_help(*pskb, inward))
		return 0;

	diffs[0] = ((u_int16_t *)tcph)[6];
	if (einfo->operation & IPT_ECN_OP_SET_ECE)
		tcph->ece = einfo->proto.tcp.ece;
	if (einfo->operation & IPT_ECN_OP_SET_CWR)
		tcph->cwr = einfo->proto.tcp.cwr;
	diffs[1] = ((u_int16_t *)tcph)[6];
	diffs[0] = diffs[0] ^ 0xFFFF;

	if ((*pskb)->ip_summed != CHECKSUM_UNNECESSARY)
		tcph->check = csum_fold(csum_partial((char *)diffs,
						     sizeof(diffs),
						     tcph->check^0xFFFF));
	return 1;
}

static unsigned int
target(struct sk_buff **pskb,
       const struct net_device *in,
       const struct net_device *out,
       unsigned int hooknum,
       const struct xt_target *target,
       const void *targinfo,
       void *userinfo)
{
	const struct ipt_ECN_info *einfo = targinfo;

	if (einfo->operation & IPT_ECN_OP_SET_IP)
		if (!set_ect_ip(pskb, einfo))
			return NF_DROP;

	if (einfo->operation & (IPT_ECN_OP_SET_ECE | IPT_ECN_OP_SET_CWR)
	    && (*pskb)->nh.iph->protocol == IPPROTO_TCP)
		if (!set_ect_tcp(pskb, einfo, (out == NULL)))
			return NF_DROP;

	return IPT_CONTINUE;
}

static int
checkentry(const char *tablename,
	   const void *e_void,
	   const struct xt_target *target,
           void *targinfo,
           unsigned int targinfosize,
           unsigned int hook_mask)
{
	const struct ipt_ECN_info *einfo = (struct ipt_ECN_info *)targinfo;
	const struct ipt_entry *e = e_void;

	if (einfo->operation & IPT_ECN_OP_MASK) {
		printk(KERN_WARNING "ECN: unsupported ECN operation %x\n",
			einfo->operation);
		return 0;
	}
	if (einfo->ip_ect & ~IPT_ECN_IP_MASK) {
		printk(KERN_WARNING "ECN: new ECT codepoint %x out of mask\n",
			einfo->ip_ect);
		return 0;
	}
	if ((einfo->operation & (IPT_ECN_OP_SET_ECE|IPT_ECN_OP_SET_CWR))
	    && (e->ip.proto != IPPROTO_TCP || (e->ip.invflags & IPT_INV_PROTO))) {
		printk(KERN_WARNING "ECN: cannot use TCP operations on a "
		       "non-tcp rule\n");
		return 0;
	}
	return 1;
}

static struct ipt_target ipt_ecn_reg = {
	.name		= "ECN",
	.target		= target,
	.targetsize	= sizeof(struct ipt_ECN_info),
	.table		= "mangle",
	.checkentry	= checkentry,
	.me		= THIS_MODULE,
};

static int __init ipt_ecn_init(void)
{
	return ipt_register_target(&ipt_ecn_reg);
}

static void __exit ipt_ecn_fini(void)
{
	ipt_unregister_target(&ipt_ecn_reg);
}

module_init(ipt_ecn_init);
module_exit(ipt_ecn_fini);
