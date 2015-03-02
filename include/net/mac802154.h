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
 */
#ifndef NET_MAC802154_H
#define NET_MAC802154_H

#include <net/af_ieee802154.h>
#include <linux/ieee802154.h>
#include <linux/skbuff.h>
#include <linux/unaligned/memmove.h>

#include <net/cfg802154.h>

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
#define IEEE802154_AFILT_SADDR_CHANGED		0x00000001
/* indicates that the IEEE Address changed */
#define IEEE802154_AFILT_IEEEADDR_CHANGED	0x00000002
/* indicates that the PAN ID changed */
#define IEEE802154_AFILT_PANID_CHANGED		0x00000004
/* indicates that PAN Coordinator status changed */
#define IEEE802154_AFILT_PANC_CHANGED		0x00000008

struct ieee802154_hw_addr_filt {
	__le16	pan_id;		/* Each independent PAN selects a unique
				 * identifier. This PAN id allows communication
				 * between devices within a network using short
				 * addresses and enables transmissions between
				 * devices across independent networks.
				 */
	__le16	short_addr;
	__le64	ieee_addr;
	u8	pan_coord;
};

struct ieee802154_vif {
	int type;

	/* must be last */
	u8 drv_priv[0] __aligned(sizeof(void *));
};

struct ieee802154_hw {
	/* filled by the driver */
	int	extra_tx_headroom;
	u32	flags;
	struct	device *parent;

	/* filled by mac802154 core */
	struct	ieee802154_hw_addr_filt hw_filt;
	void	*priv;
	struct	wpan_phy *phy;
	size_t vif_data_size;
};

/* Checksum is in hardware and is omitted from a packet
 *
 * These following flags are used to indicate hardware capabilities to
 * the stack. Generally, flags here should have their meaning
 * done in a way that the simplest hardware doesn't need setting
 * any particular flags. There are some exceptions to this rule,
 * however, so you are advised to review these flags carefully.
 */

/* Indicates that xmitter will add FCS on it's own. */
#define IEEE802154_HW_TX_OMIT_CKSUM	0x00000001
/* Indicates that receiver will autorespond with ACK frames. */
#define IEEE802154_HW_AACK		0x00000002
/* Indicates that transceiver will support transmit power setting. */
#define IEEE802154_HW_TXPOWER		0x00000004
/* Indicates that transceiver will support listen before transmit. */
#define IEEE802154_HW_LBT		0x00000008
/* Indicates that transceiver will support cca mode setting. */
#define IEEE802154_HW_CCA_MODE		0x00000010
/* Indicates that transceiver will support cca ed level setting. */
#define IEEE802154_HW_CCA_ED_LEVEL	0x00000020
/* Indicates that transceiver will support csma (max_be, min_be, csma retries)
 * settings. */
#define IEEE802154_HW_CSMA_PARAMS	0x00000040
/* Indicates that transceiver will support ARET frame retries setting. */
#define IEEE802154_HW_FRAME_RETRIES	0x00000080
/* Indicates that transceiver will support hardware address filter setting. */
#define IEEE802154_HW_AFILT		0x00000100
/* Indicates that transceiver will support promiscuous mode setting. */
#define IEEE802154_HW_PROMISCUOUS	0x00000200
/* Indicates that receiver omits FCS. */
#define IEEE802154_HW_RX_OMIT_CKSUM	0x00000400
/* Indicates that receiver will not filter frames with bad checksum. */
#define IEEE802154_HW_RX_DROP_BAD_CKSUM	0x00000800

/* Indicates that receiver omits FCS and xmitter will add FCS on it's own. */
#define IEEE802154_HW_OMIT_CKSUM	(IEEE802154_HW_TX_OMIT_CKSUM | \
					 IEEE802154_HW_RX_OMIT_CKSUM)

/* This groups the most common CSMA support fields into one. */
#define IEEE802154_HW_CSMA		(IEEE802154_HW_CCA_MODE | \
					 IEEE802154_HW_CCA_ED_LEVEL | \
					 IEEE802154_HW_CSMA_PARAMS)

