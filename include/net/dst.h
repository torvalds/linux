/* SPDX-License-Identifier: GPL-2.0 */
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
#include <linux/bug.h>
#include <linux/jiffies.h>
#include <linux/refcount.h>
#include <net/neighbour.h>
#include <asm/processor.h>

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
	struct net_device       *dev;
	struct rcu_head		rcu_head;
	struct dst_entry	*child;
	struct  dst_ops	        *ops;
	unsigned long		_metrics;
	unsigned long           expires;
	struct dst_entry	*path;
	struct dst_entry	*from;
#ifdef CONFIG_XFRM
	struct xfrm_state	*xfrm;
#else
	void			*__pad1;
#endif
	int			(*input)(struct sk_buff *);
	int			(*output)(struct net *net, struct sock *sk, struct sk_buff *skb);

	unsigned short		flags;
#define DST_HOST		0x0001
#define DST_NOXFRM		0x0002
#define DST_NOPOLICY		0x0004
#define DST_NOCOUNT		0x0008
#define DST_FAKE_RTABLE		0x0010
#define DST_XFRM_TUNNEL		0x0020
#define DST_XFRM_QUEUE		0x0040
#define DST_METADATA		0x0080

	short			error;

	/* A non-zero value of dst->obsolete forces by-hand validation
	 * of the route entry.  Positive values are set by the generic
	 * dst layer to indicate that the entry has been forcefully
	 * destroyed.
	 *
	 * Negative values are used by the implementation layer code to
	 * force invocation of the dst_ops->check() method.
	 */
	short			obsolete;
#define DST_OBSOLETE_NONE	0
#define DST_OBSOLETE_DEAD	2
#define DST_OBSOLETE_FORCE_CHK	-1
#define DST_OBSOLETE_KILL	-2
	unsigned short		header_len;	/* more space at head required */
	unsigned short		trailer_len;	/* space to reserve at tail */
	unsigned short		__pad3;

#ifdef CONFIG_IP_ROUTE_CLASSID
	__u32			tclassid;
#else
	__u32			__pad2;
#endif

#ifdef CONFIG_64BIT
	/*
	 * Align __refcnt to a 64 bytes alignment
	 * (L1_CACHE_SIZE would be too much)
	 */
	long			__pad_to_align_refcnt[2];
#endif
	/*
	 * __refcnt wants to be on a different cache line from
	 * input/output/ops or performance tanks badly
	 */
	atomic_t		__refcnt;	/* client references	*/
	int			__use;
	unsigned long		lastuse;
	struct lwtunnel_state   *lwtstate;
	union {
		struct dst_entry	*next;
		struct rtable __rcu	*rt_next;
		struct rt6_info __rcu	*rt6_next;
		struct dn_route __rcu	*dn_next;
	};
};

struct dst_metrics {
	u32		metrics[RTAX_MAX];
	refcount_t	refcnt;
};
extern const struct dst_metrics dst_default_metrics;

u32 *dst_cow_metrics_generic(struct dst_entry *dst, unsigned long old);

#define DST_METRICS_READ_ONLY		0x1UL
#define DST_METRICS_REFCOUNTED		0x2UL
#define DST_METRICS_FLAGS		0x3UL
#define __DST_METRICS_PTR(Y)	\
	((u32 *)((Y) & ~DST_METRICS_FLAGS))
#define DST_METRICS_PTR(X)	__DST_METRICS_PTR((X)->_metrics)

static inline bool dst_metrics_read_only(const struct dst_entry *dst)
{
	return dst->_metrics & DST_METRICS_READ_ONLY;
}

void __dst_destroy_metrics_generic(struct dst_entry *dst, unsigned long old);

static inline void dst_destroy_metrics_generic(struct dst_entry *dst)
{
	unsigned long val = dst->_metrics;
	if (!(val & DST_METRICS_READ_ONLY))
		__dst_destroy_metrics_generic(dst, val);
}

static inline u32 *dst_metrics_write_ptr(struct dst_entry *dst)
{
	unsigned long p = dst->_metrics;

	BUG_ON(!p);

	if (p & DST_METRICS_READ_ONLY)
		return dst->ops->cow_metrics(dst, p);
	return __DST_METRICS_PTR(p);
}

/* This may only be invoked before the entry has reached global
 * visibility.
 */
static inline void dst_init_metrics(struct dst_entry *dst,
				    const u32 *src_metrics,
				    bool read_only)
{
	dst->_metrics = ((unsigned long) src_metrics) |
		(read_only ? DST_METRICS_READ_ONLY : 0);
}

static inline void dst_copy_metrics(struct dst_entry *dest, const struct dst_entry *src)
{
	u32 *dst_metrics = dst_metrics_write_ptr(dest);

	if (dst_metrics) {
		u32 *src_metrics = DST_METRICS_PTR(src);

		memcpy(dst_metrics, src_metrics, RTAX_MAX * sizeof(u32));
	}
}

