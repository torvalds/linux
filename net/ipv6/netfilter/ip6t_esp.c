/* Kernel module to match ESP parameters. */
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

#include <linux/netfilter_ipv6/ip6_tables.h>
#include <linux/netfilter_ipv6/ip6t_esp.h>

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("IPv6 ESP match");
MODULE_AUTHOR("Andras Kis-Szabo <kisza@sch.bme.hu>");

#if 0
#define DEBUGP printk
#else
#define DEBUGP(format, args...)
#endif

/* Returns 1 if the spi is matched by the range, 0 otherwise */
static inline int
spi_match(u_int32_t min, u_int32_t max, u_int32_t spi, int invert)
{
	int r=0;
	DEBUGP("esp spi_match:%c 0x%x <= 0x%x <= 0x%x",invert? '!':' ',
	       min,spi,max);
	r=(spi >= min && spi <= max) ^ invert;
	DEBUGP(" result %s\n",r? "PASS\n" : "FAILED\n");
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
	const struct ip6t_esp *espinfo = matchinfo;
	unsigned int ptr;

	/* Make sure this isn't an evil packet */
	/*DEBUGP("ipv6_esp entered \n");*/

	if (ipv6_find_hdr(skb, &ptr, NEXTHDR_ESP, NULL) < 0)
		return 0;

	eh = skb_header_pointer(skb, ptr, sizeof(_esp), &_esp);
	if (eh == NULL) {
		*hotdrop = 1;
		return 0;
	}

	DEBUGP("IPv6 ESP SPI %u %08X\n", ntohl(eh->spi), ntohl(eh->spi));

	return (eh != NULL)
		&& spi_match(espinfo->spis[0], espinfo->spis[1],
			      ntohl(eh->spi),
			      !!(espinfo->invflags & IP6T_ESP_INV_SPI));
}

/* Called when user tries to insert an entry of this type. */
static int
checkentry(const char *tablename,
	   const void *ip,
	   const struct xt_match *match,
	   void *matchinfo,
	   unsigned int matchinfosize,
	   unsigned int hook_mask)
{
	const struct ip6t_esp *espinfo = matchinfo;

	if (espinfo->invflags & ~IP6T_ESP_INV_MASK) {
		DEBUGP("ip6t_esp: unknown flags %X\n",
			 espinfo->invflags);
		return 0;
	}
	return 1;
}

static struct ip6t_match esp_match = {
	.name		= "esp",
	.match		= match,
	.matchsize	= sizeof(struct ip6t_esp),
	.checkentry	= checkentry,
	.me		= THIS_MODULE,
};

static int __init ip6t_esp_init(void)
{
	return ip6t_register_match(&esp_match);
}

static void __exit ip6t_esp_fini(void)
{
	ip6t_unregister_match(&esp_match);
}

module_init(ip6t_esp_init);
module_exit(ip6t_esp_fini);
