/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright 2011-2014 Autronica Fire and Security AS
 *
 * Author(s):
 *	2011-2014 Arvid Brodin, arvid.brodin@alten.se
 */

#ifndef __HSR_NETLINK_H
#define __HSR_NETLINK_H

#include <linux/if_ether.h>
#include <linux/module.h>
#include <uapi/linux/hsr_netlink.h>

struct hsr_priv;
struct hsr_port;

int __init hsr_netlink_init(void);
void __exit hsr_netlink_exit(void);

void hsr_nl_ringerror(struct hsr_priv *hsr, unsigned char addr[ETH_ALEN],
		      struct hsr_port *port);
void hsr_nl_nodedown(struct hsr_priv *hsr, unsigned char addr[ETH_ALEN]);
void hsr_nl_framedrop(int dropcount, int dev_idx);
void hsr_nl_linkdown(int dev_idx);

#endif /* __HSR_NETLINK_H */
