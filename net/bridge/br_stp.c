/*
 *	Spanning tree protocol; generic parts
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
#include <linux/kernel.h>
#include <linux/rculist.h>
#include <net/switchdev.h>

#include "br_private.h"
#include "br_private_stp.h"

/* since time values in bpdu are in jiffies and then scaled (1/256)
 * before sending, make sure that is at least one STP tick.
 */
#define MESSAGE_AGE_INCR	((HZ / 256) + 1)

static const char *const br_port_state_names[] = {
	[BR_STATE_DISABLED] = "disabled",
	[BR_STATE_LISTENING] = "listening",
	[BR_STATE_LEARNING] = "learning",
	[BR_STATE_FORWARDING] = "forwarding",
	[BR_STATE_BLOCKING] = "blocking",
};

void br_log_state(const struct net_bridge_port *p)
{
	br_info(p->br, "port %u(%s) entered %s state\n",
		(unsigned int) p->port_no, p->dev->name,
		br_port_state_names[p->state]);
}

void br_set_state(struct net_bridge_port *p, unsigned int state)
{
	struct switchdev_attr attr = {
		.id = SWITCHDEV_ATTR_PORT_STP_STATE,
		.u.stp_state = state,
	};
	int err;

	p->state = state;
	err = switchdev_port_attr_set(p->dev, &attr);
	if (err && err != -EOPNOTSUPP)
		br_warn(p->br, "error setting offload STP state on port %u(%s)\n",
				(unsigned int) p->port_no, p->dev->name);
}

/* called under bridge lock */
struct net_bridge_port *br_get_port(struct net_bridge *br, u16 port_no)
{
	struct net_bridge_port *p;

	list_for_each_entry_rcu(p, &br->port_list, list) {
		if (p->port_no == port_no)
			return p;
	}

	return NULL;
}

/* called under bridge lock */
static int br_should_become_root_port(const struct net_bridge_port *p,
				      u16 root_port)
{
	struct net_bridge *br;
	struct net_bridge_port *rp;
	int t;

	br = p->br;
	if (p->state == BR_STATE_DISABLED ||
	    br_is_designated_port(p))
		return 0;

	if (memcmp(&br->bridge_id, &p->designated_root, 8) <= 0)
		return 0;

	if (!root_port)
		return 1;

	rp = br_get_port(br, root_port);

	t = memcmp(&p->designated_root, &rp->designated_root, 8);
	if (t < 0)
		return 1;
	else if (t > 0)
		return 0;

	if (p->designated_cost + p->path_cost <
	    rp->designated_cost + rp->path_cost)
		return 1;
	else if (p->designated_cost + p->path_cost >
		 rp->designated_cost + rp->path_cost)
		return 0;

	t = memcmp(&p->designated_bridge, &rp->designated_bridge, 8);
	if (t < 0)
		return 1;
	else if (t > 0)
		return 0;

	if (p->designated_port < rp->designated_port)
		return 1;
	else if (p->designated_port > rp->designated_port)
		return 0;

	if (p->port_id < rp->port_id)
		return 1;

	return 0;
}

static void br_root_port_block(const struct net_bridge *br,
			       struct net_bridge_port *p)
{

	br_notice(br, "port %u(%s) tried to become root port (blocked)",
		  (unsigned int) p->port_no, p->dev->name);

	br_set_state(p, BR_STATE_LISTENING);
	br_log_state(p);
	br_ifinfo_notify(RTM_NEWLINK, p);

	if (br->forward_delay > 0)
		mod_timer(&p->forward_delay_timer, jiffies + br->forward_delay);
}

/* called under bridge lock */
static void br_root_selection(struct net_bridge *br)
{
	struct net_bridge_port *p;
	u16 root_port = 0;

	list_for_each_entry(p, &br->port_list, list) {
		if (!br_should_become_root_port(p, root_port))
			continue;

		if (p->flags & BR_ROOT_BLOCK)
			br_root_port_block(br, p);
		else
			root_port = p->port_no;
	}

	br->root_port = root_port;

	if (!root_port) {
		br->designated_root = br->bridge_id;
		br->root_path_cost = 0;
	} else {
		p = br_get_port(br, root_port);
		br->designated_root = p->designated_root;
		br->root_path_cost = p->designated_cost + p->path_cost;
	}
}

