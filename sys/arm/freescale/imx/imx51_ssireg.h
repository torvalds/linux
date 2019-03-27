/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2012, 2013 The FreeBSD Foundation
 * All rights reserved.
 *
 * This software was developed by Oleksandr Rybalko under sponsorship
 * from the FreeBSD Foundation.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1.	Redistributions of source code must retain the above copyright
 *	notice, this list of conditions and the following disclaimer.
 * 2.	Redistributions in binary form must reproduce the above copyright
 *	notice, this list of conditions and the following disclaimer in the
 *	documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
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

/* Registers definition for Freescale i.MX515 Synchronous Serial Interface */

#define	IMX51_SSI_STX0_REG	0x0000 /* SSI TX Data Register 0 */
#define	IMX51_SSI_STX1_REG	0x0004 /* SSI TX Data Register 1 */
#define	IMX51_SSI_SRX0_REG	0x0008 /* SSI RX Data Register 0 */
#define	IMX51_SSI_SRX1_REG	0x000C /* SSI RX Data Register 1 */
#define	IMX51_SSI_SCR_REG	0x0010 /* SSI Control Register */
#define		SSI_SCR_RFR_CLK_DIS	(1 << 11) /* RX FC Disable */
#define		SSI_SCR_TFR_CLK_DIS	(1 << 10) /* TX FC Disable */
#define		SSI_SCR_CLK_IST		(1 << 9) /* Clock Idle */
#define		SSI_SCR_TCH_EN		(1 << 8) /* 2Chan Enable */
#define		SSI_SCR_SYS_CLK_EN	(1 << 7) /* System Clock En */
#define		SSI_SCR_MODE_NORMAL	(0 << 5)
#define		SSI_SCR_MODE_I2S_MASTER	(1 << 5)
#define		SSI_SCR_MODE_I2S_SLAVE	(2 << 5)
#define		SSI_SCR_MODE_MASK	(3 << 5)
#define		SSI_SCR_SYN		(1 << 4) /* Sync Mode */
#define		SSI_SCR_NET		(1 << 3) /* Network Mode */
#define		SSI_SCR_RE		(1 << 2) /* RX Enable */
#define		SSI_SCR_TE		(1 << 1) /* TX Enable */
#define		SSI_SCR_SSIEN		(1 << 0) /* SSI Enable */

#define	IMX51_SSI_SISR_REG	0x0014 /* SSI Interrupt Status Register */
#define		SSI_SISR_RFRC		(1 << 24) /* RX Frame Complete */
#define		SSI_SIR_TFRC		(1 << 23) /* TX Frame Complete */
#define		SSI_SIR_CMDAU		(1 << 18) /* Command Address Updated */
#define		SSI_SIR_CMDDU		(1 << 17) /* Command Data Updated */
#define		SSI_SIR_RXT		(1 << 16) /* RX Tag Updated */
#define		SSI_SIR_RDR1		(1 << 15) /* RX Data Ready 1 */
#define		SSI_SIR_RDR0		(1 << 14) /* RX Data Ready 0 */
#define		SSI_SIR_TDE1		(1 << 13) /* TX Data Reg Empty 1 */
#define		SSI_SIR_TDE0		(1 << 12) /* TX Data Reg Empty 0 */
#define		SSI_SIR_ROE1		(1 << 11) /* RXer Overrun Error 1 */
#define		SSI_SIR_ROE0		(1 << 10) /* RXer Overrun Error 0 */
#define		SSI_SIR_TUE1		(1 << 9) /* TXer Underrun Error 1 */
#define		SSI_SIR_TUE0		(1 << 8) /* TXer Underrun Error 0 */
#define		SSI_SIR_TFS		(1 << 7) /* TX Frame Sync */
#define		SSI_SIR_RFS		(1 << 6) /* RX Frame Sync */
#define		SSI_SIR_TLS		(1 << 5) /* TX Last Time Slot */
#define		SSI_SIR_RLS		(1 << 4) /* RX Last Time Slot */
#define		SSI_SIR_RFF1		(1 << 3) /* RX FIFO Full 1 */
#define		SSI_SIR_RFF0		(1 << 2) /* RX FIFO Full 0 */
#define		SSI_SIR_TFE1		(1 << 1) /* TX FIFO Empty 1 */
#define		SSI_SIR_TFE0		(1 << 0) /* TX FIFO Empty 0 */

