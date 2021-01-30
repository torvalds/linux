/* SPDX-License-Identifier: GPL-2.0
 * Copyright (c) 2019, Vladimir Oltean <olteanv@gmail.com>
 */

#ifndef _NET_DSA_8021Q_H
#define _NET_DSA_8021Q_H

#include <linux/refcount.h>
#include <linux/types.h>

struct dsa_switch;
struct sk_buff;
struct net_device;
struct packet_type;
struct dsa_8021q_context;

struct dsa_8021q_crosschip_link {
	struct list_head list;
	int port;
	struct dsa_8021q_context *other_ctx;
	int other_port;
	refcount_t refcount;
};

struct dsa_8021q_ops {
	int (*vlan_add)(struct dsa_switch *ds, int port, u16 vid, u16 flags);
	int (*vlan_del)(struct dsa_switch *ds, int port, u16 vid);
};

struct dsa_8021q_context {
	const struct dsa_8021q_ops *ops;
	struct dsa_switch *ds;
	struct list_head crosschip_links;
	/* EtherType of RX VID, used for filtering on master interface */
	__be16 proto;
};

#define DSA_8021Q_N_SUBVLAN			8

#if IS_ENABLED(CONFIG_NET_DSA_TAG_8021Q)

int dsa_8021q_setup(struct dsa_8021q_context *ctx, bool enabled);

int dsa_8021q_crosschip_bridge_join(struct dsa_8021q_context *ctx, int port,
				    struct dsa_8021q_context *other_ctx,
				    int other_port);

int dsa_8021q_crosschip_bridge_leave(struct dsa_8021q_context *ctx, int port,
				     struct dsa_8021q_context *other_ctx,
				     int other_port);

struct sk_buff *dsa_8021q_xmit(struct sk_buff *skb, struct net_device *netdev,
			       u16 tpid, u16 tci);

u16 dsa_8021q_tx_vid(struct dsa_switch *ds, int port);

u16 dsa_8021q_rx_vid(struct dsa_switch *ds, int port);

u16 dsa_8021q_rx_vid_subvlan(struct dsa_switch *ds, int port, u16 subvlan);

int dsa_8021q_rx_switch_id(u16 vid);

int dsa_8021q_rx_source_port(u16 vid);

u16 dsa_8021q_rx_subvlan(u16 vid);

bool vid_is_dsa_8021q_rxvlan(u16 vid);

bool vid_is_dsa_8021q_txvlan(u16 vid);

bool vid_is_dsa_8021q(u16 vid);

#else

int dsa_8021q_setup(struct dsa_8021q_context *ctx, bool enabled)
{
	return 0;
}

int dsa_8021q_crosschip_bridge_join(struct dsa_8021q_context *ctx, int port,
				    struct dsa_8021q_context *other_ctx,
				    int other_port)
{
	return 0;
}

int dsa_8021q_crosschip_bridge_leave(struct dsa_8021q_context *ctx, int port,
				     struct dsa_8021q_context *other_ctx,
				     int other_port)
{
	return 0;
}

struct sk_buff *dsa_8021q_xmit(struct sk_buff *skb, struct net_device *netdev,
			       u16 tpid, u16 tci)
{
	return NULL;
}

u16 dsa_8021q_tx_vid(struct dsa_switch *ds, int port)
{
	return 0;
}

u16 dsa_8021q_rx_vid(struct dsa_switch *ds, int port)
{
	return 0;
}

u16 dsa_8021q_rx_vid_subvlan(struct dsa_switch *ds, int port, u16 subvlan)
{
	return 0;
}

int dsa_8021q_rx_switch_id(u16 vid)
{
	return 0;
}

int dsa_8021q_rx_source_port(u16 vid)
{
	return 0;
}

u16 dsa_8021q_rx_subvlan(u16 vid)
{
	return 0;
}

bool vid_is_dsa_8021q_rxvlan(u16 vid)
{
	return false;
}

bool vid_is_dsa_8021q_txvlan(u16 vid)
{
	return false;
}

bool vid_is_dsa_8021q(u16 vid)
{
	return false;
}

#endif /* IS_ENABLED(CONFIG_NET_DSA_TAG_8021Q) */

#endif /* _NET_DSA_8021Q_H */
