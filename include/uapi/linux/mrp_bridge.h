/* SPDX-License-Identifier: GPL-2.0+ WITH Linux-syscall-note */

#ifndef _UAPI_LINUX_MRP_BRIDGE_H_
#define _UAPI_LINUX_MRP_BRIDGE_H_

#include <linux/types.h>
#include <linux/if_ether.h>

#define MRP_MAX_FRAME_LENGTH		200
#define MRP_DEFAULT_PRIO		0x8000
#define MRP_DOMAIN_UUID_LENGTH		16
#define MRP_VERSION			1
#define MRP_FRAME_PRIO			7
#define MRP_OUI_LENGTH			3
#define MRP_MANUFACTURE_DATA_LENGTH	2

enum br_mrp_ring_role_type {
	BR_MRP_RING_ROLE_DISABLED,
	BR_MRP_RING_ROLE_MRC,
	BR_MRP_RING_ROLE_MRM,
	BR_MRP_RING_ROLE_MRA,
};

enum br_mrp_ring_state_type {
	BR_MRP_RING_STATE_OPEN,
	BR_MRP_RING_STATE_CLOSED,
};

enum br_mrp_port_state_type {
	BR_MRP_PORT_STATE_DISABLED,
	BR_MRP_PORT_STATE_BLOCKED,
	BR_MRP_PORT_STATE_FORWARDING,
	BR_MRP_PORT_STATE_NOT_CONNECTED,
};

enum br_mrp_port_role_type {
	BR_MRP_PORT_ROLE_PRIMARY,
	BR_MRP_PORT_ROLE_SECONDARY,
	BR_MRP_PORT_ROLE_NONE,
};

enum br_mrp_tlv_header_type {
	BR_MRP_TLV_HEADER_END = 0x0,
	BR_MRP_TLV_HEADER_COMMON = 0x1,
	BR_MRP_TLV_HEADER_RING_TEST = 0x2,
	BR_MRP_TLV_HEADER_RING_TOPO = 0x3,
	BR_MRP_TLV_HEADER_RING_LINK_DOWN = 0x4,
	BR_MRP_TLV_HEADER_RING_LINK_UP = 0x5,
	BR_MRP_TLV_HEADER_OPTION = 0x7f,
};

enum br_mrp_sub_tlv_header_type {
	BR_MRP_SUB_TLV_HEADER_TEST_MGR_NACK = 0x1,
	BR_MRP_SUB_TLV_HEADER_TEST_PROPAGATE = 0x2,
	BR_MRP_SUB_TLV_HEADER_TEST_AUTO_MGR = 0x3,
};

struct br_mrp_tlv_hdr {
	__u8 type;
	__u8 length;
};

struct br_mrp_sub_tlv_hdr {
	__u8 type;
	__u8 length;
};

struct br_mrp_end_hdr {
	struct br_mrp_tlv_hdr hdr;
};

struct br_mrp_common_hdr {
	__be16 seq_id;
	__u8 domain[MRP_DOMAIN_UUID_LENGTH];
};

struct br_mrp_ring_test_hdr {
	__be16 prio;
	__u8 sa[ETH_ALEN];
	__be16 port_role;
	__be16 state;
	__be16 transitions;
	__be32 timestamp;
};

struct br_mrp_ring_topo_hdr {
	__be16 prio;
	__u8 sa[ETH_ALEN];
	__be16 interval;
};

struct br_mrp_ring_link_hdr {
	__u8 sa[ETH_ALEN];
	__be16 port_role;
	__be16 interval;
	__be16 blocked;
};

struct br_mrp_sub_opt_hdr {
	__u8 type;
	__u8 manufacture_data[MRP_MANUFACTURE_DATA_LENGTH];
};

struct br_mrp_test_mgr_nack_hdr {
	__be16 prio;
	__u8 sa[ETH_ALEN];
	__be16 other_prio;
	__u8 other_sa[ETH_ALEN];
};

struct br_mrp_test_prop_hdr {
	__be16 prio;
	__u8 sa[ETH_ALEN];
	__be16 other_prio;
	__u8 other_sa[ETH_ALEN];
};

struct br_mrp_oui_hdr {
	__u8 oui[MRP_OUI_LENGTH];
};

#endif
