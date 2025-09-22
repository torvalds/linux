/*	$OpenBSD: fdreg.h,v 1.12 2006/08/13 03:36:24 krw Exp $	*/
/*	$NetBSD: fdreg.h,v 1.8 1995/06/28 04:30:57 cgd Exp $	*/

/*-
 * Copyright (c) 1991 The Regents of the University of California.
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
 *	@(#)fdreg.h	7.1 (Berkeley) 5/9/91
 */

/*
 * AT floppy controller registers and bitfields
 */

/* uses NEC765 controller */
#include <dev/ic/nec765reg.h>

/* registers */
#define	fdout	2	/* Digital Output Register (W) */
#define	FDO_FDSEL	0x03	/*  floppy device select */
#define	FDO_FRST	0x04	/*  floppy controller reset */
#define	FDO_FDMAEN	0x08	/*  enable floppy DMA and Interrupt */
#define	FDO_MOEN(n)	((1 << n) * 0x10)	/* motor enable */

#define	fdsts	4	/* NEC 765 Main Status Register (R) */
#define	fddata	5	/* NEC 765 Data Register (R/W) */

#define FDCTL_OFFSET	7	/* Offset from the other registers. */
#define	fdctl	0	/* Control Register (W) */
#define	FDC_500KBPS	0x00	/* 500KBPS MFM drive transfer rate */
#define	FDC_300KBPS	0x01	/* 300KBPS MFM drive transfer rate */
#define	FDC_250KBPS	0x02	/* 250KBPS MFM drive transfer rate */
#define	FDC_125KBPS	0x03	/* 125KBPS FM drive transfer rate */

#define	fdin	0	/* Digital Input Register (R) */
#define	FDI_DCHG	0x80	/* diskette has been changed */

#define	FDC_NPORT	6
#define FDCTL_NPORT	1
#define	FDC_MAXIOSIZE	NBPG	/* XXX should be MAXBSIZE */
#define	FD_BSIZE(fd)	(128 << fd->sc_type->secsize)

#define FDUNIT(dev)	((dev & 0x80) >> 7)	/* XXX two drives max, sorry */
#define FDTYPE(dev)	((minor(dev) & 0x70) >> 4)
#define FDPART(dev)	(minor(dev) & 0x0f)
