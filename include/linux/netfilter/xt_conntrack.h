/* Header file for kernel module to match connection tracking information.
 * GPL (C) 2001  Marc Boucher (marc@mbsi.ca).
 */

#ifndef _XT_CONNTRACK_H
#define _XT_CONNTRACK_H

#include <linux/types.h>
#include <linux/netfilter/nf_conntrack_tuple_common.h>

#define XT_CONNTRACK_STATE_BIT(ctinfo) (1 << ((ctinfo)%IP_CT_IS_REPLY+1))
#define XT_CONNTRACK_STATE_INVALID (1 << 0)

#define XT_CONNTRACK_STATE_SNAT (1 << (IP_CT_NUMBER + 1))
#define XT_CONNTRACK_STATE_DNAT (1 << (IP_CT_NUMBER + 2))
#define XT_CONNTRACK_STATE_UNTRACKED (1 << (IP_CT_NUMBER + 3))

/* flags, invflags: */
enum {
	XT_CONNTRACK_STATE        = 1 << 0,
	XT_CONNTRACK_PROTO        = 1 << 1,
	XT_CONNTRACK_ORIGSRC      = 1 << 2,
	XT_CONNTRACK_ORIGDST      = 1 << 3,
	XT_CONNTRACK_REPLSRC      = 1 << 4,
	XT_CONNTRACK_REPLDST      = 1 << 5,
	XT_CONNTRACK_STATUS       = 1 << 6,
	XT_CONNTRACK_EXPIRES      = 1 << 7,
	XT_CONNTRACK_ORIGSRC_PORT = 1 << 8,
	XT_CONNTRACK_ORIGDST_PORT = 1 << 9,
	XT_CONNTRACK_REPLSRC_PORT = 1 << 10,
	XT_CONNTRACK_REPLDST_PORT = 1 << 11,
	XT_CONNTRACK_DIRECTION    = 1 << 12,
};

struct xt_conntrack_mtinfo1 {
	union nf_inet_addr origsrc_addr, origsrc_mask;
	union nf_inet_addr origdst_addr, origdst_mask;
	union nf_inet_addr replsrc_addr, replsrc_mask;
	union nf_inet_addr repldst_addr, repldst_mask;
	__u32 expires_min, expires_max;
	__u16 l4proto;
	__be16 origsrc_port, origdst_port;
	__be16 replsrc_port, repldst_port;
	__u16 match_flags, invert_flags;
	__u8 state_mask, status_mask;
};

struct xt_conntrack_mtinfo2 {
	union nf_inet_addr origsrc_addr, origsrc_mask;
	union nf_inet_addr origdst_addr, origdst_mask;
	union nf_inet_addr replsrc_addr, replsrc_mask;
	union nf_inet_addr repldst_addr, repldst_mask;
	__u32 expires_min, expires_max;
	__u16 l4proto;
	__be16 origsrc_port, origdst_port;
	__be16 replsrc_port, repldst_port;
	__u16 match_flags, invert_flags;
	__u16 state_mask, status_mask;
};

#endif /*_XT_CONNTRACK_H*/
