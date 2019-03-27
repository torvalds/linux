/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1982, 1986, 1988, 1990, 1993, 1994, 1995
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
 *	@(#)tcp_input.c	8.12 (Berkeley) 5/24/95
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_inet.h"
#include "opt_inet6.h"
#include "opt_tcpdebug.h"

/* For debugging we want counters and BB logging */
/* #define TCP_REASS_COUNTERS 1 */
/* #define TCP_REASS_LOGGING 1 */

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/eventhandler.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/sysctl.h>
#include <sys/syslog.h>
#include <sys/systm.h>

#include <vm/uma.h>

#include <net/if.h>
#include <net/if_var.h>
#include <net/route.h>
#include <net/vnet.h>

#include <netinet/in.h>
#include <netinet/in_pcb.h>
#include <netinet/in_systm.h>
#include <netinet/in_var.h>
#include <netinet/ip.h>
#include <netinet/ip_var.h>
#include <netinet/ip_options.h>
#include <netinet/ip6.h>
#include <netinet6/in6_pcb.h>
#include <netinet6/ip6_var.h>
#include <netinet6/nd6.h>
#include <netinet/tcp.h>
#include <netinet/tcp_fsm.h>
#include <netinet/tcp_seq.h>
#include <netinet/tcp_timer.h>
#include <netinet/tcp_var.h>
#ifdef TCP_REASS_LOGGING
#include <netinet/tcp_log_buf.h>
#include <netinet/tcp_hpts.h>
#endif
#include <netinet6/tcp6_var.h>
#include <netinet/tcpip.h>
#ifdef TCPDEBUG
#include <netinet/tcp_debug.h>
#endif /* TCPDEBUG */

#define TCP_R_LOG_ADD		1
#define TCP_R_LOG_LIMIT_REACHED 2
#define TCP_R_LOG_APPEND	3
#define TCP_R_LOG_PREPEND	4
#define TCP_R_LOG_REPLACE	5
#define TCP_R_LOG_MERGE_INTO	6
#define TCP_R_LOG_NEW_ENTRY	7
#define TCP_R_LOG_READ		8
#define TCP_R_LOG_ZERO		9
#define TCP_R_LOG_DUMP		10
#define TCP_R_LOG_TRIM		11

static SYSCTL_NODE(_net_inet_tcp, OID_AUTO, reass, CTLFLAG_RW, 0,
    "TCP Segment Reassembly Queue");

static SYSCTL_NODE(_net_inet_tcp_reass, OID_AUTO, stats, CTLFLAG_RW, 0,
    "TCP Segment Reassembly stats");


static int tcp_reass_maxseg = 0;
SYSCTL_INT(_net_inet_tcp_reass, OID_AUTO, maxsegments, CTLFLAG_RDTUN,
    &tcp_reass_maxseg, 0,
    "Global maximum number of TCP Segments in Reassembly Queue");

static uma_zone_t tcp_reass_zone;
SYSCTL_UMA_CUR(_net_inet_tcp_reass, OID_AUTO, cursegments, 0,
    &tcp_reass_zone,
    "Global number of TCP Segments currently in Reassembly Queue");

static u_int tcp_reass_maxqueuelen = 100;
SYSCTL_UINT(_net_inet_tcp_reass, OID_AUTO, maxqueuelen, CTLFLAG_RWTUN,
    &tcp_reass_maxqueuelen, 0,
    "Maximum number of TCP Segments per Reassembly Queue");

static int tcp_new_limits = 0;
SYSCTL_INT(_net_inet_tcp_reass, OID_AUTO, new_limit, CTLFLAG_RWTUN,
    &tcp_new_limits, 0,
    "Do we use the new limit method we are discussing?");

static u_int tcp_reass_queue_guard = 16;
SYSCTL_UINT(_net_inet_tcp_reass, OID_AUTO, queueguard, CTLFLAG_RWTUN,
    &tcp_reass_queue_guard, 16,
    "Number of TCP Segments in Reassembly Queue where we flip over to guard mode");

#ifdef TCP_REASS_COUNTERS

counter_u64_t reass_entry;
SYSCTL_COUNTER_U64(_net_inet_tcp_reass_stats, OID_AUTO, entry, CTLFLAG_RD,
    &reass_entry, "A segment entered reassembly ");

counter_u64_t reass_path1;
SYSCTL_COUNTER_U64(_net_inet_tcp_reass_stats, OID_AUTO, path1, CTLFLAG_RD,
    &reass_path1, "Took path 1");

counter_u64_t reass_path2;
SYSCTL_COUNTER_U64(_net_inet_tcp_reass_stats, OID_AUTO, path2, CTLFLAG_RD,
    &reass_path2, "Took path 2");

counter_u64_t reass_path3;
SYSCTL_COUNTER_U64(_net_inet_tcp_reass_stats, OID_AUTO, path3, CTLFLAG_RD,
    &reass_path3, "Took path 3");

counter_u64_t reass_path4;
SYSCTL_COUNTER_U64(_net_inet_tcp_reass_stats, OID_AUTO, path4, CTLFLAG_RD,
    &reass_path4, "Took path 4");

counter_u64_t reass_path5;
SYSCTL_COUNTER_U64(_net_inet_tcp_reass_stats, OID_AUTO, path5, CTLFLAG_RD,
    &reass_path5, "Took path 5");

counter_u64_t reass_path6;
SYSCTL_COUNTER_U64(_net_inet_tcp_reass_stats, OID_AUTO, path6, CTLFLAG_RD,
    &reass_path6, "Took path 6");

counter_u64_t reass_path7;
SYSCTL_COUNTER_U64(_net_inet_tcp_reass_stats, OID_AUTO, path7, CTLFLAG_RD,
    &reass_path7, "Took path 7");

counter_u64_t reass_fullwalk;
SYSCTL_COUNTER_U64(_net_inet_tcp_reass_stats, OID_AUTO, fullwalk, CTLFLAG_RD,
    &reass_fullwalk, "Took a full walk ");

counter_u64_t reass_nospace;
SYSCTL_COUNTER_U64(_net_inet_tcp_reass_stats, OID_AUTO, nospace, CTLFLAG_RD,
    &reass_nospace, "Had no mbuf capacity ");

counter_u64_t merge_fwd;
SYSCTL_COUNTER_U64(_net_inet_tcp_reass_stats, OID_AUTO, merge_fwd, CTLFLAG_RD,
    &merge_fwd, "Ran merge fwd");

counter_u64_t merge_into;
SYSCTL_COUNTER_U64(_net_inet_tcp_reass_stats, OID_AUTO, merge_into, CTLFLAG_RD,
    &merge_into, "Ran merge into");

counter_u64_t tcp_zero_input;
SYSCTL_COUNTER_U64(_net_inet_tcp_reass_stats, OID_AUTO, zero_input, CTLFLAG_RD,
    &tcp_zero_input, "The reassembly buffer saw a zero len segment etc");

#endif

/* Initialize TCP reassembly queue */
static void
tcp_reass_zone_change(void *tag)
{

	/* Set the zone limit and read back the effective value. */
	tcp_reass_maxseg = nmbclusters / 16;
	tcp_reass_maxseg = uma_zone_set_max(tcp_reass_zone,
	    tcp_reass_maxseg);
}

#ifdef TCP_REASS_LOGGING

static void
tcp_log_reassm(struct tcpcb *tp, struct tseg_qent *q, struct tseg_qent *p,
    tcp_seq seq, int len, uint8_t action, int instance)
{
	uint32_t cts;
	struct timeval tv;

	if (tp->t_logstate != TCP_LOG_STATE_OFF) {
		union tcp_log_stackspecific log;

		memset(&log, 0, sizeof(log));
		cts = tcp_get_usecs(&tv);
		log.u_bbr.flex1 = seq;
		log.u_bbr.cur_del_rate = (uint64_t)q;
		log.u_bbr.delRate = (uint64_t)p;
		if (q != NULL) {
			log.u_bbr.flex2 = q->tqe_start;
			log.u_bbr.flex3 = q->tqe_len;
			log.u_bbr.flex4 = q->tqe_mbuf_cnt;
			log.u_bbr.hptsi_gain = q->tqe_flags;
		}
		if (p != NULL)  {
			log.u_bbr.flex5 = p->tqe_start;
			log.u_bbr.pkts_out = p->tqe_len;
			log.u_bbr.epoch = p->tqe_mbuf_cnt;
			log.u_bbr.cwnd_gain = p->tqe_flags;
		}
		log.u_bbr.flex6 = tp->t_segqmbuflen;
		log.u_bbr.flex7 = instance;
		log.u_bbr.flex8 = action;
		log.u_bbr.timeStamp = cts;
		TCP_LOG_EVENTP(tp, NULL,
		    &tp->t_inpcb->inp_socket->so_rcv,
		    &tp->t_inpcb->inp_socket->so_snd,
		    TCP_LOG_REASS, 0,
		    len, &log, false, &tv);
	}
}

static void
tcp_reass_log_dump(struct tcpcb *tp)
{
	struct tseg_qent *q;

	if (tp->t_logstate != TCP_LOG_STATE_OFF) {
		TAILQ_FOREACH(q, &tp->t_segq, tqe_q) {
			tcp_log_reassm(tp, q, NULL, q->tqe_start, q->tqe_len, TCP_R_LOG_DUMP, 0);
		}
	};
}

static void
tcp_reass_log_new_in(struct tcpcb *tp, tcp_seq seq, int len, struct mbuf *m,
    int logval, struct tseg_qent *q)
{
	int cnt;
	struct mbuf *t;

	cnt = 0;
	t = m;
	while (t) {
		cnt += t->m_len;
		t = t->m_next;
	}
	tcp_log_reassm(tp, q, NULL, seq, len, logval, cnt);
}

#endif

void
tcp_reass_global_init(void)
{

	tcp_reass_maxseg = nmbclusters / 16;
	TUNABLE_INT_FETCH("net.inet.tcp.reass.maxsegments",
	    &tcp_reass_maxseg);
	tcp_reass_zone = uma_zcreate("tcpreass", sizeof (struct tseg_qent),
	    NULL, NULL, NULL, NULL, UMA_ALIGN_PTR, 0);
	/* Set the zone limit and read back the effective value. */
	tcp_reass_maxseg = uma_zone_set_max(tcp_reass_zone,
	    tcp_reass_maxseg);
#ifdef TCP_REASS_COUNTERS
	reass_path1 = counter_u64_alloc(M_WAITOK);
	reass_path2 = counter_u64_alloc(M_WAITOK);
	reass_path3 = counter_u64_alloc(M_WAITOK);
	reass_path4 = counter_u64_alloc(M_WAITOK);
	reass_path5 = counter_u64_alloc(M_WAITOK);
	reass_path6 = counter_u64_alloc(M_WAITOK);
	reass_path7 = counter_u64_alloc(M_WAITOK);
	reass_fullwalk = counter_u64_alloc(M_WAITOK);
	reass_nospace = counter_u64_alloc(M_WAITOK);
	reass_entry = counter_u64_alloc(M_WAITOK);
	merge_fwd = counter_u64_alloc(M_WAITOK);
	merge_into = counter_u64_alloc(M_WAITOK);
	tcp_zero_input = counter_u64_alloc(M_WAITOK);
#endif
	EVENTHANDLER_REGISTER(nmbclusters_change,
	    tcp_reass_zone_change, NULL, EVENTHANDLER_PRI_ANY);

}

void
tcp_reass_flush(struct tcpcb *tp)
{
	struct tseg_qent *qe;

	INP_WLOCK_ASSERT(tp->t_inpcb);

	while ((qe = TAILQ_FIRST(&tp->t_segq)) != NULL) {
		TAILQ_REMOVE(&tp->t_segq, qe, tqe_q);
		m_freem(qe->tqe_m);
		uma_zfree(tcp_reass_zone, qe);
		tp->t_segqlen--;
	}
	tp->t_segqmbuflen = 0;
	KASSERT((tp->t_segqlen == 0),
	    ("TCP reass queue %p segment count is %d instead of 0 after flush.",
	    tp, tp->t_segqlen));
}

static void
tcp_reass_append(struct tcpcb *tp, struct tseg_qent *last,
    struct mbuf *m, struct tcphdr *th, int tlen, 
    struct mbuf *mlast, int lenofoh)
{

#ifdef TCP_REASS_LOGGING
	tcp_log_reassm(tp, last, NULL, th->th_seq, tlen, TCP_R_LOG_APPEND, 0);
#endif
	last->tqe_len += tlen;
	last->tqe_m->m_pkthdr.len += tlen;
	/* Preserve the FIN bit if its there */
	last->tqe_flags |= (th->th_flags & TH_FIN);
	last->tqe_last->m_next = m;
	last->tqe_last = mlast;
	last->tqe_mbuf_cnt += lenofoh;
	tp->t_rcvoopack++;
	TCPSTAT_INC(tcps_rcvoopack);
	TCPSTAT_ADD(tcps_rcvoobyte, tlen);
#ifdef TCP_REASS_LOGGING
	tcp_reass_log_new_in(tp, last->tqe_start, lenofoh, last->tqe_m,
			     TCP_R_LOG_APPEND,
			     last);
#endif
}

static void
tcp_reass_prepend(struct tcpcb *tp, struct tseg_qent *first, struct mbuf *m, struct tcphdr *th,
		  int tlen, struct mbuf *mlast, int lenofoh)
{
	int i;
	
#ifdef TCP_REASS_LOGGING
	tcp_log_reassm(tp, first, NULL, th->th_seq, tlen, TCP_R_LOG_PREPEND, 0);
#endif
	if (SEQ_GT((th->th_seq + tlen), first->tqe_start)) {
		/* The new data overlaps into the old */
		i = (th->th_seq + tlen) - first->tqe_start;
#ifdef TCP_REASS_LOGGING
		tcp_log_reassm(tp, first, NULL, 0, i, TCP_R_LOG_TRIM, 1);
#endif
		m_adj(first->tqe_m, i);
		first->tqe_len -= i;
		first->tqe_start += i;
	}
	/* Ok now setup our chain to point to the old first */
	mlast->m_next = first->tqe_m;
	first->tqe_m = m;
	first->tqe_len += tlen;
	first->tqe_start = th->th_seq;
	first->tqe_m->m_pkthdr.len = first->tqe_len;
	first->tqe_mbuf_cnt += lenofoh;
	tp->t_rcvoopack++;
	TCPSTAT_INC(tcps_rcvoopack);
	TCPSTAT_ADD(tcps_rcvoobyte, tlen);
#ifdef TCP_REASS_LOGGING
	tcp_reass_log_new_in(tp, first->tqe_start, lenofoh, first->tqe_m,
			     TCP_R_LOG_PREPEND,
			     first);
#endif
}

static void 
tcp_reass_replace(struct tcpcb *tp, struct tseg_qent *q, struct mbuf *m,
    tcp_seq seq, int len, struct mbuf *mlast, int mbufoh, uint8_t flags)
{
	/*
	 * Free the data in q, and replace
	 * it with the new segment.
	 */
	int len_dif;

#ifdef TCP_REASS_LOGGING
	tcp_log_reassm(tp, q, NULL, seq, len, TCP_R_LOG_REPLACE, 0);
#endif
	m_freem(q->tqe_m);
	KASSERT(tp->t_segqmbuflen >= q->tqe_mbuf_cnt,
		("Tp:%p seg queue goes negative", tp));
	tp->t_segqmbuflen -= q->tqe_mbuf_cnt;		       
	q->tqe_mbuf_cnt = mbufoh;
	q->tqe_m = m;
	q->tqe_last = mlast;
	q->tqe_start = seq;
	if (len > q->tqe_len)
		len_dif = len - q->tqe_len;
	else
		len_dif = 0;
	tp->t_rcvoopack++;
	TCPSTAT_INC(tcps_rcvoopack);
	TCPSTAT_ADD(tcps_rcvoobyte, len_dif);
	q->tqe_len = len;
	q->tqe_flags = (flags & TH_FIN);
	q->tqe_m->m_pkthdr.len = q->tqe_len;
	tp->t_segqmbuflen += mbufoh;

}

static void
tcp_reass_merge_into(struct tcpcb *tp, struct tseg_qent *ent,
    struct tseg_qent *q)
{
	/* 
	 * Merge q into ent and free q from the list.
	 */
#ifdef TCP_REASS_LOGGING
	tcp_log_reassm(tp, q, ent, 0, 0, TCP_R_LOG_MERGE_INTO, 0);
#endif
#ifdef TCP_REASS_COUNTERS
	counter_u64_add(merge_into, 1);
#endif
	ent->tqe_last->m_next = q->tqe_m;
	ent->tqe_last = q->tqe_last;
	ent->tqe_len += q->tqe_len;
	ent->tqe_mbuf_cnt += q->tqe_mbuf_cnt;
	ent->tqe_m->m_pkthdr.len += q->tqe_len;
	ent->tqe_flags |= (q->tqe_flags & TH_FIN);
	TAILQ_REMOVE(&tp->t_segq, q, tqe_q);
	uma_zfree(tcp_reass_zone, q);
	tp->t_segqlen--;

}

static void
tcp_reass_merge_forward(struct tcpcb *tp, struct tseg_qent *ent)
{
	struct tseg_qent *q, *qtmp;
	int i;
	tcp_seq max;
	/*
	 * Given an entry merge forward anyplace
	 * that ent overlaps forward.
	 */

	max = ent->tqe_start + ent->tqe_len;
	q = TAILQ_NEXT(ent, tqe_q);
	if (q == NULL) {
		/* Nothing left */
		return;
	}
	TAILQ_FOREACH_FROM_SAFE(q, &tp->t_segq, tqe_q, qtmp) {
		if (SEQ_GT(q->tqe_start, max)) {
			/* Beyond q */
			break;
		}
		/* We have some or all that are overlapping */
		if (SEQ_GEQ(max, (q->tqe_start + q->tqe_len))) {
			/* It consumes it all */
			tp->t_segqmbuflen -= q->tqe_mbuf_cnt;
			m_freem(q->tqe_m);
			TAILQ_REMOVE(&tp->t_segq, q, tqe_q);
			uma_zfree(tcp_reass_zone, q);
			tp->t_segqlen--;
			continue;
		}
		/* 
		 * Trim the q entry to dovetail to this one 
		 * and then merge q into ent updating max
		 * in the process.
		 */
		i = max - q->tqe_start;
#ifdef TCP_REASS_LOGGING
		tcp_log_reassm(tp, q, NULL, 0, i, TCP_R_LOG_TRIM, 2);
#endif
		m_adj(q->tqe_m, i);
		q->tqe_len -= i;
		q->tqe_start += i;
		tcp_reass_merge_into(tp, ent, q);
		max = ent->tqe_start + ent->tqe_len;
	}
#ifdef TCP_REASS_COUNTERS
	counter_u64_add(merge_fwd, 1);
#endif
}

static int 
tcp_reass_overhead_of_chain(struct mbuf *m, struct mbuf **mlast)
{
	int len = MSIZE;

	if (m->m_flags & M_EXT)
		len += m->m_ext.ext_size;
	while (m->m_next != NULL) {
		m = m->m_next;
		len += MSIZE;
		if (m->m_flags & M_EXT)
			len += m->m_ext.ext_size;
	}
	*mlast = m;
	return (len);
}


/*
 * NOTE!!! the new tcp-reassembly code *must not* use
 * m_adj() with a negative index. That alters the chain
 * of mbufs (by possibly chopping trailing mbufs). At
 * the front of tcp_reass we count the mbuf overhead
 * and setup the tail pointer. If we use m_adj(m, -5)
 * we could corrupt the tail pointer. Currently the
 * code only uses m_adj(m, postive-num). If this
 * changes appropriate changes to update mlast would
 * be needed.
 */
int
tcp_reass(struct tcpcb *tp, struct tcphdr *th, tcp_seq *seq_start,
	  int *tlenp, struct mbuf *m)
{
	struct tseg_qent *q, *last, *first;
	struct tseg_qent *p = NULL;
	struct tseg_qent *nq = NULL;
	struct tseg_qent *te = NULL;
	struct mbuf *mlast = NULL;
	struct sockbuf *sb;
	struct socket *so = tp->t_inpcb->inp_socket;
	char *s = NULL;
	int flags, i, lenofoh;

	INP_WLOCK_ASSERT(tp->t_inpcb);
	/*
	 * XXX: tcp_reass() is rather inefficient with its data structures
	 * and should be rewritten (see NetBSD for optimizations).
	 */

	KASSERT(th == NULL || (seq_start != NULL && tlenp != NULL),
	        ("tcp_reass called with illegal parameter combination "
	         "(tp=%p, th=%p, seq_start=%p, tlenp=%p, m=%p)",
	         tp, th, seq_start, tlenp, m));
	/*
	 * Call with th==NULL after become established to
	 * force pre-ESTABLISHED data up to user socket.
	 */
	if (th == NULL)
		goto present;
	KASSERT(SEQ_GEQ(th->th_seq, tp->rcv_nxt),
		("Attempt to add old entry to reassembly queue (th=%p, tp=%p)",
		 th, tp));
#ifdef TCP_REASS_LOGGING
	tcp_reass_log_new_in(tp, th->th_seq, *tlenp, m, TCP_R_LOG_ADD, NULL);
#endif
#ifdef TCP_REASS_COUNTERS
	counter_u64_add(reass_entry, 1);
#endif
	/*
	 * Check for zero length data.
	 */
	if ((*tlenp == 0) && ((th->th_flags & TH_FIN) == 0)) {
		/*
		 * A zero length segment does no
		 * one any good. We could check
		 * the rcv_nxt <-> rcv_wnd but thats
		 * already done for us by the caller.
		 */
#ifdef TCP_REASS_COUNTERS 
		counter_u64_add(tcp_zero_input, 1);
#endif
		m_freem(m);
#ifdef TCP_REASS_LOGGING
		tcp_reass_log_dump(tp);
#endif
		return (0);
	}
	/*
	 * Will it fit?
	 */
	lenofoh = tcp_reass_overhead_of_chain(m, &mlast);
	sb = &tp->t_inpcb->inp_socket->so_rcv;
	if ((th->th_seq != tp->rcv_nxt || !TCPS_HAVEESTABLISHED(tp->t_state)) &&
	    (sb->sb_mbcnt + tp->t_segqmbuflen + lenofoh) > sb->sb_mbmax) {
		/* No room */
		TCPSTAT_INC(tcps_rcvreassfull);
#ifdef TCP_REASS_COUNTERS
		counter_u64_add(reass_nospace, 1);
#endif
#ifdef TCP_REASS_LOGGING
		tcp_log_reassm(tp, NULL, NULL, th->th_seq, lenofoh, TCP_R_LOG_LIMIT_REACHED, 0);
#endif
		if ((s = tcp_log_addrs(&tp->t_inpcb->inp_inc, th, NULL, NULL))) {
			log(LOG_DEBUG, "%s; %s: mbuf count limit reached, "
			    "segment dropped\n", s, __func__);
			free(s, M_TCPLOG);
		}
		m_freem(m);
		*tlenp = 0;
#ifdef TCP_REASS_LOGGING
		tcp_reass_log_dump(tp);
#endif
		return (0);
	}
	/*
	 * First lets deal with two common cases, the
	 * segment appends to the back of our collected
	 * segments. Or the segment is the next in line.
	 */
	last = TAILQ_LAST_FAST(&tp->t_segq, tseg_qent, tqe_q);
	if (last != NULL) {
		if ((th->th_flags & TH_FIN) &&
		    SEQ_LT((th->th_seq + *tlenp), (last->tqe_start + last->tqe_len))) {
			/* 
			 * Someone is trying to game us, dump
			 * the segment.
			 */
			*tlenp = 0;
			m_freem(m);
			return (0);
		}
		if ((SEQ_GEQ(th->th_seq, last->tqe_start)) &&
		    (SEQ_GEQ((last->tqe_start + last->tqe_len), th->th_seq))) {
			/* Common case, trailing segment is added */
			/**
			 *                                 +--last
			 *                                 v
			 *  reassembly buffer |---|  |---| |---|
			 *  new segment                       |---|
			 */
#ifdef TCP_REASS_COUNTERS
			counter_u64_add(reass_path1, 1);
#endif
			if (SEQ_GT((last->tqe_start + last->tqe_len), th->th_seq)) {
				i = (last->tqe_start + last->tqe_len) - th->th_seq;
				if (i < *tlenp) {
#ifdef TCP_REASS_LOGGING
					tcp_log_reassm(tp, last, NULL, 0, i, TCP_R_LOG_TRIM, 3);
					th->th_seq += i;
#endif
					m_adj(m, i);
					*tlenp -= i;
				} else {
					/* Complete overlap */
					TCPSTAT_INC(tcps_rcvduppack);
					TCPSTAT_ADD(tcps_rcvdupbyte, *tlenp);
					m_freem(m);
					*tlenp = last->tqe_len;
					*seq_start = last->tqe_start;
					return (0);
				}
			}
			if (last->tqe_flags & TH_FIN) {
				/* 
				 * We have data after the FIN on the last? 
				 */
				*tlenp = 0;
				m_freem(m);
				return(0);
			}
			tcp_reass_append(tp, last, m, th, *tlenp, mlast, lenofoh);
			tp->t_segqmbuflen += lenofoh;
			*seq_start = last->tqe_start;
			*tlenp = last->tqe_len;
			return (0);
		} else if (SEQ_GT(th->th_seq, (last->tqe_start + last->tqe_len))) {
			/* 
			 * Second common case, we missed
			 * another one and have something more
			 * for the end.
			 */
			/**
			 *                                 +--last
			 *                                 v
			 *  reassembly buffer |---|  |---| |---|
			 *  new segment                           |---|
			 */
			if (last->tqe_flags & TH_FIN) {
				/* 
				 * We have data after the FIN on the last? 
				 */
				*tlenp = 0;
				m_freem(m);
				return(0);
			}
#ifdef TCP_REASS_COUNTERS
			counter_u64_add(reass_path2, 1);
#endif
			p = last;
			goto new_entry;
		}
	} else {
		/* First segment (it's NULL). */
		goto new_entry;
	}
	first = TAILQ_FIRST(&tp->t_segq);
	if (SEQ_LT(th->th_seq, first->tqe_start) &&
	    SEQ_GEQ((th->th_seq + *tlenp),first->tqe_start) &&
	    SEQ_LT((th->th_seq + *tlenp), (first->tqe_start + first->tqe_len))) {
		/*
		 * The head of the queue is prepended by this and
		 * it may be the one I want most.
		 */
		/**
		 *       first-------+
		 *                   v
		 *  rea:             |---|  |---| |---|
		 *  new:         |---|
		 * Note the case we do not deal with here is:
		 *   rea=     |---|   |---|   |---|
		 *   new=  |----|
		 * Due to the fact that it could be
		 *   new   |--------------------|
		 * And we might need to merge forward.
		 */
#ifdef INVARIANTS
		struct mbuf *firstmbuf;
#endif

#ifdef TCP_REASS_COUNTERS
		counter_u64_add(reass_path3, 1);
#endif
		if (SEQ_LT(th->th_seq, tp->rcv_nxt)) {
			/* 
			 * The resend was even before 
			 * what we have. We need to trim it.
			 * Note TSNH (it should be trimmed
			 * before the call to tcp_reass()).
			 */
#ifdef INVARIANTS
			panic("th->th_seq:%u rcv_nxt:%u tp:%p not pre-trimmed",
			      th->th_seq, tp->rcv_nxt, tp);
#else
			i = tp->rcv_nxt - th->th_seq;
#ifdef TCP_REASS_LOGGING
			tcp_log_reassm(tp, first, NULL, 0, i, TCP_R_LOG_TRIM, 4);
#endif
			m_adj(m, i);
			th->th_seq += i;
			*tlenp -= i;
#endif
		}
#ifdef INVARIANTS
		firstmbuf = first->tqe_m;
#endif
		tcp_reass_prepend(tp, first, m, th, *tlenp, mlast, lenofoh);
#ifdef INVARIANTS
		if (firstmbuf == first->tqe_m) {
			panic("First stayed same m:%p foobar:%p first->tqe_m:%p tp:%p first:%p",
			      m, firstmbuf, first->tqe_m, tp, first);
		} else if (first->tqe_m != m) {
			panic("First did not change to m:%p foobar:%p first->tqe_m:%p tp:%p first:%p",
			      m, firstmbuf, first->tqe_m, tp, first);
		}
#endif
		tp->t_segqmbuflen += lenofoh;
		*seq_start = first->tqe_start;
		*tlenp = first->tqe_len;
		goto present;
	} else if (SEQ_LT((th->th_seq + *tlenp), first->tqe_start)) {
		/* New segment is before our earliest segment. */
		/**
		 *           first---->+
		 *                      v
		 *  rea=                |---| ....
		 *  new"         |---|
		 *
		 */
		goto new_entry;
	}
	/*
	 * Find a segment which begins after this one does.
	 */
#ifdef TCP_REASS_COUNTERS
	counter_u64_add(reass_fullwalk, 1);
#endif
	TAILQ_FOREACH(q, &tp->t_segq, tqe_q) {
		if (SEQ_GT(q->tqe_start, th->th_seq))
			break;
	}
	p = TAILQ_PREV(q, tsegqe_head, tqe_q);
	/**
	 * Now is this fit just in-between only? 
	 * i.e.:
	 *      p---+        +----q
	 *          v        v
	 *     res= |--|     |--|    |--|
	 *     nee       |-|
	 */
	if (SEQ_LT((th->th_seq + *tlenp), q->tqe_start) &&
	    ((p == NULL) || (SEQ_GT(th->th_seq, (p->tqe_start + p->tqe_len))))) {
		/* Yep no overlap */
		goto new_entry;
	}
	/**
	 * If we reach here we have some (possibly all) overlap
	 * such as:
	 *     res=     |--|     |--|    |--|
	 *     new=  |----|
	 * or  new=  |-----------------|
	 * or  new=      |--------|
	 * or  new=            |---|
	 * or  new=            |-----------|
	 */
	if ((p != NULL) &&
	    (SEQ_LEQ(th->th_seq, (p->tqe_start + p->tqe_len)))) {
		/* conversion to int (in i) handles seq wraparound */

#ifdef TCP_REASS_COUNTERS
		counter_u64_add(reass_path4, 1);
#endif
		i = p->tqe_start + p->tqe_len - th->th_seq;
		if (i >= 0) {
			if (i >= *tlenp) {
				/**
				 *       prev seg---->+
				 *                    v
				 *  reassembly buffer |---|
				 *  new segment        |-|
				 */
				TCPSTAT_INC(tcps_rcvduppack);
				TCPSTAT_ADD(tcps_rcvdupbyte, *tlenp);
				*tlenp = p->tqe_len;
				*seq_start = p->tqe_start;
				m_freem(m);
				/*
				 * Try to present any queued data
				 * at the left window edge to the user.
				 * This is needed after the 3-WHS
				 * completes. Note this probably
				 * will not work and we will return.
				 */
				return (0);
			}
			if (i > 0) {
				/**
				 *       prev seg---->+
				 *                    v
				 *  reassembly buffer |---|
				 *  new segment         |-----|
				 */
#ifdef TCP_REASS_COUNTERS
				counter_u64_add(reass_path5, 1);
#endif
#ifdef TCP_REASS_LOGGING
				tcp_log_reassm(tp, p, NULL, 0, i, TCP_R_LOG_TRIM, 5);
#endif
				m_adj(m, i);
				*tlenp -= i;
				th->th_seq += i;
			}
		}
		if (th->th_seq == (p->tqe_start + p->tqe_len)) {
			/* 
			 * If dovetails in with this one 
			 * append it.
			 */
			/**
			 *       prev seg---->+
			 *                    v
			 *  reassembly buffer |--|     |---|
			 *  new segment          |--|
			 * (note: it was trimmed above if it overlapped)
			 */
			tcp_reass_append(tp, p, m, th, *tlenp, mlast, lenofoh);
			tp->t_segqmbuflen += lenofoh;
		} else {
#ifdef INVARIANTS
			panic("Impossible cut th_seq:%u p->seq:%u(%d) p:%p tp:%p",
			      th->th_seq, p->tqe_start, p->tqe_len,
			      p, tp);
#endif
			*tlenp = 0;
			m_freem(m);
			return (0);
		}
		q = p;
	} else {
		/*
		 * The new data runs over the 
		 * top of previously sack'd data (in q).
		 * It may be partially overlapping, or
		 * it may overlap the entire segment.
		 */
#ifdef TCP_REASS_COUNTERS
		counter_u64_add(reass_path6, 1);
#endif
		if (SEQ_GEQ((th->th_seq + *tlenp), (q->tqe_start + q->tqe_len))) {
			/* It consumes it all */
			/**
			 *             next seg---->+
			 *                          v
			 *  reassembly buffer |--|     |---|
			 *  new segment              |----------|
			 */
#ifdef TCP_REASS_COUNTERS
			counter_u64_add(reass_path7, 1);
#endif
			tcp_reass_replace(tp, q, m, th->th_seq, *tlenp, mlast, lenofoh, th->th_flags);
		} else {
			/* 
			 * We just need to prepend the data
			 * to this. It does not overrun
			 * the end.
			 */
			/**
			 *                next seg---->+
			 *                             v
			 *  reassembly buffer |--|     |---|
			 *  new segment                   |----------|
			 */
			tcp_reass_prepend(tp, q, m, th, *tlenp, mlast, lenofoh);
			tp->t_segqmbuflen += lenofoh;
		}
	}
	/* Now does it go further than that? */
	tcp_reass_merge_forward(tp, q);
	*seq_start = q->tqe_start;
	*tlenp = q->tqe_len;
	goto present;

	/* 
	 * When we reach here we can't combine it 
	 * with any existing segment.
	 *
	 * Limit the number of segments that can be queued to reduce the
	 * potential for mbuf exhaustion. For best performance, we want to be
	 * able to queue a full window's worth of segments. The size of the
	 * socket receive buffer determines our advertised window and grows
	 * automatically when socket buffer autotuning is enabled. Use it as the
	 * basis for our queue limit.
	 *
	 * However, allow the user to specify a ceiling for the number of
	 * segments in each queue.
	 *
	 * Always let the missing segment through which caused this queue.
	 * NB: Access to the socket buffer is left intentionally unlocked as we
	 * can tolerate stale information here.
	 *
	 * XXXLAS: Using sbspace(so->so_rcv) instead of so->so_rcv.sb_hiwat
	 * should work but causes packets to be dropped when they shouldn't.
	 * Investigate why and re-evaluate the below limit after the behaviour
	 * is understood.
	 */
new_entry:
	if (th->th_seq == tp->rcv_nxt && TCPS_HAVEESTABLISHED(tp->t_state)) {
		tp->rcv_nxt += *tlenp;
		flags = th->th_flags & TH_FIN;
		TCPSTAT_INC(tcps_rcvoopack);
		TCPSTAT_ADD(tcps_rcvoobyte, *tlenp);
		SOCKBUF_LOCK(&so->so_rcv);
		if (so->so_rcv.sb_state & SBS_CANTRCVMORE) {
			m_freem(m);
		} else {
			sbappendstream_locked(&so->so_rcv, m, 0);
		}
		sorwakeup_locked(so);
		return (flags);
	}
	if (tcp_new_limits) {
		if ((tp->t_segqlen > tcp_reass_queue_guard) &&
		    (*tlenp < MSIZE)) {
			/* 
			 * This is really a lie, we are not full but
			 * are getting a segment that is above 
			 * guard threshold. If it is and its below
			 * a mbuf size (256) we drop it if it
			 * can't fill in some place.
			 */
			TCPSTAT_INC(tcps_rcvreassfull);
			*tlenp = 0;
			if ((s = tcp_log_addrs(&tp->t_inpcb->inp_inc, th, NULL, NULL))) {
				log(LOG_DEBUG, "%s; %s: queue limit reached, "
				    "segment dropped\n", s, __func__);
				free(s, M_TCPLOG);
			}
			m_freem(m);
#ifdef TCP_REASS_LOGGING
			tcp_reass_log_dump(tp);
#endif
			return (0);
		}
	} else {
		if (tp->t_segqlen >= min((so->so_rcv.sb_hiwat / tp->t_maxseg) + 1,
					 tcp_reass_maxqueuelen)) {
			TCPSTAT_INC(tcps_rcvreassfull);
			*tlenp = 0;
			if ((s = tcp_log_addrs(&tp->t_inpcb->inp_inc, th, NULL, NULL))) {
				log(LOG_DEBUG, "%s; %s: queue limit reached, "
				    "segment dropped\n", s, __func__);
				free(s, M_TCPLOG);
			}
			m_freem(m);
#ifdef TCP_REASS_LOGGING
			tcp_reass_log_dump(tp);
#endif
			return (0);
		}
	}
	/*
	 * Allocate a new queue entry. If we can't, or hit the zone limit
	 * just drop the pkt.
	 */
	te = uma_zalloc(tcp_reass_zone, M_NOWAIT);
	if (te == NULL) {
		TCPSTAT_INC(tcps_rcvmemdrop);
		m_freem(m);
		*tlenp = 0;
		if ((s = tcp_log_addrs(&tp->t_inpcb->inp_inc, th, NULL,
				       NULL))) {
			log(LOG_DEBUG, "%s; %s: global zone limit "
			    "reached, segment dropped\n", s, __func__);
			free(s, M_TCPLOG);
		}
		return (0);
	}
	tp->t_segqlen++;
	tp->t_rcvoopack++;
	TCPSTAT_INC(tcps_rcvoopack);
	TCPSTAT_ADD(tcps_rcvoobyte, *tlenp);
	/* Insert the new segment queue entry into place. */
	te->tqe_m = m;
	te->tqe_flags = th->th_flags;
	te->tqe_len = *tlenp;
	te->tqe_start = th->th_seq;
	te->tqe_last = mlast;
	te->tqe_mbuf_cnt = lenofoh;
	tp->t_segqmbuflen += te->tqe_mbuf_cnt;
	if (p == NULL) {
		TAILQ_INSERT_HEAD(&tp->t_segq, te, tqe_q);
	} else {
		TAILQ_INSERT_AFTER(&tp->t_segq, p, te, tqe_q);
	}
#ifdef TCP_REASS_LOGGING
	tcp_reass_log_new_in(tp, th->th_seq, *tlenp, m, TCP_R_LOG_NEW_ENTRY, te);
#endif
present:
	/*
	 * Present data to user, advancing rcv_nxt through
	 * completed sequence space.
	 */
	if (!TCPS_HAVEESTABLISHED(tp->t_state))
		return (0);
	q = TAILQ_FIRST(&tp->t_segq);
	KASSERT(q == NULL || SEQ_GEQ(q->tqe_start, tp->rcv_nxt),
		("Reassembly queue for %p has stale entry at head", tp));
	if (!q || q->tqe_start != tp->rcv_nxt) {
#ifdef TCP_REASS_LOGGING
		tcp_reass_log_dump(tp);
#endif
		return (0);
	}
	SOCKBUF_LOCK(&so->so_rcv);
	do {
		tp->rcv_nxt += q->tqe_len;
		flags = q->tqe_flags & TH_FIN;
		nq = TAILQ_NEXT(q, tqe_q);
		TAILQ_REMOVE(&tp->t_segq, q, tqe_q);
		if (so->so_rcv.sb_state & SBS_CANTRCVMORE) {
			m_freem(q->tqe_m);
		} else {
#ifdef TCP_REASS_LOGGING
			tcp_reass_log_new_in(tp, q->tqe_start, q->tqe_len, q->tqe_m, TCP_R_LOG_READ, q);
			if (th != NULL) {
				tcp_log_reassm(tp, q, NULL, th->th_seq, *tlenp, TCP_R_LOG_READ, 1);
			} else {
				tcp_log_reassm(tp, q, NULL, 0, 0, TCP_R_LOG_READ, 1);
			}
#endif
			sbappendstream_locked(&so->so_rcv, q->tqe_m, 0);
		}
#ifdef TCP_REASS_LOGGING
		if (th != NULL) {
			tcp_log_reassm(tp, q, NULL, th->th_seq, *tlenp, TCP_R_LOG_READ, 2);
		} else {
			tcp_log_reassm(tp, q, NULL, 0, 0, TCP_R_LOG_READ, 2);
		}
#endif
		KASSERT(tp->t_segqmbuflen >= q->tqe_mbuf_cnt,
			("tp:%p seg queue goes negative", tp));
		tp->t_segqmbuflen -= q->tqe_mbuf_cnt;
		uma_zfree(tcp_reass_zone, q);
		tp->t_segqlen--;
		q = nq;
	} while (q && q->tqe_start == tp->rcv_nxt);
	if (TAILQ_EMPTY(&tp->t_segq) &&
	    (tp->t_segqmbuflen != 0)) {
#ifdef INVARIANTS
		panic("tp:%p segq:%p len:%d queue empty",
		      tp, &tp->t_segq, tp->t_segqmbuflen);
#else
#ifdef TCP_REASS_LOGGING
		if (th != NULL) {
			tcp_log_reassm(tp, NULL, NULL, th->th_seq, *tlenp, TCP_R_LOG_ZERO, 0);
		} else {
			tcp_log_reassm(tp, NULL, NULL, 0, 0, TCP_R_LOG_ZERO, 0);
		}
#endif
		tp->t_segqmbuflen = 0;
#endif
	}
#ifdef TCP_REASS_LOGGING
	tcp_reass_log_dump(tp);
#endif
	sorwakeup_locked(so);
	return (flags);
}
