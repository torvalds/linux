/* SPDX-License-Identifier: GPL-2.0+ WITH Linux-syscall-note */
/*
 * include/linux/if_team.h - Network team device driver header
 * Copyright (c) 2011 Jiri Pirko <jpirko@redhat.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef _UAPI_LINUX_IF_TEAM_H_
#define _UAPI_LINUX_IF_TEAM_H_


#define TEAM_STRING_MAX_LEN 32

/**********************************
 * NETLINK_GENERIC netlink family.
 **********************************/

enum {
	TEAM_CMD_NOOP,
	TEAM_CMD_OPTIONS_SET,
	TEAM_CMD_OPTIONS_GET,
	TEAM_CMD_PORT_LIST_GET,

	__TEAM_CMD_MAX,
	TEAM_CMD_MAX = (__TEAM_CMD_MAX - 1),
};

enum {
	TEAM_ATTR_UNSPEC,
	TEAM_ATTR_TEAM_IFINDEX,		/* u32 */
	TEAM_ATTR_LIST_OPTION,		/* nest */
	TEAM_ATTR_LIST_PORT,		/* nest */

	__TEAM_ATTR_MAX,
	TEAM_ATTR_MAX = __TEAM_ATTR_MAX - 1,
};

/* Nested layout of get/set msg:
 *
 *	[TEAM_ATTR_LIST_OPTION]
 *		[TEAM_ATTR_ITEM_OPTION]
 *			[TEAM_ATTR_OPTION_*], ...
 *		[TEAM_ATTR_ITEM_OPTION]
 *			[TEAM_ATTR_OPTION_*], ...
 *		...
 *	[TEAM_ATTR_LIST_PORT]
 *		[TEAM_ATTR_ITEM_PORT]
 *			[TEAM_ATTR_PORT_*], ...
 *		[TEAM_ATTR_ITEM_PORT]
 *			[TEAM_ATTR_PORT_*], ...
 *		...
 */

enum {
	TEAM_ATTR_ITEM_OPTION_UNSPEC,
	TEAM_ATTR_ITEM_OPTION,		/* nest */

	__TEAM_ATTR_ITEM_OPTION_MAX,
	TEAM_ATTR_ITEM_OPTION_MAX = __TEAM_ATTR_ITEM_OPTION_MAX - 1,
};

enum {
	TEAM_ATTR_OPTION_UNSPEC,
	TEAM_ATTR_OPTION_NAME,		/* string */
	TEAM_ATTR_OPTION_CHANGED,	/* flag */
	TEAM_ATTR_OPTION_TYPE,		/* u8 */
	TEAM_ATTR_OPTION_DATA,		/* dynamic */
	TEAM_ATTR_OPTION_REMOVED,	/* flag */
	TEAM_ATTR_OPTION_PORT_IFINDEX,	/* u32 */ /* for per-port options */
	TEAM_ATTR_OPTION_ARRAY_INDEX,	/* u32 */ /* for array options */

	__TEAM_ATTR_OPTION_MAX,
	TEAM_ATTR_OPTION_MAX = __TEAM_ATTR_OPTION_MAX - 1,
};

enum {
	TEAM_ATTR_ITEM_PORT_UNSPEC,
	TEAM_ATTR_ITEM_PORT,		/* nest */

	__TEAM_ATTR_ITEM_PORT_MAX,
	TEAM_ATTR_ITEM_PORT_MAX = __TEAM_ATTR_ITEM_PORT_MAX - 1,
};

enum {
	TEAM_ATTR_PORT_UNSPEC,
	TEAM_ATTR_PORT_IFINDEX,		/* u32 */
	TEAM_ATTR_PORT_CHANGED,		/* flag */
	TEAM_ATTR_PORT_LINKUP,		/* flag */
	TEAM_ATTR_PORT_SPEED,		/* u32 */
	TEAM_ATTR_PORT_DUPLEX,		/* u8 */
	TEAM_ATTR_PORT_REMOVED,		/* flag */

	__TEAM_ATTR_PORT_MAX,
	TEAM_ATTR_PORT_MAX = __TEAM_ATTR_PORT_MAX - 1,
};

/*
 * NETLINK_GENERIC related info
 */
#define TEAM_GENL_NAME "team"
#define TEAM_GENL_VERSION 0x1
#define TEAM_GENL_CHANGE_EVENT_MC_GRP_NAME "change_event"

#endif /* _UAPI_LINUX_IF_TEAM_H_ */
