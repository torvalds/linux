/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1980, 1992, 1993
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

#ifdef lint
static const char sccsid[] = "@(#)cmdtab.c	8.1 (Berkeley) 6/6/93";
#endif

#include "systat.h"
#include "extern.h"
#include "mode.h"

struct	cmdtab cmdtab[] = {
	{ "pigs",	showpigs,	fetchpigs,	labelpigs,
	  initpigs,	openpigs,	closepigs,	0,
	  0,		CF_LOADAV },
	{ "swap",	showswap,	fetchswap,	labelswap,
	  initswap,	openswap,	closeswap,	0,
	  0,		CF_LOADAV },
	{ "iostat",	showiostat,	fetchiostat,	labeliostat,
	  initiostat,	openiostat,	closeiostat,	cmdiostat,
	  0,		CF_LOADAV },
	{ "vmstat",	showkre,	fetchkre,	labelkre,
	  initkre,	openkre,	closekre,	cmdkre,
	  0,		0 },
	{ "netstat",	shownetstat,	fetchnetstat,	labelnetstat,
	  initnetstat,	opennetstat,	closenetstat,	cmdnetstat,
	  0,		CF_LOADAV },
	{ "icmp",	showicmp,	fetchicmp,	labelicmp,
	  initicmp,	openicmp,	closeicmp,	cmdmode,
	  reseticmp,	CF_LOADAV },
	{ "ip",		showip,		fetchip,	labelip,
	  initip,	openip,		closeip,	cmdmode,
	  resetip,	CF_LOADAV },
#ifdef INET6
	{ "icmp6",	showicmp6,	fetchicmp6,	labelicmp6,
	  initicmp6,	openicmp6,	closeicmp6,	cmdmode,
	  reseticmp6,	CF_LOADAV },
	{ "ip6",	showip6,	fetchip6,	labelip6,
	  initip6,	openip6,	closeip6,	cmdmode,
	  resetip6,	CF_LOADAV },
#endif
	{ "sctp",	showsctp,	fetchsctp,	labelsctp,
	  initsctp,	opensctp,	closesctp,	cmdmode,
	  resetsctp,	CF_LOADAV },
	{ "tcp",	showtcp,	fetchtcp,	labeltcp,
	  inittcp,	opentcp,	closetcp,	cmdmode,
	  resettcp,	CF_LOADAV },
	{ "ifstat",	showifstat,	fetchifstat,	labelifstat,
	  initifstat,	openifstat,	closeifstat,	cmdifstat,
	  0,		CF_LOADAV },
	{ "zarc",	showzarc,	fetchzarc,	labelzarc,
	  initzarc,	openzarc,	closezarc,	0,
	  resetzarc,	CF_ZFSARC },
	{ NULL, NULL, NULL, NULL, NULL, NULL, NULL, 0, 0, 0 }
};
struct  cmdtab *curcmd = &cmdtab[0];
