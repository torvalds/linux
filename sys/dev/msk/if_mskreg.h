/******************************************************************************
 *
 * Name:	skgehw.h
 * Project:	Gigabit Ethernet Adapters, Common Modules
 * Version:	$Revision: 2.49 $
 * Date:	$Date: 2005/01/20 13:01:35 $
 * Purpose:	Defines and Macros for the Gigabit Ethernet Adapter Product Family
 *
 ******************************************************************************/

/******************************************************************************
 *
 *	LICENSE:
 *	Copyright (C) Marvell International Ltd. and/or its affiliates
 *
 *	The computer program files contained in this folder ("Files")
 *	are provided to you under the BSD-type license terms provided
 *	below, and any use of such Files and any derivative works
 *	thereof created by you shall be governed by the following terms
 *	and conditions:
 *
 *	- Redistributions of source code must retain the above copyright
 *	  notice, this list of conditions and the following disclaimer.
 *	- Redistributions in binary form must reproduce the above
 *	  copyright notice, this list of conditions and the following
 *	  disclaimer in the documentation and/or other materials provided
 *	  with the distribution.
 *	- Neither the name of Marvell nor the names of its contributors
 *	  may be used to endorse or promote products derived from this
 *	  software without specific prior written permission.
 *
 *	THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *	"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *	LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 *	FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 *	COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 *	INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 *	BUT NOT LIMITED TO, PROCUREMENT OF  SUBSTITUTE GOODS OR SERVICES;
 *	LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 *	HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 *	STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 *	ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 *	OF THE POSSIBILITY OF SUCH DAMAGE.
 *	/LICENSE
 *
 ******************************************************************************/

/*-
 * SPDX-License-Identifier: BSD-4-Clause AND BSD-3-Clause
 *
 * Copyright (c) 1997, 1998, 1999, 2000
 *	Bill Paul <wpaul@ctr.columbia.edu>.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by Bill Paul.
 * 4. Neither the name of the author nor the names of any co-contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY Bill Paul AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL Bill Paul OR THE VOICES IN HIS HEAD
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

/*-
 * Copyright (c) 2003 Nathan L. Binkert <binkertn@umich.edu>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/*$FreeBSD$*/

/*
 * SysKonnect PCI vendor ID
 */
#define VENDORID_SK		0x1148

/*
 * Marvell PCI vendor ID
 */
#define VENDORID_MARVELL	0x11AB

/*
 * D-Link PCI vendor ID
 */
#define	VENDORID_DLINK		0x1186

/*
 * SysKonnect ethernet device IDs
 */
#define DEVICEID_SK_YUKON2	0x9000
#define DEVICEID_SK_YUKON2_EXPR	0x9e00

/*
 * Marvell gigabit ethernet device IDs
 */
#define DEVICEID_MRVL_8021CU	0x4340
#define DEVICEID_MRVL_8022CU	0x4341
#define DEVICEID_MRVL_8061CU	0x4342
#define DEVICEID_MRVL_8062CU	0x4343
#define DEVICEID_MRVL_8021X	0x4344
#define DEVICEID_MRVL_8022X	0x4345
#define DEVICEID_MRVL_8061X	0x4346
#define DEVICEID_MRVL_8062X	0x4347
#define DEVICEID_MRVL_8035	0x4350
#define DEVICEID_MRVL_8036	0x4351
#define DEVICEID_MRVL_8038	0x4352
#define DEVICEID_MRVL_8039	0x4353
#define DEVICEID_MRVL_8040	0x4354
#define DEVICEID_MRVL_8040T	0x4355
#define DEVICEID_MRVL_8042	0x4357
#define DEVICEID_MRVL_8048	0x435A
#define DEVICEID_MRVL_4360	0x4360
#define DEVICEID_MRVL_4361	0x4361
#define DEVICEID_MRVL_4362	0x4362
#define DEVICEID_MRVL_4363	0x4363
#define DEVICEID_MRVL_4364	0x4364
#define DEVICEID_MRVL_4365	0x4365
#define DEVICEID_MRVL_436A	0x436A
#define DEVICEID_MRVL_436B	0x436B
#define DEVICEID_MRVL_436C	0x436C
#define DEVICEID_MRVL_436D	0x436D
#define DEVICEID_MRVL_4370	0x4370
#define DEVICEID_MRVL_4380	0x4380
#define DEVICEID_MRVL_4381	0x4381

/*
 * D-Link gigabit ethernet device ID
 */
#define DEVICEID_DLINK_DGE550SX	0x4001
#define DEVICEID_DLINK_DGE560SX	0x4002
#define DEVICEID_DLINK_DGE560T	0x4b00

#define BIT_31		(1U << 31)
#define BIT_30		(1 << 30)
#define BIT_29		(1 << 29)
#define BIT_28		(1 << 28)
#define BIT_27		(1 << 27)
#define BIT_26		(1 << 26)
#define BIT_25		(1 << 25)
#define BIT_24		(1 << 24)
#define BIT_23		(1 << 23)
#define BIT_22		(1 << 22)
#define BIT_21		(1 << 21)
#define BIT_20		(1 << 20)
#define BIT_19		(1 << 19)
#define BIT_18		(1 << 18)
#define BIT_17		(1 << 17)
#define BIT_16		(1 << 16)
#define BIT_15		(1 << 15)
#define BIT_14		(1 << 14)
#define BIT_13		(1 << 13)
#define BIT_12		(1 << 12)
#define BIT_11		(1 << 11)
#define BIT_10		(1 << 10)
#define BIT_9		(1 << 9)
#define BIT_8		(1 << 8)
#define BIT_7		(1 << 7)
#define BIT_6		(1 << 6)
#define BIT_5		(1 << 5)
#define BIT_4		(1 << 4)
#define BIT_3		(1 << 3)
#define BIT_2		(1 << 2)
#define BIT_1		(1 << 1)
#define BIT_0		(1 << 0)

#define SHIFT31(x)	((x) << 31)
#define SHIFT30(x)	((x) << 30)
#define SHIFT29(x)	((x) << 29)
#define SHIFT28(x)	((x) << 28)
#define SHIFT27(x)	((x) << 27)
#define SHIFT26(x)	((x) << 26)
#define SHIFT25(x)	((x) << 25)
#define SHIFT24(x)	((x) << 24)
#define SHIFT23(x)	((x) << 23)
#define SHIFT22(x)	((x) << 22)
#define SHIFT21(x)	((x) << 21)
#define SHIFT20(x)	((x) << 20)
#define SHIFT19(x)	((x) << 19)
#define SHIFT18(x)	((x) << 18)
#define SHIFT17(x)	((x) << 17)
#define SHIFT16(x)	((x) << 16)
#define SHIFT15(x)	((x) << 15)
#define SHIFT14(x)	((x) << 14)
#define SHIFT13(x)	((x) << 13)
#define SHIFT12(x)	((x) << 12)
#define SHIFT11(x)	((x) << 11)
#define SHIFT10(x)	((x) << 10)
#define SHIFT9(x)	((x) << 9)
#define SHIFT8(x)	((x) << 8)
#define SHIFT7(x)	((x) << 7)
#define SHIFT6(x)	((x) << 6)
#define SHIFT5(x)	((x) << 5)
#define SHIFT4(x)	((x) << 4)
#define SHIFT3(x)	((x) << 3)
#define SHIFT2(x)	((x) << 2)
#define SHIFT1(x)	((x) << 1)
#define SHIFT0(x)	((x) << 0)

/*
 * PCI Configuration Space header
 */
#define PCI_BASE_1ST	0x10	/* 32 bit 1st Base address */
#define PCI_BASE_2ND	0x14	/* 32 bit 2nd Base address */
#define PCI_OUR_REG_1	0x40	/* 32 bit Our Register 1 */
#define PCI_OUR_REG_2	0x44	/* 32 bit Our Register 2 */
#define PCI_OUR_STATUS	0x7c	/* 32 bit Adapter Status Register */
#define PCI_OUR_REG_3	0x80	/* 32 bit Our Register 3 */
#define PCI_OUR_REG_4	0x84	/* 32 bit Our Register 4 */
#define PCI_OUR_REG_5	0x88	/* 32 bit Our Register 5 */
#define PCI_CFG_REG_0	0x90	/* 32 bit Config Register 0 */
#define PCI_CFG_REG_1	0x94	/* 32 bit Config Register 1 */

/* PCI Express Capability */
#define PEX_CAP_ID	0xe0	/*  8 bit PEX Capability ID */
#define PEX_NITEM	0xe1	/*  8 bit PEX Next Item Pointer */
#define PEX_CAP_REG	0xe2	/* 16 bit PEX Capability Register */
#define PEX_DEV_CAP	0xe4	/* 32 bit PEX Device Capabilities */
#define PEX_DEV_CTRL	0xe8	/* 16 bit PEX Device Control */
#define PEX_DEV_STAT	0xea	/* 16 bit PEX Device Status */
#define PEX_LNK_CAP	0xec	/* 32 bit PEX Link Capabilities */
#define PEX_LNK_CTRL	0xf0	/* 16 bit PEX Link Control */
#define PEX_LNK_STAT	0xf2	/* 16 bit PEX Link Status */

/* PCI Express Extended Capabilities */
#define PEX_ADV_ERR_REP	0x100	/* 32 bit PEX Advanced Error Reporting */
#define PEX_UNC_ERR_STAT	0x104	/* 32 bit PEX Uncorr. Errors Status */
#define PEX_UNC_ERR_MASK	0x108	/* 32 bit PEX Uncorr. Errors Mask */
#define PEX_UNC_ERR_SEV		0x10c	/* 32 bit PEX Uncorr. Errors Severity */
#define PEX_COR_ERR_STAT	0x110	/* 32 bit PEX Correc. Errors Status */
#define PEX_COR_ERR_MASK	0x114	/* 32 bit PEX Correc. Errors Mask */
#define PEX_ADV_ERR_CAP_C	0x118	/* 32 bit PEX Advanced Error Cap./Ctrl */
#define PEX_HEADER_LOG		0x11c	/* 4x32 bit PEX Header Log Register */

/*	PCI_OUR_REG_1	32 bit	Our Register 1 */
#define PCI_Y2_PIG_ENA		BIT_31	/* Enable Plug-in-Go (YUKON-2) */
#define PCI_Y2_DLL_DIS		BIT_30	/* Disable PCI DLL (YUKON-2) */
#define PCI_Y2_PHY2_COMA	BIT_29	/* Set PHY 2 to Coma Mode (YUKON-2) */
#define PCI_Y2_PHY1_COMA	BIT_28	/* Set PHY 1 to Coma Mode (YUKON-2) */
#define PCI_Y2_PHY2_POWD	BIT_27	/* Set PHY 2 to Power Down (YUKON-2) */
#define PCI_Y2_PHY1_POWD	BIT_26	/* Set PHY 1 to Power Down (YUKON-2) */
#define PCI_DIS_BOOT		BIT_24	/* Disable BOOT via ROM */
#define PCI_EN_IO		BIT_23	/* Mapping to I/O space */
#define PCI_EN_FPROM		BIT_22	/* Enable FLASH mapping to memory */
					/* 1 = Map Flash to memory */
					/* 0 = Disable addr. dec */
#define PCI_PAGESIZE		(3L<<20)/* Bit 21..20:	FLASH Page Size	*/
#define PCI_PAGE_16		(0L<<20)/*		16 k pages	*/
#define PCI_PAGE_32K		(1L<<20)/*		32 k pages	*/
#define PCI_PAGE_64K		(2L<<20)/*		64 k pages	*/
#define PCI_PAGE_128K		(3L<<20)/*		128 k pages	*/
#define PCI_PAGEREG		(7L<<16)/* Bit 18..16:	Page Register	*/
#define PCI_PEX_LEGNAT		BIT_15	/* PEX PM legacy/native mode (YUKON-2) */
#define PCI_FORCE_BE		BIT_14	/* Assert all BEs on MR */
#define PCI_DIS_MRL		BIT_13	/* Disable Mem Read Line */
#define PCI_DIS_MRM		BIT_12	/* Disable Mem Read Multiple */
#define PCI_DIS_MWI		BIT_11	/* Disable Mem Write & Invalidate */
#define PCI_DISC_CLS		BIT_10	/* Disc: cacheLsz bound */
#define PCI_BURST_DIS		BIT_9	/* Burst Disable */
#define PCI_DIS_PCI_CLK		BIT_8	/* Disable PCI clock driving */
#define PCI_SKEW_DAS		(0xfL<<4)/* Bit	7.. 4:	Skew Ctrl, DAS Ext */
#define PCI_SKEW_BASE		0xfL	/* Bit	3.. 0:	Skew Ctrl, Base	*/
#define PCI_CLS_OPT		BIT_3	/* Cache Line Size opt. PCI-X (YUKON-2) */

/*	PCI_OUR_REG_2	32 bit	Our Register 2 */
#define PCI_VPD_WR_THR	(0xff<<24)	/* Bit 31..24:	VPD Write Threshold */
#define PCI_DEV_SEL	(0x7f<<17)	/* Bit 23..17:	EEPROM Device Select */
#define PCI_VPD_ROM_SZ	(0x07<<14)	/* Bit 16..14:	VPD ROM Size	*/
					/* Bit 13..12:	reserved	*/
#define PCI_PATCH_DIR	(0x0f<<8)	/* Bit 11.. 8:	Ext Patches dir 3..0 */
#define PCI_PATCH_DIR_3	BIT_11
#define PCI_PATCH_DIR_2	BIT_10
#define PCI_PATCH_DIR_1	BIT_9
#define PCI_PATCH_DIR_0	BIT_8
#define PCI_EXT_PATCHS	(0x0f<<4)	/* Bit	7.. 4:	Extended Patches 3..0 */
#define PCI_EXT_PATCH_3	BIT_7
#define PCI_EXT_PATCH_2	BIT_6
#define PCI_EXT_PATCH_1	BIT_5
#define PCI_EXT_PATCH_0	BIT_4
#define PCI_EN_DUMMY_RD	BIT_3		/* Enable Dummy Read */
#define PCI_REV_DESC	BIT_2		/* Reverse Desc. Bytes */
#define PCI_USEDATA64	BIT_0		/* Use 64Bit Data bus ext */

/* PCI_OUR_STATUS	32 bit	Adapter Status Register (Yukon-2) */
#define PCI_OS_PCI64B	BIT_31		/* Conventional PCI 64 bits Bus */
#define PCI_OS_PCIX	BIT_30		/* PCI-X Bus */
#define PCI_OS_MODE_MSK	(3<<28)		/* Bit 29..28:	PCI-X Bus Mode Mask */
#define PCI_OS_PCI66M	BIT_27		/* PCI 66 MHz Bus */
#define PCI_OS_PCI_X	BIT_26		/* PCI/PCI-X Bus (0 = PEX) */
#define PCI_OS_DLLE_MSK	(3<<24)		/* Bit 25..24:	DLL Status Indication */
#define PCI_OS_DLLR_MSK	(0x0f<<20)	/* Bit 23..20:	DLL Row Counters Values */
#define PCI_OS_DLLC_MSK	(0x0f<<16)	/* Bit 19..16:	DLL Col. Counters Values */

#define PCI_OS_SPEED(val)	((val & PCI_OS_MODE_MSK) >> 28)	/* PCI-X Speed */
/* possible values for the speed field of the register */
#define PCI_OS_SPD_PCI		0	/* PCI Conventional Bus */
#define PCI_OS_SPD_X66		1	/* PCI-X 66MHz Bus */
#define PCI_OS_SPD_X100		2	/* PCI-X 100MHz Bus */
#define PCI_OS_SPD_X133		3	/* PCI-X 133MHz Bus */

/* PCI_OUR_REG_3	32 bit	Our Register 3 (Yukon-ECU only) */
#define	PCI_CLK_MACSEC_DIS	BIT_17	/* Disable Clock MACSec. */

/* PCI_OUR_REG_4	32 bit	Our Register 4 (Yukon-ECU only) */
#define	PCI_TIMER_VALUE_MSK	(0xff<<16)	/* Bit 23..16:	Timer Value Mask */
#define	PCI_FORCE_ASPM_REQUEST	BIT_15	/* Force ASPM Request (A1 only) */
#define	PCI_ASPM_GPHY_LINK_DOWN	BIT_14	/* GPHY Link Down (A1 only) */
#define	PCI_ASPM_INT_FIFO_EMPTY	BIT_13	/* Internal FIFO Empty (A1 only) */
#define	PCI_ASPM_CLKRUN_REQUEST	BIT_12	/* CLKRUN Request (A1 only) */
#define	PCI_ASPM_FORCE_CLKREQ_ENA	BIT_4	/* Force CLKREQ Enable (A1b only) */
#define	PCI_ASPM_CLKREQ_PAD_CTL	BIT_3	/* CLKREQ PAD Control (A1 only) */
#define	PCI_ASPM_A1_MODE_SELECT	BIT_2	/* A1 Mode Select (A1 only) */
#define	PCI_CLK_GATE_PEX_UNIT_ENA	BIT_1	/* Enable Gate PEX Unit Clock */
#define	PCI_CLK_GATE_ROOT_COR_ENA	BIT_0	/* Enable Gate Root Core Clock */

/* PCI_OUR_REG_5	32 bit	Our Register 5 (Yukon-ECU only) */
						/* Bit 31..27: for A3 & later */
#define	PCI_CTL_DIV_CORE_CLK_ENA	BIT_31	/* Divide Core Clock Enable */
#define	PCI_CTL_SRESET_VMAIN_AV		BIT_30	/* Soft Reset for Vmain_av De-Glitch */
#define	PCI_CTL_BYPASS_VMAIN_AV		BIT_29	/* Bypass En. for Vmain_av De-Glitch */
#define	PCI_CTL_TIM_VMAIN_AV1		BIT_28	/* Bit 28..27: Timer Vmain_av Mask */
#define	PCI_CTL_TIM_VMAIN_AV0		BIT_27	/* Bit 28..27: Timer Vmain_av Mask */
#define	PCI_CTL_TIM_VMAIN_AV_MSK	(BIT_28 | BIT_27)
					/* Bit 26..16: Release Clock on Event */
#define	PCI_REL_PCIE_RST_DE_ASS		BIT_26	/* PCIe Reset De-Asserted */
#define	PCI_REL_GPHY_REC_PACKET		BIT_25	/* GPHY Received Packet */
#define	PCI_REL_INT_FIFO_N_EMPTY	BIT_24	/* Internal FIFO Not Empty */
#define	PCI_REL_MAIN_PWR_AVAIL		BIT_23	/* Main Power Available */
#define	PCI_REL_CLKRUN_REQ_REL		BIT_22	/* CLKRUN Request Release */
#define	PCI_REL_PCIE_RESET_ASS		BIT_21	/* PCIe Reset Asserted */
#define	PCI_REL_PME_ASSERTED		BIT_20	/* PME Asserted */
#define	PCI_REL_PCIE_EXIT_L1_ST		BIT_19	/* PCIe Exit L1 State */
#define	PCI_REL_LOADER_NOT_FIN		BIT_18	/* EPROM Loader Not Finished */
#define	PCI_REL_PCIE_RX_EX_IDLE		BIT_17	/* PCIe Rx Exit Electrical Idle State */
#define	PCI_REL_GPHY_LINK_UP		BIT_16	/* GPHY Link Up */
					/* Bit 10.. 0: Mask for Gate Clock */
#define	PCI_GAT_PCIE_RST_ASSERTED	BIT_10	/* PCIe Reset Asserted */
#define	PCI_GAT_GPHY_N_REC_PACKET	BIT_9	/* GPHY Not Received Packet */
#define	PCI_GAT_INT_FIFO_EMPTY		BIT_8	/* Internal FIFO Empty */
#define	PCI_GAT_MAIN_PWR_N_AVAIL	BIT_7	/* Main Power Not Available */
#define	PCI_GAT_CLKRUN_REQ_REL		BIT_6	/* CLKRUN Not Requested */
#define	PCI_GAT_PCIE_RESET_ASS		BIT_5	/* PCIe Reset Asserted */
#define	PCI_GAT_PME_DE_ASSERTED		BIT_4	/* PME De-Asserted */
#define	PCI_GAT_PCIE_ENTER_L1_ST	BIT_3	/* PCIe Enter L1 State */
#define	PCI_GAT_LOADER_FINISHED		BIT_2	/* EPROM Loader Finished */
#define	PCI_GAT_PCIE_RX_EL_IDLE		BIT_1	/* PCIe Rx Electrical Idle State */
#define	PCI_GAT_GPHY_LINK_DOWN		BIT_0	/* GPHY Link Down */

/* PCI_CFG_REG_1	32 bit	Config Register 1 */
#define	PCI_CF1_DIS_REL_EVT_RST		BIT_24	/* Dis. Rel. Event during PCIE reset */
						/* Bit 23..21: Release Clock on Event */
#define	PCI_CF1_REL_LDR_NOT_FIN		BIT_23	/* EEPROM Loader Not Finished */
#define	PCI_CF1_REL_VMAIN_AVLBL		BIT_22	/* Vmain available */
#define	PCI_CF1_REL_PCIE_RESET		BIT_21	/* PCI-E reset */
						/* Bit 20..18: Gate Clock on Event */
#define	PCI_CF1_GAT_LDR_NOT_FIN		BIT_20	/* EEPROM Loader Finished */
#define	PCI_CF1_GAT_PCIE_RX_IDLE	BIT_19	/* PCI-E Rx Electrical idle */
#define	PCI_CF1_GAT_PCIE_RESET		BIT_18	/* PCI-E Reset */
#define	PCI_CF1_PRST_PHY_CLKREQ		BIT_17	/* Enable PCI-E rst & PM2PHY gen. CLKREQ */
#define	PCI_CF1_PCIE_RST_CLKREQ		BIT_16	/* Enable PCI-E rst generate CLKREQ */

#define	PCI_CF1_ENA_CFG_LDR_DONE	BIT_8	/* Enable core level Config loader done */
#define	PCI_CF1_ENA_TXBMU_RD_IDLE	BIT_1	/* Enable TX BMU Read  IDLE for ASPM */
#define	PCI_CF1_ENA_TXBMU_WR_IDLE	BIT_0	/* Enable TX BMU Write IDLE for ASPM */

