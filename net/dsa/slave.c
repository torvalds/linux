// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * net/dsa/slave.c - Slave device handling
 * Copyright (c) 2008-2009 Marvell Semiconductor
 */

#include <linux/list.h>
#include <linux/etherdevice.h>
#include <linux/netdevice.h>
#include <linux/phy.h>
#include <linux/phy_fixed.h>
#include <linux/phylink.h>
#include <linux/of_net.h>
#include <linux/of_mdio.h>
#include <linux/mdio.h>
#include <net/rtnetlink.h>
#include <net/pkt_cls.h>
#include <net/selftests.h>
#include <net/tc_act/tc_mirred.h>
#include <linux/if_bridge.h>
#include <linux/if_hsr.h>
#include <net/dcbnl.h>
#include <linux/netpoll.h>

#include "dsa_priv.h"

static void dsa_slave_standalone_event_work(struct work_struct *work)
{
	struct dsa_standalone_event_work *standalone_work =
		container_of(work, struct dsa_standalone_event_work, work);
	const unsigned char *addr = standalone_work->addr;
	struct net_device *dev = standalone_work->dev;
	struct dsa_port *dp = dsa_slave_to_port(dev);
	struct switchdev_obj_port_mdb mdb;
	struct dsa_switch *ds = dp->ds;
	u16 vid = standalone_work->vid;
	int err;

	switch (standalone_work->event) {
	case DSA_UC_ADD:
		err = dsa_port_standalone_host_fdb_add(dp, addr, vid);
		if (err) {
			dev_err(ds->dev,
				"port %d failed to add %pM vid %d to fdb: %d\n",
				dp->index, addr, vid, err);
			break;
		}
		break;

	case DSA_UC_DEL:
		err = dsa_port_standalone_host_fdb_del(dp, addr, vid);
		if (err) {
			dev_err(ds->dev,
				"port %d failed to delete %pM vid %d from fdb: %d\n",
				dp->index, addr, vid, err);
		}

		break;
	case DSA_MC_ADD:
		ether_addr_copy(mdb.addr, addr);
		mdb.vid = vid;

		err = dsa_port_standalone_host_mdb_add(dp, &mdb);
		if (err) {
			dev_err(ds->dev,
				"port %d failed to add %pM vid %d to mdb: %d\n",
				dp->index, addr, vid, err);
			break;
		}
		break;
	case DSA_MC_DEL:
		ether_addr_copy(mdb.addr, addr);
		mdb.vid = vid;

		err = dsa_port_standalone_host_mdb_del(dp, &mdb);
		if (err) {
			dev_err(ds->dev,
				"port %d failed to delete %pM vid %d from mdb: %d\n",
				dp->index, addr, vid, err);
		}

		break;
	}

	kfree(standalone_work);
}

static int dsa_slave_schedule_standalone_work(struct net_device *dev,
					      enum dsa_standalone_event event,
					      const unsigned char *addr,
					      u16 vid)
{
	struct dsa_standalone_event_work *standalone_work;

	standalone_work = kzalloc(sizeof(*standalone_work), GFP_ATOMIC);
	if (!standalone_work)
		return -ENOMEM;

	INIT_WORK(&standalone_work->work, dsa_slave_standalone_event_work);
	standalone_work->event = event;
	standalone_work->dev = dev;

	ether_addr_copy(standalone_work->addr, addr);
	standalone_work->vid = vid;

	dsa_schedule_work(&standalone_work->work);

	return 0;
}

static int dsa_slave_sync_uc(struct net_device *dev,
			     const unsigned char *addr)
{
	struct net_device *master = dsa_slave_to_master(dev);
	struct dsa_port *dp = dsa_slave_to_port(dev);

	dev_uc_add(master, addr);

	if (!dsa_switch_supports_uc_filtering(dp->ds))
		return 0;

	return dsa_slave_schedule_standalone_work(dev, DSA_UC_ADD, addr, 0);
}

static int dsa_slave_unsync_uc(struct net_device *dev,
			       const unsigned char *addr)
{
	struct net_device *master = dsa_slave_to_master(dev);
	struct dsa_port *dp = dsa_slave_to_port(dev);

	dev_uc_del(master, addr);

	if (!dsa_switch_supports_uc_filtering(dp->ds))
		return 0;

	return dsa_slave_schedule_standalone_work(dev, DSA_UC_DEL, addr, 0);
}

static int dsa_slave_sync_mc(struct net_device *dev,
			     const unsigned char *addr)
{
	struct net_device *master = dsa_slave_to_master(dev);
	struct dsa_port *dp = dsa_slave_to_port(dev);

	dev_mc_add(master, addr);

	if (!dsa_switch_supports_mc_filtering(dp->ds))
		return 0;

	return dsa_slave_schedule_standalone_work(dev, DSA_MC_ADD, addr, 0);
}

static int dsa_slave_unsync_mc(struct net_device *dev,
			       const unsigned char *addr)
{
	struct net_device *master = dsa_slave_to_master(dev);
	struct dsa_port *dp = dsa_slave_to_port(dev);

	dev_mc_del(master, addr);

	if (!dsa_switch_supports_mc_filtering(dp->ds))
		return 0;

	return dsa_slave_schedule_standalone_work(dev, DSA_MC_DEL, addr, 0);
}

/* slave mii_bus handling ***************************************************/
static int dsa_slave_phy_read(struct mii_bus *bus, int addr, int reg)
{
	struct dsa_switch *ds = bus->priv;

	if (ds->phys_mii_mask & (1 << addr))
		return ds->ops->phy_read(ds, addr, reg);

	return 0xffff;
}

static int dsa_slave_phy_write(struct mii_bus *bus, int addr, int reg, u16 val)
{
	struct dsa_switch *ds = bus->priv;

	if (ds->phys_mii_mask & (1 << addr))
		return ds->ops->phy_write(ds, addr, reg, val);

	return 0;
}

void dsa_slave_mii_bus_init(struct dsa_switch *ds)
{
	ds->slave_mii_bus->priv = (void *)ds;
	ds->slave_mii_bus->name = "dsa slave smi";
	ds->slave_mii_bus->read = dsa_slave_phy_read;
	ds->slave_mii_bus->write = dsa_slave_phy_write;
	snprintf(ds->slave_mii_bus->id, MII_BUS_ID_SIZE, "dsa-%d.%d",
		 ds->dst->index, ds->index);
	ds->slave_mii_bus->parent = ds->dev;
	ds->slave_mii_bus->phy_mask = ~ds->phys_mii_mask;
}


/* slave device handling ****************************************************/
static int dsa_slave_get_iflink(const struct net_device *dev)
{
	return dsa_slave_to_master(dev)->ifindex;
}

static int dsa_slave_open(struct net_device *dev)
{
	struct net_device *master = dsa_slave_to_master(dev);
	struct dsa_port *dp = dsa_slave_to_port(dev);
	struct dsa_switch *ds = dp->ds;
	int err;

	err = dev_open(master, NULL);
	if (err < 0) {
		netdev_err(dev, "failed to open master %s\n", master->name);
		goto out;
	}

	if (dsa_switch_supports_uc_filtering(ds)) {
		err = dsa_port_standalone_host_fdb_add(dp, dev->dev_addr, 0);
		if (err)
			goto out;
	}

	if (!ether_addr_equal(dev->dev_addr, master->dev_addr)) {
		err = dev_uc_add(master, dev->dev_addr);
		if (err < 0)
			goto del_host_addr;
	}

	err = dsa_port_enable_rt(dp, dev->phydev);
	if (err)
		goto del_unicast;

	return 0;

del_unicast:
	if (!ether_addr_equal(dev->dev_addr, master->dev_addr))
		dev_uc_del(master, dev->dev_addr);
del_host_addr:
	if (dsa_switch_supports_uc_filtering(ds))
		dsa_port_standalone_host_fdb_del(dp, dev->dev_addr, 0);
out:
	return err;
}

static int dsa_slave_close(struct net_device *dev)
{
	struct net_device *master = dsa_slave_to_master(dev);
	struct dsa_port *dp = dsa_slave_to_port(dev);
	struct dsa_switch *ds = dp->ds;

	dsa_port_disable_rt(dp);

	if (!ether_addr_equal(dev->dev_addr, master->dev_addr))
		dev_uc_del(master, dev->dev_addr);

	if (dsa_switch_supports_uc_filtering(ds))
		dsa_port_standalone_host_fdb_del(dp, dev->dev_addr, 0);

	return 0;
}

static void dsa_slave_manage_host_flood(struct net_device *dev)
{
	bool mc = dev->flags & (IFF_PROMISC | IFF_ALLMULTI);
	struct dsa_port *dp = dsa_slave_to_port(dev);
	bool uc = dev->flags & IFF_PROMISC;

	dsa_port_set_host_flood(dp, uc, mc);
}

static void dsa_slave_change_rx_flags(struct net_device *dev, int change)
{
	struct net_device *master = dsa_slave_to_master(dev);
	struct dsa_port *dp = dsa_slave_to_port(dev);
	struct dsa_switch *ds = dp->ds;

	if (change & IFF_ALLMULTI)
		dev_set_allmulti(master,
				 dev->flags & IFF_ALLMULTI ? 1 : -1);
	if (change & IFF_PROMISC)
		dev_set_promiscuity(master,
				    dev->flags & IFF_PROMISC ? 1 : -1);

	if (dsa_switch_supports_uc_filtering(ds) &&
	    dsa_switch_supports_mc_filtering(ds))
		dsa_slave_manage_host_flood(dev);
}

static void dsa_slave_set_rx_mode(struct net_device *dev)
{
	__dev_mc_sync(dev, dsa_slave_sync_mc, dsa_slave_unsync_mc);
	__dev_uc_sync(dev, dsa_slave_sync_uc, dsa_slave_unsync_uc);
}

static int dsa_slave_set_mac_address(struct net_device *dev, void *a)
{
	struct net_device *master = dsa_slave_to_master(dev);
	struct dsa_port *dp = dsa_slave_to_port(dev);
	struct dsa_switch *ds = dp->ds;
	struct sockaddr *addr = a;
	int err;

	if (!is_valid_ether_addr(addr->sa_data))
		return -EADDRNOTAVAIL;

	/* If the port is down, the address isn't synced yet to hardware or
	 * to the DSA master, so there is nothing to change.
	 */
	if (!(dev->flags & IFF_UP))
		goto out_change_dev_addr;

	if (dsa_switch_supports_uc_filtering(ds)) {
		err = dsa_port_standalone_host_fdb_add(dp, addr->sa_data, 0);
		if (err)
			return err;
	}

	if (!ether_addr_equal(addr->sa_data, master->dev_addr)) {
		err = dev_uc_add(master, addr->sa_data);
		if (err < 0)
			goto del_unicast;
	}

	if (!ether_addr_equal(dev->dev_addr, master->dev_addr))
		dev_uc_del(master, dev->dev_addr);

	if (dsa_switch_supports_uc_filtering(ds))
		dsa_port_standalone_host_fdb_del(dp, dev->dev_addr, 0);

out_change_dev_addr:
	eth_hw_addr_set(dev, addr->sa_data);

	return 0;

del_unicast:
	if (dsa_switch_supports_uc_filtering(ds))
		dsa_port_standalone_host_fdb_del(dp, addr->sa_data, 0);

	return err;
}

struct dsa_slave_dump_ctx {
	struct net_device *dev;
	struct sk_buff *skb;
	struct netlink_callback *cb;
	int idx;
};

static int
dsa_slave_port_fdb_do_dump(const unsigned char *addr, u16 vid,
			   bool is_static, void *data)
{
	struct dsa_slave_dump_ctx *dump = data;
	u32 portid = NETLINK_CB(dump->cb->skb).portid;
	u32 seq = dump->cb->nlh->nlmsg_seq;
	struct nlmsghdr *nlh;
	struct ndmsg *ndm;

	if (dump->idx < dump->cb->args[2])
		goto skip;

	nlh = nlmsg_put(dump->skb, portid, seq, RTM_NEWNEIGH,
			sizeof(*ndm), NLM_F_MULTI);
	if (!nlh)
		return -EMSGSIZE;

	ndm = nlmsg_data(nlh);
	ndm->ndm_family  = AF_BRIDGE;
	ndm->ndm_pad1    = 0;
	ndm->ndm_pad2    = 0;
	ndm->ndm_flags   = NTF_SELF;
	ndm->ndm_type    = 0;
	ndm->ndm_ifindex = dump->dev->ifindex;
	ndm->ndm_state   = is_static ? NUD_NOARP : NUD_REACHABLE;

	if (nla_put(dump->skb, NDA_LLADDR, ETH_ALEN, addr))
		goto nla_put_failure;

	if (vid && nla_put_u16(dump->skb, NDA_VLAN, vid))
		goto nla_put_failure;

	nlmsg_end(dump->skb, nlh);

skip:
	dump->idx++;
	return 0;

nla_put_failure:
	nlmsg_cancel(dump->skb, nlh);
	return -EMSGSIZE;
}

