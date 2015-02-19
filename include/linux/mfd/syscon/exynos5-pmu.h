/*
 * Exynos5 SoC series Power Management Unit (PMU) register offsets
 * and bit definitions.
 *
 * Copyright (C) 2014 Samsung Electronics Co., Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _LINUX_MFD_SYSCON_PMU_EXYNOS5_H_
#define _LINUX_MFD_SYSCON_PMU_EXYNOS5_H_

/* Exynos5 PMU register definitions */
#define EXYNOS5_HDMI_PHY_CONTROL		(0x700)
#define EXYNOS5_USBDRD_PHY_CONTROL		(0x704)

/* Exynos5250 specific register definitions */
#define EXYNOS5_USBHOST_PHY_CONTROL		(0x708)
#define EXYNOS5_EFNAND_PHY_CONTROL		(0x70c)
#define EXYNOS5_MIPI_PHY0_CONTROL		(0x710)
#define EXYNOS5_MIPI_PHY1_CONTROL		(0x714)
#define EXYNOS5_ADC_PHY_CONTROL			(0x718)
#define EXYNOS5_MTCADC_PHY_CONTROL		(0x71c)
#define EXYNOS5_DPTX_PHY_CONTROL		(0x720)
#define EXYNOS5_SATA_PHY_CONTROL		(0x724)

/* Exynos5420 specific register definitions */
#define EXYNOS5420_USBDRD1_PHY_CONTROL		(0x708)
#define EXYNOS5420_USBHOST_PHY_CONTROL		(0x70c)
#define EXYNOS5420_MIPI_PHY0_CONTROL		(0x714)
#define EXYNOS5420_MIPI_PHY1_CONTROL		(0x718)
#define EXYNOS5420_MIPI_PHY2_CONTROL		(0x71c)
#define EXYNOS5420_ADC_PHY_CONTROL		(0x720)
#define EXYNOS5420_MTCADC_PHY_CONTROL		(0x724)
#define EXYNOS5420_DPTX_PHY_CONTROL		(0x728)

#define EXYNOS5_PHY_ENABLE			BIT(0)

#define EXYNOS5_MIPI_PHY_S_RESETN		BIT(1)
#define EXYNOS5_MIPI_PHY_M_RESETN		BIT(2)

#endif /* _LINUX_MFD_SYSCON_PMU_EXYNOS5_H_ */
