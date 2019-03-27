/*	BSDI inet.c,v 2.3 1995/10/24 02:19:29 prb Exp	*/
/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1983, 1988, 1993
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
static char sccsid[] = "@(#)inet6.c	8.4 (Berkeley) 4/20/94";
#endif /* not lint */
#endif

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#ifdef INET6
#include <sys/param.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/ioctl.h>
#include <sys/mbuf.h>
#include <sys/protosw.h>

#include <net/route.h>
#include <net/if.h>
#include <netinet/in.h>
#include <netinet/ip6.h>
#include <netinet/icmp6.h>
#include <netinet/in_systm.h>
#include <netinet6/in6_pcb.h>
#include <netinet6/in6_var.h>
#include <netinet6/ip6_var.h>
#include <netinet6/pim6_var.h>
#include <netinet6/raw_ip6.h>

#include <arpa/inet.h>
#include <netdb.h>

#include <err.h>
#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <libxo/xo.h>
#include "netstat.h"

static char ntop_buf[INET6_ADDRSTRLEN];

static	const char *ip6nh[] = {
	"hop by hop",
	"ICMP",
	"IGMP",
	"#3",
	"IP",
	"#5",
	"TCP",
	"#7",
	"#8",
	"#9",
	"#10",
	"#11",
	"#12",
	"#13",
	"#14",
	"#15",
	"#16",
	"UDP",
	"#18",
	"#19",
	"#20",
	"#21",
	"IDP",
	"#23",
	"#24",
	"#25",
	"#26",
	"#27",
	"#28",
	"TP",
	"#30",
	"#31",
	"#32",
	"#33",
	"#34",
	"#35",
	"#36",
	"#37",
	"#38",
	"#39",
	"#40",
	"IP6",
	"#42",
	"routing",
	"fragment",
	"#45",
	"#46",
	"#47",
	"#48",
	"#49",
	"ESP",
	"AH",
	"#52",
	"#53",
	"#54",
	"#55",
	"#56",
	"#57",
	"ICMP6",
	"no next header",
	"destination option",
	"#61",
	"mobility",
	"#63",
	"#64",
	"#65",
	"#66",
	"#67",
	"#68",
	"#69",
	"#70",
	"#71",
	"#72",
	"#73",
	"#74",
	"#75",
	"#76",
	"#77",
	"#78",
	"#79",
	"ISOIP",
	"#81",
	"#82",
	"#83",
	"#84",
	"#85",
	"#86",
	"#87",
	"#88",
	"OSPF",
	"#80",
	"#91",
	"#92",
	"#93",
	"#94",
	"#95",
	"#96",
	"Ethernet",
	"#98",
	"#99",
	"#100",
	"#101",
	"#102",
	"PIM",
	"#104",
	"#105",
	"#106",
	"#107",
	"#108",
	"#109",
	"#110",
	"#111",
	"#112",
	"#113",
	"#114",
	"#115",
	"#116",
	"#117",
	"#118",
	"#119",
	"#120",
	"#121",
	"#122",
	"#123",
	"#124",
	"#125",
	"#126",
	"#127",
	"#128",
	"#129",
	"#130",
	"#131",
	"SCTP",
	"#133",
	"#134",
	"#135",
	"UDPLite",
	"#137",
	"#138",
	"#139",
	"#140",
	"#141",
	"#142",
	"#143",
	"#144",
	"#145",
	"#146",
	"#147",
	"#148",
	"#149",
	"#150",
	"#151",
	"#152",
	"#153",
	"#154",
	"#155",
	"#156",
	"#157",
	"#158",
	"#159",
	"#160",
	"#161",
	"#162",
	"#163",
	"#164",
	"#165",
	"#166",
	"#167",
	"#168",
	"#169",
	"#170",
	"#171",
	"#172",
	"#173",
	"#174",
	"#175",
	"#176",
	"#177",
	"#178",
	"#179",
	"#180",
	"#181",
	"#182",
	"#183",
	"#184",
	"#185",
	"#186",
	"#187",
	"#188",
	"#189",
	"#180",
	"#191",
	"#192",
	"#193",
	"#194",
	"#195",
	"#196",
	"#197",
	"#198",
	"#199",
	"#200",
	"#201",
	"#202",
	"#203",
	"#204",
	"#205",
	"#206",
	"#207",
	"#208",
	"#209",
	"#210",
	"#211",
	"#212",
	"#213",
	"#214",
	"#215",
	"#216",
	"#217",
	"#218",
	"#219",
	"#220",
	"#221",
	"#222",
	"#223",
	"#224",
	"#225",
	"#226",
	"#227",
	"#228",
	"#229",
	"#230",
	"#231",
	"#232",
	"#233",
	"#234",
	"#235",
	"#236",
	"#237",
	"#238",
	"#239",
	"#240",
	"#241",
	"#242",
	"#243",
	"#244",
	"#245",
	"#246",
	"#247",
	"#248",
	"#249",
	"#250",
	"#251",
	"#252",
	"#253",
	"#254",
	"#255",
};

static const char *srcrule_str[] = {
	"first candidate",
	"same address",
	"appropriate scope",
	"deprecated address",
	"home address",
	"outgoing interface",
	"matching label",
	"public/temporary address",
	"alive interface",
	"better virtual status",
	"preferred source",
	"rule #11",
	"rule #12",
	"rule #13",
	"longest match",
	"rule #15",
};

/*
 * Dump IP6 statistics structure.
 */
