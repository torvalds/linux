/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _IP6_TUNNEL_H
#define _IP6_TUNNEL_H

#include <linux/types.h>
#include <linux/if.h>		/* For IFNAMSIZ. */
#include <linux/in6.h>		/* For struct in6_addr. */

#define IPV6_TLV_TNL_ENCAP_LIMIT 4
#define IPV6_DEFAULT_TNL_ENCAP_LIMIT 4

/* don't add encapsulation limit if one isn't present in inner packet */
#define IP6_TNL_F_IGN_ENCAP_LIMIT 0x1
/* copy the traffic class field from the inner packet */
#define IP6_TNL_F_USE_ORIG_TCLASS 0x2
/* copy the flowlabel from the inner packet */
#define IP6_TNL_F_USE_ORIG_FLOWLABEL 0x4
/* being used for Mobile IPv6 */
#define IP6_TNL_F_MIP6_DEV 0x8
/* copy DSCP from the outer packet */
#define IP6_TNL_F_RCV_DSCP_COPY 0x10
/* copy fwmark from inner packet */
#define IP6_TNL_F_USE_ORIG_FWMARK 0x20
/* allow remote endpoint on the local node */
#define IP6_TNL_F_ALLOW_LOCAL_REMOTE 0x40

struct ip6_tnl_parm {
	char name[IFNAMSIZ];	/* name of tunnel device */
	int link;		/* ifindex of underlying L2 interface */
	__u8 proto;		/* tunnel protocol */
	__u8 encap_limit;	/* encapsulation limit for tunnel */
	__u8 hop_limit;		/* hop limit for tunnel */
	__be32 flowinfo;	/* traffic class and flowlabel for tunnel */
	__u32 flags;		/* tunnel flags */
	struct in6_addr laddr;	/* local tunnel end-point address */
	struct in6_addr raddr;	/* remote tunnel end-point address */
};

struct ip6_tnl_parm2 {
	char name[IFNAMSIZ];	/* name of tunnel device */
	int link;		/* ifindex of underlying L2 interface */
	__u8 proto;		/* tunnel protocol */
	__u8 encap_limit;	/* encapsulation limit for tunnel */
	__u8 hop_limit;		/* hop limit for tunnel */
	__be32 flowinfo;	/* traffic class and flowlabel for tunnel */
	__u32 flags;		/* tunnel flags */
	struct in6_addr laddr;	/* local tunnel end-point address */
	struct in6_addr raddr;	/* remote tunnel end-point address */

	__be16			i_flags;
	__be16			o_flags;
	__be32			i_key;
	__be32			o_key;
};

#endif
