/*
 * File: pn_dev.c
 *
 * Phonet network device
 *
 * Copyright (C) 2008 Nokia Corporation.
 *
 * Contact: Remi Denis-Courmont <remi.denis-courmont@nokia.com>
 * Original author: Sakari Ailus <sakari.ailus@nokia.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 */

#include <linux/kernel.h>
#include <linux/net.h>
#include <linux/netdevice.h>
#include <linux/phonet.h>
#include <net/sock.h>
#include <net/netns/generic.h>
#include <net/phonet/pn_dev.h>

struct phonet_net {
	struct phonet_device_list pndevs;
};

int phonet_net_id;

struct phonet_device_list *phonet_device_list(struct net *net)
{
	struct phonet_net *pnn = net_generic(net, phonet_net_id);
	return &pnn->pndevs;
}

/* Allocate new Phonet device. */
static struct phonet_device *__phonet_device_alloc(struct net_device *dev)
{
	struct phonet_device_list *pndevs = phonet_device_list(dev_net(dev));
	struct phonet_device *pnd = kmalloc(sizeof(*pnd), GFP_ATOMIC);
	if (pnd == NULL)
		return NULL;
	pnd->netdev = dev;
	bitmap_zero(pnd->addrs, 64);

	list_add(&pnd->list, &pndevs->list);
	return pnd;
}

static struct phonet_device *__phonet_get(struct net_device *dev)
{
	struct phonet_device_list *pndevs = phonet_device_list(dev_net(dev));
	struct phonet_device *pnd;

	list_for_each_entry(pnd, &pndevs->list, list) {
		if (pnd->netdev == dev)
			return pnd;
	}
	return NULL;
}

static void phonet_device_destroy(struct net_device *dev)
{
	struct phonet_device_list *pndevs = phonet_device_list(dev_net(dev));
	struct phonet_device *pnd;

	ASSERT_RTNL();

	spin_lock_bh(&pndevs->lock);
	pnd = __phonet_get(dev);
	if (pnd)
		list_del(&pnd->list);
	spin_unlock_bh(&pndevs->lock);

	if (pnd) {
		u8 addr;

		for (addr = find_first_bit(pnd->addrs, 64); addr < 64;
			addr = find_next_bit(pnd->addrs, 64, 1+addr))
			phonet_address_notify(RTM_DELADDR, dev, addr);
		kfree(pnd);
	}
}

struct net_device *phonet_device_get(struct net *net)
{
	struct phonet_device_list *pndevs = phonet_device_list(net);
	struct phonet_device *pnd;
	struct net_device *dev = NULL;

	spin_lock_bh(&pndevs->lock);
	list_for_each_entry(pnd, &pndevs->list, list) {
		dev = pnd->netdev;
		BUG_ON(!dev);

		if ((dev->reg_state == NETREG_REGISTERED) &&
			((pnd->netdev->flags & IFF_UP)) == IFF_UP)
			break;
		dev = NULL;
	}
	if (dev)
		dev_hold(dev);
	spin_unlock_bh(&pndevs->lock);
	return dev;
}

int phonet_address_add(struct net_device *dev, u8 addr)
{
	struct phonet_device_list *pndevs = phonet_device_list(dev_net(dev));
	struct phonet_device *pnd;
	int err = 0;

	spin_lock_bh(&pndevs->lock);
	/* Find or create Phonet-specific device data */
	pnd = __phonet_get(dev);
	if (pnd == NULL)
		pnd = __phonet_device_alloc(dev);
	if (unlikely(pnd == NULL))
		err = -ENOMEM;
	else if (test_and_set_bit(addr >> 2, pnd->addrs))
		err = -EEXIST;
	spin_unlock_bh(&pndevs->lock);
	return err;
}

int phonet_address_del(struct net_device *dev, u8 addr)
{
	struct phonet_device_list *pndevs = phonet_device_list(dev_net(dev));
	struct phonet_device *pnd;
	int err = 0;

	spin_lock_bh(&pndevs->lock);
	pnd = __phonet_get(dev);
	if (!pnd || !test_and_clear_bit(addr >> 2, pnd->addrs))
		err = -EADDRNOTAVAIL;
	else if (bitmap_empty(pnd->addrs, 64)) {
		list_del(&pnd->list);
		kfree(pnd);
	}
	spin_unlock_bh(&pndevs->lock);
	return err;
}

/* Gets a source address toward a destination, through a interface. */
u8 phonet_address_get(struct net_device *dev, u8 addr)
{
	struct phonet_device_list *pndevs = phonet_device_list(dev_net(dev));
	struct phonet_device *pnd;

	spin_lock_bh(&pndevs->lock);
	pnd = __phonet_get(dev);
	if (pnd) {
		BUG_ON(bitmap_empty(pnd->addrs, 64));

		/* Use same source address as destination, if possible */
		if (!test_bit(addr >> 2, pnd->addrs))
			addr = find_first_bit(pnd->addrs, 64) << 2;
	} else
		addr = PN_NO_ADDR;
	spin_unlock_bh(&pndevs->lock);
	return addr;
}

int phonet_address_lookup(struct net *net, u8 addr)
{
	struct phonet_device_list *pndevs = phonet_device_list(net);
	struct phonet_device *pnd;
	int err = -EADDRNOTAVAIL;

	spin_lock_bh(&pndevs->lock);
	list_for_each_entry(pnd, &pndevs->list, list) {
		/* Don't allow unregistering devices! */
		if ((pnd->netdev->reg_state != NETREG_REGISTERED) ||
				((pnd->netdev->flags & IFF_UP)) != IFF_UP)
			continue;

		if (test_bit(addr >> 2, pnd->addrs)) {
			err = 0;
			goto found;
		}
	}
found:
	spin_unlock_bh(&pndevs->lock);
	return err;
}

/* notify Phonet of device events */
static int phonet_device_notify(struct notifier_block *me, unsigned long what,
				void *arg)
{
	struct net_device *dev = arg;

	if (what == NETDEV_UNREGISTER)
		phonet_device_destroy(dev);
	return 0;

}

static struct notifier_block phonet_device_notifier = {
	.notifier_call = phonet_device_notify,
	.priority = 0,
};

/* Per-namespace Phonet devices handling */
static int phonet_init_net(struct net *net)
{
	struct phonet_net *pnn = kmalloc(sizeof(*pnn), GFP_KERNEL);
	if (!pnn)
		return -ENOMEM;

	INIT_LIST_HEAD(&pnn->pndevs.list);
	spin_lock_init(&pnn->pndevs.lock);
	net_assign_generic(net, phonet_net_id, pnn);
	return 0;
}

static void phonet_exit_net(struct net *net)
{
	struct phonet_net *pnn = net_generic(net, phonet_net_id);
	struct net_device *dev;

	rtnl_lock();
	for_each_netdev(net, dev)
		phonet_device_destroy(dev);
	rtnl_unlock();
	kfree(pnn);
}

static struct pernet_operations phonet_net_ops = {
	.init = phonet_init_net,
	.exit = phonet_exit_net,
};

/* Initialize Phonet devices list */
int __init phonet_device_init(void)
{
	int err = register_pernet_gen_device(&phonet_net_id, &phonet_net_ops);
	if (err)
		return err;

	register_netdevice_notifier(&phonet_device_notifier);
	err = phonet_netlink_register();
	if (err)
		phonet_device_exit();
	return err;
}

void phonet_device_exit(void)
{
	rtnl_unregister_all(PF_PHONET);
	unregister_netdevice_notifier(&phonet_device_notifier);
	unregister_pernet_gen_device(phonet_net_id, &phonet_net_ops);
}
