/*
 * net/dsa/slave.c - Slave device handling
 * Copyright (c) 2008 Marvell Semiconductor
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/list.h>
#include <linux/netdevice.h>
#include <linux/phy.h>
#include "dsa_priv.h"

/* slave mii_bus handling ***************************************************/
static int dsa_slave_phy_read(struct mii_bus *bus, int addr, int reg)
{
	struct dsa_switch *ds = bus->priv;

	if (ds->valid_port_mask & (1 << addr))
		return ds->drv->phy_read(ds, addr, reg);

	return 0xffff;
}

static int dsa_slave_phy_write(struct mii_bus *bus, int addr, int reg, u16 val)
{
	struct dsa_switch *ds = bus->priv;

	if (ds->valid_port_mask & (1 << addr))
		return ds->drv->phy_write(ds, addr, reg, val);

	return 0;
}

void dsa_slave_mii_bus_init(struct dsa_switch *ds)
{
	ds->slave_mii_bus->priv = (void *)ds;
	ds->slave_mii_bus->name = "dsa slave smi";
	ds->slave_mii_bus->read = dsa_slave_phy_read;
	ds->slave_mii_bus->write = dsa_slave_phy_write;
	snprintf(ds->slave_mii_bus->id, MII_BUS_ID_SIZE, "%s:%.2x",
			ds->master_mii_bus->id, ds->pd->sw_addr);
	ds->slave_mii_bus->parent = &(ds->master_mii_bus->dev);
}


/* slave device handling ****************************************************/
static int dsa_slave_open(struct net_device *dev)
{
	return 0;
}

static int dsa_slave_close(struct net_device *dev)
{
	return 0;
}

static void dsa_slave_change_rx_flags(struct net_device *dev, int change)
{
	struct dsa_slave_priv *p = netdev_priv(dev);
	struct net_device *master = p->parent->master_netdev;

	if (change & IFF_ALLMULTI)
		dev_set_allmulti(master, dev->flags & IFF_ALLMULTI ? 1 : -1);
	if (change & IFF_PROMISC)
		dev_set_promiscuity(master, dev->flags & IFF_PROMISC ? 1 : -1);
}

static void dsa_slave_set_rx_mode(struct net_device *dev)
{
	struct dsa_slave_priv *p = netdev_priv(dev);
	struct net_device *master = p->parent->master_netdev;

	dev_mc_sync(master, dev);
	dev_unicast_sync(master, dev);
}

static int dsa_slave_set_mac_address(struct net_device *dev, void *addr)
{
	memcpy(dev->dev_addr, addr + 2, 6);

	return 0;
}

static int dsa_slave_ioctl(struct net_device *dev, struct ifreq *ifr, int cmd)
{
	struct dsa_slave_priv *p = netdev_priv(dev);
	struct mii_ioctl_data *mii_data = if_mii(ifr);

	if (p->phy != NULL)
		return phy_mii_ioctl(p->phy, mii_data, cmd);

	return -EOPNOTSUPP;
}


/* ethtool operations *******************************************************/
static int
dsa_slave_get_settings(struct net_device *dev, struct ethtool_cmd *cmd)
{
	struct dsa_slave_priv *p = netdev_priv(dev);
	int err;

	err = -EOPNOTSUPP;
	if (p->phy != NULL) {
		err = phy_read_status(p->phy);
		if (err == 0)
			err = phy_ethtool_gset(p->phy, cmd);
	}

	return err;
}

static int
dsa_slave_set_settings(struct net_device *dev, struct ethtool_cmd *cmd)
{
	struct dsa_slave_priv *p = netdev_priv(dev);

	if (p->phy != NULL)
		return phy_ethtool_sset(p->phy, cmd);

	return -EOPNOTSUPP;
}

static void dsa_slave_get_drvinfo(struct net_device *dev,
				  struct ethtool_drvinfo *drvinfo)
{
	strncpy(drvinfo->driver, "dsa", 32);
	strncpy(drvinfo->version, dsa_driver_version, 32);
	strncpy(drvinfo->fw_version, "N/A", 32);
	strncpy(drvinfo->bus_info, "platform", 32);
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
		if (ds->drv->get_strings != NULL)
			ds->drv->get_strings(ds, p->port, data + 4 * len);
	}
}

