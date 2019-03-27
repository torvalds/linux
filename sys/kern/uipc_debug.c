/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2007 Robert N. M. Watson
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * Debugger routines relating to sockets, protocols, etc, for use in DDB.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_ddb.h"

#include <sys/param.h>
#include <sys/domain.h>
#include <sys/kernel.h>
#include <sys/protosw.h>
#include <sys/socket.h>
#include <sys/socketvar.h>

#ifdef DDB
#include <ddb/ddb.h>

static void
db_print_sotype(short so_type)
{

	switch (so_type) {
	case SOCK_STREAM:
		db_printf("SOCK_STREAM");
		break;

	case SOCK_DGRAM:
		db_printf("SOCK_DGRAM");
		break;

	case SOCK_RAW:
		db_printf("SOCK_RAW");
		break;

	case SOCK_RDM:
		db_printf("SOCK_RDM");
		break;

	case SOCK_SEQPACKET:
		db_printf("SOCK_SEQPACKET");
		break;

	default:
		db_printf("unknown");
		break;
	}
}

static void
db_print_sooptions(int so_options)
{
	int comma;

	comma = 0;
	if (so_options & SO_DEBUG) {
		db_printf("%sSO_DEBUG", comma ? ", " : "");
		comma = 1;
	}
	if (so_options & SO_ACCEPTCONN) {
		db_printf("%sSO_ACCEPTCONN", comma ? ", " : "");
		comma = 1;
	}
	if (so_options & SO_REUSEADDR) {
		db_printf("%sSO_REUSEADDR", comma ? ", " : "");
		comma = 1;
	}
	if (so_options & SO_KEEPALIVE) {
		db_printf("%sSO_KEEPALIVE", comma ? ", " : "");
		comma = 1;
	}
	if (so_options & SO_DONTROUTE) {
		db_printf("%sSO_DONTROUTE", comma ? ", " : "");
		comma = 1;
	}
	if (so_options & SO_BROADCAST) {
		db_printf("%sSO_BROADCAST", comma ? ", " : "");
		comma = 1;
	}
	if (so_options & SO_USELOOPBACK) {
		db_printf("%sSO_USELOOPBACK", comma ? ", " : "");
		comma = 1;
	}
	if (so_options & SO_LINGER) {
		db_printf("%sSO_LINGER", comma ? ", " : "");
		comma = 1;
	}
	if (so_options & SO_OOBINLINE) {
		db_printf("%sSO_OOBINLINE", comma ? ", " : "");
		comma = 1;
	}
	if (so_options & SO_REUSEPORT) {
		db_printf("%sSO_REUSEPORT", comma ? ", " : "");
		comma = 1;
	}
	if (so_options & SO_REUSEPORT_LB) {
		db_printf("%sSO_REUSEPORT_LB", comma ? ", " : "");
		comma = 1;
	}
	if (so_options & SO_TIMESTAMP) {
		db_printf("%sSO_TIMESTAMP", comma ? ", " : "");
		comma = 1;
	}
	if (so_options & SO_NOSIGPIPE) {
		db_printf("%sSO_NOSIGPIPE", comma ? ", " : "");
		comma = 1;
	}
	if (so_options & SO_ACCEPTFILTER) {
		db_printf("%sSO_ACCEPTFILTER", comma ? ", " : "");
		comma = 1;
	}
	if (so_options & SO_BINTIME) {
		db_printf("%sSO_BINTIME", comma ? ", " : "");
		comma = 1;
	}
	if (so_options & SO_NO_OFFLOAD) {
		db_printf("%sSO_NO_OFFLOAD", comma ? ", " : "");
		comma = 1;
	}
	if (so_options & SO_NO_DDP) {
		db_printf("%sSO_NO_DDP", comma ? ", " : "");
		comma = 1;
	}
}

