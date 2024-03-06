/* SPDX-License-Identifier: GPL-2.0 */
#pragma once

#ifndef ETH_P_IP
#define ETH_P_IP 0x0800
#endif

#ifndef ETH_P_IPV6
#define ETH_P_IPV6 0x86DD
#endif

#ifndef ETH_P_8021Q
#define ETH_P_8021Q 0x8100
#endif

#ifndef ETH_P_8021AD
#define ETH_P_8021AD 0x88A8
#endif

#ifndef BIT
#define BIT(nr)			(1 << (nr))
#endif

/* Non-existent checksum status */
#define XDP_CHECKSUM_MAGIC	BIT(2)

enum xdp_meta_field {
	XDP_META_FIELD_TS	= BIT(0),
	XDP_META_FIELD_RSS	= BIT(1),
	XDP_META_FIELD_VLAN_TAG	= BIT(2),
};

struct xdp_meta {
	union {
		__u64 rx_timestamp;
		__s32 rx_timestamp_err;
	};
	__u64 xdp_timestamp;
	__u32 rx_hash;
	union {
		__u32 rx_hash_type;
		__s32 rx_hash_err;
	};
	union {
		struct {
			__be16 rx_vlan_proto;
			__u16 rx_vlan_tci;
		};
		__s32 rx_vlan_tag_err;
	};
	enum xdp_meta_field hint_valid;
};
