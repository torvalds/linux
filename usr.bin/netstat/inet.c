/*-
 * Copyright (c) 1983, 1988, 1993, 1995
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
static char sccsid[] = "@(#)inet.c	8.5 (Berkeley) 5/24/95";
#endif /* not lint */
#endif

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/queue.h>
#include <sys/domain.h>
#include <sys/protosw.h>
#include <sys/socket.h>
#define	_WANT_SOCKET
#include <sys/socketvar.h>
#include <sys/sysctl.h>

#include <net/route.h>
#include <net/if_arp.h>
#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#include <netinet/ip_carp.h>
#ifdef INET6
#include <netinet/ip6.h>
#endif /* INET6 */
#include <netinet/in_pcb.h>
#include <netinet/ip_icmp.h>
#include <netinet/icmp_var.h>
#include <netinet/igmp_var.h>
#include <netinet/ip_var.h>
#include <netinet/pim_var.h>
#include <netinet/tcp.h>
#include <netinet/tcpip.h>
#include <netinet/tcp_seq.h>
#define	TCPSTATES
#include <netinet/tcp_fsm.h>
#include <netinet/tcp_timer.h>
#include <netinet/tcp_var.h>
#include <netinet/udp.h>
#include <netinet/udp_var.h>

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
#include <libxo/xo.h>
#include "netstat.h"
#include "nl_defs.h"

#ifdef INET
static void inetprint(const char *, struct in_addr *, int, const char *, int,
    const int);
#endif
#ifdef INET6
static int udp_done, tcp_done, sdp_done;
#endif /* INET6 */

static int
pcblist_sysctl(int proto, const char *name, char **bufp)
{
	const char *mibvar;
	char *buf;
	size_t len;

	switch (proto) {
	case IPPROTO_TCP:
		mibvar = "net.inet.tcp.pcblist";
		break;
	case IPPROTO_UDP:
		mibvar = "net.inet.udp.pcblist";
		break;
	case IPPROTO_DIVERT:
		mibvar = "net.inet.divert.pcblist";
		break;
	default:
		mibvar = "net.inet.raw.pcblist";
		break;
	}
	if (strncmp(name, "sdp", 3) == 0)
		mibvar = "net.inet.sdp.pcblist";
	len = 0;
	if (sysctlbyname(mibvar, 0, &len, 0, 0) < 0) {
		if (errno != ENOENT)
			xo_warn("sysctl: %s", mibvar);
		return (0);
	}
	if ((buf = malloc(len)) == NULL) {
		xo_warnx("malloc %lu bytes", (u_long)len);
		return (0);
	}
	if (sysctlbyname(mibvar, buf, &len, 0, 0) < 0) {
		xo_warn("sysctl: %s", mibvar);
		free(buf);
		return (0);
	}
	*bufp = buf;
	return (1);
}

/*
 * Copied directly from uipc_socket2.c.  We leave out some fields that are in
 * nested structures that aren't used to avoid extra work.
 */
static void
sbtoxsockbuf(struct sockbuf *sb, struct xsockbuf *xsb)
{
	xsb->sb_cc = sb->sb_ccc;
	xsb->sb_hiwat = sb->sb_hiwat;
	xsb->sb_mbcnt = sb->sb_mbcnt;
	xsb->sb_mcnt = sb->sb_mcnt;
	xsb->sb_ccnt = sb->sb_ccnt;
	xsb->sb_mbmax = sb->sb_mbmax;
	xsb->sb_lowat = sb->sb_lowat;
	xsb->sb_flags = sb->sb_flags;
	xsb->sb_timeo = sb->sb_timeo;
}

int
sotoxsocket(struct socket *so, struct xsocket *xso)
{
	struct protosw proto;
	struct domain domain;

	bzero(xso, sizeof *xso);
	xso->xso_len = sizeof *xso;
	xso->xso_so = (uintptr_t)so;
	xso->so_type = so->so_type;
	xso->so_options = so->so_options;
	xso->so_linger = so->so_linger;
	xso->so_state = so->so_state;
	xso->so_pcb = (uintptr_t)so->so_pcb;
	if (kread((uintptr_t)so->so_proto, &proto, sizeof(proto)) != 0)
		return (-1);
	xso->xso_protocol = proto.pr_protocol;
	if (kread((uintptr_t)proto.pr_domain, &domain, sizeof(domain)) != 0)
		return (-1);
	xso->xso_family = domain.dom_family;
	xso->so_timeo = so->so_timeo;
	xso->so_error = so->so_error;
	if ((so->so_options & SO_ACCEPTCONN) != 0) {
		xso->so_qlen = so->sol_qlen;
		xso->so_incqlen = so->sol_incqlen;
		xso->so_qlimit = so->sol_qlimit;
	} else {
		sbtoxsockbuf(&so->so_snd, &xso->so_snd);
		sbtoxsockbuf(&so->so_rcv, &xso->so_rcv);
		xso->so_oobmark = so->so_oobmark;
	}
	return (0);
}

/*
 * Print a summary of connections related to an Internet
 * protocol.  For TCP, also give state of connection.
 * Listening processes (aflag) are suppressed unless the
 * -a (all) flag is specified.
 */
