/*
 * Platform data structure for Network Space v2 LED driver
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#ifndef __LEDS_KIRKWOOD_NS2_H
#define __LEDS_KIRKWOOD_NS2_H

enum ns2_led_modes {
	NS_V2_LED_OFF,
	NS_V2_LED_ON,
	NS_V2_LED_SATA,
};

struct ns2_led_modval {
	enum ns2_led_modes	mode;
	int			cmd_level;
	int			slow_level;
};

struct ns2_led {
	const char	*name;
	const char	*default_trigger;
	unsigned	cmd;
	unsigned	slow;
	int		num_modes;
	struct ns2_led_modval *modval;
};

struct ns2_led_platform_data {
	int		num_leds;
	struct ns2_led	*leds;
};

#endif /* __LEDS_KIRKWOOD_NS2_H */
