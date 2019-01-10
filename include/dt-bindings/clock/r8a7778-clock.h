/*
 * Copyright (C) 2014 Ulrich Hecht
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef __DT_BINDINGS_CLOCK_R8A7778_H__
#define __DT_BINDINGS_CLOCK_R8A7778_H__

/* CPG */
#define R8A7778_CLK_PLLA	0
#define R8A7778_CLK_PLLB	1
#define R8A7778_CLK_B		2
#define R8A7778_CLK_OUT		3
#define R8A7778_CLK_P		4
#define R8A7778_CLK_S		5
#define R8A7778_CLK_S1		6

/* MSTP0 */
#define R8A7778_CLK_I2C0	30
#define R8A7778_CLK_I2C1	29
#define R8A7778_CLK_I2C2	28
#define R8A7778_CLK_I2C3	27
#define R8A7778_CLK_SCIF0	26
#define R8A7778_CLK_SCIF1	25
#define R8A7778_CLK_SCIF2	24
#define R8A7778_CLK_SCIF3	23
#define R8A7778_CLK_SCIF4	22
#define R8A7778_CLK_SCIF5	21
#define R8A7778_CLK_HSCIF0	19
#define R8A7778_CLK_HSCIF1	18
#define R8A7778_CLK_TMU0	16
#define R8A7778_CLK_TMU1	15
#define R8A7778_CLK_TMU2	14
#define R8A7778_CLK_SSI0	12
#define R8A7778_CLK_SSI1	11
#define R8A7778_CLK_SSI2	10
#define R8A7778_CLK_SSI3	9
#define R8A7778_CLK_SRU		8
#define R8A7778_CLK_HSPI	7

/* MSTP1 */
#define R8A7778_CLK_ETHER	14
#define R8A7778_CLK_VIN0	10
#define R8A7778_CLK_VIN1	9
#define R8A7778_CLK_USB		0

/* MSTP3 */
#define R8A7778_CLK_MMC		31
#define R8A7778_CLK_SDHI0	23
#define R8A7778_CLK_SDHI1	22
#define R8A7778_CLK_SDHI2	21
#define R8A7778_CLK_SSI4	11
#define R8A7778_CLK_SSI5	10
#define R8A7778_CLK_SSI6	9
#define R8A7778_CLK_SSI7	8
#define R8A7778_CLK_SSI8	7

/* MSTP5 */
#define R8A7778_CLK_SRU_SRC0	31
#define R8A7778_CLK_SRU_SRC1	30
#define R8A7778_CLK_SRU_SRC2	29
#define R8A7778_CLK_SRU_SRC3	28
#define R8A7778_CLK_SRU_SRC4	27
#define R8A7778_CLK_SRU_SRC5	26
#define R8A7778_CLK_SRU_SRC6	25
#define R8A7778_CLK_SRU_SRC7	24
#define R8A7778_CLK_SRU_SRC8	23

#endif /* __DT_BINDINGS_CLOCK_R8A7778_H__ */
