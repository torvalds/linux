/*-
 * Copyright (c) 2014 Ruslan Bukin <br@bsdpad.com>
 * All rights reserved.
 *
 * This software was developed by SRI International and the University of
 * Cambridge Computer Laboratory under DARPA/AFRL contract (FA8750-10-C-0237)
 * ("CTSRD"), as part of the DARPA CRASH research programme.
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

#ifndef DEV_MMC_HOST_DWMMC_REG_H
#define DEV_MMC_HOST_DWMMC_REG_H

#define	SDMMC_CTRL		0x0	/* Control Register */
#define	 SDMMC_CTRL_USE_IDMAC	(1 << 25)	/* Use Internal DMAC */
#define	 SDMMC_CTRL_DMA_ENABLE	(1 << 5)	/* */
#define	 SDMMC_CTRL_INT_ENABLE	(1 << 4)	/* Enable interrupts */
#define	 SDMMC_CTRL_DMA_RESET	(1 << 2)	/* Reset DMA */
#define	 SDMMC_CTRL_FIFO_RESET	(1 << 1)	/* Reset FIFO */
#define	 SDMMC_CTRL_RESET	(1 << 0)	/* Reset SD/MMC controller */
#define	SDMMC_PWREN		0x4	/* Power Enable Register */
#define	 SDMMC_PWREN_PE		(1 << 0)	/* Power On */
#define	SDMMC_CLKDIV		0x8	/* Clock Divider Register */
#define	SDMMC_CLKSRC		0xC	/* SD Clock Source Register */
#define	SDMMC_CLKENA		0x10	/* Clock Enable Register */
#define	SDMMC_CLKENA_LP		(1 << 16)	/* Low-power mode */
#define	SDMMC_CLKENA_CCLK_EN	(1 << 0)	/* SD/MMC Enable */
#define	SDMMC_TMOUT		0x14	/* Timeout Register */
#define	SDMMC_CTYPE		0x18	/* Card Type Register */
#define	 SDMMC_CTYPE_8BIT	(1 << 16)
#define	 SDMMC_CTYPE_4BIT	(1 << 0)
#define	SDMMC_BLKSIZ		0x1C	/* Block Size Register */
#define	SDMMC_BYTCNT		0x20	/* Byte Count Register */
#define	SDMMC_INTMASK		0x24	/* Interrupt Mask Register */
#define	 SDMMC_INTMASK_SDIO	(1 << 16)	/* SDIO Interrupt Enable */
#define	 SDMMC_INTMASK_EBE	(1 << 15)	/* End-bit error */
#define	 SDMMC_INTMASK_ACD	(1 << 14)	/* Auto command done */
#define	 SDMMC_INTMASK_SBE	(1 << 13)	/* Start-bit error */
#define	 SDMMC_INTMASK_HLE	(1 << 12)	/* Hardware locked write err */
#define	 SDMMC_INTMASK_FRUN	(1 << 11)	/* FIFO underrun/overrun err */
#define	 SDMMC_INTMASK_HTO	(1 << 10)	/* Data starvation by host timeout */
#define	 SDMMC_INTMASK_DRT	(1 << 9)	/* Data read timeout  */
#define	 SDMMC_INTMASK_RTO	(1 << 8)	/* Response timeout */
#define	 SDMMC_INTMASK_DCRC	(1 << 7)	/* Data CRC error */
#define	 SDMMC_INTMASK_RCRC	(1 << 6)	/* Response CRC error */
#define	 SDMMC_INTMASK_RXDR	(1 << 5)	/* Receive FIFO data request */
#define	 SDMMC_INTMASK_TXDR	(1 << 4)	/* Transmit FIFO data request */
#define	 SDMMC_INTMASK_DTO	(1 << 3)	/* Data transfer over */
#define	 SDMMC_INTMASK_CMD_DONE	(1 << 2)	/* Command done */
#define	 SDMMC_INTMASK_RE	(1 << 1)	/* Response error */
#define	 SDMMC_INTMASK_CD	(1 << 0)	/* Card Detected */
#define	SDMMC_CMDARG		0x28	/* Command Argument Register */
#define	SDMMC_CMD		0x2C	/* Command Register */
#define	 SDMMC_CMD_START	(1 << 31)
#define	 SDMMC_CMD_USE_HOLD_REG	(1 << 29)
#define	 SDMMC_CMD_UPD_CLK_ONLY	(1 << 21)	/* Update clk only */
#define	 SDMMC_CMD_SEND_INIT	(1 << 15)	/* Send initialization */
#define	 SDMMC_CMD_STOP_ABORT	(1 << 14)	/* stop current data transfer */
#define	 SDMMC_CMD_WAIT_PRVDATA	(1 << 13)	/* Wait for prev data transfer completion */
#define	 SDMMC_CMD_SEND_ASTOP	(1 << 12)	/* Send stop command at end of data tx/rx */
#define	 SDMMC_CMD_MODE_STREAM	(1 << 11)	/* Stream data transfer */
#define	 SDMMC_CMD_DATA_WRITE	(1 << 10)	/* Write to card */
#define	 SDMMC_CMD_DATA_EXP	(1 << 9)	/* Data transfer expected */
#define	 SDMMC_CMD_RESP_CRC	(1 << 8)	/* Check Response CRC */
#define	 SDMMC_CMD_RESP_LONG	(1 << 7)	/* Long response expected */
#define	 SDMMC_CMD_RESP_EXP	(1 << 6)	/* Response expected */
#define	SDMMC_RESP0		0x30	/* Response Register 0 */
#define	SDMMC_RESP1		0x34	/* Response Register 1 */
#define	SDMMC_RESP2		0x38	/* Response Register 2 */
#define	SDMMC_RESP3		0x3C	/* Response Register 3 */
#define	SDMMC_MINTSTS		0x40	/* Masked Interrupt Status Register */
#define	SDMMC_RINTSTS		0x44	/* Raw Interrupt Status Register */
#define	SDMMC_STATUS		0x48	/* Status Register */
#define	 SDMMC_STATUS_DATA_BUSY	(1 << 9) /* card_data[0] */
#define	 SDMMC_STATUS_FIFO_FULL	(1 << 3) /* FIFO full */
#define	 SDMMC_STATUS_FIFO_EMPTY (1 << 2) /* FIFO empty */
#define	SDMMC_FIFOTH		0x4C	/* FIFO Threshold Watermark Register */
#define	 SDMMC_FIFOTH_MSIZE_S	28	/* Burst size of multiple transaction */
#define	 SDMMC_FIFOTH_RXWMARK_S	16	/* FIFO threshold watermark level */
#define	 SDMMC_FIFOTH_TXWMARK_S	0	/* FIFO threshold watermark level */
#define	SDMMC_CDETECT		0x50	/* Card Detect Register */
#define	SDMMC_WRTPRT		0x54	/* Write Protect Register */
#define	SDMMC_TCBCNT		0x5C	/* Transferred CIU Card Byte Count */
#define	SDMMC_TBBCNT		0x60	/* Transferred Host to BIU-FIFO Byte Count */
#define	SDMMC_DEBNCE		0x64	/* Debounce Count Register */
#define	SDMMC_USRID		0x68	/* User ID Register */
#define	SDMMC_VERID		0x6C	/* Version ID Register */
#define	SDMMC_HCON		0x70	/* Hardware Configuration Register */
#define	SDMMC_UHS_REG		0x74	/* UHS-1 Register */
#define	 SDMMC_UHS_REG_DDR	(1 << 16) /* DDR mode */
#define	SDMMC_RST_N		0x78	/* Hardware Reset Register */
#define	SDMMC_BMOD		0x80	/* Bus Mode Register */
#define	 SDMMC_BMOD_DE		(1 << 7) /* IDMAC Enable */
#define	 SDMMC_BMOD_FB		(1 << 1) /* AHB Master Fixed Burst */
#define	 SDMMC_BMOD_SWR		(1 << 0) /* Reset DMA */
#define	SDMMC_PLDMND		0x84	/* Poll Demand Register */
#define	SDMMC_DBADDR		0x88	/* Descriptor List Base Address */
#define	SDMMC_IDSTS		0x8C	/* Internal DMAC Status Register */
#define	SDMMC_IDINTEN		0x90	/* Internal DMAC Interrupt Enable */
#define	 SDMMC_IDINTEN_AI	(1 << 9) /* Abnormal Interrupt Summary */
#define	 SDMMC_IDINTEN_NI	(1 << 8) /* Normal Interrupt Summary */
#define	 SDMMC_IDINTEN_CES	(1 << 5) /* Card Error Summary */
#define	 SDMMC_IDINTEN_DU	(1 << 4) /* Descriptor Unavailable */
#define	 SDMMC_IDINTEN_FBE	(1 << 2) /* Fatal Bus Error */
#define	 SDMMC_IDINTEN_RI	(1 << 1) /* Receive Interrupt */
#define	 SDMMC_IDINTEN_TI	(1 << 0) /* Transmit Interrupt */
#define	 SDMMC_IDINTEN_MASK	(SDMMC_IDINTEN_AI | SDMMC_IDINTEN_NI | SDMMC_IDINTEN_CES | \
				 SDMMC_IDINTEN_DU | SDMMC_IDINTEN_FBE | SDMMC_IDINTEN_RI | \
				 SDMMC_IDINTEN_TI)
