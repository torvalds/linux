/* Copyright (C) 2016 National Instruments Corp.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#ifndef __PHY_LED_TRIGGERS
#define __PHY_LED_TRIGGERS

struct phy_device;

#ifdef CONFIG_LED_TRIGGER_PHY

#include <linux/leds.h>

#define PHY_LED_TRIGGER_SPEED_SUFFIX_SIZE	10
#define PHY_MII_BUS_ID_SIZE	(20 - 3)

#define PHY_LINK_LED_TRIGGER_NAME_SIZE (PHY_MII_BUS_ID_SIZE + \
				       FIELD_SIZEOF(struct mdio_device, addr)+\
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
