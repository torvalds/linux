#ifndef _NDISC_H
#define _NDISC_H

/*
 *	ICMP codes for neighbour discovery messages
 */

#define NDISC_ROUTER_SOLICITATION	133
#define NDISC_ROUTER_ADVERTISEMENT	134
#define NDISC_NEIGHBOUR_SOLICITATION	135
#define NDISC_NEIGHBOUR_ADVERTISEMENT	136
#define NDISC_REDIRECT			137

/*
 * Router type: cross-layer information from link-layer to
 * IPv6 layer reported by certain link types (e.g., RFC4214).
 */
#define NDISC_NODETYPE_UNSPEC		0	/* unspecified (default) */
#define NDISC_NODETYPE_HOST		1	/* host or unauthorized router */
#define NDISC_NODETYPE_NODEFAULT	2	/* non-default router */
#define NDISC_NODETYPE_DEFAULT		3	/* default router */

/*
 *	ndisc options
 */

enum {
	__ND_OPT_PREFIX_INFO_END = 0,
	ND_OPT_SOURCE_LL_ADDR = 1,	/* RFC2461 */
	ND_OPT_TARGET_LL_ADDR = 2,	/* RFC2461 */
	ND_OPT_PREFIX_INFO = 3,		/* RFC2461 */
	ND_OPT_REDIRECT_HDR = 4,	/* RFC2461 */
	ND_OPT_MTU = 5,			/* RFC2461 */
	__ND_OPT_ARRAY_MAX,
	ND_OPT_ROUTE_INFO = 24,		/* RFC4191 */
	ND_OPT_RDNSS = 25,		/* RFC5006 */
	ND_OPT_DNSSL = 31,		/* RFC6106 */
	__ND_OPT_MAX
};

#define MAX_RTR_SOLICITATION_DELAY	HZ

#define ND_REACHABLE_TIME		(30*HZ)
#define ND_RETRANS_TIMER		HZ

#include <linux/compiler.h>
#include <linux/icmpv6.h>
#include <linux/in6.h>
#include <linux/types.h>

#include <net/neighbour.h>

struct ctl_table;
struct inet6_dev;
struct net_device;
struct net_proto_family;
struct sk_buff;

extern struct neigh_table nd_tbl;

struct nd_msg {
        struct icmp6hdr	icmph;
        struct in6_addr	target;
	__u8		opt[0];
};

struct rs_msg {
	struct icmp6hdr	icmph;
	__u8		opt[0];
};

struct ra_msg {
        struct icmp6hdr		icmph;
	__be32			reachable_time;
	__be32			retrans_timer;
};

struct nd_opt_hdr {
	__u8		nd_opt_type;
	__u8		nd_opt_len;
} __packed;

static inline u32 ndisc_hashfn(const void *pkey, const struct net_device *dev, __u32 *hash_rnd)
{
	const u32 *p32 = pkey;

	return (((p32[0] ^ dev->ifindex) * hash_rnd[0]) +
		(p32[1] * hash_rnd[1]) +
		(p32[2] * hash_rnd[2]) +
		(p32[3] * hash_rnd[3]));
}

static inline struct neighbour *__ipv6_neigh_lookup(struct neigh_table *tbl, struct net_device *dev, const void *pkey)
{
	struct neigh_hash_table *nht;
	const u32 *p32 = pkey;
	struct neighbour *n;
	u32 hash_val;

	rcu_read_lock_bh();
	nht = rcu_dereference_bh(tbl->nht);
	hash_val = ndisc_hashfn(pkey, dev, nht->hash_rnd) >> (32 - nht->hash_shift);
	for (n = rcu_dereference_bh(nht->hash_buckets[hash_val]);
	     n != NULL;
	     n = rcu_dereference_bh(n->next)) {
		u32 *n32 = (u32 *) n->primary_key;
		if (n->dev == dev &&
		    ((n32[0] ^ p32[0]) | (n32[1] ^ p32[1]) |
		     (n32[2] ^ p32[2]) | (n32[3] ^ p32[3])) == 0) {
			if (!atomic_inc_not_zero(&n->refcnt))
				n = NULL;
			break;
		}
	}
	rcu_read_unlock_bh();

	return n;
}

extern int			ndisc_init(void);

extern void			ndisc_cleanup(void);

extern int			ndisc_rcv(struct sk_buff *skb);

extern void			ndisc_send_ns(struct net_device *dev,
					      struct neighbour *neigh,
					      const struct in6_addr *solicit,
					      const struct in6_addr *daddr,
					      const struct in6_addr *saddr);

extern void			ndisc_send_rs(struct net_device *dev,
					      const struct in6_addr *saddr,
					      const struct in6_addr *daddr);

extern void			ndisc_send_redirect(struct sk_buff *skb,
						    const struct in6_addr *target);

extern int			ndisc_mc_map(const struct in6_addr *addr, char *buf,
					     struct net_device *dev, int dir);

extern struct sk_buff		*ndisc_build_skb(struct net_device *dev,
						 const struct in6_addr *daddr,
						 const struct in6_addr *saddr,
						 struct icmp6hdr *icmp6h,
						 const struct in6_addr *target,
						 int llinfo);

extern void			ndisc_send_skb(struct sk_buff *skb,
					       struct net_device *dev,
					       struct neighbour *neigh,
					       const struct in6_addr *daddr,
					       const struct in6_addr *saddr,
					       struct icmp6hdr *icmp6h);



/*
 *	IGMP
 */
extern int			igmp6_init(void);

extern void			igmp6_cleanup(void);

extern int			igmp6_event_query(struct sk_buff *skb);

extern int			igmp6_event_report(struct sk_buff *skb);


#ifdef CONFIG_SYSCTL
extern int 			ndisc_ifinfo_sysctl_change(struct ctl_table *ctl,
							   int write,
							   void __user *buffer,
							   size_t *lenp,
							   loff_t *ppos);
int ndisc_ifinfo_sysctl_strategy(ctl_table *ctl,
				 void __user *oldval, size_t __user *oldlenp,
				 void __user *newval, size_t newlen);
#endif

extern void 			inet6_ifinfo_notify(int event,
						    struct inet6_dev *idev);

#endif
