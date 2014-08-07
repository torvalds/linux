/*
 *  hda_i915.c - routines for Haswell HDA controller power well support
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the License, or (at your option)
 *  any later version.
 *
 *  This program is distributed in the hope that it will be useful, but
 *  WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 *  or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software Foundation,
 *  Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <sound/core.h>
#include <drm/i915_powerwell.h>
#include "hda_priv.h"
#include "hda_i915.h"

/* Intel HSW/BDW display HDA controller Extended Mode registers.
 * EM4 (M value) and EM5 (N Value) are used to convert CDClk (Core Display
 * Clock) to 24MHz BCLK: BCLK = CDCLK * M / N
 * The values will be lost when the display power well is disabled.
 */
#define AZX_REG_EM4			0x100c
#define AZX_REG_EM5			0x1010

static int (*get_power)(void);
static int (*put_power)(void);
static int (*get_cdclk)(void);

int hda_display_power(bool enable)
{
	if (!get_power || !put_power)
		return -ENODEV;

	pr_debug("HDA display power %s \n",
			enable ? "Enable" : "Disable");
	if (enable)
		return get_power();
	else
		return put_power();
}

void haswell_set_bclk(struct azx *chip)
{
	int cdclk_freq;
	unsigned int bclk_m, bclk_n;

	if (!get_cdclk)
		return;

	cdclk_freq = get_cdclk();
	switch (cdclk_freq) {
	case 337500:
		bclk_m = 16;
		bclk_n = 225;
		break;

	case 450000:
	default: /* default CDCLK 450MHz */
		bclk_m = 4;
		bclk_n = 75;
		break;

	case 540000:
		bclk_m = 4;
		bclk_n = 90;
		break;

	case 675000:
		bclk_m = 8;
		bclk_n = 225;
		break;
	}

	azx_writew(chip, EM4, bclk_m);
	azx_writew(chip, EM5, bclk_n);
}


int hda_i915_init(void)
{
	int err = 0;

	get_power = symbol_request(i915_request_power_well);
	if (!get_power) {
		pr_warn("hda-i915: get_power symbol get fail\n");
		return -ENODEV;
	}

	put_power = symbol_request(i915_release_power_well);
	if (!put_power) {
		symbol_put(i915_request_power_well);
		get_power = NULL;
		return -ENODEV;
	}

	get_cdclk = symbol_request(i915_get_cdclk_freq);
	if (!get_cdclk)	/* may have abnormal BCLK and audio playback rate */
		pr_warn("hda-i915: get_cdclk symbol get fail\n");

	pr_debug("HDA driver get symbol successfully from i915 module\n");

	return err;
}

int hda_i915_exit(void)
{
	if (get_power) {
		symbol_put(i915_request_power_well);
		get_power = NULL;
	}
	if (put_power) {
		symbol_put(i915_release_power_well);
		put_power = NULL;
	}
	if (get_cdclk) {
		symbol_put(i915_get_cdclk_freq);
		get_cdclk = NULL;
	}

	return 0;
}
