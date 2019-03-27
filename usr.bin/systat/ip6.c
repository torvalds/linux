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
static const char sccsid[] = "@(#)mbufs.c	8.1 (Berkeley) 6/6/93";
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
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#include <netinet6/ip6_var.h>

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <paths.h>

#include "systat.h"
#include "extern.h"
#include "mode.h"

static struct ip6stat curstat, initstat, oldstat;

/*-
--0         1         2         3         4         5         6         7
--0123456789012345678901234567890123456789012345678901234567890123456789012345
00        IPv6 Input                         IPv6 Output
019999999 total packets received   999999999 total packets sent
029999999 - too short for header   999999999 - generated locally
039999999 - too short for data     999999999 - output drops
049999999 - with invalid version   999999999 output fragments generated
059999999 total fragments received 999999999 - fragmentation failed
069999999 - fragments dropped      999999999 destinations unreachable
079999999 - fragments timed out    999999999 packets output via raw IP
089999999 - fragments overflown
099999999 - packets reassembled ok           Input next-header histogram
109999999 packets forwarded        999999999  - destination options
119999999 - unreachable dests      999999999  - hop-by-hop options
129999999 - redirects generated    999999999  - IPv4
139999999 option errors            999999999  - TCP
149999999 unwanted multicasts      999999999  - UDP
159999999 delivered to upper layer 999999999  - IPv6
169999999 bad scope packets        999999999  - routing header
179999999 address selection failed 999999999  - fragmentation header
18                                 999999999  - ICMP6
19                                 999999999  - none
--0123456789012345678901234567890123456789012345678901234567890123456789012345
--0         1         2         3         4         5         6         7
*/

WINDOW *
openip6(void)
{
	return (subwin(stdscr, LINES-3-1, 0, MAINWIN_ROW, 0));
}

void
closeip6(WINDOW *w)
{
	if (w == NULL)
		return;
	wclear(w);
	wrefresh(w);
	delwin(w);
}

void
labelip6(void)
{
	wmove(wnd, 0, 0); wclrtoeol(wnd);
#define L(row, str) mvwprintw(wnd, row, 10, str)
#define R(row, str) mvwprintw(wnd, row, 45, str);
	L(0, "IPv6 Input");		R(0, "IPv6 Output");
	L(1, "total packets received");	R(1, "total packets sent");
	L(2, "- too short for header");	R(2, "- generated locally");
	L(3, "- too short for data");	R(3, "- output drops");
	L(4, "- with invalid version");	R(4, "output fragments generated");
	L(5, "total fragments received"); R(5, "- fragmentation failed");
	L(6, "- fragments dropped");	R(6, "destinations unreachable");
	L(7, "- fragments timed out");	R(7, "packets output via raw IP");
	L(8, "- fragments overflown");
	L(9, "- packets reassembled ok"); R(9, "Input next-header histogram");
	L(10, "packets forwarded");	R(10, " - destination options");
	L(11, "- unreachable dests");	R(11, " - hop-by-hop options");
	L(12, "- redirects generated");	R(12, " - IPv4");
	L(13, "option errors");		R(13, " - TCP");
	L(14, "unwanted multicasts");	R(14, " - UDP");
	L(15, "delivered to upper layer"); R(15, " - IPv6");
	L(16, "bad scope packets");	R(16, " - routing header");
	L(17, "address selection failed"); R(17, " - fragmentation header");
					R(18, " - ICMP6");
					R(19, " - none");
#undef L
#undef R
}

static void
domode(struct ip6stat *ret)
{
	const struct ip6stat *sub;
	int divisor = 1, i;

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
		*ret = curstat;
		return;
	}
#define DO(stat) ret->stat = (curstat.stat - sub->stat) / divisor
	DO(ip6s_total);
	DO(ip6s_tooshort);
	DO(ip6s_toosmall);
	DO(ip6s_fragments);
	DO(ip6s_fragdropped);
	DO(ip6s_fragtimeout);
	DO(ip6s_fragoverflow);
	DO(ip6s_forward);
	DO(ip6s_cantforward);
	DO(ip6s_redirectsent);
	DO(ip6s_delivered);
	DO(ip6s_localout);
	DO(ip6s_odropped);
	DO(ip6s_reassembled);
	DO(ip6s_fragmented);
	DO(ip6s_ofragments);
	DO(ip6s_cantfrag);
	DO(ip6s_badoptions);
	DO(ip6s_noroute);
	DO(ip6s_badvers);
	DO(ip6s_rawout);
	DO(ip6s_notmember);
	for (i = 0; i < 256; i++)
		DO(ip6s_nxthist[i]);
	DO(ip6s_badscope);
	DO(ip6s_sources_none);
