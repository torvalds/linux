/*	$OpenBSD: fdreg.h,v 1.1 2005/03/09 18:41:49 miod Exp $	*/
/*	$NetBSD: fdreg.h,v 1.9 2003/08/07 16:29:35 agc Exp $	*/

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

/*
 * Register offsets for the 82077 controller.
 */
#define FDREG77_STATUSA	0
#define FDREG77_STATUSB	1
#define FDREG77_DOR	2		/* Digital Output Register (R/W) */
#define FDREG77_TDR	3		/* Tape Control Register (R/W) */
#define FDREG77_MSR	4		/* Main Status Register (R) */
#define FDREG77_DRS	4		/* Data Rate Select Register (W) */
#define FDREG77_FIFO	5		/* Data (FIFO) register (R/W) */
#define FDREG77_DIR	7		/* Digital Input Register (R) */
#define FDREG77_CCR	7		/* Configuration Control (W) */

/* Data Select Register bits */
#define DRS_RESET	0x80
#define DRS_POWER	0x40
#define DRS_PLL		0x20
#define	FDC_500KBPS	0x00		/*   500KBPS MFM drive transfer rate */
#define	FDC_300KBPS	0x01		/*   300KBPS MFM drive transfer rate */
#define	FDC_250KBPS	0x02		/*   250KBPS MFM drive transfer rate */
#define	FDC_125KBPS	0x03		/*   125KBPS  FM drive transfer rate */

/* Digital Output Register bits (modified on suns) */
#define	FDO_DS		0x01		/*  floppy device select (neg) */
#define	FDO_FRST	0x04		/*  floppy controller reset (neg) */
#define	FDO_FDMAEN	0x08		/*  enable floppy DMA and Interrupt */
#define	FDO_MOEN(n)	((1 << n) << 4)	/* motor enable */
#define FDO_DEN		0x40		/* Density select */
#define FDO_EJ		0x80		/* Eject disk */

/* Digital Input Register bits */
#define	FDI_DCHG	0x80		/*   diskette has been changed */

/* XXX - find a place for these... */
#define NE7CMD_CFG		0x13
#define CFG_EIS			0x40
#define CFG_EFIFO		0x20
#define CFG_POLL		0x10
#define CFG_THRHLD_MASK		0x0f

#define NE7CMD_LOCK		0x14
#define CFG_LOCK		0x80

#define NE7CMD_MOTOR		0x0b
#define MOTOR_ON		0x80

#define NE7CMD_DUMPREG		0x0e
#define NE7CMD_VERSION		0x10

#define ST1_OVERRUN		0x10

#define NE7_SPECIFY_NODMA	0x01
