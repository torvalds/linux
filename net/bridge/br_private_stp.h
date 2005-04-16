/*
 *	Linux ethernet bridge
 *
 *	Authors:
 *	Lennert Buytenhek		<buytenh@gnu.org>
 *
 *	$Id: br_private_stp.h,v 1.3 2001/02/05 06:03:47 davem Exp $
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 */

#ifndef _BR_PRIVATE_STP_H
#define _BR_PRIVATE_STP_H

#define BPDU_TYPE_CONFIG 0
#define BPDU_TYPE_TCN 0x80

struct br_config_bpdu
{
	unsigned	topology_change:1;
	unsigned	topology_change_ack:1;
	bridge_id	root;
	int		root_path_cost;
	bridge_id	bridge_id;
	port_id		port_id;
	int		message_age;
	int		max_age;
	int		hello_time;
	int		forward_delay;
};

/* called under bridge lock */
static inline int br_is_designated_port(const struct net_bridge_port *p)
{
	return !memcmp(&p->designated_bridge, &p->br->bridge_id, 8) &&
		(p->designated_port == p->port_id);
}


/* br_stp.c */
extern void br_become_root_bridge(struct net_bridge *br);
extern void br_config_bpdu_generation(struct net_bridge *);
extern void br_configuration_update(struct net_bridge *);
extern void br_port_state_selection(struct net_bridge *);
extern void br_received_config_bpdu(struct net_bridge_port *p, struct br_config_bpdu *bpdu);
extern void br_received_tcn_bpdu(struct net_bridge_port *p);
extern void br_transmit_config(struct net_bridge_port *p);
extern void br_transmit_tcn(struct net_bridge *br);
extern void br_topology_change_detection(struct net_bridge *br);

/* br_stp_bpdu.c */
extern void br_send_config_bpdu(struct net_bridge_port *, struct br_config_bpdu *);
extern void br_send_tcn_bpdu(struct net_bridge_port *);

#endif
