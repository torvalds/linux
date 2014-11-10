/*
 * Internal interfaces for ieee 802.15.4 address family.
 *
 * Copyright 2007, 2008, 2009 Siemens AG
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * Written by:
 * Sergey Lapin <slapin@ossfans.org>
 * Dmitry Eremin-Solenikov <dbaryshkov@gmail.com>
 */

#ifndef AF802154_H
#define AF802154_H

struct sk_buff;
struct net_device;
struct ieee802154_addr;
extern struct proto ieee802154_raw_prot;
extern struct proto ieee802154_dgram_prot;
void ieee802154_raw_deliver(struct net_device *dev, struct sk_buff *skb);
int ieee802154_dgram_deliver(struct net_device *dev, struct sk_buff *skb);
struct net_device *ieee802154_get_dev(struct net *net,
				      const struct ieee802154_addr *addr);

#endif
