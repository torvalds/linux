/*
 * net/dsa/dsa_priv.h - Hardware switch handling
 * Copyright (c) 2008 Marvell Semiconductor
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
	 * Configuration data for the platform device that owns
	 * this dsa switch instance.
	 */
	struct dsa_platform_data	*pd;

	/*
	 * References to network device and mii bus to use.
	 */
	struct net_device		*master_netdev;
	struct mii_bus			*master_mii_bus;

	/*
	 * The used switch driver and frame tagging type.
	 */
	struct dsa_switch_driver	*drv;
	__be16				tag_protocol;

	/*
	 * Slave mii_bus and devices for the individual ports.
	 */
	int				cpu_port;
	u32				valid_port_mask;
	struct mii_bus			*slave_mii_bus;
	struct net_device		*ports[DSA_MAX_PORTS];

	/*
	 * Link state polling.
	 */
	struct work_struct		link_poll_work;
	struct timer_list		link_poll_timer;
};

struct dsa_slave_priv {
	struct net_device	*dev;
	struct dsa_switch	*parent;
	int			port;
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
int dsa_xmit(struct sk_buff *skb, struct net_device *dev);

/* tag_edsa.c */
int edsa_xmit(struct sk_buff *skb, struct net_device *dev);

/* tag_trailer.c */
int trailer_xmit(struct sk_buff *skb, struct net_device *dev);


#endif
