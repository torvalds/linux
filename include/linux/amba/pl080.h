/* SPDX-License-Identifier: GPL-2.0-only */
/* include/linux/amba/pl080.h
 *
 * Copyright 2008 Openmoko, Inc.
 * Copyright 2008 Simtec Electronics
 *      http://armlinux.simtec.co.uk/
 *      Ben Dooks <ben@simtec.co.uk>
 *
 * ARM PrimeCell PL080 DMA controller
*/

/* Note, there are some Samsung updates to this controller block which
 * make it not entierly compatible with the PL080 specification from
 * ARM. When in doubt, check the Samsung documentation first.
 *
 * The Samsung defines are PL080S, and add an extra control register,
 * the ability to move more than 2^11 counts of data and some extra
 * OneNAND features.
*/

#ifndef ASM_PL080_H
#define ASM_PL080_H

#define PL080_INT_STATUS			(0x00)
#define PL080_TC_STATUS				(0x04)
#define PL080_TC_CLEAR				(0x08)
#define PL080_ERR_STATUS			(0x0C)
#define PL080_ERR_CLEAR				(0x10)
#define PL080_RAW_TC_STATUS			(0x14)
#define PL080_RAW_ERR_STATUS			(0x18)
#define PL080_EN_CHAN				(0x1c)
#define PL080_SOFT_BREQ				(0x20)
#define PL080_SOFT_SREQ				(0x24)
#define PL080_SOFT_LBREQ			(0x28)
#define PL080_SOFT_LSREQ			(0x2C)

#define PL080_CONFIG				(0x30)
#define PL080_CONFIG_M2_BE			BIT(2)
#define PL080_CONFIG_M1_BE			BIT(1)
#define PL080_CONFIG_ENABLE			BIT(0)

#define PL080_SYNC				(0x34)

/* The Faraday Technology FTDMAC020 variant registers */
#define FTDMAC020_CH_BUSY			(0x20)
/* Identical to PL080_CONFIG */
#define FTDMAC020_CSR				(0x24)
/* Identical to PL080_SYNC */
#define FTDMAC020_SYNC				(0x2C)
#define FTDMAC020_REVISION			(0x30)
#define FTDMAC020_FEATURE			(0x34)

/* Per channel configuration registers */
#define PL080_Cx_BASE(x)			((0x100 + (x * 0x20)))
#define PL080_CH_SRC_ADDR			(0x00)
#define PL080_CH_DST_ADDR			(0x04)
#define PL080_CH_LLI				(0x08)
#define PL080_CH_CONTROL			(0x0C)
#define PL080_CH_CONFIG				(0x10)
#define PL080S_CH_CONTROL2			(0x10)
#define PL080S_CH_CONFIG			(0x14)
/* The Faraday FTDMAC020 derivative shuffles the registers around */
#define FTDMAC020_CH_CSR			(0x00)
#define FTDMAC020_CH_CFG			(0x04)
#define FTDMAC020_CH_SRC_ADDR			(0x08)
#define FTDMAC020_CH_DST_ADDR			(0x0C)
#define FTDMAC020_CH_LLP			(0x10)
#define FTDMAC020_CH_SIZE			(0x14)

#define PL080_LLI_ADDR_MASK			GENMASK(31, 2)
#define PL080_LLI_ADDR_SHIFT			(2)
#define PL080_LLI_LM_AHB2			BIT(0)

#define PL080_CONTROL_TC_IRQ_EN			BIT(31)
#define PL080_CONTROL_PROT_MASK			GENMASK(30, 28)
#define PL080_CONTROL_PROT_SHIFT		(28)
#define PL080_CONTROL_PROT_CACHE		BIT(30)
#define PL080_CONTROL_PROT_BUFF			BIT(29)
#define PL080_CONTROL_PROT_SYS			BIT(28)
#define PL080_CONTROL_DST_INCR			BIT(27)
#define PL080_CONTROL_SRC_INCR			BIT(26)
#define PL080_CONTROL_DST_AHB2			BIT(25)
#define PL080_CONTROL_SRC_AHB2			BIT(24)
#define PL080_CONTROL_DWIDTH_MASK		GENMASK(23, 21)
#define PL080_CONTROL_DWIDTH_SHIFT		(21)
#define PL080_CONTROL_SWIDTH_MASK		GENMASK(20, 18)
#define PL080_CONTROL_SWIDTH_SHIFT		(18)
#define PL080_CONTROL_DB_SIZE_MASK		GENMASK(17, 15)
#define PL080_CONTROL_DB_SIZE_SHIFT		(15)
#define PL080_CONTROL_SB_SIZE_MASK		GENMASK(14, 12)
#define PL080_CONTROL_SB_SIZE_SHIFT		(12)
#define PL080_CONTROL_TRANSFER_SIZE_MASK	GENMASK(11, 0)
#define PL080S_CONTROL_TRANSFER_SIZE_MASK	GENMASK(24, 0)
#define PL080_CONTROL_TRANSFER_SIZE_SHIFT	(0)

