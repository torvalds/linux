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

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Marc Boucher <marc@mbsi.ca>");
MODULE_DESCRIPTION("iptables TCP MSS match module");
MODULE_ALIAS("ipt_tcpmss");

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
	const struct xt_tcpmss_match_info *info = matchinfo;
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

			return (mssval >= info->mss_min &&
				mssval <= info->mss_max) ^ info->invert;
		}
		if (op[i] < 2)
			i++;
		else
			i += op[i+1] ? : 1;
	}
out:
	return info->invert;

dropit:
	*hotdrop = 1;
	return 0;
}

static struct xt_match xt_tcpmss_match[] = {
	{
		.name		= "tcpmss",
		.family		= AF_INET,
		.match		= match,
		.matchsize	= sizeof(struct xt_tcpmss_match_info),
		.proto		= IPPROTO_TCP,
		.me		= THIS_MODULE,
	},
	{
		.name		= "tcpmss",
		.family		= AF_INET6,
		.match		= match,
		.matchsize	= sizeof(struct xt_tcpmss_match_info),
		.proto		= IPPROTO_TCP,
		.me		= THIS_MODULE,
	},
};

static int __init xt_tcpmss_init(void)
{
	return xt_register_matches(xt_tcpmss_match,
				   ARRAY_SIZE(xt_tcpmss_match));
}

static void __exit xt_tcpmss_fini(void)
{
	xt_unregister_matches(xt_tcpmss_match, ARRAY_SIZE(xt_tcpmss_match));
}

module_init(xt_tcpmss_init);
module_exit(xt_tcpmss_fini);
