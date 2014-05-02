/*
 * Copyright 2013 - Hans de Goede <hdegoede@redhat.com>
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

#ifndef __LINUX_CLK_SUNXI_H_
#define __LINUX_CLK_SUNXI_H_

#include <linux/clk.h>

void clk_sunxi_mmc_phase_control(struct clk_hw *hw, u8 sample, u8 output);

#endif
