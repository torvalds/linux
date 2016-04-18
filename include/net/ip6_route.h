#ifndef _NET_IP6_ROUTE_H
#define _NET_IP6_ROUTE_H

struct route_info {
	__u8			type;
	__u8			length;
	__u8			prefix_len;
#if defined(__BIG_ENDIAN_BITFIELD)
	__u8			reserved_h:3,
				route_pref:2,
				reserved_l:3;
#elif defined(__LITTLE_ENDIAN_BITFIELD)
	__u8			reserved_l:3,
				route_pref:2,
				reserved_h:3;
#endif
	__be32			lifetime;
	__u8			prefix[0];	/* 0,8 or 16 */
};

#include <net/flow.h>
#include <net/ip6_fib.h>
#include <net/sock.h>
#include <linux/ip.h>
#include <linux/ipv6.h>
#include <linux/route.h>

#define RT6_LOOKUP_F_IFACE		0x00000001
#define RT6_LOOKUP_F_REACHABLE		0x00000002
#define RT6_LOOKUP_F_HAS_SADDR		0x00000004
#define RT6_LOOKUP_F_SRCPREF_TMP	0x00000008
#define RT6_LOOKUP_F_SRCPREF_PUBLIC	0x00000010
#define RT6_LOOKUP_F_SRCPREF_COA	0x00000020

/* We do not (yet ?) support IPv6 jumbograms (RFC 2675)
 * Unlike IPv4, hdr->seg_len doesn't include the IPv6 header
 */
#define IP6_MAX_MTU (0xFFFF + sizeof(struct ipv6hdr))

/*
 * rt6_srcprefs2flags() and rt6_flags2srcprefs() translate
 * between IPV6_ADDR_PREFERENCES socket option values
 *	IPV6_PREFER_SRC_TMP    = 0x1
 *	IPV6_PREFER_SRC_PUBLIC = 0x2
 *	IPV6_PREFER_SRC_COA    = 0x4
 * and above RT6_LOOKUP_F_SRCPREF_xxx flags.
 */
static inline int rt6_srcprefs2flags(unsigned int srcprefs)
{
	/* No need to bitmask because srcprefs have only 3 bits. */
	return srcprefs << 3;
}

static inline unsigned int rt6_flags2srcprefs(int flags)
{
	return (flags >> 3) & 7;
}

static inline bool rt6_need_strict(const struct in6_addr *daddr)
{
	return ipv6_addr_type(daddr) &
		(IPV6_ADDR_MULTICAST | IPV6_ADDR_LINKLOCAL | IPV6_ADDR_LOOPBACK);
}

void ip6_route_input(struct sk_buff *skb);

struct dst_entry *ip6_route_output_flags(struct net *net, const struct sock *sk,
					 struct flowi6 *fl6, int flags);

static inline struct dst_entry *ip6_route_output(struct net *net,
						 const struct sock *sk,
						 struct flowi6 *fl6)
{
	return ip6_route_output_flags(net, sk, fl6, 0);
}

struct dst_entry *ip6_route_lookup(struct net *net, struct flowi6 *fl6,
				   int flags);

int ip6_route_init(void);
void ip6_route_cleanup(void);

int ipv6_route_ioctl(struct net *net, unsigned int cmd, void __user *arg);

int ip6_route_add(struct fib6_config *cfg);
int ip6_ins_rt(struct rt6_info *);
int ip6_del_rt(struct rt6_info *);

int ip6_route_get_saddr(struct net *net, struct rt6_info *rt,
			const struct in6_addr *daddr, unsigned int prefs,
			struct in6_addr *saddr);

struct rt6_info *rt6_lookup(struct net *net, const struct in6_addr *daddr,
			    const struct in6_addr *saddr, int oif, int flags);

struct dst_entry *icmp6_dst_alloc(struct net_device *dev, struct flowi6 *fl6);
int icmp6_dst_gc(void);

void fib6_force_start_gc(struct net *net);

struct rt6_info *addrconf_dst_alloc(struct inet6_dev *idev,
				    const struct in6_addr *addr, bool anycast);

/*
 *	support functions for ND
 *
 */
