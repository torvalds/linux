/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2018 Emmanuel Vadot <manu@FreeBSD.org>
 * Copyright (c) 2013 Alexander Fedorov <alexander.fedorov@rtlservice.com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef	_AW_MMC_H_
#define	_AW_MMC_H_

#define	AW_MMC_GCTL		0x00	/* Control Register */
#define	AW_MMC_CKCR		0x04	/* Clock Control Register */
#define	AW_MMC_TMOR		0x08	/* Timeout Register */
#define	AW_MMC_BWDR		0x0C	/* Bus Width Register */
#define	AW_MMC_BKSR		0x10	/* Block Size Register */
#define	AW_MMC_BYCR		0x14	/* Byte Count Register */
#define	AW_MMC_CMDR		0x18	/* Command Register */
#define	AW_MMC_CAGR		0x1C	/* Argument Register */
#define	AW_MMC_RESP0		0x20	/* Response Register 0 */
#define	AW_MMC_RESP1		0x24	/* Response Register 1 */
#define	AW_MMC_RESP2		0x28	/* Response Register 2 */
#define	AW_MMC_RESP3		0x2C	/* Response Register 3 */
#define	AW_MMC_IMKR		0x30	/* Interrupt Mask Register */
#define	AW_MMC_MISR		0x34	/* Masked Interrupt Status Register */
#define	AW_MMC_RISR		0x38	/* Raw Interrupt Status Register */
#define	AW_MMC_STAR		0x3C	/* Status Register */
#define	AW_MMC_FWLR		0x40	/* FIFO Threshold Watermark Register */
#define	AW_MMC_FUNS		0x44	/* Function Select Register */
#define	AW_MMC_DBGC		0x50	/* Debug register */
#define	AW_MMC_CSDC		0x54	/* CRC status detect controler register (A64 smhc2 only) */
#define	AW_MMC_A12A		0x58	/* Auto command 12 argument register */
#define	AW_MMC_NTSR		0x5C	/* SD new timing register (H3, A64 smhc0/1 only) */
#define	AW_MMC_HWRST		0x78	/* Hardware reset */
#define	AW_MMC_DMAC		0x80	/* IDMAC Control Register */
#define	AW_MMC_DLBA		0x84	/* IDMAC Desc List Base Address Reg */
#define	AW_MMC_IDST		0x88	/* IDMAC Status Register */
#define	AW_MMC_IDIE		0x8C	/* IDMAC Interrupt Enable Register */

#define	AW_MMC_DDR_SBIT_DET	0x10C	/* eMMC4.5 DDR Start Bit Detection control register */
#define	AW_MMC_DRV_DL		0x140	/* Drive Delay control register */
#define	AW_MMC_SAMP_DL		0x144	/* Sample Delay controle register */
#define	AW_MMC_DS_DL		0x148	/* Data strobe delay control register */

#define	AW_MMC_FIFO		0x100	/* FIFO Access Address (A10/A20) */
#define	A31_MMC_FIFO		0x200	/* FIFO Access Address (A31) */

/* AW_MMC_GCTL */
#define	AW_MMC_GCTL_SOFT_RST		(1U << 0)
#define	AW_MMC_GCTL_FIFO_RST		(1U << 1)
#define	AW_MMC_GCTL_DMA_RST		(1U << 2)
#define	AW_MMC_GCTL_INT_ENB		(1U << 4)
#define	AW_MMC_GCTL_DMA_ENB		(1U << 5)
#define	AW_MMC_GCTL_CD_DBC_ENB		(1U << 8)
#define	AW_MMC_GCTL_DDR_MOD_SEL		(1U << 10)
#define	AW_MMC_GCTL_WAIT_MEM_ACCESS	(1U << 30)
#define	AW_MMC_GCTL_FIFO_AC_MOD		(1U << 31)
#define	AW_MMC_GCTL_RESET			\
	(AW_MMC_GCTL_SOFT_RST | AW_MMC_GCTL_FIFO_RST | AW_MMC_GCTL_DMA_RST)

/* AW_MMC_CKCR */
#define	AW_MMC_CKCR_DIV		0xff
#define	AW_MMC_CKCR_ENB		(1U << 16)
#define	AW_MMC_CKCR_LOW_POWER	(1U << 17)
#define	AW_MMC_CKCR_MASK_DATA0	(1U << 31)

/* AW_MMC_TMOR */
#define	AW_MMC_TMOR_RTO_LMT_SHIFT(x)	x		/* Response timeout limit */
#define	AW_MMC_TMOR_RTO_LMT_MASK	0xff
#define	AW_MMC_TMOR_DTO_LMT_SHIFT(x)	(x << 8)	/* Data timeout limit */
#define	AW_MMC_TMOR_DTO_LMT_MASK	0xffffff

