/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (C) 2013 Ganbold Tsagaankhuu <ganbold@freebsd.org>
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

#ifndef	__IF_EMACREG_H__
#define	__IF_EMACREG_H__

/*
 * EMAC register definitions
 */
#define	EMAC_CTL		0x00
#define	EMAC_CTL_RST		(1 << 0)
#define	EMAC_CTL_TX_EN		(1 << 1)
#define	EMAC_CTL_RX_EN		(1 << 2)

#define	EMAC_TX_MODE		0x04
#define	EMAC_TX_FLOW		0x08
#define	EMAC_TX_CTL0		0x0C
#define	EMAC_TX_CTL1		0x10
#define	EMAC_TX_INS		0x14
#define	EMAC_TX_PL0		0x18
#define	EMAC_TX_PL1		0x1C
#define	EMAC_TX_STA		0x20
#define	EMAC_TX_IO_DATA		0x24
#define	EMAC_TX_IO_DATA1	0x28
#define	EMAC_TX_TSVL0		0x2C
#define	EMAC_TX_TSVH0		0x30
#define	EMAC_TX_TSVL1		0x34
#define	EMAC_TX_TSVH1		0x38
#define	EMAC_TX_FIFO0		(1 << 0)
#define	EMAC_TX_FIFO1		(1 << 1)

#define	EMAC_RX_CTL		0x3C
#define	EMAC_RX_HASH0		0x40
#define	EMAC_RX_HASH1		0x44
#define	EMAC_RX_STA		0x48
#define	EMAC_RX_IO_DATA		0x4C
#define	EMAC_RX_FBC		0x50

#define	EMAC_INT_CTL		0x54
#define	EMAC_INT_STA		0x58
#define	EMAC_INT_STA_TX		(EMAC_TX_FIFO0 | EMAC_TX_FIFO1)
#define	EMAC_INT_STA_RX		0x100
#define	EMAC_INT_EN		(0xf << 0) | (1 << 8)

#define	EMAC_MAC_CTL0		0x5C
#define	EMAC_MAC_CTL1		0x60
#define	EMAC_MAC_IPGT		0x64
#define	EMAC_MAC_IPGR		0x68
#define	EMAC_MAC_CLRT		0x6C
#define	EMAC_MAC_MAXF		0x70
#define	EMAC_MAC_SUPP		0x74
#define	EMAC_MAC_TEST		0x78
#define	EMAC_MAC_MCFG		0x7C
#define	EMAC_MAC_MCMD		0x80
#define	EMAC_MAC_MADR		0x84
#define	EMAC_MAC_MWTD		0x88
#define	EMAC_MAC_MRDD		0x8C
#define	EMAC_MAC_MIND		0x90
#define	EMAC_MAC_SSRR		0x94
#define	EMAC_MAC_A0		0x98
#define	EMAC_MAC_A1		0x9C
#define	EMAC_MAC_A2		0xA0

#define	EMAC_SAFX_L0		0xA4
#define	EMAC_SAFX_H0		0xA8
#define	EMAC_SAFX_L1		0xAC
#define	EMAC_SAFX_H1		0xB0
#define	EMAC_SAFX_L2		0xB4
#define	EMAC_SAFX_H2		0xB8
#define	EMAC_SAFX_L3		0xBC
#define	EMAC_SAFX_H3		0xC0

#define	EMAC_PHY_DUPLEX		(1 << 8)

/*
 * Each received packet has 8 bytes header:
 * Byte 0: Packet valid flag: 0x01 valid, 0x00 not valid
 * Byte 1: 0x43 -> Ascii code 'C'
 * Byte 2: 0x41 -> Ascii code 'A'
 * Byte 3: 0x4d -> Ascii code 'M'
 * Byte 4: High byte of received packet's status
 * Byte 5: Low byte of received packet's status
 * Byte 6: High byte of packet size
 * Byte 7: Low byte of packet size
 */
#define	EMAC_PACKET_HEADER	(0x0143414d)

/* Aborted frame enable */
#define	EMAC_TX_AB_M		(1 << 0)

/* 0: Enable CPU mode for TX, 1: DMA */
#define	EMAC_TX_TM		~(1 << 1)

/* 0: DRQ asserted, 1: DRQ automatically */
#define	EMAC_RX_DRQ_MODE	(1 << 1)

/* 0: Enable CPU mode for RX, 1: DMA */
#define	EMAC_RX_TM		~(1 << 2)

/* Pass all Frames */
#define	EMAC_RX_PA		(1 << 4)

