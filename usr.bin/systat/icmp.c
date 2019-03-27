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
static char sccsid[] = "@(#)mbufs.c	8.1 (Berkeley) 6/6/93";
#endif

/* From:
	"Id: mbufs.c,v 1.5 1997/02/24 20:59:03 wollman Exp"
*/

#include <sys/param.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/sysctl.h>

#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#include <netinet/ip_icmp.h>
#include <netinet/icmp_var.h>

#include <stdlib.h>
#include <string.h>
#include <paths.h>
#include "systat.h"
#include "extern.h"
#include "mode.h"

static struct icmpstat icmpstat, initstat, oldstat;

/*-
--0         1         2         3         4         5         6         7
--0123456789012345678901234567890123456789012345678901234567890123456789012345
00          ICMP Input                         ICMP Output
01999999999 total messages           999999999 total messages
02999999999 with bad code            999999999 errors generated
03999999999 with bad length          999999999 suppressed - original too short
04999999999 with bad checksum        999999999 suppressed - original was ICMP
05999999999 with insufficient data   999999999 responses sent
06                                   999999999 suppressed - multicast echo
07                                   999999999 suppressed - multicast tstamp
08
09          Input Histogram                    Output Histogram
10999999999 echo response            999999999 echo response
11999999999 echo request             999999999 echo request
12999999999 destination unreachable  999999999 destination unreachable
13999999999 redirect                 999999999 redirect
14999999999 time-to-live exceeded    999999999 time-to-line exceeded
15999999999 parameter problem        999999999 parameter problem
16999999999 router advertisement     999999999 router solicitation
17
18
--0123456789012345678901234567890123456789012345678901234567890123456789012345
--0         1         2         3         4         5         6         7
*/

WINDOW *
openicmp(void)
{
	return (subwin(stdscr, LINES-3-1, 0, MAINWIN_ROW, 0));
}

void
closeicmp(WINDOW *w)
{
	if (w == NULL)
		return;
	wclear(w);
	wrefresh(w);
	delwin(w);
}

void
labelicmp(void)
{
	wmove(wnd, 0, 0); wclrtoeol(wnd);
#define L(row, str) mvwprintw(wnd, row, 10, str)
#define R(row, str) mvwprintw(wnd, row, 45, str);
	L(0, "ICMP Input");		R(0, "ICMP Output");
	L(1, "total messages");		R(1, "total messages");
	L(2, "with bad code");		R(2, "errors generated");
	L(3, "with bad length");	R(3, "suppressed - original too short");
	L(4, "with bad checksum");	R(4, "suppressed - original was ICMP");
	L(5, "with insufficient data");	R(5, "responses sent");
					R(6, "suppressed - multicast echo");
					R(7, "suppressed - multicast tstamp");
	L(9, "Input Histogram");	R(9, "Output Histogram");
#define B(row, str) L(row, str); R(row, str)
	B(10, "echo response");
	B(11, "echo request");
	B(12, "destination unreachable");
	B(13, "redirect");
	B(14, "time-to-live exceeded");
	B(15, "parameter problem");
	L(16, "router advertisement");	R(16, "router solicitation");
#undef L
#undef R
#undef B
}

static void
domode(struct icmpstat *ret)
{
	const struct icmpstat *sub;
	int i, divisor = 1;

	switch(currentmode) {
	case display_RATE:
		sub = &oldstat;
		divisor = (delay > 1000000) ? delay / 1000000 : 1;
		break;
	case display_DELTA:
		sub = &oldstat;
		break;
	case display_SINCE:
		sub = &initstat;
		break;
	default:
		*ret = icmpstat;
		return;
	}
#define DO(stat) ret->stat = (icmpstat.stat - sub->stat) / divisor
	DO(icps_error);
	DO(icps_oldshort);
	DO(icps_oldicmp);
	for (i = 0; i <= ICMP_MAXTYPE; i++) {
		DO(icps_outhist[i]);
	}
	DO(icps_badcode);
	DO(icps_tooshort);
	DO(icps_checksum);
	DO(icps_badlen);
	DO(icps_reflect);
	for (i = 0; i <= ICMP_MAXTYPE; i++) {
		DO(icps_inhist[i]);
	}
	DO(icps_bmcastecho);
	DO(icps_bmcasttstamp);
#undef DO
}

void
showicmp(void)
{
	struct icmpstat stats;
	u_long totalin, totalout;
	int i;

	memset(&stats, 0, sizeof stats);
	domode(&stats);
	for (i = totalin = totalout = 0; i <= ICMP_MAXTYPE; i++) {
		totalin += stats.icps_inhist[i];
		totalout += stats.icps_outhist[i];
	}
	totalin += stats.icps_badcode + stats.icps_badlen +
		stats.icps_checksum + stats.icps_tooshort;
	mvwprintw(wnd, 1, 0, "%9lu", totalin);
	mvwprintw(wnd, 1, 35, "%9lu", totalout);

#define DO(stat, row, col) \
	mvwprintw(wnd, row, col, "%9lu", stats.stat)

	DO(icps_badcode, 2, 0);
	DO(icps_badlen, 3, 0);
	DO(icps_checksum, 4, 0);
	DO(icps_tooshort, 5, 0);
	DO(icps_error, 2, 35);
	DO(icps_oldshort, 3, 35);
	DO(icps_oldicmp, 4, 35);
	DO(icps_reflect, 5, 35);
	DO(icps_bmcastecho, 6, 35);
	DO(icps_bmcasttstamp, 7, 35);
#define DO2(type, row) DO(icps_inhist[type], row, 0); DO(icps_outhist[type], \
							 row, 35)
	DO2(ICMP_ECHOREPLY, 10);
	DO2(ICMP_ECHO, 11);
	DO2(ICMP_UNREACH, 12);
	DO2(ICMP_REDIRECT, 13);
	DO2(ICMP_TIMXCEED, 14);
	DO2(ICMP_PARAMPROB, 15);
	DO(icps_inhist[ICMP_ROUTERADVERT], 16, 0);
	DO(icps_outhist[ICMP_ROUTERSOLICIT], 16, 35);
#undef DO
#undef DO2
}

int
initicmp(void)
{
	size_t len;
	int name[4];

	name[0] = CTL_NET;
	name[1] = PF_INET;
	name[2] = IPPROTO_ICMP;
	name[3] = ICMPCTL_STATS;

	len = 0;
	if (sysctl(name, 4, 0, &len, 0, 0) < 0) {
		error("sysctl getting icmpstat size failed");
		return 0;
	}
	if (len > sizeof icmpstat) {
		error("icmpstat structure has grown--recompile systat!");
		return 0;
	}
	if (sysctl(name, 4, &initstat, &len, 0, 0) < 0) {
		error("sysctl getting icmpstat size failed");
		return 0;
	}
	oldstat = initstat;
	return 1;
}

void
reseticmp(void)
{
	size_t len;
	int name[4];

	name[0] = CTL_NET;
	name[1] = PF_INET;
	name[2] = IPPROTO_ICMP;
	name[3] = ICMPCTL_STATS;

	len = sizeof initstat;
	if (sysctl(name, 4, &initstat, &len, 0, 0) < 0) {
		error("sysctl getting icmpstat size failed");
	}
	oldstat = initstat;
}

void
fetchicmp(void)
{
	int name[4];
	size_t len;

	oldstat = icmpstat;
	name[0] = CTL_NET;
	name[1] = PF_INET;
	name[2] = IPPROTO_ICMP;
	name[3] = ICMPCTL_STATS;
	len = sizeof icmpstat;

	if (sysctl(name, 4, &icmpstat, &len, 0, 0) < 0)
		return;
}
