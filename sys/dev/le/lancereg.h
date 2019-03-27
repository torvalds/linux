/*	$NetBSD: lancereg.h,v 1.12 2005/12/11 12:21:27 christos Exp $	*/

/*-
 * SPDX-License-Identifier: BSD-2-Clause-NetBSD
 *
 * Copyright (c) 1998, 2000 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Charles M. Hannum and Jason R. Thorpe.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*-
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Ralph Campbell and Rick Macklem.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)if_lereg.h	8.1 (Berkeley) 6/10/93
 */

/*
 * Register description for the following Advanced Micro Devices
 * Ethernet chips:
 *
 *	- Am7990 Local Area Network Controller for Ethernet (LANCE)
 *	  (and its descendent Am79c90 C-LANCE).
 *
 *	- Am79c900 Integrated Local Area Communications Controller (ILACC)
 *
 *	- Am79c960 PCnet-ISA Single-Chip Ethernet Controller for ISA
 *
 *	- Am79c961 PCnet-ISA+ Jumperless Single-Chip Ethernet Controller
 *	  for ISA
 *
 *	- Am79c961A PCnet-ISA II Jumperless Full-Duplex Single-Chip
 *	  Ethernet Controller for ISA
 *
 *	- Am79c965A PCnet-32 Single-Chip 32-bit Ethernet Controller
 *	  (for VESA and 486 local busses)
 *
 *	- Am79c970 PCnet-PCI Single-Chip Ethernet Controller for PCI
 *	  Local Bus
 *
 *	- Am79c970A PCnet-PCI II Single-Chip Full-Duplex Ethernet Controller
 *	  for PCI Local Bus
 *
 *	- Am79c971 PCnet-FAST Single-Chip Full-Duplex 10/100Mbps
 *	  Ethernet Controller for PCI Local Bus
 *
 *	- Am79c972 PCnet-FAST+ Enhanced 10/100Mbps PCI Ethernet Controller
 *	  with OnNow Support
 *
 *	- Am79c973/Am79c975 PCnet-FAST III Single-Chip 10/100Mbps PCI
 *	  Ethernet Controller with Integrated PHY
 *
 *	- Am79c978 PCnet-Home Single-Chip 1/10 Mbps PCI Home
 *	  Networking Controller.
 *
 * Initialization block, transmit descriptor, and receive descriptor
 * formats are described in two separate files:
 *
 *	16-bit software model (LANCE)		am7990reg.h
 *
 *	32-bit software model (ILACC)		am79900reg.h
 *
 * Note that the vast majority of the registers described in this file
 * belong to follow-on chips to the original LANCE.  Only CSR0-CSR3 are
 * valid on the LANCE.
 */

/* $FreeBSD$ */

#ifndef _DEV_LE_LANCEREG_H_
#define	_DEV_LE_LANCEREG_H_

#define	LEBLEN		(ETHER_MAX_LEN + ETHER_VLAN_ENCAP_LEN)
/* LEMINSIZE should be ETHER_MIN_LEN when LE_MODE_DTCR is set. */
#define	LEMINSIZE	(ETHER_MIN_LEN - ETHER_CRC_LEN)

#define	LE_INITADDR(sc)		(sc->sc_initaddr)
#define	LE_RMDADDR(sc, bix)	(sc->sc_rmdaddr + sizeof(struct lermd) * (bix))
#define	LE_TMDADDR(sc, bix)	(sc->sc_tmdaddr + sizeof(struct letmd) * (bix))
#define	LE_RBUFADDR(sc, bix)	(sc->sc_rbufaddr + LEBLEN * (bix))
#define	LE_TBUFADDR(sc, bix)	(sc->sc_tbufaddr + LEBLEN * (bix))

/*
 * The byte count fields in descriptors are in two's complement.
 * This macro does the conversion for us on unsigned numbers.
 */
#define	LE_BCNT(x)	(~(x) + 1)

/*
 * Control and Status Register addresses
 */
