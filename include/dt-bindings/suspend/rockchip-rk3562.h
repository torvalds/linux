/* SPDX-License-Identifier: (GPL-2.0+ OR MIT) */
/*
 * Header providing constants for Rockchip suspend bindings.
 *
 * Copyright (C) 2023, Rockchip Electronics Co., Ltd.
 * Author: Shengfei.Xu
 */

#ifndef __DT_BINDINGS_SUSPEND_ROCKCHIP_RK3562_H__
#define __DT_BINDINGS_SUSPEND_ROCKCHIP_RK3562_H__
/******************************bits ops************************************/

#ifndef BIT
#define BIT(nr)				(1 << (nr))
#endif

#define RKPM_SLP_NORMAL_MODE		BIT(0)
#define RKPM_SLP_DEEP1_MODE		BIT(1)
#define RKPM_SLP_DEEP2_MODE		BIT(2)
#define RKPM_SLP_ULTRA_MODE		BIT(3)
#define RKPM_SLP_FROM_UBOOT		BIT(4)
#define RKPM_SLP_PMIC_LP		BIT(5)
#define RKPM_SLP_HW_PLLS_OFF		BIT(6)
#define RKPM_SLP_PMUALIVE_32K		BIT(7)
#define RKPM_SLP_OSC_DIS		BIT(8)
#define RKPM_SLP_32K_EXT		BIT(9)
#define RKPM_SLP_32K_PVTM		BIT(10)
/* the wake up source */
#define RKPM_CPU0_WKUP_EN		BIT(0)
#define RKPM_CPU1_WKUP_EN		BIT(1)
#define RKPM_CPU2_WKUP_EN		BIT(2)
#define RKPM_CPU3_WKUP_EN		BIT(3)
#define RKPM_GPIO0_WKUP_EN		BIT(4)
#define RKPM_GPIO0_EXP_WKUP_EN		BIT(5)
#define RKPM_SDMMC0_WKUP_EN		BIT(6)
#define RKPM_SDMMC1_WKUP_EN		BIT(7)
#define RKPM_PCIE_WKUP_EN		BIT(8)
#define RKPM_SDIO_WKUP_EN		BIT(9)
#define RKPM_USB_WKUP_EN		BIT(10)
#define RKPM_UART0_WKUP_EN		BIT(11)
#define RKPM_PWM0_WKUP_EN		BIT(12)
#define RKPM_PWM0_PWR_WKUP_EN		BIT(13)
#define RKPM_TIMER_WKUP_EN		BIT(14)
#define RKPM_HPTIMER_WKUP_EN		BIT(15)
#define RKPM_SYS_WKUP_EN		BIT(16)
#define RKPM_TIMEOUT_WKUP_EN		BIT(17)
#endif
