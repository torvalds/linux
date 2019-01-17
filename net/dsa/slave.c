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
#include <linux/phylink.h>
#include <linux/of_net.h>
#include <linux/of_mdio.h>
#include <linux/mdio.h>
#include <net/rtnetlink.h>
#include <net/pkt_cls.h>
#include <net/tc_act/tc_mirred.h>
#include <linux/if_bridge.h>
#include <linux/netpoll.h>
#include <linux/ptp_classify.h>

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

	err = dsa_port_enable(dp, dev->phydev);
	if (err)
		goto clear_promisc;

	phylink_start(dp->pl);

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
	struct net_device *master = dsa_slave_to_master(dev);
	struct dsa_port *dp = dsa_slave_to_port(dev);

	phylink_stop(dp->pl);

	dsa_port_disable(dp, dev->phydev);

	dev_mc_unsync(master, dev);
	dev_uc_unsync(master, dev);
	if (dev->flags & IFF_ALLMULTI)
		dev_set_allmulti(master, -1);
	if (dev->flags & IFF_PROMISC)
		dev_set_promiscuity(master, -1);

	if (!ether_addr_equal(dev->dev_addr, master->dev_addr))
		dev_uc_del(master, dev->dev_addr);

	return 0;
}

static void dsa_slave_change_rx_flags(struct net_device *dev, int change)
{
	struct net_device *master = dsa_slave_to_master(dev);

	if (change & IFF_ALLMULTI)
		dev_set_allmulti(master, dev->flags & IFF_ALLMULTI ? 1 : -1);
	if (change & IFF_PROMISC)
		dev_set_promiscuity(master, dev->flags & IFF_PROMISC ? 1 : -1);
}

static void dsa_slave_set_rx_mode(struct net_device *dev)
{
	struct net_device *master = dsa_slave_to_master(dev);

	dev_mc_sync(master, dev);
	dev_uc_sync(master, dev);
}

