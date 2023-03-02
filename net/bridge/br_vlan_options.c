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
	u8 range_mc_rtr = br_vlan_multicast_router(range_end);
	u8 curr_mc_rtr = br_vlan_multicast_router(v_curr);

	return v_curr->state == range_end->state &&
	       __vlan_tun_can_enter_range(v_curr, range_end) &&
	       curr_mc_rtr == range_mc_rtr;
}

bool br_vlan_opts_fill(struct sk_buff *skb, const struct net_bridge_vlan *v,
		       const struct net_bridge_port *p)
{
	if (nla_put_u8(skb, BRIDGE_VLANDB_ENTRY_STATE, br_vlan_get_state(v)) ||
	    !__vlan_tun_put(skb, v))
		return false;

#ifdef CONFIG_BRIDGE_IGMP_SNOOPING
	if (nla_put_u8(skb, BRIDGE_VLANDB_ENTRY_MCAST_ROUTER,
		       br_vlan_multicast_router(v)))
		return false;
	if (p && !br_multicast_port_ctx_vlan_disabled(&v->port_mcast_ctx) &&
	    (nla_put_u32(skb, BRIDGE_VLANDB_ENTRY_MCAST_N_GROUPS,
			 br_multicast_ngroups_get(&v->port_mcast_ctx)) ||
	     nla_put_u32(skb, BRIDGE_VLANDB_ENTRY_MCAST_MAX_GROUPS,
			 br_multicast_ngroups_get_max(&v->port_mcast_ctx))))
		return false;
#endif

	return true;
}

