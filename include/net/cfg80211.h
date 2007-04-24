#ifndef __NET_CFG80211_H
#define __NET_CFG80211_H

#include <linux/netlink.h>
#include <linux/skbuff.h>
#include <net/genetlink.h>

/*
 * 802.11 configuration in-kernel interface
 *
 * Copyright 2006 Johannes Berg <johannes@sipsolutions.net>
 */

/* from net/wireless.h */
struct wiphy;

/**
 * struct cfg80211_ops - backend description for wireless configuration
 *
 * This struct is registered by fullmac card drivers and/or wireless stacks
 * in order to handle configuration requests on their interfaces.
 *
 * All callbacks except where otherwise noted should return 0
 * on success or a negative error code.
 *
 * All operations are currently invoked under rtnl for consistency with the
 * wireless extensions but this is subject to reevaluation as soon as this
 * code is used more widely and we have a first user without wext.
 *
 * @add_virtual_intf: create a new virtual interface with the given name
 *
 * @del_virtual_intf: remove the virtual interface determined by ifindex.
 */
struct cfg80211_ops {
	int	(*add_virtual_intf)(struct wiphy *wiphy, char *name,
				    unsigned int type);
	int	(*del_virtual_intf)(struct wiphy *wiphy, int ifindex);
};

#endif /* __NET_CFG80211_H */
