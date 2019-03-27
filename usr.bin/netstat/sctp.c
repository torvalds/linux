/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2001-2007, by Weongyo Jeong. All rights reserved.
 * Copyright (c) 2011, by Michael Tuexen. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * a) Redistributions of source code must retain the above copyright notice,
 *   this list of conditions and the following disclaimer.
 *
 * b) Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *   the documentation and/or other materials provided with the distribution.
 *
 * c) Neither the name of Cisco Systems, Inc. nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#if 0
#ifndef lint
static char sccsid[] = "@(#)sctp.c	0.1 (Berkeley) 4/18/2007";
#endif /* not lint */
#endif

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/queue.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/sysctl.h>
#include <sys/protosw.h>

#include <netinet/in.h>
#include <netinet/sctp.h>
#include <netinet/sctp_constants.h>
#include <arpa/inet.h>

#include <err.h>
#include <errno.h>
#include <libutil.h>
#include <netdb.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include "netstat.h"
#include <libxo/xo.h>

#ifdef SCTP

static void sctp_statesprint(uint32_t state);

#define	NETSTAT_SCTP_STATES_CLOSED		0x0
#define	NETSTAT_SCTP_STATES_BOUND		0x1
#define	NETSTAT_SCTP_STATES_LISTEN		0x2
#define	NETSTAT_SCTP_STATES_COOKIE_WAIT		0x3
#define	NETSTAT_SCTP_STATES_COOKIE_ECHOED	0x4
#define	NETSTAT_SCTP_STATES_ESTABLISHED		0x5
#define	NETSTAT_SCTP_STATES_SHUTDOWN_SENT	0x6
#define	NETSTAT_SCTP_STATES_SHUTDOWN_RECEIVED	0x7
#define	NETSTAT_SCTP_STATES_SHUTDOWN_ACK_SENT	0x8
#define	NETSTAT_SCTP_STATES_SHUTDOWN_PENDING	0x9

static const char *sctpstates[] = {
	"CLOSED",
	"BOUND",
	"LISTEN",
	"COOKIE_WAIT",
	"COOKIE_ECHOED",
	"ESTABLISHED",
	"SHUTDOWN_SENT",
	"SHUTDOWN_RECEIVED",
	"SHUTDOWN_ACK_SENT",
	"SHUTDOWN_PENDING"
};

static LIST_HEAD(xladdr_list, xladdr_entry) xladdr_head;
struct xladdr_entry {
	struct xsctp_laddr *xladdr;
	LIST_ENTRY(xladdr_entry) xladdr_entries;
};

static LIST_HEAD(xraddr_list, xraddr_entry) xraddr_head;
struct xraddr_entry {
	struct xsctp_raddr *xraddr;
	LIST_ENTRY(xraddr_entry) xraddr_entries;
};

static void
sctp_print_address(const char *container, union sctp_sockstore *address,
    int port, int num_port)
{
	struct servent *sp = 0;
	char line[80], *cp;
	int width;
	size_t alen, plen;

	if (container)
		xo_open_container(container);

	switch (address->sa.sa_family) {
#ifdef INET
	case AF_INET:
		snprintf(line, sizeof(line), "%.*s.",
		    Wflag ? 39 : 16, inetname(&address->sin.sin_addr));
		break;
#endif
#ifdef INET6
	case AF_INET6:
		snprintf(line, sizeof(line), "%.*s.",
		    Wflag ? 39 : 16, inet6name(&address->sin6.sin6_addr));
		break;
#endif
	default:
		snprintf(line, sizeof(line), "%.*s.",
		    Wflag ? 39 : 16, "");
		break;
	}
	alen = strlen(line);
	cp = line + alen;
	if (!num_port && port)
		sp = getservbyport((int)port, "sctp");
	if (sp || port == 0)
		snprintf(cp, sizeof(line) - alen,
		    "%.15s ", sp ? sp->s_name : "*");
	else
		snprintf(cp, sizeof(line) - alen,
		    "%d ", ntohs((u_short)port));
	width = Wflag ? 45 : 22;
	xo_emit("{d:target/%-*.*s} ", width, width, line);

	plen = strlen(cp) - 1;
	alen--;
	xo_emit("{e:address/%*.*s}{e:port/%*.*s}", alen, alen, line, plen,
	    plen, cp);

	if (container)
		xo_close_container(container);
}

