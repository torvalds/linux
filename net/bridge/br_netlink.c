/*
 *	Bridge netlink control interface
 *
 *	Authors:
 *	Stephen Hemminger		<shemminger@osdl.org>
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 */

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/etherdevice.h>
#include <net/rtnetlink.h>
#include <net/net_namespace.h>
#include <net/sock.h>
#include <uapi/linux/if_bridge.h>

#include "br_private.h"
#include "br_private_stp.h"

static int __get_num_vlan_infos(struct net_bridge_vlan_group *vg,
				u32 filter_mask)
{
	struct net_bridge_vlan *v;
	u16 vid_range_start = 0, vid_range_end = 0, vid_range_flags = 0;
	u16 flags, pvid;
	int num_vlans = 0;

	if (!(filter_mask & RTEXT_FILTER_BRVLAN_COMPRESSED))
		return 0;

	pvid = br_get_pvid(vg);
	/* Count number of vlan infos */
	list_for_each_entry_rcu(v, &vg->vlan_list, vlist) {
		flags = 0;
		/* only a context, bridge vlan not activated */
		if (!br_vlan_should_use(v))
			continue;
		if (v->vid == pvid)
			flags |= BRIDGE_VLAN_INFO_PVID;

		if (v->flags & BRIDGE_VLAN_INFO_UNTAGGED)
			flags |= BRIDGE_VLAN_INFO_UNTAGGED;

		if (vid_range_start == 0) {
			goto initvars;
		} else if ((v->vid - vid_range_end) == 1 &&
			flags == vid_range_flags) {
			vid_range_end = v->vid;
			continue;
		} else {
			if ((vid_range_end - vid_range_start) > 0)
				num_vlans += 2;
			else
				num_vlans += 1;
		}
initvars:
		vid_range_start = v->vid;
		vid_range_end = v->vid;
		vid_range_flags = flags;
	}

	if (vid_range_start != 0) {
		if ((vid_range_end - vid_range_start) > 0)
			num_vlans += 2;
		else
			num_vlans += 1;
	}

	return num_vlans;
}

static int br_get_num_vlan_infos(struct net_bridge_vlan_group *vg,
				 u32 filter_mask)
{
	int num_vlans;

	if (!vg)
		return 0;

	if (filter_mask & RTEXT_FILTER_BRVLAN)
		return vg->num_vlans;

	rcu_read_lock();
	num_vlans = __get_num_vlan_infos(vg, filter_mask);
	rcu_read_unlock();

	return num_vlans;
}

static size_t br_get_link_af_size_filtered(const struct net_device *dev,
					   u32 filter_mask)
{
	struct net_bridge_vlan_group *vg = NULL;
	struct net_bridge_port *p;
	struct net_bridge *br;
	int num_vlan_infos;

	rcu_read_lock();
	if (br_port_exists(dev)) {
		p = br_port_get_rcu(dev);
		vg = nbp_vlan_group_rcu(p);
	} else if (dev->priv_flags & IFF_EBRIDGE) {
		br = netdev_priv(dev);
		vg = br_vlan_group_rcu(br);
	}
	num_vlan_infos = br_get_num_vlan_infos(vg, filter_mask);
	rcu_read_unlock();

	/* Each VLAN is returned in bridge_vlan_info along with flags */
	return num_vlan_infos * nla_total_size(sizeof(struct bridge_vlan_info));
}

static inline size_t br_port_info_size(void)
{
	return nla_total_size(1)	/* IFLA_BRPORT_STATE  */
		+ nla_total_size(2)	/* IFLA_BRPORT_PRIORITY */
		+ nla_total_size(4)	/* IFLA_BRPORT_COST */
		+ nla_total_size(1)	/* IFLA_BRPORT_MODE */
		+ nla_total_size(1)	/* IFLA_BRPORT_GUARD */
		+ nla_total_size(1)	/* IFLA_BRPORT_PROTECT */
		+ nla_total_size(1)	/* IFLA_BRPORT_FAST_LEAVE */
		+ nla_total_size(1)	/* IFLA_BRPORT_LEARNING */
		+ nla_total_size(1)	/* IFLA_BRPORT_UNICAST_FLOOD */
		+ nla_total_size(1)	/* IFLA_BRPORT_PROXYARP */
		+ nla_total_size(1)	/* IFLA_BRPORT_PROXYARP_WIFI */
		+ nla_total_size(sizeof(struct ifla_bridge_id))	/* IFLA_BRPORT_ROOT_ID */
		+ nla_total_size(sizeof(struct ifla_bridge_id))	/* IFLA_BRPORT_BRIDGE_ID */
		+ nla_total_size(sizeof(u16))	/* IFLA_BRPORT_DESIGNATED_PORT */
		+ nla_total_size(sizeof(u16))	/* IFLA_BRPORT_DESIGNATED_COST */
		+ nla_total_size(sizeof(u16))	/* IFLA_BRPORT_ID */
		+ nla_total_size(sizeof(u16))	/* IFLA_BRPORT_NO */
		+ nla_total_size(sizeof(u8))	/* IFLA_BRPORT_TOPOLOGY_CHANGE_ACK */
		+ nla_total_size(sizeof(u8))	/* IFLA_BRPORT_CONFIG_PENDING */
		+ nla_total_size_64bit(sizeof(u64)) /* IFLA_BRPORT_MESSAGE_AGE_TIMER */
		+ nla_total_size_64bit(sizeof(u64)) /* IFLA_BRPORT_FORWARD_DELAY_TIMER */
		+ nla_total_size_64bit(sizeof(u64)) /* IFLA_BRPORT_HOLD_TIMER */
#ifdef CONFIG_BRIDGE_IGMP_SNOOPING
		+ nla_total_size(sizeof(u8))	/* IFLA_BRPORT_MULTICAST_ROUTER */
#endif
		+ 0;
}

static inline size_t br_nlmsg_size(struct net_device *dev, u32 filter_mask)
{
	return NLMSG_ALIGN(sizeof(struct ifinfomsg))
		+ nla_total_size(IFNAMSIZ) /* IFLA_IFNAME */
		+ nla_total_size(MAX_ADDR_LEN) /* IFLA_ADDRESS */
		+ nla_total_size(4) /* IFLA_MASTER */
		+ nla_total_size(4) /* IFLA_MTU */
		+ nla_total_size(4) /* IFLA_LINK */
		+ nla_total_size(1) /* IFLA_OPERSTATE */
		+ nla_total_size(br_port_info_size()) /* IFLA_PROTINFO */
		+ nla_total_size(br_get_link_af_size_filtered(dev,
				 filter_mask)); /* IFLA_AF_SPEC */
}

