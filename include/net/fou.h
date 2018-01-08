/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __NET_FOU_H
#define __NET_FOU_H

#include <linux/skbuff.h>

#include <net/flow.h>
#include <net/gue.h>
#include <net/ip_tunnels.h>
#include <net/udp.h>

size_t fou_encap_hlen(struct ip_tunnel_encap *e);
size_t gue_encap_hlen(struct ip_tunnel_encap *e);

int __fou_build_header(struct sk_buff *skb, struct ip_tunnel_encap *e,
		       u8 *protocol, __be16 *sport, int type);
int __gue_build_header(struct sk_buff *skb, struct ip_tunnel_encap *e,
		       u8 *protocol, __be16 *sport, int type);

#endif
