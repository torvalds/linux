/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Driver header for NOON010PC30L camera sensor chip.
 *
 * Copyright (c) 2010 Samsung Electronics, Co. Ltd
 * Contact: Sylwester Nawrocki <s.nawrocki@samsung.com>
 */

#ifndef NOON010PC30_H
#define NOON010PC30_H

/**
 * struct noon010pc30_platform_data - platform data
 * @clk_rate: the clock frequency in Hz
 */

struct noon010pc30_platform_data {
	unsigned long clk_rate;
};

#endif /* NOON010PC30_H */