#define	LE_CSR0		0x0000		/* Control and status register */
#define	LE_CSR1		0x0001		/* low address of init block */
#define	LE_CSR2		0x0002		/* high address of init block */
#define	LE_CSR3		0x0003		/* Bus master and control */
#define	LE_CSR4		0x0004		/* Test and features control */
#define	LE_CSR5		0x0005		/* Extended control and Interrupt 1 */
#define	LE_CSR6		0x0006		/* Rx/Tx Descriptor table length */
#define	LE_CSR7		0x0007		/* Extended control and interrupt 2 */
#define	LE_CSR8		0x0008		/* Logical Address Filter 0 */
#define	LE_CSR9		0x0009		/* Logical Address Filter 1 */
#define	LE_CSR10	0x000a		/* Logical Address Filter 2 */
#define	LE_CSR11	0x000b		/* Logical Address Filter 3 */
#define	LE_CSR12	0x000c		/* Physical Address 0 */
#define	LE_CSR13	0x000d		/* Physical Address 1 */
#define	LE_CSR14	0x000e		/* Physical Address 2 */
#define	LE_CSR15	0x000f		/* Mode */
#define	LE_CSR16	0x0010		/* Initialization Block addr lower */
#define	LE_CSR17	0x0011		/* Initialization Block addr upper */
#define	LE_CSR18	0x0012		/* Current Rx Buffer addr lower */
#define	LE_CSR19	0x0013		/* Current Rx Buffer addr upper */
#define	LE_CSR20	0x0014		/* Current Tx Buffer addr lower */
#define	LE_CSR21	0x0015		/* Current Tx Buffer addr upper */
#define	LE_CSR22	0x0016		/* Next Rx Buffer addr lower */
#define	LE_CSR23	0x0017		/* Next Rx Buffer addr upper */
#define	LE_CSR24	0x0018		/* Base addr of Rx ring lower */
#define	LE_CSR25	0x0019		/* Base addr of Rx ring upper */
#define	LE_CSR26	0x001a		/* Next Rx Desc addr lower */
#define	LE_CSR27	0x001b		/* Next Rx Desc addr upper */
#define	LE_CSR28	0x001c		/* Current Rx Desc addr lower */
#define	LE_CSR29	0x001d		/* Current Rx Desc addr upper */
#define	LE_CSR30	0x001e		/* Base addr of Tx ring lower */
#define	LE_CSR31	0x001f		/* Base addr of Tx ring upper */
#define	LE_CSR32	0x0020		/* Next Tx Desc addr lower */
#define	LE_CSR33	0x0021		/* Next Tx Desc addr upper */
#define	LE_CSR34	0x0022		/* Current Tx Desc addr lower */
#define	LE_CSR35	0x0023		/* Current Tx Desc addr upper */
#define	LE_CSR36	0x0024		/* Next Next Rx Desc addr lower */
#define	LE_CSR37	0x0025		/* Next Next Rx Desc addr upper */
#define	LE_CSR38	0x0026		/* Next Next Tx Desc addr lower */
#define	LE_CSR39	0x0027		/* Next Next Tx Desc adddr upper */
#define	LE_CSR40	0x0028		/* Current Rx Byte Count */
#define	LE_CSR41	0x0029		/* Current Rx Status */
#define	LE_CSR42	0x002a		/* Current Tx Byte Count */
#define	LE_CSR43	0x002b		/* Current Tx Status */
#define	LE_CSR44	0x002c		/* Next Rx Byte Count */
#define	LE_CSR45	0x002d		/* Next Rx Status */
#define	LE_CSR46	0x002e		/* Tx Poll Time Counter */
#define	LE_CSR47	0x002f		/* Tx Polling Interval */
#define	LE_CSR48	0x0030		/* Rx Poll Time Counter */
#define	LE_CSR49	0x0031		/* Rx Polling Interval */
#define	LE_CSR58	0x003a		/* Software Style */
#define	LE_CSR60	0x003c		/* Previous Tx Desc addr lower */
#define	LE_CSR61	0x003d		/* Previous Tx Desc addr upper */
#define	LE_CSR62	0x003e		/* Previous Tx Byte Count */
#define	LE_CSR63	0x003f		/* Previous Tx Status */
#define	LE_CSR64	0x0040		/* Next Tx Buffer addr lower */
#define	LE_CSR65	0x0041		/* Next Tx Buffer addr upper */
#define	LE_CSR66	0x0042		/* Next Tx Byte Count */
#define	LE_CSR67	0x0043		/* Next Tx Status */
#define	LE_CSR72	0x0048		/* Receive Ring Counter */
#define	LE_CSR74	0x004a		/* Transmit Ring Counter */
#define	LE_CSR76	0x004c		/* Receive Ring Length */
#define	LE_CSR78	0x004e		/* Transmit Ring Length */
#define	LE_CSR80	0x0050		/* DMA Transfer Counter and FIFO
					   Threshold Control */
