/* IPv4-specific defines for netfilter. 
 * (C)1998 Rusty Russell -- This code is GPL.
 */
#ifndef __LINUX_IP_NETFILTER_H
#define __LINUX_IP_NETFILTER_H

#include <uapi/linux/netfilter_ipv4.h>

/* Extra routing may needed on local out, as the QUEUE target never returns
 * control to the table.
 */
struct ip_rt_info {
	__be32 daddr;
	__be32 saddr;
	u_int8_t tos;
	u_int32_t mark;
};

int ip_route_me_harder(struct net *net, struct sock *sk, struct sk_buff *skb, unsigned addr_type);

struct nf_queue_entry;

#ifdef CONFIG_INET
__sum16 nf_ip_checksum(struct sk_buff *skb, unsigned int hook,
		       unsigned int dataoff, u_int8_t protocol);
int nf_ip_route(struct net *net, struct dst_entry **dst, struct flowi *fl,
		bool strict);
#else
static inline __sum16 nf_ip_checksum(struct sk_buff *skb, unsigned int hook,
				     unsigned int dataoff, u_int8_t protocol)
{
	return 0;
}
static inline int nf_ip_route(struct net *net, struct dst_entry **dst,
			      struct flowi *fl, bool strict)
{
	return -EOPNOTSUPP;
}
#endif /* CONFIG_INET */

#endif /*__LINUX_IP_NETFILTER_H*/
