/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright 2011-2014 Autronica Fire and Security AS
 *
 *	2011-2014 Arvid Brodin, arvid.brodin@alten.se
 *
 * include file for HSR and PRP.
 */

#ifndef __HSR_SLAVE_H
#define __HSR_SLAVE_H

#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <linux/rtnetlink.h>
#include "hsr_main.h"

int hsr_add_port(struct hsr_priv *hsr, struct net_device *dev,
		 enum hsr_port_type pt, struct netlink_ext_ack *extack);
void hsr_del_port(struct hsr_port *port);
bool hsr_port_exists(const struct net_device *dev);

static inline struct hsr_port *hsr_port_get_rtnl(const struct net_device *dev)
{
	ASSERT_RTNL();
	return hsr_port_exists(dev) ?
				rtnl_dereference(dev->rx_handler_data) : NULL;
}

static inline struct hsr_port *hsr_port_get_rcu(const struct net_device *dev)
{
	return hsr_port_exists(dev) ?
				rcu_dereference(dev->rx_handler_data) : NULL;
}

bool hsr_invalid_dan_ingress_frame(__be16 protocol);

#endif /* __HSR_SLAVE_H */
