#ifndef _UAPI_IF_TUNNEL_H_
#define _UAPI_IF_TUNNEL_H_

#include <linux/types.h>
#include <asm/byteorder.h>


#define SIOCGETTUNNEL   (SIOCDEVPRIVATE + 0)
#define SIOCADDTUNNEL   (SIOCDEVPRIVATE + 1)
#define SIOCDELTUNNEL   (SIOCDEVPRIVATE + 2)
#define SIOCCHGTUNNEL   (SIOCDEVPRIVATE + 3)
#define SIOCGETPRL      (SIOCDEVPRIVATE + 4)
#define SIOCADDPRL      (SIOCDEVPRIVATE + 5)
#define SIOCDELPRL      (SIOCDEVPRIVATE + 6)
#define SIOCCHGPRL      (SIOCDEVPRIVATE + 7)
#define SIOCGET6RD      (SIOCDEVPRIVATE + 8)
#define SIOCADD6RD      (SIOCDEVPRIVATE + 9)
#define SIOCDEL6RD      (SIOCDEVPRIVATE + 10)
#define SIOCCHG6RD      (SIOCDEVPRIVATE + 11)

#define GRE_CSUM	__cpu_to_be16(0x8000)
#define GRE_ROUTING	__cpu_to_be16(0x4000)
#define GRE_KEY		__cpu_to_be16(0x2000)
#define GRE_SEQ		__cpu_to_be16(0x1000)
#define GRE_STRICT	__cpu_to_be16(0x0800)
#define GRE_REC		__cpu_to_be16(0x0700)
#define GRE_FLAGS	__cpu_to_be16(0x00F8)
#define GRE_VERSION	__cpu_to_be16(0x0007)

struct ip_tunnel_parm {
	char			name[IFNAMSIZ];
	int			link;
	__be16			i_flags;
	__be16			o_flags;
	__be32			i_key;
	__be32			o_key;
	struct iphdr		iph;
};

enum {
	IFLA_IPTUN_UNSPEC,
	IFLA_IPTUN_LINK,
	IFLA_IPTUN_LOCAL,
	IFLA_IPTUN_REMOTE,
	IFLA_IPTUN_TTL,
	IFLA_IPTUN_TOS,
	IFLA_IPTUN_ENCAP_LIMIT,
	IFLA_IPTUN_FLOWINFO,
	IFLA_IPTUN_FLAGS,
	IFLA_IPTUN_PROTO,
	__IFLA_IPTUN_MAX,
};
#define IFLA_IPTUN_MAX	(__IFLA_IPTUN_MAX - 1)

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

struct ip_tunnel_6rd {
	struct in6_addr		prefix;
	__be32			relay_prefix;
	__u16			prefixlen;
	__u16			relay_prefixlen;
};

enum {
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
	IFLA_GRE_ENCAP_LIMIT,
	IFLA_GRE_FLOWINFO,
	IFLA_GRE_FLAGS,
	__IFLA_GRE_MAX,
};

#define IFLA_GRE_MAX	(__IFLA_GRE_MAX - 1)

/* VTI-mode i_flags */
#define VTI_ISVTI 0x0001

enum {
	IFLA_VTI_UNSPEC,
	IFLA_VTI_LINK,
	IFLA_VTI_IKEY,
	IFLA_VTI_OKEY,
	IFLA_VTI_LOCAL,
	IFLA_VTI_REMOTE,
	__IFLA_VTI_MAX,
};

#define IFLA_VTI_MAX	(__IFLA_VTI_MAX - 1)
#endif /* _UAPI_IF_TUNNEL_H_ */