#define	LE_CSR82	0x0052		/* Tx Desc addr Pointer lower */
#define	LE_CSR84	0x0054		/* DMA addr register lower */
#define	LE_CSR85	0x0055		/* DMA addr register upper */
#define	LE_CSR86	0x0056		/* Buffer Byte Counter */
#define	LE_CSR88	0x0058		/* Chip ID Register lower */
#define	LE_CSR89	0x0059		/* Chip ID Register upper */
#define	LE_CSR92	0x005c		/* Ring Length Conversion */
#define	LE_CSR100	0x0064		/* Bus Timeout */
#define	LE_CSR112	0x0070		/* Missed Frame Count */
#define	LE_CSR114	0x0072		/* Receive Collision Count */
#define	LE_CSR116	0x0074		/* OnNow Power Mode Register */
#define	LE_CSR122	0x007a		/* Advanced Feature Control */
#define	LE_CSR124	0x007c		/* Test Register 1 */
#define	LE_CSR125	0x007d		/* MAC Enhanced Configuration Control */

/*
 * Bus Configuration Register addresses
 */
#define	LE_BCR0		0x0000		/* Master Mode Read Active */
#define	LE_BCR1		0x0001		/* Master Mode Write Active */
#define	LE_BCR2		0x0002		/* Misc. Configuration */
#define	LE_BCR4		0x0004		/* LED0 Status */
#define	LE_BCR5		0x0005		/* LED1 Status */
#define	LE_BCR6		0x0006		/* LED2 Status */
#define	LE_BCR7		0x0007		/* LED3 Status */
#define	LE_BCR9		0x0009		/* Full-duplex Control */
#define	LE_BCR16	0x0010		/* I/O Base Address lower */
#define	LE_BCR17	0x0011		/* I/O Base Address upper */
#define	LE_BCR18	0x0012		/* Burst and Bus Control Register */
#define	LE_BCR19	0x0013		/* EEPROM Control and Status */
#define	LE_BCR20	0x0014		/* Software Style */
#define	LE_BCR22	0x0016		/* PCI Latency Register */
#define	LE_BCR23	0x0017		/* PCI Subsystem Vendor ID */
#define	LE_BCR24	0x0018		/* PCI Subsystem ID */
#define	LE_BCR25	0x0019		/* SRAM Size Register */
#define	LE_BCR26	0x001a		/* SRAM Boundary Register */
#define	LE_BCR27	0x001b		/* SRAM Interface Control Register */
#define	LE_BCR28	0x001c		/* Exp. Bus Port Addr lower */
#define	LE_BCR29	0x001d		/* Exp. Bus Port Addr upper */
#define	LE_BCR30	0x001e		/* Exp. Bus Data Port */
#define	LE_BCR31	0x001f		/* Software Timer Register */
#define	LE_BCR32	0x0020		/* PHY Control and Status Register */
#define	LE_BCR33	0x0021		/* PHY Address Register */
#define	LE_BCR34	0x0022		/* PHY Management Data Register */
#define	LE_BCR35	0x0023		/* PCI Vendor ID Register */
#define	LE_BCR36	0x0024		/* PCI Power Management Cap. Alias */
#define	LE_BCR37	0x0025		/* PCI DATA0 Alias */
#define	LE_BCR38	0x0026		/* PCI DATA1 Alias */
#define	LE_BCR39	0x0027		/* PCI DATA2 Alias */
#define	LE_BCR40	0x0028		/* PCI DATA3 Alias */
#define	LE_BCR41	0x0029		/* PCI DATA4 Alias */
#define	LE_BCR42	0x002a		/* PCI DATA5 Alias */
#define	LE_BCR43	0x002b		/* PCI DATA6 Alias */
#define	LE_BCR44	0x002c		/* PCI DATA7 Alias */
#define	LE_BCR45	0x002d		/* OnNow Pattern Matching 1 */
#define	LE_BCR46	0x002e		/* OnNow Pattern Matching 2 */
#define	LE_BCR47	0x002f		/* OnNow Pattern Matching 3 */
#define	LE_BCR48	0x0030		/* LED4 Status */
#define	LE_BCR49	0x0031		/* PHY Select */

