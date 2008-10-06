/*
 * File: pn_dev.h
 *
 * Phonet network device
 *
 * Copyright (C) 2008 Nokia Corporation.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 */

#ifndef PN_DEV_H
#define PN_DEV_H

struct phonet_device_list {
	struct list_head list;
	spinlock_t lock;
};

extern struct phonet_device_list pndevs;

struct phonet_device {
	struct list_head list;
	struct net_device *netdev;
	DECLARE_BITMAP(addrs, 64);
};

void phonet_device_init(void);
void phonet_device_exit(void);
struct net_device *phonet_device_get(struct net *net);

int phonet_address_add(struct net_device *dev, u8 addr);
int phonet_address_del(struct net_device *dev, u8 addr);
u8 phonet_address_get(struct net_device *dev, u8 addr);
int phonet_address_lookup(u8 addr);

#define PN_NO_ADDR	0xff

#endif
