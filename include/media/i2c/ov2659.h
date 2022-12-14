/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Omnivision OV2659 CMOS Image Sensor driver
 *
 * Copyright (C) 2015 Texas Instruments, Inc.
 *
 * Benoit Parrot <bparrot@ti.com>
 * Lad, Prabhakar <prabhakar.csengg@gmail.com>
 */

#ifndef OV2659_H
#define OV2659_H

/**
 * struct ov2659_platform_data - ov2659 driver platform data
 * @link_frequency: target pixel clock frequency
 */
struct ov2659_platform_data {
	s64 link_frequency;
};

#endif /* OV2659_H */
