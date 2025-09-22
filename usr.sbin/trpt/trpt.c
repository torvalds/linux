/*	$OpenBSD: trpt.c,v 1.40 2023/03/08 04:43:15 guenther Exp $	*/

/*-
 * Copyright (c) 1997 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Jason R. Thorpe of the Numerical Aerospace Simulation Facility,
 * NASA Ames Research Center.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
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

#include <sys/queue.h>
#include <sys/time.h>
#include <sys/socket.h>
#define PRUREQUESTS
#include <sys/protosw.h>
#define _KERNEL
#include <sys/timeout.h>		/* to get timeout_pending() and such */
#undef _KERNEL

#include <net/route.h>
#include <net/if.h>

#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/in_pcb.h>
#include <netinet/ip_var.h>
#include <netinet/tcp.h>
#define TCPSTATES
#include <netinet/tcp_fsm.h>
#include <netinet/tcp_seq.h>
#define	TCPTIMERS
#include <netinet/tcp_timer.h>
#include <netinet/tcp_var.h>
#define	TANAMES
#include <netinet/tcp_debug.h>

#include <arpa/inet.h>

#include <err.h>
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <kvm.h>
#include <nlist.h>
#include <paths.h>
#include <limits.h>
#include <stdlib.h>
#include <unistd.h>

struct nlist nl[] = {
#define	N_TCP_DEBUG	0		/* no sysctl */
	{ "_tcp_debug" },
#define	N_TCP_DEBX	1		/* no sysctl */
	{ "_tcp_debx" },
	{ NULL },
};

int	tcp_debx;
struct	tcp_debug tcp_debug[TCP_NDEBUG];

static caddr_t tcp_pcbs[TCP_NDEBUG];
static u_int32_t ntime;
static int aflag, follow, sflag, tflag;

extern	char *__progname;

void	dotrace(caddr_t);
void	tcp_trace(short, short, struct tcpcb *, struct tcpiphdr *,
	    struct tcpipv6hdr *, int);
int	numeric(const void *, const void *);
void	usage(void);

kvm_t	*kd;

int
main(int argc, char *argv[])
{
	char *sys = NULL, *core = NULL, *cp, errbuf[_POSIX2_LINE_MAX];
	int ch, i, jflag = 0, npcbs = 0;
	unsigned long l;
	gid_t gid;

	while ((ch = getopt(argc, argv, "afjM:N:p:st")) != -1) {
		switch (ch) {
		case 'a':
			aflag = 1;
			break;
		case 'f':
			follow = 1;
			setvbuf(stdout, NULL, _IOLBF, 0);
			break;
		case 'j':
			jflag = 1;
			break;
		case 'p':
			if (npcbs >= TCP_NDEBUG)
				errx(1, "too many pcbs specified");
			errno = 0;
			l = strtoul(optarg, &cp, 16);
			tcp_pcbs[npcbs] = (caddr_t)l;
			if (*optarg == '\0' || *cp != '\0' || errno ||
			    (unsigned long)tcp_pcbs[npcbs] != l)
				errx(1, "invalid address: %s", optarg);
			npcbs++;
			break;
		case 's':
			sflag = 1;
			break;
		case 't':
			tflag = 1;
			break;
		case 'N':
			sys = optarg;
			break;
		case 'M':
			core = optarg;
			break;
		default:
			usage();
			/* NOTREACHED */
		}
	}
	argc -= optind;
	argv += optind;

	if (argc)
		usage();

	/*
	 * Discard setgid privileged if not the running kernel so that bad
	 * guys can't print interesting stuff from kernel memory.
	 */
	gid = getgid();
	if (core != NULL || sys != NULL)
		if (setresgid(gid, gid, gid) == -1)
			err(1, "setresgid");

	kd = kvm_openfiles(sys, core, NULL, O_RDONLY, errbuf);
	if (kd == NULL)
		errx(1, "can't open kmem: %s", errbuf);

	if (core == NULL && sys == NULL)
		if (setresgid(gid, gid, gid) == -1)
			err(1, "setresgid");

	if (kvm_nlist(kd, nl))
		errx(2, "%s: no namelist", sys ? sys : _PATH_UNIX);

	if (pledge("stdio", NULL) == -1)
		err(1, "pledge");

	if (kvm_read(kd, nl[N_TCP_DEBX].n_value, (char *)&tcp_debx,
	    sizeof(tcp_debx)) != sizeof(tcp_debx))
		errx(3, "tcp_debx: %s", kvm_geterr(kd));

	if (kvm_read(kd, nl[N_TCP_DEBUG].n_value, (char *)tcp_debug,
	    sizeof(tcp_debug)) != sizeof(tcp_debug))
		errx(3, "tcp_debug: %s", kvm_geterr(kd));

	/*
	 * If no control blocks have been specified, figure
	 * out how many distinct one we have and summarize
	 * them in tcp_pcbs for sorting the trace records
	 * below.
	 */
	if (npcbs == 0) {
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
		if (npcbs == 0)
			exit(0);
	}
	qsort(tcp_pcbs, npcbs, sizeof(caddr_t), numeric);
	if (jflag) {
		for (i = 0;;) {
			printf("%lx", (long)tcp_pcbs[i]);
			if (++i == npcbs)
				break;
			fputs(", ", stdout);
		}
		putchar('\n');
	} else {
		for (i = 0; i < npcbs; i++) {
			printf("\n%lx:\n", (long)tcp_pcbs[i]);
			dotrace(tcp_pcbs[i]);
		}
	}
	exit(0);
}

