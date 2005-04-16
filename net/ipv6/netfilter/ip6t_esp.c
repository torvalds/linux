/* Kernel module to match ESP parameters. */
/* (C) 2001-2002 Andras Kis-Szabo <kisza@sch.bme.hu>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */


#include <linux/module.h>
#include <linux/skbuff.h>
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
      const void *matchinfo,
      int offset,
      unsigned int protoff,
      int *hotdrop)
{
	struct ip_esp_hdr _esp, *eh = NULL;
	const struct ip6t_esp *espinfo = matchinfo;
	unsigned int temp;
	int len;
	u8 nexthdr;
	unsigned int ptr;

	/* Make sure this isn't an evil packet */
	/*DEBUGP("ipv6_esp entered \n");*/

	/* type of the 1st exthdr */
	nexthdr = skb->nh.ipv6h->nexthdr;
	/* pointer to the 1st exthdr */
	ptr = sizeof(struct ipv6hdr);
	/* available length */
	len = skb->len - ptr;
	temp = 0;

	while (ip6t_ext_hdr(nexthdr)) {
		struct ipv6_opt_hdr _hdr, *hp;
		int hdrlen;

		DEBUGP("ipv6_esp header iteration \n");

		/* Is there enough space for the next ext header? */
		if (len < sizeof(struct ipv6_opt_hdr))
			return 0;
		/* No more exthdr -> evaluate */
		if (nexthdr == NEXTHDR_NONE)
			break;
		/* ESP -> evaluate */
		if (nexthdr == NEXTHDR_ESP) {
			temp |= MASK_ESP;
			break;
		}

		hp = skb_header_pointer(skb, ptr, sizeof(_hdr), &_hdr);
		BUG_ON(hp == NULL);

		/* Calculate the header length */
		if (nexthdr == NEXTHDR_FRAGMENT)
			hdrlen = 8;
		else if (nexthdr == NEXTHDR_AUTH)
			hdrlen = (hp->hdrlen+2)<<2;
		else
			hdrlen = ipv6_optlen(hp);

		/* set the flag */
		switch (nexthdr) {
		case NEXTHDR_HOP:
		case NEXTHDR_ROUTING:
		case NEXTHDR_FRAGMENT:
		case NEXTHDR_AUTH:
		case NEXTHDR_DEST:
			break;
		default:
			DEBUGP("ipv6_esp match: unknown nextheader %u\n",nexthdr);
			return 0;
		}

		nexthdr = hp->nexthdr;
		len -= hdrlen;
		ptr += hdrlen;
		if (ptr > skb->len) {
			DEBUGP("ipv6_esp: new pointer too large! \n");
			break;
		}
	}

	/* ESP header not found */
	if (temp != MASK_ESP)
		return 0;

	if (len < sizeof(struct ip_esp_hdr)) {
		*hotdrop = 1;
		return 0;
	}

	eh = skb_header_pointer(skb, ptr, sizeof(_esp), &_esp);
	BUG_ON(eh == NULL);

	DEBUGP("IPv6 ESP SPI %u %08X\n", ntohl(eh->spi), ntohl(eh->spi));

	return (eh != NULL)
		&& spi_match(espinfo->spis[0], espinfo->spis[1],
			      ntohl(eh->spi),
			      !!(espinfo->invflags & IP6T_ESP_INV_SPI));
}

/* Called when user tries to insert an entry of this type. */
static int
checkentry(const char *tablename,
	   const struct ip6t_ip6 *ip,
	   void *matchinfo,
	   unsigned int matchinfosize,
	   unsigned int hook_mask)
{
	const struct ip6t_esp *espinfo = matchinfo;

	if (matchinfosize != IP6T_ALIGN(sizeof(struct ip6t_esp))) {
		DEBUGP("ip6t_esp: matchsize %u != %u\n",
			 matchinfosize, IP6T_ALIGN(sizeof(struct ip6t_esp)));
		return 0;
	}
	if (espinfo->invflags & ~IP6T_ESP_INV_MASK) {
		DEBUGP("ip6t_esp: unknown flags %X\n",
			 espinfo->invflags);
		return 0;
	}
	return 1;
}

static struct ip6t_match esp_match = {
	.name		= "esp",
	.match		= &match,
	.checkentry	= &checkentry,
	.me		= THIS_MODULE,
};

static int __init init(void)
{
	return ip6t_register_match(&esp_match);
}

static void __exit cleanup(void)
{
	ip6t_unregister_match(&esp_match);
}

module_init(init);
module_exit(cleanup);
