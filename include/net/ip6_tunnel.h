#ifndef _NET_IP6_TUNNEL_H
#define _NET_IP6_TUNNEL_H

#include <linux/ipv6.h>
#include <linux/netdevice.h>
#include <linux/ip6_tunnel.h>

/* capable of sending packets */
#define IP6_TNL_F_CAP_XMIT 0x10000
/* capable of receiving packets */
#define IP6_TNL_F_CAP_RCV 0x20000

/* IPv6 tunnel */

struct ip6_tnl {
	struct ip6_tnl *next;	/* next tunnel in list */
	struct net_device *dev;	/* virtual device associated with tunnel */
	struct ip6_tnl_parm parms;	/* tunnel configuration parameters */
	struct flowi fl;	/* flowi template for xmit */
	struct dst_entry *dst_cache;    /* cached dst */
	u32 dst_cookie;
};

/* Tunnel encapsulation limit destination sub-option */

struct ipv6_tlv_tnl_enc_lim {
	__u8 type;		/* type-code for option         */
	__u8 length;		/* option length                */
	__u8 encap_limit;	/* tunnel encapsulation limit   */
} __packed;

#endif
