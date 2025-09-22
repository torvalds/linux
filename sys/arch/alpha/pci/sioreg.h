/*	$OpenBSD: sioreg.h,v 1.3 1996/10/30 22:40:16 niklas Exp $	*/
/*	$NetBSD: sioreg.h,v 1.1 1996/04/23 14:10:53 cgd Exp $	*/

/*
 * Copyright (c) 1996 BBN Corporation.
 *   BBN Systems and Technologies Division
 *   10 Moulton Street
 *   Cambridge, Ma. 02138
 *   617-873-3000
 *
 *  Permission to use, copy, modify, distribute, and sell this software and its
 *  documentation for  any purpose is hereby granted without fee, provided that
 *  the above copyright notice and this permission appear  in all copies and in
 *  supporting documentation, and that the name of BBN Corporation not  be used
 *  in  advertising  or  publicity pertaining  to  distribution of the software
 *  without specific, written  prior  permission.  BBN makes no representations
 *  about  the  suitability  of this software for any purposes.  It is provided
 *  "AS IS" without express or implied warranties.
 */

/*
 * Intel 82378 System I/O (SIO) Chip
 *
 * Taken from the Intel "Peripheral Components" manual, 1995 Edition.
 */


/*
 * Device-specific PCI Configuration Registers
 */

/*
 * PCI Control Registers
 */
#define	SIO_PCIREG_PCICON	0x40	/* PCI Control */
#define	SIO_PCIREG_PAC		0x41	/* PCI Arbiter Control */
#define	SIO_PCIREG_PAPC		0x42	/* PCI Arbiter Priority Control */
#define	SIO_PCIREG_ARBPRIX	0x43	/* PCI Arbiter Priority Control Ext. */

/*
 * Memory Chip Select Registers
 */
#define	SIO_PCIREG_MEMCSCON	0x44	/* MEMCS# Control */
#define	SIO_PCIREG_MEMCSBOH	0x45	/* MEMCS# Bottom of Hole */
#define	SIO_PCIREG_MEMCSTOH	0x46	/* MEMCS# Top of Hole */
#define	SIO_PCIREG_MEMCSTOM	0x47	/* MEMCS# Top of Memory */

#define	SIO_PCIREG_MAR1		0x54	/* MEMCS# Attribute 1 */
#define	SIO_PCIREG_MAR2		0x55	/* MEMCS# Attribute 2 */
#define	SIO_PCIREG_MAR3		0x56	/* MEMCS# Attribute 3 */
#define	SIO_PCIREG_DMASGRB	0x57	/* DMA Scatter/Gather Rel. Base Addr. */

/*
 * ISA Address Decoder Registers
 */
#define	SIO_PCIREG_IADCON	0x48	/* ISA Address Decoder Control */
#define	SIO_PCIREG_IADRBE	0x49	/* ISA Addr. Decoder ROM Block Enable */
#define	SIO_PCIREG_IADBOH	0x4A	/* ISA Addr. Decoder Bottom of Hole */
#define	SIO_PCIREG_IADTOH	0x4B	/* ISA Addr. Decoder Top of Hole */

/*
 * Clocks and Timers
 */
#define	SIO_PCIREG_ICRT		0x4C	/* ISA Controller Recovery Timer */
#define	SIO_PCIREG_ICD		0x4D	/* ISA Clock Divisor */

#define	SIO_PCIREG_		0x80	/* BIOS Timer Base Address */

#define	SIO_PCIREG_CTLTMRL	0xAC	/* Clock Throttle STPCLK# Low Timer */
#define	SIO_PCIREG_CTLTMRH	0xAE	/* Clock Throttle STPCLK# High Timer */

/*
 * Miscellaneous
 */
#define	SIO_PCIREG_UBCSA	0x4E	/* Utility Bus Chip Select A */
#define	SIO_PCIREG_UBCSB	0x4F	/* Utility Bus Chip Select B */

/*
 * PIRQ# Route Control
 */
#define	SIO_PCIREG_PIRQ0	0x60	/* PIRQ0 Route Control */
#define	SIO_PCIREG_PIRQ1	0x61	/* PIRQ1 Route Control */
#define	SIO_PCIREG_PIRQ2	0x62	/* PIRQ2 Route Control */
#define	SIO_PCIREG_PIRQ3	0x63	/* PIRQ3 Route Control */
#define	SIO_PCIREG_PIRQ_RTCTRL	SIO_PCIREG_PIRQ0

/*
 * System Management Interrupt (SMI)
 */
#define	SIO_PCIREG_SMICNTL	0xA0	/* SMI Control */
#define	SIO_PCIREG_SMIEN	0xA2	/* SMI Enable */
#define	SIO_PCIREG_SEE		0xA4	/* System Event Enable */
#define	SIO_PCIREG_FTMR		0xA8	/* Fast Off Timer */
#define	SIO_PCIREG_SMIREQ	0xAA	/* SMI Request */


/*
 * Non-Configuration Registers
 */

/*
 * Control
 */
#define	SIO_REG_RSTUB		0x060	/* Reset UBus */
#define	SIO_REG_NMICTRL		0x061	/* NMI Status and Control */
#define	SIO_REG_CMOSRAM		0x070	/* CMOS RAM Address and NMI Mask */
#define	SIO_REG_NMIMASK		0x070	/* CMOS RAM Address and NMI Mask */
#define	SIO_REG_PORT92		0x092	/* Port 92 */
#define	SIO_REG_CPERR		0x0F0	/* Coprocessor Error */

/*
 * Interrupt
 */
#define	SIO_REG_ICU1		0x020	/* Intr. Controller #1 Control */
#define	SIO_REG_ICU1MASK	0x021	/* Intr. Controller #1 Mask */
#define	SIO_REG_ICU2		0x0A0	/* Intr. Controller #2 Control */
#define	SIO_REG_ICU2MASK	0x0A1	/* Intr. Controller #2 Mask */
#define	SIO_REG_ICU1ELC		0x4D0	/* #1's Edge/Level Control */
#define	SIO_REG_ICU2ELC		0x4D1	/* #2's Edge/Level Control */
#define	SIO_ICUSIZE		16	/* I/O Port Sizes */

/*
 * Timer
 */
/* XXX need Timer definitions */

/*
 * DMA
 */
/* XXX need DMA definitions */