/* AW_MMC_BWDR */
#define	AW_MMC_BWDR1			0
#define	AW_MMC_BWDR4			1
#define	AW_MMC_BWDR8			2

/* AW_MMC_CMDR */
#define	AW_MMC_CMDR_RESP_RCV		(1U << 6)
#define	AW_MMC_CMDR_LONG_RESP		(1U << 7)
#define	AW_MMC_CMDR_CHK_RESP_CRC	(1U << 8)
#define	AW_MMC_CMDR_DATA_TRANS		(1U << 9)
#define	AW_MMC_CMDR_DIR_WRITE		(1U << 10)
#define	AW_MMC_CMDR_TRANS_MODE_STREAM	(1U << 11)
#define	AW_MMC_CMDR_STOP_CMD_FLAG	(1U << 12)
#define	AW_MMC_CMDR_WAIT_PRE_OVER	(1U << 13)
#define	AW_MMC_CMDR_STOP_ABT_CMD	(1U << 14)
#define	AW_MMC_CMDR_SEND_INIT_SEQ	(1U << 15)
#define	AW_MMC_CMDR_PRG_CLK		(1U << 21)
#define	AW_MMC_CMDR_RD_CEDATA_DEV	(1U << 22)
#define	AW_MMC_CMDR_CCS_EXP		(1U << 23)
#define	AW_MMC_CMDR_BOOT_MOD_SHIFT	24
#define	AW_MMC_CMDR_BOOT_MOD_NORMAL	0
#define	AW_MMC_CMDR_BOOT_MOD_MANDATORY	1
#define	AW_MMC_CMDR_BOOT_MOD_ALT	2
#define	AW_MMC_CMDR_EXP_BOOT_ACK	(1U << 26)
#define	AW_MMC_CMDR_BOOT_ABT		(1U << 27)
#define	AW_MMC_CMDR_VOL_SW		(1U << 28)
#define	AW_MMC_CMDR_LOAD		(1U << 31)

/* AW_MMC_IMKR and AW_MMC_RISR */
#define	AW_MMC_INT_RESP_ERR	(1U << 1)
#define	AW_MMC_INT_CMD_DONE		(1U << 2)
#define	AW_MMC_INT_DATA_OVER		(1U << 3)
#define	AW_MMC_INT_TX_DATA_REQ		(1U << 4)
#define	AW_MMC_INT_RX_DATA_REQ		(1U << 5)
#define	AW_MMC_INT_RESP_CRC_ERR		(1U << 6)
#define	AW_MMC_INT_DATA_CRC_ERR		(1U << 7)
#define	AW_MMC_INT_RESP_TIMEOUT		(1U << 8)
#define	AW_MMC_INT_BOOT_ACK_RECV	(1U << 8)
#define	AW_MMC_INT_DATA_TIMEOUT		(1U << 9)
#define	AW_MMC_INT_BOOT_START		(1U << 9)
#define	AW_MMC_INT_DATA_STARVE		(1U << 10)
#define	AW_MMC_INT_VOL_CHG_DONE		(1U << 10)
#define	AW_MMC_INT_FIFO_RUN_ERR		(1U << 11)
#define	AW_MMC_INT_CMD_BUSY		(1U << 12)
#define	AW_MMC_INT_DATA_START_ERR	(1U << 13)
#define	AW_MMC_INT_AUTO_STOP_DONE	(1U << 14)
#define	AW_MMC_INT_DATA_END_BIT_ERR	(1U << 15)
#define	AW_MMC_INT_SDIO			(1U << 16)
#define	AW_MMC_INT_CARD_INSERT		(1U << 30)
#define	AW_MMC_INT_CARD_REMOVE		(1U << 31)
#define	AW_MMC_INT_ERR_BIT				\
	(AW_MMC_INT_RESP_ERR | AW_MMC_INT_RESP_CRC_ERR |	\
	 AW_MMC_INT_DATA_CRC_ERR | AW_MMC_INT_RESP_TIMEOUT |	\
	 AW_MMC_INT_FIFO_RUN_ERR |	AW_MMC_INT_CMD_BUSY |	\
	 AW_MMC_INT_DATA_START_ERR | AW_MMC_INT_DATA_END_BIT_ERR)

/* AW_MMC_STAR */
#define	AW_MMC_STAR_FIFO_RX_LEVEL	(1U << 0)
#define	AW_MMC_STAR_FIFO_TX_LEVEL	(1U << 1)
#define	AW_MMC_STAR_FIFO_EMPTY		(1U << 2)
#define	AW_MMC_STAR_FIFO_FULL		(1U << 3)
#define	AW_MMC_STAR_CARD_PRESENT	(1U << 8)
#define	AW_MMC_STAR_CARD_BUSY		(1U << 9)
#define	AW_MMC_STAR_FSM_BUSY		(1U << 10)
#define	AW_MMC_STAR_DMA_REQ			(1U << 31)

