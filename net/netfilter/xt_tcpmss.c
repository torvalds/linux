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
MODULE_DESCRIPTION("Xtables: TCP MSS match");
MODULE_ALIAS("ipt_tcpmss");
MODULE_ALIAS("ip6t_tcpmss");

static bool
tcpmss_mt(const struct sk_buff *skb, const struct xt_match_param *par)
{
	const struct xt_tcpmss_match_info *info = par->matchinfo;
	const struct tcphdr *th;
	struct tcphdr _tcph;
	/* tcp.doff is only 4 bits, ie. max 15 * 4 bytes */
	const u_int8_t *op;
	u8 _opt[15 * 4 - sizeof(_tcph)];
	unsigned int i, optlen;

	/* If we don't have the whole header, drop packet. */
	th = skb_header_pointer(skb, par->thoff, sizeof(_tcph), &_tcph);
	if (th == NULL)
		goto dropit;

	/* Malformed. */
	if (th->doff*4 < sizeof(*th))
		goto dropit;

	optlen = th->doff*4 - sizeof(*th);
	if (!optlen)
		goto out;

	/* Truncated options. */
	op = skb_header_pointer(skb, par->thoff + sizeof(*th), optlen, _opt);
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
	*par->hotdrop = true;
	return false;
}

static struct xt_match tcpmss_mt_reg[] __read_mostly = {
	{
		.name		= "tcpmss",
		.family		= NFPROTO_IPV4,
		.match		= tcpmss_mt,
		.matchsize	= sizeof(struct xt_tcpmss_match_info),
		.proto		= IPPROTO_TCP,
		.me		= THIS_MODULE,
	},
	{
		.name		= "tcpmss",
		.family		= NFPROTO_IPV6,
		.match		= tcpmss_mt,
		.matchsize	= sizeof(struct xt_tcpmss_match_info),
		.proto		= IPPROTO_TCP,
		.me		= THIS_MODULE,
	},
};

static int __init tcpmss_mt_init(void)
{
	return xt_register_matches(tcpmss_mt_reg, ARRAY_SIZE(tcpmss_mt_reg));
}

static void __exit tcpmss_mt_exit(void)
{
	xt_unregister_matches(tcpmss_mt_reg, ARRAY_SIZE(tcpmss_mt_reg));
}

module_init(tcpmss_mt_init);
module_exit(tcpmss_mt_exit);
