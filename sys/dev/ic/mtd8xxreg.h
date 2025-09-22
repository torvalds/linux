/*	$OpenBSD: mtd8xxreg.h,v 1.3 2024/09/01 03:08:56 jsg Exp $	*/

/*
 * Copyright (c) 2003 Oleg Safiullin <form@pdp11.org.ru>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
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
 */

#ifndef __DEV_IC_MTD8XXREG_H__
#define __DEV_IC_MTD8XXREG_H__

#define MTD_PCI_LOIO	0x10		/* PCI I/O base address register */
#define MTD_PCI_LOMEM	0x14		/* PCI memory base address register */

#define MTD_TIMEOUT	1000		/* Software reset timeout */

#define MII_OPCODE_RD	0x6000
#define MII_OPCODE_WR	0x5002

/*
 * MTD8xx command and status register.
 */
#define MTD_PAR0	0x00		/* Physical address register 0*/
#define MTD_PAR4	0x04		/* Physical address register 4 */
#define MTD_MAR0	0x08		/* Multicast address register 0 */
#define MTD_MAR4	0x0C		/* Multicast address register 4 */
#define MTD_TCRRCR	0x18		/* Transmit/receive config */
#define MTD_BCR		0x1C		/* Bus configuration register */
#define MTD_TXPDR	0x20		/* Transmit poll demand */
#define MTD_RXPDR	0x24		/* Receive poll demand */
#define MTD_RXCWP	0x28		/* Receive current word pointer */
#define MTD_TXLBA	0x2C		/* Transmit list base address */
#define MTD_RXLBA	0x30		/* Receive list base address */
#define MTD_ISR		0x34		/* Interrupt status register */
#define MTD_IMR		0x38		/* Interrupt mask register */
#define MTD_TSR		0x48		/* Tally counter register */
#define MTD_MIIMGT	0x40		/* MII management register */
#define MTD_PHYCSR	0x4C		/* PHY control status register */


/*
 * Receive configuration register.
 */
#define RCR_RXS		0x00008000U	/* Receive process is running */
#define RCR_EIEN	0x00004000U	/* Early interrupt enabled */
#define RCR_RFCEN	0x00002000U	/* Receive flow control enabled */
#define RCR_NDFA	0x00001000U	/* Not defined flow control address */
#define RCR_RBLEN	0x00000800U	/* Receive burst length enable */
#define RCR_RPBL1	0x00000000U	/*   1 word */
#define RCR_RPBL4	0x00000100U	/*   4 words */
#define RCR_RPBL8	0x00000200U	/*   8 words */
#define RCR_RPBL16	0x00000300U	/*  16 words */
#define RCR_RPBL32	0x00000400U	/*  32 words */
#define RCR_RPBL64	0x00000500U	/*  64 words */
#define RCR_RPBL128	0x00000600U	/* 128 words */
#define RCR_RPBL512	0x00000700U	/* 512 words */
#define RCR_PROM	0x00000080U	/* Promiscuous mode */
#define RCR_AB		0x00000040U	/* Accept broadcast addresses */
#define RCR_AM		0x00000020U	/* Accept multicast addresses */
#define RCR_ARP		0x00000008U	/* Accept runt packets */
#define RCR_ALP		0x00000004U	/* Accept long packets */
#define RCR_SEP		0x00000002U	/* Accept packets w/ receive errors */
#define RCR_RE		0x00000001U	/* Receive enable */


/*
 * Transmit configuration register.
 */
#define TCR_TXS		0x80000000U	/* Tx process is running */
#define TCR_BACKOPT	0x10000000U	/* Optional back-off */
#define TCR_FBACK	0x08000000U	/* Fast back-off */
#define TCR_ENHANCED	0x02000000U	/* Enhanced transmit mode */
#define TCR_TFCEN	0x01000000U	/* Transmit flow control enable */
#define TCR_TFT64	0x00000000U	/*   64 bytes */
#define TCR_TFT32	0x00200000U	/*   32 bytes */
#define TCR_TFT128	0x00400000U	/*  128 bytes */
#define TCR_TFT256	0x00600000U	/*  256 bytes */
#define TCR_TFT512	0x00800000U	/*  512 bytes */
#define TCR_TFT768	0x00A00000U	/*  768 bytes */
#define TCR_TFT1024	0x00C00000U	/* 1024 bytes */
#define TCR_TFTSF	0x00E00000U	/* Transmit store and forward */
#define TCR_FD		0x00100000U	/* Full-duplex mode */
#define TCR_PS		0x00080000U	/* Port speed is 10Mbit/s */
#define TCR_TE		0x00040000U	/* Transmit enable */
#define TCR_LB		0x00020000U	/* MII loopback on */


/*
 * Bus configuration register.
 */
#define BCR_PROG	0x00000200U	/* Programming */
#define BCR_RLE		0x00000100U	/* Read line cmd enable */
#define BCR_RME		0x00000080U	/* Read multiple cmd enable */
#define BCR_WIE		0x00000040U	/* Write and invalidate cmd enable */
					/* Programmable burst length */
