/* ipv6header match - matches IPv6 packets based
   on whether they contain certain headers */

/* Original idea: Brad Chapman
 * Rewritten by: Andras Kis-Szabo <kisza@sch.bme.hu> */

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
#include <linux/netfilter_ipv6/ip6t_ipv6header.h>

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Xtables: IPv6 header types match");
MODULE_AUTHOR("Andras Kis-Szabo <kisza@sch.bme.hu>");

static bool
ipv6header_mt6(const struct sk_buff *skb, const struct xt_match_param *par)
{
	const struct ip6t_ipv6header_info *info = par->matchinfo;
	unsigned int temp;
	int len;
	u8 nexthdr;
	unsigned int ptr;

	/* Make sure this isn't an evil packet */

	/* type of the 1st exthdr */
	nexthdr = ipv6_hdr(skb)->nexthdr;
	/* pointer to the 1st exthdr */
	ptr = sizeof(struct ipv6hdr);
	/* available length */
	len = skb->len - ptr;
	temp = 0;

	while (ip6t_ext_hdr(nexthdr)) {
		const struct ipv6_opt_hdr *hp;
		struct ipv6_opt_hdr _hdr;
		int hdrlen;

		/* Is there enough space for the next ext header? */
		if (len < (int)sizeof(struct ipv6_opt_hdr))
			return false;
		/* No more exthdr -> evaluate */
		if (nexthdr == NEXTHDR_NONE) {
			temp |= MASK_NONE;
			break;
		}
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
			hdrlen = (hp->hdrlen + 2) << 2;
		else
			hdrlen = ipv6_optlen(hp);

		/* set the flag */
		switch (nexthdr) {
		case NEXTHDR_HOP:
			temp |= MASK_HOPOPTS;
			break;
		case NEXTHDR_ROUTING:
			temp |= MASK_ROUTING;
			break;
		case NEXTHDR_FRAGMENT:
			temp |= MASK_FRAGMENT;
			break;
		case NEXTHDR_AUTH:
			temp |= MASK_AH;
			break;
		case NEXTHDR_DEST:
			temp |= MASK_DSTOPTS;
			break;
		default:
			return false;
			break;
		}

		nexthdr = hp->nexthdr;
		len -= hdrlen;
		ptr += hdrlen;
		if (ptr > skb->len)
			break;
	}

	if (nexthdr != NEXTHDR_NONE && nexthdr != NEXTHDR_ESP)
		temp |= MASK_PROTO;

	if (info->modeflag)
		return !((temp ^ info->matchflags ^ info->invflags)
			 & info->matchflags);
	else {
		if (info->invflags)
			return temp != info->matchflags;
		else
			return temp == info->matchflags;
	}
}

static bool ipv6header_mt6_check(const struct xt_mtchk_param *par)
{
	const struct ip6t_ipv6header_info *info = par->matchinfo;

	/* invflags is 0 or 0xff in hard mode */
	if ((!info->modeflag) && info->invflags != 0x00 &&
	    info->invflags != 0xFF)
		return false;

	return true;
}

static struct xt_match ipv6header_mt6_reg __read_mostly = {
	.name		= "ipv6header",
	.family		= NFPROTO_IPV6,
	.match		= ipv6header_mt6,
	.matchsize	= sizeof(struct ip6t_ipv6header_info),
	.checkentry	= ipv6header_mt6_check,
	.destroy	= NULL,
	.me		= THIS_MODULE,
};

static int __init ipv6header_mt6_init(void)
{
	return xt_register_match(&ipv6header_mt6_reg);
}

static void __exit ipv6header_mt6_exit(void)
{
	xt_unregister_match(&ipv6header_mt6_reg);
}

module_init(ipv6header_mt6_init);
module_exit(ipv6header_mt6_exit);