void
protopr(u_long off, const char *name, int af1, int proto)
{
	static int first = 1;
	int istcp;
	char *buf;
	const char *vchar;
	struct xtcpcb *tp;
	struct xinpcb *inp;
	struct xinpgen *xig, *oxig;
	struct xsocket *so;

	istcp = 0;
	switch (proto) {
	case IPPROTO_TCP:
#ifdef INET6
		if (strncmp(name, "sdp", 3) != 0) {
			if (tcp_done != 0)
				return;
			else
				tcp_done = 1;
		} else {
			if (sdp_done != 0)
				return;
			else
				sdp_done = 1;
		}
#endif
		istcp = 1;
		break;
	case IPPROTO_UDP:
#ifdef INET6
		if (udp_done != 0)
			return;
		else
			udp_done = 1;
#endif
		break;
	}

	if (!pcblist_sysctl(proto, name, &buf))
		return;

	oxig = xig = (struct xinpgen *)buf;
	for (xig = (struct xinpgen *)((char *)xig + xig->xig_len);
	    xig->xig_len > sizeof(struct xinpgen);
	    xig = (struct xinpgen *)((char *)xig + xig->xig_len)) {
		if (istcp) {
			tp = (struct xtcpcb *)xig;
			inp = &tp->xt_inp;
		} else {
			inp = (struct xinpcb *)xig;
		}
		so = &inp->xi_socket;

		/* Ignore sockets for protocols other than the desired one. */
		if (so->xso_protocol != proto)
			continue;

		/* Ignore PCBs which were freed during copyout. */
		if (inp->inp_gencnt > oxig->xig_gen)
			continue;

		if ((af1 == AF_INET && (inp->inp_vflag & INP_IPV4) == 0)
#ifdef INET6
		    || (af1 == AF_INET6 && (inp->inp_vflag & INP_IPV6) == 0)
#endif /* INET6 */
		    || (af1 == AF_UNSPEC && ((inp->inp_vflag & INP_IPV4) == 0
#ifdef INET6
					  && (inp->inp_vflag & INP_IPV6) == 0
#endif /* INET6 */
			))
		    )
			continue;
		if (!aflag &&
		    (
		     (istcp && tp->t_state == TCPS_LISTEN)
		     || (af1 == AF_INET &&
		      inet_lnaof(inp->inp_laddr) == INADDR_ANY)
#ifdef INET6
		     || (af1 == AF_INET6 &&
			 IN6_IS_ADDR_UNSPECIFIED(&inp->in6p_laddr))
#endif /* INET6 */
		     || (af1 == AF_UNSPEC &&
			 (((inp->inp_vflag & INP_IPV4) != 0 &&
			   inet_lnaof(inp->inp_laddr) == INADDR_ANY)
#ifdef INET6
			  || ((inp->inp_vflag & INP_IPV6) != 0 &&
			      IN6_IS_ADDR_UNSPECIFIED(&inp->in6p_laddr))
#endif
			  ))
		     ))
			continue;

		if (first) {
			if (!Lflag) {
				xo_emit("Active Internet connections");
				if (aflag)
					xo_emit(" (including servers)");
			} else
				xo_emit(
	"Current listen queue sizes (qlen/incqlen/maxqlen)");
			xo_emit("\n");
			if (Aflag)
				xo_emit("{T:/%-*s} ", 2 * (int)sizeof(void *),
				    "Tcpcb");
			if (Lflag)
				xo_emit((Aflag && !Wflag) ?
				    "{T:/%-5.5s} {T:/%-32.32s} {T:/%-18.18s}" :
				    ((!Wflag || af1 == AF_INET) ?
				    "{T:/%-5.5s} {T:/%-32.32s} {T:/%-22.22s}" :
				    "{T:/%-5.5s} {T:/%-32.32s} {T:/%-45.45s}"),
				    "Proto", "Listen", "Local Address");
			else if (Tflag)
				xo_emit((Aflag && !Wflag) ?
    "{T:/%-5.5s} {T:/%-6.6s} {T:/%-6.6s} {T:/%-6.6s} {T:/%-18.18s} {T:/%s}" :
				    ((!Wflag || af1 == AF_INET) ?
    "{T:/%-5.5s} {T:/%-6.6s} {T:/%-6.6s} {T:/%-6.6s} {T:/%-22.22s} {T:/%s}" :
    "{T:/%-5.5s} {T:/%-6.6s} {T:/%-6.6s} {T:/%-6.6s} {T:/%-45.45s} {T:/%s}"),
				    "Proto", "Rexmit", "OOORcv", "0-win",
				    "Local Address", "Foreign Address");
			else {
				xo_emit((Aflag && !Wflag) ?
    "{T:/%-5.5s} {T:/%-6.6s} {T:/%-6.6s} {T:/%-18.18s} {T:/%-18.18s}" :
				    ((!Wflag || af1 == AF_INET) ?
    "{T:/%-5.5s} {T:/%-6.6s} {T:/%-6.6s} {T:/%-22.22s} {T:/%-22.22s}" :
    "{T:/%-5.5s} {T:/%-6.6s} {T:/%-6.6s} {T:/%-45.45s} {T:/%-45.45s}"),
				    "Proto", "Recv-Q", "Send-Q",
				    "Local Address", "Foreign Address");
				if (!xflag && !Rflag)
					xo_emit(" {T:/%-11.11s}", "(state)");
			}
			if (xflag) {
				xo_emit(" {T:/%-6.6s} {T:/%-6.6s} {T:/%-6.6s} "
				    "{T:/%-6.6s} {T:/%-6.6s} {T:/%-6.6s} "
				    "{T:/%-6.6s} {T:/%-6.6s} {T:/%-6.6s} "
				    "{T:/%-6.6s} {T:/%-6.6s} {T:/%-6.6s}",
				    "R-MBUF", "S-MBUF", "R-CLUS", "S-CLUS",
				    "R-HIWA", "S-HIWA", "R-LOWA", "S-LOWA",
				    "R-BCNT", "S-BCNT", "R-BMAX", "S-BMAX");
				xo_emit(" {T:/%7.7s} {T:/%7.7s} {T:/%7.7s} "
				    "{T:/%7.7s} {T:/%7.7s} {T:/%7.7s}",
				    "rexmt", "persist", "keep", "2msl",
				    "delack", "rcvtime");
			} else if (Rflag) {
				xo_emit("  {T:/%8.8s} {T:/%5.5s}",
				    "flowid", "ftype");
			}
			if (Pflag)
				xo_emit(" {T:/%s}", "Log ID");
			xo_emit("\n");
			first = 0;
		}
		if (Lflag && so->so_qlimit == 0)
			continue;
		xo_open_instance("socket");
		if (Aflag) {
			if (istcp)
				xo_emit("{q:address/%*lx} ",
				    2 * (int)sizeof(void *),
				    (u_long)inp->inp_ppcb);
			else
				xo_emit("{q:address/%*lx} ",
				    2 * (int)sizeof(void *),
				    (u_long)so->so_pcb);
		}
#ifdef INET6
		if ((inp->inp_vflag & INP_IPV6) != 0)
			vchar = ((inp->inp_vflag & INP_IPV4) != 0) ?
			    "46" : "6";
		else
#endif
		vchar = ((inp->inp_vflag & INP_IPV4) != 0) ?
		    "4" : "";
		if (istcp && (tp->t_flags & TF_TOE) != 0)
			xo_emit("{:protocol/%-3.3s%-2.2s/%s%s} ", "toe", vchar);
		else
			xo_emit("{:protocol/%-3.3s%-2.2s/%s%s} ", name, vchar);
		if (Lflag) {
			char buf1[33];

			snprintf(buf1, sizeof buf1, "%u/%u/%u", so->so_qlen,
			    so->so_incqlen, so->so_qlimit);
			xo_emit("{:listen-queue-sizes/%-32.32s} ", buf1);
		} else if (Tflag) {
			if (istcp)
				xo_emit("{:sent-retransmit-packets/%6u} "
				    "{:received-out-of-order-packets/%6u} "
				    "{:sent-zero-window/%6u} ",
				    tp->t_sndrexmitpack, tp->t_rcvoopack,
				    tp->t_sndzerowin);
			else
				xo_emit("{P:/%21s}", "");
		} else {
			xo_emit("{:receive-bytes-waiting/%6u} "
			    "{:send-bytes-waiting/%6u} ",
			    so->so_rcv.sb_cc, so->so_snd.sb_cc);
		}
		if (numeric_port) {
#ifdef INET
			if (inp->inp_vflag & INP_IPV4) {
				inetprint("local", &inp->inp_laddr,
				    (int)inp->inp_lport, name, 1, af1);
				if (!Lflag)
					inetprint("remote", &inp->inp_faddr,
					    (int)inp->inp_fport, name, 1, af1);
			}
#endif
#if defined(INET) && defined(INET6)
			else
#endif
#ifdef INET6
			if (inp->inp_vflag & INP_IPV6) {
				inet6print("local", &inp->in6p_laddr,
				    (int)inp->inp_lport, name, 1);
				if (!Lflag)
					inet6print("remote", &inp->in6p_faddr,
					    (int)inp->inp_fport, name, 1);
			} /* else nothing printed now */
#endif /* INET6 */
		} else if (inp->inp_flags & INP_ANONPORT) {
#ifdef INET
			if (inp->inp_vflag & INP_IPV4) {
				inetprint("local", &inp->inp_laddr,
				    (int)inp->inp_lport, name, 1, af1);
				if (!Lflag)
					inetprint("remote", &inp->inp_faddr,
					    (int)inp->inp_fport, name, 0, af1);
			}
#endif
#if defined(INET) && defined(INET6)
			else
#endif
#ifdef INET6
			if (inp->inp_vflag & INP_IPV6) {
				inet6print("local", &inp->in6p_laddr,
				    (int)inp->inp_lport, name, 1);
				if (!Lflag)
					inet6print("remote", &inp->in6p_faddr,
					    (int)inp->inp_fport, name, 0);
			} /* else nothing printed now */
#endif /* INET6 */
		} else {
#ifdef INET
			if (inp->inp_vflag & INP_IPV4) {
				inetprint("local", &inp->inp_laddr,
				    (int)inp->inp_lport, name, 0, af1);
				if (!Lflag)
					inetprint("remote", &inp->inp_faddr,
					    (int)inp->inp_fport, name,
					    inp->inp_lport != inp->inp_fport,
					    af1);
			}
#endif
#if defined(INET) && defined(INET6)
			else
#endif
#ifdef INET6
			if (inp->inp_vflag & INP_IPV6) {
				inet6print("local", &inp->in6p_laddr,
				    (int)inp->inp_lport, name, 0);
				if (!Lflag)
					inet6print("remote", &inp->in6p_faddr,
					    (int)inp->inp_fport, name,
					    inp->inp_lport != inp->inp_fport);
			} /* else nothing printed now */
#endif /* INET6 */
		}
		if (xflag) {
			xo_emit("{:receive-mbufs/%6u} {:send-mbufs/%6u} "
			    "{:receive-clusters/%6u} {:send-clusters/%6u} "
			    "{:receive-high-water/%6u} {:send-high-water/%6u} "
			    "{:receive-low-water/%6u} {:send-low-water/%6u} "
			    "{:receive-mbuf-bytes/%6u} {:send-mbuf-bytes/%6u} "
			    "{:receive-mbuf-bytes-max/%6u} "
			    "{:send-mbuf-bytes-max/%6u}",
			    so->so_rcv.sb_mcnt, so->so_snd.sb_mcnt,
			    so->so_rcv.sb_ccnt, so->so_snd.sb_ccnt,
			    so->so_rcv.sb_hiwat, so->so_snd.sb_hiwat,
			    so->so_rcv.sb_lowat, so->so_snd.sb_lowat,
			    so->so_rcv.sb_mbcnt, so->so_snd.sb_mbcnt,
			    so->so_rcv.sb_mbmax, so->so_snd.sb_mbmax);
			if (istcp)
				xo_emit(" {:retransmit-timer/%4d.%02d} "
				    "{:persist-timer/%4d.%02d} "
				    "{:keepalive-timer/%4d.%02d} "
				    "{:msl2-timer/%4d.%02d} "
				    "{:delay-ack-timer/%4d.%02d} "
				    "{:inactivity-timer/%4d.%02d}",
				    tp->tt_rexmt / 1000,
				    (tp->tt_rexmt % 1000) / 10,
				    tp->tt_persist / 1000,
				    (tp->tt_persist % 1000) / 10,
				    tp->tt_keep / 1000,
				    (tp->tt_keep % 1000) / 10,
				    tp->tt_2msl / 1000,
				    (tp->tt_2msl % 1000) / 10,
				    tp->tt_delack / 1000,
				    (tp->tt_delack % 1000) / 10,
				    tp->t_rcvtime / 1000,
				    (tp->t_rcvtime % 1000) / 10);
		}
		if (istcp && !Lflag && !xflag && !Tflag && !Rflag) {
			if (tp->t_state < 0 || tp->t_state >= TCP_NSTATES)
				xo_emit("{:tcp-state/%-11d}", tp->t_state);
			else {
				xo_emit("{:tcp-state/%-11s}",
				    tcpstates[tp->t_state]);
#if defined(TF_NEEDSYN) && defined(TF_NEEDFIN)
				/* Show T/TCP `hidden state' */
				if (tp->t_flags & (TF_NEEDSYN|TF_NEEDFIN))
					xo_emit("{:need-syn-or-fin/*}");
#endif /* defined(TF_NEEDSYN) && defined(TF_NEEDFIN) */
			}
		}
		if (Rflag) {
			/* XXX: is this right Alfred */
			xo_emit(" {:flow-id/%08x} {:flow-type/%5d}",
			    inp->inp_flowid,
			    inp->inp_flowtype);
		}
		if (istcp && Pflag)
			xo_emit(" {:log-id/%s}", tp->xt_logid[0] == '\0' ?
			    "-" : tp->xt_logid);
		xo_emit("\n");
		xo_close_instance("socket");
	}
	if (xig != oxig && xig->xig_gen != oxig->xig_gen) {
		if (oxig->xig_count > xig->xig_count) {
			xo_emit("Some {d:lost/%s} sockets may have been "
			    "deleted.\n", name);
		} else if (oxig->xig_count < xig->xig_count) {
			xo_emit("Some {d:created/%s} sockets may have been "
			    "created.\n", name);
		} else {
			xo_emit("Some {d:changed/%s} sockets may have been "
			    "created or deleted.\n", name);
		}
	}
	free(buf);
}

