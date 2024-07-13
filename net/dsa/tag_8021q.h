/* SPDX-License-Identifier: GPL-2.0-or-later */

#ifndef __DSA_TAG_8021Q_H
#define __DSA_TAG_8021Q_H

#include <net/dsa.h>

#include "switch.h"

struct sk_buff;
struct net_device;

struct sk_buff *dsa_8021q_xmit(struct sk_buff *skb, struct net_device *netdev,
			       u16 tpid, u16 tci);

void dsa_8021q_rcv(struct sk_buff *skb, int *source_port, int *switch_id,
		   int *vbid, int *vid);

struct net_device *dsa_tag_8021q_find_user(struct net_device *conduit,
					   int source_port, int switch_id,
					   int vid, int vbid);

int dsa_switch_tag_8021q_vlan_add(struct dsa_switch *ds,
				  struct dsa_notifier_tag_8021q_vlan_info *info);
int dsa_switch_tag_8021q_vlan_del(struct dsa_switch *ds,
				  struct dsa_notifier_tag_8021q_vlan_info *info);

#endif
