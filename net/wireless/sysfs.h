#ifndef __WIRELESS_SYSFS_H
#define __WIRELESS_SYSFS_H

int wiphy_sysfs_init(void);
void wiphy_sysfs_exit(void);

extern struct class ieee80211_class;

#endif /* __WIRELESS_SYSFS_H */
