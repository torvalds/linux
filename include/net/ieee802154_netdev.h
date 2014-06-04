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
#include <linux/netdevice.h>
#include <linux/skbuff.h>

struct ieee802154_sechdr {
#if defined(__LITTLE_ENDIAN_BITFIELD)
	u8 level:3,
	   key_id_mode:2,
	   reserved:3;
#elif defined(__BIG_ENDIAN_BITFIELD)
	u8 reserved:3,
	   key_id_mode:2,
	   level:3;
#else
#error	"Please fix <asm/byteorder.h>"
#endif
	u8 key_id;
	__le32 frame_counter;
	union {
		__le32 short_src;
		__le64 extended_src;
	};
};

struct ieee802154_addr {
	u8 mode;
	__le16 pan_id;
	union {
		__le16 short_addr;
		__le64 extended_addr;
	};
};

struct ieee802154_hdr_fc {
#if defined(__LITTLE_ENDIAN_BITFIELD)
	u16 type:3,
	    security_enabled:1,
	    frame_pending:1,
	    ack_request:1,
	    intra_pan:1,
	    reserved:3,
	    dest_addr_mode:2,
	    version:2,
	    source_addr_mode:2;
#elif defined(__BIG_ENDIAN_BITFIELD)
	u16 reserved:1,
	    intra_pan:1,
	    ack_request:1,
	    frame_pending:1,
	    security_enabled:1,
	    type:3,
	    source_addr_mode:2,
	    version:2,
	    dest_addr_mode:2,
	    reserved2:2;
#else
#error	"Please fix <asm/byteorder.h>"
#endif
};

struct ieee802154_hdr {
	struct ieee802154_hdr_fc fc;
	u8 seq;
	struct ieee802154_addr source;
	struct ieee802154_addr dest;
	struct ieee802154_sechdr sec;
};

/* pushes hdr onto the skb. fields of hdr->fc that can be calculated from
 * the contents of hdr will be, and the actual value of those bits in
 * hdr->fc will be ignored. this includes the INTRA_PAN bit and the frame
 * version, if SECEN is set.
 */
int ieee802154_hdr_push(struct sk_buff *skb, const struct ieee802154_hdr *hdr);

/* pulls the entire 802.15.4 header off of the skb, including the security
 * header, and performs pan id decompression
 */
int ieee802154_hdr_pull(struct sk_buff *skb, struct ieee802154_hdr *hdr);

/* parses the frame control, sequence number of address fields in a given skb
 * and stores them into hdr, performing pan id decompression and length checks
 * to be suitable for use in header_ops.parse
 */
int ieee802154_hdr_peek_addrs(const struct sk_buff *skb,
			      struct ieee802154_hdr *hdr);

static inline int ieee802154_hdr_length(struct sk_buff *skb)
{
	struct ieee802154_hdr hdr;
	int len = ieee802154_hdr_pull(skb, &hdr);

	if (len > 0)
		skb_push(skb, len);

	return len;
}

static inline bool ieee802154_addr_equal(const struct ieee802154_addr *a1,
					 const struct ieee802154_addr *a2)
{
	if (a1->pan_id != a2->pan_id || a1->mode != a2->mode)
		return false;

	if ((a1->mode == IEEE802154_ADDR_LONG &&
	     a1->extended_addr != a2->extended_addr) ||
	    (a1->mode == IEEE802154_ADDR_SHORT &&
	     a1->short_addr != a2->short_addr))
		return false;

	return true;
}

static inline __le64 ieee802154_devaddr_from_raw(const void *raw)
{
	u64 temp;

	memcpy(&temp, raw, IEEE802154_ADDR_LEN);
	return (__force __le64)swab64(temp);
}

static inline void ieee802154_devaddr_to_raw(void *raw, __le64 addr)
{
	u64 temp = swab64((__force u64)addr);

	memcpy(raw, &temp, IEEE802154_ADDR_LEN);
}

