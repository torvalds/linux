/*
 * Copyright (c) 2011, 2012 Patrick McHardy <kaber@trash.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/ipv6.h>
#include <linux/netfilter.h>
#include <linux/netfilter_ipv6.h>
#include <linux/netfilter_ipv6/ip6t_NPT.h>
#include <linux/netfilter/x_tables.h>

static int ip6t_npt_checkentry(const struct xt_tgchk_param *par)
{
	struct ip6t_npt_tginfo *npt = par->targinfo;
	__wsum src_sum = 0, dst_sum = 0;
	unsigned int i;

	if (npt->src_pfx_len > 64 || npt->dst_pfx_len > 64)
		return -EINVAL;

	for (i = 0; i < ARRAY_SIZE(npt->src_pfx.in6.s6_addr16); i++) {
		src_sum = csum_add(src_sum,
				(__force __wsum)npt->src_pfx.in6.s6_addr16[i]);
		dst_sum = csum_add(dst_sum,
				(__force __wsum)npt->dst_pfx.in6.s6_addr16[i]);
	}

	npt->adjustment = ~csum_fold(csum_sub(src_sum, dst_sum));
	return 0;
}

static bool ip6t_npt_map_pfx(const struct ip6t_npt_tginfo *npt,
			     struct in6_addr *addr)
{
	unsigned int pfx_len;
	unsigned int i, idx;
	__be32 mask;
	__sum16 sum;

	pfx_len = max(npt->src_pfx_len, npt->dst_pfx_len);
	for (i = 0; i < pfx_len; i += 32) {
		if (pfx_len - i >= 32)
			mask = 0;
		else
			mask = htonl(~((1 << (pfx_len - i)) - 1));

		idx = i / 32;
		addr->s6_addr32[idx] &= mask;
		addr->s6_addr32[idx] |= ~mask & npt->dst_pfx.in6.s6_addr32[idx];
	}

	if (pfx_len <= 48)
		idx = 3;
	else {
		for (idx = 4; idx < ARRAY_SIZE(addr->s6_addr16); idx++) {
			if ((__force __sum16)addr->s6_addr16[idx] !=
			    CSUM_MANGLED_0)
				break;
		}
		if (idx == ARRAY_SIZE(addr->s6_addr16))
			return false;
	}

	sum = ~csum_fold(csum_add(csum_unfold((__force __sum16)addr->s6_addr16[idx]),
				  csum_unfold(npt->adjustment)));
	if (sum == CSUM_MANGLED_0)
		sum = 0;
	*(__force __sum16 *)&addr->s6_addr16[idx] = sum;

	return true;
}

static unsigned int
ip6t_snpt_tg(struct sk_buff *skb, const struct xt_action_param *par)
{
	const struct ip6t_npt_tginfo *npt = par->targinfo;

	if (!ip6t_npt_map_pfx(npt, &ipv6_hdr(skb)->saddr)) {
		icmpv6_send(skb, ICMPV6_PARAMPROB, ICMPV6_HDR_FIELD,
			    offsetof(struct ipv6hdr, saddr));
		return NF_DROP;
	}
	return XT_CONTINUE;
}

static unsigned int
ip6t_dnpt_tg(struct sk_buff *skb, const struct xt_action_param *par)
{
	const struct ip6t_npt_tginfo *npt = par->targinfo;

	if (!ip6t_npt_map_pfx(npt, &ipv6_hdr(skb)->daddr)) {
		icmpv6_send(skb, ICMPV6_PARAMPROB, ICMPV6_HDR_FIELD,
			    offsetof(struct ipv6hdr, daddr));
		return NF_DROP;
	}
	return XT_CONTINUE;
}

static struct xt_target ip6t_npt_target_reg[] __read_mostly = {
	{
		.name		= "SNPT",
		.target		= ip6t_snpt_tg,
		.targetsize	= sizeof(struct ip6t_npt_tginfo),
		.checkentry	= ip6t_npt_checkentry,
		.family		= NFPROTO_IPV6,
		.hooks		= (1 << NF_INET_LOCAL_IN) |
				  (1 << NF_INET_POST_ROUTING),
		.me		= THIS_MODULE,
	},
	{
		.name		= "DNPT",
		.target		= ip6t_dnpt_tg,
		.targetsize	= sizeof(struct ip6t_npt_tginfo),
		.checkentry	= ip6t_npt_checkentry,
		.family		= NFPROTO_IPV6,
		.hooks		= (1 << NF_INET_PRE_ROUTING) |
				  (1 << NF_INET_LOCAL_OUT),
		.me		= THIS_MODULE,
	},
};

static int __init ip6t_npt_init(void)
{
	return xt_register_targets(ip6t_npt_target_reg,
				   ARRAY_SIZE(ip6t_npt_target_reg));
}

static void __exit ip6t_npt_exit(void)
{
	xt_unregister_targets(ip6t_npt_target_reg,
			      ARRAY_SIZE(ip6t_npt_target_reg));
}

module_init(ip6t_npt_init);
module_exit(ip6t_npt_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("IPv6-to-IPv6 Network Prefix Translation (RFC 6296)");
MODULE_AUTHOR("Patrick McHardy <kaber@trash.net>");
MODULE_ALIAS("ip6t_SNPT");
MODULE_ALIAS("ip6t_DNPT");