void
dotrace(caddr_t tcpcb)
{
	struct tcp_debug *td;
	int prev_debx = tcp_debx;
	int i;

 again:
	if (--tcp_debx < 0)
		tcp_debx = TCP_NDEBUG - 1;
	for (i = prev_debx % TCP_NDEBUG; i < TCP_NDEBUG; i++) {
		td = &tcp_debug[i];
		if (tcpcb && td->td_tcb != tcpcb)
			continue;
		ntime = ntohl(td->td_time);
		tcp_trace(td->td_act, td->td_ostate,
		    &td->td_cb, &td->td_ti,
		    &td->td_ti6, td->td_req);
		if (i == tcp_debx)
			goto done;
	}
	for (i = 0; i <= tcp_debx % TCP_NDEBUG; i++) {
		td = &tcp_debug[i];
		if (tcpcb && td->td_tcb != tcpcb)
			continue;
		ntime = ntohl(td->td_time);
		tcp_trace(td->td_act, td->td_ostate,
		    &td->td_cb, &td->td_ti,
		    &td->td_ti6, td->td_req);
	}
 done:
	if (follow) {
		prev_debx = tcp_debx + 1;
		if (prev_debx >= TCP_NDEBUG)
			prev_debx = 0;
		do {
			sleep(1);
			if (kvm_read(kd, nl[N_TCP_DEBX].n_value,
			    (char *)&tcp_debx, sizeof(tcp_debx)) !=
			    sizeof(tcp_debx))
				errx(3, "tcp_debx: %s", kvm_geterr(kd));
		} while (tcp_debx == prev_debx);

		if (kvm_read(kd, nl[N_TCP_DEBUG].n_value, (char *)tcp_debug,
		    sizeof(tcp_debug)) != sizeof(tcp_debug))
			errx(3, "tcp_debug: %s", kvm_geterr(kd));

		goto again;
	}
}

/*
 * Tcp debug routines
 */
void
tcp_trace(short act, short ostate, struct tcpcb *tp,
    struct tcpiphdr *ti, struct tcpipv6hdr *ti6, int req)
{
	tcp_seq seq, ack;
	int flags, len, win;
	struct tcphdr *th;
	char hbuf[INET6_ADDRSTRLEN];

	if (ti->ti_src.s_addr)
		th = &ti->ti_t;
	else
		th = &ti6->ti6_t;

	printf("%03d %s:%s ", (ntime/10) % 1000, tcpstates[ostate],
	    tanames[act]);
	switch (act) {
	case TA_INPUT:
	case TA_OUTPUT:
	case TA_DROP:
		if (aflag) {
			if (ti->ti_src.s_addr) {
				printf("(src=%s,%u, ",
				    inet_ntoa(ti->ti_src), ntohs(ti->ti_sport));
				printf("dst=%s,%u)",
				    inet_ntoa(ti->ti_dst), ntohs(ti->ti_dport));
			} else {
				printf("(src=%s,%u, ",
				    inet_ntop(AF_INET6, &ti6->ti6_src,
				    hbuf, sizeof(hbuf)), ntohs(ti->ti_sport));
				printf("dst=%s,%u)",
				    inet_ntop(AF_INET6, &ti6->ti6_dst,
				    hbuf, sizeof(hbuf)), ntohs(ti->ti_dport));
			}
		}
		seq = th->th_seq;
		ack = th->th_ack;
		if (ti->ti_src.s_addr)
			len = ti->ti_len;
		else
			len = ti6->ti6_plen;	/*XXX intermediate header*/
		win = th->th_win;
		if (act == TA_OUTPUT) {
			NTOHL(seq);
			NTOHL(ack);
			NTOHS(win);
		}
		if (len)
			printf("[%x..%x)", seq, seq + len);
		else
			printf("%x", seq);
		printf("@%x", ack);
		if (win)
			printf("(win=%x)", win);
		flags = th->th_flags;
		if (flags) {
			char *cp = "<";
#define	pf(flag, string) { \
	if (th->th_flags & flag) { \
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
		printf("%s", prurequests[req]);
		break;
	case TA_TIMER:
		printf("%s", tcptimers[req]);
		break;
	}
	printf(" -> %s", tcpstates[tp->t_state]);
	/* print out internal state of tp !?! */
	printf("\n");
	if (sflag) {
		printf("\trcv_nxt %x rcv_wnd %lx snd_una %x snd_nxt %x snd_max %x\n",
		    tp->rcv_nxt, tp->rcv_wnd, tp->snd_una, tp->snd_nxt,
		    tp->snd_max);
		printf("\tsnd_wl1 %x snd_wl2 %x snd_wnd %lx\n", tp->snd_wl1,
		    tp->snd_wl2, tp->snd_wnd);
	}
	/* print out timers? */
	if (tflag) {
		char *cp = "\t";
		int i;

		for (i = 0; i < TCPT_NTIMERS; i++) {
			if (timeout_pending(&tp->t_timer[i]))
				continue;
			printf("%s%s=%d", cp, tcptimers[i],
			    tp->t_timer[i].to_time);
			if (i == TCPT_REXMT)
				printf(" (t_rxtshft=%d)", tp->t_rxtshift);
			cp = ", ";
		}
		if (*cp != '\t')
			putchar('\n');
	}
}

int
numeric(const void *v1, const void *v2)
{
	const caddr_t *c1 = v1;
	const caddr_t *c2 = v2;
	int rv;

	if (*c1 < *c2)
		rv = -1;
	else if (*c1 > *c2)
		rv = 1;
	else
		rv = 0;

	return (rv);
}

void
usage(void)
{

	(void) fprintf(stderr, "usage: %s [-afjst] [-M core]"
	    " [-N system] [-p hex-address]\n", __progname);
	exit(1);
}
