/* SPDX-License-Identifier: GPL-2.0 */
/*
 * linux/can/dev.h
 *
 * Definitions for the CAN network device driver interface
 *
 * Copyright (C) 2006 Andrey Volkov <avolkov@varma-el.com>
 *               Varma Electronics Oy
 *
 * Copyright (C) 2008 Wolfgang Grandegger <wg@grandegger.com>
 *
 */

#ifndef _CAN_DEV_H
#define _CAN_DEV_H

#include <linux/can.h>
#include <linux/can/error.h>
#include <linux/can/led.h>
#include <linux/can/netlink.h>
#include <linux/can/skb.h>
#include <linux/netdevice.h>

/*
 * CAN mode
 */
enum can_mode {
	CAN_MODE_STOP = 0,
	CAN_MODE_START,
	CAN_MODE_SLEEP
};

/*
 * CAN common private data
 */
struct can_priv {
	struct net_device *dev;
	struct can_device_stats can_stats;

	struct can_bittiming bittiming, data_bittiming;
	const struct can_bittiming_const *bittiming_const,
		*data_bittiming_const;
	const u16 *termination_const;
	unsigned int termination_const_cnt;
	u16 termination;
	const u32 *bitrate_const;
	unsigned int bitrate_const_cnt;
	const u32 *data_bitrate_const;
	unsigned int data_bitrate_const_cnt;
	u32 bitrate_max;
	struct can_clock clock;

	enum can_state state;

	/* CAN controller features - see include/uapi/linux/can/netlink.h */
	u32 ctrlmode;		/* current options setting */
	u32 ctrlmode_supported;	/* options that can be modified by netlink */
	u32 ctrlmode_static;	/* static enabled options for driver/hardware */

	int restart_ms;
	struct delayed_work restart_work;

	int (*do_set_bittiming)(struct net_device *dev);
	int (*do_set_data_bittiming)(struct net_device *dev);
	int (*do_set_mode)(struct net_device *dev, enum can_mode mode);
	int (*do_set_termination)(struct net_device *dev, u16 term);
	int (*do_get_state)(const struct net_device *dev,
			    enum can_state *state);
	int (*do_get_berr_counter)(const struct net_device *dev,
				   struct can_berr_counter *bec);

	unsigned int echo_skb_max;
	struct sk_buff **echo_skb;

#ifdef CONFIG_CAN_LEDS
	struct led_trigger *tx_led_trig;
	char tx_led_trig_name[CAN_LED_NAME_SZ];
	struct led_trigger *rx_led_trig;
	char rx_led_trig_name[CAN_LED_NAME_SZ];
	struct led_trigger *rxtx_led_trig;
	char rxtx_led_trig_name[CAN_LED_NAME_SZ];
#endif
};

#define CAN_SYNC_SEG 1

/*
 * can_bit_time() - Duration of one bit
 *
 * Please refer to ISO 11898-1:2015, section 11.3.1.1 "Bit time" for
 * additional information.
 *
 * Return: the number of time quanta in one bit.
 */
static inline unsigned int can_bit_time(const struct can_bittiming *bt)
{
	return CAN_SYNC_SEG + bt->prop_seg + bt->phase_seg1 + bt->phase_seg2;
}

/*
 * can_cc_dlc2len(value) - convert a given data length code (dlc) of a
 * Classical CAN frame into a valid data length of max. 8 bytes.
 *
 * To be used in the CAN netdriver receive path to ensure conformance with
 * ISO 11898-1 Chapter 8.4.2.3 (DLC field)
 */
#define can_cc_dlc2len(dlc)	(min_t(u8, (dlc), CAN_MAX_DLEN))

/* Check for outgoing skbs that have not been created by the CAN subsystem */
static inline bool can_skb_headroom_valid(struct net_device *dev,
					  struct sk_buff *skb)
{
	/* af_packet creates a headroom of HH_DATA_MOD bytes which is fine */
	if (WARN_ON_ONCE(skb_headroom(skb) < sizeof(struct can_skb_priv)))
		return false;

	/* af_packet does not apply CAN skb specific settings */
	if (skb->ip_summed == CHECKSUM_NONE) {
		/* init headroom */
		can_skb_prv(skb)->ifindex = dev->ifindex;
		can_skb_prv(skb)->skbcnt = 0;

		skb->ip_summed = CHECKSUM_UNNECESSARY;

		/* perform proper loopback on capable devices */
		if (dev->flags & IFF_ECHO)
			skb->pkt_type = PACKET_LOOPBACK;
		else
			skb->pkt_type = PACKET_HOST;

		skb_reset_mac_header(skb);
		skb_reset_network_header(skb);
		skb_reset_transport_header(skb);
	}

	return true;
}

/* Drop a given socketbuffer if it does not contain a valid CAN frame. */
static inline bool can_dropped_invalid_skb(struct net_device *dev,
					  struct sk_buff *skb)
{
	const struct canfd_frame *cfd = (struct canfd_frame *)skb->data;

