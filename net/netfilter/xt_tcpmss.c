/* Kernel module to match TCP MSS values. */

/* Copyright (C) 2000 Marc Boucher <marc@mbsi.ca>
 * Portions (C) 2005 by Harald Welte <laforge@netfilter.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/skbuff.h>
#include <net/tcp.h>

#include <linux/netfilter/xt_tcpmss.h>
#include <linux/netfilter/x_tables.h>

#include <linux/netfilter_ipv4/ip_tables.h>
#include <linux/netfilter_ipv6/ip6_tables.h>

#define TH_SYN 0x02

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Marc Boucher <marc@mbsi.ca>");
MODULE_DESCRIPTION("iptables TCP MSS match module");
MODULE_ALIAS("ipt_tcpmss");

/* Returns 1 if the mss option is set and matched by the range, 0 otherwise */
static inline int
mssoption_match(u_int16_t min, u_int16_t max,
		const struct sk_buff *skb,
		unsigned int protoff,
		int invert,
		int *hotdrop)
{
	struct tcphdr _tcph, *th;
	/* tcp.doff is only 4 bits, ie. max 15 * 4 bytes */
	u8 _opt[15 * 4 - sizeof(_tcph)], *op;
	unsigned int i, optlen;

	/* If we don't have the whole header, drop packet. */
	th = skb_header_pointer(skb, protoff, sizeof(_tcph), &_tcph);
	if (th == NULL)
		goto dropit;

	/* Malformed. */
	if (th->doff*4 < sizeof(*th))
		goto dropit;

	optlen = th->doff*4 - sizeof(*th);
	if (!optlen)
		goto out;

	/* Truncated options. */
	op = skb_header_pointer(skb, protoff + sizeof(*th), optlen, _opt);
	if (op == NULL)
		goto dropit;

	for (i = 0; i < optlen; ) {
		if (op[i] == TCPOPT_MSS
		    && (optlen - i) >= TCPOLEN_MSS
		    && op[i+1] == TCPOLEN_MSS) {
			u_int16_t mssval;

			mssval = (op[i+2] << 8) | op[i+3];
			
			return (mssval >= min && mssval <= max) ^ invert;
		}
		if (op[i] < 2) i++;
		else i += op[i+1]?:1;
	}
out:
	return invert;

 dropit:
	*hotdrop = 1;
	return 0;
}

static int
match(const struct sk_buff *skb,
      const struct net_device *in,
      const struct net_device *out,
      const void *matchinfo,
      int offset,
      unsigned int protoff,
      int *hotdrop)
{
	const struct xt_tcpmss_match_info *info = matchinfo;

	return mssoption_match(info->mss_min, info->mss_max, skb, protoff,
			       info->invert, hotdrop);
}

static int
checkentry(const char *tablename,
           const void *ipinfo,
           void *matchinfo,
           unsigned int matchsize,
           unsigned int hook_mask)
{
	const struct ipt_ip *ip = ipinfo;
	if (matchsize != XT_ALIGN(sizeof(struct xt_tcpmss_match_info)))
		return 0;

	/* Must specify -p tcp */
	if (ip->proto != IPPROTO_TCP || (ip->invflags & IPT_INV_PROTO)) {
		printk("tcpmss: Only works on TCP packets\n");
		return 0;
	}

	return 1;
}

static int
checkentry6(const char *tablename,
	   const void *ipinfo,
           void *matchinfo,
           unsigned int matchsize,
           unsigned int hook_mask)
{
	const struct ip6t_ip6 *ip = ipinfo;

	if (matchsize != XT_ALIGN(sizeof(struct xt_tcpmss_match_info)))
		return 0;

	/* Must specify -p tcp */
	if (ip->proto != IPPROTO_TCP || (ip->invflags & XT_INV_PROTO)) {
		printk("tcpmss: Only works on TCP packets\n");
		return 0;
	}

	return 1;
}

static struct xt_match tcpmss_match = {
	.name		= "tcpmss",
	.match		= &match,
	.checkentry	= &checkentry,
	.me		= THIS_MODULE,
};

static struct xt_match tcpmss6_match = {
	.name		= "tcpmss",
	.match		= &match,
	.checkentry	= &checkentry6,
	.me		= THIS_MODULE,
};


static int __init init(void)
{
	int ret;
	ret = xt_register_match(AF_INET, &tcpmss_match);
	if (ret)
		return ret;

	ret = xt_register_match(AF_INET6, &tcpmss6_match);
	if (ret)
		xt_unregister_match(AF_INET, &tcpmss_match);

	return ret;
}

static void __exit fini(void)
{
	xt_unregister_match(AF_INET6, &tcpmss6_match);
	xt_unregister_match(AF_INET, &tcpmss_match);
}

module_init(init);
module_exit(fini);
