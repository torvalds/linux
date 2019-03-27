/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2001-2003, Shunsuke Akiyama <akiyama@FreeBSD.org>.
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

#define	RUE_CONFIG_IDX		0	/* config number 1 */
#define	RUE_IFACE_IDX		0

#define	RUE_INTR_PKTLEN		0x8

#define	RUE_TIMEOUT		50
#define	RUE_MIN_FRAMELEN	60

/* Registers. */
#define	RUE_IDR0		0x0120
#define	RUE_IDR1		0x0121
#define	RUE_IDR2		0x0122
#define	RUE_IDR3		0x0123
#define	RUE_IDR4		0x0124
#define	RUE_IDR5		0x0125

#define	RUE_MAR0		0x0126
#define	RUE_MAR1		0x0127
#define	RUE_MAR2		0x0128
#define	RUE_MAR3		0x0129
#define	RUE_MAR4		0x012A
#define	RUE_MAR5		0x012B
#define	RUE_MAR6		0x012C
#define	RUE_MAR7		0x012D

#define	RUE_CR			0x012E	/* B, R/W */
#define	RUE_CR_SOFT_RST		0x10
#define	RUE_CR_RE		0x08
#define	RUE_CR_TE		0x04
#define	RUE_CR_EP3CLREN		0x02

#define	RUE_TCR			0x012F	/* B, R/W */
#define	RUE_TCR_TXRR1		0x80
#define	RUE_TCR_TXRR0		0x40
#define	RUE_TCR_IFG1		0x10
#define	RUE_TCR_IFG0		0x08
#define	RUE_TCR_NOCRC		0x01
#define	RUE_TCR_CONFIG		(RUE_TCR_TXRR1 | RUE_TCR_TXRR0 | 	\
				    RUE_TCR_IFG1 | RUE_TCR_IFG0)

#define	RUE_RCR			0x0130	/* W, R/W */
#define	RUE_RCR_TAIL		0x80
#define	RUE_RCR_AER		0x40
#define	RUE_RCR_AR		0x20
#define	RUE_RCR_AM		0x10
#define	RUE_RCR_AB		0x08
#define	RUE_RCR_AD		0x04
#define	RUE_RCR_AAM		0x02
#define	RUE_RCR_AAP		0x01
#define	RUE_RCR_CONFIG		(RUE_RCR_TAIL | RUE_RCR_AD)

#define	RUE_TSR			0x0132
#define	RUE_RSR			0x0133
#define	RUE_CON0		0x0135
#define	RUE_CON1		0x0136
#define	RUE_MSR			0x0137
#define	RUE_PHYADD		0x0138
#define	RUE_PHYDAT		0x0139

#define	RUE_PHYCNT		0x013B	/* B, R/W */
#define	RUE_PHYCNT_PHYOWN	0x40
#define	RUE_PHYCNT_RWCR		0x20

#define	RUE_GPPC		0x013D
#define	RUE_WAKECNT		0x013E

#define	RUE_BMCR		0x0140
#define	RUE_BMCR_SPD_SET	0x2000
#define	RUE_BMCR_DUPLEX		0x0100

#define	RUE_BMSR		0x0142

#define	RUE_ANAR		0x0144	/* W, R/W */
#define	RUE_ANAR_PAUSE		0x0400

#define	RUE_ANLP		0x0146	/* W, R/O */
#define	RUE_ANLP_PAUSE		0x0400

#define	RUE_AER			0x0148

#define	RUE_NWAYT		0x014A
#define	RUE_CSCR		0x014C

#define	RUE_CRC0		0x014E
#define	RUE_CRC1		0x0150
#define	RUE_CRC2		0x0152
#define	RUE_CRC3		0x0154
#define	RUE_CRC4		0x0156

#define	RUE_BYTEMASK0		0x0158
#define	RUE_BYTEMASK1		0x0160
#define	RUE_BYTEMASK2		0x0168
#define	RUE_BYTEMASK3		0x0170
#define	RUE_BYTEMASK4		0x0178

#define	RUE_PHY1		0x0180
#define	RUE_PHY2		0x0184

#define	RUE_TW1			0x0186

#define	RUE_REG_MIN		0x0120
#define	RUE_REG_MAX		0x0189

/* EEPROM address declarations. */
#define	RUE_EEPROM_BASE		0x1200
#define	RUE_EEPROM_IDR0		(RUE_EEPROM_BASE + 0x02)
#define	RUE_EEPROM_IDR1		(RUE_EEPROM_BASE + 0x03)
#define	RUE_EEPROM_IDR2		(RUE_EEPROM_BASE + 0x03)
#define	RUE_EEPROM_IDR3		(RUE_EEPROM_BASE + 0x03)
#define	RUE_EEPROM_IDR4		(RUE_EEPROM_BASE + 0x03)
#define	RUE_EEPROM_IDR5		(RUE_EEPROM_BASE + 0x03)
#define	RUE_EEPROM_INTERVAL	(RUE_EEPROM_BASE + 0x17)

#define	RUE_RXSTAT_VALID	(0x01 << 12)
#define	RUE_RXSTAT_RUNT		(0x02 << 12)
#define	RUE_RXSTAT_PMATCH	(0x04 << 12)
#define	RUE_RXSTAT_MCAST	(0x08 << 12)

#define	GET_MII(sc)		uether_getmii(&(sc)->sc_ue)

struct rue_intrpkt {
	uint8_t	rue_tsr;
	uint8_t	rue_rsr;
	uint8_t	rue_gep_msr;
	uint8_t	rue_waksr;
	uint8_t	rue_txok_cnt;
	uint8_t	rue_rxlost_cnt;
	uint8_t	rue_crcerr_cnt;
	uint8_t	rue_col_cnt;
} __packed;

enum {
	RUE_BULK_DT_WR,
	RUE_BULK_DT_RD,
	RUE_INTR_DT_RD,
	RUE_N_TRANSFER,
};

struct rue_softc {
	struct usb_ether	sc_ue;
	struct mtx		sc_mtx;
	struct usb_xfer	*sc_xfer[RUE_N_TRANSFER];

	int			sc_flags;
#define	RUE_FLAG_LINK		0x0001
};

#define	RUE_LOCK(_sc)		mtx_lock(&(_sc)->sc_mtx)
#define	RUE_UNLOCK(_sc)		mtx_unlock(&(_sc)->sc_mtx)
#define	RUE_LOCK_ASSERT(_sc, t)	mtx_assert(&(_sc)->sc_mtx, t)
