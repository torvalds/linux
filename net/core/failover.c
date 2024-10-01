// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2018, Intel Corporation. */

/* A common module to handle registrations and notifications for paravirtual
 * drivers to enable accelerated datapath and support VF live migration.
 *
 * The notifier and event handling code is based on netvsc driver.
 */

#include <linux/module.h>
#include <linux/etherdevice.h>
#include <uapi/linux/if_arp.h>
#include <linux/rtnetlink.h>
#include <linux/if_vlan.h>
#include <net/failover.h>

static LIST_HEAD(failover_list);
static DEFINE_SPINLOCK(failover_lock);

static struct net_device *failover_get_bymac(u8 *mac, struct failover_ops **ops)
{
	struct net_device *failover_dev;
	struct failover *failover;

	spin_lock(&failover_lock);
	list_for_each_entry(failover, &failover_list, list) {
		failover_dev = rtnl_dereference(failover->failover_dev);
		if (ether_addr_equal(failover_dev->perm_addr, mac)) {
			*ops = rtnl_dereference(failover->ops);
			spin_unlock(&failover_lock);
			return failover_dev;
		}
	}
	spin_unlock(&failover_lock);
	return NULL;
}

/**
 * failover_slave_register - Register a slave netdev
 *
 * @slave_dev: slave netdev that is being registered
 *
 * Registers a slave device to a failover instance. Only ethernet devices
 * are supported.
 */
static int failover_slave_register(struct net_device *slave_dev)
{
	struct netdev_lag_upper_info lag_upper_info;
	struct net_device *failover_dev;
	struct failover_ops *fops;
	int err;

	if (slave_dev->type != ARPHRD_ETHER)
		goto done;

	ASSERT_RTNL();

	failover_dev = failover_get_bymac(slave_dev->perm_addr, &fops);
	if (!failover_dev)
		goto done;

	if (fops && fops->slave_pre_register &&
	    fops->slave_pre_register(slave_dev, failover_dev))
		goto done;

	err = netdev_rx_handler_register(slave_dev, fops->slave_handle_frame,
					 failover_dev);
	if (err) {
		netdev_err(slave_dev, "can not register failover rx handler (err = %d)\n",
			   err);
		goto done;
	}

	lag_upper_info.tx_type = NETDEV_LAG_TX_TYPE_ACTIVEBACKUP;
	err = netdev_master_upper_dev_link(slave_dev, failover_dev, NULL,
					   &lag_upper_info, NULL);
	if (err) {
		netdev_err(slave_dev, "can not set failover device %s (err = %d)\n",
			   failover_dev->name, err);
		goto err_upper_link;
	}

	slave_dev->priv_flags |= (IFF_FAILOVER_SLAVE | IFF_LIVE_RENAME_OK);

	if (fops && fops->slave_register &&
	    !fops->slave_register(slave_dev, failover_dev))
		return NOTIFY_OK;

	netdev_upper_dev_unlink(slave_dev, failover_dev);
	slave_dev->priv_flags &= ~(IFF_FAILOVER_SLAVE | IFF_LIVE_RENAME_OK);
err_upper_link:
	netdev_rx_handler_unregister(slave_dev);
done:
	return NOTIFY_DONE;
}

/**
 * failover_slave_unregister - Unregister a slave netdev
 *
 * @slave_dev: slave netdev that is being unregistered
 *
 * Unregisters a slave device from a failover instance.
 */
int failover_slave_unregister(struct net_device *slave_dev)
{
	struct net_device *failover_dev;
	struct failover_ops *fops;

	if (!netif_is_failover_slave(slave_dev))
		goto done;

	ASSERT_RTNL();

	failover_dev = failover_get_bymac(slave_dev->perm_addr, &fops);
	if (!failover_dev)
		goto done;

	if (fops && fops->slave_pre_unregister &&
	    fops->slave_pre_unregister(slave_dev, failover_dev))
		goto done;

	netdev_rx_handler_unregister(slave_dev);
	netdev_upper_dev_unlink(slave_dev, failover_dev);
	slave_dev->priv_flags &= ~(IFF_FAILOVER_SLAVE | IFF_LIVE_RENAME_OK);

	if (fops && fops->slave_unregister &&
	    !fops->slave_unregister(slave_dev, failover_dev))
		return NOTIFY_OK;

done:
	return NOTIFY_DONE;
}
EXPORT_SYMBOL_GPL(failover_slave_unregister);

