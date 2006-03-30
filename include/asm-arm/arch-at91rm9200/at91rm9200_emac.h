/*
 * include/asm-arm/arch-at91rm9200/at91rm9200_emac.h
 *
 * Copyright (C) 2005 Ivan Kokshaysky
 * Copyright (C) SAN People
 *
 * Ethernet MAC registers.
 * Based on AT91RM9200 datasheet revision E.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef AT91RM9200_EMAC_H
#define AT91RM9200_EMAC_H

#define	AT91_EMAC_CTL		0x00	/* Control Register */
#define		AT91_EMAC_LB		(1 <<  0)	/* Loopback */
#define		AT91_EMAC_LBL		(1 <<  1)	/* Loopback Local */
#define		AT91_EMAC_RE		(1 <<  2)	/* Receive Enable */
#define		AT91_EMAC_TE		(1 <<  3)	/* Transmit Enable */
#define		AT91_EMAC_MPE		(1 <<  4)	/* Management Port Enable */
#define		AT91_EMAC_CSR		(1 <<  5)	/* Clear Statistics Registers */
#define		AT91_EMAC_INCSTAT	(1 <<  6)	/* Increment Statistics Registers */
#define		AT91_EMAC_WES		(1 <<  7)	/* Write Enable for Statistics Registers */
#define		AT91_EMAC_BP		(1 <<  8)	/* Back Pressure */

#define	AT91_EMAC_CFG		0x04	/* Configuration Register */
#define		AT91_EMAC_SPD		(1 <<  0)	/* Speed */
#define		AT91_EMAC_FD		(1 <<  1)	/* Full Duplex */
#define		AT91_EMAC_BR		(1 <<  2)	/* Bit Rate */
#define		AT91_EMAC_CAF		(1 <<  4)	/* Copy All Frames */
#define		AT91_EMAC_NBC		(1 <<  5)	/* No Broadcast */
#define		AT91_EMAC_MTI		(1 <<  6)	/* Multicast Hash Enable */
#define		AT91_EMAC_UNI		(1 <<  7)	/* Unicast Hash Enable */
#define		AT91_EMAC_BIG		(1 <<  8)	/* Receive 1522 Bytes */
#define		AT91_EMAC_EAE		(1 <<  9)	/* External Address Match Enable */
#define		AT91_EMAC_CLK		(3 << 10)	/* MDC Clock Divisor */
#define		AT91_EMAC_CLK_DIV8		(0 << 10)
#define		AT91_EMAC_CLK_DIV16		(1 << 10)
#define		AT91_EMAC_CLK_DIV32		(2 << 10)
#define		AT91_EMAC_CLK_DIV64		(3 << 10)
#define		AT91_EMAC_RTY		(1 << 12)	/* Retry Test */
#define		AT91_EMAC_RMII		(1 << 13)	/* Reduce MII (RMII) */

#define	AT91_EMAC_SR		0x08	/* Status Register */
#define		AT91_EMAC_SR_LINK	(1 <<  0)	/* Link */
#define		AT91_EMAC_SR_MDIO	(1 <<  1)	/* MDIO pin */
#define		AT91_EMAC_SR_IDLE	(1 <<  2)	/* PHY idle */

#define	AT91_EMAC_TAR		0x0c	/* Transmit Address Register */

#define	AT91_EMAC_TCR		0x10	/* Transmit Control Register */
#define		AT91_EMAC_LEN		(0x7ff << 0)	/* Transmit Frame Length */
#define		AT91_EMAC_NCRC		(1     << 15)	/* No CRC */

#define	AT91_EMAC_TSR		0x14	/* Transmit Status Register */
#define		AT91_EMAC_TSR_OVR	(1 <<  0)	/* Transmit Buffer Overrun */
#define		AT91_EMAC_TSR_COL	(1 <<  1)	/* Collision Occurred */
#define		AT91_EMAC_TSR_RLE	(1 <<  2)	/* Retry Limit Exceeded */
#define		AT91_EMAC_TSR_IDLE	(1 <<  3)	/* Transmitter Idle */
#define		AT91_EMAC_TSR_BNQ	(1 <<  4)	/* Transmit Buffer not Queued */
#define		AT91_EMAC_TSR_COMP	(1 <<  5)	/* Transmit Complete */
#define		AT91_EMAC_TSR_UND	(1 <<  6)	/* Transmit Underrun */

#define	AT91_EMAC_RBQP		0x18	/* Receive Buffer Queue Pointer */

#define	AT91_EMAC_RSR		0x20	/* Receive Status Register */
#define		AT91_EMAC_RSR_BNA	(1 <<  0)	/* Buffer Not Available */
#define		AT91_EMAC_RSR_REC	(1 <<  1)	/* Frame Received */
#define		AT91_EMAC_RSR_OVR	(1 <<  2)	/* RX Overrun */

