/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __NET_SYSFS_H__
#define __NET_SYSFS_H__

int __init netdev_kobject_init(void);
int netdev_register_kobject(struct net_device *);
void netdev_unregister_kobject(struct net_device *);
int net_rx_queue_update_kobjects(struct net_device *, int old_num, int new_num);
int netdev_queue_update_kobjects(struct net_device *net,
				 int old_num, int new_num);
int netdev_change_owner(struct net_device *, const struct net *net_old,
			const struct net *net_new);

extern struct mutex rps_default_mask_mutex;

#endif
