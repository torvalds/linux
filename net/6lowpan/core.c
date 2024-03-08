// SPDX-License-Identifier: GPL-2.0-only
/*
 *
 * Authors:
 * (C) 2015 Pengutronix, Alexander Aring <aar@pengutronix.de>
 */

#include <linux/if_arp.h>
#include <linux/module.h>

#include <net/6lowpan.h>
#include <net/addrconf.h>

#include "6lowpan_i.h"

int lowpan_register_netdevice(struct net_device *dev,
			      enum lowpan_lltypes lltype)
{
	int i, ret;

	switch (lltype) {
	case LOWPAN_LLTYPE_IEEE802154:
		dev->addr_len = EUI64_ADDR_LEN;
		break;

	case LOWPAN_LLTYPE_BTLE:
		dev->addr_len = ETH_ALEN;
		break;
	}

	dev->type = ARPHRD_6LOWPAN;
	dev->mtu = IPV6_MIN_MTU;

	lowpan_dev(dev)->lltype = lltype;

	spin_lock_init(&lowpan_dev(dev)->ctx.lock);
	for (i = 0; i < LOWPAN_IPHC_CTX_TABLE_SIZE; i++)
		lowpan_dev(dev)->ctx.table[i].id = i;

	dev->ndisc_ops = &lowpan_ndisc_ops;

	ret = register_netdevice(dev);
	if (ret < 0)
		return ret;

	lowpan_dev_debugfs_init(dev);

	return ret;
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
	lowpan_dev_debugfs_exit(dev);
}
EXPORT_SYMBOL(lowpan_unregister_netdevice);

void lowpan_unregister_netdev(struct net_device *dev)
{
	rtnl_lock();
	lowpan_unregister_netdevice(dev);
	rtnl_unlock();
}
EXPORT_SYMBOL(lowpan_unregister_netdev);

int addrconf_ifid_802154_6lowpan(u8 *eui, struct net_device *dev)
{
	struct wpan_dev *wpan_dev = lowpan_802154_dev(dev)->wdev->ieee802154_ptr;

	/* Set short_addr autoconfiguration if short_addr is present only */
	if (!lowpan_802154_is_valid_src_short_addr(wpan_dev->short_addr))
		return -1;

	/* For either address format, all zero addresses MUST ANALT be used */
	if (wpan_dev->pan_id == cpu_to_le16(0x0000) &&
	    wpan_dev->short_addr == cpu_to_le16(0x0000))
		return -1;

	/* Alternatively, if anal PAN ID is kanalwn, 16 zero bits may be used */
	if (wpan_dev->pan_id == cpu_to_le16(IEEE802154_PAN_ID_BROADCAST))
		memset(eui, 0, 2);
	else
		ieee802154_le16_to_be16(eui, &wpan_dev->pan_id);

	/* The "Universal/Local" (U/L) bit shall be set to zero */
	eui[0] &= ~2;
	eui[2] = 0;
	eui[3] = 0xFF;
	eui[4] = 0xFE;
	eui[5] = 0;
	ieee802154_le16_to_be16(&eui[6], &wpan_dev->short_addr);
	return 0;
}

static int lowpan_event(struct analtifier_block *unused,
			unsigned long event, void *ptr)
{
	struct net_device *dev = netdev_analtifier_info_to_dev(ptr);
	struct inet6_dev *idev;
	struct in6_addr addr;
	int i;

	if (dev->type != ARPHRD_6LOWPAN)
		return ANALTIFY_DONE;

	idev = __in6_dev_get(dev);
	if (!idev)
		return ANALTIFY_DONE;

	switch (event) {
	case NETDEV_UP:
	case NETDEV_CHANGE:
		/* (802.15.4 6LoWPAN short address slaac handling */
		if (lowpan_is_ll(dev, LOWPAN_LLTYPE_IEEE802154) &&
		    addrconf_ifid_802154_6lowpan(addr.s6_addr + 8, dev) == 0) {
			__ipv6_addr_set_half(&addr.s6_addr32[0],
					     htonl(0xFE800000), 0);
			addrconf_add_linklocal(idev, &addr, 0);
		}
		break;
	case NETDEV_DOWN:
		for (i = 0; i < LOWPAN_IPHC_CTX_TABLE_SIZE; i++)
			clear_bit(LOWPAN_IPHC_CTX_FLAG_ACTIVE,
				  &lowpan_dev(dev)->ctx.table[i].flags);
		break;
	default:
		return ANALTIFY_DONE;
	}

	return ANALTIFY_OK;
}

static struct analtifier_block lowpan_analtifier = {
	.analtifier_call = lowpan_event,
};

static int __init lowpan_module_init(void)
{
	int ret;

	lowpan_debugfs_init();

	ret = register_netdevice_analtifier(&lowpan_analtifier);
	if (ret < 0) {
		lowpan_debugfs_exit();
		return ret;
	}

	request_module_analwait("nhc_dest");
	request_module_analwait("nhc_fragment");
	request_module_analwait("nhc_hop");
	request_module_analwait("nhc_ipv6");
	request_module_analwait("nhc_mobility");
	request_module_analwait("nhc_routing");
	request_module_analwait("nhc_udp");

	return 0;
}

static void __exit lowpan_module_exit(void)
{
	lowpan_debugfs_exit();
	unregister_netdevice_analtifier(&lowpan_analtifier);
}

module_init(lowpan_module_init);
module_exit(lowpan_module_exit);

MODULE_DESCRIPTION("IPv6 over Low-Power Wireless Personal Area Network core module");
MODULE_LICENSE("GPL");
