/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2006 Benno Rice.  All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 *
 */

#ifndef	_IF_SMCREG_H_
#define	_IF_SMCREG_H_

/* All Banks, Offset 0xe: Bank Select Register */
#define	BSR			0xe
#define	BSR_BANK_MASK		0x0007	/* Which bank is currently selected */
#define	BSR_IDENTIFY		0x3300	/* Static value for identification */
#define	BSR_IDENTIFY_MASK	0xff00

/* Bank 0, Offset 0x0: Transmit Control Register */
#define	TCR			0x0
#define	TCR_TXENA		0x0001	/* Enable/disable transmitter */
#define	TCR_LOOP		0x0002	/* Put the PHY into loopback mode */
#define	TCR_FORCOL		0x0004	/* Force a collision */
#define	TCR_PAD_EN		0x0080	/* Pad TX frames to 64 bytes */
#define	TCR_NOCRC		0x0100	/* Disable/enable CRC */
#define	TCR_MON_CSN		0x0400	/* Monitor carrier signal */
#define	TCR_FDUPLX		0x0800	/* Enable/disable full duplex */
#define	TCR_STP_SQET		0x1000	/* Stop TX on signal quality error */
#define	TCR_EPH_LOOP		0x2000	/* Internal loopback */
#define	TCR_SWFDUP		0x8000	/* Switched full duplex */

/* Bank 0, Offset 0x2: EPH Status Register */
#define	EPHSR			0x2
#define	EPHSR_TX_SUC		0x0001	/* Last TX was successful */
#define	EPHSR_SNGLCOL		0x0002	/* Single collision on last TX */
#define	EPHSR_MULCOL		0x0004	/* Multiple collisions on last TX */
#define	EPHSR_LTX_MULT		0x0008	/* Last TX was multicast */
#define	EPHSR_16COL		0x0010	/* 16 collisions on last TX */
#define	EPHSR_SQET		0x0020	/* Signal quality error test */
#define	EPHSR_LTX_BRD		0x0040	/* Last TX was broadcast */
#define	EPHSR_TX_DEFR		0x0080	/* Transmit deferred */
#define	EPHSR_LATCOL		0x0200	/* Late collision on last TX */
#define	EPHSR_LOST_CARR		0x0400	/* Lost carrier sense */
#define	EPHSR_EXC_DEF		0x0800	/* Excessive deferral */
#define	EPHSR_CTR_ROL		0x1000	/* Counter rollover */
#define	EPHSR_LINK_OK		0x4000	/* Inverse of nLNK pin */
#define	EPHSR_TXUNRN		0x8000	/* Transmit underrun */

/* Bank 0, Offset 0x4: Receive Control Register */
#define	RCR			0x4
#define	RCR_RX_ABORT		0x0001	/* RX aborted */
#define	RCR_PRMS		0x0002	/* Enable/disable promiscuous mode */
#define	RCR_ALMUL		0x0004	/* Accept all multicast frames */
#define	RCR_RXEN		0x0100	/* Enable/disable receiver */
#define	RCR_STRIP_CRC		0x0200	/* Strip CRC from RX packets */
#define	RCR_ABORT_ENB		0x2000	/* Abort RX on collision */
#define	RCR_FILT_CAR		0x4000	/* Filter leading 12 bits of carrier */
#define	RCR_SOFT_RST		0x8000	/* Software reset */

/* Bank 0, Offset 0x6: Counter Register */
#define	ECR			0x6
#define	ECR_SNGLCOL_MASK	0x000f	/* Single collisions */
#define	ECR_SNGLCOL_SHIFT	0
#define	ECR_MULCOL_MASK		0x00f0	/* Multiple collisions */
#define	ECR_MULCOL_SHIFT	4
#define	ECR_TX_DEFR_MASK	0x0f00	/* Transmit deferrals */
#define	ECR_TX_DEFR_SHIFT	8
#define	ECR_EXC_DEF_MASK	0xf000	/* Excessive deferrals */
#define	ECR_EXC_DEF_SHIFT	12

/* Bank 0, Offset 0x8: Memory Information Register */
#define	MIR			0x8
#define	MIR_SIZE_MASK		0x00ff	/* Memory size (2k pages) */
#define	MIR_SIZE_SHIFT		0
#define	MIR_FREE_MASK		0xff00	/* Memory free (2k pages) */
#define	MIR_FREE_SHIFT		8
#define	MIR_PAGE_SIZE		2048

