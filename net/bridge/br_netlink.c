// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *	Bridge netlink control interface
 *
 *	Authors:
 *	Stephen Hemminger		<shemminger@osdl.org>
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
#include "br_private_cfm.h"
#include "br_private_tunnel.h"
#include "br_private_mcast_eht.h"

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
	struct net_bridge_port *p = NULL;
	struct net_bridge *br = NULL;
	u32 num_cfm_peer_mep_infos;
	u32 num_cfm_mep_infos;
	size_t vinfo_sz = 0;
	int num_vlan_infos;

	rcu_read_lock();
	if (netif_is_bridge_port(dev)) {
		p = br_port_get_check_rcu(dev);
		if (p)
			vg = nbp_vlan_group_rcu(p);
	} else if (dev->priv_flags & IFF_EBRIDGE) {
		br = netdev_priv(dev);
		vg = br_vlan_group_rcu(br);
	}
	num_vlan_infos = br_get_num_vlan_infos(vg, filter_mask);
	rcu_read_unlock();

	if (p && (p->flags & BR_VLAN_TUNNEL))
		vinfo_sz += br_get_vlan_tunnel_info_size(vg);

	/* Each VLAN is returned in bridge_vlan_info along with flags */
	vinfo_sz += num_vlan_infos * nla_total_size(sizeof(struct bridge_vlan_info));

	if (!(filter_mask & RTEXT_FILTER_CFM_STATUS))
		return vinfo_sz;

	if (!br)
		return vinfo_sz;

	/* CFM status info must be added */
	br_cfm_mep_count(br, &num_cfm_mep_infos);
	br_cfm_peer_mep_count(br, &num_cfm_peer_mep_infos);

	vinfo_sz += nla_total_size(0);	/* IFLA_BRIDGE_CFM */
	/* For each status struct the MEP instance (u32) is added */
	/* MEP instance (u32) + br_cfm_mep_status */
	vinfo_sz += num_cfm_mep_infos *
		     /*IFLA_BRIDGE_CFM_MEP_STATUS_INSTANCE */
		    (nla_total_size(sizeof(u32))
		     /* IFLA_BRIDGE_CFM_MEP_STATUS_OPCODE_UNEXP_SEEN */
		     + nla_total_size(sizeof(u32))
		     /* IFLA_BRIDGE_CFM_MEP_STATUS_VERSION_UNEXP_SEEN */
		     + nla_total_size(sizeof(u32))
		     /* IFLA_BRIDGE_CFM_MEP_STATUS_RX_LEVEL_LOW_SEEN */
		     + nla_total_size(sizeof(u32)));
	/* MEP instance (u32) + br_cfm_cc_peer_status */
	vinfo_sz += num_cfm_peer_mep_infos *
		     /* IFLA_BRIDGE_CFM_CC_PEER_STATUS_INSTANCE */
		    (nla_total_size(sizeof(u32))
		     /* IFLA_BRIDGE_CFM_CC_PEER_STATUS_PEER_MEPID */
		     + nla_total_size(sizeof(u32))
		     /* IFLA_BRIDGE_CFM_CC_PEER_STATUS_CCM_DEFECT */
		     + nla_total_size(sizeof(u32))
		     /* IFLA_BRIDGE_CFM_CC_PEER_STATUS_RDI */
		     + nla_total_size(sizeof(u32))
		     /* IFLA_BRIDGE_CFM_CC_PEER_STATUS_PORT_TLV_VALUE */
		     + nla_total_size(sizeof(u8))
		     /* IFLA_BRIDGE_CFM_CC_PEER_STATUS_IF_TLV_VALUE */
		     + nla_total_size(sizeof(u8))
		     /* IFLA_BRIDGE_CFM_CC_PEER_STATUS_SEEN */
		     + nla_total_size(sizeof(u32))
		     /* IFLA_BRIDGE_CFM_CC_PEER_STATUS_TLV_SEEN */
		     + nla_total_size(sizeof(u32))
		     /* IFLA_BRIDGE_CFM_CC_PEER_STATUS_SEQ_UNEXP_SEEN */
		     + nla_total_size(sizeof(u32)));

	return vinfo_sz;
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
		+ nla_total_size(1)	/* IFLA_BRPORT_MCAST_TO_UCAST */
		+ nla_total_size(1)	/* IFLA_BRPORT_LEARNING */
		+ nla_total_size(1)	/* IFLA_BRPORT_UNICAST_FLOOD */
		+ nla_total_size(1)	/* IFLA_BRPORT_MCAST_FLOOD */
		+ nla_total_size(1)	/* IFLA_BRPORT_BCAST_FLOOD */
		+ nla_total_size(1)	/* IFLA_BRPORT_PROXYARP */
		+ nla_total_size(1)	/* IFLA_BRPORT_PROXYARP_WIFI */
		+ nla_total_size(1)	/* IFLA_BRPORT_VLAN_TUNNEL */
		+ nla_total_size(1)	/* IFLA_BRPORT_NEIGH_SUPPRESS */
		+ nla_total_size(1)	/* IFLA_BRPORT_ISOLATED */
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
		+ nla_total_size(sizeof(u16))	/* IFLA_BRPORT_GROUP_FWD_MASK */
		+ nla_total_size(sizeof(u8))	/* IFLA_BRPORT_MRP_RING_OPEN */
		+ nla_total_size(sizeof(u8))	/* IFLA_BRPORT_MRP_IN_OPEN */
		+ nla_total_size(sizeof(u32))	/* IFLA_BRPORT_MCAST_EHT_HOSTS_LIMIT */
		+ nla_total_size(sizeof(u32))	/* IFLA_BRPORT_MCAST_EHT_HOSTS_CNT */
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
				 filter_mask)) /* IFLA_AF_SPEC */
		+ nla_total_size(4); /* IFLA_BRPORT_BACKUP_PORT */
}

