/* SPDX-License-Identifier: GPL-2.0 */
/*
 *		INETPEER - A storage for permanent information about peers
 *
 *  Authors:	Andrey V. Savochkin <saw@msu.ru>
 */

#ifndef _NET_INETPEER_H
#define _NET_INETPEER_H

#include <linux/types.h>
#include <linux/init.h>
#include <linux/jiffies.h>
#include <linux/spinlock.h>
#include <linux/rtnetlink.h>
#include <net/ipv6.h>
#include <linux/atomic.h>

/* IPv4 address key for cache lookups */
struct ipv4_addr_key {
	__be32	addr;
	int	vif;
};

#define INETPEER_MAXKEYSZ   (sizeof(struct in6_addr) / sizeof(u32))

struct inetpeer_addr {
	union {
		struct ipv4_addr_key	a4;
		struct in6_addr		a6;
		u32			key[INETPEER_MAXKEYSZ];
	};
	__u16				family;
};

struct inet_peer {
	struct rb_node		rb_node;
	struct inetpeer_addr	daddr;

	u32			metrics[RTAX_MAX];
	u32			rate_tokens;	/* rate limiting for ICMP */
	u32			n_redirects;
	unsigned long		rate_last;
	/*
	 * Once inet_peer is queued for deletion (refcnt == 0), following field
	 * is not available: rid
	 * We can share memory with rcu_head to help keep inet_peer small.
	 */
	union {
		struct {
			atomic_t			rid;		/* Frag reception counter */
		};
		struct rcu_head         rcu;
	};

	/* following fields might be frequently dirtied */
	__u32			dtime;	/* the time of last use of not referenced entries */
	refcount_t		refcnt;
};

struct inet_peer_base {
	struct rb_root		rb_root;
	seqlock_t		lock;
	int			total;
};

void inet_peer_base_init(struct inet_peer_base *);

void inet_initpeers(void) __init;

#define INETPEER_METRICS_NEW	(~(u32) 0)

static inline void inetpeer_set_addr_v4(struct inetpeer_addr *iaddr, __be32 ip)
{
	iaddr->a4.addr = ip;
	iaddr->a4.vif = 0;
	iaddr->family = AF_INET;
}

static inline __be32 inetpeer_get_addr_v4(struct inetpeer_addr *iaddr)
{
	return iaddr->a4.addr;
}

static inline void inetpeer_set_addr_v6(struct inetpeer_addr *iaddr,
					struct in6_addr *in6)
{
	iaddr->a6 = *in6;
	iaddr->family = AF_INET6;
}

static inline struct in6_addr *inetpeer_get_addr_v6(struct inetpeer_addr *iaddr)
{
	return &iaddr->a6;
}

/* can be called with or without local BH being disabled */
struct inet_peer *inet_getpeer(struct inet_peer_base *base,
			       const struct inetpeer_addr *daddr,
			       int create);

static inline struct inet_peer *inet_getpeer_v4(struct inet_peer_base *base,
						__be32 v4daddr,
						int vif, int create)
{
	struct inetpeer_addr daddr;

	daddr.a4.addr = v4daddr;
	daddr.a4.vif = vif;
	daddr.family = AF_INET;
	return inet_getpeer(base, &daddr, create);
}

static inline struct inet_peer *inet_getpeer_v6(struct inet_peer_base *base,
						const struct in6_addr *v6daddr,
						int create)
{
	struct inetpeer_addr daddr;

	daddr.a6 = *v6daddr;
	daddr.family = AF_INET6;
	return inet_getpeer(base, &daddr, create);
}

static inline int inetpeer_addr_cmp(const struct inetpeer_addr *a,
				    const struct inetpeer_addr *b)
{
	int i, n;

	if (a->family == AF_INET)
		n = sizeof(a->a4) / sizeof(u32);
	else
		n = sizeof(a->a6) / sizeof(u32);

	for (i = 0; i < n; i++) {
		if (a->key[i] == b->key[i])
			continue;
		if (a->key[i] < b->key[i])
			return -1;
		return 1;
	}

	return 0;
}

/* can be called from BH context or outside */
void inet_putpeer(struct inet_peer *p);
bool inet_peer_xrlim_allow(struct inet_peer *peer, int timeout);

void inetpeer_invalidate_tree(struct inet_peer_base *);

#endif /* _NET_INETPEER_H */