/*
 * Dump TCP statistics structure.
 */
void
tcp_stats(u_long off, const char *name, int af1 __unused, int proto __unused)
{
	struct tcpstat tcpstat;
	uint64_t tcps_states[TCP_NSTATES];

#ifdef INET6
	if (tcp_done != 0)
		return;
	else
		tcp_done = 1;
#endif

	if (fetch_stats("net.inet.tcp.stats", off, &tcpstat,
	    sizeof(tcpstat), kread_counters) != 0)
		return;

	if (fetch_stats_ro("net.inet.tcp.states", nl[N_TCPS_STATES].n_value,
	    &tcps_states, sizeof(tcps_states), kread_counters) != 0)
		return;

	xo_open_container("tcp");
	xo_emit("{T:/%s}:\n", name);

#define	p(f, m) if (tcpstat.f || sflag <= 1)				\
	xo_emit(m, (uintmax_t )tcpstat.f, plural(tcpstat.f))
#define	p1a(f, m) if (tcpstat.f || sflag <= 1)				\
	xo_emit(m, (uintmax_t )tcpstat.f)
#define	p2(f1, f2, m) if (tcpstat.f1 || tcpstat.f2 || sflag <= 1)	\
	xo_emit(m, (uintmax_t )tcpstat.f1, plural(tcpstat.f1),		\
	    (uintmax_t )tcpstat.f2, plural(tcpstat.f2))
#define	p2a(f1, f2, m) if (tcpstat.f1 || tcpstat.f2 || sflag <= 1)	\
	xo_emit(m, (uintmax_t )tcpstat.f1, plural(tcpstat.f1),		\
	    (uintmax_t )tcpstat.f2)
#define	p3(f, m) if (tcpstat.f || sflag <= 1)				\
	xo_emit(m, (uintmax_t )tcpstat.f, pluralies(tcpstat.f))

	p(tcps_sndtotal, "\t{:sent-packets/%ju} {N:/packet%s sent}\n");
	p2(tcps_sndpack,tcps_sndbyte, "\t\t{:sent-data-packets/%ju} "
	    "{N:/data packet%s} ({:sent-data-bytes/%ju} {N:/byte%s})\n");
	p2(tcps_sndrexmitpack, tcps_sndrexmitbyte, "\t\t"
	    "{:sent-retransmitted-packets/%ju} {N:/data packet%s} "
	    "({:sent-retransmitted-bytes/%ju} {N:/byte%s}) "
	    "{N:retransmitted}\n");
	p(tcps_sndrexmitbad, "\t\t"
	    "{:sent-unnecessary-retransmitted-packets/%ju} "
	    "{N:/data packet%s unnecessarily retransmitted}\n");
	p(tcps_mturesent, "\t\t{:sent-resends-by-mtu-discovery/%ju} "
	    "{N:/resend%s initiated by MTU discovery}\n");
	p2a(tcps_sndacks, tcps_delack, "\t\t{:sent-ack-only-packets/%ju} "
	    "{N:/ack-only packet%s/} ({:sent-packets-delayed/%ju} "
	    "{N:delayed})\n");
	p(tcps_sndurg, "\t\t{:sent-urg-only-packets/%ju} "
	    "{N:/URG only packet%s}\n");
	p(tcps_sndprobe, "\t\t{:sent-window-probe-packets/%ju} "
	    "{N:/window probe packet%s}\n");
	p(tcps_sndwinup, "\t\t{:sent-window-update-packets/%ju} "
	    "{N:/window update packet%s}\n");
	p(tcps_sndctrl, "\t\t{:sent-control-packets/%ju} "
	    "{N:/control packet%s}\n");
	p(tcps_rcvtotal, "\t{:received-packets/%ju} "
	    "{N:/packet%s received}\n");
	p2(tcps_rcvackpack, tcps_rcvackbyte, "\t\t"
	    "{:received-ack-packets/%ju} {N:/ack%s} "
	    "{N:(for} {:received-ack-bytes/%ju} {N:/byte%s})\n");
	p(tcps_rcvdupack, "\t\t{:received-duplicate-acks/%ju} "
	    "{N:/duplicate ack%s}\n");
	p(tcps_rcvacktoomuch, "\t\t{:received-acks-for-unsent-data/%ju} "
	    "{N:/ack%s for unsent data}\n");
	p2(tcps_rcvpack, tcps_rcvbyte, "\t\t"
	    "{:received-in-sequence-packets/%ju} {N:/packet%s} "
	    "({:received-in-sequence-bytes/%ju} {N:/byte%s}) "
	    "{N:received in-sequence}\n");
	p2(tcps_rcvduppack, tcps_rcvdupbyte, "\t\t"
	    "{:received-completely-duplicate-packets/%ju} "
	    "{N:/completely duplicate packet%s} "
	    "({:received-completely-duplicate-bytes/%ju} {N:/byte%s})\n");
	p(tcps_pawsdrop, "\t\t{:received-old-duplicate-packets/%ju} "
	    "{N:/old duplicate packet%s}\n");
	p2(tcps_rcvpartduppack, tcps_rcvpartdupbyte, "\t\t"
	    "{:received-some-duplicate-packets/%ju} "
	    "{N:/packet%s with some dup. data} "
	    "({:received-some-duplicate-bytes/%ju} {N:/byte%s duped/})\n");
	p2(tcps_rcvoopack, tcps_rcvoobyte, "\t\t{:received-out-of-order/%ju} "
	    "{N:/out-of-order packet%s} "
	    "({:received-out-of-order-bytes/%ju} {N:/byte%s})\n");
	p2(tcps_rcvpackafterwin, tcps_rcvbyteafterwin, "\t\t"
	    "{:received-after-window-packets/%ju} {N:/packet%s} "
	    "({:received-after-window-bytes/%ju} {N:/byte%s}) "
	    "{N:of data after window}\n");
	p(tcps_rcvwinprobe, "\t\t{:received-window-probes/%ju} "
	    "{N:/window probe%s}\n");
	p(tcps_rcvwinupd, "\t\t{:receive-window-update-packets/%ju} "
	    "{N:/window update packet%s}\n");
	p(tcps_rcvafterclose, "\t\t{:received-after-close-packets/%ju} "
	    "{N:/packet%s received after close}\n");
	p(tcps_rcvbadsum, "\t\t{:discard-bad-checksum/%ju} "
	    "{N:/discarded for bad checksum%s}\n");
	p(tcps_rcvbadoff, "\t\t{:discard-bad-header-offset/%ju} "
	    "{N:/discarded for bad header offset field%s}\n");
	p1a(tcps_rcvshort, "\t\t{:discard-too-short/%ju} "
	    "{N:discarded because packet too short}\n");
	p1a(tcps_rcvmemdrop, "\t\t{:discard-memory-problems/%ju} "
	    "{N:discarded due to memory problems}\n");
	p(tcps_connattempt, "\t{:connection-requests/%ju} "
	    "{N:/connection request%s}\n");
	p(tcps_accepts, "\t{:connections-accepts/%ju} "
	    "{N:/connection accept%s}\n");
	p(tcps_badsyn, "\t{:bad-connection-attempts/%ju} "
	    "{N:/bad connection attempt%s}\n");
	p(tcps_listendrop, "\t{:listen-queue-overflows/%ju} "
	    "{N:/listen queue overflow%s}\n");
	p(tcps_badrst, "\t{:ignored-in-window-resets/%ju} "
	    "{N:/ignored RSTs in the window%s}\n");
	p(tcps_connects, "\t{:connections-established/%ju} "
	    "{N:/connection%s established (including accepts)}\n");
	p(tcps_usedrtt, "\t\t{:connections-hostcache-rtt/%ju} "
	    "{N:/time%s used RTT from hostcache}\n");
	p(tcps_usedrttvar, "\t\t{:connections-hostcache-rttvar/%ju} "
	    "{N:/time%s used RTT variance from hostcache}\n");
	p(tcps_usedssthresh, "\t\t{:connections-hostcache-ssthresh/%ju} "
	    "{N:/time%s used slow-start threshold from hostcache}\n");
	p2(tcps_closed, tcps_drops, "\t{:connections-closed/%ju} "
	    "{N:/connection%s closed (including} "
	    "{:connection-drops/%ju} {N:/drop%s})\n");
	p(tcps_cachedrtt, "\t\t{:connections-updated-rtt-on-close/%ju} "
	    "{N:/connection%s updated cached RTT on close}\n");
	p(tcps_cachedrttvar, "\t\t"
	    "{:connections-updated-variance-on-close/%ju} "
	    "{N:/connection%s updated cached RTT variance on close}\n");
	p(tcps_cachedssthresh, "\t\t"
	    "{:connections-updated-ssthresh-on-close/%ju} "
	    "{N:/connection%s updated cached ssthresh on close}\n");
	p(tcps_conndrops, "\t{:embryonic-connections-dropped/%ju} "
	    "{N:/embryonic connection%s dropped}\n");
	p2(tcps_rttupdated, tcps_segstimed, "\t{:segments-updated-rtt/%ju} "
	    "{N:/segment%s updated rtt (of} "
	    "{:segment-update-attempts/%ju} {N:/attempt%s})\n");
	p(tcps_rexmttimeo, "\t{:retransmit-timeouts/%ju} "
	    "{N:/retransmit timeout%s}\n");
	p(tcps_timeoutdrop, "\t\t"
	    "{:connections-dropped-by-retransmit-timeout/%ju} "
	    "{N:/connection%s dropped by rexmit timeout}\n");
	p(tcps_persisttimeo, "\t{:persist-timeout/%ju} "
	    "{N:/persist timeout%s}\n");
	p(tcps_persistdrop, "\t\t"
	    "{:connections-dropped-by-persist-timeout/%ju} "
	    "{N:/connection%s dropped by persist timeout}\n");
	p(tcps_finwait2_drops, "\t"
	    "{:connections-dropped-by-finwait2-timeout/%ju} "
	    "{N:/Connection%s (fin_wait_2) dropped because of timeout}\n");
	p(tcps_keeptimeo, "\t{:keepalive-timeout/%ju} "
	    "{N:/keepalive timeout%s}\n");
	p(tcps_keepprobe, "\t\t{:keepalive-probes/%ju} "
	    "{N:/keepalive probe%s sent}\n");
	p(tcps_keepdrops, "\t\t{:connections-dropped-by-keepalives/%ju} "
	    "{N:/connection%s dropped by keepalive}\n");
	p(tcps_predack, "\t{:ack-header-predictions/%ju} "
	    "{N:/correct ACK header prediction%s}\n");
	p(tcps_preddat, "\t{:data-packet-header-predictions/%ju} "
	    "{N:/correct data packet header prediction%s}\n");

	xo_open_container("syncache");

	p3(tcps_sc_added, "\t{:entries-added/%ju} "
	    "{N:/syncache entr%s added}\n");
	p1a(tcps_sc_retransmitted, "\t\t{:retransmitted/%ju} "
	    "{N:/retransmitted}\n");
	p1a(tcps_sc_dupsyn, "\t\t{:duplicates/%ju} {N:/dupsyn}\n");
	p1a(tcps_sc_dropped, "\t\t{:dropped/%ju} {N:/dropped}\n");
	p1a(tcps_sc_completed, "\t\t{:completed/%ju} {N:/completed}\n");
	p1a(tcps_sc_bucketoverflow, "\t\t{:bucket-overflow/%ju} "
	    "{N:/bucket overflow}\n");
	p1a(tcps_sc_cacheoverflow, "\t\t{:cache-overflow/%ju} "
	    "{N:/cache overflow}\n");
	p1a(tcps_sc_reset, "\t\t{:reset/%ju} {N:/reset}\n");
	p1a(tcps_sc_stale, "\t\t{:stale/%ju} {N:/stale}\n");
	p1a(tcps_sc_aborted, "\t\t{:aborted/%ju} {N:/aborted}\n");
	p1a(tcps_sc_badack, "\t\t{:bad-ack/%ju} {N:/badack}\n");
	p1a(tcps_sc_unreach, "\t\t{:unreachable/%ju} {N:/unreach}\n");
	p(tcps_sc_zonefail, "\t\t{:zone-failures/%ju} {N:/zone failure%s}\n");
	p(tcps_sc_sendcookie, "\t{:sent-cookies/%ju} {N:/cookie%s sent}\n");
	p(tcps_sc_recvcookie, "\t{:receivd-cookies/%ju} "
	    "{N:/cookie%s received}\n");

	xo_close_container("syncache");

	xo_open_container("hostcache");

	p3(tcps_hc_added, "\t{:entries-added/%ju} "
	    "{N:/hostcache entr%s added}\n");
	p1a(tcps_hc_bucketoverflow, "\t\t{:buffer-overflows/%ju} "
	    "{N:/bucket overflow}\n");

	xo_close_container("hostcache");

	xo_open_container("sack");

	p(tcps_sack_recovery_episode, "\t{:recovery-episodes/%ju} "
	    "{N:/SACK recovery episode%s}\n");
 	p(tcps_sack_rexmits, "\t{:segment-retransmits/%ju} "
	    "{N:/segment rexmit%s in SACK recovery episodes}\n");
 	p(tcps_sack_rexmit_bytes, "\t{:byte-retransmits/%ju} "
	    "{N:/byte rexmit%s in SACK recovery episodes}\n");
 	p(tcps_sack_rcv_blocks, "\t{:received-blocks/%ju} "
	    "{N:/SACK option%s (SACK blocks) received}\n");
	p(tcps_sack_send_blocks, "\t{:sent-option-blocks/%ju} "
	    "{N:/SACK option%s (SACK blocks) sent}\n");
	p1a(tcps_sack_sboverflow, "\t{:scoreboard-overflows/%ju} "
	    "{N:/SACK scoreboard overflow}\n");

	xo_close_container("sack");
	xo_open_container("ecn");

	p(tcps_ecn_ce, "\t{:ce-packets/%ju} "
	    "{N:/packet%s with ECN CE bit set}\n");
	p(tcps_ecn_ect0, "\t{:ect0-packets/%ju} "
	    "{N:/packet%s with ECN ECT(0) bit set}\n");
	p(tcps_ecn_ect1, "\t{:ect1-packets/%ju} "
	    "{N:/packet%s with ECN ECT(1) bit set}\n");
	p(tcps_ecn_shs, "\t{:handshakes/%ju} "
	    "{N:/successful ECN handshake%s}\n");
	p(tcps_ecn_rcwnd, "\t{:congestion-reductions/%ju} "
	    "{N:/time%s ECN reduced the congestion window}\n");

	xo_close_container("ecn");
	xo_open_container("tcp-signature");
	p(tcps_sig_rcvgoodsig, "\t{:received-good-signature/%ju} "
	    "{N:/packet%s with matching signature received}\n");
	p(tcps_sig_rcvbadsig, "\t{:received-bad-signature/%ju} "
	    "{N:/packet%s with bad signature received}\n");
	p(tcps_sig_err_buildsig, "\t{:failed-make-signature/%ju} "
	    "{N:/time%s failed to make signature due to no SA}\n");
	p(tcps_sig_err_sigopt, "\t{:no-signature-expected/%ju} "
	    "{N:/time%s unexpected signature received}\n");
	p(tcps_sig_err_nosigopt, "\t{:no-signature-provided/%ju} "
	    "{N:/time%s no signature provided by segment}\n");

	xo_close_container("tcp-signature");
	xo_open_container("pmtud");

	p(tcps_pmtud_blackhole_activated, "\t{:pmtud-activated/%ju} "
	    "{N:/Path MTU discovery black hole detection activation%s}\n");
	p(tcps_pmtud_blackhole_activated_min_mss,
	    "\t{:pmtud-activated-min-mss/%ju} "
	    "{N:/Path MTU discovery black hole detection min MSS activation%s}\n");
	p(tcps_pmtud_blackhole_failed, "\t{:pmtud-failed/%ju} "
	    "{N:/Path MTU discovery black hole detection failure%s}\n");
 #undef p
 #undef p1a
 #undef p2
 #undef p2a
 #undef p3
	xo_close_container("pmtud");


	xo_open_container("TCP connection count by state");
	xo_emit("{T:/TCP connection count by state}:\n");
	for (int i = 0; i < TCP_NSTATES; i++) {
		/*
		 * XXXGL: is there a way in libxo to use %s
		 * in the "content string" of a format
		 * string? I failed to do that, that's why
		 * a temporary buffer is used to construct
		 * format string for xo_emit().
		 */
		char fmtbuf[80];

		if (sflag > 1 && tcps_states[i] == 0)
			continue;
		snprintf(fmtbuf, sizeof(fmtbuf), "\t{:%s/%%ju} "
                    "{Np:/connection ,connections} in %s state\n",
		    tcpstates[i], tcpstates[i]);
		xo_emit(fmtbuf, (uintmax_t )tcps_states[i]);
	}
	xo_close_container("TCP connection count by state");

	xo_close_container("tcp");
}

