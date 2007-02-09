/* Kernel module to match ROUTING parameters. */

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

#include <asm/byteorder.h>

#include <linux/netfilter/x_tables.h>
#include <linux/netfilter_ipv6/ip6_tables.h>
#include <linux/netfilter_ipv6/ip6t_rt.h>

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("IPv6 RT match");
MODULE_AUTHOR("Andras Kis-Szabo <kisza@sch.bme.hu>");

#if 0
#define DEBUGP printk
#else
#define DEBUGP(format, args...)
#endif

/* Returns 1 if the id is matched by the range, 0 otherwise */
static inline int
segsleft_match(u_int32_t min, u_int32_t max, u_int32_t id, int invert)
{
	int r = 0;
	DEBUGP("rt segsleft_match:%c 0x%x <= 0x%x <= 0x%x",
	       invert ? '!' : ' ', min, id, max);
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
	struct ipv6_rt_hdr _route, *rh;
	const struct ip6t_rt *rtinfo = matchinfo;
	unsigned int temp;
	unsigned int ptr;
	unsigned int hdrlen = 0;
	unsigned int ret = 0;
	struct in6_addr *ap, _addr;
	int err;

	err = ipv6_find_hdr(skb, &ptr, NEXTHDR_ROUTING, NULL);
	if (err < 0) {
		if (err != -ENOENT)
			*hotdrop = 1;
		return 0;
	}

	rh = skb_header_pointer(skb, ptr, sizeof(_route), &_route);
	if (rh == NULL) {
		*hotdrop = 1;
		return 0;
	}

	hdrlen = ipv6_optlen(rh);
	if (skb->len - ptr < hdrlen) {
		/* Pcket smaller than its length field */
		return 0;
	}

	DEBUGP("IPv6 RT LEN %u %u ", hdrlen, rh->hdrlen);
	DEBUGP("TYPE %04X ", rh->type);
	DEBUGP("SGS_LEFT %u %02X\n", rh->segments_left, rh->segments_left);

	DEBUGP("IPv6 RT segsleft %02X ",
	       (segsleft_match(rtinfo->segsleft[0], rtinfo->segsleft[1],
			       rh->segments_left,
			       !!(rtinfo->invflags & IP6T_RT_INV_SGS))));
	DEBUGP("type %02X %02X %02X ",
	       rtinfo->rt_type, rh->type,
	       (!(rtinfo->flags & IP6T_RT_TYP) ||
		((rtinfo->rt_type == rh->type) ^
		 !!(rtinfo->invflags & IP6T_RT_INV_TYP))));
	DEBUGP("len %02X %04X %02X ",
	       rtinfo->hdrlen, hdrlen,
	       (!(rtinfo->flags & IP6T_RT_LEN) ||
		((rtinfo->hdrlen == hdrlen) ^
		 !!(rtinfo->invflags & IP6T_RT_INV_LEN))));
	DEBUGP("res %02X %02X %02X ",
	       (rtinfo->flags & IP6T_RT_RES),
	       ((struct rt0_hdr *)rh)->reserved,
	       !((rtinfo->flags & IP6T_RT_RES) &&
		 (((struct rt0_hdr *)rh)->reserved)));

	ret = (rh != NULL)
	      &&
	      (segsleft_match(rtinfo->segsleft[0], rtinfo->segsleft[1],
			      rh->segments_left,
			      !!(rtinfo->invflags & IP6T_RT_INV_SGS)))
	      &&
	      (!(rtinfo->flags & IP6T_RT_LEN) ||
	       ((rtinfo->hdrlen == hdrlen) ^
		!!(rtinfo->invflags & IP6T_RT_INV_LEN)))
	      &&
	      (!(rtinfo->flags & IP6T_RT_TYP) ||
	       ((rtinfo->rt_type == rh->type) ^
		!!(rtinfo->invflags & IP6T_RT_INV_TYP)));

	if (ret && (rtinfo->flags & IP6T_RT_RES)) {
		u_int32_t *rp, _reserved;
		rp = skb_header_pointer(skb,
					ptr + offsetof(struct rt0_hdr,
						       reserved),
					sizeof(_reserved),
					&_reserved);

		ret = (*rp == 0);
	}

	DEBUGP("#%d ", rtinfo->addrnr);
	if (!(rtinfo->flags & IP6T_RT_FST)) {
		return ret;
	} else if (rtinfo->flags & IP6T_RT_FST_NSTRICT) {
		DEBUGP("Not strict ");
		if (rtinfo->addrnr > (unsigned int)((hdrlen - 8) / 16)) {
			DEBUGP("There isn't enough space\n");
			return 0;
		} else {
			unsigned int i = 0;

			DEBUGP("#%d ", rtinfo->addrnr);
			for (temp = 0;
			     temp < (unsigned int)((hdrlen - 8) / 16);
			     temp++) {
				ap = skb_header_pointer(skb,
							ptr
							+ sizeof(struct rt0_hdr)
							+ temp * sizeof(_addr),
							sizeof(_addr),
							&_addr);

				BUG_ON(ap == NULL);

				if (ipv6_addr_equal(ap, &rtinfo->addrs[i])) {
					DEBUGP("i=%d temp=%d;\n", i, temp);
					i++;
				}
				if (i == rtinfo->addrnr)
					break;
			}
			DEBUGP("i=%d #%d\n", i, rtinfo->addrnr);
			if (i == rtinfo->addrnr)
				return ret;
			else
				return 0;
		}
	} else {
		DEBUGP("Strict ");
		if (rtinfo->addrnr > (unsigned int)((hdrlen - 8) / 16)) {
			DEBUGP("There isn't enough space\n");
			return 0;
		} else {
			DEBUGP("#%d ", rtinfo->addrnr);
			for (temp = 0; temp < rtinfo->addrnr; temp++) {
				ap = skb_header_pointer(skb,
							ptr
							+ sizeof(struct rt0_hdr)
							+ temp * sizeof(_addr),
							sizeof(_addr),
							&_addr);
				BUG_ON(ap == NULL);

				if (!ipv6_addr_equal(ap, &rtinfo->addrs[temp]))
					break;
			}
			DEBUGP("temp=%d #%d\n", temp, rtinfo->addrnr);
			if ((temp == rtinfo->addrnr) &&
			    (temp == (unsigned int)((hdrlen - 8) / 16)))
				return ret;
			else
				return 0;
		}
	}

	return 0;
}

