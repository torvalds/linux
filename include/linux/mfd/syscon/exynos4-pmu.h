/*
 * Copyright (C) 2015 Samsung Electronics Co., Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _LINUX_MFD_SYSCON_PMU_EXYNOS4_H_
#define _LINUX_MFD_SYSCON_PMU_EXYNOS4_H_

/* Exynos4 PMU register definitions */

/* MIPI_PHYn_CONTROL register offset: n = 0..1 */
#define EXYNOS4_MIPI_PHY_CONTROL(n)	(0x710 + (n) * 4)
#define EXYNOS4_MIPI_PHY_ENABLE		(1 << 0)
#define EXYNOS4_MIPI_PHY_SRESETN	(1 << 1)
#define EXYNOS4_MIPI_PHY_MRESETN	(1 << 2)
#define EXYNOS4_MIPI_PHY_RESET_MASK	(3 << 1)

#endif /* _LINUX_MFD_SYSCON_PMU_EXYNOS4_H_ */