static int br_port_fill_attrs(struct sk_buff *skb,
			      const struct net_bridge_port *p)
{
	u8 mode = !!(p->flags & BR_HAIRPIN_MODE);
	u64 timerval;

	if (nla_put_u8(skb, IFLA_BRPORT_STATE, p->state) ||
	    nla_put_u16(skb, IFLA_BRPORT_PRIORITY, p->priority) ||
	    nla_put_u32(skb, IFLA_BRPORT_COST, p->path_cost) ||
	    nla_put_u8(skb, IFLA_BRPORT_MODE, mode) ||
	    nla_put_u8(skb, IFLA_BRPORT_GUARD, !!(p->flags & BR_BPDU_GUARD)) ||
	    nla_put_u8(skb, IFLA_BRPORT_PROTECT, !!(p->flags & BR_ROOT_BLOCK)) ||
	    nla_put_u8(skb, IFLA_BRPORT_FAST_LEAVE, !!(p->flags & BR_MULTICAST_FAST_LEAVE)) ||
	    nla_put_u8(skb, IFLA_BRPORT_LEARNING, !!(p->flags & BR_LEARNING)) ||
	    nla_put_u8(skb, IFLA_BRPORT_UNICAST_FLOOD, !!(p->flags & BR_FLOOD)) ||
	    nla_put_u8(skb, IFLA_BRPORT_PROXYARP, !!(p->flags & BR_PROXYARP)) ||
	    nla_put_u8(skb, IFLA_BRPORT_PROXYARP_WIFI,
		       !!(p->flags & BR_PROXYARP_WIFI)) ||
	    nla_put(skb, IFLA_BRPORT_ROOT_ID, sizeof(struct ifla_bridge_id),
		    &p->designated_root) ||
	    nla_put(skb, IFLA_BRPORT_BRIDGE_ID, sizeof(struct ifla_bridge_id),
		    &p->designated_bridge) ||
	    nla_put_u16(skb, IFLA_BRPORT_DESIGNATED_PORT, p->designated_port) ||
	    nla_put_u16(skb, IFLA_BRPORT_DESIGNATED_COST, p->designated_cost) ||
	    nla_put_u16(skb, IFLA_BRPORT_ID, p->port_id) ||
	    nla_put_u16(skb, IFLA_BRPORT_NO, p->port_no) ||
	    nla_put_u8(skb, IFLA_BRPORT_TOPOLOGY_CHANGE_ACK,
		       p->topology_change_ack) ||
	    nla_put_u8(skb, IFLA_BRPORT_CONFIG_PENDING, p->config_pending))
		return -EMSGSIZE;

	timerval = br_timer_value(&p->message_age_timer);
	if (nla_put_u64_64bit(skb, IFLA_BRPORT_MESSAGE_AGE_TIMER, timerval,
			      IFLA_BRPORT_PAD))
		return -EMSGSIZE;
	timerval = br_timer_value(&p->forward_delay_timer);
	if (nla_put_u64_64bit(skb, IFLA_BRPORT_FORWARD_DELAY_TIMER, timerval,
			      IFLA_BRPORT_PAD))
		return -EMSGSIZE;
	timerval = br_timer_value(&p->hold_timer);
	if (nla_put_u64_64bit(skb, IFLA_BRPORT_HOLD_TIMER, timerval,
			      IFLA_BRPORT_PAD))
		return -EMSGSIZE;

#ifdef CONFIG_BRIDGE_IGMP_SNOOPING
	if (nla_put_u8(skb, IFLA_BRPORT_MULTICAST_ROUTER,
		       p->multicast_router))
		return -EMSGSIZE;
#endif

	return 0;
}

static int br_fill_ifvlaninfo_range(struct sk_buff *skb, u16 vid_start,
				    u16 vid_end, u16 flags)
{
	struct  bridge_vlan_info vinfo;

	if ((vid_end - vid_start) > 0) {
		/* add range to skb */
		vinfo.vid = vid_start;
		vinfo.flags = flags | BRIDGE_VLAN_INFO_RANGE_BEGIN;
		if (nla_put(skb, IFLA_BRIDGE_VLAN_INFO,
			    sizeof(vinfo), &vinfo))
			goto nla_put_failure;

		vinfo.vid = vid_end;
		vinfo.flags = flags | BRIDGE_VLAN_INFO_RANGE_END;
		if (nla_put(skb, IFLA_BRIDGE_VLAN_INFO,
			    sizeof(vinfo), &vinfo))
			goto nla_put_failure;
	} else {
		vinfo.vid = vid_start;
		vinfo.flags = flags;
		if (nla_put(skb, IFLA_BRIDGE_VLAN_INFO,
			    sizeof(vinfo), &vinfo))
			goto nla_put_failure;
	}

	return 0;

nla_put_failure:
	return -EMSGSIZE;
}

static int br_fill_ifvlaninfo_compressed(struct sk_buff *skb,
					 struct net_bridge_vlan_group *vg)
{
	struct net_bridge_vlan *v;
	u16 vid_range_start = 0, vid_range_end = 0, vid_range_flags = 0;
	u16 flags, pvid;
	int err = 0;

	/* Pack IFLA_BRIDGE_VLAN_INFO's for every vlan
	 * and mark vlan info with begin and end flags
	 * if vlaninfo represents a range
	 */
	pvid = br_get_pvid(vg);
	list_for_each_entry_rcu(v, &vg->vlan_list, vlist) {
		flags = 0;
		if (!br_vlan_should_use(v))
			continue;
		if (v->vid == pvid)
			flags |= BRIDGE_VLAN_INFO_PVID;

		if (v->flags & BRIDGE_VLAN_INFO_UNTAGGED)
			flags |= BRIDGE_VLAN_INFO_UNTAGGED;

		if (vid_range_start == 0) {
			goto initvars;
		} else if ((v->vid - vid_range_end) == 1 &&
			flags == vid_range_flags) {
			vid_range_end = v->vid;
			continue;
		} else {
			err = br_fill_ifvlaninfo_range(skb, vid_range_start,
						       vid_range_end,
						       vid_range_flags);
			if (err)
				return err;
		}

initvars:
		vid_range_start = v->vid;
		vid_range_end = v->vid;
		vid_range_flags = flags;
	}

	if (vid_range_start != 0) {
		/* Call it once more to send any left over vlans */
		err = br_fill_ifvlaninfo_range(skb, vid_range_start,
					       vid_range_end,
					       vid_range_flags);
		if (err)
			return err;
	}

	return 0;
}

static int br_fill_ifvlaninfo(struct sk_buff *skb,
			      struct net_bridge_vlan_group *vg)
{
	struct bridge_vlan_info vinfo;
	struct net_bridge_vlan *v;
	u16 pvid;

	pvid = br_get_pvid(vg);
	list_for_each_entry_rcu(v, &vg->vlan_list, vlist) {
		if (!br_vlan_should_use(v))
			continue;

		vinfo.vid = v->vid;
		vinfo.flags = 0;
		if (v->vid == pvid)
			vinfo.flags |= BRIDGE_VLAN_INFO_PVID;

		if (v->flags & BRIDGE_VLAN_INFO_UNTAGGED)
			vinfo.flags |= BRIDGE_VLAN_INFO_UNTAGGED;

		if (nla_put(skb, IFLA_BRIDGE_VLAN_INFO,
			    sizeof(vinfo), &vinfo))
			goto nla_put_failure;
	}

	return 0;

nla_put_failure:
	return -EMSGSIZE;
}

/*
 * Create one netlink message for one interface
 * Contains port and master info as well as carrier and bridge state.
 */
