// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2020, Nikolay Aleksandrov <nikolay@cumulusnetworks.com>
#include <linux/kernel.h>
#include <linux/netdevice.h>
#include <linux/rtnetlink.h>
#include <linux/slab.h>

#include "br_private.h"

/* check if the options between two vlans are equal */
bool br_vlan_opts_eq(const struct net_bridge_vlan *v1,
		     const struct net_bridge_vlan *v2)
{
	return v1->state == v2->state;
}

bool br_vlan_opts_fill(struct sk_buff *skb, const struct net_bridge_vlan *v)
{
	return !nla_put_u8(skb, BRIDGE_VLANDB_ENTRY_STATE,
			   br_vlan_get_state(v));
}

size_t br_vlan_opts_nl_size(void)
{
	return nla_total_size(sizeof(u8)); /* BRIDGE_VLANDB_ENTRY_STATE */
}

static int br_vlan_modify_state(struct net_bridge_vlan_group *vg,
				struct net_bridge_vlan *v,
				u8 state,
				bool *changed,
				struct netlink_ext_ack *extack)
{
	struct net_bridge *br;

	ASSERT_RTNL();

	if (state > BR_STATE_BLOCKING) {
		NL_SET_ERR_MSG_MOD(extack, "Invalid vlan state");
		return -EINVAL;
	}

	if (br_vlan_is_brentry(v))
		br = v->br;
	else
		br = v->port->br;

	if (br->stp_enabled == BR_KERNEL_STP) {
		NL_SET_ERR_MSG_MOD(extack, "Can't modify vlan state when using kernel STP");
		return -EBUSY;
	}

	if (v->state == state)
		return 0;

	if (v->vid == br_get_pvid(vg))
		br_vlan_set_pvid_state(vg, state);

	br_vlan_set_state(v, state);
	*changed = true;

	return 0;
}

static int br_vlan_process_one_opts(const struct net_bridge *br,
				    const struct net_bridge_port *p,
				    struct net_bridge_vlan_group *vg,
				    struct net_bridge_vlan *v,
				    struct nlattr **tb,
				    bool *changed,
				    struct netlink_ext_ack *extack)
{
	int err;

	*changed = false;
	if (tb[BRIDGE_VLANDB_ENTRY_STATE]) {
		u8 state = nla_get_u8(tb[BRIDGE_VLANDB_ENTRY_STATE]);

		err = br_vlan_modify_state(vg, v, state, changed, extack);
		if (err)
			return err;
	}

	return 0;
}

int br_vlan_process_options(const struct net_bridge *br,
			    const struct net_bridge_port *p,
			    struct net_bridge_vlan *range_start,
			    struct net_bridge_vlan *range_end,
			    struct nlattr **tb,
			    struct netlink_ext_ack *extack)
{
	struct net_bridge_vlan *v, *curr_start = NULL, *curr_end = NULL;
	struct net_bridge_vlan_group *vg;
	int vid, err = 0;
	u16 pvid;

	if (p)
		vg = nbp_vlan_group(p);
	else
		vg = br_vlan_group(br);

	if (!range_start || !br_vlan_should_use(range_start)) {
		NL_SET_ERR_MSG_MOD(extack, "Vlan range start doesn't exist, can't process options");
		return -ENOENT;
	}
	if (!range_end || !br_vlan_should_use(range_end)) {
		NL_SET_ERR_MSG_MOD(extack, "Vlan range end doesn't exist, can't process options");
		return -ENOENT;
	}

	pvid = br_get_pvid(vg);
	for (vid = range_start->vid; vid <= range_end->vid; vid++) {
		bool changed = false;

		v = br_vlan_find(vg, vid);
		if (!v || !br_vlan_should_use(v)) {
			NL_SET_ERR_MSG_MOD(extack, "Vlan in range doesn't exist, can't process options");
			err = -ENOENT;
			break;
		}

		err = br_vlan_process_one_opts(br, p, vg, v, tb, &changed,
					       extack);
		if (err)
			break;

		if (changed) {
			/* vlan options changed, check for range */
			if (!curr_start) {
				curr_start = v;
				curr_end = v;
				continue;
			}

			if (v->vid == pvid ||
			    !br_vlan_can_enter_range(v, curr_end)) {
				br_vlan_notify(br, p, curr_start->vid,
					       curr_end->vid, RTM_NEWVLAN);
				curr_start = v;
			}
			curr_end = v;
		} else {
			/* nothing changed and nothing to notify yet */
			if (!curr_start)
				continue;

			br_vlan_notify(br, p, curr_start->vid, curr_end->vid,
				       RTM_NEWVLAN);
			curr_start = NULL;
			curr_end = NULL;
		}
	}
	if (curr_start)
		br_vlan_notify(br, p, curr_start->vid, curr_end->vid,
			       RTM_NEWVLAN);

	return err;
}
