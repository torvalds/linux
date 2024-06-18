/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * IEEE802.15.4-2003 specification
 *
 * Copyright (C) 2007-2012 Siemens AG
 */
#ifndef NET_MAC802154_H
#define NET_MAC802154_H

#include <asm/unaligned.h>
#include <net/af_ieee802154.h>
#include <linux/ieee802154.h>
#include <linux/skbuff.h>

#include <net/cfg802154.h>

/**
 * enum ieee802154_hw_addr_filt_flags - hardware address filtering flags
 *
 * The following flags are used to indicate changed address settings from
 * the stack to the hardware.
 *
 * @IEEE802154_AFILT_SADDR_CHANGED: Indicates that the short address will be
 *	change.
 *
 * @IEEE802154_AFILT_IEEEADDR_CHANGED: Indicates that the extended address
 *	will be change.
 *
 * @IEEE802154_AFILT_PANID_CHANGED: Indicates that the pan id will be change.
 *
 * @IEEE802154_AFILT_PANC_CHANGED: Indicates that the address filter will
 *	do frame address filtering as a pan coordinator.
 */
enum ieee802154_hw_addr_filt_flags {
	IEEE802154_AFILT_SADDR_CHANGED		= BIT(0),
	IEEE802154_AFILT_IEEEADDR_CHANGED	= BIT(1),
	IEEE802154_AFILT_PANID_CHANGED		= BIT(2),
	IEEE802154_AFILT_PANC_CHANGED		= BIT(3),
};

/**
 * struct ieee802154_hw_addr_filt - hardware address filtering settings
 *
 * @pan_id: pan_id which should be set to the hardware address filter.
 *
 * @short_addr: short_addr which should be set to the hardware address filter.
 *
 * @ieee_addr: extended address which should be set to the hardware address
 *	filter.
 *
 * @pan_coord: boolean if hardware filtering should be operate as coordinator.
 */
struct ieee802154_hw_addr_filt {
	__le16	pan_id;
	__le16	short_addr;
	__le64	ieee_addr;
	bool	pan_coord;
};

/**
 * struct ieee802154_hw - ieee802154 hardware
 *
 * @extra_tx_headroom: headroom to reserve in each transmit skb for use by the
 *	driver (e.g. for transmit headers.)
 *
 * @flags: hardware flags, see &enum ieee802154_hw_flags
 *
 * @parent: parent device of the hardware.
 *
 * @priv: pointer to private area that was allocated for driver use along with
 *	this structure.
 *
 * @phy: This points to the &struct wpan_phy allocated for this 802.15.4 PHY.
 */
struct ieee802154_hw {
	/* filled by the driver */
	int	extra_tx_headroom;
	u32	flags;
	struct	device *parent;
	void	*priv;

	/* filled by mac802154 core */
	struct	wpan_phy *phy;
};

/**
 * enum ieee802154_hw_flags - hardware flags
 *
 * These flags are used to indicate hardware capabilities to
 * the stack. Generally, flags here should have their meaning
 * done in a way that the simplest hardware doesn't need setting
 * any particular flags. There are some exceptions to this rule,
 * however, so you are advised to review these flags carefully.
 *
 * @IEEE802154_HW_TX_OMIT_CKSUM: Indicates that xmitter will add FCS on it's
 *	own.
 *
 * @IEEE802154_HW_LBT: Indicates that transceiver will support listen before
 *	transmit.
 *
 * @IEEE802154_HW_CSMA_PARAMS: Indicates that transceiver will support csma
 *	parameters (max_be, min_be, backoff exponents).
 *
 * @IEEE802154_HW_FRAME_RETRIES: Indicates that transceiver will support ARET
 *	frame retries setting.
 *
 * @IEEE802154_HW_AFILT: Indicates that transceiver will support hardware
 *	address filter setting.
 *
 * @IEEE802154_HW_PROMISCUOUS: Indicates that transceiver will support
 *	promiscuous mode setting.
 *
 * @IEEE802154_HW_RX_OMIT_CKSUM: Indicates that receiver omits FCS.
 */
enum ieee802154_hw_flags {
	IEEE802154_HW_TX_OMIT_CKSUM	= BIT(0),
	IEEE802154_HW_LBT		= BIT(1),
	IEEE802154_HW_CSMA_PARAMS	= BIT(2),
	IEEE802154_HW_FRAME_RETRIES	= BIT(3),
	IEEE802154_HW_AFILT		= BIT(4),
	IEEE802154_HW_PROMISCUOUS	= BIT(5),
	IEEE802154_HW_RX_OMIT_CKSUM	= BIT(6),
};

