/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NET_PSNAP_H
#define _NET_PSNAP_H

struct datalink_proto;
struct sk_buff;
struct packet_type;
struct net_device;

struct datalink_proto *
register_snap_client(const unsigned char *desc,
		     int (*rcvfunc)(struct sk_buff *, struct net_device *,
				    struct packet_type *,
				    struct net_device *orig_dev));
void unregister_snap_client(struct datalink_proto *proto);

#endif
