/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (c) 2014 MundoReader S.L.
 * Author: Heiko Stuebner <heiko@sntech.de>
 */

#ifndef _DT_BINDINGS_CLK_ROCKCHIP_RK3066A_H
#define _DT_BINDINGS_CLK_ROCKCHIP_RK3066A_H

#include <dt-bindings/clock/rk3188-cru-common.h>

/* soft-reset indices */
#define SRST_SRST1		0
#define SRST_SRST2		1

#define SRST_L2MEM		18
#define SRST_I2S0		23
#define SRST_I2S1		24
#define SRST_I2S2		25
#define SRST_TIMER2		29

#define SRST_GPIO4		36
#define SRST_GPIO6		38

#define SRST_TSADC		92

#define SRST_HDMI		96
#define SRST_HDMI_APB		97
#define SRST_CIF1		111

#endif
