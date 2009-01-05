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
#include <net/phonet/pn_dev.h>

/* when accessing, remember to lock with spin_lock(&pndevs.lock); */
struct phonet_device_list pndevs = {
	.list = LIST_HEAD_INIT(pndevs.list),
	.lock = __SPIN_LOCK_UNLOCKED(pndevs.lock),
};

/* Allocate new Phonet device. */
static struct phonet_device *__phonet_device_alloc(struct net_device *dev)
{
	struct phonet_device *pnd = kmalloc(sizeof(*pnd), GFP_ATOMIC);
	if (pnd == NULL)
		return NULL;
	pnd->netdev = dev;
	bitmap_zero(pnd->addrs, 64);

	list_add(&pnd->list, &pndevs.list);
	return pnd;
}

static struct phonet_device *__phonet_get(struct net_device *dev)
{
	struct phonet_device *pnd;

	list_for_each_entry(pnd, &pndevs.list, list) {
		if (pnd->netdev == dev)
			return pnd;
	}
	return NULL;
}

static void __phonet_device_free(struct phonet_device *pnd)
{
	list_del(&pnd->list);
	kfree(pnd);
}

struct net_device *phonet_device_get(struct net *net)
{
	struct phonet_device *pnd;
	struct net_device *dev;

	spin_lock_bh(&pndevs.lock);
	list_for_each_entry(pnd, &pndevs.list, list) {
		dev = pnd->netdev;
		BUG_ON(!dev);

		if (net_eq(dev_net(dev), net) &&
			(dev->reg_state == NETREG_REGISTERED) &&
			((pnd->netdev->flags & IFF_UP)) == IFF_UP)
			break;
		dev = NULL;
	}
	if (dev)
		dev_hold(dev);
	spin_unlock_bh(&pndevs.lock);
	return dev;
}

int phonet_address_add(struct net_device *dev, u8 addr)
{
	struct phonet_device *pnd;
	int err = 0;

	spin_lock_bh(&pndevs.lock);
	/* Find or create Phonet-specific device data */
	pnd = __phonet_get(dev);
	if (pnd == NULL)
		pnd = __phonet_device_alloc(dev);
	if (unlikely(pnd == NULL))
		err = -ENOMEM;
	else if (test_and_set_bit(addr >> 2, pnd->addrs))
		err = -EEXIST;
	spin_unlock_bh(&pndevs.lock);
	return err;
}

int phonet_address_del(struct net_device *dev, u8 addr)
{
	struct phonet_device *pnd;
	int err = 0;

	spin_lock_bh(&pndevs.lock);
	pnd = __phonet_get(dev);
	if (!pnd || !test_and_clear_bit(addr >> 2, pnd->addrs))
		err = -EADDRNOTAVAIL;
	else if (bitmap_empty(pnd->addrs, 64))
		__phonet_device_free(pnd);
	spin_unlock_bh(&pndevs.lock);
	return err;
}

/* Gets a source address toward a destination, through a interface. */
u8 phonet_address_get(struct net_device *dev, u8 addr)
{
	struct phonet_device *pnd;

	spin_lock_bh(&pndevs.lock);
	pnd = __phonet_get(dev);
	if (pnd) {
		BUG_ON(bitmap_empty(pnd->addrs, 64));

		/* Use same source address as destination, if possible */
		if (!test_bit(addr >> 2, pnd->addrs))
			addr = find_first_bit(pnd->addrs, 64) << 2;
	} else
		addr = PN_NO_ADDR;
	spin_unlock_bh(&pndevs.lock);
	return addr;
}

int phonet_address_lookup(struct net *net, u8 addr)
{
	struct phonet_device *pnd;

	spin_lock_bh(&pndevs.lock);
	list_for_each_entry(pnd, &pndevs.list, list) {
		if (!net_eq(dev_net(pnd->netdev), net))
			continue;
		/* Don't allow unregistering devices! */
		if ((pnd->netdev->reg_state != NETREG_REGISTERED) ||
				((pnd->netdev->flags & IFF_UP)) != IFF_UP)
			continue;

		if (test_bit(addr >> 2, pnd->addrs)) {
			spin_unlock_bh(&pndevs.lock);
			return 0;
		}
	}
	spin_unlock_bh(&pndevs.lock);
	return -EADDRNOTAVAIL;
}

/* notify Phonet of device events */
static int phonet_device_notify(struct notifier_block *me, unsigned long what,
				void *arg)
{
	struct net_device *dev = arg;

	if (what == NETDEV_UNREGISTER) {
		struct phonet_device *pnd;

		/* Destroy phonet-specific device data */
		spin_lock_bh(&pndevs.lock);
		pnd = __phonet_get(dev);
		if (pnd)
			__phonet_device_free(pnd);
		spin_unlock_bh(&pndevs.lock);
	}
	return 0;

}

static struct notifier_block phonet_device_notifier = {
	.notifier_call = phonet_device_notify,
	.priority = 0,
};

/* Initialize Phonet devices list */
void phonet_device_init(void)
{
	register_netdevice_notifier(&phonet_device_notifier);
}

void phonet_device_exit(void)
{
	struct phonet_device *pnd, *n;

	rtnl_unregister_all(PF_PHONET);
	rtnl_lock();
	spin_lock_bh(&pndevs.lock);

	list_for_each_entry_safe(pnd, n, &pndevs.list, list)
		__phonet_device_free(pnd);

	spin_unlock_bh(&pndevs.lock);
	rtnl_unlock();
	unregister_netdevice_notifier(&phonet_device_notifier);
}