static int br_fill_ifinfo(struct sk_buff *skb,
			  struct net_bridge_port *port,
			  u32 pid, u32 seq, int event, unsigned int flags,
			  u32 filter_mask, const struct net_device *dev)
{
	struct net_bridge *br;
	struct ifinfomsg *hdr;
	struct nlmsghdr *nlh;
	u8 operstate = netif_running(dev) ? dev->operstate : IF_OPER_DOWN;

	if (port)
		br = port->br;
	else
		br = netdev_priv(dev);

	br_debug(br, "br_fill_info event %d port %s master %s\n",
		     event, dev->name, br->dev->name);

	nlh = nlmsg_put(skb, pid, seq, event, sizeof(*hdr), flags);
	if (nlh == NULL)
		return -EMSGSIZE;

	hdr = nlmsg_data(nlh);
	hdr->ifi_family = AF_BRIDGE;
	hdr->__ifi_pad = 0;
	hdr->ifi_type = dev->type;
	hdr->ifi_index = dev->ifindex;
	hdr->ifi_flags = dev_get_flags(dev);
	hdr->ifi_change = 0;

	if (nla_put_string(skb, IFLA_IFNAME, dev->name) ||
	    nla_put_u32(skb, IFLA_MASTER, br->dev->ifindex) ||
	    nla_put_u32(skb, IFLA_MTU, dev->mtu) ||
	    nla_put_u8(skb, IFLA_OPERSTATE, operstate) ||
	    (dev->addr_len &&
	     nla_put(skb, IFLA_ADDRESS, dev->addr_len, dev->dev_addr)) ||
	    (dev->ifindex != dev_get_iflink(dev) &&
	     nla_put_u32(skb, IFLA_LINK, dev_get_iflink(dev))))
		goto nla_put_failure;

	if (event == RTM_NEWLINK && port) {
		struct nlattr *nest
			= nla_nest_start(skb, IFLA_PROTINFO | NLA_F_NESTED);

		if (nest == NULL || br_port_fill_attrs(skb, port) < 0)
			goto nla_put_failure;
		nla_nest_end(skb, nest);
	}

	/* Check if  the VID information is requested */
	if ((filter_mask & RTEXT_FILTER_BRVLAN) ||
	    (filter_mask & RTEXT_FILTER_BRVLAN_COMPRESSED)) {
		struct net_bridge_vlan_group *vg;
		struct nlattr *af;
		int err;

		/* RCU needed because of the VLAN locking rules (rcu || rtnl) */
		rcu_read_lock();
		if (port)
			vg = nbp_vlan_group_rcu(port);
		else
			vg = br_vlan_group_rcu(br);

		if (!vg || !vg->num_vlans) {
			rcu_read_unlock();
			goto done;
		}
		af = nla_nest_start(skb, IFLA_AF_SPEC);
		if (!af) {
			rcu_read_unlock();
			goto nla_put_failure;
		}
		if (filter_mask & RTEXT_FILTER_BRVLAN_COMPRESSED)
			err = br_fill_ifvlaninfo_compressed(skb, vg);
		else
			err = br_fill_ifvlaninfo(skb, vg);
		rcu_read_unlock();
		if (err)
			goto nla_put_failure;
		nla_nest_end(skb, af);
	}

done:
	nlmsg_end(skb, nlh);
	return 0;

nla_put_failure:
	nlmsg_cancel(skb, nlh);
	return -EMSGSIZE;
}

/*
 * Notify listeners of a change in port information
 */
void br_ifinfo_notify(int event, struct net_bridge_port *port)
{
	struct net *net;
	struct sk_buff *skb;
	int err = -ENOBUFS;
	u32 filter = RTEXT_FILTER_BRVLAN_COMPRESSED;

	if (!port)
		return;

	net = dev_net(port->dev);
	br_debug(port->br, "port %u(%s) event %d\n",
		 (unsigned int)port->port_no, port->dev->name, event);

	skb = nlmsg_new(br_nlmsg_size(port->dev, filter), GFP_ATOMIC);
	if (skb == NULL)
		goto errout;

	err = br_fill_ifinfo(skb, port, 0, 0, event, 0, filter, port->dev);
	if (err < 0) {
		/* -EMSGSIZE implies BUG in br_nlmsg_size() */
		WARN_ON(err == -EMSGSIZE);
		kfree_skb(skb);
		goto errout;
	}
	rtnl_notify(skb, net, 0, RTNLGRP_LINK, NULL, GFP_ATOMIC);
	return;
errout:
	rtnl_set_sk_err(net, RTNLGRP_LINK, err);
}


/*
 * Dump information about all ports, in response to GETLINK
 */
int br_getlink(struct sk_buff *skb, u32 pid, u32 seq,
	       struct net_device *dev, u32 filter_mask, int nlflags)
{
	struct net_bridge_port *port = br_port_get_rtnl(dev);

	if (!port && !(filter_mask & RTEXT_FILTER_BRVLAN) &&
	    !(filter_mask & RTEXT_FILTER_BRVLAN_COMPRESSED))
		return 0;

	return br_fill_ifinfo(skb, port, pid, seq, RTM_NEWLINK, nlflags,
			      filter_mask, dev);
}

static int br_vlan_info(struct net_bridge *br, struct net_bridge_port *p,
			int cmd, struct bridge_vlan_info *vinfo)
{
	int err = 0;

	switch (cmd) {
	case RTM_SETLINK:
		if (p) {
			/* if the MASTER flag is set this will act on the global
			 * per-VLAN entry as well
			 */
			err = nbp_vlan_add(p, vinfo->vid, vinfo->flags);
			if (err)
				break;
		} else {
			vinfo->flags |= BRIDGE_VLAN_INFO_BRENTRY;
			err = br_vlan_add(br, vinfo->vid, vinfo->flags);
		}
		break;

	case RTM_DELLINK:
		if (p) {
			nbp_vlan_delete(p, vinfo->vid);
			if (vinfo->flags & BRIDGE_VLAN_INFO_MASTER)
				br_vlan_delete(p->br, vinfo->vid);
		} else {
			br_vlan_delete(br, vinfo->vid);
		}
		break;
	}

	return err;
}

static int br_afspec(struct net_bridge *br,
		     struct net_bridge_port *p,
		     struct nlattr *af_spec,
		     int cmd)
{
	struct bridge_vlan_info *vinfo_start = NULL;
	struct bridge_vlan_info *vinfo = NULL;
	struct nlattr *attr;
	int err = 0;
	int rem;

	nla_for_each_nested(attr, af_spec, rem) {
		if (nla_type(attr) != IFLA_BRIDGE_VLAN_INFO)
			continue;
		if (nla_len(attr) != sizeof(struct bridge_vlan_info))
			return -EINVAL;
		vinfo = nla_data(attr);
		if (!vinfo->vid || vinfo->vid >= VLAN_VID_MASK)
			return -EINVAL;
		if (vinfo->flags & BRIDGE_VLAN_INFO_RANGE_BEGIN) {
			if (vinfo_start)
				return -EINVAL;
			vinfo_start = vinfo;
			/* don't allow range of pvids */
			if (vinfo_start->flags & BRIDGE_VLAN_INFO_PVID)
				return -EINVAL;
			continue;
		}

		if (vinfo_start) {
			struct bridge_vlan_info tmp_vinfo;
			int v;

			if (!(vinfo->flags & BRIDGE_VLAN_INFO_RANGE_END))
				return -EINVAL;

			if (vinfo->vid <= vinfo_start->vid)
				return -EINVAL;

			memcpy(&tmp_vinfo, vinfo_start,
			       sizeof(struct bridge_vlan_info));

			for (v = vinfo_start->vid; v <= vinfo->vid; v++) {
				tmp_vinfo.vid = v;
				err = br_vlan_info(br, p, cmd, &tmp_vinfo);
				if (err)
					break;
			}
			vinfo_start = NULL;
		} else {
			err = br_vlan_info(br, p, cmd, vinfo);
		}
		if (err)
			break;
	}

	return err;
}

