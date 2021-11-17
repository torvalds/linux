/* IPv6-specific defines for netfilter. 
 * (C)1998 Rusty Russell -- This code is GPL.
 * (C)1999 David Jeffery
 *   this header was blatantly ripped from netfilter_ipv4.h
 *   it's amazing what adding a bunch of 6s can do =8^)
 */
#ifndef __LINUX_IP6_NETFILTER_H
#define __LINUX_IP6_NETFILTER_H

#include <uapi/linux/netfilter_ipv6.h>
#include <net/tcp.h>

/* Check for an extension */
static inline int
nf_ip6_ext_hdr(u8 nexthdr)
{	return (nexthdr == IPPROTO_HOPOPTS) ||
	       (nexthdr == IPPROTO_ROUTING) ||
	       (nexthdr == IPPROTO_FRAGMENT) ||
	       (nexthdr == IPPROTO_ESP) ||
	       (nexthdr == IPPROTO_AH) ||
	       (nexthdr == IPPROTO_NONE) ||
	       (nexthdr == IPPROTO_DSTOPTS);
}

/* Extra routing may needed on local out, as the QUEUE target never returns
 * control to the table.
 */
struct ip6_rt_info {
	struct in6_addr daddr;
	struct in6_addr saddr;
	u_int32_t mark;
};

struct nf_queue_entry;
struct nf_bridge_frag_data;

/*
 * Hook functions for ipv6 to allow xt_* modules to be built-in even
 * if IPv6 is a module.
 */
struct nf_ipv6_ops {
#if IS_MODULE(CONFIG_IPV6)
	int (*chk_addr)(struct net *net, const struct in6_addr *addr,
			const struct net_device *dev, int strict);
	int (*route_me_harder)(struct net *net, struct sock *sk, struct sk_buff *skb);
	int (*dev_get_saddr)(struct net *net, const struct net_device *dev,
		       const struct in6_addr *daddr, unsigned int srcprefs,
		       struct in6_addr *saddr);
	int (*route)(struct net *net, struct dst_entry **dst, struct flowi *fl,
		     bool strict);
	u32 (*cookie_init_sequence)(const struct ipv6hdr *iph,
				    const struct tcphdr *th, u16 *mssp);
	int (*cookie_v6_check)(const struct ipv6hdr *iph,
			       const struct tcphdr *th, __u32 cookie);
#endif
	void (*route_input)(struct sk_buff *skb);
	int (*fragment)(struct net *net, struct sock *sk, struct sk_buff *skb,
			int (*output)(struct net *, struct sock *, struct sk_buff *));
	int (*reroute)(struct sk_buff *skb, const struct nf_queue_entry *entry);
#if IS_MODULE(CONFIG_IPV6)
	int (*br_fragment)(struct net *net, struct sock *sk,
			   struct sk_buff *skb,
			   struct nf_bridge_frag_data *data,
			   int (*output)(struct net *, struct sock *sk,
					 const struct nf_bridge_frag_data *data,
					 struct sk_buff *));
#endif
};

#ifdef CONFIG_NETFILTER
#include <net/addrconf.h>

extern const struct nf_ipv6_ops __rcu *nf_ipv6_ops;
static inline const struct nf_ipv6_ops *nf_get_ipv6_ops(void)
{
	return rcu_dereference(nf_ipv6_ops);
}

static inline int nf_ipv6_chk_addr(struct net *net, const struct in6_addr *addr,
				   const struct net_device *dev, int strict)
{
#if IS_MODULE(CONFIG_IPV6)
	const struct nf_ipv6_ops *v6_ops = nf_get_ipv6_ops();

	if (!v6_ops)
		return 1;

	return v6_ops->chk_addr(net, addr, dev, strict);
#elif IS_BUILTIN(CONFIG_IPV6)
	return ipv6_chk_addr(net, addr, dev, strict);
#else
	return 1;
#endif
}

int __nf_ip6_route(struct net *net, struct dst_entry **dst,
			       struct flowi *fl, bool strict);

