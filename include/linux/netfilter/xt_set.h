#ifndef _XT_SET_H
#define _XT_SET_H

#include <linux/netfilter/ipset/ip_set.h>

/* Revision 0 interface: backward compatible with netfilter/iptables */

/*
 * Option flags for kernel operations (xt_set_info_v0)
 */
#define IPSET_SRC		0x01	/* Source match/add */
#define IPSET_DST		0x02	/* Destination match/add */
#define IPSET_MATCH_INV		0x04	/* Inverse matching */

struct xt_set_info_v0 {
	ip_set_id_t index;
	union {
		__u32 flags[IPSET_DIM_MAX + 1];
		struct {
			__u32 __flags[IPSET_DIM_MAX];
			__u8 dim;
			__u8 flags;
		} compat;
	} u;
};

/* match and target infos */
struct xt_set_info_match_v0 {
	struct xt_set_info_v0 match_set;
};

struct xt_set_info_target_v0 {
	struct xt_set_info_v0 add_set;
	struct xt_set_info_v0 del_set;
};

/* Revision 1: current interface to netfilter/iptables */

struct xt_set_info {
	ip_set_id_t index;
	__u8 dim;
	__u8 flags;
};

/* match and target infos */
struct xt_set_info_match {
	struct xt_set_info match_set;
};

struct xt_set_info_target {
	struct xt_set_info add_set;
	struct xt_set_info del_set;
};

#endif /*_XT_SET_H*/
