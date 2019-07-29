/*
 * Copyright Samuel Mendoza-Jonas, IBM Corporation 2018.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef __UAPI_NCSI_NETLINK_H__
#define __UAPI_NCSI_NETLINK_H__

/**
 * enum ncsi_nl_commands - supported NCSI commands
 *
 * @NCSI_CMD_UNSPEC: unspecified command to catch errors
 * @NCSI_CMD_PKG_INFO: list package and channel attributes. Requires
 *	NCSI_ATTR_IFINDEX. If NCSI_ATTR_PACKAGE_ID is specified returns the
 *	specific package and its channels - otherwise a dump request returns
 *	all packages and their associated channels.
 * @NCSI_CMD_SET_INTERFACE: set preferred package and channel combination.
 *	Requires NCSI_ATTR_IFINDEX and the preferred NCSI_ATTR_PACKAGE_ID and
 *	optionally the preferred NCSI_ATTR_CHANNEL_ID.
 * @NCSI_CMD_CLEAR_INTERFACE: clear any preferred package/channel combination.
 *	Requires NCSI_ATTR_IFINDEX.
 * @NCSI_CMD_SEND_CMD: send NC-SI command to network card.
 *	Requires NCSI_ATTR_IFINDEX, NCSI_ATTR_PACKAGE_ID
 *	and NCSI_ATTR_CHANNEL_ID.
 * @NCSI_CMD_MAX: highest command number
 */
enum ncsi_nl_commands {
	NCSI_CMD_UNSPEC,
	NCSI_CMD_PKG_INFO,
	NCSI_CMD_SET_INTERFACE,
	NCSI_CMD_CLEAR_INTERFACE,
	NCSI_CMD_SEND_CMD,

	__NCSI_CMD_AFTER_LAST,
	NCSI_CMD_MAX = __NCSI_CMD_AFTER_LAST - 1
};

/**
 * enum ncsi_nl_attrs - General NCSI netlink attributes
 *
 * @NCSI_ATTR_UNSPEC: unspecified attributes to catch errors
 * @NCSI_ATTR_IFINDEX: ifindex of network device using NCSI
 * @NCSI_ATTR_PACKAGE_LIST: nested array of NCSI_PKG_ATTR attributes
 * @NCSI_ATTR_PACKAGE_ID: package ID
 * @NCSI_ATTR_CHANNEL_ID: channel ID
 * @NCSI_ATTR_DATA: command payload
 * @NCSI_ATTR_MAX: highest attribute number
 */
enum ncsi_nl_attrs {
	NCSI_ATTR_UNSPEC,
	NCSI_ATTR_IFINDEX,
	NCSI_ATTR_PACKAGE_LIST,
	NCSI_ATTR_PACKAGE_ID,
	NCSI_ATTR_CHANNEL_ID,
	NCSI_ATTR_DATA,

	__NCSI_ATTR_AFTER_LAST,
	NCSI_ATTR_MAX = __NCSI_ATTR_AFTER_LAST - 1
};

/**
 * enum ncsi_nl_pkg_attrs - NCSI netlink package-specific attributes
 *
 * @NCSI_PKG_ATTR_UNSPEC: unspecified attributes to catch errors
 * @NCSI_PKG_ATTR: nested array of package attributes
 * @NCSI_PKG_ATTR_ID: package ID
 * @NCSI_PKG_ATTR_FORCED: flag signifying a package has been set as preferred
 * @NCSI_PKG_ATTR_CHANNEL_LIST: nested array of NCSI_CHANNEL_ATTR attributes
 * @NCSI_PKG_ATTR_MAX: highest attribute number
 */
enum ncsi_nl_pkg_attrs {
	NCSI_PKG_ATTR_UNSPEC,
	NCSI_PKG_ATTR,
	NCSI_PKG_ATTR_ID,
	NCSI_PKG_ATTR_FORCED,
	NCSI_PKG_ATTR_CHANNEL_LIST,

	__NCSI_PKG_ATTR_AFTER_LAST,
	NCSI_PKG_ATTR_MAX = __NCSI_PKG_ATTR_AFTER_LAST - 1
};

/**
 * enum ncsi_nl_channel_attrs - NCSI netlink channel-specific attributes
 *
 * @NCSI_CHANNEL_ATTR_UNSPEC: unspecified attributes to catch errors
 * @NCSI_CHANNEL_ATTR: nested array of channel attributes
 * @NCSI_CHANNEL_ATTR_ID: channel ID
 * @NCSI_CHANNEL_ATTR_VERSION_MAJOR: channel major version number
 * @NCSI_CHANNEL_ATTR_VERSION_MINOR: channel minor version number
 * @NCSI_CHANNEL_ATTR_VERSION_STR: channel version string
 * @NCSI_CHANNEL_ATTR_LINK_STATE: channel link state flags
 * @NCSI_CHANNEL_ATTR_ACTIVE: channels with this flag are in
 *	NCSI_CHANNEL_ACTIVE state
 * @NCSI_CHANNEL_ATTR_FORCED: flag signifying a channel has been set as
 *	preferred
 * @NCSI_CHANNEL_ATTR_VLAN_LIST: nested array of NCSI_CHANNEL_ATTR_VLAN_IDs
 * @NCSI_CHANNEL_ATTR_VLAN_ID: VLAN ID being filtered on this channel
 * @NCSI_CHANNEL_ATTR_MAX: highest attribute number
 */
enum ncsi_nl_channel_attrs {
	NCSI_CHANNEL_ATTR_UNSPEC,
	NCSI_CHANNEL_ATTR,
	NCSI_CHANNEL_ATTR_ID,
	NCSI_CHANNEL_ATTR_VERSION_MAJOR,
	NCSI_CHANNEL_ATTR_VERSION_MINOR,
	NCSI_CHANNEL_ATTR_VERSION_STR,
	NCSI_CHANNEL_ATTR_LINK_STATE,
	NCSI_CHANNEL_ATTR_ACTIVE,
	NCSI_CHANNEL_ATTR_FORCED,
	NCSI_CHANNEL_ATTR_VLAN_LIST,
	NCSI_CHANNEL_ATTR_VLAN_ID,

	__NCSI_CHANNEL_ATTR_AFTER_LAST,
	NCSI_CHANNEL_ATTR_MAX = __NCSI_CHANNEL_ATTR_AFTER_LAST - 1
};

#endif /* __UAPI_NCSI_NETLINK_H__ */
