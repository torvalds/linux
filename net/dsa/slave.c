/*
 * net/dsa/slave.c - Slave device handling
 * Copyright (c) 2008-2009 Marvell Semiconductor
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/list.h>
#include <linux/etherdevice.h>
#include <linux/netdevice.h>
#include <linux/phy.h>
#include <linux/phy_fixed.h>
#include <linux/of_net.h>
#include <linux/of_mdio.h>
#include <linux/mdio.h>
#include <net/rtnetlink.h>
#include <net/switchdev.h>
#include <linux/if_bridge.h>
#include <linux/netpoll.h>
#include "dsa_priv.h"

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
		 ds->dst->tree, ds->index);
	ds->slave_mii_bus->parent = ds->dev;
	ds->slave_mii_bus->phy_mask = ~ds->phys_mii_mask;
}


/* slave device handling ****************************************************/
static int dsa_slave_get_iflink(const struct net_device *dev)
{
	struct dsa_slave_priv *p = netdev_priv(dev);

	return p->parent->dst->master_netdev->ifindex;
}

static inline bool dsa_port_is_bridged(struct dsa_slave_priv *p)
{
	return !!p->bridge_dev;
}

static void dsa_port_set_stp_state(struct dsa_switch *ds, int port, u8 state)
{
	struct dsa_port *dp = &ds->ports[port];

	if (ds->ops->port_stp_state_set)
		ds->ops->port_stp_state_set(ds, port, state);

	if (ds->ops->port_fast_age) {
		/* Fast age FDB entries or flush appropriate forwarding database
		 * for the given port, if we are moving it from Learning or
		 * Forwarding state, to Disabled or Blocking or Listening state.
		 */

		if ((dp->stp_state == BR_STATE_LEARNING ||
		     dp->stp_state == BR_STATE_FORWARDING) &&
		    (state == BR_STATE_DISABLED ||
		     state == BR_STATE_BLOCKING ||
		     state == BR_STATE_LISTENING))
			ds->ops->port_fast_age(ds, port);
	}

	dp->stp_state = state;
}

static int dsa_slave_open(struct net_device *dev)
{
	struct dsa_slave_priv *p = netdev_priv(dev);
	struct net_device *master = p->parent->dst->master_netdev;
	struct dsa_switch *ds = p->parent;
	u8 stp_state = dsa_port_is_bridged(p) ?
			BR_STATE_BLOCKING : BR_STATE_FORWARDING;
	int err;

	if (!(master->flags & IFF_UP))
		return -ENETDOWN;

	if (!ether_addr_equal(dev->dev_addr, master->dev_addr)) {
		err = dev_uc_add(master, dev->dev_addr);
		if (err < 0)
			goto out;
	}

	if (dev->flags & IFF_ALLMULTI) {
		err = dev_set_allmulti(master, 1);
		if (err < 0)
			goto del_unicast;
	}
	if (dev->flags & IFF_PROMISC) {
		err = dev_set_promiscuity(master, 1);
		if (err < 0)
			goto clear_allmulti;
	}

	if (ds->ops->port_enable) {
		err = ds->ops->port_enable(ds, p->port, p->phy);
		if (err)
			goto clear_promisc;
	}

	dsa_port_set_stp_state(ds, p->port, stp_state);

	if (p->phy)
		phy_start(p->phy);

	return 0;

clear_promisc:
	if (dev->flags & IFF_PROMISC)
		dev_set_promiscuity(master, -1);
clear_allmulti:
	if (dev->flags & IFF_ALLMULTI)
		dev_set_allmulti(master, -1);
del_unicast:
	if (!ether_addr_equal(dev->dev_addr, master->dev_addr))
		dev_uc_del(master, dev->dev_addr);
out:
	return err;
}

static int dsa_slave_close(struct net_device *dev)
{
	struct dsa_slave_priv *p = netdev_priv(dev);
	struct net_device *master = p->parent->dst->master_netdev;
	struct dsa_switch *ds = p->parent;

	if (p->phy)
		phy_stop(p->phy);

	dev_mc_unsync(master, dev);
	dev_uc_unsync(master, dev);
	if (dev->flags & IFF_ALLMULTI)
		dev_set_allmulti(master, -1);
	if (dev->flags & IFF_PROMISC)
		dev_set_promiscuity(master, -1);

	if (!ether_addr_equal(dev->dev_addr, master->dev_addr))
		dev_uc_del(master, dev->dev_addr);

	if (ds->ops->port_disable)
		ds->ops->port_disable(ds, p->port, p->phy);

	dsa_port_set_stp_state(ds, p->port, BR_STATE_DISABLED);

	return 0;
}

static void dsa_slave_change_rx_flags(struct net_device *dev, int change)
{
	struct dsa_slave_priv *p = netdev_priv(dev);
	struct net_device *master = p->parent->dst->master_netdev;

	if (change & IFF_ALLMULTI)
		dev_set_allmulti(master, dev->flags & IFF_ALLMULTI ? 1 : -1);
	if (change & IFF_PROMISC)
		dev_set_promiscuity(master, dev->flags & IFF_PROMISC ? 1 : -1);
}

static void dsa_slave_set_rx_mode(struct net_device *dev)
{
	struct dsa_slave_priv *p = netdev_priv(dev);
	struct net_device *master = p->parent->dst->master_netdev;

	dev_mc_sync(master, dev);
	dev_uc_sync(master, dev);
}

