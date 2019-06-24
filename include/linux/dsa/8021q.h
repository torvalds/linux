/* SPDX-License-Identifier: GPL-2.0
 * Copyright (c) 2019, Vladimir Oltean <olteanv@gmail.com>
 */

#ifndef _NET_DSA_8021Q_H
#define _NET_DSA_8021Q_H

#include <linux/types.h>

struct dsa_switch;
struct sk_buff;
struct net_device;
struct packet_type;

#if IS_ENABLED(CONFIG_NET_DSA_TAG_8021Q)

int dsa_port_setup_8021q_tagging(struct dsa_switch *ds, int index,
				 bool enabled);

struct sk_buff *dsa_8021q_xmit(struct sk_buff *skb, struct net_device *netdev,
			       u16 tpid, u16 tci);

u16 dsa_8021q_tx_vid(struct dsa_switch *ds, int port);

u16 dsa_8021q_rx_vid(struct dsa_switch *ds, int port);

int dsa_8021q_rx_switch_id(u16 vid);

int dsa_8021q_rx_source_port(u16 vid);

struct sk_buff *dsa_8021q_remove_header(struct sk_buff *skb);

#else

int dsa_port_setup_8021q_tagging(struct dsa_switch *ds, int index,
				 bool enabled)
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

int dsa_8021q_rx_switch_id(u16 vid)
{
	return 0;
}

int dsa_8021q_rx_source_port(u16 vid)
{
	return 0;
}

struct sk_buff *dsa_8021q_remove_header(struct sk_buff *skb)
{
	return NULL;
}

#endif /* IS_ENABLED(CONFIG_NET_DSA_TAG_8021Q) */

#endif /* _NET_DSA_8021Q_H */
