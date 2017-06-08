/*
 * Shared helper functions for devices from the ADAU family
 *
 * Copyright 2011-2016 Analog Devices Inc.
 * Author: Lars-Peter Clausen <lars@metafoo.de>
 *
 * Licensed under the GPL-2 or later.
 */

#include <linux/gcd.h>
#include <linux/kernel.h>
#include <linux/module.h>

#include "adau-utils.h"

int adau_calc_pll_cfg(unsigned int freq_in, unsigned int freq_out,
	uint8_t regs[5])
{
	unsigned int r, n, m, i, j;
	unsigned int div;

	if (!freq_out) {
		r = 0;
		n = 0;
		m = 0;
		div = 0;
	} else {
		if (freq_out % freq_in != 0) {
			div = DIV_ROUND_UP(freq_in, 13500000);
			freq_in /= div;
			r = freq_out / freq_in;
			i = freq_out % freq_in;
			j = gcd(i, freq_in);
			n = i / j;
			m = freq_in / j;
			div--;
		} else {
			r = freq_out / freq_in;
			n = 0;
			m = 0;
			div = 0;
		}
		if (n > 0xffff || m > 0xffff || div > 3 || r > 8 || r < 2)
			return -EINVAL;
	}

	regs[0] = m >> 8;
	regs[1] = m & 0xff;
	regs[2] = n >> 8;
	regs[3] = n & 0xff;
	regs[4] = (r << 3) | (div << 1);
	if (m != 0)
		regs[4] |= 1; /* Fractional mode */

	return 0;
}
EXPORT_SYMBOL_GPL(adau_calc_pll_cfg);

MODULE_DESCRIPTION("ASoC ADAU audio CODECs shared helper functions");
MODULE_AUTHOR("Lars-Peter Clausen <lars@metafoo.de>");
MODULE_LICENSE("GPL v2");
