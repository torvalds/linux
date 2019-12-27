/* SPDX-License-Identifier: GPL-2.0-only WITH Linux-syscall-note */
/*
 * include/uapi/linux/ethtool_netlink.h - netlink interface for ethtool
 *
 * See Documentation/networking/ethtool-netlink.txt in kernel source tree for
 * doucumentation of the interface.
 */

#ifndef _UAPI_LINUX_ETHTOOL_NETLINK_H_
#define _UAPI_LINUX_ETHTOOL_NETLINK_H_

#include <linux/ethtool.h>

/* message types - userspace to kernel */
enum {
	ETHTOOL_MSG_USER_NONE,

	/* add new constants above here */
	__ETHTOOL_MSG_USER_CNT,
	ETHTOOL_MSG_USER_MAX = __ETHTOOL_MSG_USER_CNT - 1
};

/* message types - kernel to userspace */
enum {
	ETHTOOL_MSG_KERNEL_NONE,

	/* add new constants above here */
	__ETHTOOL_MSG_KERNEL_CNT,
	ETHTOOL_MSG_KERNEL_MAX = __ETHTOOL_MSG_KERNEL_CNT - 1
};

/* request header */

/* use compact bitsets in reply */
#define ETHTOOL_FLAG_COMPACT_BITSETS	(1 << 0)
/* provide optional reply for SET or ACT requests */
#define ETHTOOL_FLAG_OMIT_REPLY	(1 << 1)

#define ETHTOOL_FLAG_ALL (ETHTOOL_FLAG_COMPACT_BITSETS | \
			  ETHTOOL_FLAG_OMIT_REPLY)

enum {
	ETHTOOL_A_HEADER_UNSPEC,
	ETHTOOL_A_HEADER_DEV_INDEX,		/* u32 */
	ETHTOOL_A_HEADER_DEV_NAME,		/* string */
	ETHTOOL_A_HEADER_FLAGS,			/* u32 - ETHTOOL_FLAG_* */

	/* add new constants above here */
	__ETHTOOL_A_HEADER_CNT,
	ETHTOOL_A_HEADER_MAX = __ETHTOOL_A_HEADER_CNT - 1
};

/* generic netlink info */
#define ETHTOOL_GENL_NAME "ethtool"
#define ETHTOOL_GENL_VERSION 1

#endif /* _UAPI_LINUX_ETHTOOL_NETLINK_H_ */
