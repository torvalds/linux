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
#include <linux/if_arp.h>
#include <linux/netdevice.h>
#include <linux/hash.h>

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

struct rd_msg {
	struct icmp6hdr icmph;
	struct in6_addr	target;
	struct in6_addr	dest;
	__u8		opt[0];
};

struct nd_opt_hdr {
	__u8		nd_opt_type;
	__u8		nd_opt_len;
} __packed;

/* ND options */
struct ndisc_options {
	struct nd_opt_hdr *nd_opt_array[__ND_OPT_ARRAY_MAX];
#ifdef CONFIG_IPV6_ROUTE_INFO
	struct nd_opt_hdr *nd_opts_ri;
	struct nd_opt_hdr *nd_opts_ri_end;
#endif
	struct nd_opt_hdr *nd_useropts;
	struct nd_opt_hdr *nd_useropts_end;
};

#define nd_opts_src_lladdr	nd_opt_array[ND_OPT_SOURCE_LL_ADDR]
#define nd_opts_tgt_lladdr	nd_opt_array[ND_OPT_TARGET_LL_ADDR]
#define nd_opts_pi		nd_opt_array[ND_OPT_PREFIX_INFO]
#define nd_opts_pi_end		nd_opt_array[__ND_OPT_PREFIX_INFO_END]
#define nd_opts_rh		nd_opt_array[ND_OPT_REDIRECT_HDR]
#define nd_opts_mtu		nd_opt_array[ND_OPT_MTU]

#define NDISC_OPT_SPACE(len) (((len)+2+7)&~7)

struct ndisc_options *ndisc_parse_options(u8 *opt, int opt_len,
					  struct ndisc_options *ndopts);

/*
 * Return the padding between the option length and the start of the
 * link addr.  Currently only IP-over-InfiniBand needs this, although
 * if RFC 3831 IPv6-over-Fibre Channel is ever implemented it may
 * also need a pad of 2.
 */
static inline int ndisc_addr_option_pad(unsigned short type)
{
	switch (type) {
	case ARPHRD_INFINIBAND: return 2;
	default:                return 0;
	}
}

static inline int ndisc_opt_addr_space(struct net_device *dev)
{
	return NDISC_OPT_SPACE(dev->addr_len +
			       ndisc_addr_option_pad(dev->type));
}

static inline u8 *ndisc_opt_addr_data(struct nd_opt_hdr *p,
				      struct net_device *dev)
{
	u8 *lladdr = (u8 *)(p + 1);
	int lladdrlen = p->nd_opt_len << 3;
	int prepad = ndisc_addr_option_pad(dev->type);
	if (lladdrlen != ndisc_opt_addr_space(dev))
		return NULL;
	return lladdr + prepad;
}

static inline u32 ndisc_hashfn(const void *pkey, const struct net_device *dev, __u32 *hash_rnd)
{
	const u32 *p32 = pkey;

	return (((p32[0] ^ hash32_ptr(dev)) * hash_rnd[0]) +
		(p32[1] * hash_rnd[1]) +
		(p32[2] * hash_rnd[2]) +
		(p32[3] * hash_rnd[3]));
}

static inline struct neighbour *__ipv6_neigh_lookup_noref(struct net_device *dev, const void *pkey)
{
	return ___neigh_lookup_noref(&nd_tbl, neigh_key_eq128, ndisc_hashfn, pkey, dev);
}

static inline struct neighbour *__ipv6_neigh_lookup(struct net_device *dev, const void *pkey)
{
	struct neighbour *n;

	rcu_read_lock_bh();
	n = __ipv6_neigh_lookup_noref(dev, pkey);
	if (n && !atomic_inc_not_zero(&n->refcnt))
		n = NULL;
	rcu_read_unlock_bh();

	return n;
}

int ndisc_init(void);
int ndisc_late_init(void);

void ndisc_late_cleanup(void);
void ndisc_cleanup(void);

int ndisc_rcv(struct sk_buff *skb);

void ndisc_send_ns(struct net_device *dev, struct neighbour *neigh,
		   const struct in6_addr *solicit,
		   const struct in6_addr *daddr, const struct in6_addr *saddr,
		   struct sk_buff *oskb);

void ndisc_send_rs(struct net_device *dev,
		   const struct in6_addr *saddr, const struct in6_addr *daddr);
void ndisc_send_na(struct net_device *dev, struct neighbour *neigh,
		   const struct in6_addr *daddr,
		   const struct in6_addr *solicited_addr,
		   bool router, bool solicited, bool override, bool inc_opt);

void ndisc_send_redirect(struct sk_buff *skb, const struct in6_addr *target);

int ndisc_mc_map(const struct in6_addr *addr, char *buf, struct net_device *dev,
		 int dir);


/*
 *	IGMP
 */
int igmp6_init(void);

void igmp6_cleanup(void);

int igmp6_event_query(struct sk_buff *skb);

int igmp6_event_report(struct sk_buff *skb);


#ifdef CONFIG_SYSCTL
int ndisc_ifinfo_sysctl_change(struct ctl_table *ctl, int write,
			       void __user *buffer, size_t *lenp, loff_t *ppos);
int ndisc_ifinfo_sysctl_strategy(struct ctl_table *ctl,
				 void __user *oldval, size_t __user *oldlenp,
				 void __user *newval, size_t newlen);
#endif

void inet6_ifinfo_notify(int event, struct inet6_dev *idev);

#endif
