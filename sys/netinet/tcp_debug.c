/*	$OpenBSD: tcp_debug.c,v 1.33 2025/07/08 00:47:41 jsg Exp $	*/
/*	$NetBSD: tcp_debug.c,v 1.10 1996/02/13 23:43:36 christos Exp $	*/

/*
 * Copyright (c) 1982, 1986, 1993
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
 *
 *	@(#)COPYRIGHT	1.1 (NRL) 17 January 1995
 *
 * NRL grants permission for redistribution and use in source and binary
 * forms, with or without modification, of the software and documentation
 * created at NRL provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgements:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 *	This product includes software developed at the Information
 *	Technology Division, US Naval Research Laboratory.
 * 4. Neither the name of the NRL nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THE SOFTWARE PROVIDED BY NRL IS PROVIDED BY NRL AND CONTRIBUTORS ``AS
 * IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL NRL OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * The views and conclusions contained in the software and documentation
 * are those of the authors and should not be interpreted as representing
 * official policies, either expressed or implied, of the US Naval
 * Research Laboratory (NRL).
 */

#ifdef TCPDEBUG
/* load symbolic names */
#define	PRUREQUESTS
#define	TCPSTATES
#define	TCPTIMERS
#define	TANAMES
#endif

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/mbuf.h>
#include <sys/mutex.h>
#include <sys/socket.h>

#include <net/route.h>

#include <netinet/in_systm.h>
#include <netinet/ip.h>
#include <netinet/ip6.h>
#include <netinet/ip_var.h>
#include <netinet/tcp.h>
#include <netinet/tcp_timer.h>
#include <netinet/tcp_var.h>
#include <netinet/tcp_debug.h>
#include <netinet/tcp_fsm.h>

#ifdef TCPDEBUG
#include <sys/protosw.h>
#endif

/*
 *  Locks used to protect struct members in this file:
 *	D	TCP debug global mutex
 */

struct mutex tcp_debug_mtx = MUTEX_INITIALIZER(IPL_SOFTNET);

#ifdef TCPDEBUG
int	tcpconsdebug = 0;
#endif

struct	tcp_debug tcp_debug[TCP_NDEBUG];	/* [D] */
int	tcp_debx;				/* [D] */

/*
 * Tcp debug routines
 */
void
tcp_trace(short act, short ostate, struct tcpcb *tp, struct tcpcb *otp,
    caddr_t headers, int req, int len)
{
#ifdef TCPDEBUG
	struct tcphdr *th;
	tcp_seq seq, ack;
	int flags;
#endif
	int pf = PF_UNSPEC;
	struct tcp_debug *td;
	struct tcpiphdr *ti;
	struct tcpipv6hdr *ti6;

	mtx_enter(&tcp_debug_mtx);

	td = &tcp_debug[tcp_debx++];
	ti = (struct tcpiphdr *)headers;
	ti6 = (struct tcpipv6hdr *)headers;

	if (tcp_debx == TCP_NDEBUG)
		tcp_debx = 0;
	td->td_time = iptime();
	td->td_act = act;
	td->td_ostate = ostate;
	td->td_tcb = (caddr_t)otp;
	if (tp) {
		pf = tp->pf;
		td->td_cb = *tp;
	} else
		bzero((caddr_t)&td->td_cb, sizeof (*tp));

	bzero(&td->td_ti6, sizeof(struct tcpipv6hdr));
	bzero(&td->td_ti, sizeof(struct tcpiphdr));
	if (headers) {
		/* The address family may be in tcpcb or ip header. */
		if (pf == PF_UNSPEC) {
			switch (ti6->ti6_i.ip6_vfc & IPV6_VERSION_MASK) {
#ifdef INET6
			case IPV6_VERSION:
				pf = PF_INET6;
				break;
#endif /* INET6 */
			case IPVERSION:
				pf = PF_INET;
				break;
			}
		}
		switch (pf) {
#ifdef INET6
		case PF_INET6:
#ifdef TCPDEBUG
			th = &ti6->ti6_t;
#endif
			td->td_ti6 = *ti6;
			td->td_ti6.ti6_plen = len;
			break;
#endif /* INET6 */
		case PF_INET:
#ifdef TCPDEBUG
			th = &ti->ti_t;
#endif
			td->td_ti = *ti;
			td->td_ti.ti_len = len;
			break;
		default:
			headers = NULL;
			break;
		}
	}

	td->td_req = req;
#ifdef TCPDEBUG
	if (tcpconsdebug == 0)
		goto done;
	if (otp)
		printf("%p %s:", otp, tcpstates[ostate]);
	else
		printf("???????? ");
	printf("%s ", tanames[act]);
	switch (act) {

	case TA_INPUT:
	case TA_OUTPUT:
	case TA_DROP:
		if (headers == NULL)
			break;
		seq = th->th_seq;
		ack = th->th_ack;
		if (act == TA_OUTPUT) {
			seq = ntohl(seq);
			ack = ntohl(ack);
		}
		if (len)
			printf("[%x..%x)", seq, seq+len);
		else
			printf("%x", seq);
		printf("@%x, urp=%x", ack, th->th_urp);
		flags = th->th_flags;
		if (flags) {
			char *cp = "<";
#define pf(f) { if (th->th_flags&TH_##f) { printf("%s%s", cp, #f); cp = ","; } }
			pf(SYN); pf(ACK); pf(FIN); pf(RST); pf(PUSH); pf(URG);
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
	if (tp)
		printf(" -> %s", tcpstates[tp->t_state]);
	/* print out internal state of tp !?! */
	printf("\n");
	if (tp == NULL)
		goto done;
	printf("\trcv_(nxt,wnd,up) (%x,%lx,%x) snd_(una,nxt,max) (%x,%x,%x)\n",
	    tp->rcv_nxt, tp->rcv_wnd, tp->rcv_up, tp->snd_una, tp->snd_nxt,
	    tp->snd_max);
	printf("\tsnd_(wl1,wl2,wnd) (%x,%x,%lx)\n",
	    tp->snd_wl1, tp->snd_wl2, tp->snd_wnd);

 done:
#endif /* TCPDEBUG */
	mtx_leave(&tcp_debug_mtx);
}