#define	IMX51_SSI_SIER_REG	0x0018 /* SSI Interrupt Enable Register */
/* 24-23 Enable Bit	(See SISR) */
#define		SSI_SIER_RDMAE		(1 << 22) /* RX DMA Enable */
#define		SSI_SIER_RIE		(1 << 21) /* RX Interrupt Enable */
#define		SSI_SIER_TDMAE		(1 << 20) /* TX DMA Enable */
#define		SSI_SIER_TIE		(1 << 19) /* TX Interrupt Enable */
/* 18-0 Enable Bits	(See SISR) */

#define	IMX51_SSI_STCR_REG	0x001C /* SSI TX Configuration Register */
#define		SSI_STCR_TXBIT0		(1 << 9) /* TX Bit 0 */
#define		SSI_STCR_TFEN1		(1 << 8) /* TX FIFO Enable 1 */
#define		SSI_STCR_TFEN0		(1 << 7) /* TX FIFO Enable 0 */
#define		SSI_STCR_TFDIR		(1 << 6) /* TX Frame Direction */
#define		SSI_STCR_TXDIR		(1 << 5) /* TX Clock Direction */
#define		SSI_STCR_TSHFD		(1 << 4) /* TX Shift Direction */
#define		SSI_STCR_TSCKP		(1 << 3) /* TX Clock Polarity */
#define		SSI_STCR_TFSI		(1 << 2) /* TX Frame Sync Invert */
#define		SSI_STCR_TFSL		(1 << 1) /* TX Frame Sync Length */
#define		SSI_STCR_TEFS		(1 << 0) /* TX Early Frame Sync */

#define	IMX51_SSI_SRCR_REG	0x0020 /* SSI RX Configuration Register */
#define		SSI_SRCR_RXEXT		(1 << 10) /* RX Data Extension */
#define		SSI_SRCR_RXBIT0		(1 << 9) /* RX Bit 0 */
#define		SSI_SRCR_RFEN1		(1 << 8) /* RX FIFO Enable 1 */
#define		SSI_SRCR_RFEN0		(1 << 7) /* RX FIFO Enable 0 */
#define		SSI_SRCR_RFDIR		(1 << 6) /* RX Frame Direction */
#define		SSI_SRCR_RXDIR		(1 << 5) /* RX Clock Direction */
#define		SSI_SRCR_RSHFD		(1 << 4) /* RX Shift Direction */
#define		SSI_SRCR_RSCKP		(1 << 3) /* RX Clock Polarity */
#define		SSI_SRCR_RFSI		(1 << 2) /* RX Frame Sync Invert */
#define		SSI_SRCR_RFSL		(1 << 1) /* RX Frame Sync Length */
#define		SSI_SRCR_REFS		(1 << 0) /* RX Early Frame Sync */

#define	IMX51_SSI_STCCR_REG	0x0024 /* TX Clock Control */
#define	IMX51_SSI_SRCCR_REG	0x0028 /* RX Clock Control */
#define		SSI_SXCCR_DIV2		(1 << 18) /* Divide By 2 */
#define		SSI_SXCCR_PSR		(1 << 17) /* Prescaler Range */
#define		SSI_SXCCR_WL_MASK	0x0001e000
#define		SSI_SXCCR_WL_SHIFT	13 /* Word Length Control */
#define		SSI_SXCCR_DC_MASK	0x00001f00
#define		SSI_SXCCR_DC_SHIFT	8 /* Frame Rate Divider */
#define		SSI_SXCCR_PM_MASK	0x000000ff
#define		SSI_SXCCR_PM_SHIFT	0 /* Prescaler Modulus */

