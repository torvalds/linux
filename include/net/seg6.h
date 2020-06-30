/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *  SR-IPv6 implementation
 *
 *  Author:
 *  David Lebrun <david.lebrun@uclouvain.be>
 */

#ifndef _NET_SEG6_H
#define _NET_SEG6_H

#include <linux/net.h>
#include <linux/ipv6.h>
#include <linux/seg6.h>
#include <linux/rhashtable-types.h>

static inline void update_csum_diff4(struct sk_buff *skb, __be32 from,
				     __be32 to)
{
	__be32 diff[] = { ~from, to };

	skb->csum = ~csum_partial((char *)diff, sizeof(diff), ~skb->csum);
}

static inline void update_csum_diff16(struct sk_buff *skb, __be32 *from,
				      __be32 *to)
{
	__be32 diff[] = {
		~from[0], ~from[1], ~from[2], ~from[3],
		to[0], to[1], to[2], to[3],
	};

	skb->csum = ~csum_partial((char *)diff, sizeof(diff), ~skb->csum);
}

struct seg6_pernet_data {
	struct mutex lock;
	struct in6_addr __rcu *tun_src;
#ifdef CONFIG_IPV6_SEG6_HMAC
	struct rhashtable hmac_infos;
#endif
};

static inline struct seg6_pernet_data *seg6_pernet(struct net *net)
{
#if IS_ENABLED(CONFIG_IPV6)
	return net->ipv6.seg6_data;
#else
	return NULL;
#endif
}

extern int seg6_init(void);
extern void seg6_exit(void);
extern int seg6_iptunnel_init(void);
extern void seg6_iptunnel_exit(void);
extern int seg6_local_init(void);
extern void seg6_local_exit(void);

extern bool seg6_validate_srh(struct ipv6_sr_hdr *srh, int len, bool reduced);
extern int seg6_do_srh_encap(struct sk_buff *skb, struct ipv6_sr_hdr *osrh,
			     int proto);
extern int seg6_do_srh_inline(struct sk_buff *skb, struct ipv6_sr_hdr *osrh);
extern int seg6_lookup_nexthop(struct sk_buff *skb, struct in6_addr *nhaddr,
			       u32 tbl_id);
#endif
