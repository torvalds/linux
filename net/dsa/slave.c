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
#include <linux/list.h>
#include <net/rtnetlink.h>
#include <net/pkt_cls.h>
#include <net/tc_act/tc_mirred.h>
#include <linux/if_bridge.h>
#include <linux/netpoll.h>

#include "dsa_priv.h"

static bool dsa_slave_dev_check(struct net_device *dev);

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

	return p->dp->ds->dst->master_netdev->ifindex;
}

static int dsa_slave_open(struct net_device *dev)
{
	struct dsa_slave_priv *p = netdev_priv(dev);
	struct dsa_port *dp = p->dp;
	struct dsa_switch *ds = dp->ds;
	struct net_device *master = ds->dst->master_netdev;
	u8 stp_state = dp->bridge_dev ? BR_STATE_BLOCKING : BR_STATE_FORWARDING;
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
		err = ds->ops->port_enable(ds, p->dp->index, p->phy);
		if (err)
			goto clear_promisc;
	}

	dsa_port_set_state_now(p->dp, stp_state);

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
	struct net_device *master = p->dp->ds->dst->master_netdev;
	struct dsa_switch *ds = p->dp->ds;

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
		ds->ops->port_disable(ds, p->dp->index, p->phy);

	dsa_port_set_state_now(p->dp, BR_STATE_DISABLED);

	return 0;
}

static void dsa_slave_change_rx_flags(struct net_device *dev, int change)
{
	struct dsa_slave_priv *p = netdev_priv(dev);
	struct net_device *master = p->dp->ds->dst->master_netdev;

	if (change & IFF_ALLMULTI)
		dev_set_allmulti(master, dev->flags & IFF_ALLMULTI ? 1 : -1);
	if (change & IFF_PROMISC)
		dev_set_promiscuity(master, dev->flags & IFF_PROMISC ? 1 : -1);
}

static void dsa_slave_set_rx_mode(struct net_device *dev)
{
	struct dsa_slave_priv *p = netdev_priv(dev);
	struct net_device *master = p->dp->ds->dst->master_netdev;

	dev_mc_sync(master, dev);
	dev_uc_sync(master, dev);
}

