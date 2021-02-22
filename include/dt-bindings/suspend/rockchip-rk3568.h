/* SPDX-License-Identifier: (GPL-2.0+ OR MIT) */
/*
 * Header providing constants for Rockchip suspend bindings.
 *
 * Copyright (C) 2021, Rockchip Electronics Co., Ltd.
 * Author: XiaoDong.Huang
 */

#ifndef __DT_BINDINGS_SUSPEND_ROCKCHIP_RK3568_H__
#define __DT_BINDINGS_SUSPEND_ROCKCHIP_RK3568_H__
/******************************bits ops************************************/

#ifndef BIT
#define BIT(nr)				(1 << (nr))
#endif

#define RKPM_SLP_WFI			BIT(0)
#define RKPM_SLP_ARMOFF			BIT(1)
#define RKPM_SLP_CENTER_OFF		BIT(2)
#define RKPM_SLP_ARMOFF_LOGOFF		BIT(3)
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
#define RKPM_GPIO_WKUP_EN		BIT(4)
#define RKPM_UART0_WKUP_EN		BIT(5)
#define RKPM_SDMMC0_WKUP_EN		BIT(6)
#define RKPM_SDMMC1_WKUP_EN		BIT(7)
#define RKPM_SDMMC2_WKUP_EN		BIT(8)
#define RKPM_USB_WKUP_EN		BIT(9)
#define RKPM_PCIE_WKUP_EN		BIT(10)
#define RKPM_VAD_WKUP_EN		BIT(11)
#define RKPM_TIMER_WKUP_EN		BIT(12)
#define RKPM_PWM0_WKUP_EN		BIT(13)
#define RKPM_TIMEOUT_WKUP_EN		BIT(14)
#define RKPM_SFT_WKUP_EN		BIT(15)
#define RKPM_USB_LINESTATE_WKUP_EN	BIT(16)

#define RKPM_SLP_LDO1_ON		BIT(0)
#define RKPM_SLP_LDO2_ON		BIT(1)
#define RKPM_SLP_LDO3_ON		BIT(2)
#define RKPM_SLP_LDO4_ON		BIT(3)
#define RKPM_SLP_LDO5_ON		BIT(4)
#define RKPM_SLP_LDO6_ON		BIT(5)
#define RKPM_SLP_LDO7_ON		BIT(6)
#define RKPM_SLP_LDO8_ON		BIT(7)
#define RKPM_SLP_LDO9_ON		BIT(8)

#endif
