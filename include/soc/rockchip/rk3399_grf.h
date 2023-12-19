/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Rockchip General Register Files definitions
 *
 * Copyright (c) 2018, Collabora Ltd.
 * Author: Enric Balletbo i Serra <enric.balletbo@collabora.com>
 */

#ifndef __SOC_RK3399_GRF_H
#define __SOC_RK3399_GRF_H

/* PMU GRF Registers */
#define RK3399_PMUGRF_OS_REG2		0x308
#define RK3399_PMUGRF_OS_REG2_DDRTYPE		GENMASK(15, 13)
#define RK3399_PMUGRF_OS_REG2_BW_CH0		GENMASK(3, 2)
#define RK3399_PMUGRF_OS_REG2_BW_CH1		GENMASK(19, 18)

#endif
