#ifndef _IF_TUNNEL_H_
#define _IF_TUNNEL_H_

#include <linux/types.h>

#ifdef __KERNEL__
#include <linux/ip.h>
#endif

#define SIOCGETTUNNEL   (SIOCDEVPRIVATE + 0)
#define SIOCADDTUNNEL   (SIOCDEVPRIVATE + 1)
#define SIOCDELTUNNEL   (SIOCDEVPRIVATE + 2)
#define SIOCCHGTUNNEL   (SIOCDEVPRIVATE + 3)
#define SIOCGETPRL      (SIOCDEVPRIVATE + 4)
#define SIOCADDPRL      (SIOCDEVPRIVATE + 5)
#define SIOCDELPRL      (SIOCDEVPRIVATE + 6)
#define SIOCCHGPRL      (SIOCDEVPRIVATE + 7)

#define GRE_CSUM	__constant_htons(0x8000)
#define GRE_ROUTING	__constant_htons(0x4000)
#define GRE_KEY		__constant_htons(0x2000)
#define GRE_SEQ		__constant_htons(0x1000)
#define GRE_STRICT	__constant_htons(0x0800)
#define GRE_REC		__constant_htons(0x0700)
#define GRE_FLAGS	__constant_htons(0x00F8)
#define GRE_VERSION	__constant_htons(0x0007)

struct ip_tunnel_parm
{
	char			name[IFNAMSIZ];
	int			link;
	__be16			i_flags;
	__be16			o_flags;
	__be32			i_key;
	__be32			o_key;
	struct iphdr		iph;
};

/* SIT-mode i_flags */
#define	SIT_ISATAP	0x0001

struct ip_tunnel_prl {
	__be32			addr;
	__u16			flags;
	__u16			__reserved;
	__u32			datalen;
	__u32			__reserved2;
	/* data follows */
};

/* PRL flags */
#define	PRL_DEFAULT		0x0001

enum
{
	IFLA_GRE_UNSPEC,
	IFLA_GRE_LINK,
	IFLA_GRE_IFLAGS,
	IFLA_GRE_OFLAGS,
	IFLA_GRE_IKEY,
	IFLA_GRE_OKEY,
	IFLA_GRE_LOCAL,
	IFLA_GRE_REMOTE,
	IFLA_GRE_TTL,
	IFLA_GRE_TOS,
	IFLA_GRE_PMTUDISC,
	__IFLA_GRE_MAX,
};

#define IFLA_GRE_MAX	(__IFLA_GRE_MAX - 1)

#endif /* _IF_TUNNEL_H_ */