static int dsa_slave_set_mac_address(struct net_device *dev, void *a)
{
	struct dsa_slave_priv *p = netdev_priv(dev);
	struct net_device *master = p->parent->dst->master_netdev;
	struct sockaddr *addr = a;
	int err;

	if (!is_valid_ether_addr(addr->sa_data))
		return -EADDRNOTAVAIL;

	if (!(dev->flags & IFF_UP))
		goto out;

	if (!ether_addr_equal(addr->sa_data, master->dev_addr)) {
		err = dev_uc_add(master, addr->sa_data);
		if (err < 0)
			return err;
	}

	if (!ether_addr_equal(dev->dev_addr, master->dev_addr))
		dev_uc_del(master, dev->dev_addr);

out:
	ether_addr_copy(dev->dev_addr, addr->sa_data);

	return 0;
}

static int dsa_slave_port_vlan_add(struct net_device *dev,
				   const struct switchdev_obj_port_vlan *vlan,
				   struct switchdev_trans *trans)
{
	struct dsa_slave_priv *p = netdev_priv(dev);
	struct dsa_switch *ds = p->parent;

	if (switchdev_trans_ph_prepare(trans)) {
		if (!ds->ops->port_vlan_prepare || !ds->ops->port_vlan_add)
			return -EOPNOTSUPP;

		return ds->ops->port_vlan_prepare(ds, p->port, vlan, trans);
	}

	ds->ops->port_vlan_add(ds, p->port, vlan, trans);

	return 0;
}

static int dsa_slave_port_vlan_del(struct net_device *dev,
				   const struct switchdev_obj_port_vlan *vlan)
{
	struct dsa_slave_priv *p = netdev_priv(dev);
	struct dsa_switch *ds = p->parent;

	if (!ds->ops->port_vlan_del)
		return -EOPNOTSUPP;

	return ds->ops->port_vlan_del(ds, p->port, vlan);
}

static int dsa_slave_port_vlan_dump(struct net_device *dev,
				    struct switchdev_obj_port_vlan *vlan,
				    switchdev_obj_dump_cb_t *cb)
{
	struct dsa_slave_priv *p = netdev_priv(dev);
	struct dsa_switch *ds = p->parent;

	if (ds->ops->port_vlan_dump)
		return ds->ops->port_vlan_dump(ds, p->port, vlan, cb);

	return -EOPNOTSUPP;
}

static int dsa_slave_port_fdb_add(struct net_device *dev,
				  const struct switchdev_obj_port_fdb *fdb,
				  struct switchdev_trans *trans)
{
	struct dsa_slave_priv *p = netdev_priv(dev);
	struct dsa_switch *ds = p->parent;

	if (switchdev_trans_ph_prepare(trans)) {
		if (!ds->ops->port_fdb_prepare || !ds->ops->port_fdb_add)
			return -EOPNOTSUPP;

		return ds->ops->port_fdb_prepare(ds, p->port, fdb, trans);
	}

	ds->ops->port_fdb_add(ds, p->port, fdb, trans);

	return 0;
}

static int dsa_slave_port_fdb_del(struct net_device *dev,
				  const struct switchdev_obj_port_fdb *fdb)
{
	struct dsa_slave_priv *p = netdev_priv(dev);
	struct dsa_switch *ds = p->parent;
	int ret = -EOPNOTSUPP;

	if (ds->ops->port_fdb_del)
		ret = ds->ops->port_fdb_del(ds, p->port, fdb);

	return ret;
}

static int dsa_slave_port_fdb_dump(struct net_device *dev,
				   struct switchdev_obj_port_fdb *fdb,
				   switchdev_obj_dump_cb_t *cb)
{
	struct dsa_slave_priv *p = netdev_priv(dev);
	struct dsa_switch *ds = p->parent;

	if (ds->ops->port_fdb_dump)
		return ds->ops->port_fdb_dump(ds, p->port, fdb, cb);

	return -EOPNOTSUPP;
}

static int dsa_slave_port_mdb_add(struct net_device *dev,
				  const struct switchdev_obj_port_mdb *mdb,
				  struct switchdev_trans *trans)
{
	struct dsa_slave_priv *p = netdev_priv(dev);
	struct dsa_switch *ds = p->parent;

	if (switchdev_trans_ph_prepare(trans)) {
		if (!ds->ops->port_mdb_prepare || !ds->ops->port_mdb_add)
			return -EOPNOTSUPP;

		return ds->ops->port_mdb_prepare(ds, p->port, mdb, trans);
	}

	ds->ops->port_mdb_add(ds, p->port, mdb, trans);

	return 0;
}

static int dsa_slave_port_mdb_del(struct net_device *dev,
				  const struct switchdev_obj_port_mdb *mdb)
{
	struct dsa_slave_priv *p = netdev_priv(dev);
	struct dsa_switch *ds = p->parent;

	if (ds->ops->port_mdb_del)
		return ds->ops->port_mdb_del(ds, p->port, mdb);

	return -EOPNOTSUPP;
}

static int dsa_slave_port_mdb_dump(struct net_device *dev,
				   struct switchdev_obj_port_mdb *mdb,
				   switchdev_obj_dump_cb_t *cb)
{
	struct dsa_slave_priv *p = netdev_priv(dev);
	struct dsa_switch *ds = p->parent;

	if (ds->ops->port_mdb_dump)
		return ds->ops->port_mdb_dump(ds, p->port, mdb, cb);

	return -EOPNOTSUPP;
}

