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
	ETHTOOL_MSG_STRSET_GET,
	ETHTOOL_MSG_LINKINFO_GET,
	ETHTOOL_MSG_LINKINFO_SET,
	ETHTOOL_MSG_LINKMODES_GET,
	ETHTOOL_MSG_LINKMODES_SET,
	ETHTOOL_MSG_LINKSTATE_GET,
	ETHTOOL_MSG_DEBUG_GET,
	ETHTOOL_MSG_DEBUG_SET,
	ETHTOOL_MSG_WOL_GET,
	ETHTOOL_MSG_WOL_SET,

	/* add new constants above here */
	__ETHTOOL_MSG_USER_CNT,
	ETHTOOL_MSG_USER_MAX = __ETHTOOL_MSG_USER_CNT - 1
};

/* message types - kernel to userspace */
enum {
	ETHTOOL_MSG_KERNEL_NONE,
	ETHTOOL_MSG_STRSET_GET_REPLY,
	ETHTOOL_MSG_LINKINFO_GET_REPLY,
	ETHTOOL_MSG_LINKINFO_NTF,
	ETHTOOL_MSG_LINKMODES_GET_REPLY,
	ETHTOOL_MSG_LINKMODES_NTF,
	ETHTOOL_MSG_LINKSTATE_GET_REPLY,
	ETHTOOL_MSG_DEBUG_GET_REPLY,
	ETHTOOL_MSG_DEBUG_NTF,
	ETHTOOL_MSG_WOL_GET_REPLY,
	ETHTOOL_MSG_WOL_NTF,

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

/* bit sets */

enum {
	ETHTOOL_A_BITSET_BIT_UNSPEC,
	ETHTOOL_A_BITSET_BIT_INDEX,		/* u32 */
	ETHTOOL_A_BITSET_BIT_NAME,		/* string */
	ETHTOOL_A_BITSET_BIT_VALUE,		/* flag */

	/* add new constants above here */
	__ETHTOOL_A_BITSET_BIT_CNT,
	ETHTOOL_A_BITSET_BIT_MAX = __ETHTOOL_A_BITSET_BIT_CNT - 1
};

enum {
	ETHTOOL_A_BITSET_BITS_UNSPEC,
	ETHTOOL_A_BITSET_BITS_BIT,		/* nest - _A_BITSET_BIT_* */

	/* add new constants above here */
	__ETHTOOL_A_BITSET_BITS_CNT,
	ETHTOOL_A_BITSET_BITS_MAX = __ETHTOOL_A_BITSET_BITS_CNT - 1
};

enum {
	ETHTOOL_A_BITSET_UNSPEC,
	ETHTOOL_A_BITSET_NOMASK,		/* flag */
	ETHTOOL_A_BITSET_SIZE,			/* u32 */
	ETHTOOL_A_BITSET_BITS,			/* nest - _A_BITSET_BITS_* */
	ETHTOOL_A_BITSET_VALUE,			/* binary */
	ETHTOOL_A_BITSET_MASK,			/* binary */

	/* add new constants above here */
	__ETHTOOL_A_BITSET_CNT,
	ETHTOOL_A_BITSET_MAX = __ETHTOOL_A_BITSET_CNT - 1
};

/* string sets */

enum {
	ETHTOOL_A_STRING_UNSPEC,
	ETHTOOL_A_STRING_INDEX,			/* u32 */
	ETHTOOL_A_STRING_VALUE,			/* string */

	/* add new constants above here */
	__ETHTOOL_A_STRING_CNT,
	ETHTOOL_A_STRING_MAX = __ETHTOOL_A_STRING_CNT - 1
};

enum {
	ETHTOOL_A_STRINGS_UNSPEC,
	ETHTOOL_A_STRINGS_STRING,		/* nest - _A_STRINGS_* */

	/* add new constants above here */
	__ETHTOOL_A_STRINGS_CNT,
	ETHTOOL_A_STRINGS_MAX = __ETHTOOL_A_STRINGS_CNT - 1
};

enum {
	ETHTOOL_A_STRINGSET_UNSPEC,
	ETHTOOL_A_STRINGSET_ID,			/* u32 */
	ETHTOOL_A_STRINGSET_COUNT,		/* u32 */
	ETHTOOL_A_STRINGSET_STRINGS,		/* nest - _A_STRINGS_* */

