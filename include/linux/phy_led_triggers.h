/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright (C) 2016 National Instruments Corp.
 */
#ifndef __PHY_LED_TRIGGERS
#define __PHY_LED_TRIGGERS

struct phy_device;

#ifdef CONFIG_LED_TRIGGER_PHY

#include <linux/leds.h>
#include <linux/phy.h>

#define PHY_LED_TRIGGER_SPEED_SUFFIX_SIZE	11

#define PHY_LINK_LED_TRIGGER_NAME_SIZE (MII_BUS_ID_SIZE + \
				       sizeof_field(struct mdio_device, addr)+\
				       PHY_LED_TRIGGER_SPEED_SUFFIX_SIZE)

struct phy_led_trigger {
	struct led_trigger trigger;
	char name[PHY_LINK_LED_TRIGGER_NAME_SIZE];
	unsigned int speed;
};


extern int phy_led_triggers_register(struct phy_device *phy);
extern void phy_led_triggers_unregister(struct phy_device *phy);
extern void phy_led_trigger_change_speed(struct phy_device *phy);

#else

static inline int phy_led_triggers_register(struct phy_device *phy)
{
	return 0;
}
static inline void phy_led_triggers_unregister(struct phy_device *phy) { }
static inline void phy_led_trigger_change_speed(struct phy_device *phy) { }

#endif

#endif
