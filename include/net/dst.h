/*
 * net/dst.h	Protocol independent destination cache definitions.
 *
 * Authors:	Alexey Kuznetsov, <kuznet@ms2.inr.ac.ru>
 *
 */

#ifndef _NET_DST_H
#define _NET_DST_H

#include <net/dst_ops.h>
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

struct dst_entry {
	struct rcu_head		rcu_head;
	struct dst_entry	*child;
	struct net_device       *dev;
	short			error;
	short			obsolete;
	int			flags;
#define DST_HOST		0x0001
#define DST_NOXFRM		0x0002
#define DST_NOPOLICY		0x0004
#define DST_NOHASH		0x0008
#define DST_NOCACHE		0x0010
	unsigned long		expires;

	unsigned short		header_len;	/* more space at head required */
	unsigned short		trailer_len;	/* space to reserve at tail */

	unsigned int		rate_tokens;
	unsigned long		rate_last;	/* rate limiting for ICMP */

	struct dst_entry	*path;

	struct neighbour	*neighbour;
	struct hh_cache		*hh;
#ifdef CONFIG_XFRM
	struct xfrm_state	*xfrm;
#else
	void			*__pad1;
#endif
	int			(*input)(struct sk_buff*);
	int			(*output)(struct sk_buff*);

	struct  dst_ops	        *ops;

	u32			metrics[RTAX_MAX];

#ifdef CONFIG_NET_CLS_ROUTE
	__u32			tclassid;
#else
	__u32			__pad2;
#endif


	/*
	 * Align __refcnt to a 64 bytes alignment
	 * (L1_CACHE_SIZE would be too much)
	 */
#ifdef CONFIG_64BIT
	long			__pad_to_align_refcnt[1];
#endif
	/*
	 * __refcnt wants to be on a different cache line from
	 * input/output/ops or performance tanks badly
	 */
	atomic_t		__refcnt;	/* client references	*/
	int			__use;
	unsigned long		lastuse;
	union {
		struct dst_entry *next;
		struct rtable __rcu *rt_next;
		struct rt6_info   *rt6_next;
		struct dn_route  *dn_next;
	};
};

#ifdef __KERNEL__

static inline u32
dst_metric(const struct dst_entry *dst, int metric)
{
	return dst->metrics[metric-1];
}

