/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_ICMPV6_H
#define _LINUX_ICMPV6_H

#include <linux/skbuff.h>
#include <uapi/linux/icmpv6.h>

static inline struct icmp6hdr *icmp6_hdr(const struct sk_buff *skb)
{
	return (struct icmp6hdr *)skb_transport_header(skb);
}

#include <linux/netdevice.h>

#if IS_ENABLED(CONFIG_IPV6)
extern void icmpv6_send(struct sk_buff *skb, u8 type, u8 code, __u32 info);

typedef void ip6_icmp_send_t(struct sk_buff *skb, u8 type, u8 code, __u32 info,
			     const struct in6_addr *force_saddr);
extern int inet6_register_icmp_sender(ip6_icmp_send_t *fn);
extern int inet6_unregister_icmp_sender(ip6_icmp_send_t *fn);
int ip6_err_gen_icmpv6_unreach(struct sk_buff *skb, int nhs, int type,
			       unsigned int data_len);

#if IS_ENABLED(CONFIG_NF_NAT)
void icmpv6_ndo_send(struct sk_buff *skb_in, u8 type, u8 code, __u32 info);
#else
#define icmpv6_ndo_send icmpv6_send
#endif

#else

static inline void icmpv6_send(struct sk_buff *skb,
			       u8 type, u8 code, __u32 info)
{
}

static inline void icmpv6_ndo_send(struct sk_buff *skb,
				   u8 type, u8 code, __u32 info)
{
}
#endif

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

static inline bool icmpv6_is_err(int type)
{
	switch (type) {
	case ICMPV6_DEST_UNREACH:
	case ICMPV6_PKT_TOOBIG:
	case ICMPV6_TIME_EXCEED:
	case ICMPV6_PARAMPROB:
		return true;
	}

	return false;
}

#endif
