/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NET_P8022_H
#define _NET_P8022_H

struct net_device;
struct packet_type;
struct sk_buff;

struct datalink_proto *
register_8022_client(unsigned char type,
		     int (*func)(struct sk_buff *skb,
				 struct net_device *dev,
				 struct packet_type *pt,
				 struct net_device *orig_dev));
void unregister_8022_client(struct datalink_proto *proto);

struct datalink_proto *make_8023_client(void);
void destroy_8023_client(struct datalink_proto *dl);
#endif