static int br_port_fill_attrs(struct sk_buff *skb,
			      const struct net_bridge_port *p)
{
	u8 mode = !!(p->flags & BR_HAIRPIN_MODE);
	struct net_bridge_port *backup_p;
	u64 timerval;

	if (nla_put_u8(skb, IFLA_BRPORT_STATE, p->state) ||
	    nla_put_u16(skb, IFLA_BRPORT_PRIORITY, p->priority) ||
	    nla_put_u32(skb, IFLA_BRPORT_COST, p->path_cost) ||
	    nla_put_u8(skb, IFLA_BRPORT_MODE, mode) ||
	    nla_put_u8(skb, IFLA_BRPORT_GUARD, !!(p->flags & BR_BPDU_GUARD)) ||
	    nla_put_u8(skb, IFLA_BRPORT_PROTECT,
		       !!(p->flags & BR_ROOT_BLOCK)) ||
	    nla_put_u8(skb, IFLA_BRPORT_FAST_LEAVE,
		       !!(p->flags & BR_MULTICAST_FAST_LEAVE)) ||
	    nla_put_u8(skb, IFLA_BRPORT_MCAST_TO_UCAST,
		       !!(p->flags & BR_MULTICAST_TO_UNICAST)) ||
	    nla_put_u8(skb, IFLA_BRPORT_LEARNING, !!(p->flags & BR_LEARNING)) ||
	    nla_put_u8(skb, IFLA_BRPORT_UNICAST_FLOOD,
		       !!(p->flags & BR_FLOOD)) ||
	    nla_put_u8(skb, IFLA_BRPORT_MCAST_FLOOD,
		       !!(p->flags & BR_MCAST_FLOOD)) ||
	    nla_put_u8(skb, IFLA_BRPORT_BCAST_FLOOD,
		       !!(p->flags & BR_BCAST_FLOOD)) ||
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
	    nla_put_u8(skb, IFLA_BRPORT_CONFIG_PENDING, p->config_pending) ||
	    nla_put_u8(skb, IFLA_BRPORT_VLAN_TUNNEL, !!(p->flags &
							BR_VLAN_TUNNEL)) ||
	    nla_put_u16(skb, IFLA_BRPORT_GROUP_FWD_MASK, p->group_fwd_mask) ||
	    nla_put_u8(skb, IFLA_BRPORT_NEIGH_SUPPRESS,
		       !!(p->flags & BR_NEIGH_SUPPRESS)) ||
	    nla_put_u8(skb, IFLA_BRPORT_MRP_RING_OPEN, !!(p->flags &
							  BR_MRP_LOST_CONT)) ||
	    nla_put_u8(skb, IFLA_BRPORT_MRP_IN_OPEN,
		       !!(p->flags & BR_MRP_LOST_IN_CONT)) ||
	    nla_put_u8(skb, IFLA_BRPORT_ISOLATED, !!(p->flags & BR_ISOLATED)))
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
		       p->multicast_ctx.multicast_router) ||
	    nla_put_u32(skb, IFLA_BRPORT_MCAST_EHT_HOSTS_LIMIT,
			p->multicast_eht_hosts_limit) ||
	    nla_put_u32(skb, IFLA_BRPORT_MCAST_EHT_HOSTS_CNT,
			p->multicast_eht_hosts_cnt))
		return -EMSGSIZE;
#endif

	/* we might be called only with br->lock */
	rcu_read_lock();
	backup_p = rcu_dereference(p->backup_port);
	if (backup_p)
		nla_put_u32(skb, IFLA_BRPORT_BACKUP_PORT,
			    backup_p->dev->ifindex);
	rcu_read_unlock();

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
			  const struct net_bridge_port *port,
			  u32 pid, u32 seq, int event, unsigned int flags,
			  u32 filter_mask, const struct net_device *dev,
			  bool getlink)
{
	u8 operstate = netif_running(dev) ? dev->operstate : IF_OPER_DOWN;
	struct nlattr *af = NULL;
	struct net_bridge *br;
	struct ifinfomsg *hdr;
	struct nlmsghdr *nlh;

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
		struct nlattr *nest;

		nest = nla_nest_start(skb, IFLA_PROTINFO);
		if (nest == NULL || br_port_fill_attrs(skb, port) < 0)
			goto nla_put_failure;
		nla_nest_end(skb, nest);
	}

	if (filter_mask & (RTEXT_FILTER_BRVLAN |
			   RTEXT_FILTER_BRVLAN_COMPRESSED |
			   RTEXT_FILTER_MRP |
			   RTEXT_FILTER_CFM_CONFIG |
			   RTEXT_FILTER_CFM_STATUS)) {
		af = nla_nest_start_noflag(skb, IFLA_AF_SPEC);
		if (!af)
			goto nla_put_failure;
	}

	/* Check if  the VID information is requested */
	if ((filter_mask & RTEXT_FILTER_BRVLAN) ||
	    (filter_mask & RTEXT_FILTER_BRVLAN_COMPRESSED)) {
		struct net_bridge_vlan_group *vg;
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
		if (filter_mask & RTEXT_FILTER_BRVLAN_COMPRESSED)
			err = br_fill_ifvlaninfo_compressed(skb, vg);
		else
			err = br_fill_ifvlaninfo(skb, vg);

		if (port && (port->flags & BR_VLAN_TUNNEL))
			err = br_fill_vlan_tunnel_info(skb, vg);
		rcu_read_unlock();
		if (err)
			goto nla_put_failure;
	}

	if (filter_mask & RTEXT_FILTER_MRP) {
		int err;

		if (!br_mrp_enabled(br) || port)
			goto done;

		rcu_read_lock();
		err = br_mrp_fill_info(skb, br);
		rcu_read_unlock();

		if (err)
			goto nla_put_failure;
	}

	if (filter_mask & (RTEXT_FILTER_CFM_CONFIG | RTEXT_FILTER_CFM_STATUS)) {
		struct nlattr *cfm_nest = NULL;
		int err;

		if (!br_cfm_created(br) || port)
			goto done;

		cfm_nest = nla_nest_start(skb, IFLA_BRIDGE_CFM);
		if (!cfm_nest)
			goto nla_put_failure;

		if (filter_mask & RTEXT_FILTER_CFM_CONFIG) {
			rcu_read_lock();
			err = br_cfm_config_fill_info(skb, br);
			rcu_read_unlock();
			if (err)
				goto nla_put_failure;
		}

		if (filter_mask & RTEXT_FILTER_CFM_STATUS) {
			rcu_read_lock();
			err = br_cfm_status_fill_info(skb, br, getlink);
			rcu_read_unlock();
			if (err)
				goto nla_put_failure;
		}

		nla_nest_end(skb, cfm_nest);
	}

