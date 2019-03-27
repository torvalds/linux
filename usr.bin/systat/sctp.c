/*-
 * Copyright (c) 2015
 * The Regents of the University of California.  All rights reserved.
 * Michael Tuexen.  All rights reserved.
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

#include <sys/param.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/sysctl.h>

#include <netinet/sctp.h>

#include <stdlib.h>
#include <string.h>

#include "systat.h"
#include "extern.h"
#include "mode.h"

static struct sctpstat curstat, initstat, oldstat;

/*-
--0         1         2         3         4         5         6         7
--0123456789012345678901234567890123456789012345678901234567890123456789012345
00             SCTP Associations                     SCTP Packets
01999999999999 associations initiated   999999999999 packets sent
02999999999999 associations accepted    999999999999 packets received
03999999999999 associations restarted   999999999999 - out of the blue
04999999999999 associations terminated  999999999999 - bad vtag
05999999999999 associations aborted     999999999999 - bad crc32c
06
07             SCTP Timers                           SCTP Chunks
08999999999999 init timeouts            999999999999 control chunks sent
09999999999999 cookie timeouts          999999999999 data chunks sent
10999999999999 data timeouts            999999999999 - ordered
11999999999999 delayed sack timeouts    999999999999 - unordered
12999999999999 shutdown timeouts        999999999999 control chunks received
13999999999999 shutdown-ack timeouts    999999999999 data chunks received
14999999999999 shutdown guard timeouts  999999999999 - ordered
15999999999999 heartbeat timeouts       999999999999 - unordered
16999999999999 path MTU timeouts
17999999999999 autoclose timeouts                    SCTP user messages
18999999999999 asconf timeouts          999999999999 fragmented
19999999999999 stream reset timeouts    999999999999 reassembled
--0123456789012345678901234567890123456789012345678901234567890123456789012345
--0         1         2         3         4         5         6         7
*/

WINDOW *
opensctp(void)
{
	return (subwin(stdscr, LINES-3-1, 0, MAINWIN_ROW, 0));
}

void
closesctp(WINDOW *w)
{
	if (w != NULL) {
		wclear(w);
		wrefresh(w);
		delwin(w);
	}
}

void
labelsctp(void)
{
	wmove(wnd, 0, 0); wclrtoeol(wnd);
#define L(row, str) mvwprintw(wnd, row, 13, str)
#define R(row, str) mvwprintw(wnd, row, 51, str);
	L(0, "SCTP Associations");		R(0, "SCTP Packets");
	L(1, "associations initiated");		R(1, "packets sent");
	L(2, "associations accepted");		R(2, "packets received");
	L(3, "associations restarted");		R(3, "- out of the blue");
	L(4, "associations terminated");	R(4, "- bad vtag");
	L(5, "associations aborted");		R(5, "- bad crc32c");

	L(7, "SCTP Timers");			R(7, "SCTP Chunks");
	L(8, "init timeouts");			R(8, "control chunks sent");
	L(9, "cookie timeouts");		R(9, "data chunks sent");
	L(10, "data timeouts");			R(10, "- ordered");
	L(11, "delayed sack timeouts");		R(11, "- unordered");
	L(12, "shutdown timeouts");		R(12, "control chunks received");
	L(13, "shutdown-ack timeouts");		R(13, "data chunks received");
	L(14, "shutdown guard timeouts");	R(14, "- ordered");
	L(15, "heartbeat timeouts");		R(15, "- unordered");
	L(16, "path MTU timeouts");
	L(17, "autoclose timeouts");		R(17, "SCTP User Messages");
	L(18, "asconf timeouts");		R(18, "fragmented");
	L(19, "stream reset timeouts");		R(19, "reassembled");
#undef L
#undef R
}

