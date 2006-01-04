/*
 *  linux/include/asm-arm/arch-omap/dma.h
 *
 *  Copyright (C) 2003 Nokia Corporation
 *  Author: Juha Yrjölä <juha.yrjola@nokia.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */
#ifndef __ASM_ARCH_DMA_H
#define __ASM_ARCH_DMA_H

/* Hardware registers for omap1 */
#define OMAP_DMA_BASE			(0xfffed800)
#define OMAP_DMA_GCR			(OMAP_DMA_BASE + 0x400)
#define OMAP_DMA_GSCR			(OMAP_DMA_BASE + 0x404)
#define OMAP_DMA_GRST			(OMAP_DMA_BASE + 0x408)
#define OMAP_DMA_HW_ID			(OMAP_DMA_BASE + 0x442)
#define OMAP_DMA_PCH2_ID		(OMAP_DMA_BASE + 0x444)
#define OMAP_DMA_PCH0_ID		(OMAP_DMA_BASE + 0x446)
#define OMAP_DMA_PCH1_ID		(OMAP_DMA_BASE + 0x448)
#define OMAP_DMA_PCHG_ID		(OMAP_DMA_BASE + 0x44a)
#define OMAP_DMA_PCHD_ID		(OMAP_DMA_BASE + 0x44c)
#define OMAP_DMA_CAPS_0_U		(OMAP_DMA_BASE + 0x44e)
#define OMAP_DMA_CAPS_0_L		(OMAP_DMA_BASE + 0x450)
#define OMAP_DMA_CAPS_1_U		(OMAP_DMA_BASE + 0x452)
#define OMAP_DMA_CAPS_1_L		(OMAP_DMA_BASE + 0x454)
#define OMAP_DMA_CAPS_2			(OMAP_DMA_BASE + 0x456)
#define OMAP_DMA_CAPS_3			(OMAP_DMA_BASE + 0x458)
#define OMAP_DMA_CAPS_4			(OMAP_DMA_BASE + 0x45a)
#define OMAP_DMA_PCH2_SR		(OMAP_DMA_BASE + 0x460)
#define OMAP_DMA_PCH0_SR		(OMAP_DMA_BASE + 0x480)
#define OMAP_DMA_PCH1_SR		(OMAP_DMA_BASE + 0x482)
#define OMAP_DMA_PCHD_SR		(OMAP_DMA_BASE + 0x4c0)

/* Hardware registers for omap2 */
#define OMAP24XX_DMA_BASE		(L4_24XX_BASE + 0x56000)
#define OMAP_DMA4_REVISION		(OMAP24XX_DMA_BASE + 0x00)
#define OMAP_DMA4_GCR_REG		(OMAP24XX_DMA_BASE + 0x78)
#define OMAP_DMA4_IRQSTATUS_L0		(OMAP24XX_DMA_BASE + 0x08)
#define OMAP_DMA4_IRQSTATUS_L1		(OMAP24XX_DMA_BASE + 0x0c)
#define OMAP_DMA4_IRQSTATUS_L2		(OMAP24XX_DMA_BASE + 0x10)
#define OMAP_DMA4_IRQSTATUS_L3		(OMAP24XX_DMA_BASE + 0x14)
#define OMAP_DMA4_IRQENABLE_L0		(OMAP24XX_DMA_BASE + 0x18)
#define OMAP_DMA4_IRQENABLE_L1		(OMAP24XX_DMA_BASE + 0x1c)
#define OMAP_DMA4_IRQENABLE_L2		(OMAP24XX_DMA_BASE + 0x20)
#define OMAP_DMA4_IRQENABLE_L3		(OMAP24XX_DMA_BASE + 0x24)
#define OMAP_DMA4_SYSSTATUS		(OMAP24XX_DMA_BASE + 0x28)
#define OMAP_DMA4_CAPS_0		(OMAP24XX_DMA_BASE + 0x64)
#define OMAP_DMA4_CAPS_2		(OMAP24XX_DMA_BASE + 0x6c)
#define OMAP_DMA4_CAPS_3		(OMAP24XX_DMA_BASE + 0x70)
#define OMAP_DMA4_CAPS_4		(OMAP24XX_DMA_BASE + 0x74)

