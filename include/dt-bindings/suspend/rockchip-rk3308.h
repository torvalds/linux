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

/*
 *	RK3308 system suspend mode configure definitions.
 *
 * Driver:
 *	These configures are pass to ATF by SMC in:
 *	drivers/soc/rockchip/rockchip_pm_config.c
 *
 * DTS:
 *	rockchip_suspend: rockchip-suspend {
 *		rockchip,sleep-mode-config = <...>;
 *		rockchip,wakeup-config = <...>;
 *		rockchip,apios-suspend = <...>;
 *		rockchip,pwm-regulator-config = <...>;
 *	};
 */

/*
 * Suspend mode:
 *	rockchip,sleep-mode-config = <...>;
 */
#define RKPM_ARMOFF			BIT(0)	/* vdd_arm off */
#define RKPM_VADOFF			BIT(1)	/* assume vad off, enter lowest system suspend */
#define RKPM_PMU_HW_PLLS_PD		BIT(3)	/* disable PLLs by PMU hardware, recommend */
#define RKPM_PMU_DIS_OSC		BIT(4)	/* disable 24M osc */
#define RKPM_PMU_PMUALIVE_32K		BIT(5)	/* pvtm 32khz */
#define RKPM_PMU_EXT_32K		BIT(6)	/* ext 32khz osc */
#define RKPM_DDR_SREF_HARDWARE		BIT(7)	/* ddr enter self-refresh by PMU hardware, not recommend */
#define RKPM_DDR_EXIT_SRPD_IDLE		BIT(8)	/* ddr exit sr/pd idle by ddr controller,  not recommend */
#define RKPM_PDM_CLK_OFF		BIT(9)	/* armoff with pdm clk off, not recommend */

/*
 * Regulator mode:
 *	rockchip,pwm-regulator-config = <...>;
 */
#define RKPM_PWM_REGULATOR		BIT(2)	/* support pwm regulator */

/*
 * Wakeup source:
 *	rockchip,wakeup-config = <...>;
 */
#define RKPM_ARM_PRE_WAKEUP_EN		BIT(11)	/* all interrupts can wakeup(gic doesn't filter these) */
#define RKPM_ARM_GIC_WAKEUP_EN		BIT(12)	/* all interrupts can wakeup(gic filter these) */
#define RKPM_SDMMC_WAKEUP_EN		BIT(13)	/* sdmmc can wakeup */
#define RKPM_SDMMC_GRF_IRQ_WAKEUP_EN	BIT(14)	/* sdmmc grf irq can wakeup */
#define RKPM_TIMER_WAKEUP_EN		BIT(15)	/* rk timers can wakeup */
#define RKPM_USBDEV_WAKEUP_EN		BIT(16)	/* usbdev can wakeup */
#define RKPM_TIMEOUT_WAKEUP_EN		BIT(17)	/* PMU timeout can wakeup, for self test */
#define RKPM_GPIO0_WAKEUP_EN		BIT(18)	/* gpio0(only) can wakeup */
#define RKPM_VAD_WAKEUP_EN		BIT(19)	/* vad can wakeup */

/*
 * Debug control in system suspend:
 *	rockchip,sleep-mode-config = <...>;
 */
#define RKPM_DBG_INT_TIMER_TEST		BIT(22)	/* enable RKPM_TIMEOUT_WAKEUP_EN */
#define RKPM_DBG_WOARKAROUND		BIT(23)	/* ignore, useless */
#define RKPM_DBG_VAD_INT_OFF		BIT(24)	/* enable RKPM_VADOFF */
#define RKPM_DBG_CLK_UNGATE		BIT(25)	/* enable all clks */
#define RKPM_DBG_CLKOUT			BIT(26) /* enable test_out clk output */
#define RKPM_DBG_FSM_SOUT		BIT(27)	/* FSM state one pin out */
#define RKPM_DBG_FSM_STATE		BIT(28)	/* FSM state multi pins out */
#define RKPM_DBG_REG			BIT(29)	/* verbose regs */
#define RKPM_DBG_VERBOSE		BIT(30)	/* verbose more message */
#define RKPM_CONFIG_WAKEUP_END		BIT(31)	/* ignore, it's a placeholder */

/*
 * GPIOn/PWMn ignore global 1st reset, usually used for pwr_hold pin:
 *	rockchip,apios-suspend = <...>;
 */
#define GLB1RST_IGNORE_PWM0		BIT(23)	/* pwm0 ignore global 1st reset */
#define GLB1RST_IGNORE_PWM1		BIT(24)	/* pwm1 ignore global 1st reset */
#define GLB1RST_IGNORE_PWM2		BIT(25)	/* pwm2 ignore global 1st reset */
#define GLB1RST_IGNORE_GPIO0		BIT(26)	/* gpio0 ignore global 1st reset */
#define GLB1RST_IGNORE_GPIO1		BIT(27)	/* gpio1 ignore global 1st reset */
#define GLB1RST_IGNORE_GPIO2		BIT(28)	/* gpio2 ignore global 1st reset */
#define GLB1RST_IGNORE_GPIO3		BIT(29)	/* gpio3 ignore global 1st reset */
#define GLB1RST_IGNORE_GPIO4		BIT(30)	/* gpio4 ignore global 1st reset */

#endif
