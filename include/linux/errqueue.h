#ifndef _LINUX_ERRQUEUE_H
#define _LINUX_ERRQUEUE_H 1


#include <net/ip.h>
#if IS_ENABLED(CONFIG_IPV6)
#include <linux/ipv6.h>
#endif
#include <uapi/linux/errqueue.h>

#define SKB_EXT_ERR(skb) ((struct sock_exterr_skb *) ((skb)->cb))

struct sock_exterr_skb {
	union {
		struct inet_skb_parm	h4;
#if IS_ENABLED(CONFIG_IPV6)
		struct inet6_skb_parm	h6;
#endif
	} header;
	struct sock_extended_err	ee;
	u16				addr_offset;
	__be16				port;
};

#endif
