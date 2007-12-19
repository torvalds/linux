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
 * @NL80211_CMD_GET_KEY: Get sequence counter information for a key specified
 *	by %NL80211_ATTR_KEY_IDX and/or %NL80211_ATTR_MAC.
 * @NL80211_CMD_SET_KEY: Set key attributes %NL80211_ATTR_KEY_DEFAULT or
 *	%NL80211_ATTR_KEY_THRESHOLD.
 * @NL80211_CMD_NEW_KEY: add a key with given %NL80211_ATTR_KEY_DATA,
 *	%NL80211_ATTR_KEY_IDX, %NL80211_ATTR_MAC and %NL80211_ATTR_KEY_CIPHER
 *	attributes.
 * @NL80211_CMD_DEL_KEY: delete a key identified by %NL80211_ATTR_KEY_IDX
 *	or %NL80211_ATTR_MAC.
 *
 * @NL80211_CMD_GET_BEACON: retrieve beacon information (returned in a
 *	%NL80222_CMD_NEW_BEACON message)
 * @NL80211_CMD_SET_BEACON: set the beacon on an access point interface
 *	using the %NL80211_ATTR_BEACON_INTERVAL, %NL80211_ATTR_DTIM_PERIOD,
 *	%NL80211_BEACON_HEAD and %NL80211_BEACON_TAIL attributes.
 * @NL80211_CMD_NEW_BEACON: add a new beacon to an access point interface,
 *	parameters are like for %NL80211_CMD_SET_BEACON.
 * @NL80211_CMD_DEL_BEACON: remove the beacon, stop sending it
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

	NL80211_CMD_GET_KEY,
	NL80211_CMD_SET_KEY,
	NL80211_CMD_NEW_KEY,
	NL80211_CMD_DEL_KEY,

	NL80211_CMD_GET_BEACON,
	NL80211_CMD_SET_BEACON,
	NL80211_CMD_NEW_BEACON,
	NL80211_CMD_DEL_BEACON,

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
 * @NL80211_ATTR_MAC: MAC address (various uses)
 *
 * @NL80211_ATTR_KEY_DATA: (temporal) key data; for TKIP this consists of
 *	16 bytes encryption key followed by 8 bytes each for TX and RX MIC
 *	keys
 * @NL80211_ATTR_KEY_IDX: key ID (u8, 0-3)
 * @NL80211_ATTR_KEY_CIPHER: key cipher suite (u32, as defined by IEEE 802.11
 *	section 7.3.2.25.1, e.g. 0x000FAC04)
 * @NL80211_ATTR_KEY_SEQ: transmit key sequence number (IV/PN) for TKIP and
 *	CCMP keys, each six bytes in little endian
 *
 * @NL80211_ATTR_BEACON_INTERVAL: beacon interval in TU
 * @NL80211_ATTR_DTIM_PERIOD: DTIM period for beaconing
 * @NL80211_ATTR_BEACON_HEAD: portion of the beacon before the TIM IE
 * @NL80211_ATTR_BEACON_TAIL: portion of the beacon after the TIM IE
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

	NL80211_ATTR_MAC,

	NL80211_ATTR_KEY_DATA,
	NL80211_ATTR_KEY_IDX,
	NL80211_ATTR_KEY_CIPHER,
	NL80211_ATTR_KEY_SEQ,
	NL80211_ATTR_KEY_DEFAULT,

	NL80211_ATTR_BEACON_INTERVAL,
	NL80211_ATTR_DTIM_PERIOD,
	NL80211_ATTR_BEACON_HEAD,
	NL80211_ATTR_BEACON_TAIL,

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
