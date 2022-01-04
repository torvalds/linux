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
#include <linux/can/bittiming.h>
#include <linux/can/error.h>
#include <linux/can/led.h>
#include <linux/can/length.h>
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

enum can_termination_gpio {
	CAN_TERMINATION_GPIO_DISABLED = 0,
	CAN_TERMINATION_GPIO_ENABLED,
	CAN_TERMINATION_GPIO_MAX,
};

/*
 * CAN common private data
 */
struct can_priv {
	struct net_device *dev;
	struct can_device_stats can_stats;

	const struct can_bittiming_const *bittiming_const,
		*data_bittiming_const;
	struct can_bittiming bittiming, data_bittiming;
	const struct can_tdc_const *tdc_const;
	struct can_tdc tdc;

	unsigned int bitrate_const_cnt;
	const u32 *bitrate_const;
	const u32 *data_bitrate_const;
	unsigned int data_bitrate_const_cnt;
	u32 bitrate_max;
	struct can_clock clock;

	unsigned int termination_const_cnt;
	const u16 *termination_const;
	u16 termination;
	struct gpio_desc *termination_gpio;
	u16 termination_gpio_ohms[CAN_TERMINATION_GPIO_MAX];

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

void can_setup(struct net_device *dev);

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

const char *can_get_state_str(const enum can_state state);
void can_change_state(struct net_device *dev, struct can_frame *cf,
		      enum can_state tx_state, enum can_state rx_state);

#ifdef CONFIG_OF
void of_can_transceiver(struct net_device *dev);
#else
static inline void of_can_transceiver(struct net_device *dev) { }
#endif

extern struct rtnl_link_ops can_link_ops;
int can_netlink_register(void);
void can_netlink_unregister(void);

#endif /* !_CAN_DEV_H */
