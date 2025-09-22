/*	$OpenBSD: lptreg.h,v 1.4 2003/06/02 23:28:02 millert Exp $	*/
/*	$NetBSD: lptreg.h,v 1.4 1994/10/27 04:17:56 cgd Exp $	*/

/*-
 * Copyright (c) 1990 The Regents of the University of California.
 * All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * William Jolitz.
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
 *      @(#)lptreg.h	1.1 (Berkeley) 12/19/90
 */

/*
 * AT Parallel Port (for lineprinter)
 * Interface port and bit definitions
 * Written by William Jolitz 12/18/90
 * Copyright (C) William Jolitz 1990
 */

#define	lpt_data	0	/* Data to/from printer (R/W) */

#define	lpt_status	1	/* Status of printer (R) */
#define	LPS_NERR		0x08	/* printer no error */
#define	LPS_SELECT		0x10	/* printer selected */
#define	LPS_NOPAPER		0x20	/* printer out of paper */
#define	LPS_NACK		0x40	/* printer no ack of data */
#define	LPS_NBSY		0x80	/* printer no ack of data */

#define	lpt_control	2	/* Control printer (R/W) */
#define	LPC_STROBE		0x01	/* strobe data to printer */
#define	LPC_AUTOLF		0x02	/* automatic linefeed */
#define	LPC_NINIT		0x04	/* initialize printer */
#define	LPC_SELECT		0x08	/* printer selected */
#define	LPC_IENABLE		0x10	/* printer out of paper */

#define	LPT_NPORTS	4