/* PEX_DEV_CTRL	16 bit	PEX Device Control (Yukon-2) */
#define PEX_DC_MAX_RRS_MSK	(7<<12)	/* Bit 14..12:	Max. Read Request Size */
#define PEX_DC_EN_NO_SNOOP	BIT_11	/* Enable No Snoop */
#define PEX_DC_EN_AUX_POW	BIT_10	/* Enable AUX Power */
#define PEX_DC_EN_PHANTOM	BIT_9	/* Enable Phantom Functions */
#define PEX_DC_EN_EXT_TAG	BIT_8	/* Enable Extended Tag Field */
#define PEX_DC_MAX_PLS_MSK	(7<<5)	/* Bit  7.. 5:	Max. Payload Size Mask */
#define PEX_DC_EN_REL_ORD	BIT_4	/* Enable Relaxed Ordering */
#define PEX_DC_EN_UNS_RQ_RP	BIT_3	/* Enable Unsupported Request Reporting */
#define PEX_DC_EN_FAT_ER_RP	BIT_2	/* Enable Fatal Error Reporting */
#define PEX_DC_EN_NFA_ER_RP	BIT_1	/* Enable Non-Fatal Error Reporting */
#define PEX_DC_EN_COR_ER_RP	BIT_0	/* Enable Correctable Error Reporting */

#define PEX_DC_MAX_RD_RQ_SIZE(x)	(SHIFT12(x) & PEX_DC_MAX_RRS_MSK)

/* PEX_LNK_STAT	16 bit	PEX Link Status (Yukon-2) */
#define PEX_LS_SLOT_CLK_CFG	BIT_12	/* Slot Clock Config */
#define PEX_LS_LINK_TRAIN	BIT_11	/* Link Training */
#define PEX_LS_TRAIN_ERROR	BIT_10	/* Training Error */
#define PEX_LS_LINK_WI_MSK	(0x3f<<4) /* Bit  9.. 4: Neg. Link Width Mask */
#define PEX_LS_LINK_SP_MSK	0x0f	/* Bit  3.. 0:	Link Speed Mask */

/* PEX_UNC_ERR_STAT PEX Uncorrectable Errors Status Register (Yukon-2) */
#define PEX_UNSUP_REQ 	BIT_20		/* Unsupported Request Error */
#define PEX_MALFOR_TLP	BIT_18		/* Malformed TLP */
#define	PEX_RX_OV	BIT_17		/* Receiver Overflow (not supported) */
#define PEX_UNEXP_COMP	BIT_16		/* Unexpected Completion */
#define PEX_COMP_TO	BIT_14		/* Completion Timeout */
#define PEX_FLOW_CTRL_P	BIT_13		/* Flow Control Protocol Error */
#define PEX_POIS_TLP	BIT_12		/* Poisoned TLP */
#define PEX_DATA_LINK_P BIT_4		/* Data Link Protocol Error */

#define PEX_FATAL_ERRORS	(PEX_MALFOR_TLP | PEX_FLOW_CTRL_P | PEX_DATA_LINK_P)

/*	Control Register File (Address Map) */

/*
 *	Bank 0
 */
#define B0_RAP		0x0000	/*  8 bit Register Address Port */
#define B0_CTST		0x0004	/* 16 bit Control/Status register */
#define B0_LED		0x0006	/*  8 Bit LED register */
#define B0_POWER_CTRL	0x0007	/*  8 Bit Power Control reg (YUKON only) */
#define B0_ISRC		0x0008	/* 32 bit Interrupt Source Register */
#define B0_IMSK		0x000c	/* 32 bit Interrupt Mask Register */
#define B0_HWE_ISRC	0x0010	/* 32 bit HW Error Interrupt Src Reg */
#define B0_HWE_IMSK	0x0014	/* 32 bit HW Error Interrupt Mask Reg */
#define B0_SP_ISRC	0x0018	/* 32 bit Special Interrupt Source Reg 1 */

/* Special ISR registers (Yukon-2 only) */
#define B0_Y2_SP_ISRC2	0x001c	/* 32 bit Special Interrupt Source Reg 2 */
#define B0_Y2_SP_ISRC3	0x0020	/* 32 bit Special Interrupt Source Reg 3 */
#define B0_Y2_SP_EISR	0x0024	/* 32 bit Enter ISR Reg */
#define B0_Y2_SP_LISR	0x0028	/* 32 bit Leave ISR Reg */
#define B0_Y2_SP_ICR	0x002c	/* 32 bit Interrupt Control Reg */

/*
 *	Bank 1
 *	- completely empty (this is the RAP Block window)
 *	Note: if RAP = 1 this page is reserved
 */

/*
 *	Bank 2
 */
/* NA reg = 48 bit Network Address Register, 3x16 or 8x8 bit readable */
#define B2_MAC_1	0x0100	/* NA reg MAC Address 1 */
#define B2_MAC_2	0x0108	/* NA reg MAC Address 2 */
#define B2_MAC_3	0x0110	/* NA reg MAC Address 3 */
#define B2_CONN_TYP	0x0118	/*  8 bit Connector type */
#define B2_PMD_TYP	0x0119	/*  8 bit PMD type */
#define B2_MAC_CFG	0x011a	/*  8 bit MAC Configuration / Chip Revision */
#define B2_CHIP_ID	0x011b	/*  8 bit Chip Identification Number */
#define B2_E_0		0x011c	/*  8 bit EPROM Byte 0 (ext. SRAM size */
#define B2_Y2_CLK_GATE	0x011d	/*  8 bit Clock Gating (Yukon-2) */
#define B2_Y2_HW_RES	0x011e	/*  8 bit HW Resources (Yukon-2) */
#define B2_E_3		0x011f	/*  8 bit EPROM Byte 3 */
#define B2_Y2_CLK_CTRL	0x0120	/* 32 bit Core Clock Frequency Control */
#define B2_TI_INI	0x0130	/* 32 bit Timer Init Value */
#define B2_TI_VAL	0x0134	/* 32 bit Timer Value */
#define B2_TI_CTRL	0x0138	/*  8 bit Timer Control */
#define B2_TI_TEST	0x0139	/*  8 Bit Timer Test */
#define B2_IRQM_INI	0x0140	/* 32 bit IRQ Moderation Timer Init Reg.*/
#define B2_IRQM_VAL	0x0144	/* 32 bit IRQ Moderation Timer Value */
#define B2_IRQM_CTRL	0x0148	/*  8 bit IRQ Moderation Timer Control */
#define B2_IRQM_TEST	0x0149	/*  8 bit IRQ Moderation Timer Test */
#define B2_IRQM_MSK 	0x014c	/* 32 bit IRQ Moderation Mask */
#define B2_IRQM_HWE_MSK 0x0150	/* 32 bit IRQ Moderation HW Error Mask */
#define B2_TST_CTRL1	0x0158	/*  8 bit Test Control Register 1 */
#define B2_TST_CTRL2	0x0159	/*  8 bit Test Control Register 2 */
#define B2_GP_IO	0x015c	/* 32 bit General Purpose I/O Register */
#define B2_I2C_CTRL	0x0160	/* 32 bit I2C HW Control Register */
#define B2_I2C_DATA	0x0164	/* 32 bit I2C HW Data Register */
#define B2_I2C_IRQ	0x0168	/* 32 bit I2C HW IRQ Register */
#define B2_I2C_SW	0x016c	/* 32 bit I2C SW Port Register */

#define Y2_PEX_PHY_DATA	0x0170	/* 16 bit PEX PHY Data Register */
#define Y2_PEX_PHY_ADDR	0x0172	/* 16 bit PEX PHY Address Register */

/*
 *	Bank 3
 */
/* RAM Random Registers */
#define B3_RAM_ADDR	0x0180	/* 32 bit RAM Address, to read or write */
#define B3_RAM_DATA_LO	0x0184	/* 32 bit RAM Data Word (low dWord) */
#define B3_RAM_DATA_HI	0x0188	/* 32 bit RAM Data Word (high dWord) */

#define SELECT_RAM_BUFFER(rb, addr) (addr | (rb << 6))	/* Yukon-2 only */

/* RAM Interface Registers */
/* Yukon-2: use SELECT_RAM_BUFFER() to access the RAM buffer */
/*
 * The HW-Spec. calls this registers Timeout Value 0..11. But this names are
 * not usable in SW. Please notice these are NOT real timeouts, these are
 * the number of qWords transferred continuously.
 */
#define B3_RI_WTO_R1	0x0190	/*  8 bit WR Timeout Queue R1 (TO0) */
#define B3_RI_WTO_XA1	0x0191	/*  8 bit WR Timeout Queue XA1 (TO1) */
#define B3_RI_WTO_XS1	0x0192	/*  8 bit WR Timeout Queue XS1 (TO2) */
#define B3_RI_RTO_R1	0x0193	/*  8 bit RD Timeout Queue R1 (TO3) */
#define B3_RI_RTO_XA1	0x0194	/*  8 bit RD Timeout Queue XA1 (TO4) */
#define B3_RI_RTO_XS1	0x0195	/*  8 bit RD Timeout Queue XS1 (TO5) */
#define B3_RI_WTO_R2	0x0196	/*  8 bit WR Timeout Queue R2 (TO6) */
#define B3_RI_WTO_XA2	0x0197	/*  8 bit WR Timeout Queue XA2 (TO7) */
#define B3_RI_WTO_XS2	0x0198	/*  8 bit WR Timeout Queue XS2 (TO8) */
#define B3_RI_RTO_R2	0x0199	/*  8 bit RD Timeout Queue R2 (TO9) */
#define B3_RI_RTO_XA2	0x019a	/*  8 bit RD Timeout Queue XA2 (TO10)*/
#define B3_RI_RTO_XS2	0x019b	/*  8 bit RD Timeout Queue XS2 (TO11)*/
#define B3_RI_TO_VAL	0x019c	/*  8 bit Current Timeout Count Val */
#define B3_RI_CTRL	0x01a0	/* 16 bit RAM Interface Control Register */
#define B3_RI_TEST	0x01a2	/*  8 bit RAM Interface Test Register */

/*
 *	Bank 4 - 5
 */
/* Transmit Arbiter Registers MAC 1 and 2, use MR_ADDR() to access */
#define TXA_ITI_INI	0x0200	/* 32 bit Tx Arb Interval Timer Init Val*/
#define TXA_ITI_VAL	0x0204	/* 32 bit Tx Arb Interval Timer Value */
#define TXA_LIM_INI	0x0208	/* 32 bit Tx Arb Limit Counter Init Val */
#define TXA_LIM_VAL	0x020c	/* 32 bit Tx Arb Limit Counter Value */
#define TXA_CTRL	0x0210	/*  8 bit Tx Arbiter Control Register */
#define TXA_TEST	0x0211	/*  8 bit Tx Arbiter Test Register */
#define TXA_STAT	0x0212	/*  8 bit Tx Arbiter Status Register */

#define MR_ADDR(Mac, Offs)	(((Mac) << 7) + (Offs))

/* RSS key registers for Yukon-2 Family */
#define B4_RSS_KEY	0x0220	/* 4x32 bit RSS Key register (Yukon-2) */
/* RSS key register offsets */
#define KEY_IDX_0	 0		/* offset for location of KEY 0 */
#define KEY_IDX_1	 4		/* offset for location of KEY 1 */
#define KEY_IDX_2	 8		/* offset for location of KEY 2 */
#define KEY_IDX_3	12		/* offset for location of KEY 3 */
	/* 0x0280 - 0x0292:	MAC 2 */
#define RSS_KEY_ADDR(Port, KeyIndex)	\
		((B4_RSS_KEY | ( ((Port) == 0) ? 0 : 0x80)) + (KeyIndex))

/*
 *	Bank 8 - 15
 */
/* Receive and Transmit Queue Registers, use Q_ADDR() to access */
#define B8_Q_REGS	0x0400

/* Queue Register Offsets, use Q_ADDR() to access */
#define Q_D	0x00	/* 8*32	bit Current Descriptor */
#define Q_DA_L	0x20	/* 32 bit Current Descriptor Address Low dWord */
#define Q_DONE	0x24	/* 16 bit Done Index */
#define Q_AC_L	0x28	/* 32 bit Current Address Counter Low dWord */
#define Q_AC_H	0x2c	/* 32 bit Current Address Counter High dWord */
#define Q_BC	0x30	/* 32 bit Current Byte Counter */
#define Q_CSR	0x34	/* 32 bit BMU Control/Status Register */
#define Q_F	0x38	/* 32 bit Flag Register */
#define Q_T1	0x3c	/* 32 bit Test Register 1 */
#define Q_T1_TR	0x3c	/*  8 bit Test Register 1 Transfer SM */
#define Q_T1_WR	0x3d	/*  8 bit Test Register 1 Write Descriptor SM */
#define Q_T1_RD	0x3e	/*  8 bit Test Register 1 Read Descriptor SM */
#define Q_T1_SV	0x3f	/*  8 bit Test Register 1 Supervisor SM */
#define Q_WM	0x40	/* 16 bit FIFO Watermark */
#define Q_AL	0x42	/*  8 bit FIFO Alignment */
#define Q_RSP	0x44	/* 16 bit FIFO Read Shadow Pointer */
#define Q_RSL	0x46	/*  8 bit FIFO Read Shadow Level */
#define Q_RP	0x48	/*  8 bit FIFO Read Pointer */
#define Q_RL	0x4a	/*  8 bit FIFO Read Level */
#define Q_WP	0x4c	/*  8 bit FIFO Write Pointer */
#define Q_WSP	0x4d	/*  8 bit FIFO Write Shadow Pointer */
#define Q_WL	0x4e	/*  8 bit FIFO Write Level */
#define Q_WSL	0x4f	/*  8 bit FIFO Write Shadow Level */

#define Q_ADDR(Queue, Offs)	(B8_Q_REGS + (Queue) + (Offs))

/* Queue Prefetch Unit Offsets, use Y2_PREF_Q_ADDR() to address */
#define Y2_B8_PREF_REGS		0x0450

#define PREF_UNIT_CTRL_REG	0x00	/* 32 bit Prefetch Control register */
#define PREF_UNIT_LAST_IDX_REG	0x04	/* 16 bit Last Index */
#define PREF_UNIT_ADDR_LOW_REG	0x08	/* 32 bit List start addr, low part */
#define PREF_UNIT_ADDR_HI_REG	0x0c	/* 32 bit List start addr, high part*/
#define PREF_UNIT_GET_IDX_REG	0x10	/* 16 bit Get Index */
#define PREF_UNIT_PUT_IDX_REG	0x14	/* 16 bit Put Index */
#define PREF_UNIT_FIFO_WP_REG	0x20	/*  8 bit FIFO write pointer */
#define PREF_UNIT_FIFO_RP_REG	0x24	/*  8 bit FIFO read pointer */
#define PREF_UNIT_FIFO_WM_REG	0x28	/*  8 bit FIFO watermark */
#define PREF_UNIT_FIFO_LEV_REG	0x2c	/*  8 bit FIFO level */

#define PREF_UNIT_MASK_IDX	0x0fff

#define Y2_PREF_Q_ADDR(Queue, Offs)	(Y2_B8_PREF_REGS + (Queue) + (Offs))

/*
 *	Bank 16 - 23
 */
/* RAM Buffer Registers */
#define B16_RAM_REGS	0x0800

/* RAM Buffer Register Offsets, use RB_ADDR() to access */
#define RB_START	0x00	/* 32 bit RAM Buffer Start Address */
#define RB_END		0x04	/* 32 bit RAM Buffer End Address */
#define RB_WP		0x08	/* 32 bit RAM Buffer Write Pointer */
#define RB_RP		0x0c	/* 32 bit RAM Buffer Read Pointer */
#define RB_RX_UTPP	0x10	/* 32 bit Rx Upper Threshold, Pause Packet */
#define RB_RX_LTPP	0x14	/* 32 bit Rx Lower Threshold, Pause Packet */
#define RB_RX_UTHP	0x18	/* 32 bit Rx Upper Threshold, High Prio */
#define RB_RX_LTHP	0x1c	/* 32 bit Rx Lower Threshold, High Prio */
#define RB_PC		0x20	/* 32 bit RAM Buffer Packet Counter */
#define RB_LEV		0x24	/* 32 bit RAM Buffer Level Register */
#define RB_CTRL		0x28	/*  8 bit RAM Buffer Control Register */
#define RB_TST1		0x29	/*  8 bit RAM Buffer Test Register 1 */
#define RB_TST2		0x2a	/*  8 bit RAM Buffer Test Register 2 */

/*
 *	Bank 24
 */
/* Receive GMAC FIFO (YUKON and Yukon-2), use MR_ADDR() to access */
#define RX_GMF_EA	0x0c40	/* 32 bit Rx GMAC FIFO End Address */
#define RX_GMF_AF_THR	0x0c44	/* 32 bit Rx GMAC FIFO Almost Full Thresh. */
#define RX_GMF_CTRL_T	0x0c48	/* 32 bit Rx GMAC FIFO Control/Test */
#define RX_GMF_FL_MSK	0x0c4c	/* 32 bit Rx GMAC FIFO Flush Mask */
#define RX_GMF_FL_THR	0x0c50	/* 32 bit Rx GMAC FIFO Flush Threshold */
#define RX_GMF_TR_THR	0x0c54	/* 32 bit Rx Truncation Threshold (Yukon-2) */
#define	RX_GMF_UP_THR	0x0c58	/* 16 bit Rx Upper Pause Thr (Yukon-EC_U) */
#define	RX_GMF_LP_THR	0x0c5a	/* 16 bit Rx Lower Pause Thr (Yukon-EC_U) */
#define RX_GMF_VLAN	0x0c5c	/* 32 bit Rx VLAN Type Register (Yukon-2) */
#define RX_GMF_WP	0x0c60	/* 32 bit Rx GMAC FIFO Write Pointer */
#define RX_GMF_WLEV	0x0c68	/* 32 bit Rx GMAC FIFO Write Level */
#define RX_GMF_RP	0x0c70	/* 32 bit Rx GMAC FIFO Read Pointer */
#define RX_GMF_RLEV	0x0c78	/* 32 bit Rx GMAC FIFO Read Level */

/*
 *	Bank 25
 */
	/* 0x0c80 - 0x0cbf:	MAC 2 */
	/* 0x0cc0 - 0x0cff:	reserved */

/*
 *	Bank 26
 */
/* Transmit GMAC FIFO (YUKON and Yukon-2), use MR_ADDR() to access */
#define TX_GMF_EA	0x0d40	/* 32 bit Tx GMAC FIFO End Address */
#define TX_GMF_AE_THR	0x0d44	/* 32 bit Tx GMAC FIFO Almost Empty Thresh.*/
#define TX_GMF_CTRL_T	0x0d48	/* 32 bit Tx GMAC FIFO Control/Test */
#define TX_GMF_VLAN	0x0d5c	/* 32 bit Tx VLAN Type Register (Yukon-2) */
#define TX_GMF_WP	0x0d60	/* 32 bit Tx GMAC FIFO Write Pointer */
#define TX_GMF_WSP	0x0d64	/* 32 bit Tx GMAC FIFO Write Shadow Pointer */
#define TX_GMF_WLEV	0x0d68	/* 32 bit Tx GMAC FIFO Write Level */
#define TX_GMF_RP	0x0d70	/* 32 bit Tx GMAC FIFO Read Pointer */
#define TX_GMF_RSTP	0x0d74	/* 32 bit Tx GMAC FIFO Restart Pointer */
#define TX_GMF_RLEV	0x0d78	/* 32 bit Tx GMAC FIFO Read Level */

/*
 *	Bank 27
 */
	/* 0x0d80 - 0x0dbf:	MAC 2 */
	/* 0x0daa - 0x0dff:	reserved */

/*
 *	Bank 28
 */
/* Descriptor Poll Timer Registers */
#define B28_DPT_INI	0x0e00	/* 24 bit Descriptor Poll Timer Init Val */
#define B28_DPT_VAL	0x0e04	/* 24 bit Descriptor Poll Timer Curr Val */
#define B28_DPT_CTRL	0x0e08	/*  8 bit Descriptor Poll Timer Ctrl Reg */
#define B28_DPT_TST	0x0e0a	/*  8 bit Descriptor Poll Timer Test Reg */
/* Time Stamp Timer Registers (YUKON only) */
#define GMAC_TI_ST_VAL	0x0e14	/* 32 bit Time Stamp Timer Curr Val */
#define GMAC_TI_ST_CTRL	0x0e18	/*  8 bit Time Stamp Timer Ctrl Reg */
#define GMAC_TI_ST_TST	0x0e1a	/*  8 bit Time Stamp Timer Test Reg */
/* Polling Unit Registers (Yukon-2 only) */
#define POLL_CTRL	0x0e20	/* 32 bit Polling Unit Control Reg */
#define POLL_LAST_IDX	0x0e24	/* 16 bit Polling Unit List Last Index */
#define POLL_LIST_ADDR_LO	0x0e28	/* 32 bit Poll. List Start Addr (low) */
#define POLL_LIST_ADDR_HI	0x0e2c	/* 32 bit Poll. List Start Addr (high) */
/* ASF Subsystem Registers (Yukon-2 only) */
#define B28_Y2_SMB_CONFIG	0x0e40	/* 32 bit ASF SMBus Config Register */
#define B28_Y2_SMB_CSD_REG	0x0e44	/* 32 bit ASF SMB Control/Status/Data */
#define B28_Y2_CPU_WDOG		0x0e48	/* 32 bit Watchdog Register */
#define B28_Y2_ASF_IRQ_V_BASE	0x0e60	/* 32 bit ASF IRQ Vector Base */
#define B28_Y2_ASF_STAT_CMD	0x0e68	/* 32 bit ASF Status and Command Reg */
#define B28_Y2_ASF_HCU_CCSR	0x0e68	/* 32 bit ASF HCU CCSR (Yukon EX) */
#define B28_Y2_ASF_HOST_COM	0x0e6c	/* 32 bit ASF Host Communication Reg */
#define B28_Y2_DATA_REG_1	0x0e70	/* 32 bit ASF/Host Data Register 1 */
#define B28_Y2_DATA_REG_2	0x0e74	/* 32 bit ASF/Host Data Register 2 */
#define B28_Y2_DATA_REG_3	0x0e78	/* 32 bit ASF/Host Data Register 3 */
#define B28_Y2_DATA_REG_4	0x0e7c	/* 32 bit ASF/Host Data Register 4 */

/*
 *	Bank 29
 */

