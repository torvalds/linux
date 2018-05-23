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
#include <net/netfilter/nf_nat_l4proto.h>

static bool unknown_in_range(const struct nf_conntrack_tuple *tuple,
			     enum nf_nat_manip_type manip_type,
			     const union nf_conntrack_man_proto *min,
			     const union nf_conntrack_man_proto *max)
{
	return true;
}

static void unknown_unique_tuple(const struct nf_nat_l3proto *l3proto,
				 struct nf_conntrack_tuple *tuple,
				 const struct nf_nat_range2 *range,
				 enum nf_nat_manip_type maniptype,
				 const struct nf_conn *ct)
{
	/* Sorry: we can't help you; if it's not unique, we can't frob
	 * anything.
	 */
	return;
}

static bool
unknown_manip_pkt(struct sk_buff *skb,
		  const struct nf_nat_l3proto *l3proto,
		  unsigned int iphdroff, unsigned int hdroff,
		  const struct nf_conntrack_tuple *tuple,
		  enum nf_nat_manip_type maniptype)
{
	return true;
}

const struct nf_nat_l4proto nf_nat_l4proto_unknown = {
	.manip_pkt		= unknown_manip_pkt,
	.in_range		= unknown_in_range,
	.unique_tuple		= unknown_unique_tuple,
};
