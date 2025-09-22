/*	$OpenBSD: tcp_output.c,v 1.157 2025/09/16 17:29:35 bluhm Exp $	*/
/*	$NetBSD: tcp_output.c,v 1.16 1997/06/03 16:17:09 kml Exp $	*/

/*
 * Copyright (c) 1982, 1986, 1988, 1990, 1993
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

#include "pf.h"
#include "stoeplitz.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/mbuf.h>
#include <sys/protosw.h>
#include <sys/socket.h>

#include <net/if.h>
#include <net/if_var.h>
#if NPF > 0
#include <net/pfvar.h>
#endif

#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/in_pcb.h>
#include <netinet/ip_var.h>
#include <netinet6/ip6_var.h>
#include <netinet/tcp.h>
#define	TCPOUTFLAGS
#include <netinet/tcp_fsm.h>
#include <netinet/tcp_seq.h>
#include <netinet/tcp_timer.h>
#include <netinet/tcp_var.h>
#include <netinet/tcp_debug.h>

#ifdef notyet
extern struct mbuf *m_copypack();
#endif

#ifdef TCP_SACK_DEBUG
void tcp_print_holes(struct tcpcb *tp);

void
tcp_print_holes(struct tcpcb *tp)
{
	struct sackhole *p = tp->snd_holes;
	if (p == NULL)
		return;
	printf("Hole report: start--end dups rxmit\n");
	while (p) {
		printf("%x--%x d %d r %x\n", p->start, p->end, p->dups,
		    p->rxmit);
		p = p->next;
	}
	printf("\n");
}
#endif /* TCP_SACK_DEBUG */

/*
 * Returns pointer to a sackhole if there are any pending retransmissions;
 * NULL otherwise.
 */
struct sackhole *
tcp_sack_output(struct tcpcb *tp)
{
	struct sackhole *p;

	if (!tp->sack_enable)
		return (NULL);
	p = tp->snd_holes;
	while (p) {
		if (p->dups >= tcprexmtthresh && SEQ_LT(p->rxmit, p->end)) {
			if (SEQ_LT(p->rxmit, tp->snd_una)) {/* old SACK hole */
				p = p->next;
				continue;
			}
#ifdef TCP_SACK_DEBUG
			if (p)
				tcp_print_holes(tp);
#endif
			return (p);
		}
		p = p->next;
	}
	return (NULL);
}

/*
 * After a timeout, the SACK list may be rebuilt.  This SACK information
 * should be used to avoid retransmitting SACKed data.  This function
 * traverses the SACK list to see if snd_nxt should be moved forward.
 */

void
tcp_sack_adjust(struct tcpcb *tp)
{
	struct sackhole *cur = tp->snd_holes;
	if (cur == NULL)
		return; /* No holes */
	if (SEQ_GEQ(tp->snd_nxt, tp->rcv_lastsack))
		return; /* We're already beyond any SACKed blocks */
	/*
	 * Two cases for which we want to advance snd_nxt:
	 * i) snd_nxt lies between end of one hole and beginning of another
	 * ii) snd_nxt lies between end of last hole and rcv_lastsack
	 */
	while (cur->next) {
		if (SEQ_LT(tp->snd_nxt, cur->end))
			return;
		if (SEQ_GEQ(tp->snd_nxt, cur->next->start))
			cur = cur->next;
		else {
			tp->snd_nxt = cur->next->start;
			return;
		}
	}
	if (SEQ_LT(tp->snd_nxt, cur->end))
		return;
	tp->snd_nxt = tp->rcv_lastsack;
	return;
}

/*
 * Tcp output routine: figure out what should be sent and send it.
 */