#define PL080_BSIZE_1				(0x0)
#define PL080_BSIZE_4				(0x1)
#define PL080_BSIZE_8				(0x2)
#define PL080_BSIZE_16				(0x3)
#define PL080_BSIZE_32				(0x4)
#define PL080_BSIZE_64				(0x5)
#define PL080_BSIZE_128				(0x6)
#define PL080_BSIZE_256				(0x7)

#define PL080_WIDTH_8BIT			(0x0)
#define PL080_WIDTH_16BIT			(0x1)
#define PL080_WIDTH_32BIT			(0x2)

#define PL080N_CONFIG_ITPROT			BIT(20)
#define PL080N_CONFIG_SECPROT			BIT(19)
#define PL080_CONFIG_HALT			BIT(18)
#define PL080_CONFIG_ACTIVE			BIT(17)  /* RO */
#define PL080_CONFIG_LOCK			BIT(16)
#define PL080_CONFIG_TC_IRQ_MASK		BIT(15)
#define PL080_CONFIG_ERR_IRQ_MASK		BIT(14)
#define PL080_CONFIG_FLOW_CONTROL_MASK		GENMASK(13, 11)
#define PL080_CONFIG_FLOW_CONTROL_SHIFT		(11)
#define PL080_CONFIG_DST_SEL_MASK		GENMASK(9, 6)
#define PL080_CONFIG_DST_SEL_SHIFT		(6)
#define PL080_CONFIG_SRC_SEL_MASK		GENMASK(4, 1)
#define PL080_CONFIG_SRC_SEL_SHIFT		(1)
#define PL080_CONFIG_ENABLE			BIT(0)

#define PL080_FLOW_MEM2MEM			(0x0)
#define PL080_FLOW_MEM2PER			(0x1)
#define PL080_FLOW_PER2MEM			(0x2)
#define PL080_FLOW_SRC2DST			(0x3)
#define PL080_FLOW_SRC2DST_DST			(0x4)
#define PL080_FLOW_MEM2PER_PER			(0x5)
#define PL080_FLOW_PER2MEM_PER			(0x6)
#define PL080_FLOW_SRC2DST_SRC			(0x7)

#define FTDMAC020_CH_CSR_TC_MSK			BIT(31)
/* Later versions have a threshold in bits 24..26,  */
#define FTDMAC020_CH_CSR_FIFOTH_MSK		GENMASK(26, 24)
#define FTDMAC020_CH_CSR_FIFOTH_SHIFT		(24)
#define FTDMAC020_CH_CSR_CHPR1_MSK		GENMASK(23, 22)
#define FTDMAC020_CH_CSR_PROT3			BIT(21)
#define FTDMAC020_CH_CSR_PROT2			BIT(20)
#define FTDMAC020_CH_CSR_PROT1			BIT(19)
#define FTDMAC020_CH_CSR_SRC_SIZE_MSK		GENMASK(18, 16)
#define FTDMAC020_CH_CSR_SRC_SIZE_SHIFT		(16)
#define FTDMAC020_CH_CSR_ABT			BIT(15)
#define FTDMAC020_CH_CSR_SRC_WIDTH_MSK		GENMASK(13, 11)
#define FTDMAC020_CH_CSR_SRC_WIDTH_SHIFT	(11)
#define FTDMAC020_CH_CSR_DST_WIDTH_MSK		GENMASK(10, 8)
#define FTDMAC020_CH_CSR_DST_WIDTH_SHIFT	(8)
#define FTDMAC020_CH_CSR_MODE			BIT(7)
/* 00 = increase, 01 = decrease, 10 = fix */
#define FTDMAC020_CH_CSR_SRCAD_CTL_MSK		GENMASK(6, 5)
#define FTDMAC020_CH_CSR_SRCAD_CTL_SHIFT	(5)
#define FTDMAC020_CH_CSR_DSTAD_CTL_MSK		GENMASK(4, 3)
#define FTDMAC020_CH_CSR_DSTAD_CTL_SHIFT	(3)
#define FTDMAC020_CH_CSR_SRC_SEL		BIT(2)
#define FTDMAC020_CH_CSR_DST_SEL		BIT(1)
#define FTDMAC020_CH_CSR_EN			BIT(0)

