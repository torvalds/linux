#ifndef __LINUX_NL80211_H
#define __LINUX_NL80211_H
/*
 * 802.11 netlink interface public header
 *
 * Copyright 2006, 2007 Johannes Berg <johannes@sipsolutions.net>
 */

/**
 * enum nl80211_commands - supported nl80211 commands
 *
 * @NL80211_CMD_UNSPEC: unspecified command to catch errors
 *
 * @NL80211_CMD_GET_WIPHY: request information about a wiphy or dump request
 *	to get a list of all present wiphys.
 * @NL80211_CMD_SET_WIPHY: set wiphy name, needs %NL80211_ATTR_WIPHY and
 *	%NL80211_ATTR_WIPHY_NAME.
 * @NL80211_CMD_NEW_WIPHY: Newly created wiphy, response to get request
 *	or rename notification. Has attributes %NL80211_ATTR_WIPHY and
 *	%NL80211_ATTR_WIPHY_NAME.
 * @NL80211_CMD_DEL_WIPHY: Wiphy deleted. Has attributes
 *	%NL80211_ATTR_WIPHY and %NL80211_ATTR_WIPHY_NAME.
 *
 * @NL80211_CMD_GET_INTERFACE: Request an interface's configuration;
 *	either a dump request on a %NL80211_ATTR_WIPHY or a specific get
 *	on an %NL80211_ATTR_IFINDEX is supported.
 * @NL80211_CMD_SET_INTERFACE: Set type of a virtual interface, requires
 *	%NL80211_ATTR_IFINDEX and %NL80211_ATTR_IFTYPE.
 * @NL80211_CMD_NEW_INTERFACE: Newly created virtual interface or response
 *	to %NL80211_CMD_GET_INTERFACE. Has %NL80211_ATTR_IFINDEX,
 *	%NL80211_ATTR_WIPHY and %NL80211_ATTR_IFTYPE attributes. Can also
 *	be sent from userspace to request creation of a new virtual interface,
 *	then requires attributes %NL80211_ATTR_WIPHY, %NL80211_ATTR_IFTYPE and
 *	%NL80211_ATTR_IFNAME.
 * @NL80211_CMD_DEL_INTERFACE: Virtual interface was deleted, has attributes
 *	%NL80211_ATTR_IFINDEX and %NL80211_ATTR_WIPHY. Can also be sent from
 *	userspace to request deletion of a virtual interface, then requires
 *	attribute %NL80211_ATTR_IFINDEX.
 *
 * @NL80211_CMD_MAX: highest used command number
 * @__NL80211_CMD_AFTER_LAST: internal use
 */
enum nl80211_commands {
/* don't change the order or add anything inbetween, this is ABI! */
	NL80211_CMD_UNSPEC,

	NL80211_CMD_GET_WIPHY,		/* can dump */
	NL80211_CMD_SET_WIPHY,
	NL80211_CMD_NEW_WIPHY,
	NL80211_CMD_DEL_WIPHY,

	NL80211_CMD_GET_INTERFACE,	/* can dump */
	NL80211_CMD_SET_INTERFACE,
	NL80211_CMD_NEW_INTERFACE,
	NL80211_CMD_DEL_INTERFACE,

	/* add commands here */

	/* used to define NL80211_CMD_MAX below */
	__NL80211_CMD_AFTER_LAST,
	NL80211_CMD_MAX = __NL80211_CMD_AFTER_LAST - 1
};


/**
 * enum nl80211_attrs - nl80211 netlink attributes
 *
 * @NL80211_ATTR_UNSPEC: unspecified attribute to catch errors
 *
 * @NL80211_ATTR_WIPHY: index of wiphy to operate on, cf.
 *	/sys/class/ieee80211/<phyname>/index
 * @NL80211_ATTR_WIPHY_NAME: wiphy name (used for renaming)
 *
 * @NL80211_ATTR_IFINDEX: network interface index of the device to operate on
 * @NL80211_ATTR_IFNAME: network interface name
 * @NL80211_ATTR_IFTYPE: type of virtual interface, see &enum nl80211_iftype
 *
 * @NL80211_ATTR_MAX: highest attribute number currently defined
 * @__NL80211_ATTR_AFTER_LAST: internal use
 */
enum nl80211_attrs {
/* don't change the order or add anything inbetween, this is ABI! */
	NL80211_ATTR_UNSPEC,

	NL80211_ATTR_WIPHY,
	NL80211_ATTR_WIPHY_NAME,

	NL80211_ATTR_IFINDEX,
	NL80211_ATTR_IFNAME,
	NL80211_ATTR_IFTYPE,

	/* add attributes here, update the policy in nl80211.c */

	__NL80211_ATTR_AFTER_LAST,
	NL80211_ATTR_MAX = __NL80211_ATTR_AFTER_LAST - 1
};

/**
 * enum nl80211_iftype - (virtual) interface types
 *
 * @NL80211_IFTYPE_UNSPECIFIED: unspecified type, driver decides
 * @NL80211_IFTYPE_ADHOC: independent BSS member
 * @NL80211_IFTYPE_STATION: managed BSS member
 * @NL80211_IFTYPE_AP: access point
 * @NL80211_IFTYPE_AP_VLAN: VLAN interface for access points
 * @NL80211_IFTYPE_WDS: wireless distribution interface
 * @NL80211_IFTYPE_MONITOR: monitor interface receiving all frames
 * @NL80211_IFTYPE_MAX: highest interface type number currently defined
 * @__NL80211_IFTYPE_AFTER_LAST: internal use
 *
 * These values are used with the %NL80211_ATTR_IFTYPE
 * to set the type of an interface.
 *
 */
enum nl80211_iftype {
	NL80211_IFTYPE_UNSPECIFIED,
	NL80211_IFTYPE_ADHOC,
	NL80211_IFTYPE_STATION,
	NL80211_IFTYPE_AP,
	NL80211_IFTYPE_AP_VLAN,
	NL80211_IFTYPE_WDS,
	NL80211_IFTYPE_MONITOR,

	/* keep last */
	__NL80211_IFTYPE_AFTER_LAST,
	NL80211_IFTYPE_MAX = __NL80211_IFTYPE_AFTER_LAST - 1
};

#endif /* __LINUX_NL80211_H */