/* Status BMU Registers (Yukon-2 only)*/
#define STAT_CTRL		0x0e80	/* 32 bit Status BMU Control Reg */
#define STAT_LAST_IDX		0x0e84	/* 16 bit Status BMU Last Index */
#define STAT_LIST_ADDR_LO	0x0e88	/* 32 bit Status List Start Addr (low) */
#define STAT_LIST_ADDR_HI	0x0e8c	/* 32 bit Status List Start Addr (high) */
#define STAT_TXA1_RIDX		0x0e90	/* 16 bit Status TxA1 Report Index Reg */
#define STAT_TXS1_RIDX		0x0e92	/* 16 bit Status TxS1 Report Index Reg */
#define STAT_TXA2_RIDX		0x0e94	/* 16 bit Status TxA2 Report Index Reg */
#define STAT_TXS2_RIDX		0x0e96	/* 16 bit Status TxS2 Report Index Reg */
#define STAT_TX_IDX_TH		0x0e98	/* 16 bit Status Tx Index Threshold Reg */
#define STAT_PUT_IDX		0x0e9c	/* 16 bit Status Put Index Reg */
/* FIFO Control/Status Registers (Yukon-2 only)*/
#define STAT_FIFO_WP		0x0ea0	/*  8 bit Status FIFO Write Pointer Reg */
#define STAT_FIFO_RP		0x0ea4	/*  8 bit Status FIFO Read Pointer Reg */
#define STAT_FIFO_RSP		0x0ea6	/*  8 bit Status FIFO Read Shadow Ptr */
#define STAT_FIFO_LEVEL		0x0ea8	/*  8 bit Status FIFO Level Reg */
#define STAT_FIFO_SHLVL		0x0eaa	/*  8 bit Status FIFO Shadow Level Reg */
#define STAT_FIFO_WM		0x0eac	/*  8 bit Status FIFO Watermark Reg */
#define STAT_FIFO_ISR_WM	0x0ead	/*  8 bit Status FIFO ISR Watermark Reg */
/* Level and ISR Timer Registers (Yukon-2 only)*/
#define STAT_LEV_TIMER_INI	0x0eb0	/* 32 bit Level Timer Init. Value Reg */
#define STAT_LEV_TIMER_CNT	0x0eb4	/* 32 bit Level Timer Counter Reg */
#define STAT_LEV_TIMER_CTRL	0x0eb8	/*  8 bit Level Timer Control Reg */
#define STAT_LEV_TIMER_TEST	0x0eb9	/*  8 bit Level Timer Test Reg */
#define STAT_TX_TIMER_INI	0x0ec0	/* 32 bit Tx Timer Init. Value Reg */
#define STAT_TX_TIMER_CNT	0x0ec4	/* 32 bit Tx Timer Counter Reg */
#define STAT_TX_TIMER_CTRL	0x0ec8	/*  8 bit Tx Timer Control Reg */
#define STAT_TX_TIMER_TEST	0x0ec9	/*  8 bit Tx Timer Test Reg */
#define STAT_ISR_TIMER_INI	0x0ed0	/* 32 bit ISR Timer Init. Value Reg */
#define STAT_ISR_TIMER_CNT	0x0ed4	/* 32 bit ISR Timer Counter Reg */
#define STAT_ISR_TIMER_CTRL	0x0ed8	/*  8 bit ISR Timer Control Reg */
#define STAT_ISR_TIMER_TEST	0x0ed9	/*  8 bit ISR Timer Test Reg */

#define	ST_LAST_IDX_MASK	0x007f	/* Last Index Mask */
#define	ST_TXRP_IDX_MASK	0x0fff	/* Tx Report Index Mask */
#define	ST_TXTH_IDX_MASK	0x0fff	/* Tx Threshold Index Mask */
#define	ST_WM_IDX_MASK		0x3f	/* FIFO Watermark Index Mask */

/*
 *	Bank 30
 */
/* GMAC and GPHY Control Registers (YUKON only) */
#define GMAC_CTRL	0x0f00	/* 32 bit GMAC Control Reg */
#define GPHY_CTRL	0x0f04	/* 32 bit GPHY Control Reg */
#define GMAC_IRQ_SRC	0x0f08	/*  8 bit GMAC Interrupt Source Reg */
#define GMAC_IRQ_MSK	0x0f0c	/*  8 bit GMAC Interrupt Mask Reg */
#define GMAC_LINK_CTRL	0x0f10	/* 16 bit Link Control Reg */

/* Wake-up Frame Pattern Match Control Registers (YUKON only) */

#define WOL_REG_OFFS	0x20	/* HW-Bug: Address is + 0x20 against spec. */

#define WOL_CTRL_STAT	0x0f20	/* 16 bit WOL Control/Status Reg */
#define WOL_MATCH_CTL	0x0f22	/*  8 bit WOL Match Control Reg */
#define WOL_MATCH_RES	0x0f23	/*  8 bit WOL Match Result Reg */
#define WOL_MAC_ADDR_LO	0x0f24	/* 32 bit WOL MAC Address Low */
#define WOL_MAC_ADDR_HI	0x0f28	/* 16 bit WOL MAC Address High */
#define WOL_PATT_PME	0x0f2a	/*  8 bit WOL PME Match Enable (Yukon-2) */
#define WOL_PATT_ASFM	0x0f2b	/*  8 bit WOL ASF Match Enable (Yukon-2) */
#define WOL_PATT_RPTR	0x0f2c	/*  8 bit WOL Pattern Read Pointer */

/* WOL Pattern Length Registers (YUKON only) */

#define WOL_PATT_LEN_LO	0x0f30	/* 32 bit WOL Pattern Length 3..0 */
#define WOL_PATT_LEN_HI	0x0f34	/* 24 bit WOL Pattern Length 6..4 */

/* WOL Pattern Counter Registers (YUKON only) */

#define WOL_PATT_CNT_0	0x0f38	/* 32 bit WOL Pattern Counter 3..0 */
#define WOL_PATT_CNT_4	0x0f3c	/* 24 bit WOL Pattern Counter 6..4 */

/*
 *	Bank 32	- 33
 */
#define WOL_PATT_RAM_1	0x1000	/*  WOL Pattern RAM Link 1 */
#define WOL_PATT_RAM_2	0x1400	/*  WOL Pattern RAM Link 2 */

/* offset to configuration space on Yukon-2 */
#define Y2_CFG_SPC 	0x1c00
#define BASE_GMAC_1	0x2800	/* GMAC 1 registers */
#define BASE_GMAC_2	0x3800	/* GMAC 2 registers */

/*
 *	Control Register Bit Definitions:
 */
/*	B0_CTST	24 bit	Control/Status register */
#define Y2_VMAIN_AVAIL	BIT_17	/* VMAIN available (YUKON-2 only) */
#define Y2_VAUX_AVAIL	BIT_16	/* VAUX available (YUKON-2 only) */
#define	Y2_HW_WOL_ON	BIT_15	/* HW WOL On  (Yukon-EC Ultra A1 only) */
#define	Y2_HW_WOL_OFF	BIT_14	/* HW WOL Off (Yukon-EC Ultra A1 only) */
#define Y2_ASF_ENABLE	BIT_13	/* ASF Unit Enable (YUKON-2 only) */
#define Y2_ASF_DISABLE	BIT_12	/* ASF Unit Disable (YUKON-2 only) */
#define Y2_CLK_RUN_ENA	BIT_11	/* CLK_RUN Enable  (YUKON-2 only) */
#define Y2_CLK_RUN_DIS	BIT_10	/* CLK_RUN Disable (YUKON-2 only) */
#define Y2_LED_STAT_ON	BIT_9	/* Status LED On  (YUKON-2 only) */
#define Y2_LED_STAT_OFF	BIT_8	/* Status LED Off (YUKON-2 only) */
#define CS_ST_SW_IRQ	BIT_7	/* Set IRQ SW Request */
#define CS_CL_SW_IRQ	BIT_6	/* Clear IRQ SW Request */
#define CS_STOP_DONE	BIT_5	/* Stop Master is finished */
#define CS_STOP_MAST	BIT_4	/* Command Bit to stop the master */
#define CS_MRST_CLR	BIT_3	/* Clear Master Reset */
#define CS_MRST_SET	BIT_2	/* Set   Master Reset */
#define CS_RST_CLR	BIT_1	/* Clear Software Reset	*/
#define CS_RST_SET	BIT_0	/* Set   Software Reset	*/

#define LED_STAT_ON	BIT_1	/* Status LED On	*/
#define LED_STAT_OFF	BIT_0	/* Status LED Off	*/

/* B0_POWER_CTRL	 8 Bit	Power Control reg (YUKON only) */
#define PC_VAUX_ENA	BIT_7	/* Switch VAUX Enable  */
#define PC_VAUX_DIS	BIT_6	/* Switch VAUX Disable */
#define PC_VCC_ENA	BIT_5	/* Switch VCC Enable  */
#define PC_VCC_DIS	BIT_4	/* Switch VCC Disable */
#define PC_VAUX_ON	BIT_3	/* Switch VAUX On  */
#define PC_VAUX_OFF	BIT_2	/* Switch VAUX Off */
#define PC_VCC_ON	BIT_1	/* Switch VCC On  */
#define PC_VCC_OFF	BIT_0	/* Switch VCC Off */

/*	B0_ISRC		32 bit	Interrupt Source Register */
/*	B0_IMSK		32 bit	Interrupt Mask Register */
/*	B0_SP_ISRC	32 bit	Special Interrupt Source Reg */
/*	B2_IRQM_MSK	32 bit	IRQ Moderation Mask */
/*	B0_Y2_SP_ISRC2	32 bit	Special Interrupt Source Reg 2 */
/*	B0_Y2_SP_ISRC3	32 bit	Special Interrupt Source Reg 3 */
/*	B0_Y2_SP_EISR	32 bit	Enter ISR Reg */
/*	B0_Y2_SP_LISR	32 bit	Leave ISR Reg */
#define Y2_IS_PORT_MASK(Port, Mask)	((Mask) << (Port*8))
#define Y2_IS_HW_ERR	BIT_31	/* Interrupt HW Error */
#define Y2_IS_STAT_BMU	BIT_30	/* Status BMU Interrupt */
#define Y2_IS_ASF	BIT_29	/* ASF subsystem Interrupt */
#define Y2_IS_POLL_CHK	BIT_27	/* Check IRQ from polling unit */
#define Y2_IS_TWSI_RDY	BIT_26	/* IRQ on end of TWSI Tx */
#define Y2_IS_IRQ_SW	BIT_25	/* SW forced IRQ	*/
#define Y2_IS_TIMINT	BIT_24	/* IRQ from Timer	*/
#define Y2_IS_IRQ_PHY2	BIT_12	/* Interrupt from PHY 2 */
#define Y2_IS_IRQ_MAC2	BIT_11	/* Interrupt from MAC 2 */
#define Y2_IS_CHK_RX2	BIT_10	/* Descriptor error Rx 2 */
#define Y2_IS_CHK_TXS2	BIT_9	/* Descriptor error TXS 2 */
#define Y2_IS_CHK_TXA2	BIT_8	/* Descriptor error TXA 2 */
#define Y2_IS_PSM_ACK	BIT_7	/* PSM Ack (Yukon Optima) */
#define Y2_IS_PTP_TIST	BIT_6	/* PTP TIme Stamp (Yukon Optima) */
#define Y2_IS_PHY_QLNK	BIT_5	/* PHY Quick Link (Yukon Optima) */
#define Y2_IS_IRQ_PHY1	BIT_4	/* Interrupt from PHY 1 */
#define Y2_IS_IRQ_MAC1	BIT_3	/* Interrupt from MAC 1 */
#define Y2_IS_CHK_RX1	BIT_2	/* Descriptor error Rx 1 */
#define Y2_IS_CHK_TXS1	BIT_1	/* Descriptor error TXS 1 */
#define Y2_IS_CHK_TXA1	BIT_0	/* Descriptor error TXA 1 */

#define Y2_IS_L1_MASK	0x0000001f	/* IRQ Mask for port 1 */

#define Y2_IS_L2_MASK	0x00001f00	/* IRQ Mask for port 2 */

#define Y2_IS_ALL_MSK	0xef001f1f	/* All Interrupt bits */

#define	Y2_IS_PORT_A	\
	(Y2_IS_IRQ_PHY1 | Y2_IS_IRQ_MAC1 | Y2_IS_CHK_TXA1 | Y2_IS_CHK_RX1)
#define	Y2_IS_PORT_B	\
	(Y2_IS_IRQ_PHY2 | Y2_IS_IRQ_MAC2 | Y2_IS_CHK_TXA2 | Y2_IS_CHK_RX2)

/*	B0_HWE_ISRC	32 bit	HW Error Interrupt Src Reg */
/*	B0_HWE_IMSK	32 bit	HW Error Interrupt Mask Reg */
/*	B2_IRQM_HWE_MSK	32 bit	IRQ Moderation HW Error Mask */
#define Y2_IS_TIST_OV	BIT_29	/* Time Stamp Timer overflow interrupt */
#define Y2_IS_SENSOR	BIT_28	/* Sensor interrupt */
#define Y2_IS_MST_ERR	BIT_27	/* Master error interrupt */
#define Y2_IS_IRQ_STAT	BIT_26	/* Status exception interrupt */
#define Y2_IS_PCI_EXP	BIT_25	/* PCI-Express interrupt */
#define Y2_IS_PCI_NEXP	BIT_24	/* PCI-Express error similar to PCI error */
#define Y2_IS_PAR_RD2	BIT_13	/* Read RAM parity error interrupt */
#define Y2_IS_PAR_WR2	BIT_12	/* Write RAM parity error interrupt */
#define Y2_IS_PAR_MAC2	BIT_11	/* MAC hardware fault interrupt */
#define Y2_IS_PAR_RX2	BIT_10	/* Parity Error Rx Queue 2 */
#define Y2_IS_TCP_TXS2	BIT_9	/* TCP length mismatch sync Tx queue IRQ */
#define Y2_IS_TCP_TXA2	BIT_8	/* TCP length mismatch async Tx queue IRQ */
#define Y2_IS_PAR_RD1	BIT_5	/* Read RAM parity error interrupt */
#define Y2_IS_PAR_WR1	BIT_4	/* Write RAM parity error interrupt */
#define Y2_IS_PAR_MAC1	BIT_3	/* MAC hardware fault interrupt */
#define Y2_IS_PAR_RX1	BIT_2	/* Parity Error Rx Queue 1 */
#define Y2_IS_TCP_TXS1	BIT_1	/* TCP length mismatch sync Tx queue IRQ */
#define Y2_IS_TCP_TXA1	BIT_0	/* TCP length mismatch async Tx queue IRQ */

#define Y2_HWE_L1_MASK	(Y2_IS_PAR_RD1 | Y2_IS_PAR_WR1 | Y2_IS_PAR_MAC1 |\
			 Y2_IS_PAR_RX1 | Y2_IS_TCP_TXS1| Y2_IS_TCP_TXA1)
#define Y2_HWE_L2_MASK	(Y2_IS_PAR_RD2 | Y2_IS_PAR_WR2 | Y2_IS_PAR_MAC2 |\
			 Y2_IS_PAR_RX2 | Y2_IS_TCP_TXS2| Y2_IS_TCP_TXA2)

#define Y2_HWE_ALL_MSK	(Y2_IS_TIST_OV | /* Y2_IS_SENSOR | */ Y2_IS_MST_ERR |\
			 Y2_IS_IRQ_STAT | Y2_IS_PCI_EXP | Y2_IS_PCI_NEXP |\
			 Y2_HWE_L1_MASK | Y2_HWE_L2_MASK)

/*	B2_MAC_CFG	 8 bit	MAC Configuration / Chip Revision */
#define CFG_CHIP_R_MSK	(0x0f<<4) /* Bit 7.. 4: Chip Revision */
#define CFG_DIS_M2_CLK	BIT_1	/* Disable Clock for 2nd MAC */
#define CFG_SNG_MAC	BIT_0	/* MAC Config: 0 = 2 MACs; 1 = 1 MAC */

/*	B2_CHIP_ID	 8 bit	Chip Identification Number */
#define CHIP_ID_GENESIS		0x0a /* Chip ID for GENESIS */
#define CHIP_ID_YUKON		0xb0 /* Chip ID for YUKON */
#define CHIP_ID_YUKON_LITE	0xb1 /* Chip ID for YUKON-Lite (Rev. A1-A3) */
#define CHIP_ID_YUKON_LP	0xb2 /* Chip ID for YUKON-LP */
#define CHIP_ID_YUKON_XL	0xb3 /* Chip ID for YUKON-2 XL */
#define CHIP_ID_YUKON_EC_U	0xb4 /* Chip ID for YUKON-2 EC Ultra */
#define CHIP_ID_YUKON_EX	0xb5 /* Chip ID for YUKON-2 Extreme */
#define CHIP_ID_YUKON_EC	0xb6 /* Chip ID for YUKON-2 EC */
#define CHIP_ID_YUKON_FE	0xb7 /* Chip ID for YUKON-2 FE */
#define CHIP_ID_YUKON_FE_P	0xb8 /* Chip ID for YUKON-2 FE+ */
#define CHIP_ID_YUKON_SUPR	0xb9 /* Chip ID for YUKON-2 Supreme */
#define CHIP_ID_YUKON_UL_2	0xba /* Chip ID for YUKON-2 Ultra 2 */
#define CHIP_ID_YUKON_UNKNOWN	0xbb
#define CHIP_ID_YUKON_OPT	0xbc /* Chip ID for YUKON-2 Optima */

#define	CHIP_REV_YU_XL_A0	0 /* Chip Rev. for Yukon-2 A0 */
#define	CHIP_REV_YU_XL_A1	1 /* Chip Rev. for Yukon-2 A1 */
#define	CHIP_REV_YU_XL_A2	2 /* Chip Rev. for Yukon-2 A2 */
#define	CHIP_REV_YU_XL_A3	3 /* Chip Rev. for Yukon-2 A3 */

#define CHIP_REV_YU_EC_A1	0 /* Chip Rev. for Yukon-EC A1/A0 */
#define CHIP_REV_YU_EC_A2	1 /* Chip Rev. for Yukon-EC A2 */
#define CHIP_REV_YU_EC_A3	2 /* Chip Rev. for Yukon-EC A3 */

#define	CHIP_REV_YU_EC_U_A0	1
#define	CHIP_REV_YU_EC_U_A1	2

#define	CHIP_REV_YU_FE_P_A0	0 /* Chip Rev. for Yukon-2 FE+ A0 */

#define	CHIP_REV_YU_EX_A0	1 /* Chip Rev. for Yukon-2 EX A0 */
#define	CHIP_REV_YU_EX_B0	2 /* Chip Rev. for Yukon-2 EX B0 */

#define	CHIP_REV_YU_SU_A0	0 /* Chip Rev. for Yukon-2 SUPR A0 */
#define	CHIP_REV_YU_SU_B0	1 /* Chip Rev. for Yukon-2 SUPR B0 */
#define	CHIP_REV_YU_SU_B1	3 /* Chip Rev. for Yukon-2 SUPR B1 */

/*	B2_Y2_CLK_GATE	 8 bit	Clock Gating (Yukon-2 only) */
#define Y2_STATUS_LNK2_INAC	BIT_7	/* Status Link 2 inactiv (0 = activ) */
#define Y2_CLK_GAT_LNK2_DIS	BIT_6	/* Disable clock gating Link 2 */
#define Y2_COR_CLK_LNK2_DIS	BIT_5	/* Disable Core clock Link 2 */
#define Y2_PCI_CLK_LNK2_DIS	BIT_4	/* Disable PCI clock Link 2 */
#define Y2_STATUS_LNK1_INAC	BIT_3	/* Status Link 1 inactiv (0 = activ) */
#define Y2_CLK_GAT_LNK1_DIS	BIT_2	/* Disable clock gating Link 1 */
#define Y2_COR_CLK_LNK1_DIS	BIT_1	/* Disable Core clock Link 1 */
#define Y2_PCI_CLK_LNK1_DIS	BIT_0	/* Disable PCI clock Link 1 */

/*	B2_Y2_HW_RES	8 bit	HW Resources (Yukon-2 only) */
#define CFG_LED_MODE_MSK	(0x07<<2)	/* Bit  4.. 2:	LED Mode Mask */
#define CFG_LINK_2_AVAIL	BIT_1	/* Link 2 available */
#define CFG_LINK_1_AVAIL	BIT_0	/* Link 1 available */

#define CFG_LED_MODE(x)		(((x) & CFG_LED_MODE_MSK) >> 2)
#define CFG_DUAL_MAC_MSK	(CFG_LINK_2_AVAIL | CFG_LINK_1_AVAIL)

/*	B2_E_3	 	8 bit	lower 4 bits used for HW self test result */
#define B2_E3_RES_MASK	0x0f

/*	B2_Y2_CLK_CTRL	32 bit	Core Clock Frequency Control Register (Yukon-2/EC) */
/* Yukon-EC/FE */
#define Y2_CLK_DIV_VAL_MSK	(0xff<<16) /* Bit 23..16: Clock Divisor Value */
#define Y2_CLK_DIV_VAL(x)	(SHIFT16(x) & Y2_CLK_DIV_VAL_MSK)
/* Yukon-2 */
#define Y2_CLK_DIV_VAL2_MSK	(0x07<<21) /* Bit 23..21: Clock Divisor Value */
#define Y2_CLK_SELECT2_MSK	(0x1f<<16) /* Bit 20..16: Clock Select */
#define Y2_CLK_DIV_VAL_2(x)	(SHIFT21(x) & Y2_CLK_DIV_VAL2_MSK)
#define Y2_CLK_SEL_VAL_2(x)	(SHIFT16(x) & Y2_CLK_SELECT2_MSK)
#define Y2_CLK_DIV_ENA		BIT_1	/* Enable  Core Clock Division */
#define Y2_CLK_DIV_DIS		BIT_0	/* Disable Core Clock Division */

/*	B2_TI_CTRL	 8 bit	Timer control */
/*	B2_IRQM_CTRL	 8 bit	IRQ Moderation Timer Control */
#define TIM_START	BIT_2	/* Start Timer */
#define TIM_STOP	BIT_1	/* Stop  Timer */
#define TIM_CLR_IRQ	BIT_0	/* Clear Timer IRQ (!IRQM) */