/* Control and status register 0 (csr0) */
#define	LE_C0_ERR	0x8000		/* error summary */
#define	LE_C0_BABL	0x4000		/* transmitter timeout error */
#define	LE_C0_CERR	0x2000		/* collision */
#define	LE_C0_MISS	0x1000		/* missed a packet */
#define	LE_C0_MERR	0x0800		/* memory error */
#define	LE_C0_RINT	0x0400		/* receiver interrupt */
#define	LE_C0_TINT	0x0200		/* transmitter interrupt */
#define	LE_C0_IDON	0x0100		/* initialization done */
#define	LE_C0_INTR	0x0080		/* interrupt condition */
#define	LE_C0_INEA	0x0040		/* interrupt enable */
#define	LE_C0_RXON	0x0020		/* receiver on */
#define	LE_C0_TXON	0x0010		/* transmitter on */
#define	LE_C0_TDMD	0x0008		/* transmit demand */
#define	LE_C0_STOP	0x0004		/* disable all external activity */
#define	LE_C0_STRT	0x0002		/* enable external activity */
#define	LE_C0_INIT	0x0001		/* begin initialization */

#define	LE_C0_BITS \
    "\20\20ERR\17BABL\16CERR\15MISS\14MERR\13RINT\
\12TINT\11IDON\10INTR\07INEA\06RXON\05TXON\04TDMD\03STOP\02STRT\01INIT"

/* Control and status register 3 (csr3) */
#define	LE_C3_BABLM	0x4000		/* babble mask */
#define	LE_C3_MISSM	0x1000		/* missed frame mask */
#define	LE_C3_MERRM	0x0800		/* memory error mask */
#define	LE_C3_RINTM	0x0400		/* receive interrupt mask */
#define	LE_C3_TINTM	0x0200		/* transmit interrupt mask */
#define	LE_C3_IDONM	0x0100		/* initialization done mask */
#define	LE_C3_DXSUFLO	0x0040		/* disable tx stop on underflow */
#define	LE_C3_LAPPEN	0x0020		/* look ahead packet processing enbl */
#define	LE_C3_DXMT2PD	0x0010		/* disable tx two part deferral */
#define	LE_C3_EMBA	0x0008		/* enable modified backoff algorithm */
#define	LE_C3_BSWP	0x0004		/* byte swap */
#define	LE_C3_ACON	0x0002		/* ALE control, eh? */
#define	LE_C3_BCON	0x0001		/* byte control */

/* Control and status register 4 (csr4) */
#define	LE_C4_EN124	0x8000		/* enable CSR124 */
#define	LE_C4_DMAPLUS	0x4000		/* always set (PCnet-PCI) */
#define	LE_C4_TIMER	0x2000		/* enable bus activity timer */
#define	LE_C4_TXDPOLL	0x1000		/* disable transmit polling */
#define	LE_C4_APAD_XMT	0x0800		/* auto pad transmit */
#define	LE_C4_ASTRP_RCV	0x0400		/* auto strip receive */
#define	LE_C4_MFCO	0x0200		/* missed frame counter overflow */
#define	LE_C4_MFCOM	0x0100		/* missed frame coutner overflow mask */
#define	LE_C4_UINTCMD	0x0080		/* user interrupt command */
#define	LE_C4_UINT	0x0040		/* user interrupt */
#define	LE_C4_RCVCCO	0x0020		/* receive collision counter overflow */
#define	LE_C4_RCVCCOM	0x0010		/* receive collision counter overflow
					   mask */
#define	LE_C4_TXSTRT	0x0008		/* transmit start status */
#define	LE_C4_TXSTRTM	0x0004		/* transmit start mask */

/* Control and status register 5 (csr5) */
#define	LE_C5_TOKINTD	0x8000		/* transmit ok interrupt disable */
#define	LE_C5_LTINTEN	0x4000		/* last transmit interrupt enable */
#define	LE_C5_SINT	0x0800		/* system interrupt */
#define	LE_C5_SINTE	0x0400		/* system interrupt enable */
#define	LE_C5_EXDINT	0x0080		/* excessive deferral interrupt */
#define	LE_C5_EXDINTE	0x0040		/* excessive deferral interrupt enbl */
#define	LE_C5_MPPLBA	0x0020		/* magic packet physical logical
					   broadcast accept */
