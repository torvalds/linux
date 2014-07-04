/* Copyright 2011-2014 Autronica Fire and Security AS
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * Author(s):
 *	2011-2014 Arvid Brodin, arvid.brodin@alten.se
 */

#include <linux/netdevice.h>
#include <linux/rculist.h>
#include <linux/timer.h>
#include <linux/etherdevice.h>
#include "hsr_main.h"
#include "hsr_device.h"
#include "hsr_netlink.h"
#include "hsr_framereg.h"


/* List of all registered virtual HSR devices */
static LIST_HEAD(hsr_list);

void register_hsr_master(struct hsr_priv *hsr)
{
	list_add_tail_rcu(&hsr->hsr_list, &hsr_list);
}

void unregister_hsr_master(struct hsr_priv *hsr)
{
	struct hsr_priv *hsr_it;

	list_for_each_entry(hsr_it, &hsr_list, hsr_list)
		if (hsr_it == hsr) {
			list_del_rcu(&hsr_it->hsr_list);
			return;
		}
}

bool is_hsr_slave(struct net_device *dev)
{
	struct hsr_priv *hsr_it;

	list_for_each_entry_rcu(hsr_it, &hsr_list, hsr_list) {
		if (dev == hsr_it->slave[0])
			return true;
		if (dev == hsr_it->slave[1])
			return true;
	}

	return false;
}

/* If dev is a HSR slave device, return the virtual master device. Return NULL
 * otherwise.
 */
struct hsr_priv *get_hsr_master(struct net_device *dev)
{
	struct hsr_priv *hsr;

	rcu_read_lock();
	list_for_each_entry_rcu(hsr, &hsr_list, hsr_list)
		if ((dev == hsr->slave[0]) ||
		    (dev == hsr->slave[1])) {
			rcu_read_unlock();
			return hsr;
		}

	rcu_read_unlock();
	return NULL;
}

/* If dev is a HSR slave device, return the other slave device. Return NULL
 * otherwise.
 */
struct net_device *get_other_slave(struct hsr_priv *hsr,
				   struct net_device *dev)
{
	if (dev == hsr->slave[0])
		return hsr->slave[1];
	if (dev == hsr->slave[1])
		return hsr->slave[0];

	return NULL;
}


static int hsr_netdev_notify(struct notifier_block *nb, unsigned long event,
			     void *ptr)
{
	struct net_device *slave, *other_slave;
	struct hsr_priv *hsr;
	int old_operstate;
	int mtu_max;
	int res;
	struct net_device *dev;

	dev = netdev_notifier_info_to_dev(ptr);

	hsr = get_hsr_master(dev);
	if (hsr) {
		/* dev is a slave device */
		slave = dev;
		other_slave = get_other_slave(hsr, slave);
	} else {
		if (!is_hsr_master(dev))
			return NOTIFY_DONE;
		hsr = netdev_priv(dev);
		slave = hsr->slave[0];
		other_slave = hsr->slave[1];
	}

	switch (event) {
	case NETDEV_UP:		/* Administrative state DOWN */
	case NETDEV_DOWN:	/* Administrative state UP */
	case NETDEV_CHANGE:	/* Link (carrier) state changes */
		old_operstate = hsr->dev->operstate;
		hsr_set_carrier(hsr->dev, slave, other_slave);
		/* netif_stacked_transfer_operstate() cannot be used here since
		 * it doesn't set IF_OPER_LOWERLAYERDOWN (?)
		 */
		hsr_set_operstate(hsr->dev, slave, other_slave);
		hsr_check_announce(hsr->dev, old_operstate);
		break;
	case NETDEV_CHANGEADDR:

		/* This should not happen since there's no ndo_set_mac_address()
		 * for HSR devices - i.e. not supported.
		 */
		if (dev == hsr->dev)
			break;

		if (dev == hsr->slave[0])
			ether_addr_copy(hsr->dev->dev_addr,
					hsr->slave[0]->dev_addr);

		/* Make sure we recognize frames from ourselves in hsr_rcv() */
		res = hsr_create_self_node(&hsr->self_node_db,
					   hsr->dev->dev_addr,
					   hsr->slave[1] ?
						hsr->slave[1]->dev_addr :
						hsr->dev->dev_addr);
		if (res)
			netdev_warn(hsr->dev,
				    "Could not update HSR node address.\n");

		if (dev == hsr->slave[0])
			call_netdevice_notifiers(NETDEV_CHANGEADDR, hsr->dev);
		break;
	case NETDEV_CHANGEMTU:
		if (dev == hsr->dev)
			break; /* Handled in ndo_change_mtu() */
		mtu_max = hsr_get_max_mtu(hsr);
		if (hsr->dev->mtu > mtu_max)
			dev_set_mtu(hsr->dev, mtu_max);
		break;
	case NETDEV_UNREGISTER:
		if (dev == hsr->slave[0])
			hsr->slave[0] = NULL;
		if (dev == hsr->slave[1])
			hsr->slave[1] = NULL;

		/* There should really be a way to set a new slave device... */

		break;
	case NETDEV_PRE_TYPE_CHANGE:
		/* HSR works only on Ethernet devices. Refuse slave to change
		 * its type.
		 */
		return NOTIFY_BAD;
	}

	return NOTIFY_DONE;
}


static struct timer_list prune_timer;

static void prune_nodes_all(unsigned long data)
{
	struct hsr_priv *hsr;

	rcu_read_lock();
	list_for_each_entry_rcu(hsr, &hsr_list, hsr_list)
		hsr_prune_nodes(hsr);
	rcu_read_unlock();

	prune_timer.expires = jiffies + msecs_to_jiffies(PRUNE_PERIOD);
	add_timer(&prune_timer);
}


static struct notifier_block hsr_nb = {
	.notifier_call = hsr_netdev_notify,	/* Slave event notifications */
};


static int __init hsr_init(void)
{
	int res;

	BUILD_BUG_ON(sizeof(struct hsr_tag) != HSR_HLEN);

	init_timer(&prune_timer);
	prune_timer.function = prune_nodes_all;
	prune_timer.data = 0;
	prune_timer.expires = jiffies + msecs_to_jiffies(PRUNE_PERIOD);
	add_timer(&prune_timer);

	register_netdevice_notifier(&hsr_nb);

	res = hsr_netlink_init();

	return res;
}

static void __exit hsr_exit(void)
{
	unregister_netdevice_notifier(&hsr_nb);
	del_timer_sync(&prune_timer);
	hsr_netlink_exit();
}

module_init(hsr_init);
module_exit(hsr_exit);
MODULE_LICENSE("GPL");