static int failover_slave_link_change(struct net_device *slave_dev)
{
	struct net_device *failover_dev;
	struct failover_ops *fops;

	if (!netif_is_failover_slave(slave_dev))
		goto done;

	ASSERT_RTNL();

	failover_dev = failover_get_bymac(slave_dev->perm_addr, &fops);
	if (!failover_dev)
		goto done;

	if (!netif_running(failover_dev))
		goto done;

	if (fops && fops->slave_link_change &&
	    !fops->slave_link_change(slave_dev, failover_dev))
		return NOTIFY_OK;

done:
	return NOTIFY_DONE;
}

static int failover_slave_name_change(struct net_device *slave_dev)
{
	struct net_device *failover_dev;
	struct failover_ops *fops;

	if (!netif_is_failover_slave(slave_dev))
		goto done;

	ASSERT_RTNL();

	failover_dev = failover_get_bymac(slave_dev->perm_addr, &fops);
	if (!failover_dev)
		goto done;

	if (!netif_running(failover_dev))
		goto done;

	if (fops && fops->slave_name_change &&
	    !fops->slave_name_change(slave_dev, failover_dev))
		return NOTIFY_OK;

done:
	return NOTIFY_DONE;
}

static int
failover_event(struct notifier_block *this, unsigned long event, void *ptr)
{
	struct net_device *event_dev = netdev_notifier_info_to_dev(ptr);

	/* Skip parent events */
	if (netif_is_failover(event_dev))
		return NOTIFY_DONE;

	switch (event) {
	case NETDEV_REGISTER:
		return failover_slave_register(event_dev);
	case NETDEV_UNREGISTER:
		return failover_slave_unregister(event_dev);
	case NETDEV_UP:
	case NETDEV_DOWN:
	case NETDEV_CHANGE:
		return failover_slave_link_change(event_dev);
	case NETDEV_CHANGENAME:
		return failover_slave_name_change(event_dev);
	default:
		return NOTIFY_DONE;
	}
}

static struct notifier_block failover_notifier = {
	.notifier_call = failover_event,
};

static void
failover_existing_slave_register(struct net_device *failover_dev)
{
	struct net *net = dev_net(failover_dev);
	struct net_device *dev;

	rtnl_lock();
	for_each_netdev(net, dev) {
		if (netif_is_failover(dev))
			continue;
		if (ether_addr_equal(failover_dev->perm_addr, dev->perm_addr))
			failover_slave_register(dev);
	}
	rtnl_unlock();
}

/**
 * failover_register - Register a failover instance
 *
 * @dev: failover netdev
 * @ops: failover ops
 *
 * Allocate and register a failover instance for a failover netdev. ops
 * provides handlers for slave device register/unregister/link change/
 * name change events.
 *
 * Return: pointer to failover instance
 */
struct failover *failover_register(struct net_device *dev,
				   struct failover_ops *ops)
{
	struct failover *failover;

	if (dev->type != ARPHRD_ETHER)
		return ERR_PTR(-EINVAL);

	failover = kzalloc(sizeof(*failover), GFP_KERNEL);
	if (!failover)
		return ERR_PTR(-ENOMEM);

	rcu_assign_pointer(failover->ops, ops);
	netdev_hold(dev, &failover->dev_tracker, GFP_KERNEL);
	dev->priv_flags |= IFF_FAILOVER;
	rcu_assign_pointer(failover->failover_dev, dev);

	spin_lock(&failover_lock);
	list_add_tail(&failover->list, &failover_list);
	spin_unlock(&failover_lock);

	netdev_info(dev, "failover master:%s registered\n", dev->name);

	failover_existing_slave_register(dev);

	return failover;
}
EXPORT_SYMBOL_GPL(failover_register);

/**
 * failover_unregister - Unregister a failover instance
 *
 * @failover: pointer to failover instance
 *
 * Unregisters and frees a failover instance.
 */
void failover_unregister(struct failover *failover)
{
	struct net_device *failover_dev;

	failover_dev = rcu_dereference(failover->failover_dev);

	netdev_info(failover_dev, "failover master:%s unregistered\n",
		    failover_dev->name);

	failover_dev->priv_flags &= ~IFF_FAILOVER;
	netdev_put(failover_dev, &failover->dev_tracker);

	spin_lock(&failover_lock);
	list_del(&failover->list);
	spin_unlock(&failover_lock);

	kfree(failover);
}
EXPORT_SYMBOL_GPL(failover_unregister);

static __init int
failover_init(void)
{
	register_netdevice_notifier(&failover_notifier);

	return 0;
}
module_init(failover_init);

static __exit
void failover_exit(void)
{
	unregister_netdevice_notifier(&failover_notifier);
}
module_exit(failover_exit);

MODULE_DESCRIPTION("Generic failover infrastructure/interface");
MODULE_LICENSE("GPL v2");