	/* add new constants above here */
	__ETHTOOL_A_STRINGSET_CNT,
	ETHTOOL_A_STRINGSET_MAX = __ETHTOOL_A_STRINGSET_CNT - 1
};

enum {
	ETHTOOL_A_STRINGSETS_UNSPEC,
	ETHTOOL_A_STRINGSETS_STRINGSET,		/* nest - _A_STRINGSET_* */

	/* add new constants above here */
	__ETHTOOL_A_STRINGSETS_CNT,
	ETHTOOL_A_STRINGSETS_MAX = __ETHTOOL_A_STRINGSETS_CNT - 1
};

/* STRSET */

enum {
	ETHTOOL_A_STRSET_UNSPEC,
	ETHTOOL_A_STRSET_HEADER,		/* nest - _A_HEADER_* */
	ETHTOOL_A_STRSET_STRINGSETS,		/* nest - _A_STRINGSETS_* */
	ETHTOOL_A_STRSET_COUNTS_ONLY,		/* flag */

	/* add new constants above here */
	__ETHTOOL_A_STRSET_CNT,
	ETHTOOL_A_STRSET_MAX = __ETHTOOL_A_STRSET_CNT - 1
};

/* LINKINFO */

enum {
	ETHTOOL_A_LINKINFO_UNSPEC,
	ETHTOOL_A_LINKINFO_HEADER,		/* nest - _A_HEADER_* */
	ETHTOOL_A_LINKINFO_PORT,		/* u8 */
	ETHTOOL_A_LINKINFO_PHYADDR,		/* u8 */
	ETHTOOL_A_LINKINFO_TP_MDIX,		/* u8 */
	ETHTOOL_A_LINKINFO_TP_MDIX_CTRL,	/* u8 */
	ETHTOOL_A_LINKINFO_TRANSCEIVER,		/* u8 */

	/* add new constants above here */
	__ETHTOOL_A_LINKINFO_CNT,
	ETHTOOL_A_LINKINFO_MAX = __ETHTOOL_A_LINKINFO_CNT - 1
};

/* LINKMODES */

enum {
	ETHTOOL_A_LINKMODES_UNSPEC,
	ETHTOOL_A_LINKMODES_HEADER,		/* nest - _A_HEADER_* */
	ETHTOOL_A_LINKMODES_AUTONEG,		/* u8 */
	ETHTOOL_A_LINKMODES_OURS,		/* bitset */
	ETHTOOL_A_LINKMODES_PEER,		/* bitset */
	ETHTOOL_A_LINKMODES_SPEED,		/* u32 */
	ETHTOOL_A_LINKMODES_DUPLEX,		/* u8 */

	/* add new constants above here */
	__ETHTOOL_A_LINKMODES_CNT,
	ETHTOOL_A_LINKMODES_MAX = __ETHTOOL_A_LINKMODES_CNT - 1
};

/* LINKSTATE */

enum {
	ETHTOOL_A_LINKSTATE_UNSPEC,
	ETHTOOL_A_LINKSTATE_HEADER,		/* nest - _A_HEADER_* */
	ETHTOOL_A_LINKSTATE_LINK,		/* u8 */

	/* add new constants above here */
	__ETHTOOL_A_LINKSTATE_CNT,
	ETHTOOL_A_LINKSTATE_MAX = __ETHTOOL_A_LINKSTATE_CNT - 1
};

/* DEBUG */

enum {
	ETHTOOL_A_DEBUG_UNSPEC,
	ETHTOOL_A_DEBUG_HEADER,			/* nest - _A_HEADER_* */
	ETHTOOL_A_DEBUG_MSGMASK,		/* bitset */

	/* add new constants above here */
	__ETHTOOL_A_DEBUG_CNT,
	ETHTOOL_A_DEBUG_MAX = __ETHTOOL_A_DEBUG_CNT - 1
};

/* WOL */

enum {
	ETHTOOL_A_WOL_UNSPEC,
	ETHTOOL_A_WOL_HEADER,			/* nest - _A_HEADER_* */
	ETHTOOL_A_WOL_MODES,			/* bitset */
	ETHTOOL_A_WOL_SOPASS,			/* binary */

	/* add new constants above here */
	__ETHTOOL_A_WOL_CNT,
	ETHTOOL_A_WOL_MAX = __ETHTOOL_A_WOL_CNT - 1
};

/* generic netlink info */
#define ETHTOOL_GENL_NAME "ethtool"
#define ETHTOOL_GENL_VERSION 1

#define ETHTOOL_MCGRP_MONITOR_NAME "monitor"

#endif /* _UAPI_LINUX_ETHTOOL_NETLINK_H_ */