#define	LE_C5_MPINT	0x0010		/* magic packet interrupt */
#define	LE_C5_MPINTE	0x0008		/* magic packet interrupt enable */
#define	LE_C5_MPEN	0x0004		/* magic packet enable */
#define	LE_C5_MPMODE	0x0002		/* magic packet mode */
#define	LE_C5_SPND	0x0001		/* suspend */

/* Control and status register 6 (csr6) */
#define	LE_C6_TLEN	0xf000		/* TLEN from init block */
#define	LE_C6_RLEN	0x0f00		/* RLEN from init block */

/* Control and status register 7 (csr7) */
#define	LE_C7_FASTSPNDE	0x8000		/* fast suspend enable */
#define	LE_C7_RDMD	0x2000		/* receive demand */
#define	LE_C7_RDXPOLL	0x1000		/* receive disable polling */
#define	LE_C7_STINT	0x0800		/* software timer interrupt */
#define	LE_C7_STINTE	0x0400		/* software timer interrupt enable */
#define	LE_C7_MREINT	0x0200		/* PHY management read error intr */
#define	LE_C7_MREINTE	0x0100		/* PHY management read error intr
					   enable */
#define	LE_C7_MAPINT	0x0080		/* PHY management auto-poll intr */
#define	LE_C7_MAPINTE	0x0040		/* PHY management auto-poll intr
					   enable */
#define	LE_C7_MCCINT	0x0020		/* PHY management command complete
					   interrupt */
#define	LE_C7_MCCINTE	0x0010		/* PHY management command complete
					   interrupt enable */
#define	LE_C7_MCCIINT	0x0008		/* PHY management command complete
					   internal interrupt */
#define	LE_C7_MCCIINTE	0x0004		/* PHY management command complete
					   internal interrupt enable */
#define	LE_C7_MIIPDTINT	0x0002		/* PHY management detect transition
					   interrupt */
#define	LE_C7_MIIPDTINTE 0x0001		/* PHY management detect transition
					   interrupt enable */

/* Control and status register 15 (csr15) */
#define	LE_C15_PROM	0x8000		/* promiscuous mode */
#define	LE_C15_DRCVBC	0x4000		/* disable Rx of broadcast */
#define	LE_C15_DRCVPA	0x2000		/* disable Rx of physical address */
#define	LE_C15_DLNKTST	0x1000		/* disable link status */
#define	LE_C15_DAPC	0x0800		/* disable auto-polarity correction */
#define	LE_C15_MENDECL	0x0400		/* MENDEC Loopback mode */
#define	LE_C15_LRT	0x0200		/* low receive threshold (TMAU) */
#define	LE_C15_TSEL	0x0200		/* transmit mode select (AUI) */
#define	LE_C15_PORTSEL(x) ((x) << 7)	/* port select */
#define	LE_C15_INTL	0x0040		/* internal loopback */
#define	LE_C15_DRTY	0x0020		/* disable retry */
#define	LE_C15_FCOLL	0x0010		/* force collision */
#define	LE_C15_DXMTFCS	0x0008		/* disable Tx FCS (ADD_FCS overrides) */
#define	LE_C15_LOOP	0x0004		/* loopback enable */
#define	LE_C15_DTX	0x0002		/* disable transmit */
#define	LE_C15_DRX	0x0001		/* disable receiver */

#define	LE_PORTSEL_AUI	0
#define	LE_PORTSEL_10T	1
#define	LE_PORTSEL_GPSI	2
#define	LE_PORTSEL_MII	3
#define	LE_PORTSEL_MASK	3

/* control and status register 80 (csr80) */
#define	LE_C80_RCVFW(x)	((x) << 12)	/* Receive FIFO Watermark */
#define	LE_C80_RCVFW_MAX 3
#define	LE_C80_XMTSP(x)	((x) << 10)	/* Transmit Start Point */
#define	LE_C80_XMTSP_MAX 3
#define	LE_C80_XMTFW(x)	((x) << 8)	/* Transmit FIFO Watermark */
#define	LE_C80_XMTFW_MAX 3
#define	LE_C80_DMATC	0x00ff		/* DMA transfer counter */