/* Indicates that receiver omits FCS and xmitter will add FCS on it's own. */
#define IEEE802154_HW_OMIT_CKSUM	(IEEE802154_HW_TX_OMIT_CKSUM | \
					 IEEE802154_HW_RX_OMIT_CKSUM)

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
 *	  Set radio transmit power in mBm. Called with pib_lock held.
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
 *	  Sets the CCA energy detection threshold in mBm. Called with pib_lock
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
	int		(*set_txpower)(struct ieee802154_hw *hw, s32 mbm);
	int		(*set_lbt)(struct ieee802154_hw *hw, bool on);
	int		(*set_cca_mode)(struct ieee802154_hw *hw,
					const struct wpan_phy_cca *cca);
	int		(*set_cca_ed_level)(struct ieee802154_hw *hw, s32 mbm);
	int		(*set_csma_params)(struct ieee802154_hw *hw,
					   u8 min_be, u8 max_be, u8 retries);
	int		(*set_frame_retries)(struct ieee802154_hw *hw,
					     s8 retries);
	int             (*set_promiscuous_mode)(struct ieee802154_hw *hw,
						const bool on);
};

/**
 * ieee802154_get_fc_from_skb - get the frame control field from an skb
 * @skb: skb where the frame control field will be get from
 */
static inline __le16 ieee802154_get_fc_from_skb(const struct sk_buff *skb)
{
	__le16 fc;

	/* check if we can fc at skb_mac_header of sk buffer */
	if (WARN_ON(!skb_mac_header_was_set(skb) ||
		    (skb_tail_pointer(skb) -
		     skb_mac_header(skb)) < IEEE802154_FC_LEN))
		return cpu_to_le16(0);

	memcpy(&fc, skb_mac_header(skb), IEEE802154_FC_LEN);
	return fc;
}

/**
 * ieee802154_skb_dst_pan - get the pointer to destination pan field
 * @fc: mac header frame control field
 * @skb: skb where the destination pan pointer will be get from
 */
static inline unsigned char *ieee802154_skb_dst_pan(__le16 fc,
						    const struct sk_buff *skb)
{
	unsigned char *dst_pan;

	switch (ieee802154_daddr_mode(fc)) {
	case cpu_to_le16(IEEE802154_FCTL_ADDR_NONE):
		dst_pan = NULL;
		break;
	case cpu_to_le16(IEEE802154_FCTL_DADDR_SHORT):
	case cpu_to_le16(IEEE802154_FCTL_DADDR_EXTENDED):
		dst_pan = skb_mac_header(skb) +
			  IEEE802154_FC_LEN +
			  IEEE802154_SEQ_LEN;
		break;
	default:
		WARN_ONCE(1, "invalid addr mode detected");
		dst_pan = NULL;
		break;
	}

	return dst_pan;
}

/**
 * ieee802154_skb_src_pan - get the pointer to source pan field
 * @fc: mac header frame control field
 * @skb: skb where the source pan pointer will be get from
 */
static inline unsigned char *ieee802154_skb_src_pan(__le16 fc,
						    const struct sk_buff *skb)
{
	unsigned char *src_pan;

	switch (ieee802154_saddr_mode(fc)) {
	case cpu_to_le16(IEEE802154_FCTL_ADDR_NONE):
		src_pan = NULL;
		break;
	case cpu_to_le16(IEEE802154_FCTL_SADDR_SHORT):
	case cpu_to_le16(IEEE802154_FCTL_SADDR_EXTENDED):
		/* if intra-pan and source addr mode is non none,
		 * then source pan id is equal destination pan id.
		 */
		if (ieee802154_is_intra_pan(fc)) {
			src_pan = ieee802154_skb_dst_pan(fc, skb);
			break;
		}

		switch (ieee802154_daddr_mode(fc)) {
		case cpu_to_le16(IEEE802154_FCTL_ADDR_NONE):
			src_pan = skb_mac_header(skb) +
				  IEEE802154_FC_LEN +
				  IEEE802154_SEQ_LEN;
			break;
		case cpu_to_le16(IEEE802154_FCTL_DADDR_SHORT):
			src_pan = skb_mac_header(skb) +
				  IEEE802154_FC_LEN +
				  IEEE802154_SEQ_LEN +
				  IEEE802154_PAN_ID_LEN +
				  IEEE802154_SHORT_ADDR_LEN;
			break;
		case cpu_to_le16(IEEE802154_FCTL_DADDR_EXTENDED):
			src_pan = skb_mac_header(skb) +
				  IEEE802154_FC_LEN +
				  IEEE802154_SEQ_LEN +
				  IEEE802154_PAN_ID_LEN +
				  IEEE802154_EXTENDED_ADDR_LEN;
			break;
		default:
			WARN_ONCE(1, "invalid addr mode detected");
			src_pan = NULL;
			break;
		}
		break;
	default:
		WARN_ONCE(1, "invalid addr mode detected");
		src_pan = NULL;
		break;
	}

	return src_pan;
}

/**
 * ieee802154_skb_is_intra_pan_addressing - checks whenever the mac addressing
 *	is an intra pan communication
 * @fc: mac header frame control field
 * @skb: skb where the source and destination pan should be get from
 */