static int
dsa_slave_fdb_dump(struct sk_buff *skb, struct netlink_callback *cb,
		   struct net_device *dev, struct net_device *filter_dev,
		   int *idx)
{
	struct dsa_port *dp = dsa_slave_to_port(dev);
	struct dsa_slave_dump_ctx dump = {
		.dev = dev,
		.skb = skb,
		.cb = cb,
		.idx = *idx,
	};
	int err;

	err = dsa_port_fdb_dump(dp, dsa_slave_port_fdb_do_dump, &dump);
	*idx = dump.idx;

	return err;
}

static int dsa_slave_ioctl(struct net_device *dev, struct ifreq *ifr, int cmd)
{
	struct dsa_slave_priv *p = netdev_priv(dev);
	struct dsa_switch *ds = p->dp->ds;
	int port = p->dp->index;

	/* Pass through to switch driver if it supports timestamping */
	switch (cmd) {
	case SIOCGHWTSTAMP:
		if (ds->ops->port_hwtstamp_get)
			return ds->ops->port_hwtstamp_get(ds, port, ifr);
		break;
	case SIOCSHWTSTAMP:
		if (ds->ops->port_hwtstamp_set)
			return ds->ops->port_hwtstamp_set(ds, port, ifr);
		break;
	}

	return phylink_mii_ioctl(p->dp->pl, ifr, cmd);
}

static int dsa_slave_port_attr_set(struct net_device *dev, const void *ctx,
				   const struct switchdev_attr *attr,
				   struct netlink_ext_ack *extack)
{
	struct dsa_port *dp = dsa_slave_to_port(dev);
	int ret;

	if (ctx && ctx != dp)
		return 0;

	switch (attr->id) {
	case SWITCHDEV_ATTR_ID_PORT_STP_STATE:
		if (!dsa_port_offloads_bridge_port(dp, attr->orig_dev))
			return -EOPNOTSUPP;

		ret = dsa_port_set_state(dp, attr->u.stp_state, true);
		break;
	case SWITCHDEV_ATTR_ID_PORT_MST_STATE:
		if (!dsa_port_offloads_bridge_port(dp, attr->orig_dev))
			return -EOPNOTSUPP;

		ret = dsa_port_set_mst_state(dp, &attr->u.mst_state, extack);
		break;
	case SWITCHDEV_ATTR_ID_BRIDGE_VLAN_FILTERING:
		if (!dsa_port_offloads_bridge_dev(dp, attr->orig_dev))
			return -EOPNOTSUPP;

		ret = dsa_port_vlan_filtering(dp, attr->u.vlan_filtering,
					      extack);
		break;
	case SWITCHDEV_ATTR_ID_BRIDGE_AGEING_TIME:
		if (!dsa_port_offloads_bridge_dev(dp, attr->orig_dev))
			return -EOPNOTSUPP;

		ret = dsa_port_ageing_time(dp, attr->u.ageing_time);
		break;
	case SWITCHDEV_ATTR_ID_BRIDGE_MST:
		if (!dsa_port_offloads_bridge_dev(dp, attr->orig_dev))
			return -EOPNOTSUPP;

		ret = dsa_port_mst_enable(dp, attr->u.mst, extack);
		break;
	case SWITCHDEV_ATTR_ID_PORT_PRE_BRIDGE_FLAGS:
		if (!dsa_port_offloads_bridge_port(dp, attr->orig_dev))
			return -EOPNOTSUPP;

		ret = dsa_port_pre_bridge_flags(dp, attr->u.brport_flags,
						extack);
		break;
	case SWITCHDEV_ATTR_ID_PORT_BRIDGE_FLAGS:
		if (!dsa_port_offloads_bridge_port(dp, attr->orig_dev))
			return -EOPNOTSUPP;

		ret = dsa_port_bridge_flags(dp, attr->u.brport_flags, extack);
		break;
	case SWITCHDEV_ATTR_ID_VLAN_MSTI:
		if (!dsa_port_offloads_bridge_dev(dp, attr->orig_dev))
			return -EOPNOTSUPP;

		ret = dsa_port_vlan_msti(dp, &attr->u.vlan_msti);
		break;
	default:
		ret = -EOPNOTSUPP;
		break;
	}

	return ret;
}

/* Must be called under rcu_read_lock() */
static int
dsa_slave_vlan_check_for_8021q_uppers(struct net_device *slave,
				      const struct switchdev_obj_port_vlan *vlan)
{
	struct net_device *upper_dev;
	struct list_head *iter;

	netdev_for_each_upper_dev_rcu(slave, upper_dev, iter) {
		u16 vid;

		if (!is_vlan_dev(upper_dev))
			continue;

		vid = vlan_dev_vlan_id(upper_dev);
		if (vid == vlan->vid)
			return -EBUSY;
	}

	return 0;
}

static int dsa_slave_vlan_add(struct net_device *dev,
			      const struct switchdev_obj *obj,
			      struct netlink_ext_ack *extack)
{
	struct dsa_port *dp = dsa_slave_to_port(dev);
	struct switchdev_obj_port_vlan *vlan;
	int err;

	if (dsa_port_skip_vlan_configuration(dp)) {
		NL_SET_ERR_MSG_MOD(extack, "skipping configuration of VLAN");
		return 0;
	}

	vlan = SWITCHDEV_OBJ_PORT_VLAN(obj);

	/* Deny adding a bridge VLAN when there is already an 802.1Q upper with
	 * the same VID.
	 */
	if (br_vlan_enabled(dsa_port_bridge_dev_get(dp))) {
		rcu_read_lock();
		err = dsa_slave_vlan_check_for_8021q_uppers(dev, vlan);
		rcu_read_unlock();
		if (err) {
			NL_SET_ERR_MSG_MOD(extack,
					   "Port already has a VLAN upper with this VID");
			return err;
		}
	}

	return dsa_port_vlan_add(dp, vlan, extack);
}

/* Offload a VLAN installed on the bridge or on a foreign interface by
 * installing it as a VLAN towards the CPU port.
 */
static int dsa_slave_host_vlan_add(struct net_device *dev,
				   const struct switchdev_obj *obj,
				   struct netlink_ext_ack *extack)
{
	struct dsa_port *dp = dsa_slave_to_port(dev);
	struct switchdev_obj_port_vlan vlan;

	/* Do nothing if this is a software bridge */
	if (!dp->bridge)
		return -EOPNOTSUPP;

	if (dsa_port_skip_vlan_configuration(dp)) {
		NL_SET_ERR_MSG_MOD(extack, "skipping configuration of VLAN");
		return 0;
	}

	vlan = *SWITCHDEV_OBJ_PORT_VLAN(obj);

	/* Even though drivers often handle CPU membership in special ways,
	 * it doesn't make sense to program a PVID, so clear this flag.
	 */
	vlan.flags &= ~BRIDGE_VLAN_INFO_PVID;

	return dsa_port_host_vlan_add(dp, &vlan, extack);
}

static int dsa_slave_port_obj_add(struct net_device *dev, const void *ctx,
				  const struct switchdev_obj *obj,
				  struct netlink_ext_ack *extack)
{
	struct dsa_port *dp = dsa_slave_to_port(dev);
	int err;

	if (ctx && ctx != dp)
		return 0;

	switch (obj->id) {
	case SWITCHDEV_OBJ_ID_PORT_MDB:
		if (!dsa_port_offloads_bridge_port(dp, obj->orig_dev))
			return -EOPNOTSUPP;

		err = dsa_port_mdb_add(dp, SWITCHDEV_OBJ_PORT_MDB(obj));
		break;
	case SWITCHDEV_OBJ_ID_HOST_MDB:
		if (!dsa_port_offloads_bridge_dev(dp, obj->orig_dev))
			return -EOPNOTSUPP;

		err = dsa_port_bridge_host_mdb_add(dp, SWITCHDEV_OBJ_PORT_MDB(obj));
		break;
	case SWITCHDEV_OBJ_ID_PORT_VLAN:
		if (dsa_port_offloads_bridge_port(dp, obj->orig_dev))
			err = dsa_slave_vlan_add(dev, obj, extack);
		else
			err = dsa_slave_host_vlan_add(dev, obj, extack);
		break;
	case SWITCHDEV_OBJ_ID_MRP:
		if (!dsa_port_offloads_bridge_dev(dp, obj->orig_dev))
			return -EOPNOTSUPP;

		err = dsa_port_mrp_add(dp, SWITCHDEV_OBJ_MRP(obj));
		break;
	case SWITCHDEV_OBJ_ID_RING_ROLE_MRP:
		if (!dsa_port_offloads_bridge_dev(dp, obj->orig_dev))
			return -EOPNOTSUPP;

		err = dsa_port_mrp_add_ring_role(dp,
						 SWITCHDEV_OBJ_RING_ROLE_MRP(obj));
		break;
	default:
		err = -EOPNOTSUPP;
		break;
	}

	return err;
}

static int dsa_slave_vlan_del(struct net_device *dev,
			      const struct switchdev_obj *obj)
{
	struct dsa_port *dp = dsa_slave_to_port(dev);
	struct switchdev_obj_port_vlan *vlan;

	if (dsa_port_skip_vlan_configuration(dp))
		return 0;

	vlan = SWITCHDEV_OBJ_PORT_VLAN(obj);

	return dsa_port_vlan_del(dp, vlan);
}

static int dsa_slave_host_vlan_del(struct net_device *dev,
				   const struct switchdev_obj *obj)
{
	struct dsa_port *dp = dsa_slave_to_port(dev);
	struct switchdev_obj_port_vlan *vlan;

	/* Do nothing if this is a software bridge */
	if (!dp->bridge)
		return -EOPNOTSUPP;

	if (dsa_port_skip_vlan_configuration(dp))
		return 0;

	vlan = SWITCHDEV_OBJ_PORT_VLAN(obj);

	return dsa_port_host_vlan_del(dp, vlan);
}

static int dsa_slave_port_obj_del(struct net_device *dev, const void *ctx,
				  const struct switchdev_obj *obj)
{
	struct dsa_port *dp = dsa_slave_to_port(dev);
	int err;

	if (ctx && ctx != dp)
		return 0;

	switch (obj->id) {
	case SWITCHDEV_OBJ_ID_PORT_MDB:
		if (!dsa_port_offloads_bridge_port(dp, obj->orig_dev))
			return -EOPNOTSUPP;

		err = dsa_port_mdb_del(dp, SWITCHDEV_OBJ_PORT_MDB(obj));
		break;
	case SWITCHDEV_OBJ_ID_HOST_MDB:
		if (!dsa_port_offloads_bridge_dev(dp, obj->orig_dev))
			return -EOPNOTSUPP;

		err = dsa_port_bridge_host_mdb_del(dp, SWITCHDEV_OBJ_PORT_MDB(obj));
		break;
	case SWITCHDEV_OBJ_ID_PORT_VLAN:
		if (dsa_port_offloads_bridge_port(dp, obj->orig_dev))
			err = dsa_slave_vlan_del(dev, obj);
		else
			err = dsa_slave_host_vlan_del(dev, obj);
		break;
	case SWITCHDEV_OBJ_ID_MRP:
		if (!dsa_port_offloads_bridge_dev(dp, obj->orig_dev))
			return -EOPNOTSUPP;

		err = dsa_port_mrp_del(dp, SWITCHDEV_OBJ_MRP(obj));
		break;
	case SWITCHDEV_OBJ_ID_RING_ROLE_MRP:
		if (!dsa_port_offloads_bridge_dev(dp, obj->orig_dev))
			return -EOPNOTSUPP;

		err = dsa_port_mrp_del_ring_role(dp,
						 SWITCHDEV_OBJ_RING_ROLE_MRP(obj));
		break;
	default:
		err = -EOPNOTSUPP;
		break;
	}

	return err;
}

static inline netdev_tx_t dsa_slave_netpoll_send_skb(struct net_device *dev,
						     struct sk_buff *skb)
{
#ifdef CONFIG_NET_POLL_CONTROLLER
	struct dsa_slave_priv *p = netdev_priv(dev);

	return netpoll_send_skb(p->netpoll, skb);
#else
	BUG();
	return NETDEV_TX_OK;
#endif
}

static void dsa_skb_tx_timestamp(struct dsa_slave_priv *p,
				 struct sk_buff *skb)
{
	struct dsa_switch *ds = p->dp->ds;

	if (!(skb_shinfo(skb)->tx_flags & SKBTX_HW_TSTAMP))
		return;

