/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1990 The Regents of the University of California.
 * All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * William Jolitz.
 *
 * Copyright (c) 1997, 1999 Nicolas Souchu
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
 * Distantly from :
 *	@(#)lptreg.h      1.1 (Berkeley) 12/19/90
 *	Id: lptreg.h,v 1.6 1997/02/22 09:36:52 peter Exp
 *	From Id: nlpt.h,v 1.3 1999/01/10 12:04:54 nsouch Exp
 *
 * $FreeBSD$
 */

/*
 * AT Parallel Port (for lineprinter)
 * Interface port and bit definitions
 * Written by William Jolitz 12/18/90
 * Copyright (C) William Jolitz 1990
 */

#ifndef __LPT_H
#define	__LPT_H

/* machine independent definitions, it shall only depend on the ppbus
 * parallel port model */

					/* PIN */
#define	LPS_NERR		0x08	/* 15  printer no error */
#define	LPS_SEL			0x10	/* 13  printer selected */
#define	LPS_OUT			0x20	/* 12  printer out of paper */
#define	LPS_NACK		0x40	/* 10  printer no ack of data */
#define	LPS_NBSY		0x80	/* 11  printer busy */

#define	LPC_STB			0x01	/*  1  strobe data to printer */
#define	LPC_AUTOL		0x02	/* 14  automatic linefeed */
#define	LPC_NINIT		0x04	/* 16  initialize printer */
#define	LPC_SEL			0x08	/* 17  printer selected */
#define	LPC_ENA			0x10	/*  -  enable IRQ */

#endif
