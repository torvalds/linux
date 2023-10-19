/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Interconnect framework driver for i.MX SoC
 *
 * Copyright (c) 2019, BayLibre
 * Copyright (c) 2019-2020, NXP
 * Author: Alexandre Bailon <abailon@baylibre.com>
 */

#ifndef __DT_BINDINGS_INTERCONNECT_IMX8MM_H
#define __DT_BINDINGS_INTERCONNECT_IMX8MM_H

#define IMX8MM_ICN_NOC		1
#define IMX8MM_ICS_DRAM		2
#define IMX8MM_ICS_OCRAM	3
#define IMX8MM_ICM_A53		4

#define IMX8MM_ICM_VPU_H1	5
#define IMX8MM_ICM_VPU_G1	6
#define IMX8MM_ICM_VPU_G2	7
#define IMX8MM_ICN_VIDEO	8

#define IMX8MM_ICM_GPU2D	9
#define IMX8MM_ICM_GPU3D	10
#define IMX8MM_ICN_GPU		11

#define IMX8MM_ICM_CSI		12
#define IMX8MM_ICM_LCDIF	13
#define IMX8MM_ICN_MIPI		14

#define IMX8MM_ICM_USB1		15
#define IMX8MM_ICM_USB2		16
#define IMX8MM_ICM_PCIE		17
#define IMX8MM_ICN_HSIO		18

#define IMX8MM_ICM_SDMA2	19
#define IMX8MM_ICM_SDMA3	20
#define IMX8MM_ICN_AUDIO	21

#define IMX8MM_ICN_ENET		22
#define IMX8MM_ICM_ENET		23

#define IMX8MM_ICN_MAIN		24
#define IMX8MM_ICM_NAND		25
#define IMX8MM_ICM_SDMA1	26
#define IMX8MM_ICM_USDHC1	27
#define IMX8MM_ICM_USDHC2	28
#define IMX8MM_ICM_USDHC3	29

#endif /* __DT_BINDINGS_INTERCONNECT_IMX8MM_H */
