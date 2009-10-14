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
#include <linux/proc_fs.h>
#include <linux/if_arp.h>
#include <net/sock.h>
#include <net/netns/generic.h>
#include <net/phonet/pn_dev.h>

struct phonet_routes {
	spinlock_t		lock;
	struct net_device	*table[64];
};

struct phonet_net {
	struct phonet_device_list pndevs;
	struct phonet_routes routes;
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
u8 phonet_address_get(struct net_device *dev, u8 daddr)
{
	struct phonet_device_list *pndevs = phonet_device_list(dev_net(dev));
	struct phonet_device *pnd;
	u8 saddr;

	spin_lock_bh(&pndevs->lock);
	pnd = __phonet_get(dev);
	if (pnd) {
		BUG_ON(bitmap_empty(pnd->addrs, 64));

		/* Use same source address as destination, if possible */
		if (test_bit(daddr >> 2, pnd->addrs))
			saddr = daddr;
		else
			saddr = find_first_bit(pnd->addrs, 64) << 2;
	} else
		saddr = PN_NO_ADDR;
	spin_unlock_bh(&pndevs->lock);

	if (saddr == PN_NO_ADDR) {
		/* Fallback to another device */
		struct net_device *def_dev;

		def_dev = phonet_device_get(dev_net(dev));
		if (def_dev) {
			if (def_dev != dev)
				saddr = phonet_address_get(def_dev, daddr);
			dev_put(def_dev);
		}
	}
	return saddr;
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

/* automatically configure a Phonet device, if supported */
static int phonet_device_autoconf(struct net_device *dev)
{
	struct if_phonet_req req;
	int ret;

	if (!dev->netdev_ops->ndo_do_ioctl)
		return -EOPNOTSUPP;

	ret = dev->netdev_ops->ndo_do_ioctl(dev, (struct ifreq *)&req,
						SIOCPNGAUTOCONF);
	if (ret < 0)
		return ret;

	ASSERT_RTNL();
	ret = phonet_address_add(dev, req.ifr_phonet_autoconf.device);
	if (ret)
		return ret;
	phonet_address_notify(RTM_NEWADDR, dev,
				req.ifr_phonet_autoconf.device);
	return 0;
}

static void phonet_route_autodel(struct net_device *dev)
{
	struct phonet_net *pnn = net_generic(dev_net(dev), phonet_net_id);
	unsigned i;
	DECLARE_BITMAP(deleted, 64);

	/* Remove left-over Phonet routes */
	bitmap_zero(deleted, 64);
	spin_lock_bh(&pnn->routes.lock);
	for (i = 0; i < 64; i++)
		if (dev == pnn->routes.table[i]) {
			set_bit(i, deleted);
			pnn->routes.table[i] = NULL;
			dev_put(dev);
		}
	spin_unlock_bh(&pnn->routes.lock);
	for (i = find_first_bit(deleted, 64); i < 64;
			i = find_next_bit(deleted, 64, i + 1))
		rtm_phonet_notify(RTM_DELROUTE, dev, i);
}

/* notify Phonet of device events */
static int phonet_device_notify(struct notifier_block *me, unsigned long what,
				void *arg)
{
	struct net_device *dev = arg;

	switch (what) {
	case NETDEV_REGISTER:
		if (dev->type == ARPHRD_PHONET)
			phonet_device_autoconf(dev);
		break;
	case NETDEV_UNREGISTER:
		phonet_device_destroy(dev);
		phonet_route_autodel(dev);
		break;
	}
	return 0;

}

static struct notifier_block phonet_device_notifier = {
	.notifier_call = phonet_device_notify,
	.priority = 0,
};

/* Per-namespace Phonet devices handling */
static int phonet_init_net(struct net *net)
{
	struct phonet_net *pnn = kzalloc(sizeof(*pnn), GFP_KERNEL);
	if (!pnn)
		return -ENOMEM;

	if (!proc_net_fops_create(net, "phonet", 0, &pn_sock_seq_fops)) {
		kfree(pnn);
		return -ENOMEM;
	}

	INIT_LIST_HEAD(&pnn->pndevs.list);
	spin_lock_init(&pnn->pndevs.lock);
	spin_lock_init(&pnn->routes.lock);
	net_assign_generic(net, phonet_net_id, pnn);
	return 0;
}

static void phonet_exit_net(struct net *net)
{
	struct phonet_net *pnn = net_generic(net, phonet_net_id);
	struct net_device *dev;
	unsigned i;

	rtnl_lock();
	for_each_netdev(net, dev)
		phonet_device_destroy(dev);

	for (i = 0; i < 64; i++) {
		dev = pnn->routes.table[i];
		if (dev) {
			rtm_phonet_notify(RTM_DELROUTE, dev, i);
			dev_put(dev);
		}
	}
	rtnl_unlock();

	proc_net_remove(net, "phonet");
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

int phonet_route_add(struct net_device *dev, u8 daddr)
{
	struct phonet_net *pnn = net_generic(dev_net(dev), phonet_net_id);
	struct phonet_routes *routes = &pnn->routes;
	int err = -EEXIST;

	daddr = daddr >> 2;
	spin_lock_bh(&routes->lock);
	if (routes->table[daddr] == NULL) {
		routes->table[daddr] = dev;
		dev_hold(dev);
		err = 0;
	}
	spin_unlock_bh(&routes->lock);
	return err;
}

int phonet_route_del(struct net_device *dev, u8 daddr)
{
	struct phonet_net *pnn = net_generic(dev_net(dev), phonet_net_id);
	struct phonet_routes *routes = &pnn->routes;
	int err = -ENOENT;

	daddr = daddr >> 2;
	spin_lock_bh(&routes->lock);
	if (dev == routes->table[daddr]) {
		routes->table[daddr] = NULL;
		dev_put(dev);
		err = 0;
	}
	spin_unlock_bh(&routes->lock);
	return err;
}

struct net_device *phonet_route_get(struct net *net, u8 daddr)
{
	struct phonet_net *pnn = net_generic(net, phonet_net_id);
	struct phonet_routes *routes = &pnn->routes;
	struct net_device *dev;

	ASSERT_RTNL(); /* no need to hold the device */

	daddr >>= 2;
	spin_lock_bh(&routes->lock);
	dev = routes->table[daddr];
	spin_unlock_bh(&routes->lock);
	return dev;
}

struct net_device *phonet_route_output(struct net *net, u8 daddr)
{
	struct phonet_net *pnn = net_generic(net, phonet_net_id);
	struct phonet_routes *routes = &pnn->routes;
	struct net_device *dev;

	spin_lock_bh(&routes->lock);
	dev = routes->table[daddr >> 2];
	if (dev)
		dev_hold(dev);
	spin_unlock_bh(&routes->lock);

	if (!dev)
		dev = phonet_device_get(net); /* Default route */
	return dev;
}