void
ip6_stats(u_long off, const char *name, int af1 __unused, int proto __unused)
{
	struct ip6stat ip6stat;
	int first, i;

	if (fetch_stats("net.inet6.ip6.stats", off, &ip6stat,
	    sizeof(ip6stat), kread_counters) != 0)
		return;

	xo_open_container(name);
	xo_emit("{T:/%s}:\n", name);

#define	p(f, m) if (ip6stat.f || sflag <= 1) \
	xo_emit(m, (uintmax_t)ip6stat.f, plural(ip6stat.f))
#define	p1a(f, m) if (ip6stat.f || sflag <= 1) \
	xo_emit(m, (uintmax_t)ip6stat.f)

	p(ip6s_total, "\t{:received-packets/%ju} "
	    "{N:/total packet%s received}\n");
	p1a(ip6s_toosmall, "\t{:dropped-below-minimum-size/%ju} "
	    "{N:/with size smaller than minimum}\n");
	p1a(ip6s_tooshort, "\t{:dropped-short-packets/%ju} "
	    "{N:/with data size < data length}\n");
	p1a(ip6s_badoptions, "\t{:dropped-bad-options/%ju} "
	    "{N:/with bad options}\n");
	p1a(ip6s_badvers, "\t{:dropped-bad-version/%ju} "
	    "{N:/with incorrect version number}\n");
	p(ip6s_fragments, "\t{:received-fragments/%ju} "
	    "{N:/fragment%s received}\n");
	p(ip6s_fragdropped, "\t{:dropped-fragment/%ju} "
	    "{N:/fragment%s dropped (dup or out of space)}\n");
	p(ip6s_fragtimeout, "\t{:dropped-fragment-after-timeout/%ju} "
	    "{N:/fragment%s dropped after timeout}\n");
	p(ip6s_fragoverflow, "\t{:dropped-fragments-overflow/%ju} "
	    "{N:/fragment%s that exceeded limit}\n");
	p(ip6s_reassembled, "\t{:reassembled-packets/%ju} "
	    "{N:/packet%s reassembled ok}\n");
	p(ip6s_delivered, "\t{:received-local-packets/%ju} "
	    "{N:/packet%s for this host}\n");
	p(ip6s_forward, "\t{:forwarded-packets/%ju} "
	    "{N:/packet%s forwarded}\n");
	p(ip6s_cantforward, "\t{:packets-not-forwardable/%ju} "
	    "{N:/packet%s not forwardable}\n");
	p(ip6s_redirectsent, "\t{:sent-redirects/%ju} "
	    "{N:/redirect%s sent}\n");
	p(ip6s_localout, "\t{:sent-packets/%ju} "
	    "{N:/packet%s sent from this host}\n");
	p(ip6s_rawout, "\t{:send-packets-fabricated-header/%ju} "
	    "{N:/packet%s sent with fabricated ip header}\n");
	p(ip6s_odropped, "\t{:discard-no-mbufs/%ju} "
	    "{N:/output packet%s dropped due to no bufs, etc.}\n");
	p(ip6s_noroute, "\t{:discard-no-route/%ju} "
	    "{N:/output packet%s discarded due to no route}\n");
	p(ip6s_fragmented, "\t{:sent-fragments/%ju} "
	    "{N:/output datagram%s fragmented}\n");
	p(ip6s_ofragments, "\t{:fragments-created/%ju} "
	    "{N:/fragment%s created}\n");
	p(ip6s_cantfrag, "\t{:discard-cannot-fragment/%ju} "
	    "{N:/datagram%s that can't be fragmented}\n");
	p(ip6s_badscope, "\t{:discard-scope-violations/%ju} "
	    "{N:/packet%s that violated scope rules}\n");
	p(ip6s_notmember, "\t{:multicast-no-join-packets/%ju} "
	    "{N:/multicast packet%s which we don't join}\n");
	for (first = 1, i = 0; i < IP6S_HDRCNT; i++)
		if (ip6stat.ip6s_nxthist[i] != 0) {
			if (first) {
				xo_emit("\t{T:Input histogram}:\n");
				xo_open_list("input-histogram");
				first = 0;
			}
			xo_open_instance("input-histogram");
			xo_emit("\t\t{k:name/%s}: {:count/%ju}\n", ip6nh[i],
			    (uintmax_t)ip6stat.ip6s_nxthist[i]);
			xo_close_instance("input-histogram");
		}
	if (!first)
		xo_close_list("input-histogram");

	xo_open_container("mbuf-statistics");
	xo_emit("\t{T:Mbuf statistics}:\n");
	xo_emit("\t\t{:one-mbuf/%ju} {N:/one mbuf}\n",
	    (uintmax_t)ip6stat.ip6s_m1);
	for (first = 1, i = 0; i < IP6S_M2MMAX; i++) {
		char ifbuf[IFNAMSIZ];
		if (ip6stat.ip6s_m2m[i] != 0) {
			if (first) {
				xo_emit("\t\t{N:two or more mbuf}:\n");
				xo_open_list("mbuf-data");
				first = 0;
			}
			xo_open_instance("mbuf-data");
			xo_emit("\t\t\t{k:name/%s}= {:count/%ju}\n",
			    if_indextoname(i, ifbuf),
			    (uintmax_t)ip6stat.ip6s_m2m[i]);
			xo_close_instance("mbuf-data");
		}
	}
	if (!first)
		xo_close_list("mbuf-data");
	xo_emit("\t\t{:one-extra-mbuf/%ju} {N:one ext mbuf}\n",
	    (uintmax_t)ip6stat.ip6s_mext1);
	xo_emit("\t\t{:two-or-more-extra-mbufs/%ju} "
	    "{N:/two or more ext mbuf}\n", (uintmax_t)ip6stat.ip6s_mext2m);
	xo_close_container("mbuf-statistics");

	p(ip6s_exthdrtoolong, "\t{:dropped-header-too-long/%ju} "
	    "{N:/packet%s whose headers are not contiguous}\n");
	p(ip6s_nogif, "\t{:discard-tunnel-no-gif/%ju} "
	    "{N:/tunneling packet%s that can't find gif}\n");
	p(ip6s_toomanyhdr, "\t{:dropped-too-many-headers/%ju} "
	    "{N:/packet%s discarded because of too many headers}\n");

	/* for debugging source address selection */
#define	PRINT_SCOPESTAT(s,i) do {\
		switch(i) { /* XXX hardcoding in each case */\
		case 1:\
			p(s, "\t\t{ke:name/interface-locals}{:count/%ju} " \
			  "{N:/interface-local%s}\n");	\
			break;\
		case 2:\
			p(s,"\t\t{ke:name/link-locals}{:count/%ju} " \
			"{N:/link-local%s}\n"); \
			break;\
		case 5:\
			p(s,"\t\t{ke:name/site-locals}{:count/%ju} " \
			  "{N:/site-local%s}\n");\
			break;\
		case 14:\
			p(s,"\t\t{ke:name/globals}{:count/%ju} " \
			  "{N:/global%s}\n");\
			break;\
		default:\
			xo_emit("\t\t{qke:name/%#x}{:count/%ju} " \
				"{N:/addresses scope=%#x}\n",\
				i, (uintmax_t)ip6stat.s, i);	   \
		}\
	} while (0);

	xo_open_container("source-address-selection");
	p(ip6s_sources_none, "\t{:address-selection-failures/%ju} "
	    "{N:/failure%s of source address selection}\n");

	for (first = 1, i = 0; i < IP6S_SCOPECNT; i++) {
		if (ip6stat.ip6s_sources_sameif[i]) {
			if (first) {
				xo_open_list("outgoing-interface");
				xo_emit("\tsource addresses on an outgoing "
				    "I/F\n");
				first = 0;
			}
			xo_open_instance("outgoing-interface");
			PRINT_SCOPESTAT(ip6s_sources_sameif[i], i);
			xo_close_instance("outgoing-interface");
		}
	}
	if (!first)
		xo_close_list("outgoing-interface");

	for (first = 1, i = 0; i < IP6S_SCOPECNT; i++) {
		if (ip6stat.ip6s_sources_otherif[i]) {
			if (first) {
				xo_open_list("non-outgoing-interface");
				xo_emit("\tsource addresses on a non-outgoing "
				    "I/F\n");
				first = 0;
			}
			xo_open_instance("non-outgoing-interface");
			PRINT_SCOPESTAT(ip6s_sources_otherif[i], i);
			xo_close_instance("non-outgoing-interface");
		}
	}
	if (!first)
		xo_close_list("non-outgoing-interface");

	for (first = 1, i = 0; i < IP6S_SCOPECNT; i++) {
		if (ip6stat.ip6s_sources_samescope[i]) {
			if (first) {
				xo_open_list("same-source");
				xo_emit("\tsource addresses of same scope\n");
				first = 0;
			}
			xo_open_instance("same-source");
			PRINT_SCOPESTAT(ip6s_sources_samescope[i], i);
			xo_close_instance("same-source");
		}
	}
	if (!first)
		xo_close_list("same-source");

	for (first = 1, i = 0; i < IP6S_SCOPECNT; i++) {
		if (ip6stat.ip6s_sources_otherscope[i]) {
			if (first) {
				xo_open_list("different-scope");
				xo_emit("\tsource addresses of a different "
				    "scope\n");
				first = 0;
			}
			xo_open_instance("different-scope");
			PRINT_SCOPESTAT(ip6s_sources_otherscope[i], i);
			xo_close_instance("different-scope");
		}
	}
	if (!first)
		xo_close_list("different-scope");

	for (first = 1, i = 0; i < IP6S_SCOPECNT; i++) {
		if (ip6stat.ip6s_sources_deprecated[i]) {
			if (first) {
				xo_open_list("deprecated-source");
				xo_emit("\tdeprecated source addresses\n");
				first = 0;
			}
			xo_open_instance("deprecated-source");
			PRINT_SCOPESTAT(ip6s_sources_deprecated[i], i);
			xo_close_instance("deprecated-source");
		}
	}
	if (!first)
		xo_close_list("deprecated-source");

	for (first = 1, i = 0; i < IP6S_RULESMAX; i++) {
		if (ip6stat.ip6s_sources_rule[i]) {
			if (first) {
				xo_open_list("rules-applied");
				xo_emit("\t{T:Source addresses selection "
				    "rule applied}:\n");
				first = 0;
			}
			xo_open_instance("rules-applied");
			xo_emit("\t\t{ke:name/%s}{:count/%ju} {d:name/%s}\n",
			    srcrule_str[i],
			    (uintmax_t)ip6stat.ip6s_sources_rule[i],
			    srcrule_str[i]);
			xo_close_instance("rules-applied");
		}
	}
	if (!first)
		xo_close_list("rules-applied");

	xo_close_container("source-address-selection");

