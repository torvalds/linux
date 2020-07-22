// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2020, Nikolay Aleksandrov <nikolay@cumulusnetworks.com>
#include <linux/kernel.h>
#include <linux/netdevice.h>
#include <linux/rtnetlink.h>
#include <linux/slab.h>
#include <net/ip_tunnels.h>

#include "br_private.h"
#include "br_private_tunnel.h"

static bool __vlan_tun_put(struct sk_buff *skb, const struct net_bridge_vlan *v)
{
	__be32 tid = tunnel_id_to_key32(v->tinfo.tunnel_id);
	struct nlattr *nest;

	if (!v->tinfo.tunnel_dst)
		return true;

	nest = nla_nest_start(skb, BRIDGE_VLANDB_ENTRY_TUNNEL_INFO);
	if (!nest)
		return false;
	if (nla_put_u32(skb, BRIDGE_VLANDB_TINFO_ID, be32_to_cpu(tid))) {
		nla_nest_cancel(skb, nest);
		return false;
	}
	nla_nest_end(skb, nest);

	return true;
}

static bool __vlan_tun_can_enter_range(const struct net_bridge_vlan *v_curr,
				       const struct net_bridge_vlan *range_end)
{
	return (!v_curr->tinfo.tunnel_dst && !range_end->tinfo.tunnel_dst) ||
	       vlan_tunid_inrange(v_curr, range_end);
}

/* check if the options' state of v_curr allow it to enter the range */
bool br_vlan_opts_eq_range(const struct net_bridge_vlan *v_curr,
			   const struct net_bridge_vlan *range_end)
{
	return v_curr->state == range_end->state &&
	       __vlan_tun_can_enter_range(v_curr, range_end);
}

bool br_vlan_opts_fill(struct sk_buff *skb, const struct net_bridge_vlan *v)
{
	return !nla_put_u8(skb, BRIDGE_VLANDB_ENTRY_STATE,
			   br_vlan_get_state(v)) &&
	       __vlan_tun_put(skb, v);
}

size_t br_vlan_opts_nl_size(void)
{
	return nla_total_size(sizeof(u8)) /* BRIDGE_VLANDB_ENTRY_STATE */
	       + nla_total_size(0) /* BRIDGE_VLANDB_ENTRY_TUNNEL_INFO */
	       + nla_total_size(sizeof(u32)); /* BRIDGE_VLANDB_TINFO_ID */
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

static const struct nla_policy br_vlandb_tinfo_pol[BRIDGE_VLANDB_TINFO_MAX + 1] = {
	[BRIDGE_VLANDB_TINFO_ID]	= { .type = NLA_U32 },
	[BRIDGE_VLANDB_TINFO_CMD]	= { .type = NLA_U32 },
};

static int br_vlan_modify_tunnel(const struct net_bridge_port *p,
				 struct net_bridge_vlan *v,
				 struct nlattr **tb,
				 bool *changed,
				 struct netlink_ext_ack *extack)
{
	struct nlattr *tun_tb[BRIDGE_VLANDB_TINFO_MAX + 1], *attr;
	struct bridge_vlan_info *vinfo;
	u32 tun_id = 0;
	int cmd, err;

	if (!p) {
		NL_SET_ERR_MSG_MOD(extack, "Can't modify tunnel mapping of non-port vlans");
		return -EINVAL;
	}
	if (!(p->flags & BR_VLAN_TUNNEL)) {
		NL_SET_ERR_MSG_MOD(extack, "Port doesn't have tunnel flag set");
		return -EINVAL;
	}

	attr = tb[BRIDGE_VLANDB_ENTRY_TUNNEL_INFO];
	err = nla_parse_nested(tun_tb, BRIDGE_VLANDB_TINFO_MAX, attr,
			       br_vlandb_tinfo_pol, extack);
	if (err)
		return err;

	if (!tun_tb[BRIDGE_VLANDB_TINFO_CMD]) {
		NL_SET_ERR_MSG_MOD(extack, "Missing tunnel command attribute");
		return -ENOENT;
	}
	cmd = nla_get_u32(tun_tb[BRIDGE_VLANDB_TINFO_CMD]);
	switch (cmd) {
	case RTM_SETLINK:
		if (!tun_tb[BRIDGE_VLANDB_TINFO_ID]) {
			NL_SET_ERR_MSG_MOD(extack, "Missing tunnel id attribute");
			return -ENOENT;
		}
		/* when working on vlan ranges this is the starting tunnel id */
		tun_id = nla_get_u32(tun_tb[BRIDGE_VLANDB_TINFO_ID]);
		/* vlan info attr is guaranteed by br_vlan_rtm_process_one */
		vinfo = nla_data(tb[BRIDGE_VLANDB_ENTRY_INFO]);
		/* tunnel ids are mapped to each vlan in increasing order,
		 * the starting vlan is in BRIDGE_VLANDB_ENTRY_INFO and v is the
		 * current vlan, so we compute: tun_id + v - vinfo->vid
		 */
		tun_id += v->vid - vinfo->vid;
		break;
	case RTM_DELLINK:
		break;
	default:
		NL_SET_ERR_MSG_MOD(extack, "Unsupported tunnel command");
		return -EINVAL;
	}

	return br_vlan_tunnel_info(p, cmd, v->vid, tun_id, changed);
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
	if (tb[BRIDGE_VLANDB_ENTRY_TUNNEL_INFO]) {
		err = br_vlan_modify_tunnel(p, v, tb, changed, extack);
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
