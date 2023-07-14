/* SPDX-License-Identifier: GPL-2.0-or-later */

#ifndef __DSA_SLAVE_H
#define __DSA_SLAVE_H

#include <linux/if_bridge.h>
#include <linux/if_vlan.h>
#include <linux/list.h>
#include <linux/netpoll.h>
#include <linux/types.h>
#include <net/dsa.h>
#include <net/gro_cells.h>

struct net_device;
struct netlink_ext_ack;

extern struct notifier_block dsa_slave_switchdev_notifier;
extern struct notifier_block dsa_slave_switchdev_blocking_notifier;

struct dsa_slave_priv {
	/* Copy of CPU port xmit for faster access in slave transmit hot path */
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

void dsa_slave_mii_bus_init(struct dsa_switch *ds);
int dsa_slave_create(struct dsa_port *dp);
void dsa_slave_destroy(struct net_device *slave_dev);
int dsa_slave_suspend(struct net_device *slave_dev);
int dsa_slave_resume(struct net_device *slave_dev);
int dsa_slave_register_notifier(void);
void dsa_slave_unregister_notifier(void);
void dsa_slave_sync_ha(struct net_device *dev);
void dsa_slave_unsync_ha(struct net_device *dev);
void dsa_slave_setup_tagger(struct net_device *slave);
int dsa_slave_change_mtu(struct net_device *dev, int new_mtu);
int dsa_slave_change_master(struct net_device *dev, struct net_device *master,
			    struct netlink_ext_ack *extack);
int dsa_slave_manage_vlan_filtering(struct net_device *dev,
				    bool vlan_filtering);

static inline struct dsa_port *dsa_slave_to_port(const struct net_device *dev)
{
	struct dsa_slave_priv *p = netdev_priv(dev);

	return p->dp;
}

static inline struct net_device *
dsa_slave_to_master(const struct net_device *dev)
{
	struct dsa_port *dp = dsa_slave_to_port(dev);

	return dsa_port_to_master(dp);
}

#endif