#ifdef CONFIG_ARCH_OMAP1

#define OMAP_LOGICAL_DMA_CH_COUNT	17

/* Common channel specific registers for omap1 */
#define OMAP_DMA_CSDP_REG(n)		__REG16(OMAP_DMA_BASE + 0x40 * (n) + 0x00)
#define OMAP_DMA_CCR_REG(n)		__REG16(OMAP_DMA_BASE + 0x40 * (n) + 0x02)
#define OMAP_DMA_CICR_REG(n)		__REG16(OMAP_DMA_BASE + 0x40 * (n) + 0x04)
#define OMAP_DMA_CSR_REG(n)		__REG16(OMAP_DMA_BASE + 0x40 * (n) + 0x06)
#define OMAP_DMA_CEN_REG(n)		__REG16(OMAP_DMA_BASE + 0x40 * (n) + 0x10)
#define OMAP_DMA_CFN_REG(n)		__REG16(OMAP_DMA_BASE + 0x40 * (n) + 0x12)
#define OMAP_DMA_CSFI_REG(n)		__REG16(OMAP_DMA_BASE + 0x40 * (n) + 0x14)
#define OMAP_DMA_CSEI_REG(n)		__REG16(OMAP_DMA_BASE + 0x40 * (n) + 0x16)
#define OMAP_DMA_CSAC_REG(n)		__REG16(OMAP_DMA_BASE + 0x40 * (n) + 0x18)
#define OMAP_DMA_CDAC_REG(n)		__REG16(OMAP_DMA_BASE + 0x40 * (n) + 0x1a)
#define OMAP_DMA_CDEI_REG(n)		__REG16(OMAP_DMA_BASE + 0x40 * (n) + 0x1c)
#define OMAP_DMA_CDFI_REG(n)		__REG16(OMAP_DMA_BASE + 0x40 * (n) + 0x1e)
#define OMAP_DMA_CLNK_CTRL_REG(n)	__REG16(OMAP_DMA_BASE + 0x40 * (n) + 0x28)

#else

#define OMAP_LOGICAL_DMA_CH_COUNT	32	/* REVISIT: Is this 32 + 2? */

/* Common channel specific registers for omap2 */
#define OMAP_DMA_CCR_REG(n)		__REG32(OMAP24XX_DMA_BASE + 0x60 * (n) + 0x80)
#define OMAP_DMA_CLNK_CTRL_REG(n)	__REG32(OMAP24XX_DMA_BASE + 0x60 * (n) + 0x84)
#define OMAP_DMA_CICR_REG(n)		__REG32(OMAP24XX_DMA_BASE + 0x60 * (n) + 0x88)
#define OMAP_DMA_CSR_REG(n)		__REG32(OMAP24XX_DMA_BASE + 0x60 * (n) + 0x8c)
#define OMAP_DMA_CSDP_REG(n)		__REG32(OMAP24XX_DMA_BASE + 0x60 * (n) + 0x90)
#define OMAP_DMA_CEN_REG(n)		__REG32(OMAP24XX_DMA_BASE + 0x60 * (n) + 0x94)
#define OMAP_DMA_CFN_REG(n)		__REG32(OMAP24XX_DMA_BASE + 0x60 * (n) + 0x98)
#define OMAP_DMA_CSEI_REG(n)		__REG32(OMAP24XX_DMA_BASE + 0x60 * (n) + 0xa4)
#define OMAP_DMA_CSFI_REG(n)		__REG32(OMAP24XX_DMA_BASE + 0x60 * (n) + 0xa8)
#define OMAP_DMA_CDEI_REG(n)		__REG32(OMAP24XX_DMA_BASE + 0x60 * (n) + 0xac)
#define OMAP_DMA_CDFI_REG(n)		__REG32(OMAP24XX_DMA_BASE + 0x60 * (n) + 0xb0)
#define OMAP_DMA_CSAC_REG(n)		__REG32(OMAP24XX_DMA_BASE + 0x60 * (n) + 0xb4)
#define OMAP_DMA_CDAC_REG(n)		__REG32(OMAP24XX_DMA_BASE + 0x60 * (n) + 0xb8)

#endif

