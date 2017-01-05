/*
 * Header providing constants for Rockchip suspend bindings.
 *
 * Copyright (C) 2017, Fuzhou Rockchip Electronics Co., Ltd
 * Author: Tony.Xie
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

#ifndef __DT_BINDINGS_SUSPEND_ROCKCHIP_RK3399_H__
#define __DT_BINDINGS_SUSPEND_ROCKCHIP_RK3399_H__

/* the suspend mode */
#define RKPM_SLP_WFI				(1 << 0)
#define RKPM_SLP_ARMPD				(1 << 1)
#define RKPM_SLP_PERILPPD			(1 << 2)
#define RKPM_SLP_DDR_RET			(1 << 3)
#define RKPM_SLP_PLLPD				(1 << 4)
#define RKPM_SLP_OSC_DIS			(1 << 5)
#define RKPM_SLP_CENTER_PD			(1 << 6)
#define RKPM_SLP_AP_PWROFF			(1 << 7)

/* the wake up source */
#define RKPM_CLUSTER_L_WKUP_EN			(1 << 0)
#define RKPM_CLUSTER_B_WKUPB_EN			(1 << 1)
#define RKPM_GPIO_WKUP_EN			(1 << 2)
#define RKPM_SDIO_WKUP_EN			(1 << 3)
#define RKPM_SDMMC_WKUP_EN			(1 << 4)
#define RKPM_TIMER_WKUP_EN			(1 << 6)
#define RKPM_USB_WKUP_EN			(1 << 7)
#define RKPM_SFT_WKUP_EN			(1 << 8)
#define RKPM_WDT_M0_WKUP_EN			(1 << 9)
#define RKPM_TIME_OUT_WKUP_EN			(1 << 10)
#define RKPM_PWM_WKUP_EN			(1 << 11)
#define RKPM_PCIE_WKUP_EN			(1 << 13)

/* the pwm regulator */
#define PWM0_REGULATOR_EN			(1 << 0)
#define PWM1_REGULATOR_EN			(1 << 1)
#define PWM2_REGULATOR_EN			(1 << 2)
#define PWM3A_REGULATOR_EN			(1 << 3)
#define PWM3B_REGULATOR_EN			(1 << 4)

/* the APIO voltage domain */
#define RKPM_APIO0_SUSPEND			(1 << 0)
#define RKPM_APIO1_SUSPEND			(1 << 1)
#define RKPM_APIO2_SUSPEND			(1 << 2)
#define RKPM_APIO3_SUSPEND			(1 << 3)
#define RKPM_APIO4_SUSPEND			(1 << 4)
#define RKPM_APIO5_SUSPEND			(1 << 5)

#endif
