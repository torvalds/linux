/*
 * Copyright (c) 2015 Hans de Goede <hdegoede@redhat.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef PHY_SUN4I_USB_H_
#define PHY_SUN4I_USB_H_

#include "phy.h"

/**
 * sun4i_usb_phy_set_squelch_detect() - Enable/disable squelch detect
 * @phy: reference to a sun4i usb phy
 * @enabled: wether to enable or disable squelch detect
 */
void sun4i_usb_phy_set_squelch_detect(struct phy *phy, bool enabled);

#endif
