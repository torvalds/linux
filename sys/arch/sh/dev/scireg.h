/*	$OpenBSD: scireg.h,v 1.1.1.1 2006/10/06 21:02:55 miod Exp $	*/
/* $NetBSD: scireg.h,v 1.8 2003/07/01 11:49:37 uwe Exp $ */

/*-
 * Copyright (C) 1999 SAITOH Masanobu.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Serial Communication Interface (SCI)
 */

#if !defined(SH4)

/* SH3 definitions */

#define	SHREG_SCSMR	(*(volatile unsigned char *)	0xFFFFFE80)
#define	SHREG_SCBRR	(*(volatile unsigned char *)	0xFFFFFE82)
#define	SHREG_SCSCR	(*(volatile unsigned char *)	0xFFFFFE84)
#define	SHREG_SCTDR	(*(volatile unsigned char *)	0xFFFFFE86)
#define	SHREG_SCSSR	(*(volatile unsigned char *)	0xFFFFFE88)
#define	SHREG_SCRDR	(*(volatile unsigned char *)	0xFFFFFE8A)
#define	SHREG_SCSPDR	(*(volatile unsigned char *)	0xf4000136)

#else

/* SH4 definitions */

#define	SHREG_SCSMR	(*(volatile unsigned char *)	0xffe00000)
#define	SHREG_SCBRR	(*(volatile unsigned char *)	0xffe00004)
#define	SHREG_SCSCR	(*(volatile unsigned char *)	0xffe00008)
#define	SHREG_SCTDR	(*(volatile unsigned char *)	0xffe0000c)
#define	SHREG_SCSSR	(*(volatile unsigned char *)	0xffe00010)
#define	SHREG_SCRDR	(*(volatile unsigned char *)	0xffe00014)
#define	SHREG_SCSPTR	(*(volatile unsigned char *)	0xffe0001c)

#endif

#define	SCSCR_TIE	0x80	/* Transmit Interrupt Enable */
#define	SCSCR_RIE	0x40	/* Receive Interrupt Enable */
#define	SCSCR_TE	0x20	/* Transmit Enable */
#define	SCSCR_RE	0x10	/* Receive Enable */
#define	SCSCR_MPIE	0x08	/* Multi Processor Interrupt Enable */
#define	SCSCR_TEIE	0x04	/* Transmit End Interrupt Enable */
#define	SCSCR_CKE1	0x02	/* ClocK Enable 1 */
#define	SCSCR_CKE0	0x01	/* ClocK Enable 0 */

#define	SCSSR_TDRE	0x80
#define	SCSSR_RDRF	0x40
#define	SCSSR_ORER	0x20
#define	SCSSR_FER	0x10
#define	SCSSR_PER	0x08

#define	SCSPTR_SPB1IO	0x08
#define	SCSPTR_SPB1DT	0x04
#define	SCSPTR_SPB0IO	0x02
#define	SCSPTR_SPB0DT	0x01

#if defined(SH3)
#define	SCSPDR_SCP0DT	0x01
#endif