/*	B2_TI_TEST	 8 Bit	Timer Test */
/*	B2_IRQM_TEST	 8 bit	IRQ Moderation Timer Test */
/*	B28_DPT_TST	 8 bit	Descriptor Poll Timer Test Reg */
#define TIM_T_ON	BIT_2	/* Test mode on */
#define TIM_T_OFF	BIT_1	/* Test mode off */
#define TIM_T_STEP	BIT_0	/* Test step */

/*	B28_DPT_INI	32 bit	Descriptor Poll Timer Init Val */
/*	B28_DPT_VAL	32 bit	Descriptor Poll Timer Curr Val */
#define DPT_MSK		0x00ffffff	/* Bit 23.. 0:	Desc Poll Timer Bits */

/*	B28_DPT_CTRL	 8 bit	Descriptor Poll Timer Ctrl Reg */
#define DPT_START	BIT_1	/* Start Descriptor Poll Timer */
#define DPT_STOP	BIT_0	/* Stop  Descriptor Poll Timer */

/*	B2_TST_CTRL1	 8 bit	Test Control Register 1 */
#define TST_FRC_DPERR_MR	BIT_7	/* force DATAPERR on MST RD */
#define TST_FRC_DPERR_MW	BIT_6	/* force DATAPERR on MST WR */
#define TST_FRC_DPERR_TR	BIT_5	/* force DATAPERR on TRG RD */
#define TST_FRC_DPERR_TW	BIT_4	/* force DATAPERR on TRG WR */
#define TST_FRC_APERR_M		BIT_3	/* force ADDRPERR on MST */
#define TST_FRC_APERR_T		BIT_2	/* force ADDRPERR on TRG */
#define TST_CFG_WRITE_ON	BIT_1	/* Enable  Config Reg WR */
#define TST_CFG_WRITE_OFF	BIT_0	/* Disable Config Reg WR */

/*	B2_GP_IO */
#define	GLB_GPIO_CLK_DEB_ENA	BIT_31	/* Clock Debug Enable */
#define	GLB_GPIO_CLK_DBG_MSK	0x3c000000	/* Clock Debug */

#define	GLB_GPIO_INT_RST_D3_DIS	BIT_15	/* Disable Internal Reset After D3 to D0 */
#define	GLB_GPIO_LED_PAD_SPEED_UP	BIT_14	/* LED PAD Speed Up */
#define	GLB_GPIO_STAT_RACE_DIS	BIT_13	/* Status Race Disable */
#define	GLB_GPIO_TEST_SEL_MSK	0x00001800	/* Testmode Select */
#define	GLB_GPIO_TEST_SEL_BASE	BIT_11
#define	GLB_GPIO_RAND_ENA	BIT_10	/* Random Enable */
#define	GLB_GPIO_RAND_BIT_1	BIT_9	/* Random Bit 1 */

/*	B2_I2C_CTRL	32 bit	I2C HW Control Register */
#define I2C_FLAG	BIT_31		/* Start read/write if WR */
#define I2C_ADDR	(0x7fff<<16)	/* Bit 30..16:	Addr to be RD/WR */
#define I2C_DEV_SEL	(0x7f<<9)	/* Bit 15.. 9:	I2C Device Select */
#define I2C_BURST_LEN	BIT_4		/* Burst Len, 1/4 bytes */
#define I2C_DEV_SIZE	(7<<1)		/* Bit	3.. 1:	I2C Device Size	*/
#define I2C_025K_DEV	(0<<1)		/*		0: 256 Bytes or smal. */
#define I2C_05K_DEV	(1<<1)		/* 		1: 512	Bytes	*/
#define I2C_1K_DEV	(2<<1)		/*		2: 1024 Bytes	*/
#define I2C_2K_DEV	(3<<1)		/*		3: 2048	Bytes	*/
#define I2C_4K_DEV	(4<<1)		/*		4: 4096 Bytes	*/
#define I2C_8K_DEV	(5<<1)		/*		5: 8192 Bytes	*/
#define I2C_16K_DEV	(6<<1)		/*		6: 16384 Bytes	*/
#define I2C_32K_DEV	(7<<1)		/*		7: 32768 Bytes	*/
#define I2C_STOP	BIT_0		/* Interrupt I2C transfer */

/*	B2_I2C_IRQ	32 bit	I2C HW IRQ Register */
#define I2C_CLR_IRQ	BIT_0		/* Clear I2C IRQ */

/*	B2_I2C_SW	32 bit (8 bit access)	I2C HW SW Port Register */
#define I2C_DATA_DIR	BIT_2		/* direction of I2C_DATA */
#define I2C_DATA	BIT_1		/* I2C Data Port	*/
#define I2C_CLK		BIT_0		/* I2C Clock Port	*/

/* I2C Address */
#define I2C_SENS_ADDR	LM80_ADDR	/* I2C Sensor Address (Volt and Temp) */


/*	B2_BSC_CTRL	 8 bit	Blink Source Counter Control */
#define BSC_START	BIT_1		/* Start Blink Source Counter */
#define BSC_STOP	BIT_0		/* Stop  Blink Source Counter */

/*	B2_BSC_STAT	 8 bit	Blink Source Counter Status */
#define BSC_SRC		BIT_0		/* Blink Source, 0=Off / 1=On */

/*	B2_BSC_TST	16 bit	Blink Source Counter Test Reg */
#define BSC_T_ON	BIT_2		/* Test mode on */
#define BSC_T_OFF	BIT_1		/* Test mode off */
#define BSC_T_STEP	BIT_0		/* Test step */

/*	Y2_PEX_PHY_ADDR/DATA	PEX PHY address and data reg  (Yukon-2 only) */
#define PEX_RD_ACCESS	BIT_31	/* Access Mode Read = 1, Write = 0 */
#define PEX_DB_ACCESS	BIT_30	/* Access to debug register */

/*	B3_RAM_ADDR		32 bit	RAM Address, to read or write */
#define RAM_ADR_RAN	0x0007ffff	/* Bit 18.. 0:	RAM Address Range */

/* RAM Interface Registers */
/*	B3_RI_CTRL		16 bit	RAM Interface Control Register */
#define RI_CLR_RD_PERR	BIT_9	/* Clear IRQ RAM Read  Parity Err */
#define RI_CLR_WR_PERR	BIT_8	/* Clear IRQ RAM Write Parity Err */
#define RI_RST_CLR	BIT_1	/* Clear RAM Interface Reset */
#define RI_RST_SET	BIT_0	/* Set   RAM Interface Reset */

#define	MSK_RI_TO_53	36	/* RAM interface timeout */

/* Transmit Arbiter Registers MAC 1 and 2, use MR_ADDR() to access */
/*	TXA_ITI_INI	32 bit	Tx Arb Interval Timer Init Val */
/*	TXA_ITI_VAL	32 bit	Tx Arb Interval Timer Value */
/*	TXA_LIM_INI	32 bit	Tx Arb Limit Counter Init Val */
/*	TXA_LIM_VAL	32 bit	Tx Arb Limit Counter Value */
#define TXA_MAX_VAL	0x00ffffff/* Bit 23.. 0:	Max TXA Timer/Cnt Val */

/*	TXA_CTRL	 8 bit	Tx Arbiter Control Register */
#define TXA_ENA_FSYNC	BIT_7	/* Enable  force of sync Tx queue */
#define TXA_DIS_FSYNC	BIT_6	/* Disable force of sync Tx queue */
#define TXA_ENA_ALLOC	BIT_5	/* Enable  alloc of free bandwidth */
#define TXA_DIS_ALLOC	BIT_4	/* Disable alloc of free bandwidth */
#define TXA_START_RC	BIT_3	/* Start sync Rate Control */
#define TXA_STOP_RC	BIT_2	/* Stop  sync Rate Control */
#define TXA_ENA_ARB	BIT_1	/* Enable  Tx Arbiter */
#define TXA_DIS_ARB	BIT_0	/* Disable Tx Arbiter */

/*	TXA_TEST	 8 bit	Tx Arbiter Test Register */
#define TXA_INT_T_ON	BIT_5	/* Tx Arb Interval Timer Test On */
#define TXA_INT_T_OFF	BIT_4	/* Tx Arb Interval Timer Test Off */
#define TXA_INT_T_STEP	BIT_3	/* Tx Arb Interval Timer Step */
#define TXA_LIM_T_ON	BIT_2	/* Tx Arb Limit Timer Test On */
#define TXA_LIM_T_OFF	BIT_1	/* Tx Arb Limit Timer Test Off */
#define TXA_LIM_T_STEP	BIT_0	/* Tx Arb Limit Timer Step */

/*	TXA_STAT	 8 bit	Tx Arbiter Status Register */
#define TXA_PRIO_XS	BIT_0	/* sync queue has prio to send */

/*	Q_BC		32 bit	Current Byte Counter */
#define BC_MAX		0xffff	/* Bit 15.. 0:	Byte counter */

/* Rx BMU Control / Status Registers (Yukon-2) */
#define BMU_IDLE		BIT_31	/* BMU Idle State */
#define BMU_RX_TCP_PKT		BIT_30	/* Rx TCP Packet (when RSS Hash enabled) */
#define BMU_RX_IP_PKT		BIT_29	/* Rx IP  Packet (when RSS Hash enabled) */
#define BMU_ENA_RX_RSS_HASH	BIT_15	/* Enable  Rx RSS Hash */
#define BMU_DIS_RX_RSS_HASH	BIT_14	/* Disable Rx RSS Hash */
#define BMU_ENA_RX_CHKSUM	BIT_13	/* Enable  Rx TCP/IP Checksum Check */
#define BMU_DIS_RX_CHKSUM	BIT_12	/* Disable Rx TCP/IP Checksum Check */
#define BMU_CLR_IRQ_PAR		BIT_11	/* Clear IRQ on Parity errors (Rx) */
#define BMU_CLR_IRQ_TCP		BIT_11	/* Clear IRQ on TCP segmen. error (Tx) */
#define BMU_CLR_IRQ_CHK		BIT_10	/* Clear IRQ Check */
#define BMU_STOP		BIT_9	/* Stop  Rx/Tx Queue */
#define BMU_START		BIT_8	/* Start Rx/Tx Queue */
#define BMU_FIFO_OP_ON		BIT_7	/* FIFO Operational On */
#define BMU_FIFO_OP_OFF 	BIT_6	/* FIFO Operational Off */
#define BMU_FIFO_ENA		BIT_5	/* Enable FIFO */
#define BMU_FIFO_RST		BIT_4	/* Reset  FIFO */
#define BMU_OP_ON		BIT_3	/* BMU Operational On */
#define BMU_OP_OFF		BIT_2	/* BMU Operational Off */
#define BMU_RST_CLR		BIT_1	/* Clear BMU Reset (Enable) */
#define BMU_RST_SET		BIT_0	/* Set   BMU Reset */

#define BMU_CLR_RESET		(BMU_FIFO_RST | BMU_OP_OFF | BMU_RST_CLR)
#define BMU_OPER_INIT		(BMU_CLR_IRQ_PAR | BMU_CLR_IRQ_CHK | \
				 BMU_START | BMU_FIFO_ENA | BMU_OP_ON)

/* Tx BMU Control / Status Registers (Yukon-2) */
					/* Bit 31: same as for Rx */
#define BMU_TX_IPIDINCR_ON	BIT_13	/* Enable  IP ID Increment */
#define BMU_TX_IPIDINCR_OFF	BIT_12	/* Disable IP ID Increment */
#define BMU_TX_CLR_IRQ_TCP	BIT_11	/* Clear IRQ on TCP segm. length mism. */
					/* Bit 10..0: same as for Rx */

/*	Q_F		32 bit	Flag Register */
#define F_TX_CHK_AUTO_OFF	BIT_31	/* Tx checksum auto-calc Off(Yukon EX)*/
#define F_TX_CHK_AUTO_ON	BIT_30	/* Tx checksum auto-calc On(Yukon EX)*/
#define F_ALM_FULL		BIT_28	/* Rx FIFO: almost full */
#define F_EMPTY			BIT_27	/* Tx FIFO: empty flag */
#define F_FIFO_EOF		BIT_26	/* Tag (EOF Flag) bit in FIFO */
#define F_WM_REACHED		BIT_25	/* Watermark reached */
#define F_M_RX_RAM_DIS		BIT_24	/* MAC Rx RAM Read Port disable */
#define F_FIFO_LEVEL		(0x1f<<16)
					/* Bit 23..16:	# of Qwords in FIFO */
#define F_WATER_MARK		0x0007ff/* Bit 10.. 0:	Watermark */

/* Queue Prefetch Unit Offsets, use Y2_PREF_Q_ADDR() to address (Yukon-2 only)*/
/* PREF_UNIT_CTRL_REG	32 bit	Prefetch Control register */
#define PREF_UNIT_OP_ON		BIT_3	/* prefetch unit operational */
#define PREF_UNIT_OP_OFF	BIT_2	/* prefetch unit not operational */
#define PREF_UNIT_RST_CLR	BIT_1	/* Clear Prefetch Unit Reset */
#define PREF_UNIT_RST_SET	BIT_0	/* Set   Prefetch Unit Reset */

/* RAM Buffer Register Offsets, use RB_ADDR(Queue, Offs) to access */
/*	RB_START	32 bit	RAM Buffer Start Address */
/*	RB_END		32 bit	RAM Buffer End Address */
/*	RB_WP		32 bit	RAM Buffer Write Pointer */
/*	RB_RP		32 bit	RAM Buffer Read Pointer */
/*	RB_RX_UTPP	32 bit	Rx Upper Threshold, Pause Pack */
/*	RB_RX_LTPP	32 bit	Rx Lower Threshold, Pause Pack */
/*	RB_RX_UTHP	32 bit	Rx Upper Threshold, High Prio */
/*	RB_RX_LTHP	32 bit	Rx Lower Threshold, High Prio */
/*	RB_PC		32 bit	RAM Buffer Packet Counter */
/*	RB_LEV		32 bit	RAM Buffer Level Register */
#define RB_MSK	0x0007ffff	/* Bit 18.. 0:	RAM Buffer Pointer Bits */

/*	RB_TST2		 8 bit	RAM Buffer Test Register 2 */
#define RB_PC_DEC	BIT_3	/* Packet Counter Decrement */
#define RB_PC_T_ON	BIT_2	/* Packet Counter Test On */
#define RB_PC_T_OFF	BIT_1	/* Packet Counter Test Off */
#define RB_PC_INC	BIT_0	/* Packet Counter Increment */

/*	RB_TST1		 8 bit	RAM Buffer Test Register 1 */
#define RB_WP_T_ON	BIT_6	/* Write Pointer Test On */
#define RB_WP_T_OFF	BIT_5	/* Write Pointer Test Off */
#define RB_WP_INC	BIT_4	/* Write Pointer Increment */
#define RB_RP_T_ON	BIT_2	/* Read Pointer Test On */
#define RB_RP_T_OFF	BIT_1	/* Read Pointer Test Off */
#define RB_RP_INC	BIT_0	/* Read Pointer Increment */

/*	RB_CTRL		 8 bit	RAM Buffer Control Register */
#define RB_ENA_STFWD	BIT_5	/* Enable  Store & Forward */
#define RB_DIS_STFWD	BIT_4	/* Disable Store & Forward */
#define RB_ENA_OP_MD	BIT_3	/* Enable  Operation Mode */
#define RB_DIS_OP_MD	BIT_2	/* Disable Operation Mode */
#define RB_RST_CLR	BIT_1	/* Clear RAM Buf STM Reset */
#define RB_RST_SET	BIT_0	/* Set   RAM Buf STM Reset */

/* RAM Buffer High Pause Threshold values */
#define	MSK_RB_ULPP	(8 * 1024)	/* Upper Level in kB/8 */
#define	MSK_RB_LLPP_S	(10 * 1024)	/* Lower Level for small Queues */
#define	MSK_RB_LLPP_B	(16 * 1024)	/* Lower Level for big Queues */

/* Threshold values for Yukon-EC Ultra */
#define	MSK_ECU_ULPP	0x0080	/* Upper Pause Threshold (multiples of 8) */
#define	MSK_ECU_LLPP	0x0060	/* Lower Pause Threshold (multiples of 8) */
#define	MSK_ECU_AE_THR	0x0070  /* Almost Empty Threshold */
#define	MSK_ECU_TXFF_LEV	0x01a0	/* Tx BMU FIFO Level */
#define	MSK_ECU_JUMBO_WM	0x01

#define MSK_BMU_RX_WM		0x600	/* BMU Rx Watermark */
#define MSK_BMU_TX_WM		0x600	/* BMU Tx Watermark */
/* performance sensitive drivers should set this define to 0x80 */
#define MSK_BMU_RX_WM_PEX	0x600	/* BMU Rx Watermark for PEX */

/* Receive and Transmit Queues */
#define Q_R1		0x0000	/* Receive Queue 1 */
#define Q_R2		0x0080	/* Receive Queue 2 */
#define Q_XS1		0x0200	/* Synchronous Transmit Queue 1 */
#define Q_XA1		0x0280	/* Asynchronous Transmit Queue 1 */
#define Q_XS2		0x0300	/* Synchronous Transmit Queue 2 */
#define Q_XA2		0x0380	/* Asynchronous Transmit Queue 2 */

#define Q_ASF_R1	0x100	/* ASF Rx Queue 1 */
#define Q_ASF_R2	0x180	/* ASF Rx Queue 2 */
#define Q_ASF_T1	0x140	/* ASF Tx Queue 1 */
#define Q_ASF_T2	0x1c0	/* ASF Tx Queue 2 */

#define RB_ADDR(Queue, Offs)	(B16_RAM_REGS + (Queue) + (Offs))

/* Minimum RAM Buffer Rx Queue Size */
#define	MSK_MIN_RXQ_SIZE	10
/* Minimum RAM Buffer Tx Queue Size */
#define	MSK_MIN_TXQ_SIZE	10
/* Percentage of queue size from whole memory. 80 % for receive */
#define	MSK_RAM_QUOTA_RX	80

/*	WOL_CTRL_STAT	16 bit	WOL Control/Status Reg */
#define WOL_CTL_LINK_CHG_OCC		BIT_15
#define WOL_CTL_MAGIC_PKT_OCC		BIT_14
#define WOL_CTL_PATTERN_OCC		BIT_13
#define WOL_CTL_CLEAR_RESULT		BIT_12
#define WOL_CTL_ENA_PME_ON_LINK_CHG	BIT_11
#define WOL_CTL_DIS_PME_ON_LINK_CHG	BIT_10
#define WOL_CTL_ENA_PME_ON_MAGIC_PKT	BIT_9
#define WOL_CTL_DIS_PME_ON_MAGIC_PKT	BIT_8
#define WOL_CTL_ENA_PME_ON_PATTERN	BIT_7
#define WOL_CTL_DIS_PME_ON_PATTERN	BIT_6
#define WOL_CTL_ENA_LINK_CHG_UNIT	BIT_5
#define WOL_CTL_DIS_LINK_CHG_UNIT	BIT_4
#define WOL_CTL_ENA_MAGIC_PKT_UNIT	BIT_3
#define WOL_CTL_DIS_MAGIC_PKT_UNIT	BIT_2
#define WOL_CTL_ENA_PATTERN_UNIT	BIT_1
#define WOL_CTL_DIS_PATTERN_UNIT	BIT_0

#define WOL_CTL_DEFAULT				\
	(WOL_CTL_DIS_PME_ON_LINK_CHG |	\
	 WOL_CTL_DIS_PME_ON_PATTERN |	\
	 WOL_CTL_DIS_PME_ON_MAGIC_PKT |	\
	 WOL_CTL_DIS_LINK_CHG_UNIT |	\
	 WOL_CTL_DIS_PATTERN_UNIT |		\
	 WOL_CTL_DIS_MAGIC_PKT_UNIT)

/*	WOL_MATCH_CTL	 8 bit	WOL Match Control Reg */
#define WOL_CTL_PATT_ENA(x)	(BIT_0 << (x))

/*	WOL_PATT_PME	8 bit	WOL PME Match Enable (Yukon-2) */
#define WOL_PATT_FORCE_PME	BIT_7	/* Generates a PME */
#define WOL_PATT_MATCH_PME_ALL	0x7f


/*
 * Marvel-PHY Registers, indirect addressed over GMAC
 */
#define PHY_MARV_CTRL		0x00	/* 16 bit r/w	PHY Control Register */
#define PHY_MARV_STAT		0x01	/* 16 bit r/o	PHY Status Register */
#define PHY_MARV_ID0		0x02	/* 16 bit r/o	PHY ID0 Register */
#define PHY_MARV_ID1		0x03	/* 16 bit r/o	PHY ID1 Register */
#define PHY_MARV_AUNE_ADV	0x04	/* 16 bit r/w	Auto-Neg. Advertisement */
#define PHY_MARV_AUNE_LP	0x05	/* 16 bit r/o	Link Part Ability Reg */
#define PHY_MARV_AUNE_EXP	0x06	/* 16 bit r/o	Auto-Neg. Expansion Reg */
#define PHY_MARV_NEPG		0x07	/* 16 bit r/w	Next Page Register */
#define PHY_MARV_NEPG_LP	0x08	/* 16 bit r/o	Next Page Link Partner */
	/* Marvel-specific registers */
#define PHY_MARV_1000T_CTRL	0x09	/* 16 bit r/w	1000Base-T Control Reg */
#define PHY_MARV_1000T_STAT	0x0a	/* 16 bit r/o	1000Base-T Status Reg */
	/* 0x0b - 0x0e:		reserved */