/* control and status register 116 (csr116) */
#define	LE_C116_PME_EN_OVR 0x0400	/* PME_EN overwrite */
#define	LE_C116_LCDET	   0x0200	/* link change detected */
#define	LE_C116_LCMODE	   0x0100	/* link change wakeup mode */
#define	LE_C116_PMAT	   0x0080	/* pattern matched */
#define	LE_C116_EMPPLBA	   0x0040	/* magic packet physical logical
					   broadcast accept */
#define	LE_C116_MPMAT	   0x0020	/* magic packet match */
#define	LE_C116_MPPEN	   0x0010	/* magic packet pin enable */
#define	LE_C116_RST_POL	   0x0001	/* PHY_RST pin polarity */

/* control and status register 122 (csr122) */
#define	LE_C122_RCVALGN	0x0001		/* receive packet align */

/* control and status register 124 (csr124) */
#define	LE_C124_RPA	0x0008		/* runt packet accept */

/* control and status register 125 (csr125) */
#define	LE_C125_IPG	0xff00		/* inter-packet gap */
#define	LE_C125_IFS1	0x00ff		/* inter-frame spacing part 1 */

/* bus configuration register 0 (bcr0) */
#define	LE_B0_MSRDA	0xffff		/* reserved locations */

/* bus configuration register 1 (bcr1) */
#define	LE_B1_MSWRA	0xffff		/* reserved locations */

/* bus configuration register 2 (bcr2) */
#define	LE_B2_PHYSSELEN	0x2000		/* enable writes to BCR18[4:3] */
#define	LE_B2_LEDPE	0x1000		/* LED program enable */
#define	LE_B2_APROMWE	0x0100		/* Address PROM Write Enable */
#define	LE_B2_INTLEVEL	0x0080		/* 1 == edge triggered */
#define	LE_B2_DXCVRCTL	0x0020		/* DXCVR control */
#define	LE_B2_DXCVRPOL	0x0010		/* DXCVR polarity */
#define	LE_B2_EADISEL	0x0008		/* EADI select */
#define	LE_B2_AWAKE	0x0004		/* power saving mode select */
#define	LE_B2_ASEL	0x0002		/* auto-select PORTSEL */
#define	LE_B2_XMAUSEL	0x0001		/* reserved location */

/* bus configuration register 4 (bcr4) */
/* bus configuration register 5 (bcr5) */
/* bus configuration register 6 (bcr6) */
/* bus configuration register 7 (bcr7) */
/* bus configuration register 48 (bcr48) */
#define	LE_B4_LEDOUT	0x8000		/* LED output active */
#define	LE_B4_LEDPOL	0x4000		/* LED polarity */
#define	LE_B4_LEDDIS	0x2000		/* LED disable */
#define	LE_B4_100E	0x1000		/* 100Mb/s enable */
#define	LE_B4_MPSE	0x0200		/* magic packet status enable */
#define	LE_B4_FDLSE	0x0100		/* full-duplex link status enable */
#define	LE_B4_PSE	0x0080		/* pulse stretcher enable */
#define	LE_B4_LNKSE	0x0040		/* link status enable */
#define	LE_B4_RCVME	0x0020		/* receive match status enable */
#define	LE_B4_XMTE	0x0010		/* transmit status enable */
#define	LE_B4_POWER	0x0008		/* power enable */
#define	LE_B4_RCVE	0x0004		/* receive status enable */
#define	LE_B4_SPEED	0x0002		/* high speed enable */
#define	LE_B4_COLE	0x0001		/* collision status enable */

/* bus configuration register 9 (bcr9) */
#define	LE_B9_FDRPAD	0x0004		/* full-duplex runt packet accept
					   disable */
#define	LE_B9_AUIFD	0x0002		/* AUI full-duplex */
#define	LE_B9_FDEN	0x0001		/* full-duplex enable */