/* called under bridge lock */
void br_become_root_bridge(struct net_bridge *br)
{
	br->max_age = br->bridge_max_age;
	br->hello_time = br->bridge_hello_time;
	br->forward_delay = br->bridge_forward_delay;
	br_topology_change_detection(br);
	del_timer(&br->tcn_timer);

	if (br->dev->flags & IFF_UP) {
		br_config_bpdu_generation(br);
		mod_timer(&br->hello_timer, jiffies + br->hello_time);
	}
}

/* called under bridge lock */
void br_transmit_config(struct net_bridge_port *p)
{
	struct br_config_bpdu bpdu;
	struct net_bridge *br;

	if (timer_pending(&p->hold_timer)) {
		p->config_pending = 1;
		return;
	}

	br = p->br;

	bpdu.topology_change = br->topology_change;
	bpdu.topology_change_ack = p->topology_change_ack;
	bpdu.root = br->designated_root;
	bpdu.root_path_cost = br->root_path_cost;
	bpdu.bridge_id = br->bridge_id;
	bpdu.port_id = p->port_id;
	if (br_is_root_bridge(br))
		bpdu.message_age = 0;
	else {
		struct net_bridge_port *root
			= br_get_port(br, br->root_port);
		bpdu.message_age = (jiffies - root->designated_age)
			+ MESSAGE_AGE_INCR;
	}
	bpdu.max_age = br->max_age;
	bpdu.hello_time = br->hello_time;
	bpdu.forward_delay = br->forward_delay;

	if (bpdu.message_age < br->max_age) {
		br_send_config_bpdu(p, &bpdu);
		p->topology_change_ack = 0;
		p->config_pending = 0;
		if (p->br->stp_enabled == BR_KERNEL_STP)
			mod_timer(&p->hold_timer,
				  round_jiffies(jiffies + BR_HOLD_TIME));
	}
}

/* called under bridge lock */
static void br_record_config_information(struct net_bridge_port *p,
					 const struct br_config_bpdu *bpdu)
{
	p->designated_root = bpdu->root;
	p->designated_cost = bpdu->root_path_cost;
	p->designated_bridge = bpdu->bridge_id;
	p->designated_port = bpdu->port_id;
	p->designated_age = jiffies - bpdu->message_age;

	mod_timer(&p->message_age_timer, jiffies
		  + (bpdu->max_age - bpdu->message_age));
}

/* called under bridge lock */
static void br_record_config_timeout_values(struct net_bridge *br,
					    const struct br_config_bpdu *bpdu)
{
	br->max_age = bpdu->max_age;
	br->hello_time = bpdu->hello_time;
	br->forward_delay = bpdu->forward_delay;
	br->topology_change = bpdu->topology_change;
}

/* called under bridge lock */
void br_transmit_tcn(struct net_bridge *br)
{
	struct net_bridge_port *p;

	p = br_get_port(br, br->root_port);
	if (p)
		br_send_tcn_bpdu(p);
	else
		br_notice(br, "root port %u not found for topology notice\n",
			  br->root_port);
}

/* called under bridge lock */
static int br_should_become_designated_port(const struct net_bridge_port *p)
{
	struct net_bridge *br;
	int t;

	br = p->br;
	if (br_is_designated_port(p))
		return 1;

	if (memcmp(&p->designated_root, &br->designated_root, 8))
		return 1;

	if (br->root_path_cost < p->designated_cost)
		return 1;
	else if (br->root_path_cost > p->designated_cost)
		return 0;

	t = memcmp(&br->bridge_id, &p->designated_bridge, 8);
	if (t < 0)
		return 1;
	else if (t > 0)
		return 0;

	if (p->port_id < p->designated_port)
		return 1;

	return 0;
}

/* called under bridge lock */
static void br_designated_port_selection(struct net_bridge *br)
{
	struct net_bridge_port *p;

	list_for_each_entry(p, &br->port_list, list) {
		if (p->state != BR_STATE_DISABLED &&
		    br_should_become_designated_port(p))
			br_become_designated_port(p);

	}
}

