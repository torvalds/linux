/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __6LOWPAN_I_H
#define __6LOWPAN_I_H

#include <linux/netdevice.h>

#include <net/6lowpan.h>

/* caller need to be sure it's dev->type is ARPHRD_6LOWPAN */
static inline bool lowpan_is_ll(const struct net_device *dev,
				enum lowpan_lltypes lltype)
{
	return lowpan_dev(dev)->lltype == lltype;
}

extern const struct ndisc_ops lowpan_ndisc_ops;

int addrconf_ifid_802154_6lowpan(u8 *eui, struct net_device *dev);

#ifdef CONFIG_6LOWPAN_DEBUGFS
int lowpan_dev_debugfs_init(struct net_device *dev);
void lowpan_dev_debugfs_exit(struct net_device *dev);

int __init lowpan_debugfs_init(void);
void lowpan_debugfs_exit(void);
#else
static inline int lowpan_dev_debugfs_init(struct net_device *dev)
{
	return 0;
}

static inline void lowpan_dev_debugfs_exit(struct net_device *dev) { }

static inline int __init lowpan_debugfs_init(void)
{
	return 0;
}

static inline void lowpan_debugfs_exit(void) { }
#endif /* CONFIG_6LOWPAN_DEBUGFS */

#endif /* __6LOWPAN_I_H */
