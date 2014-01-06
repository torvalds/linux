/*
 * IEEE802.15.4-2003 specification
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
 */
#ifndef NET_MAC802154_H
#define NET_MAC802154_H

#include <net/af_ieee802154.h>

/* General MAC frame format:
 *  2 bytes: Frame Control
 *  1 byte:  Sequence Number
 * 20 bytes: Addressing fields
 * 14 bytes: Auxiliary Security Header
 */
#define MAC802154_FRAME_HARD_HEADER_LEN		(2 + 1 + 20 + 14)

/* The following flags are used to indicate changed address settings from
 * the stack to the hardware.
 */

/* indicates that the Short Address changed */
#define IEEE802515_AFILT_SADDR_CHANGED		0x00000001
/* indicates that the IEEE Address changed */
#define IEEE802515_AFILT_IEEEADDR_CHANGED	0x00000002
/* indicates that the PAN ID changed */
#define IEEE802515_AFILT_PANID_CHANGED		0x00000004
/* indicates that PAN Coordinator status changed */
#define	IEEE802515_AFILT_PANC_CHANGED		0x00000008

struct ieee802154_hw_addr_filt {
	__le16	pan_id;		/* Each independent PAN selects a unique
				 * identifier. This PAN id allows communication
				 * between devices within a network using short
				 * addresses and enables transmissions between
				 * devices across independent networks.
				 */
	__le16	short_addr;
	u8	ieee_addr[IEEE802154_ADDR_LEN];
	u8	pan_coord;
};

struct ieee802154_dev {
	/* filled by the driver */
	int	extra_tx_headroom;
	u32	flags;
	struct	device *parent;

	/* filled by mac802154 core */
	struct	ieee802154_hw_addr_filt hw_filt;
	void	*priv;
	struct	wpan_phy *phy;
};

/* Checksum is in hardware and is omitted from a packet
 *
 * These following flags are used to indicate hardware capabilities to
 * the stack. Generally, flags here should have their meaning
 * done in a way that the simplest hardware doesn't need setting
 * any particular flags. There are some exceptions to this rule,
 * however, so you are advised to review these flags carefully.
 */

/* Indicates that receiver omits FCS and xmitter will add FCS on it's own. */
#define	IEEE802154_HW_OMIT_CKSUM	0x00000001
/* Indicates that receiver will autorespond with ACK frames. */
#define	IEEE802154_HW_AACK		0x00000002

/* struct ieee802154_ops - callbacks from mac802154 to the driver
 *
 * This structure contains various callbacks that the driver may
 * handle or, in some cases, must handle, for example to transmit
 * a frame.
 *
 * start: Handler that 802.15.4 module calls for device initialization.
 *	  This function is called before the first interface is attached.
 *
 * stop:  Handler that 802.15.4 module calls for device cleanup.
 *	  This function is called after the last interface is removed.
 *
 * xmit:  Handler that 802.15.4 module calls for each transmitted frame.
 *	  skb cntains the buffer starting from the IEEE 802.15.4 header.
 *	  The low-level driver should send the frame based on available
 *	  configuration.
 *	  This function should return zero or negative errno. Called with
 *	  pib_lock held.
 *
 * ed:    Handler that 802.15.4 module calls for Energy Detection.
 *	  This function should place the value for detected energy
 *	  (usually device-dependant) in the level pointer and return
 *	  either zero or negative errno. Called with pib_lock held.
 *
 * set_channel:
 * 	  Set radio for listening on specific channel.
 *	  Set the device for listening on specified channel.
 *	  Returns either zero, or negative errno. Called with pib_lock held.
 *
 * set_hw_addr_filt:
 *	  Set radio for listening on specific address.
 *	  Set the device for listening on specified address.
 *	  Returns either zero, or negative errno.
 */
struct ieee802154_ops {
	struct module	*owner;
	int		(*start)(struct ieee802154_dev *dev);
	void		(*stop)(struct ieee802154_dev *dev);
	int		(*xmit)(struct ieee802154_dev *dev,
				struct sk_buff *skb);
	int		(*ed)(struct ieee802154_dev *dev, u8 *level);
	int		(*set_channel)(struct ieee802154_dev *dev,
				       int page,
				       int channel);
	int		(*set_hw_addr_filt)(struct ieee802154_dev *dev,
					  struct ieee802154_hw_addr_filt *filt,
					    unsigned long changed);
	int		(*ieee_addr)(struct ieee802154_dev *dev,
				     u8 addr[IEEE802154_ADDR_LEN]);
};

/* Basic interface to register ieee802154 device */
struct ieee802154_dev *
ieee802154_alloc_device(size_t priv_data_len, struct ieee802154_ops *ops);
void ieee802154_free_device(struct ieee802154_dev *dev);
int ieee802154_register_device(struct ieee802154_dev *dev);
void ieee802154_unregister_device(struct ieee802154_dev *dev);

void ieee802154_rx_irqsafe(struct ieee802154_dev *dev, struct sk_buff *skb,
			   u8 lqi);

#endif /* NET_MAC802154_H */