int
tcp_output(struct tcpcb *tp)
{
	struct socket *so = tp->t_inpcb->inp_socket;
	long len, win, rcv_hiwat, txmaxseg;
	int off, flags, error;
	struct mbuf *m;
	struct tcphdr *th;
	u_int32_t optbuf[howmany(MAX_TCPOPTLEN, sizeof(u_int32_t))];
	u_char *opt = (u_char *)optbuf;
	unsigned int optlen, hdrlen, packetlen;
	int doing_sosend, idle, sendalot = 0;
	int i, sack_rxmit = 0;
	struct sackhole *p;
	uint64_t now;
#ifdef TCP_SIGNATURE
	unsigned int sigoff;
#endif /* TCP_SIGNATURE */
#ifdef TCP_ECN
	int do_ecn = atomic_load_int(&tcp_do_ecn);
	int needect;
#endif
	int tso;

#if defined(TCP_SIGNATURE) && defined(DIAGNOSTIC)
	if (tp->sack_enable && (tp->t_flags & TF_SIGNATURE))
		return (EINVAL);
#endif /* defined(TCP_SIGNATURE) && defined(DIAGNOSTIC) */

	now = tcp_now();

	mtx_enter(&so->so_snd.sb_mtx);
	doing_sosend = soissending(so);
	mtx_leave(&so->so_snd.sb_mtx);

	/*
	 * Determine length of data that should be transmitted,
	 * and flags that will be used.
	 * If there is some data or critical controls (SYN, RST)
	 * to send, then transmit; otherwise, investigate further.
	 */
	idle = (tp->t_flags & TF_LASTIDLE) || (tp->snd_max == tp->snd_una);
	if (idle && (now - tp->t_rcvtime) >= tp->t_rxtcur)
		/*
		 * We have been idle for "a while" and no acks are
		 * expected to clock out any data we send --
		 * slow start to get ack "clock" running again.
		 */
		tp->snd_cwnd = 2 * tp->t_maxseg;

	/* remember 'idle' for next invocation of tcp_output */
	if (idle && doing_sosend) {
		tp->t_flags |= TF_LASTIDLE;
		idle = 0;
	} else
		tp->t_flags &= ~TF_LASTIDLE;

again:
	/*
	 * If we've recently taken a timeout, snd_max will be greater than
	 * snd_nxt.  There may be SACK information that allows us to avoid
	 * resending already delivered data.  Adjust snd_nxt accordingly.
	 */
	if (tp->sack_enable && SEQ_LT(tp->snd_nxt, tp->snd_max))
		tcp_sack_adjust(tp);
	off = tp->snd_nxt - tp->snd_una;
	win = ulmin(tp->snd_wnd, tp->snd_cwnd);

	flags = tcp_outflags[tp->t_state];

	/*
	 * Send any SACK-generated retransmissions.  If we're explicitly trying
	 * to send out new data (when sendalot is 1), bypass this function.
	 * If we retransmit in fast recovery mode, decrement snd_cwnd, since
	 * we're replacing a (future) new transmission with a retransmission
	 * now, and we previously incremented snd_cwnd in tcp_input().
	 */
	if (tp->sack_enable && !sendalot) {
		if (tp->t_dupacks >= tcprexmtthresh &&
		    (p = tcp_sack_output(tp))) {
			off = p->rxmit - tp->snd_una;
			sack_rxmit = 1;
			/* Coalesce holes into a single retransmission */
			len = min(tp->t_maxseg, p->end - p->rxmit);
			if (SEQ_LT(tp->snd_una, tp->snd_last))
				tp->snd_cwnd -= tp->t_maxseg;
		}
	}

	sendalot = 0;
	tso = 0;
	/*
	 * If in persist timeout with window of 0, send 1 byte.
	 * Otherwise, if window is small but nonzero
	 * and timer expired, we will send what we can
	 * and go to transmit state.
	 */
	if (tp->t_force) {
		if (win == 0) {
			/*
			 * If we still have some data to send, then
			 * clear the FIN bit.  Usually this would
			 * happen below when it realizes that we
			 * aren't sending all the data.  However,
			 * if we have exactly 1 byte of unset data,
			 * then it won't clear the FIN bit below,
			 * and if we are in persist state, we wind
			 * up sending the packet without recording
			 * that we sent the FIN bit.
			 *
			 * We can't just blindly clear the FIN bit,
			 * because if we don't have any more data
			 * to send then the probe will be the FIN
			 * itself.
			 */
			if (off < so->so_snd.sb_cc)
				flags &= ~TH_FIN;
			win = 1;
		} else {
			TCP_TIMER_DISARM(tp, TCPT_PERSIST);
			tp->t_rxtshift = 0;
		}
	}

	if (!sack_rxmit) {
		len = ulmin(so->so_snd.sb_cc, win) - off;
	}

	if (len < 0) {
		/*
		 * If FIN has been sent but not acked,
		 * but we haven't been called to retransmit,
		 * len will be -1.  Otherwise, window shrank
		 * after we sent into it.  If window shrank to 0,
		 * cancel pending retransmit, pull snd_nxt back
		 * to (closed) window, and set the persist timer
		 * if it isn't already going.  If the window didn't
		 * close completely, just wait for an ACK.
		 */
		len = 0;
		if (win == 0) {
			TCP_TIMER_DISARM(tp, TCPT_REXMT);
			tp->t_rxtshift = 0;
			tp->snd_nxt = tp->snd_una;
			if (TCP_TIMER_ISARMED(tp, TCPT_PERSIST) == 0)
				tcp_setpersist(tp);
		}
	}

	/*
	 * Never send more than half a buffer full.  This insures that we can
	 * always keep 2 packets on the wire, no matter what SO_SNDBUF is, and
	 * therefore acks will never be delayed unless we run out of data to
	 * transmit.
	 */
	txmaxseg = ulmin(so->so_snd.sb_hiwat / 2, tp->t_maxseg);

	if (len > txmaxseg) {
		if (atomic_load_int(&tcp_do_tso) &&
		    tp->t_inpcb->inp_options == NULL &&
		    tp->t_inpcb->inp_outputopts6 == NULL &&
#ifdef TCP_SIGNATURE
		    ((tp->t_flags & TF_SIGNATURE) == 0) &&
#endif
		    len >= 2 * tp->t_maxseg &&
		    tp->rcv_numsacks == 0 && sack_rxmit == 0 &&
		    !(flags & (TH_SYN|TH_RST|TH_FIN))) {
			tso = 1;
			/* avoid small chopped packets */
			if (len > (len / tp->t_maxseg) * tp->t_maxseg) {
				len = (len / tp->t_maxseg) * tp->t_maxseg;
				sendalot = 1;
			}
		} else {
			len = txmaxseg;
			sendalot = 1;
		}
	}
	if (off + len < so->so_snd.sb_cc)
		flags &= ~TH_FIN;

	mtx_enter(&so->so_rcv.sb_mtx);
	win = sbspace_locked(&so->so_rcv);
	rcv_hiwat = (long) so->so_rcv.sb_hiwat;
	mtx_leave(&so->so_rcv.sb_mtx);

	/*
	 * Sender silly window avoidance.  If connection is idle
	 * and can send all data, a maximum segment,
	 * at least a maximum default-size segment do it,
	 * or are forced, do it; otherwise don't bother.
	 * If peer's buffer is tiny, then send
	 * when window is at least half open.
	 * If retransmitting (possibly after persist timer forced us
	 * to send into a small window), then must resend.
	 */
	if (len) {
		if (len >= txmaxseg)
			goto send;
		if ((idle || (tp->t_flags & TF_NODELAY)) &&
		    len + off >= so->so_snd.sb_cc && !doing_sosend &&
		    (tp->t_flags & TF_NOPUSH) == 0)
			goto send;
		if (tp->t_force)
			goto send;
		if (len >= tp->max_sndwnd / 2 && tp->max_sndwnd > 0)
			goto send;
		if (SEQ_LT(tp->snd_nxt, tp->snd_max))
			goto send;
		if (sack_rxmit)
			goto send;
	}

	/*
	 * Compare available window to amount of window
	 * known to peer (as advertised window less
	 * next expected input).  If the difference is at least two
	 * max size segments, or at least 50% of the maximum possible
	 * window, then want to send a window update to peer.
	 */
	if (win > 0) {
		/*
		 * "adv" is the amount we can increase the window,
		 * taking into account that we are limited by
		 * TCP_MAXWIN << tp->rcv_scale.
		 */
		long adv = lmin(win, (long)TCP_MAXWIN << tp->rcv_scale) -
			(tp->rcv_adv - tp->rcv_nxt);

		if (adv >= (long) (2 * tp->t_maxseg))
			goto send;
		if (2 * adv >= rcv_hiwat)
			goto send;
	}

	/*
	 * Send if we owe peer an ACK.
	 */
	if (tp->t_flags & TF_ACKNOW)
		goto send;
	if (flags & (TH_SYN|TH_RST))
		goto send;
	if (SEQ_GT(tp->snd_up, tp->snd_una))
		goto send;
	/*
	 * If our state indicates that FIN should be sent
	 * and we have not yet done so, or we're retransmitting the FIN,
	 * then we need to send.
	 */
	if (flags & TH_FIN &&
	    ((tp->t_flags & TF_SENTFIN) == 0 || tp->snd_nxt == tp->snd_una))
		goto send;
	/*
	 * In SACK, it is possible for tcp_output to fail to send a segment
	 * after the retransmission timer has been turned off.  Make sure
	 * that the retransmission timer is set.
	 */
	if (SEQ_GT(tp->snd_max, tp->snd_una) &&
	    TCP_TIMER_ISARMED(tp, TCPT_REXMT) == 0 &&
	    TCP_TIMER_ISARMED(tp, TCPT_PERSIST) == 0) {
		TCP_TIMER_ARM(tp, TCPT_REXMT, tp->t_rxtcur);
		return (0);
	}

	/*
	 * TCP window updates are not reliable, rather a polling protocol
	 * using ``persist'' packets is used to insure receipt of window
	 * updates.  The three ``states'' for the output side are:
	 *	idle			not doing retransmits or persists
	 *	persisting		to move a small or zero window
	 *	(re)transmitting	and thereby not persisting
	 *
	 * tp->t_timer[TCPT_PERSIST]
	 *	is set when we are in persist state.
	 * tp->t_force
	 *	is set when we are called to send a persist packet.
	 * tp->t_timer[TCPT_REXMT]
	 *	is set when we are retransmitting
	 * The output side is idle when both timers are zero.
	 *
	 * If send window is too small, there is data to transmit, and no
	 * retransmit or persist is pending, then go to persist state.
	 * If nothing happens soon, send when timer expires:
	 * if window is nonzero, transmit what we can,
	 * otherwise force out a byte.
	 */
	if (so->so_snd.sb_cc && TCP_TIMER_ISARMED(tp, TCPT_REXMT) == 0 &&
	    TCP_TIMER_ISARMED(tp, TCPT_PERSIST) == 0) {
		tp->t_rxtshift = 0;
		tcp_setpersist(tp);
	}

	/*
	 * No reason to send a segment, just return.
	 */
	return (0);

send:
	/*
	 * Before ESTABLISHED, force sending of initial options
	 * unless TCP set not to do any options.
	 * NOTE: we assume that the IP/TCP header plus TCP options
	 * always fit in a single mbuf, leaving room for a maximum
	 * link header, i.e.
	 *	max_linkhdr + sizeof(network header) + sizeof(struct tcphdr +
	 *		optlen <= MHLEN
	 */
	optlen = 0;

	switch (tp->pf) {
	case 0:	/*default to PF_INET*/
	case PF_INET:
		hdrlen = sizeof(struct ip) + sizeof(struct tcphdr);
		break;
#ifdef INET6
	case PF_INET6:
		hdrlen = sizeof(struct ip6_hdr) + sizeof(struct tcphdr);
		break;
#endif /* INET6 */
	default:
		return (EPFNOSUPPORT);
	}

	if (flags & TH_SYN) {
		tp->snd_nxt = tp->iss;
		if ((tp->t_flags & TF_NOOPT) == 0) {
			u_int16_t mss;

			opt[0] = TCPOPT_MAXSEG;
			opt[1] = 4;
			mss = htons((u_int16_t) tcp_mss(tp, 0));
			memcpy(opt + 2, &mss, sizeof(mss));
			optlen = 4;

			if (flags & TH_ACK)
				tcp_mss_update(tp);
			/*
			 * If this is the first SYN of connection (not a SYN
			 * ACK), include SACK_PERMIT_HDR option.  If this is a
			 * SYN ACK, include SACK_PERMIT_HDR option if peer has
			 * already done so.
			 */
			if (tp->sack_enable && ((flags & TH_ACK) == 0 ||
			    (tp->t_flags & TF_SACK_PERMIT))) {
				*((u_int32_t *) (opt + optlen)) =
				    htonl(TCPOPT_SACK_PERMIT_HDR);
				optlen += 4;
			}
			if ((tp->t_flags & TF_REQ_SCALE) &&
			    ((flags & TH_ACK) == 0 ||
			    (tp->t_flags & TF_RCVD_SCALE))) {
				*((u_int32_t *) (opt + optlen)) = htonl(
					TCPOPT_NOP << 24 |
					TCPOPT_WINDOW << 16 |
					TCPOLEN_WINDOW << 8 |
					tp->request_r_scale);
				optlen += 4;
			}
		}
	}

	/*
	 * Send a timestamp and echo-reply if this is a SYN and our side
	 * wants to use timestamps (TF_REQ_TSTMP is set) or both our side
	 * and our peer have sent timestamps in our SYN's.
	 */
	if ((tp->t_flags & (TF_REQ_TSTMP|TF_NOOPT)) == TF_REQ_TSTMP &&
	     (flags & TH_RST) == 0 &&
	    ((flags & (TH_SYN|TH_ACK)) == TH_SYN ||
	     (tp->t_flags & TF_RCVD_TSTMP))) {
		u_int32_t *lp = (u_int32_t *)(opt + optlen);

		/* Form timestamp option as shown in appendix A of RFC 1323. */
		*lp++ = htonl(TCPOPT_TSTAMP_HDR);
		*lp++ = htonl(now + tp->ts_modulate);
		*lp   = htonl(tp->ts_recent);
		optlen += TCPOLEN_TSTAMP_APPA;
	}
	/* Set receive buffer autosizing timestamp. */
	if (tp->rfbuf_ts == 0) {
		tp->rfbuf_ts = now;
		tp->rfbuf_cnt = 0;
	}

#ifdef TCP_SIGNATURE
	if (tp->t_flags & TF_SIGNATURE) {
		u_int8_t *bp = (u_int8_t *)(opt + optlen);

		/* Send signature option */
		*(bp++) = TCPOPT_SIGNATURE;
		*(bp++) = TCPOLEN_SIGNATURE;
		sigoff = optlen + 2;

		{
			unsigned int i;

			for (i = 0; i < 16; i++)
				*(bp++) = 0;
		}


		/* Pad options list to the next 32 bit boundary and
		 * terminate it.
		 */
		*bp++ = TCPOPT_NOP;
		*bp++ = TCPOPT_NOP;

		optlen += TCPOLEN_SIGLEN;
	}
#endif /* TCP_SIGNATURE */

	/*
	 * Send SACKs if necessary.  This should be the last option processed.
	 * Only as many SACKs are sent as are permitted by the maximum options
	 * size.  No more than three SACKs are sent.
	 */
	if (tp->sack_enable && tp->t_state == TCPS_ESTABLISHED &&
	    (tp->t_flags & (TF_SACK_PERMIT|TF_NOOPT)) == TF_SACK_PERMIT &&
	    tp->rcv_numsacks) {
		u_int32_t *lp = (u_int32_t *)(opt + optlen);
		u_int32_t *olp = lp++;
		int count = 0;  /* actual number of SACKs inserted */
		int maxsack = (MAX_TCPOPTLEN - (optlen + 4))/TCPOLEN_SACK;

		tcpstat_inc(tcps_sack_snd_opts);
		maxsack = min(maxsack, TCP_MAX_SACK);
		for (i = 0; (i < tp->rcv_numsacks && count < maxsack); i++) {
			struct sackblk sack = tp->sackblks[i];
			if (sack.start == 0 && sack.end == 0)
				continue;
			*lp++ = htonl(sack.start);
			*lp++ = htonl(sack.end);
			count++;
		}
		*olp = htonl(TCPOPT_SACK_HDR|(TCPOLEN_SACK*count+2));
		optlen += TCPOLEN_SACK*count + 4; /* including leading NOPs */
	}

#ifdef DIAGNOSTIC
	if (optlen > MAX_TCPOPTLEN)
		panic("tcp_output: options too long");
#endif /* DIAGNOSTIC */

	hdrlen += optlen;

	/*
	 * Adjust data length if insertion of options will
	 * bump the packet length beyond the t_maxopd length.
	 * Clear the FIN bit because we cut off the tail of
	 * the segment.
	 */
	if (len > tp->t_maxopd - optlen) {
		if (tso) {
			if (len + hdrlen + max_linkhdr > MAXMCLBYTES) {
				len = MAXMCLBYTES - hdrlen - max_linkhdr;
				sendalot = 1;
			}
		} else {
			len = tp->t_maxopd - optlen;
			sendalot = 1;
		}
		flags &= ~TH_FIN;
	}

#ifdef DIAGNOSTIC
	if (max_linkhdr + hdrlen > MCLBYTES)
		panic("tcphdr too big");
#endif

	/*
	 * Grab a header mbuf, attaching a copy of data to
	 * be transmitted, and initialize the header from
	 * the template for sends on this connection.
	 */
	if (len) {
		if (tp->t_force && len == 1)
			tcpstat_inc(tcps_sndprobe);
		else if (SEQ_LT(tp->snd_nxt, tp->snd_max)) {
			tcpstat_pkt(tcps_sndrexmitpack, tcps_sndrexmitbyte,
			    len);
			tp->t_sndrexmitpack++;
		} else {
			tcpstat_pkt(tcps_sndpack, tcps_sndbyte, len);
		}
#ifdef notyet
		if ((m = m_copypack(so->so_snd.sb_mb, off,
		    (int)len, max_linkhdr + hdrlen)) == 0) {
			error = ENOBUFS;
			goto out;
		}
		/*
		 * m_copypack left space for our hdr; use it.
		 */
		m->m_len += hdrlen;
		m->m_data -= hdrlen;
#else
		MGETHDR(m, M_DONTWAIT, MT_HEADER);
		if (m != NULL && max_linkhdr + hdrlen > MHLEN) {
			MCLGET(m, M_DONTWAIT);
			if ((m->m_flags & M_EXT) == 0) {
				m_freem(m);
				m = NULL;
			}
		}
		if (m == NULL) {
			error = ENOBUFS;
			goto out;
		}
		m->m_data += max_linkhdr;
		m->m_len = hdrlen;
		if (len <= m_trailingspace(m)) {
			m_copydata(so->so_snd.sb_mb, off, (int) len,
			    mtod(m, caddr_t) + hdrlen);
			m->m_len += len;
		} else {
			m->m_next = m_copym(so->so_snd.sb_mb, off, (int) len,
			    M_NOWAIT);
			if (m->m_next == 0) {
				(void) m_free(m);
				error = ENOBUFS;
				goto out;
			}
		}
		if (so->so_snd.sb_mb->m_flags & M_PKTHDR)
			m->m_pkthdr.ph_loopcnt =
			    so->so_snd.sb_mb->m_pkthdr.ph_loopcnt;
#endif
		/*
		 * If we're sending everything we've got, set PUSH.
		 * (This will keep happy those implementations which only
		 * give data to the user when a buffer fills or
		 * a PUSH comes in.)
		 */
		if (off + len == so->so_snd.sb_cc && !doing_sosend)
			flags |= TH_PUSH;
		tp->t_sndtime = now;
	} else {
		if (tp->t_flags & TF_ACKNOW)
			tcpstat_inc(tcps_sndacks);
		else if (flags & (TH_SYN|TH_FIN|TH_RST))
			tcpstat_inc(tcps_sndctrl);
		else if (SEQ_GT(tp->snd_up, tp->snd_una))
			tcpstat_inc(tcps_sndurg);
		else
			tcpstat_inc(tcps_sndwinup);

		MGETHDR(m, M_DONTWAIT, MT_HEADER);
		if (m != NULL && max_linkhdr + hdrlen > MHLEN) {
			MCLGET(m, M_DONTWAIT);
			if ((m->m_flags & M_EXT) == 0) {
				m_freem(m);
				m = NULL;
			}
		}
		if (m == NULL) {
			error = ENOBUFS;
			goto out;
		}
		m->m_data += max_linkhdr;
		m->m_len = hdrlen;
	}
	m->m_pkthdr.ph_ifidx = 0;
	m->m_pkthdr.len = hdrlen + len;

	/* Enable TSO and specify the size of the resulting segments. */
	if (tso) {
		SET(m->m_pkthdr.csum_flags, M_TCP_TSO);
		m->m_pkthdr.ph_mss = tp->t_maxseg;
	}

	if (!tp->t_template)
		panic("tcp_output");
#ifdef DIAGNOSTIC
	if (tp->t_template->m_len != hdrlen - optlen)
		panic("tcp_output: template len != hdrlen - optlen");
#endif /* DIAGNOSTIC */
	memcpy(mtod(m, caddr_t), mtod(tp->t_template, caddr_t),
	    tp->t_template->m_len);
	th = (struct tcphdr *)(mtod(m, caddr_t) + tp->t_template->m_len -
		sizeof(struct tcphdr));

	/*
	 * Fill in fields, remembering maximum advertised
	 * window for use in delaying messages about window sizes.
	 * If resending a FIN, be sure not to use a new sequence number.
	 */
	if ((flags & TH_FIN) && (tp->t_flags & TF_SENTFIN) &&
	    (tp->snd_nxt == tp->snd_max))
		tp->snd_nxt--;
	/*
	 * If we are doing retransmissions, then snd_nxt will
	 * not reflect the first unsent octet.  For ACK only
	 * packets, we do not want the sequence number of the
	 * retransmitted packet, we want the sequence number
	 * of the next unsent octet.  So, if there is no data
	 * (and no SYN or FIN), use snd_max instead of snd_nxt
	 * when filling in ti_seq.  But if we are in persist
	 * state, snd_max might reflect one byte beyond the
	 * right edge of the window, so use snd_nxt in that
	 * case, since we know we aren't doing a retransmission.
	 * (retransmit and persist are mutually exclusive...)
	 */
	if (len || (flags & (TH_SYN|TH_FIN)) ||
	    TCP_TIMER_ISARMED(tp, TCPT_PERSIST))
		th->th_seq = htonl(tp->snd_nxt);
	else
		th->th_seq = htonl(tp->snd_max);

	if (sack_rxmit) {
		/*
		 * If sendalot was turned on (due to option stuffing), turn it
		 * off. Properly set th_seq field.  Advance the ret'x pointer
		 * by len.
		 */
		if (sendalot)
			sendalot = 0;
		th->th_seq = htonl(p->rxmit);
		p->rxmit += len;
		tcpstat_pkt(tcps_sack_rexmits, tcps_sack_rexmit_bytes, len);
	}

	th->th_ack = htonl(tp->rcv_nxt);
	if (optlen) {
		memcpy(th + 1, opt, optlen);
		th->th_off = (sizeof (struct tcphdr) + optlen) >> 2;
	}
#ifdef TCP_ECN
	if (do_ecn) {
		/*
		 * if we have received congestion experienced segs,
		 * set ECE bit.
		 */
		if (tp->t_flags & TF_RCVD_CE) {
			flags |= TH_ECE;
			tcpstat_inc(tcps_ecn_sndece);
		}
		if (!(tp->t_flags & TF_DISABLE_ECN)) {
			/*
			 * if this is a SYN seg, set ECE and CWR.
			 * set only ECE for SYN-ACK if peer supports ECN.
			 */
			if ((flags & (TH_SYN|TH_ACK)) == TH_SYN)
				flags |= (TH_ECE|TH_CWR);
			else if ((tp->t_flags & TF_ECN_PERMIT) &&
			    (flags & (TH_SYN|TH_ACK)) == (TH_SYN|TH_ACK))
				flags |= TH_ECE;
		}
		/*
		 * if we have reduced the congestion window, notify
		 * the peer by setting CWR bit.
		 */
		if ((tp->t_flags & TF_ECN_PERMIT) &&
		    (tp->t_flags & TF_SEND_CWR)) {
			flags |= TH_CWR;
			tp->t_flags &= ~TF_SEND_CWR;
			tcpstat_inc(tcps_ecn_sndcwr);
		}
	}
#endif
	th->th_flags = flags;

	/*
	 * Calculate receive window.  Don't shrink window,
	 * but avoid silly window syndrome.
	 */
	if (win < (rcv_hiwat / 4) && win < (long)tp->t_maxseg)
		win = 0;
	if (win > (long)TCP_MAXWIN << tp->rcv_scale)
		win = (long)TCP_MAXWIN << tp->rcv_scale;
	if (win < (long)(int32_t)(tp->rcv_adv - tp->rcv_nxt))
		win = (long)(int32_t)(tp->rcv_adv - tp->rcv_nxt);
	if (flags & TH_RST)
		win = 0;
	th->th_win = htons((u_int16_t) (win>>tp->rcv_scale));
	if (th->th_win == 0)
		tp->t_sndzerowin++;
	if (SEQ_GT(tp->snd_up, tp->snd_nxt)) {
		u_int32_t urp = tp->snd_up - tp->snd_nxt;
		if (urp > IP_MAXPACKET)
			urp = IP_MAXPACKET;
		th->th_urp = htons((u_int16_t)urp);
		th->th_flags |= TH_URG;
	} else
		/*
		 * If no urgent pointer to send, then we pull
		 * the urgent pointer to the left edge of the send window
		 * so that it doesn't drift into the send window on sequence
		 * number wraparound.
		 */
		tp->snd_up = tp->snd_una;		/* drag it along */

#ifdef TCP_SIGNATURE
	if (tp->t_flags & TF_SIGNATURE) {
		int iphlen;
		union sockaddr_union src, dst;
		struct tdb *tdb;

		bzero(&src, sizeof(union sockaddr_union));
		bzero(&dst, sizeof(union sockaddr_union));

		switch (tp->pf) {
		case 0:	/*default to PF_INET*/
		case AF_INET:
			iphlen = sizeof(struct ip);
			src.sa.sa_len = sizeof(struct sockaddr_in);
			src.sa.sa_family = AF_INET;
			src.sin.sin_addr = mtod(m, struct ip *)->ip_src;
			dst.sa.sa_len = sizeof(struct sockaddr_in);
			dst.sa.sa_family = AF_INET;
			dst.sin.sin_addr = mtod(m, struct ip *)->ip_dst;
			break;
#ifdef INET6
		case AF_INET6:
			iphlen = sizeof(struct ip6_hdr);
			src.sa.sa_len = sizeof(struct sockaddr_in6);
			src.sa.sa_family = AF_INET6;
			src.sin6.sin6_addr = mtod(m, struct ip6_hdr *)->ip6_src;
			dst.sa.sa_len = sizeof(struct sockaddr_in6);
			dst.sa.sa_family = AF_INET6;
			dst.sin6.sin6_addr = mtod(m, struct ip6_hdr *)->ip6_dst;
			break;
#endif /* INET6 */
		}

		tdb = gettdbbysrcdst(rtable_l2(tp->t_inpcb->inp_rtableid),
		    0, &src, &dst, IPPROTO_TCP);
		if (tdb == NULL) {
			m_freem(m);
			return (EPERM);
		}

		if (tcp_signature(tdb, tp->pf, m, th, iphlen, 0,
		    mtod(m, caddr_t) + hdrlen - optlen + sigoff) < 0) {
			m_freem(m);
			tdb_unref(tdb);
			return (EINVAL);
		}
		tdb_unref(tdb);
	}
#endif /* TCP_SIGNATURE */

	/* Defer checksumming until later (ip_output() or hardware) */
	m->m_pkthdr.csum_flags |= M_TCP_CSUM_OUT;

	/*
	 * In transmit state, time the transmission and arrange for
	 * the retransmit.  In persist state, just set snd_max.
	 */
	if (tp->t_force == 0 || TCP_TIMER_ISARMED(tp, TCPT_PERSIST) == 0) {
		tcp_seq startseq = tp->snd_nxt;

		/*
		 * Advance snd_nxt over sequence space of this segment.
		 */
		if (flags & (TH_SYN|TH_FIN)) {
			if (flags & TH_SYN)
				tp->snd_nxt++;
			if (flags & TH_FIN) {
				tp->snd_nxt++;
				tp->t_flags |= TF_SENTFIN;
			}
		}
		if (tp->sack_enable) {
			if (sack_rxmit && (p->rxmit != tp->snd_nxt)) {
				goto timer;
			}
		}
		tp->snd_nxt += len;
		if (SEQ_GT(tp->snd_nxt, tp->snd_max)) {
			tp->snd_max = tp->snd_nxt;
			/*
			 * Time this transmission if not a retransmission and
			 * not currently timing anything.
			 */
			if (tp->t_rtttime == 0) {
				tp->t_rtttime = now;
				tp->t_rtseq = startseq;
				tcpstat_inc(tcps_segstimed);
			}
		}

		/*
		 * Set retransmit timer if not currently set,
		 * and not doing an ack or a keep-alive probe.
		 * Initial value for retransmit timer is smoothed
		 * round-trip time + 2 * round-trip time variance.
		 * Initialize shift counter which is used for backoff
		 * of retransmit time.
		 */
 timer:
		if (tp->sack_enable && sack_rxmit &&
		    TCP_TIMER_ISARMED(tp, TCPT_REXMT) == 0 &&
		    tp->snd_nxt != tp->snd_max) {
			TCP_TIMER_ARM(tp, TCPT_REXMT, tp->t_rxtcur);
			if (TCP_TIMER_ISARMED(tp, TCPT_PERSIST)) {
				TCP_TIMER_DISARM(tp, TCPT_PERSIST);
				tp->t_rxtshift = 0;
			}
		}

		if (TCP_TIMER_ISARMED(tp, TCPT_REXMT) == 0 &&
		    tp->snd_nxt != tp->snd_una) {
			TCP_TIMER_ARM(tp, TCPT_REXMT, tp->t_rxtcur);
			if (TCP_TIMER_ISARMED(tp, TCPT_PERSIST)) {
				TCP_TIMER_DISARM(tp, TCPT_PERSIST);
				tp->t_rxtshift = 0;
			}
		}

		if (len == 0 && so->so_snd.sb_cc &&
		    TCP_TIMER_ISARMED(tp, TCPT_REXMT) == 0 &&
		    TCP_TIMER_ISARMED(tp, TCPT_PERSIST) == 0) {
			/*
			 * Avoid a situation where we do not set persist timer
			 * after a zero window condition. For example:
			 * 1) A -> B: packet with enough data to fill the window
			 * 2) B -> A: ACK for #1 + new data (0 window
			 *    advertisement)
			 * 3) A -> B: ACK for #2, 0 len packet
			 *
			 * In this case, A will not activate the persist timer,
			 * because it chose to send a packet. Unless tcp_output
			 * is called for some other reason (delayed ack timer,
			 * another input packet from B, socket syscall), A will
			 * not send zero window probes.
			 *
			 * So, if you send a 0-length packet, but there is data
			 * in the socket buffer, and neither the rexmt or
			 * persist timer is already set, then activate the
			 * persist timer.
			 */
			tp->t_rxtshift = 0;
			tcp_setpersist(tp);
		}
	} else
		if (SEQ_GT(tp->snd_nxt + len, tp->snd_max))
			tp->snd_max = tp->snd_nxt + len;

	tcp_update_sndspace(tp);

	/*
	 * Trace.
	 */
	if (so->so_options & SO_DEBUG)
		tcp_trace(TA_OUTPUT, tp->t_state, tp, tp, mtod(m, caddr_t), 0,
			len);

	/*
	 * Fill in IP length and desired time to live and
	 * send to IP level.  There should be a better way
	 * to handle ttl and tos; we could keep them in
	 * the template, but need a way to checksum without them.
	 */

#ifdef TCP_ECN
	/*
	 * if peer is ECN capable, set the ECT bit in the IP header.
	 * but don't set ECT for a pure ack, a retransmit or a window probe.
	 */
	needect = 0;
	if (do_ecn && (tp->t_flags & TF_ECN_PERMIT)) {
		if (len == 0 || SEQ_LT(tp->snd_nxt, tp->snd_max) ||
		    (tp->t_force && len == 1)) {
			/* don't set ECT */
		} else {
			needect = 1;
			tcpstat_inc(tcps_ecn_sndect);
		}
	}
#endif

	/* force routing table */
	m->m_pkthdr.ph_rtableid = tp->t_inpcb->inp_rtableid;

#if NPF > 0
	pf_mbuf_link_inpcb(m, tp->t_inpcb);
#endif
#if NSTOEPLITZ > 0
	m->m_pkthdr.ph_flowid = tp->t_inpcb->inp_flowid;
	SET(m->m_pkthdr.csum_flags, M_FLOWID);
#endif

	switch (tp->pf) {
	case 0:	/*default to PF_INET*/
	case AF_INET:
		{
			struct ip *ip;

			ip = mtod(m, struct ip *);
			ip->ip_len = htons(m->m_pkthdr.len);
			packetlen = m->m_pkthdr.len;
			ip->ip_ttl = tp->t_inpcb->inp_ip.ip_ttl;
			ip->ip_tos = tp->t_inpcb->inp_ip.ip_tos;
#ifdef TCP_ECN
			if (needect)
				ip->ip_tos |= IPTOS_ECN_ECT0;
#endif
		}
		error = ip_output(m, tp->t_inpcb->inp_options,
		    &tp->t_inpcb->inp_route,
		    (atomic_load_int(&ip_mtudisc) ? IP_MTUDISC : 0), NULL,
		    &tp->t_inpcb->inp_seclevel, 0);
		break;
#ifdef INET6
	case AF_INET6:
		{
			struct ip6_hdr *ip6;

			ip6 = mtod(m, struct ip6_hdr *);
			ip6->ip6_plen = m->m_pkthdr.len -
				sizeof(struct ip6_hdr);
			packetlen = m->m_pkthdr.len;
			ip6->ip6_nxt = IPPROTO_TCP;
			ip6->ip6_hlim = in6_selecthlim(tp->t_inpcb);
#ifdef TCP_ECN
			if (needect)
				ip6->ip6_flow |= htonl(IPTOS_ECN_ECT0 << 20);
#endif
		}
		error = ip6_output(m, tp->t_inpcb->inp_outputopts6,
		    &tp->t_inpcb->inp_route, 0, NULL,
		    &tp->t_inpcb->inp_seclevel);
		break;
#endif /* INET6 */
	}

	if (error) {
out:
		if (error == ENOBUFS) {
			/*
			 * If the interface queue is full, or IP cannot
			 * get an mbuf, trigger TCP slow start.
			 */
			tp->snd_cwnd = tp->t_maxseg;
			return (0);
		}
		if (error == EMSGSIZE) {
			/*
			 * ip_output() will have already fixed the route
			 * for us.  tcp_mtudisc() will, as its last action,
			 * initiate retransmission, so it is important to
			 * not do so here.
			 */
			tcp_mtudisc(tp->t_inpcb, -1);
			return (0);
		}
		if ((error == EHOSTUNREACH || error == ENETDOWN) &&
		    TCPS_HAVERCVDSYN(tp->t_state)) {
			tp->t_softerror = error;
			return (0);
		}

		/* Restart the delayed ACK timer, if necessary. */
		if (TCP_TIMER_ISARMED(tp, TCPT_DELACK))
			TCP_TIMER_ARM(tp, TCPT_DELACK, tcp_delack_msecs);

		return (error);
	}

	if (packetlen > tp->t_pmtud_mtu_sent)
		tp->t_pmtud_mtu_sent = packetlen;

	tcpstat_inc(tcps_sndtotal);
	if (TCP_TIMER_ISARMED(tp, TCPT_DELACK))
		tcpstat_inc(tcps_delack);

	/*
	 * Data sent (as far as we can tell).
	 * If this advertises a larger window than any other segment,
	 * then remember the size of the advertised window.
	 * Any pending ACK has now been sent.
	 */
	if (win > 0 && SEQ_GT(tp->rcv_nxt+win, tp->rcv_adv))
		tp->rcv_adv = tp->rcv_nxt + win;
	tp->last_ack_sent = tp->rcv_nxt;
	tp->t_sndacktime = now;
	tp->t_flags &= ~TF_ACKNOW;
	TCP_TIMER_DISARM(tp, TCPT_DELACK);
	if (sendalot)
		goto again;
	return (0);
}

