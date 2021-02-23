/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2020 Krzysztof Kozlowski <krzk@kernel.org>
 */

#ifndef __LINUX_PLATFORM_DATA_CLK_S3C2410_H_
#define __LINUX_PLATFORM_DATA_CLK_S3C2410_H_

/**
 * struct s3c2410_clk_platform_data - platform data for S3C2410 clock driver
 *
 * @modify_misccr: Function to modify the MISCCR and return the new value
 */
struct s3c2410_clk_platform_data {
	unsigned int (*modify_misccr)(unsigned int clr, unsigned int chg);
};

#endif /* __LINUX_PLATFORM_DATA_CLK_S3C2410_H_ */

