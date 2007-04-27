#ifndef __LINUX_NL80211_H
#define __LINUX_NL80211_H
/*
 * 802.11 netlink interface public header
 *
 * Copyright 2006, 2007 Johannes Berg <johannes@sipsolutions.net>
 */

/**
 * enum nl80211_iftype - (virtual) interface types
 * @NL80211_IFTYPE_UNSPECIFIED: unspecified type, driver decides
 * @NL80211_IFTYPE_ADHOC: independent BSS member
 * @NL80211_IFTYPE_STATION: managed BSS member
 * @NL80211_IFTYPE_AP: access point
 * @NL80211_IFTYPE_AP_VLAN: VLAN interface for access points
 * @NL80211_IFTYPE_WDS: wireless distribution interface
 * @NL80211_IFTYPE_MONITOR: monitor interface receiving all frames
 * @__NL80211_IFTYPE_AFTER_LAST: internal use
 *
 * These values are used with the NL80211_ATTR_IFTYPE
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
	__NL80211_IFTYPE_AFTER_LAST
};
#define NL80211_IFTYPE_MAX (__NL80211_IFTYPE_AFTER_LAST - 1)

#endif /* __LINUX_NL80211_H */
