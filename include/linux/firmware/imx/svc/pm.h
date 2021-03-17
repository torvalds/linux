/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (C) 2016 Freescale Semiconductor, Inc.
 * Copyright 2017-2018 NXP
 *
 * Header file containing the public API for the System Controller (SC)
 * Power Management (PM) function. This includes functions for power state
 * control, clock control, reset control, and wake-up event control.
 *
 * PM_SVC (SVC) Power Management Service
 *
 * Module for the Power Management (PM) service.
 */

#ifndef _SC_PM_API_H
#define _SC_PM_API_H

#include <linux/firmware/imx/sci.h>

/*
 * This type is used to indicate RPC PM function calls.
 */
enum imx_sc_pm_func {
	IMX_SC_PM_FUNC_UNKNOWN = 0,
	IMX_SC_PM_FUNC_SET_SYS_POWER_MODE = 19,
	IMX_SC_PM_FUNC_SET_PARTITION_POWER_MODE = 1,
	IMX_SC_PM_FUNC_GET_SYS_POWER_MODE = 2,
	IMX_SC_PM_FUNC_SET_RESOURCE_POWER_MODE = 3,
	IMX_SC_PM_FUNC_GET_RESOURCE_POWER_MODE = 4,
	IMX_SC_PM_FUNC_REQ_LOW_POWER_MODE = 16,
	IMX_SC_PM_FUNC_SET_CPU_RESUME_ADDR = 17,
	IMX_SC_PM_FUNC_REQ_SYS_IF_POWER_MODE = 18,
	IMX_SC_PM_FUNC_SET_CLOCK_RATE = 5,
	IMX_SC_PM_FUNC_GET_CLOCK_RATE = 6,
	IMX_SC_PM_FUNC_CLOCK_ENABLE = 7,
	IMX_SC_PM_FUNC_SET_CLOCK_PARENT = 14,
	IMX_SC_PM_FUNC_GET_CLOCK_PARENT = 15,
	IMX_SC_PM_FUNC_RESET = 13,
	IMX_SC_PM_FUNC_RESET_REASON = 10,
	IMX_SC_PM_FUNC_BOOT = 8,
	IMX_SC_PM_FUNC_REBOOT = 9,
	IMX_SC_PM_FUNC_REBOOT_PARTITION = 12,
	IMX_SC_PM_FUNC_CPU_START = 11,
};

/*
 * Defines for ALL parameters
 */
#define IMX_SC_PM_CLK_ALL		UINT8_MAX	/* All clocks */

/*
 * Defines for SC PM Power Mode
 */
#define IMX_SC_PM_PW_MODE_OFF	0	/* Power off */
#define IMX_SC_PM_PW_MODE_STBY	1	/* Power in standby */
#define IMX_SC_PM_PW_MODE_LP	2	/* Power in low-power */
#define IMX_SC_PM_PW_MODE_ON	3	/* Power on */

/*
 * Defines for SC PM CLK
 */
#define IMX_SC_PM_CLK_SLV_BUS	0	/* Slave bus clock */
#define IMX_SC_PM_CLK_MST_BUS	1	/* Master bus clock */
#define IMX_SC_PM_CLK_PER	2	/* Peripheral clock */
#define IMX_SC_PM_CLK_PHY	3	/* Phy clock */
#define IMX_SC_PM_CLK_MISC	4	/* Misc clock */
#define IMX_SC_PM_CLK_MISC0	0	/* Misc 0 clock */
#define IMX_SC_PM_CLK_MISC1	1	/* Misc 1 clock */
#define IMX_SC_PM_CLK_MISC2	2	/* Misc 2 clock */
#define IMX_SC_PM_CLK_MISC3	3	/* Misc 3 clock */
#define IMX_SC_PM_CLK_MISC4	4	/* Misc 4 clock */
#define IMX_SC_PM_CLK_CPU	2	/* CPU clock */
#define IMX_SC_PM_CLK_PLL	4	/* PLL */
#define IMX_SC_PM_CLK_BYPASS	4	/* Bypass clock */

/*
 * Defines for SC PM CLK Parent
 */
#define IMX_SC_PM_PARENT_XTAL	0	/* Parent is XTAL. */
#define IMX_SC_PM_PARENT_PLL0	1	/* Parent is PLL0 */
#define IMX_SC_PM_PARENT_PLL1	2	/* Parent is PLL1 or PLL0/2 */
#define IMX_SC_PM_PARENT_PLL2	3	/* Parent in PLL2 or PLL0/4 */
#define IMX_SC_PM_PARENT_BYPS	4	/* Parent is a bypass clock. */

#endif /* _SC_PM_API_H */
