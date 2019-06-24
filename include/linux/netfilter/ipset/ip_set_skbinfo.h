/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _IP_SET_SKBINFO_H
#define _IP_SET_SKBINFO_H

/* Copyright (C) 2015 Jozsef Kadlecsik <kadlec@blackhole.kfki.hu>
 */

#ifdef __KERNEL__

static inline void
ip_set_get_skbinfo(struct ip_set_skbinfo *skbinfo,
		   const struct ip_set_ext *ext,
		   struct ip_set_ext *mext, u32 flags)
{
	mext->skbinfo = *skbinfo;
}

static inline bool
ip_set_put_skbinfo(struct sk_buff *skb, const struct ip_set_skbinfo *skbinfo)
{
	/* Send nonzero parameters only */
	return ((skbinfo->skbmark || skbinfo->skbmarkmask) &&
		nla_put_net64(skb, IPSET_ATTR_SKBMARK,
			      cpu_to_be64((u64)skbinfo->skbmark << 32 |
					  skbinfo->skbmarkmask),
			      IPSET_ATTR_PAD)) ||
	       (skbinfo->skbprio &&
		nla_put_net32(skb, IPSET_ATTR_SKBPRIO,
			      cpu_to_be32(skbinfo->skbprio))) ||
	       (skbinfo->skbqueue &&
		nla_put_net16(skb, IPSET_ATTR_SKBQUEUE,
			      cpu_to_be16(skbinfo->skbqueue)));
}

static inline void
ip_set_init_skbinfo(struct ip_set_skbinfo *skbinfo,
		    const struct ip_set_ext *ext)
{
	*skbinfo = ext->skbinfo;
}

#endif /* __KERNEL__ */
#endif /* _IP_SET_SKBINFO_H */
