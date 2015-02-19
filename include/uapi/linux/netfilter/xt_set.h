#ifndef _XT_SET_H
#define _XT_SET_H

#include <linux/types.h>
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

/* Revision 1  match and target */

struct xt_set_info {
	ip_set_id_t index;
	__u8 dim;
	__u8 flags;
};

/* match and target infos */
struct xt_set_info_match_v1 {
	struct xt_set_info match_set;
};

struct xt_set_info_target_v1 {
	struct xt_set_info add_set;
	struct xt_set_info del_set;
};

/* Revision 2 target */

struct xt_set_info_target_v2 {
	struct xt_set_info add_set;
	struct xt_set_info del_set;
	__u32 flags;
	__u32 timeout;
};

/* Revision 3 match */

struct xt_set_info_match_v3 {
	struct xt_set_info match_set;
	struct ip_set_counter_match0 packets;
	struct ip_set_counter_match0 bytes;
	__u32 flags;
};

/* Revision 3 target */

struct xt_set_info_target_v3 {
	struct xt_set_info add_set;
	struct xt_set_info del_set;
	struct xt_set_info map_set;
	__u32 flags;
	__u32 timeout;
};

/* Revision 4 match */

struct xt_set_info_match_v4 {
	struct xt_set_info match_set;
	struct ip_set_counter_match packets;
	struct ip_set_counter_match bytes;
	__u32 flags;
};

#endif /*_XT_SET_H*/