/*
 * Dump UDP statistics structure.
 */
void
udp_stats(u_long off, const char *name, int af1 __unused, int proto __unused)
{
	struct udpstat udpstat;
	uint64_t delivered;

#ifdef INET6
	if (udp_done != 0)
		return;
	else
		udp_done = 1;
#endif

	if (fetch_stats("net.inet.udp.stats", off, &udpstat,
	    sizeof(udpstat), kread_counters) != 0)
		return;

	xo_open_container("udp");
	xo_emit("{T:/%s}:\n", name);

#define	p(f, m) if (udpstat.f || sflag <= 1) \
	xo_emit("\t" m, (uintmax_t)udpstat.f, plural(udpstat.f))
#define	p1a(f, m) if (udpstat.f || sflag <= 1) \
	xo_emit("\t" m, (uintmax_t)udpstat.f)

	p(udps_ipackets, "{:received-datagrams/%ju} "
	    "{N:/datagram%s received}\n");
	p1a(udps_hdrops, "{:dropped-incomplete-headers/%ju} "
	    "{N:/with incomplete header}\n");
	p1a(udps_badlen, "{:dropped-bad-data-length/%ju} "
	    "{N:/with bad data length field}\n");
	p1a(udps_badsum, "{:dropped-bad-checksum/%ju} "
	    "{N:/with bad checksum}\n");
	p1a(udps_nosum, "{:dropped-no-checksum/%ju} "
	    "{N:/with no checksum}\n");
	p1a(udps_noport, "{:dropped-no-socket/%ju} "
	    "{N:/dropped due to no socket}\n");
	p(udps_noportbcast, "{:dropped-broadcast-multicast/%ju} "
	    "{N:/broadcast\\/multicast datagram%s undelivered}\n");
	p1a(udps_fullsock, "{:dropped-full-socket-buffer/%ju} "
	    "{N:/dropped due to full socket buffers}\n");
	p1a(udpps_pcbhashmiss, "{:not-for-hashed-pcb/%ju} "
	    "{N:/not for hashed pcb}\n");
	delivered = udpstat.udps_ipackets -
		    udpstat.udps_hdrops -
		    udpstat.udps_badlen -
		    udpstat.udps_badsum -
		    udpstat.udps_noport -
		    udpstat.udps_noportbcast -
		    udpstat.udps_fullsock;
	if (delivered || sflag <= 1)
		xo_emit("\t{:delivered-packets/%ju} {N:/delivered}\n",
		    (uint64_t)delivered);
	p(udps_opackets, "{:output-packets/%ju} {N:/datagram%s output}\n");
	/* the next statistic is cumulative in udps_noportbcast */
	p(udps_filtermcast, "{:multicast-source-filter-matches/%ju} "
	    "{N:/time%s multicast source filter matched}\n");
#undef p
#undef p1a
	xo_close_container("udp");
}

