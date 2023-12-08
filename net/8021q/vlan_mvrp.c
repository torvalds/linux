// SPDX-License-Identifier: GPL-2.0-only
/*
 *	IEEE 802.1Q Multiple VLAN Registration Protocol (MVRP)
 *
 *	Copyright (c) 2012 Massachusetts Institute of Technology
 *
 *	Adapted from code in net/8021q/vlan_gvrp.c
 *	Copyright (c) 2008 Patrick McHardy <kaber@trash.net>
 */
#include <linux/types.h>
#include <linux/if_ether.h>
#include <linux/if_vlan.h>
#include <net/mrp.h>
#include "vlan.h"

#define MRP_MVRP_ADDRESS	{ 0x01, 0x80, 0xc2, 0x00, 0x00, 0x21 }

enum mvrp_attributes {
	MVRP_ATTR_INVALID,
	MVRP_ATTR_VID,
	__MVRP_ATTR_MAX
};
#define MVRP_ATTR_MAX	(__MVRP_ATTR_MAX - 1)

static struct mrp_application vlan_mrp_app __read_mostly = {
	.type		= MRP_APPLICATION_MVRP,
	.maxattr	= MVRP_ATTR_MAX,
	.pkttype.type	= htons(ETH_P_MVRP),
	.group_address	= MRP_MVRP_ADDRESS,
	.version	= 0,
};

int vlan_mvrp_request_join(const struct net_device *dev)
{
	const struct vlan_dev_priv *vlan = vlan_dev_priv(dev);
	__be16 vlan_id = htons(vlan->vlan_id);

	if (vlan->vlan_proto != htons(ETH_P_8021Q))
		return 0;
	return mrp_request_join(vlan->real_dev, &vlan_mrp_app,
				&vlan_id, sizeof(vlan_id), MVRP_ATTR_VID);
}

void vlan_mvrp_request_leave(const struct net_device *dev)
{
	const struct vlan_dev_priv *vlan = vlan_dev_priv(dev);
	__be16 vlan_id = htons(vlan->vlan_id);

	if (vlan->vlan_proto != htons(ETH_P_8021Q))
		return;
	mrp_request_leave(vlan->real_dev, &vlan_mrp_app,
			  &vlan_id, sizeof(vlan_id), MVRP_ATTR_VID);
}

int vlan_mvrp_init_applicant(struct net_device *dev)
{
	return mrp_init_applicant(dev, &vlan_mrp_app);
}

void vlan_mvrp_uninit_applicant(struct net_device *dev)
{
	mrp_uninit_applicant(dev, &vlan_mrp_app);
}

int __init vlan_mvrp_init(void)
{
	return mrp_register_application(&vlan_mrp_app);
}

void vlan_mvrp_uninit(void)
{
	mrp_unregister_application(&vlan_mrp_app);
}
