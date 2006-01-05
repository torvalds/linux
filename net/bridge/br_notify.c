/*
 *	Device event handling
 *	Linux ethernet bridge
 *
 *	Authors:
 *	Lennert Buytenhek		<buytenh@gnu.org>
 *
 *	$Id: br_notify.c,v 1.2 2000/02/21 15:51:34 davem Exp $
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 */

#include <linux/kernel.h>

#include "br_private.h"

static int br_device_event(struct notifier_block *unused, unsigned long event, void *ptr);

struct notifier_block br_device_notifier = {
	.notifier_call = br_device_event
};

/*
 * Handle changes in state of network devices enslaved to a bridge.
 * 
 * Note: don't care about up/down if bridge itself is down, because
 *     port state is checked when bridge is brought up.
 */
static int br_device_event(struct notifier_block *unused, unsigned long event, void *ptr)
{
	struct net_device *dev = ptr;
	struct net_bridge_port *p = dev->br_port;
	struct net_bridge *br;

	/* not a port of a bridge */
	if (p == NULL)
		return NOTIFY_DONE;

	br = p->br;

	spin_lock_bh(&br->lock);
	switch (event) {
	case NETDEV_CHANGEMTU:
		dev_set_mtu(br->dev, br_min_mtu(br));
		break;

	case NETDEV_CHANGEADDR:
		br_fdb_changeaddr(p, dev->dev_addr);
		br_stp_recalculate_bridge_id(br);
		break;

	case NETDEV_CHANGE:
		if (br->dev->flags & IFF_UP)
			schedule_delayed_work(&p->carrier_check, BR_PORT_DEBOUNCE);
		break;

	case NETDEV_FEAT_CHANGE:
		if (br->dev->flags & IFF_UP) 
			br_features_recompute(br);

		/* could do recursive feature change notification
		 * but who would care?? 
		 */
		break;

	case NETDEV_DOWN:
		if (br->dev->flags & IFF_UP)
			br_stp_disable_port(p);
		break;

	case NETDEV_UP:
		if (netif_carrier_ok(dev) && (br->dev->flags & IFF_UP)) 
			br_stp_enable_port(p);
		break;

	case NETDEV_UNREGISTER:
		spin_unlock_bh(&br->lock);
		br_del_if(br, dev);
		goto done;
	} 
	spin_unlock_bh(&br->lock);

 done:
	return NOTIFY_DONE;
}
