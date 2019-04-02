/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __MAC80211_DEFS_STA_H
#define __MAC80211_DEFS_STA_H

#include "sta_info.h"

#ifdef CONFIG_MAC80211_DEFS
void ieee80211_sta_defs_add(struct sta_info *sta);
void ieee80211_sta_defs_remove(struct sta_info *sta);
#else
static inline void ieee80211_sta_defs_add(struct sta_info *sta) {}
static inline void ieee80211_sta_defs_remove(struct sta_info *sta) {}
#endif

#endif /* __MAC80211_DEFS_STA_H */
