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

#include <linux/netfilter/x_tables.h>
#include <linux/netfilter_ipv6/ip6_tables.h>
#include <linux/netfilter_ipv6/ip6t_frag.h>

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("IPv6 FRAG match");
MODULE_AUTHOR("Andras Kis-Szabo <kisza@sch.bme.hu>");

/* Returns 1 if the id is matched by the range, 0 otherwise */
static inline bool
id_match(u_int32_t min, u_int32_t max, u_int32_t id, bool invert)
{
	bool r;
	pr_debug("frag id_match:%c 0x%x <= 0x%x <= 0x%x", invert ? '!' : ' ',
		 min, id, max);
	r = (id >= min && id <= max) ^ invert;
	pr_debug(" result %s\n", r ? "PASS" : "FAILED");
	return r;
}

static bool
match(const struct sk_buff *skb,
      const struct net_device *in,
      const struct net_device *out,
      const struct xt_match *match,
      const void *matchinfo,
      int offset,
      unsigned int protoff,
      bool *hotdrop)
{
	struct frag_hdr _frag;
	const struct frag_hdr *fh;
	const struct ip6t_frag *fraginfo = matchinfo;
	unsigned int ptr;
	int err;

	err = ipv6_find_hdr(skb, &ptr, NEXTHDR_FRAGMENT, NULL);
	if (err < 0) {
		if (err != -ENOENT)
			*hotdrop = true;
		return false;
	}

	fh = skb_header_pointer(skb, ptr, sizeof(_frag), &_frag);
	if (fh == NULL) {
		*hotdrop = true;
		return false;
	}

	pr_debug("INFO %04X ", fh->frag_off);
	pr_debug("OFFSET %04X ", ntohs(fh->frag_off) & ~0x7);
	pr_debug("RES %02X %04X", fh->reserved, ntohs(fh->frag_off) & 0x6);
	pr_debug("MF %04X ", fh->frag_off & htons(IP6_MF));
	pr_debug("ID %u %08X\n", ntohl(fh->identification),
		 ntohl(fh->identification));

	pr_debug("IPv6 FRAG id %02X ",
		 id_match(fraginfo->ids[0], fraginfo->ids[1],
			  ntohl(fh->identification),
			  !!(fraginfo->invflags & IP6T_FRAG_INV_IDS)));
	pr_debug("res %02X %02X%04X %02X ",
		 fraginfo->flags & IP6T_FRAG_RES, fh->reserved,
		 ntohs(fh->frag_off) & 0x6,
		 !((fraginfo->flags & IP6T_FRAG_RES)
		   && (fh->reserved || (ntohs(fh->frag_off) & 0x06))));
	pr_debug("first %02X %02X %02X ",
		 fraginfo->flags & IP6T_FRAG_FST,
		 ntohs(fh->frag_off) & ~0x7,
		 !((fraginfo->flags & IP6T_FRAG_FST)
		   && (ntohs(fh->frag_off) & ~0x7)));
	pr_debug("mf %02X %02X %02X ",
		 fraginfo->flags & IP6T_FRAG_MF,
		 ntohs(fh->frag_off) & IP6_MF,
		 !((fraginfo->flags & IP6T_FRAG_MF)
		   && !((ntohs(fh->frag_off) & IP6_MF))));
	pr_debug("last %02X %02X %02X\n",
		 fraginfo->flags & IP6T_FRAG_NMF,
		 ntohs(fh->frag_off) & IP6_MF,
		 !((fraginfo->flags & IP6T_FRAG_NMF)
		   && (ntohs(fh->frag_off) & IP6_MF)));

	return (fh != NULL)
	       &&
	       id_match(fraginfo->ids[0], fraginfo->ids[1],
			ntohl(fh->identification),
			!!(fraginfo->invflags & IP6T_FRAG_INV_IDS))
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
static bool
checkentry(const char *tablename,
	   const void *ip,
	   const struct xt_match *match,
	   void *matchinfo,
	   unsigned int hook_mask)
{
	const struct ip6t_frag *fraginfo = matchinfo;

	if (fraginfo->invflags & ~IP6T_FRAG_INV_MASK) {
		pr_debug("ip6t_frag: unknown flags %X\n", fraginfo->invflags);
		return false;
	}
	return true;
}

static struct xt_match frag_match __read_mostly = {
	.name		= "frag",
	.family		= AF_INET6,
	.match		= match,
	.matchsize	= sizeof(struct ip6t_frag),
	.checkentry	= checkentry,
	.me		= THIS_MODULE,
};

static int __init ip6t_frag_init(void)
{
	return xt_register_match(&frag_match);
}

static void __exit ip6t_frag_fini(void)
{
	xt_unregister_match(&frag_match);
}

module_init(ip6t_frag_init);
module_exit(ip6t_frag_fini);
