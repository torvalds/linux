/* SPDX-License-Identifier: GPL-2.0
 * Copyright (c) 2019, Vladimir Oltean <olteanv@gmail.com>
 */

#ifndef _NET_DSA_8021Q_H
#define _NET_DSA_8021Q_H

#include <linux/refcount.h>
#include <linux/types.h>
#include <net/dsa.h>

struct dsa_switch;
struct dsa_port;
struct sk_buff;
struct net_device;

struct dsa_tag_8021q_vlan {
	struct list_head list;
	int port;
	u16 vid;
	refcount_t refcount;
};

struct dsa_8021q_context {
	struct dsa_switch *ds;
	struct list_head vlans;
	/* EtherType of RX VID, used for filtering on master interface */
	__be16 proto;
};

int dsa_tag_8021q_register(struct dsa_switch *ds, __be16 proto);

void dsa_tag_8021q_unregister(struct dsa_switch *ds);

struct sk_buff *dsa_8021q_xmit(struct sk_buff *skb, struct net_device *netdev,
			       u16 tpid, u16 tci);

void dsa_8021q_rcv(struct sk_buff *skb, int *source_port, int *switch_id);

int dsa_tag_8021q_bridge_tx_fwd_offload(struct dsa_switch *ds, int port,
					struct dsa_bridge bridge);

void dsa_tag_8021q_bridge_tx_fwd_unoffload(struct dsa_switch *ds, int port,
					   struct dsa_bridge bridge);

u16 dsa_8021q_bridge_tx_fwd_offload_vid(unsigned int bridge_num);

u16 dsa_tag_8021q_tx_vid(const struct dsa_port *dp);

u16 dsa_tag_8021q_rx_vid(const struct dsa_port *dp);

int dsa_8021q_rx_switch_id(u16 vid);

int dsa_8021q_rx_source_port(u16 vid);

bool vid_is_dsa_8021q_rxvlan(u16 vid);

bool vid_is_dsa_8021q_txvlan(u16 vid);

bool vid_is_dsa_8021q(u16 vid);

#endif /* _NET_DSA_8021Q_H */
