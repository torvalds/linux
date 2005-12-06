#ifndef __LINUX_GENERIC_NETLINK_H
#define __LINUX_GENERIC_NETLINK_H

#include <linux/netlink.h>

#define GENL_NAMSIZ	16	/* length of family name */

#define GENL_MIN_ID	NLMSG_MIN_TYPE
#define GENL_MAX_ID	1023

struct genlmsghdr {
	__u8	cmd;
	__u8	version;
	__u16	reserved;
};

#define GENL_HDRLEN	NLMSG_ALIGN(sizeof(struct genlmsghdr))

/*
 * List of reserved static generic netlink identifiers:
 */
#define GENL_ID_GENERATE	0
#define GENL_ID_CTRL		NLMSG_MIN_TYPE

/**************************************************************************
 * Controller
 **************************************************************************/

enum {
	CTRL_CMD_UNSPEC,
	CTRL_CMD_NEWFAMILY,
	CTRL_CMD_DELFAMILY,
	CTRL_CMD_GETFAMILY,
	CTRL_CMD_NEWOPS,
	CTRL_CMD_DELOPS,
	CTRL_CMD_GETOPS,
	__CTRL_CMD_MAX,
};

#define CTRL_CMD_MAX (__CTRL_CMD_MAX - 1)

enum {
	CTRL_ATTR_UNSPEC,
	CTRL_ATTR_FAMILY_ID,
	CTRL_ATTR_FAMILY_NAME,
	__CTRL_ATTR_MAX,
};

#define CTRL_ATTR_MAX (__CTRL_ATTR_MAX - 1)

#endif	/* __LINUX_GENERIC_NETLINK_H */