#define PHY_MARV_EXT_STAT	0x0f	/* 16 bit r/o	Extended Status Reg */
#define PHY_MARV_PHY_CTRL	0x10	/* 16 bit r/w	PHY Specific Control Reg */
#define PHY_MARV_PHY_STAT	0x11	/* 16 bit r/o	PHY Specific Status Reg */
#define PHY_MARV_INT_MASK	0x12	/* 16 bit r/w	Interrupt Mask Reg */
#define PHY_MARV_INT_STAT	0x13	/* 16 bit r/o	Interrupt Status Reg */
#define PHY_MARV_EXT_CTRL	0x14	/* 16 bit r/w	Ext. PHY Specific Ctrl */
#define PHY_MARV_RXE_CNT	0x15	/* 16 bit r/w	Receive Error Counter */
#define PHY_MARV_EXT_ADR	0x16	/* 16 bit r/w	Ext. Ad. for Cable Diag. */
#define PHY_MARV_PORT_IRQ	0x17	/* 16 bit r/o	Port 0 IRQ (88E1111 only) */
#define PHY_MARV_LED_CTRL	0x18	/* 16 bit r/w	LED Control Reg */
#define PHY_MARV_LED_OVER	0x19	/* 16 bit r/w	Manual LED Override Reg */
#define PHY_MARV_EXT_CTRL_2	0x1a	/* 16 bit r/w	Ext. PHY Specific Ctrl 2 */
#define PHY_MARV_EXT_P_STAT	0x1b	/* 16 bit r/w	Ext. PHY Spec. Stat Reg */
#define PHY_MARV_CABLE_DIAG	0x1c	/* 16 bit r/o	Cable Diagnostic Reg */
#define PHY_MARV_PAGE_ADDR	0x1d	/* 16 bit r/w	Extended Page Address Reg */
#define PHY_MARV_PAGE_DATA	0x1e	/* 16 bit r/w	Extended Page Data Reg */

/* for 10/100 Fast Ethernet PHY (88E3082 only) */
#define PHY_MARV_FE_LED_PAR	0x16	/* 16 bit r/w	LED Parallel Select Reg. */
#define PHY_MARV_FE_LED_SER	0x17	/* 16 bit r/w	LED Stream Select S. LED */
#define PHY_MARV_FE_VCT_TX	0x1a	/* 16 bit r/w	VCT Reg. for TXP/N Pins */
#define PHY_MARV_FE_VCT_RX	0x1b	/* 16 bit r/o	VCT Reg. for RXP/N Pins */
#define PHY_MARV_FE_SPEC_2	0x1c	/* 16 bit r/w	Specific Control Reg. 2 */

#define PHY_CT_RESET	(1<<15)	/* Bit 15: (sc)	clear all PHY related regs */
#define PHY_CT_LOOP	(1<<14)	/* Bit 14:	enable Loopback over PHY */
#define PHY_CT_SPS_LSB	(1<<13) /* Bit 13:	Speed select, lower bit */
#define PHY_CT_ANE	(1<<12)	/* Bit 12:	Auto-Negotiation Enabled */
#define PHY_CT_PDOWN	(1<<11)	/* Bit 11:	Power Down Mode */
#define PHY_CT_ISOL	(1<<10)	/* Bit 10:	Isolate Mode */
#define PHY_CT_RE_CFG	(1<<9)	/* Bit  9:	(sc) Restart Auto-Negotiation */
#define PHY_CT_DUP_MD	(1<<8)	/* Bit  8:	Duplex Mode */
#define PHY_CT_COL_TST	(1<<7)	/* Bit  7:	Collision Test enabled */
#define PHY_CT_SPS_MSB	(1<<6)	/* Bit  6:	Speed select, upper bit */

#define PHY_CT_SP1000	PHY_CT_SPS_MSB	/* enable speed of 1000 Mbps */
#define PHY_CT_SP100	PHY_CT_SPS_LSB	/* enable speed of  100 Mbps */
#define PHY_CT_SP10	(0)		/* enable speed of   10 Mbps */

#define PHY_ST_EXT_ST	(1<<8)	/* Bit  8:	Extended Status Present */
#define PHY_ST_PRE_SUP	(1<<6)	/* Bit  6:	Preamble Suppression */
#define PHY_ST_AN_OVER	(1<<5)	/* Bit  5:	Auto-Negotiation Over */
#define	PHY_ST_REM_FLT	(1<<4)	/* Bit  4:	Remote Fault Condition Occurred */
#define PHY_ST_AN_CAP	(1<<3)	/* Bit  3:	Auto-Negotiation Capability */
#define PHY_ST_LSYNC	(1<<2)	/* Bit  2:	Link Synchronized */
#define PHY_ST_JAB_DET	(1<<1)	/* Bit  1:	Jabber Detected */
#define PHY_ST_EXT_REG	(1<<0)	/* Bit  0:	Extended Register available */

#define PHY_I1_OUI_MSK	(0x3f<<10)	/* Bit 15..10:	Organization Unique ID */
#define PHY_I1_MOD_NUM	(0x3f<<4)	/* Bit  9.. 4:	Model Number */
#define PHY_I1_REV_MSK	0xf		/* Bit  3.. 0:	Revision Number */

/* different Marvell PHY Ids */
#define PHY_MARV_ID0_VAL	0x0141	/* Marvell Unique Identifier */

#define PHY_MARV_ID1_B0		0x0C23	/* Yukon (PHY 88E1011) */
#define PHY_MARV_ID1_B2		0x0C25	/* Yukon-Plus (PHY 88E1011) */
#define PHY_MARV_ID1_C2		0x0CC2	/* Yukon-EC (PHY 88E1111) */
#define PHY_MARV_ID1_Y2		0x0C91	/* Yukon-2 (PHY 88E1112) */
#define PHY_MARV_ID1_FE		0x0C83	/* Yukon-FE (PHY 88E3082 Rev.A1) */
#define PHY_MARV_ID1_ECU	0x0CB0	/* Yukon-2 (PHY 88E1149 Rev.B2?) */

/*****  PHY_MARV_1000T_STAT	16 bit r/o	1000Base-T Status Reg *****/
#define PHY_B_1000S_MSF		(1<<15)	/* Bit 15:	Master/Slave Fault */
#define PHY_B_1000S_MSR		(1<<14)	/* Bit 14:	Master/Slave Result */
#define PHY_B_1000S_LRS		(1<<13)	/* Bit 13:	Local Receiver Status */
#define PHY_B_1000S_RRS		(1<<12)	/* Bit 12:	Remote Receiver Status */
#define PHY_B_1000S_LP_FD	(1<<11)	/* Bit 11:	Link Partner can FD */
#define PHY_B_1000S_LP_HD	(1<<10)	/* Bit 10:	Link Partner can HD */
#define PHY_B_1000S_IEC		0xff	/* Bit  7..0:	Idle Error Count */

/*****  PHY_MARV_AUNE_ADV	16 bit r/w	Auto-Negotiation Advertisement *****/
/*****  PHY_MARV_AUNE_LP	16 bit r/w	Link Part Ability Reg *****/
#define PHY_M_AN_NXT_PG		BIT_15	/* Request Next Page */
#define PHY_M_AN_ACK		BIT_14	/* (ro)	Acknowledge Received */
#define PHY_M_AN_RF		BIT_13	/* Remote Fault */
#define PHY_M_AN_ASP		BIT_11	/* Asymmetric Pause */
#define PHY_M_AN_PC		BIT_10	/* MAC Pause implemented */
#define PHY_M_AN_100_T4		BIT_9	/* Not cap. 100Base-T4 (always 0) */
#define PHY_M_AN_100_FD		BIT_8	/* Advertise 100Base-TX Full Duplex */
#define PHY_M_AN_100_HD		BIT_7	/* Advertise 100Base-TX Half Duplex */
#define PHY_M_AN_10_FD		BIT_6	/* Advertise 10Base-TX Full Duplex */
#define PHY_M_AN_10_HD		BIT_5	/* Advertise 10Base-TX Half Duplex */
#define PHY_M_AN_SEL_MSK	(0x1f<<4)	/* Bit  4.. 0: Selector Field Mask */

/* special defines for FIBER (88E1011S only) */
#define PHY_M_AN_ASP_X		BIT_8	/* Asymmetric Pause */
#define PHY_M_AN_PC_X		BIT_7	/* MAC Pause implemented */
#define PHY_M_AN_1000X_AHD	BIT_6	/* Advertise 10000Base-X Half Duplex */
#define PHY_M_AN_1000X_AFD	BIT_5	/* Advertise 10000Base-X Full Duplex */

/* Pause Bits (PHY_M_AN_ASP_X and PHY_M_AN_PC_X) encoding */
#define PHY_M_P_NO_PAUSE_X	(0<<7)	/* Bit  8.. 7:	no Pause Mode */
#define PHY_M_P_SYM_MD_X	(1<<7)	/* Bit  8.. 7:	symmetric Pause Mode */
#define PHY_M_P_ASYM_MD_X	(2<<7)	/* Bit  8.. 7:	asymmetric Pause Mode */
#define PHY_M_P_BOTH_MD_X	(3<<7)	/* Bit  8.. 7:	both Pause Mode */

/*****  PHY_MARV_1000T_CTRL	16 bit r/w	1000Base-T Control Reg *****/
#define PHY_M_1000C_TEST	(7<<13)	/* Bit 15..13:	Test Modes */
#define PHY_M_1000C_MSE		BIT_12	/* Manual Master/Slave Enable */
#define PHY_M_1000C_MSC		BIT_11	/* M/S Configuration (1=Master) */
#define PHY_M_1000C_MPD		BIT_10	/* Multi-Port Device */
#define PHY_M_1000C_AFD		BIT_9	/* Advertise Full Duplex */
#define PHY_M_1000C_AHD		BIT_8	/* Advertise Half Duplex */

/*****  PHY_MARV_PHY_CTRL	16 bit r/w	PHY Specific Ctrl Reg *****/
#define PHY_M_PC_TX_FFD_MSK	(3<<14)	/* Bit 15..14: Tx FIFO Depth Mask */
#define PHY_M_PC_RX_FFD_MSK	(3<<12)	/* Bit 13..12: Rx FIFO Depth Mask */
#define PHY_M_PC_ASS_CRS_TX	BIT_11	/* Assert CRS on Transmit */
#define PHY_M_PC_FL_GOOD	BIT_10	/* Force Link Good */
#define PHY_M_PC_EN_DET_MSK	(3<<8)	/* Bit  9.. 8: Energy Detect Mask */
#define PHY_M_PC_ENA_EXT_D	BIT_7	/* Enable Ext. Distance (10BT) */
#define PHY_M_PC_MDIX_MSK	(3<<5)	/* Bit  6.. 5: MDI/MDIX Config. Mask */
#define PHY_M_PC_DIS_125CLK	BIT_4	/* Disable 125 CLK */
#define PHY_M_PC_MAC_POW_UP	BIT_3	/* MAC Power up */
#define PHY_M_PC_SQE_T_ENA	BIT_2	/* SQE Test Enabled */
#define PHY_M_PC_POL_R_DIS	BIT_1	/* Polarity Reversal Disabled */
#define PHY_M_PC_DIS_JABBER	BIT_0	/* Disable Jabber */

#define PHY_M_PC_EN_DET		SHIFT8(2)	/* Energy Detect (Mode 1) */
#define PHY_M_PC_EN_DET_PLUS	SHIFT8(3)	/* Energy Detect Plus (Mode 2) */

#define PHY_M_PC_MDI_XMODE(x)	(SHIFT5(x) & PHY_M_PC_MDIX_MSK)

#define PHY_M_PC_MAN_MDI	0	/* 00 = Manual MDI configuration */
#define PHY_M_PC_MAN_MDIX	1	/* 01 = Manual MDIX configuration */
#define PHY_M_PC_ENA_AUTO	3	/* 11 = Enable Automatic Crossover */

/* for Yukon-2 Gigabit Ethernet PHY (88E1112 only) */
#define PHY_M_PC_DIS_LINK_P	BIT_15	/* Disable Link Pulses */
#define PHY_M_PC_DSC_MSK	(7<<12)	/* Bit 14..12:	Downshift Counter */
#define PHY_M_PC_DOWN_S_ENA	BIT_11	/* Downshift Enable */
					/* !!! Errata in spec. (1 = disable) */

#define PHY_M_PC_DSC(x)			(SHIFT12(x) & PHY_M_PC_DSC_MSK)
					/* 000=1x; 001=2x; 010=3x; 011=4x */
					/* 100=5x; 101=6x; 110=7x; 111=8x */

/* for 10/100 Fast Ethernet PHY (88E3082 only) */
#define PHY_M_PC_ENA_DTE_DT	BIT_15	/* Enable Data Terminal Equ. (DTE) Detect */
#define PHY_M_PC_ENA_ENE_DT	BIT_14	/* Enable Energy Detect (sense & pulse) */
#define PHY_M_PC_DIS_NLP_CK	BIT_13	/* Disable Normal Link Puls (NLP) Check */
#define PHY_M_PC_ENA_LIP_NP	BIT_12	/* Enable Link Partner Next Page Reg. */
#define PHY_M_PC_DIS_NLP_GN	BIT_11	/* Disable Normal Link Puls Generation */
#define PHY_M_PC_DIS_SCRAMB	BIT_9	/* Disable Scrambler */
#define PHY_M_PC_DIS_FEFI	BIT_8	/* Disable Far End Fault Indic. (FEFI) */
#define PHY_M_PC_SH_TP_SEL	BIT_6	/* Shielded Twisted Pair Select */
#define PHY_M_PC_RX_FD_MSK	(3<<2)	/* Bit  3.. 2: Rx FIFO Depth Mask */

/*****  PHY_MARV_PHY_STAT	16 bit r/o	PHY Specific Status Reg *****/
#define PHY_M_PS_SPEED_MSK	(3<<14)	/* Bit 15..14: Speed Mask */
#define PHY_M_PS_SPEED_1000	BIT_15	/*	10 = 1000 Mbps */
#define PHY_M_PS_SPEED_100	BIT_14	/*	01 =  100 Mbps */
#define PHY_M_PS_SPEED_10	0	/*	00 =   10 Mbps */
#define PHY_M_PS_FULL_DUP	BIT_13	/* Full Duplex */
#define PHY_M_PS_PAGE_REC	BIT_12	/* Page Received */
#define PHY_M_PS_SPDUP_RES	BIT_11	/* Speed & Duplex Resolved */
#define PHY_M_PS_LINK_UP	BIT_10	/* Link Up */
#define PHY_M_PS_CABLE_MSK	(7<<7)	/* Bit  9.. 7: Cable Length Mask */
#define PHY_M_PS_MDI_X_STAT	BIT_6	/* MDI Crossover Stat (1=MDIX) */
#define PHY_M_PS_DOWNS_STAT	BIT_5	/* Downshift Status (1=downsh.) */
#define PHY_M_PS_ENDET_STAT	BIT_4	/* Energy Detect Status (1=act) */
#define PHY_M_PS_TX_P_EN	BIT_3	/* Tx Pause Enabled */
#define PHY_M_PS_RX_P_EN	BIT_2	/* Rx Pause Enabled */
#define PHY_M_PS_POL_REV	BIT_1	/* Polarity Reversed */
#define PHY_M_PS_JABBER		BIT_0	/* Jabber */

#define PHY_M_PS_PAUSE_MSK	(PHY_M_PS_TX_P_EN | PHY_M_PS_RX_P_EN)

/* for 10/100 Fast Ethernet PHY (88E3082 only) */
#define PHY_M_PS_DTE_DETECT	BIT_15	/* Data Terminal Equipment (DTE) Detected */
#define PHY_M_PS_RES_SPEED	BIT_14	/* Resolved Speed (1=100 Mbps, 0=10 Mbps */

/*****  PHY_MARV_INT_MASK	16 bit r/w	Interrupt Mask Reg *****/
/*****  PHY_MARV_INT_STAT	16 bit r/o	Interrupt Status Reg *****/
#define PHY_M_IS_AN_ERROR	BIT_15	/* Auto-Negotiation Error */
#define PHY_M_IS_LSP_CHANGE	BIT_14	/* Link Speed Changed */
#define PHY_M_IS_DUP_CHANGE	BIT_13	/* Duplex Mode Changed */
#define PHY_M_IS_AN_PR		BIT_12	/* Page Received */
#define PHY_M_IS_AN_COMPL	BIT_11	/* Auto-Negotiation Completed */
#define PHY_M_IS_LST_CHANGE	BIT_10	/* Link Status Changed */
#define PHY_M_IS_SYMB_ERROR	BIT_9	/* Symbol Error */
#define PHY_M_IS_FALSE_CARR	BIT_8	/* False Carrier */
#define PHY_M_IS_FIFO_ERROR	BIT_7	/* FIFO Overflow/Underrun Error */
#define PHY_M_IS_MDI_CHANGE	BIT_6	/* MDI Crossover Changed */
#define PHY_M_IS_DOWNSH_DET	BIT_5	/* Downshift Detected */
#define PHY_M_IS_END_CHANGE	BIT_4	/* Energy Detect Changed */
#define PHY_M_IS_DTE_CHANGE	BIT_2	/* DTE Power Det. Status Changed */
#define PHY_M_IS_POL_CHANGE	BIT_1	/* Polarity Changed */
#define PHY_M_IS_JABBER		BIT_0	/* Jabber */

#define PHY_M_DEF_MSK		(PHY_M_IS_AN_ERROR | PHY_M_IS_AN_PR | \
				PHY_M_IS_LST_CHANGE | PHY_M_IS_FIFO_ERROR)

/*****  PHY_MARV_EXT_CTRL	16 bit r/w	Ext. PHY Specific Ctrl *****/
#define PHY_M_EC_ENA_BC_EXT	BIT_15	/* Enable Block Carr. Ext. (88E1111 only) */
#define PHY_M_EC_ENA_LIN_LB	BIT_14	/* Enable Line Loopback (88E1111 only) */
#define PHY_M_EC_DIS_LINK_P	BIT_12	/* Disable Link Pulses (88E1111 only) */
#define PHY_M_EC_M_DSC_MSK	(3<<10)	/* Bit 11..10:	Master Downshift Counter */
					/* (88E1011 only) */
#define PHY_M_EC_S_DSC_MSK	(3<<8)	/* Bit  9.. 8:	Slave  Downshift Counter */
					/* (88E1011 only) */
#define PHY_M_EC_DSC_MSK_2	(7<<9)	/* Bit 11.. 9:	Downshift Counter */
					/* (88E1111 only) */
#define PHY_M_EC_DOWN_S_ENA	BIT_8	/* Downshift Enable (88E1111 only) */
					/* !!! Errata in spec. (1 = disable) */
#define PHY_M_EC_RX_TIM_CT	BIT_7	/* RGMII Rx Timing Control*/
#define PHY_M_EC_MAC_S_MSK	(7<<4)	/* Bit  6.. 4:	Def. MAC interface speed */
#define PHY_M_EC_FIB_AN_ENA	BIT_3	/* Fiber Auto-Neg. Enable (88E1011S only) */
#define PHY_M_EC_DTE_D_ENA	BIT_2	/* DTE Detect Enable (88E1111 only) */
#define PHY_M_EC_TX_TIM_CT	BIT_1	/* RGMII Tx Timing Control */
#define PHY_M_EC_TRANS_DIS	BIT_0	/* Transmitter Disable (88E1111 only) */

#define PHY_M_EC_M_DSC(x)	(SHIFT10(x) & PHY_M_EC_M_DSC_MSK)
					/* 00=1x; 01=2x; 10=3x; 11=4x */
#define PHY_M_EC_S_DSC(x)	(SHIFT8(x) & PHY_M_EC_S_DSC_MSK)
					/* 00=dis; 01=1x; 10=2x; 11=3x */
#define PHY_M_EC_MAC_S(x)	(SHIFT4(x) & PHY_M_EC_MAC_S_MSK)
					/* 01X=0; 110=2.5; 111=25 (MHz) */

#define PHY_M_EC_DSC_2(x)	(SHIFT9(x) & PHY_M_EC_DSC_MSK_2)
					/* 000=1x; 001=2x; 010=3x; 011=4x */
					/* 100=5x; 101=6x; 110=7x; 111=8x */
#define MAC_TX_CLK_0_MHZ	2
#define MAC_TX_CLK_2_5_MHZ	6
#define MAC_TX_CLK_25_MHZ	7

/*****  PHY_MARV_LED_CTRL	16 bit r/w	LED Control Reg *****/
#define PHY_M_LEDC_DIS_LED	BIT_15	/* Disable LED */
#define PHY_M_LEDC_PULS_MSK	(7<<12)	/* Bit 14..12: Pulse Stretch Mask */
#define PHY_M_LEDC_F_INT	BIT_11	/* Force Interrupt */
#define PHY_M_LEDC_BL_R_MSK	(7<<8)	/* Bit 10.. 8: Blink Rate Mask */
#define PHY_M_LEDC_DP_C_LSB	BIT_7	/* Duplex Control (LSB, 88E1111 only) */
#define PHY_M_LEDC_TX_C_LSB	BIT_6	/* Tx Control (LSB, 88E1111 only) */
#define PHY_M_LEDC_LK_C_MSK	(7<<3)	/* Bit  5.. 3: Link Control Mask */
					/* (88E1111 only) */
#define PHY_M_LEDC_LINK_MSK	(3<<3)	/* Bit  4.. 3: Link Control Mask */
					/* (88E1011 only) */
#define PHY_M_LEDC_DP_CTRL	BIT_2	/* Duplex Control */
#define PHY_M_LEDC_DP_C_MSB	BIT_2	/* Duplex Control (MSB, 88E1111 only) */
#define PHY_M_LEDC_RX_CTRL	BIT_1	/* Rx Activity / Link */
#define PHY_M_LEDC_TX_CTRL	BIT_0	/* Tx Activity / Link */
#define PHY_M_LEDC_TX_C_MSB	BIT_0	/* Tx Control (MSB, 88E1111 only) */

#define PHY_M_LED_PULS_DUR(x)	(SHIFT12(x) & PHY_M_LEDC_PULS_MSK)

#define PULS_NO_STR		0	/* no pulse stretching */
#define PULS_21MS		1	/* 21 ms to 42 ms */
#define PULS_42MS		2	/* 42 ms to 84 ms */
#define PULS_84MS		3	/* 84 ms to 170 ms */
#define PULS_170MS		4	/* 170 ms to 340 ms */
#define PULS_340MS		5	/* 340 ms to 670 ms */
#define PULS_670MS		6	/* 670 ms to 1.3 s */
#define PULS_1300MS		7	/* 1.3 s to 2.7 s */

#define PHY_M_LED_BLINK_RT(x)	(SHIFT8(x) & PHY_M_LEDC_BL_R_MSK)

#define BLINK_42MS		0	/* 42 ms */
#define BLINK_84MS		1	/* 84 ms */
#define BLINK_170MS		2	/* 170 ms */
#define BLINK_340MS		3	/* 340 ms */
#define BLINK_670MS		4	/* 670 ms */

