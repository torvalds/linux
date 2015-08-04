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
#include <net/switchdev.h>
#include <uapi/linux/if_bridge.h>

#include "br_private.h"
#include "br_private_stp.h"

static int br_get_num_vlan_infos(const struct net_port_vlans *pv,
				 u32 filter_mask)
{
	u16 vid_range_start = 0, vid_range_end = 0;
	u16 vid_range_flags = 0;
	u16 pvid, vid, flags;
	int num_vlans = 0;

	if (filter_mask & RTEXT_FILTER_BRVLAN)
		return pv->num_vlans;

	if (!(filter_mask & RTEXT_FILTER_BRVLAN_COMPRESSED))
		return 0;

	/* Count number of vlan info's
	 */
	pvid = br_get_pvid(pv);
	for_each_set_bit(vid, pv->vlan_bitmap, VLAN_N_VID) {
		flags = 0;
		if (vid == pvid)
			flags |= BRIDGE_VLAN_INFO_PVID;

		if (test_bit(vid, pv->untagged_bitmap))
			flags |= BRIDGE_VLAN_INFO_UNTAGGED;

		if (vid_range_start == 0) {
			goto initvars;
		} else if ((vid - vid_range_end) == 1 &&
			flags == vid_range_flags) {
			vid_range_end = vid;
			continue;
		} else {
			if ((vid_range_end - vid_range_start) > 0)
				num_vlans += 2;
			else
				num_vlans += 1;
		}
initvars:
		vid_range_start = vid;
		vid_range_end = vid;
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

static size_t br_get_link_af_size_filtered(const struct net_device *dev,
					   u32 filter_mask)
{
	struct net_port_vlans *pv;
	int num_vlan_infos;

	rcu_read_lock();
	if (br_port_exists(dev))
		pv = nbp_get_vlan_info(br_port_get_rcu(dev));
	else if (dev->priv_flags & IFF_EBRIDGE)
		pv = br_get_vlan_info((struct net_bridge *)netdev_priv(dev));
	else
		pv = NULL;
	if (pv)
		num_vlan_infos = br_get_num_vlan_infos(pv, filter_mask);
	else
		num_vlan_infos = 0;
	rcu_read_unlock();

	if (!num_vlan_infos)
		return 0;

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
		       !!(p->flags & BR_PROXYARP_WIFI)))
		return -EMSGSIZE;

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

		vinfo.flags &= ~BRIDGE_VLAN_INFO_RANGE_BEGIN;

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
					 const struct net_port_vlans *pv)
{
	u16 vid_range_start = 0, vid_range_end = 0;
	u16 vid_range_flags = 0;
	u16 pvid, vid, flags;
	int err = 0;

	/* Pack IFLA_BRIDGE_VLAN_INFO's for every vlan
	 * and mark vlan info with begin and end flags
	 * if vlaninfo represents a range
	 */
	pvid = br_get_pvid(pv);
	for_each_set_bit(vid, pv->vlan_bitmap, VLAN_N_VID) {
		flags = 0;
		if (vid == pvid)
			flags |= BRIDGE_VLAN_INFO_PVID;

		if (test_bit(vid, pv->untagged_bitmap))
			flags |= BRIDGE_VLAN_INFO_UNTAGGED;

		if (vid_range_start == 0) {
			goto initvars;
		} else if ((vid - vid_range_end) == 1 &&
			flags == vid_range_flags) {
			vid_range_end = vid;
			continue;
		} else {
			err = br_fill_ifvlaninfo_range(skb, vid_range_start,
						       vid_range_end,
						       vid_range_flags);
			if (err)
				return err;
		}

initvars:
		vid_range_start = vid;
		vid_range_end = vid;
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
			      const struct net_port_vlans *pv)
{
	struct bridge_vlan_info vinfo;
	u16 pvid, vid;

	pvid = br_get_pvid(pv);
	for_each_set_bit(vid, pv->vlan_bitmap, VLAN_N_VID) {
		vinfo.vid = vid;
		vinfo.flags = 0;
		if (vid == pvid)
			vinfo.flags |= BRIDGE_VLAN_INFO_PVID;

		if (test_bit(vid, pv->untagged_bitmap))
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
			  const struct net_bridge_port *port,
			  u32 pid, u32 seq, int event, unsigned int flags,
			  u32 filter_mask, const struct net_device *dev)
{
	const struct net_bridge *br;
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
		const struct net_port_vlans *pv;
		struct nlattr *af;
		int err;

		if (port)
			pv = nbp_get_vlan_info(port);
		else
			pv = br_get_vlan_info(br);

		if (!pv || bitmap_empty(pv->vlan_bitmap, VLAN_N_VID))
			goto done;

		af = nla_nest_start(skb, IFLA_AF_SPEC);
		if (!af)
			goto nla_put_failure;

		if (filter_mask & RTEXT_FILTER_BRVLAN_COMPRESSED)
			err = br_fill_ifvlaninfo_compressed(skb, pv);
		else
			err = br_fill_ifvlaninfo(skb, pv);
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
			err = nbp_vlan_add(p, vinfo->vid, vinfo->flags);
			if (err)
				break;

			if (vinfo->flags & BRIDGE_VLAN_INFO_MASTER)
				err = br_vlan_add(p->br, vinfo->vid,
						  vinfo->flags);
		} else {
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
		if (vinfo->flags & BRIDGE_VLAN_INFO_RANGE_BEGIN) {
			if (vinfo_start)
				return -EINVAL;
			vinfo_start = vinfo;
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
	br_log_state(p);
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
	int err = 0, ret_offload = 0;

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

	if (p && !(flags & BRIDGE_FLAGS_SELF)) {
		/* set bridge attributes in hardware if supported
		 */
		ret_offload = netdev_switch_port_bridge_setlink(dev, nlh,
								flags);
		if (ret_offload && ret_offload != -EOPNOTSUPP)
			br_warn(p->br, "error setting attrs on port %u(%s)\n",
				(unsigned int)p->port_no, p->dev->name);
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
	int err = 0, ret_offload = 0;

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

	if (p && !(flags & BRIDGE_FLAGS_SELF)) {
		/* del bridge attributes in hardware
		 */
		ret_offload = netdev_switch_port_bridge_dellink(dev, nlh,
								flags);
		if (ret_offload && ret_offload != -EOPNOTSUPP)
			br_warn(p->br, "error deleting attrs on port %u (%s)\n",
				(unsigned int)p->port_no, p->dev->name);
	}

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
		u32 ageing_time = nla_get_u32(data[IFLA_BR_AGEING_TIME]);

		br->ageing_time = clock_t_to_jiffies(ageing_time);
	}

	if (data[IFLA_BR_STP_STATE]) {
		u32 stp_enabled = nla_get_u32(data[IFLA_BR_STP_STATE]);

		br_stp_set_enabled(br, stp_enabled);
	}

	if (data[IFLA_BR_PRIORITY]) {
		u32 priority = nla_get_u16(data[IFLA_BR_PRIORITY]);

		br_stp_set_bridge_priority(br, priority);
	}

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

	if (nla_put_u32(skb, IFLA_BR_FORWARD_DELAY, forward_delay) ||
	    nla_put_u32(skb, IFLA_BR_HELLO_TIME, hello_time) ||
	    nla_put_u32(skb, IFLA_BR_MAX_AGE, age_time) ||
	    nla_put_u32(skb, IFLA_BR_AGEING_TIME, ageing_time) ||
	    nla_put_u32(skb, IFLA_BR_STP_STATE, stp_enabled) ||
	    nla_put_u16(skb, IFLA_BR_PRIORITY, priority))
		return -EMSGSIZE;

	return 0;
}

static size_t br_get_link_af_size(const struct net_device *dev)
{
	struct net_port_vlans *pv;

	if (br_port_exists(dev))
		pv = nbp_get_vlan_info(br_port_get_rtnl(dev));
	else if (dev->priv_flags & IFF_EBRIDGE)
		pv = br_get_vlan_info((struct net_bridge *)netdev_priv(dev));
	else
		return 0;

	if (!pv)
		return 0;

	/* Each VLAN is returned in bridge_vlan_info along with flags */
	return pv->num_vlans * nla_total_size(sizeof(struct bridge_vlan_info));
}

static struct rtnl_af_ops br_af_ops __read_mostly = {
	.family			= AF_BRIDGE,
	.get_link_af_size	= br_get_link_af_size,
};

struct rtnl_link_ops br_link_ops __read_mostly = {
	.kind			= "bridge",
	.priv_size		= sizeof(struct net_bridge),
	.setup			= br_dev_setup,
	.maxtype		= IFLA_BRPORT_MAX,
	.policy			= br_policy,
	.validate		= br_validate,
	.newlink		= br_dev_newlink,
	.changelink		= br_changelink,
	.dellink		= br_dev_delete,
	.get_size		= br_get_size,
	.fill_info		= br_fill_info,

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