static void
db_print_sostate(short so_state)
{
	int comma;

	comma = 0;
	if (so_state & SS_NOFDREF) {
		db_printf("%sSS_NOFDREF", comma ? ", " : "");
		comma = 1;
	}
	if (so_state & SS_ISCONNECTED) {
		db_printf("%sSS_ISCONNECTED", comma ? ", " : "");
		comma = 1;
	}
	if (so_state & SS_ISCONNECTING) {
		db_printf("%sSS_ISCONNECTING", comma ? ", " : "");
		comma = 1;
	}
	if (so_state & SS_ISDISCONNECTING) {
		db_printf("%sSS_ISDISCONNECTING", comma ? ", " : "");
		comma = 1;
	}
	if (so_state & SS_NBIO) {
		db_printf("%sSS_NBIO", comma ? ", " : "");
		comma = 1;
	}
	if (so_state & SS_ASYNC) {
		db_printf("%sSS_ASYNC", comma ? ", " : "");
		comma = 1;
	}
	if (so_state & SS_ISCONFIRMING) {
		db_printf("%sSS_ISCONFIRMING", comma ? ", " : "");
		comma = 1;
	}
	if (so_state & SS_PROTOREF) {
		db_printf("%sSS_PROTOREF", comma ? ", " : "");
		comma = 1;
	}
}

static void
db_print_soqstate(int so_qstate)
{
	int comma;

	comma = 0;
	if (so_qstate & SQ_INCOMP) {
		db_printf("%sSQ_INCOMP", comma ? ", " : "");
		comma = 1;
	}
	if (so_qstate & SQ_COMP) {
		db_printf("%sSQ_COMP", comma ? ", " : "");
		comma = 1;
	}
}

static void
db_print_sbstate(short sb_state)
{
	int comma;

	comma = 0;
	if (sb_state & SBS_CANTSENDMORE) {
		db_printf("%sSBS_CANTSENDMORE", comma ? ", " : "");
		comma = 1;
	}
	if (sb_state & SBS_CANTRCVMORE) {
		db_printf("%sSBS_CANTRCVMORE", comma ? ", " : "");
		comma = 1;
	}
	if (sb_state & SBS_RCVATMARK) {
		db_printf("%sSBS_RCVATMARK", comma ? ", " : "");
		comma = 1;
	}
}

static void
db_print_indent(int indent)
{
	int i;

	for (i = 0; i < indent; i++)
		db_printf(" ");
}

static void
db_print_domain(struct domain *d, const char *domain_name, int indent)
{

	db_print_indent(indent);
	db_printf("%s at %p\n", domain_name, d);

	indent += 2;

	db_print_indent(indent);
	db_printf("dom_family: %d   ", d->dom_family);
	db_printf("dom_name: %s\n", d->dom_name);

	db_print_indent(indent);
	db_printf("dom_init: %p   ", d->dom_init);
	db_printf("dom_externalize: %p   ", d->dom_externalize);
	db_printf("dom_dispose: %p\n", d->dom_dispose);

	db_print_indent(indent);
	db_printf("dom_protosw: %p   ", d->dom_protosw);
	db_printf("dom_next: %p\n", d->dom_next);

	db_print_indent(indent);
	db_printf("dom_rtattach: %p   ", d->dom_rtattach);

	db_print_indent(indent);
	db_printf("dom_ifattach: %p   ", d->dom_ifattach);
	db_printf("dom_ifdetach: %p\n", d->dom_ifdetach);
}

static void
db_print_prflags(short pr_flags)
{
	int comma;

	comma = 0;
	if (pr_flags & PR_ATOMIC) {
		db_printf("%sPR_ATOMIC", comma ? ", " : "");
		comma = 1;
	}
	if (pr_flags & PR_ADDR) {
		db_printf("%sPR_ADDR", comma ? ", " : "");
		comma = 1;
	}
	if (pr_flags & PR_CONNREQUIRED) {
		db_printf("%sPR_CONNREQUIRED", comma ? ", " : "");
		comma = 1;
	}
	if (pr_flags & PR_WANTRCVD) {
		db_printf("%sPR_WANTRCVD", comma ? ", " : "");
		comma = 1;
	}
	if (pr_flags & PR_RIGHTS) {
		db_printf("%sPR_RIGHTS", comma ? ", " : "");
		comma = 1;
	}
	if (pr_flags & PR_IMPLOPCL) {
		db_printf("%sPR_IMPLOPCL", comma ? ", " : "");
		comma = 1;
	}
	if (pr_flags & PR_LASTHDR) {
		db_printf("%sPR_LASTHDR", comma ? ", " : "");
		comma = 1;
	}
}

