/*
 *	Spanning tree protocol; interface code
 *	Linux ethernet bridge
 *
 *	Authors:
 *	Lennert Buytenhek		<buytenh@gnu.org>
 *
 *	$Id: br_stp_if.c,v 1.4 2001/04/14 21:14:39 davem Exp $
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 */

#include <linux/kernel.h>
#include <linux/smp_lock.h>
#include <linux/etherdevice.h>

#include "br_private.h"
#include "br_private_stp.h"


/* Port id is composed of priority and port number.
 * NB: least significant bits of priority are dropped to
 *     make room for more ports.
 */
static inline port_id br_make_port_id(__u8 priority, __u16 port_no)
{
	return ((u16)priority << BR_PORT_BITS) 
		| (port_no & ((1<<BR_PORT_BITS)-1));
}

/* called under bridge lock */
void br_init_port(struct net_bridge_port *p)
{
	p->port_id = br_make_port_id(p->priority, p->port_no);
	br_become_designated_port(p);
	p->state = BR_STATE_BLOCKING;
	p->topology_change_ack = 0;
	p->config_pending = 0;

	br_stp_port_timer_init(p);
}

/* called under bridge lock */
void br_stp_enable_bridge(struct net_bridge *br)
{
	struct net_bridge_port *p;

	spin_lock_bh(&br->lock);
	mod_timer(&br->hello_timer, jiffies + br->hello_time);
	mod_timer(&br->gc_timer, jiffies + HZ/10);
	
	br_config_bpdu_generation(br);

	list_for_each_entry(p, &br->port_list, list) {
		if ((p->dev->flags & IFF_UP) && netif_carrier_ok(p->dev))
			br_stp_enable_port(p);

	}
	spin_unlock_bh(&br->lock);
}

/* NO locks held */
void br_stp_disable_bridge(struct net_bridge *br)
{
	struct net_bridge_port *p;

	spin_lock(&br->lock);
	list_for_each_entry(p, &br->port_list, list) {
		if (p->state != BR_STATE_DISABLED)
			br_stp_disable_port(p);

	}

	br->topology_change = 0;
	br->topology_change_detected = 0;
	spin_unlock(&br->lock);

	del_timer_sync(&br->hello_timer);
	del_timer_sync(&br->topology_change_timer);
	del_timer_sync(&br->tcn_timer);
	del_timer_sync(&br->gc_timer);
}

/* called under bridge lock */
void br_stp_enable_port(struct net_bridge_port *p)
{
	br_init_port(p);
	br_port_state_selection(p->br);
}

/* called under bridge lock */
void br_stp_disable_port(struct net_bridge_port *p)
{
	struct net_bridge *br;
	int wasroot;

	br = p->br;
	printk(KERN_INFO "%s: port %i(%s) entering %s state\n",
	       br->dev->name, p->port_no, p->dev->name, "disabled");

	wasroot = br_is_root_bridge(br);
	br_become_designated_port(p);
	p->state = BR_STATE_DISABLED;
	p->topology_change_ack = 0;
	p->config_pending = 0;

	del_timer(&p->message_age_timer);
	del_timer(&p->forward_delay_timer);
	del_timer(&p->hold_timer);

	br_configuration_update(br);

	br_port_state_selection(br);

	if (br_is_root_bridge(br) && !wasroot)
		br_become_root_bridge(br);
}

/* called under bridge lock */
void br_stp_change_bridge_id(struct net_bridge *br, const unsigned char *addr)
{
	unsigned char oldaddr[6];
	struct net_bridge_port *p;
	int wasroot;

	wasroot = br_is_root_bridge(br);

	memcpy(oldaddr, br->bridge_id.addr, ETH_ALEN);
	memcpy(br->bridge_id.addr, addr, ETH_ALEN);
	memcpy(br->dev->dev_addr, addr, ETH_ALEN);

	list_for_each_entry(p, &br->port_list, list) {
		if (!compare_ether_addr(p->designated_bridge.addr, oldaddr))
			memcpy(p->designated_bridge.addr, addr, ETH_ALEN);

		if (!compare_ether_addr(p->designated_root.addr, oldaddr))
			memcpy(p->designated_root.addr, addr, ETH_ALEN);

	}

	br_configuration_update(br);
	br_port_state_selection(br);
	if (br_is_root_bridge(br) && !wasroot)
		br_become_root_bridge(br);
}

static const unsigned char br_mac_zero[6];

/* called under bridge lock */
void br_stp_recalculate_bridge_id(struct net_bridge *br)
{
	const unsigned char *addr = br_mac_zero;
	struct net_bridge_port *p;

	list_for_each_entry(p, &br->port_list, list) {
		if (addr == br_mac_zero ||
		    memcmp(p->dev->dev_addr, addr, ETH_ALEN) < 0)
			addr = p->dev->dev_addr;

	}

	if (compare_ether_addr(br->bridge_id.addr, addr))
		br_stp_change_bridge_id(br, addr);
}

/* called under bridge lock */
void br_stp_set_bridge_priority(struct net_bridge *br, u16 newprio)
{
	struct net_bridge_port *p;
	int wasroot;

	wasroot = br_is_root_bridge(br);

	list_for_each_entry(p, &br->port_list, list) {
		if (p->state != BR_STATE_DISABLED &&
		    br_is_designated_port(p)) {
			p->designated_bridge.prio[0] = (newprio >> 8) & 0xFF;
			p->designated_bridge.prio[1] = newprio & 0xFF;
		}

	}

	br->bridge_id.prio[0] = (newprio >> 8) & 0xFF;
	br->bridge_id.prio[1] = newprio & 0xFF;
	br_configuration_update(br);
	br_port_state_selection(br);
	if (br_is_root_bridge(br) && !wasroot)
		br_become_root_bridge(br);
}

/* called under bridge lock */
void br_stp_set_port_priority(struct net_bridge_port *p, u8 newprio)
{
	port_id new_port_id = br_make_port_id(newprio, p->port_no);

	if (br_is_designated_port(p))
		p->designated_port = new_port_id;

	p->port_id = new_port_id;
	p->priority = newprio;
	if (!memcmp(&p->br->bridge_id, &p->designated_bridge, 8) &&
	    p->port_id < p->designated_port) {
		br_become_designated_port(p);
		br_port_state_selection(p->br);
	}
}

/* called under bridge lock */
void br_stp_set_path_cost(struct net_bridge_port *p, u32 path_cost)
{
	p->path_cost = path_cost;
	br_configuration_update(p->br);
	br_port_state_selection(p->br);
}

ssize_t br_show_bridge_id(char *buf, const struct bridge_id *id)
{
	return sprintf(buf, "%.2x%.2x.%.2x%.2x%.2x%.2x%.2x%.2x\n",
	       id->prio[0], id->prio[1],
	       id->addr[0], id->addr[1], id->addr[2],
	       id->addr[3], id->addr[4], id->addr[5]);
}
