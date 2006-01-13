/* Kernel module to match AH parameters. */
/* (C) 1999-2000 Yon Uriarte <yon@astaro.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/ip.h>

#include <linux/netfilter_ipv4/ipt_ah.h>
#include <linux/netfilter_ipv4/ip_tables.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Yon Uriarte <yon@astaro.de>");
MODULE_DESCRIPTION("iptables AH SPI match module");

#ifdef DEBUG_CONNTRACK
#define duprintf(format, args...) printk(format , ## args)
#else
#define duprintf(format, args...)
#endif

/* Returns 1 if the spi is matched by the range, 0 otherwise */
static inline int
spi_match(u_int32_t min, u_int32_t max, u_int32_t spi, int invert)
{
	int r=0;
        duprintf("ah spi_match:%c 0x%x <= 0x%x <= 0x%x",invert? '!':' ',
        	min,spi,max);
	r=(spi >= min && spi <= max) ^ invert;
	duprintf(" result %s\n",r? "PASS" : "FAILED");
	return r;
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
	struct ip_auth_hdr _ahdr, *ah;
	const struct ipt_ah *ahinfo = matchinfo;

	/* Must not be a fragment. */
	if (offset)
		return 0;

	ah = skb_header_pointer(skb, protoff,
				sizeof(_ahdr), &_ahdr);
	if (ah == NULL) {
		/* We've been asked to examine this packet, and we
		 * can't.  Hence, no choice but to drop.
		 */
		duprintf("Dropping evil AH tinygram.\n");
		*hotdrop = 1;
		return 0;
	}

	return spi_match(ahinfo->spis[0], ahinfo->spis[1],
			 ntohl(ah->spi),
			 !!(ahinfo->invflags & IPT_AH_INV_SPI));
}

/* Called when user tries to insert an entry of this type. */
static int
checkentry(const char *tablename,
	   const void *ip_void,
	   void *matchinfo,
	   unsigned int matchinfosize,
	   unsigned int hook_mask)
{
	const struct ipt_ah *ahinfo = matchinfo;
	const struct ipt_ip *ip = ip_void;

	/* Must specify proto == AH, and no unknown invflags */
	if (ip->proto != IPPROTO_AH || (ip->invflags & IPT_INV_PROTO)) {
		duprintf("ipt_ah: Protocol %u != %u\n", ip->proto,
			 IPPROTO_AH);
		return 0;
	}
	if (matchinfosize != IPT_ALIGN(sizeof(struct ipt_ah))) {
		duprintf("ipt_ah: matchsize %u != %u\n",
			 matchinfosize, IPT_ALIGN(sizeof(struct ipt_ah)));
		return 0;
	}
	if (ahinfo->invflags & ~IPT_AH_INV_MASK) {
		duprintf("ipt_ah: unknown flags %X\n",
			 ahinfo->invflags);
		return 0;
	}

	return 1;
}

static struct ipt_match ah_match = {
	.name		= "ah",
	.match		= &match,
	.checkentry	= &checkentry,
	.me		= THIS_MODULE,
};

static int __init init(void)
{
	return ipt_register_match(&ah_match);
}

static void __exit cleanup(void)
{
	ipt_unregister_match(&ah_match);
}

module_init(init);
module_exit(cleanup);