/* Bank 0, Offset 0xa: Receive/PHY Control Reigster */
#define	RPCR			0xa
#define	RPCR_ANEG		0x0800	/* Put PHY in autonegotiation mode */
#define	RPCR_DPLX		0x1000	/* Put PHY in full-duplex mode */
#define	RPCR_SPEED		0x2000	/* Manual speed selection */
#define	RPCR_LSA_MASK		0x00e0	/* Select LED A function */
#define	RPCR_LSA_SHIFT		5
#define	RPCR_LSB_MASK		0x001c	/* Select LED B function */
#define	RPCR_LSB_SHIFT		2
#define	RPCR_LED_LINK_ANY	0x0	/* 10baseT or 100baseTX link detected */
#define	RPCR_LED_LINK_10	0x2	/* 10baseT link detected */
#define	RPCR_LED_LINK_FDX	0x3	/* Full-duplex link detected */
#define	RPCR_LED_LINK_100	0x5	/* 100baseTX link detected */
#define	RPCR_LED_ACT_ANY	0x4	/* TX or RX activity detected */
#define	RPCR_LED_ACT_RX		0x6	/* RX activity detected */
#define	RPCR_LED_ACT_TX		0x7	/* TX activity detected */

/* Bank 1, Offset 0x0: Configuration Register */
#define	CR			0x0
#define	CR_EXT_PHY		0x0200	/* Enable/disable external PHY */
#define	CR_GPCNTRL		0x0400	/* Inverse drives nCNTRL pin */
#define	CR_NO_WAIT		0x1000	/* Do not request additional waits */
#define	CR_EPH_POWER_EN		0x8000	/* Disable/enable low power mode */

/* Bank 1, Offset 0x2: Base Address Register */
#define	BAR			0x2
#define	BAR_HIGH_MASK		0xe000
#define	BAR_LOW_MASK		0x1f00
#define	BAR_LOW_SHIFT		4
#define	BAR_ADDRESS(val)	\
	((val & BAR_HIGH_MASK) | ((val & BAR_LOW_MASK) >> BAR_LOW_SHIFT))

/* Bank 1, Offsets 0x4: Individual Address Registers */
#define	IAR0			0x4
#define	IAR1			0x5
#define	IAR2			0x6
#define	IAR3			0x7
#define	IAR4			0x8
#define	IAR5			0x9

/* Bank 1, Offset 0xa: General Purpose Register */
#define	GPR			0xa

/* Bank 1, Offset 0xc: Control Register */
#define	CTR			0xa
#define	CTR_STORE		0x0001	/* Store registers to EEPROM */
#define	CTR_RELOAD		0x0002	/* Reload registers from EEPROM */
#define	CTR_EEPROM_SELECT	0x0004	/* Select registers to store/reload */
#define	CTR_TE_ENABLE		0x0020	/* TX error causes EPH interrupt */
#define	CTR_CR_ENABLE		0x0040	/* Ctr rollover causes EPH interrupt */
#define	CTR_LE_ENABLE		0x0080	/* Link error causes EPH interrupt */
#define	CTR_AUTO_RELEASE	0x0800	/* Automatically release TX packets */
#define	CTR_RCV_BAD		0x4000	/* Receive/discard bad CRC packets */

/* Bank 2, Offset 0x0: MMU Command Register */
#define	MMUCR			0x0
#define	MMUCR_BUSY		0x0001	/* MMU is busy */
#define	MMUCR_CMD_NOOP		(0<<5)	/* No operation */
#define	MMUCR_CMD_TX_ALLOC	(1<<5)	/* Alloc TX memory (256b chunks) */
#define	MMUCR_CMD_MMU_RESET	(2<<5)	/* Reset MMU */
#define	MMUCR_CMD_REMOVE	(3<<5)	/* Remove frame from RX FIFO */
#define	MMUCR_CMD_RELEASE	(4<<5)	/* Remove and release from RX FIFO */
#define	MMUCR_CMD_RELEASE_PKT	(5<<5)	/* Release packet specified in PNR */
#define	MMUCR_CMD_ENQUEUE	(6<<5)	/* Enqueue packet for TX */
#define	MMUCR_CMD_TX_RESET	(7<<5)	/* Reset TX FIFOs */

/* Bank 2, Offset 0x2: Packet Number Register */
#define	PNR			0x2
#define	PNR_MASK		0x3fff

/* Bank 2, Offset 0x3: Allocation Result Register */
#define	ARR			0x3
#define	ARR_FAILED		0x8000	/* Last allocation request failed */
#define	ARR_MASK		0x3000

