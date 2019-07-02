// SPDX-License-Identifier: GPL-2.0-only
/* iptables module for the IPv4 and TCP ECN bits, Version 1.5
 *
 * (C) 2002 by Harald Welte <laforge@netfilter.org>
*/
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt
#include <linux/in.h>
#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/ip.h>
#include <net/ip.h>
#include <linux/tcp.h>
#include <net/checksum.h>

#include <linux/netfilter/x_tables.h>
#include <linux/netfilter_ipv4/ip_tables.h>
#include <linux/netfilter_ipv4/ipt_ECN.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Harald Welte <laforge@netfilter.org>");
MODULE_DESCRIPTION("Xtables: Explicit Congestion Notification (ECN) flag modification");

/* set ECT codepoint from IP header.
 * 	return false if there was an error. */
static inline bool
set_ect_ip(struct sk_buff *skb, const struct ipt_ECN_info *einfo)
{
	struct iphdr *iph = ip_hdr(skb);

	if ((iph->tos & IPT_ECN_IP_MASK) != (einfo->ip_ect & IPT_ECN_IP_MASK)) {
		__u8 oldtos;
		if (!skb_make_writable(skb, sizeof(struct iphdr)))
			return false;
		iph = ip_hdr(skb);
		oldtos = iph->tos;
		iph->tos &= ~IPT_ECN_IP_MASK;
		iph->tos |= (einfo->ip_ect & IPT_ECN_IP_MASK);
		csum_replace2(&iph->check, htons(oldtos), htons(iph->tos));
	}
	return true;
}

/* Return false if there was an error. */
static inline bool
set_ect_tcp(struct sk_buff *skb, const struct ipt_ECN_info *einfo)
{
	struct tcphdr _tcph, *tcph;
	__be16 oldval;

	/* Not enough header? */
	tcph = skb_header_pointer(skb, ip_hdrlen(skb), sizeof(_tcph), &_tcph);
	if (!tcph)
		return false;

	if ((!(einfo->operation & IPT_ECN_OP_SET_ECE) ||
	     tcph->ece == einfo->proto.tcp.ece) &&
	    (!(einfo->operation & IPT_ECN_OP_SET_CWR) ||
	     tcph->cwr == einfo->proto.tcp.cwr))
		return true;

	if (!skb_make_writable(skb, ip_hdrlen(skb) + sizeof(*tcph)))
		return false;
	tcph = (void *)ip_hdr(skb) + ip_hdrlen(skb);

	oldval = ((__be16 *)tcph)[6];
	if (einfo->operation & IPT_ECN_OP_SET_ECE)
		tcph->ece = einfo->proto.tcp.ece;
	if (einfo->operation & IPT_ECN_OP_SET_CWR)
		tcph->cwr = einfo->proto.tcp.cwr;

	inet_proto_csum_replace2(&tcph->check, skb,
				 oldval, ((__be16 *)tcph)[6], false);
	return true;
}

static unsigned int
ecn_tg(struct sk_buff *skb, const struct xt_action_param *par)
{
	const struct ipt_ECN_info *einfo = par->targinfo;

	if (einfo->operation & IPT_ECN_OP_SET_IP)
		if (!set_ect_ip(skb, einfo))
			return NF_DROP;

	if (einfo->operation & (IPT_ECN_OP_SET_ECE | IPT_ECN_OP_SET_CWR) &&
	    ip_hdr(skb)->protocol == IPPROTO_TCP)
		if (!set_ect_tcp(skb, einfo))
			return NF_DROP;

	return XT_CONTINUE;
}

static int ecn_tg_check(const struct xt_tgchk_param *par)
{
	const struct ipt_ECN_info *einfo = par->targinfo;
	const struct ipt_entry *e = par->entryinfo;

	if (einfo->operation & IPT_ECN_OP_MASK)
		return -EINVAL;

	if (einfo->ip_ect & ~IPT_ECN_IP_MASK)
		return -EINVAL;

	if ((einfo->operation & (IPT_ECN_OP_SET_ECE|IPT_ECN_OP_SET_CWR)) &&
	    (e->ip.proto != IPPROTO_TCP || (e->ip.invflags & XT_INV_PROTO))) {
		pr_info_ratelimited("cannot use operation on non-tcp rule\n");
		return -EINVAL;
	}
	return 0;
}

static struct xt_target ecn_tg_reg __read_mostly = {
	.name		= "ECN",
	.family		= NFPROTO_IPV4,
	.target		= ecn_tg,
	.targetsize	= sizeof(struct ipt_ECN_info),
	.table		= "mangle",
	.checkentry	= ecn_tg_check,
	.me		= THIS_MODULE,
};

static int __init ecn_tg_init(void)
{
	return xt_register_target(&ecn_tg_reg);
}

static void __exit ecn_tg_exit(void)
{
	xt_unregister_target(&ecn_tg_reg);
}

module_init(ecn_tg_init);
module_exit(ecn_tg_exit);