/* AW_MMC_FUNS */
#define	AW_MMC_CE_ATA_ON		(0xceaaU << 16)
#define	AW_MMC_SEND_IRQ_RESP		(1U << 0)
#define	AW_MMC_SDIO_RD_WAIT		(1U << 1)
#define	AW_MMC_ABT_RD_DATA		(1U << 2)
#define	AW_MMC_SEND_CC_SD		(1U << 8)
#define	AW_MMC_SEND_AUTOSTOP_CC_SD	(1U << 9)
#define	AW_MMC_CE_ATA_DEV_INT_ENB	(1U << 10)

/* AW_MMC_NTSR */
#define	AW_MMC_NTSR_MODE_SELECT		(1U << 31)

/* IDMA CONTROLLER BUS MOD BIT FIELD */
#define	AW_MMC_DMAC_IDMAC_SOFT_RST	(1U << 0)
#define	AW_MMC_DMAC_IDMAC_FIX_BURST	(1U << 1)
#define	AW_MMC_DMAC_IDMAC_IDMA_ON	(1U << 7)
#define	AW_MMC_DMAC_IDMAC_REFETCH_DES	(1U << 31)

/* AW_MMC_IDST */
#define	AW_MMC_IDST_TX_INT		(1U << 0)
#define	AW_MMC_IDST_RX_INT		(1U << 1)
#define	AW_MMC_IDST_FATAL_BERR_INT	(1U << 2)
#define	AW_MMC_IDST_DES_UNAVL_INT	(1U << 4)
#define	AW_MMC_IDST_ERR_FLAG_SUM	(1U << 5)
#define	AW_MMC_IDST_NOR_INT_SUM		(1U << 8)
#define	AW_MMC_IDST_ABN_INT_SUM		(1U << 9)
#define	AW_MMC_IDST_HOST_ABT_INTX	(1U << 10)
#define	AW_MMC_IDST_HOST_ABT_INRX	(1U << 10)
#define	AW_MMC_IDST_IDLE		(0U << 13)
#define	AW_MMC_IDST_SUSPEND		(1U << 13)
#define	AW_MMC_IDST_DESC_RD		(2U << 13)
#define	AW_MMC_IDST_DESC_CHECK		(3U << 13)
#define	AW_MMC_IDST_RD_REQ_WAIT		(4U << 13)
#define	AW_MMC_IDST_WR_REQ_WAIT		(5U << 13)
#define	AW_MMC_IDST_RD			(6U << 13)
#define	AW_MMC_IDST_WR			(7U << 13)
#define	AW_MMC_IDST_DESC_CLOSE		(8U << 13)
#define	AW_MMC_IDST_ERROR				\
	(AW_MMC_IDST_FATAL_BERR_INT | AW_MMC_IDST_ERR_FLAG_SUM |	\
	 AW_MMC_IDST_DES_UNAVL_INT | AW_MMC_IDST_ABN_INT_SUM)
#define	AW_MMC_IDST_COMPLETE				\
	(AW_MMC_IDST_TX_INT | AW_MMC_IDST_RX_INT)

/* AW_MMC_DDR_SBIT_DET */
#define	AW_MMC_DDR_SBIT_HS_MD_EN	(1U << 31)

/* AW_MMC_SAMP */
#define	AW_MMC_SAMP_DL_SW_EN		(1U << 7)

/* The DMA descriptor table. */
struct aw_mmc_dma_desc {
	uint32_t config;
#define	AW_MMC_DMA_CONFIG_DIC		(1U << 1)	/* Disable Interrupt Completion */
#define	AW_MMC_DMA_CONFIG_LD		(1U << 2)	/* Last DES */
#define	AW_MMC_DMA_CONFIG_FD		(1U << 3)	/* First DES */
#define	AW_MMC_DMA_CONFIG_CH		(1U << 4)	/* CHAIN MOD */
#define	AW_MMC_DMA_CONFIG_ER		(1U << 5)	/* End of Ring (undocumented register) */
#define	AW_MMC_DMA_CONFIG_CES		(1U << 30)	/* Card Error Summary */
#define	AW_MMC_DMA_CONFIG_OWN		(1U << 31)	/* DES Own Flag */
	uint32_t buf_size;
	uint32_t buf_addr;
	uint32_t next;
};

#define	AW_MMC_DMA_ALIGN	4

#endif /* _AW_MMC_H_ */
