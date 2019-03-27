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

#if 0
#ifndef lint
/* From: */
static char sccsid[] = "@(#)mbufs.c	8.1 (Berkeley) 6/6/93";
static const char rcsid[] =
	"Id: mbufs.c,v 1.5 1997/02/24 20:59:03 wollman Exp";
#endif /* not lint */
#endif

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/queue.h>
#include <sys/sysctl.h>

#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>
#include <netinet/tcp_seq.h>
#include <netinet/tcp_fsm.h>
#include <netinet/tcp_timer.h>
#include <netinet/tcp_var.h>

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <paths.h>

#include "systat.h"
#include "extern.h"
#include "mode.h"

static struct tcpstat curstat, initstat, oldstat;

/*-
--0         1         2         3         4         5         6         7
--0123456789012345678901234567890123456789012345678901234567890123456789012345
00             TCP Connections                       TCP Packets
01999999999999 connections initiated    999999999999 total packets sent
02999999999999 connections accepted     999999999999 - data
03999999999999 connections established  999999999999 - data (retransmit by dupack)
04999999999999 connections dropped      999999999999 - data (retransmit by sack)
05999999999999 - in embryonic state     999999999999 - ack-only
06999999999999 - on retransmit timeout  999999999999 - window probes
07999999999999 - by keepalive           999999999999 - window updates
08999999999999 - from listen queue      999999999999 - urgent data only
09                                      999999999999 - control
10                                      999999999999 - resends by PMTU discovery
11             TCP Timers               999999999999 total packets received
12999999999999 potential rtt updates    999999999999 - in sequence
13999999999999 - successful             999999999999 - completely duplicate
14999999999999 delayed acks sent        999999999999 - with some duplicate data
15999999999999 retransmit timeouts      999999999999 - out-of-order
16999999999999 persist timeouts         999999999999 - duplicate acks
17999999999999 keepalive probes         999999999999 - acks
18999999999999 - timeouts               999999999999 - window probes
19                                      999999999999 - window updates
20                                      999999999999 - bad checksum
--0123456789012345678901234567890123456789012345678901234567890123456789012345
--0         1         2         3         4         5         6         7
*/

WINDOW *
opentcp(void)
{
	return (subwin(stdscr, LINES-3-1, 0, MAINWIN_ROW, 0));
}

void
closetcp(WINDOW *w)
{
	if (w == NULL)
		return;
	wclear(w);
	wrefresh(w);
	delwin(w);
}

void
labeltcp(void)
{
	wmove(wnd, 0, 0); wclrtoeol(wnd);
#define L(row, str) mvwprintw(wnd, row, 13, str)
#define R(row, str) mvwprintw(wnd, row, 51, str);
	L(0, "TCP Connections");		R(0, "TCP Packets");
	L(1, "connections initiated");		R(1, "total packets sent");
	L(2, "connections accepted");		R(2, "- data");
	L(3, "connections established");	R(3, "- data (retransmit by dupack)");
	L(4, "connections dropped");		R(4, "- data (retransmit by sack)");
	L(5, "- in embryonic state");		R(5, "- ack-only");
	L(6, "- on retransmit timeout");	R(6, "- window probes");
	L(7, "- by keepalive");			R(7, "- window updates");
	L(8, "- from listen queue");		R(8, "- urgent data only");
						R(9, "- control");
						R(10, "- resends by PMTU discovery");
	L(11, "TCP Timers");			R(11, "total packets received");
	L(12, "potential rtt updates");		R(12, "- in sequence");
	L(13, "- successful");			R(13, "- completely duplicate");
	L(14, "delayed acks sent");		R(14, "- with some duplicate data");
	L(15, "retransmit timeouts");		R(15, "- out-of-order");
	L(16, "persist timeouts");		R(16, "- duplicate acks");
	L(17, "keepalive probes");		R(17, "- acks");
	L(18, "- timeouts");			R(18, "- window probes");
						R(19, "- window updates");
						R(20, "- bad checksum");
#undef L
#undef R
}

