/*
 * xfrm_device.c - IPsec device offloading code.
 *
 * Copyright (c) 2015 secunet Security Networks AG
 *
 * Author:
 * Steffen Klassert <steffen.klassert@secunet.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/errno.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <net/dst.h>
#include <net/xfrm.h>
#include <linux/notifier.h>

static int xfrm_dev_event(struct notifier_block *this, unsigned long event, void *ptr)
{
	struct net_device *dev = netdev_notifier_info_to_dev(ptr);

	switch (event) {
	case NETDEV_DOWN:
		xfrm_garbage_collect(dev_net(dev));
	}
	return NOTIFY_DONE;
}

static struct notifier_block xfrm_dev_notifier = {
	.notifier_call	= xfrm_dev_event,
};

void __net_init xfrm_dev_init(void)
{
	register_netdevice_notifier(&xfrm_dev_notifier);
}
