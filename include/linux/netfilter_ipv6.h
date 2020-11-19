/* IPv6-specific defines for netfilter. 
 * (C)1998 Rusty Russell -- This code is GPL.
 * (C)1999 David Jeffery
 *   this header was blatantly ripped from netfilter_ipv4.h 
 *   it's amazing what adding a bunch of 6s can do =8^)
 */
#ifndef __LINUX_IP6_NETFILTER_H
#define __LINUX_IP6_NETFILTER_H

#include <uapi/linux/netfilter_ipv6.h>

/* Extra routing may needed on local out, as the QUEUE target never returns
 * control to the table.
 */
struct ip6_rt_info {
	struct in6_addr daddr;
	struct in6_addr saddr;
	u_int32_t mark;
};

struct nf_queue_entry;

/*
 * Hook functions for ipv6 to allow xt_* modules to be built-in even
 * if IPv6 is a module.
 */
struct nf_ipv6_ops {
	int (*chk_addr)(struct net *net, const struct in6_addr *addr,
			const struct net_device *dev, int strict);
	void (*route_input)(struct sk_buff *skb);
	int (*fragment)(struct net *net, struct sock *sk, struct sk_buff *skb,
			int (*output)(struct net *, struct sock *, struct sk_buff *));
	int (*route)(struct net *net, struct dst_entry **dst, struct flowi *fl,
		     bool strict);
	int (*reroute)(struct sk_buff *skb, const struct nf_queue_entry *entry);
};

#ifdef CONFIG_NETFILTER
int ip6_route_me_harder(struct net *net, struct sock *sk, struct sk_buff *skb);
__sum16 nf_ip6_checksum(struct sk_buff *skb, unsigned int hook,
			unsigned int dataoff, u_int8_t protocol);

int ipv6_netfilter_init(void);
void ipv6_netfilter_fini(void);

extern const struct nf_ipv6_ops __rcu *nf_ipv6_ops;
static inline const struct nf_ipv6_ops *nf_get_ipv6_ops(void)
{
	return rcu_dereference(nf_ipv6_ops);
}

#else /* CONFIG_NETFILTER */
static inline int ipv6_netfilter_init(void) { return 0; }
static inline void ipv6_netfilter_fini(void) { return; }
static inline const struct nf_ipv6_ops *nf_get_ipv6_ops(void) { return NULL; }
#endif /* CONFIG_NETFILTER */

#endif /*__LINUX_IP6_NETFILTER_H*/