static void
db_print_protosw(struct protosw *pr, const char *prname, int indent)
{

	db_print_indent(indent);
	db_printf("%s at %p\n", prname, pr);

	indent += 2;

	db_print_indent(indent);
	db_printf("pr_type: %d   ", pr->pr_type);
	db_printf("pr_domain: %p\n", pr->pr_domain);
	if (pr->pr_domain != NULL)
		db_print_domain(pr->pr_domain, "pr_domain", indent);

	db_print_indent(indent);
	db_printf("pr_protocol: %d\n", pr->pr_protocol);

	db_print_indent(indent);
	db_printf("pr_flags: %d (", pr->pr_flags);
	db_print_prflags(pr->pr_flags);
	db_printf(")\n");

	db_print_indent(indent);
	db_printf("pr_input: %p   ", pr->pr_input);
	db_printf("pr_output: %p   ", pr->pr_output);
	db_printf("pr_ctlinput: %p\n", pr->pr_ctlinput);

	db_print_indent(indent);
	db_printf("pr_ctloutput: %p   ", pr->pr_ctloutput);
	db_printf("pr_init: %p\n", pr->pr_init);

	db_print_indent(indent);
	db_printf("pr_fasttimo: %p   ", pr->pr_fasttimo);
	db_printf("pr_slowtimo: %p   ", pr->pr_slowtimo);
	db_printf("pr_drain: %p\n", pr->pr_drain);
}

static void
db_print_sbflags(short sb_flags)
{
	int comma;

	comma = 0;
	if (sb_flags & SB_WAIT) {
		db_printf("%sSB_WAIT", comma ? ", " : "");
		comma = 1;
	}
	if (sb_flags & SB_SEL) {
		db_printf("%sSB_SEL", comma ? ", " : "");
		comma = 1;
	}
	if (sb_flags & SB_ASYNC) {
		db_printf("%sSB_ASYNC", comma ? ", " : "");
		comma = 1;
	}
	if (sb_flags & SB_UPCALL) {
		db_printf("%sSB_UPCALL", comma ? ", " : "");
		comma = 1;
	}
	if (sb_flags & SB_NOINTR) {
		db_printf("%sSB_NOINTR", comma ? ", " : "");
		comma = 1;
	}
	if (sb_flags & SB_AIO) {
		db_printf("%sSB_AIO", comma ? ", " : "");
		comma = 1;
	}
	if (sb_flags & SB_KNOTE) {
		db_printf("%sSB_KNOTE", comma ? ", " : "");
		comma = 1;
	}
	if (sb_flags & SB_AUTOSIZE) {
		db_printf("%sSB_AUTOSIZE", comma ? ", " : "");
		comma = 1;
	}
}

static void
db_print_sockbuf(struct sockbuf *sb, const char *sockbufname, int indent)
{

	db_print_indent(indent);
	db_printf("%s at %p\n", sockbufname, sb);

	indent += 2;

	db_print_indent(indent);
	db_printf("sb_state: 0x%x (", sb->sb_state);
	db_print_sbstate(sb->sb_state);
	db_printf(")\n");

	db_print_indent(indent);
	db_printf("sb_mb: %p   ", sb->sb_mb);
	db_printf("sb_mbtail: %p   ", sb->sb_mbtail);
	db_printf("sb_lastrecord: %p\n", sb->sb_lastrecord);

	db_print_indent(indent);
	db_printf("sb_sndptr: %p   ", sb->sb_sndptr);
	db_printf("sb_sndptroff: %u\n", sb->sb_sndptroff);

	db_print_indent(indent);
	db_printf("sb_acc: %u   ", sb->sb_acc);
	db_printf("sb_ccc: %u   ", sb->sb_ccc);
	db_printf("sb_hiwat: %u   ", sb->sb_hiwat);
	db_printf("sb_mbcnt: %u   ", sb->sb_mbcnt);
	db_printf("sb_mbmax: %u\n", sb->sb_mbmax);

	db_print_indent(indent);
	db_printf("sb_mcnt: %u   ", sb->sb_mcnt);
	db_printf("sb_ccnt: %u   ", sb->sb_ccnt);
	db_printf("sb_ctl: %u   ", sb->sb_ctl);
	db_printf("sb_lowat: %d   ", sb->sb_lowat);
	db_printf("sb_timeo: %jd\n", sb->sb_timeo);

	db_print_indent(indent);
	db_printf("sb_flags: 0x%x (", sb->sb_flags);
	db_print_sbflags(sb->sb_flags);
	db_printf(")\n");

	db_print_indent(indent);
	db_printf("sb_aiojobq first: %p\n", TAILQ_FIRST(&sb->sb_aiojobq));
}

