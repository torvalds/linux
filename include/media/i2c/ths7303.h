/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2013 Texas Instruments Inc
 *
 * Copyright 2013 Cisco Systems, Inc. and/or its affiliates.
 *
 * Contributors:
 *     Hans Verkuil <hverkuil@kernel.org>
 *     Lad, Prabhakar <prabhakar.lad@ti.com>
 *     Martin Bugge <marbugge@cisco.com>
 */

#ifndef THS7303_H
#define THS7303_H

/**
 * struct ths7303_platform_data - Platform dependent data
 * @ch_1: Bias value for channel one.
 * @ch_2: Bias value for channel two.
 * @ch_3: Bias value for channel three.
 */
struct ths7303_platform_data {
	u8 ch_1;
	u8 ch_2;
	u8 ch_3;
};

#endif
