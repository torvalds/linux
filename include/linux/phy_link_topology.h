/* SPDX-License-Identifier: GPL-2.0 */
/*
 * PHY device list allow maintaining a list of PHY devices that are
 * part of a netdevice's link topology. PHYs can for example be chained,
 * as is the case when using a PHY that exposes an SFP module, on which an
 * SFP transceiver that embeds a PHY is connected.
 *
 * This list can then be used by userspace to leverage individual PHY
 * capabilities.
 */
#ifndef __PHY_LINK_TOPOLOGY_H
#define __PHY_LINK_TOPOLOGY_H

#include <linux/ethtool.h>
#include <linux/phy_link_topology_core.h>

struct xarray;
struct phy_device;
struct net_device;
struct sfp_bus;

struct phy_device_node {
	enum phy_upstream upstream_type;

	union {
		struct net_device	*netdev;
		struct phy_device	*phydev;
	} upstream;

	struct sfp_bus *parent_sfp_bus;

	struct phy_device *phy;
};

static inline struct phy_device *
phy_link_topo_get_phy(struct phy_link_topology *topo, u32 phyindex)
{
	struct phy_device_node *pdn = xa_load(&topo->phys, phyindex);

	if (pdn)
		return pdn->phy;

	return NULL;
}

#if IS_ENABLED(CONFIG_PHYLIB)
int phy_link_topo_add_phy(struct phy_link_topology *topo,
			  struct phy_device *phy,
			  enum phy_upstream upt, void *upstream);

void phy_link_topo_del_phy(struct phy_link_topology *lt, struct phy_device *phy);

#else
static inline int phy_link_topo_add_phy(struct phy_link_topology *topo,
					struct phy_device *phy,
					enum phy_upstream upt, void *upstream)
{
	return 0;
}

static inline void phy_link_topo_del_phy(struct phy_link_topology *topo,
					 struct phy_device *phy)
{
}
#endif

#endif /* __PHY_LINK_TOPOLOGY_H */