/* Pass Control Frames */
#define	EMAC_RX_PCF		(1 << 5)

/* Pass Frames with CRC Error */
#define	EMAC_RX_PCRCE		(1 << 6)

/* Pass Frames with Length Error */
#define	EMAC_RX_PLE		(1 << 7)

/* Pass Frames length out of range */
#define	EMAC_RX_POR		(1 << 8)

/* Accept unicast Packets */
#define	EMAC_RX_UCAD		(1 << 16)

/* Enable DA Filtering */
#define	EMAC_RX_DAF		(1 << 17)

/* Accept multicast Packets */
#define	EMAC_RX_MCO		(1 << 20)

/* Enable Hash filter */
#define	EMAC_RX_MHF		(1 << 21)

/* Accept Broadcast Packets */
#define	EMAC_RX_BCO		(1 << 22)

/* Enable SA Filtering */
#define	EMAC_RX_SAF		(1 << 24)

/* Inverse Filtering */
#define	EMAC_RX_SAIF		(1 << 25)

#define	EMAC_RX_SETUP		(EMAC_RX_POR | EMAC_RX_UCAD | \
    EMAC_RX_DAF | EMAC_RX_MCO | EMAC_RX_BCO)

/* Enable Receive Flow Control */
#define	EMAC_MAC_CTL0_RFC	(1 << 2)

/* Enable Transmit Flow Control */
#define	EMAC_MAC_CTL0_TFC	(1 << 3)

/* Enable soft reset */
#define	EMAC_MAC_CTL0_SOFT_RST	(1 << 15)

#define	EMAC_MAC_CTL0_SETUP	(EMAC_MAC_CTL0_RFC | EMAC_MAC_CTL0_TFC)

/* Enable duplex */
#define	EMAC_MAC_CTL1_DUP	(1 << 0)

/* Enable MAC Frame Length Checking */
#define	EMAC_MAC_CTL1_FLC	(1 << 1)

/* Enable Huge Frame */
#define	EMAC_MAC_CTL1_HF	(1 << 2)

/* Enable MAC Delayed CRC */
#define	EMAC_MAC_CTL1_DCRC	(1 << 3)

/* Enable MAC CRC */
#define	EMAC_MAC_CTL1_CRC	(1 << 4)

/* Enable MAC PAD Short frames */
#define	EMAC_MAC_CTL1_PC	(1 << 5)

/* Enable MAC PAD Short frames and append CRC */
#define	EMAC_MAC_CTL1_VC	(1 << 6)

/* Enable MAC auto detect Short frames */
#define	EMAC_MAC_CTL1_ADP	(1 << 7)

#define	EMAC_MAC_CTL1_PRE	(1 << 8)
#define	EMAC_MAC_CTL1_LPE	(1 << 9)

/* Enable no back off */
#define	EMAC_MAC_CTL1_NB	(1 << 12)

#define	EMAC_MAC_CTL1_BNB	(1 << 13)
#define	EMAC_MAC_CTL1_ED	(1 << 14)

#define	EMAC_MAC_CTL1_SETUP	(EMAC_MAC_CTL1_FLC | EMAC_MAC_CTL1_CRC | \
    EMAC_MAC_CTL1_PC)

/* half duplex */
#define	EMAC_MAC_IPGT_HD	0x12

/* full duplex */
#define	EMAC_MAC_IPGT_FD	0x15

#define	EMAC_MAC_NBTB_IPG1	0xC
#define	EMAC_MAC_NBTB_IPG2	0x12

#define	EMAC_MAC_CW		0x37
#define	EMAC_MAC_RM		0xF

#define	EMAC_MAC_MFL		0x0600

/* Receive status */
#define	EMAC_CRCERR		(1 << 4)
#define	EMAC_LENERR		(3 << 5)
#define	EMAC_PKT_OK		(1 << 7)

#define	EMAC_RX_FLUSH_FIFO	(1 << 3)
#define	EMAC_PHY_RESET		(1 << 15)
#define	EMAC_PHY_PWRDOWN	(1 << 11)

#define	EMAC_PROC_MIN		16
#define	EMAC_PROC_MAX		255
#define	EMAC_PROC_DEFAULT	64

#define	EMAC_LOCK(cs)		mtx_lock(&(sc)->emac_mtx)
#define	EMAC_UNLOCK(cs)		mtx_unlock(&(sc)->emac_mtx)
#define	EMAC_ASSERT_LOCKED(sc)	mtx_assert(&(sc)->emac_mtx, MA_OWNED);

#endif	/* __IF_EMACREG_H__ */