static int
sctp_skip_xinpcb_ifneed(char *buf, const size_t buflen, size_t *offset)
{
	int exist_tcb = 0;
	struct xsctp_tcb *xstcb;
	struct xsctp_raddr *xraddr;
	struct xsctp_laddr *xladdr;

	while (*offset < buflen) {
		xladdr = (struct xsctp_laddr *)(buf + *offset);
		*offset += sizeof(struct xsctp_laddr);
		if (xladdr->last == 1)
			break;
	}

	while (*offset < buflen) {
		xstcb = (struct xsctp_tcb *)(buf + *offset);
		*offset += sizeof(struct xsctp_tcb);
		if (xstcb->last == 1)
			break;

		exist_tcb = 1;

		while (*offset < buflen) {
			xladdr = (struct xsctp_laddr *)(buf + *offset);
			*offset += sizeof(struct xsctp_laddr);
			if (xladdr->last == 1)
				break;
		}

		while (*offset < buflen) {
			xraddr = (struct xsctp_raddr *)(buf + *offset);
			*offset += sizeof(struct xsctp_raddr);
			if (xraddr->last == 1)
				break;
		}
	}

	/*
	 * If Lflag is set, we don't care about the return value.
	 */
	if (Lflag)
		return 0;

	return exist_tcb;
}

static void
sctp_process_tcb(struct xsctp_tcb *xstcb,
    char *buf, const size_t buflen, size_t *offset, int *indent)
{
	int i, xl_total = 0, xr_total = 0, x_max;
	struct xsctp_raddr *xraddr;
	struct xsctp_laddr *xladdr;
	struct xladdr_entry *prev_xl = NULL, *xl = NULL, *xl_tmp;
	struct xraddr_entry *prev_xr = NULL, *xr = NULL, *xr_tmp;

	LIST_INIT(&xladdr_head);
	LIST_INIT(&xraddr_head);

	/*
	 * Make `struct xladdr_list' list and `struct xraddr_list' list
	 * to handle the address flexibly.
	 */
	while (*offset < buflen) {
		xladdr = (struct xsctp_laddr *)(buf + *offset);
		*offset += sizeof(struct xsctp_laddr);
		if (xladdr->last == 1)
			break;

		prev_xl = xl;
		xl = malloc(sizeof(struct xladdr_entry));
		if (xl == NULL) {
			xo_warnx("malloc %lu bytes",
			    (u_long)sizeof(struct xladdr_entry));
			goto out;
		}
		xl->xladdr = xladdr;
		if (prev_xl == NULL)
			LIST_INSERT_HEAD(&xladdr_head, xl, xladdr_entries);
		else
			LIST_INSERT_AFTER(prev_xl, xl, xladdr_entries);
		xl_total++;
	}

	while (*offset < buflen) {
		xraddr = (struct xsctp_raddr *)(buf + *offset);
		*offset += sizeof(struct xsctp_raddr);
		if (xraddr->last == 1)
			break;

		prev_xr = xr;
		xr = malloc(sizeof(struct xraddr_entry));
		if (xr == NULL) {
			xo_warnx("malloc %lu bytes",
			    (u_long)sizeof(struct xraddr_entry));
			goto out;
		}
		xr->xraddr = xraddr;
		if (prev_xr == NULL)
			LIST_INSERT_HEAD(&xraddr_head, xr, xraddr_entries);
		else
			LIST_INSERT_AFTER(prev_xr, xr, xraddr_entries);
		xr_total++;
	}

	/*
	 * Let's print the address infos.
	 */
	xo_open_list("address");
	xl = LIST_FIRST(&xladdr_head);
	xr = LIST_FIRST(&xraddr_head);
	x_max = MAX(xl_total, xr_total);
	for (i = 0; i < x_max; i++) {
		xo_open_instance("address");

		if (((*indent == 0) && i > 0) || *indent > 0)
			xo_emit("{P:/%-12s} ", " ");

		if (xl != NULL) {
			sctp_print_address("local", &(xl->xladdr->address),
			    htons(xstcb->local_port), numeric_port);
		} else {
			if (Wflag) {
				xo_emit("{P:/%-45s} ", " ");
			} else {
				xo_emit("{P:/%-22s} ", " ");
			}
		}

		if (xr != NULL && !Lflag) {
			sctp_print_address("remote", &(xr->xraddr->address),
			    htons(xstcb->remote_port), numeric_port);
		}

		if (xl != NULL)
			xl = LIST_NEXT(xl, xladdr_entries);
		if (xr != NULL)
			xr = LIST_NEXT(xr, xraddr_entries);

		if (i == 0 && !Lflag)
			sctp_statesprint(xstcb->state);

		if (i < x_max)
			xo_emit("\n");
		xo_close_instance("address");
	}

out:
	/*
	 * Free the list which be used to handle the address.
	 */
	xl = LIST_FIRST(&xladdr_head);
	while (xl != NULL) {
		xl_tmp = LIST_NEXT(xl, xladdr_entries);
		free(xl);
		xl = xl_tmp;
	}

	xr = LIST_FIRST(&xraddr_head);
	while (xr != NULL) {
		xr_tmp = LIST_NEXT(xr, xraddr_entries);
		free(xr);
		xr = xr_tmp;
	}
}