done:
	if (af)
		nla_nest_end(skb, af);
	nlmsg_end(skb, nlh);
	return 0;

nla_put_failure:
	nlmsg_cancel(skb, nlh);
	return -EMSGSIZE;
}

void br_info_notify(int event, const struct net_bridge *br,
		    const struct net_bridge_port *port, u32 filter)
{
	struct net_device *dev;
	struct sk_buff *skb;
	int err = -ENOBUFS;
	struct net *net;
	u16 port_no = 0;

	if (WARN_ON(!port && !br))
		return;

	if (port) {
		dev = port->dev;
		br = port->br;
		port_no = port->port_no;
	} else {
		dev = br->dev;
	}

	net = dev_net(dev);
	br_debug(br, "port %u(%s) event %d\n", port_no, dev->name, event);

	skb = nlmsg_new(br_nlmsg_size(dev, filter), GFP_ATOMIC);
	if (skb == NULL)
		goto errout;

	err = br_fill_ifinfo(skb, port, 0, 0, event, 0, filter, dev, false);
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

/* Notify listeners of a change in bridge or port information */
void br_ifinfo_notify(int event, const struct net_bridge *br,
		      const struct net_bridge_port *port)
{
	u32 filter = RTEXT_FILTER_BRVLAN_COMPRESSED;

	return br_info_notify(event, br, port, filter);
}

/*
 * Dump information about all ports, in response to GETLINK
 */
int br_getlink(struct sk_buff *skb, u32 pid, u32 seq,
	       struct net_device *dev, u32 filter_mask, int nlflags)
{
	struct net_bridge_port *port = br_port_get_rtnl(dev);

	if (!port && !(filter_mask & RTEXT_FILTER_BRVLAN) &&
	    !(filter_mask & RTEXT_FILTER_BRVLAN_COMPRESSED) &&
	    !(filter_mask & RTEXT_FILTER_MRP) &&
	    !(filter_mask & RTEXT_FILTER_CFM_CONFIG) &&
	    !(filter_mask & RTEXT_FILTER_CFM_STATUS))
		return 0;

	return br_fill_ifinfo(skb, port, pid, seq, RTM_NEWLINK, nlflags,
			      filter_mask, dev, true);
}

static int br_vlan_info(struct net_bridge *br, struct net_bridge_port *p,
			int cmd, struct bridge_vlan_info *vinfo, bool *changed,
			struct netlink_ext_ack *extack)
{
	bool curr_change;
	int err = 0;

	switch (cmd) {
	case RTM_SETLINK:
		if (p) {
			/* if the MASTER flag is set this will act on the global
			 * per-VLAN entry as well
			 */
			err = nbp_vlan_add(p, vinfo->vid, vinfo->flags,
					   &curr_change, extack);
		} else {
			vinfo->flags |= BRIDGE_VLAN_INFO_BRENTRY;
			err = br_vlan_add(br, vinfo->vid, vinfo->flags,
					  &curr_change, extack);
		}
		if (curr_change)
			*changed = true;
		break;

	case RTM_DELLINK:
		if (p) {
			if (!nbp_vlan_delete(p, vinfo->vid))
				*changed = true;

			if ((vinfo->flags & BRIDGE_VLAN_INFO_MASTER) &&
			    !br_vlan_delete(p->br, vinfo->vid))
				*changed = true;
		} else if (!br_vlan_delete(br, vinfo->vid)) {
			*changed = true;
		}
		break;
	}

	return err;
}

int br_process_vlan_info(struct net_bridge *br,
			 struct net_bridge_port *p, int cmd,
			 struct bridge_vlan_info *vinfo_curr,
			 struct bridge_vlan_info **vinfo_last,
			 bool *changed,
			 struct netlink_ext_ack *extack)
{
	int err, rtm_cmd;

	if (!br_vlan_valid_id(vinfo_curr->vid, extack))
		return -EINVAL;

	/* needed for vlan-only NEWVLAN/DELVLAN notifications */
	rtm_cmd = br_afspec_cmd_to_rtm(cmd);

	if (vinfo_curr->flags & BRIDGE_VLAN_INFO_RANGE_BEGIN) {
		if (!br_vlan_valid_range(vinfo_curr, *vinfo_last, extack))
			return -EINVAL;
		*vinfo_last = vinfo_curr;
		return 0;
	}

	if (*vinfo_last) {
		struct bridge_vlan_info tmp_vinfo;
		int v, v_change_start = 0;

		if (!br_vlan_valid_range(vinfo_curr, *vinfo_last, extack))
			return -EINVAL;

		memcpy(&tmp_vinfo, *vinfo_last,
		       sizeof(struct bridge_vlan_info));
		for (v = (*vinfo_last)->vid; v <= vinfo_curr->vid; v++) {
			bool curr_change = false;

			tmp_vinfo.vid = v;
			err = br_vlan_info(br, p, cmd, &tmp_vinfo, &curr_change,
					   extack);
			if (err)
				break;
			if (curr_change) {
				*changed = curr_change;
				if (!v_change_start)
					v_change_start = v;
			} else {
				/* nothing to notify yet */
				if (!v_change_start)
					continue;
				br_vlan_notify(br, p, v_change_start,
					       v - 1, rtm_cmd);
				v_change_start = 0;
			}
			cond_resched();
		}
		/* v_change_start is set only if the last/whole range changed */
		if (v_change_start)
			br_vlan_notify(br, p, v_change_start,
				       v - 1, rtm_cmd);

		*vinfo_last = NULL;

		return err;
	}

	err = br_vlan_info(br, p, cmd, vinfo_curr, changed, extack);
	if (*changed)
		br_vlan_notify(br, p, vinfo_curr->vid, 0, rtm_cmd);

	return err;
}

static int br_afspec(struct net_bridge *br,
		     struct net_bridge_port *p,
		     struct nlattr *af_spec,
		     int cmd, bool *changed,
		     struct netlink_ext_ack *extack)
{
	struct bridge_vlan_info *vinfo_curr = NULL;
	struct bridge_vlan_info *vinfo_last = NULL;
	struct nlattr *attr;
	struct vtunnel_info tinfo_last = {};
	struct vtunnel_info tinfo_curr = {};
	int err = 0, rem;

	nla_for_each_nested(attr, af_spec, rem) {
		err = 0;
		switch (nla_type(attr)) {
		case IFLA_BRIDGE_VLAN_TUNNEL_INFO:
			if (!p || !(p->flags & BR_VLAN_TUNNEL))
				return -EINVAL;
			err = br_parse_vlan_tunnel_info(attr, &tinfo_curr);
			if (err)
				return err;
			err = br_process_vlan_tunnel_info(br, p, cmd,
							  &tinfo_curr,
							  &tinfo_last,
							  changed);
			if (err)
				return err;
			break;
		case IFLA_BRIDGE_VLAN_INFO:
			if (nla_len(attr) != sizeof(struct bridge_vlan_info))
				return -EINVAL;
			vinfo_curr = nla_data(attr);
			err = br_process_vlan_info(br, p, cmd, vinfo_curr,
						   &vinfo_last, changed,
						   extack);
			if (err)
				return err;
			break;
		case IFLA_BRIDGE_MRP:
			err = br_mrp_parse(br, p, attr, cmd, extack);
			if (err)
				return err;
			break;
		case IFLA_BRIDGE_CFM:
			err = br_cfm_parse(br, p, attr, cmd, extack);
			if (err)
				return err;
			break;
		}
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
	[IFLA_BRPORT_MCAST_TO_UCAST] = { .type = NLA_U8 },
	[IFLA_BRPORT_MCAST_FLOOD] = { .type = NLA_U8 },
	[IFLA_BRPORT_BCAST_FLOOD] = { .type = NLA_U8 },
	[IFLA_BRPORT_VLAN_TUNNEL] = { .type = NLA_U8 },
	[IFLA_BRPORT_GROUP_FWD_MASK] = { .type = NLA_U16 },
	[IFLA_BRPORT_NEIGH_SUPPRESS] = { .type = NLA_U8 },
	[IFLA_BRPORT_ISOLATED]	= { .type = NLA_U8 },
	[IFLA_BRPORT_BACKUP_PORT] = { .type = NLA_U32 },
	[IFLA_BRPORT_MCAST_EHT_HOSTS_LIMIT] = { .type = NLA_U32 },
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
	if (!tb[attrtype])
		return;

	if (nla_get_u8(tb[attrtype]))
		p->flags |= mask;
	else
		p->flags &= ~mask;
}

/* Process bridge protocol info on port */
static int br_setport(struct net_bridge_port *p, struct nlattr *tb[],
		      struct netlink_ext_ack *extack)
{
	unsigned long old_flags, changed_mask;
	bool br_vlan_tunnel_old;
	int err;

	old_flags = p->flags;
	br_vlan_tunnel_old = (old_flags & BR_VLAN_TUNNEL) ? true : false;

	br_set_port_flag(p, tb, IFLA_BRPORT_MODE, BR_HAIRPIN_MODE);
	br_set_port_flag(p, tb, IFLA_BRPORT_GUARD, BR_BPDU_GUARD);
	br_set_port_flag(p, tb, IFLA_BRPORT_FAST_LEAVE,
			 BR_MULTICAST_FAST_LEAVE);
	br_set_port_flag(p, tb, IFLA_BRPORT_PROTECT, BR_ROOT_BLOCK);
	br_set_port_flag(p, tb, IFLA_BRPORT_LEARNING, BR_LEARNING);
	br_set_port_flag(p, tb, IFLA_BRPORT_UNICAST_FLOOD, BR_FLOOD);
	br_set_port_flag(p, tb, IFLA_BRPORT_MCAST_FLOOD, BR_MCAST_FLOOD);
	br_set_port_flag(p, tb, IFLA_BRPORT_MCAST_TO_UCAST,
			 BR_MULTICAST_TO_UNICAST);
	br_set_port_flag(p, tb, IFLA_BRPORT_BCAST_FLOOD, BR_BCAST_FLOOD);
	br_set_port_flag(p, tb, IFLA_BRPORT_PROXYARP, BR_PROXYARP);
	br_set_port_flag(p, tb, IFLA_BRPORT_PROXYARP_WIFI, BR_PROXYARP_WIFI);
	br_set_port_flag(p, tb, IFLA_BRPORT_VLAN_TUNNEL, BR_VLAN_TUNNEL);
	br_set_port_flag(p, tb, IFLA_BRPORT_NEIGH_SUPPRESS, BR_NEIGH_SUPPRESS);
	br_set_port_flag(p, tb, IFLA_BRPORT_ISOLATED, BR_ISOLATED);

	changed_mask = old_flags ^ p->flags;

	err = br_switchdev_set_port_flag(p, p->flags, changed_mask, extack);
	if (err) {
		p->flags = old_flags;
		return err;
	}

	if (br_vlan_tunnel_old && !(p->flags & BR_VLAN_TUNNEL))
		nbp_vlan_tunnel_info_flush(p);

	br_port_flags_change(p, changed_mask);

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

		err = br_multicast_set_port_router(&p->multicast_ctx,
						   mcast_router);
		if (err)
			return err;
	}

	if (tb[IFLA_BRPORT_MCAST_EHT_HOSTS_LIMIT]) {
		u32 hlimit;

		hlimit = nla_get_u32(tb[IFLA_BRPORT_MCAST_EHT_HOSTS_LIMIT]);
		err = br_multicast_eht_set_hosts_limit(p, hlimit);
		if (err)
			return err;
	}
#endif

	if (tb[IFLA_BRPORT_GROUP_FWD_MASK]) {
		u16 fwd_mask = nla_get_u16(tb[IFLA_BRPORT_GROUP_FWD_MASK]);

		if (fwd_mask & BR_GROUPFWD_MACPAUSE)
			return -EINVAL;
		p->group_fwd_mask = fwd_mask;
	}

	if (tb[IFLA_BRPORT_BACKUP_PORT]) {
		struct net_device *backup_dev = NULL;
		u32 backup_ifindex;

		backup_ifindex = nla_get_u32(tb[IFLA_BRPORT_BACKUP_PORT]);
		if (backup_ifindex) {
			backup_dev = __dev_get_by_index(dev_net(p->dev),
							backup_ifindex);
			if (!backup_dev)
				return -ENOENT;
		}

		err = nbp_backup_change(p, backup_dev);
		if (err)
			return err;
	}

	return 0;
}

/* Change state and parameters on port. */
int br_setlink(struct net_device *dev, struct nlmsghdr *nlh, u16 flags,
	       struct netlink_ext_ack *extack)
{
	struct net_bridge *br = (struct net_bridge *)netdev_priv(dev);
	struct nlattr *tb[IFLA_BRPORT_MAX + 1];
	struct net_bridge_port *p;
	struct nlattr *protinfo;
	struct nlattr *afspec;
	bool changed = false;
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
			err = nla_parse_nested_deprecated(tb, IFLA_BRPORT_MAX,
							  protinfo,
							  br_port_policy,
							  NULL);
			if (err)
				return err;

			spin_lock_bh(&p->br->lock);
			err = br_setport(p, tb, extack);
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
		changed = true;
	}

	if (afspec)
		err = br_afspec(br, p, afspec, RTM_SETLINK, &changed, extack);

	if (changed)
		br_ifinfo_notify(RTM_NEWLINK, br, p);
out:
	return err;
}