#undef p
#undef p1a
	xo_close_container(name);
}

/*
 * Dump IPv6 per-interface statistics based on RFC 2465.
 */
void
ip6_ifstats(char *ifname)
{
	struct in6_ifreq ifr;
	int s;

#define	p(f, m) if (ifr.ifr_ifru.ifru_stat.f || sflag <= 1)	\
	xo_emit(m, (uintmax_t)ifr.ifr_ifru.ifru_stat.f,		\
	    plural(ifr.ifr_ifru.ifru_stat.f))

	if ((s = socket(AF_INET6, SOCK_DGRAM, 0)) < 0) {
		xo_warn("Warning: socket(AF_INET6)");
		return;
	}

	strlcpy(ifr.ifr_name, ifname, sizeof(ifr.ifr_name));
	if (ioctl(s, SIOCGIFSTAT_IN6, (char *)&ifr) < 0) {
		if (errno != EPFNOSUPPORT)
			xo_warn("Warning: ioctl(SIOCGIFSTAT_IN6)");
		goto end;
	}

	xo_emit("{T:/ip6 on %s}:\n", ifr.ifr_name);

	xo_open_instance("ip6-interface-statistics");
	xo_emit("{ke:name/%s}", ifr.ifr_name);

	p(ifs6_in_receive, "\t{:received-packets/%ju} "
	    "{N:/total input datagram%s}\n");
	p(ifs6_in_hdrerr, "\t{:dropped-invalid-header/%ju} "
	    "{N:/datagram%s with invalid header received}\n");
	p(ifs6_in_toobig, "\t{:dropped-mtu-exceeded/%ju} "
	    "{N:/datagram%s exceeded MTU received}\n");
	p(ifs6_in_noroute, "\t{:dropped-no-route/%ju} "
	    "{N:/datagram%s with no route received}\n");
	p(ifs6_in_addrerr, "\t{:dropped-invalid-destination/%ju} "
	    "{N:/datagram%s with invalid dst received}\n");
	p(ifs6_in_protounknown, "\t{:dropped-unknown-protocol/%ju} "
	    "{N:/datagram%s with unknown proto received}\n");
	p(ifs6_in_truncated, "\t{:dropped-truncated/%ju} "
	    "{N:/truncated datagram%s received}\n");
	p(ifs6_in_discard, "\t{:dropped-discarded/%ju} "
	    "{N:/input datagram%s discarded}\n");
 	p(ifs6_in_deliver, "\t{:received-valid-packets/%ju} "
	    "{N:/datagram%s delivered to an upper layer protocol}\n");
	p(ifs6_out_forward, "\t{:sent-forwarded/%ju} "
	    "{N:/datagram%s forwarded to this interface}\n");
 	p(ifs6_out_request, "\t{:sent-packets/%ju} "
	    "{N:/datagram%s sent from an upper layer protocol}\n");
	p(ifs6_out_discard, "\t{:discard-packets/%ju} "
	    "{N:/total discarded output datagram%s}\n");
	p(ifs6_out_fragok, "\t{:discard-fragments/%ju} "
	    "{N:/output datagram%s fragmented}\n");
	p(ifs6_out_fragfail, "\t{:fragments-failed/%ju} "
	    "{N:/output datagram%s failed on fragment}\n");
	p(ifs6_out_fragcreat, "\t{:fragments-created/%ju} "
	    "{N:/output datagram%s succeeded on fragment}\n");
	p(ifs6_reass_reqd, "\t{:reassembly-required/%ju} "
	    "{N:/incoming datagram%s fragmented}\n");
	p(ifs6_reass_ok, "\t{:reassembled-packets/%ju} "
	    "{N:/datagram%s reassembled}\n");
	p(ifs6_reass_fail, "\t{:reassembly-failed/%ju} "
	    "{N:/datagram%s failed on reassembly}\n");
	p(ifs6_in_mcast, "\t{:received-multicast/%ju} "
	    "{N:/multicast datagram%s received}\n");
	p(ifs6_out_mcast, "\t{:sent-multicast/%ju} "
	    "{N:/multicast datagram%s sent}\n");

 end:
	xo_close_instance("ip6-interface-statistics");
 	close(s);

#undef p
}