static const struct nla_policy br_port_policy[IFLA_BRPORT_MAX + 1] = {
	[IFLA_BRPORT_STATE]	= { .type = NLA_U8 },
	[IFLA_BRPORT_COST]	= { .type = NLA_U32 },
	[IFLA_BRPORT_PRIORITY]	= { .type = NLA_U16 },
	[IFLA_BRPORT_MODE]	= { .type = NLA_U8 },
	[IFLA_BRPORT_GUARD]	= { .type = NLA_U8 },
	[IFLA_BRPORT_PROTECT]	= { .type = NLA_U8 },
	[IFLA_BRPORT_FAST_LEAVE]= { .type = NLA_U8 },
	[IFLA_BRPORT_LEARNING]	= { .type = NLA_U8 },
	[IFLA_BRPORT_UNICAST_FLOOD] = { .type = NLA_U8 },
	[IFLA_BRPORT_PROXYARP]	= { .type = NLA_U8 },
	[IFLA_BRPORT_PROXYARP_WIFI] = { .type = NLA_U8 },
	[IFLA_BRPORT_MULTICAST_ROUTER] = { .type = NLA_U8 },
};

/* Change the state of the port and notify spanning tree */
static int br_set_port_state(struct net_bridge_port *p, u8 state)
{
	if (state > BR_STATE_BLOCKING)
		return -EINVAL;

	/* if kernel STP is running, don't allow changes */
	if (p->br->stp_enabled == BR_KERNEL_STP)
		return -EBUSY;

	/* if device is not up, change is not allowed
	 * if link is not present, only allowable state is disabled
	 */
	if (!netif_running(p->dev) ||
	    (!netif_oper_up(p->dev) && state != BR_STATE_DISABLED))
		return -ENETDOWN;

	br_set_state(p, state);
	br_port_state_selection(p->br);
	return 0;
}

/* Set/clear or port flags based on attribute */
static void br_set_port_flag(struct net_bridge_port *p, struct nlattr *tb[],
			   int attrtype, unsigned long mask)
{
	if (tb[attrtype]) {
		u8 flag = nla_get_u8(tb[attrtype]);
		if (flag)
			p->flags |= mask;
		else
			p->flags &= ~mask;
	}
}

/* Process bridge protocol info on port */
static int br_setport(struct net_bridge_port *p, struct nlattr *tb[])
{
	int err;
	unsigned long old_flags = p->flags;

	br_set_port_flag(p, tb, IFLA_BRPORT_MODE, BR_HAIRPIN_MODE);
	br_set_port_flag(p, tb, IFLA_BRPORT_GUARD, BR_BPDU_GUARD);
	br_set_port_flag(p, tb, IFLA_BRPORT_FAST_LEAVE, BR_MULTICAST_FAST_LEAVE);
	br_set_port_flag(p, tb, IFLA_BRPORT_PROTECT, BR_ROOT_BLOCK);
	br_set_port_flag(p, tb, IFLA_BRPORT_LEARNING, BR_LEARNING);
	br_set_port_flag(p, tb, IFLA_BRPORT_UNICAST_FLOOD, BR_FLOOD);
	br_set_port_flag(p, tb, IFLA_BRPORT_PROXYARP, BR_PROXYARP);
	br_set_port_flag(p, tb, IFLA_BRPORT_PROXYARP_WIFI, BR_PROXYARP_WIFI);

	if (tb[IFLA_BRPORT_COST]) {
		err = br_stp_set_path_cost(p, nla_get_u32(tb[IFLA_BRPORT_COST]));
		if (err)
			return err;
	}

	if (tb[IFLA_BRPORT_PRIORITY]) {
		err = br_stp_set_port_priority(p, nla_get_u16(tb[IFLA_BRPORT_PRIORITY]));
		if (err)
			return err;
	}

	if (tb[IFLA_BRPORT_STATE]) {
		err = br_set_port_state(p, nla_get_u8(tb[IFLA_BRPORT_STATE]));
		if (err)
			return err;
	}

	if (tb[IFLA_BRPORT_FLUSH])
		br_fdb_delete_by_port(p->br, p, 0, 0);

#ifdef CONFIG_BRIDGE_IGMP_SNOOPING
	if (tb[IFLA_BRPORT_MULTICAST_ROUTER]) {
		u8 mcast_router = nla_get_u8(tb[IFLA_BRPORT_MULTICAST_ROUTER]);

		err = br_multicast_set_port_router(p, mcast_router);
		if (err)
			return err;
	}
#endif
	br_port_flags_change(p, old_flags ^ p->flags);
	return 0;
}

/* Change state and parameters on port. */
int br_setlink(struct net_device *dev, struct nlmsghdr *nlh, u16 flags)
{
	struct nlattr *protinfo;
	struct nlattr *afspec;
	struct net_bridge_port *p;
	struct nlattr *tb[IFLA_BRPORT_MAX + 1];
	int err = 0;

	protinfo = nlmsg_find_attr(nlh, sizeof(struct ifinfomsg), IFLA_PROTINFO);
	afspec = nlmsg_find_attr(nlh, sizeof(struct ifinfomsg), IFLA_AF_SPEC);
	if (!protinfo && !afspec)
		return 0;

	p = br_port_get_rtnl(dev);
	/* We want to accept dev as bridge itself if the AF_SPEC
	 * is set to see if someone is setting vlan info on the bridge
	 */
	if (!p && !afspec)
		return -EINVAL;

	if (p && protinfo) {
		if (protinfo->nla_type & NLA_F_NESTED) {
			err = nla_parse_nested(tb, IFLA_BRPORT_MAX,
					       protinfo, br_port_policy);
			if (err)
				return err;

			spin_lock_bh(&p->br->lock);
			err = br_setport(p, tb);
			spin_unlock_bh(&p->br->lock);
		} else {
			/* Binary compatibility with old RSTP */
			if (nla_len(protinfo) < sizeof(u8))
				return -EINVAL;

			spin_lock_bh(&p->br->lock);
			err = br_set_port_state(p, nla_get_u8(protinfo));
			spin_unlock_bh(&p->br->lock);
		}
		if (err)
			goto out;
	}

	if (afspec) {
		err = br_afspec((struct net_bridge *)netdev_priv(dev), p,
				afspec, RTM_SETLINK);
	}

	if (err == 0)
		br_ifinfo_notify(RTM_NEWLINK, p);
out:
	return err;
}

/* Delete port information */
int br_dellink(struct net_device *dev, struct nlmsghdr *nlh, u16 flags)
{
	struct nlattr *afspec;
	struct net_bridge_port *p;
	int err = 0;

	afspec = nlmsg_find_attr(nlh, sizeof(struct ifinfomsg), IFLA_AF_SPEC);
	if (!afspec)
		return 0;

	p = br_port_get_rtnl(dev);
	/* We want to accept dev as bridge itself as well */
	if (!p && !(dev->priv_flags & IFF_EBRIDGE))
		return -EINVAL;

	err = br_afspec((struct net_bridge *)netdev_priv(dev), p,
			afspec, RTM_DELLINK);
	if (err == 0)
		/* Send RTM_NEWLINK because userspace
		 * expects RTM_NEWLINK for vlan dels
		 */
		br_ifinfo_notify(RTM_NEWLINK, p);

	return err;
}
static int br_validate(struct nlattr *tb[], struct nlattr *data[])
{
	if (tb[IFLA_ADDRESS]) {
		if (nla_len(tb[IFLA_ADDRESS]) != ETH_ALEN)
			return -EINVAL;
		if (!is_valid_ether_addr(nla_data(tb[IFLA_ADDRESS])))
			return -EADDRNOTAVAIL;
	}

	if (!data)
		return 0;

#ifdef CONFIG_BRIDGE_VLAN_FILTERING
	if (data[IFLA_BR_VLAN_PROTOCOL]) {
		switch (nla_get_be16(data[IFLA_BR_VLAN_PROTOCOL])) {
		case htons(ETH_P_8021Q):
		case htons(ETH_P_8021AD):
			break;
		default:
			return -EPROTONOSUPPORT;
		}
	}
#endif

	return 0;
}