void
tcp_setpersist(struct tcpcb *tp)
{
	int t = ((tp->t_srtt >> 2) + tp->t_rttvar) >> (1 + TCP_RTT_BASE_SHIFT);
	int msec;

	if (TCP_TIMER_ISARMED(tp, TCPT_REXMT))
		panic("tcp_output REXMT");
	/*
	 * Start/restart persistence timer.
	 */
	if (t < tp->t_rttmin)
		t = tp->t_rttmin;
	TCPT_RANGESET(msec, t * tcp_backoff[tp->t_rxtshift],
	    TCPTV_PERSMIN, TCPTV_PERSMAX);
	TCP_TIMER_ARM(tp, TCPT_PERSIST, msec);
	if (tp->t_rxtshift < TCP_MAXRXTSHIFT)
		tp->t_rxtshift++;
}

int
tcp_softtso_chop(struct mbuf_list *ml, struct mbuf *m0, struct ifnet *ifp,
    u_int mss)
{
	struct ip *ip = NULL;
#ifdef INET6
	struct ip6_hdr *ip6 = NULL;
#endif
	struct tcphdr *th;
	int firstlen, iphlen, hlen, tlen, off;
	int error;

	ml_init(ml);
	ml_enqueue(ml, m0);

	if (mss == 0) {
		error = EINVAL;
		goto bad;
	}

	ip = mtod(m0, struct ip *);
	switch (ip->ip_v) {
	case 4:
		iphlen = ip->ip_hl << 2;
		if (ISSET(ip->ip_off, htons(IP_OFFMASK | IP_MF)) ||
		    iphlen != sizeof(struct ip) || ip->ip_p != IPPROTO_TCP) {
			/* only TCP without fragment or IP option supported */
			error = EPROTOTYPE;
			goto bad;
		}
		break;
#ifdef INET6
	case 6:
		ip = NULL;
		ip6 = mtod(m0, struct ip6_hdr *);
		iphlen = sizeof(struct ip6_hdr);
		if (ip6->ip6_nxt != IPPROTO_TCP) {
			/* only TCP without IPv6 header chain supported */
			error = EPROTOTYPE;
			goto bad;
		}
		break;
#endif
	default:
		panic("%s: unknown ip version %d", __func__, ip->ip_v);
	}

	tlen = m0->m_pkthdr.len;
	if (tlen < iphlen + sizeof(struct tcphdr)) {
		error = ENOPROTOOPT;
		goto bad;
	}
	/* IP and TCP header should be contiguous, this check is paranoia */
	if (m0->m_len < iphlen + sizeof(*th)) {
		ml_dequeue(ml);
		if ((m0 = m_pullup(m0, iphlen + sizeof(*th))) == NULL) {
			error = ENOBUFS;
			goto bad;
		}
		ml_enqueue(ml, m0);
	}
	th = (struct tcphdr *)(mtod(m0, caddr_t) + iphlen);
	hlen = iphlen + (th->th_off << 2);
	if (tlen < hlen) {
		error = ENOPROTOOPT;
		goto bad;
	}
	firstlen = MIN(tlen - hlen, mss);

	CLR(m0->m_pkthdr.csum_flags, M_TCP_TSO);

	/*
	 * Loop through length of payload after first segment,
	 * make new header and copy data of each part and link onto chain.
	 */
	for (off = hlen + firstlen; off < tlen; off += mss) {
		struct mbuf *m;
		struct tcphdr *mhth;
		int len;

		len = MIN(tlen - off, mss);

		MGETHDR(m, M_DONTWAIT, MT_HEADER);
		if (m == NULL) {
			error = ENOBUFS;
			goto bad;
		}
		ml_enqueue(ml, m);
		if ((error = m_dup_pkthdr(m, m0, M_DONTWAIT)) != 0)
			goto bad;

		/* IP and TCP header to the end, space for link layer header */
		m->m_len = hlen;
		m_align(m, hlen);

		/* copy and adjust TCP header */
		mhth = (struct tcphdr *)(mtod(m, caddr_t) + iphlen);
		memcpy(mhth, th, hlen - iphlen);
		mhth->th_seq = htonl(ntohl(th->th_seq) + (off - hlen));
		if (off + len < tlen)
			CLR(mhth->th_flags, TH_PUSH|TH_FIN);

		/* add mbuf chain with payload */
		m->m_pkthdr.len = hlen + len;
		if ((m->m_next = m_copym(m0, off, len, M_DONTWAIT)) == NULL) {
			error = ENOBUFS;
			goto bad;
		}

		/* copy and adjust IP header, calculate checksum */
		SET(m->m_pkthdr.csum_flags, M_TCP_CSUM_OUT);
		if (ip) {
			struct ip *mhip;

			mhip = mtod(m, struct ip *);
			*mhip = *ip;
			mhip->ip_len = htons(hlen + len);
			mhip->ip_id = htons(ip_randomid());
			in_hdr_cksum_out(m, ifp);
			in_proto_cksum_out(m, ifp);
		}
#ifdef INET6
		if (ip6) {
			struct ip6_hdr *mhip6;

			mhip6 = mtod(m, struct ip6_hdr *);
			*mhip6 = *ip6;
			mhip6->ip6_plen = htons(hlen - iphlen + len);
			in6_proto_cksum_out(m, ifp);
		}
#endif
	}

	/*
	 * Update first segment by trimming what's been copied out
	 * and updating header, then send each segment (in order).
	 */
	if (hlen + firstlen < tlen) {
		m_adj(m0, hlen + firstlen - tlen);
		CLR(th->th_flags, TH_PUSH|TH_FIN);
	}
	/* adjust IP header, calculate checksum */
	SET(m0->m_pkthdr.csum_flags, M_TCP_CSUM_OUT);
	if (ip) {
		ip->ip_len = htons(m0->m_pkthdr.len);
		in_hdr_cksum_out(m0, ifp);
		in_proto_cksum_out(m0, ifp);
	}
#ifdef INET6
	if (ip6) {
		ip6->ip6_plen = htons(m0->m_pkthdr.len - iphlen);
		in6_proto_cksum_out(m0, ifp);
	}
#endif

	tcpstat_add(tcps_outpkttso, ml_len(ml));
	return 0;

 bad:
	tcpstat_inc(tcps_outbadtso);
	ml_purge(ml);
	return error;
}