/* Channel specific registers only on omap1 */
#define OMAP1_DMA_CSSA_L_REG(n)		__REG16(OMAP_DMA_BASE + 0x40 * (n) + 0x08)
#define OMAP1_DMA_CSSA_U_REG(n)		__REG16(OMAP_DMA_BASE + 0x40 * (n) + 0x0a)
#define OMAP1_DMA_CDSA_L_REG(n)		__REG16(OMAP_DMA_BASE + 0x40 * (n) + 0x0c)
#define OMAP1_DMA_CDSA_U_REG(n)		__REG16(OMAP_DMA_BASE + 0x40 * (n) + 0x0e)
#define OMAP1_DMA_COLOR_L_REG(n)	__REG16(OMAP_DMA_BASE + 0x40 * (n) + 0x20)
#define OMAP1_DMA_CCR2_REG(n)		__REG16(OMAP_DMA_BASE + 0x40 * (n) + 0x24)
#define OMAP1_DMA_COLOR_U_REG(n)	__REG16(OMAP_DMA_BASE + 0x40 * (n) + 0x22)
#define OMAP1_DMA_LCH_CTRL_REG(n)	__REG16(OMAP_DMA_BASE + 0x40 * (n) + 0x2a)

/* Channel specific registers only on omap2 */
#define OMAP2_DMA_CSSA_REG(n)		__REG32(OMAP24XX_DMA_BASE + 0x60 * (n) + 0x9c)
#define OMAP2_DMA_CDSA_REG(n)		__REG32(OMAP24XX_DMA_BASE + 0x60 * (n) + 0xa0)
#define OMAP2_DMA_CCEN_REG(n)		__REG32(OMAP24XX_DMA_BASE + 0x60 * (n) + 0xbc)
#define OMAP2_DMA_CCFN_REG(n)		__REG32(OMAP24XX_DMA_BASE + 0x60 * (n) + 0xc0)
#define OMAP2_DMA_COLOR_REG(n)		__REG32(OMAP24XX_DMA_BASE + 0x60 * (n) + 0xc4)

/*----------------------------------------------------------------------------*/

/* DMA channels for omap1 */
#define OMAP_DMA_NO_DEVICE		0
#define OMAP_DMA_MCSI1_TX		1
#define OMAP_DMA_MCSI1_RX		2
#define OMAP_DMA_I2C_RX			3
#define OMAP_DMA_I2C_TX			4
#define OMAP_DMA_EXT_NDMA_REQ		5
#define OMAP_DMA_EXT_NDMA_REQ2		6
#define OMAP_DMA_UWIRE_TX		7
#define OMAP_DMA_MCBSP1_TX		8
#define OMAP_DMA_MCBSP1_RX		9
#define OMAP_DMA_MCBSP3_TX		10
#define OMAP_DMA_MCBSP3_RX		11
#define OMAP_DMA_UART1_TX		12
#define OMAP_DMA_UART1_RX		13
#define OMAP_DMA_UART2_TX		14
#define OMAP_DMA_UART2_RX		15
#define OMAP_DMA_MCBSP2_TX		16
#define OMAP_DMA_MCBSP2_RX		17
#define OMAP_DMA_UART3_TX		18
#define OMAP_DMA_UART3_RX		19
#define OMAP_DMA_CAMERA_IF_RX		20
#define OMAP_DMA_MMC_TX			21
#define OMAP_DMA_MMC_RX			22
#define OMAP_DMA_NAND			23
#define OMAP_DMA_IRQ_LCD_LINE		24
#define OMAP_DMA_MEMORY_STICK		25
#define OMAP_DMA_USB_W2FC_RX0		26
#define OMAP_DMA_USB_W2FC_RX1		27
#define OMAP_DMA_USB_W2FC_RX2		28
#define OMAP_DMA_USB_W2FC_TX0		29
#define OMAP_DMA_USB_W2FC_TX1		30
#define OMAP_DMA_USB_W2FC_TX2		31

