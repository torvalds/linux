/*
 * nl802154.h
 *
 * Copyright (C) 2007, 2008, 2009 Siemens AG
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
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */

#ifndef IEEE802154_NL_H
#define IEEE802154_NL_H

struct net_device;
struct ieee802154_addr;

int ieee802154_nl_assoc_indic(struct net_device *dev,
		struct ieee802154_addr *addr, u8 cap);
int ieee802154_nl_assoc_confirm(struct net_device *dev,
		u16 short_addr, u8 status);
int ieee802154_nl_disassoc_indic(struct net_device *dev,
		struct ieee802154_addr *addr, u8 reason);
int ieee802154_nl_disassoc_confirm(struct net_device *dev,
		u8 status);
int ieee802154_nl_scan_confirm(struct net_device *dev,
		u8 status, u8 scan_type, u32 unscanned,
		u8 *edl/*, struct list_head *pan_desc_list */);
int ieee802154_nl_beacon_indic(struct net_device *dev, u16 panid,
		u16 coord_addr);

#endif