/*
 * Dump CARP statistics structure.
 */
void
carp_stats(u_long off, const char *name, int af1 __unused, int proto __unused)
{
	struct carpstats carpstat;

	if (fetch_stats("net.inet.carp.stats", off, &carpstat,
	    sizeof(carpstat), kread_counters) != 0)
		return;

	xo_open_container(name);
	xo_emit("{T:/%s}:\n", name);

#define	p(f, m) if (carpstat.f || sflag <= 1) \
	xo_emit(m, (uintmax_t)carpstat.f, plural(carpstat.f))
#define	p2(f, m) if (carpstat.f || sflag <= 1) \
	xo_emit(m, (uintmax_t)carpstat.f)

	p(carps_ipackets, "\t{:received-inet-packets/%ju} "
	    "{N:/packet%s received (IPv4)}\n");
	p(carps_ipackets6, "\t{:received-inet6-packets/%ju} "
	    "{N:/packet%s received (IPv6)}\n");
	p(carps_badttl, "\t\t{:dropped-wrong-ttl/%ju} "
	    "{N:/packet%s discarded for wrong TTL}\n");
	p(carps_hdrops, "\t\t{:dropped-short-header/%ju} "
	    "{N:/packet%s shorter than header}\n");
	p(carps_badsum, "\t\t{:dropped-bad-checksum/%ju} "
	    "{N:/discarded for bad checksum%s}\n");
	p(carps_badver,	"\t\t{:dropped-bad-version/%ju} "
	    "{N:/discarded packet%s with a bad version}\n");
	p2(carps_badlen, "\t\t{:dropped-short-packet/%ju} "
	    "{N:/discarded because packet too short}\n");
	p2(carps_badauth, "\t\t{:dropped-bad-authentication/%ju} "
	    "{N:/discarded for bad authentication}\n");
	p2(carps_badvhid, "\t\t{:dropped-bad-vhid/%ju} "
	    "{N:/discarded for bad vhid}\n");
	p2(carps_badaddrs, "\t\t{:dropped-bad-address-list/%ju} "
	    "{N:/discarded because of a bad address list}\n");
	p(carps_opackets, "\t{:sent-inet-packets/%ju} "
	    "{N:/packet%s sent (IPv4)}\n");
	p(carps_opackets6, "\t{:sent-inet6-packets/%ju} "
	    "{N:/packet%s sent (IPv6)}\n");
	p2(carps_onomem, "\t\t{:send-failed-memory-error/%ju} "
	    "{N:/send failed due to mbuf memory error}\n");
#if notyet
	p(carps_ostates, "\t\t{:send-state-updates/%s} "
	    "{N:/state update%s sent}\n");
#endif
#undef p
#undef p2
	xo_close_container(name);
}

