/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Portions:
 * Copyright (C) 2023 Intel Corporation
 */
/* routines exported for debugfs handling */

#ifndef __IEEE80211_DEBUGFS_NETDEV_H
#define __IEEE80211_DEBUGFS_NETDEV_H

#include "ieee80211_i.h"

#ifdef CONFIG_MAC80211_DEBUGFS
void ieee80211_debugfs_remove_netdev(struct ieee80211_sub_if_data *sdata);
void ieee80211_debugfs_rename_netdev(struct ieee80211_sub_if_data *sdata);
void ieee80211_debugfs_recreate_netdev(struct ieee80211_sub_if_data *sdata,
				       bool mld_vif);

void ieee80211_link_debugfs_add(struct ieee80211_link_data *link);
void ieee80211_link_debugfs_remove(struct ieee80211_link_data *link);

void ieee80211_link_debugfs_drv_add(struct ieee80211_link_data *link);
void ieee80211_link_debugfs_drv_remove(struct ieee80211_link_data *link);
#else
static inline void ieee80211_debugfs_remove_netdev(
	struct ieee80211_sub_if_data *sdata)
{}
static inline void ieee80211_debugfs_rename_netdev(
	struct ieee80211_sub_if_data *sdata)
{}
static inline void ieee80211_debugfs_recreate_netdev(
	struct ieee80211_sub_if_data *sdata, bool mld_vif)
{}
static inline void ieee80211_link_debugfs_add(struct ieee80211_link_data *link)
{}
static inline void ieee80211_link_debugfs_remove(struct ieee80211_link_data *link)
{}

static inline void ieee80211_link_debugfs_drv_add(struct ieee80211_link_data *link)
{}
static inline void ieee80211_link_debugfs_drv_remove(struct ieee80211_link_data *link)
{}
#endif

#endif /* __IEEE80211_DEBUGFS_NETDEV_H */