static inline u32 *dst_metrics_ptr(struct dst_entry *dst)
{
	return DST_METRICS_PTR(dst);
}

static inline u32
dst_metric_raw(const struct dst_entry *dst, const int metric)
{
	u32 *p = DST_METRICS_PTR(dst);

	return p[metric-1];
}

static inline u32
dst_metric(const struct dst_entry *dst, const int metric)
{
	WARN_ON_ONCE(metric == RTAX_HOPLIMIT ||
		     metric == RTAX_ADVMSS ||
		     metric == RTAX_MTU);
	return dst_metric_raw(dst, metric);
}

static inline u32
dst_metric_advmss(const struct dst_entry *dst)
{
	u32 advmss = dst_metric_raw(dst, RTAX_ADVMSS);

	if (!advmss)
		advmss = dst->ops->default_advmss(dst);

	return advmss;
}

static inline void dst_metric_set(struct dst_entry *dst, int metric, u32 val)
{
	u32 *p = dst_metrics_write_ptr(dst);

	if (p)
		p[metric-1] = val;
}

/* Kernel-internal feature bits that are unallocated in user space. */
#define DST_FEATURE_ECN_CA	(1 << 31)

#define DST_FEATURE_MASK	(DST_FEATURE_ECN_CA)
#define DST_FEATURE_ECN_MASK	(DST_FEATURE_ECN_CA | RTAX_FEATURE_ECN)

static inline u32
dst_feature(const struct dst_entry *dst, u32 feature)
{
	return dst_metric(dst, RTAX_FEATURES) & feature;
}

static inline u32 dst_mtu(const struct dst_entry *dst)
{
	return dst->ops->mtu(dst);
}

/* RTT metrics are stored in milliseconds for user ABI, but used as jiffies */
static inline unsigned long dst_metric_rtt(const struct dst_entry *dst, int metric)
{
	return msecs_to_jiffies(dst_metric(dst, metric));
}

static inline u32
dst_allfrag(const struct dst_entry *dst)
{
	int ret = dst_feature(dst,  RTAX_FEATURE_ALLFRAG);
	return ret;
}

static inline int
dst_metric_locked(const struct dst_entry *dst, int metric)
{
	return dst_metric(dst, RTAX_LOCK) & (1<<metric);
}

static inline void dst_hold(struct dst_entry *dst)
{
	/*
	 * If your kernel compilation stops here, please check
	 * __pad_to_align_refcnt declaration in struct dst_entry
	 */
	BUILD_BUG_ON(offsetof(struct dst_entry, __refcnt) & 63);
	WARN_ON(atomic_inc_not_zero(&dst->__refcnt) == 0);
}

static inline void dst_use_noref(struct dst_entry *dst, unsigned long time)
{
	if (unlikely(time != dst->lastuse)) {
		dst->__use++;
		dst->lastuse = time;
	}
}

static inline void dst_hold_and_use(struct dst_entry *dst, unsigned long time)
{
	dst_hold(dst);
	dst_use_noref(dst, time);
}

static inline struct dst_entry *dst_clone(struct dst_entry *dst)
{
	if (dst)
		dst_hold(dst);
	return dst;
}

void dst_release(struct dst_entry *dst);

void dst_release_immediate(struct dst_entry *dst);

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

static inline void __skb_dst_copy(struct sk_buff *nskb, unsigned long refdst)
{
	nskb->_skb_refdst = refdst;
	if (!(nskb->_skb_refdst & SKB_DST_NOREF))
		dst_clone(skb_dst(nskb));
}

static inline void skb_dst_copy(struct sk_buff *nskb, const struct sk_buff *oskb)
{
	__skb_dst_copy(nskb, oskb->_skb_refdst);
}

/**
 * dst_hold_safe - Take a reference on a dst if possible
 * @dst: pointer to dst entry
 *
 * This helper returns false if it could not safely
 * take a reference on a dst.
 */
static inline bool dst_hold_safe(struct dst_entry *dst)
{
	return atomic_inc_not_zero(&dst->__refcnt);
}

/**
 * skb_dst_force - makes sure skb dst is refcounted
 * @skb: buffer
 *
 * If dst is not yet refcounted and not destroyed, grab a ref on it.
 */
static inline void skb_dst_force(struct sk_buff *skb)
{
	if (skb_dst_is_noref(skb)) {
		struct dst_entry *dst = skb_dst(skb);

		WARN_ON(!rcu_read_lock_held());
		if (!dst_hold_safe(dst))
			dst = NULL;

		skb->_skb_refdst = (unsigned long)dst;
	}
}


