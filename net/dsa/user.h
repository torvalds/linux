/* SPDX-License-Identifier: GPL-2.0-or-later */

#ifndef __DSA_USER_H
#define __DSA_USER_H

#include <linux/if_bridge.h>
#include <linux/if_vlan.h>
#include <linux/list.h>
#include <linux/netpoll.h>
#include <linux/types.h>
#include <net/dsa.h>
#include <net/gro_cells.h>

struct net_device;
struct netlink_ext_ack;

extern struct notifier_block dsa_user_switchdev_notifier;
extern struct notifier_block dsa_user_switchdev_blocking_notifier;

struct dsa_user_priv {
	/* Copy of CPU port xmit for faster access in user transmit hot path */
	struct sk_buff *	(*xmit)(struct sk_buff *skb,
					struct net_device *dev);

	struct gro_cells	gcells;

	/* DSA port data, such as switch, port index, etc. */
	struct dsa_port		*dp;

#ifdef CONFIG_NET_POLL_CONTROLLER
	struct netpoll		*netpoll;
#endif

	/* TC context */
	struct list_head	mall_tc_list;
};

void dsa_user_mii_bus_init(struct dsa_switch *ds);
int dsa_user_create(struct dsa_port *dp);
void dsa_user_destroy(struct net_device *user_dev);
int dsa_user_suspend(struct net_device *user_dev);
int dsa_user_resume(struct net_device *user_dev);
int dsa_user_register_notifier(void);
void dsa_user_unregister_notifier(void);
int dsa_user_host_uc_install(struct net_device *dev, const u8 *addr);
void dsa_user_host_uc_uninstall(struct net_device *dev);
void dsa_user_sync_ha(struct net_device *dev);
void dsa_user_unsync_ha(struct net_device *dev);
void dsa_user_setup_tagger(struct net_device *user);
int dsa_user_change_mtu(struct net_device *dev, int new_mtu);
int dsa_user_change_conduit(struct net_device *dev, struct net_device *conduit,
			    struct netlink_ext_ack *extack);
int dsa_user_manage_vlan_filtering(struct net_device *dev,
				   bool vlan_filtering);

static inline struct dsa_port *dsa_user_to_port(const struct net_device *dev)
{
	struct dsa_user_priv *p = netdev_priv(dev);

	return p->dp;
}

static inline struct net_device *
dsa_user_to_conduit(const struct net_device *dev)
{
	struct dsa_port *dp = dsa_user_to_port(dev);

	return dsa_port_to_conduit(dp);
}

#endif
