// SPDX-License-Identifier: GPL-2.0-or-later

#include <net/genetlink.h>

#include <uapi/linux/mrp_bridge.h>
#include "br_private.h"
#include "br_private_mrp.h"

int br_mrp_port_open(struct net_device *dev, u8 loc)
{
	struct net_bridge_port *p;
	int err = 0;

	p = br_port_get_rcu(dev);
	if (!p) {
		err = -EINVAL;
		goto out;
	}

	if (loc)
		p->flags |= BR_MRP_LOST_CONT;
	else
		p->flags &= ~BR_MRP_LOST_CONT;

	br_ifinfo_notify(RTM_NEWLINK, NULL, p);

out:
	return err;
}