static	const char *icmp6names[] = {
	"#0",
	"unreach",
	"packet too big",
	"time exceed",
	"parameter problem",
	"#5",
	"#6",
	"#7",
	"#8",
	"#9",
	"#10",
	"#11",
	"#12",
	"#13",
	"#14",
	"#15",
	"#16",
	"#17",
	"#18",
	"#19",
	"#20",
	"#21",
	"#22",
	"#23",
	"#24",
	"#25",
	"#26",
	"#27",
	"#28",
	"#29",
	"#30",
	"#31",
	"#32",
	"#33",
	"#34",
	"#35",
	"#36",
	"#37",
	"#38",
	"#39",
	"#40",
	"#41",
	"#42",
	"#43",
	"#44",
	"#45",
	"#46",
	"#47",
	"#48",
	"#49",
	"#50",
	"#51",
	"#52",
	"#53",
	"#54",
	"#55",
	"#56",
	"#57",
	"#58",
	"#59",
	"#60",
	"#61",
	"#62",
	"#63",
	"#64",
	"#65",
	"#66",
	"#67",
	"#68",
	"#69",
	"#70",
	"#71",
	"#72",
	"#73",
	"#74",
	"#75",
	"#76",
	"#77",
	"#78",
	"#79",
	"#80",
	"#81",
	"#82",
	"#83",
	"#84",
	"#85",
	"#86",
	"#87",
	"#88",
	"#89",
	"#80",
	"#91",
	"#92",
	"#93",
	"#94",
	"#95",
	"#96",
	"#97",
	"#98",
	"#99",
	"#100",
	"#101",
	"#102",
	"#103",
	"#104",
	"#105",
	"#106",
	"#107",
	"#108",
	"#109",
	"#110",
	"#111",
	"#112",
	"#113",
	"#114",
	"#115",
	"#116",
	"#117",
	"#118",
	"#119",
	"#120",
	"#121",
	"#122",
	"#123",
	"#124",
	"#125",
	"#126",
	"#127",
	"echo",
	"echo reply",
	"multicast listener query",
	"MLDv1 listener report",
	"MLDv1 listener done",
	"router solicitation",
	"router advertisement",
	"neighbor solicitation",
	"neighbor advertisement",
	"redirect",
	"router renumbering",
	"node information request",
	"node information reply",
	"inverse neighbor solicitation",
	"inverse neighbor advertisement",
	"MLDv2 listener report",
	"#144",
	"#145",
	"#146",
	"#147",
	"#148",
	"#149",
	"#150",
	"#151",
	"#152",
	"#153",
	"#154",
	"#155",
	"#156",
	"#157",
	"#158",
	"#159",
	"#160",
	"#161",
	"#162",
	"#163",
	"#164",
	"#165",
	"#166",
	"#167",
	"#168",
	"#169",
	"#170",
	"#171",
	"#172",
	"#173",
	"#174",
	"#175",
	"#176",
	"#177",
	"#178",
	"#179",
	"#180",
	"#181",
	"#182",
	"#183",
	"#184",
	"#185",
	"#186",
	"#187",
	"#188",
	"#189",
	"#180",
	"#191",
	"#192",
	"#193",
	"#194",
	"#195",
	"#196",
	"#197",
	"#198",
	"#199",
	"#200",
	"#201",
	"#202",
	"#203",
	"#204",
	"#205",
	"#206",
	"#207",
	"#208",
	"#209",
	"#210",
	"#211",
	"#212",
	"#213",
	"#214",
	"#215",
	"#216",
	"#217",
	"#218",
	"#219",
	"#220",
	"#221",
	"#222",
	"#223",
	"#224",
	"#225",
	"#226",
	"#227",
	"#228",
	"#229",
	"#230",
	"#231",
	"#232",
	"#233",
	"#234",
	"#235",
	"#236",
	"#237",
	"#238",
	"#239",
	"#240",
	"#241",
	"#242",
	"#243",
	"#244",
	"#245",
	"#246",
	"#247",
	"#248",
	"#249",
	"#250",
	"#251",
	"#252",
	"#253",
	"#254",
	"#255",
};