struct rt6_info *rt6_get_dflt_router(const struct in6_addr *addr,
				     struct net_device *dev);
struct rt6_info *rt6_add_dflt_router(const struct in6_addr *gwaddr,
				     struct net_device *dev, unsigned int pref);

void rt6_purge_dflt_routers(struct net *net);

int rt6_route_rcv(struct net_device *dev, u8 *opt, int len,
		  const struct in6_addr *gwaddr);

void ip6_update_pmtu(struct sk_buff *skb, struct net *net, __be32 mtu, int oif,
		     u32 mark);
void ip6_sk_update_pmtu(struct sk_buff *skb, struct sock *sk, __be32 mtu);
void ip6_redirect(struct sk_buff *skb, struct net *net, int oif, u32 mark);
void ip6_redirect_no_header(struct sk_buff *skb, struct net *net, int oif,
			    u32 mark);
void ip6_sk_redirect(struct sk_buff *skb, struct sock *sk);

struct netlink_callback;

struct rt6_rtnl_dump_arg {
	struct sk_buff *skb;
	struct netlink_callback *cb;
	struct net *net;
};

int rt6_dump_route(struct rt6_info *rt, void *p_arg);
void rt6_ifdown(struct net *net, struct net_device *dev);
void rt6_mtu_change(struct net_device *dev, unsigned int mtu);
void rt6_remove_prefsrc(struct inet6_ifaddr *ifp);
void rt6_clean_tohost(struct net *net, struct in6_addr *gateway);


/*
 *	Store a destination cache entry in a socket
 */
static inline void ip6_dst_store(struct sock *sk, struct dst_entry *dst,
				 const struct in6_addr *daddr,
				 const struct in6_addr *saddr)
{
	struct ipv6_pinfo *np = inet6_sk(sk);

	np->dst_cookie = rt6_get_cookie((struct rt6_info *)dst);
	sk_setup_caps(sk, dst);
	np->daddr_cache = daddr;
#ifdef CONFIG_IPV6_SUBTREES
	np->saddr_cache = saddr;
#endif
}

static inline bool ipv6_unicast_destination(const struct sk_buff *skb)
{
	struct rt6_info *rt = (struct rt6_info *) skb_dst(skb);

	return rt->rt6i_flags & RTF_LOCAL;
}

static inline bool ipv6_anycast_destination(const struct dst_entry *dst,
					    const struct in6_addr *daddr)
{
	struct rt6_info *rt = (struct rt6_info *)dst;

	return rt->rt6i_flags & RTF_ANYCAST ||
		(rt->rt6i_dst.plen != 128 &&
		 ipv6_addr_equal(&rt->rt6i_dst.addr, daddr));
}

int ip6_fragment(struct net *net, struct sock *sk, struct sk_buff *skb,
		 int (*output)(struct net *, struct sock *, struct sk_buff *));

static inline int ip6_skb_dst_mtu(struct sk_buff *skb)
{
	struct ipv6_pinfo *np = skb->sk && !dev_recursion_level() ?
				inet6_sk(skb->sk) : NULL;

	return (np && np->pmtudisc >= IPV6_PMTUDISC_PROBE) ?
	       skb_dst(skb)->dev->mtu : dst_mtu(skb_dst(skb));
}

static inline bool ip6_sk_accept_pmtu(const struct sock *sk)
{
	return inet6_sk(sk)->pmtudisc != IPV6_PMTUDISC_INTERFACE &&
	       inet6_sk(sk)->pmtudisc != IPV6_PMTUDISC_OMIT;
}

static inline bool ip6_sk_ignore_df(const struct sock *sk)
{
	return inet6_sk(sk)->pmtudisc < IPV6_PMTUDISC_DO ||
	       inet6_sk(sk)->pmtudisc == IPV6_PMTUDISC_OMIT;
}

static inline struct in6_addr *rt6_nexthop(struct rt6_info *rt,
					   struct in6_addr *daddr)
{
	if (rt->rt6i_flags & RTF_GATEWAY)
		return &rt->rt6i_gateway;
	else if (unlikely(rt->rt6i_flags & RTF_CACHE))
		return &rt->rt6i_dst.addr;
	else
		return daddr;
}

#endif
