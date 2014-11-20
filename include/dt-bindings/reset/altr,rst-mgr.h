/*
 * Copyright (c) 2014, Steffen Trumtrar <s.trumtrar@pengutronix.de>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef _DT_BINDINGS_RESET_ALTR_RST_MGR_H
#define _DT_BINDINGS_RESET_ALTR_RST_MGR_H

/* MPUMODRST */
#define CPU0_RESET		0
#define CPU1_RESET		1
#define WDS_RESET		2
#define SCUPER_RESET		3
#define L2_RESET		4

/* PERMODRST */
#define EMAC0_RESET		32
#define EMAC1_RESET		33
#define USB0_RESET		34
#define USB1_RESET		35
#define NAND_RESET		36
#define QSPI_RESET		37
#define L4WD0_RESET		38
#define L4WD1_RESET		39
#define OSC1TIMER0_RESET	40
#define OSC1TIMER1_RESET	41
#define SPTIMER0_RESET		42
#define SPTIMER1_RESET		43
#define I2C0_RESET		44
#define I2C1_RESET		45
#define I2C2_RESET		46
#define I2C3_RESET		47
#define UART0_RESET		48
#define UART1_RESET		49
#define SPIM0_RESET		50
#define SPIM1_RESET		51
#define SPIS0_RESET		52
#define SPIS1_RESET		53
#define SDMMC_RESET		54
#define CAN0_RESET		55
#define CAN1_RESET		56
#define GPIO0_RESET		57
#define GPIO1_RESET		58
#define GPIO2_RESET		59
#define DMA_RESET		60
#define SDR_RESET		61

/* PER2MODRST */
#define DMAIF0_RESET		64
#define DMAIF1_RESET		65
#define DMAIF2_RESET		66
#define DMAIF3_RESET		67
#define DMAIF4_RESET		68
#define DMAIF5_RESET		69
#define DMAIF6_RESET		70
#define DMAIF7_RESET		71

/* BRGMODRST */
#define HPS2FPGA_RESET		96
#define LWHPS2FPGA_RESET	97
#define FPGA2HPS_RESET		98

/* MISCMODRST*/
#define ROM_RESET		128
#define OCRAM_RESET		129
#define SYSMGR_RESET		130
#define SYSMGRCOLD_RESET	131
#define FPGAMGR_RESET		132
#define ACPIDMAP_RESET		133
#define S2F_RESET		134
#define S2FCOLD_RESET		135
#define NRSTPIN_RESET		136
#define TIMESTAMPCOLD_RESET	137
#define CLKMGRCOLD_RESET	138
#define SCANMGR_RESET		139
#define FRZCTRLCOLD_RESET	140
#define SYSDBG_RESET		141
#define DBG_RESET		142
#define TAPCOLD_RESET		143
#define SDRCOLD_RESET		144

#endif