/* called under bridge lock */
static int br_supersedes_port_info(const struct net_bridge_port *p,
				   const struct br_config_bpdu *bpdu)
{
	int t;

	t = memcmp(&bpdu->root, &p->designated_root, 8);
	if (t < 0)
		return 1;
	else if (t > 0)
		return 0;

	if (bpdu->root_path_cost < p->designated_cost)
		return 1;
	else if (bpdu->root_path_cost > p->designated_cost)
		return 0;

	t = memcmp(&bpdu->bridge_id, &p->designated_bridge, 8);
	if (t < 0)
		return 1;
	else if (t > 0)
		return 0;

	if (memcmp(&bpdu->bridge_id, &p->br->bridge_id, 8))
		return 1;

	if (bpdu->port_id <= p->designated_port)
		return 1;

	return 0;
}

/* called under bridge lock */
static void br_topology_change_acknowledged(struct net_bridge *br)
{
	br->topology_change_detected = 0;
	del_timer(&br->tcn_timer);
}

/* called under bridge lock */
void br_topology_change_detection(struct net_bridge *br)
{
	int isroot = br_is_root_bridge(br);

	if (br->stp_enabled != BR_KERNEL_STP)
		return;

	br_info(br, "topology change detected, %s\n",
		isroot ? "propagating" : "sending tcn bpdu");

	if (isroot) {
		br->topology_change = 1;
		mod_timer(&br->topology_change_timer, jiffies
			  + br->bridge_forward_delay + br->bridge_max_age);
	} else if (!br->topology_change_detected) {
		br_transmit_tcn(br);
		mod_timer(&br->tcn_timer, jiffies + br->bridge_hello_time);
	}

	br->topology_change_detected = 1;
}

/* called under bridge lock */
void br_config_bpdu_generation(struct net_bridge *br)
{
	struct net_bridge_port *p;

	list_for_each_entry(p, &br->port_list, list) {
		if (p->state != BR_STATE_DISABLED &&
		    br_is_designated_port(p))
			br_transmit_config(p);
	}
}

/* called under bridge lock */
static void br_reply(struct net_bridge_port *p)
{
	br_transmit_config(p);
}

/* called under bridge lock */
void br_configuration_update(struct net_bridge *br)
{
	br_root_selection(br);
	br_designated_port_selection(br);
}

/* called under bridge lock */
void br_become_designated_port(struct net_bridge_port *p)
{
	struct net_bridge *br;

	br = p->br;
	p->designated_root = br->designated_root;
	p->designated_cost = br->root_path_cost;
	p->designated_bridge = br->bridge_id;
	p->designated_port = p->port_id;
}


/* called under bridge lock */
static void br_make_blocking(struct net_bridge_port *p)
{
	if (p->state != BR_STATE_DISABLED &&
	    p->state != BR_STATE_BLOCKING) {
		if (p->state == BR_STATE_FORWARDING ||
		    p->state == BR_STATE_LEARNING)
			br_topology_change_detection(p->br);

		br_set_state(p, BR_STATE_BLOCKING);
		br_log_state(p);
		br_ifinfo_notify(RTM_NEWLINK, p);

		del_timer(&p->forward_delay_timer);
	}
}

/* called under bridge lock */
static void br_make_forwarding(struct net_bridge_port *p)
{
	struct net_bridge *br = p->br;

	if (p->state != BR_STATE_BLOCKING)
		return;

	if (br->stp_enabled == BR_NO_STP || br->forward_delay == 0) {
		br_set_state(p, BR_STATE_FORWARDING);
		br_topology_change_detection(br);
		del_timer(&p->forward_delay_timer);
	} else if (br->stp_enabled == BR_KERNEL_STP)
		br_set_state(p, BR_STATE_LISTENING);
	else
		br_set_state(p, BR_STATE_LEARNING);

	br_log_state(p);
	br_ifinfo_notify(RTM_NEWLINK, p);

	if (br->forward_delay != 0)
		mod_timer(&p->forward_delay_timer, jiffies + br->forward_delay);
}

