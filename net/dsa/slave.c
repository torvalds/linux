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
#include <net/rtnetlink.h>
#include <net/switchdev.h>
#include <linux/if_bridge.h>
#include "dsa_priv.h"

/* slave mii_bus handling ***************************************************/
static int dsa_slave_phy_read(struct mii_bus *bus, int addr, int reg)
{
	struct dsa_switch *ds = bus->priv;

	if (ds->phys_mii_mask & (1 << addr))
		return ds->drv->phy_read(ds, addr, reg);

	return 0xffff;
}

static int dsa_slave_phy_write(struct mii_bus *bus, int addr, int reg, u16 val)
{
	struct dsa_switch *ds = bus->priv;

	if (ds->phys_mii_mask & (1 << addr))
		return ds->drv->phy_write(ds, addr, reg, val);

	return 0;
}

void dsa_slave_mii_bus_init(struct dsa_switch *ds)
{
	ds->slave_mii_bus->priv = (void *)ds;
	ds->slave_mii_bus->name = "dsa slave smi";
	ds->slave_mii_bus->read = dsa_slave_phy_read;
	ds->slave_mii_bus->write = dsa_slave_phy_write;
	snprintf(ds->slave_mii_bus->id, MII_BUS_ID_SIZE, "dsa-%d:%.2x",
			ds->index, ds->pd->sw_addr);
	ds->slave_mii_bus->parent = ds->master_dev;
	ds->slave_mii_bus->phy_mask = ~ds->phys_mii_mask;
}


/* slave device handling ****************************************************/
static int dsa_slave_init(struct net_device *dev)
{
	struct dsa_slave_priv *p = netdev_priv(dev);

	dev->iflink = p->parent->dst->master_netdev->ifindex;

	return 0;
}

static inline bool dsa_port_is_bridged(struct dsa_slave_priv *p)
{
	return !!p->bridge_dev;
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

	if (ds->drv->port_enable) {
		err = ds->drv->port_enable(ds, p->port, p->phy);
		if (err)
			goto clear_promisc;
	}

	if (ds->drv->port_stp_update)
		ds->drv->port_stp_update(ds, p->port, stp_state);

	if (p->phy)
		phy_start(p->phy);

	return 0;

clear_promisc:
	if (dev->flags & IFF_PROMISC)
		dev_set_promiscuity(master, 0);
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

	if (ds->drv->port_disable)
		ds->drv->port_disable(ds, p->port, p->phy);

	if (ds->drv->port_stp_update)
		ds->drv->port_stp_update(ds, p->port, BR_STATE_DISABLED);

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

static int dsa_slave_ioctl(struct net_device *dev, struct ifreq *ifr, int cmd)
{
	struct dsa_slave_priv *p = netdev_priv(dev);

	if (p->phy != NULL)
		return phy_mii_ioctl(p->phy, ifr, cmd);

	return -EOPNOTSUPP;
}

/* Return a bitmask of all ports being currently bridged within a given bridge
 * device. Note that on leave, the mask will still return the bitmask of ports
 * currently bridged, prior to port removal, and this is exactly what we want.
 */
static u32 dsa_slave_br_port_mask(struct dsa_switch *ds,
				  struct net_device *bridge)
{
	struct dsa_slave_priv *p;
	unsigned int port;
	u32 mask = 0;

	for (port = 0; port < DSA_MAX_PORTS; port++) {
		if (!dsa_is_port_initialized(ds, port))
			continue;

		p = netdev_priv(ds->ports[port]);

		if (ds->ports[port]->priv_flags & IFF_BRIDGE_PORT &&
		    p->bridge_dev == bridge)
			mask |= 1 << port;
	}

	return mask;
}

static int dsa_slave_stp_update(struct net_device *dev, u8 state)
{
	struct dsa_slave_priv *p = netdev_priv(dev);
	struct dsa_switch *ds = p->parent;
	int ret = -EOPNOTSUPP;

	if (ds->drv->port_stp_update)
		ret = ds->drv->port_stp_update(ds, p->port, state);

	return ret;
}

static int dsa_slave_bridge_port_join(struct net_device *dev,
				      struct net_device *br)
{
	struct dsa_slave_priv *p = netdev_priv(dev);
	struct dsa_switch *ds = p->parent;
	int ret = -EOPNOTSUPP;

	p->bridge_dev = br;

	if (ds->drv->port_join_bridge)
		ret = ds->drv->port_join_bridge(ds, p->port,
						dsa_slave_br_port_mask(ds, br));

	return ret;
}

static int dsa_slave_bridge_port_leave(struct net_device *dev)
{
	struct dsa_slave_priv *p = netdev_priv(dev);
	struct dsa_switch *ds = p->parent;
	int ret = -EOPNOTSUPP;


	if (ds->drv->port_leave_bridge)
		ret = ds->drv->port_leave_bridge(ds, p->port,
						 dsa_slave_br_port_mask(ds, p->bridge_dev));

	p->bridge_dev = NULL;

	/* Port left the bridge, put in BR_STATE_DISABLED by the bridge layer,
	 * so allow it to be in BR_STATE_FORWARDING to be kept functional
	 */
	dsa_slave_stp_update(dev, BR_STATE_FORWARDING);

	return ret;
}

static int dsa_slave_parent_id_get(struct net_device *dev,
				   struct netdev_phys_item_id *psid)
{
	struct dsa_slave_priv *p = netdev_priv(dev);
	struct dsa_switch *ds = p->parent;

	psid->id_len = sizeof(ds->index);
	memcpy(&psid->id, &ds->index, psid->id_len);

	return 0;
}

static netdev_tx_t dsa_slave_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct dsa_slave_priv *p = netdev_priv(dev);

	return p->xmit(skb, dev);
}

