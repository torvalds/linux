/* SPDX-License-Identifier: (GPL-2.0-only OR MIT) */
/*
 * Copyright (C) 2024-2025 Amlogic, Inc. All rights reserved
 */

#ifndef __T7_PLL_CLKC_H
#define __T7_PLL_CLKC_H

/* GP0 */
#define CLKID_GP0_PLL_DCO	0
#define CLKID_GP0_PLL		1

/* GP1 */
#define CLKID_GP1_PLL_DCO	0
#define CLKID_GP1_PLL		1

/* HIFI */
#define CLKID_HIFI_PLL_DCO	0
#define CLKID_HIFI_PLL		1

/* PCIE */
#define CLKID_PCIE_PLL_DCO	0
#define CLKID_PCIE_PLL_DCO_DIV2	1
#define CLKID_PCIE_PLL_OD	2
#define CLKID_PCIE_PLL		3

/* MPLL */
#define CLKID_MPLL_PREDIV	0
#define CLKID_MPLL0_DIV		1
#define CLKID_MPLL0		2
#define CLKID_MPLL1_DIV		3
#define CLKID_MPLL1		4
#define CLKID_MPLL2_DIV		5
#define CLKID_MPLL2		6
#define CLKID_MPLL3_DIV		7
#define CLKID_MPLL3		8

/* HDMI */
#define CLKID_HDMI_PLL_DCO	0
#define CLKID_HDMI_PLL_OD	1
#define CLKID_HDMI_PLL		2

/* MCLK */
#define CLKID_MCLK_PLL_DCO	0
#define CLKID_MCLK_PRE		1
#define CLKID_MCLK_PLL		2
#define CLKID_MCLK_0_SEL	3
#define CLKID_MCLK_0_DIV2	4
#define CLKID_MCLK_0_PRE	5
#define CLKID_MCLK_0		6
#define CLKID_MCLK_1_SEL	7
#define CLKID_MCLK_1_DIV2	8
#define CLKID_MCLK_1_PRE	9
#define CLKID_MCLK_1		10

#endif /* __T7_PLL_CLKC_H */
