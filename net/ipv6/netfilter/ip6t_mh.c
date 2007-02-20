/*
 * Copyright (C)2006 USAGI/WIDE Project
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Author:
 *	Masahide NAKAMURA @USAGI <masahide.nakamura.cz@hitachi.com>
 *
 * Based on net/netfilter/xt_tcpudp.c
 *
 */
#include <linux/types.h>
#include <linux/module.h>
#include <net/ip.h>
#include <linux/ipv6.h>
#include <net/ipv6.h>
#include <net/mip6.h>

#include <linux/netfilter/x_tables.h>
#include <linux/netfilter_ipv6/ip6t_mh.h>

MODULE_DESCRIPTION("ip6t_tables match for MH");
MODULE_LICENSE("GPL");

#ifdef DEBUG_IP_FIREWALL_USER
#define duprintf(format, args...) printk(format , ## args)
#else
#define duprintf(format, args...)
#endif

/* Returns 1 if the type is matched by the range, 0 otherwise */
static inline int
type_match(u_int8_t min, u_int8_t max, u_int8_t type, int invert)
{
	int ret;

	ret = (type >= min && type <= max) ^ invert;
	return ret;
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
	struct ip6_mh _mh, *mh;
	const struct ip6t_mh *mhinfo = matchinfo;

	/* Must not be a fragment. */
	if (offset)
		return 0;

	mh = skb_header_pointer(skb, protoff, sizeof(_mh), &_mh);
	if (mh == NULL) {
		/* We've been asked to examine this packet, and we
		   can't.  Hence, no choice but to drop. */
		duprintf("Dropping evil MH tinygram.\n");
		*hotdrop = 1;
		return 0;
	}

	if (mh->ip6mh_proto != IPPROTO_NONE) {
		duprintf("Dropping invalid MH Payload Proto: %u\n",
			 mh->ip6mh_proto);
		*hotdrop = 1;
		return 0;
	}

	return type_match(mhinfo->types[0], mhinfo->types[1], mh->ip6mh_type,
			  !!(mhinfo->invflags & IP6T_MH_INV_TYPE));
}

/* Called when user tries to insert an entry of this type. */
static int
mh_checkentry(const char *tablename,
	      const void *entry,
	      const struct xt_match *match,
	      void *matchinfo,
	      unsigned int hook_mask)
{
	const struct ip6t_mh *mhinfo = matchinfo;

	/* Must specify no unknown invflags */
	return !(mhinfo->invflags & ~IP6T_MH_INV_MASK);
}

static struct xt_match mh_match = {
	.name		= "mh",
	.family		= AF_INET6,
	.checkentry	= mh_checkentry,
	.match		= match,
	.matchsize	= sizeof(struct ip6t_mh),
	.proto		= IPPROTO_MH,
	.me		= THIS_MODULE,
};

static int __init ip6t_mh_init(void)
{
	return xt_register_match(&mh_match);
}

static void __exit ip6t_mh_fini(void)
{
	xt_unregister_match(&mh_match);
}

module_init(ip6t_mh_init);
module_exit(ip6t_mh_fini);
