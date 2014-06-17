/*
 * Copyright (c) 2013 Samsung Electronics Co., Ltd.
 * Author: Andrzej Haja <a.hajda-Sze3O3UU22JBDgjK7y7TUQ@public.gmane.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Device Tree binding constants for Exynos5440 clock controller.
*/

#ifndef _DT_BINDINGS_CLOCK_EXYNOS_5440_H
#define _DT_BINDINGS_CLOCK_EXYNOS_5440_H

#define CLK_XTAL		1
#define CLK_ARM_CLK		2
#define CLK_SPI_BAUD		16
#define CLK_PB0_250		17
#define CLK_PR0_250		18
#define CLK_PR1_250		19
#define CLK_B_250		20
#define CLK_B_125		21
#define CLK_B_200		22
#define CLK_SATA		23
#define CLK_USB			24
#define CLK_GMAC0		25
#define CLK_CS250		26
#define CLK_PB0_250_O		27
#define CLK_PR0_250_O		28
#define CLK_PR1_250_O		29
#define CLK_B_250_O		30
#define CLK_B_125_O		31
#define CLK_B_200_O		32
#define CLK_SATA_O		33
#define CLK_USB_O		34
#define CLK_GMAC0_O		35
#define CLK_CS250_O		36

/* must be greater than maximal clock id */
#define CLK_NR_CLKS		37

#endif /* _DT_BINDINGS_CLOCK_EXYNOS_5440_H */
