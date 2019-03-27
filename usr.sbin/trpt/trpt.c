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
static const char copyright[] =
"@(#) Copyright (c) 1983, 1988, 1993\n\
	The Regents of the University of California.  All rights reserved.\n";
#endif /* not lint */

#ifndef lint
static char sccsid[] = "@(#)trpt.c	8.1 (Berkeley) 6/6/93";
#endif /* not lint */
#endif
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/queue.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#define PRUREQUESTS
#include <sys/protosw.h>
#include <sys/file.h>
#include <sys/time.h>

#include <net/route.h>
#include <net/if.h>

#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#ifdef INET6
#include <netinet/ip6.h>
#endif
#include <netinet/ip_var.h>
#include <netinet/tcp.h>
#define TCPSTATES
#include <netinet/tcp_fsm.h>
#include <netinet/tcp_seq.h>
#define	TCPTIMERS
#include <netinet/tcp_timer.h>
#define	_WANT_TCPCB
#include <netinet/tcp_var.h>
#include <netinet/tcpip.h>
#define	TANAMES
#include <netinet/tcp_debug.h>

#include <arpa/inet.h>

#include <err.h>
#include <nlist.h>
#include <paths.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static struct nlist nl[3];
#define	N_TCP_DEBUG	0
#define	N_TCP_DEBX	1

static caddr_t tcp_pcbs[TCP_NDEBUG];
static n_time ntime;
static int aflag, kflag, memf, follow, sflag;

static void dotrace(caddr_t);
static void klseek(int, off_t, int);
static int numeric(const void *, const void *);
static void tcp_trace(short, short, struct tcpcb *, int, void *, struct tcphdr *, int);
static void usage(void);

int
main(int argc, char **argv)
{
	int ch, i, jflag, npcbs;
	const char *core, *syst;

	nl[0].n_name = strdup("_tcp_debug");
	nl[1].n_name = strdup("_tcp_debx");

	jflag = npcbs = 0;
	while ((ch = getopt(argc, argv, "afjp:s")) != -1)
		switch (ch) {
		case 'a':
			++aflag;
			break;
		case 'f':
			++follow;
			setlinebuf(stdout);
			break;
		case 'j':
			++jflag;
			break;
		case 'p':
			if (npcbs >= TCP_NDEBUG)
				errx(1, "too many pcb's specified");
			(void)sscanf(optarg, "%x", (int *)&tcp_pcbs[npcbs++]);
			break;
		case 's':
			++sflag;
			break;
		case '?':
		default:
			usage();
		}
	argc -= optind;
	argv += optind;

	core = _PATH_KMEM;
	if (argc > 0) {
		syst = *argv;
		argc--, argv++;
		if (argc > 0) {
			core = *argv;
			argc--, argv++;
			++kflag;
		}
		/*
		 * Discard setgid privileges if not the running kernel so that
		 * bad guys can't print interesting stuff from kernel memory.
		 */
		if (setgid(getgid()) != 0)
			err(1, "setgid");
	} else
		syst = getbootfile();

	if (nlist(syst, nl) < 0 || !nl[0].n_value)
		errx(1, "%s: no namelist", syst);
	if ((memf = open(core, O_RDONLY)) < 0)
		err(2, "%s", core);
	if (setgid(getgid()) != 0)
		err(1, "setgid");
	if (kflag)
		errx(1, "can't do core files yet");
	(void)klseek(memf, (off_t)nl[N_TCP_DEBX].n_value, L_SET);
	if (read(memf, (char *)&tcp_debx, sizeof(tcp_debx)) !=
	    sizeof(tcp_debx))
		err(3, "tcp_debx");
	(void)klseek(memf, (off_t)nl[N_TCP_DEBUG].n_value, L_SET);
	if (read(memf, (char *)tcp_debug, sizeof(tcp_debug)) !=
	    sizeof(tcp_debug))
		err(3, "tcp_debug");
	/*
	 * If no control blocks have been specified, figure
	 * out how many distinct one we have and summarize
	 * them in tcp_pcbs for sorting the trace records
	 * below.
	 */
	if (!npcbs) {
		for (i = 0; i < TCP_NDEBUG; i++) {
			struct tcp_debug *td = &tcp_debug[i];
			int j;

			if (td->td_tcb == 0)
				continue;
			for (j = 0; j < npcbs; j++)
				if (tcp_pcbs[j] == td->td_tcb)
					break;
			if (j >= npcbs)
				tcp_pcbs[npcbs++] = td->td_tcb;
		}
		if (!npcbs)
			exit(0);
	}
	qsort(tcp_pcbs, npcbs, sizeof(caddr_t), numeric);
	if (jflag) {
		for (i = 0;;) {
			printf("%p", (void *)tcp_pcbs[i]);
			if (++i == npcbs)
				break;
			fputs(", ", stdout);
		}
		putchar('\n');
	} else
		for (i = 0; i < npcbs; i++) {
			printf("\n%p:\n", tcp_pcbs[i]);
			dotrace(tcp_pcbs[i]);
		}
	exit(0);
}

