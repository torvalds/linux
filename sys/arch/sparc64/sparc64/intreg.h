/*	$OpenBSD: intreg.h,v 1.6 2025/06/28 11:34:01 miod Exp $	*/
/*	$NetBSD: intreg.h,v 1.4 2000/06/24 04:21:05 eeh Exp $ */

/*
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This software was developed by the Computer Systems Engineering group
 * at Lawrence Berkeley Laboratory under DARPA contract BG 91-66 and
 * contributed to Berkeley.
 *
 * All advertising materials mentioning features or use of this software
 * must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Lawrence Berkeley Laboratory.
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
 *	@(#)intreg.h	8.1 (Berkeley) 6/11/93
 */

/*
 * All sun4u interrupts arrive as interrupt packets.  These packets
 * consist of up to six (three on spitfires) quads.  The first one
 * contains the interrupt number.  This is either the device ID
 * or a pointer to the handler routine.
 * 
 * Device IDs are split into two parts: a 5-bit interrupt group number
 * and a 5-bit interrupt number.  We ignore this distinction.
 *
 */
#define MAXINTNUM	(1<<11)

/*
 * Interrupt map stuff.
 */

#define INTMAP_V	0x080000000LL	/* Interrupt valid (enabled) */
#define INTMAP_TID	0x07c000000LL	/* UPA target ID mask */
#define INTMAP_IGN	0x0000007c0LL	/* Interrupt group no (sbus only). */
#define INTMAP_IGN_SHIFT	6
#define INTMAP_INO	0x00000003fLL	/* Interrupt number */
#define INTMAP_INR	(INTMAP_IGN|INTMAP_INO)
#define INTMAP_SBUSSLOT	0x000000018LL	/* SBus slot # */
#define INTMAP_PCIBUS	0x000000010LL	/* PCI bus number (A or B) */
#define INTMAP_PCISLOT	0x00000000cLL	/* PCI slot # */
#define INTMAP_PCIINT	0x000000003LL	/* PCI interrupt #A,#B,#C,#D */
#define INTMAP_OBIO	0x000000020LL	/* Onboard device */
#define INTMAP_LSHIFT	11		/* Encode level in vector */
#define	INTLEVENCODE(x)	(((x)&0x0f)<<INTMAP_LSHIFT)
#define INTLEV(x)	(((x)>>INTMAP_LSHIFT)&0x0f)
#define INTVEC(x)	((x)&INTMAP_INR)
#define INTSLOT(x)	(((x)>>3)&0x7)
#define	INTPRI(x)	((x)&0x7)
#define INTIGN(x)	((x)&INTMAP_IGN)
#define	INTINO(x)	((x)&INTMAP_INO)
#define INTTID_SHIFT	26
#define INTTID(x)	(((x) & INTMAP_TID) >> INTTID_SHIFT)

#define	INTCLR_IDLE	0