int
tcp_if_output_tso(struct ifnet *ifp, struct mbuf **mp, struct sockaddr *dst,
    struct rtentry *rt, uint32_t ifcap, u_int mtu)
{
	struct mbuf_list ml;
	int error;

	/* caller must fail later or fragment */
	if (!ISSET((*mp)->m_pkthdr.csum_flags, M_TCP_TSO))
		return 0;
	if ((*mp)->m_pkthdr.ph_mss > mtu) {
		CLR((*mp)->m_pkthdr.csum_flags, M_TCP_TSO);
		return 0;
	}

	/* network interface hardware will do TSO */
	if (in_ifcap_cksum(*mp, ifp, ifcap)) {
		if (ISSET(ifcap, IFCAP_TSOv4)) {
			in_hdr_cksum_out(*mp, ifp);
			in_proto_cksum_out(*mp, ifp);
		}
#ifdef INET6
		if (ISSET(ifcap, IFCAP_TSOv6))
			in6_proto_cksum_out(*mp, ifp);
#endif
		error = ifp->if_output(ifp, *mp, dst, rt);
		if (!error)
			tcpstat_inc(tcps_outhwtso);
		goto done;
	}

	/* as fallback do TSO in software */
	if ((error = tcp_softtso_chop(&ml, *mp, ifp, (*mp)->m_pkthdr.ph_mss)) ||
	    (error = if_output_ml(ifp, &ml, dst, rt)))
		goto done;
	tcpstat_inc(tcps_outswtso);

 done:
	*mp = NULL;
	return error;
}
