/* Copyright 2011-2014 Autronica Fire and Security AS
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * Author(s):
 *	2011-2014 Arvid Brodin, arvid.brodin@alten.se
 */

#ifndef __HSR_SLAVE_H
#define __HSR_SLAVE_H

#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <linux/rtnetlink.h>
#include "hsr_main.h"

int hsr_add_port(struct hsr_priv *hsr, struct net_device *dev,
		 enum hsr_port_type pt);
void hsr_del_port(struct hsr_port *port);
rx_handler_result_t hsr_handle_frame(struct sk_buff **pskb);


#define hsr_for_each_port(hsr, port) \
	list_for_each_entry_rcu((port), &(hsr)->ports, port_list)


static inline bool hsr_port_exists(const struct net_device *dev)
{
	return dev->rx_handler == hsr_handle_frame;
}

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

#endif /* __HSR_SLAVE_H */