static netdev_tx_t dsa_slave_notag_xmit(struct sk_buff *skb,
					struct net_device *dev)
{
	struct dsa_slave_priv *p = netdev_priv(dev);

	skb->dev = p->parent->dst->master_netdev;
	dev_queue_xmit(skb);

	return NETDEV_TX_OK;
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
	strlcpy(drvinfo->driver, "dsa", sizeof(drvinfo->driver));
	strlcpy(drvinfo->version, dsa_driver_version, sizeof(drvinfo->version));
	strlcpy(drvinfo->fw_version, "N/A", sizeof(drvinfo->fw_version));
	strlcpy(drvinfo->bus_info, "platform", sizeof(drvinfo->bus_info));
}

static int dsa_slave_get_regs_len(struct net_device *dev)
{
	struct dsa_slave_priv *p = netdev_priv(dev);
	struct dsa_switch *ds = p->parent;

	if (ds->drv->get_regs_len)
		return ds->drv->get_regs_len(ds, p->port);

	return -EOPNOTSUPP;
}

static void
dsa_slave_get_regs(struct net_device *dev, struct ethtool_regs *regs, void *_p)
{
	struct dsa_slave_priv *p = netdev_priv(dev);
	struct dsa_switch *ds = p->parent;

	if (ds->drv->get_regs)
		ds->drv->get_regs(ds, p->port, regs, _p);
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

	if (ds->pd->eeprom_len)
		return ds->pd->eeprom_len;

	if (ds->drv->get_eeprom_len)
		return ds->drv->get_eeprom_len(ds);

	return 0;
}

static int dsa_slave_get_eeprom(struct net_device *dev,
				struct ethtool_eeprom *eeprom, u8 *data)
{
	struct dsa_slave_priv *p = netdev_priv(dev);
	struct dsa_switch *ds = p->parent;

	if (ds->drv->get_eeprom)
		return ds->drv->get_eeprom(ds, eeprom, data);

	return -EOPNOTSUPP;
}

static int dsa_slave_set_eeprom(struct net_device *dev,
				struct ethtool_eeprom *eeprom, u8 *data)
{
	struct dsa_slave_priv *p = netdev_priv(dev);
	struct dsa_switch *ds = p->parent;