/* Delete port information */
int br_dellink(struct net_device *dev, struct nlmsghdr *nlh, u16 flags)
{
	struct net_bridge *br = (struct net_bridge *)netdev_priv(dev);
	struct net_bridge_port *p;
	struct nlattr *afspec;
	bool changed = false;
	int err = 0;

	afspec = nlmsg_find_attr(nlh, sizeof(struct ifinfomsg), IFLA_AF_SPEC);
	if (!afspec)
		return 0;

	p = br_port_get_rtnl(dev);
	/* We want to accept dev as bridge itself as well */
	if (!p && !(dev->priv_flags & IFF_EBRIDGE))
		return -EINVAL;

	err = br_afspec(br, p, afspec, RTM_DELLINK, &changed, NULL);
	if (changed)
		/* Send RTM_NEWLINK because userspace
		 * expects RTM_NEWLINK for vlan dels
		 */
		br_ifinfo_notify(RTM_NEWLINK, br, p);

	return err;
}

static int br_validate(struct nlattr *tb[], struct nlattr *data[],
		       struct netlink_ext_ack *extack)
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
	if (data[IFLA_BR_VLAN_PROTOCOL] &&
	    !eth_type_vlan(nla_get_be16(data[IFLA_BR_VLAN_PROTOCOL])))
		return -EPROTONOSUPPORT;

	if (data[IFLA_BR_VLAN_DEFAULT_PVID]) {
		__u16 defpvid = nla_get_u16(data[IFLA_BR_VLAN_DEFAULT_PVID]);

		if (defpvid >= VLAN_VID_MASK)
			return -EINVAL;
	}
