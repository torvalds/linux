/* This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * Authors:
 * (C) 2015 Pengutronix, Alexander Aring <aar@pengutronix.de>
 */

#include <linux/module.h>

#include <net/6lowpan.h>

int lowpan_register_netdevice(struct net_device *dev,
			      enum lowpan_lltypes lltype)
{
	dev->addr_len = EUI64_ADDR_LEN;
	dev->type = ARPHRD_6LOWPAN;
	dev->mtu = IPV6_MIN_MTU;
	dev->priv_flags |= IFF_NO_QUEUE;

	lowpan_priv(dev)->lltype = lltype;

	return register_netdevice(dev);
}
EXPORT_SYMBOL(lowpan_register_netdevice);

int lowpan_register_netdev(struct net_device *dev,
			   enum lowpan_lltypes lltype)
{
	int ret;

	rtnl_lock();
	ret = lowpan_register_netdevice(dev, lltype);
	rtnl_unlock();
	return ret;
}
EXPORT_SYMBOL(lowpan_register_netdev);

void lowpan_unregister_netdevice(struct net_device *dev)
{
	unregister_netdevice(dev);
}
EXPORT_SYMBOL(lowpan_unregister_netdevice);

void lowpan_unregister_netdev(struct net_device *dev)
{
	rtnl_lock();
	lowpan_unregister_netdevice(dev);
	rtnl_unlock();
}
EXPORT_SYMBOL(lowpan_unregister_netdev);

static int __init lowpan_module_init(void)
{
	request_module_nowait("ipv6");

	request_module_nowait("nhc_dest");
	request_module_nowait("nhc_fragment");
	request_module_nowait("nhc_hop");
	request_module_nowait("nhc_ipv6");
	request_module_nowait("nhc_mobility");
	request_module_nowait("nhc_routing");
	request_module_nowait("nhc_udp");

	return 0;
}
module_init(lowpan_module_init);

MODULE_LICENSE("GPL");
