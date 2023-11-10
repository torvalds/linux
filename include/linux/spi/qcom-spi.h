/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (c) 2014-2018, 2020, The Linux Foundation. All rights reserved.
 * Copyright (c) 2023, Qualcomm Innovation Center, Inc. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */
/*
 * SPI driver for Qualcomm Technologies, Inc. MSM platforms.
 */

/**
 * msm_spi_platform_data: msm spi-controller's configuration data
 *
 * @max_clock_speed max spi clock speed
 * @active_only when set, votes when system active and removes the vote when
 *       system goes idle (optimises for performance). When unset, voting using
 *       runtime pm (optimizes for power).
 * @master_id master id number of the controller's wrapper (BLSP or GSBI).
 *       When zero, clock path voting is disabled.
 * @gpio_config pointer to function for configuring gpio
 * @gpio_release pointer to function for releasing gpio pins
 * @dma_config function poniter for configuring dma engine
 * @pm_lat power management latency
 * @infinite_mode use FIFO mode in infinite mode
 * @ver_reg_exists if the version register exists
 * @use_beam true if BAM is available
 * @bam_consumer_pipe_index BAM conusmer pipe
 * @bam_producer_pipe_index BAM producer pipe
 * @rt_priority true if RT thread
 * @use_pinctrl true if pinctrl library is used
 * @is_shared true when qup is shared between ee's
 */
struct msm_spi_platform_data {
	u32 max_clock_speed;
	u32  master_id;
	u32 bus_width;
	int (*gpio_config)(void);
	void (*gpio_release)(void);
	int (*dma_config)(void);
	const char *rsl_id;
	u32  pm_lat;
	u32  infinite_mode;
	bool ver_reg_exists;
	bool use_bam;
	u32  bam_consumer_pipe_index;
	u32  bam_producer_pipe_index;
	bool rt_priority;
	bool use_pinctrl;
	bool is_shared;
};