static inline int nf_ip6_route(struct net *net, struct dst_entry **dst,
			       struct flowi *fl, bool strict)
{
#if IS_MODULE(CONFIG_IPV6)
	const struct nf_ipv6_ops *v6ops = nf_get_ipv6_ops();

	if (v6ops)
		return v6ops->route(net, dst, fl, strict);

	return -EHOSTUNREACH;
#endif
#if IS_BUILTIN(CONFIG_IPV6)
	return __nf_ip6_route(net, dst, fl, strict);
#else
	return -EHOSTUNREACH;
#endif
}

#include <net/netfilter/ipv6/nf_defrag_ipv6.h>

int br_ip6_fragment(struct net *net, struct sock *sk, struct sk_buff *skb,
		    struct nf_bridge_frag_data *data,
		    int (*output)(struct net *, struct sock *sk,
				  const struct nf_bridge_frag_data *data,
				  struct sk_buff *));

static inline int nf_br_ip6_fragment(struct net *net, struct sock *sk,
				     struct sk_buff *skb,
				     struct nf_bridge_frag_data *data,
				     int (*output)(struct net *, struct sock *sk,
						   const struct nf_bridge_frag_data *data,
						   struct sk_buff *))
{
#if IS_MODULE(CONFIG_IPV6)
	const struct nf_ipv6_ops *v6_ops = nf_get_ipv6_ops();

	if (!v6_ops)
		return 1;

	return v6_ops->br_fragment(net, sk, skb, data, output);
#elif IS_BUILTIN(CONFIG_IPV6)
	return br_ip6_fragment(net, sk, skb, data, output);
#else
	return 1;
#endif
}

int ip6_route_me_harder(struct net *net, struct sock *sk, struct sk_buff *skb);

static inline int nf_ip6_route_me_harder(struct net *net, struct sock *sk, struct sk_buff *skb)
{
#if IS_MODULE(CONFIG_IPV6)
	const struct nf_ipv6_ops *v6_ops = nf_get_ipv6_ops();

	if (!v6_ops)
		return -EHOSTUNREACH;

	return v6_ops->route_me_harder(net, sk, skb);
#elif IS_BUILTIN(CONFIG_IPV6)
	return ip6_route_me_harder(net, sk, skb);
#else
	return -EHOSTUNREACH;
#endif
}

static inline u32 nf_ipv6_cookie_init_sequence(const struct ipv6hdr *iph,
					       const struct tcphdr *th,
					       u16 *mssp)
{
#if IS_ENABLED(CONFIG_SYN_COOKIES)
#if IS_MODULE(CONFIG_IPV6)
	const struct nf_ipv6_ops *v6_ops = nf_get_ipv6_ops();

	if (v6_ops)
		return v6_ops->cookie_init_sequence(iph, th, mssp);
#elif IS_BUILTIN(CONFIG_IPV6)
	return __cookie_v6_init_sequence(iph, th, mssp);
#endif
#endif
	return 0;
}

static inline int nf_cookie_v6_check(const struct ipv6hdr *iph,
				     const struct tcphdr *th, __u32 cookie)
{
#if IS_ENABLED(CONFIG_SYN_COOKIES)
#if IS_MODULE(CONFIG_IPV6)
	const struct nf_ipv6_ops *v6_ops = nf_get_ipv6_ops();

	if (v6_ops)
		return v6_ops->cookie_v6_check(iph, th, cookie);
#elif IS_BUILTIN(CONFIG_IPV6)
	return __cookie_v6_check(iph, th, cookie);
#endif
#endif
	return 0;
}

__sum16 nf_ip6_checksum(struct sk_buff *skb, unsigned int hook,
			unsigned int dataoff, u_int8_t protocol);

int ipv6_netfilter_init(void);
void ipv6_netfilter_fini(void);

#else /* CONFIG_NETFILTER */
static inline int ipv6_netfilter_init(void) { return 0; }
static inline void ipv6_netfilter_fini(void) { return; }
static inline const struct nf_ipv6_ops *nf_get_ipv6_ops(void) { return NULL; }
#endif /* CONFIG_NETFILTER */

#endif /*__LINUX_IP6_NETFILTER_H*/
