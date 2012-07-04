/*
 * An interface between IEEE802.15.4 device and rest of the kernel.
 *
 * Copyright (C) 2007-2012 Siemens AG
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
 * Written by:
 * Pavel Smolenskiy <pavel.smolenskiy@gmail.com>
 * Maxim Gorbachyov <maxim.gorbachev@siemens.com>
 * Maxim Osipov <maxim.osipov@siemens.com>
 * Dmitry Eremin-Solenikov <dbaryshkov@gmail.com>
 * Alexander Smirnov <alex.bluesman.smirnov@gmail.com>
 */

#ifndef IEEE802154_NETDEVICE_H
#define IEEE802154_NETDEVICE_H

#include <net/af_ieee802154.h>

/*
 * A control block of skb passed between the ARPHRD_IEEE802154 device
 * and other stack parts.
 */
struct ieee802154_mac_cb {
	u8 lqi;
	struct ieee802154_addr sa;
	struct ieee802154_addr da;
	u8 flags;
	u8 seq;
};

static inline struct ieee802154_mac_cb *mac_cb(struct sk_buff *skb)
{
	return (struct ieee802154_mac_cb *)skb->cb;
}

#define MAC_CB_FLAG_TYPEMASK		((1 << 3) - 1)

#define MAC_CB_FLAG_ACKREQ		(1 << 3)
#define MAC_CB_FLAG_SECEN		(1 << 4)
#define MAC_CB_FLAG_INTRAPAN		(1 << 5)

static inline int mac_cb_is_ackreq(struct sk_buff *skb)
{
	return mac_cb(skb)->flags & MAC_CB_FLAG_ACKREQ;
}

static inline int mac_cb_is_secen(struct sk_buff *skb)
{
	return mac_cb(skb)->flags & MAC_CB_FLAG_SECEN;
}

static inline int mac_cb_is_intrapan(struct sk_buff *skb)
{
	return mac_cb(skb)->flags & MAC_CB_FLAG_INTRAPAN;
}

static inline int mac_cb_type(struct sk_buff *skb)
{
	return mac_cb(skb)->flags & MAC_CB_FLAG_TYPEMASK;
}

#define IEEE802154_MAC_SCAN_ED		0
#define IEEE802154_MAC_SCAN_ACTIVE	1
#define IEEE802154_MAC_SCAN_PASSIVE	2
#define IEEE802154_MAC_SCAN_ORPHAN	3

struct wpan_phy;
/*
 * This should be located at net_device->ml_priv
 *
 * get_phy should increment the reference counting on returned phy.
 * Use wpan_wpy_put to put that reference.
 */
struct ieee802154_mlme_ops {
	int (*assoc_req)(struct net_device *dev,
			struct ieee802154_addr *addr,
			u8 channel, u8 page, u8 cap);
	int (*assoc_resp)(struct net_device *dev,
			struct ieee802154_addr *addr,
			u16 short_addr, u8 status);
	int (*disassoc_req)(struct net_device *dev,
			struct ieee802154_addr *addr,
			u8 reason);
	int (*start_req)(struct net_device *dev,
			struct ieee802154_addr *addr,
			u8 channel, u8 page, u8 bcn_ord, u8 sf_ord,
			u8 pan_coord, u8 blx, u8 coord_realign);
	int (*scan_req)(struct net_device *dev,
			u8 type, u32 channels, u8 page, u8 duration);

	struct wpan_phy *(*get_phy)(const struct net_device *dev);

	/*
	 * FIXME: these should become the part of PIB/MIB interface.
	 * However we still don't have IB interface of any kind
	 */
	u16 (*get_pan_id)(const struct net_device *dev);
	u16 (*get_short_addr)(const struct net_device *dev);
	u8 (*get_dsn)(const struct net_device *dev);
	u8 (*get_bsn)(const struct net_device *dev);
};

/* The IEEE 802.15.4 standard defines 2 type of the devices:
 * - FFD - full functionality device
 * - RFD - reduce functionality device
 *
 * So 2 sets of mlme operations are needed
 */
struct ieee802154_reduced_mlme_ops {
	struct wpan_phy *(*get_phy)(const struct net_device *dev);
};

static inline struct ieee802154_mlme_ops *
ieee802154_mlme_ops(const struct net_device *dev)
{
	return dev->ml_priv;
}

static inline struct ieee802154_reduced_mlme_ops *
ieee802154_reduced_mlme_ops(const struct net_device *dev)
{
	return dev->ml_priv;
}

#endif
