/* SPDX-License-Identifier: ((GPL-2.0 WITH Linux-syscall-note) OR BSD-3-Clause) */
/* Do not edit directly, auto-generated from: */
/*	Documentation/netlink/specs/net_shaper.yaml */
/* YNL-GEN uapi header */

#ifndef _UAPI_LINUX_NET_SHAPER_H
#define _UAPI_LINUX_NET_SHAPER_H

#define NET_SHAPER_FAMILY_NAME		"net-shaper"
#define NET_SHAPER_FAMILY_VERSION	1

/**
 * enum net_shaper_scope - Defines the shaper @id interpretation.
 * @NET_SHAPER_SCOPE_UNSPEC: The scope is not specified.
 * @NET_SHAPER_SCOPE_NETDEV: The main shaper for the given network device.
 * @NET_SHAPER_SCOPE_QUEUE: The shaper is attached to the given device queue,
 *   the @id represents the queue number.
 * @NET_SHAPER_SCOPE_NODE: The shaper allows grouping of queues or other node
 *   shapers; can be nested in either @netdev shapers or other @node shapers,
 *   allowing placement in any location of the scheduling tree, except leaves
 *   and root.
 */
enum net_shaper_scope {
	NET_SHAPER_SCOPE_UNSPEC,
	NET_SHAPER_SCOPE_NETDEV,
	NET_SHAPER_SCOPE_QUEUE,
	NET_SHAPER_SCOPE_NODE,

	/* private: */
	__NET_SHAPER_SCOPE_MAX,
	NET_SHAPER_SCOPE_MAX = (__NET_SHAPER_SCOPE_MAX - 1)
};

/**
 * enum net_shaper_metric - Different metric supported by the shaper.
 * @NET_SHAPER_METRIC_BPS: Shaper operates on a bits per second basis.
 * @NET_SHAPER_METRIC_PPS: Shaper operates on a packets per second basis.
 */
enum net_shaper_metric {
	NET_SHAPER_METRIC_BPS,
	NET_SHAPER_METRIC_PPS,
};

enum {
	NET_SHAPER_A_HANDLE = 1,
	NET_SHAPER_A_METRIC,
	NET_SHAPER_A_BW_MIN,
	NET_SHAPER_A_BW_MAX,
	NET_SHAPER_A_BURST,
	NET_SHAPER_A_PRIORITY,
	NET_SHAPER_A_WEIGHT,
	NET_SHAPER_A_IFINDEX,
	NET_SHAPER_A_PARENT,
	NET_SHAPER_A_LEAVES,

	__NET_SHAPER_A_MAX,
	NET_SHAPER_A_MAX = (__NET_SHAPER_A_MAX - 1)
};

enum {
	NET_SHAPER_A_HANDLE_SCOPE = 1,
	NET_SHAPER_A_HANDLE_ID,

	__NET_SHAPER_A_HANDLE_MAX,
	NET_SHAPER_A_HANDLE_MAX = (__NET_SHAPER_A_HANDLE_MAX - 1)
};

enum {
	NET_SHAPER_A_CAPS_IFINDEX = 1,
	NET_SHAPER_A_CAPS_SCOPE,
	NET_SHAPER_A_CAPS_SUPPORT_METRIC_BPS,
	NET_SHAPER_A_CAPS_SUPPORT_METRIC_PPS,
	NET_SHAPER_A_CAPS_SUPPORT_NESTING,
	NET_SHAPER_A_CAPS_SUPPORT_BW_MIN,
	NET_SHAPER_A_CAPS_SUPPORT_BW_MAX,
	NET_SHAPER_A_CAPS_SUPPORT_BURST,
	NET_SHAPER_A_CAPS_SUPPORT_PRIORITY,
	NET_SHAPER_A_CAPS_SUPPORT_WEIGHT,

	__NET_SHAPER_A_CAPS_MAX,
	NET_SHAPER_A_CAPS_MAX = (__NET_SHAPER_A_CAPS_MAX - 1)
};

enum {
	NET_SHAPER_CMD_GET = 1,
	NET_SHAPER_CMD_SET,
	NET_SHAPER_CMD_DELETE,
	NET_SHAPER_CMD_GROUP,
	NET_SHAPER_CMD_CAP_GET,

	__NET_SHAPER_CMD_MAX,
	NET_SHAPER_CMD_MAX = (__NET_SHAPER_CMD_MAX - 1)
};

#endif /* _UAPI_LINUX_NET_SHAPER_H */