static inline void ieee802154_addr_from_sa(struct ieee802154_addr *a,
					   const struct ieee802154_addr_sa *sa)
{
	a->mode = sa->addr_type;
	a->pan_id = cpu_to_le16(sa->pan_id);

	switch (a->mode) {
	case IEEE802154_ADDR_SHORT:
		a->short_addr = cpu_to_le16(sa->short_addr);
		break;
	case IEEE802154_ADDR_LONG:
		a->extended_addr = ieee802154_devaddr_from_raw(sa->hwaddr);
		break;
	}
}

static inline void ieee802154_addr_to_sa(struct ieee802154_addr_sa *sa,
					 const struct ieee802154_addr *a)
{
	sa->addr_type = a->mode;
	sa->pan_id = le16_to_cpu(a->pan_id);

	switch (a->mode) {
	case IEEE802154_ADDR_SHORT:
		sa->short_addr = le16_to_cpu(a->short_addr);
		break;
	case IEEE802154_ADDR_LONG:
		ieee802154_devaddr_to_raw(sa->hwaddr, a->extended_addr);
		break;
	}
}

/*
 * A control block of skb passed between the ARPHRD_IEEE802154 device
 * and other stack parts.
 */
struct ieee802154_mac_cb {
	u8 lqi;
	u8 flags;
	u8 seq;
	struct ieee802154_addr source;
	struct ieee802154_addr dest;
};

static inline struct ieee802154_mac_cb *mac_cb(struct sk_buff *skb)
{
	return (struct ieee802154_mac_cb *)skb->cb;
}

#define MAC_CB_FLAG_TYPEMASK		((1 << 3) - 1)

#define MAC_CB_FLAG_ACKREQ		(1 << 3)
#define MAC_CB_FLAG_SECEN		(1 << 4)

static inline bool mac_cb_is_ackreq(struct sk_buff *skb)
{
	return mac_cb(skb)->flags & MAC_CB_FLAG_ACKREQ;
}

static inline bool mac_cb_is_secen(struct sk_buff *skb)
{
	return mac_cb(skb)->flags & MAC_CB_FLAG_SECEN;
}

static inline int mac_cb_type(struct sk_buff *skb)
{
	return mac_cb(skb)->flags & MAC_CB_FLAG_TYPEMASK;
}

#define IEEE802154_MAC_SCAN_ED		0
#define IEEE802154_MAC_SCAN_ACTIVE	1
#define IEEE802154_MAC_SCAN_PASSIVE	2
#define IEEE802154_MAC_SCAN_ORPHAN	3

struct ieee802154_mac_params {
	s8 transmit_power;
	u8 min_be;
	u8 max_be;
	u8 csma_retries;
	s8 frame_retries;

	bool lbt;
	u8 cca_mode;
	s32 cca_ed_level;
};

struct wpan_phy;
/*
 * This should be located at net_device->ml_priv
 *
 * get_phy should increment the reference counting on returned phy.
 * Use wpan_wpy_put to put that reference.
 */
struct ieee802154_mlme_ops {
	/* The following fields are optional (can be NULL). */

	int (*assoc_req)(struct net_device *dev,
			struct ieee802154_addr *addr,
			u8 channel, u8 page, u8 cap);
	int (*assoc_resp)(struct net_device *dev,
			struct ieee802154_addr *addr,
			__le16 short_addr, u8 status);
	int (*disassoc_req)(struct net_device *dev,
			struct ieee802154_addr *addr,
			u8 reason);
	int (*start_req)(struct net_device *dev,
			struct ieee802154_addr *addr,
			u8 channel, u8 page, u8 bcn_ord, u8 sf_ord,
			u8 pan_coord, u8 blx, u8 coord_realign);
	int (*scan_req)(struct net_device *dev,
			u8 type, u32 channels, u8 page, u8 duration);

	int (*set_mac_params)(struct net_device *dev,
			      const struct ieee802154_mac_params *params);
	void (*get_mac_params)(struct net_device *dev,
			       struct ieee802154_mac_params *params);

	/* The fields below are required. */

	struct wpan_phy *(*get_phy)(const struct net_device *dev);

	/*
	 * FIXME: these should become the part of PIB/MIB interface.
	 * However we still don't have IB interface of any kind
	 */
	__le16 (*get_pan_id)(const struct net_device *dev);
	__le16 (*get_short_addr)(const struct net_device *dev);
	u8 (*get_dsn)(const struct net_device *dev);
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
