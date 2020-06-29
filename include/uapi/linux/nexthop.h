/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _UAPI_LINUX_NEXTHOP_H
#define _UAPI_LINUX_NEXTHOP_H

#include <linux/types.h>

struct nhmsg {
	unsigned char	nh_family;
	unsigned char	nh_scope;     /* return only */
	unsigned char	nh_protocol;  /* Routing protocol that installed nh */
	unsigned char	resvd;
	unsigned int	nh_flags;     /* RTNH_F flags */
};

/* entry in a nexthop group */
struct nexthop_grp {
	__u32	id;	  /* nexthop id - must exist */
	__u8	weight;   /* weight of this nexthop */
	__u8	resvd1;
	__u16	resvd2;
};

enum {
	NEXTHOP_GRP_TYPE_MPATH,  /* default type if not specified */
	__NEXTHOP_GRP_TYPE_MAX,
};

#define NEXTHOP_GRP_TYPE_MAX (__NEXTHOP_GRP_TYPE_MAX - 1)

enum {
	NHA_UNSPEC,
	NHA_ID,		/* u32; id for nexthop. id == 0 means auto-assign */

	NHA_GROUP,	/* array of nexthop_grp */
	NHA_GROUP_TYPE,	/* u16 one of NEXTHOP_GRP_TYPE */
	/* if NHA_GROUP attribute is added, no other attributes can be set */

	NHA_BLACKHOLE,	/* flag; nexthop used to blackhole packets */
	/* if NHA_BLACKHOLE is added, OIF, GATEWAY, ENCAP can not be set */

	NHA_OIF,	/* u32; nexthop device */
	NHA_GATEWAY,	/* be32 (IPv4) or in6_addr (IPv6) gw address */
	NHA_ENCAP_TYPE, /* u16; lwt encap type */
	NHA_ENCAP,	/* lwt encap data */

	/* NHA_OIF can be appended to dump request to return only
	 * nexthops using given device
	 */
	NHA_GROUPS,	/* flag; only return nexthop groups in dump */
	NHA_MASTER,	/* u32;  only return nexthops with given master dev */

	NHA_FDB,	/* flag; nexthop belongs to a bridge fdb */
	/* if NHA_FDB is added, OIF, BLACKHOLE, ENCAP cannot be set */

	__NHA_MAX,
};

#define NHA_MAX	(__NHA_MAX - 1)
#endif
