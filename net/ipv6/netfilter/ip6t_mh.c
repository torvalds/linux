/*
 * Copyright (C)2006 USAGI/WIDE Project
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Author:
 *	Masahide NAKAMURA @USAGI <masahide.nakamura.cz@hitachi.com>
 *
 * Based on net/netfilter/xt_tcpudp.c
 *
 */
#include <linux/types.h>
#include <linux/module.h>
#include <net/ip.h>
#include <linux/ipv6.h>
#include <net/ipv6.h>
#include <net/mip6.h>

#include <linux/netfilter/x_tables.h>
#include <linux/netfilter_ipv6/ip6t_mh.h>

MODULE_DESCRIPTION("Xtables: IPv6 Mobility Header match");
MODULE_LICENSE("GPL");

#ifdef DEBUG_IP_FIREWALL_USER
#define duprintf(format, args...) printk(format , ## args)
#else
#define duprintf(format, args...)
#endif

/* Returns 1 if the type is matched by the range, 0 otherwise */
static inline bool
type_match(u_int8_t min, u_int8_t max, u_int8_t type, bool invert)
{
	return (type >= min && type <= max) ^ invert;
}

static bool mh_mt6(const struct sk_buff *skb, const struct xt_match_param *par)
{
	struct ip6_mh _mh;
	const struct ip6_mh *mh;
	const struct ip6t_mh *mhinfo = par->matchinfo;

	/* Must not be a fragment. */
	if (par->fragoff != 0)
		return false;

	mh = skb_header_pointer(skb, par->thoff, sizeof(_mh), &_mh);
	if (mh == NULL) {
		/* We've been asked to examine this packet, and we
		   can't.  Hence, no choice but to drop. */
		duprintf("Dropping evil MH tinygram.\n");
		*par->hotdrop = true;
		return false;
	}

	if (mh->ip6mh_proto != IPPROTO_NONE) {
		duprintf("Dropping invalid MH Payload Proto: %u\n",
			 mh->ip6mh_proto);
		*par->hotdrop = true;
		return false;
	}

	return type_match(mhinfo->types[0], mhinfo->types[1], mh->ip6mh_type,
			  !!(mhinfo->invflags & IP6T_MH_INV_TYPE));
}

static bool mh_mt6_check(const struct xt_mtchk_param *par)
{
	const struct ip6t_mh *mhinfo = par->matchinfo;

	/* Must specify no unknown invflags */
	return !(mhinfo->invflags & ~IP6T_MH_INV_MASK);
}

static struct xt_match mh_mt6_reg __read_mostly = {
	.name		= "mh",
	.family		= NFPROTO_IPV6,
	.checkentry	= mh_mt6_check,
	.match		= mh_mt6,
	.matchsize	= sizeof(struct ip6t_mh),
	.proto		= IPPROTO_MH,
	.me		= THIS_MODULE,
};

static int __init mh_mt6_init(void)
{
	return xt_register_match(&mh_mt6_reg);
}

static void __exit mh_mt6_exit(void)
{
	xt_unregister_match(&mh_mt6_reg);
}

module_init(mh_mt6_init);
module_exit(mh_mt6_exit);