static int br_dev_newlink(struct net *src_net, struct net_device *dev,
			  struct nlattr *tb[], struct nlattr *data[])
{
	struct net_bridge *br = netdev_priv(dev);

	if (tb[IFLA_ADDRESS]) {
		spin_lock_bh(&br->lock);
		br_stp_change_bridge_id(br, nla_data(tb[IFLA_ADDRESS]));
		spin_unlock_bh(&br->lock);
	}

	return register_netdevice(dev);
}

static int br_port_slave_changelink(struct net_device *brdev,
				    struct net_device *dev,
				    struct nlattr *tb[],
				    struct nlattr *data[])
{
	struct net_bridge *br = netdev_priv(brdev);
	int ret;

	if (!data)
		return 0;

	spin_lock_bh(&br->lock);
	ret = br_setport(br_port_get_rtnl(dev), data);
	spin_unlock_bh(&br->lock);

	return ret;
}

static int br_port_fill_slave_info(struct sk_buff *skb,
				   const struct net_device *brdev,
				   const struct net_device *dev)
{
	return br_port_fill_attrs(skb, br_port_get_rtnl(dev));
}

static size_t br_port_get_slave_size(const struct net_device *brdev,
				     const struct net_device *dev)
{
	return br_port_info_size();
}

static const struct nla_policy br_policy[IFLA_BR_MAX + 1] = {
	[IFLA_BR_FORWARD_DELAY]	= { .type = NLA_U32 },
	[IFLA_BR_HELLO_TIME]	= { .type = NLA_U32 },
	[IFLA_BR_MAX_AGE]	= { .type = NLA_U32 },
	[IFLA_BR_AGEING_TIME] = { .type = NLA_U32 },
	[IFLA_BR_STP_STATE] = { .type = NLA_U32 },
	[IFLA_BR_PRIORITY] = { .type = NLA_U16 },
	[IFLA_BR_VLAN_FILTERING] = { .type = NLA_U8 },
	[IFLA_BR_VLAN_PROTOCOL] = { .type = NLA_U16 },
	[IFLA_BR_GROUP_FWD_MASK] = { .type = NLA_U16 },
	[IFLA_BR_GROUP_ADDR] = { .type = NLA_BINARY,
				 .len  = ETH_ALEN },
	[IFLA_BR_MCAST_ROUTER] = { .type = NLA_U8 },
	[IFLA_BR_MCAST_SNOOPING] = { .type = NLA_U8 },
	[IFLA_BR_MCAST_QUERY_USE_IFADDR] = { .type = NLA_U8 },
	[IFLA_BR_MCAST_QUERIER] = { .type = NLA_U8 },
	[IFLA_BR_MCAST_HASH_ELASTICITY] = { .type = NLA_U32 },
	[IFLA_BR_MCAST_HASH_MAX] = { .type = NLA_U32 },
	[IFLA_BR_MCAST_LAST_MEMBER_CNT] = { .type = NLA_U32 },
	[IFLA_BR_MCAST_STARTUP_QUERY_CNT] = { .type = NLA_U32 },
	[IFLA_BR_MCAST_LAST_MEMBER_INTVL] = { .type = NLA_U64 },
	[IFLA_BR_MCAST_MEMBERSHIP_INTVL] = { .type = NLA_U64 },
	[IFLA_BR_MCAST_QUERIER_INTVL] = { .type = NLA_U64 },
	[IFLA_BR_MCAST_QUERY_INTVL] = { .type = NLA_U64 },
	[IFLA_BR_MCAST_QUERY_RESPONSE_INTVL] = { .type = NLA_U64 },
	[IFLA_BR_MCAST_STARTUP_QUERY_INTVL] = { .type = NLA_U64 },
	[IFLA_BR_NF_CALL_IPTABLES] = { .type = NLA_U8 },
	[IFLA_BR_NF_CALL_IP6TABLES] = { .type = NLA_U8 },
	[IFLA_BR_NF_CALL_ARPTABLES] = { .type = NLA_U8 },
	[IFLA_BR_VLAN_DEFAULT_PVID] = { .type = NLA_U16 },
	[IFLA_BR_VLAN_STATS_ENABLED] = { .type = NLA_U8 },
	[IFLA_BR_MCAST_STATS_ENABLED] = { .type = NLA_U8 },
};

static int br_changelink(struct net_device *brdev, struct nlattr *tb[],
			 struct nlattr *data[])
{
	struct net_bridge *br = netdev_priv(brdev);
	int err;

	if (!data)
		return 0;

	if (data[IFLA_BR_FORWARD_DELAY]) {
		err = br_set_forward_delay(br, nla_get_u32(data[IFLA_BR_FORWARD_DELAY]));
		if (err)
			return err;
	}

	if (data[IFLA_BR_HELLO_TIME]) {
		err = br_set_hello_time(br, nla_get_u32(data[IFLA_BR_HELLO_TIME]));
		if (err)
			return err;
	}

	if (data[IFLA_BR_MAX_AGE]) {
		err = br_set_max_age(br, nla_get_u32(data[IFLA_BR_MAX_AGE]));
		if (err)
			return err;
	}

	if (data[IFLA_BR_AGEING_TIME]) {
		err = br_set_ageing_time(br, nla_get_u32(data[IFLA_BR_AGEING_TIME]));
		if (err)
			return err;
	}

	if (data[IFLA_BR_STP_STATE]) {
		u32 stp_enabled = nla_get_u32(data[IFLA_BR_STP_STATE]);

		br_stp_set_enabled(br, stp_enabled);
	}

	if (data[IFLA_BR_PRIORITY]) {
		u32 priority = nla_get_u16(data[IFLA_BR_PRIORITY]);

		br_stp_set_bridge_priority(br, priority);
	}

	if (data[IFLA_BR_VLAN_FILTERING]) {
		u8 vlan_filter = nla_get_u8(data[IFLA_BR_VLAN_FILTERING]);

		err = __br_vlan_filter_toggle(br, vlan_filter);
		if (err)
			return err;
	}

#ifdef CONFIG_BRIDGE_VLAN_FILTERING
	if (data[IFLA_BR_VLAN_PROTOCOL]) {
		__be16 vlan_proto = nla_get_be16(data[IFLA_BR_VLAN_PROTOCOL]);

		err = __br_vlan_set_proto(br, vlan_proto);
		if (err)
			return err;
	}

	if (data[IFLA_BR_VLAN_DEFAULT_PVID]) {
		__u16 defpvid = nla_get_u16(data[IFLA_BR_VLAN_DEFAULT_PVID]);

		err = __br_vlan_set_default_pvid(br, defpvid);
		if (err)
			return err;
	}

	if (data[IFLA_BR_VLAN_STATS_ENABLED]) {
		__u8 vlan_stats = nla_get_u8(data[IFLA_BR_VLAN_STATS_ENABLED]);

		err = br_vlan_set_stats(br, vlan_stats);
		if (err)
			return err;
	}
#endif

	if (data[IFLA_BR_GROUP_FWD_MASK]) {
		u16 fwd_mask = nla_get_u16(data[IFLA_BR_GROUP_FWD_MASK]);

		if (fwd_mask & BR_GROUPFWD_RESTRICTED)
			return -EINVAL;
		br->group_fwd_mask = fwd_mask;
	}