/* Bank 2, Offset 0x4: FIFO Ports Register */
#define	FIFO_TX			0x4
#define	FIFO_RX			0x5
#define	FIFO_EMPTY		0x80	/* FIFO empty */
#define	FIFO_PACKET_MASK	0x3f	/* Packet number mask */

/* Bank 2, Offset 0x6: Pointer Register */
#define	PTR			0x6
#define	PTR_MASK		0x07ff	/* Address accessible within TX/RX */
#define	PTR_NOT_EMPTY		0x0800	/* Write Data FIFO not empty */
#define	PTR_ETEN		0x1000	/* Enable early TX underrun detection */
#define	PTR_READ		0x2000	/* Set read/write */
#define	PTR_AUTO_INCR		0x4000	/* Auto increment on read/write */
#define	PTR_RCV			0x8000	/* Read/write to/from RX/TX */

/* Bank 2, Offset 0x8: Data Registers */
#define	DATA0			0x8
#define	DATA1			0xa

/* Bank 2, Offset 0xc: Interrupt Status Registers */
#define	IST			0xc	/* read only */
#define	ACK			0xc	/* write only */
#define	MSK			0xd

#define	RCV_INT			0x0001	/* RX */
#define	TX_INT			0x0002	/* TX */
#define	TX_EMPTY_INT		0x0004	/* TX empty */
#define	ALLOC_INT		0x0008	/* Allocation complete */
#define	RX_OVRN_INT		0x0010	/* RX overrun */
#define	EPH_INT			0x0020	/* EPH interrupt */
#define	ERCV_INT		0x0040	/* Early RX */
#define	MD_INT			0x0080	/* MII */

#define	IST_PRINTF		"\20\01RCV\02TX\03TX_EMPTY\04ALLOC" \
				"\05RX_OVRN\06EPH\07ERCV\10MD"

/* Bank 3, Offset 0x0: Multicast Table Registers */
#define	MT			0x0

/* Bank 3, Offset 0x8: Management Interface */
#define	MGMT			0x8
#define	MGMT_MDO		0x0001	/* MII management output */
#define	MGMT_MDI		0x0002	/* MII management input */
#define	MGMT_MCLK		0x0004	/* MII management clock */
#define	MGMT_MDOE		0x0008	/* MII management output enable */
#define	MGMT_MSK_CRS100		0x4000	/* Disable CRS100 detection during TX */

/* Bank 3, Offset 0xa: Revision Register */
#define	REV			0xa
#define	REV_CHIP_MASK		0x00f0	/* Chip ID */
#define	REV_CHIP_SHIFT		4
#define	REV_REV_MASK		0x000f	/* Revision ID */
#define	REV_REV_SHIFT		0

#define	REV_CHIP_9192		3
#define	REV_CHIP_9194		4
#define	REV_CHIP_9195		5
#define	REV_CHIP_9196		6
#define	REV_CHIP_91100		7
#define	REV_CHIP_91100FD	8
#define	REV_CHIP_91110FD	9

/* Bank 3, Offset 0xc: Early RCV Register */
#define	ERCV			0xc
#define	ERCV_THRESHOLD_MASK	0x001f	/* ERCV int threshold (64b chunks) */
#define	ERCV_RCV_DISCARD	0x0080	/* Discard packet being received */

/* Control Byte */
#define	CTRL_CRC		0x10	/* Frame has CRC */
#define	CTRL_ODD		0x20	/* Frame has odd byte count */

/* Receive Frame Status */
#define	RX_MULTCAST		0x0001	/* Frame was multicast */
#define	RX_HASH_MASK		0x007e	/* Hash value for multicast */
#define	RX_HASH_SHIFT		1
#define	RX_TOOSHORT		0x0400	/* Frame was too short */
#define	RX_TOOLNG		0x0800	/* Frame was too long */
#define	RX_ODDFRM		0x1000	/* Frame has odd number of bytes */
#define	RX_BADCRC		0x2000	/* Frame failed CRC */
#define	RX_BROADCAST		0x4000	/* Frame was broadcast */
#define	RX_ALGNERR		0x8000	/* Frame had alignment error */
#define	RX_LEN_MASK		0x07ff

/* Length of status word + byte count + control bytes for packets */
#define	PKT_CTRL_DATA_LEN	6

/* Number of times to spin on TX allocations */
#define	TX_ALLOC_WAIT_TIME	1000

#endif /* IF_SMCREG_H_ */