/*
 * Dump IP statistics structure.
 */
void
ip_stats(u_long off, const char *name, int af1 __unused, int proto __unused)
{
	struct ipstat ipstat;

	if (fetch_stats("net.inet.ip.stats", off, &ipstat,
	    sizeof(ipstat), kread_counters) != 0)
		return;

	xo_open_container(name);
	xo_emit("{T:/%s}:\n", name);

#define	p(f, m) if (ipstat.f || sflag <= 1) \
	xo_emit(m, (uintmax_t )ipstat.f, plural(ipstat.f))
#define	p1a(f, m) if (ipstat.f || sflag <= 1) \
	xo_emit(m, (uintmax_t )ipstat.f)

	p(ips_total, "\t{:received-packets/%ju} "
	    "{N:/total packet%s received}\n");
	p(ips_badsum, "\t{:dropped-bad-checksum/%ju} "
	    "{N:/bad header checksum%s}\n");
	p1a(ips_toosmall, "\t{:dropped-below-minimum-size/%ju} "
	    "{N:/with size smaller than minimum}\n");
	p1a(ips_tooshort, "\t{:dropped-short-packets/%ju} "
	    "{N:/with data size < data length}\n");
	p1a(ips_toolong, "\t{:dropped-too-long/%ju} "
	    "{N:/with ip length > max ip packet size}\n");
	p1a(ips_badhlen, "\t{:dropped-short-header-length/%ju} "
	    "{N:/with header length < data size}\n");
	p1a(ips_badlen, "\t{:dropped-short-data/%ju} "
	    "{N:/with data length < header length}\n");
	p1a(ips_badoptions, "\t{:dropped-bad-options/%ju} "
	    "{N:/with bad options}\n");
	p1a(ips_badvers, "\t{:dropped-bad-version/%ju} "
	    "{N:/with incorrect version number}\n");
	p(ips_fragments, "\t{:received-fragments/%ju} "
	    "{N:/fragment%s received}\n");
	p(ips_fragdropped, "\t{:dropped-fragments/%ju} "
	    "{N:/fragment%s dropped (dup or out of space)}\n");
	p(ips_fragtimeout, "\t{:dropped-fragments-after-timeout/%ju} "
	    "{N:/fragment%s dropped after timeout}\n");
	p(ips_reassembled, "\t{:reassembled-packets/%ju} "
	    "{N:/packet%s reassembled ok}\n");
	p(ips_delivered, "\t{:received-local-packets/%ju} "
	    "{N:/packet%s for this host}\n");
	p(ips_noproto, "\t{:dropped-unknown-protocol/%ju} "
	    "{N:/packet%s for unknown\\/unsupported protocol}\n");
	p(ips_forward, "\t{:forwarded-packets/%ju} "
	    "{N:/packet%s forwarded}");
	p(ips_fastforward, " ({:fast-forwarded-packets/%ju} "
	    "{N:/packet%s fast forwarded})");
	if (ipstat.ips_forward || sflag <= 1)
		xo_emit("\n");
	p(ips_cantforward, "\t{:packets-cannot-forward/%ju} "
	    "{N:/packet%s not forwardable}\n");
	p(ips_notmember, "\t{:received-unknown-multicast-group/%ju} "
	    "{N:/packet%s received for unknown multicast group}\n");
	p(ips_redirectsent, "\t{:redirects-sent/%ju} "
	    "{N:/redirect%s sent}\n");
	p(ips_localout, "\t{:sent-packets/%ju} "
	    "{N:/packet%s sent from this host}\n");
	p(ips_rawout, "\t{:send-packets-fabricated-header/%ju} "
	    "{N:/packet%s sent with fabricated ip header}\n");
	p(ips_odropped, "\t{:discard-no-mbufs/%ju} "
	    "{N:/output packet%s dropped due to no bufs, etc.}\n");
	p(ips_noroute, "\t{:discard-no-route/%ju} "
	    "{N:/output packet%s discarded due to no route}\n");
	p(ips_fragmented, "\t{:sent-fragments/%ju} "
	    "{N:/output datagram%s fragmented}\n");
	p(ips_ofragments, "\t{:fragments-created/%ju} "
	    "{N:/fragment%s created}\n");
	p(ips_cantfrag, "\t{:discard-cannot-fragment/%ju} "
	    "{N:/datagram%s that can't be fragmented}\n");
	p(ips_nogif, "\t{:discard-tunnel-no-gif/%ju} "
	    "{N:/tunneling packet%s that can't find gif}\n");
	p(ips_badaddr, "\t{:discard-bad-address/%ju} "
	    "{N:/datagram%s with bad address in header}\n");
#undef p
#undef p1a
	xo_close_container(name);
}

/*
 * Dump ARP statistics structure.
 */