/* bus configuration register 18 (bcr18) */
#define	LE_B18_ROMTMG	0xf000		/* expansion rom timing */
#define	LE_B18_NOUFLO	0x0800		/* no underflow on transmit */
#define	LE_B18_MEMCMD	0x0200		/* memory read multiple enable */
#define	LE_B18_EXTREQ	0x0100		/* extended request */
#define	LE_B18_DWIO	0x0080		/* double-word I/O */
#define	LE_B18_BREADE	0x0040		/* burst read enable */
#define	LE_B18_BWRITE	0x0020		/* burst write enable */
#define	LE_B18_PHYSEL1	0x0010		/* PHYSEL 1 */
#define	LE_B18_PHYSEL0	0x0008		/* PHYSEL 0 */
					/*	00	ex ROM/Flash	*/
					/*	01	EADI/MII snoop	*/
					/*	10	reserved	*/
					/*	11	reserved	*/
#define	LE_B18_LINBC	0x0007		/* reserved locations */

/* bus configuration register 19 (bcr19) */
#define	LE_B19_PVALID	0x8000		/* EEPROM status valid */
#define	LE_B19_PREAD	0x4000		/* EEPROM read command */
#define	LE_B19_EEDET	0x2000		/* EEPROM detect */
#define	LE_B19_EEN	0x0010		/* EEPROM port enable */
#define	LE_B19_ECS	0x0004		/* EEPROM chip select */
#define	LE_B19_ESK	0x0002		/* EEPROM serial clock */
#define	LE_B19_EDI	0x0001		/* EEPROM data in */
#define	LE_B19_EDO	0x0001		/* EEPROM data out */

/* bus configuration register 20 (bcr20) */
#define	LE_B20_APERREN	0x0400		/* Advanced parity error handling */
#define	LE_B20_CSRPCNET	0x0200		/* PCnet-style CSRs (0 = ILACC) */
#define	LE_B20_SSIZE32	0x0100		/* Software Size 32-bit */
#define	LE_B20_SSTYLE	0x0007		/* Software Style */
#define	LE_B20_SSTYLE_LANCE	0	/* LANCE/PCnet-ISA (16-bit) */
#define	LE_B20_SSTYLE_ILACC	1	/* ILACC (32-bit) */
#define	LE_B20_SSTYLE_PCNETPCI2	2	/* PCnet-PCI (32-bit) */
#define	LE_B20_SSTYLE_PCNETPCI3	3	/* PCnet-PCI II (32-bit) */

/* bus configuration register 25 (bcr25) */
#define	LE_B25_SRAM_SIZE  0x00ff	/* SRAM size */

/* bus configuration register 26 (bcr26) */
#define	LE_B26_SRAM_BND	  0x00ff	/* SRAM boundary */

/* bus configuration register 27 (bcr27) */
#define	LE_B27_PTRTST	0x8000		/* reserved for manuf. tests */
#define	LE_B27_LOLATRX	0x4000		/* low latency receive */
#define	LE_B27_EBCS	0x0038		/* expansion bus clock source */
					/*	000	CLK pin		*/
					/*	001	time base clock	*/
					/*	010	EBCLK pin	*/
					/*	011	reserved	*/
					/*	1xx	reserved	*/
#define	LE_B27_CLK_FAC	0x0007		/* clock factor */
					/*	000	1		*/
					/*	001	1/2		*/
					/*	010	reserved	*/
					/*	011	1/4		*/
					/*	1xx	reserved	*/

/* bus configuration register 28 (bcr28) */
#define	LE_B28_EADDRL	0xffff		/* expansion port address lower */

/* bus configuration register 29 (bcr29) */
#define	LE_B29_FLASH	0x8000		/* flash access */
#define	LE_B29_LAAINC	0x4000		/* lower address auto increment */
#define	LE_B29_EPADDRU	0x0007		/* expansion port address upper */

/* bus configuration register 30 (bcr30) */
#define	LE_B30_EBDATA	0xffff		/* expansion bus data port */

/* bus configuration register 31 (bcr31) */
#define	LE_B31_STVAL	0xffff		/* software timer value */

/* bus configuration register 32 (bcr32) */
#define	LE_B32_ANTST	0x8000		/* reserved for manuf. tests */
#define	LE_B32_MIIPD	0x4000		/* MII PHY Detect (manuf. tests) */
#define	LE_B32_FMDC	0x3000		/* fast management data clock */
#define	LE_B32_APEP	0x0800		/* auto-poll PHY */
#define	LE_B32_APDW	0x0700		/* auto-poll dwell time */
#define	LE_B32_DANAS	0x0080		/* disable autonegotiation */
#define	LE_B32_XPHYRST	0x0040		/* PHY reset */
#define	LE_B32_XPHYANE	0x0020		/* PHY autonegotiation enable */
#define	LE_B32_XPHYFD	0x0010		/* PHY full-duplex */
#define	LE_B32_XPHYSP	0x0008		/* PHY speed */
#define	LE_B32_MIIILP	0x0002		/* MII internal loopback */

