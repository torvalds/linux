/*
 * Copyright (c) 2014 Jiri Pirko <jiri@resnulli.us>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef __NET_TC_VLAN_H
#define __NET_TC_VLAN_H

#include <net/act_api.h>
#include <linux/tc_act/tc_vlan.h>

struct tcf_vlan_params {
	int               tcfv_action;
	u16               tcfv_push_vid;
	__be16            tcfv_push_proto;
	u8                tcfv_push_prio;
	struct rcu_head   rcu;
};

struct tcf_vlan {
	struct tc_action	common;
	struct tcf_vlan_params __rcu *vlan_p;
};
#define to_vlan(a) ((struct tcf_vlan *)a)

static inline bool is_tcf_vlan(const struct tc_action *a)
{
#ifdef CONFIG_NET_CLS_ACT
	if (a->ops && a->ops->id == TCA_ID_VLAN)
		return true;
#endif
	return false;
}

static inline u32 tcf_vlan_action(const struct tc_action *a)
{
	u32 tcfv_action;

	rcu_read_lock();
	tcfv_action = rcu_dereference(to_vlan(a)->vlan_p)->tcfv_action;
	rcu_read_unlock();

	return tcfv_action;
}

static inline u16 tcf_vlan_push_vid(const struct tc_action *a)
{
	u16 tcfv_push_vid;

	rcu_read_lock();
	tcfv_push_vid = rcu_dereference(to_vlan(a)->vlan_p)->tcfv_push_vid;
	rcu_read_unlock();

	return tcfv_push_vid;
}

static inline __be16 tcf_vlan_push_proto(const struct tc_action *a)
{
	__be16 tcfv_push_proto;

	rcu_read_lock();
	tcfv_push_proto = rcu_dereference(to_vlan(a)->vlan_p)->tcfv_push_proto;
	rcu_read_unlock();

	return tcfv_push_proto;
}

static inline u8 tcf_vlan_push_prio(const struct tc_action *a)
{
	u8 tcfv_push_prio;

	rcu_read_lock();
	tcfv_push_prio = rcu_dereference(to_vlan(a)->vlan_p)->tcfv_push_prio;
	rcu_read_unlock();

	return tcfv_push_prio;
}
#endif /* __NET_TC_VLAN_H */