/*
 * Dump ICMP6 statistics.
 */
void
icmp6_stats(u_long off, const char *name, int af1 __unused, int proto __unused)
{
	struct icmp6stat icmp6stat;
	int i, first;

	if (fetch_stats("net.inet6.icmp6.stats", off, &icmp6stat,
	    sizeof(icmp6stat), kread_counters) != 0)
		return;

	xo_emit("{T:/%s}:\n", name);
	xo_open_container(name);

#define	p(f, m) if (icmp6stat.f || sflag <= 1) \
	xo_emit(m, (uintmax_t)icmp6stat.f, plural(icmp6stat.f))
#define	p_5(f, m) if (icmp6stat.f || sflag <= 1) \
	xo_emit(m, (uintmax_t)icmp6stat.f)

	p(icp6s_error, "\t{:icmp6-calls/%ju} "
	    "{N:/call%s to icmp6_error}\n");
	p(icp6s_canterror, "\t{:errors-not-generated-from-message/%ju} "
	    "{N:/error%s not generated in response to an icmp6 message}\n");
	p(icp6s_toofreq, "\t{:errors-discarded-by-rate-limitation/%ju} "
	    "{N:/error%s not generated because of rate limitation}\n");
#define	NELEM (int)(sizeof(icmp6stat.icp6s_outhist)/sizeof(icmp6stat.icp6s_outhist[0]))
	for (first = 1, i = 0; i < NELEM; i++)
		if (icmp6stat.icp6s_outhist[i] != 0) {
			if (first) {
				xo_open_list("output-histogram");
				xo_emit("\t{T:Output histogram}:\n");
				first = 0;
			}
			xo_open_instance("output-histogram");
			xo_emit("\t\t{k:name/%s}: {:count/%ju}\n",
			    icmp6names[i],
			    (uintmax_t)icmp6stat.icp6s_outhist[i]);
			xo_close_instance("output-histogram");
		}
	if (!first)
		xo_close_list("output-histogram");
#undef NELEM

	p(icp6s_badcode, "\t{:dropped-bad-code/%ju} "
	    "{N:/message%s with bad code fields}\n");
	p(icp6s_tooshort, "\t{:dropped-too-short/%ju} "
	    "{N:/message%s < minimum length}\n");
	p(icp6s_checksum, "\t{:dropped-bad-checksum/%ju} "
	    "{N:/bad checksum%s}\n");
	p(icp6s_badlen, "\t{:dropped-bad-length/%ju} "
	    "{N:/message%s with bad length}\n");
#define	NELEM (int)(sizeof(icmp6stat.icp6s_inhist)/sizeof(icmp6stat.icp6s_inhist[0]))
	for (first = 1, i = 0; i < NELEM; i++)
		if (icmp6stat.icp6s_inhist[i] != 0) {
			if (first) {
				xo_open_list("input-histogram");
				xo_emit("\t{T:Input histogram}:\n");
				first = 0;
			}
			xo_open_instance("input-histogram");
			xo_emit("\t\t{k:name/%s}: {:count/%ju}\n",
			    icmp6names[i],
			    (uintmax_t)icmp6stat.icp6s_inhist[i]);
			xo_close_instance("input-histogram");
		}
	if (!first)
		xo_close_list("input-histogram");
#undef NELEM
	xo_emit("\t{T:Histogram of error messages to be generated}:\n");
	xo_open_container("errors");
	p_5(icp6s_odst_unreach_noroute, "\t\t{:no-route/%ju} "
	    "{N:/no route}\n");
	p_5(icp6s_odst_unreach_admin, "\t\t{:admin-prohibited/%ju} "
	    "{N:/administratively prohibited}\n");
	p_5(icp6s_odst_unreach_beyondscope, "\t\t{:beyond-scope/%ju} "
	    "{N:/beyond scope}\n");
	p_5(icp6s_odst_unreach_addr, "\t\t{:address-unreachable/%ju} "
	    "{N:/address unreachable}\n");
	p_5(icp6s_odst_unreach_noport, "\t\t{:port-unreachable/%ju} "
	    "{N:/port unreachable}\n");
	p_5(icp6s_opacket_too_big, "\t\t{:packet-too-big/%ju} "
	    "{N:/packet too big}\n");
	p_5(icp6s_otime_exceed_transit, "\t\t{:time-exceed-transmit/%ju} "
	    "{N:/time exceed transit}\n");
	p_5(icp6s_otime_exceed_reassembly, "\t\t{:time-exceed-reassembly/%ju} "
	    "{N:/time exceed reassembly}\n");
	p_5(icp6s_oparamprob_header, "\t\t{:bad-header/%ju} "
	    "{N:/erroneous header field}\n");
	p_5(icp6s_oparamprob_nextheader, "\t\t{:bad-next-header/%ju} "
	    "{N:/unrecognized next header}\n");
	p_5(icp6s_oparamprob_option, "\t\t{:bad-option/%ju} "
	    "{N:/unrecognized option}\n");
	p_5(icp6s_oredirect, "\t\t{:redirects/%ju} "
	    "{N:/redirect}\n");
	p_5(icp6s_ounknown, "\t\t{:unknown/%ju} {N:unknown}\n");

	p(icp6s_reflect, "\t{:reflect/%ju} "
	    "{N:/message response%s generated}\n");
	p(icp6s_nd_toomanyopt, "\t{:too-many-nd-options/%ju} "
	    "{N:/message%s with too many ND options}\n");
	p(icp6s_nd_badopt, "\t{:bad-nd-options/%ju} "
	    "{N:/message%s with bad ND options}\n");
	p(icp6s_badns, "\t{:bad-neighbor-solicitation/%ju} "
	    "{N:/bad neighbor solicitation message%s}\n");
	p(icp6s_badna, "\t{:bad-neighbor-advertisement/%ju} "
	    "{N:/bad neighbor advertisement message%s}\n");
	p(icp6s_badrs, "\t{:bad-router-solicitation/%ju} "
	    "{N:/bad router solicitation message%s}\n");
	p(icp6s_badra, "\t{:bad-router-advertisement/%ju} "
	    "{N:/bad router advertisement message%s}\n");
	p(icp6s_badredirect, "\t{:bad-redirect/%ju} "
	    "{N:/bad redirect message%s}\n");
	xo_close_container("errors");
	p(icp6s_pmtuchg, "\t{:path-mtu-changes/%ju} {N:/path MTU change%s}\n");
#undef p
#undef p_5
	xo_close_container(name);
}

