/*
 * Header providing constants for Rockchip suspend bindings.
 *
 * Copyright (C) 2017, Fuzhou Rockchip Electronics Co., Ltd
 * Author: Power.xu
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

#ifndef __DT_BINDINGS_SUSPEND_ROCKCHIP_RK3288_H__
#define __DT_BINDINGS_SUSPEND_ROCKCHIP_RK3288_H__

/* the suspend mode */
#define	RKPM_CTR_PWR_DMNS		(1 << 0)
#define RKPM_CTR_GTCLKS			(1 << 1)
#define RKPM_CTR_PLLS			(1 << 2)
#define RKPM_CTR_VOLTS			(1 << 3)
#define RKPM_CTR_GPIOS			(1 << 4)
#define RKPM_CTR_DDR			(1 << 5)
#define RKPM_CTR_PMIC			(1 << 6)
/* system clk is 24M,and div to min */
#define RKPM_CTR_SYSCLK_DIV		(1 << 7)
/* switch sysclk to 32k, need hardwart support, and div to min */
#define RKPM_CTR_SYSCLK_32K		(1 << 8)
/* switch sysclk to 32k,disable 24M OSC,
 * need hardwart susport. and div to min
 */
#define RKPM_CTR_SYSCLK_OSC_DIS		(1 << 9)
#define RKPM_CTR_BUS_IDLE		(1 << 14)
#define RKPM_CTR_SRAM			(1 << 15)
/*Low Power Function Selection*/
#define RKPM_CTR_IDLESRAM_MD		(1 << 16)
#define RKPM_CTR_IDLEAUTO_MD		(1 << 17)
#define RKPM_CTR_ARMDP_LPMD		(1 << 18)
#define RKPM_CTR_ARMOFF_LPMD		(1 << 19)
#define RKPM_CTR_ARMLOGDP_LPMD		(1 << 20)
#define RKPM_CTR_ARMOFF_LOGDP_LPMD	(1 << 21)
#define RKPM_CTR_ARMLOGOFF_DLPMD	(1 << 22)

/* the wake up source */
#define RKPM_ARMINT_WKUP_EN		(1 << 0)
#define RKPM_SDMMC_WKUP_EN		(1 << 2)
#define RKPM_GPIO_WKUP_EN		(1 << 3)

/* the pwm regulator */
#define PWM0_REGULATOR_EN		(1 << 0)
#define PWM1_REGULATOR_EN		(1 << 1)
#define PWM2_REGULATOR_EN		(1 << 2)
#define PWM3_REGULATOR_EN		(1 << 3)

#endif