	if (!ds->ops->port_txtstamp)
		return;

	ds->ops->port_txtstamp(ds, p->dp->index, skb);
}

netdev_tx_t dsa_enqueue_skb(struct sk_buff *skb, struct net_device *dev)
{
	/* SKB for netpoll still need to be mangled with the protocol-specific
	 * tag to be successfully transmitted
	 */
	if (unlikely(netpoll_tx_running(dev)))
		return dsa_slave_netpoll_send_skb(dev, skb);

	/* Queue the SKB for transmission on the parent interface, but
	 * do not modify its EtherType
	 */
	skb->dev = dsa_slave_to_master(dev);
	dev_queue_xmit(skb);

	return NETDEV_TX_OK;
}
EXPORT_SYMBOL_GPL(dsa_enqueue_skb);

static int dsa_realloc_skb(struct sk_buff *skb, struct net_device *dev)
{
	int needed_headroom = dev->needed_headroom;
	int needed_tailroom = dev->needed_tailroom;

	/* For tail taggers, we need to pad short frames ourselves, to ensure
	 * that the tail tag does not fail at its role of being at the end of
	 * the packet, once the master interface pads the frame. Account for
	 * that pad length here, and pad later.
	 */
	if (unlikely(needed_tailroom && skb->len < ETH_ZLEN))
		needed_tailroom += ETH_ZLEN - skb->len;
	/* skb_headroom() returns unsigned int... */
	needed_headroom = max_t(int, needed_headroom - skb_headroom(skb), 0);
	needed_tailroom = max_t(int, needed_tailroom - skb_tailroom(skb), 0);

	if (likely(!needed_headroom && !needed_tailroom && !skb_cloned(skb)))
		/* No reallocation needed, yay! */
		return 0;

	return pskb_expand_head(skb, needed_headroom, needed_tailroom,
				GFP_ATOMIC);
}

static netdev_tx_t dsa_slave_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct dsa_slave_priv *p = netdev_priv(dev);
	struct sk_buff *nskb;

	dev_sw_netstats_tx_add(dev, 1, skb->len);

	memset(skb->cb, 0, sizeof(skb->cb));

	/* Handle tx timestamp if any */
	dsa_skb_tx_timestamp(p, skb);

	if (dsa_realloc_skb(skb, dev)) {
		dev_kfree_skb_any(skb);
		return NETDEV_TX_OK;
	}

	/* needed_tailroom should still be 'warm' in the cache line from
	 * dsa_realloc_skb(), which has also ensured that padding is safe.
	 */
	if (dev->needed_tailroom)
		eth_skb_pad(skb);

	/* Transmit function may have to reallocate the original SKB,
	 * in which case it must have freed it. Only free it here on error.
	 */
	nskb = p->xmit(skb, dev);
	if (!nskb) {
		kfree_skb(skb);
		return NETDEV_TX_OK;
	}

	return dsa_enqueue_skb(nskb, dev);
}

/* ethtool operations *******************************************************/

static void dsa_slave_get_drvinfo(struct net_device *dev,
				  struct ethtool_drvinfo *drvinfo)
{
	strlcpy(drvinfo->driver, "dsa", sizeof(drvinfo->driver));
	strlcpy(drvinfo->fw_version, "N/A", sizeof(drvinfo->fw_version));
	strlcpy(drvinfo->bus_info, "platform", sizeof(drvinfo->bus_info));
}

static int dsa_slave_get_regs_len(struct net_device *dev)
{
	struct dsa_port *dp = dsa_slave_to_port(dev);
	struct dsa_switch *ds = dp->ds;

	if (ds->ops->get_regs_len)
		return ds->ops->get_regs_len(ds, dp->index);

	return -EOPNOTSUPP;
}

static void
dsa_slave_get_regs(struct net_device *dev, struct ethtool_regs *regs, void *_p)
{
	struct dsa_port *dp = dsa_slave_to_port(dev);
	struct dsa_switch *ds = dp->ds;

	if (ds->ops->get_regs)
		ds->ops->get_regs(ds, dp->index, regs, _p);
}

static int dsa_slave_nway_reset(struct net_device *dev)
{
	struct dsa_port *dp = dsa_slave_to_port(dev);

	return phylink_ethtool_nway_reset(dp->pl);
}

static int dsa_slave_get_eeprom_len(struct net_device *dev)
{
	struct dsa_port *dp = dsa_slave_to_port(dev);
	struct dsa_switch *ds = dp->ds;

	if (ds->cd && ds->cd->eeprom_len)
		return ds->cd->eeprom_len;

	if (ds->ops->get_eeprom_len)
		return ds->ops->get_eeprom_len(ds);

	return 0;
}

static int dsa_slave_get_eeprom(struct net_device *dev,
				struct ethtool_eeprom *eeprom, u8 *data)
{
	struct dsa_port *dp = dsa_slave_to_port(dev);
	struct dsa_switch *ds = dp->ds;

	if (ds->ops->get_eeprom)
		return ds->ops->get_eeprom(ds, eeprom, data);

	return -EOPNOTSUPP;
}

static int dsa_slave_set_eeprom(struct net_device *dev,
				struct ethtool_eeprom *eeprom, u8 *data)
{
	struct dsa_port *dp = dsa_slave_to_port(dev);
	struct dsa_switch *ds = dp->ds;

	if (ds->ops->set_eeprom)
		return ds->ops->set_eeprom(ds, eeprom, data);

	return -EOPNOTSUPP;
}

static void dsa_slave_get_strings(struct net_device *dev,
				  uint32_t stringset, uint8_t *data)
{
	struct dsa_port *dp = dsa_slave_to_port(dev);
	struct dsa_switch *ds = dp->ds;

	if (stringset == ETH_SS_STATS) {
		int len = ETH_GSTRING_LEN;

		strncpy(data, "tx_packets", len);
		strncpy(data + len, "tx_bytes", len);
		strncpy(data + 2 * len, "rx_packets", len);
		strncpy(data + 3 * len, "rx_bytes", len);
		if (ds->ops->get_strings)
			ds->ops->get_strings(ds, dp->index, stringset,
					     data + 4 * len);
	} else if (stringset ==  ETH_SS_TEST) {
		net_selftest_get_strings(data);
	}

}

static void dsa_slave_get_ethtool_stats(struct net_device *dev,
					struct ethtool_stats *stats,
					uint64_t *data)
{
	struct dsa_port *dp = dsa_slave_to_port(dev);
	struct dsa_switch *ds = dp->ds;
	struct pcpu_sw_netstats *s;
	unsigned int start;
	int i;

	for_each_possible_cpu(i) {
		u64 tx_packets, tx_bytes, rx_packets, rx_bytes;

		s = per_cpu_ptr(dev->tstats, i);
		do {
			start = u64_stats_fetch_begin_irq(&s->syncp);
			tx_packets = s->tx_packets;
			tx_bytes = s->tx_bytes;
			rx_packets = s->rx_packets;
			rx_bytes = s->rx_bytes;
		} while (u64_stats_fetch_retry_irq(&s->syncp, start));
		data[0] += tx_packets;
		data[1] += tx_bytes;
		data[2] += rx_packets;
		data[3] += rx_bytes;
	}
	if (ds->ops->get_ethtool_stats)
		ds->ops->get_ethtool_stats(ds, dp->index, data + 4);
}

static int dsa_slave_get_sset_count(struct net_device *dev, int sset)
{
	struct dsa_port *dp = dsa_slave_to_port(dev);
	struct dsa_switch *ds = dp->ds;

	if (sset == ETH_SS_STATS) {
		int count = 0;

		if (ds->ops->get_sset_count) {
			count = ds->ops->get_sset_count(ds, dp->index, sset);
			if (count < 0)
				return count;
		}

		return count + 4;
	} else if (sset ==  ETH_SS_TEST) {
		return net_selftest_get_count();
	}

	return -EOPNOTSUPP;
}

static void dsa_slave_get_eth_phy_stats(struct net_device *dev,
					struct ethtool_eth_phy_stats *phy_stats)
{
	struct dsa_port *dp = dsa_slave_to_port(dev);
	struct dsa_switch *ds = dp->ds;

	if (ds->ops->get_eth_phy_stats)
		ds->ops->get_eth_phy_stats(ds, dp->index, phy_stats);
}

static void dsa_slave_get_eth_mac_stats(struct net_device *dev,
					struct ethtool_eth_mac_stats *mac_stats)
{
	struct dsa_port *dp = dsa_slave_to_port(dev);
	struct dsa_switch *ds = dp->ds;

	if (ds->ops->get_eth_mac_stats)
		ds->ops->get_eth_mac_stats(ds, dp->index, mac_stats);
}

static void
dsa_slave_get_eth_ctrl_stats(struct net_device *dev,
			     struct ethtool_eth_ctrl_stats *ctrl_stats)
{
	struct dsa_port *dp = dsa_slave_to_port(dev);
	struct dsa_switch *ds = dp->ds;

	if (ds->ops->get_eth_ctrl_stats)
		ds->ops->get_eth_ctrl_stats(ds, dp->index, ctrl_stats);
}

static void dsa_slave_net_selftest(struct net_device *ndev,
				   struct ethtool_test *etest, u64 *buf)
{
	struct dsa_port *dp = dsa_slave_to_port(ndev);
	struct dsa_switch *ds = dp->ds;

	if (ds->ops->self_test) {
		ds->ops->self_test(ds, dp->index, etest, buf);
		return;
	}

	net_selftest(ndev, etest, buf);
}

static void dsa_slave_get_wol(struct net_device *dev, struct ethtool_wolinfo *w)
{
	struct dsa_port *dp = dsa_slave_to_port(dev);
	struct dsa_switch *ds = dp->ds;

	phylink_ethtool_get_wol(dp->pl, w);

	if (ds->ops->get_wol)
		ds->ops->get_wol(ds, dp->index, w);
}

static int dsa_slave_set_wol(struct net_device *dev, struct ethtool_wolinfo *w)
{
	struct dsa_port *dp = dsa_slave_to_port(dev);
	struct dsa_switch *ds = dp->ds;
	int ret = -EOPNOTSUPP;

	phylink_ethtool_set_wol(dp->pl, w);

	if (ds->ops->set_wol)
		ret = ds->ops->set_wol(ds, dp->index, w);

	return ret;
}

static int dsa_slave_set_eee(struct net_device *dev, struct ethtool_eee *e)
{
	struct dsa_port *dp = dsa_slave_to_port(dev);
	struct dsa_switch *ds = dp->ds;
	int ret;

	/* Port's PHY and MAC both need to be EEE capable */
	if (!dev->phydev || !dp->pl)
		return -ENODEV;

	if (!ds->ops->set_mac_eee)
		return -EOPNOTSUPP;

	ret = ds->ops->set_mac_eee(ds, dp->index, e);
	if (ret)
		return ret;

	return phylink_ethtool_set_eee(dp->pl, e);
}

static int dsa_slave_get_eee(struct net_device *dev, struct ethtool_eee *e)
{
	struct dsa_port *dp = dsa_slave_to_port(dev);
	struct dsa_switch *ds = dp->ds;
	int ret;

	/* Port's PHY and MAC both need to be EEE capable */
	if (!dev->phydev || !dp->pl)
		return -ENODEV;

	if (!ds->ops->get_mac_eee)
		return -EOPNOTSUPP;

	ret = ds->ops->get_mac_eee(ds, dp->index, e);
	if (ret)
		return ret;

	return phylink_ethtool_get_eee(dp->pl, e);
}

static int dsa_slave_get_link_ksettings(struct net_device *dev,
					struct ethtool_link_ksettings *cmd)
{
	struct dsa_port *dp = dsa_slave_to_port(dev);

	return phylink_ethtool_ksettings_get(dp->pl, cmd);
}

static int dsa_slave_set_link_ksettings(struct net_device *dev,
					const struct ethtool_link_ksettings *cmd)
{
	struct dsa_port *dp = dsa_slave_to_port(dev);

	return phylink_ethtool_ksettings_set(dp->pl, cmd);
}

static void dsa_slave_get_pauseparam(struct net_device *dev,
				     struct ethtool_pauseparam *pause)
{
	struct dsa_port *dp = dsa_slave_to_port(dev);

	phylink_ethtool_get_pauseparam(dp->pl, pause);
}

static int dsa_slave_set_pauseparam(struct net_device *dev,
				    struct ethtool_pauseparam *pause)
{
	struct dsa_port *dp = dsa_slave_to_port(dev);

	return phylink_ethtool_set_pauseparam(dp->pl, pause);
}

