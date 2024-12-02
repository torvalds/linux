/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * File: pn_dev.h
 *
 * Phonet network device
 *
 * Copyright (C) 2008 Nokia Corporation.
 */

#ifndef PN_DEV_H
#define PN_DEV_H

#include <linux/list.h>
#include <linux/mutex.h>

struct net;

struct phonet_device_list {
	struct list_head list;
	struct mutex lock;
};

struct phonet_device_list *phonet_device_list(struct net *net);

struct phonet_device {
	struct list_head list;
	struct net_device *netdev;
	DECLARE_BITMAP(addrs, 64);
	struct rcu_head	rcu;
};

int phonet_device_init(void);
void phonet_device_exit(void);
int phonet_netlink_register(void);
struct net_device *phonet_device_get(struct net *net);

int phonet_address_add(struct net_device *dev, u8 addr);
int phonet_address_del(struct net_device *dev, u8 addr);
u8 phonet_address_get(struct net_device *dev, u8 addr);
int phonet_address_lookup(struct net *net, u8 addr);
void phonet_address_notify(int event, struct net_device *dev, u8 addr);

int phonet_route_add(struct net_device *dev, u8 daddr);
int phonet_route_del(struct net_device *dev, u8 daddr);
void rtm_phonet_notify(int event, struct net_device *dev, u8 dst);
struct net_device *phonet_route_get_rcu(struct net *net, u8 daddr);
struct net_device *phonet_route_output(struct net *net, u8 daddr);

#define PN_NO_ADDR	0xff

extern const struct seq_operations pn_sock_seq_ops;
extern const struct seq_operations pn_res_seq_ops;

#endif