/*
 * Dump ICMPv6 per-interface statistics based on RFC 2466.
 */
void
icmp6_ifstats(char *ifname)
{
	struct in6_ifreq ifr;
	int s;

#define	p(f, m) if (ifr.ifr_ifru.ifru_icmp6stat.f || sflag <= 1)	\
	xo_emit(m, (uintmax_t)ifr.ifr_ifru.ifru_icmp6stat.f,		\
	    plural(ifr.ifr_ifru.ifru_icmp6stat.f))
#define	p2(f, m) if (ifr.ifr_ifru.ifru_icmp6stat.f || sflag <= 1)	\
	xo_emit(m, (uintmax_t)ifr.ifr_ifru.ifru_icmp6stat.f,		\
	    pluralies(ifr.ifr_ifru.ifru_icmp6stat.f))

	if ((s = socket(AF_INET6, SOCK_DGRAM, 0)) < 0) {
		xo_warn("Warning: socket(AF_INET6)");
		return;
	}

	strlcpy(ifr.ifr_name, ifname, sizeof(ifr.ifr_name));
	if (ioctl(s, SIOCGIFSTAT_ICMP6, (char *)&ifr) < 0) {
		if (errno != EPFNOSUPPORT)
			xo_warn("Warning: ioctl(SIOCGIFSTAT_ICMP6)");
		goto end;
	}

	xo_emit("{T:/icmp6 on %s}:\n", ifr.ifr_name);

	xo_open_instance("icmp6-interface-statistics");
	xo_emit("{ke:name/%s}", ifr.ifr_name);
	p(ifs6_in_msg, "\t{:received-packets/%ju} "
	    "{N:/total input message%s}\n");
	p(ifs6_in_error, "\t{:received-errors/%ju} "
	    "{N:/total input error message%s}\n");
	p(ifs6_in_dstunreach, "\t{:received-destination-unreachable/%ju} "
	    "{N:/input destination unreachable error%s}\n");
	p(ifs6_in_adminprohib, "\t{:received-admin-prohibited/%ju} "
	    "{N:/input administratively prohibited error%s}\n");
	p(ifs6_in_timeexceed, "\t{:received-time-exceeded/%ju} "
	    "{N:/input time exceeded error%s}\n");
	p(ifs6_in_paramprob, "\t{:received-bad-parameter/%ju} "
	    "{N:/input parameter problem error%s}\n");
	p(ifs6_in_pkttoobig, "\t{:received-packet-too-big/%ju} "
	    "{N:/input packet too big error%s}\n");
	p(ifs6_in_echo, "\t{:received-echo-requests/%ju} "
	    "{N:/input echo request%s}\n");
	p2(ifs6_in_echoreply, "\t{:received-echo-replies/%ju} "
	    "{N:/input echo repl%s}\n");
	p(ifs6_in_routersolicit, "\t{:received-router-solicitation/%ju} "
	    "{N:/input router solicitation%s}\n");
	p(ifs6_in_routeradvert, "\t{:received-router-advertisement/%ju} "
	    "{N:/input router advertisement%s}\n");
	p(ifs6_in_neighborsolicit, "\t{:received-neighbor-solicitation/%ju} "
	    "{N:/input neighbor solicitation%s}\n");
	p(ifs6_in_neighboradvert, "\t{:received-neighbor-advertisement/%ju} "
	    "{N:/input neighbor advertisement%s}\n");
	p(ifs6_in_redirect, "\t{received-redirects/%ju} "
	    "{N:/input redirect%s}\n");
	p2(ifs6_in_mldquery, "\t{:received-mld-queries/%ju} "
	    "{N:/input MLD quer%s}\n");
	p(ifs6_in_mldreport, "\t{:received-mld-reports/%ju} "
	    "{N:/input MLD report%s}\n");
	p(ifs6_in_mlddone, "\t{:received-mld-done/%ju} "
	    "{N:/input MLD done%s}\n");

	p(ifs6_out_msg, "\t{:sent-packets/%ju} "
	    "{N:/total output message%s}\n");
	p(ifs6_out_error, "\t{:sent-errors/%ju} "
	    "{N:/total output error message%s}\n");
	p(ifs6_out_dstunreach, "\t{:sent-destination-unreachable/%ju} "
	    "{N:/output destination unreachable error%s}\n");
	p(ifs6_out_adminprohib, "\t{:sent-admin-prohibited/%ju} "
	    "{N:/output administratively prohibited error%s}\n");
	p(ifs6_out_timeexceed, "\t{:sent-time-exceeded/%ju} "
	    "{N:/output time exceeded error%s}\n");
	p(ifs6_out_paramprob, "\t{:sent-bad-parameter/%ju} "
	    "{N:/output parameter problem error%s}\n");
	p(ifs6_out_pkttoobig, "\t{:sent-packet-too-big/%ju} "
	    "{N:/output packet too big error%s}\n");
	p(ifs6_out_echo, "\t{:sent-echo-requests/%ju} "
	    "{N:/output echo request%s}\n");
	p2(ifs6_out_echoreply, "\t{:sent-echo-replies/%ju} "
	    "{N:/output echo repl%s}\n");
	p(ifs6_out_routersolicit, "\t{:sent-router-solicitation/%ju} "
	    "{N:/output router solicitation%s}\n");
	p(ifs6_out_routeradvert, "\t{:sent-router-advertisement/%ju} "
	    "{N:/output router advertisement%s}\n");
	p(ifs6_out_neighborsolicit, "\t{:sent-neighbor-solicitation/%ju} "
	    "{N:/output neighbor solicitation%s}\n");
	p(ifs6_out_neighboradvert, "\t{:sent-neighbor-advertisement/%ju} "
	    "{N:/output neighbor advertisement%s}\n");
	p(ifs6_out_redirect, "\t{:sent-redirects/%ju} "
	    "{N:/output redirect%s}\n");
	p2(ifs6_out_mldquery, "\t{:sent-mld-queries/%ju} "
	    "{N:/output MLD quer%s}\n");
	p(ifs6_out_mldreport, "\t{:sent-mld-reports/%ju} "
	    "{N:/output MLD report%s}\n");
	p(ifs6_out_mlddone, "\t{:sent-mld-dones/%ju} "
	    "{N:/output MLD done%s}\n");

end:
	xo_close_instance("icmp6-interface-statistics");
	close(s);
#undef p
}

