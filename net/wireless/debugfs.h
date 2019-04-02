/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __CFG80211_DEFS_H
#define __CFG80211_DEFS_H

#ifdef CONFIG_CFG80211_DEFS
void cfg80211_defs_rdev_add(struct cfg80211_registered_device *rdev);
#else
static inline
void cfg80211_defs_rdev_add(struct cfg80211_registered_device *rdev) {}
#endif

#endif /* __CFG80211_DEFS_H */