#undef DO
}

void
showip6(void)
{
	struct ip6stat stats;
	uint64_t totalout;

	domode(&stats);
	totalout = stats.ip6s_forward + stats.ip6s_localout;

#define DO(stat, row, col) \
	mvwprintw(wnd, row, col, "%9"PRIu64, stats.stat)

	DO(ip6s_total, 1, 0);
	mvwprintw(wnd, 1, 35, "%9"PRIu64, totalout);
	DO(ip6s_tooshort, 2, 0);
	DO(ip6s_localout, 2, 35);
	DO(ip6s_toosmall, 3, 0);
	DO(ip6s_odropped, 3, 35);
	DO(ip6s_badvers, 4, 0);
	DO(ip6s_ofragments, 4, 35);
	DO(ip6s_fragments, 5, 0);
	DO(ip6s_cantfrag, 5, 35);
	DO(ip6s_fragdropped, 6, 0);
	DO(ip6s_noroute, 6, 35);
	DO(ip6s_fragtimeout, 7, 0);
	DO(ip6s_rawout, 7, 35);
	DO(ip6s_fragoverflow, 8, 0);
	DO(ip6s_reassembled, 9, 0);
	DO(ip6s_forward, 10, 0);
	DO(ip6s_nxthist[IPPROTO_DSTOPTS], 10, 35);
	DO(ip6s_cantforward, 11, 0);
	DO(ip6s_nxthist[IPPROTO_HOPOPTS], 11, 35);
	DO(ip6s_redirectsent, 12, 0);
	DO(ip6s_nxthist[IPPROTO_IPV4], 12, 35);
	DO(ip6s_badoptions, 13, 0);
	DO(ip6s_nxthist[IPPROTO_TCP], 13, 35);
	DO(ip6s_notmember, 14, 0);
	DO(ip6s_nxthist[IPPROTO_UDP], 14, 35);
	DO(ip6s_delivered, 15, 0);
	DO(ip6s_nxthist[IPPROTO_IPV6], 15, 35);
	DO(ip6s_badscope, 16, 0);
	DO(ip6s_nxthist[IPPROTO_ROUTING], 16, 35);
	DO(ip6s_sources_none, 17, 0);
	DO(ip6s_nxthist[IPPROTO_FRAGMENT], 17, 35);
	DO(ip6s_nxthist[IPPROTO_ICMPV6], 18, 35);
	DO(ip6s_nxthist[IPPROTO_NONE], 19, 35);
#undef DO
}

int
initip6(void)
{
	size_t len;
	int name[4];

	name[0] = CTL_NET;
	name[1] = PF_INET6;
	name[2] = IPPROTO_IPV6;
	name[3] = IPV6CTL_STATS;

	len = 0;
	if (sysctl(name, 4, 0, &len, 0, 0) < 0) {
		error("sysctl getting ip6stat size failed");
		return 0;
	}
	if (len > sizeof curstat) {
		error("ip6stat structure has grown--recompile systat!");
		return 0;
	}
	if (sysctl(name, 4, &initstat, &len, 0, 0) < 0) {
		error("sysctl getting ip6stat failed");
		return 0;
	}
	oldstat = initstat;
	return 1;
}

void
resetip6(void)
{
	size_t len;
	int name[4];

	name[0] = CTL_NET;
	name[1] = PF_INET6;
	name[2] = IPPROTO_IPV6;
	name[3] = IPV6CTL_STATS;

	len = sizeof initstat;
	if (sysctl(name, 4, &initstat, &len, 0, 0) < 0) {
		error("sysctl getting ipstat failed");
	}

	oldstat = initstat;
}

void
fetchip6(void)
{
	int name[4];
	size_t len;

	oldstat = curstat;
	name[0] = CTL_NET;
	name[1] = PF_INET6;
	name[2] = IPPROTO_IPV6;
	name[3] = IPV6CTL_STATS;
	len = sizeof curstat;

	if (sysctl(name, 4, &curstat, &len, 0, 0) < 0)
		return;
}

#endif