	if (data[IFLA_BR_GROUP_ADDR]) {
		u8 new_addr[ETH_ALEN];

		if (nla_len(data[IFLA_BR_GROUP_ADDR]) != ETH_ALEN)
			return -EINVAL;
		memcpy(new_addr, nla_data(data[IFLA_BR_GROUP_ADDR]), ETH_ALEN);
		if (!is_link_local_ether_addr(new_addr))
			return -EINVAL;
		if (new_addr[5] == 1 ||		/* 802.3x Pause address */
		    new_addr[5] == 2 ||		/* 802.3ad Slow protocols */
		    new_addr[5] == 3)		/* 802.1X PAE address */
			return -EINVAL;
		spin_lock_bh(&br->lock);
		memcpy(br->group_addr, new_addr, sizeof(br->group_addr));
		spin_unlock_bh(&br->lock);
		br->group_addr_set = true;
		br_recalculate_fwd_mask(br);
	}

	if (data[IFLA_BR_FDB_FLUSH])
		br_fdb_flush(br);

#ifdef CONFIG_BRIDGE_IGMP_SNOOPING
	if (data[IFLA_BR_MCAST_ROUTER]) {
		u8 multicast_router = nla_get_u8(data[IFLA_BR_MCAST_ROUTER]);

		err = br_multicast_set_router(br, multicast_router);
		if (err)
			return err;
	}

	if (data[IFLA_BR_MCAST_SNOOPING]) {
		u8 mcast_snooping = nla_get_u8(data[IFLA_BR_MCAST_SNOOPING]);

		err = br_multicast_toggle(br, mcast_snooping);
		if (err)
			return err;
	}

	if (data[IFLA_BR_MCAST_QUERY_USE_IFADDR]) {
		u8 val;

		val = nla_get_u8(data[IFLA_BR_MCAST_QUERY_USE_IFADDR]);
		br->multicast_query_use_ifaddr = !!val;
	}

	if (data[IFLA_BR_MCAST_QUERIER]) {
		u8 mcast_querier = nla_get_u8(data[IFLA_BR_MCAST_QUERIER]);

		err = br_multicast_set_querier(br, mcast_querier);
		if (err)
			return err;
	}

	if (data[IFLA_BR_MCAST_HASH_ELASTICITY]) {
		u32 val = nla_get_u32(data[IFLA_BR_MCAST_HASH_ELASTICITY]);

		br->hash_elasticity = val;
	}

	if (data[IFLA_BR_MCAST_HASH_MAX]) {
		u32 hash_max = nla_get_u32(data[IFLA_BR_MCAST_HASH_MAX]);

		err = br_multicast_set_hash_max(br, hash_max);
		if (err)
			return err;
	}

	if (data[IFLA_BR_MCAST_LAST_MEMBER_CNT]) {
		u32 val = nla_get_u32(data[IFLA_BR_MCAST_LAST_MEMBER_CNT]);

		br->multicast_last_member_count = val;
	}

	if (data[IFLA_BR_MCAST_STARTUP_QUERY_CNT]) {
		u32 val = nla_get_u32(data[IFLA_BR_MCAST_STARTUP_QUERY_CNT]);

		br->multicast_startup_query_count = val;
	}

	if (data[IFLA_BR_MCAST_LAST_MEMBER_INTVL]) {
		u64 val = nla_get_u64(data[IFLA_BR_MCAST_LAST_MEMBER_INTVL]);

		br->multicast_last_member_interval = clock_t_to_jiffies(val);
	}

	if (data[IFLA_BR_MCAST_MEMBERSHIP_INTVL]) {
		u64 val = nla_get_u64(data[IFLA_BR_MCAST_MEMBERSHIP_INTVL]);

		br->multicast_membership_interval = clock_t_to_jiffies(val);
	}

	if (data[IFLA_BR_MCAST_QUERIER_INTVL]) {
		u64 val = nla_get_u64(data[IFLA_BR_MCAST_QUERIER_INTVL]);

		br->multicast_querier_interval = clock_t_to_jiffies(val);
	}

	if (data[IFLA_BR_MCAST_QUERY_INTVL]) {
		u64 val = nla_get_u64(data[IFLA_BR_MCAST_QUERY_INTVL]);

		br->multicast_query_interval = clock_t_to_jiffies(val);
	}

	if (data[IFLA_BR_MCAST_QUERY_RESPONSE_INTVL]) {
		u64 val = nla_get_u64(data[IFLA_BR_MCAST_QUERY_RESPONSE_INTVL]);

		br->multicast_query_response_interval = clock_t_to_jiffies(val);
	}

	if (data[IFLA_BR_MCAST_STARTUP_QUERY_INTVL]) {
		u64 val = nla_get_u64(data[IFLA_BR_MCAST_STARTUP_QUERY_INTVL]);

		br->multicast_startup_query_interval = clock_t_to_jiffies(val);
	}

	if (data[IFLA_BR_MCAST_STATS_ENABLED]) {
		__u8 mcast_stats;

		mcast_stats = nla_get_u8(data[IFLA_BR_MCAST_STATS_ENABLED]);
		br->multicast_stats_enabled = !!mcast_stats;
	}
#endif
#if IS_ENABLED(CONFIG_BRIDGE_NETFILTER)
	if (data[IFLA_BR_NF_CALL_IPTABLES]) {
		u8 val = nla_get_u8(data[IFLA_BR_NF_CALL_IPTABLES]);

		br->nf_call_iptables = val ? true : false;
	}

	if (data[IFLA_BR_NF_CALL_IP6TABLES]) {
		u8 val = nla_get_u8(data[IFLA_BR_NF_CALL_IP6TABLES]);

		br->nf_call_ip6tables = val ? true : false;
	}

	if (data[IFLA_BR_NF_CALL_ARPTABLES]) {
		u8 val = nla_get_u8(data[IFLA_BR_NF_CALL_ARPTABLES]);

		br->nf_call_arptables = val ? true : false;
	}
#endif

	return 0;
}