static int dsa_slave_ioctl(struct net_device *dev, struct ifreq *ifr, int cmd)
{
	struct dsa_slave_priv *p = netdev_priv(dev);

	if (p->phy != NULL)
		return phy_mii_ioctl(p->phy, ifr, cmd);

	return -EOPNOTSUPP;
}

static int dsa_slave_stp_state_set(struct net_device *dev,
				   const struct switchdev_attr *attr,
				   struct switchdev_trans *trans)
{
	struct dsa_slave_priv *p = netdev_priv(dev);
	struct dsa_switch *ds = p->parent;

	if (switchdev_trans_ph_prepare(trans))
		return ds->ops->port_stp_state_set ? 0 : -EOPNOTSUPP;

	dsa_port_set_stp_state(ds, p->port, attr->u.stp_state);

	return 0;
}

static int dsa_slave_vlan_filtering(struct net_device *dev,
				    const struct switchdev_attr *attr,
				    struct switchdev_trans *trans)
{
	struct dsa_slave_priv *p = netdev_priv(dev);
	struct dsa_switch *ds = p->parent;

	/* bridge skips -EOPNOTSUPP, so skip the prepare phase */
	if (switchdev_trans_ph_prepare(trans))
		return 0;

	if (ds->ops->port_vlan_filtering)
		return ds->ops->port_vlan_filtering(ds, p->port,
						    attr->u.vlan_filtering);

	return 0;
}

static int dsa_fastest_ageing_time(struct dsa_switch *ds,
				   unsigned int ageing_time)
{
	int i;

	for (i = 0; i < DSA_MAX_PORTS; ++i) {
		struct dsa_port *dp = &ds->ports[i];

		if (dp && dp->ageing_time && dp->ageing_time < ageing_time)
			ageing_time = dp->ageing_time;
	}

	return ageing_time;
}

static int dsa_slave_ageing_time(struct net_device *dev,
				 const struct switchdev_attr *attr,
				 struct switchdev_trans *trans)
{
	struct dsa_slave_priv *p = netdev_priv(dev);
	struct dsa_switch *ds = p->parent;
	unsigned long ageing_jiffies = clock_t_to_jiffies(attr->u.ageing_time);
	unsigned int ageing_time = jiffies_to_msecs(ageing_jiffies);

	/* bridge skips -EOPNOTSUPP, so skip the prepare phase */
	if (switchdev_trans_ph_prepare(trans))
		return 0;

	/* Keep the fastest ageing time in case of multiple bridges */
	ds->ports[p->port].ageing_time = ageing_time;
	ageing_time = dsa_fastest_ageing_time(ds, ageing_time);

	if (ds->ops->set_ageing_time)
		return ds->ops->set_ageing_time(ds, ageing_time);

	return 0;
}

static int dsa_slave_port_attr_set(struct net_device *dev,
				   const struct switchdev_attr *attr,
				   struct switchdev_trans *trans)
{
	int ret;

	switch (attr->id) {
	case SWITCHDEV_ATTR_ID_PORT_STP_STATE:
		ret = dsa_slave_stp_state_set(dev, attr, trans);
		break;
	case SWITCHDEV_ATTR_ID_BRIDGE_VLAN_FILTERING:
		ret = dsa_slave_vlan_filtering(dev, attr, trans);
		break;
	case SWITCHDEV_ATTR_ID_BRIDGE_AGEING_TIME:
		ret = dsa_slave_ageing_time(dev, attr, trans);
		break;
	default:
		ret = -EOPNOTSUPP;
		break;
	}

	return ret;
}

static int dsa_slave_port_obj_add(struct net_device *dev,
				  const struct switchdev_obj *obj,
				  struct switchdev_trans *trans)
{
	int err;

	/* For the prepare phase, ensure the full set of changes is feasable in
	 * one go in order to signal a failure properly. If an operation is not
	 * supported, return -EOPNOTSUPP.
	 */

	switch (obj->id) {
	case SWITCHDEV_OBJ_ID_PORT_FDB:
		err = dsa_slave_port_fdb_add(dev,
					     SWITCHDEV_OBJ_PORT_FDB(obj),
					     trans);
		break;
	case SWITCHDEV_OBJ_ID_PORT_MDB:
		err = dsa_slave_port_mdb_add(dev, SWITCHDEV_OBJ_PORT_MDB(obj),
					     trans);
		break;
	case SWITCHDEV_OBJ_ID_PORT_VLAN:
		err = dsa_slave_port_vlan_add(dev,
					      SWITCHDEV_OBJ_PORT_VLAN(obj),
					      trans);
		break;
	default:
		err = -EOPNOTSUPP;
		break;
	}

	return err;
}

static int dsa_slave_port_obj_del(struct net_device *dev,
				  const struct switchdev_obj *obj)
{
	int err;

	switch (obj->id) {
	case SWITCHDEV_OBJ_ID_PORT_FDB:
		err = dsa_slave_port_fdb_del(dev,
					     SWITCHDEV_OBJ_PORT_FDB(obj));
		break;
	case SWITCHDEV_OBJ_ID_PORT_MDB:
		err = dsa_slave_port_mdb_del(dev, SWITCHDEV_OBJ_PORT_MDB(obj));
		break;
	case SWITCHDEV_OBJ_ID_PORT_VLAN:
		err = dsa_slave_port_vlan_del(dev,
					      SWITCHDEV_OBJ_PORT_VLAN(obj));
		break;
	default:
		err = -EOPNOTSUPP;
		break;
	}

	return err;
}

