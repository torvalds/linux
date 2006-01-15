/* Kernel module to match one of a list of TCP/UDP ports: ports are in
   the same place so we can treat them as equal. */

/* (C) 1999-2001 Paul `Rusty' Russell
 * (C) 2002-2004 Netfilter Core Team <coreteam@netfilter.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/udp.h>
#include <linux/skbuff.h>
#include <linux/in.h>

#include <linux/netfilter_ipv6/ip6t_multiport.h>
#include <linux/netfilter_ipv6/ip6_tables.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Netfilter Core Team <coreteam@netfilter.org>");
MODULE_DESCRIPTION("ip6tables match for multiple ports");

#if 0
#define duprintf(format, args...) printk(format , ## args)
#else
#define duprintf(format, args...)
#endif

/* Returns 1 if the port is matched by the test, 0 otherwise. */
static inline int
ports_match(const u_int16_t *portlist, enum ip6t_multiport_flags flags,
	    u_int8_t count, u_int16_t src, u_int16_t dst)
{
	unsigned int i;
	for (i=0; i<count; i++) {
		if (flags != IP6T_MULTIPORT_DESTINATION
		    && portlist[i] == src)
			return 1;

		if (flags != IP6T_MULTIPORT_SOURCE
		    && portlist[i] == dst)
			return 1;
	}

	return 0;
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
	u16 _ports[2], *pptr;
	const struct ip6t_multiport *multiinfo = matchinfo;

	/* Must not be a fragment. */
	if (offset)
		return 0;

	/* Must be big enough to read ports (both UDP and TCP have
	   them at the start). */
	pptr = skb_header_pointer(skb, protoff, sizeof(_ports), &_ports[0]);
	if (pptr == NULL) {
		/* We've been asked to examine this packet, and we
		 * can't.  Hence, no choice but to drop.
		 */
		duprintf("ip6t_multiport:"
			 " Dropping evil offset=0 tinygram.\n");
		*hotdrop = 1;
		return 0;
	}

	return ports_match(multiinfo->ports,
			   multiinfo->flags, multiinfo->count,
			   ntohs(pptr[0]), ntohs(pptr[1]));
}

/* Called when user tries to insert an entry of this type. */
static int
checkentry(const char *tablename,
	   const void *info,
	   void *matchinfo,
	   unsigned int matchsize,
	   unsigned int hook_mask)
{
	const struct ip6t_ip6 *ip = info;
	const struct ip6t_multiport *multiinfo = matchinfo;

	if (matchsize != IP6T_ALIGN(sizeof(struct ip6t_multiport)))
		return 0;

	/* Must specify proto == TCP/UDP, no unknown flags or bad count */
	return (ip->proto == IPPROTO_TCP || ip->proto == IPPROTO_UDP)
		&& !(ip->invflags & IP6T_INV_PROTO)
		&& matchsize == IP6T_ALIGN(sizeof(struct ip6t_multiport))
		&& (multiinfo->flags == IP6T_MULTIPORT_SOURCE
		    || multiinfo->flags == IP6T_MULTIPORT_DESTINATION
		    || multiinfo->flags == IP6T_MULTIPORT_EITHER)
		&& multiinfo->count <= IP6T_MULTI_PORTS;
}

static struct ip6t_match multiport_match = {
	.name		= "multiport",
	.match		= &match,
	.checkentry	= &checkentry,
	.me		= THIS_MODULE,
};

static int __init init(void)
{
	return ip6t_register_match(&multiport_match);
}

static void __exit fini(void)
{
	ip6t_unregister_match(&multiport_match);
}

module_init(init);
module_exit(fini);