	if (skb->protocol == htons(ETH_P_CAN)) {
		if (unlikely(skb->len != CAN_MTU ||
			     cfd->len > CAN_MAX_DLEN))
			goto inval_skb;
	} else if (skb->protocol == htons(ETH_P_CANFD)) {
		if (unlikely(skb->len != CANFD_MTU ||
			     cfd->len > CANFD_MAX_DLEN))
			goto inval_skb;
	} else
		goto inval_skb;

	if (!can_skb_headroom_valid(dev, skb))
		goto inval_skb;

	return false;

inval_skb:
	kfree_skb(skb);
	dev->stats.tx_dropped++;
	return true;
}

static inline bool can_is_canfd_skb(const struct sk_buff *skb)
{
	/* the CAN specific type of skb is identified by its data length */
	return skb->len == CANFD_MTU;
}

/* helper to get the data length code (DLC) for Classical CAN raw DLC access */
static inline u8 can_get_cc_dlc(const struct can_frame *cf, const u32 ctrlmode)
{
	/* return len8_dlc as dlc value only if all conditions apply */
	if ((ctrlmode & CAN_CTRLMODE_CC_LEN8_DLC) &&
	    (cf->len == CAN_MAX_DLEN) &&
	    (cf->len8_dlc > CAN_MAX_DLEN && cf->len8_dlc <= CAN_MAX_RAW_DLC))
		return cf->len8_dlc;

	/* return the payload length as dlc value */
	return cf->len;
}

/* helper to set len and len8_dlc value for Classical CAN raw DLC access */
static inline void can_frame_set_cc_len(struct can_frame *cf, const u8 dlc,
					const u32 ctrlmode)
{
	/* the caller already ensured that dlc is a value from 0 .. 15 */
	if (ctrlmode & CAN_CTRLMODE_CC_LEN8_DLC && dlc > CAN_MAX_DLEN)
		cf->len8_dlc = dlc;

	/* limit the payload length 'len' to CAN_MAX_DLEN */
	cf->len = can_cc_dlc2len(dlc);
}

/* helper to define static CAN controller features at device creation time */
static inline void can_set_static_ctrlmode(struct net_device *dev,
					   u32 static_mode)
{
	struct can_priv *priv = netdev_priv(dev);

	/* alloc_candev() succeeded => netdev_priv() is valid at this point */
	priv->ctrlmode = static_mode;
	priv->ctrlmode_static = static_mode;

	/* override MTU which was set by default in can_setup()? */
	if (static_mode & CAN_CTRLMODE_FD)
		dev->mtu = CANFD_MTU;
}

/* get data length from raw data length code (DLC) */
u8 can_fd_dlc2len(u8 dlc);

/* map the sanitized data length to an appropriate data length code */
u8 can_fd_len2dlc(u8 len);

struct net_device *alloc_candev_mqs(int sizeof_priv, unsigned int echo_skb_max,
				    unsigned int txqs, unsigned int rxqs);
#define alloc_candev(sizeof_priv, echo_skb_max) \
	alloc_candev_mqs(sizeof_priv, echo_skb_max, 1, 1)
#define alloc_candev_mq(sizeof_priv, echo_skb_max, count) \
	alloc_candev_mqs(sizeof_priv, echo_skb_max, count, count)
void free_candev(struct net_device *dev);

/* a candev safe wrapper around netdev_priv */
struct can_priv *safe_candev_priv(struct net_device *dev);

int open_candev(struct net_device *dev);
void close_candev(struct net_device *dev);
int can_change_mtu(struct net_device *dev, int new_mtu);

int register_candev(struct net_device *dev);
void unregister_candev(struct net_device *dev);

int can_restart_now(struct net_device *dev);
void can_bus_off(struct net_device *dev);

void can_change_state(struct net_device *dev, struct can_frame *cf,
		      enum can_state tx_state, enum can_state rx_state);

int can_put_echo_skb(struct sk_buff *skb, struct net_device *dev,
		     unsigned int idx);
struct sk_buff *__can_get_echo_skb(struct net_device *dev, unsigned int idx,
				   u8 *len_ptr);
unsigned int can_get_echo_skb(struct net_device *dev, unsigned int idx);
void can_free_echo_skb(struct net_device *dev, unsigned int idx);

#ifdef CONFIG_OF
void of_can_transceiver(struct net_device *dev);
#else
static inline void of_can_transceiver(struct net_device *dev) { }
#endif

struct sk_buff *alloc_can_skb(struct net_device *dev, struct can_frame **cf);
struct sk_buff *alloc_canfd_skb(struct net_device *dev,
				struct canfd_frame **cfd);
struct sk_buff *alloc_can_err_skb(struct net_device *dev,
				  struct can_frame **cf);

#endif /* !_CAN_DEV_H */
