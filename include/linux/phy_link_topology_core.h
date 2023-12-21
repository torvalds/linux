/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __PHY_LINK_TOPOLOGY_CORE_H
#define __PHY_LINK_TOPOLOGY_CORE_H

struct xarray;

struct phy_link_topology {
	struct xarray phys;

	u32 next_phy_index;
};

static inline void phy_link_topo_init(struct phy_link_topology *topo)
{
	xa_init_flags(&topo->phys, XA_FLAGS_ALLOC1);
	topo->next_phy_index = 1;
}

#endif /* __PHY_LINK_TOPOLOGY_CORE_H */
