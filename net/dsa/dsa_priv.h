/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * net/dsa/dsa_priv.h - Hardware switch handling
 * Copyright (c) 2008-2009 Marvell Semiconductor
 */

#ifndef __DSA_PRIV_H
#define __DSA_PRIV_H

#include <linux/phy.h>
#include <linux/netdevice.h>
#include <net/dsa.h>

#define DSA_MAX_NUM_OFFLOADING_BRIDGES		BITS_PER_LONG

struct dsa_notifier_tag_8021q_vlan_info;

/* netlink.c */
extern struct rtnl_link_ops dsa_link_ops __read_mostly;

/* tag_8021q.c */
int dsa_switch_tag_8021q_vlan_add(struct dsa_switch *ds,
				  struct dsa_notifier_tag_8021q_vlan_info *info);
int dsa_switch_tag_8021q_vlan_del(struct dsa_switch *ds,
				  struct dsa_notifier_tag_8021q_vlan_info *info);

#endif