#endif

	return 0;
}

static int br_port_slave_changelink(struct net_device *brdev,
				    struct net_device *dev,
				    struct nlattr *tb[],
				    struct nlattr *data[],
				    struct netlink_ext_ack *extack)
{
	struct net_bridge *br = netdev_priv(brdev);
	int ret;

	if (!data)
		return 0;

	spin_lock_bh(&br->lock);
	ret = br_setport(br_port_get_rtnl(dev), data, extack);
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
	[IFLA_BR_MCAST_IGMP_VERSION] = { .type = NLA_U8 },
	[IFLA_BR_MCAST_MLD_VERSION] = { .type = NLA_U8 },
	[IFLA_BR_VLAN_STATS_PER_PORT] = { .type = NLA_U8 },
	[IFLA_BR_MULTI_BOOLOPT] =
		NLA_POLICY_EXACT_LEN(sizeof(struct br_boolopt_multi)),
};

static int br_changelink(struct net_device *brdev, struct nlattr *tb[],
			 struct nlattr *data[],
			 struct netlink_ext_ack *extack)
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

		err = br_stp_set_enabled(br, stp_enabled, extack);
		if (err)
			return err;
	}

	if (data[IFLA_BR_PRIORITY]) {
		u32 priority = nla_get_u16(data[IFLA_BR_PRIORITY]);

		br_stp_set_bridge_priority(br, priority);
	}

	if (data[IFLA_BR_VLAN_FILTERING]) {
		u8 vlan_filter = nla_get_u8(data[IFLA_BR_VLAN_FILTERING]);

		err = br_vlan_filter_toggle(br, vlan_filter, extack);
		if (err)
			return err;
	}

