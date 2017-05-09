/*
 * Copyright (C) 2016 Cogent Embedded, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef __DT_BINDINGS_CLOCK_R8A7792_H__
#define __DT_BINDINGS_CLOCK_R8A7792_H__

/* CPG */
#define R8A7792_CLK_MAIN		0
#define R8A7792_CLK_PLL0		1
#define R8A7792_CLK_PLL1		2
#define R8A7792_CLK_PLL3		3
#define R8A7792_CLK_LB			4
#define R8A7792_CLK_QSPI		5

/* MSTP0 */
#define R8A7792_CLK_MSIOF0		0

/* MSTP1 */
#define R8A7792_CLK_JPU			6
#define R8A7792_CLK_TMU1		11
#define R8A7792_CLK_TMU3		21
#define R8A7792_CLK_TMU2		22
#define R8A7792_CLK_CMT0		24
#define R8A7792_CLK_TMU0		25
#define R8A7792_CLK_VSP1DU1		27
#define R8A7792_CLK_VSP1DU0		28
#define R8A7792_CLK_VSP1_SY		31

/* MSTP2 */
#define R8A7792_CLK_MSIOF1		8
#define R8A7792_CLK_SYS_DMAC1		18
#define R8A7792_CLK_SYS_DMAC0		19

/* MSTP3 */
#define R8A7792_CLK_TPU0		4
#define R8A7792_CLK_SDHI0		14
#define R8A7792_CLK_CMT1		29

/* MSTP4 */
#define R8A7792_CLK_IRQC		7
#define R8A7792_CLK_INTC_SYS		8

/* MSTP5 */
#define R8A7792_CLK_AUDIO_DMAC0		2
#define R8A7792_CLK_THERMAL		22
#define R8A7792_CLK_PWM			23

/* MSTP7 */
#define R8A7792_CLK_HSCIF1		16
#define R8A7792_CLK_HSCIF0		17
#define R8A7792_CLK_SCIF3		18
#define R8A7792_CLK_SCIF2		19
#define R8A7792_CLK_SCIF1		20
#define R8A7792_CLK_SCIF0		21
#define R8A7792_CLK_DU1			23
#define R8A7792_CLK_DU0			24

/* MSTP8 */
#define R8A7792_CLK_VIN5		4
#define R8A7792_CLK_VIN4		5
#define R8A7792_CLK_VIN3		8
#define R8A7792_CLK_VIN2		9
#define R8A7792_CLK_VIN1		10
#define R8A7792_CLK_VIN0		11
#define R8A7792_CLK_ETHERAVB		12

/* MSTP9 */
#define R8A7792_CLK_GPIO7		4
#define R8A7792_CLK_GPIO6		5
#define R8A7792_CLK_GPIO5		7
#define R8A7792_CLK_GPIO4		8
#define R8A7792_CLK_GPIO3		9
#define R8A7792_CLK_GPIO2		10
#define R8A7792_CLK_GPIO1		11
#define R8A7792_CLK_GPIO0		12
#define R8A7792_CLK_GPIO11		13
#define R8A7792_CLK_GPIO10		14
#define R8A7792_CLK_CAN1		15
#define R8A7792_CLK_CAN0		16
#define R8A7792_CLK_QSPI_MOD		17
#define R8A7792_CLK_GPIO9		19
#define R8A7792_CLK_GPIO8		21
#define R8A7792_CLK_I2C5		25
#define R8A7792_CLK_IICDVFS		26
#define R8A7792_CLK_I2C4		27
#define R8A7792_CLK_I2C3		28
#define R8A7792_CLK_I2C2		29
#define R8A7792_CLK_I2C1		30
#define R8A7792_CLK_I2C0		31

/* MSTP10 */
#define R8A7792_CLK_SSI_ALL		5
#define R8A7792_CLK_SSI4		11
#define R8A7792_CLK_SSI3		12

#endif /* __DT_BINDINGS_CLOCK_R8A7792_H__ */
