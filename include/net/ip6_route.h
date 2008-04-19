#ifndef _NET_IP6_ROUTE_H
#define _NET_IP6_ROUTE_H

#define IP6_RT_PRIO_USER	1024
#define IP6_RT_PRIO_ADDRCONF	256
#define IP6_RT_PRIO_KERN	512

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

#ifdef __KERNEL__

#include <net/flow.h>
#include <net/ip6_fib.h>
#include <net/sock.h>
#include <linux/ip.h>
#include <linux/ipv6.h>

#define RT6_LOOKUP_F_IFACE		0x00000001
#define RT6_LOOKUP_F_REACHABLE		0x00000002
#define RT6_LOOKUP_F_HAS_SADDR		0x00000004
#define RT6_LOOKUP_F_SRCPREF_TMP	0x00000008
#define RT6_LOOKUP_F_SRCPREF_PUBLIC	0x00000010
#define RT6_LOOKUP_F_SRCPREF_COA	0x00000020


#ifdef CONFIG_IPV6_MULTIPLE_TABLES
extern struct rt6_info	*ip6_prohibit_entry;
extern struct rt6_info	*ip6_blk_hole_entry;
#endif

extern void			ip6_route_input(struct sk_buff *skb);

extern struct dst_entry *	ip6_route_output(struct net *net,
						 struct sock *sk,
						 struct flowi *fl);

extern int			ip6_route_init(void);
extern void			ip6_route_cleanup(void);

extern int			ipv6_route_ioctl(struct net *net,
						 unsigned int cmd,
						 void __user *arg);

extern int			ip6_route_add(struct fib6_config *cfg);
extern int			ip6_ins_rt(struct rt6_info *);
extern int			ip6_del_rt(struct rt6_info *);

extern struct rt6_info		*rt6_lookup(struct net *net,
					    const struct in6_addr *daddr,
					    const struct in6_addr *saddr,
					    int oif, int flags);

extern struct dst_entry *icmp6_dst_alloc(struct net_device *dev,
					 struct neighbour *neigh,
					 const struct in6_addr *addr);
extern int icmp6_dst_gc(int *more);

extern void fib6_force_start_gc(struct net *net);

extern struct rt6_info *addrconf_dst_alloc(struct inet6_dev *idev,
					   const struct in6_addr *addr,
					   int anycast);

extern int			ip6_dst_hoplimit(struct dst_entry *dst);

/*
 *	support functions for ND
 *
 */
extern struct rt6_info *	rt6_get_dflt_router(struct in6_addr *addr,
						    struct net_device *dev);
extern struct rt6_info *	rt6_add_dflt_router(struct in6_addr *gwaddr,
						    struct net_device *dev,
						    unsigned int pref);

extern void			rt6_purge_dflt_routers(struct net *net);

extern int			rt6_route_rcv(struct net_device *dev,
					      u8 *opt, int len,
					      struct in6_addr *gwaddr);

extern void			rt6_redirect(struct in6_addr *dest,
					     struct in6_addr *src,
					     struct in6_addr *saddr,
					     struct neighbour *neigh,
					     u8 *lladdr,
					     int on_link);

extern void			rt6_pmtu_discovery(struct in6_addr *daddr,
						   struct in6_addr *saddr,
						   struct net_device *dev,
						   u32 pmtu);

struct netlink_callback;

struct rt6_rtnl_dump_arg
{
	struct sk_buff *skb;
	struct netlink_callback *cb;
};

extern int rt6_dump_route(struct rt6_info *rt, void *p_arg);
extern void rt6_ifdown(struct net *net, struct net_device *dev);
extern void rt6_mtu_change(struct net_device *dev, unsigned mtu);

extern rwlock_t rt6_lock;

/*
 *	Store a destination cache entry in a socket
 */
static inline void __ip6_dst_store(struct sock *sk, struct dst_entry *dst,
				   struct in6_addr *daddr, struct in6_addr *saddr)
{
	struct ipv6_pinfo *np = inet6_sk(sk);
	struct rt6_info *rt = (struct rt6_info *) dst;

	sk_setup_caps(sk, dst);
	np->daddr_cache = daddr;
#ifdef CONFIG_IPV6_SUBTREES
	np->saddr_cache = saddr;
#endif
	np->dst_cookie = rt->rt6i_node ? rt->rt6i_node->fn_sernum : 0;
}

static inline void ip6_dst_store(struct sock *sk, struct dst_entry *dst,
				 struct in6_addr *daddr, struct in6_addr *saddr)
{
	write_lock(&sk->sk_dst_lock);
	__ip6_dst_store(sk, dst, daddr, saddr);
	write_unlock(&sk->sk_dst_lock);
}

static inline int ipv6_unicast_destination(struct sk_buff *skb)
{
	struct rt6_info *rt = (struct rt6_info *) skb->dst;

	return rt->rt6i_flags & RTF_LOCAL;
}

#endif
#endif