/* These are only for 1610 */
#define OMAP_DMA_CRYPTO_DES_IN		32
#define OMAP_DMA_SPI_TX			33
#define OMAP_DMA_SPI_RX			34
#define OMAP_DMA_CRYPTO_HASH		35
#define OMAP_DMA_CCP_ATTN		36
#define OMAP_DMA_CCP_FIFO_NOT_EMPTY	37
#define OMAP_DMA_CMT_APE_TX_CHAN_0	38
#define OMAP_DMA_CMT_APE_RV_CHAN_0	39
#define OMAP_DMA_CMT_APE_TX_CHAN_1	40
#define OMAP_DMA_CMT_APE_RV_CHAN_1	41
#define OMAP_DMA_CMT_APE_TX_CHAN_2	42
#define OMAP_DMA_CMT_APE_RV_CHAN_2	43
#define OMAP_DMA_CMT_APE_TX_CHAN_3	44
#define OMAP_DMA_CMT_APE_RV_CHAN_3	45
#define OMAP_DMA_CMT_APE_TX_CHAN_4	46
#define OMAP_DMA_CMT_APE_RV_CHAN_4	47
#define OMAP_DMA_CMT_APE_TX_CHAN_5	48
#define OMAP_DMA_CMT_APE_RV_CHAN_5	49
#define OMAP_DMA_CMT_APE_TX_CHAN_6	50
#define OMAP_DMA_CMT_APE_RV_CHAN_6	51
#define OMAP_DMA_CMT_APE_TX_CHAN_7	52
#define OMAP_DMA_CMT_APE_RV_CHAN_7	53
#define OMAP_DMA_MMC2_TX		54
#define OMAP_DMA_MMC2_RX		55
#define OMAP_DMA_CRYPTO_DES_OUT		56

/* DMA channels for 24xx */
#define OMAP24XX_DMA_NO_DEVICE		0
#define OMAP24XX_DMA_XTI_DMA		1	/* S_DMA_0 */
#define OMAP24XX_DMA_EXT_NDMA_REQ0	2	/* S_DMA_1 */
#define OMAP24XX_DMA_EXT_NDMA_REQ1	3	/* S_DMA_2 */
#define OMAP24XX_DMA_GPMC		4	/* S_DMA_3 */
#define OMAP24XX_DMA_GFX		5	/* S_DMA_4 */
#define OMAP24XX_DMA_DSS		6	/* S_DMA_5 */
#define OMAP24XX_DMA_VLYNQ_TX		7	/* S_DMA_6 */
#define OMAP24XX_DMA_CWT		8	/* S_DMA_7 */
#define OMAP24XX_DMA_AES_TX		9	/* S_DMA_8 */
#define OMAP24XX_DMA_AES_RX		10	/* S_DMA_9 */
#define OMAP24XX_DMA_DES_TX		11	/* S_DMA_10 */
#define OMAP24XX_DMA_DES_RX		12	/* S_DMA_11 */
#define OMAP24XX_DMA_SHA1MD5_RX		13	/* S_DMA_12 */

#define OMAP24XX_DMA_EAC_AC_RD		17	/* S_DMA_16 */
#define OMAP24XX_DMA_EAC_AC_WR		18	/* S_DMA_17 */
#define OMAP24XX_DMA_EAC_MD_UL_RD	19	/* S_DMA_18 */
#define OMAP24XX_DMA_EAC_MD_UL_WR	20	/* S_DMA_19 */
#define OMAP24XX_DMA_EAC_MD_DL_RD	21	/* S_DMA_20 */
#define OMAP24XX_DMA_EAC_MD_DL_WR	22	/* S_DMA_21 */
#define OMAP24XX_DMA_EAC_BT_UL_RD	23	/* S_DMA_22 */
#define OMAP24XX_DMA_EAC_BT_UL_WR	24	/* S_DMA_23 */
#define OMAP24XX_DMA_EAC_BT_DL_RD	25	/* S_DMA_24 */
#define OMAP24XX_DMA_EAC_BT_DL_WR	26	/* S_DMA_25 */
#define OMAP24XX_DMA_I2C1_TX		27	/* S_DMA_26 */
#define OMAP24XX_DMA_I2C1_RX		28	/* S_DMA_27 */
#define OMAP24XX_DMA_I2C2_TX		29	/* S_DMA_28 */
#define OMAP24XX_DMA_I2C2_RX		30	/* S_DMA_29 */
#define OMAP24XX_DMA_MCBSP1_TX		31	/* SDMA_30 */
#define OMAP24XX_DMA_MCBSP1_RX		32	/* SDMA_31 */
#define OMAP24XX_DMA_MCBSP2_TX		33	/* SDMA_32 */
#define OMAP24XX_DMA_MCBSP2_RX		34	/* SDMA_33 */
#define OMAP24XX_DMA_SPI1_TX0		35	/* SDMA_34 */
#define OMAP24XX_DMA_SPI1_RX0		36	/* SDMA_35 */
#define OMAP24XX_DMA_SPI1_TX1		37	/* SDMA_36 */
#define OMAP24XX_DMA_SPI1_RX1		38	/* SDMA_37 */
#define OMAP24XX_DMA_SPI1_TX2		39	/* SDMA_38 */
#define OMAP24XX_DMA_SPI1_RX2		40	/* SDMA_39 */
#define OMAP24XX_DMA_SPI1_TX3		41	/* SDMA_40 */
#define OMAP24XX_DMA_SPI1_RX3		42	/* SDMA_41 */
#define OMAP24XX_DMA_SPI2_TX0		43	/* SDMA_42 */
#define OMAP24XX_DMA_SPI2_RX0		44	/* SDMA_43 */
#define OMAP24XX_DMA_SPI2_TX1		45	/* SDMA_44 */
#define OMAP24XX_DMA_SPI2_RX1		46	/* SDMA_45 */