static void
db_print_socket(struct socket *so, const char *socketname, int indent)
{

	db_print_indent(indent);
	db_printf("%s at %p\n", socketname, so);

	indent += 2;

	db_print_indent(indent);
	db_printf("so_count: %d   ", so->so_count);
	db_printf("so_type: %d (", so->so_type);
	db_print_sotype(so->so_type);
	db_printf(")\n");

	db_print_indent(indent);
	db_printf("so_options: 0x%x (", so->so_options);
	db_print_sooptions(so->so_options);
	db_printf(")\n");

	db_print_indent(indent);
	db_printf("so_linger: %d   ", so->so_linger);
	db_printf("so_state: 0x%x (", so->so_state);
	db_print_sostate(so->so_state);
	db_printf(")\n");

	db_print_indent(indent);
	db_printf("so_pcb: %p   ", so->so_pcb);
	db_printf("so_proto: %p\n", so->so_proto);

	if (so->so_proto != NULL)
		db_print_protosw(so->so_proto, "so_proto", indent);

	db_print_indent(indent);
	if (so->so_options & SO_ACCEPTCONN) {
		db_printf("sol_incomp first: %p   ",
		    TAILQ_FIRST(&so->sol_incomp));
		db_printf("sol_comp first: %p\n", TAILQ_FIRST(&so->sol_comp));
		db_printf("sol_qlen: %d   ", so->sol_qlen);
		db_printf("sol_incqlen: %d   ", so->sol_incqlen);
		db_printf("sol_qlimit: %d   ", so->sol_qlimit);
	} else {
		db_printf("so_qstate: 0x%x (", so->so_qstate);
		db_print_soqstate(so->so_qstate);
		db_printf(")   ");
		db_printf("so_listen: %p   ", so->so_listen);
		/* so_list skipped */
		db_printf("so_timeo: %d   ", so->so_timeo);
		db_printf("so_error: %d\n", so->so_error);

		db_print_indent(indent);
		db_printf("so_sigio: %p   ", so->so_sigio);
		db_printf("so_oobmark: %lu\n", so->so_oobmark);

		db_print_sockbuf(&so->so_rcv, "so_rcv", indent);
		db_print_sockbuf(&so->so_snd, "so_snd", indent);
	}
}

DB_SHOW_COMMAND(socket, db_show_socket)
{
	struct socket *so;

	if (!have_addr) {
		db_printf("usage: show socket <addr>\n");
		return;
	}
	so = (struct socket *)addr;

	db_print_socket(so, "socket", 0);
}

DB_SHOW_COMMAND(sockbuf, db_show_sockbuf)
{
	struct sockbuf *sb;

	if (!have_addr) {
		db_printf("usage: show sockbuf <addr>\n");
		return;
	}
	sb = (struct sockbuf *)addr;

	db_print_sockbuf(sb, "sockbuf", 0);
}

DB_SHOW_COMMAND(protosw, db_show_protosw)
{
	struct protosw *pr;

	if (!have_addr) {
		db_printf("usage: show protosw <addr>\n");
		return;
	}
	pr = (struct protosw *)addr;

	db_print_protosw(pr, "protosw", 0);
}

DB_SHOW_COMMAND(domain, db_show_domain)
{
	struct domain *d;

	if (!have_addr) {
		db_printf("usage: show protosw <addr>\n");
		return;
	}
	d = (struct domain *)addr;

	db_print_domain(d, "domain", 0);
}
#endif
