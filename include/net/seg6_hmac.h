/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *  SR-IPv6 implementation
 *
 *  Author:
 *  David Lebrun <david.lebrun@uclouvain.be>
 */

#ifndef _NET_SEG6_HMAC_H
#define _NET_SEG6_HMAC_H

#include <crypto/sha1.h>
#include <crypto/sha2.h>
#include <net/flow.h>
#include <net/ip6_fib.h>
#include <net/sock.h>
#include <linux/ip.h>
#include <linux/ipv6.h>
#include <linux/route.h>
#include <net/seg6.h>
#include <linux/seg6_hmac.h>
#include <linux/rhashtable-types.h>

#define SEG6_HMAC_RING_SIZE		256

struct seg6_hmac_info {
	struct rhash_head node;
	struct rcu_head rcu;

	u32 hmackeyid;
	/* The raw key, kept only so it can be returned back to userspace */
	char secret[SEG6_HMAC_SECRET_LEN];
	u8 slen;
	u8 alg_id;
	/* The prepared key, which the calculations actually use */
	union {
		struct hmac_sha1_key sha1;
		struct hmac_sha256_key sha256;
	} key;
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
#ifdef CONFIG_IPV6_SEG6_HMAC
extern int seg6_hmac_net_init(struct net *net);
extern void seg6_hmac_net_exit(struct net *net);
#else
static inline int seg6_hmac_net_init(struct net *net) { return 0; }
static inline void seg6_hmac_net_exit(struct net *net) {}
#endif

#endif