#ifdef CONFIG_NET_POLL_CONTROLLER
static int dsa_slave_netpoll_setup(struct net_device *dev,
				   struct netpoll_info *ni)
{
	struct net_device *master = dsa_slave_to_master(dev);
	struct dsa_slave_priv *p = netdev_priv(dev);
	struct netpoll *netpoll;
	int err = 0;

	netpoll = kzalloc(sizeof(*netpoll), GFP_KERNEL);
	if (!netpoll)
		return -ENOMEM;

	err = __netpoll_setup(netpoll, master);
	if (err) {
		kfree(netpoll);
		goto out;
	}

	p->netpoll = netpoll;
out:
	return err;
}

static void dsa_slave_netpoll_cleanup(struct net_device *dev)
{
	struct dsa_slave_priv *p = netdev_priv(dev);
	struct netpoll *netpoll = p->netpoll;

	if (!netpoll)
		return;

	p->netpoll = NULL;

	__netpoll_free(netpoll);
}

static void dsa_slave_poll_controller(struct net_device *dev)
{
}
#endif

static struct dsa_mall_tc_entry *
dsa_slave_mall_tc_entry_find(struct net_device *dev, unsigned long cookie)
{
	struct dsa_slave_priv *p = netdev_priv(dev);
	struct dsa_mall_tc_entry *mall_tc_entry;

	list_for_each_entry(mall_tc_entry, &p->mall_tc_list, list)
		if (mall_tc_entry->cookie == cookie)
			return mall_tc_entry;

	return NULL;
}

static int
dsa_slave_add_cls_matchall_mirred(struct net_device *dev,
				  struct tc_cls_matchall_offload *cls,
				  bool ingress)
{
	struct netlink_ext_ack *extack = cls->common.extack;
	struct dsa_port *dp = dsa_slave_to_port(dev);
	struct dsa_slave_priv *p = netdev_priv(dev);
	struct dsa_mall_mirror_tc_entry *mirror;
	struct dsa_mall_tc_entry *mall_tc_entry;
	struct dsa_switch *ds = dp->ds;
	struct flow_action_entry *act;
	struct dsa_port *to_dp;
	int err;

	if (!ds->ops->port_mirror_add)
		return -EOPNOTSUPP;

	if (!flow_action_basic_hw_stats_check(&cls->rule->action,
					      cls->common.extack))
		return -EOPNOTSUPP;

	act = &cls->rule->action.entries[0];

	if (!act->dev)
		return -EINVAL;

	if (!dsa_slave_dev_check(act->dev))
		return -EOPNOTSUPP;

	mall_tc_entry = kzalloc(sizeof(*mall_tc_entry), GFP_KERNEL);
	if (!mall_tc_entry)
		return -ENOMEM;

	mall_tc_entry->cookie = cls->cookie;
	mall_tc_entry->type = DSA_PORT_MALL_MIRROR;
	mirror = &mall_tc_entry->mirror;

	to_dp = dsa_slave_to_port(act->dev);

	mirror->to_local_port = to_dp->index;
	mirror->ingress = ingress;

	err = ds->ops->port_mirror_add(ds, dp->index, mirror, ingress, extack);
	if (err) {
		kfree(mall_tc_entry);
		return err;
	}

	list_add_tail(&mall_tc_entry->list, &p->mall_tc_list);

	return err;
}

static int
dsa_slave_add_cls_matchall_police(struct net_device *dev,
				  struct tc_cls_matchall_offload *cls,
				  bool ingress)
{
	struct netlink_ext_ack *extack = cls->common.extack;
	struct dsa_port *dp = dsa_slave_to_port(dev);
	struct dsa_slave_priv *p = netdev_priv(dev);
	struct dsa_mall_policer_tc_entry *policer;
	struct dsa_mall_tc_entry *mall_tc_entry;
	struct dsa_switch *ds = dp->ds;
	struct flow_action_entry *act;
	int err;

	if (!ds->ops->port_policer_add) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Policing offload not implemented");
		return -EOPNOTSUPP;
	}

	if (!ingress) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Only supported on ingress qdisc");
		return -EOPNOTSUPP;
	}

	if (!flow_action_basic_hw_stats_check(&cls->rule->action,
					      cls->common.extack))
		return -EOPNOTSUPP;

	list_for_each_entry(mall_tc_entry, &p->mall_tc_list, list) {
		if (mall_tc_entry->type == DSA_PORT_MALL_POLICER) {
			NL_SET_ERR_MSG_MOD(extack,
					   "Only one port policer allowed");
			return -EEXIST;
		}
	}

	act = &cls->rule->action.entries[0];

	mall_tc_entry = kzalloc(sizeof(*mall_tc_entry), GFP_KERNEL);
	if (!mall_tc_entry)
		return -ENOMEM;

	mall_tc_entry->cookie = cls->cookie;
	mall_tc_entry->type = DSA_PORT_MALL_POLICER;
	policer = &mall_tc_entry->policer;
	policer->rate_bytes_per_sec = act->police.rate_bytes_ps;
	policer->burst = act->police.burst;

	err = ds->ops->port_policer_add(ds, dp->index, policer);
	if (err) {
		kfree(mall_tc_entry);
		return err;
	}

	list_add_tail(&mall_tc_entry->list, &p->mall_tc_list);

	return err;
}

static int dsa_slave_add_cls_matchall(struct net_device *dev,
				      struct tc_cls_matchall_offload *cls,
				      bool ingress)
{
	int err = -EOPNOTSUPP;

	if (cls->common.protocol == htons(ETH_P_ALL) &&
	    flow_offload_has_one_action(&cls->rule->action) &&
	    cls->rule->action.entries[0].id == FLOW_ACTION_MIRRED)
		err = dsa_slave_add_cls_matchall_mirred(dev, cls, ingress);
	else if (flow_offload_has_one_action(&cls->rule->action) &&
		 cls->rule->action.entries[0].id == FLOW_ACTION_POLICE)
		err = dsa_slave_add_cls_matchall_police(dev, cls, ingress);

	return err;
}

static void dsa_slave_del_cls_matchall(struct net_device *dev,
				       struct tc_cls_matchall_offload *cls)
{
	struct dsa_port *dp = dsa_slave_to_port(dev);
	struct dsa_mall_tc_entry *mall_tc_entry;
	struct dsa_switch *ds = dp->ds;

	mall_tc_entry = dsa_slave_mall_tc_entry_find(dev, cls->cookie);
	if (!mall_tc_entry)
		return;

	list_del(&mall_tc_entry->list);

	switch (mall_tc_entry->type) {
	case DSA_PORT_MALL_MIRROR:
		if (ds->ops->port_mirror_del)
			ds->ops->port_mirror_del(ds, dp->index,
						 &mall_tc_entry->mirror);
		break;
	case DSA_PORT_MALL_POLICER:
		if (ds->ops->port_policer_del)
			ds->ops->port_policer_del(ds, dp->index);
		break;
	default:
		WARN_ON(1);
	}

	kfree(mall_tc_entry);
}

static int dsa_slave_setup_tc_cls_matchall(struct net_device *dev,
					   struct tc_cls_matchall_offload *cls,
					   bool ingress)
{
	if (cls->common.chain_index)
		return -EOPNOTSUPP;

	switch (cls->command) {
	case TC_CLSMATCHALL_REPLACE:
		return dsa_slave_add_cls_matchall(dev, cls, ingress);
	case TC_CLSMATCHALL_DESTROY:
		dsa_slave_del_cls_matchall(dev, cls);
		return 0;
	default:
		return -EOPNOTSUPP;
	}
}

static int dsa_slave_add_cls_flower(struct net_device *dev,
				    struct flow_cls_offload *cls,
				    bool ingress)
{
	struct dsa_port *dp = dsa_slave_to_port(dev);
	struct dsa_switch *ds = dp->ds;
	int port = dp->index;

	if (!ds->ops->cls_flower_add)
		return -EOPNOTSUPP;

	return ds->ops->cls_flower_add(ds, port, cls, ingress);
}

static int dsa_slave_del_cls_flower(struct net_device *dev,
				    struct flow_cls_offload *cls,
				    bool ingress)
{
	struct dsa_port *dp = dsa_slave_to_port(dev);
	struct dsa_switch *ds = dp->ds;
	int port = dp->index;

	if (!ds->ops->cls_flower_del)
		return -EOPNOTSUPP;

	return ds->ops->cls_flower_del(ds, port, cls, ingress);
}

static int dsa_slave_stats_cls_flower(struct net_device *dev,
				      struct flow_cls_offload *cls,
				      bool ingress)
{
	struct dsa_port *dp = dsa_slave_to_port(dev);
	struct dsa_switch *ds = dp->ds;
	int port = dp->index;

	if (!ds->ops->cls_flower_stats)
		return -EOPNOTSUPP;

	return ds->ops->cls_flower_stats(ds, port, cls, ingress);
}

static int dsa_slave_setup_tc_cls_flower(struct net_device *dev,
					 struct flow_cls_offload *cls,
					 bool ingress)
{
	switch (cls->command) {
	case FLOW_CLS_REPLACE:
		return dsa_slave_add_cls_flower(dev, cls, ingress);
	case FLOW_CLS_DESTROY:
		return dsa_slave_del_cls_flower(dev, cls, ingress);
	case FLOW_CLS_STATS:
		return dsa_slave_stats_cls_flower(dev, cls, ingress);
	default:
		return -EOPNOTSUPP;
	}
}

static int dsa_slave_setup_tc_block_cb(enum tc_setup_type type, void *type_data,
				       void *cb_priv, bool ingress)
{
	struct net_device *dev = cb_priv;

	if (!tc_can_offload(dev))
		return -EOPNOTSUPP;

	switch (type) {
	case TC_SETUP_CLSMATCHALL:
		return dsa_slave_setup_tc_cls_matchall(dev, type_data, ingress);
	case TC_SETUP_CLSFLOWER:
		return dsa_slave_setup_tc_cls_flower(dev, type_data, ingress);
	default:
		return -EOPNOTSUPP;
	}
}

static int dsa_slave_setup_tc_block_cb_ig(enum tc_setup_type type,
					  void *type_data, void *cb_priv)
{
	return dsa_slave_setup_tc_block_cb(type, type_data, cb_priv, true);
}

static int dsa_slave_setup_tc_block_cb_eg(enum tc_setup_type type,
					  void *type_data, void *cb_priv)
{
	return dsa_slave_setup_tc_block_cb(type, type_data, cb_priv, false);
}

static LIST_HEAD(dsa_slave_block_cb_list);

static int dsa_slave_setup_tc_block(struct net_device *dev,
				    struct flow_block_offload *f)
{
	struct flow_block_cb *block_cb;
	flow_setup_cb_t *cb;

	if (f->binder_type == FLOW_BLOCK_BINDER_TYPE_CLSACT_INGRESS)
		cb = dsa_slave_setup_tc_block_cb_ig;
	else if (f->binder_type == FLOW_BLOCK_BINDER_TYPE_CLSACT_EGRESS)
		cb = dsa_slave_setup_tc_block_cb_eg;
	else
		return -EOPNOTSUPP;

	f->driver_block_list = &dsa_slave_block_cb_list;

	switch (f->command) {
	case FLOW_BLOCK_BIND:
		if (flow_block_cb_is_busy(cb, dev, &dsa_slave_block_cb_list))
			return -EBUSY;

		block_cb = flow_block_cb_alloc(cb, dev, dev, NULL);
		if (IS_ERR(block_cb))
			return PTR_ERR(block_cb);

		flow_block_cb_add(block_cb, f);
		list_add_tail(&block_cb->driver_list, &dsa_slave_block_cb_list);
		return 0;
	case FLOW_BLOCK_UNBIND:
		block_cb = flow_block_cb_lookup(f->block, cb, dev);
		if (!block_cb)
			return -ENOENT;

		flow_block_cb_remove(block_cb, f);
		list_del(&block_cb->driver_list);
		return 0;
	default:
		return -EOPNOTSUPP;
	}
}

static int dsa_slave_setup_ft_block(struct dsa_switch *ds, int port,
				    void *type_data)
{
	struct dsa_port *cpu_dp = dsa_to_port(ds, port)->cpu_dp;
	struct net_device *master = cpu_dp->master;

	if (!master->netdev_ops->ndo_setup_tc)
		return -EOPNOTSUPP;

	return master->netdev_ops->ndo_setup_tc(master, TC_SETUP_FT, type_data);
}

static int dsa_slave_setup_tc(struct net_device *dev, enum tc_setup_type type,
			      void *type_data)
{
	struct dsa_port *dp = dsa_slave_to_port(dev);
	struct dsa_switch *ds = dp->ds;

	switch (type) {
	case TC_SETUP_BLOCK:
		return dsa_slave_setup_tc_block(dev, type_data);
	case TC_SETUP_FT:
		return dsa_slave_setup_ft_block(ds, dp->index, type_data);
	default:
		break;
	}

	if (!ds->ops->port_setup_tc)
		return -EOPNOTSUPP;

	return ds->ops->port_setup_tc(ds, dp->index, type, type_data);
}