#define OMAP24XX_DMA_UART1_TX		49	/* SDMA_48 */
#define OMAP24XX_DMA_UART1_RX		50	/* SDMA_49 */
#define OMAP24XX_DMA_UART2_TX		51	/* SDMA_50 */
#define OMAP24XX_DMA_UART2_RX		52	/* SDMA_51 */
#define OMAP24XX_DMA_UART3_TX		53	/* SDMA_52 */
#define OMAP24XX_DMA_UART3_RX		54	/* SDMA_53 */
#define OMAP24XX_DMA_USB_W2FC_TX0	55	/* SDMA_54 */
#define OMAP24XX_DMA_USB_W2FC_RX0	56	/* SDMA_55 */
#define OMAP24XX_DMA_USB_W2FC_TX1	57	/* SDMA_56 */
#define OMAP24XX_DMA_USB_W2FC_RX1	58	/* SDMA_57 */
#define OMAP24XX_DMA_USB_W2FC_TX2	59	/* SDMA_58 */
#define OMAP24XX_DMA_USB_W2FC_RX2	60	/* SDMA_59 */
#define OMAP24XX_DMA_MMC1_TX		61	/* SDMA_60 */
#define OMAP24XX_DMA_MMC1_RX		62	/* SDMA_61 */
#define OMAP24XX_DMA_MS			63	/* SDMA_62 */

/*----------------------------------------------------------------------------*/

/* Hardware registers for LCD DMA */
#define OMAP1510_DMA_LCD_BASE		(0xfffedb00)
#define OMAP1510_DMA_LCD_CTRL		(OMAP1510_DMA_LCD_BASE + 0x00)
#define OMAP1510_DMA_LCD_TOP_F1_L	(OMAP1510_DMA_LCD_BASE + 0x02)
#define OMAP1510_DMA_LCD_TOP_F1_U	(OMAP1510_DMA_LCD_BASE + 0x04)
#define OMAP1510_DMA_LCD_BOT_F1_L	(OMAP1510_DMA_LCD_BASE + 0x06)
#define OMAP1510_DMA_LCD_BOT_F1_U	(OMAP1510_DMA_LCD_BASE + 0x08)

