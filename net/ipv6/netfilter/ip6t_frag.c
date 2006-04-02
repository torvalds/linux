/* Kernel module to match FRAG parameters. */

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
#include <linux/netfilter_ipv6/ip6t_frag.h>

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("IPv6 FRAG match");
MODULE_AUTHOR("Andras Kis-Szabo <kisza@sch.bme.hu>");

#if 0
#define DEBUGP printk
#else
#define DEBUGP(format, args...)
#endif

/* Returns 1 if the id is matched by the range, 0 otherwise */
static inline int
id_match(u_int32_t min, u_int32_t max, u_int32_t id, int invert)
{
	int r = 0;
	DEBUGP("frag id_match:%c 0x%x <= 0x%x <= 0x%x", invert ? '!' : ' ',
	       min, id, max);
	r = (id >= min && id <= max) ^ invert;
	DEBUGP(" result %s\n", r ? "PASS" : "FAILED");
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
	struct frag_hdr _frag, *fh;
	const struct ip6t_frag *fraginfo = matchinfo;
	unsigned int ptr;

	if (ipv6_find_hdr(skb, &ptr, NEXTHDR_FRAGMENT, NULL) < 0)
		return 0;

	fh = skb_header_pointer(skb, ptr, sizeof(_frag), &_frag);
	if (fh == NULL) {
		*hotdrop = 1;
		return 0;
	}

	DEBUGP("INFO %04X ", fh->frag_off);
	DEBUGP("OFFSET %04X ", ntohs(fh->frag_off) & ~0x7);
	DEBUGP("RES %02X %04X", fh->reserved, ntohs(fh->frag_off) & 0x6);
	DEBUGP("MF %04X ", fh->frag_off & htons(IP6_MF));
	DEBUGP("ID %u %08X\n", ntohl(fh->identification),
	       ntohl(fh->identification));

	DEBUGP("IPv6 FRAG id %02X ",
	       (id_match(fraginfo->ids[0], fraginfo->ids[1],
			 ntohl(fh->identification),
			 !!(fraginfo->invflags & IP6T_FRAG_INV_IDS))));
	DEBUGP("res %02X %02X%04X %02X ",
	       (fraginfo->flags & IP6T_FRAG_RES), fh->reserved,
	       ntohs(fh->frag_off) & 0x6,
	       !((fraginfo->flags & IP6T_FRAG_RES)
		 && (fh->reserved || (ntohs(fh->frag_off) & 0x06))));
	DEBUGP("first %02X %02X %02X ",
	       (fraginfo->flags & IP6T_FRAG_FST),
	       ntohs(fh->frag_off) & ~0x7,
	       !((fraginfo->flags & IP6T_FRAG_FST)
		 && (ntohs(fh->frag_off) & ~0x7)));
	DEBUGP("mf %02X %02X %02X ",
	       (fraginfo->flags & IP6T_FRAG_MF),
	       ntohs(fh->frag_off) & IP6_MF,
	       !((fraginfo->flags & IP6T_FRAG_MF)
		 && !((ntohs(fh->frag_off) & IP6_MF))));
	DEBUGP("last %02X %02X %02X\n",
	       (fraginfo->flags & IP6T_FRAG_NMF),
	       ntohs(fh->frag_off) & IP6_MF,
	       !((fraginfo->flags & IP6T_FRAG_NMF)
		 && (ntohs(fh->frag_off) & IP6_MF)));

	return (fh != NULL)
	       &&
	       (id_match(fraginfo->ids[0], fraginfo->ids[1],
			 ntohl(fh->identification),
			 !!(fraginfo->invflags & IP6T_FRAG_INV_IDS)))
	       &&
	       !((fraginfo->flags & IP6T_FRAG_RES)
		 && (fh->reserved || (ntohs(fh->frag_off) & 0x6)))
	       &&
	       !((fraginfo->flags & IP6T_FRAG_FST)
		 && (ntohs(fh->frag_off) & ~0x7))
	       &&
	       !((fraginfo->flags & IP6T_FRAG_MF)
		 && !(ntohs(fh->frag_off) & IP6_MF))
	       &&
	       !((fraginfo->flags & IP6T_FRAG_NMF)
		 && (ntohs(fh->frag_off) & IP6_MF));
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
	const struct ip6t_frag *fraginfo = matchinfo;

	if (fraginfo->invflags & ~IP6T_FRAG_INV_MASK) {
		DEBUGP("ip6t_frag: unknown flags %X\n", fraginfo->invflags);
		return 0;
	}
	return 1;
}

static struct ip6t_match frag_match = {
	.name		= "frag",
	.match		= match,
	.matchsize	= sizeof(struct ip6t_frag),
	.checkentry	= checkentry,
	.me		= THIS_MODULE,
};

static int __init ip6t_frag_init(void)
{
	return ip6t_register_match(&frag_match);
}

static void __exit ip6t_frag_fini(void)
{
	ip6t_unregister_match(&frag_match);
}

module_init(ip6t_frag_init);
module_exit(ip6t_frag_fini);
