/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2021  Angelo Dureghello <angelo@kernel-space.org>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef _CAN_PLATFORM_FLEXCAN_H
#define _CAN_PLATFORM_FLEXCAN_H

struct flexcan_platform_data {
	u32 clock_frequency;
	u8 clk_src;
};

#endif /* _CAN_PLATFORM_FLEXCAN_H */