static void dsa_slave_get_ethtool_stats(struct net_device *dev,
					struct ethtool_stats *stats,
					uint64_t *data)
{
	struct dsa_slave_priv *p = netdev_priv(dev);
	struct dsa_switch *ds = p->parent;

	data[0] = p->dev->stats.tx_packets;
	data[1] = p->dev->stats.tx_bytes;
	data[2] = p->dev->stats.rx_packets;
	data[3] = p->dev->stats.rx_bytes;
	if (ds->drv->get_ethtool_stats != NULL)
		ds->drv->get_ethtool_stats(ds, p->port, data + 4);
}

static int dsa_slave_get_sset_count(struct net_device *dev, int sset)
{
	struct dsa_slave_priv *p = netdev_priv(dev);
	struct dsa_switch *ds = p->parent;

	if (sset == ETH_SS_STATS) {
		int count;

		count = 4;
		if (ds->drv->get_sset_count != NULL)
			count += ds->drv->get_sset_count(ds);

		return count;
	}

	return -EOPNOTSUPP;
}

static const struct ethtool_ops dsa_slave_ethtool_ops = {
	.get_settings		= dsa_slave_get_settings,
	.set_settings		= dsa_slave_set_settings,
	.get_drvinfo		= dsa_slave_get_drvinfo,
	.nway_reset		= dsa_slave_nway_reset,
	.get_link		= dsa_slave_get_link,
	.set_sg			= ethtool_op_set_sg,
	.get_strings		= dsa_slave_get_strings,
	.get_ethtool_stats	= dsa_slave_get_ethtool_stats,
	.get_sset_count		= dsa_slave_get_sset_count,
};


/* slave device setup *******************************************************/
struct net_device *
dsa_slave_create(struct dsa_switch *ds, struct device *parent,
		 int port, char *name)
{
	struct net_device *master = ds->master_netdev;
	struct net_device *slave_dev;
	struct dsa_slave_priv *p;
	int ret;

	slave_dev = alloc_netdev(sizeof(struct dsa_slave_priv),
				 name, ether_setup);
	if (slave_dev == NULL)
		return slave_dev;

	slave_dev->features = master->vlan_features;
	SET_ETHTOOL_OPS(slave_dev, &dsa_slave_ethtool_ops);
	memcpy(slave_dev->dev_addr, master->dev_addr, ETH_ALEN);
	slave_dev->tx_queue_len = 0;
	switch (ds->tag_protocol) {
#ifdef CONFIG_NET_DSA_TAG_DSA
	case htons(ETH_P_DSA):
		slave_dev->hard_start_xmit = dsa_xmit;
		break;
#endif
#ifdef CONFIG_NET_DSA_TAG_EDSA
	case htons(ETH_P_EDSA):
		slave_dev->hard_start_xmit = edsa_xmit;
		break;
#endif
	default:
		BUG();
	}
	slave_dev->open = dsa_slave_open;
	slave_dev->stop = dsa_slave_close;
	slave_dev->change_rx_flags = dsa_slave_change_rx_flags;
	slave_dev->set_rx_mode = dsa_slave_set_rx_mode;
	slave_dev->set_multicast_list = dsa_slave_set_rx_mode;
	slave_dev->set_mac_address = dsa_slave_set_mac_address;
	slave_dev->do_ioctl = dsa_slave_ioctl;
	SET_NETDEV_DEV(slave_dev, parent);
	slave_dev->vlan_features = master->vlan_features;

	p = netdev_priv(slave_dev);
	p->dev = slave_dev;
	p->parent = ds;
	p->port = port;
	p->phy = ds->slave_mii_bus->phy_map[port];

	ret = register_netdev(slave_dev);
	if (ret) {
		printk(KERN_ERR "%s: error %d registering interface %s\n",
				master->name, ret, slave_dev->name);
		free_netdev(slave_dev);
		return NULL;
	}

	netif_carrier_off(slave_dev);

	if (p->phy != NULL) {
		phy_attach(slave_dev, p->phy->dev.bus_id,
			   0, PHY_INTERFACE_MODE_GMII);

		p->phy->autoneg = AUTONEG_ENABLE;
		p->phy->speed = 0;
		p->phy->duplex = 0;
		p->phy->advertising = p->phy->supported | ADVERTISED_Autoneg;
		phy_start_aneg(p->phy);
	}

	return slave_dev;
}