#ifdef CONFIG_BRIDGE_VLAN_FILTERING
	if (data[IFLA_BR_VLAN_PROTOCOL]) {
		__be16 vlan_proto = nla_get_be16(data[IFLA_BR_VLAN_PROTOCOL]);

		err = __br_vlan_set_proto(br, vlan_proto, extack);
		if (err)
			return err;
	}

	if (data[IFLA_BR_VLAN_DEFAULT_PVID]) {
		__u16 defpvid = nla_get_u16(data[IFLA_BR_VLAN_DEFAULT_PVID]);

		err = __br_vlan_set_default_pvid(br, defpvid, extack);
		if (err)
			return err;
	}

	if (data[IFLA_BR_VLAN_STATS_ENABLED]) {
		__u8 vlan_stats = nla_get_u8(data[IFLA_BR_VLAN_STATS_ENABLED]);

		err = br_vlan_set_stats(br, vlan_stats);
		if (err)
			return err;
	}

	if (data[IFLA_BR_VLAN_STATS_PER_PORT]) {
		__u8 per_port = nla_get_u8(data[IFLA_BR_VLAN_STATS_PER_PORT]);

		err = br_vlan_set_stats_per_port(br, per_port);
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
		br_opt_toggle(br, BROPT_GROUP_ADDR_SET, true);
		br_recalculate_fwd_mask(br);
	}

	if (data[IFLA_BR_FDB_FLUSH])
		br_fdb_flush(br);

#ifdef CONFIG_BRIDGE_IGMP_SNOOPING
	if (data[IFLA_BR_MCAST_ROUTER]) {
		u8 multicast_router = nla_get_u8(data[IFLA_BR_MCAST_ROUTER]);

		err = br_multicast_set_router(&br->multicast_ctx,
					      multicast_router);
		if (err)
			return err;
	}

	if (data[IFLA_BR_MCAST_SNOOPING]) {
		u8 mcast_snooping = nla_get_u8(data[IFLA_BR_MCAST_SNOOPING]);

		err = br_multicast_toggle(br, mcast_snooping, extack);
		if (err)
			return err;
	}

	if (data[IFLA_BR_MCAST_QUERY_USE_IFADDR]) {
		u8 val;

		val = nla_get_u8(data[IFLA_BR_MCAST_QUERY_USE_IFADDR]);
		br_opt_toggle(br, BROPT_MULTICAST_QUERY_USE_IFADDR, !!val);
	}

	if (data[IFLA_BR_MCAST_QUERIER]) {
		u8 mcast_querier = nla_get_u8(data[IFLA_BR_MCAST_QUERIER]);

		err = br_multicast_set_querier(&br->multicast_ctx,
					       mcast_querier);
		if (err)
			return err;
	}

	if (data[IFLA_BR_MCAST_HASH_ELASTICITY])
		br_warn(br, "the hash_elasticity option has been deprecated and is always %u\n",
			RHT_ELASTICITY);

	if (data[IFLA_BR_MCAST_HASH_MAX])
		br->hash_max = nla_get_u32(data[IFLA_BR_MCAST_HASH_MAX]);

	if (data[IFLA_BR_MCAST_LAST_MEMBER_CNT]) {
		u32 val = nla_get_u32(data[IFLA_BR_MCAST_LAST_MEMBER_CNT]);

		br->multicast_ctx.multicast_last_member_count = val;
	}

	if (data[IFLA_BR_MCAST_STARTUP_QUERY_CNT]) {
		u32 val = nla_get_u32(data[IFLA_BR_MCAST_STARTUP_QUERY_CNT]);

		br->multicast_ctx.multicast_startup_query_count = val;
	}

	if (data[IFLA_BR_MCAST_LAST_MEMBER_INTVL]) {
		u64 val = nla_get_u64(data[IFLA_BR_MCAST_LAST_MEMBER_INTVL]);

		br->multicast_ctx.multicast_last_member_interval = clock_t_to_jiffies(val);
	}

	if (data[IFLA_BR_MCAST_MEMBERSHIP_INTVL]) {
		u64 val = nla_get_u64(data[IFLA_BR_MCAST_MEMBERSHIP_INTVL]);

		br->multicast_ctx.multicast_membership_interval = clock_t_to_jiffies(val);
	}

	if (data[IFLA_BR_MCAST_QUERIER_INTVL]) {
		u64 val = nla_get_u64(data[IFLA_BR_MCAST_QUERIER_INTVL]);

		br->multicast_ctx.multicast_querier_interval = clock_t_to_jiffies(val);
	}

	if (data[IFLA_BR_MCAST_QUERY_INTVL]) {
		u64 val = nla_get_u64(data[IFLA_BR_MCAST_QUERY_INTVL]);

		br->multicast_ctx.multicast_query_interval = clock_t_to_jiffies(val);
	}

	if (data[IFLA_BR_MCAST_QUERY_RESPONSE_INTVL]) {
		u64 val = nla_get_u64(data[IFLA_BR_MCAST_QUERY_RESPONSE_INTVL]);

		br->multicast_ctx.multicast_query_response_interval = clock_t_to_jiffies(val);
	}

	if (data[IFLA_BR_MCAST_STARTUP_QUERY_INTVL]) {
		u64 val = nla_get_u64(data[IFLA_BR_MCAST_STARTUP_QUERY_INTVL]);

		br->multicast_ctx.multicast_startup_query_interval = clock_t_to_jiffies(val);
	}

	if (data[IFLA_BR_MCAST_STATS_ENABLED]) {
		__u8 mcast_stats;

		mcast_stats = nla_get_u8(data[IFLA_BR_MCAST_STATS_ENABLED]);
		br_opt_toggle(br, BROPT_MULTICAST_STATS_ENABLED, !!mcast_stats);
	}

	if (data[IFLA_BR_MCAST_IGMP_VERSION]) {
		__u8 igmp_version;

		igmp_version = nla_get_u8(data[IFLA_BR_MCAST_IGMP_VERSION]);
		err = br_multicast_set_igmp_version(&br->multicast_ctx,
						    igmp_version);
		if (err)
			return err;
	}

