/* The "unknown" protocol.  This is what is used for protocols we
 * don't understand.  It's returned by ip_ct_find_proto().
 */

/* (C) 1999-2001 Paul `Rusty' Russell
 * (C) 2002-2006 Netfilter Core Team <coreteam@netfilter.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/types.h>
#include <linux/init.h>

#include <linux/netfilter.h>
#include <net/netfilter/nf_nat.h>
#include <net/netfilter/nf_nat_rule.h>
#include <net/netfilter/nf_nat_protocol.h>

static int unknown_in_range(const struct nf_conntrack_tuple *tuple,
			    enum nf_nat_manip_type manip_type,
			    const union nf_conntrack_man_proto *min,
			    const union nf_conntrack_man_proto *max)
{
	return 1;
}

static int unknown_unique_tuple(struct nf_conntrack_tuple *tuple,
				const struct nf_nat_range *range,
				enum nf_nat_manip_type maniptype,
				const struct nf_conn *ct)
{
	/* Sorry: we can't help you; if it's not unique, we can't frob
	   anything. */
	return 0;
}

static int
unknown_manip_pkt(struct sk_buff **pskb,
		  unsigned int iphdroff,
		  const struct nf_conntrack_tuple *tuple,
		  enum nf_nat_manip_type maniptype)
{
	return 1;
}

struct nf_nat_protocol nf_nat_unknown_protocol = {
	.name			= "unknown",
	/* .me isn't set: getting a ref to this cannot fail. */
	.manip_pkt		= unknown_manip_pkt,
	.in_range		= unknown_in_range,
	.unique_tuple		= unknown_unique_tuple,
};