void
arp_stats(u_long off, const char *name, int af1 __unused, int proto __unused)
{
	struct arpstat arpstat;

	if (fetch_stats("net.link.ether.arp.stats", off, &arpstat,
	    sizeof(arpstat), kread_counters) != 0)
		return;

	xo_open_container(name);
	xo_emit("{T:/%s}:\n", name);

#define	p(f, m) if (arpstat.f || sflag <= 1) \
	xo_emit("\t" m, (uintmax_t)arpstat.f, plural(arpstat.f))
#define	p2(f, m) if (arpstat.f || sflag <= 1) \
	xo_emit("\t" m, (uintmax_t)arpstat.f, pluralies(arpstat.f))

	p(txrequests, "{:sent-requests/%ju} {N:/ARP request%s sent}\n");
	p(txerrors, "{:sent-failures/%ju} {N:/ARP request%s failed to sent}\n");
	p2(txreplies, "{:sent-replies/%ju} {N:/ARP repl%s sent}\n");
	p(rxrequests, "{:received-requests/%ju} "
	    "{N:/ARP request%s received}\n");
	p2(rxreplies, "{:received-replies/%ju} "
	    "{N:/ARP repl%s received}\n");
	p(received, "{:received-packers/%ju} "
	    "{N:/ARP packet%s received}\n");
	p(dropped, "{:dropped-no-entry/%ju} "
	    "{N:/total packet%s dropped due to no ARP entry}\n");
	p(timeouts, "{:entries-timeout/%ju} "
	    "{N:/ARP entry%s timed out}\n");
	p(dupips, "{:dropped-duplicate-address/%ju} "
	    "{N:/Duplicate IP%s seen}\n");
#undef p
#undef p2
	xo_close_container(name);
}



static	const char *icmpnames[ICMP_MAXTYPE + 1] = {
	"echo reply",			/* RFC 792 */
	"#1",
	"#2",
	"destination unreachable",	/* RFC 792 */
	"source quench",		/* RFC 792 */
	"routing redirect",		/* RFC 792 */
	"#6",
	"#7",
	"echo",				/* RFC 792 */
	"router advertisement",		/* RFC 1256 */
	"router solicitation",		/* RFC 1256 */
	"time exceeded",		/* RFC 792 */
	"parameter problem",		/* RFC 792 */
	"time stamp",			/* RFC 792 */
	"time stamp reply",		/* RFC 792 */
	"information request",		/* RFC 792 */
	"information request reply",	/* RFC 792 */
	"address mask request",		/* RFC 950 */
	"address mask reply",		/* RFC 950 */
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
	"icmp traceroute",		/* RFC 1393 */
	"datagram conversion error",	/* RFC 1475 */
	"mobile host redirect",
	"IPv6 where-are-you",
	"IPv6 i-am-here",
	"mobile registration req",
	"mobile registration reply",
	"domain name request",		/* RFC 1788 */
	"domain name reply",		/* RFC 1788 */
	"icmp SKIP",
	"icmp photuris",		/* RFC 2521 */
};

/*
 * Dump ICMP statistics.
 */
void
icmp_stats(u_long off, const char *name, int af1 __unused, int proto __unused)
{
	struct icmpstat icmpstat;
	size_t len;
	int i, first;

	if (fetch_stats("net.inet.icmp.stats", off, &icmpstat,
	    sizeof(icmpstat), kread_counters) != 0)
		return;

	xo_open_container(name);
	xo_emit("{T:/%s}:\n", name);

#define	p(f, m) if (icmpstat.f || sflag <= 1) \
	xo_emit(m, icmpstat.f, plural(icmpstat.f))
#define	p1a(f, m) if (icmpstat.f || sflag <= 1) \
	xo_emit(m, icmpstat.f)
#define	p2(f, m) if (icmpstat.f || sflag <= 1) \
	xo_emit(m, icmpstat.f, plurales(icmpstat.f))

	p(icps_error, "\t{:icmp-calls/%lu} "
	    "{N:/call%s to icmp_error}\n");
	p(icps_oldicmp, "\t{:errors-not-from-message/%lu} "
	    "{N:/error%s not generated in response to an icmp message}\n");

	for (first = 1, i = 0; i < ICMP_MAXTYPE + 1; i++) {
		if (icmpstat.icps_outhist[i] != 0) {
			if (first) {
				xo_open_list("output-histogram");
				xo_emit("\tOutput histogram:\n");
				first = 0;
			}
			xo_open_instance("output-histogram");
			if (icmpnames[i] != NULL)
				xo_emit("\t\t{k:name/%s}: {:count/%lu}\n",
				    icmpnames[i], icmpstat.icps_outhist[i]);
			else
				xo_emit("\t\tunknown ICMP #{k:name/%d}: "
				    "{:count/%lu}\n",
				    i, icmpstat.icps_outhist[i]);
			xo_close_instance("output-histogram");
		}
	}
	if (!first)
		xo_close_list("output-histogram");

	p(icps_badcode, "\t{:dropped-bad-code/%lu} "
	    "{N:/message%s with bad code fields}\n");
	p(icps_tooshort, "\t{:dropped-too-short/%lu} "
	    "{N:/message%s less than the minimum length}\n");
	p(icps_checksum, "\t{:dropped-bad-checksum/%lu} "
	    "{N:/message%s with bad checksum}\n");
	p(icps_badlen, "\t{:dropped-bad-length/%lu} "
	    "{N:/message%s with bad length}\n");
	p1a(icps_bmcastecho, "\t{:dropped-multicast-echo/%lu} "
	    "{N:/multicast echo requests ignored}\n");
	p1a(icps_bmcasttstamp, "\t{:dropped-multicast-timestamp/%lu} "
	    "{N:/multicast timestamp requests ignored}\n");

	for (first = 1, i = 0; i < ICMP_MAXTYPE + 1; i++) {
		if (icmpstat.icps_inhist[i] != 0) {
			if (first) {
				xo_open_list("input-histogram");
				xo_emit("\tInput histogram:\n");
				first = 0;
			}
			xo_open_instance("input-histogram");
			if (icmpnames[i] != NULL)
				xo_emit("\t\t{k:name/%s}: {:count/%lu}\n",
					icmpnames[i],
					icmpstat.icps_inhist[i]);
			else
				xo_emit(
			"\t\tunknown ICMP #{k:name/%d}: {:count/%lu}\n",
					i, icmpstat.icps_inhist[i]);
			xo_close_instance("input-histogram");
		}
	}
	if (!first)
		xo_close_list("input-histogram");

	p(icps_reflect, "\t{:sent-packets/%lu} "
	    "{N:/message response%s generated}\n");
	p2(icps_badaddr, "\t{:discard-invalid-return-address/%lu} "
	    "{N:/invalid return address%s}\n");
	p(icps_noroute, "\t{:discard-no-route/%lu} "
	    "{N:/no return route%s}\n");
#undef p
#undef p1a
#undef p2
	if (live) {
		len = sizeof i;
		if (sysctlbyname("net.inet.icmp.maskrepl", &i, &len, NULL, 0) <
		    0)
			return;
		xo_emit("\tICMP address mask responses are "
		    "{q:icmp-address-responses/%sabled}\n", i ? "en" : "dis");
	}

	xo_close_container(name);
}

/*
 * Dump IGMP statistics structure.
 */