static int dsa_slave_get_rxnfc(struct net_device *dev,
			       struct ethtool_rxnfc *nfc, u32 *rule_locs)
{
	struct dsa_port *dp = dsa_slave_to_port(dev);
	struct dsa_switch *ds = dp->ds;

	if (!ds->ops->get_rxnfc)
		return -EOPNOTSUPP;

	return ds->ops->get_rxnfc(ds, dp->index, nfc, rule_locs);
}

static int dsa_slave_set_rxnfc(struct net_device *dev,
			       struct ethtool_rxnfc *nfc)
{
	struct dsa_port *dp = dsa_slave_to_port(dev);
	struct dsa_switch *ds = dp->ds;

	if (!ds->ops->set_rxnfc)
		return -EOPNOTSUPP;

	return ds->ops->set_rxnfc(ds, dp->index, nfc);
}

static int dsa_slave_get_ts_info(struct net_device *dev,
				 struct ethtool_ts_info *ts)
{
	struct dsa_slave_priv *p = netdev_priv(dev);
	struct dsa_switch *ds = p->dp->ds;

	if (!ds->ops->get_ts_info)
		return -EOPNOTSUPP;

	return ds->ops->get_ts_info(ds, p->dp->index, ts);
}

static int dsa_slave_vlan_rx_add_vid(struct net_device *dev, __be16 proto,
				     u16 vid)
{
	struct dsa_port *dp = dsa_slave_to_port(dev);
	struct switchdev_obj_port_vlan vlan = {
		.obj.id = SWITCHDEV_OBJ_ID_PORT_VLAN,
		.vid = vid,
		/* This API only allows programming tagged, non-PVID VIDs */
		.flags = 0,
	};
	struct netlink_ext_ack extack = {0};
	int ret;

	/* User port... */
	ret = dsa_port_vlan_add(dp, &vlan, &extack);
	if (ret) {
		if (extack._msg)
			netdev_err(dev, "%s\n", extack._msg);
		return ret;
	}

	/* And CPU port... */
	ret = dsa_port_host_vlan_add(dp, &vlan, &extack);
	if (ret) {
		if (extack._msg)
			netdev_err(dev, "CPU port %d: %s\n", dp->cpu_dp->index,
				   extack._msg);
		return ret;
	}

	return 0;
}

static int dsa_slave_vlan_rx_kill_vid(struct net_device *dev, __be16 proto,
				      u16 vid)
{
	struct dsa_port *dp = dsa_slave_to_port(dev);
	struct switchdev_obj_port_vlan vlan = {
		.vid = vid,
		/* This API only allows programming tagged, non-PVID VIDs */
		.flags = 0,
	};
	int err;

	err = dsa_port_vlan_del(dp, &vlan);
	if (err)
		return err;

	return dsa_port_host_vlan_del(dp, &vlan);
}

static int dsa_slave_restore_vlan(struct net_device *vdev, int vid, void *arg)
{
	__be16 proto = vdev ? vlan_dev_vlan_proto(vdev) : htons(ETH_P_8021Q);

	return dsa_slave_vlan_rx_add_vid(arg, proto, vid);
}

static int dsa_slave_clear_vlan(struct net_device *vdev, int vid, void *arg)
{
	__be16 proto = vdev ? vlan_dev_vlan_proto(vdev) : htons(ETH_P_8021Q);

	return dsa_slave_vlan_rx_kill_vid(arg, proto, vid);
}

/* Keep the VLAN RX filtering list in sync with the hardware only if VLAN
 * filtering is enabled. The baseline is that only ports that offload a
 * VLAN-aware bridge are VLAN-aware, and standalone ports are VLAN-unaware,
 * but there are exceptions for quirky hardware.
 *
 * If ds->vlan_filtering_is_global = true, then standalone ports which share
 * the same switch with other ports that offload a VLAN-aware bridge are also
 * inevitably VLAN-aware.
 *
 * To summarize, a DSA switch port offloads:
 *
 * - If standalone (this includes software bridge, software LAG):
 *     - if ds->needs_standalone_vlan_filtering = true, OR if
 *       (ds->vlan_filtering_is_global = true AND there are bridges spanning
 *       this switch chip which have vlan_filtering=1)
 *         - the 8021q upper VLANs
 *     - else (standalone VLAN filtering is not needed, VLAN filtering is not
 *       global, or it is, but no port is under a VLAN-aware bridge):
 *         - no VLAN (any 8021q upper is a software VLAN)
 *
 * - If under a vlan_filtering=0 bridge which it offload:
 *     - if ds->configure_vlan_while_not_filtering = true (default):
 *         - the bridge VLANs. These VLANs are committed to hardware but inactive.
 *     - else (deprecated):
 *         - no VLAN. The bridge VLANs are not restored when VLAN awareness is
 *           enabled, so this behavior is broken and discouraged.
 *
 * - If under a vlan_filtering=1 bridge which it offload:
 *     - the bridge VLANs
 *     - the 8021q upper VLANs
 */
int dsa_slave_manage_vlan_filtering(struct net_device *slave,
				    bool vlan_filtering)
{
	int err;

	if (vlan_filtering) {
		slave->features |= NETIF_F_HW_VLAN_CTAG_FILTER;

		err = vlan_for_each(slave, dsa_slave_restore_vlan, slave);
		if (err) {
			vlan_for_each(slave, dsa_slave_clear_vlan, slave);
			slave->features &= ~NETIF_F_HW_VLAN_CTAG_FILTER;
			return err;
		}
	} else {
		err = vlan_for_each(slave, dsa_slave_clear_vlan, slave);
		if (err)
			return err;

		slave->features &= ~NETIF_F_HW_VLAN_CTAG_FILTER;
	}

	return 0;
}

struct dsa_hw_port {
	struct list_head list;
	struct net_device *dev;
	int old_mtu;
};

static int dsa_hw_port_list_set_mtu(struct list_head *hw_port_list, int mtu)
{
	const struct dsa_hw_port *p;
	int err;

	list_for_each_entry(p, hw_port_list, list) {
		if (p->dev->mtu == mtu)
			continue;

		err = dev_set_mtu(p->dev, mtu);
		if (err)
			goto rollback;
	}

	return 0;

rollback:
	list_for_each_entry_continue_reverse(p, hw_port_list, list) {
		if (p->dev->mtu == p->old_mtu)
			continue;

		if (dev_set_mtu(p->dev, p->old_mtu))
			netdev_err(p->dev, "Failed to restore MTU\n");
	}

	return err;
}

static void dsa_hw_port_list_free(struct list_head *hw_port_list)
{
	struct dsa_hw_port *p, *n;

	list_for_each_entry_safe(p, n, hw_port_list, list)
		kfree(p);
}

/* Make the hardware datapath to/from @dev limited to a common MTU */
static void dsa_bridge_mtu_normalization(struct dsa_port *dp)
{
	struct list_head hw_port_list;
	struct dsa_switch_tree *dst;
	int min_mtu = ETH_MAX_MTU;
	struct dsa_port *other_dp;
	int err;

	if (!dp->ds->mtu_enforcement_ingress)
		return;

	if (!dp->bridge)
		return;

	INIT_LIST_HEAD(&hw_port_list);

	/* Populate the list of ports that are part of the same bridge
	 * as the newly added/modified port
	 */
	list_for_each_entry(dst, &dsa_tree_list, list) {
		list_for_each_entry(other_dp, &dst->ports, list) {
			struct dsa_hw_port *hw_port;
			struct net_device *slave;

			if (other_dp->type != DSA_PORT_TYPE_USER)
				continue;

			if (!dsa_port_bridge_same(dp, other_dp))
				continue;

			if (!other_dp->ds->mtu_enforcement_ingress)
				continue;

			slave = other_dp->slave;

			if (min_mtu > slave->mtu)
				min_mtu = slave->mtu;

			hw_port = kzalloc(sizeof(*hw_port), GFP_KERNEL);
			if (!hw_port)
				goto out;

			hw_port->dev = slave;
			hw_port->old_mtu = slave->mtu;

			list_add(&hw_port->list, &hw_port_list);
		}
	}

	/* Attempt to configure the entire hardware bridge to the newly added
	 * interface's MTU first, regardless of whether the intention of the
	 * user was to raise or lower it.
	 */
	err = dsa_hw_port_list_set_mtu(&hw_port_list, dp->slave->mtu);
	if (!err)
		goto out;

	/* Clearly that didn't work out so well, so just set the minimum MTU on
	 * all hardware bridge ports now. If this fails too, then all ports will
	 * still have their old MTU rolled back anyway.
	 */
	dsa_hw_port_list_set_mtu(&hw_port_list, min_mtu);

out:
	dsa_hw_port_list_free(&hw_port_list);
}

int dsa_slave_change_mtu(struct net_device *dev, int new_mtu)
{
	struct net_device *master = dsa_slave_to_master(dev);
	struct dsa_port *dp = dsa_slave_to_port(dev);
	struct dsa_port *cpu_dp = dp->cpu_dp;
	struct dsa_switch *ds = dp->ds;
	struct dsa_port *other_dp;
	int largest_mtu = 0;
	int new_master_mtu;
	int old_master_mtu;
	int mtu_limit;
	int cpu_mtu;
	int err;

	if (!ds->ops->port_change_mtu)
		return -EOPNOTSUPP;

	dsa_tree_for_each_user_port(other_dp, ds->dst) {
		int slave_mtu;

		/* During probe, this function will be called for each slave
		 * device, while not all of them have been allocated. That's
		 * ok, it doesn't change what the maximum is, so ignore it.
		 */
		if (!other_dp->slave)
			continue;

		/* Pretend that we already applied the setting, which we
		 * actually haven't (still haven't done all integrity checks)
		 */
		if (dp == other_dp)
			slave_mtu = new_mtu;
		else
			slave_mtu = other_dp->slave->mtu;

		if (largest_mtu < slave_mtu)
			largest_mtu = slave_mtu;
	}

	mtu_limit = min_t(int, master->max_mtu, dev->max_mtu);
	old_master_mtu = master->mtu;
	new_master_mtu = largest_mtu + dsa_tag_protocol_overhead(cpu_dp->tag_ops);
	if (new_master_mtu > mtu_limit)
		return -ERANGE;

	/* If the master MTU isn't over limit, there's no need to check the CPU
	 * MTU, since that surely isn't either.
	 */
	cpu_mtu = largest_mtu;

	/* Start applying stuff */
	if (new_master_mtu != old_master_mtu) {
		err = dev_set_mtu(master, new_master_mtu);
		if (err < 0)
			goto out_master_failed;

		/* We only need to propagate the MTU of the CPU port to
		 * upstream switches, so emit a notifier which updates them.
		 */
		err = dsa_port_mtu_change(cpu_dp, cpu_mtu);
		if (err)
			goto out_cpu_failed;
	}

	err = ds->ops->port_change_mtu(ds, dp->index, new_mtu);
	if (err)
		goto out_port_failed;

	dev->mtu = new_mtu;

	dsa_bridge_mtu_normalization(dp);

	return 0;

out_port_failed:
	if (new_master_mtu != old_master_mtu)
		dsa_port_mtu_change(cpu_dp, old_master_mtu -
				    dsa_tag_protocol_overhead(cpu_dp->tag_ops));
out_cpu_failed:
	if (new_master_mtu != old_master_mtu)
		dev_set_mtu(master, old_master_mtu);
out_master_failed:
	return err;
}

static int __maybe_unused
dsa_slave_dcbnl_set_default_prio(struct net_device *dev, struct dcb_app *app)
{
	struct dsa_port *dp = dsa_slave_to_port(dev);
	struct dsa_switch *ds = dp->ds;
	unsigned long mask, new_prio;
	int err, port = dp->index;

	if (!ds->ops->port_set_default_prio)
		return -EOPNOTSUPP;

	err = dcb_ieee_setapp(dev, app);
	if (err)
		return err;

	mask = dcb_ieee_getapp_mask(dev, app);
	new_prio = __fls(mask);

	err = ds->ops->port_set_default_prio(ds, port, new_prio);
	if (err) {
		dcb_ieee_delapp(dev, app);
		return err;
	}

	return 0;
}

