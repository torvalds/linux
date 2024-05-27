// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *	Bridge Multiple Spanning Tree Support
 *
 *	Authors:
 *	Tobias Waldekranz		<tobias@waldekranz.com>
 */

#include <linux/kernel.h>
#include <net/switchdev.h>

#include "br_private.h"

DEFINE_STATIC_KEY_FALSE(br_mst_used);

bool br_mst_enabled(const struct net_device *dev)
{
	if (!netif_is_bridge_master(dev))
		return false;

	return br_opt_get(netdev_priv(dev), BROPT_MST_ENABLED);
}
EXPORT_SYMBOL_GPL(br_mst_enabled);

int br_mst_get_info(const struct net_device *dev, u16 msti, unsigned long *vids)
{
	const struct net_bridge_vlan_group *vg;
	const struct net_bridge_vlan *v;
	const struct net_bridge *br;

	ASSERT_RTNL();

	if (!netif_is_bridge_master(dev))
		return -EINVAL;

	br = netdev_priv(dev);
	if (!br_opt_get(br, BROPT_MST_ENABLED))
		return -EINVAL;

	vg = br_vlan_group(br);

	list_for_each_entry(v, &vg->vlan_list, vlist) {
		if (v->msti == msti)
			__set_bit(v->vid, vids);
	}

	return 0;
}
EXPORT_SYMBOL_GPL(br_mst_get_info);

int br_mst_get_state(const struct net_device *dev, u16 msti, u8 *state)
{
	const struct net_bridge_port *p = NULL;
	const struct net_bridge_vlan_group *vg;
	const struct net_bridge_vlan *v;

	ASSERT_RTNL();

	p = br_port_get_check_rtnl(dev);
	if (!p || !br_opt_get(p->br, BROPT_MST_ENABLED))
		return -EINVAL;

	vg = nbp_vlan_group(p);

	list_for_each_entry(v, &vg->vlan_list, vlist) {
		if (v->brvlan->msti == msti) {
			*state = v->state;
			return 0;
		}
	}

	return -ENOENT;
}
EXPORT_SYMBOL_GPL(br_mst_get_state);

static void br_mst_vlan_set_state(struct net_bridge_port *p, struct net_bridge_vlan *v,
				  u8 state)
{
	struct net_bridge_vlan_group *vg = nbp_vlan_group(p);

	if (br_vlan_get_state(v) == state)
		return;

	br_vlan_set_state(v, state);

	if (v->vid == vg->pvid)
		br_vlan_set_pvid_state(vg, state);
}

int br_mst_set_state(struct net_bridge_port *p, u16 msti, u8 state,
		     struct netlink_ext_ack *extack)
{
	struct switchdev_attr attr = {
		.id = SWITCHDEV_ATTR_ID_PORT_MST_STATE,
		.orig_dev = p->dev,
		.u.mst_state = {
			.msti = msti,
			.state = state,
		},
	};
	struct net_bridge_vlan_group *vg;
	struct net_bridge_vlan *v;
	int err = 0;

	rcu_read_lock();
	vg = nbp_vlan_group(p);
	if (!vg)
		goto out;

	/* MSTI 0 (CST) state changes are notified via the regular
	 * SWITCHDEV_ATTR_ID_PORT_STP_STATE.
	 */
	if (msti) {
		err = switchdev_port_attr_set(p->dev, &attr, extack);
		if (err && err != -EOPNOTSUPP)
			goto out;
	}

	err = 0;
	list_for_each_entry_rcu(v, &vg->vlan_list, vlist) {
		if (v->brvlan->msti != msti)
			continue;

		br_mst_vlan_set_state(p, v, state);
	}

out:
	rcu_read_unlock();
	return err;
}

static void br_mst_vlan_sync_state(struct net_bridge_vlan *pv, u16 msti)
{
	struct net_bridge_vlan_group *vg = nbp_vlan_group(pv->port);
	struct net_bridge_vlan *v;

	list_for_each_entry(v, &vg->vlan_list, vlist) {
		/* If this port already has a defined state in this
		 * MSTI (through some other VLAN membership), inherit
		 * it.
		 */
		if (v != pv && v->brvlan->msti == msti) {
			br_mst_vlan_set_state(pv->port, pv, v->state);
			return;
		}
	}

	/* Otherwise, start out in a new MSTI with all ports disabled. */
	return br_mst_vlan_set_state(pv->port, pv, BR_STATE_DISABLED);
}