/* bus configuration register 33 (bcr33) */
#define	LE_B33_SHADOW	0x8000		/* shadow enable */
#define	LE_B33_MII_SEL	0x4000		/* MII selected */
#define	LE_B33_ACOMP	0x2000		/* internal PHY autonegotiation comp */
#define	LE_B33_LINK	0x1000		/* link status */
#define	LE_B33_FDX	0x0800		/* full-duplex */
#define	LE_B33_SPEED	0x0400		/* 1 == high speed */
#define	LE_B33_PHYAD	0x03e0		/* PHY address */
#define	PHYAD_SHIFT	5
#define	LE_B33_REGAD	0x001f		/* register address */

/* bus configuration register 34 (bcr34) */
#define	LE_B34_MIIMD	0xffff		/* MII data */

/* bus configuration register 49 (bcr49) */
#define	LE_B49_PCNET	0x8000		/* PCnet mode - Must Be One */
#define	LE_B49_PHYSEL_D	0x0300		/* PHY_SEL_Default */
#define	LE_B49_PHYSEL_L	0x0010		/* PHY_SEL_Lock */
#define	LE_B49_PHYSEL	0x0003		/* PHYSEL */
					/*	00	10baseT PHY	*/
					/*	01	HomePNA PHY	*/
					/*	10	external PHY	*/
					/*	11	reserved	*/

/* Initialization block (mode) */
#define	LE_MODE_PROM	0x8000		/* promiscuous mode */
/*			0x7f80		   reserved, must be zero */
/* 0x4000 - 0x0080 are not available on LANCE 7990. */
#define	LE_MODE_DRCVBC	0x4000		/* disable receive brodcast */
#define	LE_MODE_DRCVPA	0x2000		/* disable physical address detection */
#define	LE_MODE_DLNKTST	0x1000		/* disable link status */
#define	LE_MODE_DAPC	0x0800		/* disable automatic polarity correction */
#define	LE_MODE_MENDECL	0x0400		/* MENDEC loopback mode */
#define	LE_MODE_LRTTSEL	0x0200		/* lower receive threshold /
					   transmit mode selection */
#define	LE_MODE_PSEL1	0x0100		/* port selection bit1 */
#define	LE_MODE_PSEL0	0x0080		/* port selection bit0 */
#define	LE_MODE_INTL	0x0040		/* internal loopback */
#define	LE_MODE_DRTY	0x0020		/* disable retry */
#define	LE_MODE_COLL	0x0010		/* force a collision */
#define	LE_MODE_DTCR	0x0008		/* disable transmit CRC */
#define	LE_MODE_LOOP	0x0004		/* loopback mode */
#define	LE_MODE_DTX	0x0002		/* disable transmitter */
#define	LE_MODE_DRX	0x0001		/* disable receiver */
#define	LE_MODE_NORMAL	0		/* none of the above */

/*
 * Chip ID (CSR88 IDL, CSR89 IDU) values for various AMD PCnet parts
 */
#define	CHIPID_MANFID(x)	(((x) >> 1) & 0x3ff)
#define	CHIPID_PARTID(x)	(((x) >> 12) & 0xffff)
#define	CHIPID_VER(x)		(((x) >> 28) & 0x7)

#define	PARTID_Am79c960		0x0003
#define	PARTID_Am79c961		0x2260
#define	PARTID_Am79c961A	0x2261
#define	PARTID_Am79c965		0x2430	/* yes, these... */
#define	PARTID_Am79c970		0x2430	/* ...are the same */
#define	PARTID_Am79c970A	0x2621
#define	PARTID_Am79c971		0x2623
#define	PARTID_Am79c972		0x2624
#define	PARTID_Am79c973		0x2625
#define	PARTID_Am79c978		0x2626
#define	PARTID_Am79c975		0x2627
#define	PARTID_Am79c976		0x2628

#endif	/* !_DEV_LE_LANCEREG_H_ */
