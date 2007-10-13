#ifndef __NET_CFG80211_H
#define __NET_CFG80211_H

#include <linux/netlink.h>
#include <linux/skbuff.h>
#include <linux/nl80211.h>
#include <net/genetlink.h>

/*
 * 802.11 configuration in-kernel interface
 *
 * Copyright 2006, 2007	Johannes Berg <johannes@sipsolutions.net>
 */

/* Radiotap header iteration
 *   implemented in net/wireless/radiotap.c
 *   docs in Documentation/networking/radiotap-headers.txt
 */
/**
 * struct ieee80211_radiotap_iterator - tracks walk thru present radiotap args
 * @rtheader: pointer to the radiotap header we are walking through
 * @max_length: length of radiotap header in cpu byte ordering
 * @this_arg_index: IEEE80211_RADIOTAP_... index of current arg
 * @this_arg: pointer to current radiotap arg
 * @arg_index: internal next argument index
 * @arg: internal next argument pointer
 * @next_bitmap: internal pointer to next present u32
 * @bitmap_shifter: internal shifter for curr u32 bitmap, b0 set == arg present
 */

struct ieee80211_radiotap_iterator {
	struct ieee80211_radiotap_header *rtheader;
	int max_length;
	int this_arg_index;
	u8 *this_arg;

	int arg_index;
	u8 *arg;
	__le32 *next_bitmap;
	u32 bitmap_shifter;
};

extern int ieee80211_radiotap_iterator_init(
   struct ieee80211_radiotap_iterator *iterator,
   struct ieee80211_radiotap_header *radiotap_header,
   int max_length);

extern int ieee80211_radiotap_iterator_next(
   struct ieee80211_radiotap_iterator *iterator);


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
 *
 * @change_virtual_intf: change type of virtual interface
 *
 */
struct cfg80211_ops {
	int	(*add_virtual_intf)(struct wiphy *wiphy, char *name,
				    enum nl80211_iftype type);
	int	(*del_virtual_intf)(struct wiphy *wiphy, int ifindex);
	int	(*change_virtual_intf)(struct wiphy *wiphy, int ifindex,
				       enum nl80211_iftype type);
};

#endif /* __NET_CFG80211_H */
