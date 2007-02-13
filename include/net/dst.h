/*
 * net/dst.h	Protocol independent destination cache definitions.
 *
 * Authors:	Alexey Kuznetsov, <kuznet@ms2.inr.ac.ru>
 *
 */

#ifndef _NET_DST_H
#define _NET_DST_H

#include <linux/netdevice.h>
#include <linux/rtnetlink.h>
#include <linux/rcupdate.h>
#include <linux/jiffies.h>
#include <net/neighbour.h>
#include <asm/processor.h>

/*
 * 0 - no debugging messages
 * 1 - rare events and bugs (default)
 * 2 - trace mode.
 */
#define RT_CACHE_DEBUG		0

#define DST_GC_MIN	(HZ/10)
#define DST_GC_INC	(HZ/2)
#define DST_GC_MAX	(120*HZ)

/* Each dst_entry has reference count and sits in some parent list(s).
 * When it is removed from parent list, it is "freed" (dst_free).
 * After this it enters dead state (dst->obsolete > 0) and if its refcnt
 * is zero, it can be destroyed immediately, otherwise it is added
 * to gc list and garbage collector periodically checks the refcnt.
 */

struct sk_buff;

struct dst_entry
{
	struct rcu_head		rcu_head;
	struct dst_entry	*child;
	struct net_device       *dev;
	short			error;
	short			obsolete;
	int			flags;
#define DST_HOST		1
#define DST_NOXFRM		2
#define DST_NOPOLICY		4
#define DST_NOHASH		8
#define DST_BALANCED            0x10
	unsigned long		expires;

	unsigned short		header_len;	/* more space at head required */
	unsigned short		nfheader_len;	/* more non-fragment space at head required */
	unsigned short		trailer_len;	/* space to reserve at tail */

	u32			metrics[RTAX_MAX];
	struct dst_entry	*path;

	unsigned long		rate_last;	/* rate limiting for ICMP */
	unsigned long		rate_tokens;

	struct neighbour	*neighbour;
	struct hh_cache		*hh;
	struct xfrm_state	*xfrm;

	int			(*input)(struct sk_buff*);
	int			(*output)(struct sk_buff*);

#ifdef CONFIG_NET_CLS_ROUTE
	__u32			tclassid;
#endif

	struct  dst_ops	        *ops;
		
	unsigned long		lastuse;
	atomic_t		__refcnt;	/* client references	*/
	int			__use;
	union {
		struct dst_entry *next;
		struct rtable    *rt_next;
		struct rt6_info   *rt6_next;
		struct dn_route  *dn_next;
	};
	char			info[0];
};


struct dst_ops
{
	unsigned short		family;
	__be16			protocol;
	unsigned		gc_thresh;

	int			(*gc)(void);
	struct dst_entry *	(*check)(struct dst_entry *, __u32 cookie);
	void			(*destroy)(struct dst_entry *);
	void			(*ifdown)(struct dst_entry *,
					  struct net_device *dev, int how);
	struct dst_entry *	(*negative_advice)(struct dst_entry *);
	void			(*link_failure)(struct sk_buff *);
	void			(*update_pmtu)(struct dst_entry *dst, u32 mtu);
	int			entry_size;

	atomic_t		entries;
	struct kmem_cache 		*kmem_cachep;
};

#ifdef __KERNEL__

static inline u32
dst_metric(const struct dst_entry *dst, int metric)
{
	return dst->metrics[metric-1];
}

static inline u32 dst_mtu(const struct dst_entry *dst)
{
	u32 mtu = dst_metric(dst, RTAX_MTU);
	/*
	 * Alexey put it here, so ask him about it :)
	 */
	barrier();
	return mtu;
}

static inline u32
dst_allfrag(const struct dst_entry *dst)
{
	int ret = dst_metric(dst, RTAX_FEATURES) & RTAX_FEATURE_ALLFRAG;
	/* Yes, _exactly_. This is paranoia. */
	barrier();
	return ret;
}

static inline int
dst_metric_locked(struct dst_entry *dst, int metric)
{
	return dst_metric(dst, RTAX_LOCK) & (1<<metric);
}

static inline void dst_hold(struct dst_entry * dst)
{
	atomic_inc(&dst->__refcnt);
}

static inline
struct dst_entry * dst_clone(struct dst_entry * dst)
{
	if (dst)
		atomic_inc(&dst->__refcnt);
	return dst;
}

static inline
void dst_release(struct dst_entry * dst)
{
	if (dst) {
		WARN_ON(atomic_read(&dst->__refcnt) < 1);
		smp_mb__before_atomic_dec();
		atomic_dec(&dst->__refcnt);
	}
}

/* Children define the path of the packet through the
 * Linux networking.  Thus, destinations are stackable.
 */

static inline struct dst_entry *dst_pop(struct dst_entry *dst)
{
	struct dst_entry *child = dst_clone(dst->child);

	dst_release(dst);
	return child;
}

extern void * dst_alloc(struct dst_ops * ops);
extern void __dst_free(struct dst_entry * dst);
extern struct dst_entry *dst_destroy(struct dst_entry * dst);

static inline void dst_free(struct dst_entry * dst)
{
	if (dst->obsolete > 1)
		return;
	if (!atomic_read(&dst->__refcnt)) {
		dst = dst_destroy(dst);
		if (!dst)
			return;
	}
	__dst_free(dst);
}

static inline void dst_rcu_free(struct rcu_head *head)
{
	struct dst_entry *dst = container_of(head, struct dst_entry, rcu_head);
	dst_free(dst);
}

static inline void dst_confirm(struct dst_entry *dst)
{
	if (dst)
		neigh_confirm(dst->neighbour);
}

static inline void dst_negative_advice(struct dst_entry **dst_p)
{
	struct dst_entry * dst = *dst_p;
	if (dst && dst->ops->negative_advice)
		*dst_p = dst->ops->negative_advice(dst);
}

static inline void dst_link_failure(struct sk_buff *skb)
{
	struct dst_entry * dst = skb->dst;
	if (dst && dst->ops && dst->ops->link_failure)
		dst->ops->link_failure(skb);
}

static inline void dst_set_expires(struct dst_entry *dst, int timeout)
{
	unsigned long expires = jiffies + timeout;

	if (expires == 0)
		expires = 1;

	if (dst->expires == 0 || time_before(expires, dst->expires))
		dst->expires = expires;
}

/* Output packet to network from transport.  */
static inline int dst_output(struct sk_buff *skb)
{
	return skb->dst->output(skb);
}

/* Input packet from network to transport.  */
static inline int dst_input(struct sk_buff *skb)
{
	int err;

	for (;;) {
		err = skb->dst->input(skb);

		if (likely(err == 0))
			return err;
		/* Oh, Jamal... Seems, I will not forgive you this mess. :-) */
		if (unlikely(err != NET_XMIT_BYPASS))
			return err;
	}
}

static inline struct dst_entry *dst_check(struct dst_entry *dst, u32 cookie)
{
	if (dst->obsolete)
		dst = dst->ops->check(dst, cookie);
	return dst;
}

extern void		dst_init(void);

struct flowi;
#ifndef CONFIG_XFRM
static inline int xfrm_lookup(struct dst_entry **dst_p, struct flowi *fl,
		       struct sock *sk, int flags)
{
	return 0;
} 
#else
extern int xfrm_lookup(struct dst_entry **dst_p, struct flowi *fl,
		       struct sock *sk, int flags);
#endif
#endif

#endif /* _NET_DST_H */
