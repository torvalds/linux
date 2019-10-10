// SPDX-License-Identifier: GPL-2.0-only
/*
 * 	IEEE 802.1Q GARP VLAN Registration Protocol (GVRP)
 *
 * 	Copyright (c) 2008 Patrick McHardy <kaber@trash.net>
 */
#include <linux/types.h>
#include <linux/if_vlan.h>
#include <net/garp.h>
#include "vlan.h"

#define GARP_GVRP_ADDRESS	{ 0x01, 0x80, 0xc2, 0x00, 0x00, 0x21 }

enum gvrp_attributes {
	GVRP_ATTR_INVALID,
	GVRP_ATTR_VID,
	__GVRP_ATTR_MAX
};
#define GVRP_ATTR_MAX	(__GVRP_ATTR_MAX - 1)

static struct garp_application vlan_gvrp_app __read_mostly = {
	.proto.group_address	= GARP_GVRP_ADDRESS,
	.maxattr		= GVRP_ATTR_MAX,
	.type			= GARP_APPLICATION_GVRP,
};

int vlan_gvrp_request_join(const struct net_device *dev)
{
	const struct vlan_dev_priv *vlan = vlan_dev_priv(dev);
	__be16 vlan_id = htons(vlan->vlan_id);

	if (vlan->vlan_proto != htons(ETH_P_8021Q))
		return 0;
	return garp_request_join(vlan->real_dev, &vlan_gvrp_app,
				 &vlan_id, sizeof(vlan_id), GVRP_ATTR_VID);
}

void vlan_gvrp_request_leave(const struct net_device *dev)
{
	const struct vlan_dev_priv *vlan = vlan_dev_priv(dev);
	__be16 vlan_id = htons(vlan->vlan_id);

	if (vlan->vlan_proto != htons(ETH_P_8021Q))
		return;
	garp_request_leave(vlan->real_dev, &vlan_gvrp_app,
			   &vlan_id, sizeof(vlan_id), GVRP_ATTR_VID);
}

int vlan_gvrp_init_applicant(struct net_device *dev)
{
	return garp_init_applicant(dev, &vlan_gvrp_app);
}

void vlan_gvrp_uninit_applicant(struct net_device *dev)
{
	garp_uninit_applicant(dev, &vlan_gvrp_app);
}

int __init vlan_gvrp_init(void)
{
	return garp_register_application(&vlan_gvrp_app);
}

void vlan_gvrp_uninit(void)
{
	garp_unregister_application(&vlan_gvrp_app);
}
