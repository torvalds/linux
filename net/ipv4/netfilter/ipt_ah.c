/* Kernel module to match AH parameters. */
/* (C) 1999-2000 Yon Uriarte <yon@astaro.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/in.h>
#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/ip.h>

#include <linux/netfilter_ipv4/ipt_ah.h>
#include <linux/netfilter/x_tables.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Yon Uriarte <yon@astaro.de>");
MODULE_DESCRIPTION("Xtables: IPv4 IPsec-AH SPI match");

#ifdef DEBUG_CONNTRACK
#define duprintf(format, args...) printk(format , ## args)
#else
#define duprintf(format, args...)
#endif

/* Returns 1 if the spi is matched by the range, 0 otherwise */
static inline bool
spi_match(u_int32_t min, u_int32_t max, u_int32_t spi, bool invert)
{
	bool r;
	duprintf("ah spi_match:%c 0x%x <= 0x%x <= 0x%x",invert? '!':' ',
		min,spi,max);
	r=(spi >= min && spi <= max) ^ invert;
	duprintf(" result %s\n",r? "PASS" : "FAILED");
	return r;
}

static bool ah_mt(const struct sk_buff *skb, const struct xt_match_param *par)
{
	struct ip_auth_hdr _ahdr;
	const struct ip_auth_hdr *ah;
	const struct ipt_ah *ahinfo = par->matchinfo;

	/* Must not be a fragment. */
	if (par->fragoff != 0)
		return false;

	ah = skb_header_pointer(skb, par->thoff, sizeof(_ahdr), &_ahdr);
	if (ah == NULL) {
		/* We've been asked to examine this packet, and we
		 * can't.  Hence, no choice but to drop.
		 */
		duprintf("Dropping evil AH tinygram.\n");
		*par->hotdrop = true;
		return 0;
	}

	return spi_match(ahinfo->spis[0], ahinfo->spis[1],
			 ntohl(ah->spi),
			 !!(ahinfo->invflags & IPT_AH_INV_SPI));
}

static bool ah_mt_check(const struct xt_mtchk_param *par)
{
	const struct ipt_ah *ahinfo = par->matchinfo;

	/* Must specify no unknown invflags */
	if (ahinfo->invflags & ~IPT_AH_INV_MASK) {
		duprintf("ipt_ah: unknown flags %X\n", ahinfo->invflags);
		return false;
	}
	return true;
}

static struct xt_match ah_mt_reg __read_mostly = {
	.name		= "ah",
	.family		= NFPROTO_IPV4,
	.match		= ah_mt,
	.matchsize	= sizeof(struct ipt_ah),
	.proto		= IPPROTO_AH,
	.checkentry	= ah_mt_check,
	.me		= THIS_MODULE,
};

static int __init ah_mt_init(void)
{
	return xt_register_match(&ah_mt_reg);
}

static void __exit ah_mt_exit(void)
{
	xt_unregister_match(&ah_mt_reg);
}

module_init(ah_mt_init);
module_exit(ah_mt_exit);