static void
sctp_process_inpcb(struct xsctp_inpcb *xinpcb,
    char *buf, const size_t buflen, size_t *offset)
{
	int indent = 0, xladdr_total = 0, is_listening = 0;
	static int first = 1;
	const char *tname, *pname;
	struct xsctp_tcb *xstcb;
	struct xsctp_laddr *xladdr;
	size_t offset_laddr;
	int process_closed;

	if (xinpcb->maxqlen > 0)
		is_listening = 1;

	if (first) {
		if (!Lflag) {
			xo_emit("Active SCTP associations");
			if (aflag)
				xo_emit(" (including servers)");
		} else
			xo_emit("Current listen queue sizes (qlen/maxqlen)");
		xo_emit("\n");
		if (Lflag)
			xo_emit("{T:/%-6.6s} {T:/%-5.5s} {T:/%-8.8s} "
			    "{T:/%-22.22s}\n",
			    "Proto", "Type", "Listen", "Local Address");
		else
			if (Wflag)
				xo_emit("{T:/%-6.6s} {T:/%-5.5s} {T:/%-45.45s} "
				    "{T:/%-45.45s} {T:/%s}\n",
				    "Proto", "Type",
				    "Local Address", "Foreign Address",
				    "(state)");
			else
				xo_emit("{T:/%-6.6s} {T:/%-5.5s} {T:/%-22.22s} "
				    "{T:/%-22.22s} {T:/%s}\n",
				    "Proto", "Type",
				    "Local Address", "Foreign Address",
				    "(state)");
		first = 0;
	}
	xladdr = (struct xsctp_laddr *)(buf + *offset);
	if ((!aflag && is_listening) ||
	    (Lflag && !is_listening)) {
		sctp_skip_xinpcb_ifneed(buf, buflen, offset);
		return;
	}

	if (xinpcb->flags & SCTP_PCB_FLAGS_BOUND_V6) {
		/* Can't distinguish between sctp46 and sctp6 */
		pname = "sctp46";
	} else {
		pname = "sctp4";
	}

	if (xinpcb->flags & SCTP_PCB_FLAGS_TCPTYPE)
		tname = "1to1";
	else if (xinpcb->flags & SCTP_PCB_FLAGS_UDPTYPE)
		tname = "1toN";
	else
		tname = "????";

	if (Lflag) {
		char buf1[22];

		snprintf(buf1, sizeof buf1, "%u/%u", 
		    xinpcb->qlen, xinpcb->maxqlen);
		xo_emit("{:protocol/%-6.6s/%s} {:type/%-5.5s/%s} ",
		    pname, tname);
		xo_emit("{d:queues/%-8.8s}{e:queue-len/%hu}"
		    "{e:max-queue-len/%hu} ",
		    buf1, xinpcb->qlen, xinpcb->maxqlen);
	}

	offset_laddr = *offset;
	process_closed = 0;

	xo_open_list("local-address");
retry:
	while (*offset < buflen) {
		xladdr = (struct xsctp_laddr *)(buf + *offset);
		*offset += sizeof(struct xsctp_laddr);
		if (xladdr->last) {
			if (aflag && !Lflag && (xladdr_total == 0) && process_closed) {
				xo_open_instance("local-address");

				xo_emit("{:protocol/%-6.6s/%s} "
				    "{:type/%-5.5s/%s} ", pname, tname);
				if (Wflag) {
					xo_emit("{P:/%-91.91s/%s} "
					    "{:state/CLOSED}", " ");
				} else {
					xo_emit("{P:/%-45.45s/%s} "
					    "{:state/CLOSED}", " ");
				}
				xo_close_instance("local-address");
			}
			if (process_closed || is_listening) {
				xo_emit("\n");
			}
			break;
		}

		if (!Lflag && !is_listening && !process_closed)
			continue;

		xo_open_instance("local-address");

		if (xladdr_total == 0) {
			if (!Lflag) {
				xo_emit("{:protocol/%-6.6s/%s} "
				    "{:type/%-5.5s/%s} ", pname, tname);
			}
		} else {
			xo_emit("\n");
			xo_emit(Lflag ? "{P:/%-21.21s} " : "{P:/%-12.12s} ",
			    " ");
		}
		sctp_print_address("local", &(xladdr->address),
		    htons(xinpcb->local_port), numeric_port);
		if (aflag && !Lflag && xladdr_total == 0) {
			if (Wflag) {
				if (process_closed) {
					xo_emit("{P:/%-45.45s} "
					    "{:state/CLOSED}", " ");
				} else {
					xo_emit("{P:/%-45.45s} "
					    "{:state/LISTEN}", " ");
				}
			} else {
				if (process_closed) {
					xo_emit("{P:/%-22.22s} "
					    "{:state/CLOSED}", " ");
				} else {
					xo_emit("{P:/%-22.22s} "
					    "{:state/LISTEN}", " ");
				}
			}
		}
		xladdr_total++;
		xo_close_instance("local-address");
	}

	xstcb = (struct xsctp_tcb *)(buf + *offset);
	*offset += sizeof(struct xsctp_tcb);
	if (aflag && (xladdr_total == 0) && xstcb->last && !process_closed) {
		process_closed = 1;
		*offset = offset_laddr;
		goto retry;
	}
	while (xstcb->last == 0 && *offset < buflen) {
		xo_emit("{:protocol/%-6.6s/%s} {:type/%-5.5s/%s} ",
		    pname, tname);
		sctp_process_tcb(xstcb, buf, buflen, offset, &indent);
		indent++;
		xstcb = (struct xsctp_tcb *)(buf + *offset);
		*offset += sizeof(struct xsctp_tcb);
	}

	xo_close_list("local-address");
}