#define	AT91_EMAC_ISR		0x24	/* Interrupt Status Register */
#define		AT91_EMAC_DONE		(1 <<  0)	/* Management Done */
#define		AT91_EMAC_RCOM		(1 <<  1)	/* Receive Complete */
#define		AT91_EMAC_RBNA		(1 <<  2)	/* Receive Buffer Not Available */
#define		AT91_EMAC_TOVR		(1 <<  3)	/* Transmit Buffer Overrun */
#define		AT91_EMAC_TUND		(1 <<  4)	/* Transmit Buffer Underrun */
#define		AT91_EMAC_RTRY		(1 <<  5)	/* Retry Limit */
#define		AT91_EMAC_TBRE		(1 <<  6)	/* Transmit Buffer Register Empty */
#define		AT91_EMAC_TCOM		(1 <<  7)	/* Transmit Complete */
#define		AT91_EMAC_TIDLE		(1 <<  8)	/* Transmit Idle */
#define		AT91_EMAC_LINK		(1 <<  9)	/* Link */
#define		AT91_EMAC_ROVR		(1 << 10)	/* RX Overrun */
#define		AT91_EMAC_ABT		(1 << 11)	/* Abort */

#define	AT91_EMAC_IER		0x28	/* Interrupt Enable Register */
#define	AT91_EMAC_IDR		0x2c	/* Interrupt Disable Register */
#define	AT91_EMAC_IMR		0x30	/* Interrupt Mask Register */

#define	AT91_EMAC_MAN		0x34	/* PHY Maintenance Register */
#define		AT91_EMAC_DATA		(0xffff << 0)	/* MDIO Data */
#define		AT91_EMAC_REGA		(0x1f	<< 18)	/* MDIO Register */
#define		AT91_EMAC_PHYA		(0x1f	<< 23)	/* MDIO PHY Address */
#define		AT91_EMAC_RW		(3	<< 28)	/* Read/Write operation */
#define			AT91_EMAC_RW_W		(1 << 28)
#define			AT91_EMAC_RW_R		(2 << 28)
#define		AT91_EMAC_MAN_802_3	0x40020000	/* IEEE 802.3 value */

/*
 * Statistics Registers.
 */
#define AT91_EMAC_FRA		0x40	/* Frames Transmitted OK */
#define AT91_EMAC_SCOL		0x44	/* Single Collision Frame */
#define AT91_EMAC_MCOL		0x48	/* Multiple Collision Frame */
#define AT91_EMAC_OK		0x4c	/* Frames Received OK */
#define AT91_EMAC_SEQE		0x50	/* Frame Check Sequence Error */
#define AT91_EMAC_ALE		0x54	/* Alignmemt Error */
#define AT91_EMAC_DTE		0x58	/* Deffered Transmission Frame */
#define AT91_EMAC_LCOL		0x5c	/* Late Collision */
#define AT91_EMAC_ECOL		0x60	/* Excessive Collision */
#define AT91_EMAC_TUE		0x64	/* Transmit Underrun Error */
#define AT91_EMAC_CSE		0x68	/* Carrier Sense Error */
#define AT91_EMAC_DRFC		0x6c	/* Discard RX Frame */
#define AT91_EMAC_ROV		0x70	/* Receive Overrun */
#define AT91_EMAC_CDE		0x74	/* Code Error */
#define AT91_EMAC_ELR		0x78	/* Excessive Length Error */
#define AT91_EMAC_RJB		0x7c	/* Receive Jabber */
#define AT91_EMAC_USF		0x80	/* Undersize Frame */
#define AT91_EMAC_SQEE		0x84	/* SQE Test Error */

/*
 * Address Registers.
 */
#define AT91_EMAC_HSL		0x90	/* Hash Address Low [31:0] */
#define AT91_EMAC_HSH		0x94	/* Hash Address High [63:32] */
#define AT91_EMAC_SA1L		0x98	/* Specific Address 1 Low, bytes 0-3 */
#define AT91_EMAC_SA1H		0x9c	/* Specific Address 1 High, bytes 4-5 */
#define AT91_EMAC_SA2L		0xa0	/* Specific Address 2 Low, bytes 0-3 */
#define AT91_EMAC_SA2H		0xa4	/* Specific Address 2 High, bytes 4-5 */
#define AT91_EMAC_SA3L		0xa8	/* Specific Address 3 Low, bytes 0-3 */
#define AT91_EMAC_SA3H		0xac	/* Specific Address 3 High, bytes 4-5 */
#define AT91_EMAC_SA4L		0xb0	/* Specific Address 4 Low, bytes 0-3 */
#define AT91_EMAC_SA4H		0xb4	/* Specific Address 4 High, bytes 4-5 */

#endif
