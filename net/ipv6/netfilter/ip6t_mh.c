// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C)2006 USAGI/WIDE Project
 *
 * Author:
 *	Masahide NAKAMURA @USAGI <masahide.nakamura.cz@hitachi.com>
 *
 * Based on net/netfilter/xt_tcpudp.c
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt
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

/* Returns 1 if the type is matched by the range, 0 otherwise */
static inline bool
type_match(u_int8_t min, u_int8_t max, u_int8_t type, bool invert)
{
	return (type >= min && type <= max) ^ invert;
}

static bool mh_mt6(const struct sk_buff *skb, struct xt_action_param *par)
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
		pr_debug("Dropping evil MH tinygram.\n");
		par->hotdrop = true;
		return false;
	}

	if (mh->ip6mh_proto != IPPROTO_NONE) {
		pr_debug("Dropping invalid MH Payload Proto: %u\n",
			 mh->ip6mh_proto);
		par->hotdrop = true;
		return false;
	}

	return type_match(mhinfo->types[0], mhinfo->types[1], mh->ip6mh_type,
			  !!(mhinfo->invflags & IP6T_MH_INV_TYPE));
}

static int mh_mt6_check(const struct xt_mtchk_param *par)
{
	const struct ip6t_mh *mhinfo = par->matchinfo;

	/* Must specify no unknown invflags */
	return (mhinfo->invflags & ~IP6T_MH_INV_MASK) ? -EINVAL : 0;
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