void
igmp_stats(u_long off, const char *name, int af1 __unused, int proto __unused)
{
	struct igmpstat igmpstat;

	if (fetch_stats("net.inet.igmp.stats", 0, &igmpstat,
	    sizeof(igmpstat), kread) != 0)
		return;

	if (igmpstat.igps_version != IGPS_VERSION_3) {
		xo_warnx("%s: version mismatch (%d != %d)", __func__,
		    igmpstat.igps_version, IGPS_VERSION_3);
	}
	if (igmpstat.igps_len != IGPS_VERSION3_LEN) {
		xo_warnx("%s: size mismatch (%d != %d)", __func__,
		    igmpstat.igps_len, IGPS_VERSION3_LEN);
	}

	xo_open_container(name);
	xo_emit("{T:/%s}:\n", name);

#define	p64(f, m) if (igmpstat.f || sflag <= 1) \
	xo_emit(m, (uintmax_t) igmpstat.f, plural(igmpstat.f))
#define	py64(f, m) if (igmpstat.f || sflag <= 1) \
	xo_emit(m, (uintmax_t) igmpstat.f, pluralies(igmpstat.f))

	p64(igps_rcv_total, "\t{:received-messages/%ju} "
	    "{N:/message%s received}\n");
	p64(igps_rcv_tooshort, "\t{:dropped-too-short/%ju} "
	    "{N:/message%s received with too few bytes}\n");
	p64(igps_rcv_badttl, "\t{:dropped-wrong-ttl/%ju} "
	    "{N:/message%s received with wrong TTL}\n");
	p64(igps_rcv_badsum, "\t{:dropped-bad-checksum/%ju} "
	    "{N:/message%s received with bad checksum}\n");
	py64(igps_rcv_v1v2_queries, "\t{:received-membership-queries/%ju} "
	    "{N:/V1\\/V2 membership quer%s received}\n");
	py64(igps_rcv_v3_queries, "\t{:received-v3-membership-queries/%ju} "
	    "{N:/V3 membership quer%s received}\n");
	py64(igps_rcv_badqueries, "\t{:dropped-membership-queries/%ju} "
	    "{N:/membership quer%s received with invalid field(s)}\n");
	py64(igps_rcv_gen_queries, "\t{:received-general-queries/%ju} "
	    "{N:/general quer%s received}\n");
	py64(igps_rcv_group_queries, "\t{:received-group-queries/%ju} "
	    "{N:/group quer%s received}\n");
	py64(igps_rcv_gsr_queries, "\t{:received-group-source-queries/%ju} "
	    "{N:/group-source quer%s received}\n");
	py64(igps_drop_gsr_queries, "\t{:dropped-group-source-queries/%ju} "
	    "{N:/group-source quer%s dropped}\n");
	p64(igps_rcv_reports, "\t{:received-membership-requests/%ju} "
	    "{N:/membership report%s received}\n");
	p64(igps_rcv_badreports, "\t{:dropped-membership-reports/%ju} "
	    "{N:/membership report%s received with invalid field(s)}\n");
	p64(igps_rcv_ourreports, "\t"
	    "{:received-membership-reports-matching/%ju} "
	    "{N:/membership report%s received for groups to which we belong}"
	    "\n");
	p64(igps_rcv_nora, "\t{:received-v3-reports-no-router-alert/%ju} "
	    "{N:/V3 report%s received without Router Alert}\n");
	p64(igps_snd_reports, "\t{:sent-membership-reports/%ju} "
	    "{N:/membership report%s sent}\n");
#undef p64
#undef py64
	xo_close_container(name);
}

/*
 * Dump PIM statistics structure.
 */
void
pim_stats(u_long off __unused, const char *name, int af1 __unused,
    int proto __unused)
{
	struct pimstat pimstat;

	if (fetch_stats("net.inet.pim.stats", off, &pimstat,
	    sizeof(pimstat), kread_counters) != 0)
		return;

	xo_open_container(name);
	xo_emit("{T:/%s}:\n", name);

#define	p(f, m) if (pimstat.f || sflag <= 1) \
	xo_emit(m, (uintmax_t)pimstat.f, plural(pimstat.f))
#define	py(f, m) if (pimstat.f || sflag <= 1) \
	xo_emit(m, (uintmax_t)pimstat.f, pimstat.f != 1 ? "ies" : "y")

	p(pims_rcv_total_msgs, "\t{:received-messages/%ju} "
	    "{N:/message%s received}\n");
	p(pims_rcv_total_bytes, "\t{:received-bytes/%ju} "
	    "{N:/byte%s received}\n");
	p(pims_rcv_tooshort, "\t{:dropped-too-short/%ju} "
	    "{N:/message%s received with too few bytes}\n");
	p(pims_rcv_badsum, "\t{:dropped-bad-checksum/%ju} "
	    "{N:/message%s received with bad checksum}\n");
	p(pims_rcv_badversion, "\t{:dropped-bad-version/%ju} "
	    "{N:/message%s received with bad version}\n");
	p(pims_rcv_registers_msgs, "\t{:received-data-register-messages/%ju} "
	    "{N:/data register message%s received}\n");
	p(pims_rcv_registers_bytes, "\t{:received-data-register-bytes/%ju} "
	    "{N:/data register byte%s received}\n");
	p(pims_rcv_registers_wrongiif, "\t"
	    "{:received-data-register-wrong-interface/%ju} "
	    "{N:/data register message%s received on wrong iif}\n");
	p(pims_rcv_badregisters, "\t{:received-bad-registers/%ju} "
	    "{N:/bad register%s received}\n");
	p(pims_snd_registers_msgs, "\t{:sent-data-register-messages/%ju} "
	    "{N:/data register message%s sent}\n");
	p(pims_snd_registers_bytes, "\t{:sent-data-register-bytes/%ju} "
	    "{N:/data register byte%s sent}\n");
#undef p
#undef py
	xo_close_container(name);
}

#ifdef INET
/*
 * Pretty print an Internet address (net address + port).
 */
static void
inetprint(const char *container, struct in_addr *in, int port,
    const char *proto, int num_port, const int af1)
{
	struct servent *sp = 0;
	char line[80], *cp;
	int width;
	size_t alen, plen;

	if (container)
		xo_open_container(container);

	if (Wflag)
	    snprintf(line, sizeof(line), "%s.", inetname(in));
	else
	    snprintf(line, sizeof(line), "%.*s.",
		(Aflag && !num_port) ? 12 : 16, inetname(in));
	alen = strlen(line);
	cp = line + alen;
	if (!num_port && port)
		sp = getservbyport((int)port, proto);
	if (sp || port == 0)
		snprintf(cp, sizeof(line) - alen,
		    "%.15s ", sp ? sp->s_name : "*");
	else
		snprintf(cp, sizeof(line) - alen,
		    "%d ", ntohs((u_short)port));
	width = (Aflag && !Wflag) ? 18 :
		((!Wflag || af1 == AF_INET) ? 22 : 45);
	if (Wflag)
		xo_emit("{d:target/%-*s} ", width, line);
	else
		xo_emit("{d:target/%-*.*s} ", width, width, line);

	plen = strlen(cp) - 1;
	alen--;
	xo_emit("{e:address/%*.*s}{e:port/%*.*s}", alen, alen, line, plen,
	    plen, cp);

	if (container)
		xo_close_container(container);
}

/*
 * Construct an Internet address representation.
 * If numeric_addr has been supplied, give
 * numeric value, otherwise try for symbolic name.
 */
char *
inetname(struct in_addr *inp)
{
	char *cp;
	static char line[MAXHOSTNAMELEN];
	struct hostent *hp;
	struct netent *np;

	cp = 0;
	if (!numeric_addr && inp->s_addr != INADDR_ANY) {
		int net = inet_netof(*inp);
		int lna = inet_lnaof(*inp);

		if (lna == INADDR_ANY) {
			np = getnetbyaddr(net, AF_INET);
			if (np)
				cp = np->n_name;
		}
		if (cp == NULL) {
			hp = gethostbyaddr((char *)inp, sizeof (*inp), AF_INET);
			if (hp) {
				cp = hp->h_name;
				trimdomain(cp, strlen(cp));
			}
		}
	}
	if (inp->s_addr == INADDR_ANY)
		strcpy(line, "*");
	else if (cp) {
		strlcpy(line, cp, sizeof(line));
	} else {
		inp->s_addr = ntohl(inp->s_addr);
#define	C(x)	((u_int)((x) & 0xff))
		snprintf(line, sizeof(line), "%u.%u.%u.%u",
		    C(inp->s_addr >> 24), C(inp->s_addr >> 16),
		    C(inp->s_addr >> 8), C(inp->s_addr));
	}
	return (line);
}
#endif
