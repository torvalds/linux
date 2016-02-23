/*  Kernel module to match IPComp parameters for IPv4 and IPv6
 *
 *  Copyright (C) 2013 WindRiver
 *
 *  Author:
 *  Fan Du <fan.du@windriver.com>
 *
 *  Based on:
 *  net/netfilter/xt_esp.c
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version
 *  2 of the License, or (at your option) any later version.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt
#include <linux/in.h>
#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/ip.h>

#include <linux/netfilter/xt_ipcomp.h>
#include <linux/netfilter/x_tables.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Fan Du <fan.du@windriver.com>");
MODULE_DESCRIPTION("Xtables: IPv4/6 IPsec-IPComp SPI match");

/* Returns 1 if the spi is matched by the range, 0 otherwise */
static inline bool
spi_match(u_int32_t min, u_int32_t max, u_int32_t spi, bool invert)
{
	bool r;
	pr_debug("spi_match:%c 0x%x <= 0x%x <= 0x%x\n",
		 invert ? '!' : ' ', min, spi, max);
	r = (spi >= min && spi <= max) ^ invert;
	pr_debug(" result %s\n", r ? "PASS" : "FAILED");
	return r;
}

static bool comp_mt(const struct sk_buff *skb, struct xt_action_param *par)
{
	struct ip_comp_hdr _comphdr;
	const struct ip_comp_hdr *chdr;
	const struct xt_ipcomp *compinfo = par->matchinfo;

	/* Must not be a fragment. */
	if (par->fragoff != 0)
		return false;

	chdr = skb_header_pointer(skb, par->thoff, sizeof(_comphdr), &_comphdr);
	if (chdr == NULL) {
		/* We've been asked to examine this packet, and we
		 * can't.  Hence, no choice but to drop.
		 */
		pr_debug("Dropping evil IPComp tinygram.\n");
		par->hotdrop = true;
		return 0;
	}

	return spi_match(compinfo->spis[0], compinfo->spis[1],
			 ntohs(chdr->cpi),
			 !!(compinfo->invflags & XT_IPCOMP_INV_SPI));
}

static int comp_mt_check(const struct xt_mtchk_param *par)
{
	const struct xt_ipcomp *compinfo = par->matchinfo;

	/* Must specify no unknown invflags */
	if (compinfo->invflags & ~XT_IPCOMP_INV_MASK) {
		pr_err("unknown flags %X\n", compinfo->invflags);
		return -EINVAL;
	}
	return 0;
}

static struct xt_match comp_mt_reg[] __read_mostly = {
	{
		.name		= "ipcomp",
		.family		= NFPROTO_IPV4,
		.match		= comp_mt,
		.matchsize	= sizeof(struct xt_ipcomp),
		.proto		= IPPROTO_COMP,
		.checkentry	= comp_mt_check,
		.me		= THIS_MODULE,
	},
	{
		.name		= "ipcomp",
		.family		= NFPROTO_IPV6,
		.match		= comp_mt,
		.matchsize	= sizeof(struct xt_ipcomp),
		.proto		= IPPROTO_COMP,
		.checkentry	= comp_mt_check,
		.me		= THIS_MODULE,
	},
};

static int __init comp_mt_init(void)
{
	return xt_register_matches(comp_mt_reg, ARRAY_SIZE(comp_mt_reg));
}

static void __exit comp_mt_exit(void)
{
	xt_unregister_matches(comp_mt_reg, ARRAY_SIZE(comp_mt_reg));
}

module_init(comp_mt_init);
module_exit(comp_mt_exit);