static int dsa_slave_set_mac_address(struct net_device *dev, void *a)
{
	struct dsa_slave_priv *p = netdev_priv(dev);
	struct net_device *master = p->dp->ds->dst->master_netdev;
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

static int dsa_slave_ioctl(struct net_device *dev, struct ifreq *ifr, int cmd)
{
	struct dsa_slave_priv *p = netdev_priv(dev);

	if (p->phy != NULL)
		return phy_mii_ioctl(p->phy, ifr, cmd);

	return -EOPNOTSUPP;
}

static int dsa_slave_port_attr_set(struct net_device *dev,
				   const struct switchdev_attr *attr,
				   struct switchdev_trans *trans)
{
	struct dsa_slave_priv *p = netdev_priv(dev);
	struct dsa_port *dp = p->dp;
	int ret;

	switch (attr->id) {
	case SWITCHDEV_ATTR_ID_PORT_STP_STATE:
		ret = dsa_port_set_state(dp, attr->u.stp_state, trans);
		break;
	case SWITCHDEV_ATTR_ID_BRIDGE_VLAN_FILTERING:
		ret = dsa_port_vlan_filtering(dp, attr->u.vlan_filtering,
					      trans);
		break;
	case SWITCHDEV_ATTR_ID_BRIDGE_AGEING_TIME:
		ret = dsa_port_ageing_time(dp, attr->u.ageing_time, trans);
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
	struct dsa_slave_priv *p = netdev_priv(dev);
	struct dsa_port *dp = p->dp;
	int err;

	/* For the prepare phase, ensure the full set of changes is feasable in
	 * one go in order to signal a failure properly. If an operation is not
	 * supported, return -EOPNOTSUPP.
	 */

	switch (obj->id) {
	case SWITCHDEV_OBJ_ID_PORT_FDB:
		err = dsa_port_fdb_add(dp, SWITCHDEV_OBJ_PORT_FDB(obj), trans);
		break;
	case SWITCHDEV_OBJ_ID_PORT_MDB:
		err = dsa_port_mdb_add(dp, SWITCHDEV_OBJ_PORT_MDB(obj), trans);
		break;
	case SWITCHDEV_OBJ_ID_PORT_VLAN:
		err = dsa_port_vlan_add(dp, SWITCHDEV_OBJ_PORT_VLAN(obj),
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
	struct dsa_slave_priv *p = netdev_priv(dev);
	struct dsa_port *dp = p->dp;
	int err;

	switch (obj->id) {
	case SWITCHDEV_OBJ_ID_PORT_FDB:
		err = dsa_port_fdb_del(dp, SWITCHDEV_OBJ_PORT_FDB(obj));
		break;
	case SWITCHDEV_OBJ_ID_PORT_MDB:
		err = dsa_port_mdb_del(dp, SWITCHDEV_OBJ_PORT_MDB(obj));
		break;
	case SWITCHDEV_OBJ_ID_PORT_VLAN:
		err = dsa_port_vlan_del(dp, SWITCHDEV_OBJ_PORT_VLAN(obj));
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
	struct dsa_slave_priv *p = netdev_priv(dev);
	struct dsa_port *dp = p->dp;
	int err;

	switch (obj->id) {
	case SWITCHDEV_OBJ_ID_PORT_FDB:
		err = dsa_port_fdb_dump(dp, SWITCHDEV_OBJ_PORT_FDB(obj), cb);
		break;
	case SWITCHDEV_OBJ_ID_PORT_MDB:
		err = dsa_port_mdb_dump(dp, SWITCHDEV_OBJ_PORT_MDB(obj), cb);
		break;
	case SWITCHDEV_OBJ_ID_PORT_VLAN:
		err = dsa_port_vlan_dump(dp, SWITCHDEV_OBJ_PORT_VLAN(obj), cb);
		break;
	default:
		err = -EOPNOTSUPP;
		break;
	}

	return err;
}

static int dsa_slave_port_attr_get(struct net_device *dev,
				   struct switchdev_attr *attr)
{
	struct dsa_slave_priv *p = netdev_priv(dev);
	struct dsa_switch *ds = p->dp->ds;

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
	nskb->dev = p->dp->ds->dst->master_netdev;
	dev_queue_xmit(nskb);

	return NETDEV_TX_OK;
}

/* ethtool operations *******************************************************/
static int
dsa_slave_get_link_ksettings(struct net_device *dev,
			     struct ethtool_link_ksettings *cmd)
{
	struct dsa_slave_priv *p = netdev_priv(dev);
	int err = -EOPNOTSUPP;

	if (p->phy != NULL)
		err = phy_ethtool_ksettings_get(p->phy, cmd);

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
	strlcpy(drvinfo->fw_version, "N/A", sizeof(drvinfo->fw_version));
	strlcpy(drvinfo->bus_info, "platform", sizeof(drvinfo->bus_info));
}

static int dsa_slave_get_regs_len(struct net_device *dev)
{
	struct dsa_slave_priv *p = netdev_priv(dev);
	struct dsa_switch *ds = p->dp->ds;

	if (ds->ops->get_regs_len)
		return ds->ops->get_regs_len(ds, p->dp->index);

	return -EOPNOTSUPP;
}

static void
dsa_slave_get_regs(struct net_device *dev, struct ethtool_regs *regs, void *_p)
{
	struct dsa_slave_priv *p = netdev_priv(dev);
	struct dsa_switch *ds = p->dp->ds;

	if (ds->ops->get_regs)
		ds->ops->get_regs(ds, p->dp->index, regs, _p);
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
	struct dsa_switch *ds = p->dp->ds;

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
	struct dsa_switch *ds = p->dp->ds;

	if (ds->ops->get_eeprom)
		return ds->ops->get_eeprom(ds, eeprom, data);

	return -EOPNOTSUPP;
}

static int dsa_slave_set_eeprom(struct net_device *dev,
				struct ethtool_eeprom *eeprom, u8 *data)
{
	struct dsa_slave_priv *p = netdev_priv(dev);
	struct dsa_switch *ds = p->dp->ds;

	if (ds->ops->set_eeprom)
		return ds->ops->set_eeprom(ds, eeprom, data);

	return -EOPNOTSUPP;
}

static void dsa_slave_get_strings(struct net_device *dev,
				  uint32_t stringset, uint8_t *data)
{
	struct dsa_slave_priv *p = netdev_priv(dev);
	struct dsa_switch *ds = p->dp->ds;

	if (stringset == ETH_SS_STATS) {
		int len = ETH_GSTRING_LEN;

		strncpy(data, "tx_packets", len);
		strncpy(data + len, "tx_bytes", len);
		strncpy(data + 2 * len, "rx_packets", len);
		strncpy(data + 3 * len, "rx_bytes", len);
		if (ds->ops->get_strings)
			ds->ops->get_strings(ds, p->dp->index, data + 4 * len);
	}
}

static void dsa_cpu_port_get_ethtool_stats(struct net_device *dev,
					   struct ethtool_stats *stats,
					   uint64_t *data)
{
	struct dsa_switch_tree *dst = dev->dsa_ptr;
	struct dsa_switch *ds = dst->cpu_dp->ds;
	s8 cpu_port = dst->cpu_dp->index;
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
	struct dsa_switch *ds = dst->cpu_dp->ds;
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
	struct dsa_switch *ds = dst->cpu_dp->ds;
	s8 cpu_port = dst->cpu_dp->index;
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
	struct dsa_switch *ds = p->dp->ds;

	data[0] = dev->stats.tx_packets;
	data[1] = dev->stats.tx_bytes;
	data[2] = dev->stats.rx_packets;
	data[3] = dev->stats.rx_bytes;
	if (ds->ops->get_ethtool_stats)
		ds->ops->get_ethtool_stats(ds, p->dp->index, data + 4);
}

static int dsa_slave_get_sset_count(struct net_device *dev, int sset)
{
	struct dsa_slave_priv *p = netdev_priv(dev);
	struct dsa_switch *ds = p->dp->ds;

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
	struct dsa_switch *ds = p->dp->ds;

	if (ds->ops->get_wol)
		ds->ops->get_wol(ds, p->dp->index, w);
}

static int dsa_slave_set_wol(struct net_device *dev, struct ethtool_wolinfo *w)
{
	struct dsa_slave_priv *p = netdev_priv(dev);
	struct dsa_switch *ds = p->dp->ds;
	int ret = -EOPNOTSUPP;

	if (ds->ops->set_wol)
		ret = ds->ops->set_wol(ds, p->dp->index, w);

	return ret;
}

static int dsa_slave_set_eee(struct net_device *dev, struct ethtool_eee *e)
{
	struct dsa_slave_priv *p = netdev_priv(dev);
	struct dsa_switch *ds = p->dp->ds;
	int ret;

	if (!ds->ops->set_eee)
		return -EOPNOTSUPP;

	ret = ds->ops->set_eee(ds, p->dp->index, p->phy, e);
	if (ret)
		return ret;

	if (p->phy)
		ret = phy_ethtool_set_eee(p->phy, e);

	return ret;
}

static int dsa_slave_get_eee(struct net_device *dev, struct ethtool_eee *e)
{
	struct dsa_slave_priv *p = netdev_priv(dev);
	struct dsa_switch *ds = p->dp->ds;
	int ret;

	if (!ds->ops->get_eee)
		return -EOPNOTSUPP;

	ret = ds->ops->get_eee(ds, p->dp->index, e);
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
	struct dsa_switch *ds = p->dp->ds;
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

static int dsa_slave_get_phys_port_name(struct net_device *dev,
					char *name, size_t len)
{
	struct dsa_slave_priv *p = netdev_priv(dev);

	if (snprintf(name, len, "p%d", p->dp->index) >= len)
		return -EINVAL;

	return 0;
}

static struct dsa_mall_tc_entry *
dsa_slave_mall_tc_entry_find(struct dsa_slave_priv *p,
			     unsigned long cookie)
{
	struct dsa_mall_tc_entry *mall_tc_entry;

	list_for_each_entry(mall_tc_entry, &p->mall_tc_list, list)
		if (mall_tc_entry->cookie == cookie)
			return mall_tc_entry;

	return NULL;
}

static int dsa_slave_add_cls_matchall(struct net_device *dev,
				      __be16 protocol,
				      struct tc_cls_matchall_offload *cls,
				      bool ingress)
{
	struct dsa_slave_priv *p = netdev_priv(dev);
	struct dsa_mall_tc_entry *mall_tc_entry;
	struct dsa_switch *ds = p->dp->ds;
	struct net *net = dev_net(dev);
	struct dsa_slave_priv *to_p;
	struct net_device *to_dev;
	const struct tc_action *a;
	int err = -EOPNOTSUPP;
	LIST_HEAD(actions);
	int ifindex;

	if (!ds->ops->port_mirror_add)
		return err;

	if (!tc_single_action(cls->exts))
		return err;

	tcf_exts_to_list(cls->exts, &actions);
	a = list_first_entry(&actions, struct tc_action, list);

	if (is_tcf_mirred_egress_mirror(a) && protocol == htons(ETH_P_ALL)) {
		struct dsa_mall_mirror_tc_entry *mirror;

		ifindex = tcf_mirred_ifindex(a);
		to_dev = __dev_get_by_index(net, ifindex);
		if (!to_dev)
			return -EINVAL;

		if (!dsa_slave_dev_check(to_dev))
			return -EOPNOTSUPP;

		mall_tc_entry = kzalloc(sizeof(*mall_tc_entry), GFP_KERNEL);
		if (!mall_tc_entry)
			return -ENOMEM;

		mall_tc_entry->cookie = cls->cookie;
		mall_tc_entry->type = DSA_PORT_MALL_MIRROR;
		mirror = &mall_tc_entry->mirror;

		to_p = netdev_priv(to_dev);

		mirror->to_local_port = to_p->dp->index;
		mirror->ingress = ingress;

		err = ds->ops->port_mirror_add(ds, p->dp->index, mirror,
					       ingress);
		if (err) {
			kfree(mall_tc_entry);
			return err;
		}

		list_add_tail(&mall_tc_entry->list, &p->mall_tc_list);
	}

	return 0;
}

static void dsa_slave_del_cls_matchall(struct net_device *dev,
				       struct tc_cls_matchall_offload *cls)
{
	struct dsa_slave_priv *p = netdev_priv(dev);
	struct dsa_mall_tc_entry *mall_tc_entry;
	struct dsa_switch *ds = p->dp->ds;

	if (!ds->ops->port_mirror_del)
		return;

	mall_tc_entry = dsa_slave_mall_tc_entry_find(p, cls->cookie);
	if (!mall_tc_entry)
		return;

	list_del(&mall_tc_entry->list);

	switch (mall_tc_entry->type) {
	case DSA_PORT_MALL_MIRROR:
		ds->ops->port_mirror_del(ds, p->dp->index,
					 &mall_tc_entry->mirror);
		break;
	default:
		WARN_ON(1);
	}

	kfree(mall_tc_entry);
}

static int dsa_slave_setup_tc(struct net_device *dev, u32 handle,
			      __be16 protocol, struct tc_to_netdev *tc)
{
	bool ingress = TC_H_MAJ(handle) == TC_H_MAJ(TC_H_INGRESS);
	int ret = -EOPNOTSUPP;

	switch (tc->type) {
	case TC_SETUP_MATCHALL:
		switch (tc->cls_mall->command) {
		case TC_CLSMATCHALL_REPLACE:
			return dsa_slave_add_cls_matchall(dev, protocol,
							  tc->cls_mall,
							  ingress);
		case TC_CLSMATCHALL_DESTROY:
			dsa_slave_del_cls_matchall(dev, tc->cls_mall);
			return 0;
		}
	default:
		break;
	}

	return ret;
}

void dsa_cpu_port_ethtool_init(struct ethtool_ops *ops)
{
	ops->get_sset_count = dsa_cpu_port_get_sset_count;
	ops->get_ethtool_stats = dsa_cpu_port_get_ethtool_stats;
	ops->get_strings = dsa_cpu_port_get_strings;
}

static int dsa_slave_get_rxnfc(struct net_device *dev,
			       struct ethtool_rxnfc *nfc, u32 *rule_locs)
{
	struct dsa_slave_priv *p = netdev_priv(dev);
	struct dsa_switch *ds = p->dp->ds;

	if (!ds->ops->get_rxnfc)
		return -EOPNOTSUPP;

	return ds->ops->get_rxnfc(ds, p->dp->index, nfc, rule_locs);
}

static int dsa_slave_set_rxnfc(struct net_device *dev,
			       struct ethtool_rxnfc *nfc)
{
	struct dsa_slave_priv *p = netdev_priv(dev);
	struct dsa_switch *ds = p->dp->ds;

	if (!ds->ops->set_rxnfc)
		return -EOPNOTSUPP;

	return ds->ops->set_rxnfc(ds, p->dp->index, nfc);
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
	.get_rxnfc		= dsa_slave_get_rxnfc,
	.set_rxnfc		= dsa_slave_set_rxnfc,
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
	.ndo_get_phys_port_name	= dsa_slave_get_phys_port_name,
	.ndo_setup_tc		= dsa_slave_setup_tc,
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
	struct dsa_switch *ds = p->dp->ds;
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
		ds->ops->adjust_link(ds, p->dp->index, p->phy);

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
		ds = p->dp->ds;
		if (ds->ops->fixed_link_update)
			ds->ops->fixed_link_update(ds, p->dp->index, status);
	}

	return 0;
}

/* slave device setup *******************************************************/
static int dsa_slave_phy_connect(struct dsa_slave_priv *p,
				 struct net_device *slave_dev,
				 int addr)
{
	struct dsa_switch *ds = p->dp->ds;

	p->phy = mdiobus_get_phy(ds->slave_mii_bus, addr);
	if (!p->phy) {
		netdev_err(slave_dev, "no phy at %d\n", addr);
		return -ENODEV;
	}

	/* Use already configured phy mode */
	if (p->phy_interface == PHY_INTERFACE_MODE_NA)
		p->phy_interface = p->phy->interface;
	return phy_connect_direct(slave_dev, p->phy, dsa_slave_adjust_link,
				  p->phy_interface);
}

static int dsa_slave_phy_setup(struct dsa_slave_priv *p,
				struct net_device *slave_dev)
{
	struct dsa_switch *ds = p->dp->ds;
	struct device_node *phy_dn, *port_dn;
	bool phy_is_fixed = false;
	u32 phy_flags = 0;
	int mode, ret;

	port_dn = p->dp->dn;
	mode = of_get_phy_mode(port_dn);
	if (mode < 0)
		mode = PHY_INTERFACE_MODE_NA;
	p->phy_interface = mode;

	phy_dn = of_parse_phandle(port_dn, "phy-handle", 0);
	if (!phy_dn && of_phy_is_fixed_link(port_dn)) {
		/* In the case of a fixed PHY, the DT node associated
		 * to the fixed PHY is the Port DT node
		 */
		ret = of_phy_register_fixed_link(port_dn);
		if (ret) {
			netdev_err(slave_dev, "failed to register fixed PHY: %d\n", ret);
			return ret;
		}
		phy_is_fixed = true;
		phy_dn = of_node_get(port_dn);
	}

	if (ds->ops->get_phy_flags)
		phy_flags = ds->ops->get_phy_flags(ds, p->dp->index);

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
				of_node_put(phy_dn);
				return ret;
			}
		} else {
			p->phy = of_phy_connect(slave_dev, phy_dn,
						dsa_slave_adjust_link,
						phy_flags,
						p->phy_interface);
		}

		of_node_put(phy_dn);
	}

	if (p->phy && phy_is_fixed)
		fixed_phy_set_link_update(p->phy, dsa_slave_fixed_link_update);

	/* We could not connect to a designated PHY, so use the switch internal
	 * MDIO bus instead
	 */
	if (!p->phy) {
		ret = dsa_slave_phy_connect(p, slave_dev, p->dp->index);
		if (ret) {
			netdev_err(slave_dev, "failed to connect to port %d: %d\n",
				   p->dp->index, ret);
			if (phy_is_fixed)
				of_phy_deregister_fixed_link(port_dn);
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

	netif_device_detach(slave_dev);

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

	slave_dev->features = master->vlan_features | NETIF_F_HW_TC;
	slave_dev->hw_features |= NETIF_F_HW_TC;
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
	p->dp = &ds->ports[port];
	INIT_LIST_HEAD(&p->mall_tc_list);
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
	struct device_node *port_dn;

	port_dn = p->dp->dn;

	netif_carrier_off(slave_dev);
	if (p->phy) {
		phy_disconnect(p->phy);

		if (of_phy_is_fixed_link(port_dn))
			of_phy_deregister_fixed_link(port_dn);
	}
	unregister_netdev(slave_dev);
	free_netdev(slave_dev);
}

static bool dsa_slave_dev_check(struct net_device *dev)
{
	return dev->netdev_ops == &dsa_slave_netdev_ops;
}

static int dsa_slave_changeupper(struct net_device *dev,
				 struct netdev_notifier_changeupper_info *info)
{
	struct dsa_slave_priv *p = netdev_priv(dev);
	struct dsa_port *dp = p->dp;
	int err = NOTIFY_DONE;

	if (netif_is_bridge_master(info->upper_dev)) {
		if (info->linking) {
			err = dsa_port_bridge_join(dp, info->upper_dev);
			err = notifier_from_errno(err);
		} else {
			dsa_port_bridge_leave(dp, info->upper_dev);
			err = NOTIFY_OK;
		}
	}

	return err;
}

static int dsa_slave_netdevice_event(struct notifier_block *nb,
				     unsigned long event, void *ptr)
{
	struct net_device *dev = netdev_notifier_info_to_dev(ptr);

	if (dev->netdev_ops != &dsa_slave_netdev_ops)
		return NOTIFY_DONE;

	if (event == NETDEV_CHANGEUPPER)
		return dsa_slave_changeupper(dev, ptr);

	return NOTIFY_DONE;
}

static struct notifier_block dsa_slave_nb __read_mostly = {
	.notifier_call	= dsa_slave_netdevice_event,
};

int dsa_slave_register_notifier(void)
{
	return register_netdevice_notifier(&dsa_slave_nb);
}

void dsa_slave_unregister_notifier(void)
{
	int err;

	err = unregister_netdevice_notifier(&dsa_slave_nb);
	if (err)
		pr_err("DSA: failed to unregister slave notifier (%d)\n", err);
}