static int dsa_slave_port_obj_dump(struct net_device *dev,
				   struct switchdev_obj *obj,
				   switchdev_obj_dump_cb_t *cb)
{
	int err;

	switch (obj->id) {
	case SWITCHDEV_OBJ_ID_PORT_FDB:
		err = dsa_slave_port_fdb_dump(dev,
					      SWITCHDEV_OBJ_PORT_FDB(obj),
					      cb);
		break;
	case SWITCHDEV_OBJ_ID_PORT_MDB:
		err = dsa_slave_port_mdb_dump(dev, SWITCHDEV_OBJ_PORT_MDB(obj),
					      cb);
		break;
	case SWITCHDEV_OBJ_ID_PORT_VLAN:
		err = dsa_slave_port_vlan_dump(dev,
					       SWITCHDEV_OBJ_PORT_VLAN(obj),
					       cb);
		break;
	default:
		err = -EOPNOTSUPP;
		break;
	}

	return err;
}

static int dsa_slave_bridge_port_join(struct net_device *dev,
				      struct net_device *br)
{
	struct dsa_slave_priv *p = netdev_priv(dev);
	struct dsa_switch *ds = p->parent;
	int ret = -EOPNOTSUPP;

	p->bridge_dev = br;

	if (ds->ops->port_bridge_join)
		ret = ds->ops->port_bridge_join(ds, p->port, br);

	return ret == -EOPNOTSUPP ? 0 : ret;
}

static void dsa_slave_bridge_port_leave(struct net_device *dev)
{
	struct dsa_slave_priv *p = netdev_priv(dev);
	struct dsa_switch *ds = p->parent;


	if (ds->ops->port_bridge_leave)
		ds->ops->port_bridge_leave(ds, p->port);

	p->bridge_dev = NULL;

	/* Port left the bridge, put in BR_STATE_DISABLED by the bridge layer,
	 * so allow it to be in BR_STATE_FORWARDING to be kept functional
	 */
	dsa_port_set_stp_state(ds, p->port, BR_STATE_FORWARDING);
}

static int dsa_slave_port_attr_get(struct net_device *dev,
				   struct switchdev_attr *attr)
{
	struct dsa_slave_priv *p = netdev_priv(dev);
	struct dsa_switch *ds = p->parent;

	switch (attr->id) {
	case SWITCHDEV_ATTR_ID_PORT_PARENT_ID:
		attr->u.ppid.id_len = sizeof(ds->index);
		memcpy(&attr->u.ppid.id, &ds->index, attr->u.ppid.id_len);
		break;
	default:
		return -EOPNOTSUPP;
	}

	return 0;
}

static inline netdev_tx_t dsa_netpoll_send_skb(struct dsa_slave_priv *p,
					       struct sk_buff *skb)
{
#ifdef CONFIG_NET_POLL_CONTROLLER
	if (p->netpoll)
		netpoll_send_skb(p->netpoll, skb);
#else
	BUG();
#endif
	return NETDEV_TX_OK;
}

static netdev_tx_t dsa_slave_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct dsa_slave_priv *p = netdev_priv(dev);
	struct sk_buff *nskb;

	dev->stats.tx_packets++;
	dev->stats.tx_bytes += skb->len;

	/* Transmit function may have to reallocate the original SKB */
	nskb = p->xmit(skb, dev);
	if (!nskb)
		return NETDEV_TX_OK;

	/* SKB for netpoll still need to be mangled with the protocol-specific
	 * tag to be successfully transmitted
	 */
	if (unlikely(netpoll_tx_running(dev)))
		return dsa_netpoll_send_skb(p, nskb);

	/* Queue the SKB for transmission on the parent interface, but
	 * do not modify its EtherType
	 */
	nskb->dev = p->parent->dst->master_netdev;
	dev_queue_xmit(nskb);

	return NETDEV_TX_OK;
}

/* ethtool operations *******************************************************/
static int
dsa_slave_get_link_ksettings(struct net_device *dev,
			     struct ethtool_link_ksettings *cmd)
{
	struct dsa_slave_priv *p = netdev_priv(dev);
	int err;

	err = -EOPNOTSUPP;
	if (p->phy != NULL) {
		err = phy_read_status(p->phy);
		if (err == 0)
			err = phy_ethtool_ksettings_get(p->phy, cmd);
	}

	return err;
}

static int
dsa_slave_set_link_ksettings(struct net_device *dev,
			     const struct ethtool_link_ksettings *cmd)
{
	struct dsa_slave_priv *p = netdev_priv(dev);

	if (p->phy != NULL)
		return phy_ethtool_ksettings_set(p->phy, cmd);

	return -EOPNOTSUPP;
}

static void dsa_slave_get_drvinfo(struct net_device *dev,
				  struct ethtool_drvinfo *drvinfo)
{
	strlcpy(drvinfo->driver, "dsa", sizeof(drvinfo->driver));
	strlcpy(drvinfo->version, dsa_driver_version, sizeof(drvinfo->version));
	strlcpy(drvinfo->fw_version, "N/A", sizeof(drvinfo->fw_version));
	strlcpy(drvinfo->bus_info, "platform", sizeof(drvinfo->bus_info));
}