static size_t br_get_size(const struct net_device *brdev)
{
	return nla_total_size(sizeof(u32)) +	/* IFLA_BR_FORWARD_DELAY  */
	       nla_total_size(sizeof(u32)) +	/* IFLA_BR_HELLO_TIME */
	       nla_total_size(sizeof(u32)) +	/* IFLA_BR_MAX_AGE */
	       nla_total_size(sizeof(u32)) +    /* IFLA_BR_AGEING_TIME */
	       nla_total_size(sizeof(u32)) +    /* IFLA_BR_STP_STATE */
	       nla_total_size(sizeof(u16)) +    /* IFLA_BR_PRIORITY */
	       nla_total_size(sizeof(u8)) +     /* IFLA_BR_VLAN_FILTERING */
#ifdef CONFIG_BRIDGE_VLAN_FILTERING
	       nla_total_size(sizeof(__be16)) +	/* IFLA_BR_VLAN_PROTOCOL */
	       nla_total_size(sizeof(u16)) +    /* IFLA_BR_VLAN_DEFAULT_PVID */
	       nla_total_size(sizeof(u8)) +     /* IFLA_BR_VLAN_STATS_ENABLED */
#endif
	       nla_total_size(sizeof(u16)) +    /* IFLA_BR_GROUP_FWD_MASK */
	       nla_total_size(sizeof(struct ifla_bridge_id)) +   /* IFLA_BR_ROOT_ID */
	       nla_total_size(sizeof(struct ifla_bridge_id)) +   /* IFLA_BR_BRIDGE_ID */
	       nla_total_size(sizeof(u16)) +    /* IFLA_BR_ROOT_PORT */
	       nla_total_size(sizeof(u32)) +    /* IFLA_BR_ROOT_PATH_COST */
	       nla_total_size(sizeof(u8)) +     /* IFLA_BR_TOPOLOGY_CHANGE */
	       nla_total_size(sizeof(u8)) +     /* IFLA_BR_TOPOLOGY_CHANGE_DETECTED */
	       nla_total_size_64bit(sizeof(u64)) + /* IFLA_BR_HELLO_TIMER */
	       nla_total_size_64bit(sizeof(u64)) + /* IFLA_BR_TCN_TIMER */
	       nla_total_size_64bit(sizeof(u64)) + /* IFLA_BR_TOPOLOGY_CHANGE_TIMER */
	       nla_total_size_64bit(sizeof(u64)) + /* IFLA_BR_GC_TIMER */
	       nla_total_size(ETH_ALEN) +       /* IFLA_BR_GROUP_ADDR */
#ifdef CONFIG_BRIDGE_IGMP_SNOOPING
	       nla_total_size(sizeof(u8)) +     /* IFLA_BR_MCAST_ROUTER */
	       nla_total_size(sizeof(u8)) +     /* IFLA_BR_MCAST_SNOOPING */
	       nla_total_size(sizeof(u8)) +     /* IFLA_BR_MCAST_QUERY_USE_IFADDR */
	       nla_total_size(sizeof(u8)) +     /* IFLA_BR_MCAST_QUERIER */
	       nla_total_size(sizeof(u8)) +     /* IFLA_BR_MCAST_STATS_ENABLED */
	       nla_total_size(sizeof(u32)) +    /* IFLA_BR_MCAST_HASH_ELASTICITY */
	       nla_total_size(sizeof(u32)) +    /* IFLA_BR_MCAST_HASH_MAX */
	       nla_total_size(sizeof(u32)) +    /* IFLA_BR_MCAST_LAST_MEMBER_CNT */
	       nla_total_size(sizeof(u32)) +    /* IFLA_BR_MCAST_STARTUP_QUERY_CNT */
	       nla_total_size_64bit(sizeof(u64)) + /* IFLA_BR_MCAST_LAST_MEMBER_INTVL */
	       nla_total_size_64bit(sizeof(u64)) + /* IFLA_BR_MCAST_MEMBERSHIP_INTVL */
	       nla_total_size_64bit(sizeof(u64)) + /* IFLA_BR_MCAST_QUERIER_INTVL */
	       nla_total_size_64bit(sizeof(u64)) + /* IFLA_BR_MCAST_QUERY_INTVL */
	       nla_total_size_64bit(sizeof(u64)) + /* IFLA_BR_MCAST_QUERY_RESPONSE_INTVL */
	       nla_total_size_64bit(sizeof(u64)) + /* IFLA_BR_MCAST_STARTUP_QUERY_INTVL */
#endif
#if IS_ENABLED(CONFIG_BRIDGE_NETFILTER)
	       nla_total_size(sizeof(u8)) +     /* IFLA_BR_NF_CALL_IPTABLES */
	       nla_total_size(sizeof(u8)) +     /* IFLA_BR_NF_CALL_IP6TABLES */
	       nla_total_size(sizeof(u8)) +     /* IFLA_BR_NF_CALL_ARPTABLES */
#endif
	       0;
}

static int br_fill_info(struct sk_buff *skb, const struct net_device *brdev)
{
	struct net_bridge *br = netdev_priv(brdev);
	u32 forward_delay = jiffies_to_clock_t(br->forward_delay);
	u32 hello_time = jiffies_to_clock_t(br->hello_time);
	u32 age_time = jiffies_to_clock_t(br->max_age);
	u32 ageing_time = jiffies_to_clock_t(br->ageing_time);
	u32 stp_enabled = br->stp_enabled;
	u16 priority = (br->bridge_id.prio[0] << 8) | br->bridge_id.prio[1];
	u8 vlan_enabled = br_vlan_enabled(br);
	u64 clockval;

	clockval = br_timer_value(&br->hello_timer);
	if (nla_put_u64_64bit(skb, IFLA_BR_HELLO_TIMER, clockval, IFLA_BR_PAD))
		return -EMSGSIZE;
	clockval = br_timer_value(&br->tcn_timer);
	if (nla_put_u64_64bit(skb, IFLA_BR_TCN_TIMER, clockval, IFLA_BR_PAD))
		return -EMSGSIZE;
	clockval = br_timer_value(&br->topology_change_timer);
	if (nla_put_u64_64bit(skb, IFLA_BR_TOPOLOGY_CHANGE_TIMER, clockval,
			      IFLA_BR_PAD))
		return -EMSGSIZE;
	clockval = br_timer_value(&br->gc_timer);
	if (nla_put_u64_64bit(skb, IFLA_BR_GC_TIMER, clockval, IFLA_BR_PAD))
		return -EMSGSIZE;

	if (nla_put_u32(skb, IFLA_BR_FORWARD_DELAY, forward_delay) ||
	    nla_put_u32(skb, IFLA_BR_HELLO_TIME, hello_time) ||
	    nla_put_u32(skb, IFLA_BR_MAX_AGE, age_time) ||
	    nla_put_u32(skb, IFLA_BR_AGEING_TIME, ageing_time) ||
	    nla_put_u32(skb, IFLA_BR_STP_STATE, stp_enabled) ||
	    nla_put_u16(skb, IFLA_BR_PRIORITY, priority) ||
	    nla_put_u8(skb, IFLA_BR_VLAN_FILTERING, vlan_enabled) ||
	    nla_put_u16(skb, IFLA_BR_GROUP_FWD_MASK, br->group_fwd_mask) ||
	    nla_put(skb, IFLA_BR_BRIDGE_ID, sizeof(struct ifla_bridge_id),
		    &br->bridge_id) ||
	    nla_put(skb, IFLA_BR_ROOT_ID, sizeof(struct ifla_bridge_id),
		    &br->designated_root) ||
	    nla_put_u16(skb, IFLA_BR_ROOT_PORT, br->root_port) ||
	    nla_put_u32(skb, IFLA_BR_ROOT_PATH_COST, br->root_path_cost) ||
	    nla_put_u8(skb, IFLA_BR_TOPOLOGY_CHANGE, br->topology_change) ||
	    nla_put_u8(skb, IFLA_BR_TOPOLOGY_CHANGE_DETECTED,
		       br->topology_change_detected) ||
	    nla_put(skb, IFLA_BR_GROUP_ADDR, ETH_ALEN, br->group_addr))
		return -EMSGSIZE;

#ifdef CONFIG_BRIDGE_VLAN_FILTERING
	if (nla_put_be16(skb, IFLA_BR_VLAN_PROTOCOL, br->vlan_proto) ||
	    nla_put_u16(skb, IFLA_BR_VLAN_DEFAULT_PVID, br->default_pvid) ||
	    nla_put_u8(skb, IFLA_BR_VLAN_STATS_ENABLED, br->vlan_stats_enabled))
		return -EMSGSIZE;
#endif
#ifdef CONFIG_BRIDGE_IGMP_SNOOPING
	if (nla_put_u8(skb, IFLA_BR_MCAST_ROUTER, br->multicast_router) ||
	    nla_put_u8(skb, IFLA_BR_MCAST_SNOOPING, !br->multicast_disabled) ||
	    nla_put_u8(skb, IFLA_BR_MCAST_QUERY_USE_IFADDR,
		       br->multicast_query_use_ifaddr) ||
	    nla_put_u8(skb, IFLA_BR_MCAST_QUERIER, br->multicast_querier) ||
	    nla_put_u8(skb, IFLA_BR_MCAST_STATS_ENABLED,
		       br->multicast_stats_enabled) ||
	    nla_put_u32(skb, IFLA_BR_MCAST_HASH_ELASTICITY,
			br->hash_elasticity) ||
	    nla_put_u32(skb, IFLA_BR_MCAST_HASH_MAX, br->hash_max) ||
	    nla_put_u32(skb, IFLA_BR_MCAST_LAST_MEMBER_CNT,
			br->multicast_last_member_count) ||
	    nla_put_u32(skb, IFLA_BR_MCAST_STARTUP_QUERY_CNT,
			br->multicast_startup_query_count))
		return -EMSGSIZE;

	clockval = jiffies_to_clock_t(br->multicast_last_member_interval);
	if (nla_put_u64_64bit(skb, IFLA_BR_MCAST_LAST_MEMBER_INTVL, clockval,
			      IFLA_BR_PAD))
		return -EMSGSIZE;
	clockval = jiffies_to_clock_t(br->multicast_membership_interval);
	if (nla_put_u64_64bit(skb, IFLA_BR_MCAST_MEMBERSHIP_INTVL, clockval,
			      IFLA_BR_PAD))
		return -EMSGSIZE;
	clockval = jiffies_to_clock_t(br->multicast_querier_interval);
	if (nla_put_u64_64bit(skb, IFLA_BR_MCAST_QUERIER_INTVL, clockval,
			      IFLA_BR_PAD))
		return -EMSGSIZE;
	clockval = jiffies_to_clock_t(br->multicast_query_interval);
	if (nla_put_u64_64bit(skb, IFLA_BR_MCAST_QUERY_INTVL, clockval,
			      IFLA_BR_PAD))
		return -EMSGSIZE;
	clockval = jiffies_to_clock_t(br->multicast_query_response_interval);
	if (nla_put_u64_64bit(skb, IFLA_BR_MCAST_QUERY_RESPONSE_INTVL, clockval,
			      IFLA_BR_PAD))
		return -EMSGSIZE;
	clockval = jiffies_to_clock_t(br->multicast_startup_query_interval);
	if (nla_put_u64_64bit(skb, IFLA_BR_MCAST_STARTUP_QUERY_INTVL, clockval,
			      IFLA_BR_PAD))
		return -EMSGSIZE;
#endif
#if IS_ENABLED(CONFIG_BRIDGE_NETFILTER)
	if (nla_put_u8(skb, IFLA_BR_NF_CALL_IPTABLES,
		       br->nf_call_iptables ? 1 : 0) ||
	    nla_put_u8(skb, IFLA_BR_NF_CALL_IP6TABLES,
		       br->nf_call_ip6tables ? 1 : 0) ||
	    nla_put_u8(skb, IFLA_BR_NF_CALL_ARPTABLES,
		       br->nf_call_arptables ? 1 : 0))
		return -EMSGSIZE;
#endif

	return 0;
}

