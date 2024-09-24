/* SPDX-License-Identifier: GPL-2.0
 * Copyright (c) 2019, Vladimir Oltean <olteanv@gmail.com>
 */

#ifndef _NET_DSA_8021Q_H
#define _NET_DSA_8021Q_H

#include <net/dsa.h>
#include <linux/types.h>

/* VBID is limited to three bits only and zero is reserved.
 * Only 7 bridges can be enumerated.
 */
#define DSA_TAG_8021Q_MAX_NUM_BRIDGES	7

int dsa_tag_8021q_register(struct dsa_switch *ds, __be16 proto);

void dsa_tag_8021q_unregister(struct dsa_switch *ds);

int dsa_tag_8021q_bridge_join(struct dsa_switch *ds, int port,
			      struct dsa_bridge bridge, bool *tx_fwd_offload,
			      struct netlink_ext_ack *extack);

void dsa_tag_8021q_bridge_leave(struct dsa_switch *ds, int port,
				struct dsa_bridge bridge);

u16 dsa_tag_8021q_bridge_vid(unsigned int bridge_num);

u16 dsa_tag_8021q_standalone_vid(const struct dsa_port *dp);

int dsa_8021q_rx_switch_id(u16 vid);

int dsa_8021q_rx_source_port(u16 vid);

bool vid_is_dsa_8021q(u16 vid);

#endif /* _NET_DSA_8021Q_H */
