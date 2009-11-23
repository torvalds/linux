/* Kernel module to match AH parameters. */

/* (C) 2001-2002 Andras Kis-Szabo <kisza@sch.bme.hu>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/ip.h>
#include <linux/ipv6.h>
#include <linux/types.h>
#include <net/checksum.h>
#include <net/ipv6.h>

#include <linux/netfilter/x_tables.h>
#include <linux/netfilter_ipv6/ip6_tables.h>
#include <linux/netfilter_ipv6/ip6t_ah.h>

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Xtables: IPv6 IPsec-AH match");
MODULE_AUTHOR("Andras Kis-Szabo <kisza@sch.bme.hu>");

/* Returns 1 if the spi is matched by the range, 0 otherwise */
static inline bool
spi_match(u_int32_t min, u_int32_t max, u_int32_t spi, bool invert)
{
	bool r;

	pr_debug("ah spi_match:%c 0x%x <= 0x%x <= 0x%x",
		 invert ? '!' : ' ', min, spi, max);
	r = (spi >= min && spi <= max) ^ invert;
	pr_debug(" result %s\n", r ? "PASS" : "FAILED");
	return r;
}

static bool ah_mt6(const struct sk_buff *skb, const struct xt_match_param *par)
{
	struct ip_auth_hdr _ah;
	const struct ip_auth_hdr *ah;
	const struct ip6t_ah *ahinfo = par->matchinfo;
	unsigned int ptr;
	unsigned int hdrlen = 0;
	int err;

	err = ipv6_find_hdr(skb, &ptr, NEXTHDR_AUTH, NULL);
	if (err < 0) {
		if (err != -ENOENT)
			*par->hotdrop = true;
		return false;
	}

	ah = skb_header_pointer(skb, ptr, sizeof(_ah), &_ah);
	if (ah == NULL) {
		*par->hotdrop = true;
		return false;
	}

	hdrlen = (ah->hdrlen + 2) << 2;

	pr_debug("IPv6 AH LEN %u %u ", hdrlen, ah->hdrlen);
	pr_debug("RES %04X ", ah->reserved);
	pr_debug("SPI %u %08X\n", ntohl(ah->spi), ntohl(ah->spi));

	pr_debug("IPv6 AH spi %02X ",
		 spi_match(ahinfo->spis[0], ahinfo->spis[1],
			   ntohl(ah->spi),
			   !!(ahinfo->invflags & IP6T_AH_INV_SPI)));
	pr_debug("len %02X %04X %02X ",
		 ahinfo->hdrlen, hdrlen,
		 (!ahinfo->hdrlen ||
		  (ahinfo->hdrlen == hdrlen) ^
		  !!(ahinfo->invflags & IP6T_AH_INV_LEN)));
	pr_debug("res %02X %04X %02X\n",
		 ahinfo->hdrres, ah->reserved,
		 !(ahinfo->hdrres && ah->reserved));

	return (ah != NULL) &&
		spi_match(ahinfo->spis[0], ahinfo->spis[1],
			  ntohl(ah->spi),
			  !!(ahinfo->invflags & IP6T_AH_INV_SPI)) &&
		(!ahinfo->hdrlen ||
		 (ahinfo->hdrlen == hdrlen) ^
		 !!(ahinfo->invflags & IP6T_AH_INV_LEN)) &&
		!(ahinfo->hdrres && ah->reserved);
}

static bool ah_mt6_check(const struct xt_mtchk_param *par)
{
	const struct ip6t_ah *ahinfo = par->matchinfo;

	if (ahinfo->invflags & ~IP6T_AH_INV_MASK) {
		pr_debug("ip6t_ah: unknown flags %X\n", ahinfo->invflags);
		return false;
	}
	return true;
}

static struct xt_match ah_mt6_reg __read_mostly = {
	.name		= "ah",
	.family		= NFPROTO_IPV6,
	.match		= ah_mt6,
	.matchsize	= sizeof(struct ip6t_ah),
	.checkentry	= ah_mt6_check,
	.me		= THIS_MODULE,
};

static int __init ah_mt6_init(void)
{
	return xt_register_match(&ah_mt6_reg);
}

static void __exit ah_mt6_exit(void)
{
	xt_unregister_match(&ah_mt6_reg);
}

module_init(ah_mt6_init);
module_exit(ah_mt6_exit);