#define OMAP1610_DMA_LCD_BASE		(0xfffee300)
#define OMAP1610_DMA_LCD_CSDP		(OMAP1610_DMA_LCD_BASE + 0xc0)
#define OMAP1610_DMA_LCD_CCR		(OMAP1610_DMA_LCD_BASE + 0xc2)
#define OMAP1610_DMA_LCD_CTRL		(OMAP1610_DMA_LCD_BASE + 0xc4)
#define OMAP1610_DMA_LCD_TOP_B1_L	(OMAP1610_DMA_LCD_BASE + 0xc8)
#define OMAP1610_DMA_LCD_TOP_B1_U	(OMAP1610_DMA_LCD_BASE + 0xca)
#define OMAP1610_DMA_LCD_BOT_B1_L	(OMAP1610_DMA_LCD_BASE + 0xcc)
#define OMAP1610_DMA_LCD_BOT_B1_U	(OMAP1610_DMA_LCD_BASE + 0xce)
#define OMAP1610_DMA_LCD_TOP_B2_L	(OMAP1610_DMA_LCD_BASE + 0xd0)
#define OMAP1610_DMA_LCD_TOP_B2_U	(OMAP1610_DMA_LCD_BASE + 0xd2)
#define OMAP1610_DMA_LCD_BOT_B2_L	(OMAP1610_DMA_LCD_BASE + 0xd4)
#define OMAP1610_DMA_LCD_BOT_B2_U	(OMAP1610_DMA_LCD_BASE + 0xd6)
#define OMAP1610_DMA_LCD_SRC_EI_B1	(OMAP1610_DMA_LCD_BASE + 0xd8)
#define OMAP1610_DMA_LCD_SRC_FI_B1_L	(OMAP1610_DMA_LCD_BASE + 0xda)
#define OMAP1610_DMA_LCD_SRC_EN_B1	(OMAP1610_DMA_LCD_BASE + 0xe0)
#define OMAP1610_DMA_LCD_SRC_FN_B1	(OMAP1610_DMA_LCD_BASE + 0xe4)
#define OMAP1610_DMA_LCD_LCH_CTRL	(OMAP1610_DMA_LCD_BASE + 0xea)
#define OMAP1610_DMA_LCD_SRC_FI_B1_U	(OMAP1610_DMA_LCD_BASE + 0xf4)

#define OMAP_DMA_TOUT_IRQ		(1 << 0)	/* Only on omap1 */
#define OMAP_DMA_DROP_IRQ		(1 << 1)
#define OMAP_DMA_HALF_IRQ		(1 << 2)
#define OMAP_DMA_FRAME_IRQ		(1 << 3)
#define OMAP_DMA_LAST_IRQ		(1 << 4)
#define OMAP_DMA_BLOCK_IRQ		(1 << 5)
#define OMAP1_DMA_SYNC_IRQ		(1 << 6)
#define OMAP2_DMA_PKT_IRQ		(1 << 7)
#define OMAP2_DMA_TRANS_ERR_IRQ		(1 << 8)
#define OMAP2_DMA_SECURE_ERR_IRQ	(1 << 9)
#define OMAP2_DMA_SUPERVISOR_ERR_IRQ	(1 << 10)
#define OMAP2_DMA_MISALIGNED_ERR_IRQ	(1 << 11)

#define OMAP_DMA_DATA_TYPE_S8		0x00
#define OMAP_DMA_DATA_TYPE_S16		0x01
#define OMAP_DMA_DATA_TYPE_S32		0x02

#define OMAP_DMA_SYNC_ELEMENT		0x00
#define OMAP_DMA_SYNC_FRAME		0x01
#define OMAP_DMA_SYNC_BLOCK		0x02

#define OMAP_DMA_PORT_EMIFF		0x00
#define OMAP_DMA_PORT_EMIFS		0x01
#define OMAP_DMA_PORT_OCP_T1		0x02
#define OMAP_DMA_PORT_TIPB		0x03
#define OMAP_DMA_PORT_OCP_T2		0x04
#define OMAP_DMA_PORT_MPUI		0x05

#define OMAP_DMA_AMODE_CONSTANT		0x00
#define OMAP_DMA_AMODE_POST_INC		0x01
#define OMAP_DMA_AMODE_SINGLE_IDX	0x02
#define OMAP_DMA_AMODE_DOUBLE_IDX	0x03

/* LCD DMA block numbers */
enum {
	OMAP_LCD_DMA_B1_TOP,
	OMAP_LCD_DMA_B1_BOTTOM,
	OMAP_LCD_DMA_B2_TOP,
	OMAP_LCD_DMA_B2_BOTTOM
};

/* REVISIT: Check if BURST_4 is really 1 (or 2) */
enum omap_dma_burst_mode {
	OMAP_DMA_DATA_BURST_DIS = 0,
	OMAP_DMA_DATA_BURST_4,
	OMAP_DMA_DATA_BURST_8
};