static int __maybe_unused
dsa_slave_dcbnl_add_dscp_prio(struct net_device *dev, struct dcb_app *app)
{
	struct dsa_port *dp = dsa_slave_to_port(dev);
	struct dsa_switch *ds = dp->ds;
	unsigned long mask, new_prio;
	int err, port = dp->index;
	u8 dscp = app->protocol;

	if (!ds->ops->port_add_dscp_prio)
		return -EOPNOTSUPP;

	if (dscp >= 64) {
		netdev_err(dev, "DSCP APP entry with protocol value %u is invalid\n",
			   dscp);
		return -EINVAL;
	}

	err = dcb_ieee_setapp(dev, app);
	if (err)
		return err;

	mask = dcb_ieee_getapp_mask(dev, app);
	new_prio = __fls(mask);

	err = ds->ops->port_add_dscp_prio(ds, port, dscp, new_prio);
	if (err) {
		dcb_ieee_delapp(dev, app);
		return err;
	}

	return 0;
}

static int __maybe_unused dsa_slave_dcbnl_ieee_setapp(struct net_device *dev,
						      struct dcb_app *app)
{
	switch (app->selector) {
	case IEEE_8021QAZ_APP_SEL_ETHERTYPE:
		switch (app->protocol) {
		case 0:
			return dsa_slave_dcbnl_set_default_prio(dev, app);
		default:
			return -EOPNOTSUPP;
		}
		break;
	case IEEE_8021QAZ_APP_SEL_DSCP:
		return dsa_slave_dcbnl_add_dscp_prio(dev, app);
	default:
		return -EOPNOTSUPP;
	}
}

static int __maybe_unused
dsa_slave_dcbnl_del_default_prio(struct net_device *dev, struct dcb_app *app)
{
	struct dsa_port *dp = dsa_slave_to_port(dev);
	struct dsa_switch *ds = dp->ds;
	unsigned long mask, new_prio;
	int err, port = dp->index;

	if (!ds->ops->port_set_default_prio)
		return -EOPNOTSUPP;

	err = dcb_ieee_delapp(dev, app);
	if (err)
		return err;

	mask = dcb_ieee_getapp_mask(dev, app);
	new_prio = mask ? __fls(mask) : 0;

	err = ds->ops->port_set_default_prio(ds, port, new_prio);
	if (err) {
		dcb_ieee_setapp(dev, app);
		return err;
	}

	return 0;
}

static int __maybe_unused
dsa_slave_dcbnl_del_dscp_prio(struct net_device *dev, struct dcb_app *app)
{
	struct dsa_port *dp = dsa_slave_to_port(dev);
	struct dsa_switch *ds = dp->ds;
	int err, port = dp->index;
	u8 dscp = app->protocol;

	if (!ds->ops->port_del_dscp_prio)
		return -EOPNOTSUPP;

	err = dcb_ieee_delapp(dev, app);
	if (err)
		return err;

	err = ds->ops->port_del_dscp_prio(ds, port, dscp, app->priority);
	if (err) {
		dcb_ieee_setapp(dev, app);
		return err;
	}

	return 0;
}

static int __maybe_unused dsa_slave_dcbnl_ieee_delapp(struct net_device *dev,
						      struct dcb_app *app)
{
	switch (app->selector) {
	case IEEE_8021QAZ_APP_SEL_ETHERTYPE:
		switch (app->protocol) {
		case 0:
			return dsa_slave_dcbnl_del_default_prio(dev, app);
		default:
			return -EOPNOTSUPP;
		}
		break;
	case IEEE_8021QAZ_APP_SEL_DSCP:
		return dsa_slave_dcbnl_del_dscp_prio(dev, app);
	default:
		return -EOPNOTSUPP;
	}
}

/* Pre-populate the DCB application priority table with the priorities
 * configured during switch setup, which we read from hardware here.
 */
static int dsa_slave_dcbnl_init(struct net_device *dev)
{
	struct dsa_port *dp = dsa_slave_to_port(dev);
	struct dsa_switch *ds = dp->ds;
	int port = dp->index;
	int err;

	if (ds->ops->port_get_default_prio) {
		int prio = ds->ops->port_get_default_prio(ds, port);
		struct dcb_app app = {
			.selector = IEEE_8021QAZ_APP_SEL_ETHERTYPE,
			.protocol = 0,
			.priority = prio,
		};

		if (prio < 0)
			return prio;

		err = dcb_ieee_setapp(dev, &app);
		if (err)
			return err;
	}

	if (ds->ops->port_get_dscp_prio) {
		int protocol;

		for (protocol = 0; protocol < 64; protocol++) {
			struct dcb_app app = {
				.selector = IEEE_8021QAZ_APP_SEL_DSCP,
				.protocol = protocol,
			};
			int prio;

			prio = ds->ops->port_get_dscp_prio(ds, port, protocol);
			if (prio == -EOPNOTSUPP)
				continue;
			if (prio < 0)
				return prio;

			app.priority = prio;

			err = dcb_ieee_setapp(dev, &app);
			if (err)
				return err;
		}
	}

	return 0;
}

static const struct ethtool_ops dsa_slave_ethtool_ops = {
	.get_drvinfo		= dsa_slave_get_drvinfo,
	.get_regs_len		= dsa_slave_get_regs_len,
	.get_regs		= dsa_slave_get_regs,
	.nway_reset		= dsa_slave_nway_reset,
	.get_link		= ethtool_op_get_link,
	.get_eeprom_len		= dsa_slave_get_eeprom_len,
	.get_eeprom		= dsa_slave_get_eeprom,
	.set_eeprom		= dsa_slave_set_eeprom,
	.get_strings		= dsa_slave_get_strings,
	.get_ethtool_stats	= dsa_slave_get_ethtool_stats,
	.get_sset_count		= dsa_slave_get_sset_count,
	.get_eth_phy_stats	= dsa_slave_get_eth_phy_stats,
	.get_eth_mac_stats	= dsa_slave_get_eth_mac_stats,
	.get_eth_ctrl_stats	= dsa_slave_get_eth_ctrl_stats,
	.set_wol		= dsa_slave_set_wol,
	.get_wol		= dsa_slave_get_wol,
	.set_eee		= dsa_slave_set_eee,
	.get_eee		= dsa_slave_get_eee,
	.get_link_ksettings	= dsa_slave_get_link_ksettings,
	.set_link_ksettings	= dsa_slave_set_link_ksettings,
	.get_pauseparam		= dsa_slave_get_pauseparam,
	.set_pauseparam		= dsa_slave_set_pauseparam,
	.get_rxnfc		= dsa_slave_get_rxnfc,
	.set_rxnfc		= dsa_slave_set_rxnfc,
	.get_ts_info		= dsa_slave_get_ts_info,
	.self_test		= dsa_slave_net_selftest,
};

static const struct dcbnl_rtnl_ops __maybe_unused dsa_slave_dcbnl_ops = {
	.ieee_setapp		= dsa_slave_dcbnl_ieee_setapp,
	.ieee_delapp		= dsa_slave_dcbnl_ieee_delapp,
};

static struct devlink_port *dsa_slave_get_devlink_port(struct net_device *dev)
{
	struct dsa_port *dp = dsa_slave_to_port(dev);

	return &dp->devlink_port;
}

static void dsa_slave_get_stats64(struct net_device *dev,
				  struct rtnl_link_stats64 *s)
{
	struct dsa_port *dp = dsa_slave_to_port(dev);
	struct dsa_switch *ds = dp->ds;

	if (ds->ops->get_stats64)
		ds->ops->get_stats64(ds, dp->index, s);
	else
		dev_get_tstats64(dev, s);
}

static int dsa_slave_fill_forward_path(struct net_device_path_ctx *ctx,
				       struct net_device_path *path)
{
	struct dsa_port *dp = dsa_slave_to_port(ctx->dev);
	struct dsa_port *cpu_dp = dp->cpu_dp;

	path->dev = ctx->dev;
	path->type = DEV_PATH_DSA;
	path->dsa.proto = cpu_dp->tag_ops->proto;
	path->dsa.port = dp->index;
	ctx->dev = cpu_dp->master;

	return 0;
}

static const struct net_device_ops dsa_slave_netdev_ops = {
	.ndo_open	 	= dsa_slave_open,
	.ndo_stop		= dsa_slave_close,
	.ndo_start_xmit		= dsa_slave_xmit,
	.ndo_change_rx_flags	= dsa_slave_change_rx_flags,
	.ndo_set_rx_mode	= dsa_slave_set_rx_mode,
	.ndo_set_mac_address	= dsa_slave_set_mac_address,
	.ndo_fdb_dump		= dsa_slave_fdb_dump,
	.ndo_eth_ioctl		= dsa_slave_ioctl,
	.ndo_get_iflink		= dsa_slave_get_iflink,
#ifdef CONFIG_NET_POLL_CONTROLLER
	.ndo_netpoll_setup	= dsa_slave_netpoll_setup,
	.ndo_netpoll_cleanup	= dsa_slave_netpoll_cleanup,
	.ndo_poll_controller	= dsa_slave_poll_controller,
#endif
	.ndo_setup_tc		= dsa_slave_setup_tc,
	.ndo_get_stats64	= dsa_slave_get_stats64,
	.ndo_vlan_rx_add_vid	= dsa_slave_vlan_rx_add_vid,
	.ndo_vlan_rx_kill_vid	= dsa_slave_vlan_rx_kill_vid,
	.ndo_get_devlink_port	= dsa_slave_get_devlink_port,
	.ndo_change_mtu		= dsa_slave_change_mtu,
	.ndo_fill_forward_path	= dsa_slave_fill_forward_path,
};

static struct device_type dsa_type = {
	.name	= "dsa",
};

void dsa_port_phylink_mac_change(struct dsa_switch *ds, int port, bool up)
{
	const struct dsa_port *dp = dsa_to_port(ds, port);

	if (dp->pl)
		phylink_mac_change(dp->pl, up);
}
EXPORT_SYMBOL_GPL(dsa_port_phylink_mac_change);

static void dsa_slave_phylink_fixed_state(struct phylink_config *config,
					  struct phylink_link_state *state)
{
	struct dsa_port *dp = container_of(config, struct dsa_port, pl_config);
	struct dsa_switch *ds = dp->ds;

	/* No need to check that this operation is valid, the callback would
	 * not be called if it was not.
	 */
	ds->ops->phylink_fixed_state(ds, dp->index, state);
}

/* slave device setup *******************************************************/
static int dsa_slave_phy_connect(struct net_device *slave_dev, int addr,
				 u32 flags)
{
	struct dsa_port *dp = dsa_slave_to_port(slave_dev);
	struct dsa_switch *ds = dp->ds;

	slave_dev->phydev = mdiobus_get_phy(ds->slave_mii_bus, addr);
	if (!slave_dev->phydev) {
		netdev_err(slave_dev, "no phy at %d\n", addr);
		return -ENODEV;
	}

	slave_dev->phydev->dev_flags |= flags;

	return phylink_connect_phy(dp->pl, slave_dev->phydev);
}

static int dsa_slave_phy_setup(struct net_device *slave_dev)
{
	struct dsa_port *dp = dsa_slave_to_port(slave_dev);
	struct device_node *port_dn = dp->dn;
	struct dsa_switch *ds = dp->ds;
	u32 phy_flags = 0;
	int ret;

	dp->pl_config.dev = &slave_dev->dev;
	dp->pl_config.type = PHYLINK_NETDEV;

	/* The get_fixed_state callback takes precedence over polling the
	 * link GPIO in PHYLINK (see phylink_get_fixed_state).  Only set
	 * this if the switch provides such a callback.
	 */
	if (ds->ops->phylink_fixed_state) {
		dp->pl_config.get_fixed_state = dsa_slave_phylink_fixed_state;
		dp->pl_config.poll_fixed_state = true;
	}

	ret = dsa_port_phylink_create(dp);
	if (ret)
		return ret;

	if (ds->ops->get_phy_flags)
		phy_flags = ds->ops->get_phy_flags(ds, dp->index);

	ret = phylink_of_phy_connect(dp->pl, port_dn, phy_flags);
	if (ret == -ENODEV && ds->slave_mii_bus) {
		/* We could not connect to a designated PHY or SFP, so try to
		 * use the switch internal MDIO bus instead
		 */
		ret = dsa_slave_phy_connect(slave_dev, dp->index, phy_flags);
	}
	if (ret) {
		netdev_err(slave_dev, "failed to connect to PHY: %pe\n",
			   ERR_PTR(ret));
		phylink_destroy(dp->pl);
	}

	return ret;
}

