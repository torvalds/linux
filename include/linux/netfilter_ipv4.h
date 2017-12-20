/* IPv4-specific defines for netfilter. 
 * (C)1998 Rusty Russell -- This code is GPL.
 */
#ifndef __LINUX_IP_NETFILTER_H
#define __LINUX_IP_NETFILTER_H

#include <uapi/linux/netfilter_ipv4.h>

int ip_route_me_harder(struct net *net, struct sk_buff *skb, unsigned addr_type);

#ifdef CONFIG_INET
__sum16 nf_ip_checksum(struct sk_buff *skb, unsigned int hook,
		       unsigned int dataoff, u_int8_t protocol);
__sum16 nf_ip_checksum_partial(struct sk_buff *skb, unsigned int hook,
			       unsigned int dataoff, unsigned int len,
			       u_int8_t protocol);
#else
static inline __sum16 nf_ip_checksum(struct sk_buff *skb, unsigned int hook,
				     unsigned int dataoff, u_int8_t protocol)
{
	return 0;
}
static inline __sum16 nf_ip_checksum_partial(struct sk_buff *skb,
					     unsigned int hook,
					     unsigned int dataoff,
					     unsigned int len,
					     u_int8_t protocol)
{
	return 0;
}
#endif /* CONFIG_INET */

#endif /*__LINUX_IP_NETFILTER_H*/