static inline u32
dst_feature(const struct dst_entry *dst, u32 feature)
{
	return dst_metric(dst, RTAX_FEATURES) & feature;
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

/* RTT metrics are stored in milliseconds for user ABI, but used as jiffies */
static inline unsigned long dst_metric_rtt(const struct dst_entry *dst, int metric)
{
	return msecs_to_jiffies(dst_metric(dst, metric));
}

static inline void set_dst_metric_rtt(struct dst_entry *dst, int metric,
				      unsigned long rtt)
{
	dst->metrics[metric-1] = jiffies_to_msecs(rtt);
}

static inline u32
dst_allfrag(const struct dst_entry *dst)
{
	int ret = dst_feature(dst,  RTAX_FEATURE_ALLFRAG);
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
	/*
	 * If your kernel compilation stops here, please check
	 * __pad_to_align_refcnt declaration in struct dst_entry
	 */
	BUILD_BUG_ON(offsetof(struct dst_entry, __refcnt) & 63);
	atomic_inc(&dst->__refcnt);
}

static inline void dst_use(struct dst_entry *dst, unsigned long time)
{
	dst_hold(dst);
	dst->__use++;
	dst->lastuse = time;
}

static inline void dst_use_noref(struct dst_entry *dst, unsigned long time)
{
	dst->__use++;
	dst->lastuse = time;
}

static inline
struct dst_entry * dst_clone(struct dst_entry * dst)
{
	if (dst)
		atomic_inc(&dst->__refcnt);
	return dst;
}

extern void dst_release(struct dst_entry *dst);

static inline void refdst_drop(unsigned long refdst)
{
	if (!(refdst & SKB_DST_NOREF))
		dst_release((struct dst_entry *)(refdst & SKB_DST_PTRMASK));
}

/**
 * skb_dst_drop - drops skb dst
 * @skb: buffer
 *
 * Drops dst reference count if a reference was taken.
 */
static inline void skb_dst_drop(struct sk_buff *skb)
{
	if (skb->_skb_refdst) {
		refdst_drop(skb->_skb_refdst);
		skb->_skb_refdst = 0UL;
	}
}

static inline void skb_dst_copy(struct sk_buff *nskb, const struct sk_buff *oskb)
{
	nskb->_skb_refdst = oskb->_skb_refdst;
	if (!(nskb->_skb_refdst & SKB_DST_NOREF))
		dst_clone(skb_dst(nskb));
}

/**
 * skb_dst_force - makes sure skb dst is refcounted
 * @skb: buffer
 *
 * If dst is not yet refcounted, let's do it
 */
static inline void skb_dst_force(struct sk_buff *skb)
{
	if (skb_dst_is_noref(skb)) {
		WARN_ON(!rcu_read_lock_held());
		skb->_skb_refdst &= ~SKB_DST_NOREF;
		dst_clone(skb_dst(skb));
	}
}


/**
 *	__skb_tunnel_rx - prepare skb for rx reinsert
 *	@skb: buffer
 *	@dev: tunnel device
 *
 *	After decapsulation, packet is going to re-enter (netif_rx()) our stack,
 *	so make some cleanups. (no accounting done)
 */
static inline void __skb_tunnel_rx(struct sk_buff *skb, struct net_device *dev)
{
	skb->dev = dev;
	skb->rxhash = 0;
	skb_set_queue_mapping(skb, 0);
	skb_dst_drop(skb);
	nf_reset(skb);
}

/**
 *	skb_tunnel_rx - prepare skb for rx reinsert
 *	@skb: buffer
 *	@dev: tunnel device
 *
 *	After decapsulation, packet is going to re-enter (netif_rx()) our stack,
 *	so make some cleanups, and perform accounting.
 *	Note: this accounting is not SMP safe.
 */
static inline void skb_tunnel_rx(struct sk_buff *skb, struct net_device *dev)
{
	/* TODO : stats should be SMP safe */
	dev->stats.rx_packets++;
	dev->stats.rx_bytes += skb->len;
	__skb_tunnel_rx(skb, dev);
}

/* Children define the path of the packet through the
 * Linux networking.  Thus, destinations are stackable.
 */

static inline struct dst_entry *skb_dst_pop(struct sk_buff *skb)
{
	struct dst_entry *child = skb_dst(skb)->child;

	skb_dst_drop(skb);
	return child;
}

extern int dst_discard(struct sk_buff *skb);
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

static inline void dst_link_failure(struct sk_buff *skb)
{
	struct dst_entry *dst = skb_dst(skb);
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
	return skb_dst(skb)->output(skb);
}

/* Input packet from network to transport.  */
static inline int dst_input(struct sk_buff *skb)
{
	return skb_dst(skb)->input(skb);
}

static inline struct dst_entry *dst_check(struct dst_entry *dst, u32 cookie)
{
	if (dst->obsolete)
		dst = dst->ops->check(dst, cookie);
	return dst;
}

extern void		dst_init(void);

/* Flags for xfrm_lookup flags argument. */
enum {
	XFRM_LOOKUP_WAIT = 1 << 0,
	XFRM_LOOKUP_ICMP = 1 << 1,
};

struct flowi;
#ifndef CONFIG_XFRM
static inline int xfrm_lookup(struct net *net, struct dst_entry **dst_p,
			      struct flowi *fl, struct sock *sk, int flags)
{
	return 0;
} 
static inline int __xfrm_lookup(struct net *net, struct dst_entry **dst_p,
				struct flowi *fl, struct sock *sk, int flags)
{
	return 0;
}
#else
extern int xfrm_lookup(struct net *net, struct dst_entry **dst_p,
		       struct flowi *fl, struct sock *sk, int flags);
extern int __xfrm_lookup(struct net *net, struct dst_entry **dst_p,
			 struct flowi *fl, struct sock *sk, int flags);
#endif
#endif

#endif /* _NET_DST_H */
