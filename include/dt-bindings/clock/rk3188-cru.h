/* SPDX-License-Identifier: (GPL-2.0+ OR MIT) */
/*
 * Copyright (c) 2014 MundoReader S.L.
 * Author: Heiko Stuebner <heiko@sntech.de>
 */

#ifndef _DT_BINDINGS_CLK_ROCKCHIP_RK3188_H
#define _DT_BINDINGS_CLK_ROCKCHIP_RK3188_H

#include <dt-bindings/clock/rk3188-cru-common.h>

/* soft-reset indices */
#define SRST_PTM_CORE2		0
#define SRST_PTM_CORE3		1
#define SRST_CORE2		5
#define SRST_CORE3		6
#define SRST_CORE2_DBG		10
#define SRST_CORE3_DBG		11

#define SRST_TIMER2		16
#define SRST_TIMER4		23
#define SRST_I2S0		24
#define SRST_TIMER5		25
#define SRST_TIMER3		29
#define SRST_TIMER6		31

#define SRST_PTM3		36
#define SRST_PTM3_ATB		37

#define SRST_GPS		67
#define SRST_HSICPHY		75
#define SRST_TIMER		78

#define SRST_PTM2		92
#define SRST_CORE2_WDT		94
#define SRST_CORE3_WDT		95

#define SRST_PTM2_ATB		111

#define SRST_HSIC		117
#define SRST_CTI2		118
#define SRST_CTI2_APB		119
#define SRST_GPU_BRIDGE		121
#define SRST_CTI3		123
#define SRST_CTI3_APB		124

#endif