/*****  PHY_MARV_LED_OVER	16 bit r/w	Manual LED Override Reg *****/
#define PHY_M_LED_MO_SGMII(x)	SHIFT14(x)	/* Bit 15..14:  SGMII AN Timer */
#define PHY_M_LED_MO_DUP(x)	SHIFT10(x)	/* Bit 11..10:  Duplex */
#define PHY_M_LED_MO_10(x)	SHIFT8(x)	/* Bit  9.. 8:  Link 10 */
#define PHY_M_LED_MO_100(x)	SHIFT6(x)	/* Bit  7.. 6:  Link 100 */
#define PHY_M_LED_MO_1000(x)	SHIFT4(x)	/* Bit  5.. 4:  Link 1000 */
#define PHY_M_LED_MO_RX(x)	SHIFT2(x)	/* Bit  3.. 2:  Rx */
#define PHY_M_LED_MO_TX(x)	SHIFT0(x)	/* Bit  1.. 0:  Tx */

#define MO_LED_NORM		0
#define MO_LED_BLINK		1
#define MO_LED_OFF		2
#define MO_LED_ON		3

/*****  PHY_MARV_EXT_CTRL_2	16 bit r/w	Ext. PHY Specific Ctrl 2 *****/
#define PHY_M_EC2_FI_IMPED	BIT_6	/* Fiber Input  Impedance */
#define PHY_M_EC2_FO_IMPED	BIT_5	/* Fiber Output Impedance */
#define PHY_M_EC2_FO_M_CLK	BIT_4	/* Fiber Mode Clock Enable */
#define PHY_M_EC2_FO_BOOST	BIT_3	/* Fiber Output Boost */
#define PHY_M_EC2_FO_AM_MSK	7	/* Bit  2.. 0:	Fiber Output Amplitude */

/*****  PHY_MARV_EXT_P_STAT 16 bit r/w	Ext. PHY Specific Status *****/
#define PHY_M_FC_AUTO_SEL	BIT_15	/* Fiber/Copper Auto Sel. Dis. */
#define PHY_M_FC_AN_REG_ACC	BIT_14	/* Fiber/Copper AN Reg. Access */
#define PHY_M_FC_RESOLUTION	BIT_13	/* Fiber/Copper Resolution */
#define PHY_M_SER_IF_AN_BP	BIT_12	/* Ser. IF AN Bypass Enable */
#define PHY_M_SER_IF_BP_ST	BIT_11	/* Ser. IF AN Bypass Status */
#define PHY_M_IRQ_POLARITY	BIT_10	/* IRQ polarity */
#define PHY_M_DIS_AUT_MED	BIT_9	/* Disable Aut. Medium Reg. Selection */
					/* (88E1111 only) */
#define PHY_M_UNDOC1		BIT_7	/* undocumented bit !! */
#define PHY_M_DTE_POW_STAT	BIT_4	/* DTE Power Status (88E1111 only) */
#define PHY_M_MODE_MASK		0xf	/* Bit  3.. 0: copy of HWCFG MODE[3:0] */

/*****  PHY_MARV_CABLE_DIAG	16 bit r/o	Cable Diagnostic Reg *****/
#define PHY_M_CABD_ENA_TEST	BIT_15	/* Enable Test (Page 0) */
#define PHY_M_CABD_DIS_WAIT	BIT_15	/* Disable Waiting Period (Page 1) */
					/* (88E1111 only) */
#define PHY_M_CABD_STAT_MSK	(3<<13)		/* Bit 14..13: Status Mask */
#define PHY_M_CABD_AMPL_MSK	(0x1f<<8)	/* Bit 12.. 8: Amplitude Mask */
						/* (88E1111 only) */
#define PHY_M_CABD_DIST_MSK	0xff		/* Bit  7.. 0: Distance Mask */

/* values for Cable Diagnostic Status (11=fail; 00=OK; 10=open; 01=short) */
#define CABD_STAT_NORMAL	0
#define CABD_STAT_SHORT		1
#define CABD_STAT_OPEN		2
#define CABD_STAT_FAIL		3

/* for 10/100 Fast Ethernet PHY (88E3082 only) */
/*****  PHY_MARV_FE_LED_PAR	16 bit r/w	LED Parallel Select Reg. *****/
#define PHY_M_FELP_LED2_MSK	(0xf<<8)	/* Bit 11.. 8: LED2 Mask (LINK) */
#define PHY_M_FELP_LED1_MSK	(0xf<<4)	/* Bit  7.. 4: LED1 Mask (ACT) */
#define PHY_M_FELP_LED0_MSK	0xf		/* Bit  3.. 0: LED0 Mask (SPEED) */

#define PHY_M_FELP_LED2_CTRL(x)	(SHIFT8(x) & PHY_M_FELP_LED2_MSK)
#define PHY_M_FELP_LED1_CTRL(x)	(SHIFT4(x) & PHY_M_FELP_LED1_MSK)
#define PHY_M_FELP_LED0_CTRL(x)	(SHIFT0(x) & PHY_M_FELP_LED0_MSK)

#define LED_PAR_CTRL_COLX	0x00
#define LED_PAR_CTRL_ERROR	0x01
#define LED_PAR_CTRL_DUPLEX	0x02
#define LED_PAR_CTRL_DP_COL	0x03
#define LED_PAR_CTRL_SPEED	0x04
#define LED_PAR_CTRL_LINK	0x05
#define LED_PAR_CTRL_TX		0x06
#define LED_PAR_CTRL_RX		0x07
#define LED_PAR_CTRL_ACT	0x08
#define LED_PAR_CTRL_LNK_RX	0x09
#define LED_PAR_CTRL_LNK_AC	0x0a
#define LED_PAR_CTRL_ACT_BL	0x0b
#define LED_PAR_CTRL_TX_BL	0x0c
#define LED_PAR_CTRL_RX_BL	0x0d
#define LED_PAR_CTRL_COL_BL	0x0e
#define LED_PAR_CTRL_INACT	0x0f

/*****  PHY_MARV_FE_SPEC_2	16 bit r/w Specific Control Reg. 2 *****/
#define PHY_M_FESC_DIS_WAIT	BIT_2	/* Disable TDR Waiting Period */
#define PHY_M_FESC_ENA_MCLK	BIT_1	/* Enable MAC Rx Clock in sleep mode */
#define PHY_M_FESC_SEL_CL_A	BIT_0	/* Select Class A driver (100B-TX) */

/* for Yukon-2 Gigabit Ethernet PHY (88E1112 only) */
/*****  PHY_MARV_PHY_CTRL (page 1)	16 bit r/w Fiber Specific Ctrl *****/
#define PHY_M_FIB_FORCE_LNK	BIT_10	/* Force Link Good */
#define PHY_M_FIB_SIGD_POL	BIT_9	/* SIGDET Polarity */
#define PHY_M_FIB_TX_DIS	BIT_3	/* Transmitter Disable */

/*****  PHY_MARV_PHY_CTRL (page 2)	16 bit r/w MAC Specific Ctrl *****/
#define PHY_M_MAC_MD_MSK	(7<<7)	/* Bit  9.. 7: Mode Select Mask */
#define PHY_M_MAC_MD_AUTO	3	/* Auto Copper/1000Base-X */
#define PHY_M_MAC_MD_COPPER	5	/* Copper only */
#define PHY_M_MAC_MD_1000BX	7	/* 1000Base-X only */
#define PHY_M_MAC_MODE_SEL(x)	(SHIFT7(x) & PHY_M_MAC_MD_MSK)

/*****  PHY_MARV_PHY_CTRL (page 3)	16 bit r/w LED Control Reg. *****/
#define PHY_M_LEDC_LOS_MSK	(0xf<<12)	/* Bit 15..12: LOS LED Ctrl. Mask */
#define PHY_M_LEDC_INIT_MSK	(0xf<<8)	/* Bit 11.. 8: INIT LED Ctrl. Mask */
#define PHY_M_LEDC_STA1_MSK	(0xf<<4)	/* Bit  7.. 4: STAT1 LED Ctrl. Mask */
#define PHY_M_LEDC_STA0_MSK	0xf		/* Bit  3.. 0: STAT0 LED Ctrl. Mask */

#define PHY_M_LEDC_LOS_CTRL(x)	(SHIFT12(x) & PHY_M_LEDC_LOS_MSK)
#define PHY_M_LEDC_INIT_CTRL(x)	(SHIFT8(x) & PHY_M_LEDC_INIT_MSK)
#define PHY_M_LEDC_STA1_CTRL(x)	(SHIFT4(x) & PHY_M_LEDC_STA1_MSK)
#define PHY_M_LEDC_STA0_CTRL(x)	(SHIFT0(x) & PHY_M_LEDC_STA0_MSK)

/*****  PHY_MARV_PHY_STAT (page 3)	16 bit r/w Polarity Control Reg. *****/
#define PHY_M_POLC_LS1M_MSK	(0xf<<12)	/* Bit 15..12: LOS,STAT1 Mix % Mask */
#define PHY_M_POLC_IS0M_MSK	(0xf<<8)	/* Bit 11.. 8: INIT,STAT0 Mix % Mask */
#define PHY_M_POLC_LOS_MSK	(0x3<<6)	/* Bit  7.. 6: LOS Pol. Ctrl. Mask */
#define PHY_M_POLC_INIT_MSK	(0x3<<4)	/* Bit  5.. 4: INIT Pol. Ctrl. Mask */
#define PHY_M_POLC_STA1_MSK	(0x3<<2)	/* Bit  3.. 2: STAT1 Pol. Ctrl. Mask */
#define PHY_M_POLC_STA0_MSK	0x3		/* Bit  1.. 0: STAT0 Pol. Ctrl. Mask */

#define PHY_M_POLC_LS1_P_MIX(x)	(SHIFT12(x) & PHY_M_POLC_LS1M_MSK)
#define PHY_M_POLC_IS0_P_MIX(x)	(SHIFT8(x) & PHY_M_POLC_IS0M_MSK)
#define PHY_M_POLC_LOS_CTRL(x)	(SHIFT6(x) & PHY_M_POLC_LOS_MSK)
#define PHY_M_POLC_INIT_CTRL(x)	(SHIFT4(x) & PHY_M_POLC_INIT_MSK)
#define PHY_M_POLC_STA1_CTRL(x)	(SHIFT2(x) & PHY_M_POLC_STA1_MSK)
#define PHY_M_POLC_STA0_CTRL(x)	(SHIFT0(x) & PHY_M_POLC_STA0_MSK)

/*
 * GMAC registers
 *
 * The GMAC registers are 16 or 32 bits wide.
 * The GMACs host processor interface is 16 bits wide,
 * therefore ALL registers will be addressed with 16 bit accesses.
 *
 * Note:	NA reg	= Network Address e.g DA, SA etc.
 */

/* Port Registers */
#define GM_GP_STAT	0x0000	/* 16 bit r/o	General Purpose Status */
#define GM_GP_CTRL	0x0004	/* 16 bit r/w	General Purpose Control */
#define GM_TX_CTRL	0x0008	/* 16 bit r/w	Transmit Control Reg. */
#define GM_RX_CTRL	0x000c	/* 16 bit r/w	Receive Control Reg. */
#define GM_TX_FLOW_CTRL	0x0010	/* 16 bit r/w	Transmit Flow-Control */
#define GM_TX_PARAM	0x0014	/* 16 bit r/w	Transmit Parameter Reg. */
#define GM_SERIAL_MODE	0x0018	/* 16 bit r/w	Serial Mode Register */

/* Source Address Registers */
#define GM_SRC_ADDR_1L	0x001c	/* 16 bit r/w	Source Address 1 (low) */
#define GM_SRC_ADDR_1M	0x0020	/* 16 bit r/w	Source Address 1 (middle) */
#define GM_SRC_ADDR_1H	0x0024	/* 16 bit r/w	Source Address 1 (high) */
#define GM_SRC_ADDR_2L	0x0028	/* 16 bit r/w	Source Address 2 (low) */
#define GM_SRC_ADDR_2M	0x002c	/* 16 bit r/w	Source Address 2 (middle) */
#define GM_SRC_ADDR_2H	0x0030	/* 16 bit r/w	Source Address 2 (high) */

/* Multicast Address Hash Registers */
#define GM_MC_ADDR_H1	0x0034	/* 16 bit r/w	Multicast Address Hash 1 */
#define GM_MC_ADDR_H2	0x0038	/* 16 bit r/w	Multicast Address Hash 2 */
#define GM_MC_ADDR_H3	0x003c	/* 16 bit r/w	Multicast Address Hash 3 */
#define GM_MC_ADDR_H4	0x0040	/* 16 bit r/w	Multicast Address Hash 4 */

/* Interrupt Source Registers */
#define GM_TX_IRQ_SRC	0x0044	/* 16 bit r/o	Tx Overflow IRQ Source */
#define GM_RX_IRQ_SRC	0x0048	/* 16 bit r/o	Rx Overflow IRQ Source */
#define GM_TR_IRQ_SRC	0x004c	/* 16 bit r/o	Tx/Rx Over. IRQ Source */

/* Interrupt Mask Registers */
#define GM_TX_IRQ_MSK	0x0050	/* 16 bit r/w	Tx Overflow IRQ Mask */
#define GM_RX_IRQ_MSK	0x0054	/* 16 bit r/w	Rx Overflow IRQ Mask */
#define GM_TR_IRQ_MSK	0x0058	/* 16 bit r/w	Tx/Rx Over. IRQ Mask */

/* Serial Management Interface (SMI) Registers */
#define GM_SMI_CTRL	0x0080	/* 16 bit r/w	SMI Control Register */
#define GM_SMI_DATA	0x0084	/* 16 bit r/w	SMI Data Register */
#define GM_PHY_ADDR	0x0088	/* 16 bit r/w	GPHY Address Register */

/* MIB Counters */
#define GM_MIB_CNT_BASE	0x0100	/* Base Address of MIB Counters */
#define GM_MIB_CNT_SIZE	44	/* Number of MIB Counters */

/*
 * MIB Counters base address definitions (low word) -
 * use offset 4 for access to high word	(32 bit r/o)
 */
#define GM_RXF_UC_OK \
			(GM_MIB_CNT_BASE + 0)	/* Unicast Frames Received OK */
#define GM_RXF_BC_OK \
			(GM_MIB_CNT_BASE + 8)	/* Broadcast Frames Received OK */
#define GM_RXF_MPAUSE \
			(GM_MIB_CNT_BASE + 16)	/* Pause MAC Ctrl Frames Received */
#define GM_RXF_MC_OK \
			(GM_MIB_CNT_BASE + 24)	/* Multicast Frames Received OK */
#define GM_RXF_FCS_ERR \
			(GM_MIB_CNT_BASE + 32)	/* Rx Frame Check Seq. Error */
#define GM_RXF_SPARE1 \
			(GM_MIB_CNT_BASE + 40)	/* Rx spare 1 */
#define GM_RXO_OK_LO \
			(GM_MIB_CNT_BASE + 48)	/* Octets Received OK Low */
#define GM_RXO_OK_HI \
			(GM_MIB_CNT_BASE + 56)	/* Octets Received OK High */
#define GM_RXO_ERR_LO \
			(GM_MIB_CNT_BASE + 64)	/* Octets Received Invalid Low */
#define GM_RXO_ERR_HI \
			(GM_MIB_CNT_BASE + 72)	/* Octets Received Invalid High */
#define GM_RXF_SHT \
			(GM_MIB_CNT_BASE + 80)	/* Frames <64 Byte Received OK */
#define GM_RXE_FRAG \
			(GM_MIB_CNT_BASE + 88)	/* Frames <64 Byte Received with FCS Err */
#define GM_RXF_64B \
			(GM_MIB_CNT_BASE + 96)	/* 64 Byte Rx Frame */
#define GM_RXF_127B \
			(GM_MIB_CNT_BASE + 104)	/* 65-127 Byte Rx Frame */
#define GM_RXF_255B \
			(GM_MIB_CNT_BASE + 112)	/* 128-255 Byte Rx Frame */
#define GM_RXF_511B \
			(GM_MIB_CNT_BASE + 120)	/* 256-511 Byte Rx Frame */
#define GM_RXF_1023B \
			(GM_MIB_CNT_BASE + 128)	/* 512-1023 Byte Rx Frame */
#define GM_RXF_1518B \
			(GM_MIB_CNT_BASE + 136)	/* 1024-1518 Byte Rx Frame */
#define GM_RXF_MAX_SZ \
			(GM_MIB_CNT_BASE + 144)	/* 1519-MaxSize Byte Rx Frame */
#define GM_RXF_LNG_ERR \
			(GM_MIB_CNT_BASE + 152)	/* Rx Frame too Long Error */
#define GM_RXF_JAB_PKT \
			(GM_MIB_CNT_BASE + 160)	/* Rx Jabber Packet Frame */
#define GM_RXF_SPARE2 \
			(GM_MIB_CNT_BASE + 168)	/* Rx spare 2 */
#define GM_RXE_FIFO_OV \
			(GM_MIB_CNT_BASE + 176)	/* Rx FIFO overflow Event */
#define GM_RXF_SPARE3 \
			(GM_MIB_CNT_BASE + 184)	/* Rx spare 3 */
#define GM_TXF_UC_OK \
			(GM_MIB_CNT_BASE + 192)	/* Unicast Frames Xmitted OK */
#define GM_TXF_BC_OK \
			(GM_MIB_CNT_BASE + 200)	/* Broadcast Frames Xmitted OK */
#define GM_TXF_MPAUSE \
			(GM_MIB_CNT_BASE + 208)	/* Pause MAC Ctrl Frames Xmitted */
#define GM_TXF_MC_OK \
			(GM_MIB_CNT_BASE + 216)	/* Multicast Frames Xmitted OK */
#define GM_TXO_OK_LO \
			(GM_MIB_CNT_BASE + 224)	/* Octets Transmitted OK Low */
#define GM_TXO_OK_HI \
			(GM_MIB_CNT_BASE + 232)	/* Octets Transmitted OK High */
#define GM_TXF_64B \
			(GM_MIB_CNT_BASE + 240)	/* 64 Byte Tx Frame */
#define GM_TXF_127B \
			(GM_MIB_CNT_BASE + 248)	/* 65-127 Byte Tx Frame */
#define GM_TXF_255B \
			(GM_MIB_CNT_BASE + 256)	/* 128-255 Byte Tx Frame */
#define GM_TXF_511B \
			(GM_MIB_CNT_BASE + 264)	/* 256-511 Byte Tx Frame */
#define GM_TXF_1023B \
			(GM_MIB_CNT_BASE + 272)	/* 512-1023 Byte Tx Frame */
#define GM_TXF_1518B \
			(GM_MIB_CNT_BASE + 280)	/* 1024-1518 Byte Tx Frame */
#define GM_TXF_MAX_SZ \
			(GM_MIB_CNT_BASE + 288)	/* 1519-MaxSize Byte Tx Frame */
#define GM_TXF_SPARE1 \
			(GM_MIB_CNT_BASE + 296)	/* Tx spare 1 */
#define GM_TXF_COL \
			(GM_MIB_CNT_BASE + 304)	/* Tx Collision */
#define GM_TXF_LAT_COL \
			(GM_MIB_CNT_BASE + 312)	/* Tx Late Collision */
#define GM_TXF_ABO_COL \
			(GM_MIB_CNT_BASE + 320)	/* Tx aborted due to Exces. Col. */
#define GM_TXF_MUL_COL \
			(GM_MIB_CNT_BASE + 328)	/* Tx Multiple Collision */
#define GM_TXF_SNG_COL \
			(GM_MIB_CNT_BASE + 336)	/* Tx Single Collision */
#define GM_TXE_FIFO_UR \
			(GM_MIB_CNT_BASE + 344)	/* Tx FIFO Underrun Event */

/*----------------------------------------------------------------------------*/
/*
 * GMAC Bit Definitions
 *
 * If the bit access behaviour differs from the register access behaviour
 * (r/w, r/o) this is documented after the bit number.
 * The following bit access behaviours are used:
 *	(sc)	self clearing
 *	(r/o)	read only
 */

/*	GM_GP_STAT	16 bit r/o	General Purpose Status Register */
#define GM_GPSR_SPEED		BIT_15	/* Port Speed (1 = 100 Mbps) */
#define GM_GPSR_DUPLEX		BIT_14	/* Duplex Mode (1 = Full) */
#define GM_GPSR_FC_TX_DIS	BIT_13	/* Tx Flow-Control Mode Disabled */
#define GM_GPSR_LINK_UP		BIT_12	/* Link Up Status */
#define GM_GPSR_PAUSE		BIT_11	/* Pause State */
#define GM_GPSR_TX_ACTIVE	BIT_10	/* Tx in Progress */
#define	GM_GPSR_EXC_COL		BIT_9	/* Excessive Collisions Occurred */
#define	GM_GPSR_LAT_COL		BIT_8	/* Late Collisions Occurred */
#define GM_GPSR_PHY_ST_CH	BIT_5	/* PHY Status Change */
#define GM_GPSR_GIG_SPEED	BIT_4	/* Gigabit Speed (1 = 1000 Mbps) */
#define GM_GPSR_PART_MODE	BIT_3	/* Partition mode */
#define GM_GPSR_FC_RX_DIS	BIT_2	/* Rx Flow-Control Mode Disabled */

/*	GM_GP_CTRL	16 bit r/w	General Purpose Control Register */
#define GM_GPCR_RMII_PH_ENA	BIT_15	/* Enable RMII for PHY (Yukon-FE only) */
#define GM_GPCR_RMII_LB_ENA	BIT_14	/* Enable RMII Loopback (Yukon-FE only) */
#define GM_GPCR_FC_TX_DIS	BIT_13	/* Disable Tx Flow-Control Mode */
#define GM_GPCR_TX_ENA		BIT_12	/* Enable Transmit */
#define GM_GPCR_RX_ENA		BIT_11	/* Enable Receive */
#define GM_GPCR_LOOP_ENA	BIT_9	/* Enable MAC Loopback Mode */
#define GM_GPCR_PART_ENA	BIT_8	/* Enable Partition Mode */
#define GM_GPCR_GIGS_ENA	BIT_7	/* Gigabit Speed (1000 Mbps) */
#define GM_GPCR_FL_PASS		BIT_6	/* Force Link Pass */
#define GM_GPCR_DUP_FULL	BIT_5	/* Full Duplex Mode */
#define GM_GPCR_FC_RX_DIS	BIT_4	/* Disable Rx Flow-Control Mode */
#define GM_GPCR_SPEED_100	BIT_3	/* Port Speed 100 Mbps */
#define GM_GPCR_AU_DUP_DIS	BIT_2	/* Disable Auto-Update Duplex */
#define GM_GPCR_AU_FCT_DIS	BIT_1	/* Disable Auto-Update Flow-C. */
#define GM_GPCR_AU_SPD_DIS	BIT_0	/* Disable Auto-Update Speed */

