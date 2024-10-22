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
#include <linux/rcuref.h>
#include <net/neighbour.h>
#include <asm/processor.h>
#include <linux/indirect_call_wrapper.h>

struct sk_buff;

struct dst_entry {
	struct net_device       *dev;
	struct  dst_ops	        *ops;
	unsigned long		_metrics;
	unsigned long           expires;
#ifdef CONFIG_XFRM
	struct xfrm_state	*xfrm;
#else
	void			*__pad1;
#endif
	int			(*input)(struct sk_buff *);
	int			(*output)(struct net *net, struct sock *sk, struct sk_buff *skb);

	unsigned short		flags;
#define DST_NOXFRM		0x0002
#define DST_NOPOLICY		0x0004
#define DST_NOCOUNT		0x0008
#define DST_FAKE_RTABLE		0x0010
#define DST_XFRM_TUNNEL		0x0020
#define DST_XFRM_QUEUE		0x0040
#define DST_METADATA		0x0080

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

	/*
	 * __rcuref wants to be on a different cache line from
	 * input/output/ops or performance tanks badly
	 */
#ifdef CONFIG_64BIT
	rcuref_t		__rcuref;	/* 64-bit offset 64 */
#endif
	int			__use;
	unsigned long		lastuse;
	struct rcu_head		rcu_head;
	short			error;
	short			__pad;
	__u32			tclassid;
#ifndef CONFIG_64BIT
	struct lwtunnel_state   *lwtstate;
	rcuref_t		__rcuref;	/* 32-bit offset 64 */
#endif
	netdevice_tracker	dev_tracker;

	/*
	 * Used by rtable and rt6_info. Moves lwtstate into the next cache
	 * line on 64bit so that lwtstate does not cause false sharing with
	 * __rcuref under contention of __rcuref. This also puts the
	 * frequently accessed members of rtable and rt6_info out of the
	 * __rcuref cache line.
	 */
	struct list_head	rt_uncached;
	struct uncached_list	*rt_uncached_list;
#ifdef CONFIG_64BIT
	struct lwtunnel_state   *lwtstate;
#endif
};

struct dst_metrics {
	u32		metrics[RTAX_MAX];
	refcount_t	refcnt;
} __aligned(4);		/* Low pointer bits contain DST_METRICS_FLAGS */
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
#define DST_FEATURE_ECN_CA	(1U << 31)

#define DST_FEATURE_MASK	(DST_FEATURE_ECN_CA)
#define DST_FEATURE_ECN_MASK	(DST_FEATURE_ECN_CA | RTAX_FEATURE_ECN)

static inline u32
dst_feature(const struct dst_entry *dst, u32 feature)
{
	return dst_metric(dst, RTAX_FEATURES) & feature;
}

INDIRECT_CALLABLE_DECLARE(unsigned int ip6_mtu(const struct dst_entry *));
INDIRECT_CALLABLE_DECLARE(unsigned int ipv4_mtu(const struct dst_entry *));
static inline u32 dst_mtu(const struct dst_entry *dst)
{
	return INDIRECT_CALL_INET(dst->ops->mtu, ip6_mtu, ipv4_mtu, dst);
}

/* RTT metrics are stored in milliseconds for user ABI, but used as jiffies */
static inline unsigned long dst_metric_rtt(const struct dst_entry *dst, int metric)
{
	return msecs_to_jiffies(dst_metric(dst, metric));
}

static inline int
dst_metric_locked(const struct dst_entry *dst, int metric)
{
	return dst_metric(dst, RTAX_LOCK) & (1 << metric);
}

static inline void dst_hold(struct dst_entry *dst)
{
	/*
	 * If your kernel compilation stops here, please check
	 * the placement of __rcuref in struct dst_entry
	 */
	BUILD_BUG_ON(offsetof(struct dst_entry, __rcuref) & 63);
	WARN_ON(!rcuref_get(&dst->__rcuref));
}

static inline void dst_use_noref(struct dst_entry *dst, unsigned long time)
{
	if (unlikely(time != dst->lastuse)) {
		dst->__use++;
		dst->lastuse = time;
	}
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
	nskb->slow_gro |= !!refdst;
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
	return rcuref_get(&dst->__rcuref);
}

/**
 * skb_dst_force - makes sure skb dst is refcounted
 * @skb: buffer
 *
 * If dst is not yet refcounted and not destroyed, grab a ref on it.
 * Returns true if dst is refcounted.
 */