#if IS_ENABLED(CONFIG_IPV6)
	if (data[IFLA_BR_MCAST_MLD_VERSION]) {
		__u8 mld_version;

		mld_version = nla_get_u8(data[IFLA_BR_MCAST_MLD_VERSION]);
		err = br_multicast_set_mld_version(&br->multicast_ctx,
						   mld_version);
		if (err)
			return err;
	}
#endif
#endif
#if IS_ENABLED(CONFIG_BRIDGE_NETFILTER)
	if (data[IFLA_BR_NF_CALL_IPTABLES]) {
		u8 val = nla_get_u8(data[IFLA_BR_NF_CALL_IPTABLES]);

		br_opt_toggle(br, BROPT_NF_CALL_IPTABLES, !!val);
	}

	if (data[IFLA_BR_NF_CALL_IP6TABLES]) {
		u8 val = nla_get_u8(data[IFLA_BR_NF_CALL_IP6TABLES]);

		br_opt_toggle(br, BROPT_NF_CALL_IP6TABLES, !!val);
	}

	if (data[IFLA_BR_NF_CALL_ARPTABLES]) {
		u8 val = nla_get_u8(data[IFLA_BR_NF_CALL_ARPTABLES]);

		br_opt_toggle(br, BROPT_NF_CALL_ARPTABLES, !!val);
	}
#endif

	if (data[IFLA_BR_MULTI_BOOLOPT]) {
		struct br_boolopt_multi *bm;

		bm = nla_data(data[IFLA_BR_MULTI_BOOLOPT]);
		err = br_boolopt_multi_toggle(br, bm, extack);
		if (err)
			return err;
	}

	return 0;
}

static int br_dev_newlink(struct net *src_net, struct net_device *dev,
			  struct nlattr *tb[], struct nlattr *data[],
			  struct netlink_ext_ack *extack)
{
	struct net_bridge *br = netdev_priv(dev);
	int err;

	err = register_netdevice(dev);
	if (err)
		return err;

	if (tb[IFLA_ADDRESS]) {
		spin_lock_bh(&br->lock);
		br_stp_change_bridge_id(br, nla_data(tb[IFLA_ADDRESS]));
		spin_unlock_bh(&br->lock);
	}

	err = br_changelink(dev, tb, data, extack);
	if (err)
		br_dev_delete(dev, NULL);

	return err;
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
	       nla_total_size(sizeof(u8)) +	/* IFLA_BR_VLAN_STATS_PER_PORT */
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
	       nla_total_size(sizeof(u8)) +	/* IFLA_BR_MCAST_IGMP_VERSION */
	       nla_total_size(sizeof(u8)) +	/* IFLA_BR_MCAST_MLD_VERSION */
	       br_multicast_querier_state_size() + /* IFLA_BR_MCAST_QUERIER_STATE */
#endif
#if IS_ENABLED(CONFIG_BRIDGE_NETFILTER)
	       nla_total_size(sizeof(u8)) +     /* IFLA_BR_NF_CALL_IPTABLES */
	       nla_total_size(sizeof(u8)) +     /* IFLA_BR_NF_CALL_IP6TABLES */
	       nla_total_size(sizeof(u8)) +     /* IFLA_BR_NF_CALL_ARPTABLES */
#endif
	       nla_total_size(sizeof(struct br_boolopt_multi)) + /* IFLA_BR_MULTI_BOOLOPT */
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
	u8 vlan_enabled = br_vlan_enabled(br->dev);
	struct br_boolopt_multi bm;
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
	clockval = br_timer_value(&br->gc_work.timer);
	if (nla_put_u64_64bit(skb, IFLA_BR_GC_TIMER, clockval, IFLA_BR_PAD))
		return -EMSGSIZE;

	br_boolopt_multi_get(br, &bm);
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
	    nla_put(skb, IFLA_BR_GROUP_ADDR, ETH_ALEN, br->group_addr) ||
	    nla_put(skb, IFLA_BR_MULTI_BOOLOPT, sizeof(bm), &bm))
		return -EMSGSIZE;

#ifdef CONFIG_BRIDGE_VLAN_FILTERING
	if (nla_put_be16(skb, IFLA_BR_VLAN_PROTOCOL, br->vlan_proto) ||
	    nla_put_u16(skb, IFLA_BR_VLAN_DEFAULT_PVID, br->default_pvid) ||
	    nla_put_u8(skb, IFLA_BR_VLAN_STATS_ENABLED,
		       br_opt_get(br, BROPT_VLAN_STATS_ENABLED)) ||
	    nla_put_u8(skb, IFLA_BR_VLAN_STATS_PER_PORT,
		       br_opt_get(br, BROPT_VLAN_STATS_PER_PORT)))
		return -EMSGSIZE;