#define BCR_PBL1	0x00000000U	/*   1 dword */
#define BCR_PBL4	0x00000008U	/*   4 dwords */
#define BCR_PBL8	0x00000010U	/*   8 dwords */
#define BCR_PBL16	0x00000018U	/*  16 dwords */
#define BCR_PBL32	0x00000020U	/*  32 dwords */
#define BCR_PBL64	0x00000028U	/*  64 dwords */
#define BCR_PBL128	0x00000030U	/* 128 dwords */
#define BCR_PBL512	0x00000038U	/* 512 dwords */
#define BCR_SWR		0x00000001U	/* Software reset */


/*
 * Interrupt status register.
 */
#define ISR_PDF		0x00040000U	/* Parallel detection fault */
#define ISR_RFCON	0x00020000U	/* Receive flow control XON */
#define ISR_RFCOFF	0x00010000U	/* Receive flow control XOFF */
#define ISR_LSC		0x00008000U	/* Link status change */
#define ISR_ANC		0x00004000U	/* Auto-negotiation completed */
#define ISR_FBE		0x00002000U	/* Fatal bus error */
#define ISR_ETMASK	0x00001800U	/* Error type mask */
#define ISR_ET(x)	((x) & ISR_ETMASK)
#define ISR_ETPARITY	0x00000000U	/* Parity error */
#define ISR_ETMASTER	0x00000800U	/* Master abort */
#define ISR_ETTARGET	0x00001000U	/* Target abort */
#define ISR_TUNF	0x00000400U	/* Transmit underflow */
#define ISR_ROVF	0x00000200U	/* Receive overflow */
#define ISR_ETI		0x00000100U	/* Early transfer interrupt */
#define ISR_ERI		0x00000080U	/* Early receive interrupt */
#define ISR_CNTOVF	0x00000040U	/* CRC or MPA tally counter overflow */
#define ISR_RBU		0x00000020U	/* Receive buffer unavailable */
#define ISR_TBU		0x00000010U	/* Transmit buffer unavailable */
#define ISR_TI		0x00000008U	/* Transmit interrupt */
#define ISR_RI		0x00000004U	/* Receive interrupt */
#define ISR_RXERI	0x00000002U	/* Receive error interrupt */

#define ISR_INTRS	(ISR_RBU | ISR_TBU | ISR_TI | ISR_RI | ISR_ETI)


/*
 * Interrupt mask register.
 */
#define IMR_MPDF	0x00040000U	/* Parallel detection fault */
#define IMR_MRFCON	0x00020000U	/* Receive flow control XON */
#define IMR_MRFCOFF	0x00010000U	/* Receive flow control XOFF */
#define IMR_MLSC	0x00008000U	/* Link status change */
#define IMR_MANC	0x00004000U	/* Auto-negotiation completed */
#define IMR_MFBE	0x00002000U	/* Fatal bus error */
#define IMR_MTUNF	0x00000400U	/* Transmit underflow */
#define IMR_MROVF	0x00000200U	/* Receive overflow */
#define IMR_METI	0x00000100U	/* Early transfer interrupt */
#define IMR_MERI	0x00000080U	/* Early receive interrupt */
#define IMR_MCNTOVF	0x00000040U	/* CRC or MPA tally counter overflow */
#define IMR_MRBU	0x00000020U	/* Receive buffer unavailable */
#define IMR_MTBU	0x00000010U	/* Transmit buffer unavailable */
#define IMR_MTI		0x00000008U	/* Transmit interrupt */
#define IMR_MRI		0x00000004U	/* Receive interrupt */
#define IMR_MRXERI	0x00000002U	/* Receive error interrupt */

#define IMR_INTRS	(IMR_MRBU | IMR_MTBU | IMR_MTI | IMR_MRI | IMR_METI)

/*
 * Transmit status register.
 */
#define TSR_NCR_MASK	0x0000FFFFU
#define TSR_NCR_SHIFT	0
#define TSR_NCR_GET(x)	(((x) & TSR_NCR_MASK) >> TSR_NCR_SHIFT)
					/* Retry collisions count */

/*
 * MII management register.
 */
#define MIIMGT_READ	0x00000000U
#define MIIMGT_WRITE	0x00000008U
#define MIIMGT_MDO	0x00000004U
#define MIIMGT_MDI	0x00000002U
#define MIIMGT_MDC	0x00000001U
#define MIIMGT_MASK	0x0000000FU

/*
 * Command and status register space access macros.
 */
#define CSR_READ_1(reg)	bus_space_read_1(sc->sc_bust, sc->sc_bush, reg)
#define CSR_WRITE_1(reg, val) \
    bus_space_write_1(sc->sc_bust, sc->sc_bush, reg, val)

#define CSR_READ_2(reg)	bus_space_read_2(sc->sc_bust, sc->sc_bush, reg)
#define CSR_WRITE_2(reg, vat) \
    bus_space_write_2(sc->sc_bust, sc->sc_bush, reg, val)

#define CSR_READ_4(reg)	bus_space_read_4(sc->sc_bust, sc->sc_bush, reg)
#define CSR_WRITE_4(reg, val) \
    bus_space_write_4(sc->sc_bust, sc->sc_bush, reg, val)

#define CSR_SETBIT(reg, val) CSR_WRITE_4(reg, CSR_READ_4(reg) | (val))
#define CSR_CLRBIT(reg, val) CSR_WRITE_4(reg, CSR_READ_4(reg) & ~(val))

#endif	/* __DEV_IC_MTD8XXREG_H__ */
