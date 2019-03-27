/*	$NetBSD: if_udavreg.h,v 1.2 2003/09/04 15:17:39 tsutsui Exp $	*/
/*	$nabe: if_udavreg.h,v 1.2 2003/08/21 16:26:40 nabe Exp $	*/
/*	$FreeBSD$	*/
/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2003
 *     Shingo WATANABE <nabe@nabechan.org>.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the author nor the names of any co-contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
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
 */

#define	UDAV_IFACE_INDEX	0
#define	UDAV_CONFIG_INDEX	0	/* config number 1 */

#define	UDAV_TX_TIMEOUT		1000
#define	UDAV_TIMEOUT		10000

#define	UDAV_TX_TIMEOUT		1000
#define	UDAV_TIMEOUT		10000

/* Packet length */
#define	UDAV_MIN_FRAME_LEN	60

/* Request */
#define	UDAV_REQ_REG_READ	0x00	/* Read from register(s) */
#define	UDAV_REQ_REG_WRITE	0x01	/* Write to register(s) */
#define	UDAV_REQ_REG_WRITE1	0x03	/* Write to a register */

#define	UDAV_REQ_MEM_READ	0x02	/* Read from memory */
#define	UDAV_REQ_MEM_WRITE	0x05	/* Write to memory */
#define	UDAV_REQ_MEM_WRITE1	0x07	/* Write a byte to memory */

/* Registers */
#define	UDAV_NCR		0x00	/* Network Control Register */
#define	UDAV_NCR_EXT_PHY	(1<<7)	/* Select External PHY */
#define	UDAV_NCR_WAKEEN	(1<<6)		/* Wakeup Event Enable */
#define	UDAV_NCR_FCOL		(1<<4)	/* Force Collision Mode */
#define	UDAV_NCR_FDX		(1<<3)	/* Full-Duplex Mode (RO on Int. PHY) */
#define	UDAV_NCR_LBK1		(1<<2)	/* Lookback Mode */
#define	UDAV_NCR_LBK0		(1<<1)	/* Lookback Mode */
#define	UDAV_NCR_RST		(1<<0)	/* Software reset */

#define	UDAV_RCR		0x05	/* RX Control Register */
#define	UDAV_RCR_WTDIS		(1<<6)	/* Watchdog Timer Disable */
#define	UDAV_RCR_DIS_LONG	(1<<5)	/* Discard Long Packet(over 1522Byte) */
#define	UDAV_RCR_DIS_CRC	(1<<4)	/* Discard CRC Error Packet */
#define	UDAV_RCR_ALL		(1<<3)	/* Pass All Multicast */
#define	UDAV_RCR_RUNT		(1<<2)	/* Pass Runt Packet */
#define	UDAV_RCR_PRMSC		(1<<1)	/* Promiscuous Mode */
#define	UDAV_RCR_RXEN		(1<<0)	/* RX Enable */

#define	UDAV_RSR		0x06	/* RX Status Register */
#define	UDAV_RSR_RF		(1<<7)	/* Runt Frame */
#define	UDAV_RSR_MF		(1<<6)	/* Multicast Frame */
#define	UDAV_RSR_LCS		(1<<5)	/* Late Collision Seen */
#define	UDAV_RSR_RWTO		(1<<4)	/* Receive Watchdog Time-Out */
#define	UDAV_RSR_PLE		(1<<3)	/* Physical Layer Error */
#define	UDAV_RSR_AE		(1<<2)	/* Alignment Error */
#define	UDAV_RSR_CE		(1<<1)	/* CRC Error */
#define	UDAV_RSR_FOE		(1<<0)	/* FIFO Overflow Error */
#define	UDAV_RSR_ERR		(UDAV_RSR_RF | UDAV_RSR_LCS |		\
				    UDAV_RSR_RWTO | UDAV_RSR_PLE |	\
				    UDAV_RSR_AE | UDAV_RSR_CE | UDAV_RSR_FOE)

#define	UDAV_EPCR		0x0b	/* EEPROM & PHY Control Register */
#define	UDAV_EPCR_REEP		(1<<5)	/* Reload EEPROM */
#define	UDAV_EPCR_WEP		(1<<4)	/* Write EEPROM enable */
#define	UDAV_EPCR_EPOS		(1<<3)	/* EEPROM or PHY Operation Select */
#define	UDAV_EPCR_ERPRR		(1<<2)	/* EEPROM/PHY Register Read Command */
#define	UDAV_EPCR_ERPRW		(1<<1)	/* EEPROM/PHY Register Write Command */
#define	UDAV_EPCR_ERRE		(1<<0)	/* EEPROM/PHY Access Status */

