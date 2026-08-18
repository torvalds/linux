/* SPDX-License-Identifier: ((GPL-2.0 WITH Linux-syscall-note) OR BSD-3-Clause) */
/* Do not edit directly, auto-generated from: */
/*	Documentation/netlink/specs/drm_ras.yaml */
/* YNL-GEN uapi header */
/* To regenerate run: tools/net/ynl/ynl-regen.sh */

#ifndef _UAPI_LINUX_DRM_RAS_H
#define _UAPI_LINUX_DRM_RAS_H

#define DRM_RAS_FAMILY_NAME	"drm-ras"
#define DRM_RAS_FAMILY_VERSION	1

/*
 * Type of the node. Currently, only error-counter nodes are supported, which
 * expose reliability counters for a hardware/software component.
 */
enum drm_ras_node_type {
	DRM_RAS_NODE_TYPE_ERROR_COUNTER = 1,
};

enum {
	DRM_RAS_A_NODE_ATTRS_NODE_ID = 1,
	DRM_RAS_A_NODE_ATTRS_DEVICE_NAME,
	DRM_RAS_A_NODE_ATTRS_NODE_NAME,
	DRM_RAS_A_NODE_ATTRS_NODE_TYPE,

	__DRM_RAS_A_NODE_ATTRS_MAX,
	DRM_RAS_A_NODE_ATTRS_MAX = (__DRM_RAS_A_NODE_ATTRS_MAX - 1)
};

enum {
	DRM_RAS_A_ERROR_COUNTER_ATTRS_NODE_ID = 1,
	DRM_RAS_A_ERROR_COUNTER_ATTRS_ERROR_ID,
	DRM_RAS_A_ERROR_COUNTER_ATTRS_ERROR_NAME,
	DRM_RAS_A_ERROR_COUNTER_ATTRS_ERROR_VALUE,

	__DRM_RAS_A_ERROR_COUNTER_ATTRS_MAX,
	DRM_RAS_A_ERROR_COUNTER_ATTRS_MAX = (__DRM_RAS_A_ERROR_COUNTER_ATTRS_MAX - 1)
};

enum {
	DRM_RAS_CMD_LIST_NODES = 1,
	DRM_RAS_CMD_GET_ERROR_COUNTER,
	DRM_RAS_CMD_CLEAR_ERROR_COUNTER,

	__DRM_RAS_CMD_MAX,
	DRM_RAS_CMD_MAX = (__DRM_RAS_CMD_MAX - 1)
};

#endif /* _UAPI_LINUX_DRM_RAS_H */