/* This groups the most common ARET support fields into one. */
#define IEEE802154_HW_ARET		(IEEE802154_HW_CSMA | \
					 IEEE802154_HW_FRAME_RETRIES)

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
 * xmit_sync:
 *	  Handler that 802.15.4 module calls for each transmitted frame.
 *	  skb cntains the buffer starting from the IEEE 802.15.4 header.
 *	  The low-level driver should send the frame based on available
 *	  configuration. This is called by a workqueue and useful for
 *	  synchronous 802.15.4 drivers.
 *	  This function should return zero or negative errno.
 *
 *	  WARNING:
 *	  This will be deprecated soon. We don't accept synced xmit callbacks
 *	  drivers anymore.
 *
 * xmit_async:
 *	  Handler that 802.15.4 module calls for each transmitted frame.
 *	  skb cntains the buffer starting from the IEEE 802.15.4 header.
 *	  The low-level driver should send the frame based on available
 *	  configuration.
 *	  This function should return zero or negative errno.
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
 *
 * set_txpower:
 *	  Set radio transmit power in dB. Called with pib_lock held.
 *	  Returns either zero, or negative errno.
 *
 * set_lbt
 *	  Enables or disables listen before talk on the device. Called with
 *	  pib_lock held.
 *	  Returns either zero, or negative errno.
 *
 * set_cca_mode
 *	  Sets the CCA mode used by the device. Called with pib_lock held.
 *	  Returns either zero, or negative errno.
 *
 * set_cca_ed_level
 *	  Sets the CCA energy detection threshold in dBm. Called with pib_lock
 *	  held.
 *	  Returns either zero, or negative errno.
 *
 * set_csma_params
 *	  Sets the CSMA parameter set for the PHY. Called with pib_lock held.
 *	  Returns either zero, or negative errno.
 *
 * set_frame_retries
 *	  Sets the retransmission attempt limit. Called with pib_lock held.
 *	  Returns either zero, or negative errno.
 *
 * set_promiscuous_mode
 *	  Enables or disable promiscuous mode.
 */
struct ieee802154_ops {
	struct module	*owner;
	int		(*start)(struct ieee802154_hw *hw);
	void		(*stop)(struct ieee802154_hw *hw);
	int		(*xmit_sync)(struct ieee802154_hw *hw,
				     struct sk_buff *skb);
	int		(*xmit_async)(struct ieee802154_hw *hw,
				      struct sk_buff *skb);
	int		(*ed)(struct ieee802154_hw *hw, u8 *level);
	int		(*set_channel)(struct ieee802154_hw *hw, u8 page,
				       u8 channel);
	int		(*set_hw_addr_filt)(struct ieee802154_hw *hw,
					    struct ieee802154_hw_addr_filt *filt,
					    unsigned long changed);
	int		(*set_txpower)(struct ieee802154_hw *hw, int db);
	int		(*set_lbt)(struct ieee802154_hw *hw, bool on);
	int		(*set_cca_mode)(struct ieee802154_hw *hw,
					const struct wpan_phy_cca *cca);
	int		(*set_cca_ed_level)(struct ieee802154_hw *hw,
					    s32 level);
	int		(*set_csma_params)(struct ieee802154_hw *hw,
					   u8 min_be, u8 max_be, u8 retries);
	int		(*set_frame_retries)(struct ieee802154_hw *hw,
					     s8 retries);
	int             (*set_promiscuous_mode)(struct ieee802154_hw *hw,
						const bool on);
};

/**
 * ieee802154_be64_to_le64 - copies and convert be64 to le64
 * @le64_dst: le64 destination pointer
 * @be64_src: be64 source pointer
 */
static inline void ieee802154_be64_to_le64(void *le64_dst, const void *be64_src)
{
	__put_unaligned_memmove64(swab64p(be64_src), le64_dst);
}

/**
 * ieee802154_le64_to_be64 - copies and convert le64 to be64
 * @be64_dst: be64 destination pointer
 * @le64_src: le64 source pointer
 */
static inline void ieee802154_le64_to_be64(void *be64_dst, const void *le64_src)
{
	__put_unaligned_memmove64(swab64p(le64_src), be64_dst);
}

/* Basic interface to register ieee802154 hwice */
struct ieee802154_hw *
ieee802154_alloc_hw(size_t priv_data_len, const struct ieee802154_ops *ops);
void ieee802154_free_hw(struct ieee802154_hw *hw);
int ieee802154_register_hw(struct ieee802154_hw *hw);
void ieee802154_unregister_hw(struct ieee802154_hw *hw);

void ieee802154_rx(struct ieee802154_hw *hw, struct sk_buff *skb);
void ieee802154_rx_irqsafe(struct ieee802154_hw *hw, struct sk_buff *skb,
			   u8 lqi);

void ieee802154_wake_queue(struct ieee802154_hw *hw);
void ieee802154_stop_queue(struct ieee802154_hw *hw);
void ieee802154_xmit_complete(struct ieee802154_hw *hw, struct sk_buff *skb,
			      bool ifs_handling);

#endif /* NET_MAC802154_H */