static int dsa_slave_set_mac_address(struct net_device *dev, void *a)
{
	struct net_device *master = dsa_slave_to_master(dev);
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

static int dsa_slave_port_attr_set(struct net_device *dev,
				   const struct switchdev_attr *attr,
				   struct switchdev_trans *trans)
{
	struct dsa_port *dp = dsa_slave_to_port(dev);
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
	struct dsa_port *dp = dsa_slave_to_port(dev);
	int err;

	/* For the prepare phase, ensure the full set of changes is feasable in
	 * one go in order to signal a failure properly. If an operation is not
	 * supported, return -EOPNOTSUPP.
	 */

	switch (obj->id) {
	case SWITCHDEV_OBJ_ID_PORT_MDB:
		err = dsa_port_mdb_add(dp, SWITCHDEV_OBJ_PORT_MDB(obj), trans);
		break;
	case SWITCHDEV_OBJ_ID_HOST_MDB:
		/* DSA can directly translate this to a normal MDB add,
		 * but on the CPU port.
		 */
		err = dsa_port_mdb_add(dp->cpu_dp, SWITCHDEV_OBJ_PORT_MDB(obj),
				       trans);
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
	struct dsa_port *dp = dsa_slave_to_port(dev);
	int err;

	switch (obj->id) {
	case SWITCHDEV_OBJ_ID_PORT_MDB:
		err = dsa_port_mdb_del(dp, SWITCHDEV_OBJ_PORT_MDB(obj));
		break;
	case SWITCHDEV_OBJ_ID_HOST_MDB:
		/* DSA can directly translate this to a normal MDB add,
		 * but on the CPU port.
		 */
		err = dsa_port_mdb_del(dp->cpu_dp, SWITCHDEV_OBJ_PORT_MDB(obj));
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

static int dsa_slave_port_attr_get(struct net_device *dev,
				   struct switchdev_attr *attr)
{
	struct dsa_port *dp = dsa_slave_to_port(dev);
	struct dsa_switch *ds = dp->ds;
	struct dsa_switch_tree *dst = ds->dst;

	switch (attr->id) {
	case SWITCHDEV_ATTR_ID_PORT_PARENT_ID:
		attr->u.ppid.id_len = sizeof(dst->index);
		memcpy(&attr->u.ppid.id, &dst->index, attr->u.ppid.id_len);
		break;
	case SWITCHDEV_ATTR_ID_PORT_BRIDGE_FLAGS_SUPPORT:
		attr->u.brport_flags_support = 0;
		break;
	default:
		return -EOPNOTSUPP;
	}

	return 0;
}

static inline netdev_tx_t dsa_slave_netpoll_send_skb(struct net_device *dev,
						     struct sk_buff *skb)
{
#ifdef CONFIG_NET_POLL_CONTROLLER
	struct dsa_slave_priv *p = netdev_priv(dev);

	if (p->netpoll)
		netpoll_send_skb(p->netpoll, skb);
#else
	BUG();
#endif
	return NETDEV_TX_OK;
}

static void dsa_skb_tx_timestamp(struct dsa_slave_priv *p,
				 struct sk_buff *skb)
{
	struct dsa_switch *ds = p->dp->ds;
	struct sk_buff *clone;
	unsigned int type;

	type = ptp_classify_raw(skb);
	if (type == PTP_CLASS_NONE)
		return;

	if (!ds->ops->port_txtstamp)
		return;

	clone = skb_clone_sk(skb);
	if (!clone)
		return;

	if (ds->ops->port_txtstamp(ds, p->dp->index, clone, type))
		return;

	kfree_skb(clone);
}

static netdev_tx_t dsa_slave_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct dsa_slave_priv *p = netdev_priv(dev);
	struct pcpu_sw_netstats *s;
	struct sk_buff *nskb;

	s = this_cpu_ptr(p->stats64);
	u64_stats_update_begin(&s->syncp);
	s->tx_packets++;
	s->tx_bytes += skb->len;
	u64_stats_update_end(&s->syncp);

	/* Identify PTP protocol packets, clone them, and pass them to the
	 * switch driver
	 */
	dsa_skb_tx_timestamp(p, skb);

	/* Transmit function may have to reallocate the original SKB,
	 * in which case it must have freed it. Only free it here on error.
	 */
	nskb = p->xmit(skb, dev);
	if (!nskb) {
		kfree_skb(skb);
		return NETDEV_TX_OK;
	}

	/* SKB for netpoll still need to be mangled with the protocol-specific
	 * tag to be successfully transmitted
	 */
	if (unlikely(netpoll_tx_running(dev)))
		return dsa_slave_netpoll_send_skb(dev, nskb);

	/* Queue the SKB for transmission on the parent interface, but
	 * do not modify its EtherType
	 */
	nskb->dev = dsa_slave_to_master(dev);
	dev_queue_xmit(nskb);

	return NETDEV_TX_OK;
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
	}
}

static void dsa_slave_get_ethtool_stats(struct net_device *dev,
					struct ethtool_stats *stats,
					uint64_t *data)
{
	struct dsa_port *dp = dsa_slave_to_port(dev);
	struct dsa_slave_priv *p = netdev_priv(dev);
	struct dsa_switch *ds = dp->ds;
	struct pcpu_sw_netstats *s;
	unsigned int start;
	int i;

	for_each_possible_cpu(i) {
		u64 tx_packets, tx_bytes, rx_packets, rx_bytes;

		s = per_cpu_ptr(p->stats64, i);
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
		int count;

		count = 4;
		if (ds->ops->get_sset_count)
			count += ds->ops->get_sset_count(ds, dp->index, sset);

		return count;
	}

	return -EOPNOTSUPP;
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
	if (!dev->phydev && !dp->pl)
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
	if (!dev->phydev && !dp->pl)
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

static int dsa_slave_get_phys_port_name(struct net_device *dev,
					char *name, size_t len)
{
	struct dsa_port *dp = dsa_slave_to_port(dev);

	if (snprintf(name, len, "p%d", dp->index) >= len)
		return -EINVAL;

	return 0;
}

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

static int dsa_slave_add_cls_matchall(struct net_device *dev,
				      struct tc_cls_matchall_offload *cls,
				      bool ingress)
{
	struct dsa_port *dp = dsa_slave_to_port(dev);
	struct dsa_slave_priv *p = netdev_priv(dev);
	struct dsa_mall_tc_entry *mall_tc_entry;
	__be16 protocol = cls->common.protocol;
	struct dsa_switch *ds = dp->ds;
	struct net_device *to_dev;
	const struct tc_action *a;
	struct dsa_port *to_dp;
	int err = -EOPNOTSUPP;

	if (!ds->ops->port_mirror_add)
		return err;

	if (!tcf_exts_has_one_action(cls->exts))
		return err;

	a = tcf_exts_first_action(cls->exts);

	if (is_tcf_mirred_egress_mirror(a) && protocol == htons(ETH_P_ALL)) {
		struct dsa_mall_mirror_tc_entry *mirror;

		to_dev = tcf_mirred_dev(a);
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

		to_dp = dsa_slave_to_port(to_dev);

		mirror->to_local_port = to_dp->index;
		mirror->ingress = ingress;

		err = ds->ops->port_mirror_add(ds, dp->index, mirror, ingress);
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
	struct dsa_port *dp = dsa_slave_to_port(dev);
	struct dsa_mall_tc_entry *mall_tc_entry;
	struct dsa_switch *ds = dp->ds;

	if (!ds->ops->port_mirror_del)
		return;

	mall_tc_entry = dsa_slave_mall_tc_entry_find(dev, cls->cookie);
	if (!mall_tc_entry)
		return;

	list_del(&mall_tc_entry->list);

	switch (mall_tc_entry->type) {
	case DSA_PORT_MALL_MIRROR:
		ds->ops->port_mirror_del(ds, dp->index, &mall_tc_entry->mirror);
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

static int dsa_slave_setup_tc_block_cb(enum tc_setup_type type, void *type_data,
				       void *cb_priv, bool ingress)
{
	struct net_device *dev = cb_priv;

	if (!tc_can_offload(dev))
		return -EOPNOTSUPP;

	switch (type) {
	case TC_SETUP_CLSMATCHALL:
		return dsa_slave_setup_tc_cls_matchall(dev, type_data, ingress);
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

static int dsa_slave_setup_tc_block(struct net_device *dev,
				    struct tc_block_offload *f)
{
	tc_setup_cb_t *cb;

	if (f->binder_type == TCF_BLOCK_BINDER_TYPE_CLSACT_INGRESS)
		cb = dsa_slave_setup_tc_block_cb_ig;
	else if (f->binder_type == TCF_BLOCK_BINDER_TYPE_CLSACT_EGRESS)
		cb = dsa_slave_setup_tc_block_cb_eg;
	else
		return -EOPNOTSUPP;

	switch (f->command) {
	case TC_BLOCK_BIND:
		return tcf_block_cb_register(f->block, cb, dev, dev, f->extack);
	case TC_BLOCK_UNBIND:
		tcf_block_cb_unregister(f->block, cb, dev);
		return 0;
	default:
		return -EOPNOTSUPP;
	}
}

static int dsa_slave_setup_tc(struct net_device *dev, enum tc_setup_type type,
			      void *type_data)
{
	switch (type) {
	case TC_SETUP_BLOCK:
		return dsa_slave_setup_tc_block(dev, type_data);
	default:
		return -EOPNOTSUPP;
	}
}

static void dsa_slave_get_stats64(struct net_device *dev,
				  struct rtnl_link_stats64 *stats)
{
	struct dsa_slave_priv *p = netdev_priv(dev);
	struct pcpu_sw_netstats *s;
	unsigned int start;
	int i;

	netdev_stats_to_stats64(stats, &dev->stats);
	for_each_possible_cpu(i) {
		u64 tx_packets, tx_bytes, rx_packets, rx_bytes;

		s = per_cpu_ptr(p->stats64, i);
		do {
			start = u64_stats_fetch_begin_irq(&s->syncp);
			tx_packets = s->tx_packets;
			tx_bytes = s->tx_bytes;
			rx_packets = s->rx_packets;
			rx_bytes = s->rx_bytes;
		} while (u64_stats_fetch_retry_irq(&s->syncp, start));

		stats->tx_packets += tx_packets;
		stats->tx_bytes += tx_bytes;
		stats->rx_packets += rx_packets;
		stats->rx_bytes += rx_bytes;
	}
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
	.set_wol		= dsa_slave_set_wol,
	.get_wol		= dsa_slave_get_wol,
	.set_eee		= dsa_slave_set_eee,
	.get_eee		= dsa_slave_get_eee,
	.get_link_ksettings	= dsa_slave_get_link_ksettings,
	.set_link_ksettings	= dsa_slave_set_link_ksettings,
	.get_rxnfc		= dsa_slave_get_rxnfc,
	.set_rxnfc		= dsa_slave_set_rxnfc,
	.get_ts_info		= dsa_slave_get_ts_info,
};

/* legacy way, bypassing the bridge *****************************************/
int dsa_legacy_fdb_add(struct ndmsg *ndm, struct nlattr *tb[],
		       struct net_device *dev,
		       const unsigned char *addr, u16 vid,
		       u16 flags,
		       struct netlink_ext_ack *extack)
{
	struct dsa_port *dp = dsa_slave_to_port(dev);

	return dsa_port_fdb_add(dp, addr, vid);
}

int dsa_legacy_fdb_del(struct ndmsg *ndm, struct nlattr *tb[],
		       struct net_device *dev,
		       const unsigned char *addr, u16 vid)
{
	struct dsa_port *dp = dsa_slave_to_port(dev);

	return dsa_port_fdb_del(dp, addr, vid);
}

static const struct net_device_ops dsa_slave_netdev_ops = {
	.ndo_open	 	= dsa_slave_open,
	.ndo_stop		= dsa_slave_close,
	.ndo_start_xmit		= dsa_slave_xmit,
	.ndo_change_rx_flags	= dsa_slave_change_rx_flags,
	.ndo_set_rx_mode	= dsa_slave_set_rx_mode,
	.ndo_set_mac_address	= dsa_slave_set_mac_address,
	.ndo_fdb_add		= dsa_legacy_fdb_add,
	.ndo_fdb_del		= dsa_legacy_fdb_del,
	.ndo_fdb_dump		= dsa_slave_fdb_dump,
	.ndo_do_ioctl		= dsa_slave_ioctl,
	.ndo_get_iflink		= dsa_slave_get_iflink,
#ifdef CONFIG_NET_POLL_CONTROLLER
	.ndo_netpoll_setup	= dsa_slave_netpoll_setup,
	.ndo_netpoll_cleanup	= dsa_slave_netpoll_cleanup,
	.ndo_poll_controller	= dsa_slave_poll_controller,
#endif
	.ndo_get_phys_port_name	= dsa_slave_get_phys_port_name,
	.ndo_setup_tc		= dsa_slave_setup_tc,
	.ndo_get_stats64	= dsa_slave_get_stats64,
};

static const struct switchdev_ops dsa_slave_switchdev_ops = {
	.switchdev_port_attr_get	= dsa_slave_port_attr_get,
	.switchdev_port_attr_set	= dsa_slave_port_attr_set,
};

static struct device_type dsa_type = {
	.name	= "dsa",
};

static void dsa_slave_phylink_validate(struct net_device *dev,
				       unsigned long *supported,
				       struct phylink_link_state *state)
{
	struct dsa_port *dp = dsa_slave_to_port(dev);
	struct dsa_switch *ds = dp->ds;

	if (!ds->ops->phylink_validate)
		return;

	ds->ops->phylink_validate(ds, dp->index, supported, state);
}

static int dsa_slave_phylink_mac_link_state(struct net_device *dev,
					    struct phylink_link_state *state)
{
	struct dsa_port *dp = dsa_slave_to_port(dev);
	struct dsa_switch *ds = dp->ds;

	/* Only called for SGMII and 802.3z */
	if (!ds->ops->phylink_mac_link_state)
		return -EOPNOTSUPP;

	return ds->ops->phylink_mac_link_state(ds, dp->index, state);
}

static void dsa_slave_phylink_mac_config(struct net_device *dev,
					 unsigned int mode,
					 const struct phylink_link_state *state)
{
	struct dsa_port *dp = dsa_slave_to_port(dev);
	struct dsa_switch *ds = dp->ds;

	if (!ds->ops->phylink_mac_config)
		return;

	ds->ops->phylink_mac_config(ds, dp->index, mode, state);
}

static void dsa_slave_phylink_mac_an_restart(struct net_device *dev)
{
	struct dsa_port *dp = dsa_slave_to_port(dev);
	struct dsa_switch *ds = dp->ds;

	if (!ds->ops->phylink_mac_an_restart)
		return;

	ds->ops->phylink_mac_an_restart(ds, dp->index);
}

static void dsa_slave_phylink_mac_link_down(struct net_device *dev,
					    unsigned int mode,
					    phy_interface_t interface)
{
	struct dsa_port *dp = dsa_slave_to_port(dev);
	struct dsa_switch *ds = dp->ds;

	if (!ds->ops->phylink_mac_link_down) {
		if (ds->ops->adjust_link && dev->phydev)
			ds->ops->adjust_link(ds, dp->index, dev->phydev);
		return;
	}

	ds->ops->phylink_mac_link_down(ds, dp->index, mode, interface);
}

static void dsa_slave_phylink_mac_link_up(struct net_device *dev,
					  unsigned int mode,
					  phy_interface_t interface,
					  struct phy_device *phydev)
{
	struct dsa_port *dp = dsa_slave_to_port(dev);
	struct dsa_switch *ds = dp->ds;

	if (!ds->ops->phylink_mac_link_up) {
		if (ds->ops->adjust_link && dev->phydev)
			ds->ops->adjust_link(ds, dp->index, dev->phydev);
		return;
	}

	ds->ops->phylink_mac_link_up(ds, dp->index, mode, interface, phydev);
}

static const struct phylink_mac_ops dsa_slave_phylink_mac_ops = {
	.validate = dsa_slave_phylink_validate,
	.mac_link_state = dsa_slave_phylink_mac_link_state,
	.mac_config = dsa_slave_phylink_mac_config,
	.mac_an_restart = dsa_slave_phylink_mac_an_restart,
	.mac_link_down = dsa_slave_phylink_mac_link_down,
	.mac_link_up = dsa_slave_phylink_mac_link_up,
};

void dsa_port_phylink_mac_change(struct dsa_switch *ds, int port, bool up)
{
	const struct dsa_port *dp = dsa_to_port(ds, port);

	phylink_mac_change(dp->pl, up);
}
EXPORT_SYMBOL_GPL(dsa_port_phylink_mac_change);

static void dsa_slave_phylink_fixed_state(struct net_device *dev,
					  struct phylink_link_state *state)
{
	struct dsa_port *dp = dsa_slave_to_port(dev);
	struct dsa_switch *ds = dp->ds;

	/* No need to check that this operation is valid, the callback would
	 * not be called if it was not.
	 */
	ds->ops->phylink_fixed_state(ds, dp->index, state);
}

/* slave device setup *******************************************************/
static int dsa_slave_phy_connect(struct net_device *slave_dev, int addr)
{
	struct dsa_port *dp = dsa_slave_to_port(slave_dev);
	struct dsa_switch *ds = dp->ds;

	slave_dev->phydev = mdiobus_get_phy(ds->slave_mii_bus, addr);
	if (!slave_dev->phydev) {
		netdev_err(slave_dev, "no phy at %d\n", addr);
		return -ENODEV;
	}

	return phylink_connect_phy(dp->pl, slave_dev->phydev);
}

static int dsa_slave_phy_setup(struct net_device *slave_dev)
{
	struct dsa_port *dp = dsa_slave_to_port(slave_dev);
	struct device_node *port_dn = dp->dn;
	struct dsa_switch *ds = dp->ds;
	u32 phy_flags = 0;
	int mode, ret;

	mode = of_get_phy_mode(port_dn);
	if (mode < 0)
		mode = PHY_INTERFACE_MODE_NA;

	dp->pl = phylink_create(slave_dev, of_fwnode_handle(port_dn), mode,
				&dsa_slave_phylink_mac_ops);
	if (IS_ERR(dp->pl)) {
		netdev_err(slave_dev,
			   "error creating PHYLINK: %ld\n", PTR_ERR(dp->pl));
		return PTR_ERR(dp->pl);
	}

	/* Register only if the switch provides such a callback, since this
	 * callback takes precedence over polling the link GPIO in PHYLINK
	 * (see phylink_get_fixed_state).
	 */
	if (ds->ops->phylink_fixed_state)
		phylink_fixed_state_cb(dp->pl, dsa_slave_phylink_fixed_state);

	if (ds->ops->get_phy_flags)
		phy_flags = ds->ops->get_phy_flags(ds, dp->index);

	ret = phylink_of_phy_connect(dp->pl, port_dn, phy_flags);
	if (ret == -ENODEV) {
		/* We could not connect to a designated PHY or SFP, so use the
		 * switch internal MDIO bus instead
		 */
		ret = dsa_slave_phy_connect(slave_dev, dp->index);
		if (ret) {
			netdev_err(slave_dev,
				   "failed to connect to port %d: %d\n",
				   dp->index, ret);
			phylink_destroy(dp->pl);
			return ret;
		}
	}

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

static void dsa_slave_notify(struct net_device *dev, unsigned long val)
{
	struct net_device *master = dsa_slave_to_master(dev);
	struct dsa_port *dp = dsa_slave_to_port(dev);
	struct dsa_notifier_register_info rinfo = {
		.switch_number = dp->ds->index,
		.port_number = dp->index,
		.master = master,
		.info.dev = dev,
	};

	call_dsa_notifiers(val, dev, &rinfo.info);
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

	SET_NETDEV_DEV(slave_dev, port->ds->dev);
	slave_dev->dev.of_node = port->dn;
	slave_dev->vlan_features = master->vlan_features;

	p = netdev_priv(slave_dev);
	p->stats64 = netdev_alloc_pcpu_stats(struct pcpu_sw_netstats);
	if (!p->stats64) {
		free_netdev(slave_dev);
		return -ENOMEM;
	}
	p->dp = port;
	INIT_LIST_HEAD(&p->mall_tc_list);
	p->xmit = cpu_dp->tag_ops->xmit;
	port->slave = slave_dev;

	netif_carrier_off(slave_dev);

	ret = dsa_slave_phy_setup(slave_dev);
	if (ret) {
		netdev_err(master, "error %d setting up slave phy\n", ret);
		goto out_free;
	}

	dsa_slave_notify(slave_dev, DSA_PORT_REGISTER);

	ret = register_netdev(slave_dev);
	if (ret) {
		netdev_err(master, "error %d registering interface %s\n",
			   ret, slave_dev->name);
		goto out_phy;
	}

	return 0;

out_phy:
	rtnl_lock();
	phylink_disconnect_phy(p->dp->pl);
	rtnl_unlock();
	phylink_destroy(p->dp->pl);
out_free:
	free_percpu(p->stats64);
	free_netdev(slave_dev);
	port->slave = NULL;
	return ret;
}

void dsa_slave_destroy(struct net_device *slave_dev)
{
	struct dsa_port *dp = dsa_slave_to_port(slave_dev);
	struct dsa_slave_priv *p = netdev_priv(slave_dev);

	netif_carrier_off(slave_dev);
	rtnl_lock();
	phylink_disconnect_phy(dp->pl);
	rtnl_unlock();

	dsa_slave_notify(slave_dev, DSA_PORT_UNREGISTER);
	unregister_netdev(slave_dev);
	phylink_destroy(dp->pl);
	free_percpu(p->stats64);
	free_netdev(slave_dev);
}

static bool dsa_slave_dev_check(struct net_device *dev)
{
	return dev->netdev_ops == &dsa_slave_netdev_ops;
}

static int dsa_slave_changeupper(struct net_device *dev,
				 struct netdev_notifier_changeupper_info *info)
{
	struct dsa_port *dp = dsa_slave_to_port(dev);
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

	if (!dsa_slave_dev_check(dev))
		return NOTIFY_DONE;

	if (event == NETDEV_CHANGEUPPER)
		return dsa_slave_changeupper(dev, ptr);

	return NOTIFY_DONE;
}

struct dsa_switchdev_event_work {
	struct work_struct work;
	struct switchdev_notifier_fdb_info fdb_info;
	struct net_device *dev;
	unsigned long event;
};

static void dsa_slave_switchdev_event_work(struct work_struct *work)
{
	struct dsa_switchdev_event_work *switchdev_work =
		container_of(work, struct dsa_switchdev_event_work, work);
	struct net_device *dev = switchdev_work->dev;
	struct switchdev_notifier_fdb_info *fdb_info;
	struct dsa_port *dp = dsa_slave_to_port(dev);
	int err;

	rtnl_lock();
	switch (switchdev_work->event) {
	case SWITCHDEV_FDB_ADD_TO_DEVICE:
		fdb_info = &switchdev_work->fdb_info;
		if (!fdb_info->added_by_user)
			break;

		err = dsa_port_fdb_add(dp, fdb_info->addr, fdb_info->vid);
		if (err) {
			netdev_dbg(dev, "fdb add failed err=%d\n", err);
			break;
		}
		fdb_info->offloaded = true;
		call_switchdev_notifiers(SWITCHDEV_FDB_OFFLOADED, dev,
					 &fdb_info->info, NULL);
		break;

	case SWITCHDEV_FDB_DEL_TO_DEVICE:
		fdb_info = &switchdev_work->fdb_info;
		if (!fdb_info->added_by_user)
			break;

		err = dsa_port_fdb_del(dp, fdb_info->addr, fdb_info->vid);
		if (err) {
			netdev_dbg(dev, "fdb del failed err=%d\n", err);
			dev_close(dev);
		}
		break;
	}
	rtnl_unlock();

	kfree(switchdev_work->fdb_info.addr);
	kfree(switchdev_work);
	dev_put(dev);
}

static int
dsa_slave_switchdev_fdb_work_init(struct dsa_switchdev_event_work *
				  switchdev_work,
				  const struct switchdev_notifier_fdb_info *
				  fdb_info)
{
	memcpy(&switchdev_work->fdb_info, fdb_info,
	       sizeof(switchdev_work->fdb_info));
	switchdev_work->fdb_info.addr = kzalloc(ETH_ALEN, GFP_ATOMIC);
	if (!switchdev_work->fdb_info.addr)
		return -ENOMEM;
	ether_addr_copy((u8 *)switchdev_work->fdb_info.addr,
			fdb_info->addr);
	return 0;
}

/* Called under rcu_read_lock() */
static int dsa_slave_switchdev_event(struct notifier_block *unused,
				     unsigned long event, void *ptr)
{
	struct net_device *dev = switchdev_notifier_info_to_dev(ptr);
	struct dsa_switchdev_event_work *switchdev_work;

	if (!dsa_slave_dev_check(dev))
		return NOTIFY_DONE;

	switchdev_work = kzalloc(sizeof(*switchdev_work), GFP_ATOMIC);
	if (!switchdev_work)
		return NOTIFY_BAD;

	INIT_WORK(&switchdev_work->work,
		  dsa_slave_switchdev_event_work);
	switchdev_work->dev = dev;
	switchdev_work->event = event;

	switch (event) {
	case SWITCHDEV_FDB_ADD_TO_DEVICE: /* fall through */
	case SWITCHDEV_FDB_DEL_TO_DEVICE:
		if (dsa_slave_switchdev_fdb_work_init(switchdev_work, ptr))
			goto err_fdb_work_init;
		dev_hold(dev);
		break;
	default:
		kfree(switchdev_work);
		return NOTIFY_DONE;
	}

	dsa_schedule_work(&switchdev_work->work);
	return NOTIFY_OK;

err_fdb_work_init:
	kfree(switchdev_work);
	return NOTIFY_BAD;
}

static int
dsa_slave_switchdev_port_obj_event(unsigned long event,
			struct net_device *netdev,
			struct switchdev_notifier_port_obj_info *port_obj_info)
{
	int err = -EOPNOTSUPP;

	switch (event) {
	case SWITCHDEV_PORT_OBJ_ADD:
		err = dsa_slave_port_obj_add(netdev, port_obj_info->obj,
					     port_obj_info->trans);
		break;
	case SWITCHDEV_PORT_OBJ_DEL:
		err = dsa_slave_port_obj_del(netdev, port_obj_info->obj);
		break;
	}

	port_obj_info->handled = true;
	return notifier_from_errno(err);
}

static int dsa_slave_switchdev_blocking_event(struct notifier_block *unused,
					      unsigned long event, void *ptr)
{
	struct net_device *dev = switchdev_notifier_info_to_dev(ptr);

	if (!dsa_slave_dev_check(dev))
		return NOTIFY_DONE;

	switch (event) {
	case SWITCHDEV_PORT_OBJ_ADD: /* fall through */
	case SWITCHDEV_PORT_OBJ_DEL:
		return dsa_slave_switchdev_port_obj_event(event, dev, ptr);
	}

	return NOTIFY_DONE;
}

static struct notifier_block dsa_slave_nb __read_mostly = {
	.notifier_call  = dsa_slave_netdevice_event,
};

static struct notifier_block dsa_slave_switchdev_notifier = {
	.notifier_call = dsa_slave_switchdev_event,
};

static struct notifier_block dsa_slave_switchdev_blocking_notifier = {
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