void dsa_slave_setup_tagger(struct net_device *slave)
{
	struct dsa_port *dp = dsa_slave_to_port(slave);
	struct dsa_slave_priv *p = netdev_priv(slave);
	const struct dsa_port *cpu_dp = dp->cpu_dp;
	struct net_device *master = cpu_dp->master;
	const struct dsa_switch *ds = dp->ds;

	slave->needed_headroom = cpu_dp->tag_ops->needed_headroom;
	slave->needed_tailroom = cpu_dp->tag_ops->needed_tailroom;
	/* Try to save one extra realloc later in the TX path (in the master)
	 * by also inheriting the master's needed headroom and tailroom.
	 * The 8021q driver also does this.
	 */
	slave->needed_headroom += master->needed_headroom;
	slave->needed_tailroom += master->needed_tailroom;

	p->xmit = cpu_dp->tag_ops->xmit;

	slave->features = master->vlan_features | NETIF_F_HW_TC;
	slave->hw_features |= NETIF_F_HW_TC;
	slave->features |= NETIF_F_LLTX;
	if (slave->needed_tailroom)
		slave->features &= ~(NETIF_F_SG | NETIF_F_FRAGLIST);
	if (ds->needs_standalone_vlan_filtering)
		slave->features |= NETIF_F_HW_VLAN_CTAG_FILTER;
}

int dsa_slave_suspend(struct net_device *slave_dev)
{
	struct dsa_port *dp = dsa_slave_to_port(slave_dev);

	if (!netif_running(slave_dev))
		return 0;

	netif_device_detach(slave_dev);

	rtnl_lock();
	phylink_stop(dp->pl);
	rtnl_unlock();

	return 0;
}

int dsa_slave_resume(struct net_device *slave_dev)
{
	struct dsa_port *dp = dsa_slave_to_port(slave_dev);

	if (!netif_running(slave_dev))
		return 0;

	netif_device_attach(slave_dev);

	rtnl_lock();
	phylink_start(dp->pl);
	rtnl_unlock();

	return 0;
}

int dsa_slave_create(struct dsa_port *port)
{
	const struct dsa_port *cpu_dp = port->cpu_dp;
	struct net_device *master = cpu_dp->master;
	struct dsa_switch *ds = port->ds;
	const char *name = port->name;
	struct net_device *slave_dev;
	struct dsa_slave_priv *p;
	int ret;

	if (!ds->num_tx_queues)
		ds->num_tx_queues = 1;

	slave_dev = alloc_netdev_mqs(sizeof(struct dsa_slave_priv), name,
				     NET_NAME_UNKNOWN, ether_setup,
				     ds->num_tx_queues, 1);
	if (slave_dev == NULL)
		return -ENOMEM;

	slave_dev->ethtool_ops = &dsa_slave_ethtool_ops;
#if IS_ENABLED(CONFIG_DCB)
	slave_dev->dcbnl_ops = &dsa_slave_dcbnl_ops;
#endif
	if (!is_zero_ether_addr(port->mac))
		eth_hw_addr_set(slave_dev, port->mac);
	else
		eth_hw_addr_inherit(slave_dev, master);
	slave_dev->priv_flags |= IFF_NO_QUEUE;
	if (dsa_switch_supports_uc_filtering(ds))
		slave_dev->priv_flags |= IFF_UNICAST_FLT;
	slave_dev->netdev_ops = &dsa_slave_netdev_ops;
	if (ds->ops->port_max_mtu)
		slave_dev->max_mtu = ds->ops->port_max_mtu(ds, port->index);
	SET_NETDEV_DEVTYPE(slave_dev, &dsa_type);

	SET_NETDEV_DEV(slave_dev, port->ds->dev);
	slave_dev->dev.of_node = port->dn;
	slave_dev->vlan_features = master->vlan_features;

	p = netdev_priv(slave_dev);
	slave_dev->tstats = netdev_alloc_pcpu_stats(struct pcpu_sw_netstats);
	if (!slave_dev->tstats) {
		free_netdev(slave_dev);
		return -ENOMEM;
	}

	ret = gro_cells_init(&p->gcells, slave_dev);
	if (ret)
		goto out_free;

	p->dp = port;
	INIT_LIST_HEAD(&p->mall_tc_list);
	port->slave = slave_dev;
	dsa_slave_setup_tagger(slave_dev);

	netif_carrier_off(slave_dev);

	ret = dsa_slave_phy_setup(slave_dev);
	if (ret) {
		netdev_err(slave_dev,
			   "error %d setting up PHY for tree %d, switch %d, port %d\n",
			   ret, ds->dst->index, ds->index, port->index);
		goto out_gcells;
	}

	rtnl_lock();

	ret = dsa_slave_change_mtu(slave_dev, ETH_DATA_LEN);
	if (ret && ret != -EOPNOTSUPP)
		dev_warn(ds->dev, "nonfatal error %d setting MTU to %d on port %d\n",
			 ret, ETH_DATA_LEN, port->index);

	ret = register_netdevice(slave_dev);
	if (ret) {
		netdev_err(master, "error %d registering interface %s\n",
			   ret, slave_dev->name);
		rtnl_unlock();
		goto out_phy;
	}

	if (IS_ENABLED(CONFIG_DCB)) {
		ret = dsa_slave_dcbnl_init(slave_dev);
		if (ret) {
			netdev_err(slave_dev,
				   "failed to initialize DCB: %pe\n",
				   ERR_PTR(ret));
			rtnl_unlock();
			goto out_unregister;
		}
	}

	ret = netdev_upper_dev_link(master, slave_dev, NULL);

	rtnl_unlock();

	if (ret)
		goto out_unregister;

	return 0;

out_unregister:
	unregister_netdev(slave_dev);
out_phy:
	rtnl_lock();
	phylink_disconnect_phy(p->dp->pl);
	rtnl_unlock();
	phylink_destroy(p->dp->pl);
out_gcells:
	gro_cells_destroy(&p->gcells);
out_free:
	free_percpu(slave_dev->tstats);
	free_netdev(slave_dev);
	port->slave = NULL;
	return ret;
}

void dsa_slave_destroy(struct net_device *slave_dev)
{
	struct net_device *master = dsa_slave_to_master(slave_dev);
	struct dsa_port *dp = dsa_slave_to_port(slave_dev);
	struct dsa_slave_priv *p = netdev_priv(slave_dev);

	netif_carrier_off(slave_dev);
	rtnl_lock();
	netdev_upper_dev_unlink(master, slave_dev);
	unregister_netdevice(slave_dev);
	phylink_disconnect_phy(dp->pl);
	rtnl_unlock();

	phylink_destroy(dp->pl);
	gro_cells_destroy(&p->gcells);
	free_percpu(slave_dev->tstats);
	free_netdev(slave_dev);
}

bool dsa_slave_dev_check(const struct net_device *dev)
{
	return dev->netdev_ops == &dsa_slave_netdev_ops;
}
EXPORT_SYMBOL_GPL(dsa_slave_dev_check);

static int dsa_slave_changeupper(struct net_device *dev,
				 struct netdev_notifier_changeupper_info *info)
{
	struct dsa_port *dp = dsa_slave_to_port(dev);
	struct netlink_ext_ack *extack;
	int err = NOTIFY_DONE;

	extack = netdev_notifier_info_to_extack(&info->info);

	if (netif_is_bridge_master(info->upper_dev)) {
		if (info->linking) {
			err = dsa_port_bridge_join(dp, info->upper_dev, extack);
			if (!err)
				dsa_bridge_mtu_normalization(dp);
			if (err == -EOPNOTSUPP) {
				NL_SET_ERR_MSG_MOD(extack,
						   "Offloading not supported");
				err = 0;
			}
			err = notifier_from_errno(err);
		} else {
			dsa_port_bridge_leave(dp, info->upper_dev);
			err = NOTIFY_OK;
		}
	} else if (netif_is_lag_master(info->upper_dev)) {
		if (info->linking) {
			err = dsa_port_lag_join(dp, info->upper_dev,
						info->upper_info, extack);
			if (err == -EOPNOTSUPP) {
				NL_SET_ERR_MSG_MOD(info->info.extack,
						   "Offloading not supported");
				err = 0;
			}
			err = notifier_from_errno(err);
		} else {
			dsa_port_lag_leave(dp, info->upper_dev);
			err = NOTIFY_OK;
		}
	} else if (is_hsr_master(info->upper_dev)) {
		if (info->linking) {
			err = dsa_port_hsr_join(dp, info->upper_dev);
			if (err == -EOPNOTSUPP) {
				NL_SET_ERR_MSG_MOD(info->info.extack,
						   "Offloading not supported");
				err = 0;
			}
			err = notifier_from_errno(err);
		} else {
			dsa_port_hsr_leave(dp, info->upper_dev);
			err = NOTIFY_OK;
		}
	}

	return err;
}

static int dsa_slave_prechangeupper(struct net_device *dev,
				    struct netdev_notifier_changeupper_info *info)
{
	struct dsa_port *dp = dsa_slave_to_port(dev);

	if (netif_is_bridge_master(info->upper_dev) && !info->linking)
		dsa_port_pre_bridge_leave(dp, info->upper_dev);
	else if (netif_is_lag_master(info->upper_dev) && !info->linking)
		dsa_port_pre_lag_leave(dp, info->upper_dev);
	/* dsa_port_pre_hsr_leave is not yet necessary since hsr cannot be
	 * meaningfully enslaved to a bridge yet
	 */

	return NOTIFY_DONE;
}

static int
dsa_slave_lag_changeupper(struct net_device *dev,
			  struct netdev_notifier_changeupper_info *info)
{
	struct net_device *lower;
	struct list_head *iter;
	int err = NOTIFY_DONE;
	struct dsa_port *dp;

	netdev_for_each_lower_dev(dev, lower, iter) {
		if (!dsa_slave_dev_check(lower))
			continue;

		dp = dsa_slave_to_port(lower);
		if (!dp->lag)
			/* Software LAG */
			continue;

		err = dsa_slave_changeupper(lower, info);
		if (notifier_to_errno(err))
			break;
	}

	return err;
}

/* Same as dsa_slave_lag_changeupper() except that it calls
 * dsa_slave_prechangeupper()
 */
static int
dsa_slave_lag_prechangeupper(struct net_device *dev,
			     struct netdev_notifier_changeupper_info *info)
{
	struct net_device *lower;
	struct list_head *iter;
	int err = NOTIFY_DONE;
	struct dsa_port *dp;

	netdev_for_each_lower_dev(dev, lower, iter) {
		if (!dsa_slave_dev_check(lower))
			continue;

		dp = dsa_slave_to_port(lower);
		if (!dp->lag)
			/* Software LAG */
			continue;

		err = dsa_slave_prechangeupper(lower, info);
		if (notifier_to_errno(err))
			break;
	}

	return err;
}

static int
dsa_prevent_bridging_8021q_upper(struct net_device *dev,
				 struct netdev_notifier_changeupper_info *info)
{
	struct netlink_ext_ack *ext_ack;
	struct net_device *slave, *br;
	struct dsa_port *dp;

	ext_ack = netdev_notifier_info_to_extack(&info->info);

	if (!is_vlan_dev(dev))
		return NOTIFY_DONE;

	slave = vlan_dev_real_dev(dev);
	if (!dsa_slave_dev_check(slave))
		return NOTIFY_DONE;

	dp = dsa_slave_to_port(slave);
	br = dsa_port_bridge_dev_get(dp);
	if (!br)
		return NOTIFY_DONE;

	/* Deny enslaving a VLAN device into a VLAN-aware bridge */
	if (br_vlan_enabled(br) &&
	    netif_is_bridge_master(info->upper_dev) && info->linking) {
		NL_SET_ERR_MSG_MOD(ext_ack,
				   "Cannot enslave VLAN device into VLAN aware bridge");
		return notifier_from_errno(-EINVAL);
	}

	return NOTIFY_DONE;
}

static int
dsa_slave_check_8021q_upper(struct net_device *dev,
			    struct netdev_notifier_changeupper_info *info)
{
	struct dsa_port *dp = dsa_slave_to_port(dev);
	struct net_device *br = dsa_port_bridge_dev_get(dp);
	struct bridge_vlan_info br_info;
	struct netlink_ext_ack *extack;
	int err = NOTIFY_DONE;
	u16 vid;

	if (!br || !br_vlan_enabled(br))
		return NOTIFY_DONE;

	extack = netdev_notifier_info_to_extack(&info->info);
	vid = vlan_dev_vlan_id(info->upper_dev);

	/* br_vlan_get_info() returns -EINVAL or -ENOENT if the
	 * device, respectively the VID is not found, returning
	 * 0 means success, which is a failure for us here.
	 */
	err = br_vlan_get_info(br, vid, &br_info);
	if (err == 0) {
		NL_SET_ERR_MSG_MOD(extack,
				   "This VLAN is already configured by the bridge");
		return notifier_from_errno(-EBUSY);
	}

	return NOTIFY_DONE;
}

static int
dsa_slave_prechangeupper_sanity_check(struct net_device *dev,
				      struct netdev_notifier_changeupper_info *info)
{
	struct dsa_switch *ds;
	struct dsa_port *dp;
	int err;

