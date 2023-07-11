/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_ICMPV6_H
#define _LINUX_ICMPV6_H

#include <linux/skbuff.h>
#include <linux/ipv6.h>
#include <uapi/linux/icmpv6.h>

static inline struct icmp6hdr *icmp6_hdr(const struct sk_buff *skb)
{
	return (struct icmp6hdr *)skb_transport_header(skb);
}

#include <linux/netdevice.h>

#if IS_ENABLED(CONFIG_IPV6)

typedef void ip6_icmp_send_t(struct sk_buff *skb, u8 type, u8 code, __u32 info,
			     const struct in6_addr *force_saddr,
			     const struct inet6_skb_parm *parm);
void icmp6_send(struct sk_buff *skb, u8 type, u8 code, __u32 info,
		const struct in6_addr *force_saddr,
		const struct inet6_skb_parm *parm);
#if IS_BUILTIN(CONFIG_IPV6)
static inline void __icmpv6_send(struct sk_buff *skb, u8 type, u8 code, __u32 info,
				 const struct inet6_skb_parm *parm)
{
	icmp6_send(skb, type, code, info, NULL, parm);
}
static inline int inet6_register_icmp_sender(ip6_icmp_send_t *fn)
{
	BUILD_BUG_ON(fn != icmp6_send);
	return 0;
}
static inline int inet6_unregister_icmp_sender(ip6_icmp_send_t *fn)
{
	BUILD_BUG_ON(fn != icmp6_send);
	return 0;
}
#else
extern void __icmpv6_send(struct sk_buff *skb, u8 type, u8 code, __u32 info,
			  const struct inet6_skb_parm *parm);
extern int inet6_register_icmp_sender(ip6_icmp_send_t *fn);
extern int inet6_unregister_icmp_sender(ip6_icmp_send_t *fn);
#endif

static inline void icmpv6_send(struct sk_buff *skb, u8 type, u8 code, __u32 info)
{
	__icmpv6_send(skb, type, code, info, IP6CB(skb));
}

int ip6_err_gen_icmpv6_unreach(struct sk_buff *skb, int nhs, int type,
			       unsigned int data_len);

#if IS_ENABLED(CONFIG_NF_NAT)
void icmpv6_ndo_send(struct sk_buff *skb_in, u8 type, u8 code, __u32 info);
#else
static inline void icmpv6_ndo_send(struct sk_buff *skb_in, u8 type, u8 code, __u32 info)
{
	struct inet6_skb_parm parm = { 0 };
	__icmpv6_send(skb_in, type, code, info, &parm);
}
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
extern void				icmpv6_param_prob_reason(struct sk_buff *skb,
								 u8 code, int pos,
								 enum skb_drop_reason reason);

struct flowi6;
struct in6_addr;

void icmpv6_flow_init(const struct sock *sk, struct flowi6 *fl6, u8 type,
		      const struct in6_addr *saddr,
		      const struct in6_addr *daddr, int oif);

static inline void icmpv6_param_prob(struct sk_buff *skb, u8 code, int pos)
{
	icmpv6_param_prob_reason(skb, code, pos,
				 SKB_DROP_REASON_NOT_SPECIFIED);
}

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
