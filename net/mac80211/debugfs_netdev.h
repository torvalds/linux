/* SPDX-License-Identifier: GPL-2.0 */
/* routines exported for defs handling */

#ifndef __IEEE80211_DEFS_NETDEV_H
#define __IEEE80211_DEFS_NETDEV_H

#include "ieee80211_i.h"

#ifdef CONFIG_MAC80211_DEFS
void ieee80211_defs_add_netdev(struct ieee80211_sub_if_data *sdata);
void ieee80211_defs_remove_netdev(struct ieee80211_sub_if_data *sdata);
void ieee80211_defs_rename_netdev(struct ieee80211_sub_if_data *sdata);
#else
static inline void ieee80211_defs_add_netdev(
	struct ieee80211_sub_if_data *sdata)
{}
static inline void ieee80211_defs_remove_netdev(
	struct ieee80211_sub_if_data *sdata)
{}
static inline void ieee80211_defs_rename_netdev(
	struct ieee80211_sub_if_data *sdata)
{}
#endif

#endif /* __IEEE80211_DEFS_NETDEV_H */