#define GM_GPCR_SPEED_1000	(GM_GPCR_GIGS_ENA | GM_GPCR_SPEED_100)
#define GM_GPCR_AU_ALL_DIS	(GM_GPCR_AU_DUP_DIS | GM_GPCR_AU_FCT_DIS |\
				 GM_GPCR_AU_SPD_DIS)

/*	GM_TX_CTRL	16 bit r/w	Transmit Control Register */
#define GM_TXCR_FORCE_JAM	BIT_15	/* Force Jam / Flow-Control */
#define GM_TXCR_CRC_DIS		BIT_14	/* Disable insertion of CRC */
#define GM_TXCR_PAD_DIS		BIT_13	/* Disable padding of packets */
#define GM_TXCR_COL_THR_MSK	(7<<10)	/* Bit 12..10: Collision Threshold Mask */
#define GM_TXCR_PAD_PAT_MSK	0xff	/* Bit  7.. 0: Padding Pattern Mask */
					/* (Yukon-2 only) */

#define TX_COL_THR(x)		(SHIFT10(x) & GM_TXCR_COL_THR_MSK)
#define TX_COL_DEF		0x04

/*	GM_RX_CTRL	16 bit r/w	Receive Control Register */
#define GM_RXCR_UCF_ENA		BIT_15	/* Enable Unicast filtering */
#define GM_RXCR_MCF_ENA		BIT_14	/* Enable Multicast filtering */
#define GM_RXCR_CRC_DIS		BIT_13	/* Remove 4-byte CRC */
#define GM_RXCR_PASS_FC		BIT_12	/* Pass FC packets to FIFO (Yukon-1 only) */

/*	GM_TX_PARAM	16 bit r/w	Transmit Parameter Register */
#define GM_TXPA_JAMLEN_MSK	(3<<14)		/* Bit 15..14: Jam Length Mask */
#define GM_TXPA_JAMIPG_MSK	(0x1f<<9)	/* Bit 13.. 9: Jam IPG Mask */
#define GM_TXPA_JAMDAT_MSK	(0x1f<<4)	/* Bit  8.. 4: IPG Jam to Data Mask */
#define GM_TXPA_BO_LIM_MSK	0x0f		/* Bit  3.. 0: Backoff Limit Mask */
						/* (Yukon-2 only) */

#define TX_JAM_LEN_VAL(x)	(SHIFT14(x) & GM_TXPA_JAMLEN_MSK)
#define TX_JAM_IPG_VAL(x)	(SHIFT9(x) & GM_TXPA_JAMIPG_MSK)
#define TX_IPG_JAM_DATA(x)	(SHIFT4(x) & GM_TXPA_JAMDAT_MSK)
#define TX_BACK_OFF_LIM(x)	((x) & GM_TXPA_BO_LIM_MSK)

#define TX_JAM_LEN_DEF		0x03
#define TX_JAM_IPG_DEF		0x0b
#define TX_IPG_JAM_DEF		0x1c
#define TX_BOF_LIM_DEF		0x04

/*	GM_SERIAL_MODE	16 bit r/w	Serial Mode Register */
#define GM_SMOD_DATABL_MSK	(0x1f<<11)	/* Bit 15..11:	Data Blinder */
						/* r/o on Yukon, r/w on Yukon-EC */
#define GM_SMOD_LIMIT_4		BIT_10	/* 4 consecutive Tx trials */
#define GM_SMOD_VLAN_ENA	BIT_9	/* Enable VLAN  (Max. Frame Len) */
#define GM_SMOD_JUMBO_ENA	BIT_8	/* Enable Jumbo (Max. Frame Len) */
#define GM_SMOD_IPG_MSK		0x1f	/* Bit  4.. 0:	Inter-Packet Gap (IPG) */

#define DATA_BLIND_VAL(x)	(SHIFT11(x) & GM_SMOD_DATABL_MSK)
#define IPG_DATA_VAL(x)		((x) & GM_SMOD_IPG_MSK)

#define DATA_BLIND_DEF		0x04
#define IPG_DATA_DEF		0x1e

/*	GM_SMI_CTRL	16 bit r/w	SMI Control Register */
#define GM_SMI_CT_PHY_A_MSK	(0x1f<<11)	/* Bit 15..11:	PHY Device Address */
#define GM_SMI_CT_REG_A_MSK	(0x1f<<6)	/* Bit 10.. 6:	PHY Register Address */
#define GM_SMI_CT_OP_RD		BIT_5	/* OpCode Read (0=Write)*/
#define GM_SMI_CT_RD_VAL	BIT_4	/* Read Valid (Read completed) */
#define GM_SMI_CT_BUSY		BIT_3	/* Busy (Operation in progress) */

#define GM_SMI_CT_PHY_AD(x)	(SHIFT11(x) & GM_SMI_CT_PHY_A_MSK)
#define GM_SMI_CT_REG_AD(x)	(SHIFT6(x) & GM_SMI_CT_REG_A_MSK)

/*	GM_PHY_ADDR	16 bit r/w	GPHY Address Register */
#define GM_PAR_MIB_CLR		BIT_5	/* Set MIB Clear Counter Mode */
#define GM_PAR_MIB_TST		BIT_4	/* MIB Load Counter (Test Mode) */

/* Receive Frame Status Encoding */
#define GMR_FS_LEN_MSK	(0xffff<<16)	/* Bit 31..16:	Rx Frame Length */
#define GMR_FS_VLAN		BIT_13	/* VLAN Packet */
#define GMR_FS_JABBER		BIT_12	/* Jabber Packet */
#define GMR_FS_UN_SIZE		BIT_11	/* Undersize Packet */
#define GMR_FS_MC		BIT_10	/* Multicast Packet */
#define GMR_FS_BC		BIT_9	/* Broadcast Packet */
#define GMR_FS_RX_OK		BIT_8	/* Receive OK (Good Packet) */
#define GMR_FS_GOOD_FC		BIT_7	/* Good Flow-Control Packet */
#define GMR_FS_BAD_FC		BIT_6	/* Bad  Flow-Control Packet */
#define GMR_FS_MII_ERR		BIT_5	/* MII Error */
#define GMR_FS_LONG_ERR		BIT_4	/* Too Long Packet */
#define GMR_FS_FRAGMENT		BIT_3	/* Fragment */
#define GMR_FS_CRC_ERR		BIT_1	/* CRC Error */
#define GMR_FS_RX_FF_OV		BIT_0	/* Rx FIFO Overflow */

#define GMR_FS_LEN_SHIFT	16

#define GMR_FS_ANY_ERR		( \
			GMR_FS_RX_FF_OV | \
			GMR_FS_CRC_ERR | \
			GMR_FS_FRAGMENT | \
			GMR_FS_LONG_ERR | \
			GMR_FS_MII_ERR | \
			GMR_FS_BAD_FC | \
			GMR_FS_GOOD_FC | \
			GMR_FS_UN_SIZE | \
			GMR_FS_JABBER)

/* Rx GMAC FIFO Flush Mask (default) */
#define RX_FF_FL_DEF_MSK	GMR_FS_ANY_ERR

/*	Receive and Transmit GMAC FIFO Registers (YUKON only) */

/*	RX_GMF_EA	32 bit	Rx GMAC FIFO End Address */
/*	RX_GMF_AF_THR	32 bit	Rx GMAC FIFO Almost Full Thresh. */
/*	RX_GMF_WP	32 bit	Rx GMAC FIFO Write Pointer */
/*	RX_GMF_WLEV	32 bit	Rx GMAC FIFO Write Level */
/*	RX_GMF_RP	32 bit	Rx GMAC FIFO Read Pointer */
/*	RX_GMF_RLEV	32 bit	Rx GMAC FIFO Read Level */
/*	TX_GMF_EA	32 bit	Tx GMAC FIFO End Address */
/*	TX_GMF_AE_THR	32 bit	Tx GMAC FIFO Almost Empty Thresh.*/
/*	TX_GMF_WP	32 bit	Tx GMAC FIFO Write Pointer */
/*	TX_GMF_WSP	32 bit	Tx GMAC FIFO Write Shadow Pointer */
/*	TX_GMF_WLEV	32 bit	Tx GMAC FIFO Write Level */
/*	TX_GMF_RP	32 bit	Tx GMAC FIFO Read Pointer */
/*	TX_GMF_RSTP	32 bit	Tx GMAC FIFO Restart Pointer */
/*	TX_GMF_RLEV	32 bit	Tx GMAC FIFO Read Level */

/*	RX_GMF_CTRL_T	32 bit	Rx GMAC FIFO Control/Test */
#define RX_TRUNC_ON		BIT_27  /* enable  packet truncation */
#define RX_TRUNC_OFF		BIT_26	/* disable packet truncation */
#define RX_VLAN_STRIP_ON	BIT_25	/* enable  VLAN stripping */
#define RX_VLAN_STRIP_OFF	BIT_24	/* disable VLAN stripping */
#define GMF_RX_MACSEC_FLUSH_ON	BIT_23
#define GMF_RX_MACSEC_FLUSH_OFF	BIT_22
#define GMF_RX_OVER_ON		BIT_19	/* enable flushing on receive overrun */
#define GMF_RX_OVER_OFF		BIT_18	/* disable flushing on receive overrun */
#define GMF_ASF_RX_OVER_ON	BIT_17	/* enable flushing of ASF when overrun */
#define GMF_ASF_RX_OVER_OFF	BIT_16	/* disable flushing of ASF when overrun */
#define GMF_WP_TST_ON		BIT_14	/* Write Pointer Test On */
#define GMF_WP_TST_OFF		BIT_13	/* Write Pointer Test Off */
#define GMF_WP_STEP		BIT_12	/* Write Pointer Step/Increment */
#define GMF_RP_TST_ON		BIT_10	/* Read Pointer Test On */
#define GMF_RP_TST_OFF		BIT_9	/* Read Pointer Test Off */
#define GMF_RP_STEP		BIT_8	/* Read Pointer Step/Increment */
#define GMF_RX_F_FL_ON		BIT_7	/* Rx FIFO Flush Mode On */
#define GMF_RX_F_FL_OFF		BIT_6	/* Rx FIFO Flush Mode Off */
#define GMF_CLI_RX_FO		BIT_5	/* Clear IRQ Rx FIFO Overrun */
#define GMF_CLI_RX_FC		BIT_4	/* Clear IRQ Rx Frame Complete */
#define GMF_OPER_ON		BIT_3	/* Operational Mode On */
#define GMF_OPER_OFF		BIT_2	/* Operational Mode Off */
#define GMF_RST_CLR		BIT_1	/* Clear GMAC FIFO Reset */
#define GMF_RST_SET		BIT_0	/* Set   GMAC FIFO Reset */

/*	TX_GMF_CTRL_T	32 bit	Tx GMAC FIFO Control/Test (YUKON and Yukon-2) */
#define	TX_STFW_DIS	BIT_31	/* Disable Store & Forward (Yukon-EC Ultra) */
#define	TX_STFW_ENA	BIT_30	/* Enable Store & Forward (Yukon-EC Ultra) */
#define TX_VLAN_TAG_ON	BIT_25	/* enable  VLAN tagging */
#define TX_VLAN_TAG_OFF	BIT_24	/* disable VLAN tagging */
#define	TX_JUMBO_ENA	BIT_23	/* Enable Jumbo Mode (Yukon-EC Ultra) */
#define	TX_JUMBO_DIS	BIT_22	/* Disable Jumbo Mode (Yukon-EC Ultra) */
#define GMF_WSP_TST_ON	BIT_18	/* Write Shadow Pointer Test On */
#define GMF_WSP_TST_OFF	BIT_17	/* Write Shadow Pointer Test Off */
#define GMF_WSP_STEP	BIT_16	/* Write Shadow Pointer Step/Increment */
				/* Bits 15..8: same as for RX_GMF_CTRL_T */
#define GMF_CLI_TX_FU	BIT_6	/* Clear IRQ Tx FIFO Underrun */
#define GMF_CLI_TX_FC	BIT_5	/* Clear IRQ Tx Frame Complete */
#define GMF_CLI_TX_PE	BIT_4	/* Clear IRQ Tx Parity Error */
				/* Bits 3..0: same as for RX_GMF_CTRL_T */

#define GMF_RX_CTRL_DEF		(GMF_OPER_ON | GMF_RX_F_FL_ON)
#define GMF_TX_CTRL_DEF		GMF_OPER_ON

#define RX_GMF_AF_THR_MIN	0x0c	/* Rx GMAC FIFO Almost Full Thresh. min. */
#define RX_GMF_FL_THR_DEF	0x0a	/* Rx GMAC FIFO Flush Threshold default */

/*	GMAC_TI_ST_CTRL	 8 bit	Time Stamp Timer Ctrl Reg (YUKON only) */
#define GMT_ST_START	BIT_2	/* Start Time Stamp Timer */
#define GMT_ST_STOP	BIT_1	/* Stop  Time Stamp Timer */
#define GMT_ST_CLR_IRQ	BIT_0	/* Clear Time Stamp Timer IRQ */

/*	POLL_CTRL	32 bit	Polling Unit control register (Yukon-2 only) */
#define PC_CLR_IRQ_CHK	BIT_5	/* Clear IRQ Check */
#define PC_POLL_RQ	BIT_4	/* Poll Request Start */
#define PC_POLL_OP_ON	BIT_3	/* Operational Mode On */
#define PC_POLL_OP_OFF	BIT_2	/* Operational Mode Off */
#define PC_POLL_RST_CLR	BIT_1	/* Clear Polling Unit Reset (Enable) */
#define PC_POLL_RST_SET	BIT_0	/* Set   Polling Unit Reset */

/* B28_Y2_ASF_STAT_CMD		32 bit	ASF Status and Command Reg */
/* This register is used by the host driver software */
#define Y2_ASF_OS_PRES	BIT_4	/* ASF operation system present */
#define Y2_ASF_RESET	BIT_3	/* ASF system in reset state */
#define Y2_ASF_RUNNING	BIT_2	/* ASF system operational */
#define Y2_ASF_CLR_HSTI	BIT_1	/* Clear ASF IRQ */
#define Y2_ASF_IRQ	BIT_0	/* Issue an IRQ to ASF system */

#define Y2_ASF_UC_STATE	(3<<2)	/* ASF uC State */
#define Y2_ASF_CLK_HALT	0	/* ASF system clock stopped */

/* B28_Y2_ASF_HCU_CCSR	32bit CPU Control and Status Register (Yukon EX) */
#define	Y2_ASF_HCU_CCSR_SMBALERT_MONITOR	BIT_27	/* SMBALERT pin monitor */
#define	Y2_ASF_HCU_CCSR_CPU_SLEEP	BIT_26	/* CPU sleep status */
#define	Y2_ASF_HCU_CCSR_CS_TO		BIT_25	/* Clock Stretching Timeout */
#define	Y2_ASF_HCU_CCSR_WDOG		BIT_24	/* Watchdog Reset */
#define	Y2_ASF_HCU_CCSR_CLR_IRQ_HOST	BIT_17	/* Clear IRQ_HOST */
#define	Y2_ASF_HCU_CCSR_SET_IRQ_HCU	BIT_16	/* Set IRQ_HCU */
#define	Y2_ASF_HCU_CCSR_AHB_RST		BIT_9	/* Reset AHB bridge */
#define	Y2_ASF_HCU_CCSR_CPU_RST_MODE	BIT_8	/* CPU Reset Mode */
#define	Y2_ASF_HCU_CCSR_SET_SYNC_CPU	BIT_5
#define	Y2_ASF_HCU_CCSR_CPU_CLK_DIVIDE1	BIT_4
#define	Y2_ASF_HCU_CCSR_CPU_CLK_DIVIDE0	BIT_3
#define	Y2_ASF_HCU_CCSR_CPU_CLK_DIVIDE_MSK	(BIT_4 | BIT_3)	/* CPU Clock Divide */
#define	Y2_ASF_HCU_CCSR_CPU_CLK_DIVIDE_BASE	BIT_3
#define	Y2_ASF_HCU_CCSR_OS_PRSNT	BIT_2	/* ASF OS Present */
	/* Microcontroller State */
#define	Y2_ASF_HCU_CCSR_UC_STATE_MSK	3
#define	Y2_ASF_HCU_CCSR_UC_STATE_BASE	BIT_0
#define	Y2_ASF_HCU_CCSR_ASF_RESET	0
#define	Y2_ASF_HCU_CCSR_ASF_HALTED	BIT_1
#define	Y2_ASF_HCU_CCSR_ASF_RUNNING	BIT_0

/* B28_Y2_ASF_HOST_COM	32 bit	ASF Host Communication Reg */
/* This register is used by the ASF firmware */
#define Y2_ASF_CLR_ASFI	BIT_1	/* Clear host IRQ */
#define Y2_ASF_HOST_IRQ	BIT_0	/* Issue an IRQ to HOST system */

/*	STAT_CTRL	32 bit	Status BMU control register (Yukon-2 only) */
#define SC_STAT_CLR_IRQ	BIT_4	/* Status Burst IRQ clear */
#define SC_STAT_OP_ON	BIT_3	/* Operational Mode On */
#define SC_STAT_OP_OFF	BIT_2	/* Operational Mode Off */
#define SC_STAT_RST_CLR	BIT_1	/* Clear Status Unit Reset (Enable) */
#define SC_STAT_RST_SET	BIT_0	/* Set   Status Unit Reset */

/*	GMAC_CTRL	32 bit	GMAC Control Reg (YUKON only) */
#define GMC_SEC_RST	BIT_15	/* MAC SEC RST */
#define GMC_SEC_RST_OFF	BIT_14	/* MAC SEC RST Off */
#define GMC_BYP_MACSECRX_ON	BIT_13	/* Bypass MAC SEC RX */
#define GMC_BYP_MACSECRX_OFF	BIT_12	/* Bypass MAC SEC RX Off */
#define GMC_BYP_MACSECTX_ON	BIT_11	/* Bypass MAC SEC TX */
#define GMC_BYP_MACSECTX_OFF	BIT_10	/* Bypass MAC SEC TX Off */
#define GMC_BYP_RETR_ON	BIT_9	/* Bypass MAC retransmit FIFO On */
#define GMC_BYP_RETR_OFF	BIT_8	/* Bypass MAC retransmit FIFO Off */
#define GMC_H_BURST_ON	BIT_7	/* Half Duplex Burst Mode On */
#define GMC_H_BURST_OFF	BIT_6	/* Half Duplex Burst Mode Off */
#define GMC_F_LOOPB_ON	BIT_5	/* FIFO Loopback On */
#define GMC_F_LOOPB_OFF	BIT_4	/* FIFO Loopback Off */
#define GMC_PAUSE_ON	BIT_3	/* Pause On */
#define GMC_PAUSE_OFF	BIT_2	/* Pause Off */
#define GMC_RST_CLR	BIT_1	/* Clear GMAC Reset */
#define GMC_RST_SET	BIT_0	/* Set   GMAC Reset */

/*	GPHY_CTRL	32 bit	GPHY Control Reg (YUKON only) */
#define GPC_SEL_BDT	BIT_28	/* Select Bi-Dir. Transfer for MDC/MDIO */
#define GPC_INT_POL	BIT_27	/* IRQ Polarity is Active Low */
#define GPC_75_OHM	BIT_26	/* Use 75 Ohm Termination instead of 50 */
#define GPC_DIS_FC	BIT_25	/* Disable Automatic Fiber/Copper Detection */
#define GPC_DIS_SLEEP	BIT_24	/* Disable Energy Detect */
#define GPC_HWCFG_M_3	BIT_23	/* HWCFG_MODE[3] */
#define GPC_HWCFG_M_2	BIT_22	/* HWCFG_MODE[2] */
#define GPC_HWCFG_M_1	BIT_21	/* HWCFG_MODE[1] */
#define GPC_HWCFG_M_0	BIT_20	/* HWCFG_MODE[0] */
#define GPC_ANEG_0	BIT_19	/* ANEG[0] */
#define GPC_ENA_XC	BIT_18	/* Enable MDI crossover */
#define GPC_DIS_125	BIT_17	/* Disable 125 MHz clock */
#define GPC_ANEG_3	BIT_16	/* ANEG[3] */
#define GPC_ANEG_2	BIT_15	/* ANEG[2] */
#define GPC_ANEG_1	BIT_14	/* ANEG[1] */
#define GPC_ENA_PAUSE	BIT_13	/* Enable Pause (SYM_OR_REM) */
#define GPC_PHYADDR_4	BIT_12	/* Bit 4 of Phy Addr */
#define GPC_PHYADDR_3	BIT_11	/* Bit 3 of Phy Addr */
#define GPC_PHYADDR_2	BIT_10	/* Bit 2 of Phy Addr */
#define GPC_PHYADDR_1	BIT_9	/* Bit 1 of Phy Addr */
#define GPC_PHYADDR_0	BIT_8	/* Bit 0 of Phy Addr */
#define GPC_RST_CLR	BIT_1	/* Clear GPHY Reset */
#define GPC_RST_SET	BIT_0	/* Set   GPHY Reset */

/*	GMAC_IRQ_SRC	 8 bit	GMAC Interrupt Source Reg (YUKON only) */
/*	GMAC_IRQ_MSK	 8 bit	GMAC Interrupt Mask   Reg (YUKON only) */
#define GM_IS_RX_CO_OV	BIT_5	/* Receive Counter Overflow IRQ */
#define GM_IS_TX_CO_OV	BIT_4	/* Transmit Counter Overflow IRQ */
#define GM_IS_TX_FF_UR	BIT_3	/* Transmit FIFO Underrun */
#define GM_IS_TX_COMPL	BIT_2	/* Frame Transmission Complete */
#define GM_IS_RX_FF_OR	BIT_1	/* Receive FIFO Overrun */
#define GM_IS_RX_COMPL	BIT_0	/* Frame Reception Complete */

#define GMAC_DEF_MSK	(GM_IS_RX_CO_OV | GM_IS_TX_CO_OV | GM_IS_TX_FF_UR)