	if (ds->drv->set_eeprom)
		return ds->drv->set_eeprom(ds, eeprom, data);

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

static void dsa_slave_get_wol(struct net_device *dev, struct ethtool_wolinfo *w)
{
	struct dsa_slave_priv *p = netdev_priv(dev);
	struct dsa_switch *ds = p->parent;

	if (ds->drv->get_wol)
		ds->drv->get_wol(ds, p->port, w);
}

static int dsa_slave_set_wol(struct net_device *dev, struct ethtool_wolinfo *w)
{
	struct dsa_slave_priv *p = netdev_priv(dev);
	struct dsa_switch *ds = p->parent;
	int ret = -EOPNOTSUPP;

	if (ds->drv->set_wol)
		ret = ds->drv->set_wol(ds, p->port, w);

	return ret;
}

static int dsa_slave_set_eee(struct net_device *dev, struct ethtool_eee *e)
{
	struct dsa_slave_priv *p = netdev_priv(dev);
	struct dsa_switch *ds = p->parent;
	int ret;

	if (!ds->drv->set_eee)
		return -EOPNOTSUPP;

	ret = ds->drv->set_eee(ds, p->port, p->phy, e);
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

	if (!ds->drv->get_eee)
		return -EOPNOTSUPP;

	ret = ds->drv->get_eee(ds, p->port, e);
	if (ret)
		return ret;

	if (p->phy)
		ret = phy_ethtool_get_eee(p->phy, e);

	return ret;
}

static const struct ethtool_ops dsa_slave_ethtool_ops = {
	.get_settings		= dsa_slave_get_settings,
	.set_settings		= dsa_slave_set_settings,
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
};

static const struct net_device_ops dsa_slave_netdev_ops = {
	.ndo_init		= dsa_slave_init,
	.ndo_open	 	= dsa_slave_open,
	.ndo_stop		= dsa_slave_close,
	.ndo_start_xmit		= dsa_slave_xmit,
	.ndo_change_rx_flags	= dsa_slave_change_rx_flags,
	.ndo_set_rx_mode	= dsa_slave_set_rx_mode,
	.ndo_set_mac_address	= dsa_slave_set_mac_address,
	.ndo_do_ioctl		= dsa_slave_ioctl,
};

static const struct swdev_ops dsa_slave_swdev_ops = {
	.swdev_parent_id_get = dsa_slave_parent_id_get,
	.swdev_port_stp_update = dsa_slave_stp_update,
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

	if (ds->drv->adjust_link && status_changed)
		ds->drv->adjust_link(ds, p->port, p->phy);

	if (status_changed)
		phy_print_status(p->phy);
}

static int dsa_slave_fixed_link_update(struct net_device *dev,
				       struct fixed_phy_status *status)
{
	struct dsa_slave_priv *p = netdev_priv(dev);
	struct dsa_switch *ds = p->parent;

	if (ds->drv->fixed_link_update)
		ds->drv->fixed_link_update(ds, p->port, status);

	return 0;
}

/* slave device setup *******************************************************/
static int dsa_slave_phy_connect(struct dsa_slave_priv *p,
				 struct net_device *slave_dev,
				 int addr)
{
	struct dsa_switch *ds = p->parent;

	p->phy = ds->slave_mii_bus->phy_map[addr];
	if (!p->phy)
		return -ENODEV;

	/* Use already configured phy mode */
	p->phy_interface = p->phy->interface;
	phy_connect_direct(slave_dev, p->phy, dsa_slave_adjust_link,
			   p->phy_interface);

	return 0;
}

static int dsa_slave_phy_setup(struct dsa_slave_priv *p,
				struct net_device *slave_dev)
{
	struct dsa_switch *ds = p->parent;
	struct dsa_chip_data *cd = ds->pd;
	struct device_node *phy_dn, *port_dn;
	bool phy_is_fixed = false;
	u32 phy_flags = 0;
	int mode, ret;

