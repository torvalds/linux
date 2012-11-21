#ifndef _LINUX_ICMPV6_H
#define _LINUX_ICMPV6_H

#include <linux/skbuff.h>
#include <uapi/linux/icmpv6.h>

static inline struct icmp6hdr *icmp6_hdr(const struct sk_buff *skb)
{
	return (struct icmp6hdr *)skb_transport_header(skb);
}

#include <linux/netdevice.h>

extern void				icmpv6_send(struct sk_buff *skb,
						    u8 type, u8 code,
						    __u32 info);

extern int				icmpv6_init(void);
extern int				icmpv6_err_convert(u8 type, u8 code,
							   int *err);
extern void				icmpv6_cleanup(void);
extern void				icmpv6_param_prob(struct sk_buff *skb,
							  u8 code, int pos);

struct flowi6;
struct in6_addr;
extern void				icmpv6_flow_init(struct sock *sk,
							 struct flowi6 *fl6,
							 u8 type,
							 const struct in6_addr *saddr,
							 const struct in6_addr *daddr,
							 int oif);
#endif