/* Called when user tries to insert an entry of this type. */
static int
checkentry(const char *tablename,
	   const void *entry,
	   const struct xt_match *match,
	   void *matchinfo,
	   unsigned int hook_mask)
{
	const struct ip6t_rt *rtinfo = matchinfo;

	if (rtinfo->invflags & ~IP6T_RT_INV_MASK) {
		DEBUGP("ip6t_rt: unknown flags %X\n", rtinfo->invflags);
		return 0;
	}
	if ((rtinfo->flags & (IP6T_RT_RES | IP6T_RT_FST_MASK)) &&
	    (!(rtinfo->flags & IP6T_RT_TYP) ||
	     (rtinfo->rt_type != 0) ||
	     (rtinfo->invflags & IP6T_RT_INV_TYP))) {
		DEBUGP("`--rt-type 0' required before `--rt-0-*'");
		return 0;
	}

	return 1;
}

static struct xt_match rt_match = {
	.name		= "rt",
	.family		= AF_INET6,
	.match		= match,
	.matchsize	= sizeof(struct ip6t_rt),
	.checkentry	= checkentry,
	.me		= THIS_MODULE,
};

static int __init ip6t_rt_init(void)
{
	return xt_register_match(&rt_match);
}

static void __exit ip6t_rt_fini(void)
{
	xt_unregister_match(&rt_match);
}

module_init(ip6t_rt_init);
module_exit(ip6t_rt_fini);