/*
 * Dump PIM statistics structure.
 */
void
pim6_stats(u_long off, const char *name, int af1 __unused, int proto __unused)
{
	struct pim6stat pim6stat;

	if (fetch_stats("net.inet6.pim.stats", off, &pim6stat,
	    sizeof(pim6stat), kread) != 0)
		return;

	xo_emit("{T:/%s}:\n", name);
	xo_open_container(name);

#define	p(f, m) if (pim6stat.f || sflag <= 1) \
	xo_emit(m, (uintmax_t)pim6stat.f, plural(pim6stat.f))

	p(pim6s_rcv_total, "\t{:received-packets/%ju} "
	    "{N:/message%s received}\n");
	p(pim6s_rcv_tooshort, "\t{:dropped-too-short/%ju} "
	    "{N:/message%s received with too few bytes}\n");
	p(pim6s_rcv_badsum, "\t{:dropped-bad-checksum/%ju} "
	    "{N:/message%s received with bad checksum}\n");
	p(pim6s_rcv_badversion, "\t{:dropped-bad-version/%ju} "
	    "{N:/message%s received with bad version}\n");
	p(pim6s_rcv_registers, "\t{:received-registers/%ju} "
	    "{N:/register%s received}\n");
	p(pim6s_rcv_badregisters, "\t{:received-bad-registers/%ju} "
	    "{N:/bad register%s received}\n");
	p(pim6s_snd_registers, "\t{:sent-registers/%ju} "
	    "{N:/register%s sent}\n");
#undef p
	xo_close_container(name);
}

/*
 * Dump raw ip6 statistics structure.
 */
