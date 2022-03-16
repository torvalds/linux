// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *	Bridge Multiple Spanning Tree Support
 *
 *	Authors:
 *	Tobias Waldekranz		<tobias@waldekranz.com>
 */

#include <linux/kernel.h>

#include "br_private.h"

DEFINE_STATIC_KEY_FALSE(br_mst_used);

static void br_mst_vlan_set_state(struct net_bridge_port *p, struct net_bridge_vlan *v,
				  u8 state)
{
	struct net_bridge_vlan_group *vg = nbp_vlan_group(p);

	if (v->state == state)
		return;

	br_vlan_set_state(v, state);

	if (v->vid == vg->pvid)
		br_vlan_set_pvid_state(vg, state);
}

int br_mst_set_state(struct net_bridge_port *p, u16 msti, u8 state,
		     struct netlink_ext_ack *extack)
{
	struct net_bridge_vlan_group *vg;
	struct net_bridge_vlan *v;

	vg = nbp_vlan_group(p);
	if (!vg)
		return 0;

	list_for_each_entry(v, &vg->vlan_list, vlist) {
		if (v->brvlan->msti != msti)
			continue;

		br_mst_vlan_set_state(p, v, state);
	}

	return 0;
}

void br_mst_vlan_init_state(struct net_bridge_vlan *v)
{
	/* VLANs always start out in MSTI 0 (CST) */
	v->msti = 0;

	if (br_vlan_is_master(v))
		v->state = BR_STATE_FORWARDING;
	else
		v->state = v->port->state;
}

int br_mst_set_enabled(struct net_bridge *br, bool on,
		       struct netlink_ext_ack *extack)
{
	struct net_bridge_vlan_group *vg;
	struct net_bridge_port *p;

	list_for_each_entry(p, &br->port_list, list) {
		vg = nbp_vlan_group(p);

		if (!vg->num_vlans)
			continue;

		NL_SET_ERR_MSG(extack,
			       "MST mode can't be changed while VLANs exist");
		return -EBUSY;
	}

	if (br_opt_get(br, BROPT_MST_ENABLED) == on)
		return 0;

	if (on)
		static_branch_enable(&br_mst_used);
	else
		static_branch_disable(&br_mst_used);

	br_opt_toggle(br, BROPT_MST_ENABLED, on);
	return 0;
}