int br_mst_vlan_set_msti(struct net_bridge_vlan *mv, u16 msti)
{
	struct switchdev_attr attr = {
		.id = SWITCHDEV_ATTR_ID_VLAN_MSTI,
		.orig_dev = mv->br->dev,
		.u.vlan_msti = {
			.vid = mv->vid,
			.msti = msti,
		},
	};
	struct net_bridge_vlan_group *vg;
	struct net_bridge_vlan *pv;
	struct net_bridge_port *p;
	int err;

	if (mv->msti == msti)
		return 0;

	err = switchdev_port_attr_set(mv->br->dev, &attr, NULL);
	if (err && err != -EOPNOTSUPP)
		return err;

	mv->msti = msti;

	list_for_each_entry(p, &mv->br->port_list, list) {
		vg = nbp_vlan_group(p);

		pv = br_vlan_find(vg, mv->vid);
		if (pv)
			br_mst_vlan_sync_state(pv, msti);
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
	struct switchdev_attr attr = {
		.id = SWITCHDEV_ATTR_ID_BRIDGE_MST,
		.orig_dev = br->dev,
		.u.mst = on,
	};
	struct net_bridge_vlan_group *vg;
	struct net_bridge_port *p;
	int err;

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

	err = switchdev_port_attr_set(br->dev, &attr, extack);
	if (err && err != -EOPNOTSUPP)
		return err;

	if (on)
		static_branch_enable(&br_mst_used);
	else
		static_branch_disable(&br_mst_used);

	br_opt_toggle(br, BROPT_MST_ENABLED, on);
	return 0;
}

size_t br_mst_info_size(const struct net_bridge_vlan_group *vg)
{
	DECLARE_BITMAP(seen, VLAN_N_VID) = { 0 };
	const struct net_bridge_vlan *v;
	size_t sz;

	/* IFLA_BRIDGE_MST */
	sz = nla_total_size(0);

	list_for_each_entry_rcu(v, &vg->vlan_list, vlist) {
		if (test_bit(v->brvlan->msti, seen))
			continue;

		/* IFLA_BRIDGE_MST_ENTRY */
		sz += nla_total_size(0) +
			/* IFLA_BRIDGE_MST_ENTRY_MSTI */
			nla_total_size(sizeof(u16)) +
			/* IFLA_BRIDGE_MST_ENTRY_STATE */
			nla_total_size(sizeof(u8));

		__set_bit(v->brvlan->msti, seen);
	}

	return sz;
}

int br_mst_fill_info(struct sk_buff *skb,
		     const struct net_bridge_vlan_group *vg)
{
	DECLARE_BITMAP(seen, VLAN_N_VID) = { 0 };
	const struct net_bridge_vlan *v;
	struct nlattr *nest;
	int err = 0;

	list_for_each_entry(v, &vg->vlan_list, vlist) {
		if (test_bit(v->brvlan->msti, seen))
			continue;

		nest = nla_nest_start_noflag(skb, IFLA_BRIDGE_MST_ENTRY);
		if (!nest ||
		    nla_put_u16(skb, IFLA_BRIDGE_MST_ENTRY_MSTI, v->brvlan->msti) ||
		    nla_put_u8(skb, IFLA_BRIDGE_MST_ENTRY_STATE, v->state)) {
			err = -EMSGSIZE;
			break;
		}
		nla_nest_end(skb, nest);

		__set_bit(v->brvlan->msti, seen);
	}

	return err;
}

static const struct nla_policy br_mst_nl_policy[IFLA_BRIDGE_MST_ENTRY_MAX + 1] = {
	[IFLA_BRIDGE_MST_ENTRY_MSTI] = NLA_POLICY_RANGE(NLA_U16,
						   1, /* 0 reserved for CST */
						   VLAN_N_VID - 1),
	[IFLA_BRIDGE_MST_ENTRY_STATE] = NLA_POLICY_RANGE(NLA_U8,
						    BR_STATE_DISABLED,
						    BR_STATE_BLOCKING),
};

static int br_mst_process_one(struct net_bridge_port *p,
			      const struct nlattr *attr,
			      struct netlink_ext_ack *extack)
{
	struct nlattr *tb[IFLA_BRIDGE_MST_ENTRY_MAX + 1];
	u16 msti;
	u8 state;
	int err;

	err = nla_parse_nested(tb, IFLA_BRIDGE_MST_ENTRY_MAX, attr,
			       br_mst_nl_policy, extack);
	if (err)
		return err;

	if (!tb[IFLA_BRIDGE_MST_ENTRY_MSTI]) {
		NL_SET_ERR_MSG_MOD(extack, "MSTI not specified");
		return -EINVAL;
	}

	if (!tb[IFLA_BRIDGE_MST_ENTRY_STATE]) {
		NL_SET_ERR_MSG_MOD(extack, "State not specified");
		return -EINVAL;
	}

	msti = nla_get_u16(tb[IFLA_BRIDGE_MST_ENTRY_MSTI]);
	state = nla_get_u8(tb[IFLA_BRIDGE_MST_ENTRY_STATE]);

	return br_mst_set_state(p, msti, state, extack);
}

int br_mst_process(struct net_bridge_port *p, const struct nlattr *mst_attr,
		   struct netlink_ext_ack *extack)
{
	struct nlattr *attr;
	int err, msts = 0;
	int rem;

	if (!br_opt_get(p->br, BROPT_MST_ENABLED)) {
		NL_SET_ERR_MSG_MOD(extack, "Can't modify MST state when MST is disabled");
		return -EBUSY;
	}

	nla_for_each_nested(attr, mst_attr, rem) {
		switch (nla_type(attr)) {
		case IFLA_BRIDGE_MST_ENTRY:
			err = br_mst_process_one(p, attr, extack);
			break;
		default:
			continue;
		}

		msts++;
		if (err)
			break;
	}

	if (!msts) {
		NL_SET_ERR_MSG_MOD(extack, "Found no MST entries to process");
		err = -EINVAL;
	}

	return err;
}