static int dsa_slave_get_regs_len(struct net_device *dev)
{
	struct dsa_slave_priv *p = netdev_priv(dev);
	struct dsa_switch *ds = p->parent;

	if (ds->ops->get_regs_len)
		return ds->ops->get_regs_len(ds, p->port);

	return -EOPNOTSUPP;
}

static void
dsa_slave_get_regs(struct net_device *dev, struct ethtool_regs *regs, void *_p)
{
	struct dsa_slave_priv *p = netdev_priv(dev);
	struct dsa_switch *ds = p->parent;

	if (ds->ops->get_regs)
		ds->ops->get_regs(ds, p->port, regs, _p);
}

static int dsa_slave_nway_reset(struct net_device *dev)
{
	struct dsa_slave_priv *p = netdev_priv(dev);

	if (p->phy != NULL)
		return genphy_restart_aneg(p->phy);

	return -EOPNOTSUPP;
}

static u32 dsa_slave_get_link(struct net_device *dev)
{
	struct dsa_slave_priv *p = netdev_priv(dev);

	if (p->phy != NULL) {
		genphy_update_link(p->phy);
		return p->phy->link;
	}

	return -EOPNOTSUPP;
}

static int dsa_slave_get_eeprom_len(struct net_device *dev)
{
	struct dsa_slave_priv *p = netdev_priv(dev);
	struct dsa_switch *ds = p->parent;

	if (ds->cd && ds->cd->eeprom_len)
		return ds->cd->eeprom_len;

	if (ds->ops->get_eeprom_len)
		return ds->ops->get_eeprom_len(ds);

	return 0;
}

static int dsa_slave_get_eeprom(struct net_device *dev,
				struct ethtool_eeprom *eeprom, u8 *data)
{
	struct dsa_slave_priv *p = netdev_priv(dev);
	struct dsa_switch *ds = p->parent;

	if (ds->ops->get_eeprom)
		return ds->ops->get_eeprom(ds, eeprom, data);

	return -EOPNOTSUPP;
}

static int dsa_slave_set_eeprom(struct net_device *dev,
				struct ethtool_eeprom *eeprom, u8 *data)
{
	struct dsa_slave_priv *p = netdev_priv(dev);
	struct dsa_switch *ds = p->parent;

	if (ds->ops->set_eeprom)
		return ds->ops->set_eeprom(ds, eeprom, data);

	return -EOPNOTSUPP;
}

static void dsa_slave_get_strings(struct net_device *dev,
				  uint32_t stringset, uint8_t *data)
{
	struct dsa_slave_priv *p = netdev_priv(dev);
	struct dsa_switch *ds = p->parent;

	if (stringset == ETH_SS_STATS) {
		int len = ETH_GSTRING_LEN;

		strncpy(data, "tx_packets", len);
		strncpy(data + len, "tx_bytes", len);
		strncpy(data + 2 * len, "rx_packets", len);
		strncpy(data + 3 * len, "rx_bytes", len);
		if (ds->ops->get_strings)
			ds->ops->get_strings(ds, p->port, data + 4 * len);
	}
}

static void dsa_cpu_port_get_ethtool_stats(struct net_device *dev,
					   struct ethtool_stats *stats,
					   uint64_t *data)
{
	struct dsa_switch_tree *dst = dev->dsa_ptr;
	struct dsa_switch *ds = dst->ds[0];
	s8 cpu_port = dst->cpu_port;
	int count = 0;

	if (dst->master_ethtool_ops.get_sset_count) {
		count = dst->master_ethtool_ops.get_sset_count(dev,
							       ETH_SS_STATS);
		dst->master_ethtool_ops.get_ethtool_stats(dev, stats, data);
	}

	if (ds->ops->get_ethtool_stats)
		ds->ops->get_ethtool_stats(ds, cpu_port, data + count);
}

static int dsa_cpu_port_get_sset_count(struct net_device *dev, int sset)
{
	struct dsa_switch_tree *dst = dev->dsa_ptr;
	struct dsa_switch *ds = dst->ds[0];
	int count = 0;

	if (dst->master_ethtool_ops.get_sset_count)
		count += dst->master_ethtool_ops.get_sset_count(dev, sset);

	if (sset == ETH_SS_STATS && ds->ops->get_sset_count)
		count += ds->ops->get_sset_count(ds);

	return count;
}

static void dsa_cpu_port_get_strings(struct net_device *dev,
				     uint32_t stringset, uint8_t *data)
{
	struct dsa_switch_tree *dst = dev->dsa_ptr;
	struct dsa_switch *ds = dst->ds[0];
	s8 cpu_port = dst->cpu_port;
	int len = ETH_GSTRING_LEN;
	int mcount = 0, count;
	unsigned int i;
	uint8_t pfx[4];
	uint8_t *ndata;

	snprintf(pfx, sizeof(pfx), "p%.2d", cpu_port);
	/* We do not want to be NULL-terminated, since this is a prefix */
	pfx[sizeof(pfx) - 1] = '_';

	if (dst->master_ethtool_ops.get_sset_count) {
		mcount = dst->master_ethtool_ops.get_sset_count(dev,
								ETH_SS_STATS);
		dst->master_ethtool_ops.get_strings(dev, stringset, data);
	}

	if (stringset == ETH_SS_STATS && ds->ops->get_strings) {
		ndata = data + mcount * len;
		/* This function copies ETH_GSTRINGS_LEN bytes, we will mangle
		 * the output after to prepend our CPU port prefix we
		 * constructed earlier
		 */
		ds->ops->get_strings(ds, cpu_port, ndata);
		count = ds->ops->get_sset_count(ds);
		for (i = 0; i < count; i++) {
			memmove(ndata + (i * len + sizeof(pfx)),
				ndata + i * len, len - sizeof(pfx));
			memcpy(ndata + i * len, pfx, sizeof(pfx));
		}
	}
}

