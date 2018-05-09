/*
 * Header providing constants for Rockchip suspend bindings.
 *
 * Copyright (C) 2018, Fuzhou Rockchip Electronics Co., Ltd
 * Author: Joseph Chen
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
#ifndef __DT_BINDINGS_RK3308_PM_H__
#define __DT_BINDINGS_RK3308_PM_H__
/******************************bits ops************************************/

#ifndef BIT
#define BIT(nr)				(1 << (nr))
#endif

#define RKPM_ARMOFF			BIT(0)
#define RKPM_VADOFF			BIT(1)
#define RKPM_PWM_REGULATOR		BIT(2)
#define RKPM_PMU_HW_PLLS_PD		BIT(3)
#define RKPM_PMU_DIS_OSC		BIT(4)
#define RKPM_PMU_PMUALIVE_32K		BIT(5)
#define RKPM_PMU_EXT_32K		BIT(6)
#define RKPM_DDR_SREF_HARDWARE		BIT(7)
#define RKPM_DDR_EXIT_SRPD_IDLE		BIT(8)

/* Reserved to be add... */

/* Wakeup source */

#define RKPM_ARM_PRE_WAKEUP_EN		BIT(11)
#define RKPM_ARM_GIC_WAKEUP_EN		BIT(12)
#define RKPM_SDMMC_WAKEUP_EN		BIT(13)
#define RKPM_SDMMC_GRF_IRQ_WAKEUP_EN	BIT(14)
#define RKPM_TIMER_WAKEUP_EN		BIT(15)
#define RKPM_USBDEV_WAKEUP_EN		BIT(16)
#define RKPM_TIMEOUT_WAKEUP_EN		BIT(17)
#define RKPM_GPIO0_WAKEUP_EN		BIT(18)
#define RKPM_VAD_WAKEUP_EN		BIT(19)

/* Reserved to be add... */

/* All for debug */
#define RKPM_DBG_WOARKAROUND		BIT(23)
#define RKPM_DBG_VAD_INT_OFF		BIT(24)
#define RKPM_DBG_CLK_UNGATE		BIT(25)
#define RKPM_DBG_CLKOUT			BIT(26)
#define RKPM_DBG_FSM_SOUT		BIT(27)
#define RKPM_DBG_FSM_STATE		BIT(28)
#define RKPM_DBG_REG			BIT(29)
#define RKPM_DBG_VERBOSE		BIT(30)
#define RKPM_CONFIG_WAKEUP_END		BIT(31)

#endif
