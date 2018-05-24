/*
 * Device Tree constants for the Texas Instruments DP83867 PHY
 *
 * Author: Dan Murphy <dmurphy@ti.com>
 *
 * Copyright:   (C) 2015 Texas Instruments, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */

#ifndef _DT_BINDINGS_TI_DP83867_H
#define _DT_BINDINGS_TI_DP83867_H

/* PHY CTRL bits */
#define DP83867_PHYCR_FIFO_DEPTH_3_B_NIB	0x00
#define DP83867_PHYCR_FIFO_DEPTH_4_B_NIB	0x01
#define DP83867_PHYCR_FIFO_DEPTH_6_B_NIB	0x02
#define DP83867_PHYCR_FIFO_DEPTH_8_B_NIB	0x03

/* RGMIIDCTL internal delay for rx and tx */
#define	DP83867_RGMIIDCTL_250_PS	0x0
#define	DP83867_RGMIIDCTL_500_PS	0x1
#define	DP83867_RGMIIDCTL_750_PS	0x2
#define	DP83867_RGMIIDCTL_1_NS		0x3
#define	DP83867_RGMIIDCTL_1_25_NS	0x4
#define	DP83867_RGMIIDCTL_1_50_NS	0x5
#define	DP83867_RGMIIDCTL_1_75_NS	0x6
#define	DP83867_RGMIIDCTL_2_00_NS	0x7
#define	DP83867_RGMIIDCTL_2_25_NS	0x8
#define	DP83867_RGMIIDCTL_2_50_NS	0x9
#define	DP83867_RGMIIDCTL_2_75_NS	0xa
#define	DP83867_RGMIIDCTL_3_00_NS	0xb
#define	DP83867_RGMIIDCTL_3_25_NS	0xc
#define	DP83867_RGMIIDCTL_3_50_NS	0xd
#define	DP83867_RGMIIDCTL_3_75_NS	0xe
#define	DP83867_RGMIIDCTL_4_00_NS	0xf

/* IO_MUX_CFG - Clock output selection */
#define DP83867_CLK_O_SEL_CHN_A_RCLK		0x0
#define DP83867_CLK_O_SEL_CHN_B_RCLK		0x1
#define DP83867_CLK_O_SEL_CHN_C_RCLK		0x2
#define DP83867_CLK_O_SEL_CHN_D_RCLK		0x3
#define DP83867_CLK_O_SEL_CHN_A_RCLK_DIV5	0x4
#define DP83867_CLK_O_SEL_CHN_B_RCLK_DIV5	0x5
#define DP83867_CLK_O_SEL_CHN_C_RCLK_DIV5	0x6
#define DP83867_CLK_O_SEL_CHN_D_RCLK_DIV5	0x7
#define DP83867_CLK_O_SEL_CHN_A_TCLK		0x8
#define DP83867_CLK_O_SEL_CHN_B_TCLK		0x9
#define DP83867_CLK_O_SEL_CHN_C_TCLK		0xA
#define DP83867_CLK_O_SEL_CHN_D_TCLK		0xB
#define DP83867_CLK_O_SEL_REF_CLK		0xC
#endif
