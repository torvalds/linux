#ifndef _LINUX_ERRQUEUE_H
#define _LINUX_ERRQUEUE_H 1

struct sock_extended_err
{
	__u32	ee_errno;	
	__u8	ee_origin;
	__u8	ee_type;
	__u8	ee_code;
	__u8	ee_pad;
	__u32   ee_info;
	__u32   ee_data;
};

#define SO_EE_ORIGIN_NONE	0
#define SO_EE_ORIGIN_LOCAL	1
#define SO_EE_ORIGIN_ICMP	2
#define SO_EE_ORIGIN_ICMP6	3

#define SO_EE_OFFENDER(ee)	((struct sockaddr*)((ee)+1))

#ifdef __KERNEL__

#include <linux/config.h>
#include <net/ip.h>
#if defined(CONFIG_IPV6) || defined (CONFIG_IPV6_MODULE)
#include <linux/ipv6.h>
#endif

#define SKB_EXT_ERR(skb) ((struct sock_exterr_skb *) ((skb)->cb))

struct sock_exterr_skb
{
	union {
		struct inet_skb_parm	h4;
#if defined(CONFIG_IPV6) || defined (CONFIG_IPV6_MODULE)
		struct inet6_skb_parm	h6;
#endif
	} header;
	struct sock_extended_err	ee;
	u16				addr_offset;
	u16				port;
};

#endif

#endif