/* called under bridge lock */
void br_port_state_selection(struct net_bridge *br)
{
	struct net_bridge_port *p;
	unsigned int liveports = 0;

	list_for_each_entry(p, &br->port_list, list) {
		if (p->state == BR_STATE_DISABLED)
			continue;

		/* Don't change port states if userspace is handling STP */
		if (br->stp_enabled != BR_USER_STP) {
			if (p->port_no == br->root_port) {
				p->config_pending = 0;
				p->topology_change_ack = 0;
				br_make_forwarding(p);
			} else if (br_is_designated_port(p)) {
				del_timer(&p->message_age_timer);
				br_make_forwarding(p);
			} else {
				p->config_pending = 0;
				p->topology_change_ack = 0;
				br_make_blocking(p);
			}
		}

		if (p->state != BR_STATE_BLOCKING)
			br_multicast_enable_port(p);
		/* Multicast is not disabled for the port when it goes in
		 * blocking state because the timers will expire and stop by
		 * themselves without sending more queries.
		 */
		if (p->state == BR_STATE_FORWARDING)
			++liveports;
	}

	if (liveports == 0)
		netif_carrier_off(br->dev);
	else
		netif_carrier_on(br->dev);
}

/* called under bridge lock */
static void br_topology_change_acknowledge(struct net_bridge_port *p)
{
	p->topology_change_ack = 1;
	br_transmit_config(p);
}

/* called under bridge lock */
void br_received_config_bpdu(struct net_bridge_port *p,
			     const struct br_config_bpdu *bpdu)
{
	struct net_bridge *br;
	int was_root;

	br = p->br;
	was_root = br_is_root_bridge(br);

	if (br_supersedes_port_info(p, bpdu)) {
		br_record_config_information(p, bpdu);
		br_configuration_update(br);
		br_port_state_selection(br);

		if (!br_is_root_bridge(br) && was_root) {
			del_timer(&br->hello_timer);
			if (br->topology_change_detected) {
				del_timer(&br->topology_change_timer);
				br_transmit_tcn(br);

				mod_timer(&br->tcn_timer,
					  jiffies + br->bridge_hello_time);
			}
		}

		if (p->port_no == br->root_port) {
			br_record_config_timeout_values(br, bpdu);
			br_config_bpdu_generation(br);
			if (bpdu->topology_change_ack)
				br_topology_change_acknowledged(br);
		}
	} else if (br_is_designated_port(p)) {
		br_reply(p);
	}
}

/* called under bridge lock */
void br_received_tcn_bpdu(struct net_bridge_port *p)
{
	if (br_is_designated_port(p)) {
		br_info(p->br, "port %u(%s) received tcn bpdu\n",
			(unsigned int) p->port_no, p->dev->name);

		br_topology_change_detection(p->br);
		br_topology_change_acknowledge(p);
	}
}

/* Change bridge STP parameter */
int br_set_hello_time(struct net_bridge *br, unsigned long val)
{
	unsigned long t = clock_t_to_jiffies(val);

	if (t < BR_MIN_HELLO_TIME || t > BR_MAX_HELLO_TIME)
		return -ERANGE;

	spin_lock_bh(&br->lock);
	br->bridge_hello_time = t;
	if (br_is_root_bridge(br))
		br->hello_time = br->bridge_hello_time;
	spin_unlock_bh(&br->lock);
	return 0;
}

int br_set_max_age(struct net_bridge *br, unsigned long val)
{
	unsigned long t = clock_t_to_jiffies(val);

	if (t < BR_MIN_MAX_AGE || t > BR_MAX_MAX_AGE)
		return -ERANGE;

	spin_lock_bh(&br->lock);
	br->bridge_max_age = t;
	if (br_is_root_bridge(br))
		br->max_age = br->bridge_max_age;
	spin_unlock_bh(&br->lock);
	return 0;

}

void __br_set_forward_delay(struct net_bridge *br, unsigned long t)
{
	br->bridge_forward_delay = t;
	if (br_is_root_bridge(br))
		br->forward_delay = br->bridge_forward_delay;
}

int br_set_forward_delay(struct net_bridge *br, unsigned long val)
{
	unsigned long t = clock_t_to_jiffies(val);
	int err = -ERANGE;

	spin_lock_bh(&br->lock);
	if (br->stp_enabled != BR_NO_STP &&
	    (t < BR_MIN_FORWARD_DELAY || t > BR_MAX_FORWARD_DELAY))
		goto unlock;

	__br_set_forward_delay(br, t);
	err = 0;

unlock:
	spin_unlock_bh(&br->lock);
	return err;
}
