/*
 * Copyright (c) 2007-2013 Nicira, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA
 */


#ifndef FLOW_NETLINK_H
#define FLOW_NETLINK_H 1

#include <linux/kernel.h>
#include <linux/netlink.h>
#include <linux/openvswitch.h>
#include <linux/spinlock.h>
#include <linux/types.h>
#include <linux/rcupdate.h>
#include <linux/if_ether.h>
#include <linux/in6.h>
#include <linux/jiffies.h>
#include <linux/time.h>
#include <linux/flex_array.h>

#include <net/inet_ecn.h>
#include <net/ip_tunnels.h>

#include "flow.h"

void ovs_match_init(struct sw_flow_match *match,
		    struct sw_flow_key *key, struct sw_flow_mask *mask);

int ovs_nla_put_flow(const struct sw_flow_key *,
		     const struct sw_flow_key *, struct sk_buff *);
int ovs_nla_get_flow_metadata(struct sw_flow *flow,
			      const struct nlattr *attr);
int ovs_nla_get_match(struct sw_flow_match *match,
		      const struct nlattr *,
		      const struct nlattr *);

int ovs_nla_copy_actions(const struct nlattr *attr,
			 const struct sw_flow_key *key, int depth,
			 struct sw_flow_actions **sfa);
int ovs_nla_put_actions(const struct nlattr *attr,
			int len, struct sk_buff *skb);

struct sw_flow_actions *ovs_nla_alloc_flow_actions(int actions_len);
void ovs_nla_free_flow_actions(struct sw_flow_actions *);

#endif /* flow_netlink.h */