	port_dn = cd->port_dn[p->port];
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
			netdev_err(slave_dev, "failed to register fixed PHY\n");
			return ret;
		}
		phy_is_fixed = true;
		phy_dn = port_dn;
	}

	if (ds->drv->get_phy_flags)
		phy_flags = ds->drv->get_phy_flags(ds, p->port);

	if (phy_dn) {
		ret = of_mdio_parse_addr(&slave_dev->dev, phy_dn);
		/* If this PHY address is part of phys_mii_mask, which means
		 * that we need to divert reads and writes to/from it, then we
		 * want to bind this device using the slave MII bus created by
		 * DSA to make that happen.
		 */
		if (!phy_is_fixed && ret >= 0 &&
		    (ds->phys_mii_mask & (1 << ret))) {
			ret = dsa_slave_phy_connect(p, slave_dev, ret);
			if (ret)
				return ret;
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
		if (ret)
			return ret;
	} else {
		netdev_info(slave_dev, "attached PHY at address %d [%s]\n",
			    p->phy->addr, p->phy->drv->name);
	}

	return 0;
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
		     int port, char *name)
{
	struct net_device *master = ds->dst->master_netdev;
	struct net_device *slave_dev;
	struct dsa_slave_priv *p;
	int ret;

	slave_dev = alloc_netdev(sizeof(struct dsa_slave_priv), name,
				 NET_NAME_UNKNOWN, ether_setup);
	if (slave_dev == NULL)
		return -ENOMEM;

	slave_dev->features = master->vlan_features;
	slave_dev->ethtool_ops = &dsa_slave_ethtool_ops;
	eth_hw_addr_inherit(slave_dev, master);
	slave_dev->tx_queue_len = 0;
	slave_dev->netdev_ops = &dsa_slave_netdev_ops;
	slave_dev->swdev_ops = &dsa_slave_swdev_ops;

	SET_NETDEV_DEV(slave_dev, parent);
	slave_dev->dev.of_node = ds->pd->port_dn[port];
	slave_dev->vlan_features = master->vlan_features;

	p = netdev_priv(slave_dev);
	p->dev = slave_dev;
	p->parent = ds;
	p->port = port;

	switch (ds->dst->tag_protocol) {
#ifdef CONFIG_NET_DSA_TAG_DSA
	case DSA_TAG_PROTO_DSA:
		p->xmit = dsa_netdev_ops.xmit;
		break;
#endif
#ifdef CONFIG_NET_DSA_TAG_EDSA
	case DSA_TAG_PROTO_EDSA:
		p->xmit = edsa_netdev_ops.xmit;
		break;
#endif
#ifdef CONFIG_NET_DSA_TAG_TRAILER
	case DSA_TAG_PROTO_TRAILER:
		p->xmit = trailer_netdev_ops.xmit;
		break;
#endif
#ifdef CONFIG_NET_DSA_TAG_BRCM
	case DSA_TAG_PROTO_BRCM:
		p->xmit = brcm_netdev_ops.xmit;
		break;
#endif
	default:
		p->xmit	= dsa_slave_notag_xmit;
		break;
	}

	p->old_pause = -1;
	p->old_link = -1;
	p->old_duplex = -1;

	ret = dsa_slave_phy_setup(p, slave_dev);
	if (ret) {
		free_netdev(slave_dev);
		return ret;
	}

	ds->ports[port] = slave_dev;
	ret = register_netdev(slave_dev);
	if (ret) {
		netdev_err(master, "error %d registering interface %s\n",
			   ret, slave_dev->name);
		phy_disconnect(p->phy);
		ds->ports[port] = NULL;
		free_netdev(slave_dev);
		return ret;
	}

	netif_carrier_off(slave_dev);

	return 0;
}

static bool dsa_slave_dev_check(struct net_device *dev)
{
	return dev->netdev_ops == &dsa_slave_netdev_ops;
}

static int dsa_slave_master_changed(struct net_device *dev)
{
	struct net_device *master = netdev_master_upper_dev_get(dev);
	int err = 0;

	if (master && master->rtnl_link_ops &&
	    !strcmp(master->rtnl_link_ops->kind, "bridge"))
		err = dsa_slave_bridge_port_join(dev, master);
	else
		err = dsa_slave_bridge_port_leave(dev);

	return err;
}

int dsa_slave_netdevice_event(struct notifier_block *unused,
			      unsigned long event, void *ptr)
{
	struct net_device *dev;
	int err = 0;

	switch (event) {
	case NETDEV_CHANGEUPPER:
		dev = netdev_notifier_info_to_dev(ptr);
		if (!dsa_slave_dev_check(dev))
			goto out;

		err = dsa_slave_master_changed(dev);
		if (err)
			netdev_warn(dev, "failed to reflect master change\n");

		break;
	}

out:
	return NOTIFY_DONE;
}