#endif
#ifdef CONFIG_BRIDGE_IGMP_SNOOPING
	if (nla_put_u8(skb, IFLA_BR_MCAST_ROUTER,
		       br->multicast_ctx.multicast_router) ||
	    nla_put_u8(skb, IFLA_BR_MCAST_SNOOPING,
		       br_opt_get(br, BROPT_MULTICAST_ENABLED)) ||
	    nla_put_u8(skb, IFLA_BR_MCAST_QUERY_USE_IFADDR,
		       br_opt_get(br, BROPT_MULTICAST_QUERY_USE_IFADDR)) ||
	    nla_put_u8(skb, IFLA_BR_MCAST_QUERIER,
		       br->multicast_ctx.multicast_querier) ||
	    nla_put_u8(skb, IFLA_BR_MCAST_STATS_ENABLED,
		       br_opt_get(br, BROPT_MULTICAST_STATS_ENABLED)) ||
	    nla_put_u32(skb, IFLA_BR_MCAST_HASH_ELASTICITY, RHT_ELASTICITY) ||
	    nla_put_u32(skb, IFLA_BR_MCAST_HASH_MAX, br->hash_max) ||
	    nla_put_u32(skb, IFLA_BR_MCAST_LAST_MEMBER_CNT,
			br->multicast_ctx.multicast_last_member_count) ||
	    nla_put_u32(skb, IFLA_BR_MCAST_STARTUP_QUERY_CNT,
			br->multicast_ctx.multicast_startup_query_count) ||
	    nla_put_u8(skb, IFLA_BR_MCAST_IGMP_VERSION,
		       br->multicast_ctx.multicast_igmp_version) ||
	    br_multicast_dump_querier_state(skb, &br->multicast_ctx,
					    IFLA_BR_MCAST_QUERIER_STATE))
		return -EMSGSIZE;
#if IS_ENABLED(CONFIG_IPV6)
	if (nla_put_u8(skb, IFLA_BR_MCAST_MLD_VERSION,
		       br->multicast_ctx.multicast_mld_version))
		return -EMSGSIZE;
#endif
	clockval = jiffies_to_clock_t(br->multicast_ctx.multicast_last_member_interval);
	if (nla_put_u64_64bit(skb, IFLA_BR_MCAST_LAST_MEMBER_INTVL, clockval,
			      IFLA_BR_PAD))
		return -EMSGSIZE;
	clockval = jiffies_to_clock_t(br->multicast_ctx.multicast_membership_interval);
	if (nla_put_u64_64bit(skb, IFLA_BR_MCAST_MEMBERSHIP_INTVL, clockval,
			      IFLA_BR_PAD))
		return -EMSGSIZE;
	clockval = jiffies_to_clock_t(br->multicast_ctx.multicast_querier_interval);
	if (nla_put_u64_64bit(skb, IFLA_BR_MCAST_QUERIER_INTVL, clockval,
			      IFLA_BR_PAD))
		return -EMSGSIZE;
	clockval = jiffies_to_clock_t(br->multicast_ctx.multicast_query_interval);
	if (nla_put_u64_64bit(skb, IFLA_BR_MCAST_QUERY_INTVL, clockval,
			      IFLA_BR_PAD))
		return -EMSGSIZE;
	clockval = jiffies_to_clock_t(br->multicast_ctx.multicast_query_response_interval);
	if (nla_put_u64_64bit(skb, IFLA_BR_MCAST_QUERY_RESPONSE_INTVL, clockval,
			      IFLA_BR_PAD))
		return -EMSGSIZE;
	clockval = jiffies_to_clock_t(br->multicast_ctx.multicast_startup_query_interval);
	if (nla_put_u64_64bit(skb, IFLA_BR_MCAST_STARTUP_QUERY_INTVL, clockval,
			      IFLA_BR_PAD))
		return -EMSGSIZE;
#endif
#if IS_ENABLED(CONFIG_BRIDGE_NETFILTER)
	if (nla_put_u8(skb, IFLA_BR_NF_CALL_IPTABLES,
		       br_opt_get(br, BROPT_NF_CALL_IPTABLES) ? 1 : 0) ||
	    nla_put_u8(skb, IFLA_BR_NF_CALL_IP6TABLES,
		       br_opt_get(br, BROPT_NF_CALL_IP6TABLES) ? 1 : 0) ||
	    nla_put_u8(skb, IFLA_BR_NF_CALL_ARPTABLES,
		       br_opt_get(br, BROPT_NF_CALL_ARPTABLES) ? 1 : 0))
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

	nest = nla_nest_start_noflag(skb, LINK_XSTATS_TYPE_BRIDGE);
	if (!nest)
		return -EMSGSIZE;

	if (vg) {
		u16 pvid;

		pvid = br_get_pvid(vg);
		list_for_each_entry(v, &vg->vlan_list, vlist) {
			struct bridge_vlan_xstats vxi;
			struct pcpu_sw_netstats stats;

			if (++vl_idx < *prividx)
				continue;
			memset(&vxi, 0, sizeof(vxi));
			vxi.vid = v->vid;
			vxi.flags = v->flags;
			if (v->vid == pvid)
				vxi.flags |= BRIDGE_VLAN_INFO_PVID;
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

	if (p) {
		nla = nla_reserve_64bit(skb, BRIDGE_XSTATS_STP,
					sizeof(p->stp_xstats),
					BRIDGE_XSTATS_PAD);
		if (!nla)
			goto nla_put_failure;

		spin_lock_bh(&br->lock);
		memcpy(nla_data(nla), &p->stp_xstats, sizeof(p->stp_xstats));
		spin_unlock_bh(&br->lock);
	}

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
	br_vlan_rtnl_init();
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
	br_vlan_rtnl_uninit();
	rtnl_af_unregister(&br_af_ops);
	rtnl_link_unregister(&br_link_ops);
}