	if (!dsa_slave_dev_check(dev))
		return dsa_prevent_bridging_8021q_upper(dev, info);

	dp = dsa_slave_to_port(dev);
	ds = dp->ds;

	if (ds->ops->port_prechangeupper) {
		err = ds->ops->port_prechangeupper(ds, dp->index, info);
		if (err)
			return notifier_from_errno(err);
	}

	if (is_vlan_dev(info->upper_dev))
		return dsa_slave_check_8021q_upper(dev, info);

	return NOTIFY_DONE;
}

static int dsa_slave_netdevice_event(struct notifier_block *nb,
				     unsigned long event, void *ptr)
{
	struct net_device *dev = netdev_notifier_info_to_dev(ptr);

	switch (event) {
	case NETDEV_PRECHANGEUPPER: {
		struct netdev_notifier_changeupper_info *info = ptr;
		int err;

		err = dsa_slave_prechangeupper_sanity_check(dev, info);
		if (err != NOTIFY_DONE)
			return err;

		if (dsa_slave_dev_check(dev))
			return dsa_slave_prechangeupper(dev, ptr);

		if (netif_is_lag_master(dev))
			return dsa_slave_lag_prechangeupper(dev, ptr);

		break;
	}
	case NETDEV_CHANGEUPPER:
		if (dsa_slave_dev_check(dev))
			return dsa_slave_changeupper(dev, ptr);

		if (netif_is_lag_master(dev))
			return dsa_slave_lag_changeupper(dev, ptr);

		break;
	case NETDEV_CHANGELOWERSTATE: {
		struct netdev_notifier_changelowerstate_info *info = ptr;
		struct dsa_port *dp;
		int err;

		if (!dsa_slave_dev_check(dev))
			break;

		dp = dsa_slave_to_port(dev);

		err = dsa_port_lag_change(dp, info->lower_state_info);
		return notifier_from_errno(err);
	}
	case NETDEV_CHANGE:
	case NETDEV_UP: {
		/* Track state of master port.
		 * DSA driver may require the master port (and indirectly
		 * the tagger) to be available for some special operation.
		 */
		if (netdev_uses_dsa(dev)) {
			struct dsa_port *cpu_dp = dev->dsa_ptr;
			struct dsa_switch_tree *dst = cpu_dp->ds->dst;

			/* Track when the master port is UP */
			dsa_tree_master_oper_state_change(dst, dev,
							  netif_oper_up(dev));

			/* Track when the master port is ready and can accept
			 * packet.
			 * NETDEV_UP event is not enough to flag a port as ready.
			 * We also have to wait for linkwatch_do_dev to dev_activate
			 * and emit a NETDEV_CHANGE event.
			 * We check if a master port is ready by checking if the dev
			 * have a qdisc assigned and is not noop.
			 */
			dsa_tree_master_admin_state_change(dst, dev,
							   !qdisc_tx_is_noop(dev));

			return NOTIFY_OK;
		}

		return NOTIFY_DONE;
	}
	case NETDEV_GOING_DOWN: {
		struct dsa_port *dp, *cpu_dp;
		struct dsa_switch_tree *dst;
		LIST_HEAD(close_list);

		if (!netdev_uses_dsa(dev))
			return NOTIFY_DONE;

		cpu_dp = dev->dsa_ptr;
		dst = cpu_dp->ds->dst;

		dsa_tree_master_admin_state_change(dst, dev, false);

		list_for_each_entry(dp, &dst->ports, list) {
			if (!dsa_port_is_user(dp))
				continue;

			list_add(&dp->slave->close_list, &close_list);
		}

		dev_close_many(&close_list, true);

		return NOTIFY_OK;
	}
	default:
		break;
	}

	return NOTIFY_DONE;
}

static void
dsa_fdb_offload_notify(struct dsa_switchdev_event_work *switchdev_work)
{
	struct switchdev_notifier_fdb_info info = {};

	info.addr = switchdev_work->addr;
	info.vid = switchdev_work->vid;
	info.offloaded = true;
	call_switchdev_notifiers(SWITCHDEV_FDB_OFFLOADED,
				 switchdev_work->orig_dev, &info.info, NULL);
}

static void dsa_slave_switchdev_event_work(struct work_struct *work)
{
	struct dsa_switchdev_event_work *switchdev_work =
		container_of(work, struct dsa_switchdev_event_work, work);
	const unsigned char *addr = switchdev_work->addr;
	struct net_device *dev = switchdev_work->dev;
	u16 vid = switchdev_work->vid;
	struct dsa_switch *ds;
	struct dsa_port *dp;
	int err;

	dp = dsa_slave_to_port(dev);
	ds = dp->ds;

	switch (switchdev_work->event) {
	case SWITCHDEV_FDB_ADD_TO_DEVICE:
		if (switchdev_work->host_addr)
			err = dsa_port_bridge_host_fdb_add(dp, addr, vid);
		else if (dp->lag)
			err = dsa_port_lag_fdb_add(dp, addr, vid);
		else
			err = dsa_port_fdb_add(dp, addr, vid);
		if (err) {
			dev_err(ds->dev,
				"port %d failed to add %pM vid %d to fdb: %d\n",
				dp->index, addr, vid, err);
			break;
		}
		dsa_fdb_offload_notify(switchdev_work);
		break;

	case SWITCHDEV_FDB_DEL_TO_DEVICE:
		if (switchdev_work->host_addr)
			err = dsa_port_bridge_host_fdb_del(dp, addr, vid);
		else if (dp->lag)
			err = dsa_port_lag_fdb_del(dp, addr, vid);
		else
			err = dsa_port_fdb_del(dp, addr, vid);
		if (err) {
			dev_err(ds->dev,
				"port %d failed to delete %pM vid %d from fdb: %d\n",
				dp->index, addr, vid, err);
		}

		break;
	}

	kfree(switchdev_work);
}

static bool dsa_foreign_dev_check(const struct net_device *dev,
				  const struct net_device *foreign_dev)
{
	const struct dsa_port *dp = dsa_slave_to_port(dev);
	struct dsa_switch_tree *dst = dp->ds->dst;

	if (netif_is_bridge_master(foreign_dev))
		return !dsa_tree_offloads_bridge_dev(dst, foreign_dev);

	if (netif_is_bridge_port(foreign_dev))
		return !dsa_tree_offloads_bridge_port(dst, foreign_dev);

	/* Everything else is foreign */
	return true;
}

static int dsa_slave_fdb_event(struct net_device *dev,
			       struct net_device *orig_dev,
			       unsigned long event, const void *ctx,
			       const struct switchdev_notifier_fdb_info *fdb_info)
{
	struct dsa_switchdev_event_work *switchdev_work;
	struct dsa_port *dp = dsa_slave_to_port(dev);
	bool host_addr = fdb_info->is_local;
	struct dsa_switch *ds = dp->ds;

	if (ctx && ctx != dp)
		return 0;

	if (!dp->bridge)
		return 0;

	if (switchdev_fdb_is_dynamically_learned(fdb_info)) {
		if (dsa_port_offloads_bridge_port(dp, orig_dev))
			return 0;

		/* FDB entries learned by the software bridge or by foreign
		 * bridge ports should be installed as host addresses only if
		 * the driver requests assisted learning.
		 */
		if (!ds->assisted_learning_on_cpu_port)
			return 0;
	}

	/* Also treat FDB entries on foreign interfaces bridged with us as host
	 * addresses.
	 */
	if (dsa_foreign_dev_check(dev, orig_dev))
		host_addr = true;

	/* Check early that we're not doing work in vain.
	 * Host addresses on LAG ports still require regular FDB ops,
	 * since the CPU port isn't in a LAG.
	 */
	if (dp->lag && !host_addr) {
		if (!ds->ops->lag_fdb_add || !ds->ops->lag_fdb_del)
			return -EOPNOTSUPP;
	} else {
		if (!ds->ops->port_fdb_add || !ds->ops->port_fdb_del)
			return -EOPNOTSUPP;
	}

	switchdev_work = kzalloc(sizeof(*switchdev_work), GFP_ATOMIC);
	if (!switchdev_work)
		return -ENOMEM;

	netdev_dbg(dev, "%s FDB entry towards %s, addr %pM vid %d%s\n",
		   event == SWITCHDEV_FDB_ADD_TO_DEVICE ? "Adding" : "Deleting",
		   orig_dev->name, fdb_info->addr, fdb_info->vid,
		   host_addr ? " as host address" : "");

	INIT_WORK(&switchdev_work->work, dsa_slave_switchdev_event_work);
	switchdev_work->event = event;
	switchdev_work->dev = dev;
	switchdev_work->orig_dev = orig_dev;

	ether_addr_copy(switchdev_work->addr, fdb_info->addr);
	switchdev_work->vid = fdb_info->vid;
	switchdev_work->host_addr = host_addr;

	dsa_schedule_work(&switchdev_work->work);

	return 0;
}

/* Called under rcu_read_lock() */
static int dsa_slave_switchdev_event(struct notifier_block *unused,
				     unsigned long event, void *ptr)
{
	struct net_device *dev = switchdev_notifier_info_to_dev(ptr);
	int err;

	switch (event) {
	case SWITCHDEV_PORT_ATTR_SET:
		err = switchdev_handle_port_attr_set(dev, ptr,
						     dsa_slave_dev_check,
						     dsa_slave_port_attr_set);
		return notifier_from_errno(err);
	case SWITCHDEV_FDB_ADD_TO_DEVICE:
	case SWITCHDEV_FDB_DEL_TO_DEVICE:
		err = switchdev_handle_fdb_event_to_device(dev, event, ptr,
							   dsa_slave_dev_check,
							   dsa_foreign_dev_check,
							   dsa_slave_fdb_event);
		return notifier_from_errno(err);
	default:
		return NOTIFY_DONE;
	}

	return NOTIFY_OK;
}

static int dsa_slave_switchdev_blocking_event(struct notifier_block *unused,
					      unsigned long event, void *ptr)
{
	struct net_device *dev = switchdev_notifier_info_to_dev(ptr);
	int err;

	switch (event) {
	case SWITCHDEV_PORT_OBJ_ADD:
		err = switchdev_handle_port_obj_add_foreign(dev, ptr,
							    dsa_slave_dev_check,
							    dsa_foreign_dev_check,
							    dsa_slave_port_obj_add);
		return notifier_from_errno(err);
	case SWITCHDEV_PORT_OBJ_DEL:
		err = switchdev_handle_port_obj_del_foreign(dev, ptr,
							    dsa_slave_dev_check,
							    dsa_foreign_dev_check,
							    dsa_slave_port_obj_del);
		return notifier_from_errno(err);
	case SWITCHDEV_PORT_ATTR_SET:
		err = switchdev_handle_port_attr_set(dev, ptr,
						     dsa_slave_dev_check,
						     dsa_slave_port_attr_set);
		return notifier_from_errno(err);
	}

	return NOTIFY_DONE;
}

static struct notifier_block dsa_slave_nb __read_mostly = {
	.notifier_call  = dsa_slave_netdevice_event,
};

struct notifier_block dsa_slave_switchdev_notifier = {
	.notifier_call = dsa_slave_switchdev_event,
};

struct notifier_block dsa_slave_switchdev_blocking_notifier = {
	.notifier_call = dsa_slave_switchdev_blocking_event,
};

int dsa_slave_register_notifier(void)
{
	struct notifier_block *nb;
	int err;

	err = register_netdevice_notifier(&dsa_slave_nb);
	if (err)
		return err;

	err = register_switchdev_notifier(&dsa_slave_switchdev_notifier);
	if (err)
		goto err_switchdev_nb;

	nb = &dsa_slave_switchdev_blocking_notifier;
	err = register_switchdev_blocking_notifier(nb);
	if (err)
		goto err_switchdev_blocking_nb;

	return 0;

err_switchdev_blocking_nb:
	unregister_switchdev_notifier(&dsa_slave_switchdev_notifier);
err_switchdev_nb:
	unregister_netdevice_notifier(&dsa_slave_nb);
	return err;
}

void dsa_slave_unregister_notifier(void)
{
	struct notifier_block *nb;
	int err;

	nb = &dsa_slave_switchdev_blocking_notifier;
	err = unregister_switchdev_blocking_notifier(nb);
	if (err)
		pr_err("DSA: failed to unregister switchdev blocking notifier (%d)\n", err);

	err = unregister_switchdev_notifier(&dsa_slave_switchdev_notifier);
	if (err)
		pr_err("DSA: failed to unregister switchdev notifier (%d)\n", err);

	err = unregister_netdevice_notifier(&dsa_slave_nb);
	if (err)
		pr_err("DSA: failed to unregister slave notifier (%d)\n", err);
}