/**
 *	__skb_tunnel_rx - prepare skb for rx reinsert
 *	@skb: buffer
 *	@dev: tunnel device
 *	@net: netns for packet i/o
 *
 *	After decapsulation, packet is going to re-enter (netif_rx()) our stack,
 *	so make some cleanups. (no accounting done)
 */
static inline void __skb_tunnel_rx(struct sk_buff *skb, struct net_device *dev,
				   struct net *net)
{
	skb->dev = dev;

	/*
	 * Clear hash so that we can recalulate the hash for the
	 * encapsulated packet, unless we have already determine the hash
	 * over the L4 4-tuple.
	 */
	skb_clear_hash_if_not_l4(skb);
	skb_set_queue_mapping(skb, 0);
	skb_scrub_packet(skb, !net_eq(net, dev_net(dev)));
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
static inline void skb_tunnel_rx(struct sk_buff *skb, struct net_device *dev,
				 struct net *net)
{
	/* TODO : stats should be SMP safe */
	dev->stats.rx_packets++;
	dev->stats.rx_bytes += skb->len;
	__skb_tunnel_rx(skb, dev, net);
}

static inline u32 dst_tclassid(const struct sk_buff *skb)
{
#ifdef CONFIG_IP_ROUTE_CLASSID
	const struct dst_entry *dst;

	dst = skb_dst(skb);
	if (dst)
		return dst->tclassid;
#endif
	return 0;
}

int dst_discard_out(struct net *net, struct sock *sk, struct sk_buff *skb);
static inline int dst_discard(struct sk_buff *skb)
{
	return dst_discard_out(&init_net, skb->sk, skb);
}
void *dst_alloc(struct dst_ops *ops, struct net_device *dev, int initial_ref,
		int initial_obsolete, unsigned short flags);
void dst_init(struct dst_entry *dst, struct dst_ops *ops,
	      struct net_device *dev, int initial_ref, int initial_obsolete,
	      unsigned short flags);
struct dst_entry *dst_destroy(struct dst_entry *dst);
void dst_dev_put(struct dst_entry *dst);

static inline void dst_confirm(struct dst_entry *dst)
{
}

static inline struct neighbour *dst_neigh_lookup(const struct dst_entry *dst, const void *daddr)
{
	struct neighbour *n = dst->ops->neigh_lookup(dst, NULL, daddr);
	return IS_ERR(n) ? NULL : n;
}

static inline struct neighbour *dst_neigh_lookup_skb(const struct dst_entry *dst,
						     struct sk_buff *skb)
{
	struct neighbour *n =  dst->ops->neigh_lookup(dst, skb, NULL);
	return IS_ERR(n) ? NULL : n;
}

static inline void dst_confirm_neigh(const struct dst_entry *dst,
				     const void *daddr)
{
	if (dst->ops->confirm_neigh)
		dst->ops->confirm_neigh(dst, daddr);
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
static inline int dst_output(struct net *net, struct sock *sk, struct sk_buff *skb)
{
	return skb_dst(skb)->output(net, sk, skb);
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

/* Flags for xfrm_lookup flags argument. */
enum {
	XFRM_LOOKUP_ICMP = 1 << 0,
	XFRM_LOOKUP_QUEUE = 1 << 1,
	XFRM_LOOKUP_KEEP_DST_REF = 1 << 2,
};

struct flowi;
#ifndef CONFIG_XFRM
static inline struct dst_entry *xfrm_lookup(struct net *net,
					    struct dst_entry *dst_orig,
					    const struct flowi *fl,
					    const struct sock *sk,
					    int flags)
{
	return dst_orig;
}

static inline struct dst_entry *xfrm_lookup_route(struct net *net,
						  struct dst_entry *dst_orig,
						  const struct flowi *fl,
						  const struct sock *sk,
						  int flags)
{
	return dst_orig;
}

static inline struct xfrm_state *dst_xfrm(const struct dst_entry *dst)
{
	return NULL;
}

#else
struct dst_entry *xfrm_lookup(struct net *net, struct dst_entry *dst_orig,
			      const struct flowi *fl, const struct sock *sk,
			      int flags);

struct dst_entry *xfrm_lookup_route(struct net *net, struct dst_entry *dst_orig,
				    const struct flowi *fl, const struct sock *sk,
				    int flags);

/* skb attached with this dst needs transformation if dst->xfrm is valid */
static inline struct xfrm_state *dst_xfrm(const struct dst_entry *dst)
{
	return dst->xfrm;
}
#endif

static inline void skb_dst_update_pmtu(struct sk_buff *skb, u32 mtu)
{
	struct dst_entry *dst = skb_dst(skb);

	if (dst && dst->ops->update_pmtu)
		dst->ops->update_pmtu(dst, NULL, skb, mtu);
}

#endif /* _NET_DST_H */