static void
usage(void)
{
	(void)fprintf(stderr,
	    "usage: trpt [-afjs] [-p hex-address] [system [core]]\n");
	exit(1);
}

static void
dotrace(caddr_t tcpcb)
{
	struct tcp_debug *td;
	int i;
	int prev_debx = tcp_debx, family;

again:
	if (--tcp_debx < 0)
		tcp_debx = TCP_NDEBUG - 1;
	for (i = prev_debx % TCP_NDEBUG; i < TCP_NDEBUG; i++) {
		td = &tcp_debug[i];
		if (tcpcb && td->td_tcb != tcpcb)
			continue;
		ntime = ntohl(td->td_time);
#ifdef INET6
		family = td->td_family;
#else
		family = AF_INET;
#endif
		switch (family) {
		case AF_INET:
			tcp_trace(td->td_act, td->td_ostate, &td->td_cb,
			    td->td_family, &td->td_ti.ti_i, &td->td_ti.ti_t,
			    td->td_req);
			break;
#ifdef INET6
		case AF_INET6:
			tcp_trace(td->td_act, td->td_ostate, &td->td_cb,
			    td->td_family, &td->td_ti6.ip6, &td->td_ti6.th,
			    td->td_req);
			break;
#endif
		}
		if (i == tcp_debx)
			goto done;
	}
	for (i = 0; i <= tcp_debx % TCP_NDEBUG; i++) {
		td = &tcp_debug[i];
		if (tcpcb && td->td_tcb != tcpcb)
			continue;
		ntime = ntohl(td->td_time);
#ifdef INET6
		family = td->td_family;
#else
		family = AF_INET;
#endif
		switch (family) {
		case AF_INET:
			tcp_trace(td->td_act, td->td_ostate, &td->td_cb,
			    td->td_family, &td->td_ti.ti_i, &td->td_ti.ti_t,
			    td->td_req);
			break;
#ifdef INET6
		case AF_INET6:
			tcp_trace(td->td_act, td->td_ostate, &td->td_cb,
			    td->td_family, &td->td_ti6.ip6, &td->td_ti6.th,
			    td->td_req);
			break;
#endif
		}
	}
done:
	if (follow) {
		prev_debx = tcp_debx + 1;
		if (prev_debx >= TCP_NDEBUG)
			prev_debx = 0;
		do {
			sleep(1);
			(void)klseek(memf, (off_t)nl[N_TCP_DEBX].n_value, L_SET);
			if (read(memf, (char *)&tcp_debx, sizeof(tcp_debx)) !=
			    sizeof(tcp_debx))
				err(3, "tcp_debx");
		} while (tcp_debx == prev_debx);
		(void)klseek(memf, (off_t)nl[N_TCP_DEBUG].n_value, L_SET);
		if (read(memf, (char *)tcp_debug, sizeof(tcp_debug)) !=
		    sizeof(tcp_debug))
			err(3, "tcp_debug");
		goto again;
	}
}

/*
 * Tcp debug routines
 */
