/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Shared library for Kinetic's ExpressWire protocol.
 * This protocol works by pulsing the ExpressWire IC's control GPIO.
 * ktd2692 and ktd2801 are known to use this protocol.
 */

#ifndef _LEDS_EXPRESSWIRE_H
#define _LEDS_EXPRESSWIRE_H

#include <linux/types.h>

struct gpio_desc;

struct expresswire_timing {
	unsigned long poweroff_us;
	unsigned long detect_delay_us;
	unsigned long detect_us;
	unsigned long data_start_us;
	unsigned long end_of_data_low_us;
	unsigned long end_of_data_high_us;
	unsigned long short_bitset_us;
	unsigned long long_bitset_us;
};

struct expresswire_common_props {
	struct gpio_desc *ctrl_gpio;
	struct expresswire_timing timing;
};

void expresswire_power_off(struct expresswire_common_props *props);
void expresswire_enable(struct expresswire_common_props *props);
void expresswire_start(struct expresswire_common_props *props);
void expresswire_end(struct expresswire_common_props *props);
void expresswire_set_bit(struct expresswire_common_props *props, bool bit);
void expresswire_write_u8(struct expresswire_common_props *props, u8 val);

#endif /* _LEDS_EXPRESSWIRE_H */
