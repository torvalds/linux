/*	$OpenBSD: partab.c,v 1.5 2003/06/03 02:56:18 millert Exp $	*/
/*	$NetBSD: partab.c,v 1.4 1996/12/29 10:38:21 cgd Exp $	*/

/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1983, 1993
 *	The Regents of the University of California.  All rights reserved.
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#ifndef lint
#if 0
static char sccsid[] = "@(#)partab.c	8.1 (Berkeley) 6/6/93";
static const char rcsid[] = "$OpenBSD: partab.c,v 1.5 2003/06/03 02:56:18 millert Exp $";
#endif
#endif /* not lint */

/*
 * Even parity table for 0-0177
 */
const unsigned char evenpartab[] = {
	0000,0201,0202,0003,0204,0005,0006,0207,
	0210,0011,0012,0213,0014,0215,0216,0017,
	0220,0021,0022,0223,0024,0225,0226,0027,
	0030,0231,0232,0033,0234,0035,0036,0237,
	0240,0041,0042,0243,0044,0245,0246,0047,
	0050,0251,0252,0053,0254,0055,0056,0257,
	0060,0261,0262,0063,0264,0065,0066,0267,
	0270,0071,0072,0273,0074,0275,0276,0077,
	0300,0101,0102,0303,0104,0305,0306,0107,
	0110,0311,0312,0113,0314,0115,0116,0317,
	0120,0321,0322,0123,0324,0125,0126,0327,
	0330,0131,0132,0333,0134,0335,0336,0137,
	0140,0341,0342,0143,0344,0145,0146,0347,
	0350,0151,0152,0353,0154,0355,0356,0157,
	0360,0161,0162,0363,0164,0365,0366,0167,
	0170,0371,0372,0173,0374,0175,0176,0377,
};
