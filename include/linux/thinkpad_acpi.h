/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __THINKPAD_ACPI_H__
#define __THINKPAD_ACPI_H__

/* These two functions return 0 if success, or negative error code
   (e g -ENODEV if no led present) */

enum {
	TPACPI_LED_MUTE,
	TPACPI_LED_MICMUTE,
	TPACPI_LED_MAX,
};

int tpacpi_led_set(int whichled, bool on);

#endif