void
rip6_stats(u_long off, const char *name, int af1 __unused, int proto __unused)
{
	struct rip6stat rip6stat;
	u_quad_t delivered;

	if (fetch_stats("net.inet6.ip6.rip6stats", off, &rip6stat,
	    sizeof(rip6stat), kread_counters) != 0)
		return;

	xo_emit("{T:/%s}:\n", name);
	xo_open_container(name);

#define	p(f, m) if (rip6stat.f || sflag <= 1) \
	xo_emit(m, (uintmax_t)rip6stat.f, plural(rip6stat.f))

	p(rip6s_ipackets, "\t{:received-packets/%ju} "
	    "{N:/message%s received}\n");
	p(rip6s_isum, "\t{:input-checksum-computation/%ju} "
	    "{N:/checksum calculation%s on inbound}\n");
	p(rip6s_badsum, "\t{:received-bad-checksum/%ju} "
	    "{N:/message%s with bad checksum}\n");
	p(rip6s_nosock, "\t{:dropped-no-socket/%ju} "
	    "{N:/message%s dropped due to no socket}\n");
	p(rip6s_nosockmcast, "\t{:dropped-multicast-no-socket/%ju} "
	    "{N:/multicast message%s dropped due to no socket}\n");
	p(rip6s_fullsock, "\t{:dropped-full-socket-buffer/%ju} "
	    "{N:/message%s dropped due to full socket buffers}\n");
	delivered = rip6stat.rip6s_ipackets -
		    rip6stat.rip6s_badsum -
		    rip6stat.rip6s_nosock -
		    rip6stat.rip6s_nosockmcast -
		    rip6stat.rip6s_fullsock;
	if (delivered || sflag <= 1)
		xo_emit("\t{:delivered-packets/%ju} {N:/delivered}\n",
		    (uintmax_t)delivered);
	p(rip6s_opackets, "\t{:sent-packets/%ju} "
	    "{N:/datagram%s output}\n");
#undef p
	xo_close_container(name);
}

/*
 * Pretty print an Internet address (net address + port).
 * Take numeric_addr and numeric_port into consideration.
 */
#define	GETSERVBYPORT6(port, proto, ret)\
{\
	if (strcmp((proto), "tcp6") == 0)\
		(ret) = getservbyport((int)(port), "tcp");\
	else if (strcmp((proto), "udp6") == 0)\
		(ret) = getservbyport((int)(port), "udp");\
	else\
		(ret) = getservbyport((int)(port), (proto));\
};

void
inet6print(const char *container, struct in6_addr *in6, int port,
    const char *proto, int numeric)
{
	struct servent *sp = 0;
	char line[80], *cp;
	int width;
	size_t alen, plen;

	if (container)
		xo_open_container(container);

	snprintf(line, sizeof(line), "%.*s.",
	    Wflag ? 39 : (Aflag && !numeric) ? 12 : 16,
	    inet6name(in6));
	alen = strlen(line);
	cp = line + alen;
	if (!numeric && port)
		GETSERVBYPORT6(port, proto, sp);
	if (sp || port == 0)
		snprintf(cp, sizeof(line) - alen,
		    "%.15s", sp ? sp->s_name : "*");
	else
		snprintf(cp, sizeof(line) - alen,
		    "%d", ntohs((u_short)port));
	width = Wflag ? 45 : Aflag ? 18 : 22;

	xo_emit("{d:target/%-*.*s} ", width, width, line);

	plen = strlen(cp);
	alen--;
	xo_emit("{e:address/%*.*s}{e:port/%*.*s}", alen, alen, line, plen,
	    plen, cp);

	if (container)
		xo_close_container(container);
}

/*
 * Construct an Internet address representation.
 * If the numeric_addr has been supplied, give
 * numeric value, otherwise try for symbolic name.
 */

char *
inet6name(struct in6_addr *in6p)
{
	struct sockaddr_in6 sin6;
	char hbuf[NI_MAXHOST], *cp;
	static char line[NI_MAXHOST];
	static char domain[MAXHOSTNAMELEN];
	static int first = 1;
	int flags, error;

	if (IN6_IS_ADDR_UNSPECIFIED(in6p)) {
		strcpy(line, "*");
		return (line);
	}
	if (first && !numeric_addr) {
		first = 0;
		if (gethostname(domain, sizeof(domain)) == 0 &&
		    (cp = strchr(domain, '.')))
			strlcpy(domain, cp + 1, sizeof(domain));
		else
			domain[0] = 0;
	}
	memset(&sin6, 0, sizeof(sin6));
	memcpy(&sin6.sin6_addr, in6p, sizeof(*in6p));
	sin6.sin6_family = AF_INET6;
	/* XXX: in6p.s6_addr[2] can contain scopeid. */
	in6_fillscopeid(&sin6);
	flags = (numeric_addr) ? NI_NUMERICHOST : 0;
	error = getnameinfo((struct sockaddr *)&sin6, sizeof(sin6), hbuf,
	    sizeof(hbuf), NULL, 0, flags);
	if (error == 0) {
		if ((flags & NI_NUMERICHOST) == 0 &&
		    (cp = strchr(hbuf, '.')) &&
		    !strcmp(cp + 1, domain))
			*cp = 0;
		strlcpy(line, hbuf, sizeof(line));
	} else {
		/* XXX: this should not happen. */
		snprintf(line, sizeof(line), "%s",
			inet_ntop(AF_INET6, (void *)&sin6.sin6_addr, ntop_buf,
				sizeof(ntop_buf)));
	}
	return (line);
}
#endif /*INET6*/