#define	IMX51_SSI_SFCSR_REG	0x002C /* SSI FIFO Control/Status Register */
#define		SSI_SFCSR_RFCNT1_MASK	0xf0000000
#define		SSI_SFCSR_RFCNT1_SHIFT	28 /* RX FIFO Counter 1 */
#define		SSI_SFCSR_TFCNT1_MASK	0x0f000000
#define		SSI_SFCSR_TFCNT1_SHIFT	24 /* TX FIFO Counter 1 */
#define		SSI_SFCSR_RFWM1_MASK	0x00f00000
#define		SSI_SFCSR_RFWM1_SHIFT	20 /* RX FIFO Full WaterMark 1 */
#define		SSI_SFCSR_TFWM1_MASK	0x000f0000
#define		SSI_SFCSR_TFWM1_SHIFT	16 /* TX FIFO Empty WaterMark 1 */
#define		SSI_SFCSR_RFCNT0_MASK	0x0000f000
#define		SSI_SFCSR_RFCNT0_SHIFT	12 /* RX FIFO Counter 0 */
#define		SSI_SFCSR_TFCNT0_MASK	0x00000f00
#define		SSI_SFCSR_TFCNT0_SHIFT	8 /* TX FIFO Counter 0 */
#define		SSI_SFCSR_RFWM0_MASK	0x000000f0
#define		SSI_SFCSR_RFWM0_SHIFT	4 /* RX FIFO Full WaterMark 0 */
#define		SSI_SFCSR_TFWM0_MASK	0x0000000f
#define		SSI_SFCSR_TFWM0_SHIFT	0 /* TX FIFO Empty WaterMark 0 */

#define	IMX51_SSI_STR_REG	0x0030 /* SSI Test Register1 */
#define		SSI_STR_TEST		(1 << 15) /* Test Mode */
#define		SSI_STR_RCK2TCK		(1 << 14) /* RX<->TX Clock Loop Back */
#define		SSI_STR_RFS2TFS		(1 << 13) /* RX<->TX Frame Loop Back */
#define		SSI_STR_RXSTATE_MASK	0x00001f00
#define		SSI_STR_RXSTATE_SHIFT	8 /* RXer State Machine Status */
#define		SSI_STR_TXD2RXD		(1 << 7) /* TX<->RX Data Loop Back */
#define		SSI_STR_TCK2RCK		(1 << 6) /* TX<->RX Clock Loop Back */
#define		SSI_STR_TFS2RFS		(1 << 5) /* TX<->RX Frame Loop Back */
#define		SSI_STR_TXSTATE_MASK	0x0000001f
#define		SSI_STR_TXSTATE_SHIFT	0 /* TXer State Machine Status */

#define	IMX51_SSI_SOR_REG	0x0034 /* SSI Option Register2 */
#define		SSI_SOR_CLKOFF		(1 << 6) /* Clock Off */
#define		SSI_SOR_RX_CLR		(1 << 5) /* RXer Clear */
#define		SSI_SOR_TX_CLR		(1 << 4) /* TXer Clear */
#define		SSI_SOR_INIT		(1 << 3) /* Initialize */
#define		SSI_SOR_WAIT_MASK	0x00000006
#define		SSI_SOR_INIT_SHIFT	1 /* Wait */
#define		SSI_SOR_SYNRST		(1 << 0) /* Frame Sync Reset */

#define	IMX51_SSI_SACNT_REG	0x0038 /* SSI AC97 Control Register */
#define		SSI_SACNT_FRDIV_MASK	0x000007e0
#define		SSI_SACNT_FRDIV_SHIFT	5 /* Frame Rate Divider */
#define		SSI_SACNT_WR		(1 << 4) /* Write Command */
#define		SSI_SACNT_RD		(1 << 3) /* Read Command */
#define		SSI_SACNT_TIF		(1 << 2) /* Tag in FIFO */
#define		SSI_SACNT_FV		(1 << 1) /* Fixed/Variable Operation */
#define		SSI_SACNT_AC97EN	(1 << 0) /* AC97 Mode Enable */

#define	IMX51_SSI_SACADD_REG	0x003C /* SSI AC97 Command Address Register */
#define		SSI_SACADD_MASK		0x0007ffff
#define	IMX51_SSI_SACDAT_REG	0x0040 /* SSI AC97 Command Data Register */
#define		SSI_SACDAT_MASK		0x000fffff
#define	IMX51_SSI_SATAG_REG	0x0044 /* SSI AC97 Tag Register */
#define		SSI_SATAG_MASK		0x0000ffff
#define	IMX51_SSI_STMSK_REG	0x0048 /* SSI TX Time Slot Mask Register */
#define	IMX51_SSI_SRMSK_REG	0x004C /* SSI RX Time Slot Mask Register */
#define	IMX51_SSI_SACCST_REG	0x0050 /* SSI AC97 Channel Status Register */
#define	IMX51_SSI_SACCEN_REG	0x0054 /* SSI AC97 Channel Enable Register */
#define	IMX51_SSI_SACCDIS_REG	0x0058 /* SSI AC97 Channel Disable Register */
#define		SSI_SAC_MASK		0x000003ff /* SACCST,SACCEN,SACCDIS */
