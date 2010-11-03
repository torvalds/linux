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
#include <asm/atomic.h>

struct inet_peer {
	/* group together avl_left,avl_right,v4daddr to speedup lookups */
	struct inet_peer __rcu	*avl_left, *avl_right;
	__be32			v4daddr;	/* peer's address */
	__u32			avl_height;
	struct list_head	unused;
	__u32			dtime;		/* the time of last use of not
						 * referenced entries */
	atomic_t		refcnt;
	/*
	 * Once inet_peer is queued for deletion (refcnt == -1), following fields
	 * are not available: rid, ip_id_count, tcp_ts, tcp_ts_stamp
	 * We can share memory with rcu_head to keep inet_peer small
	 * (less then 64 bytes)
	 */
	union {
		struct {
			atomic_t	rid;		/* Frag reception counter */
			atomic_t	ip_id_count;	/* IP ID for the next packet */
			__u32		tcp_ts;
			__u32		tcp_ts_stamp;
		};
		struct rcu_head         rcu;
	};
};

void			inet_initpeers(void) __init;

/* can be called with or without local BH being disabled */
struct inet_peer	*inet_getpeer(__be32 daddr, int create);

/* can be called from BH context or outside */
extern void inet_putpeer(struct inet_peer *p);

/*
 * temporary check to make sure we dont access rid, ip_id_count, tcp_ts,
 * tcp_ts_stamp if no refcount is taken on inet_peer
 */
static inline void inet_peer_refcheck(const struct inet_peer *p)
{
	WARN_ON_ONCE(atomic_read(&p->refcnt) <= 0);
}


/* can be called with or without local BH being disabled */
static inline __u16	inet_getid(struct inet_peer *p, int more)
{
	more++;
	inet_peer_refcheck(p);
	return atomic_add_return(more, &p->ip_id_count) - more;
}

#endif /* _NET_INETPEER_H */
