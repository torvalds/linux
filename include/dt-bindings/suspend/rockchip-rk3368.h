/*
 * Header providing constants for Rockchip suspend bindings.
 *
 * Copyright (C) 2015, Fuzhou Rockchip Electronics Co., Ltd
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
#ifndef __DT_BINDINGS_ROCKCHIP_PM_H__
#define __DT_BINDINGS_ROCKCHIP_PM_H__
/******************************bits ops************************************/

#ifndef BIT
#define BIT(nr)				(1 << (nr))
#endif

#define RKPM_SLP_WFI			BIT(0)
#define RKPM_SLP_ARMPD			BIT(1)
#define RKPM_SLP_ARMOFF			BIT(2)
#define RKPM_SLP_ARMOFF_LOGPD		BIT(3)
#define RKPM_SLP_ARMOFF_LOGOFF		BIT(4)
#define RKPM_RUNNING_ARMMODE		BIT(5)

/* func ctrl by pmu auto ctr */
#define RKPM_SLP_PMU_PLLS_PWRDN		BIT(8) /* all plls except ddr's pll*/
#define RKPM_SLP_PMU_PMUALIVE_32K	BIT(9)
#define RKPM_SLP_PMU_DIS_OSC		BIT(10)

/* func ctrl by software set */
#define RKPM_SLP_SFT_PLLS_DEEP		BIT(16) /* all plls except ddr's pll*/
#define RKPM_SLP_SFT_32K_EXT		BIT(17)
#define RKPM_SLP_SFT_PD_PERI		BIT(18)
#define RKPM_SLP_SFT_PD_NBSCUS		BIT(19) /* noboot scus in muti-cluster */

/* the wake up source */
#define RKPM_CLUSTER_L_WKUP_EN		BIT(0)
#define RKPM_CLUSTER_B_WKUPB_EN		BIT(1)
#define RKPM_GPIO_WKUP_EN		BIT(2)
#define RKPM_SDIO_WKUP_EN		BIT(3)
#define RKPM_SDMMC_WKUP_EN		BIT(4)
#define RKPM_SIM_WKUP_EN		BIT(5)
#define RKPM_TIMER_WKUP_EN		BIT(6)
#define RKPM_USB_WKUP_EN		BIT(7)
#define RKPM_SFT_WKUP_EN		BIT(8)
#define RKPM_WDT_M0_WKUP_EN		BIT(9)
#define RKPM_TIME_OUT_WKUP_EN		BIT(10)

#endif