/*
 * Print a summary of SCTP connections related to an Internet
 * protocol.
 */
void
sctp_protopr(u_long off __unused,
    const char *name __unused, int af1 __unused, int proto)
{
	char *buf;
	const char *mibvar = "net.inet.sctp.assoclist";
	size_t offset = 0;
	size_t len = 0;
	struct xsctp_inpcb *xinpcb;

	if (proto != IPPROTO_SCTP)
		return;

	if (sysctlbyname(mibvar, 0, &len, 0, 0) < 0) {
		if (errno != ENOENT)
			xo_warn("sysctl: %s", mibvar);
		return;
	}
	if ((buf = malloc(len)) == NULL) {
		xo_warnx("malloc %lu bytes", (u_long)len);
		return;
	}
	if (sysctlbyname(mibvar, buf, &len, 0, 0) < 0) {
		xo_warn("sysctl: %s", mibvar);
		free(buf);
		return;
	}

	xinpcb = (struct xsctp_inpcb *)(buf + offset);
	offset += sizeof(struct xsctp_inpcb);
	while (xinpcb->last == 0 && offset < len) {
		sctp_process_inpcb(xinpcb, buf, (const size_t)len,
		    &offset);

		xinpcb = (struct xsctp_inpcb *)(buf + offset);
		offset += sizeof(struct xsctp_inpcb);
	}

	free(buf);
}

