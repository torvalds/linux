/* Kernel module to match ESP parameters. */

/* (C) 1999-2000 Yon Uriarte <yon@astaro.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/in.h>
#include <linux/ip.h>

#include <linux/netfilter/xt_esp.h>
#include <linux/netfilter/x_tables.h>

#include <linux/netfilter_ipv4/ip_tables.h>
#include <linux/netfilter_ipv6/ip6_tables.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Yon Uriarte <yon@astaro.de>");
MODULE_DESCRIPTION("x_tables ESP SPI match module");
MODULE_ALIAS("ipt_esp");
MODULE_ALIAS("ip6t_esp");

#if 0
#define duprintf(format, args...) printk(format , ## args)
#else
#define duprintf(format, args...)
#endif

/* Returns 1 if the spi is matched by the range, 0 otherwise */
static inline int
spi_match(u_int32_t min, u_int32_t max, u_int32_t spi, int invert)
{
	int r = 0;
	duprintf("esp spi_match:%c 0x%x <= 0x%x <= 0x%x", invert ? '!' : ' ',
		 min, spi, max);
	r = (spi >= min && spi <= max) ^ invert;
	duprintf(" result %s\n", r ? "PASS" : "FAILED");
	return r;
}

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
	struct ip_esp_hdr _esp, *eh;
	const struct xt_esp *espinfo = matchinfo;

	/* Must not be a fragment. */
	if (offset)
		return 0;

	eh = skb_header_pointer(skb, protoff, sizeof(_esp), &_esp);
	if (eh == NULL) {
		/* We've been asked to examine this packet, and we
		 * can't.  Hence, no choice but to drop.
		 */
		duprintf("Dropping evil ESP tinygram.\n");
		*hotdrop = 1;
		return 0;
	}

	return spi_match(espinfo->spis[0], espinfo->spis[1], ntohl(eh->spi),
			 !!(espinfo->invflags & XT_ESP_INV_SPI));
}

/* Called when user tries to insert an entry of this type. */
static int
checkentry(const char *tablename,
	   const void *ip_void,
	   const struct xt_match *match,
	   void *matchinfo,
	   unsigned int matchinfosize,
	   unsigned int hook_mask)
{
	const struct xt_esp *espinfo = matchinfo;

	if (espinfo->invflags & ~XT_ESP_INV_MASK) {
		duprintf("xt_esp: unknown flags %X\n", espinfo->invflags);
		return 0;
	}

	return 1;
}

static struct xt_match esp_match = {
	.name		= "esp",
	.family		= AF_INET,
	.proto		= IPPROTO_ESP,
	.match		= &match,
	.matchsize	= sizeof(struct xt_esp),
	.checkentry	= &checkentry,
	.me		= THIS_MODULE,
};

static struct xt_match esp6_match = {
	.name		= "esp",
	.family		= AF_INET6,
	.proto		= IPPROTO_ESP,
	.match		= &match,
	.matchsize	= sizeof(struct xt_esp),
	.checkentry	= &checkentry,
	.me		= THIS_MODULE,
};

static int __init xt_esp_init(void)
{
	int ret;
	ret = xt_register_match(&esp_match);
	if (ret)
		return ret;

	ret = xt_register_match(&esp6_match);
	if (ret)
		xt_unregister_match(&esp_match);

	return ret;
}

static void __exit xt_esp_cleanup(void)
{
	xt_unregister_match(&esp_match);
	xt_unregister_match(&esp6_match);
}

module_init(xt_esp_init);
module_exit(xt_esp_cleanup);