static void
domode(struct sctpstat *ret)
{
	const struct sctpstat *sub;
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
	DO(sctps_currestab);
	DO(sctps_activeestab);
	DO(sctps_restartestab);
	DO(sctps_collisionestab);
	DO(sctps_passiveestab);
	DO(sctps_aborted);
	DO(sctps_shutdown);
	DO(sctps_outoftheblue);
	DO(sctps_checksumerrors);
	DO(sctps_outcontrolchunks);
	DO(sctps_outorderchunks);
	DO(sctps_outunorderchunks);
	DO(sctps_incontrolchunks);
	DO(sctps_inorderchunks);
	DO(sctps_inunorderchunks);
	DO(sctps_fragusrmsgs);
	DO(sctps_reasmusrmsgs);
	DO(sctps_outpackets);
	DO(sctps_inpackets);

	DO(sctps_recvpackets);
	DO(sctps_recvdatagrams);
	DO(sctps_recvpktwithdata);
	DO(sctps_recvsacks);
	DO(sctps_recvdata);
	DO(sctps_recvdupdata);
	DO(sctps_recvheartbeat);
	DO(sctps_recvheartbeatack);
	DO(sctps_recvecne);
	DO(sctps_recvauth);
	DO(sctps_recvauthmissing);
	DO(sctps_recvivalhmacid);
	DO(sctps_recvivalkeyid);
	DO(sctps_recvauthfailed);
	DO(sctps_recvexpress);
	DO(sctps_recvexpressm);
	DO(sctps_recvswcrc);
	DO(sctps_recvhwcrc);

	DO(sctps_sendpackets);
	DO(sctps_sendsacks);
	DO(sctps_senddata);
	DO(sctps_sendretransdata);
	DO(sctps_sendfastretrans);
	DO(sctps_sendmultfastretrans);
	DO(sctps_sendheartbeat);
	DO(sctps_sendecne);
	DO(sctps_sendauth);
	DO(sctps_senderrors);
	DO(sctps_sendswcrc);
	DO(sctps_sendhwcrc);

	DO(sctps_pdrpfmbox);
	DO(sctps_pdrpfehos);
	DO(sctps_pdrpmbda);
	DO(sctps_pdrpmbct);
	DO(sctps_pdrpbwrpt);
	DO(sctps_pdrpcrupt);
	DO(sctps_pdrpnedat);
	DO(sctps_pdrppdbrk);
	DO(sctps_pdrptsnnf);
	DO(sctps_pdrpdnfnd);
	DO(sctps_pdrpdiwnp);
	DO(sctps_pdrpdizrw);
	DO(sctps_pdrpbadd);
	DO(sctps_pdrpmark);

	DO(sctps_timoiterator);
	DO(sctps_timodata);
	DO(sctps_timowindowprobe);
	DO(sctps_timoinit);
	DO(sctps_timosack);
	DO(sctps_timoshutdown);
	DO(sctps_timoheartbeat);
	DO(sctps_timocookie);
	DO(sctps_timosecret);
	DO(sctps_timopathmtu);
	DO(sctps_timoshutdownack);
	DO(sctps_timoshutdownguard);
	DO(sctps_timostrmrst);
	DO(sctps_timoearlyfr);
	DO(sctps_timoasconf);
	DO(sctps_timodelprim);
	DO(sctps_timoautoclose);
	DO(sctps_timoassockill);
	DO(sctps_timoinpkill);

	DO(sctps_hdrops);
	DO(sctps_badsum);
	DO(sctps_noport);
	DO(sctps_badvtag);
	DO(sctps_badsid);
	DO(sctps_nomem);
	DO(sctps_fastretransinrtt);
	DO(sctps_markedretrans);
	DO(sctps_naglesent);
	DO(sctps_naglequeued);
	DO(sctps_maxburstqueued);
	DO(sctps_ifnomemqueued);
	DO(sctps_windowprobed);
	DO(sctps_lowlevelerr);
	DO(sctps_lowlevelerrusr);
	DO(sctps_datadropchklmt);
	DO(sctps_datadroprwnd);
	DO(sctps_ecnereducedcwnd);
	DO(sctps_vtagexpress);
	DO(sctps_vtagbogus);
	DO(sctps_primary_randry);
	DO(sctps_cmt_randry);
	DO(sctps_slowpath_sack);
	DO(sctps_wu_sacks_sent);
	DO(sctps_sends_with_flags);
	DO(sctps_sends_with_unord);
	DO(sctps_sends_with_eof);
	DO(sctps_sends_with_abort);
	DO(sctps_protocol_drain_calls);
	DO(sctps_protocol_drains_done);
	DO(sctps_read_peeks);
	DO(sctps_cached_chk);
	DO(sctps_cached_strmoq);
	DO(sctps_left_abandon);
	DO(sctps_send_burst_avoid);
	DO(sctps_send_cwnd_avoid);
	DO(sctps_fwdtsn_map_over);
	DO(sctps_queue_upd_ecne);
#undef DO
}

void
showsctp(void)
{
	struct sctpstat stats;

	memset(&stats, 0, sizeof stats);
	domode(&stats);

#define DO(stat, row, col) \
	mvwprintw(wnd, row, col, "%12lu", stats.stat)
#define	L(row, stat) DO(stat, row, 0)
#define	R(row, stat) DO(stat, row, 38)
	L(1, sctps_activeestab);	R(1, sctps_outpackets);
	L(2, sctps_passiveestab);	R(2, sctps_inpackets);
	L(3, sctps_restartestab);	R(3, sctps_outoftheblue);
	L(4, sctps_shutdown);		R(4, sctps_badvtag);
	L(5, sctps_aborted);		R(5, sctps_checksumerrors);


	L(8, sctps_timoinit);		R(8, sctps_outcontrolchunks);
	L(9, sctps_timocookie);		R(9, sctps_senddata);
	L(10, sctps_timodata);		R(10, sctps_outorderchunks);
	L(11, sctps_timosack);		R(11, sctps_outunorderchunks);
	L(12, sctps_timoshutdown);	R(12, sctps_incontrolchunks);
	L(13, sctps_timoshutdownack);	R(13, sctps_recvdata);
	L(14, sctps_timoshutdownguard);	R(14, sctps_inorderchunks);
	L(15, sctps_timoheartbeat);	R(15, sctps_inunorderchunks);
	L(16, sctps_timopathmtu);
	L(17, sctps_timoautoclose);
	L(18, sctps_timoasconf);	R(18, sctps_fragusrmsgs);
	L(19, sctps_timostrmrst);	R(19, sctps_reasmusrmsgs);
#undef DO
#undef L
#undef R
}

int
initsctp(void)
{
	size_t len;
	const char *name = "net.inet.sctp.stats";

	len = 0;
	if (sysctlbyname(name, NULL, &len, NULL, 0) < 0) {
		error("sysctl getting sctpstat size failed");
		return 0;
	}
	if (len > sizeof curstat) {
		error("sctpstat structure has grown--recompile systat!");
		return 0;
	}
	if (sysctlbyname(name, &initstat, &len, NULL, 0) < 0) {
		error("sysctl getting sctpstat failed");
		return 0;
	}
	oldstat = initstat;
	return 1;
}

void
resetsctp(void)
{
	size_t len;
	const char *name = "net.inet.sctp.stats";

	len = sizeof initstat;
	if (sysctlbyname(name, &initstat, &len, NULL, 0) < 0) {
		error("sysctl getting sctpstat failed");
	}
	oldstat = initstat;
}

void
fetchsctp(void)
{
	size_t len;
	const char *name = "net.inet.sctp.stats";

	oldstat = curstat;
	len = sizeof curstat;
	if (sysctlbyname(name, &curstat, &len, NULL, 0) < 0) {
		error("sysctl getting sctpstat failed");
	}
	return;
}