static inline bool skb_dst_force(struct sk_buff *skb)
{
	if (skb_dst_is_noref(skb)) {
		struct dst_entry *dst = skb_dst(skb);

		WARN_ON(!rcu_read_lock_held());
		if (!dst_hold_safe(dst))
			dst = NULL;

		skb->_skb_refdst = (unsigned long)dst;
		skb->slow_gro |= !!dst;
	}

	return skb->_skb_refdst != 0UL;
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
	 * Clear hash so that we can recalculate the hash for the
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
 *	@net: netns for packet i/o
 *
 *	After decapsulation, packet is going to re-enter (netif_rx()) our stack,
 *	so make some cleanups, and perform accounting.
 *	Note: this accounting is not SMP safe.
 */
static inline void skb_tunnel_rx(struct sk_buff *skb, struct net_device *dev,
				 struct net *net)
{
	DEV_STATS_INC(dev, rx_packets);
	DEV_STATS_ADD(dev, rx_bytes, skb->len);
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
void *dst_alloc(struct dst_ops *ops, struct net_device *dev,
		int initial_obsolete, unsigned short flags);
void dst_init(struct dst_entry *dst, struct dst_ops *ops,
	      struct net_device *dev, int initial_obsolete,
	      unsigned short flags);
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
	struct neighbour *n;

	if (WARN_ON_ONCE(!dst->ops->neigh_lookup))
		return NULL;

	n = dst->ops->neigh_lookup(dst, skb, NULL);

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

INDIRECT_CALLABLE_DECLARE(int ip6_output(struct net *, struct sock *,
					 struct sk_buff *));
INDIRECT_CALLABLE_DECLARE(int ip_output(struct net *, struct sock *,
					 struct sk_buff *));
/* Output packet to network from transport.  */
static inline int dst_output(struct net *net, struct sock *sk, struct sk_buff *skb)
{
	return INDIRECT_CALL_INET(skb_dst(skb)->output,
				  ip6_output, ip_output,
				  net, sk, skb);
}

INDIRECT_CALLABLE_DECLARE(int ip6_input(struct sk_buff *));
INDIRECT_CALLABLE_DECLARE(int ip_local_deliver(struct sk_buff *));
/* Input packet from network to transport.  */
static inline int dst_input(struct sk_buff *skb)
{
	return INDIRECT_CALL_INET(skb_dst(skb)->input,
				  ip6_input, ip_local_deliver, skb);
}

INDIRECT_CALLABLE_DECLARE(struct dst_entry *ip6_dst_check(struct dst_entry *,
							  u32));
INDIRECT_CALLABLE_DECLARE(struct dst_entry *ipv4_dst_check(struct dst_entry *,
							   u32));
static inline struct dst_entry *dst_check(struct dst_entry *dst, u32 cookie)
{
	if (dst->obsolete)
		dst = INDIRECT_CALL_INET(dst->ops->check, ip6_dst_check,
					 ipv4_dst_check, dst, cookie);
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

static inline struct dst_entry *
xfrm_lookup_with_ifid(struct net *net, struct dst_entry *dst_orig,
		      const struct flowi *fl, const struct sock *sk,
		      int flags, u32 if_id)
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

struct dst_entry *xfrm_lookup_with_ifid(struct net *net,
					struct dst_entry *dst_orig,
					const struct flowi *fl,
					const struct sock *sk, int flags,
					u32 if_id);

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
		dst->ops->update_pmtu(dst, NULL, skb, mtu, true);
}

/* update dst pmtu but not do neighbor confirm */
static inline void skb_dst_update_pmtu_no_confirm(struct sk_buff *skb, u32 mtu)
{
	struct dst_entry *dst = skb_dst(skb);

	if (dst && dst->ops->update_pmtu)
		dst->ops->update_pmtu(dst, NULL, skb, mtu, false);
}

struct dst_entry *dst_blackhole_check(struct dst_entry *dst, u32 cookie);
void dst_blackhole_update_pmtu(struct dst_entry *dst, struct sock *sk,
			       struct sk_buff *skb, u32 mtu, bool confirm_neigh);
void dst_blackhole_redirect(struct dst_entry *dst, struct sock *sk,
			    struct sk_buff *skb);
u32 *dst_blackhole_cow_metrics(struct dst_entry *dst, unsigned long old);
struct neighbour *dst_blackhole_neigh_lookup(const struct dst_entry *dst,
					     struct sk_buff *skb,
					     const void *daddr);
unsigned int dst_blackhole_mtu(const struct dst_entry *dst);

#endif /* _NET_DST_H */