size_t br_vlan_opts_nl_size(void)
{
	return nla_total_size(sizeof(u8)) /* BRIDGE_VLANDB_ENTRY_STATE */
	       + nla_total_size(0) /* BRIDGE_VLANDB_ENTRY_TUNNEL_INFO */
	       + nla_total_size(sizeof(u32)) /* BRIDGE_VLANDB_TINFO_ID */
#ifdef CONFIG_BRIDGE_IGMP_SNOOPING
	       + nla_total_size(sizeof(u8)) /* BRIDGE_VLANDB_ENTRY_MCAST_ROUTER */
	       + nla_total_size(sizeof(u32)) /* BRIDGE_VLANDB_ENTRY_MCAST_N_GROUPS */
	       + nla_total_size(sizeof(u32)) /* BRIDGE_VLANDB_ENTRY_MCAST_MAX_GROUPS */
#endif
	       + 0;
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

	if (br_opt_get(br, BROPT_MST_ENABLED)) {
		NL_SET_ERR_MSG_MOD(extack, "Can't modify vlan state directly when MST is enabled");
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

#ifdef CONFIG_BRIDGE_IGMP_SNOOPING
	if (tb[BRIDGE_VLANDB_ENTRY_MCAST_ROUTER]) {
		u8 val;

		val = nla_get_u8(tb[BRIDGE_VLANDB_ENTRY_MCAST_ROUTER]);
		err = br_multicast_set_vlan_router(v, val);
		if (err)
			return err;
		*changed = true;
	}
	if (tb[BRIDGE_VLANDB_ENTRY_MCAST_MAX_GROUPS]) {
		u32 val;

		if (!p) {
			NL_SET_ERR_MSG_MOD(extack, "Can't set mcast_max_groups for non-port vlans");
			return -EINVAL;
		}
		if (br_multicast_port_ctx_vlan_disabled(&v->port_mcast_ctx)) {
			NL_SET_ERR_MSG_MOD(extack, "Multicast snooping disabled on this VLAN");
			return -EINVAL;
		}

		val = nla_get_u32(tb[BRIDGE_VLANDB_ENTRY_MCAST_MAX_GROUPS]);
		br_multicast_ngroups_set_max(&v->port_mcast_ctx, val);
		*changed = true;
	}
#endif

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

bool br_vlan_global_opts_can_enter_range(const struct net_bridge_vlan *v_curr,
					 const struct net_bridge_vlan *r_end)
{
	return v_curr->vid - r_end->vid == 1 &&
		v_curr->msti == r_end->msti &&
	       ((v_curr->priv_flags ^ r_end->priv_flags) &
		BR_VLFLAG_GLOBAL_MCAST_ENABLED) == 0 &&
		br_multicast_ctx_options_equal(&v_curr->br_mcast_ctx,
					       &r_end->br_mcast_ctx);
}

bool br_vlan_global_opts_fill(struct sk_buff *skb, u16 vid, u16 vid_range,
			      const struct net_bridge_vlan *v_opts)
{
	struct nlattr *nest2 __maybe_unused;
	u64 clockval __maybe_unused;
	struct nlattr *nest;

	nest = nla_nest_start(skb, BRIDGE_VLANDB_GLOBAL_OPTIONS);
	if (!nest)
		return false;

	if (nla_put_u16(skb, BRIDGE_VLANDB_GOPTS_ID, vid))
		goto out_err;

	if (vid_range && vid < vid_range &&
	    nla_put_u16(skb, BRIDGE_VLANDB_GOPTS_RANGE, vid_range))
		goto out_err;

#ifdef CONFIG_BRIDGE_IGMP_SNOOPING
	if (nla_put_u8(skb, BRIDGE_VLANDB_GOPTS_MCAST_SNOOPING,
		       !!(v_opts->priv_flags & BR_VLFLAG_GLOBAL_MCAST_ENABLED)) ||
	    nla_put_u8(skb, BRIDGE_VLANDB_GOPTS_MCAST_IGMP_VERSION,
		       v_opts->br_mcast_ctx.multicast_igmp_version) ||
	    nla_put_u32(skb, BRIDGE_VLANDB_GOPTS_MCAST_LAST_MEMBER_CNT,
			v_opts->br_mcast_ctx.multicast_last_member_count) ||
	    nla_put_u32(skb, BRIDGE_VLANDB_GOPTS_MCAST_STARTUP_QUERY_CNT,
			v_opts->br_mcast_ctx.multicast_startup_query_count) ||
	    nla_put_u8(skb, BRIDGE_VLANDB_GOPTS_MCAST_QUERIER,
		       v_opts->br_mcast_ctx.multicast_querier) ||
	    br_multicast_dump_querier_state(skb, &v_opts->br_mcast_ctx,
					    BRIDGE_VLANDB_GOPTS_MCAST_QUERIER_STATE))
		goto out_err;

	clockval = jiffies_to_clock_t(v_opts->br_mcast_ctx.multicast_last_member_interval);
	if (nla_put_u64_64bit(skb, BRIDGE_VLANDB_GOPTS_MCAST_LAST_MEMBER_INTVL,
			      clockval, BRIDGE_VLANDB_GOPTS_PAD))
		goto out_err;
	clockval = jiffies_to_clock_t(v_opts->br_mcast_ctx.multicast_membership_interval);
	if (nla_put_u64_64bit(skb, BRIDGE_VLANDB_GOPTS_MCAST_MEMBERSHIP_INTVL,
			      clockval, BRIDGE_VLANDB_GOPTS_PAD))
		goto out_err;
	clockval = jiffies_to_clock_t(v_opts->br_mcast_ctx.multicast_querier_interval);
	if (nla_put_u64_64bit(skb, BRIDGE_VLANDB_GOPTS_MCAST_QUERIER_INTVL,
			      clockval, BRIDGE_VLANDB_GOPTS_PAD))
		goto out_err;
	clockval = jiffies_to_clock_t(v_opts->br_mcast_ctx.multicast_query_interval);
	if (nla_put_u64_64bit(skb, BRIDGE_VLANDB_GOPTS_MCAST_QUERY_INTVL,
			      clockval, BRIDGE_VLANDB_GOPTS_PAD))
		goto out_err;
	clockval = jiffies_to_clock_t(v_opts->br_mcast_ctx.multicast_query_response_interval);
	if (nla_put_u64_64bit(skb, BRIDGE_VLANDB_GOPTS_MCAST_QUERY_RESPONSE_INTVL,
			      clockval, BRIDGE_VLANDB_GOPTS_PAD))
		goto out_err;
	clockval = jiffies_to_clock_t(v_opts->br_mcast_ctx.multicast_startup_query_interval);
	if (nla_put_u64_64bit(skb, BRIDGE_VLANDB_GOPTS_MCAST_STARTUP_QUERY_INTVL,
			      clockval, BRIDGE_VLANDB_GOPTS_PAD))
		goto out_err;

	if (br_rports_have_mc_router(&v_opts->br_mcast_ctx)) {
		nest2 = nla_nest_start(skb,
				       BRIDGE_VLANDB_GOPTS_MCAST_ROUTER_PORTS);
		if (!nest2)
			goto out_err;

		rcu_read_lock();
		if (br_rports_fill_info(skb, &v_opts->br_mcast_ctx)) {
			rcu_read_unlock();
			nla_nest_cancel(skb, nest2);
			goto out_err;
		}
		rcu_read_unlock();

		nla_nest_end(skb, nest2);
	}

#if IS_ENABLED(CONFIG_IPV6)
	if (nla_put_u8(skb, BRIDGE_VLANDB_GOPTS_MCAST_MLD_VERSION,
		       v_opts->br_mcast_ctx.multicast_mld_version))
		goto out_err;
#endif
#endif

	if (nla_put_u16(skb, BRIDGE_VLANDB_GOPTS_MSTI, v_opts->msti))
		goto out_err;

	nla_nest_end(skb, nest);

	return true;

out_err:
	nla_nest_cancel(skb, nest);
	return false;
}

static size_t rtnl_vlan_global_opts_nlmsg_size(const struct net_bridge_vlan *v)
{
	return NLMSG_ALIGN(sizeof(struct br_vlan_msg))
		+ nla_total_size(0) /* BRIDGE_VLANDB_GLOBAL_OPTIONS */
		+ nla_total_size(sizeof(u16)) /* BRIDGE_VLANDB_GOPTS_ID */
#ifdef CONFIG_BRIDGE_IGMP_SNOOPING
		+ nla_total_size(sizeof(u8)) /* BRIDGE_VLANDB_GOPTS_MCAST_SNOOPING */
		+ nla_total_size(sizeof(u8)) /* BRIDGE_VLANDB_GOPTS_MCAST_IGMP_VERSION */
		+ nla_total_size(sizeof(u8)) /* BRIDGE_VLANDB_GOPTS_MCAST_MLD_VERSION */
		+ nla_total_size(sizeof(u32)) /* BRIDGE_VLANDB_GOPTS_MCAST_LAST_MEMBER_CNT */
		+ nla_total_size(sizeof(u32)) /* BRIDGE_VLANDB_GOPTS_MCAST_STARTUP_QUERY_CNT */
		+ nla_total_size(sizeof(u64)) /* BRIDGE_VLANDB_GOPTS_MCAST_LAST_MEMBER_INTVL */
		+ nla_total_size(sizeof(u64)) /* BRIDGE_VLANDB_GOPTS_MCAST_MEMBERSHIP_INTVL */
		+ nla_total_size(sizeof(u64)) /* BRIDGE_VLANDB_GOPTS_MCAST_QUERIER_INTVL */
		+ nla_total_size(sizeof(u64)) /* BRIDGE_VLANDB_GOPTS_MCAST_QUERY_INTVL */
		+ nla_total_size(sizeof(u64)) /* BRIDGE_VLANDB_GOPTS_MCAST_QUERY_RESPONSE_INTVL */
		+ nla_total_size(sizeof(u64)) /* BRIDGE_VLANDB_GOPTS_MCAST_STARTUP_QUERY_INTVL */
		+ nla_total_size(sizeof(u8)) /* BRIDGE_VLANDB_GOPTS_MCAST_QUERIER */
		+ br_multicast_querier_state_size() /* BRIDGE_VLANDB_GOPTS_MCAST_QUERIER_STATE */
		+ nla_total_size(0) /* BRIDGE_VLANDB_GOPTS_MCAST_ROUTER_PORTS */
		+ br_rports_size(&v->br_mcast_ctx) /* BRIDGE_VLANDB_GOPTS_MCAST_ROUTER_PORTS */
#endif
		+ nla_total_size(sizeof(u16)) /* BRIDGE_VLANDB_GOPTS_MSTI */
		+ nla_total_size(sizeof(u16)); /* BRIDGE_VLANDB_GOPTS_RANGE */
}

static void br_vlan_global_opts_notify(const struct net_bridge *br,
				       u16 vid, u16 vid_range)
{
	struct net_bridge_vlan *v;
	struct br_vlan_msg *bvm;
	struct nlmsghdr *nlh;
	struct sk_buff *skb;
	int err = -ENOBUFS;

	/* right now notifications are done only with rtnl held */
	ASSERT_RTNL();

	/* need to find the vlan due to flags/options */
	v = br_vlan_find(br_vlan_group(br), vid);
	if (!v)
		return;

	skb = nlmsg_new(rtnl_vlan_global_opts_nlmsg_size(v), GFP_KERNEL);
	if (!skb)
		goto out_err;

	err = -EMSGSIZE;
	nlh = nlmsg_put(skb, 0, 0, RTM_NEWVLAN, sizeof(*bvm), 0);
	if (!nlh)
		goto out_err;
	bvm = nlmsg_data(nlh);
	memset(bvm, 0, sizeof(*bvm));
	bvm->family = AF_BRIDGE;
	bvm->ifindex = br->dev->ifindex;

	if (!br_vlan_global_opts_fill(skb, vid, vid_range, v))
		goto out_err;

	nlmsg_end(skb, nlh);
	rtnl_notify(skb, dev_net(br->dev), 0, RTNLGRP_BRVLAN, NULL, GFP_KERNEL);
	return;

out_err:
	rtnl_set_sk_err(dev_net(br->dev), RTNLGRP_BRVLAN, err);
	kfree_skb(skb);
}

static int br_vlan_process_global_one_opts(const struct net_bridge *br,
					   struct net_bridge_vlan_group *vg,
					   struct net_bridge_vlan *v,
					   struct nlattr **tb,
					   bool *changed,
					   struct netlink_ext_ack *extack)
{
	int err __maybe_unused;

	*changed = false;
#ifdef CONFIG_BRIDGE_IGMP_SNOOPING
	if (tb[BRIDGE_VLANDB_GOPTS_MCAST_SNOOPING]) {
		u8 mc_snooping;

		mc_snooping = nla_get_u8(tb[BRIDGE_VLANDB_GOPTS_MCAST_SNOOPING]);
		if (br_multicast_toggle_global_vlan(v, !!mc_snooping))
			*changed = true;
	}
	if (tb[BRIDGE_VLANDB_GOPTS_MCAST_IGMP_VERSION]) {
		u8 ver;

		ver = nla_get_u8(tb[BRIDGE_VLANDB_GOPTS_MCAST_IGMP_VERSION]);
		err = br_multicast_set_igmp_version(&v->br_mcast_ctx, ver);
		if (err)
			return err;
		*changed = true;
	}
	if (tb[BRIDGE_VLANDB_GOPTS_MCAST_LAST_MEMBER_CNT]) {
		u32 cnt;

		cnt = nla_get_u32(tb[BRIDGE_VLANDB_GOPTS_MCAST_LAST_MEMBER_CNT]);
		v->br_mcast_ctx.multicast_last_member_count = cnt;
		*changed = true;
	}
	if (tb[BRIDGE_VLANDB_GOPTS_MCAST_STARTUP_QUERY_CNT]) {
		u32 cnt;

		cnt = nla_get_u32(tb[BRIDGE_VLANDB_GOPTS_MCAST_STARTUP_QUERY_CNT]);
		v->br_mcast_ctx.multicast_startup_query_count = cnt;
		*changed = true;
	}
	if (tb[BRIDGE_VLANDB_GOPTS_MCAST_LAST_MEMBER_INTVL]) {
		u64 val;

		val = nla_get_u64(tb[BRIDGE_VLANDB_GOPTS_MCAST_LAST_MEMBER_INTVL]);
		v->br_mcast_ctx.multicast_last_member_interval = clock_t_to_jiffies(val);
		*changed = true;
	}
	if (tb[BRIDGE_VLANDB_GOPTS_MCAST_MEMBERSHIP_INTVL]) {
		u64 val;

		val = nla_get_u64(tb[BRIDGE_VLANDB_GOPTS_MCAST_MEMBERSHIP_INTVL]);
		v->br_mcast_ctx.multicast_membership_interval = clock_t_to_jiffies(val);
		*changed = true;
	}
	if (tb[BRIDGE_VLANDB_GOPTS_MCAST_QUERIER_INTVL]) {
		u64 val;

		val = nla_get_u64(tb[BRIDGE_VLANDB_GOPTS_MCAST_QUERIER_INTVL]);
		v->br_mcast_ctx.multicast_querier_interval = clock_t_to_jiffies(val);
		*changed = true;
	}
	if (tb[BRIDGE_VLANDB_GOPTS_MCAST_QUERY_INTVL]) {
		u64 val;

		val = nla_get_u64(tb[BRIDGE_VLANDB_GOPTS_MCAST_QUERY_INTVL]);
		br_multicast_set_query_intvl(&v->br_mcast_ctx, val);
		*changed = true;
	}
	if (tb[BRIDGE_VLANDB_GOPTS_MCAST_QUERY_RESPONSE_INTVL]) {
		u64 val;

		val = nla_get_u64(tb[BRIDGE_VLANDB_GOPTS_MCAST_QUERY_RESPONSE_INTVL]);
		v->br_mcast_ctx.multicast_query_response_interval = clock_t_to_jiffies(val);
		*changed = true;
	}
	if (tb[BRIDGE_VLANDB_GOPTS_MCAST_STARTUP_QUERY_INTVL]) {
		u64 val;

		val = nla_get_u64(tb[BRIDGE_VLANDB_GOPTS_MCAST_STARTUP_QUERY_INTVL]);
		br_multicast_set_startup_query_intvl(&v->br_mcast_ctx, val);
		*changed = true;
	}
	if (tb[BRIDGE_VLANDB_GOPTS_MCAST_QUERIER]) {
		u8 val;

		val = nla_get_u8(tb[BRIDGE_VLANDB_GOPTS_MCAST_QUERIER]);
		err = br_multicast_set_querier(&v->br_mcast_ctx, val);
		if (err)
			return err;
		*changed = true;
	}
#if IS_ENABLED(CONFIG_IPV6)
	if (tb[BRIDGE_VLANDB_GOPTS_MCAST_MLD_VERSION]) {
		u8 ver;

		ver = nla_get_u8(tb[BRIDGE_VLANDB_GOPTS_MCAST_MLD_VERSION]);
		err = br_multicast_set_mld_version(&v->br_mcast_ctx, ver);
		if (err)
			return err;
		*changed = true;
	}
#endif
#endif
	if (tb[BRIDGE_VLANDB_GOPTS_MSTI]) {
		u16 msti;

		msti = nla_get_u16(tb[BRIDGE_VLANDB_GOPTS_MSTI]);
		err = br_mst_vlan_set_msti(v, msti);
		if (err)
			return err;
		*changed = true;
	}

	return 0;
}

static const struct nla_policy br_vlan_db_gpol[BRIDGE_VLANDB_GOPTS_MAX + 1] = {
	[BRIDGE_VLANDB_GOPTS_ID]	= { .type = NLA_U16 },
	[BRIDGE_VLANDB_GOPTS_RANGE]	= { .type = NLA_U16 },
	[BRIDGE_VLANDB_GOPTS_MCAST_SNOOPING]	= { .type = NLA_U8 },
	[BRIDGE_VLANDB_GOPTS_MCAST_MLD_VERSION]	= { .type = NLA_U8 },
	[BRIDGE_VLANDB_GOPTS_MCAST_QUERY_INTVL]	= { .type = NLA_U64 },
	[BRIDGE_VLANDB_GOPTS_MCAST_QUERIER]	= { .type = NLA_U8 },
	[BRIDGE_VLANDB_GOPTS_MCAST_IGMP_VERSION]	= { .type = NLA_U8 },
	[BRIDGE_VLANDB_GOPTS_MCAST_LAST_MEMBER_CNT]	= { .type = NLA_U32 },
	[BRIDGE_VLANDB_GOPTS_MCAST_STARTUP_QUERY_CNT]	= { .type = NLA_U32 },
	[BRIDGE_VLANDB_GOPTS_MCAST_LAST_MEMBER_INTVL]	= { .type = NLA_U64 },
	[BRIDGE_VLANDB_GOPTS_MCAST_MEMBERSHIP_INTVL]	= { .type = NLA_U64 },
	[BRIDGE_VLANDB_GOPTS_MCAST_QUERIER_INTVL]	= { .type = NLA_U64 },
	[BRIDGE_VLANDB_GOPTS_MCAST_STARTUP_QUERY_INTVL]	= { .type = NLA_U64 },
	[BRIDGE_VLANDB_GOPTS_MCAST_QUERY_RESPONSE_INTVL] = { .type = NLA_U64 },
	[BRIDGE_VLANDB_GOPTS_MSTI] = NLA_POLICY_MAX(NLA_U16, VLAN_N_VID - 1),
};

int br_vlan_rtm_process_global_options(struct net_device *dev,
				       const struct nlattr *attr,
				       int cmd,
				       struct netlink_ext_ack *extack)
{
	struct net_bridge_vlan *v, *curr_start = NULL, *curr_end = NULL;
	struct nlattr *tb[BRIDGE_VLANDB_GOPTS_MAX + 1];
	struct net_bridge_vlan_group *vg;
	u16 vid, vid_range = 0;
	struct net_bridge *br;
	int err = 0;

	if (cmd != RTM_NEWVLAN) {
		NL_SET_ERR_MSG_MOD(extack, "Global vlan options support only set operation");
		return -EINVAL;
	}
	if (!netif_is_bridge_master(dev)) {
		NL_SET_ERR_MSG_MOD(extack, "Global vlan options can only be set on bridge device");
		return -EINVAL;
	}
	br = netdev_priv(dev);
	vg = br_vlan_group(br);
	if (WARN_ON(!vg))
		return -ENODEV;

	err = nla_parse_nested(tb, BRIDGE_VLANDB_GOPTS_MAX, attr,
			       br_vlan_db_gpol, extack);
	if (err)
		return err;

	if (!tb[BRIDGE_VLANDB_GOPTS_ID]) {
		NL_SET_ERR_MSG_MOD(extack, "Missing vlan entry id");
		return -EINVAL;
	}
	vid = nla_get_u16(tb[BRIDGE_VLANDB_GOPTS_ID]);
	if (!br_vlan_valid_id(vid, extack))
		return -EINVAL;

	if (tb[BRIDGE_VLANDB_GOPTS_RANGE]) {
		vid_range = nla_get_u16(tb[BRIDGE_VLANDB_GOPTS_RANGE]);
		if (!br_vlan_valid_id(vid_range, extack))
			return -EINVAL;
		if (vid >= vid_range) {
			NL_SET_ERR_MSG_MOD(extack, "End vlan id is less than or equal to start vlan id");
			return -EINVAL;
		}
	} else {
		vid_range = vid;
	}

	for (; vid <= vid_range; vid++) {
		bool changed = false;

		v = br_vlan_find(vg, vid);
		if (!v) {
			NL_SET_ERR_MSG_MOD(extack, "Vlan in range doesn't exist, can't process global options");
			err = -ENOENT;
			break;
		}

		err = br_vlan_process_global_one_opts(br, vg, v, tb, &changed,
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

			if (!br_vlan_global_opts_can_enter_range(v, curr_end)) {
				br_vlan_global_opts_notify(br, curr_start->vid,
							   curr_end->vid);
				curr_start = v;
			}
			curr_end = v;
		} else {
			/* nothing changed and nothing to notify yet */
			if (!curr_start)
				continue;

			br_vlan_global_opts_notify(br, curr_start->vid,
						   curr_end->vid);
			curr_start = NULL;
			curr_end = NULL;
		}
	}
	if (curr_start)
		br_vlan_global_opts_notify(br, curr_start->vid, curr_end->vid);

	return err;
}
