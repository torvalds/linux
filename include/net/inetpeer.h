/*
 *		INETPEER - A storage for permanent information about peers
 *
 *  Version:	$Id: inetpeer.h,v 1.2 2002/01/12 07:54:56 davem Exp $
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

struct inet_peer
{
	struct inet_peer	*avl_left, *avl_right;
	struct inet_peer	*unused_next, **unused_prevp;
	unsigned long		dtime;		/* the time of last use of not
						 * referenced entries */
	atomic_t		refcnt;
	__u32			v4daddr;	/* peer's address */
	__u16			avl_height;
	__u16			ip_id_count;	/* IP ID for the next packet */
	atomic_t		rid;		/* Frag reception counter */
	__u32			tcp_ts;
	unsigned long		tcp_ts_stamp;
};

void			inet_initpeers(void) __init;

/* can be called with or without local BH being disabled */
struct inet_peer	*inet_getpeer(__u32 daddr, int create);

extern spinlock_t inet_peer_unused_lock;
extern struct inet_peer **inet_peer_unused_tailp;
/* can be called from BH context or outside */
static inline void	inet_putpeer(struct inet_peer *p)
{
	spin_lock_bh(&inet_peer_unused_lock);
	if (atomic_dec_and_test(&p->refcnt)) {
		p->unused_prevp = inet_peer_unused_tailp;
		p->unused_next = NULL;
		*inet_peer_unused_tailp = p;
		inet_peer_unused_tailp = &p->unused_next;
		p->dtime = jiffies;
	}
	spin_unlock_bh(&inet_peer_unused_lock);
}

extern spinlock_t inet_peer_idlock;
/* can be called with or without local BH being disabled */
static inline __u16	inet_getid(struct inet_peer *p, int more)
{
	__u16 id;

	spin_lock_bh(&inet_peer_idlock);
	id = p->ip_id_count;
	p->ip_id_count += 1 + more;
	spin_unlock_bh(&inet_peer_idlock);
	return id;
}

#endif /* _NET_INETPEER_H */
