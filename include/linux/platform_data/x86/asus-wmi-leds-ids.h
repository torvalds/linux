/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __PLATFORM_DATA_X86_ASUS_WMI_LEDS_IDS_H
#define __PLATFORM_DATA_X86_ASUS_WMI_LEDS_IDS_H

#include <linux/dmi.h>
#include <linux/types.h>

/* To be used by both hid-asus and asus-wmi to determine which controls kbd_brightness */
#if IS_REACHABLE(CONFIG_ASUS_WMI) || IS_REACHABLE(CONFIG_HID_ASUS)
static const struct dmi_system_id asus_use_hid_led_dmi_ids[] = {
	{
		.matches = {
			DMI_MATCH(DMI_PRODUCT_FAMILY, "ROG Zephyrus"),
		},
	},
	{
		.matches = {
			DMI_MATCH(DMI_PRODUCT_FAMILY, "ROG Strix"),
		},
	},
	{
		.matches = {
			DMI_MATCH(DMI_PRODUCT_FAMILY, "ROG Flow"),
		},
	},
	{
		.matches = {
			DMI_MATCH(DMI_PRODUCT_FAMILY, "ProArt P16"),
		},
	},
	{
		.matches = {
			DMI_MATCH(DMI_BOARD_NAME, "GA403U"),
		},
	},
	{
		.matches = {
			DMI_MATCH(DMI_BOARD_NAME, "GU605M"),
		},
	},
	{
		.matches = {
			DMI_MATCH(DMI_BOARD_NAME, "RC71L"),
		},
	},
	{ },
};
#endif

#endif	/* __PLATFORM_DATA_X86_ASUS_WMI_LEDS_IDS_H */