static void dsa_slave_get_ethtool_stats(struct net_device *dev,
					struct ethtool_stats *stats,
					uint64_t *data)
{
	struct dsa_slave_priv *p = netdev_priv(dev);
	struct dsa_switch *ds = p->parent;

	data[0] = dev->stats.tx_packets;
	data[1] = dev->stats.tx_bytes;
	data[2] = dev->stats.rx_packets;
	data[3] = dev->stats.rx_bytes;
	if (ds->ops->get_ethtool_stats)
		ds->ops->get_ethtool_stats(ds, p->port, data + 4);
}

static int dsa_slave_get_sset_count(struct net_device *dev, int sset)
{
	struct dsa_slave_priv *p = netdev_priv(dev);
	struct dsa_switch *ds = p->parent;

	if (sset == ETH_SS_STATS) {
		int count;

		count = 4;
		if (ds->ops->get_sset_count)
			count += ds->ops->get_sset_count(ds);

		return count;
	}

	return -EOPNOTSUPP;
}

static void dsa_slave_get_wol(struct net_device *dev, struct ethtool_wolinfo *w)
{
	struct dsa_slave_priv *p = netdev_priv(dev);
	struct dsa_switch *ds = p->parent;

	if (ds->ops->get_wol)
		ds->ops->get_wol(ds, p->port, w);
}

static int dsa_slave_set_wol(struct net_device *dev, struct ethtool_wolinfo *w)
{
	struct dsa_slave_priv *p = netdev_priv(dev);
	struct dsa_switch *ds = p->parent;
	int ret = -EOPNOTSUPP;

	if (ds->ops->set_wol)
		ret = ds->ops->set_wol(ds, p->port, w);

	return ret;
}

static int dsa_slave_set_eee(struct net_device *dev, struct ethtool_eee *e)
{
	struct dsa_slave_priv *p = netdev_priv(dev);
	struct dsa_switch *ds = p->parent;
	int ret;

	if (!ds->ops->set_eee)
		return -EOPNOTSUPP;

	ret = ds->ops->set_eee(ds, p->port, p->phy, e);
	if (ret)
		return ret;

	if (p->phy)
		ret = phy_ethtool_set_eee(p->phy, e);

	return ret;
}

static int dsa_slave_get_eee(struct net_device *dev, struct ethtool_eee *e)
{
	struct dsa_slave_priv *p = netdev_priv(dev);
	struct dsa_switch *ds = p->parent;
	int ret;

	if (!ds->ops->get_eee)
		return -EOPNOTSUPP;

	ret = ds->ops->get_eee(ds, p->port, e);
	if (ret)
		return ret;

	if (p->phy)
		ret = phy_ethtool_get_eee(p->phy, e);

	return ret;
}

#ifdef CONFIG_NET_POLL_CONTROLLER
static int dsa_slave_netpoll_setup(struct net_device *dev,
				   struct netpoll_info *ni)
{
	struct dsa_slave_priv *p = netdev_priv(dev);
	struct dsa_switch *ds = p->parent;
	struct net_device *master = ds->dst->master_netdev;
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

	__netpoll_free_async(netpoll);
}

static void dsa_slave_poll_controller(struct net_device *dev)
{
}
#endif

void dsa_cpu_port_ethtool_init(struct ethtool_ops *ops)
{
	ops->get_sset_count = dsa_cpu_port_get_sset_count;
	ops->get_ethtool_stats = dsa_cpu_port_get_ethtool_stats;
	ops->get_strings = dsa_cpu_port_get_strings;
}

static const struct ethtool_ops dsa_slave_ethtool_ops = {
	.get_drvinfo		= dsa_slave_get_drvinfo,
	.get_regs_len		= dsa_slave_get_regs_len,
	.get_regs		= dsa_slave_get_regs,
	.nway_reset		= dsa_slave_nway_reset,
	.get_link		= dsa_slave_get_link,
	.get_eeprom_len		= dsa_slave_get_eeprom_len,
	.get_eeprom		= dsa_slave_get_eeprom,
	.set_eeprom		= dsa_slave_set_eeprom,
	.get_strings		= dsa_slave_get_strings,
	.get_ethtool_stats	= dsa_slave_get_ethtool_stats,
	.get_sset_count		= dsa_slave_get_sset_count,
	.set_wol		= dsa_slave_set_wol,
	.get_wol		= dsa_slave_get_wol,
	.set_eee		= dsa_slave_set_eee,
	.get_eee		= dsa_slave_get_eee,
	.get_link_ksettings	= dsa_slave_get_link_ksettings,
	.set_link_ksettings	= dsa_slave_set_link_ksettings,
};

