/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * ALSA SoC Audio Layer - Rockchip SAI Controller driver
 *
 * Copyright (c) 2022 Rockchip Electronics Co. Ltd.
 */

#ifndef _ROCKCHIP_SAI_H
#define _ROCKCHIP_SAI_H

/* XCR Transmit / Receive Control Register */
#define SAI_XCR_EDGE_SHIFT_MASK		BIT(22)
#define SAI_XCR_EDGE_SHIFT_1		BIT(22)
#define SAI_XCR_EDGE_SHIFT_0		0
#define SAI_XCR_CSR_MASK		GENMASK(21, 20)
#define SAI_XCR_CSR(x)			((x - 1) << 20)
#define SAI_XCR_CSR_V(v)		((((v) & SAI_XCR_CSR_MASK) >> 20) + 1)
#define SAI_XCR_SJM_MASK		BIT(19)
#define SAI_XCR_SJM_L			BIT(19)
#define SAI_XCR_SJM_R			0
#define SAI_XCR_FBM_MASK		BIT(18)
#define SAI_XCR_FBM_LSB			BIT(18)
#define SAI_XCR_FBM_MSB			0
#define SAI_XCR_SNB_MASK		GENMASK(17, 11)
#define SAI_XCR_SNB(x)			((x - 1) << 11)
#define SAI_XCR_VDJ_MASK		BIT(10)
#define SAI_XCR_VDJ_L			BIT(10)
#define SAI_XCR_VDJ_R			0
#define SAI_XCR_SBW_MASK		GENMASK(9, 5)
#define SAI_XCR_SBW(x)			((x - 1) << 5)
#define SAI_XCR_SBW_V(v)		((((v) & SAI_XCR_SBW_MASK) >> 5) + 1)
#define SAI_XCR_VDW_MASK		GENMASK(4, 0)
#define SAI_XCR_VDW(x)			((x - 1) << 0)

/* FSCR Frame Sync Control Register */
#define SAI_FSCR_EDGE_MASK		BIT(24)
#define SAI_FSCR_EDGE_DUAL		BIT(24)
#define SAI_FSCR_EDGE_RISING		0
#define SAI_FSCR_FPW_MASK		GENMASK(23, 12)
#define SAI_FSCR_FPW(x)			((x - 1) << 12)
#define SAI_FSCR_FW_MASK		GENMASK(11, 0)
#define SAI_FSCR_FW(x)			((x - 1) << 0)

/* MONO_CR Mono Control Register */
#define SAI_MCR_RX_MONO_SLOT_MASK	GENMASK(8, 2)
#define SAI_MCR_RX_MONO_SLOT_SEL(x)	((x - 1) << 2)
#define SAI_MCR_RX_MONO_MASK		BIT(1)
#define SAI_MCR_RX_MONO_EN		BIT(1)
#define SAI_MCR_RX_MONO_DIS		0
#define SAI_MCR_TX_MONO_MASK		BIT(0)
#define SAI_MCR_TX_MONO_EN		BIT(0)
#define SAI_MCR_TX_MONO_DIS		0

/* XFER Transfer Start Register */
#define SAI_XFER_RX_IDLE		BIT(8)
#define SAI_XFER_TX_IDLE		BIT(7)
#define SAI_XFER_FS_IDLE		BIT(6)
#define SAI_XFER_RX_CNT_MASK		BIT(5)
#define SAI_XFER_RX_CNT_EN		BIT(5)
#define SAI_XFER_RX_CNT_DIS		0
#define SAI_XFER_TX_CNT_MASK		BIT(4)
#define SAI_XFER_TX_CNT_EN		BIT(4)
#define SAI_XFER_TX_CNT_DIS		0
#define SAI_XFER_RXS_MASK		BIT(3)
#define SAI_XFER_RXS_EN			BIT(3)
#define SAI_XFER_RXS_DIS		0
#define SAI_XFER_TXS_MASK		BIT(2)
#define SAI_XFER_TXS_EN			BIT(2)
#define SAI_XFER_TXS_DIS		0
#define SAI_XFER_FSS_MASK		BIT(1)
#define SAI_XFER_FSS_EN			BIT(1)
#define SAI_XFER_FSS_DIS		0
#define SAI_XFER_CLK_MASK		BIT(0)
#define SAI_XFER_CLK_EN			BIT(0)
#define SAI_XFER_CLK_DIS		0