static void
domode(struct tcpstat *ret)
{
	const struct tcpstat *sub;
	int divisor = 1;

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
	DO(tcps_connattempt);
	DO(tcps_accepts);
	DO(tcps_connects);
	DO(tcps_drops);
	DO(tcps_conndrops);
	DO(tcps_closed);
	DO(tcps_segstimed);
	DO(tcps_rttupdated);
	DO(tcps_delack);
	DO(tcps_timeoutdrop);
	DO(tcps_rexmttimeo);
	DO(tcps_persisttimeo);
	DO(tcps_keeptimeo);
	DO(tcps_keepprobe);
	DO(tcps_keepdrops);

	DO(tcps_sndtotal);
	DO(tcps_sndpack);
	DO(tcps_sndbyte);
	DO(tcps_sndrexmitpack);
	DO(tcps_sack_rexmits);
	DO(tcps_sndacks);
	DO(tcps_sndprobe);
	DO(tcps_sndurg);
	DO(tcps_sndwinup);
	DO(tcps_sndctrl);

	DO(tcps_rcvtotal);
	DO(tcps_rcvpack);
	DO(tcps_rcvbyte);
	DO(tcps_rcvbadsum);
	DO(tcps_rcvbadoff);
	DO(tcps_rcvshort);
	DO(tcps_rcvduppack);
	DO(tcps_rcvdupbyte);
	DO(tcps_rcvpartduppack);
	DO(tcps_rcvpartdupbyte);
	DO(tcps_rcvoopack);
	DO(tcps_rcvoobyte);
	DO(tcps_rcvpackafterwin);
	DO(tcps_rcvbyteafterwin);
	DO(tcps_rcvafterclose);
	DO(tcps_rcvwinprobe);
	DO(tcps_rcvdupack);
	DO(tcps_rcvacktoomuch);
	DO(tcps_rcvackpack);
	DO(tcps_rcvackbyte);
	DO(tcps_rcvwinupd);
	DO(tcps_pawsdrop);
	DO(tcps_predack);
	DO(tcps_preddat);
	DO(tcps_pcbcachemiss);
	DO(tcps_cachedrtt);
	DO(tcps_cachedrttvar);
	DO(tcps_cachedssthresh);
	DO(tcps_usedrtt);
	DO(tcps_usedrttvar);
	DO(tcps_usedssthresh);
	DO(tcps_persistdrop);
	DO(tcps_badsyn);
	DO(tcps_mturesent);
	DO(tcps_listendrop);
#undef DO
}

void
showtcp(void)
{
	struct tcpstat stats;

	memset(&stats, 0, sizeof stats);
	domode(&stats);

#define DO(stat, row, col) \
	mvwprintw(wnd, row, col, "%12"PRIu64, stats.stat)
#define	L(row, stat) DO(stat, row, 0)
#define	R(row, stat) DO(stat, row, 38)
	L(1, tcps_connattempt);		R(1, tcps_sndtotal);
	L(2, tcps_accepts);		R(2, tcps_sndpack);
	L(3, tcps_connects);		R(3, tcps_sndrexmitpack);
	L(4, tcps_drops);		R(4, tcps_sack_rexmits);
	L(5, tcps_conndrops);		R(5, tcps_sndacks);
	L(6, tcps_timeoutdrop);		R(6, tcps_sndprobe);
	L(7, tcps_keepdrops);		R(7, tcps_sndwinup);
	L(8, tcps_listendrop);		R(8, tcps_sndurg);
					R(9, tcps_sndctrl);
					R(10, tcps_mturesent);
					R(11, tcps_rcvtotal);
	L(12, tcps_segstimed);		R(12, tcps_rcvpack);
	L(13, tcps_rttupdated);		R(13, tcps_rcvduppack);
	L(14, tcps_delack);		R(14, tcps_rcvpartduppack);
	L(15, tcps_rexmttimeo);		R(15, tcps_rcvoopack);
	L(16, tcps_persisttimeo);	R(16, tcps_rcvdupack);
	L(17, tcps_keepprobe);		R(17, tcps_rcvackpack);
	L(18, tcps_keeptimeo);		R(18, tcps_rcvwinprobe);
					R(19, tcps_rcvwinupd);
					R(20, tcps_rcvbadsum);
#undef DO
#undef L
#undef R
}

int
inittcp(void)
{
	size_t len;
	int name[4];

	name[0] = CTL_NET;
	name[1] = PF_INET;
	name[2] = IPPROTO_TCP;
	name[3] = TCPCTL_STATS;

	len = 0;
	if (sysctl(name, 4, 0, &len, 0, 0) < 0) {
		error("sysctl getting tcpstat size failed");
		return 0;
	}
	if (len > sizeof curstat) {
		error("tcpstat structure has grown--recompile systat!");
		return 0;
	}
	if (sysctl(name, 4, &initstat, &len, 0, 0) < 0) {
		error("sysctl getting tcpstat failed");
		return 0;
	}
	oldstat = initstat;
	return 1;
}

void
resettcp(void)
{
	size_t len;
	int name[4];

	name[0] = CTL_NET;
	name[1] = PF_INET;
	name[2] = IPPROTO_TCP;
	name[3] = TCPCTL_STATS;

	len = sizeof initstat;
	if (sysctl(name, 4, &initstat, &len, 0, 0) < 0) {
		error("sysctl getting tcpstat failed");
	}
	oldstat = initstat;
}

void
fetchtcp(void)
{
	int name[4];
	size_t len;

	oldstat = curstat;
	name[0] = CTL_NET;
	name[1] = PF_INET;
	name[2] = IPPROTO_TCP;
	name[3] = TCPCTL_STATS;
	len = sizeof curstat;

	if (sysctl(name, 4, &curstat, &len, 0, 0) < 0)
		return;
}
