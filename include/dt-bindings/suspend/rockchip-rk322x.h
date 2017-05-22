/*
 * Header providing constants for Rockchip suspend bindings.
 *
 * Copyright (C) 2017, Fuzhou Rockchip Electronics Co., Ltd
 * Author: XiaoDong.Huang
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

#ifndef __DT_BINDINGS_SUSPEND_ROCKCHIP_RK322X_H__
#define __DT_BINDINGS_SUSPEND_ROCKCHIP_RK322X_H__

#ifndef BIT
#define BIT(nr)				(1 << (nr))
#endif

/* the suspend mode */
#define RKPM_CTR_PWR_DMNS		BIT(0)
#define RKPM_CTR_GTCLKS			BIT(1)
#define RKPM_CTR_PLLS			BIT(2)
#define RKPM_CTR_VOLTS			BIT(3)
#define RKPM_CTR_GPIOS			BIT(4)
#define RKPM_CTR_DDR			BIT(5)
#define RKPM_CTR_PMIC			BIT(6)

/* system clk is 24M,and div to min */
#define RKPM_CTR_SYSCLK_DIV		BIT(7)
/* switch sysclk to 32k, need hardwart support, and div to min */
#define RKPM_CTR_SYSCLK_32K		BIT(8)
/* switch sysclk to 32k,disable 24M OSC,
 * need hardwart susport. and div to min
 */
#define RKPM_CTR_SYSCLK_OSC_DIS		BIT(9)
#define RKPM_CTR_VOL_PWM0		BIT(10)
#define RKPM_CTR_VOL_PWM1		BIT(11)
#define RKPM_CTR_VOL_PWM2		BIT(12)
#define RKPM_CTR_VOL_PWM3		BIT(13)
#define RKPM_CTR_BUS_IDLE		BIT(14)
#define RKPM_CTR_SRAM			BIT(15)
/*Low Power Function Selection*/
#define RKPM_CTR_IDLESRAM_MD		BIT(16)
#define RKPM_CTR_IDLEAUTO_MD		BIT(17)
#define RKPM_CTR_ARMDP_LPMD		BIT(18)
#define RKPM_CTR_ARMOFF_LPMD		BIT(19)
#define RKPM_CTR_ARMLOGDP_LPMD		BIT(20)
#define RKPM_CTR_ARMOFF_LOGDP_LPMD	BIT(21)
#define RKPM_CTR_ARMLOGOFF_DLPMD	BIT(22)

#endif