static void
sctp_statesprint(uint32_t state)
{
	int idx;

	switch (state) {
	case SCTP_CLOSED:
		idx = NETSTAT_SCTP_STATES_CLOSED;
		break;
	case SCTP_BOUND:
		idx = NETSTAT_SCTP_STATES_BOUND;
		break;
	case SCTP_LISTEN:
		idx = NETSTAT_SCTP_STATES_LISTEN;
		break;
	case SCTP_COOKIE_WAIT:
		idx = NETSTAT_SCTP_STATES_COOKIE_WAIT;
		break;
	case SCTP_COOKIE_ECHOED:
		idx = NETSTAT_SCTP_STATES_COOKIE_ECHOED;
		break;
	case SCTP_ESTABLISHED:
		idx = NETSTAT_SCTP_STATES_ESTABLISHED;
		break;
	case SCTP_SHUTDOWN_SENT:
		idx = NETSTAT_SCTP_STATES_SHUTDOWN_SENT;
		break;
	case SCTP_SHUTDOWN_RECEIVED:
		idx = NETSTAT_SCTP_STATES_SHUTDOWN_RECEIVED;
		break;
	case SCTP_SHUTDOWN_ACK_SENT:
		idx = NETSTAT_SCTP_STATES_SHUTDOWN_ACK_SENT;
		break;
	case SCTP_SHUTDOWN_PENDING:
		idx = NETSTAT_SCTP_STATES_SHUTDOWN_PENDING;
		break;
	default:
		xo_emit("UNKNOWN {:state/0x%08x}", state);
		return;
	}

	xo_emit("{:state/%s}", sctpstates[idx]);
}

/*
 * Dump SCTP statistics structure.
 */
