/*
 * Copyright 2012, Fabio Baltieri <fabio.baltieri@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _CAN_LED_H
#define _CAN_LED_H

#include <linux/if.h>
#include <linux/leds.h>
#include <linux/netdevice.h>

enum can_led_event {
	CAN_LED_EVENT_OPEN,
	CAN_LED_EVENT_STOP,
	CAN_LED_EVENT_TX,
	CAN_LED_EVENT_RX,
};

#ifdef CONFIG_CAN_LEDS

/* keep space for interface name + "-tx"/"-rx"/"-rxtx"
 * suffix and null terminator
 */
#define CAN_LED_NAME_SZ (IFNAMSIZ + 6)

void can_led_event(struct net_device *netdev, enum can_led_event event);
void devm_can_led_init(struct net_device *netdev);
int __init can_led_notifier_init(void);
void __exit can_led_notifier_exit(void);

#else

static inline void can_led_event(struct net_device *netdev,
				 enum can_led_event event)
{
}
static inline void devm_can_led_init(struct net_device *netdev)
{
}
static inline int can_led_notifier_init(void)
{
	return 0;
}
static inline void can_led_notifier_exit(void)
{
}

#endif

#endif /* !_CAN_LED_H */
