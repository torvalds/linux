/*
 * Copyright (c) 2016, Jamal Hadi Salim
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
*/

#ifndef __NET_TC_SKBMOD_H
#define __NET_TC_SKBMOD_H

#include <net/act_api.h>
#include <linux/tc_act/tc_skbmod.h>

struct tcf_skbmod_params {
	struct rcu_head	rcu;
	u64	flags; /*up to 64 types of operations; extend if needed */
	u8	eth_dst[ETH_ALEN];
	u16	eth_type;
	u8	eth_src[ETH_ALEN];
};

struct tcf_skbmod {
	struct tc_action	common;
	struct tcf_skbmod_params __rcu *skbmod_p;
};
#define to_skbmod(a) ((struct tcf_skbmod *)a)

#endif /* __NET_TC_SKBMOD_H */