void
sctp_stats(u_long off, const char *name, int af1 __unused, int proto __unused)
{
	struct sctpstat sctpstat;

	if (fetch_stats("net.inet.sctp.stats", off, &sctpstat,
	    sizeof(sctpstat), kread) != 0)
		return;

	xo_open_container(name);
	xo_emit("{T:/%s}:\n", name);

#define	p(f, m) if (sctpstat.f || sflag <= 1) \
	xo_emit(m, (uintmax_t)sctpstat.f, plural(sctpstat.f))
#define	p1a(f, m) if (sctpstat.f || sflag <= 1) \
	xo_emit(m, (uintmax_t)sctpstat.f)

	/*
	 * input statistics
	 */
	p(sctps_recvpackets, "\t{:received-packets/%ju} "
	    "{N:/input packet%s}\n");
	p(sctps_recvdatagrams, "\t\t{:received-datagrams/%ju} "
	    "{N:/datagram%s}\n");
	p(sctps_recvpktwithdata, "\t\t{:received-with-data/%ju} "
	    "{N:/packet%s that had data}\n");
	p(sctps_recvsacks, "\t\t{:received-sack-chunks/%ju} "
	    "{N:/input SACK chunk%s}\n");
	p(sctps_recvdata, "\t\t{:received-data-chunks/%ju} "
	    "{N:/input DATA chunk%s}\n");
	p(sctps_recvdupdata, "\t\t{:received-duplicate-data-chunks/%ju} "
	    "{N:/duplicate DATA chunk%s}\n");
	p(sctps_recvheartbeat, "\t\t{:received-hb-chunks/%ju} "
	    "{N:/input HB chunk%s}\n");
	p(sctps_recvheartbeatack, "\t\t{:received-hb-ack-chunks/%ju} "
	    "{N:/HB-ACK chunk%s}\n");
	p(sctps_recvecne, "\t\t{:received-ecne-chunks/%ju} "
	    "{N:/input ECNE chunk%s}\n");
	p(sctps_recvauth, "\t\t{:received-auth-chunks/%ju} "
	    "{N:/input AUTH chunk%s}\n");
	p(sctps_recvauthmissing, "\t\t{:dropped-missing-auth/%ju} "
	    "{N:/chunk%s missing AUTH}\n");
	p(sctps_recvivalhmacid, "\t\t{:dropped-invalid-hmac/%ju} "
	    "{N:/invalid HMAC id%s received}\n");
	p(sctps_recvivalkeyid, "\t\t{:dropped-invalid-secret/%ju} "
	    "{N:/invalid secret id%s received}\n");
	p1a(sctps_recvauthfailed, "\t\t{:dropped-auth-failed/%ju} "
	    "{N:/auth failed}\n");
	p1a(sctps_recvexpress, "\t\t{:received-fast-path/%ju} "
	    "{N:/fast path receives all one chunk}\n");
	p1a(sctps_recvexpressm, "\t\t{:receives-fast-path-multipart/%ju} "
	    "{N:/fast path multi-part data}\n");

	/*
	 * output statistics
	 */
	p(sctps_sendpackets, "\t{:sent-packets/%ju} "
	    "{N:/output packet%s}\n");
	p(sctps_sendsacks, "\t\t{:sent-sacks/%ju} "
	    "{N:/output SACK%s}\n");
	p(sctps_senddata, "\t\t{:sent-data-chunks/%ju} "
	    "{N:/output DATA chunk%s}\n");
	p(sctps_sendretransdata, "\t\t{:sent-retransmitted-data-chunks/%ju} "
	    "{N:/retransmitted DATA chunk%s}\n");
	p(sctps_sendfastretrans, "\t\t"
	    "{:sent-fast-retransmitted-data-chunks/%ju} "
	    "{N:/fast retransmitted DATA chunk%s}\n");
	p(sctps_sendmultfastretrans, "\t\t"
	    "{:sent-fast-retransmitted-data-chunk-multiple-times/%ju} "
	    "{N:/FR'%s that happened more than once to same chunk}\n");
	p(sctps_sendheartbeat, "\t\t{:sent-hb-chunks/%ju} "
	    "{N:/output HB chunk%s}\n");
	p(sctps_sendecne, "\t\t{:sent-ecne-chunks/%ju} "
	    "{N:/output ECNE chunk%s}\n");
	p(sctps_sendauth, "\t\t{:sent-auth-chunks/%ju} "
	    "{N:/output AUTH chunk%s}\n");
	p1a(sctps_senderrors, "\t\t{:send-errors/%ju} "
	    "{N:/ip_output error counter}\n");

	/*
	 * PCKDROPREP statistics
	 */
	xo_emit("\t{T:Packet drop statistics}:\n");
	xo_open_container("drop-statistics");
	p1a(sctps_pdrpfmbox, "\t\t{:middle-box/%ju} "
	    "{N:/from middle box}\n");
	p1a(sctps_pdrpfehos, "\t\t{:end-host/%ju} "
	    "{N:/from end host}\n");
	p1a(sctps_pdrpmbda, "\t\t{:with-data/%ju} "
	    "{N:/with data}\n");
	p1a(sctps_pdrpmbct, "\t\t{:non-data/%ju} "
	    "{N:/non-data, non-endhost}\n");
	p1a(sctps_pdrpbwrpt, "\t\t{:non-endhost/%ju} "
	    "{N:/non-endhost, bandwidth rep only}\n");
	p1a(sctps_pdrpcrupt, "\t\t{:short-header/%ju} "
	    "{N:/not enough for chunk header}\n");
	p1a(sctps_pdrpnedat, "\t\t{:short-data/%ju} "
	    "{N:/not enough data to confirm}\n");
	p1a(sctps_pdrppdbrk, "\t\t{:chunk-break/%ju} "
	    "{N:/where process_chunk_drop said break}\n");
	p1a(sctps_pdrptsnnf, "\t\t{:tsn-not-found/%ju} "
	    "{N:/failed to find TSN}\n");
	p1a(sctps_pdrpdnfnd, "\t\t{:reverse-tsn/%ju} "
	    "{N:/attempt reverse TSN lookup}\n");
	p1a(sctps_pdrpdiwnp, "\t\t{:confirmed-zero-window/%ju} "
	    "{N:/e-host confirms zero-rwnd}\n");
	p1a(sctps_pdrpdizrw, "\t\t{:middle-box-no-space/%ju} "
	    "{N:/midbox confirms no space}\n");
	p1a(sctps_pdrpbadd, "\t\t{:bad-data/%ju} "
	    "{N:/data did not match TSN}\n");
	p(sctps_pdrpmark, "\t\t{:tsn-marked-fast-retransmission/%ju} "
	    "{N:/TSN'%s marked for Fast Retran}\n");
	xo_close_container("drop-statistics");

	/*
	 * Timeouts
	 */
	xo_emit("\t{T:Timeouts}:\n");
	xo_open_container("timeouts");
	p(sctps_timoiterator, "\t\t{:iterator/%ju} "
	    "{N:/iterator timer%s fired}\n");
	p(sctps_timodata, "\t\t{:t3-data/%ju} "
	    "{N:/T3 data time out%s}\n");
	p(sctps_timowindowprobe, "\t\t{:window-probe/%ju} "
	    "{N:/window probe (T3) timer%s fired}\n");
	p(sctps_timoinit, "\t\t{:init-timer/%ju} "
	    "{N:/INIT timer%s fired}\n");
	p(sctps_timosack, "\t\t{:sack-timer/%ju} "
	    "{N:/sack timer%s fired}\n");
	p(sctps_timoshutdown, "\t\t{:shutdown-timer/%ju} "
	    "{N:/shutdown timer%s fired}\n");
	p(sctps_timoheartbeat, "\t\t{:heartbeat-timer/%ju} "
	    "{N:/heartbeat timer%s fired}\n");
	p1a(sctps_timocookie, "\t\t{:cookie-timer/%ju} "
	    "{N:/a cookie timeout fired}\n");
	p1a(sctps_timosecret, "\t\t{:endpoint-changed-cookie/%ju} "
	    "{N:/an endpoint changed its cook}ie"
	    "secret\n");
	p(sctps_timopathmtu, "\t\t{:pmtu-timer/%ju} "
	    "{N:/PMTU timer%s fired}\n");
	p(sctps_timoshutdownack, "\t\t{:shutdown-timer/%ju} "
	    "{N:/shutdown ack timer%s fired}\n");
	p(sctps_timoshutdownguard, "\t\t{:shutdown-guard-timer/%ju} "
	    "{N:/shutdown guard timer%s fired}\n");
	p(sctps_timostrmrst, "\t\t{:stream-reset-timer/%ju} "
	    "{N:/stream reset timer%s fired}\n");
	p(sctps_timoearlyfr, "\t\t{:early-fast-retransmission-timer/%ju} "
	    "{N:/early FR timer%s fired}\n");
	p1a(sctps_timoasconf, "\t\t{:asconf-timer/%ju} "
	    "{N:/an asconf timer fired}\n");
	p1a(sctps_timoautoclose, "\t\t{:auto-close-timer/%ju} "
	    "{N:/auto close timer fired}\n");
	p(sctps_timoassockill, "\t\t{:asoc-free-timer/%ju} "
	    "{N:/asoc free timer%s expired}\n");
	p(sctps_timoinpkill, "\t\t{:input-free-timer/%ju} "
	    "{N:/inp free timer%s expired}\n");
	xo_close_container("timeouts");

#if 0
	/*
	 * Early fast retransmission counters
	 */
	p(sctps_earlyfrstart, "\t%ju TODO:sctps_earlyfrstart\n");
	p(sctps_earlyfrstop, "\t%ju TODO:sctps_earlyfrstop\n");
	p(sctps_earlyfrmrkretrans, "\t%ju TODO:sctps_earlyfrmrkretrans\n");
	p(sctps_earlyfrstpout, "\t%ju TODO:sctps_earlyfrstpout\n");
	p(sctps_earlyfrstpidsck1, "\t%ju TODO:sctps_earlyfrstpidsck1\n");
	p(sctps_earlyfrstpidsck2, "\t%ju TODO:sctps_earlyfrstpidsck2\n");
	p(sctps_earlyfrstpidsck3, "\t%ju TODO:sctps_earlyfrstpidsck3\n");
	p(sctps_earlyfrstpidsck4, "\t%ju TODO:sctps_earlyfrstpidsck4\n");
	p(sctps_earlyfrstrid, "\t%ju TODO:sctps_earlyfrstrid\n");
	p(sctps_earlyfrstrout, "\t%ju TODO:sctps_earlyfrstrout\n");
	p(sctps_earlyfrstrtmr, "\t%ju TODO:sctps_earlyfrstrtmr\n");
#endif

	/*
	 * Others
	 */
	p1a(sctps_hdrops, "\t{:dropped-too-short/%ju} "
	    "{N:/packet shorter than header}\n");
	p1a(sctps_badsum, "\t{:dropped-bad-checksum/%ju} "
	    "{N:/checksum error}\n");
	p1a(sctps_noport, "\t{:dropped-no-endpoint/%ju} "
	    "{N:/no endpoint for port}\n");
	p1a(sctps_badvtag, "\t{:dropped-bad-v-tag/%ju} "
	    "{N:/bad v-tag}\n");
	p1a(sctps_badsid, "\t{:dropped-bad-sid/%ju} "
	    "{N:/bad SID}\n");
	p1a(sctps_nomem, "\t{:dropped-no-memory/%ju} "
	    "{N:/no memory}\n");
	p1a(sctps_fastretransinrtt, "\t{:multiple-fast-retransmits-in-rtt/%ju} "
	    "{N:/number of multiple FR in a RT}T window\n");
#if 0
	p(sctps_markedretrans, "\t%ju TODO:sctps_markedretrans\n");
#endif
	p1a(sctps_naglesent, "\t{:rfc813-sent/%ju} "
	    "{N:/RFC813 allowed sending}\n");
	p1a(sctps_naglequeued, "\t{:rfc813-queued/%ju} "
	    "{N:/RFC813 does not allow sending}\n");
	p1a(sctps_maxburstqueued, "\t{:max-burst-queued/%ju} "
	    "{N:/times max burst prohibited sending}\n");
	p1a(sctps_ifnomemqueued, "\t{:no-memory-in-interface/%ju} "
	    "{N:/look ahead tells us no memory in interface}\n");
	p(sctps_windowprobed, "\t{:sent-window-probes/%ju} "
	    "{N:/number%s of window probes sent}\n");
	p(sctps_lowlevelerr, "\t{:low-level-err/%ju} "
	    "{N:/time%s an output error to clamp down on next user send}\n");
	p(sctps_lowlevelerrusr, "\t{:low-level-user-error/%ju} "
	    "{N:/time%s sctp_senderrors were caused from a user}\n");
	p(sctps_datadropchklmt, "\t{:dropped-chunk-limit/%ju} "
	    "{N:/number of in data drop%s due to chunk limit reached}\n");
	p(sctps_datadroprwnd, "\t{:dropped-rwnd-limit/%ju} "
	    "{N:/number of in data drop%s due to rwnd limit reached}\n");
	p(sctps_ecnereducedcwnd, "\t{:ecn-reduced-cwnd/%ju} "
	    "{N:/time%s a ECN reduced the cwnd}\n");
	p1a(sctps_vtagexpress, "\t{:v-tag-express-lookup/%ju} "
	    "{N:/used express lookup via vtag}\n");
	p1a(sctps_vtagbogus, "\t{:v-tag-collision/%ju} "
	    "{N:/collision in express lookup}\n");
	p(sctps_primary_randry, "\t{:sender-ran-dry/%ju} "
	    "{N:/time%s the sender ran dry of user data on primary}\n");
	p1a(sctps_cmt_randry, "\t{:cmt-ran-dry/%ju} "
	    "{N:/same for above}\n");
	p(sctps_slowpath_sack, "\t{:slow-path-sack/%ju} "
	    "{N:/sack%s the slow way}\n");
	p(sctps_wu_sacks_sent, "\t{:sent-window-update-only-sack/%ju} "
	    "{N:/window update only sack%s sent}\n");
	p(sctps_sends_with_flags, "\t{:sent-with-sinfo/%ju} "
	    "{N:/send%s with sinfo_flags !=0}\n");
	p(sctps_sends_with_unord, "\t{:sent-with-unordered/%ju} "
	    "{N:/unordered send%s}\n");
	p(sctps_sends_with_eof, "\t{:sent-with-eof/%ju} "
	    "{N:/send%s with EOF flag set}\n");
	p(sctps_sends_with_abort, "\t{:sent-with-abort/%ju} "
	    "{N:/send%s with ABORT flag set}\n");
	p(sctps_protocol_drain_calls, "\t{:protocol-drain-called/%ju} "
	    "{N:/time%s protocol drain called}\n");
	p(sctps_protocol_drains_done, "\t{:protocol-drain/%ju} "
	    "{N:/time%s we did a protocol drain}\n");
	p(sctps_read_peeks, "\t{:read-with-peek/%ju} "
	    "{N:/time%s recv was called with peek}\n");
	p(sctps_cached_chk, "\t{:cached-chunks/%ju} "
	    "{N:/cached chunk%s used}\n");
	p1a(sctps_cached_strmoq, "\t{:cached-output-queue-used/%ju} "
	    "{N:/cached stream oq's used}\n");
	p(sctps_left_abandon, "\t{:messages-abandoned/%ju} "
	    "{N:/unread message%s abandonded by close}\n");
	p1a(sctps_send_burst_avoid, "\t{:send-burst-avoidance/%ju} "
	    "{N:/send burst avoidance, already max burst inflight to net}\n");
	p1a(sctps_send_cwnd_avoid, "\t{:send-cwnd-avoidance/%ju} "
	    "{N:/send cwnd full avoidance, already max burst inflight "
	    "to net}\n");
	p(sctps_fwdtsn_map_over, "\t{:tsn-map-overruns/%ju} "
	   "{N:/number of map array over-run%s via fwd-tsn's}\n");

#undef p
#undef p1a
	xo_close_container(name);
}

#endif /* SCTP */
