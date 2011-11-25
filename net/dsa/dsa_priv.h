/*
 * net/dsa/dsa_priv.h - Hardware switch handling
 * Copyright (c) 2008-2009 Marvell Semiconductor
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef __DSA_PRIV_H
#define __DSA_PRIV_H

#include <linux/list.h>
#include <linux/phy.h>
#include <linux/timer.h>
#include <linux/workqueue.h>
#include <net/dsa.h>

struct dsa_switch {
	/*
	 * Parent switch tree, and switch index.
	 */
	struct dsa_switch_tree	*dst;
	int			index;

	/*
	 * Configuration data for this switch.
	 */
	struct dsa_chip_data	*pd;

	/*
	 * The used switch driver.
	 */
	struct dsa_switch_driver	*drv;

	/*
	 * Reference to mii bus to use.
	 */
	struct mii_bus		*master_mii_bus;

	/*
	 * Slave mii_bus and devices for the individual ports.
	 */
	u32			dsa_port_mask;
	u32			phys_port_mask;
	struct mii_bus		*slave_mii_bus;
	struct net_device	*ports[DSA_MAX_PORTS];
};

static inline bool dsa_is_cpu_port(struct dsa_switch *ds, int p)
{
	return !!(ds->index == ds->dst->cpu_switch && p == ds->dst->cpu_port);
}

static inline u8 dsa_upstream_port(struct dsa_switch *ds)
{
	struct dsa_switch_tree *dst = ds->dst;

	/*
	 * If this is the root switch (i.e. the switch that connects
	 * to the CPU), return the cpu port number on this switch.
	 * Else return the (DSA) port number that connects to the
	 * switch that is one hop closer to the cpu.
	 */
	if (dst->cpu_switch == ds->index)
		return dst->cpu_port;
	else
		return ds->pd->rtable[dst->cpu_switch];
}

struct dsa_slave_priv {
	/*
	 * The linux network interface corresponding to this
	 * switch port.
	 */
	struct net_device	*dev;

	/*
	 * Which switch this port is a part of, and the port index
	 * for this port.
	 */
	struct dsa_switch	*parent;
	u8			port;

	/*
	 * The phylib phy_device pointer for the PHY connected
	 * to this port.
	 */
	struct phy_device	*phy;
};

struct dsa_switch_driver {
	struct list_head	list;

	__be16			tag_protocol;
	int			priv_size;

	/*
	 * Probing and setup.
	 */
	char	*(*probe)(struct mii_bus *bus, int sw_addr);
	int	(*setup)(struct dsa_switch *ds);
	int	(*set_addr)(struct dsa_switch *ds, u8 *addr);

	/*
	 * Access to the switch's PHY registers.
	 */
	int	(*phy_read)(struct dsa_switch *ds, int port, int regnum);
	int	(*phy_write)(struct dsa_switch *ds, int port,
			     int regnum, u16 val);

	/*
	 * Link state polling and IRQ handling.
	 */
	void	(*poll_link)(struct dsa_switch *ds);

	/*
	 * ethtool hardware statistics.
	 */
	void	(*get_strings)(struct dsa_switch *ds, int port, uint8_t *data);
	void	(*get_ethtool_stats)(struct dsa_switch *ds,
				     int port, uint64_t *data);
	int	(*get_sset_count)(struct dsa_switch *ds);
};

/* dsa.c */
extern char dsa_driver_version[];
void register_switch_driver(struct dsa_switch_driver *type);
void unregister_switch_driver(struct dsa_switch_driver *type);

/* slave.c */
void dsa_slave_mii_bus_init(struct dsa_switch *ds);
struct net_device *dsa_slave_create(struct dsa_switch *ds,
				    struct device *parent,
				    int port, char *name);

/* tag_dsa.c */
netdev_tx_t dsa_xmit(struct sk_buff *skb, struct net_device *dev);
extern struct packet_type dsa_packet_type;

/* tag_edsa.c */
netdev_tx_t edsa_xmit(struct sk_buff *skb, struct net_device *dev);
extern struct packet_type edsa_packet_type;

/* tag_trailer.c */
netdev_tx_t trailer_xmit(struct sk_buff *skb, struct net_device *dev);
extern struct packet_type trailer_packet_type;


#endif
