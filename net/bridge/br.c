/*
 *	Generic parts
 *	Linux ethernet bridge
 *
 *	Authors:
 *	Lennert Buytenhek		<buytenh@gnu.org>
 *
 *	$Id: br.c,v 1.47 2001/12/24 00:56:41 davem Exp $
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/init.h>

#include "br_private.h"

int (*br_should_route_hook) (struct sk_buff **pskb) = NULL;

static int __init br_init(void)
{
	br_fdb_init();

#ifdef CONFIG_BRIDGE_NETFILTER
	if (br_netfilter_init())
		return 1;
#endif
	brioctl_set(br_ioctl_deviceless_stub);
	br_handle_frame_hook = br_handle_frame;

	br_fdb_get_hook = br_fdb_get;
	br_fdb_put_hook = br_fdb_put;

	register_netdevice_notifier(&br_device_notifier);

	return 0;
}

static void __exit br_deinit(void)
{
#ifdef CONFIG_BRIDGE_NETFILTER
	br_netfilter_fini();
#endif
	unregister_netdevice_notifier(&br_device_notifier);
	brioctl_set(NULL);

	br_cleanup_bridges();

	synchronize_net();

	br_fdb_get_hook = NULL;
	br_fdb_put_hook = NULL;

	br_handle_frame_hook = NULL;
	br_fdb_fini();
}

EXPORT_SYMBOL(br_should_route_hook);

module_init(br_init)
module_exit(br_deinit)
MODULE_LICENSE("GPL");
MODULE_VERSION(BR_VERSION);
