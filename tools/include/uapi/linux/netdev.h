/* SPDX-License-Identifier: ((GPL-2.0 WITH Linux-syscall-note) OR BSD-3-Clause) */
/* Do not edit directly, auto-generated from: */
/*	Documentation/netlink/specs/netdev.yaml */
/* YNL-GEN uapi header */

#ifndef _UAPI_LINUX_NETDEV_H
#define _UAPI_LINUX_NETDEV_H

#define NETDEV_FAMILY_NAME	"netdev"
#define NETDEV_FAMILY_VERSION	1

/**
 * enum netdev_xdp_act
 * @NETDEV_XDP_ACT_BASIC: XDP features set supported by all drivers
 *   (XDP_ABORTED, XDP_DROP, XDP_PASS, XDP_TX)
 * @NETDEV_XDP_ACT_REDIRECT: The netdev supports XDP_REDIRECT
 * @NETDEV_XDP_ACT_NDO_XMIT: This feature informs if netdev implements
 *   ndo_xdp_xmit callback.
 * @NETDEV_XDP_ACT_XSK_ZEROCOPY: This feature informs if netdev supports AF_XDP
 *   in zero copy mode.
 * @NETDEV_XDP_ACT_HW_OFFLOAD: This feature informs if netdev supports XDP hw
 *   offloading.
 * @NETDEV_XDP_ACT_RX_SG: This feature informs if netdev implements non-linear
 *   XDP buffer support in the driver napi callback.
 * @NETDEV_XDP_ACT_NDO_XMIT_SG: This feature informs if netdev implements
 *   non-linear XDP buffer support in ndo_xdp_xmit callback.
 */
enum netdev_xdp_act {
	NETDEV_XDP_ACT_BASIC = 1,
	NETDEV_XDP_ACT_REDIRECT = 2,
	NETDEV_XDP_ACT_NDO_XMIT = 4,
	NETDEV_XDP_ACT_XSK_ZEROCOPY = 8,
	NETDEV_XDP_ACT_HW_OFFLOAD = 16,
	NETDEV_XDP_ACT_RX_SG = 32,
	NETDEV_XDP_ACT_NDO_XMIT_SG = 64,

	/* private: */
	NETDEV_XDP_ACT_MASK = 127,
};

/**
 * enum netdev_xdp_rx_metadata
 * @NETDEV_XDP_RX_METADATA_TIMESTAMP: Device is capable of exposing receive HW
 *   timestamp via bpf_xdp_metadata_rx_timestamp().
 * @NETDEV_XDP_RX_METADATA_HASH: Device is capable of exposing receive packet
 *   hash via bpf_xdp_metadata_rx_hash().
 */
enum netdev_xdp_rx_metadata {
	NETDEV_XDP_RX_METADATA_TIMESTAMP = 1,
	NETDEV_XDP_RX_METADATA_HASH = 2,

	/* private: */
	NETDEV_XDP_RX_METADATA_MASK = 3,
};

enum {
	NETDEV_A_DEV_IFINDEX = 1,
	NETDEV_A_DEV_PAD,
	NETDEV_A_DEV_XDP_FEATURES,
	NETDEV_A_DEV_XDP_ZC_MAX_SEGS,
	NETDEV_A_DEV_XDP_RX_METADATA_FEATURES,

	__NETDEV_A_DEV_MAX,
	NETDEV_A_DEV_MAX = (__NETDEV_A_DEV_MAX - 1)
};

enum {
	NETDEV_CMD_DEV_GET = 1,
	NETDEV_CMD_DEV_ADD_NTF,
	NETDEV_CMD_DEV_DEL_NTF,
	NETDEV_CMD_DEV_CHANGE_NTF,

	__NETDEV_CMD_MAX,
	NETDEV_CMD_MAX = (__NETDEV_CMD_MAX - 1)
};

#define NETDEV_MCGRP_MGMT	"mgmt"

#endif /* _UAPI_LINUX_NETDEV_H */
