/*	$OpenBSD: acutab.c,v 1.5 2006/03/17 19:17:13 moritz Exp $	*/
/*	$NetBSD: acutab.c,v 1.3 1994/12/08 09:30:41 jtc Exp $	*/

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
static char sccsid[] = "@(#)acutab.c	8.1 (Berkeley) 6/6/93";
static const char rcsid[] = "$OpenBSD: acutab.c,v 1.5 2006/03/17 19:17:13 moritz Exp $";
#endif
#endif /* not lint */

#include "tip.h"

acu_t acutable[] = {
#if BIZ1031
	{ "biz31f",	biz31f_dialer,	biz31_disconnect,	biz31_abort },
	{ "biz31w",	biz31w_dialer,	biz31_disconnect,	biz31_abort },
#endif
#if BIZ1022
	{ "biz22f",	biz22f_dialer,	biz22_disconnect,	biz22_abort },
	{ "biz22w",	biz22w_dialer,	biz22_disconnect,	biz22_abort },
#endif
#if DF02
	{ "df02",	df02_dialer,	df_disconnect,		df_abort },
#endif
#if DF03
	{ "df03",	df03_dialer,	df_disconnect,		df_abort },
#endif
#if DN11
	{ "dn11",	dn_dialer,	dn_disconnect,		dn_abort },
#endif
#ifdef VENTEL
	{ "ventel",	ven_dialer,	ven_disconnect,		ven_abort },
#endif
#ifdef HAYES
	{ "hayes",	hay_dialer,	hay_disconnect,		hay_abort },
#endif
#ifdef COURIER
	{ "courier",	cour_dialer,	cour_disconnect,	cour_abort },
#endif
#ifdef T3000
	{ "t3000",	t3000_dialer,	t3000_disconnect,	t3000_abort },
#endif
#ifdef V3451
#ifndef V831
	{ "vadic",	v3451_dialer,	v3451_disconnect,	v3451_abort },
#endif
	{ "v3451",	v3451_dialer,	v3451_disconnect,	v3451_abort },
#endif
#ifdef V831
#ifndef V3451
	{ "vadic",	v831_dialer,	v831_disconnect,	v831_abort },
#endif
	{ "v831",	v831_dialer,	v831_disconnect,	v831_abort },
#endif
	{ 0,		0,		0,			0 }
};