static const struct net_device_ops dsa_slave_netdev_ops = {
	.ndo_open	 	= dsa_slave_open,
	.ndo_stop		= dsa_slave_close,
	.ndo_start_xmit		= dsa_slave_xmit,
	.ndo_change_rx_flags	= dsa_slave_change_rx_flags,
	.ndo_set_rx_mode	= dsa_slave_set_rx_mode,
	.ndo_set_mac_address	= dsa_slave_set_mac_address,
	.ndo_fdb_add		= switchdev_port_fdb_add,
	.ndo_fdb_del		= switchdev_port_fdb_del,
	.ndo_fdb_dump		= switchdev_port_fdb_dump,
	.ndo_do_ioctl		= dsa_slave_ioctl,
	.ndo_get_iflink		= dsa_slave_get_iflink,
#ifdef CONFIG_NET_POLL_CONTROLLER
	.ndo_netpoll_setup	= dsa_slave_netpoll_setup,
	.ndo_netpoll_cleanup	= dsa_slave_netpoll_cleanup,
	.ndo_poll_controller	= dsa_slave_poll_controller,
#endif
	.ndo_bridge_getlink	= switchdev_port_bridge_getlink,
	.ndo_bridge_setlink	= switchdev_port_bridge_setlink,
	.ndo_bridge_dellink	= switchdev_port_bridge_dellink,
};

static const struct switchdev_ops dsa_slave_switchdev_ops = {
	.switchdev_port_attr_get	= dsa_slave_port_attr_get,
	.switchdev_port_attr_set	= dsa_slave_port_attr_set,
	.switchdev_port_obj_add		= dsa_slave_port_obj_add,
	.switchdev_port_obj_del		= dsa_slave_port_obj_del,
	.switchdev_port_obj_dump	= dsa_slave_port_obj_dump,
};

static struct device_type dsa_type = {
	.name	= "dsa",
};

static void dsa_slave_adjust_link(struct net_device *dev)
{
	struct dsa_slave_priv *p = netdev_priv(dev);
	struct dsa_switch *ds = p->parent;
	unsigned int status_changed = 0;

	if (p->old_link != p->phy->link) {
		status_changed = 1;
		p->old_link = p->phy->link;
	}

	if (p->old_duplex != p->phy->duplex) {
		status_changed = 1;
		p->old_duplex = p->phy->duplex;
	}

	if (p->old_pause != p->phy->pause) {
		status_changed = 1;
		p->old_pause = p->phy->pause;
	}

	if (ds->ops->adjust_link && status_changed)
		ds->ops->adjust_link(ds, p->port, p->phy);

	if (status_changed)
		phy_print_status(p->phy);
}

static int dsa_slave_fixed_link_update(struct net_device *dev,
				       struct fixed_phy_status *status)
{
	struct dsa_slave_priv *p;
	struct dsa_switch *ds;

	if (dev) {
		p = netdev_priv(dev);
		ds = p->parent;
		if (ds->ops->fixed_link_update)
			ds->ops->fixed_link_update(ds, p->port, status);
	}

	return 0;
}

/* slave device setup *******************************************************/
static int dsa_slave_phy_connect(struct dsa_slave_priv *p,
				 struct net_device *slave_dev,
				 int addr)
{
	struct dsa_switch *ds = p->parent;

	p->phy = mdiobus_get_phy(ds->slave_mii_bus, addr);
	if (!p->phy) {
		netdev_err(slave_dev, "no phy at %d\n", addr);
		return -ENODEV;
	}

	/* Use already configured phy mode */
	if (p->phy_interface == PHY_INTERFACE_MODE_NA)
		p->phy_interface = p->phy->interface;
	phy_connect_direct(slave_dev, p->phy, dsa_slave_adjust_link,
			   p->phy_interface);

	return 0;
}

static int dsa_slave_phy_setup(struct dsa_slave_priv *p,
				struct net_device *slave_dev)
{
	struct dsa_switch *ds = p->parent;
	struct device_node *phy_dn, *port_dn;
	bool phy_is_fixed = false;
	u32 phy_flags = 0;
	int mode, ret;

	port_dn = ds->ports[p->port].dn;
	mode = of_get_phy_mode(port_dn);
	if (mode < 0)
		mode = PHY_INTERFACE_MODE_NA;
	p->phy_interface = mode;

	phy_dn = of_parse_phandle(port_dn, "phy-handle", 0);
	if (of_phy_is_fixed_link(port_dn)) {
		/* In the case of a fixed PHY, the DT node associated
		 * to the fixed PHY is the Port DT node
		 */
		ret = of_phy_register_fixed_link(port_dn);
		if (ret) {
			netdev_err(slave_dev, "failed to register fixed PHY: %d\n", ret);
			return ret;
		}
		phy_is_fixed = true;
		phy_dn = port_dn;
	}

	if (ds->ops->get_phy_flags)
		phy_flags = ds->ops->get_phy_flags(ds, p->port);

	if (phy_dn) {
		int phy_id = of_mdio_parse_addr(&slave_dev->dev, phy_dn);

		/* If this PHY address is part of phys_mii_mask, which means
		 * that we need to divert reads and writes to/from it, then we
		 * want to bind this device using the slave MII bus created by
		 * DSA to make that happen.
		 */
		if (!phy_is_fixed && phy_id >= 0 &&
		    (ds->phys_mii_mask & (1 << phy_id))) {
			ret = dsa_slave_phy_connect(p, slave_dev, phy_id);
			if (ret) {
				netdev_err(slave_dev, "failed to connect to phy%d: %d\n", phy_id, ret);
				return ret;
			}
		} else {
			p->phy = of_phy_connect(slave_dev, phy_dn,
						dsa_slave_adjust_link,
						phy_flags,
						p->phy_interface);
		}
	}

	if (p->phy && phy_is_fixed)
		fixed_phy_set_link_update(p->phy, dsa_slave_fixed_link_update);

