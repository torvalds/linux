/*	$OpenBSD: tqphyreg.h,v 1.2 2002/05/04 11:30:06 fgsch Exp $	*/
/*	$NetBSD: tqphyreg.h,v 1.2 1999/09/16 05:58:18 soren Exp $	*/

/*
 * Copyright (c) 1999 Soren S. Jorvang.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer.
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
 */

#ifndef _DEV_MII_TQPHYREG_H_
#define	_DEV_MII_TQPHYREG_H_

/*
 * TDK TSC78Q2120 PHY registers
 *
 * Documentation available at http://www.tsc.tdk.com/lan/78Q2120.pdf .
 */

/*
 * http://cesdis.gsfc.nasa.gov/linux/misc/100mbps.html has this to say:
 *
 * TDK Semiconductor (formerly Silicon Systems) 78Q2120 (10/100) and 78Q2121
 * (100Mbps only) MII transceivers. The first PHY available which worked at
 * both 5.0 and 3.3V. Used on the 3Com 3c574 and Ositech products. The OUI
 * is 00:c0:39, models 20 and 21.  Warning: The older revision 3 part has
 * several bugs. It always responds to MDIO address 0, and has clear-only
 * semantics for the capability-advertise registers. The current (3/99)
 * revision 11 part, shipping since 8/98, has reportedly fixed these problems.
 */

#define MII_TQPHY_VENDOR	0x10	/* Vendor specific register */
#define VENDOR_RPTR		0x8000	/* Repeater mode */
#define VENDOR_INTLEVEL		0x4000	/* INTR pin level */
#define VENDOR_RSVD1		0x2000	/* Reserved */
#define VENDOR_TXHIM		0x1000	/* Transmit high impedance */
#define VENDOR_SEQTESTINHIBIT	0x0800	/* Disables 10baseT SQE testing */
#define VENDOR_10BT_LOOPBACK	0x0400	/* 10baseT natural loopback */
#define VENDOR_GPIO1_DAT	0x0200	/* General purpose I/O 1 data */
#define VENDOR_GPIO1_DIR	0x0100	/* General purpose I/O 1 direction */
#define VENDOR_GPIO0_DAT	0x0080	/* General purpose I/O 0 data */
#define VENDOR_GPIO0_DIR	0x0040	/* General purpose I/O 0 direction */
#define VENDOR_APOL		0x0020	/* Auto polarity */
#define VENDOR_RVSPOL		0x0010	/* Reverse polarity */
#define VENDOR_RSVD2		0x0008	/* Reserved (must be zero) */
#define VENDOR_RSVD3		0x0004	/* Reserved (must be zero) */
#define VENDOR_PCSBP		0x0002	/* PCS bypass */
#define VENDOR_RXCC		0x0001	/* Receive clock control */

#define MII_TQPHY_INTR		0x11	/* Interrupt control/status register */
#define INTR_JABBER_IE		0x8000	/* Jabber interrupt enable */
#define INTR_RXER_IE		0x4000	/* Receive error enable */
#define INTR_PRX_IE		0x2000	/* Page received enable */
#define INTR_PFD_IE		0x1000	/* Parallel detect fault enable */
#define INTR_LPACK_IE		0x0800	/* Link partner ack. enable */
#define INTR_LSCHG_IE		0x0400	/* Link status change enable */
#define INTR_RFAULT_IE		0x0200	/* Remote fault enable */
#define INTR_ANEGCOMP_IE	0x0100	/* Autonegotiation complete enable */
#define INTR_JABBER_INT		0x0080	/* Jabber interrupt */
#define INTR_RXER_INT		0x0040	/* Receive error interrupt */
#define INTR_PRX_INT		0x0020	/* Page receive interrupt */
#define INTR_PDF_INT		0x0010	/* Parallel detect fault interrupt */
#define INTR_LPACK_INT		0x0008	/* Link partner ack. interrupt */
#define INTR_LSCHG_INT		0x0004	/* Link status change interrupt */
#define INTR_RFAULT_INT		0x0002	/* Remote fault interrupt */
#define INTR_ANEGCOMP_INT	0x0001	/* Autonegotiation complete interrupt */

#define MII_TQPHY_DIAG		0x12	/* Diagnostic register */
#define DIAG_ANEGF		0x1000	/* Autonegotiation fail */
#define DIAG_DPLX		0x0800	/* Duplex (half/full) */
#define DIAG_RATE		0x0400	/* Rate (10/100) */
#define DIAG_RXPASS		0x0200	/* Receive pass */
#define DIAG_RXLOCK		0x0100	/* Receive lock */

#endif /* _DEV_MII_TQPHYREG_H_ */