#define	UDAV_EPAR		0x0c	/* EEPROM & PHY Control Register */
#define	UDAV_EPAR_PHY_ADR1	(1<<7)	/* PHY Address bit 1 */
#define	UDAV_EPAR_PHY_ADR0	(1<<6)	/* PHY Address bit 0 */
#define	UDAV_EPAR_EROA		(1<<0)	/* EEPROM Word/PHY Register Address */
#define	UDAV_EPAR_EROA_MASK	(0x1f)	/* [5:0] */

#define	UDAV_EPDRL		0x0d	/* EEPROM & PHY Data Register */
#define	UDAV_EPDRH		0x0e	/* EEPROM & PHY Data Register */

#define	UDAV_PAR0		0x10	/* Ethernet Address, load from EEPROM */
#define	UDAV_PAR1		0x11	/* Ethernet Address, load from EEPROM */
#define	UDAV_PAR2		0x12	/* Ethernet Address, load from EEPROM */
#define	UDAV_PAR3		0x13	/* Ethernet Address, load from EEPROM */
#define	UDAV_PAR4		0x14	/* Ethernet Address, load from EEPROM */
#define	UDAV_PAR5		0x15	/* Ethernet Address, load from EEPROM */
#define	UDAV_PAR		UDAV_PAR0

#define	UDAV_MAR0		0x16	/* Multicast Register */
#define	UDAV_MAR1		0x17	/* Multicast Register */
#define	UDAV_MAR2		0x18	/* Multicast Register */
#define	UDAV_MAR3		0x19	/* Multicast Register */
#define	UDAV_MAR4		0x1a	/* Multicast Register */
#define	UDAV_MAR5		0x1b	/* Multicast Register */
#define	UDAV_MAR6		0x1c	/* Multicast Register */
#define	UDAV_MAR7		0x1d	/* Multicast Register */
#define	UDAV_MAR		UDAV_MAR0

#define	UDAV_GPCR		0x1e	/* General purpose control register */
#define	UDAV_GPCR_GEP_CNTL6	(1<<6)	/* General purpose control 6 */
#define	UDAV_GPCR_GEP_CNTL5	(1<<5)	/* General purpose control 5 */
#define	UDAV_GPCR_GEP_CNTL4	(1<<4)	/* General purpose control 4 */
#define	UDAV_GPCR_GEP_CNTL3	(1<<3)	/* General purpose control 3 */
#define	UDAV_GPCR_GEP_CNTL2	(1<<2)	/* General purpose control 2 */
#define	UDAV_GPCR_GEP_CNTL1	(1<<1)	/* General purpose control 1 */
#define	UDAV_GPCR_GEP_CNTL0	(1<<0)	/* General purpose control 0 */

#define	UDAV_GPR		0x1f	/* General purpose register */
#define	UDAV_GPR_GEPIO6		(1<<6)	/* General purpose 6 */
#define	UDAV_GPR_GEPIO5		(1<<5)	/* General purpose 5 */
#define	UDAV_GPR_GEPIO4		(1<<4)	/* General purpose 4 */
#define	UDAV_GPR_GEPIO3		(1<<3)	/* General purpose 3 */
#define	UDAV_GPR_GEPIO2		(1<<2)	/* General purpose 2 */
#define	UDAV_GPR_GEPIO1		(1<<1)	/* General purpose 1 */
#define	UDAV_GPR_GEPIO0		(1<<0)	/* General purpose 0 */

#define	GET_MII(sc)		uether_getmii(&(sc)->sc_ue)

struct udav_rxpkt {
	uint8_t	rxstat;
	uint16_t pktlen;
} __packed;

enum {
	UDAV_BULK_DT_WR,
	UDAV_BULK_DT_RD,
	UDAV_INTR_DT_RD,
	UDAV_N_TRANSFER,
};

struct udav_softc {
	struct usb_ether	sc_ue;
	struct mtx		sc_mtx;
	struct usb_xfer	*sc_xfer[UDAV_N_TRANSFER];

	int			sc_flags;
#define	UDAV_FLAG_LINK		0x0001
#define	UDAV_FLAG_EXT_PHY	0x0040
#define	UDAV_FLAG_NO_PHY	0x0080
};

#define	UDAV_LOCK(_sc)			mtx_lock(&(_sc)->sc_mtx)
#define	UDAV_UNLOCK(_sc)		mtx_unlock(&(_sc)->sc_mtx)
#define	UDAV_LOCK_ASSERT(_sc, t)	mtx_assert(&(_sc)->sc_mtx, t)