	/* We could not connect to a designated PHY, so use the switch internal
	 * MDIO bus instead
	 */
	if (!p->phy) {
		ret = dsa_slave_phy_connect(p, slave_dev, p->port);
		if (ret) {
			netdev_err(slave_dev, "failed to connect to port %d: %d\n", p->port, ret);
			return ret;
		}
	}

	phy_attached_info(p->phy);

	return 0;
}

static struct lock_class_key dsa_slave_netdev_xmit_lock_key;
static void dsa_slave_set_lockdep_class_one(struct net_device *dev,
					    struct netdev_queue *txq,
					    void *_unused)
{
	lockdep_set_class(&txq->_xmit_lock,
			  &dsa_slave_netdev_xmit_lock_key);
}

int dsa_slave_suspend(struct net_device *slave_dev)
{
	struct dsa_slave_priv *p = netdev_priv(slave_dev);

	if (p->phy) {
		phy_stop(p->phy);
		p->old_pause = -1;
		p->old_link = -1;
		p->old_duplex = -1;
		phy_suspend(p->phy);
	}

	return 0;
}

int dsa_slave_resume(struct net_device *slave_dev)
{
	struct dsa_slave_priv *p = netdev_priv(slave_dev);

	netif_device_attach(slave_dev);

	if (p->phy) {
		phy_resume(p->phy);
		phy_start(p->phy);
	}

	return 0;
}

int dsa_slave_create(struct dsa_switch *ds, struct device *parent,
		     int port, const char *name)
{
	struct dsa_switch_tree *dst = ds->dst;
	struct net_device *master;
	struct net_device *slave_dev;
	struct dsa_slave_priv *p;
	int ret;

	master = ds->dst->master_netdev;
	if (ds->master_netdev)
		master = ds->master_netdev;

	slave_dev = alloc_netdev(sizeof(struct dsa_slave_priv), name,
				 NET_NAME_UNKNOWN, ether_setup);
	if (slave_dev == NULL)
		return -ENOMEM;

	slave_dev->features = master->vlan_features;
	slave_dev->ethtool_ops = &dsa_slave_ethtool_ops;
	eth_hw_addr_inherit(slave_dev, master);
	slave_dev->priv_flags |= IFF_NO_QUEUE;
	slave_dev->netdev_ops = &dsa_slave_netdev_ops;
	slave_dev->switchdev_ops = &dsa_slave_switchdev_ops;
	slave_dev->min_mtu = 0;
	slave_dev->max_mtu = ETH_MAX_MTU;
	SET_NETDEV_DEVTYPE(slave_dev, &dsa_type);

	netdev_for_each_tx_queue(slave_dev, dsa_slave_set_lockdep_class_one,
				 NULL);

	SET_NETDEV_DEV(slave_dev, parent);
	slave_dev->dev.of_node = ds->ports[port].dn;
	slave_dev->vlan_features = master->vlan_features;

	p = netdev_priv(slave_dev);
	p->parent = ds;
	p->port = port;
	p->xmit = dst->tag_ops->xmit;

	p->old_pause = -1;
	p->old_link = -1;
	p->old_duplex = -1;

	ds->ports[port].netdev = slave_dev;
	ret = register_netdev(slave_dev);
	if (ret) {
		netdev_err(master, "error %d registering interface %s\n",
			   ret, slave_dev->name);
		ds->ports[port].netdev = NULL;
		free_netdev(slave_dev);
		return ret;
	}

	netif_carrier_off(slave_dev);

	ret = dsa_slave_phy_setup(p, slave_dev);
	if (ret) {
		netdev_err(master, "error %d setting up slave phy\n", ret);
		unregister_netdev(slave_dev);
		free_netdev(slave_dev);
		return ret;
	}

	return 0;
}

void dsa_slave_destroy(struct net_device *slave_dev)
{
	struct dsa_slave_priv *p = netdev_priv(slave_dev);

	netif_carrier_off(slave_dev);
	if (p->phy)
		phy_disconnect(p->phy);
	unregister_netdev(slave_dev);
	free_netdev(slave_dev);
}

static bool dsa_slave_dev_check(struct net_device *dev)
{
	return dev->netdev_ops == &dsa_slave_netdev_ops;
}

static int dsa_slave_port_upper_event(struct net_device *dev,
				      unsigned long event, void *ptr)
{
	struct netdev_notifier_changeupper_info *info = ptr;
	struct net_device *upper = info->upper_dev;
	int err = 0;

	switch (event) {
	case NETDEV_CHANGEUPPER:
		if (netif_is_bridge_master(upper)) {
			if (info->linking)
				err = dsa_slave_bridge_port_join(dev, upper);
			else
				dsa_slave_bridge_port_leave(dev);
		}

		break;
	}

	return notifier_from_errno(err);
}

static int dsa_slave_port_event(struct net_device *dev, unsigned long event,
				void *ptr)
{
	switch (event) {
	case NETDEV_CHANGEUPPER:
		return dsa_slave_port_upper_event(dev, event, ptr);
	}

	return NOTIFY_DONE;
}

int dsa_slave_netdevice_event(struct notifier_block *unused,
			      unsigned long event, void *ptr)
{
	struct net_device *dev = netdev_notifier_info_to_dev(ptr);

	if (dsa_slave_dev_check(dev))
		return dsa_slave_port_event(dev, event, ptr);

	return NOTIFY_DONE;
}
