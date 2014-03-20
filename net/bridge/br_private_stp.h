/*
 *	Linux ethernet bridge
 *
 *	Authors:
 *	Lennert Buytenhek		<buytenh@gnu.org>
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

/* IEEE 802.1D-1998 timer values */
#define BR_MIN_HELLO_TIME	(1*HZ)
#define BR_MAX_HELLO_TIME	(10*HZ)

#define BR_MIN_FORWARD_DELAY	(2*HZ)
#define BR_MAX_FORWARD_DELAY	(30*HZ)

#define BR_MIN_MAX_AGE		(6*HZ)
#define BR_MAX_MAX_AGE		(40*HZ)

#define BR_MIN_PATH_COST	1
#define BR_MAX_PATH_COST	65535

struct br_config_bpdu {
	unsigned int	topology_change:1;
	unsigned int	topology_change_ack:1;
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
void br_become_root_bridge(struct net_bridge *br);
void br_config_bpdu_generation(struct net_bridge *);
void br_configuration_update(struct net_bridge *);
void br_port_state_selection(struct net_bridge *);
void br_received_config_bpdu(struct net_bridge_port *p,
			     const struct br_config_bpdu *bpdu);
void br_received_tcn_bpdu(struct net_bridge_port *p);
void br_transmit_config(struct net_bridge_port *p);
void br_transmit_tcn(struct net_bridge *br);
void br_topology_change_detection(struct net_bridge *br);

/* br_stp_bpdu.c */
void br_send_config_bpdu(struct net_bridge_port *, struct br_config_bpdu *);
void br_send_tcn_bpdu(struct net_bridge_port *);

#endif