/* CLR Clear Logic Register */
#define SAI_CLR_FSC			BIT(2)
#define SAI_CLR_RXC			BIT(1)
#define SAI_CLR_TXC			BIT(0)

/* CKR Clock Generation Register */
#define SAI_CKR_MDIV_MASK		GENMASK(14, 3)
#define SAI_CKR_MDIV(x)			((x - 1) << 3)
#define SAI_CKR_MSS_MASK		BIT(2)
#define SAI_CKR_MSS_SLAVE		BIT(2)
#define SAI_CKR_MSS_MASTER		0
#define SAI_CKR_CKP_MASK		BIT(1)
#define SAI_CKR_CKP_INVERTED		BIT(1)
#define SAI_CKR_CKP_NORMAL		0
#define SAI_CKR_FSP_MASK		BIT(0)
#define SAI_CKR_FSP_INVERTED		BIT(0)
#define SAI_CKR_FSP_NORMAL		0

/* DMACR DMA Control Register */
#define SAI_DMACR_RDE_MASK		BIT(24)
#define SAI_DMACR_RDE(x)		((x) << 24)
#define SAI_DMACR_RDL_MASK		GENMASK(20, 16)
#define SAI_DMACR_RDL(x)		((x - 1) << 16)
#define SAI_DMACR_TDE_MASK		BIT(8)
#define SAI_DMACR_TDE(x)		((x) << 8)
#define SAI_DMACR_TDL_MASK		GENMASK(4, 0)
#define SAI_DMACR_TDL(x)		((x) << 0)

/* INTCR Interrupt Ctrl Register */
#define SAI_INTCR_RXOIC			BIT(18)
#define SAI_INTCR_RXOIE_MASK		BIT(17)
#define SAI_INTCR_RXOIE(x)		((x) << 17)
#define SAI_INTCR_TXUIC			BIT(2)
#define SAI_INTCR_TXUIE_MASK		BIT(1)
#define SAI_INTCR_TXUIE(x)		((x) << 1)

/* INTSR Interrupt Status Register */
#define SAI_INTSR_RXOI_INA		0
#define SAI_INTSR_RXOI_ACT		BIT(17)
#define SAI_INTSR_TXUI_INA		0
#define SAI_INTSR_TXUI_ACT		BIT(1)

/* XSHIFT: Transfer / Receive Frame Sync Shift Register */
#define SAI_XSHIFT_SEL_MASK		GENMASK(23, 0)
#define SAI_XSHIFT_SEL(x)		(x)

/* SAI Registers */
#define SAI_TXCR			(0x0000)
#define SAI_FSCR			(0x0004)
#define SAI_RXCR			(0x0008)
#define SAI_MONO_CR			(0x000c)
#define SAI_XFER			(0x0010)
#define SAI_CLR				(0x0014)
#define SAI_CKR				(0x0018)
#define SAI_TXFIFOLR			(0x001c)
#define SAI_RXFIFOLR			(0x0020)
#define SAI_DMACR			(0x0024)
#define SAI_INTCR			(0x0028)
#define SAI_INTSR			(0x002c)
#define SAI_TXDR			(0x0030)
#define SAI_RXDR			(0x0034)
#define SAI_PATH_SEL			(0x0038)
#define SAI_TX_SLOT_MASK0		(0x003c)
#define SAI_TX_SLOT_MASK1		(0x0040)
#define SAI_TX_SLOT_MASK2		(0x0044)
#define SAI_TX_SLOT_MASK3		(0x0048)
#define SAI_RX_SLOT_MASK0		(0x004c)
#define SAI_RX_SLOT_MASK1		(0x0050)
#define SAI_RX_SLOT_MASK2		(0x0054)
#define SAI_RX_SLOT_MASK3		(0x0058)
#define SAI_TX_DATA_CNT			(0x005c)
#define SAI_RX_DATA_CNT			(0x0060)
#define SAI_TX_SHIFT			(0x0064)
#define SAI_RX_SHIFT			(0x0068)
#define SAI_VERSION			(0x0070)

#endif /* _ROCKCHIP_SAI_H */
