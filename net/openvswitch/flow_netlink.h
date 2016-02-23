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

size_t ovs_tun_key_attr_size(void);
size_t ovs_key_attr_size(void);

void ovs_match_init(struct sw_flow_match *match,
		    struct sw_flow_key *key, struct sw_flow_mask *mask);

int ovs_nla_put_key(const struct sw_flow_key *, const struct sw_flow_key *,
		    int attr, bool is_mask, struct sk_buff *);
int ovs_nla_get_flow_metadata(struct net *, const struct nlattr *,
			      struct sw_flow_key *, bool log);

int ovs_nla_put_identifier(const struct sw_flow *flow, struct sk_buff *skb);
int ovs_nla_put_masked_key(const struct sw_flow *flow, struct sk_buff *skb);
int ovs_nla_put_mask(const struct sw_flow *flow, struct sk_buff *skb);

int ovs_nla_get_match(struct net *, struct sw_flow_match *,
		      const struct nlattr *key, const struct nlattr *mask,
		      bool log);

int ovs_nla_put_tunnel_info(struct sk_buff *skb,
			    struct ip_tunnel_info *tun_info);

bool ovs_nla_get_ufid(struct sw_flow_id *, const struct nlattr *, bool log);
int ovs_nla_get_identifier(struct sw_flow_id *sfid, const struct nlattr *ufid,
			   const struct sw_flow_key *key, bool log);
u32 ovs_nla_get_ufid_flags(const struct nlattr *attr);

int ovs_nla_copy_actions(struct net *net, const struct nlattr *attr,
			 const struct sw_flow_key *key,
			 struct sw_flow_actions **sfa, bool log);
int ovs_nla_add_action(struct sw_flow_actions **sfa, int attrtype,
		       void *data, int len, bool log);
int ovs_nla_put_actions(const struct nlattr *attr,
			int len, struct sk_buff *skb);

void ovs_nla_free_flow_actions(struct sw_flow_actions *);
void ovs_nla_free_flow_actions_rcu(struct sw_flow_actions *);

#endif /* flow_netlink.h */