#define	SDMMC_DSCADDR		0x94	/* Current Host Descriptor Address */
#define	SDMMC_BUFADDR		0x98	/* Current Buffer Descriptor Address */
#define	SDMMC_CARDTHRCTL	0x100	/* Card Threshold Control Register */
#define	SDMMC_BACK_END_POWER_R	0x104	/* Back End Power Register */
#define	SDMMC_DATA		0x200	/* Data FIFO Access */

/* eMMC */
#define	EMMCP_MPSBEGIN0			0x1200	/*  */
#define	EMMCP_SEND0			0x1204	/*  */
#define	EMMCP_CTRL0			0x120C	/*  */
#define	 MPSCTRL_SECURE_READ_BIT	(1 << 7)
#define	 MPSCTRL_SECURE_WRITE_BIT	(1 << 6)
#define	 MPSCTRL_NON_SECURE_READ_BIT	(1 << 5)
#define	 MPSCTRL_NON_SECURE_WRITE_BIT	(1 << 4)
#define	 MPSCTRL_USE_FUSE_KEY		(1 << 3)
#define	 MPSCTRL_ECB_MODE		(1 << 2)
#define	 MPSCTRL_ENCRYPTION		(1 << 1)
#define	 MPSCTRL_VALID			(1 << 0)

/* Platform-specific defines */
#define	SDMMC_CLKSEL			0x9C
#define	 SDMMC_CLKSEL_SAMPLE_SHIFT	0
#define	 SDMMC_CLKSEL_DRIVE_SHIFT	16
#define	 SDMMC_CLKSEL_DIVIDER_SHIFT	24

#endif /* DEV_MMC_HOST_DWMMC_REG_H */