/* FIFO threshold setting */
#define FTDMAC020_CH_CSR_FIFOTH_1		(0x0)
#define FTDMAC020_CH_CSR_FIFOTH_2		(0x1)
#define FTDMAC020_CH_CSR_FIFOTH_4		(0x2)
#define FTDMAC020_CH_CSR_FIFOTH_8		(0x3)
#define FTDMAC020_CH_CSR_FIFOTH_16		(0x4)
/* The FTDMAC020 supports 64bit wide transfers */
#define FTDMAC020_WIDTH_64BIT			(0x3)
/* Address can be increased, decreased or fixed */
#define FTDMAC020_CH_CSR_SRCAD_CTL_INC		(0x0)
#define FTDMAC020_CH_CSR_SRCAD_CTL_DEC		(0x1)
#define FTDMAC020_CH_CSR_SRCAD_CTL_FIXED	(0x2)

#define FTDMAC020_CH_CFG_LLP_CNT_MASK		GENMASK(19, 16)
#define FTDMAC020_CH_CFG_LLP_CNT_SHIFT		(16)
#define FTDMAC020_CH_CFG_BUSY			BIT(8)
#define FTDMAC020_CH_CFG_INT_ABT_MASK		BIT(2)
#define FTDMAC020_CH_CFG_INT_ERR_MASK		BIT(1)
#define FTDMAC020_CH_CFG_INT_TC_MASK		BIT(0)

/* Inside the LLIs, the applicable CSR fields are mapped differently */
#define FTDMAC020_LLI_TC_MSK			BIT(28)
#define FTDMAC020_LLI_SRC_WIDTH_MSK		GENMASK(27, 25)
#define FTDMAC020_LLI_SRC_WIDTH_SHIFT		(25)
#define FTDMAC020_LLI_DST_WIDTH_MSK		GENMASK(24, 22)
#define FTDMAC020_LLI_DST_WIDTH_SHIFT		(22)
#define FTDMAC020_LLI_SRCAD_CTL_MSK		GENMASK(21, 20)
#define FTDMAC020_LLI_SRCAD_CTL_SHIFT		(20)
#define FTDMAC020_LLI_DSTAD_CTL_MSK		GENMASK(19, 18)
#define FTDMAC020_LLI_DSTAD_CTL_SHIFT		(18)
#define FTDMAC020_LLI_SRC_SEL			BIT(17)
#define FTDMAC020_LLI_DST_SEL			BIT(16)
#define FTDMAC020_LLI_TRANSFER_SIZE_MASK	GENMASK(11, 0)
#define FTDMAC020_LLI_TRANSFER_SIZE_SHIFT	(0)

#define FTDMAC020_CFG_LLP_CNT_MASK		GENMASK(19, 16)
#define FTDMAC020_CFG_LLP_CNT_SHIFT		(16)
#define FTDMAC020_CFG_BUSY			BIT(8)
#define FTDMAC020_CFG_INT_ABT_MSK		BIT(2)
#define FTDMAC020_CFG_INT_ERR_MSK		BIT(1)
#define FTDMAC020_CFG_INT_TC_MSK		BIT(0)

/* DMA linked list chain structure */

struct pl080_lli {
	u32	src_addr;
	u32	dst_addr;
	u32	next_lli;
	u32	control0;
};

struct pl080s_lli {
	u32	src_addr;
	u32	dst_addr;
	u32	next_lli;
	u32	control0;
	u32	control1;
};

#endif /* ASM_PL080_H */