static inline bool ieee802154_skb_is_intra_pan_addressing(__le16 fc,
							  const struct sk_buff *skb)
{
	unsigned char *dst_pan = ieee802154_skb_dst_pan(fc, skb),
		      *src_pan = ieee802154_skb_src_pan(fc, skb);

	/* if one is NULL is no intra pan addressing */
	if (!dst_pan || !src_pan)
		return false;

	return !memcmp(dst_pan, src_pan, IEEE802154_PAN_ID_LEN);
}

/**
 * ieee802154_be64_to_le64 - copies and convert be64 to le64
 * @le64_dst: le64 destination pointer
 * @be64_src: be64 source pointer
 */
static inline void ieee802154_be64_to_le64(void *le64_dst, const void *be64_src)
{
	put_unaligned_le64(get_unaligned_be64(be64_src), le64_dst);
}

/**
 * ieee802154_le64_to_be64 - copies and convert le64 to be64
 * @be64_dst: be64 destination pointer
 * @le64_src: le64 source pointer
 */
static inline void ieee802154_le64_to_be64(void *be64_dst, const void *le64_src)
{
	put_unaligned_be64(get_unaligned_le64(le64_src), be64_dst);
}

/**
 * ieee802154_le16_to_be16 - copies and convert le16 to be16
 * @be16_dst: be16 destination pointer
 * @le16_src: le16 source pointer
 */
static inline void ieee802154_le16_to_be16(void *be16_dst, const void *le16_src)
{
	put_unaligned_be16(get_unaligned_le16(le16_src), be16_dst);
}

/**
 * ieee802154_be16_to_le16 - copies and convert be16 to le16
 * @le16_dst: le16 destination pointer
 * @be16_src: be16 source pointer
 */
static inline void ieee802154_be16_to_le16(void *le16_dst, const void *be16_src)
{
	put_unaligned_le16(get_unaligned_be16(be16_src), le16_dst);
}

/**
 * ieee802154_alloc_hw - Allocate a new hardware device
 *
 * This must be called once for each hardware device. The returned pointer
 * must be used to refer to this device when calling other functions.
 * mac802154 allocates a private data area for the driver pointed to by
 * @priv in &struct ieee802154_hw, the size of this area is given as
 * @priv_data_len.
 *
 * @priv_data_len: length of private data
 * @ops: callbacks for this device
 *
 * Return: A pointer to the new hardware device, or %NULL on error.
 */
struct ieee802154_hw *
ieee802154_alloc_hw(size_t priv_data_len, const struct ieee802154_ops *ops);

/**
 * ieee802154_free_hw - free hardware descriptor
 *
 * This function frees everything that was allocated, including the
 * private data for the driver. You must call ieee802154_unregister_hw()
 * before calling this function.
 *
 * @hw: the hardware to free
 */
void ieee802154_free_hw(struct ieee802154_hw *hw);

/**
 * ieee802154_register_hw - Register hardware device
 *
 * You must call this function before any other functions in
 * mac802154. Note that before a hardware can be registered, you
 * need to fill the contained wpan_phy's information.
 *
 * @hw: the device to register as returned by ieee802154_alloc_hw()
 *
 * Return: 0 on success. An error code otherwise.
 */
int ieee802154_register_hw(struct ieee802154_hw *hw);

/**
 * ieee802154_unregister_hw - Unregister a hardware device
 *
 * This function instructs mac802154 to free allocated resources
 * and unregister netdevices from the networking subsystem.
 *
 * @hw: the hardware to unregister
 */
void ieee802154_unregister_hw(struct ieee802154_hw *hw);

/**
 * ieee802154_rx_irqsafe - receive frame
 *
 * Like ieee802154_rx() but can be called in IRQ context
 * (internally defers to a tasklet.)
 *
 * @hw: the hardware this frame came in on
 * @skb: the buffer to receive, owned by mac802154 after this call
 * @lqi: link quality indicator
 */
void ieee802154_rx_irqsafe(struct ieee802154_hw *hw, struct sk_buff *skb,
			   u8 lqi);

/**
 * ieee802154_xmit_complete - frame transmission complete
 *
 * @hw: pointer as obtained from ieee802154_alloc_hw().
 * @skb: buffer for transmission
 * @ifs_handling: indicate interframe space handling
 */
void ieee802154_xmit_complete(struct ieee802154_hw *hw, struct sk_buff *skb,
			      bool ifs_handling);

/**
 * ieee802154_xmit_error - offloaded frame transmission failed
 *
 * @hw: pointer as obtained from ieee802154_alloc_hw().
 * @skb: buffer for transmission
 * @reason: error code
 */
void ieee802154_xmit_error(struct ieee802154_hw *hw, struct sk_buff *skb,
			   int reason);

/**
 * ieee802154_xmit_hw_error - frame could not be offloaded to the transmitter
 *                            because of a hardware error (bus error, timeout, etc)
 *
 * @hw: pointer as obtained from ieee802154_alloc_hw().
 * @skb: buffer for transmission
 */
void ieee802154_xmit_hw_error(struct ieee802154_hw *hw, struct sk_buff *skb);

#endif /* NET_MAC802154_H */
