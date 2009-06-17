#ifndef __CFG80211_DEBUGFS_H
#define __CFG80211_DEBUGFS_H

#ifdef CONFIG_CFG80211_DEBUGFS
void cfg80211_debugfs_drv_add(struct cfg80211_registered_device *drv);
void cfg80211_debugfs_drv_del(struct cfg80211_registered_device *drv);
#else
static inline
void cfg80211_debugfs_drv_add(struct cfg80211_registered_device *drv) {}
static inline
void cfg80211_debugfs_drv_del(struct cfg80211_registered_device *drv) {}
#endif

#endif /* __CFG80211_DEBUGFS_H */
