/* SPDX-License-Identifier: (GPL-2.0+ OR MIT) */
/*
 * Header providing constants for Rockchip suspend bindings.
 *
 * Copyright (C) 2022, Rockchip Electronics Co., Ltd
 * Author: XiaoDong.Huang
 */

#ifndef __DT_BINDINGS_RK3528_PM_H__
#define __DT_BINDINGS_RK3528_PM_H__
/******************************bits ops************************************/

#ifndef BIT
#define BIT(nr)				(1 << (nr))
#endif

#define RKPM_SLP_ARMPD			BIT(0)
#define RKPM_SLP_ARMOFF			BIT(1)
#define RKPM_SLP_ARMOFF_DDRPD		BIT(2)
#define RKPM_SLP_ARMOFF_LOGOFF		BIT(3)

/* all plls except ddr's pll*/
#define RKPM_SLP_PMU_HW_PLLS_PD		BIT(8)
#define RKPM_SLP_PMU_PMUALIVE_32K	BIT(9)
#define RKPM_SLP_PMU_DIS_OSC		BIT(10)

#define RKPM_SLP_CLK_GT			BIT(16)
#define RKPM_SLP_PMIC_LP		BIT(17)

#define RKPM_SLP_32K_EXT		BIT(24)
#define RKPM_SLP_TIME_OUT_WKUP		BIT(25)
#define RKPM_SLP_PMU_DBG		BIT(26)

/* the wake up source */
#define RKPM_CPU0_WKUP_EN		BIT(0)
#define RKPM_CPU1_WKUP_EN		BIT(1)
#define RKPM_CPU2_WKUP_EN		BIT(2)
#define RKPM_CPU3_WKUP_EN		BIT(3)
#define RKPM_GPIO_WKUP_EN		BIT(4)
#define RKPM_HDMI_HDP_WKUP_EN		BIT(5)
#define RKPM_HDMI_CEC_WKUP_EN		BIT(6)
#define RKPM_PWMIR_WKUP_EN		BIT(7)
#define RKPM_GMAC_WKUP_EN		BIT(8)
#define RKPM_TIMER_WKUP_EN		BIT(9)
#define RKPM_USBDEV_WKUP_EN		BIT(10)
#define RKPM_SYSINT_WKUP_EN		BIT(11)
#define RKPM_TIME_OUT_WKUP_EN		BIT(12)

/* the pwm regulator */
#define RKPM_PWM0_M0_REGULATOR_EN	BIT(0)
#define RKPM_PWM1_M0_REGULATOR_EN	BIT(1)
#define RKPM_PWM2_M0_REGULATOR_EN	BIT(2)

/* sleep pin */
#define RKPM_SLEEP_PIN0_EN		BIT(0)	/* GPIO4_C2 */
#define RKPM_SLEEP_PIN1_EN		BIT(1)	/* GPIO4_B6 */
#define RKPM_SLEEP_PIN2_EN		BIT(2)	/* GPIO0_A0 */
#define RKPM_SLEEP_PIN3_EN		BIT(3)	/* GPIO0_A1 */
#define RKPM_SLEEP_PIN4_EN		BIT(4)	/* GPIO0_A2 */
#define RKPM_SLEEP_PIN5_EN		BIT(5)	/* GPIO0_A3 */
#define RKPM_SLEEP_PIN6_EN		BIT(6)	/* GPIO0_A4 */
#define RKPM_SLEEP_PIN7_EN		BIT(7)	/* GPIO0_A5 */

#define RKPM_SLEEP_PIN0_ACT_LOW		BIT(0)	/* GPIO4_C2 */
#define RKPM_SLEEP_PIN1_ACT_LOW		BIT(1)	/* GPIO4_B6 */
#define RKPM_SLEEP_PIN2_7_ACT_LOW	0xfc	/* GPIO0_A0~5 */
#endif