enum omap_dma_color_mode {
	OMAP_DMA_COLOR_DIS = 0,
	OMAP_DMA_CONSTANT_FILL,
	OMAP_DMA_TRANSPARENT_COPY
};

struct omap_dma_channel_params {
	int data_type;		/* data type 8,16,32 */
	int elem_count;		/* number of elements in a frame */
	int frame_count;	/* number of frames in a element */

	int src_port;		/* Only on OMAP1 REVISIT: Is this needed? */
	int src_amode;		/* constant , post increment, indexed , double indexed */
	int src_start;		/* source address : physical */
	int src_ei;		/* source element index */
	int src_fi;		/* source frame index */

	int dst_port;		/* Only on OMAP1 REVISIT: Is this needed? */
	int dst_amode;		/* constant , post increment, indexed , double indexed */
	int dst_start;		/* source address : physical */
	int dst_ei;		/* source element index */
	int dst_fi;		/* source frame index */

	int trigger;		/* trigger attached if the channel is synchronized */
	int sync_mode;		/* sycn on element, frame , block or packet */
	int src_or_dst_synch;	/* source synch(1) or destination synch(0) */

	int ie;			/* interrupt enabled */
};


extern void omap_set_dma_priority(int dst_port, int priority);
extern int omap_request_dma(int dev_id, const char *dev_name,
			    void (* callback)(int lch, u16 ch_status, void *data),
			    void *data, int *dma_ch);
extern void omap_enable_dma_irq(int ch, u16 irq_bits);
extern void omap_disable_dma_irq(int ch, u16 irq_bits);
extern void omap_free_dma(int ch);
extern void omap_start_dma(int lch);
extern void omap_stop_dma(int lch);
extern void omap_set_dma_transfer_params(int lch, int data_type,
					 int elem_count, int frame_count,
					 int sync_mode,
					 int dma_trigger, int src_or_dst_synch);
extern void omap_set_dma_color_mode(int lch, enum omap_dma_color_mode mode,
				    u32 color);

extern void omap_set_dma_src_params(int lch, int src_port, int src_amode,
				    unsigned long src_start,
				    int src_ei, int src_fi);
extern void omap_set_dma_src_index(int lch, int eidx, int fidx);
extern void omap_set_dma_src_data_pack(int lch, int enable);
extern void omap_set_dma_src_burst_mode(int lch,
					enum omap_dma_burst_mode burst_mode);

extern void omap_set_dma_dest_params(int lch, int dest_port, int dest_amode,
				     unsigned long dest_start,
				     int dst_ei, int dst_fi);
extern void omap_set_dma_dest_index(int lch, int eidx, int fidx);
extern void omap_set_dma_dest_data_pack(int lch, int enable);
extern void omap_set_dma_dest_burst_mode(int lch,
					 enum omap_dma_burst_mode burst_mode);

extern void omap_set_dma_params(int lch,
				struct omap_dma_channel_params * params);

extern void omap_dma_link_lch (int lch_head, int lch_queue);
extern void omap_dma_unlink_lch (int lch_head, int lch_queue);

extern dma_addr_t omap_get_dma_src_pos(int lch);
extern dma_addr_t omap_get_dma_dst_pos(int lch);
extern int omap_get_dma_src_addr_counter(int lch);
extern void omap_clear_dma(int lch);
extern int omap_dma_running(void);

/* LCD DMA functions */
extern int omap_request_lcd_dma(void (* callback)(u16 status, void *data),
				void *data);
extern void omap_free_lcd_dma(void);
extern void omap_setup_lcd_dma(void);
extern void omap_enable_lcd_dma(void);
extern void omap_stop_lcd_dma(void);
extern void omap_set_lcd_dma_ext_controller(int external);
extern void omap_set_lcd_dma_single_transfer(int single);
extern void omap_set_lcd_dma_b1(unsigned long addr, u16 fb_xres, u16 fb_yres,
				int data_type);
extern void omap_set_lcd_dma_b1_rotation(int rotate);
extern void omap_set_lcd_dma_b1_vxres(unsigned long vxres);
extern void omap_set_lcd_dma_b1_mirror(int mirror);
extern void omap_set_lcd_dma_b1_scale(unsigned int xscale, unsigned int yscale);

#endif /* __ASM_ARCH_DMA_H */