static size_t br_get_linkxstats_size(const struct net_device *dev, int attr)
{
	struct net_bridge_port *p = NULL;
	struct net_bridge_vlan_group *vg;
	struct net_bridge_vlan *v;
	struct net_bridge *br;
	int numvls = 0;

	switch (attr) {
	case IFLA_STATS_LINK_XSTATS:
		br = netdev_priv(dev);
		vg = br_vlan_group(br);
		break;
	case IFLA_STATS_LINK_XSTATS_SLAVE:
		p = br_port_get_rtnl(dev);
		if (!p)
			return 0;
		br = p->br;
		vg = nbp_vlan_group(p);
		break;
	default:
		return 0;
	}

	if (vg) {
		/* we need to count all, even placeholder entries */
		list_for_each_entry(v, &vg->vlan_list, vlist)
			numvls++;
	}

	return numvls * nla_total_size(sizeof(struct bridge_vlan_xstats)) +
	       nla_total_size(sizeof(struct br_mcast_stats)) +
	       nla_total_size(0);
}

static int br_fill_linkxstats(struct sk_buff *skb,
			      const struct net_device *dev,
			      int *prividx, int attr)
{
	struct nlattr *nla __maybe_unused;
	struct net_bridge_port *p = NULL;
	struct net_bridge_vlan_group *vg;
	struct net_bridge_vlan *v;
	struct net_bridge *br;
	struct nlattr *nest;
	int vl_idx = 0;

	switch (attr) {
	case IFLA_STATS_LINK_XSTATS:
		br = netdev_priv(dev);
		vg = br_vlan_group(br);
		break;
	case IFLA_STATS_LINK_XSTATS_SLAVE:
		p = br_port_get_rtnl(dev);
		if (!p)
			return 0;
		br = p->br;
		vg = nbp_vlan_group(p);
		break;
	default:
		return -EINVAL;
	}

	nest = nla_nest_start(skb, LINK_XSTATS_TYPE_BRIDGE);
	if (!nest)
		return -EMSGSIZE;

	if (vg) {
		list_for_each_entry(v, &vg->vlan_list, vlist) {
			struct bridge_vlan_xstats vxi;
			struct br_vlan_stats stats;

			if (++vl_idx < *prividx)
				continue;
			memset(&vxi, 0, sizeof(vxi));
			vxi.vid = v->vid;
			br_vlan_get_stats(v, &stats);
			vxi.rx_bytes = stats.rx_bytes;
			vxi.rx_packets = stats.rx_packets;
			vxi.tx_bytes = stats.tx_bytes;
			vxi.tx_packets = stats.tx_packets;

			if (nla_put(skb, BRIDGE_XSTATS_VLAN, sizeof(vxi), &vxi))
				goto nla_put_failure;
		}
	}

#ifdef CONFIG_BRIDGE_IGMP_SNOOPING
	if (++vl_idx >= *prividx) {
		nla = nla_reserve_64bit(skb, BRIDGE_XSTATS_MCAST,
					sizeof(struct br_mcast_stats),
					BRIDGE_XSTATS_PAD);
		if (!nla)
			goto nla_put_failure;
		br_multicast_get_stats(br, p, nla_data(nla));
	}
#endif
	nla_nest_end(skb, nest);
	*prividx = 0;

	return 0;

nla_put_failure:
	nla_nest_end(skb, nest);
	*prividx = vl_idx;

	return -EMSGSIZE;
}

static struct rtnl_af_ops br_af_ops __read_mostly = {
	.family			= AF_BRIDGE,
	.get_link_af_size	= br_get_link_af_size_filtered,
};

struct rtnl_link_ops br_link_ops __read_mostly = {
	.kind			= "bridge",
	.priv_size		= sizeof(struct net_bridge),
	.setup			= br_dev_setup,
	.maxtype		= IFLA_BR_MAX,
	.policy			= br_policy,
	.validate		= br_validate,
	.newlink		= br_dev_newlink,
	.changelink		= br_changelink,
	.dellink		= br_dev_delete,
	.get_size		= br_get_size,
	.fill_info		= br_fill_info,
	.fill_linkxstats	= br_fill_linkxstats,
	.get_linkxstats_size	= br_get_linkxstats_size,

	.slave_maxtype		= IFLA_BRPORT_MAX,
	.slave_policy		= br_port_policy,
	.slave_changelink	= br_port_slave_changelink,
	.get_slave_size		= br_port_get_slave_size,
	.fill_slave_info	= br_port_fill_slave_info,
};

int __init br_netlink_init(void)
{
	int err;

	br_mdb_init();
	rtnl_af_register(&br_af_ops);

	err = rtnl_link_register(&br_link_ops);
	if (err)
		goto out_af;

	return 0;

out_af:
	rtnl_af_unregister(&br_af_ops);
	br_mdb_uninit();
	return err;
}

void br_netlink_fini(void)
{
	br_mdb_uninit();
	rtnl_af_unregister(&br_af_ops);
	rtnl_link_unregister(&br_link_ops);
}
