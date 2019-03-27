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

#ifdef INET6
#include <sys/param.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/sysctl.h>

#include <netinet/in.h>
#include <netinet/icmp6.h>

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <paths.h>
#include "systat.h"
#include "extern.h"
#include "mode.h"

static struct icmp6stat icmp6stat, initstat, oldstat;

/*-
--0         1         2         3         4         5         6         7
--0123456789012345678901234567890123456789012345678901234567890123456789012345
00          ICMPv6 Input                       ICMPv6 Output
01999999999 total messages           999999999 total messages
02999999999 with bad code            999999999 errors generated
03999999999 with bad length          999999999 suppressed - original too short
04999999999 with bad checksum        999999999 suppressed - original was ICMP
05999999999 with insufficient data   999999999 responses sent
06
07          Input Histogram                    Output Histogram
08999999999 echo response            999999999 echo response
09999999999 echo request             999999999 echo request
10999999999 destination unreachable  999999999 destination unreachable
11999999999 redirect                 999999999 redirect
12999999999 time-to-live exceeded    999999999 time-to-line exceeded
13999999999 parameter problem        999999999 parameter problem
14999999999 neighbor solicitation    999999999 neighbor solicitation
15999999999 neighbor advertisement   999999999 neighbor advertisement
16999999999 router advertisement     999999999 router solicitation
17
18
--0123456789012345678901234567890123456789012345678901234567890123456789012345
--0         1         2         3         4         5         6         7
*/

WINDOW *
openicmp6(void)
{
	return (subwin(stdscr, LINES-3-1, 0, MAINWIN_ROW, 0));
}

void
closeicmp6(WINDOW *w)
{
	if (w == NULL)
		return;
	wclear(w);
	wrefresh(w);
	delwin(w);
}

void
labelicmp6(void)
{
	wmove(wnd, 0, 0); wclrtoeol(wnd);
#define L(row, str) mvwprintw(wnd, row, 10, str)
#define R(row, str) mvwprintw(wnd, row, 45, str);
	L(0, "ICMPv6 Input");		R(0, "ICMPv6 Output");
	L(1, "total messages");		R(1, "total messages");
	L(2, "with bad code");		R(2, "errors generated");
	L(3, "with bad length");	R(3, "suppressed - original too short");
	L(4, "with bad checksum");	R(4, "suppressed - original was ICMP");
	L(5, "with insufficient data");	R(5, "responses sent");

	L(7, "Input Histogram");	R(7, "Output Histogram");
#define B(row, str) L(row, str); R(row, str)
	B(8, "echo response");
	B(9, "echo request");
	B(10, "destination unreachable");
	B(11, "redirect");
	B(12, "time-to-live exceeded");
	B(13, "parameter problem");
	B(14, "neighbor solicitation");
	B(15, "neighbor advertisement");
	L(16, "router advertisement");	R(16, "router solicitation");
#undef L
#undef R
#undef B
}

static void
domode(struct icmp6stat *ret)
{
	const struct icmp6stat *sub;
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
		*ret = icmp6stat;
		return;
	}
#define DO(stat) ret->stat = (icmp6stat.stat - sub->stat) / divisor
	DO(icp6s_error);
	DO(icp6s_tooshort);
	DO(icp6s_canterror);
	for (i = 0; i <= ICMP6_MAXTYPE; i++) {
		DO(icp6s_outhist[i]);
	}
	DO(icp6s_badcode);
	DO(icp6s_tooshort);
	DO(icp6s_checksum);
	DO(icp6s_badlen);
	DO(icp6s_reflect);
	for (i = 0; i <= ICMP6_MAXTYPE; i++) {
		DO(icp6s_inhist[i]);
	}
#undef DO
}

void
showicmp6(void)
{
	struct icmp6stat stats;
	uint64_t totalin, totalout;
	int i;

	memset(&stats, 0, sizeof stats);
	domode(&stats);
	for (i = totalin = totalout = 0; i <= ICMP6_MAXTYPE; i++) {
		totalin += stats.icp6s_inhist[i];
		totalout += stats.icp6s_outhist[i];
	}
	totalin += stats.icp6s_badcode + stats.icp6s_badlen +
		stats.icp6s_checksum + stats.icp6s_tooshort;
	mvwprintw(wnd, 1, 0, "%9"PRIu64, totalin);
	mvwprintw(wnd, 1, 35, "%9"PRIu64, totalout);

#define DO(stat, row, col) \
	mvwprintw(wnd, row, col, "%9"PRIu64, stats.stat)

	DO(icp6s_badcode, 2, 0);
	DO(icp6s_badlen, 3, 0);
	DO(icp6s_checksum, 4, 0);
	DO(icp6s_tooshort, 5, 0);
	DO(icp6s_error, 2, 35);
	DO(icp6s_tooshort, 3, 35);
	DO(icp6s_canterror, 4, 35);
	DO(icp6s_reflect, 5, 35);
#define DO2(type, row) DO(icp6s_inhist[type], row, 0); DO(icp6s_outhist[type], \
							 row, 35)
	DO2(ICMP6_ECHO_REPLY, 8);
	DO2(ICMP6_ECHO_REQUEST, 9);
	DO2(ICMP6_DST_UNREACH, 10);
	DO2(ND_REDIRECT, 11);
	DO2(ICMP6_TIME_EXCEEDED, 12);
	DO2(ICMP6_PARAM_PROB, 13);
	DO2(ND_NEIGHBOR_SOLICIT, 14);
	DO2(ND_NEIGHBOR_ADVERT, 15);
	DO(icp6s_inhist[ND_ROUTER_SOLICIT], 16, 0);
	DO(icp6s_outhist[ND_ROUTER_ADVERT], 16, 35);
#undef DO
#undef DO2
}

int
initicmp6(void)
{
	size_t len;
	int name[4];

	name[0] = CTL_NET;
	name[1] = PF_INET6;
	name[2] = IPPROTO_ICMPV6;
	name[3] = ICMPV6CTL_STATS;

	len = 0;
	if (sysctl(name, 4, 0, &len, 0, 0) < 0) {
		error("sysctl getting icmp6stat size failed");
		return 0;
	}
	if (len > sizeof icmp6stat) {
		error("icmp6stat structure has grown--recompile systat!");
		return 0;
	}
	if (sysctl(name, 4, &initstat, &len, 0, 0) < 0) {
		error("sysctl getting icmp6stat size failed");
		return 0;
	}
	oldstat = initstat;
	return 1;
}

void
reseticmp6(void)
{
	size_t len;
	int name[4];

	name[0] = CTL_NET;
	name[1] = PF_INET6;
	name[2] = IPPROTO_ICMPV6;
	name[3] = ICMPV6CTL_STATS;

	len = sizeof initstat;
	if (sysctl(name, 4, &initstat, &len, 0, 0) < 0) {
		error("sysctl getting icmp6stat size failed");
	}
	oldstat = initstat;
}

void
fetchicmp6(void)
{
	int name[4];
	size_t len;

	oldstat = icmp6stat;
	name[0] = CTL_NET;
	name[1] = PF_INET6;
	name[2] = IPPROTO_ICMPV6;
	name[3] = ICMPV6CTL_STATS;
	len = sizeof icmp6stat;

	if (sysctl(name, 4, &icmp6stat, &len, 0, 0) < 0)
		return;
}

#endif
