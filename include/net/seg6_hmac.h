/*
 *  SR-IPv6 implementation
 *
 *  Author:
 *  David Lebrun <david.lebrun@uclouvain.be>
 *
 *
 *  This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */

#ifndef _NET_SEG6_HMAC_H
#define _NET_SEG6_HMAC_H

#include <net/flow.h>
#include <net/ip6_fib.h>
#include <net/sock.h>
#include <linux/ip.h>
#include <linux/ipv6.h>
#include <linux/route.h>
#include <net/seg6.h>
#include <linux/seg6_hmac.h>
#include <linux/rhashtable-types.h>

#define SEG6_HMAC_MAX_DIGESTSIZE	160
#define SEG6_HMAC_RING_SIZE		256

struct seg6_hmac_info {
	struct rhash_head node;
	struct rcu_head rcu;

	u32 hmackeyid;
	char secret[SEG6_HMAC_SECRET_LEN];
	u8 slen;
	u8 alg_id;
};

struct seg6_hmac_algo {
	u8 alg_id;
	char name[64];
	struct crypto_shash * __percpu *tfms;
	struct shash_desc * __percpu *shashs;
};

extern int seg6_hmac_compute(struct seg6_hmac_info *hinfo,
			     struct ipv6_sr_hdr *hdr, struct in6_addr *saddr,
			     u8 *output);
extern struct seg6_hmac_info *seg6_hmac_info_lookup(struct net *net, u32 key);
extern int seg6_hmac_info_add(struct net *net, u32 key,
			      struct seg6_hmac_info *hinfo);
extern int seg6_hmac_info_del(struct net *net, u32 key);
extern int seg6_push_hmac(struct net *net, struct in6_addr *saddr,
			  struct ipv6_sr_hdr *srh);
extern bool seg6_hmac_validate_skb(struct sk_buff *skb);
extern int seg6_hmac_init(void);
extern void seg6_hmac_exit(void);
extern int seg6_hmac_net_init(struct net *net);
extern void seg6_hmac_net_exit(struct net *net);

#endif
