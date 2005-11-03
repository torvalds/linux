/* The "unknown" protocol.  This is what is used for protocols we
 * don't understand.  It's returned by ip_ct_find_proto().
 */

/* (C) 1999-2001 Paul `Rusty' Russell
 * (C) 2002-2004 Netfilter Core Team <coreteam@netfilter.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/types.h>
#include <linux/init.h>
#include <linux/netfilter.h>
#include <linux/if.h>

#include <linux/netfilter_ipv4/ip_nat.h>
#include <linux/netfilter_ipv4/ip_nat_rule.h>
#include <linux/netfilter_ipv4/ip_nat_protocol.h>

static int unknown_in_range(const struct ip_conntrack_tuple *tuple,
			    enum ip_nat_manip_type manip_type,
			    const union ip_conntrack_manip_proto *min,
			    const union ip_conntrack_manip_proto *max)
{
	return 1;
}

static int unknown_unique_tuple(struct ip_conntrack_tuple *tuple,
				const struct ip_nat_range *range,
				enum ip_nat_manip_type maniptype,
				const struct ip_conntrack *conntrack)
{
	/* Sorry: we can't help you; if it's not unique, we can't frob
	   anything. */
	return 0;
}

static int
unknown_manip_pkt(struct sk_buff **pskb,
		  unsigned int iphdroff,
		  const struct ip_conntrack_tuple *tuple,
		  enum ip_nat_manip_type maniptype)
{
	return 1;
}

static unsigned int
unknown_print(char *buffer,
	      const struct ip_conntrack_tuple *match,
	      const struct ip_conntrack_tuple *mask)
{
	return 0;
}

static unsigned int
unknown_print_range(char *buffer, const struct ip_nat_range *range)
{
	return 0;
}

struct ip_nat_protocol ip_nat_unknown_protocol = {
	.name			= "unknown",
	/* .me isn't set: getting a ref to this cannot fail. */
	.manip_pkt		= unknown_manip_pkt,
	.in_range		= unknown_in_range,
	.unique_tuple		= unknown_unique_tuple,
	.print			= unknown_print,
	.print_range		= unknown_print_range
};
