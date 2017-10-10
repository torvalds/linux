/*
 * Copyright (c) 2015 Nicira, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 */

#ifndef OVS_CONNTRACK_H
#define OVS_CONNTRACK_H 1

#include "flow.h"

struct ovs_conntrack_info;
enum ovs_key_attr;

#if IS_ENABLED(CONFIG_NF_CONNTRACK)
void ovs_ct_init(struct net *);
void ovs_ct_exit(struct net *);
bool ovs_ct_verify(struct net *, enum ovs_key_attr attr);
int ovs_ct_copy_action(struct net *, const struct nlattr *,
		       const struct sw_flow_key *, struct sw_flow_actions **,
		       bool log);
int ovs_ct_action_to_attr(const struct ovs_conntrack_info *, struct sk_buff *);

int ovs_ct_execute(struct net *, struct sk_buff *, struct sw_flow_key *,
		   const struct ovs_conntrack_info *);
int ovs_ct_clear(struct sk_buff *skb, struct sw_flow_key *key);

void ovs_ct_fill_key(const struct sk_buff *skb, struct sw_flow_key *key);
int ovs_ct_put_key(const struct sw_flow_key *swkey,
		   const struct sw_flow_key *output, struct sk_buff *skb);
void ovs_ct_free_action(const struct nlattr *a);

#define CT_SUPPORTED_MASK (OVS_CS_F_NEW | OVS_CS_F_ESTABLISHED | \
			   OVS_CS_F_RELATED | OVS_CS_F_REPLY_DIR | \
			   OVS_CS_F_INVALID | OVS_CS_F_TRACKED | \
			   OVS_CS_F_SRC_NAT | OVS_CS_F_DST_NAT)
#else
#include <linux/errno.h>

static inline void ovs_ct_init(struct net *net) { }

static inline void ovs_ct_exit(struct net *net) { }

static inline bool ovs_ct_verify(struct net *net, int attr)
{
	return false;
}

static inline int ovs_ct_copy_action(struct net *net, const struct nlattr *nla,
				     const struct sw_flow_key *key,
				     struct sw_flow_actions **acts, bool log)
{
	return -ENOTSUPP;
}

static inline int ovs_ct_action_to_attr(const struct ovs_conntrack_info *info,
					struct sk_buff *skb)
{
	return -ENOTSUPP;
}

static inline int ovs_ct_execute(struct net *net, struct sk_buff *skb,
				 struct sw_flow_key *key,
				 const struct ovs_conntrack_info *info)
{
	kfree_skb(skb);
	return -ENOTSUPP;
}

static inline int ovs_ct_clear(struct sk_buff *skb,
			       struct sw_flow_key *key)
{
	return -ENOTSUPP;
}

static inline void ovs_ct_fill_key(const struct sk_buff *skb,
				   struct sw_flow_key *key)
{
	key->ct_state = 0;
	key->ct_zone = 0;
	key->ct.mark = 0;
	memset(&key->ct.labels, 0, sizeof(key->ct.labels));
	/* Clear 'ct_orig_proto' to mark the non-existence of original
	 * direction key fields.
	 */
	key->ct_orig_proto = 0;
}

static inline int ovs_ct_put_key(const struct sw_flow_key *swkey,
				 const struct sw_flow_key *output,
				 struct sk_buff *skb)
{
	return 0;
}

static inline void ovs_ct_free_action(const struct nlattr *a) { }

#define CT_SUPPORTED_MASK 0
#endif /* CONFIG_NF_CONNTRACK */
#endif /* ovs_conntrack.h */
