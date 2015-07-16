#ifndef __IEEE802154_SYSFS_H
#define __IEEE802154_SYSFS_H

int wpan_phy_sysfs_init(void);
void wpan_phy_sysfs_exit(void);

extern struct class wpan_phy_class;

#endif /* __IEEE802154_SYSFS_H */
