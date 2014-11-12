/*
 * Copyright 2014 Ulrich Hecht
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef __DT_BINDINGS_CLOCK_R8A7740_H__
#define __DT_BINDINGS_CLOCK_R8A7740_H__

/* CPG */
#define R8A7740_CLK_SYSTEM	0
#define R8A7740_CLK_PLLC0	1
#define R8A7740_CLK_PLLC1	2
#define R8A7740_CLK_PLLC2	3
#define R8A7740_CLK_R		4
#define R8A7740_CLK_USB24S	5
#define R8A7740_CLK_I		6
#define R8A7740_CLK_ZG		7
#define R8A7740_CLK_B		8
#define R8A7740_CLK_M1		9
#define R8A7740_CLK_HP		10
#define R8A7740_CLK_HPP		11
#define R8A7740_CLK_USBP	12
#define R8A7740_CLK_S		13
#define R8A7740_CLK_ZB		14
#define R8A7740_CLK_M3		15
#define R8A7740_CLK_CP		16

/* MSTP1 */
#define R8A7740_CLK_CEU21	28
#define R8A7740_CLK_CEU20	27
#define R8A7740_CLK_TMU0	25
#define R8A7740_CLK_LCDC1	17
#define R8A7740_CLK_IIC0	16
#define R8A7740_CLK_TMU1	11
#define R8A7740_CLK_LCDC0	0

/* MSTP2 */
#define R8A7740_CLK_SCIFA6	30
#define R8A7740_CLK_SCIFA7	22
#define R8A7740_CLK_DMAC1	18
#define R8A7740_CLK_DMAC2	17
#define R8A7740_CLK_DMAC3	16
#define R8A7740_CLK_USBDMAC	14
#define R8A7740_CLK_SCIFA5	7
#define R8A7740_CLK_SCIFB	6
#define R8A7740_CLK_SCIFA0	4
#define R8A7740_CLK_SCIFA1	3
#define R8A7740_CLK_SCIFA2	2
#define R8A7740_CLK_SCIFA3	1
#define R8A7740_CLK_SCIFA4	0

/* MSTP3 */
#define R8A7740_CLK_CMT1	29
#define R8A7740_CLK_FSI		28
#define R8A7740_CLK_IIC1	23
#define R8A7740_CLK_USBF	20
#define R8A7740_CLK_SDHI0	14
#define R8A7740_CLK_SDHI1	13
#define R8A7740_CLK_MMC		12
#define R8A7740_CLK_GETHER	9
#define R8A7740_CLK_TPU0	4

/* MSTP4 */
#define R8A7740_CLK_USBH	16
#define R8A7740_CLK_SDHI2	15
#define R8A7740_CLK_USBFUNC	7
#define R8A7740_CLK_USBPHY	6

/* SUBCK* */
#define R8A7740_CLK_SUBCK	9
#define R8A7740_CLK_SUBCK2	10

#endif /* __DT_BINDINGS_CLOCK_R8A7740_H__ */
