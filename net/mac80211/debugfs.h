#ifndef __MAC80211_DEBUGFS_H
#define __MAC80211_DEBUGFS_H

#ifdef CONFIG_MAC80211_DEBUGFS
extern void debugfs_hw_add(struct ieee80211_local *local);
extern int mac80211_open_file_generic(struct inode *inode, struct file *file);
#else
static inline void debugfs_hw_add(struct ieee80211_local *local)
{
	return;
}
#endif

#endif /* __MAC80211_DEBUGFS_H */
