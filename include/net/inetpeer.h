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

struct inetpeer_addr_base {
	union {
		__be32			a4;
		__be32			a6[4];
		struct in6_addr		in6;
	};
};

struct inetpeer_addr {
	struct inetpeer_addr_base	addr;
	__u16				family;
};

struct inet_peer {
	/* group together avl_left,avl_right,v4daddr to speedup lookups */
	struct inet_peer __rcu	*avl_left, *avl_right;
	struct inetpeer_addr	daddr;
	__u32			avl_height;

	u32			metrics[RTAX_MAX];
	u32			rate_tokens;	/* rate limiting for ICMP */
	unsigned long		rate_last;
	union {
		struct list_head	gc_list;
		struct rcu_head     gc_rcu;
	};
	/*
	 * Once inet_peer is queued for deletion (refcnt == -1), following field
	 * is not available: rid
	 * We can share memory with rcu_head to help keep inet_peer small.
	 */
	union {
		struct {
			atomic_t			rid;		/* Frag reception counter */
		};
		struct rcu_head         rcu;
		struct inet_peer	*gc_next;
	};

	/* following fields might be frequently dirtied */
	__u32			dtime;	/* the time of last use of not referenced entries */
	atomic_t		refcnt;
};

struct inet_peer_base {
	struct inet_peer __rcu	*root;
	seqlock_t		lock;
	int			total;
};

void inet_peer_base_init(struct inet_peer_base *);

void inet_initpeers(void) __init;

#define INETPEER_METRICS_NEW	(~(u32) 0)

static inline void inetpeer_set_addr_v4(struct inetpeer_addr *iaddr, __be32 ip)
{
	iaddr->addr.a4 = ip;
	iaddr->family = AF_INET;
}

static inline __be32 inetpeer_get_addr_v4(struct inetpeer_addr *iaddr)
{
	return iaddr->addr.a4;
}

static inline void inetpeer_set_addr_v6(struct inetpeer_addr *iaddr,
					struct in6_addr *in6)
{
	iaddr->addr.in6 = *in6;
	iaddr->family = AF_INET6;
}

static inline struct in6_addr *inetpeer_get_addr_v6(struct inetpeer_addr *iaddr)
{
	return &iaddr->addr.in6;
}

/* can be called with or without local BH being disabled */
struct inet_peer *inet_getpeer(struct inet_peer_base *base,
			       const struct inetpeer_addr *daddr,
			       int create);

static inline struct inet_peer *inet_getpeer_v4(struct inet_peer_base *base,
						__be32 v4daddr,
						int create)
{
	struct inetpeer_addr daddr;

	daddr.addr.a4 = v4daddr;
	daddr.family = AF_INET;
	return inet_getpeer(base, &daddr, create);
}

static inline struct inet_peer *inet_getpeer_v6(struct inet_peer_base *base,
						const struct in6_addr *v6daddr,
						int create)
{
	struct inetpeer_addr daddr;

	daddr.addr.in6 = *v6daddr;
	daddr.family = AF_INET6;
	return inet_getpeer(base, &daddr, create);
}

static inline int inetpeer_addr_cmp(const struct inetpeer_addr *a,
				    const struct inetpeer_addr *b)
{
	int i, n = (a->family == AF_INET ? 1 : 4);

	for (i = 0; i < n; i++) {
		if (a->addr.a6[i] == b->addr.a6[i])
			continue;
		if ((__force u32)a->addr.a6[i] < (__force u32)b->addr.a6[i])
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