/*ARGSUSED*/
static void
tcp_trace(short act, short ostate, struct tcpcb *tp, int family __unused,
    void *ip, struct tcphdr *th, int req)
{
	tcp_seq seq, ack;
	int flags, len, win, timer;
	struct ip *ip4;
#ifdef INET6
	bool isipv6, nopkt = true;
	struct ip6_hdr *ip6;
	char ntop_buf[INET6_ADDRSTRLEN];
#endif

#ifdef INET6
	/* Appease GCC -Wmaybe-uninitialized */
	ip4 = NULL;
	ip6 = NULL;
	isipv6 = false;

	switch (family) {
	case AF_INET:
		nopkt = false;
		isipv6 = false;
		ip4 = (struct ip *)ip;
		break;
	case AF_INET6:
		nopkt = false;
		isipv6 = true;
		ip6 = (struct ip6_hdr *)ip;
	case 0:
	default:
		break;
	}
#else
	ip4 = (struct ip *)ip;
#endif
	printf("%03ld %s:%s ", (long)((ntime / 10) % 1000), tcpstates[ostate],
	    tanames[act]);
	switch (act) {
	case TA_INPUT:
	case TA_OUTPUT:
	case TA_DROP:
#ifdef INET6
		if (nopkt)
			break;
#endif
		if (aflag) {
			printf("(src=%s,%u, ",

#ifdef INET6
			    isipv6 ? inet_ntop(AF_INET6, &ip6->ip6_src,
				ntop_buf, sizeof(ntop_buf)) :
#endif
			    inet_ntoa(ip4->ip_src), ntohs(th->th_sport));
			printf("dst=%s,%u)",
#ifdef INET6
			    isipv6 ? inet_ntop(AF_INET6, &ip6->ip6_dst,
				ntop_buf, sizeof(ntop_buf)) :
#endif
			    inet_ntoa(ip4->ip_dst), ntohs(th->th_dport));
		}
		seq = th->th_seq;
		ack = th->th_ack;

		len =
#ifdef INET6
		    isipv6 ? ip6->ip6_plen :
#endif
		    ip4->ip_len;
		win = th->th_win;
		if (act == TA_OUTPUT) {
			seq = ntohl(seq);
			ack = ntohl(ack);
			len = ntohs(len);
			win = ntohs(win);
		}
		if (act == TA_OUTPUT)
			len -= sizeof(struct tcphdr);
		if (len)
			printf("[%lx..%lx)", (u_long)seq, (u_long)(seq + len));
		else
			printf("%lx", (u_long)seq);
		printf("@%lx", (u_long)ack);
		if (win)
			printf("(win=%x)", win);
		flags = th->th_flags;
		if (flags) {
			const char *cp = "<";
#define	pf(flag, string) { \
	if (th->th_flags&flag) { \
		(void)printf("%s%s", cp, string); \
		cp = ","; \
	} \
}
			pf(TH_SYN, "SYN");
			pf(TH_ACK, "ACK");
			pf(TH_FIN, "FIN");
			pf(TH_RST, "RST");
			pf(TH_PUSH, "PUSH");
			pf(TH_URG, "URG");
			printf(">");
		}
		break;
	case TA_USER:
		timer = req >> 8;
		req &= 0xff;
		printf("%s", prurequests[req]);
		if (req == PRU_SLOWTIMO || req == PRU_FASTTIMO)
			printf("<%s>", tcptimers[timer]);
		break;
	}
	printf(" -> %s", tcpstates[tp->t_state]);
	/* print out internal state of tp !?! */
	printf("\n");
	if (sflag) {
		printf("\trcv_nxt %lx rcv_wnd %lx snd_una %lx snd_nxt %lx snd_max %lx\n",
		    (u_long)tp->rcv_nxt, (u_long)tp->rcv_wnd,
		    (u_long)tp->snd_una, (u_long)tp->snd_nxt,
		    (u_long)tp->snd_max);
		printf("\tsnd_wl1 %lx snd_wl2 %lx snd_wnd %lx\n",
		    (u_long)tp->snd_wl1, (u_long)tp->snd_wl2,
		    (u_long)tp->snd_wnd);
	}
}

static int
numeric(const void *v1, const void *v2)
{
	const caddr_t *c1 = v1, *c2 = v2;

	return (*c1 - *c2);
}

static void
klseek(int fd, off_t base, int off)
{
	(void)lseek(fd, base, off);
}