/*	GMAC_LINK_CTRL	16 bit	GMAC Link Control Reg (YUKON only) */
#define GMLC_RST_CLR	BIT_1	/* Clear GMAC Link Reset */
#define GMLC_RST_SET	BIT_0	/* Set   GMAC Link Reset */

#define MSK_PORT_A	0
#define MSK_PORT_B	1

/* Register access macros */
#define CSR_WRITE_4(sc, reg, val)	\
	bus_write_4((sc)->msk_res[0], (reg), (val))
#define CSR_WRITE_2(sc, reg, val)	\
	bus_write_2((sc)->msk_res[0], (reg), (val))
#define CSR_WRITE_1(sc, reg, val)	\
	bus_write_1((sc)->msk_res[0], (reg), (val))

#define CSR_READ_4(sc, reg)		\
	bus_read_4((sc)->msk_res[0], (reg))
#define CSR_READ_2(sc, reg)		\
	bus_read_2((sc)->msk_res[0], (reg))
#define CSR_READ_1(sc, reg)		\
	bus_read_1((sc)->msk_res[0], (reg))

#define CSR_PCI_WRITE_4(sc, reg, val)	\
	bus_write_4((sc)->msk_res[0], Y2_CFG_SPC + (reg), (val))
#define CSR_PCI_WRITE_2(sc, reg, val)	\
	bus_write_2((sc)->msk_res[0], Y2_CFG_SPC + (reg), (val))
#define CSR_PCI_WRITE_1(sc, reg, val)	\
	bus_write_1((sc)->msk_res[0], Y2_CFG_SPC + (reg), (val))

#define CSR_PCI_READ_4(sc, reg)		\
	bus_read_4((sc)->msk_res[0], Y2_CFG_SPC + (reg))
#define CSR_PCI_READ_2(sc, reg)		\
	bus_read_2((sc)->msk_res[0], Y2_CFG_SPC + (reg))
#define CSR_PCI_READ_1(sc, reg)		\
	bus_read_1((sc)->msk_res[0], Y2_CFG_SPC + (reg))

#define MSK_IF_READ_4(sc_if, reg)	\
	CSR_READ_4((sc_if)->msk_softc, (reg))
#define MSK_IF_READ_2(sc_if, reg)	\
	CSR_READ_2((sc_if)->msk_softc, (reg))
#define MSK_IF_READ_1(sc_if, reg)	\
	CSR_READ_1((sc_if)->msk_softc, (reg))

#define MSK_IF_WRITE_4(sc_if, reg, val)	\
	CSR_WRITE_4((sc_if)->msk_softc, (reg), (val))
#define MSK_IF_WRITE_2(sc_if, reg, val)	\
	CSR_WRITE_2((sc_if)->msk_softc, (reg), (val))
#define MSK_IF_WRITE_1(sc_if, reg, val)	\
	CSR_WRITE_1((sc_if)->msk_softc, (reg), (val))

#define GMAC_REG(port, reg)			\
	((BASE_GMAC_1 + (port) * (BASE_GMAC_2 - BASE_GMAC_1)) | (reg))
#define	GMAC_WRITE_2(sc, port, reg, val)	\
	CSR_WRITE_2((sc), GMAC_REG((port), (reg)), (val))
#define	GMAC_READ_2(sc, port, reg)		\
	CSR_READ_2((sc), GMAC_REG((port), (reg)))

/* GPHY address (bits 15..11 of SMI control reg) */
#define PHY_ADDR_MARV	0

#define MSK_ADDR_LO(x)	((uint64_t) (x) & 0xffffffffUL)
#define MSK_ADDR_HI(x)	((uint64_t) (x) >> 32)

#define	MSK_RING_ALIGN	32768
#define	MSK_STAT_ALIGN	32768

/* Rx descriptor data structure */
struct msk_rx_desc {
	uint32_t	msk_addr;
	uint32_t	msk_control;
};

/* Tx descriptor data structure */
struct msk_tx_desc {
	uint32_t	msk_addr;
	uint32_t	msk_control;
};

/* Status descriptor data structure */
struct msk_stat_desc {
	uint32_t	msk_status;
	uint32_t	msk_control;
};

/* mask and shift value to get Tx async queue status for port 1 */
#define STLE_TXA1_MSKL		0x00000fff
#define STLE_TXA1_SHIFTL	0

/* mask and shift value to get Tx sync queue status for port 1 */
#define STLE_TXS1_MSKL		0x00fff000
#define STLE_TXS1_SHIFTL	12

/* mask and shift value to get Tx async queue status for port 2 */
#define STLE_TXA2_MSKL		0xff000000
#define STLE_TXA2_SHIFTL	24
#define STLE_TXA2_MSKH		0x000f
/* this one shifts up */
#define STLE_TXA2_SHIFTH	8

/* mask and shift value to get Tx sync queue status for port 2 */
#define STLE_TXS2_MSKL		0x00000000
#define STLE_TXS2_SHIFTL	0
#define STLE_TXS2_MSKH		0xfff0
#define STLE_TXS2_SHIFTH	4

/* YUKON-2 bit values */
#define HW_OWNER		0x80000000
#define SW_OWNER		0x00000000

#define PU_PUTIDX_VALID		0x10000000

/* YUKON-2 Control flags */
#define UDPTCP		0x00010000
#define CALSUM		0x00020000
#define WR_SUM		0x00040000
#define INIT_SUM	0x00080000
#define LOCK_SUM	0x00100000
#define INS_VLAN	0x00200000
#define FRC_STAT	0x00400000
#define EOP		0x00800000

#define TX_LOCK		0x01000000
#define BUF_SEND	0x02000000
#define PACKET_SEND	0x04000000

#define NO_WARNING	0x40000000
#define NO_UPDATE	0x80000000

/* YUKON-2 Rx/Tx opcodes defines */
#define OP_TCPWRITE	0x11000000
#define OP_TCPSTART	0x12000000
#define OP_TCPINIT	0x14000000
#define OP_TCPLCK	0x18000000
#define OP_TCPCHKSUM	OP_TCPSTART
#define OP_TCPIS	(OP_TCPINIT | OP_TCPSTART)
#define OP_TCPLW	(OP_TCPLCK | OP_TCPWRITE)
#define OP_TCPLSW	(OP_TCPLCK | OP_TCPSTART | OP_TCPWRITE)
#define OP_TCPLISW	(OP_TCPLCK | OP_TCPINIT | OP_TCPSTART | OP_TCPWRITE)
#define OP_ADDR64	0x21000000
#define OP_VLAN		0x22000000
#define OP_ADDR64VLAN	(OP_ADDR64 | OP_VLAN)
#define OP_LRGLEN	0x24000000
#define OP_LRGLENVLAN	(OP_LRGLEN | OP_VLAN)
#define OP_MSS		0x28000000
#define OP_MSSVLAN	(OP_MSS | OP_VLAN)
#define OP_BUFFER	0x40000000
#define OP_PACKET	0x41000000
#define OP_LARGESEND	0x43000000

/* YUKON-2 STATUS opcodes defines */
#define OP_RXSTAT	0x60000000
#define OP_RXTIMESTAMP	0x61000000
#define OP_RXVLAN	0x62000000
#define OP_RXCHKS	0x64000000
#define OP_RXCHKSVLAN	(OP_RXCHKS | OP_RXVLAN)
#define OP_RXTIMEVLAN	(OP_RXTIMESTAMP | OP_RXVLAN)
#define OP_RSS_HASH	0x65000000
#define OP_TXINDEXLE	0x68000000

/* YUKON-2 SPECIAL opcodes defines */
#define OP_PUTIDX	0x70000000

#define	STLE_OP_MASK	0xff000000
#define	STLE_CSS_MASK	0x00ff0000
#define	STLE_LEN_MASK	0x0000ffff

/* CSS defined in status LE(valid for descriptor V2 format). */
#define	CSS_TCPUDP_CSUM_OK	0x00800000
#define	CSS_UDP			0x00400000
#define	CSS_TCP			0x00200000
#define	CSS_IPFRAG		0x00100000
#define	CSS_IPV6		0x00080000
#define	CSS_IPV4_CSUM_OK	0x00040000
#define	CSS_IPV4		0x00020000
#define	CSS_PORT		0x00010000

/* Descriptor Bit Definition */
/*	TxCtrl		Transmit Buffer Control Field */
/*	RxCtrl		Receive  Buffer Control Field */
#define BMU_OWN		BIT_31	/* OWN bit: 0=host/1=BMU */
#define BMU_STF		BIT_30	/* Start of Frame */
#define BMU_EOF		BIT_29	/* End of Frame */
#define BMU_IRQ_EOB	BIT_28	/* Req "End of Buffer" IRQ */
#define BMU_IRQ_EOF	BIT_27	/* Req "End of Frame" IRQ */
/* TxCtrl specific bits */
#define BMU_STFWD	BIT_26	/* (Tx)	Store & Forward Frame */
#define BMU_NO_FCS	BIT_25	/* (Tx) Disable MAC FCS (CRC) generation */
#define BMU_SW		BIT_24	/* (Tx)	1 bit res. for SW use */
/* RxCtrl specific bits */
#define BMU_DEV_0	BIT_26	/* (Rx)	Transfer data to Dev0 */
#define BMU_STAT_VAL	BIT_25	/* (Rx)	Rx Status Valid */
#define BMU_TIST_VAL	BIT_24	/* (Rx)	Rx TimeStamp Valid */
				/* Bit 23..16:	BMU Check Opcodes */
#define BMU_CHECK	(0x55<<16)	/* Default BMU check */
#define BMU_TCP_CHECK	(0x56<<16)	/* Descr with TCP ext */
#define BMU_UDP_CHECK	(0x57<<16)	/* Descr with UDP ext (YUKON only) */
#define BMU_BBC		0xffff	/* Bit 15.. 0:	Buffer Byte Counter */

/*
 * Controller requires an additional LE op code for 64bit DMA operation.
 * Driver uses fixed number of RX buffers such that this limitation
 * reduces number of available RX buffers with 64bit DMA so double
 * number of RX buffers on platforms that support 64bit DMA. For TX
 * side, controller requires an additional OP_ADDR64 op code if a TX
 * buffer uses different high address value than previously used one.
 * Driver monitors high DMA address change in TX and inserts an
 * OP_ADDR64 op code if the high DMA address is changed.  Driver
 * allocates 50% more total TX buffers on platforms that support 64bit
 * DMA.
 */
#if (BUS_SPACE_MAXADDR > 0xFFFFFFFF)
#define	MSK_64BIT_DMA
#define MSK_TX_RING_CNT		384
#define MSK_RX_RING_CNT		512
#else
#undef	MSK_64BIT_DMA
#define MSK_TX_RING_CNT		256
#define MSK_RX_RING_CNT		256
#endif
#define	MSK_RX_BUF_ALIGN	8
#define MSK_JUMBO_RX_RING_CNT	MSK_RX_RING_CNT
#define MSK_MAXTXSEGS		35
#define	MSK_TSO_MAXSGSIZE	4096
#define	MSK_TSO_MAXSIZE		(65535 + sizeof(struct ether_vlan_header))

/*
 * It seems that the hardware requires extra descriptors(LEs) to offload
 * TCP/UDP checksum, VLAN hardware tag insertion and TSO.
 *
 * 1 descriptor for TCP/UDP checksum offload.
 * 1 descriptor VLAN hardware tag insertion.
 * 1 descriptor for TSO(TCP Segmentation Offload)
 * 1 descriptor for each 64bits DMA transfers 
 */
#ifdef MSK_64BIT_DMA
#define	MSK_RESERVED_TX_DESC_CNT	(MSK_MAXTXSEGS + 3)
#else
#define	MSK_RESERVED_TX_DESC_CNT	3
#endif

#define MSK_JUMBO_FRAMELEN	9022
#define MSK_JUMBO_MTU		(MSK_JUMBO_FRAMELEN-ETHER_HDR_LEN-ETHER_CRC_LEN)
#define MSK_MAX_FRAMELEN		\
	(ETHER_MAX_LEN + ETHER_VLAN_ENCAP_LEN - ETHER_CRC_LEN)
#define MSK_MIN_FRAMELEN	(ETHER_MIN_LEN - ETHER_CRC_LEN)

struct msk_txdesc {
	struct mbuf		*tx_m;
	bus_dmamap_t		tx_dmamap;
	struct msk_tx_desc	*tx_le;
};

struct msk_rxdesc {
	struct mbuf		*rx_m;
	bus_dmamap_t		rx_dmamap;
	struct msk_rx_desc	*rx_le;
};

struct msk_chain_data {
	bus_dma_tag_t		msk_parent_tag;
	bus_dma_tag_t		msk_tx_tag;
	struct msk_txdesc	msk_txdesc[MSK_TX_RING_CNT];
	bus_dma_tag_t		msk_rx_tag;
	struct msk_rxdesc	msk_rxdesc[MSK_RX_RING_CNT];
	bus_dma_tag_t		msk_tx_ring_tag;
	bus_dma_tag_t		msk_rx_ring_tag;
	bus_dmamap_t		msk_tx_ring_map;
	bus_dmamap_t		msk_rx_ring_map;
	bus_dmamap_t		msk_rx_sparemap;
	bus_dma_tag_t		msk_jumbo_rx_tag;
	struct msk_rxdesc	msk_jumbo_rxdesc[MSK_JUMBO_RX_RING_CNT];
	bus_dma_tag_t		msk_jumbo_rx_ring_tag;
	bus_dmamap_t		msk_jumbo_rx_ring_map;
	bus_dmamap_t		msk_jumbo_rx_sparemap;
	uint16_t		msk_tso_mtu;
	uint32_t		msk_last_csum;
	uint32_t		msk_tx_high_addr;
	int			msk_tx_prod;
	int			msk_tx_cons;
	int			msk_tx_cnt;
	int			msk_tx_put;
	int			msk_rx_cons;
	int			msk_rx_prod;
	int			msk_rx_putwm;
};

struct msk_ring_data {
	struct msk_tx_desc	*msk_tx_ring;
	bus_addr_t		msk_tx_ring_paddr;
	struct msk_rx_desc	*msk_rx_ring;
	bus_addr_t		msk_rx_ring_paddr;
	struct msk_rx_desc	*msk_jumbo_rx_ring;
	bus_addr_t		msk_jumbo_rx_ring_paddr;
};

#define MSK_TX_RING_ADDR(sc, i)	\
    ((sc)->msk_rdata.msk_tx_ring_paddr + sizeof(struct msk_tx_desc) * (i))
#define MSK_RX_RING_ADDR(sc, i) \
    ((sc)->msk_rdata.msk_rx_ring_paddr + sizeof(struct msk_rx_desc) * (i))
#define MSK_JUMBO_RX_RING_ADDR(sc, i) \
    ((sc)->msk_rdata.msk_jumbo_rx_ring_paddr + sizeof(struct msk_rx_desc) * (i))

#define MSK_TX_RING_SZ		\
    (sizeof(struct msk_tx_desc) * MSK_TX_RING_CNT)
#define MSK_RX_RING_SZ		\
    (sizeof(struct msk_rx_desc) * MSK_RX_RING_CNT)
#define MSK_JUMBO_RX_RING_SZ		\
    (sizeof(struct msk_rx_desc) * MSK_JUMBO_RX_RING_CNT)

#define MSK_INC(x, y)	(x) = (x + 1) % y
#ifdef MSK_64BIT_DMA
#define MSK_RX_INC(x, y)	(x) = (x + 2) % y
#define MSK_RX_BUF_CNT		(MSK_RX_RING_CNT / 2)
#define MSK_JUMBO_RX_BUF_CNT	(MSK_JUMBO_RX_RING_CNT / 2)
#else
#define MSK_RX_INC(x, y)	(x) = (x + 1) % y
#define MSK_RX_BUF_CNT		MSK_RX_RING_CNT
#define MSK_JUMBO_RX_BUF_CNT	MSK_JUMBO_RX_RING_CNT
#endif

#define	MSK_PCI_BUS	0
#define	MSK_PCIX_BUS	1
#define	MSK_PEX_BUS	2

#define	MSK_PROC_DEFAULT	(MSK_RX_RING_CNT / 2)
#define	MSK_PROC_MIN		30
#define	MSK_PROC_MAX		(MSK_RX_RING_CNT - 1)

#define	MSK_INT_HOLDOFF_DEFAULT	100

#define	MSK_TX_TIMEOUT		5
#define	MSK_PUT_WM	10

struct msk_mii_data {
	int		port;
	uint32_t	pmd;
	int		mii_flags;
};

/* Forward decl. */
struct msk_if_softc;

struct msk_hw_stats {
	/* Rx stats. */
	uint32_t rx_ucast_frames;
	uint32_t rx_bcast_frames;
	uint32_t rx_pause_frames;
	uint32_t rx_mcast_frames;
	uint32_t rx_crc_errs;
	uint32_t rx_spare1;
	uint64_t rx_good_octets;
	uint64_t rx_bad_octets;
	uint32_t rx_runts;
	uint32_t rx_runt_errs;
	uint32_t rx_pkts_64;
	uint32_t rx_pkts_65_127;
	uint32_t rx_pkts_128_255;
	uint32_t rx_pkts_256_511;
	uint32_t rx_pkts_512_1023;
	uint32_t rx_pkts_1024_1518;
	uint32_t rx_pkts_1519_max;
	uint32_t rx_pkts_too_long;
	uint32_t rx_pkts_jabbers;
	uint32_t rx_spare2;
	uint32_t rx_fifo_oflows;
	uint32_t rx_spare3;
	/* Tx stats. */
	uint32_t tx_ucast_frames;
	uint32_t tx_bcast_frames;
	uint32_t tx_pause_frames;
	uint32_t tx_mcast_frames;
	uint64_t tx_octets;
	uint32_t tx_pkts_64;
	uint32_t tx_pkts_65_127;
	uint32_t tx_pkts_128_255;
	uint32_t tx_pkts_256_511;
	uint32_t tx_pkts_512_1023;
	uint32_t tx_pkts_1024_1518;
	uint32_t tx_pkts_1519_max;
	uint32_t tx_spare1;
	uint32_t tx_colls;
	uint32_t tx_late_colls;
	uint32_t tx_excess_colls;
	uint32_t tx_multi_colls;
	uint32_t tx_single_colls;
	uint32_t tx_underflows;
};

/* Softc for the Marvell Yukon II controller. */
struct msk_softc {
	struct resource		*msk_res[1];	/* I/O resource */
	struct resource_spec	*msk_res_spec;
	struct resource		*msk_irq[1];	/* IRQ resources */
	struct resource_spec	*msk_irq_spec;
	void			*msk_intrhand; /* irq handler handle */
	device_t		msk_dev;
	uint8_t			msk_hw_id;
	uint8_t			msk_hw_rev;
	uint8_t			msk_bustype;
	uint8_t			msk_num_port;
	int			msk_expcap;
	int			msk_pcixcap;
	int			msk_ramsize;	/* amount of SRAM on NIC */
	uint32_t		msk_pmd;	/* physical media type */
	uint32_t		msk_intrmask;
	uint32_t		msk_intrhwemask;
	uint32_t		msk_pflags;
	int			msk_clock;
	struct msk_if_softc	*msk_if[2];
	device_t		msk_devs[2];
	int			msk_txqsize;
	int			msk_rxqsize;
	int			msk_txqstart[2];
	int			msk_txqend[2];
	int			msk_rxqstart[2];
	int			msk_rxqend[2];
	bus_dma_tag_t		msk_stat_tag;
	bus_dmamap_t		msk_stat_map;
	struct msk_stat_desc	*msk_stat_ring;
	bus_addr_t		msk_stat_ring_paddr;
	int			msk_int_holdoff;
	int			msk_process_limit;
	int			msk_stat_cons;
	int			msk_stat_count;
	struct mtx		msk_mtx;
};

#define	MSK_LOCK(_sc)		mtx_lock(&(_sc)->msk_mtx)
#define	MSK_UNLOCK(_sc)		mtx_unlock(&(_sc)->msk_mtx)
#define	MSK_LOCK_ASSERT(_sc)	mtx_assert(&(_sc)->msk_mtx, MA_OWNED)
#define	MSK_IF_LOCK(_sc)	MSK_LOCK((_sc)->msk_softc)
#define	MSK_IF_UNLOCK(_sc)	MSK_UNLOCK((_sc)->msk_softc)
#define	MSK_IF_LOCK_ASSERT(_sc)	MSK_LOCK_ASSERT((_sc)->msk_softc)

#define	MSK_USECS(sc, us)	((sc)->msk_clock * (us))

/* Softc for each logical interface. */
struct msk_if_softc {
	struct ifnet		*msk_ifp;	/* interface info */
	device_t		msk_miibus;
	device_t		msk_if_dev;
	int32_t			msk_port;	/* port # on controller */
	int			msk_framesize;
	int			msk_phytype;
	int			msk_phyaddr;
	uint32_t		msk_flags;
#define	MSK_FLAG_MSI		0x0001
#define	MSK_FLAG_FASTETHER	0x0004
#define	MSK_FLAG_JUMBO		0x0008
#define	MSK_FLAG_JUMBO_NOCSUM	0x0010
#define	MSK_FLAG_RAMBUF		0x0020
#define	MSK_FLAG_DESCV2		0x0040
#define	MSK_FLAG_AUTOTX_CSUM	0x0080
#define	MSK_FLAG_NOHWVLAN	0x0100
#define	MSK_FLAG_NORXCHK	0x0200
#define	MSK_FLAG_NORX_CSUM	0x0400
#define	MSK_FLAG_SUSPEND	0x2000
#define	MSK_FLAG_DETACH		0x4000
#define	MSK_FLAG_LINK		0x8000
	struct callout		msk_tick_ch;
	int			msk_watchdog_timer;
	uint32_t		msk_txq;	/* Tx. Async Queue offset */
	uint32_t		msk_txsq;	/* Tx. Syn Queue offset */
	uint32_t		msk_rxq;	/* Rx. Qeueue offset */
	struct msk_chain_data	msk_cdata;
	struct msk_ring_data	msk_rdata;
	struct msk_softc	*msk_softc;	/* parent controller */
	struct msk_hw_stats	msk_stats;
	int			msk_if_flags;
	uint16_t		msk_vtag;	/* VLAN tag id. */
	uint32_t		msk_csum;
};

#define MSK_TIMEOUT	1000
#define	MSK_PHY_POWERUP		1
#define	MSK_PHY_POWERDOWN	0
