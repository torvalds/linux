/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2001-2008, by Cisco Systems, Inc. All rights reserved.
 * Copyright (c) 2008-2012, by Randall Stewart. All rights reserved.
 * Copyright (c) 2008-2012, by Michael Tuexen. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * a) Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 * b) Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the distribution.
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <netinet/sctp_os.h>
#include <netinet/sctp_pcb.h>
#include <netinet/sctputil.h>
#include <netinet/sctp_var.h>
#include <netinet/sctp_sysctl.h>
#ifdef INET6
#include <netinet6/sctp6_var.h>
#endif
#include <netinet/sctp_header.h>
#include <netinet/sctp_output.h>
#include <netinet/sctp_uio.h>
#include <netinet/sctp_timer.h>
#include <netinet/sctp_indata.h>
#include <netinet/sctp_auth.h>
#include <netinet/sctp_asconf.h>
#include <netinet/sctp_bsd_addr.h>
#if defined(INET6) || defined(INET)
#include <netinet/tcp_var.h>
#endif
#include <netinet/udp.h>
#include <netinet/udp_var.h>
#include <netinet/in_kdtrace.h>
#include <sys/proc.h>
#ifdef INET6
#include <netinet/icmp6.h>
#endif


#ifndef KTR_SCTP
#define KTR_SCTP KTR_SUBSYS
#endif

extern const struct sctp_cc_functions sctp_cc_functions[];
extern const struct sctp_ss_functions sctp_ss_functions[];

void
sctp_sblog(struct sockbuf *sb, struct sctp_tcb *stcb, int from, int incr)
{
#if defined(SCTP_LOCAL_TRACE_BUF)
	struct sctp_cwnd_log sctp_clog;

	sctp_clog.x.sb.stcb = stcb;
	sctp_clog.x.sb.so_sbcc = sb->sb_cc;
	if (stcb)
		sctp_clog.x.sb.stcb_sbcc = stcb->asoc.sb_cc;
	else
		sctp_clog.x.sb.stcb_sbcc = 0;
	sctp_clog.x.sb.incr = incr;
	SCTP_CTR6(KTR_SCTP, "SCTP:%d[%d]:%x-%x-%x-%x",
	    SCTP_LOG_EVENT_SB,
	    from,
	    sctp_clog.x.misc.log1,
	    sctp_clog.x.misc.log2,
	    sctp_clog.x.misc.log3,
	    sctp_clog.x.misc.log4);
#endif
}

void
sctp_log_closing(struct sctp_inpcb *inp, struct sctp_tcb *stcb, int16_t loc)
{
#if defined(SCTP_LOCAL_TRACE_BUF)
	struct sctp_cwnd_log sctp_clog;

	sctp_clog.x.close.inp = (void *)inp;
	sctp_clog.x.close.sctp_flags = inp->sctp_flags;
	if (stcb) {
		sctp_clog.x.close.stcb = (void *)stcb;
		sctp_clog.x.close.state = (uint16_t)stcb->asoc.state;
	} else {
		sctp_clog.x.close.stcb = 0;
		sctp_clog.x.close.state = 0;
	}
	sctp_clog.x.close.loc = loc;
	SCTP_CTR6(KTR_SCTP, "SCTP:%d[%d]:%x-%x-%x-%x",
	    SCTP_LOG_EVENT_CLOSE,
	    0,
	    sctp_clog.x.misc.log1,
	    sctp_clog.x.misc.log2,
	    sctp_clog.x.misc.log3,
	    sctp_clog.x.misc.log4);
#endif
}

void
rto_logging(struct sctp_nets *net, int from)
{
#if defined(SCTP_LOCAL_TRACE_BUF)
	struct sctp_cwnd_log sctp_clog;

	memset(&sctp_clog, 0, sizeof(sctp_clog));
	sctp_clog.x.rto.net = (void *)net;
	sctp_clog.x.rto.rtt = net->rtt / 1000;
	SCTP_CTR6(KTR_SCTP, "SCTP:%d[%d]:%x-%x-%x-%x",
	    SCTP_LOG_EVENT_RTT,
	    from,
	    sctp_clog.x.misc.log1,
	    sctp_clog.x.misc.log2,
	    sctp_clog.x.misc.log3,
	    sctp_clog.x.misc.log4);
#endif
}

void
sctp_log_strm_del_alt(struct sctp_tcb *stcb, uint32_t tsn, uint16_t sseq, uint16_t stream, int from)
{
#if defined(SCTP_LOCAL_TRACE_BUF)
	struct sctp_cwnd_log sctp_clog;

	sctp_clog.x.strlog.stcb = stcb;
	sctp_clog.x.strlog.n_tsn = tsn;
	sctp_clog.x.strlog.n_sseq = sseq;
	sctp_clog.x.strlog.e_tsn = 0;
	sctp_clog.x.strlog.e_sseq = 0;
	sctp_clog.x.strlog.strm = stream;
	SCTP_CTR6(KTR_SCTP, "SCTP:%d[%d]:%x-%x-%x-%x",
	    SCTP_LOG_EVENT_STRM,
	    from,
	    sctp_clog.x.misc.log1,
	    sctp_clog.x.misc.log2,
	    sctp_clog.x.misc.log3,
	    sctp_clog.x.misc.log4);
#endif
}

void
sctp_log_nagle_event(struct sctp_tcb *stcb, int action)
{
#if defined(SCTP_LOCAL_TRACE_BUF)
	struct sctp_cwnd_log sctp_clog;

	sctp_clog.x.nagle.stcb = (void *)stcb;
	sctp_clog.x.nagle.total_flight = stcb->asoc.total_flight;
	sctp_clog.x.nagle.total_in_queue = stcb->asoc.total_output_queue_size;
	sctp_clog.x.nagle.count_in_queue = stcb->asoc.chunks_on_out_queue;
	sctp_clog.x.nagle.count_in_flight = stcb->asoc.total_flight_count;
	SCTP_CTR6(KTR_SCTP, "SCTP:%d[%d]:%x-%x-%x-%x",
	    SCTP_LOG_EVENT_NAGLE,
	    action,
	    sctp_clog.x.misc.log1,
	    sctp_clog.x.misc.log2,
	    sctp_clog.x.misc.log3,
	    sctp_clog.x.misc.log4);
#endif
}

void
sctp_log_sack(uint32_t old_cumack, uint32_t cumack, uint32_t tsn, uint16_t gaps, uint16_t dups, int from)
{
#if defined(SCTP_LOCAL_TRACE_BUF)
	struct sctp_cwnd_log sctp_clog;

	sctp_clog.x.sack.cumack = cumack;
	sctp_clog.x.sack.oldcumack = old_cumack;
	sctp_clog.x.sack.tsn = tsn;
	sctp_clog.x.sack.numGaps = gaps;
	sctp_clog.x.sack.numDups = dups;
	SCTP_CTR6(KTR_SCTP, "SCTP:%d[%d]:%x-%x-%x-%x",
	    SCTP_LOG_EVENT_SACK,
	    from,
	    sctp_clog.x.misc.log1,
	    sctp_clog.x.misc.log2,
	    sctp_clog.x.misc.log3,
	    sctp_clog.x.misc.log4);
#endif
}

void
sctp_log_map(uint32_t map, uint32_t cum, uint32_t high, int from)
{
#if defined(SCTP_LOCAL_TRACE_BUF)
	struct sctp_cwnd_log sctp_clog;

	memset(&sctp_clog, 0, sizeof(sctp_clog));
	sctp_clog.x.map.base = map;
	sctp_clog.x.map.cum = cum;
	sctp_clog.x.map.high = high;
	SCTP_CTR6(KTR_SCTP, "SCTP:%d[%d]:%x-%x-%x-%x",
	    SCTP_LOG_EVENT_MAP,
	    from,
	    sctp_clog.x.misc.log1,
	    sctp_clog.x.misc.log2,
	    sctp_clog.x.misc.log3,
	    sctp_clog.x.misc.log4);
#endif
}

void
sctp_log_fr(uint32_t biggest_tsn, uint32_t biggest_new_tsn, uint32_t tsn, int from)
{
#if defined(SCTP_LOCAL_TRACE_BUF)
	struct sctp_cwnd_log sctp_clog;

	memset(&sctp_clog, 0, sizeof(sctp_clog));
	sctp_clog.x.fr.largest_tsn = biggest_tsn;
	sctp_clog.x.fr.largest_new_tsn = biggest_new_tsn;
	sctp_clog.x.fr.tsn = tsn;
	SCTP_CTR6(KTR_SCTP, "SCTP:%d[%d]:%x-%x-%x-%x",
	    SCTP_LOG_EVENT_FR,
	    from,
	    sctp_clog.x.misc.log1,
	    sctp_clog.x.misc.log2,
	    sctp_clog.x.misc.log3,
	    sctp_clog.x.misc.log4);
#endif
}

#ifdef SCTP_MBUF_LOGGING
void
sctp_log_mb(struct mbuf *m, int from)
{
#if defined(SCTP_LOCAL_TRACE_BUF)
	struct sctp_cwnd_log sctp_clog;

	sctp_clog.x.mb.mp = m;
	sctp_clog.x.mb.mbuf_flags = (uint8_t)(SCTP_BUF_GET_FLAGS(m));
	sctp_clog.x.mb.size = (uint16_t)(SCTP_BUF_LEN(m));
	sctp_clog.x.mb.data = SCTP_BUF_AT(m, 0);
	if (SCTP_BUF_IS_EXTENDED(m)) {
		sctp_clog.x.mb.ext = SCTP_BUF_EXTEND_BASE(m);
		sctp_clog.x.mb.refcnt = (uint8_t)(SCTP_BUF_EXTEND_REFCNT(m));
	} else {
		sctp_clog.x.mb.ext = 0;
		sctp_clog.x.mb.refcnt = 0;
	}
	SCTP_CTR6(KTR_SCTP, "SCTP:%d[%d]:%x-%x-%x-%x",
	    SCTP_LOG_EVENT_MBUF,
	    from,
	    sctp_clog.x.misc.log1,
	    sctp_clog.x.misc.log2,
	    sctp_clog.x.misc.log3,
	    sctp_clog.x.misc.log4);
#endif
}

void
sctp_log_mbc(struct mbuf *m, int from)
{
	struct mbuf *mat;

	for (mat = m; mat; mat = SCTP_BUF_NEXT(mat)) {
		sctp_log_mb(mat, from);
	}
}
#endif

void
sctp_log_strm_del(struct sctp_queued_to_read *control, struct sctp_queued_to_read *poschk, int from)
{
#if defined(SCTP_LOCAL_TRACE_BUF)
	struct sctp_cwnd_log sctp_clog;

	if (control == NULL) {
		SCTP_PRINTF("Gak log of NULL?\n");
		return;
	}
	sctp_clog.x.strlog.stcb = control->stcb;
	sctp_clog.x.strlog.n_tsn = control->sinfo_tsn;
	sctp_clog.x.strlog.n_sseq = (uint16_t)control->mid;
	sctp_clog.x.strlog.strm = control->sinfo_stream;
	if (poschk != NULL) {
		sctp_clog.x.strlog.e_tsn = poschk->sinfo_tsn;
		sctp_clog.x.strlog.e_sseq = (uint16_t)poschk->mid;
	} else {
		sctp_clog.x.strlog.e_tsn = 0;
		sctp_clog.x.strlog.e_sseq = 0;
	}
	SCTP_CTR6(KTR_SCTP, "SCTP:%d[%d]:%x-%x-%x-%x",
	    SCTP_LOG_EVENT_STRM,
	    from,
	    sctp_clog.x.misc.log1,
	    sctp_clog.x.misc.log2,
	    sctp_clog.x.misc.log3,
	    sctp_clog.x.misc.log4);
#endif
}

void
sctp_log_cwnd(struct sctp_tcb *stcb, struct sctp_nets *net, int augment, uint8_t from)
{
#if defined(SCTP_LOCAL_TRACE_BUF)
	struct sctp_cwnd_log sctp_clog;

	sctp_clog.x.cwnd.net = net;
	if (stcb->asoc.send_queue_cnt > 255)
		sctp_clog.x.cwnd.cnt_in_send = 255;
	else
		sctp_clog.x.cwnd.cnt_in_send = stcb->asoc.send_queue_cnt;
	if (stcb->asoc.stream_queue_cnt > 255)
		sctp_clog.x.cwnd.cnt_in_str = 255;
	else
		sctp_clog.x.cwnd.cnt_in_str = stcb->asoc.stream_queue_cnt;

	if (net) {
		sctp_clog.x.cwnd.cwnd_new_value = net->cwnd;
		sctp_clog.x.cwnd.inflight = net->flight_size;
		sctp_clog.x.cwnd.pseudo_cumack = net->pseudo_cumack;
		sctp_clog.x.cwnd.meets_pseudo_cumack = net->new_pseudo_cumack;
		sctp_clog.x.cwnd.need_new_pseudo_cumack = net->find_pseudo_cumack;
	}
	if (SCTP_CWNDLOG_PRESEND == from) {
		sctp_clog.x.cwnd.meets_pseudo_cumack = stcb->asoc.peers_rwnd;
	}
	sctp_clog.x.cwnd.cwnd_augment = augment;
	SCTP_CTR6(KTR_SCTP, "SCTP:%d[%d]:%x-%x-%x-%x",
	    SCTP_LOG_EVENT_CWND,
	    from,
	    sctp_clog.x.misc.log1,
	    sctp_clog.x.misc.log2,
	    sctp_clog.x.misc.log3,
	    sctp_clog.x.misc.log4);
#endif
}

void
sctp_log_lock(struct sctp_inpcb *inp, struct sctp_tcb *stcb, uint8_t from)
{
#if defined(SCTP_LOCAL_TRACE_BUF)
	struct sctp_cwnd_log sctp_clog;

	memset(&sctp_clog, 0, sizeof(sctp_clog));
	if (inp) {
		sctp_clog.x.lock.sock = (void *)inp->sctp_socket;

	} else {
		sctp_clog.x.lock.sock = (void *)NULL;
	}
	sctp_clog.x.lock.inp = (void *)inp;
	if (stcb) {
		sctp_clog.x.lock.tcb_lock = mtx_owned(&stcb->tcb_mtx);
	} else {
		sctp_clog.x.lock.tcb_lock = SCTP_LOCK_UNKNOWN;
	}
	if (inp) {
		sctp_clog.x.lock.inp_lock = mtx_owned(&inp->inp_mtx);
		sctp_clog.x.lock.create_lock = mtx_owned(&inp->inp_create_mtx);
	} else {
		sctp_clog.x.lock.inp_lock = SCTP_LOCK_UNKNOWN;
		sctp_clog.x.lock.create_lock = SCTP_LOCK_UNKNOWN;
	}
	sctp_clog.x.lock.info_lock = rw_wowned(&SCTP_BASE_INFO(ipi_ep_mtx));
	if (inp && (inp->sctp_socket)) {
		sctp_clog.x.lock.sock_lock = mtx_owned(&(inp->sctp_socket->so_rcv.sb_mtx));
		sctp_clog.x.lock.sockrcvbuf_lock = mtx_owned(&(inp->sctp_socket->so_rcv.sb_mtx));
		sctp_clog.x.lock.socksndbuf_lock = mtx_owned(&(inp->sctp_socket->so_snd.sb_mtx));
	} else {
		sctp_clog.x.lock.sock_lock = SCTP_LOCK_UNKNOWN;
		sctp_clog.x.lock.sockrcvbuf_lock = SCTP_LOCK_UNKNOWN;
		sctp_clog.x.lock.socksndbuf_lock = SCTP_LOCK_UNKNOWN;
	}
	SCTP_CTR6(KTR_SCTP, "SCTP:%d[%d]:%x-%x-%x-%x",
	    SCTP_LOG_LOCK_EVENT,
	    from,
	    sctp_clog.x.misc.log1,
	    sctp_clog.x.misc.log2,
	    sctp_clog.x.misc.log3,
	    sctp_clog.x.misc.log4);
#endif
}

void
sctp_log_maxburst(struct sctp_tcb *stcb, struct sctp_nets *net, int error, int burst, uint8_t from)
{
#if defined(SCTP_LOCAL_TRACE_BUF)
	struct sctp_cwnd_log sctp_clog;

	memset(&sctp_clog, 0, sizeof(sctp_clog));
	sctp_clog.x.cwnd.net = net;
	sctp_clog.x.cwnd.cwnd_new_value = error;
	sctp_clog.x.cwnd.inflight = net->flight_size;
	sctp_clog.x.cwnd.cwnd_augment = burst;
	if (stcb->asoc.send_queue_cnt > 255)
		sctp_clog.x.cwnd.cnt_in_send = 255;
	else
		sctp_clog.x.cwnd.cnt_in_send = stcb->asoc.send_queue_cnt;
	if (stcb->asoc.stream_queue_cnt > 255)
		sctp_clog.x.cwnd.cnt_in_str = 255;
	else
		sctp_clog.x.cwnd.cnt_in_str = stcb->asoc.stream_queue_cnt;
	SCTP_CTR6(KTR_SCTP, "SCTP:%d[%d]:%x-%x-%x-%x",
	    SCTP_LOG_EVENT_MAXBURST,
	    from,
	    sctp_clog.x.misc.log1,
	    sctp_clog.x.misc.log2,
	    sctp_clog.x.misc.log3,
	    sctp_clog.x.misc.log4);
#endif
}

void
sctp_log_rwnd(uint8_t from, uint32_t peers_rwnd, uint32_t snd_size, uint32_t overhead)
{
#if defined(SCTP_LOCAL_TRACE_BUF)
	struct sctp_cwnd_log sctp_clog;

	sctp_clog.x.rwnd.rwnd = peers_rwnd;
	sctp_clog.x.rwnd.send_size = snd_size;
	sctp_clog.x.rwnd.overhead = overhead;
	sctp_clog.x.rwnd.new_rwnd = 0;
	SCTP_CTR6(KTR_SCTP, "SCTP:%d[%d]:%x-%x-%x-%x",
	    SCTP_LOG_EVENT_RWND,
	    from,
	    sctp_clog.x.misc.log1,
	    sctp_clog.x.misc.log2,
	    sctp_clog.x.misc.log3,
	    sctp_clog.x.misc.log4);
#endif
}

void
sctp_log_rwnd_set(uint8_t from, uint32_t peers_rwnd, uint32_t flight_size, uint32_t overhead, uint32_t a_rwndval)
{
#if defined(SCTP_LOCAL_TRACE_BUF)
	struct sctp_cwnd_log sctp_clog;

	sctp_clog.x.rwnd.rwnd = peers_rwnd;
	sctp_clog.x.rwnd.send_size = flight_size;
	sctp_clog.x.rwnd.overhead = overhead;
	sctp_clog.x.rwnd.new_rwnd = a_rwndval;
	SCTP_CTR6(KTR_SCTP, "SCTP:%d[%d]:%x-%x-%x-%x",
	    SCTP_LOG_EVENT_RWND,
	    from,
	    sctp_clog.x.misc.log1,
	    sctp_clog.x.misc.log2,
	    sctp_clog.x.misc.log3,
	    sctp_clog.x.misc.log4);
#endif
}

#ifdef SCTP_MBCNT_LOGGING
static void
sctp_log_mbcnt(uint8_t from, uint32_t total_oq, uint32_t book, uint32_t total_mbcnt_q, uint32_t mbcnt)
{
#if defined(SCTP_LOCAL_TRACE_BUF)
	struct sctp_cwnd_log sctp_clog;

	sctp_clog.x.mbcnt.total_queue_size = total_oq;
	sctp_clog.x.mbcnt.size_change = book;
	sctp_clog.x.mbcnt.total_queue_mb_size = total_mbcnt_q;
	sctp_clog.x.mbcnt.mbcnt_change = mbcnt;
	SCTP_CTR6(KTR_SCTP, "SCTP:%d[%d]:%x-%x-%x-%x",
	    SCTP_LOG_EVENT_MBCNT,
	    from,
	    sctp_clog.x.misc.log1,
	    sctp_clog.x.misc.log2,
	    sctp_clog.x.misc.log3,
	    sctp_clog.x.misc.log4);
#endif
}
#endif

void
sctp_misc_ints(uint8_t from, uint32_t a, uint32_t b, uint32_t c, uint32_t d)
{
#if defined(SCTP_LOCAL_TRACE_BUF)
	SCTP_CTR6(KTR_SCTP, "SCTP:%d[%d]:%x-%x-%x-%x",
	    SCTP_LOG_MISC_EVENT,
	    from,
	    a, b, c, d);
#endif
}

void
sctp_wakeup_log(struct sctp_tcb *stcb, uint32_t wake_cnt, int from)
{
#if defined(SCTP_LOCAL_TRACE_BUF)
	struct sctp_cwnd_log sctp_clog;

	sctp_clog.x.wake.stcb = (void *)stcb;
	sctp_clog.x.wake.wake_cnt = wake_cnt;
	sctp_clog.x.wake.flight = stcb->asoc.total_flight_count;
	sctp_clog.x.wake.send_q = stcb->asoc.send_queue_cnt;
	sctp_clog.x.wake.sent_q = stcb->asoc.sent_queue_cnt;

	if (stcb->asoc.stream_queue_cnt < 0xff)
		sctp_clog.x.wake.stream_qcnt = (uint8_t)stcb->asoc.stream_queue_cnt;
	else
		sctp_clog.x.wake.stream_qcnt = 0xff;

	if (stcb->asoc.chunks_on_out_queue < 0xff)
		sctp_clog.x.wake.chunks_on_oque = (uint8_t)stcb->asoc.chunks_on_out_queue;
	else
		sctp_clog.x.wake.chunks_on_oque = 0xff;

	sctp_clog.x.wake.sctpflags = 0;
	/* set in the defered mode stuff */
	if (stcb->sctp_ep->sctp_flags & SCTP_PCB_FLAGS_DONT_WAKE)
		sctp_clog.x.wake.sctpflags |= 1;
	if (stcb->sctp_ep->sctp_flags & SCTP_PCB_FLAGS_WAKEOUTPUT)
		sctp_clog.x.wake.sctpflags |= 2;
	if (stcb->sctp_ep->sctp_flags & SCTP_PCB_FLAGS_WAKEINPUT)
		sctp_clog.x.wake.sctpflags |= 4;
	/* what about the sb */
	if (stcb->sctp_socket) {
		struct socket *so = stcb->sctp_socket;

		sctp_clog.x.wake.sbflags = (uint8_t)((so->so_snd.sb_flags & 0x00ff));
	} else {
		sctp_clog.x.wake.sbflags = 0xff;
	}
	SCTP_CTR6(KTR_SCTP, "SCTP:%d[%d]:%x-%x-%x-%x",
	    SCTP_LOG_EVENT_WAKE,
	    from,
	    sctp_clog.x.misc.log1,
	    sctp_clog.x.misc.log2,
	    sctp_clog.x.misc.log3,
	    sctp_clog.x.misc.log4);
#endif
}

void
sctp_log_block(uint8_t from, struct sctp_association *asoc, ssize_t sendlen)
{
#if defined(SCTP_LOCAL_TRACE_BUF)
	struct sctp_cwnd_log sctp_clog;

	sctp_clog.x.blk.onsb = asoc->total_output_queue_size;
	sctp_clog.x.blk.send_sent_qcnt = (uint16_t)(asoc->send_queue_cnt + asoc->sent_queue_cnt);
	sctp_clog.x.blk.peer_rwnd = asoc->peers_rwnd;
	sctp_clog.x.blk.stream_qcnt = (uint16_t)asoc->stream_queue_cnt;
	sctp_clog.x.blk.chunks_on_oque = (uint16_t)asoc->chunks_on_out_queue;
	sctp_clog.x.blk.flight_size = (uint16_t)(asoc->total_flight / 1024);
	sctp_clog.x.blk.sndlen = (uint32_t)sendlen;
	SCTP_CTR6(KTR_SCTP, "SCTP:%d[%d]:%x-%x-%x-%x",
	    SCTP_LOG_EVENT_BLOCK,
	    from,
	    sctp_clog.x.misc.log1,
	    sctp_clog.x.misc.log2,
	    sctp_clog.x.misc.log3,
	    sctp_clog.x.misc.log4);
#endif
}

int
sctp_fill_stat_log(void *optval SCTP_UNUSED, size_t *optsize SCTP_UNUSED)
{
	/* May need to fix this if ktrdump does not work */
	return (0);
}

#ifdef SCTP_AUDITING_ENABLED
uint8_t sctp_audit_data[SCTP_AUDIT_SIZE][2];
static int sctp_audit_indx = 0;

static
void
sctp_print_audit_report(void)
{
	int i;
	int cnt;

	cnt = 0;
	for (i = sctp_audit_indx; i < SCTP_AUDIT_SIZE; i++) {
		if ((sctp_audit_data[i][0] == 0xe0) &&
		    (sctp_audit_data[i][1] == 0x01)) {
			cnt = 0;
			SCTP_PRINTF("\n");
		} else if (sctp_audit_data[i][0] == 0xf0) {
			cnt = 0;
			SCTP_PRINTF("\n");
		} else if ((sctp_audit_data[i][0] == 0xc0) &&
		    (sctp_audit_data[i][1] == 0x01)) {
			SCTP_PRINTF("\n");
			cnt = 0;
		}
		SCTP_PRINTF("%2.2x%2.2x ", (uint32_t)sctp_audit_data[i][0],
		    (uint32_t)sctp_audit_data[i][1]);
		cnt++;
		if ((cnt % 14) == 0)
			SCTP_PRINTF("\n");
	}
	for (i = 0; i < sctp_audit_indx; i++) {
		if ((sctp_audit_data[i][0] == 0xe0) &&
		    (sctp_audit_data[i][1] == 0x01)) {
			cnt = 0;
			SCTP_PRINTF("\n");
		} else if (sctp_audit_data[i][0] == 0xf0) {
			cnt = 0;
			SCTP_PRINTF("\n");
		} else if ((sctp_audit_data[i][0] == 0xc0) &&
		    (sctp_audit_data[i][1] == 0x01)) {
			SCTP_PRINTF("\n");
			cnt = 0;
		}
		SCTP_PRINTF("%2.2x%2.2x ", (uint32_t)sctp_audit_data[i][0],
		    (uint32_t)sctp_audit_data[i][1]);
		cnt++;
		if ((cnt % 14) == 0)
			SCTP_PRINTF("\n");
	}
	SCTP_PRINTF("\n");
}

void
sctp_auditing(int from, struct sctp_inpcb *inp, struct sctp_tcb *stcb,
    struct sctp_nets *net)
{
	int resend_cnt, tot_out, rep, tot_book_cnt;
	struct sctp_nets *lnet;
	struct sctp_tmit_chunk *chk;

	sctp_audit_data[sctp_audit_indx][0] = 0xAA;
	sctp_audit_data[sctp_audit_indx][1] = 0x000000ff & from;
	sctp_audit_indx++;
	if (sctp_audit_indx >= SCTP_AUDIT_SIZE) {
		sctp_audit_indx = 0;
	}
	if (inp == NULL) {
		sctp_audit_data[sctp_audit_indx][0] = 0xAF;
		sctp_audit_data[sctp_audit_indx][1] = 0x01;
		sctp_audit_indx++;
		if (sctp_audit_indx >= SCTP_AUDIT_SIZE) {
			sctp_audit_indx = 0;
		}
		return;
	}
	if (stcb == NULL) {
		sctp_audit_data[sctp_audit_indx][0] = 0xAF;
		sctp_audit_data[sctp_audit_indx][1] = 0x02;
		sctp_audit_indx++;
		if (sctp_audit_indx >= SCTP_AUDIT_SIZE) {
			sctp_audit_indx = 0;
		}
		return;
	}
	sctp_audit_data[sctp_audit_indx][0] = 0xA1;
	sctp_audit_data[sctp_audit_indx][1] =
	    (0x000000ff & stcb->asoc.sent_queue_retran_cnt);
	sctp_audit_indx++;
	if (sctp_audit_indx >= SCTP_AUDIT_SIZE) {
		sctp_audit_indx = 0;
	}
	rep = 0;
	tot_book_cnt = 0;
	resend_cnt = tot_out = 0;
	TAILQ_FOREACH(chk, &stcb->asoc.sent_queue, sctp_next) {
		if (chk->sent == SCTP_DATAGRAM_RESEND) {
			resend_cnt++;
		} else if (chk->sent < SCTP_DATAGRAM_RESEND) {
			tot_out += chk->book_size;
			tot_book_cnt++;
		}
	}
	if (resend_cnt != stcb->asoc.sent_queue_retran_cnt) {
		sctp_audit_data[sctp_audit_indx][0] = 0xAF;
		sctp_audit_data[sctp_audit_indx][1] = 0xA1;
		sctp_audit_indx++;
		if (sctp_audit_indx >= SCTP_AUDIT_SIZE) {
			sctp_audit_indx = 0;
		}
		SCTP_PRINTF("resend_cnt:%d asoc-tot:%d\n",
		    resend_cnt, stcb->asoc.sent_queue_retran_cnt);
		rep = 1;
		stcb->asoc.sent_queue_retran_cnt = resend_cnt;
		sctp_audit_data[sctp_audit_indx][0] = 0xA2;
		sctp_audit_data[sctp_audit_indx][1] =
		    (0x000000ff & stcb->asoc.sent_queue_retran_cnt);
		sctp_audit_indx++;
		if (sctp_audit_indx >= SCTP_AUDIT_SIZE) {
			sctp_audit_indx = 0;
		}
	}
	if (tot_out != stcb->asoc.total_flight) {
		sctp_audit_data[sctp_audit_indx][0] = 0xAF;
		sctp_audit_data[sctp_audit_indx][1] = 0xA2;
		sctp_audit_indx++;
		if (sctp_audit_indx >= SCTP_AUDIT_SIZE) {
			sctp_audit_indx = 0;
		}
		rep = 1;
		SCTP_PRINTF("tot_flt:%d asoc_tot:%d\n", tot_out,
		    (int)stcb->asoc.total_flight);
		stcb->asoc.total_flight = tot_out;
	}
	if (tot_book_cnt != stcb->asoc.total_flight_count) {
		sctp_audit_data[sctp_audit_indx][0] = 0xAF;
		sctp_audit_data[sctp_audit_indx][1] = 0xA5;
		sctp_audit_indx++;
		if (sctp_audit_indx >= SCTP_AUDIT_SIZE) {
			sctp_audit_indx = 0;
		}
		rep = 1;
		SCTP_PRINTF("tot_flt_book:%d\n", tot_book_cnt);

		stcb->asoc.total_flight_count = tot_book_cnt;
	}
	tot_out = 0;
	TAILQ_FOREACH(lnet, &stcb->asoc.nets, sctp_next) {
		tot_out += lnet->flight_size;
	}
	if (tot_out != stcb->asoc.total_flight) {
		sctp_audit_data[sctp_audit_indx][0] = 0xAF;
		sctp_audit_data[sctp_audit_indx][1] = 0xA3;
		sctp_audit_indx++;
		if (sctp_audit_indx >= SCTP_AUDIT_SIZE) {
			sctp_audit_indx = 0;
		}
		rep = 1;
		SCTP_PRINTF("real flight:%d net total was %d\n",
		    stcb->asoc.total_flight, tot_out);
		/* now corrective action */
		TAILQ_FOREACH(lnet, &stcb->asoc.nets, sctp_next) {

			tot_out = 0;
			TAILQ_FOREACH(chk, &stcb->asoc.sent_queue, sctp_next) {
				if ((chk->whoTo == lnet) &&
				    (chk->sent < SCTP_DATAGRAM_RESEND)) {
					tot_out += chk->book_size;
				}
			}
			if (lnet->flight_size != tot_out) {
				SCTP_PRINTF("net:%p flight was %d corrected to %d\n",
				    (void *)lnet, lnet->flight_size,
				    tot_out);
				lnet->flight_size = tot_out;
			}
		}
	}
	if (rep) {
		sctp_print_audit_report();
	}
}

void
sctp_audit_log(uint8_t ev, uint8_t fd)
{

	sctp_audit_data[sctp_audit_indx][0] = ev;
	sctp_audit_data[sctp_audit_indx][1] = fd;
	sctp_audit_indx++;
	if (sctp_audit_indx >= SCTP_AUDIT_SIZE) {
		sctp_audit_indx = 0;
	}
}

#endif

/*
 * sctp_stop_timers_for_shutdown() should be called
 * when entering the SHUTDOWN_SENT or SHUTDOWN_ACK_SENT
 * state to make sure that all timers are stopped.
 */
void
sctp_stop_timers_for_shutdown(struct sctp_tcb *stcb)
{
	struct sctp_association *asoc;
	struct sctp_nets *net;

	asoc = &stcb->asoc;

	(void)SCTP_OS_TIMER_STOP(&asoc->dack_timer.timer);
	(void)SCTP_OS_TIMER_STOP(&asoc->strreset_timer.timer);
	(void)SCTP_OS_TIMER_STOP(&asoc->asconf_timer.timer);
	(void)SCTP_OS_TIMER_STOP(&asoc->autoclose_timer.timer);
	(void)SCTP_OS_TIMER_STOP(&asoc->delayed_event_timer.timer);
	TAILQ_FOREACH(net, &asoc->nets, sctp_next) {
		(void)SCTP_OS_TIMER_STOP(&net->pmtu_timer.timer);
		(void)SCTP_OS_TIMER_STOP(&net->hb_timer.timer);
	}
}

/*
 * A list of sizes based on typical mtu's, used only if next hop size not
 * returned. These values MUST be multiples of 4 and MUST be ordered.
 */
static uint32_t sctp_mtu_sizes[] = {
	68,
	296,
	508,
	512,
	544,
	576,
	1004,
	1492,
	1500,
	1536,
	2000,
	2048,
	4352,
	4464,
	8166,
	17912,
	32000,
	65532
};

/*
 * Return the largest MTU in sctp_mtu_sizes smaller than val.
 * If val is smaller than the minimum, just return the largest
 * multiple of 4 smaller or equal to val.
 * Ensure that the result is a multiple of 4.
 */
uint32_t
sctp_get_prev_mtu(uint32_t val)
{
	uint32_t i;

	val &= 0xfffffffc;
	if (val <= sctp_mtu_sizes[0]) {
		return (val);
	}
	for (i = 1; i < (sizeof(sctp_mtu_sizes) / sizeof(uint32_t)); i++) {
		if (val <= sctp_mtu_sizes[i]) {
			break;
		}
	}
	KASSERT((sctp_mtu_sizes[i - 1] & 0x00000003) == 0,
	    ("sctp_mtu_sizes[%u] not a multiple of 4", i - 1));
	return (sctp_mtu_sizes[i - 1]);
}

/*
 * Return the smallest MTU in sctp_mtu_sizes larger than val.
 * If val is larger than the maximum, just return the largest multiple of 4 smaller
 * or equal to val.
 * Ensure that the result is a multiple of 4.
 */
uint32_t
sctp_get_next_mtu(uint32_t val)
{
	/* select another MTU that is just bigger than this one */
	uint32_t i;

	val &= 0xfffffffc;
	for (i = 0; i < (sizeof(sctp_mtu_sizes) / sizeof(uint32_t)); i++) {
		if (val < sctp_mtu_sizes[i]) {
			KASSERT((sctp_mtu_sizes[i] & 0x00000003) == 0,
			    ("sctp_mtu_sizes[%u] not a multiple of 4", i));
			return (sctp_mtu_sizes[i]);
		}
	}
	return (val);
}

void
sctp_fill_random_store(struct sctp_pcb *m)
{
	/*
	 * Here we use the MD5/SHA-1 to hash with our good randomNumbers and
	 * our counter. The result becomes our good random numbers and we
	 * then setup to give these out. Note that we do no locking to
	 * protect this. This is ok, since if competing folks call this we
	 * will get more gobbled gook in the random store which is what we
	 * want. There is a danger that two guys will use the same random
	 * numbers, but thats ok too since that is random as well :->
	 */
	m->store_at = 0;
	(void)sctp_hmac(SCTP_HMAC, (uint8_t *)m->random_numbers,
	    sizeof(m->random_numbers), (uint8_t *)&m->random_counter,
	    sizeof(m->random_counter), (uint8_t *)m->random_store);
	m->random_counter++;
}

uint32_t
sctp_select_initial_TSN(struct sctp_pcb *inp)
{
	/*
	 * A true implementation should use random selection process to get
	 * the initial stream sequence number, using RFC1750 as a good
	 * guideline
	 */
	uint32_t x, *xp;
	uint8_t *p;
	int store_at, new_store;

	if (inp->initial_sequence_debug != 0) {
		uint32_t ret;

		ret = inp->initial_sequence_debug;
		inp->initial_sequence_debug++;
		return (ret);
	}
retry:
	store_at = inp->store_at;
	new_store = store_at + sizeof(uint32_t);
	if (new_store >= (SCTP_SIGNATURE_SIZE - 3)) {
		new_store = 0;
	}
	if (!atomic_cmpset_int(&inp->store_at, store_at, new_store)) {
		goto retry;
	}
	if (new_store == 0) {
		/* Refill the random store */
		sctp_fill_random_store(inp);
	}
	p = &inp->random_store[store_at];
	xp = (uint32_t *)p;
	x = *xp;
	return (x);
}

uint32_t
sctp_select_a_tag(struct sctp_inpcb *inp, uint16_t lport, uint16_t rport, int check)
{
	uint32_t x;
	struct timeval now;

	if (check) {
		(void)SCTP_GETTIME_TIMEVAL(&now);
	}
	for (;;) {
		x = sctp_select_initial_TSN(&inp->sctp_ep);
		if (x == 0) {
			/* we never use 0 */
			continue;
		}
		if (!check || sctp_is_vtag_good(x, lport, rport, &now)) {
			break;
		}
	}
	return (x);
}

int32_t
sctp_map_assoc_state(int kernel_state)
{
	int32_t user_state;

	if (kernel_state & SCTP_STATE_WAS_ABORTED) {
		user_state = SCTP_CLOSED;
	} else if (kernel_state & SCTP_STATE_SHUTDOWN_PENDING) {
		user_state = SCTP_SHUTDOWN_PENDING;
	} else {
		switch (kernel_state & SCTP_STATE_MASK) {
		case SCTP_STATE_EMPTY:
			user_state = SCTP_CLOSED;
			break;
		case SCTP_STATE_INUSE:
			user_state = SCTP_CLOSED;
			break;
		case SCTP_STATE_COOKIE_WAIT:
			user_state = SCTP_COOKIE_WAIT;
			break;
		case SCTP_STATE_COOKIE_ECHOED:
			user_state = SCTP_COOKIE_ECHOED;
			break;
		case SCTP_STATE_OPEN:
			user_state = SCTP_ESTABLISHED;
			break;
		case SCTP_STATE_SHUTDOWN_SENT:
			user_state = SCTP_SHUTDOWN_SENT;
			break;
		case SCTP_STATE_SHUTDOWN_RECEIVED:
			user_state = SCTP_SHUTDOWN_RECEIVED;
			break;
		case SCTP_STATE_SHUTDOWN_ACK_SENT:
			user_state = SCTP_SHUTDOWN_ACK_SENT;
			break;
		default:
			user_state = SCTP_CLOSED;
			break;
		}
	}
	return (user_state);
}

int
sctp_init_asoc(struct sctp_inpcb *inp, struct sctp_tcb *stcb,
    uint32_t override_tag, uint32_t vrf_id, uint16_t o_strms)
{
	struct sctp_association *asoc;

	/*
	 * Anything set to zero is taken care of by the allocation routine's
	 * bzero
	 */

	/*
	 * Up front select what scoping to apply on addresses I tell my peer
	 * Not sure what to do with these right now, we will need to come up
	 * with a way to set them. We may need to pass them through from the
	 * caller in the sctp_aloc_assoc() function.
	 */
	int i;
#if defined(SCTP_DETAILED_STR_STATS)
	int j;
#endif

	asoc = &stcb->asoc;
	/* init all variables to a known value. */
	SCTP_SET_STATE(stcb, SCTP_STATE_INUSE);
	asoc->max_burst = inp->sctp_ep.max_burst;
	asoc->fr_max_burst = inp->sctp_ep.fr_max_burst;
	asoc->heart_beat_delay = TICKS_TO_MSEC(inp->sctp_ep.sctp_timeoutticks[SCTP_TIMER_HEARTBEAT]);
	asoc->cookie_life = inp->sctp_ep.def_cookie_life;
	asoc->sctp_cmt_on_off = inp->sctp_cmt_on_off;
	asoc->ecn_supported = inp->ecn_supported;
	asoc->prsctp_supported = inp->prsctp_supported;
	asoc->idata_supported = inp->idata_supported;
	asoc->auth_supported = inp->auth_supported;
	asoc->asconf_supported = inp->asconf_supported;
	asoc->reconfig_supported = inp->reconfig_supported;
	asoc->nrsack_supported = inp->nrsack_supported;
	asoc->pktdrop_supported = inp->pktdrop_supported;
	asoc->idata_supported = inp->idata_supported;
	asoc->sctp_cmt_pf = (uint8_t)0;
	asoc->sctp_frag_point = inp->sctp_frag_point;
	asoc->sctp_features = inp->sctp_features;
	asoc->default_dscp = inp->sctp_ep.default_dscp;
	asoc->max_cwnd = inp->max_cwnd;
#ifdef INET6
	if (inp->sctp_ep.default_flowlabel) {
		asoc->default_flowlabel = inp->sctp_ep.default_flowlabel;
	} else {
		if (inp->ip_inp.inp.inp_flags & IN6P_AUTOFLOWLABEL) {
			asoc->default_flowlabel = sctp_select_initial_TSN(&inp->sctp_ep);
			asoc->default_flowlabel &= 0x000fffff;
			asoc->default_flowlabel |= 0x80000000;
		} else {
			asoc->default_flowlabel = 0;
		}
	}
#endif
	asoc->sb_send_resv = 0;
	if (override_tag) {
		asoc->my_vtag = override_tag;
	} else {
		asoc->my_vtag = sctp_select_a_tag(inp, stcb->sctp_ep->sctp_lport, stcb->rport, 1);
	}
	/* Get the nonce tags */
	asoc->my_vtag_nonce = sctp_select_a_tag(inp, stcb->sctp_ep->sctp_lport, stcb->rport, 0);
	asoc->peer_vtag_nonce = sctp_select_a_tag(inp, stcb->sctp_ep->sctp_lport, stcb->rport, 0);
	asoc->vrf_id = vrf_id;

#ifdef SCTP_ASOCLOG_OF_TSNS
	asoc->tsn_in_at = 0;
	asoc->tsn_out_at = 0;
	asoc->tsn_in_wrapped = 0;
	asoc->tsn_out_wrapped = 0;
	asoc->cumack_log_at = 0;
	asoc->cumack_log_atsnt = 0;
#endif
#ifdef SCTP_FS_SPEC_LOG
	asoc->fs_index = 0;
#endif
	asoc->refcnt = 0;
	asoc->assoc_up_sent = 0;
	asoc->asconf_seq_out = asoc->str_reset_seq_out = asoc->init_seq_number = asoc->sending_seq =
	    sctp_select_initial_TSN(&inp->sctp_ep);
	asoc->asconf_seq_out_acked = asoc->asconf_seq_out - 1;
	/* we are optimisitic here */
	asoc->peer_supports_nat = 0;
	asoc->sent_queue_retran_cnt = 0;

	/* for CMT */
	asoc->last_net_cmt_send_started = NULL;

	/* This will need to be adjusted */
	asoc->last_acked_seq = asoc->init_seq_number - 1;
	asoc->advanced_peer_ack_point = asoc->last_acked_seq;
	asoc->asconf_seq_in = asoc->last_acked_seq;

	/* here we are different, we hold the next one we expect */
	asoc->str_reset_seq_in = asoc->last_acked_seq + 1;

	asoc->initial_init_rto_max = inp->sctp_ep.initial_init_rto_max;
	asoc->initial_rto = inp->sctp_ep.initial_rto;

	asoc->default_mtu = inp->sctp_ep.default_mtu;
	asoc->max_init_times = inp->sctp_ep.max_init_times;
	asoc->max_send_times = inp->sctp_ep.max_send_times;
	asoc->def_net_failure = inp->sctp_ep.def_net_failure;
	asoc->def_net_pf_threshold = inp->sctp_ep.def_net_pf_threshold;
	asoc->free_chunk_cnt = 0;

	asoc->iam_blocking = 0;
	asoc->context = inp->sctp_context;
	asoc->local_strreset_support = inp->local_strreset_support;
	asoc->def_send = inp->def_send;
	asoc->delayed_ack = TICKS_TO_MSEC(inp->sctp_ep.sctp_timeoutticks[SCTP_TIMER_RECV]);
	asoc->sack_freq = inp->sctp_ep.sctp_sack_freq;
	asoc->pr_sctp_cnt = 0;
	asoc->total_output_queue_size = 0;

	if (inp->sctp_flags & SCTP_PCB_FLAGS_BOUND_V6) {
		asoc->scope.ipv6_addr_legal = 1;
		if (SCTP_IPV6_V6ONLY(inp) == 0) {
			asoc->scope.ipv4_addr_legal = 1;
		} else {
			asoc->scope.ipv4_addr_legal = 0;
		}
	} else {
		asoc->scope.ipv6_addr_legal = 0;
		asoc->scope.ipv4_addr_legal = 1;
	}

	asoc->my_rwnd = max(SCTP_SB_LIMIT_RCV(inp->sctp_socket), SCTP_MINIMAL_RWND);
	asoc->peers_rwnd = SCTP_SB_LIMIT_RCV(inp->sctp_socket);

	asoc->smallest_mtu = inp->sctp_frag_point;
	asoc->minrto = inp->sctp_ep.sctp_minrto;
	asoc->maxrto = inp->sctp_ep.sctp_maxrto;

	asoc->stream_locked_on = 0;
	asoc->ecn_echo_cnt_onq = 0;
	asoc->stream_locked = 0;

	asoc->send_sack = 1;

	LIST_INIT(&asoc->sctp_restricted_addrs);

	TAILQ_INIT(&asoc->nets);
	TAILQ_INIT(&asoc->pending_reply_queue);
	TAILQ_INIT(&asoc->asconf_ack_sent);
	/* Setup to fill the hb random cache at first HB */
	asoc->hb_random_idx = 4;

	asoc->sctp_autoclose_ticks = inp->sctp_ep.auto_close_time;

	stcb->asoc.congestion_control_module = inp->sctp_ep.sctp_default_cc_module;
	stcb->asoc.cc_functions = sctp_cc_functions[inp->sctp_ep.sctp_default_cc_module];

	stcb->asoc.stream_scheduling_module = inp->sctp_ep.sctp_default_ss_module;
	stcb->asoc.ss_functions = sctp_ss_functions[inp->sctp_ep.sctp_default_ss_module];

	/*
	 * Now the stream parameters, here we allocate space for all streams
	 * that we request by default.
	 */
	asoc->strm_realoutsize = asoc->streamoutcnt = asoc->pre_open_streams =
	    o_strms;
	SCTP_MALLOC(asoc->strmout, struct sctp_stream_out *,
	    asoc->streamoutcnt * sizeof(struct sctp_stream_out),
	    SCTP_M_STRMO);
	if (asoc->strmout == NULL) {
		/* big trouble no memory */
		SCTP_LTRACE_ERR_RET(NULL, stcb, NULL, SCTP_FROM_SCTPUTIL, ENOMEM);
		return (ENOMEM);
	}
	for (i = 0; i < asoc->streamoutcnt; i++) {
		/*
		 * inbound side must be set to 0xffff, also NOTE when we get
		 * the INIT-ACK back (for INIT sender) we MUST reduce the
		 * count (streamoutcnt) but first check if we sent to any of
		 * the upper streams that were dropped (if some were). Those
		 * that were dropped must be notified to the upper layer as
		 * failed to send.
		 */
		asoc->strmout[i].next_mid_ordered = 0;
		asoc->strmout[i].next_mid_unordered = 0;
		TAILQ_INIT(&asoc->strmout[i].outqueue);
		asoc->strmout[i].chunks_on_queues = 0;
#if defined(SCTP_DETAILED_STR_STATS)
		for (j = 0; j < SCTP_PR_SCTP_MAX + 1; j++) {
			asoc->strmout[i].abandoned_sent[j] = 0;
			asoc->strmout[i].abandoned_unsent[j] = 0;
		}
#else
		asoc->strmout[i].abandoned_sent[0] = 0;
		asoc->strmout[i].abandoned_unsent[0] = 0;
#endif
		asoc->strmout[i].sid = i;
		asoc->strmout[i].last_msg_incomplete = 0;
		asoc->strmout[i].state = SCTP_STREAM_OPENING;
		asoc->ss_functions.sctp_ss_init_stream(stcb, &asoc->strmout[i], NULL);
	}
	asoc->ss_functions.sctp_ss_init(stcb, asoc, 0);

	/* Now the mapping array */
	asoc->mapping_array_size = SCTP_INITIAL_MAPPING_ARRAY;
	SCTP_MALLOC(asoc->mapping_array, uint8_t *, asoc->mapping_array_size,
	    SCTP_M_MAP);
	if (asoc->mapping_array == NULL) {
		SCTP_FREE(asoc->strmout, SCTP_M_STRMO);
		SCTP_LTRACE_ERR_RET(NULL, stcb, NULL, SCTP_FROM_SCTPUTIL, ENOMEM);
		return (ENOMEM);
	}
	memset(asoc->mapping_array, 0, asoc->mapping_array_size);
	SCTP_MALLOC(asoc->nr_mapping_array, uint8_t *, asoc->mapping_array_size,
	    SCTP_M_MAP);
	if (asoc->nr_mapping_array == NULL) {
		SCTP_FREE(asoc->strmout, SCTP_M_STRMO);
		SCTP_FREE(asoc->mapping_array, SCTP_M_MAP);
		SCTP_LTRACE_ERR_RET(NULL, stcb, NULL, SCTP_FROM_SCTPUTIL, ENOMEM);
		return (ENOMEM);
	}
	memset(asoc->nr_mapping_array, 0, asoc->mapping_array_size);

	/* Now the init of the other outqueues */
	TAILQ_INIT(&asoc->free_chunks);
	TAILQ_INIT(&asoc->control_send_queue);
	TAILQ_INIT(&asoc->asconf_send_queue);
	TAILQ_INIT(&asoc->send_queue);
	TAILQ_INIT(&asoc->sent_queue);
	TAILQ_INIT(&asoc->resetHead);
	asoc->max_inbound_streams = inp->sctp_ep.max_open_streams_intome;
	TAILQ_INIT(&asoc->asconf_queue);
	/* authentication fields */
	asoc->authinfo.random = NULL;
	asoc->authinfo.active_keyid = 0;
	asoc->authinfo.assoc_key = NULL;
	asoc->authinfo.assoc_keyid = 0;
	asoc->authinfo.recv_key = NULL;
	asoc->authinfo.recv_keyid = 0;
	LIST_INIT(&asoc->shared_keys);
	asoc->marked_retrans = 0;
	asoc->port = inp->sctp_ep.port;
	asoc->timoinit = 0;
	asoc->timodata = 0;
	asoc->timosack = 0;
	asoc->timoshutdown = 0;
	asoc->timoheartbeat = 0;
	asoc->timocookie = 0;
	asoc->timoshutdownack = 0;
	(void)SCTP_GETTIME_TIMEVAL(&asoc->start_time);
	asoc->discontinuity_time = asoc->start_time;
	for (i = 0; i < SCTP_PR_SCTP_MAX + 1; i++) {
		asoc->abandoned_unsent[i] = 0;
		asoc->abandoned_sent[i] = 0;
	}
	/*
	 * sa_ignore MEMLEAK {memory is put in the assoc mapping array and
	 * freed later when the association is freed.
	 */
	return (0);
}

void
sctp_print_mapping_array(struct sctp_association *asoc)
{
	unsigned int i, limit;

	SCTP_PRINTF("Mapping array size: %d, baseTSN: %8.8x, cumAck: %8.8x, highestTSN: (%8.8x, %8.8x).\n",
	    asoc->mapping_array_size,
	    asoc->mapping_array_base_tsn,
	    asoc->cumulative_tsn,
	    asoc->highest_tsn_inside_map,
	    asoc->highest_tsn_inside_nr_map);
	for (limit = asoc->mapping_array_size; limit > 1; limit--) {
		if (asoc->mapping_array[limit - 1] != 0) {
			break;
		}
	}
	SCTP_PRINTF("Renegable mapping array (last %d entries are zero):\n", asoc->mapping_array_size - limit);
	for (i = 0; i < limit; i++) {
		SCTP_PRINTF("%2.2x%c", asoc->mapping_array[i], ((i + 1) % 16) ? ' ' : '\n');
	}
	if (limit % 16)
		SCTP_PRINTF("\n");
	for (limit = asoc->mapping_array_size; limit > 1; limit--) {
		if (asoc->nr_mapping_array[limit - 1]) {
			break;
		}
	}
	SCTP_PRINTF("Non renegable mapping array (last %d entries are zero):\n", asoc->mapping_array_size - limit);
	for (i = 0; i < limit; i++) {
		SCTP_PRINTF("%2.2x%c", asoc->nr_mapping_array[i], ((i + 1) % 16) ? ' ' : '\n');
	}
	if (limit % 16)
		SCTP_PRINTF("\n");
}

int
sctp_expand_mapping_array(struct sctp_association *asoc, uint32_t needed)
{
	/* mapping array needs to grow */
	uint8_t *new_array1, *new_array2;
	uint32_t new_size;

	new_size = asoc->mapping_array_size + ((needed + 7) / 8 + SCTP_MAPPING_ARRAY_INCR);
	SCTP_MALLOC(new_array1, uint8_t *, new_size, SCTP_M_MAP);
	SCTP_MALLOC(new_array2, uint8_t *, new_size, SCTP_M_MAP);
	if ((new_array1 == NULL) || (new_array2 == NULL)) {
		/* can't get more, forget it */
		SCTP_PRINTF("No memory for expansion of SCTP mapping array %d\n", new_size);
		if (new_array1) {
			SCTP_FREE(new_array1, SCTP_M_MAP);
		}
		if (new_array2) {
			SCTP_FREE(new_array2, SCTP_M_MAP);
		}
		return (-1);
	}
	memset(new_array1, 0, new_size);
	memset(new_array2, 0, new_size);
	memcpy(new_array1, asoc->mapping_array, asoc->mapping_array_size);
	memcpy(new_array2, asoc->nr_mapping_array, asoc->mapping_array_size);
	SCTP_FREE(asoc->mapping_array, SCTP_M_MAP);
	SCTP_FREE(asoc->nr_mapping_array, SCTP_M_MAP);
	asoc->mapping_array = new_array1;
	asoc->nr_mapping_array = new_array2;
	asoc->mapping_array_size = new_size;
	return (0);
}


static void
sctp_iterator_work(struct sctp_iterator *it)
{
	int iteration_count = 0;
	int inp_skip = 0;
	int first_in = 1;
	struct sctp_inpcb *tinp;

	SCTP_INP_INFO_RLOCK();
	SCTP_ITERATOR_LOCK();
	sctp_it_ctl.cur_it = it;
	if (it->inp) {
		SCTP_INP_RLOCK(it->inp);
		SCTP_INP_DECR_REF(it->inp);
	}
	if (it->inp == NULL) {
		/* iterator is complete */
done_with_iterator:
		sctp_it_ctl.cur_it = NULL;
		SCTP_ITERATOR_UNLOCK();
		SCTP_INP_INFO_RUNLOCK();
		if (it->function_atend != NULL) {
			(*it->function_atend) (it->pointer, it->val);
		}
		SCTP_FREE(it, SCTP_M_ITER);
		return;
	}
select_a_new_ep:
	if (first_in) {
		first_in = 0;
	} else {
		SCTP_INP_RLOCK(it->inp);
	}
	while (((it->pcb_flags) &&
	    ((it->inp->sctp_flags & it->pcb_flags) != it->pcb_flags)) ||
	    ((it->pcb_features) &&
	    ((it->inp->sctp_features & it->pcb_features) != it->pcb_features))) {
		/* endpoint flags or features don't match, so keep looking */
		if (it->iterator_flags & SCTP_ITERATOR_DO_SINGLE_INP) {
			SCTP_INP_RUNLOCK(it->inp);
			goto done_with_iterator;
		}
		tinp = it->inp;
		it->inp = LIST_NEXT(it->inp, sctp_list);
		SCTP_INP_RUNLOCK(tinp);
		if (it->inp == NULL) {
			goto done_with_iterator;
		}
		SCTP_INP_RLOCK(it->inp);
	}
	/* now go through each assoc which is in the desired state */
	if (it->done_current_ep == 0) {
		if (it->function_inp != NULL)
			inp_skip = (*it->function_inp) (it->inp, it->pointer, it->val);
		it->done_current_ep = 1;
	}
	if (it->stcb == NULL) {
		/* run the per instance function */
		it->stcb = LIST_FIRST(&it->inp->sctp_asoc_list);
	}
	if ((inp_skip) || it->stcb == NULL) {
		if (it->function_inp_end != NULL) {
			inp_skip = (*it->function_inp_end) (it->inp,
			    it->pointer,
			    it->val);
		}
		SCTP_INP_RUNLOCK(it->inp);
		goto no_stcb;
	}
	while (it->stcb) {
		SCTP_TCB_LOCK(it->stcb);
		if (it->asoc_state && ((it->stcb->asoc.state & it->asoc_state) != it->asoc_state)) {
			/* not in the right state... keep looking */
			SCTP_TCB_UNLOCK(it->stcb);
			goto next_assoc;
		}
		/* see if we have limited out the iterator loop */
		iteration_count++;
		if (iteration_count > SCTP_ITERATOR_MAX_AT_ONCE) {
			/* Pause to let others grab the lock */
			atomic_add_int(&it->stcb->asoc.refcnt, 1);
			SCTP_TCB_UNLOCK(it->stcb);
			SCTP_INP_INCR_REF(it->inp);
			SCTP_INP_RUNLOCK(it->inp);
			SCTP_ITERATOR_UNLOCK();
			SCTP_INP_INFO_RUNLOCK();
			SCTP_INP_INFO_RLOCK();
			SCTP_ITERATOR_LOCK();
			if (sctp_it_ctl.iterator_flags) {
				/* We won't be staying here */
				SCTP_INP_DECR_REF(it->inp);
				atomic_add_int(&it->stcb->asoc.refcnt, -1);
				if (sctp_it_ctl.iterator_flags &
				    SCTP_ITERATOR_STOP_CUR_IT) {
					sctp_it_ctl.iterator_flags &= ~SCTP_ITERATOR_STOP_CUR_IT;
					goto done_with_iterator;
				}
				if (sctp_it_ctl.iterator_flags &
				    SCTP_ITERATOR_STOP_CUR_INP) {
					sctp_it_ctl.iterator_flags &= ~SCTP_ITERATOR_STOP_CUR_INP;
					goto no_stcb;
				}
				/* If we reach here huh? */
				SCTP_PRINTF("Unknown it ctl flag %x\n",
				    sctp_it_ctl.iterator_flags);
				sctp_it_ctl.iterator_flags = 0;
			}
			SCTP_INP_RLOCK(it->inp);
			SCTP_INP_DECR_REF(it->inp);
			SCTP_TCB_LOCK(it->stcb);
			atomic_add_int(&it->stcb->asoc.refcnt, -1);
			iteration_count = 0;
		}

		/* run function on this one */
		(*it->function_assoc) (it->inp, it->stcb, it->pointer, it->val);

		/*
		 * we lie here, it really needs to have its own type but
		 * first I must verify that this won't effect things :-0
		 */
		if (it->no_chunk_output == 0)
			sctp_chunk_output(it->inp, it->stcb, SCTP_OUTPUT_FROM_T3, SCTP_SO_NOT_LOCKED);

		SCTP_TCB_UNLOCK(it->stcb);
next_assoc:
		it->stcb = LIST_NEXT(it->stcb, sctp_tcblist);
		if (it->stcb == NULL) {
			/* Run last function */
			if (it->function_inp_end != NULL) {
				inp_skip = (*it->function_inp_end) (it->inp,
				    it->pointer,
				    it->val);
			}
		}
	}
	SCTP_INP_RUNLOCK(it->inp);
no_stcb:
	/* done with all assocs on this endpoint, move on to next endpoint */
	it->done_current_ep = 0;
	if (it->iterator_flags & SCTP_ITERATOR_DO_SINGLE_INP) {
		it->inp = NULL;
	} else {
		it->inp = LIST_NEXT(it->inp, sctp_list);
	}
	if (it->inp == NULL) {
		goto done_with_iterator;
	}
	goto select_a_new_ep;
}

void
sctp_iterator_worker(void)
{
	struct sctp_iterator *it, *nit;

	/* This function is called with the WQ lock in place */

	sctp_it_ctl.iterator_running = 1;
	TAILQ_FOREACH_SAFE(it, &sctp_it_ctl.iteratorhead, sctp_nxt_itr, nit) {
		/* now lets work on this one */
		TAILQ_REMOVE(&sctp_it_ctl.iteratorhead, it, sctp_nxt_itr);
		SCTP_IPI_ITERATOR_WQ_UNLOCK();
		CURVNET_SET(it->vn);
		sctp_iterator_work(it);
		CURVNET_RESTORE();
		SCTP_IPI_ITERATOR_WQ_LOCK();
		/* sa_ignore FREED_MEMORY */
	}
	sctp_it_ctl.iterator_running = 0;
	return;
}


static void
sctp_handle_addr_wq(void)
{
	/* deal with the ADDR wq from the rtsock calls */
	struct sctp_laddr *wi, *nwi;
	struct sctp_asconf_iterator *asc;

	SCTP_MALLOC(asc, struct sctp_asconf_iterator *,
	    sizeof(struct sctp_asconf_iterator), SCTP_M_ASC_IT);
	if (asc == NULL) {
		/* Try later, no memory */
		sctp_timer_start(SCTP_TIMER_TYPE_ADDR_WQ,
		    (struct sctp_inpcb *)NULL,
		    (struct sctp_tcb *)NULL,
		    (struct sctp_nets *)NULL);
		return;
	}
	LIST_INIT(&asc->list_of_work);
	asc->cnt = 0;

	LIST_FOREACH_SAFE(wi, &SCTP_BASE_INFO(addr_wq), sctp_nxt_addr, nwi) {
		LIST_REMOVE(wi, sctp_nxt_addr);
		LIST_INSERT_HEAD(&asc->list_of_work, wi, sctp_nxt_addr);
		asc->cnt++;
	}

	if (asc->cnt == 0) {
		SCTP_FREE(asc, SCTP_M_ASC_IT);
	} else {
		int ret;

		ret = sctp_initiate_iterator(sctp_asconf_iterator_ep,
		    sctp_asconf_iterator_stcb,
		    NULL,	/* No ep end for boundall */
		    SCTP_PCB_FLAGS_BOUNDALL,
		    SCTP_PCB_ANY_FEATURES,
		    SCTP_ASOC_ANY_STATE,
		    (void *)asc, 0,
		    sctp_asconf_iterator_end, NULL, 0);
		if (ret) {
			SCTP_PRINTF("Failed to initiate iterator for handle_addr_wq\n");
			/*
			 * Freeing if we are stopping or put back on the
			 * addr_wq.
			 */
			if (SCTP_BASE_VAR(sctp_pcb_initialized) == 0) {
				sctp_asconf_iterator_end(asc, 0);
			} else {
				LIST_FOREACH(wi, &asc->list_of_work, sctp_nxt_addr) {
					LIST_INSERT_HEAD(&SCTP_BASE_INFO(addr_wq), wi, sctp_nxt_addr);
				}
				SCTP_FREE(asc, SCTP_M_ASC_IT);
			}
		}
	}
}

void
sctp_timeout_handler(void *t)
{
	struct sctp_inpcb *inp;
	struct sctp_tcb *stcb;
	struct sctp_nets *net;
	struct sctp_timer *tmr;
	struct mbuf *op_err;
#if defined(__APPLE__) || defined(SCTP_SO_LOCK_TESTING)
	struct socket *so;
#endif
	int did_output;
	int type;

	tmr = (struct sctp_timer *)t;
	inp = (struct sctp_inpcb *)tmr->ep;
	stcb = (struct sctp_tcb *)tmr->tcb;
	net = (struct sctp_nets *)tmr->net;
	CURVNET_SET((struct vnet *)tmr->vnet);
	did_output = 1;

#ifdef SCTP_AUDITING_ENABLED
	sctp_audit_log(0xF0, (uint8_t)tmr->type);
	sctp_auditing(3, inp, stcb, net);
#endif

	/* sanity checks... */
	if (tmr->self != (void *)tmr) {
		/*
		 * SCTP_PRINTF("Stale SCTP timer fired (%p), ignoring...\n",
		 * (void *)tmr);
		 */
		CURVNET_RESTORE();
		return;
	}
	tmr->stopped_from = 0xa001;
	if (!SCTP_IS_TIMER_TYPE_VALID(tmr->type)) {
		/*
		 * SCTP_PRINTF("SCTP timer fired with invalid type: 0x%x\n",
		 * tmr->type);
		 */
		CURVNET_RESTORE();
		return;
	}
	tmr->stopped_from = 0xa002;
	if ((tmr->type != SCTP_TIMER_TYPE_ADDR_WQ) && (inp == NULL)) {
		CURVNET_RESTORE();
		return;
	}
	/* if this is an iterator timeout, get the struct and clear inp */
	tmr->stopped_from = 0xa003;
	if (inp) {
		SCTP_INP_INCR_REF(inp);
		if ((inp->sctp_socket == NULL) &&
		    ((tmr->type != SCTP_TIMER_TYPE_INPKILL) &&
		    (tmr->type != SCTP_TIMER_TYPE_INIT) &&
		    (tmr->type != SCTP_TIMER_TYPE_SEND) &&
		    (tmr->type != SCTP_TIMER_TYPE_RECV) &&
		    (tmr->type != SCTP_TIMER_TYPE_HEARTBEAT) &&
		    (tmr->type != SCTP_TIMER_TYPE_SHUTDOWN) &&
		    (tmr->type != SCTP_TIMER_TYPE_SHUTDOWNACK) &&
		    (tmr->type != SCTP_TIMER_TYPE_SHUTDOWNGUARD) &&
		    (tmr->type != SCTP_TIMER_TYPE_ASOCKILL))) {
			SCTP_INP_DECR_REF(inp);
			CURVNET_RESTORE();
			return;
		}
	}
	tmr->stopped_from = 0xa004;
	if (stcb) {
		atomic_add_int(&stcb->asoc.refcnt, 1);
		if (stcb->asoc.state == 0) {
			atomic_add_int(&stcb->asoc.refcnt, -1);
			if (inp) {
				SCTP_INP_DECR_REF(inp);
			}
			CURVNET_RESTORE();
			return;
		}
	}
	type = tmr->type;
	tmr->stopped_from = 0xa005;
	SCTPDBG(SCTP_DEBUG_TIMER1, "Timer type %d goes off\n", type);
	if (!SCTP_OS_TIMER_ACTIVE(&tmr->timer)) {
		if (inp) {
			SCTP_INP_DECR_REF(inp);
		}
		if (stcb) {
			atomic_add_int(&stcb->asoc.refcnt, -1);
		}
		CURVNET_RESTORE();
		return;
	}
	tmr->stopped_from = 0xa006;

	if (stcb) {
		SCTP_TCB_LOCK(stcb);
		atomic_add_int(&stcb->asoc.refcnt, -1);
		if ((type != SCTP_TIMER_TYPE_ASOCKILL) &&
		    ((stcb->asoc.state == 0) ||
		    (stcb->asoc.state & SCTP_STATE_ABOUT_TO_BE_FREED))) {
			SCTP_TCB_UNLOCK(stcb);
			if (inp) {
				SCTP_INP_DECR_REF(inp);
			}
			CURVNET_RESTORE();
			return;
		}
	} else if (inp != NULL) {
		if (type != SCTP_TIMER_TYPE_INPKILL) {
			SCTP_INP_WLOCK(inp);
		}
	} else {
		SCTP_WQ_ADDR_LOCK();
	}
	/* record in stopped what t-o occurred */
	tmr->stopped_from = type;

	/* mark as being serviced now */
	if (SCTP_OS_TIMER_PENDING(&tmr->timer)) {
		/*
		 * Callout has been rescheduled.
		 */
		goto get_out;
	}
	if (!SCTP_OS_TIMER_ACTIVE(&tmr->timer)) {
		/*
		 * Not active, so no action.
		 */
		goto get_out;
	}
	SCTP_OS_TIMER_DEACTIVATE(&tmr->timer);

	/* call the handler for the appropriate timer type */
	switch (type) {
	case SCTP_TIMER_TYPE_ADDR_WQ:
		sctp_handle_addr_wq();
		break;
	case SCTP_TIMER_TYPE_SEND:
		if ((stcb == NULL) || (inp == NULL)) {
			break;
		}
		SCTP_STAT_INCR(sctps_timodata);
		stcb->asoc.timodata++;
		stcb->asoc.num_send_timers_up--;
		if (stcb->asoc.num_send_timers_up < 0) {
			stcb->asoc.num_send_timers_up = 0;
		}
		SCTP_TCB_LOCK_ASSERT(stcb);
		if (sctp_t3rxt_timer(inp, stcb, net)) {
			/* no need to unlock on tcb its gone */

			goto out_decr;
		}
		SCTP_TCB_LOCK_ASSERT(stcb);
#ifdef SCTP_AUDITING_ENABLED
		sctp_auditing(4, inp, stcb, net);
#endif
		sctp_chunk_output(inp, stcb, SCTP_OUTPUT_FROM_T3, SCTP_SO_NOT_LOCKED);
		if ((stcb->asoc.num_send_timers_up == 0) &&
		    (stcb->asoc.sent_queue_cnt > 0)) {
			struct sctp_tmit_chunk *chk;

			/*
			 * safeguard. If there on some on the sent queue
			 * somewhere but no timers running something is
			 * wrong... so we start a timer on the first chunk
			 * on the send queue on whatever net it is sent to.
			 */
			chk = TAILQ_FIRST(&stcb->asoc.sent_queue);
			sctp_timer_start(SCTP_TIMER_TYPE_SEND, inp, stcb,
			    chk->whoTo);
		}
		break;
	case SCTP_TIMER_TYPE_INIT:
		if ((stcb == NULL) || (inp == NULL)) {
			break;
		}
		SCTP_STAT_INCR(sctps_timoinit);
		stcb->asoc.timoinit++;
		if (sctp_t1init_timer(inp, stcb, net)) {
			/* no need to unlock on tcb its gone */
			goto out_decr;
		}
		/* We do output but not here */
		did_output = 0;
		break;
	case SCTP_TIMER_TYPE_RECV:
		if ((stcb == NULL) || (inp == NULL)) {
			break;
		}
		SCTP_STAT_INCR(sctps_timosack);
		stcb->asoc.timosack++;
		sctp_send_sack(stcb, SCTP_SO_NOT_LOCKED);
#ifdef SCTP_AUDITING_ENABLED
		sctp_auditing(4, inp, stcb, net);
#endif
		sctp_chunk_output(inp, stcb, SCTP_OUTPUT_FROM_SACK_TMR, SCTP_SO_NOT_LOCKED);
		break;
	case SCTP_TIMER_TYPE_SHUTDOWN:
		if ((stcb == NULL) || (inp == NULL)) {
			break;
		}
		if (sctp_shutdown_timer(inp, stcb, net)) {
			/* no need to unlock on tcb its gone */
			goto out_decr;
		}
		SCTP_STAT_INCR(sctps_timoshutdown);
		stcb->asoc.timoshutdown++;
#ifdef SCTP_AUDITING_ENABLED
		sctp_auditing(4, inp, stcb, net);
#endif
		sctp_chunk_output(inp, stcb, SCTP_OUTPUT_FROM_SHUT_TMR, SCTP_SO_NOT_LOCKED);
		break;
	case SCTP_TIMER_TYPE_HEARTBEAT:
		if ((stcb == NULL) || (inp == NULL) || (net == NULL)) {
			break;
		}
		SCTP_STAT_INCR(sctps_timoheartbeat);
		stcb->asoc.timoheartbeat++;
		if (sctp_heartbeat_timer(inp, stcb, net)) {
			/* no need to unlock on tcb its gone */
			goto out_decr;
		}
#ifdef SCTP_AUDITING_ENABLED
		sctp_auditing(4, inp, stcb, net);
#endif
		if (!(net->dest_state & SCTP_ADDR_NOHB)) {
			sctp_timer_start(SCTP_TIMER_TYPE_HEARTBEAT, inp, stcb, net);
			sctp_chunk_output(inp, stcb, SCTP_OUTPUT_FROM_HB_TMR, SCTP_SO_NOT_LOCKED);
		}
		break;
	case SCTP_TIMER_TYPE_COOKIE:
		if ((stcb == NULL) || (inp == NULL)) {
			break;
		}

		if (sctp_cookie_timer(inp, stcb, net)) {
			/* no need to unlock on tcb its gone */
			goto out_decr;
		}
		SCTP_STAT_INCR(sctps_timocookie);
		stcb->asoc.timocookie++;
#ifdef SCTP_AUDITING_ENABLED
		sctp_auditing(4, inp, stcb, net);
#endif
		/*
		 * We consider T3 and Cookie timer pretty much the same with
		 * respect to where from in chunk_output.
		 */
		sctp_chunk_output(inp, stcb, SCTP_OUTPUT_FROM_T3, SCTP_SO_NOT_LOCKED);
		break;
	case SCTP_TIMER_TYPE_NEWCOOKIE:
		{
			struct timeval tv;
			int i, secret;

			if (inp == NULL) {
				break;
			}
			SCTP_STAT_INCR(sctps_timosecret);
			(void)SCTP_GETTIME_TIMEVAL(&tv);
			inp->sctp_ep.time_of_secret_change = tv.tv_sec;
			inp->sctp_ep.last_secret_number =
			    inp->sctp_ep.current_secret_number;
			inp->sctp_ep.current_secret_number++;
			if (inp->sctp_ep.current_secret_number >=
			    SCTP_HOW_MANY_SECRETS) {
				inp->sctp_ep.current_secret_number = 0;
			}
			secret = (int)inp->sctp_ep.current_secret_number;
			for (i = 0; i < SCTP_NUMBER_OF_SECRETS; i++) {
				inp->sctp_ep.secret_key[secret][i] =
				    sctp_select_initial_TSN(&inp->sctp_ep);
			}
			sctp_timer_start(SCTP_TIMER_TYPE_NEWCOOKIE, inp, stcb, net);
		}
		did_output = 0;
		break;
	case SCTP_TIMER_TYPE_PATHMTURAISE:
		if ((stcb == NULL) || (inp == NULL)) {
			break;
		}
		SCTP_STAT_INCR(sctps_timopathmtu);
		sctp_pathmtu_timer(inp, stcb, net);
		did_output = 0;
		break;
	case SCTP_TIMER_TYPE_SHUTDOWNACK:
		if ((stcb == NULL) || (inp == NULL)) {
			break;
		}
		if (sctp_shutdownack_timer(inp, stcb, net)) {
			/* no need to unlock on tcb its gone */
			goto out_decr;
		}
		SCTP_STAT_INCR(sctps_timoshutdownack);
		stcb->asoc.timoshutdownack++;
#ifdef SCTP_AUDITING_ENABLED
		sctp_auditing(4, inp, stcb, net);
#endif
		sctp_chunk_output(inp, stcb, SCTP_OUTPUT_FROM_SHUT_ACK_TMR, SCTP_SO_NOT_LOCKED);
		break;
	case SCTP_TIMER_TYPE_SHUTDOWNGUARD:
		if ((stcb == NULL) || (inp == NULL)) {
			break;
		}
		SCTP_STAT_INCR(sctps_timoshutdownguard);
		op_err = sctp_generate_cause(SCTP_BASE_SYSCTL(sctp_diag_info_code),
		    "Shutdown guard timer expired");
		sctp_abort_an_association(inp, stcb, op_err, SCTP_SO_NOT_LOCKED);
		/* no need to unlock on tcb its gone */
		goto out_decr;

	case SCTP_TIMER_TYPE_STRRESET:
		if ((stcb == NULL) || (inp == NULL)) {
			break;
		}
		if (sctp_strreset_timer(inp, stcb, net)) {
			/* no need to unlock on tcb its gone */
			goto out_decr;
		}
		SCTP_STAT_INCR(sctps_timostrmrst);
		sctp_chunk_output(inp, stcb, SCTP_OUTPUT_FROM_STRRST_TMR, SCTP_SO_NOT_LOCKED);
		break;
	case SCTP_TIMER_TYPE_ASCONF:
		if ((stcb == NULL) || (inp == NULL)) {
			break;
		}
		if (sctp_asconf_timer(inp, stcb, net)) {
			/* no need to unlock on tcb its gone */
			goto out_decr;
		}
		SCTP_STAT_INCR(sctps_timoasconf);
#ifdef SCTP_AUDITING_ENABLED
		sctp_auditing(4, inp, stcb, net);
#endif
		sctp_chunk_output(inp, stcb, SCTP_OUTPUT_FROM_ASCONF_TMR, SCTP_SO_NOT_LOCKED);
		break;
	case SCTP_TIMER_TYPE_PRIM_DELETED:
		if ((stcb == NULL) || (inp == NULL)) {
			break;
		}
		sctp_delete_prim_timer(inp, stcb, net);
		SCTP_STAT_INCR(sctps_timodelprim);
		break;

	case SCTP_TIMER_TYPE_AUTOCLOSE:
		if ((stcb == NULL) || (inp == NULL)) {
			break;
		}
		SCTP_STAT_INCR(sctps_timoautoclose);
		sctp_autoclose_timer(inp, stcb, net);
		sctp_chunk_output(inp, stcb, SCTP_OUTPUT_FROM_AUTOCLOSE_TMR, SCTP_SO_NOT_LOCKED);
		did_output = 0;
		break;
	case SCTP_TIMER_TYPE_ASOCKILL:
		if ((stcb == NULL) || (inp == NULL)) {
			break;
		}
		SCTP_STAT_INCR(sctps_timoassockill);
		/* Can we free it yet? */
		SCTP_INP_DECR_REF(inp);
		sctp_timer_stop(SCTP_TIMER_TYPE_ASOCKILL, inp, stcb, NULL,
		    SCTP_FROM_SCTPUTIL + SCTP_LOC_1);
#if defined(__APPLE__) || defined(SCTP_SO_LOCK_TESTING)
		so = SCTP_INP_SO(inp);
		atomic_add_int(&stcb->asoc.refcnt, 1);
		SCTP_TCB_UNLOCK(stcb);
		SCTP_SOCKET_LOCK(so, 1);
		SCTP_TCB_LOCK(stcb);
		atomic_subtract_int(&stcb->asoc.refcnt, 1);
#endif
		(void)sctp_free_assoc(inp, stcb, SCTP_NORMAL_PROC,
		    SCTP_FROM_SCTPUTIL + SCTP_LOC_2);
#if defined(__APPLE__) || defined(SCTP_SO_LOCK_TESTING)
		SCTP_SOCKET_UNLOCK(so, 1);
#endif
		/*
		 * free asoc, always unlocks (or destroy's) so prevent
		 * duplicate unlock or unlock of a free mtx :-0
		 */
		stcb = NULL;
		goto out_no_decr;
	case SCTP_TIMER_TYPE_INPKILL:
		SCTP_STAT_INCR(sctps_timoinpkill);
		if (inp == NULL) {
			break;
		}
		/*
		 * special case, take away our increment since WE are the
		 * killer
		 */
		SCTP_INP_DECR_REF(inp);
		sctp_timer_stop(SCTP_TIMER_TYPE_INPKILL, inp, NULL, NULL,
		    SCTP_FROM_SCTPUTIL + SCTP_LOC_3);
		sctp_inpcb_free(inp, SCTP_FREE_SHOULD_USE_ABORT,
		    SCTP_CALLED_FROM_INPKILL_TIMER);
		inp = NULL;
		goto out_no_decr;
	default:
		SCTPDBG(SCTP_DEBUG_TIMER1, "sctp_timeout_handler:unknown timer %d\n",
		    type);
		break;
	}
#ifdef SCTP_AUDITING_ENABLED
	sctp_audit_log(0xF1, (uint8_t)type);
	if (inp)
		sctp_auditing(5, inp, stcb, net);
#endif
	if ((did_output) && stcb) {
		/*
		 * Now we need to clean up the control chunk chain if an
		 * ECNE is on it. It must be marked as UNSENT again so next
		 * call will continue to send it until such time that we get
		 * a CWR, to remove it. It is, however, less likely that we
		 * will find a ecn echo on the chain though.
		 */
		sctp_fix_ecn_echo(&stcb->asoc);
	}
get_out:
	if (stcb) {
		SCTP_TCB_UNLOCK(stcb);
	} else if (inp != NULL) {
		SCTP_INP_WUNLOCK(inp);
	} else {
		SCTP_WQ_ADDR_UNLOCK();
	}

out_decr:
	if (inp) {
		SCTP_INP_DECR_REF(inp);
	}

out_no_decr:
	SCTPDBG(SCTP_DEBUG_TIMER1, "Timer now complete (type = %d)\n", type);
	CURVNET_RESTORE();
}

void
sctp_timer_start(int t_type, struct sctp_inpcb *inp, struct sctp_tcb *stcb,
    struct sctp_nets *net)
{
	uint32_t to_ticks;
	struct sctp_timer *tmr;

	if ((t_type != SCTP_TIMER_TYPE_ADDR_WQ) && (inp == NULL))
		return;

	tmr = NULL;
	if (stcb) {
		SCTP_TCB_LOCK_ASSERT(stcb);
	}
	switch (t_type) {
	case SCTP_TIMER_TYPE_ADDR_WQ:
		/* Only 1 tick away :-) */
		tmr = &SCTP_BASE_INFO(addr_wq_timer);
		to_ticks = SCTP_ADDRESS_TICK_DELAY;
		break;
	case SCTP_TIMER_TYPE_SEND:
		/* Here we use the RTO timer */
		{
			int rto_val;

			if ((stcb == NULL) || (net == NULL)) {
				return;
			}
			tmr = &net->rxt_timer;
			if (net->RTO == 0) {
				rto_val = stcb->asoc.initial_rto;
			} else {
				rto_val = net->RTO;
			}
			to_ticks = MSEC_TO_TICKS(rto_val);
		}
		break;
	case SCTP_TIMER_TYPE_INIT:
		/*
		 * Here we use the INIT timer default usually about 1
		 * minute.
		 */
		if ((stcb == NULL) || (net == NULL)) {
			return;
		}
		tmr = &net->rxt_timer;
		if (net->RTO == 0) {
			to_ticks = MSEC_TO_TICKS(stcb->asoc.initial_rto);
		} else {
			to_ticks = MSEC_TO_TICKS(net->RTO);
		}
		break;
	case SCTP_TIMER_TYPE_RECV:
		/*
		 * Here we use the Delayed-Ack timer value from the inp
		 * ususually about 200ms.
		 */
		if (stcb == NULL) {
			return;
		}
		tmr = &stcb->asoc.dack_timer;
		to_ticks = MSEC_TO_TICKS(stcb->asoc.delayed_ack);
		break;
	case SCTP_TIMER_TYPE_SHUTDOWN:
		/* Here we use the RTO of the destination. */
		if ((stcb == NULL) || (net == NULL)) {
			return;
		}
		if (net->RTO == 0) {
			to_ticks = MSEC_TO_TICKS(stcb->asoc.initial_rto);
		} else {
			to_ticks = MSEC_TO_TICKS(net->RTO);
		}
		tmr = &net->rxt_timer;
		break;
	case SCTP_TIMER_TYPE_HEARTBEAT:
		/*
		 * the net is used here so that we can add in the RTO. Even
		 * though we use a different timer. We also add the HB timer
		 * PLUS a random jitter.
		 */
		if ((stcb == NULL) || (net == NULL)) {
			return;
		} else {
			uint32_t rndval;
			uint32_t jitter;

			if ((net->dest_state & SCTP_ADDR_NOHB) &&
			    !(net->dest_state & SCTP_ADDR_UNCONFIRMED)) {
				return;
			}
			if (net->RTO == 0) {
				to_ticks = stcb->asoc.initial_rto;
			} else {
				to_ticks = net->RTO;
			}
			rndval = sctp_select_initial_TSN(&inp->sctp_ep);
			jitter = rndval % to_ticks;
			if (jitter >= (to_ticks >> 1)) {
				to_ticks = to_ticks + (jitter - (to_ticks >> 1));
			} else {
				to_ticks = to_ticks - jitter;
			}
			if (!(net->dest_state & SCTP_ADDR_UNCONFIRMED) &&
			    !(net->dest_state & SCTP_ADDR_PF)) {
				to_ticks += net->heart_beat_delay;
			}
			/*
			 * Now we must convert the to_ticks that are now in
			 * ms to ticks.
			 */
			to_ticks = MSEC_TO_TICKS(to_ticks);
			tmr = &net->hb_timer;
		}
		break;
	case SCTP_TIMER_TYPE_COOKIE:
		/*
		 * Here we can use the RTO timer from the network since one
		 * RTT was compelete. If a retran happened then we will be
		 * using the RTO initial value.
		 */
		if ((stcb == NULL) || (net == NULL)) {
			return;
		}
		if (net->RTO == 0) {
			to_ticks = MSEC_TO_TICKS(stcb->asoc.initial_rto);
		} else {
			to_ticks = MSEC_TO_TICKS(net->RTO);
		}
		tmr = &net->rxt_timer;
		break;
	case SCTP_TIMER_TYPE_NEWCOOKIE:
		/*
		 * nothing needed but the endpoint here ususually about 60
		 * minutes.
		 */
		tmr = &inp->sctp_ep.signature_change;
		to_ticks = inp->sctp_ep.sctp_timeoutticks[SCTP_TIMER_SIGNATURE];
		break;
	case SCTP_TIMER_TYPE_ASOCKILL:
		if (stcb == NULL) {
			return;
		}
		tmr = &stcb->asoc.strreset_timer;
		to_ticks = MSEC_TO_TICKS(SCTP_ASOC_KILL_TIMEOUT);
		break;
	case SCTP_TIMER_TYPE_INPKILL:
		/*
		 * The inp is setup to die. We re-use the signature_chage
		 * timer since that has stopped and we are in the GONE
		 * state.
		 */
		tmr = &inp->sctp_ep.signature_change;
		to_ticks = MSEC_TO_TICKS(SCTP_INP_KILL_TIMEOUT);
		break;
	case SCTP_TIMER_TYPE_PATHMTURAISE:
		/*
		 * Here we use the value found in the EP for PMTU ususually
		 * about 10 minutes.
		 */
		if ((stcb == NULL) || (net == NULL)) {
			return;
		}
		if (net->dest_state & SCTP_ADDR_NO_PMTUD) {
			return;
		}
		to_ticks = inp->sctp_ep.sctp_timeoutticks[SCTP_TIMER_PMTU];
		tmr = &net->pmtu_timer;
		break;
	case SCTP_TIMER_TYPE_SHUTDOWNACK:
		/* Here we use the RTO of the destination */
		if ((stcb == NULL) || (net == NULL)) {
			return;
		}
		if (net->RTO == 0) {
			to_ticks = MSEC_TO_TICKS(stcb->asoc.initial_rto);
		} else {
			to_ticks = MSEC_TO_TICKS(net->RTO);
		}
		tmr = &net->rxt_timer;
		break;
	case SCTP_TIMER_TYPE_SHUTDOWNGUARD:
		/*
		 * Here we use the endpoints shutdown guard timer usually
		 * about 3 minutes.
		 */
		if (stcb == NULL) {
			return;
		}
		if (inp->sctp_ep.sctp_timeoutticks[SCTP_TIMER_MAXSHUTDOWN] == 0) {
			to_ticks = 5 * MSEC_TO_TICKS(stcb->asoc.maxrto);
		} else {
			to_ticks = inp->sctp_ep.sctp_timeoutticks[SCTP_TIMER_MAXSHUTDOWN];
		}
		tmr = &stcb->asoc.shut_guard_timer;
		break;
	case SCTP_TIMER_TYPE_STRRESET:
		/*
		 * Here the timer comes from the stcb but its value is from
		 * the net's RTO.
		 */
		if ((stcb == NULL) || (net == NULL)) {
			return;
		}
		if (net->RTO == 0) {
			to_ticks = MSEC_TO_TICKS(stcb->asoc.initial_rto);
		} else {
			to_ticks = MSEC_TO_TICKS(net->RTO);
		}
		tmr = &stcb->asoc.strreset_timer;
		break;
	case SCTP_TIMER_TYPE_ASCONF:
		/*
		 * Here the timer comes from the stcb but its value is from
		 * the net's RTO.
		 */
		if ((stcb == NULL) || (net == NULL)) {
			return;
		}
		if (net->RTO == 0) {
			to_ticks = MSEC_TO_TICKS(stcb->asoc.initial_rto);
		} else {
			to_ticks = MSEC_TO_TICKS(net->RTO);
		}
		tmr = &stcb->asoc.asconf_timer;
		break;
	case SCTP_TIMER_TYPE_PRIM_DELETED:
		if ((stcb == NULL) || (net != NULL)) {
			return;
		}
		to_ticks = MSEC_TO_TICKS(stcb->asoc.initial_rto);
		tmr = &stcb->asoc.delete_prim_timer;
		break;
	case SCTP_TIMER_TYPE_AUTOCLOSE:
		if (stcb == NULL) {
			return;
		}
		if (stcb->asoc.sctp_autoclose_ticks == 0) {
			/*
			 * Really an error since stcb is NOT set to
			 * autoclose
			 */
			return;
		}
		to_ticks = stcb->asoc.sctp_autoclose_ticks;
		tmr = &stcb->asoc.autoclose_timer;
		break;
	default:
		SCTPDBG(SCTP_DEBUG_TIMER1, "%s: Unknown timer type %d\n",
		    __func__, t_type);
		return;
		break;
	}
	if ((to_ticks <= 0) || (tmr == NULL)) {
		SCTPDBG(SCTP_DEBUG_TIMER1, "%s: %d:software error to_ticks:%d tmr:%p not set ??\n",
		    __func__, t_type, to_ticks, (void *)tmr);
		return;
	}
	if (SCTP_OS_TIMER_PENDING(&tmr->timer)) {
		/*
		 * we do NOT allow you to have it already running. if it is
		 * we leave the current one up unchanged
		 */
		return;
	}
	/* At this point we can proceed */
	if (t_type == SCTP_TIMER_TYPE_SEND) {
		stcb->asoc.num_send_timers_up++;
	}
	tmr->stopped_from = 0;
	tmr->type = t_type;
	tmr->ep = (void *)inp;
	tmr->tcb = (void *)stcb;
	tmr->net = (void *)net;
	tmr->self = (void *)tmr;
	tmr->vnet = (void *)curvnet;
	tmr->ticks = sctp_get_tick_count();
	(void)SCTP_OS_TIMER_START(&tmr->timer, to_ticks, sctp_timeout_handler, tmr);
	return;
}

void
sctp_timer_stop(int t_type, struct sctp_inpcb *inp, struct sctp_tcb *stcb,
    struct sctp_nets *net, uint32_t from)
{
	struct sctp_timer *tmr;

	if ((t_type != SCTP_TIMER_TYPE_ADDR_WQ) &&
	    (inp == NULL))
		return;

	tmr = NULL;
	if (stcb) {
		SCTP_TCB_LOCK_ASSERT(stcb);
	}
	switch (t_type) {
	case SCTP_TIMER_TYPE_ADDR_WQ:
		tmr = &SCTP_BASE_INFO(addr_wq_timer);
		break;
	case SCTP_TIMER_TYPE_SEND:
		if ((stcb == NULL) || (net == NULL)) {
			return;
		}
		tmr = &net->rxt_timer;
		break;
	case SCTP_TIMER_TYPE_INIT:
		if ((stcb == NULL) || (net == NULL)) {
			return;
		}
		tmr = &net->rxt_timer;
		break;
	case SCTP_TIMER_TYPE_RECV:
		if (stcb == NULL) {
			return;
		}
		tmr = &stcb->asoc.dack_timer;
		break;
	case SCTP_TIMER_TYPE_SHUTDOWN:
		if ((stcb == NULL) || (net == NULL)) {
			return;
		}
		tmr = &net->rxt_timer;
		break;
	case SCTP_TIMER_TYPE_HEARTBEAT:
		if ((stcb == NULL) || (net == NULL)) {
			return;
		}
		tmr = &net->hb_timer;
		break;
	case SCTP_TIMER_TYPE_COOKIE:
		if ((stcb == NULL) || (net == NULL)) {
			return;
		}
		tmr = &net->rxt_timer;
		break;
	case SCTP_TIMER_TYPE_NEWCOOKIE:
		/* nothing needed but the endpoint here */
		tmr = &inp->sctp_ep.signature_change;
		/*
		 * We re-use the newcookie timer for the INP kill timer. We
		 * must assure that we do not kill it by accident.
		 */
		break;
	case SCTP_TIMER_TYPE_ASOCKILL:
		/*
		 * Stop the asoc kill timer.
		 */
		if (stcb == NULL) {
			return;
		}
		tmr = &stcb->asoc.strreset_timer;
		break;

	case SCTP_TIMER_TYPE_INPKILL:
		/*
		 * The inp is setup to die. We re-use the signature_chage
		 * timer since that has stopped and we are in the GONE
		 * state.
		 */
		tmr = &inp->sctp_ep.signature_change;
		break;
	case SCTP_TIMER_TYPE_PATHMTURAISE:
		if ((stcb == NULL) || (net == NULL)) {
			return;
		}
		tmr = &net->pmtu_timer;
		break;
	case SCTP_TIMER_TYPE_SHUTDOWNACK:
		if ((stcb == NULL) || (net == NULL)) {
			return;
		}
		tmr = &net->rxt_timer;
		break;
	case SCTP_TIMER_TYPE_SHUTDOWNGUARD:
		if (stcb == NULL) {
			return;
		}
		tmr = &stcb->asoc.shut_guard_timer;
		break;
	case SCTP_TIMER_TYPE_STRRESET:
		if (stcb == NULL) {
			return;
		}
		tmr = &stcb->asoc.strreset_timer;
		break;
	case SCTP_TIMER_TYPE_ASCONF:
		if (stcb == NULL) {
			return;
		}
		tmr = &stcb->asoc.asconf_timer;
		break;
	case SCTP_TIMER_TYPE_PRIM_DELETED:
		if (stcb == NULL) {
			return;
		}
		tmr = &stcb->asoc.delete_prim_timer;
		break;
	case SCTP_TIMER_TYPE_AUTOCLOSE:
		if (stcb == NULL) {
			return;
		}
		tmr = &stcb->asoc.autoclose_timer;
		break;
	default:
		SCTPDBG(SCTP_DEBUG_TIMER1, "%s: Unknown timer type %d\n",
		    __func__, t_type);
		break;
	}
	if (tmr == NULL) {
		return;
	}
	if ((tmr->type != t_type) && tmr->type) {
		/*
		 * Ok we have a timer that is under joint use. Cookie timer
		 * per chance with the SEND timer. We therefore are NOT
		 * running the timer that the caller wants stopped.  So just
		 * return.
		 */
		return;
	}
	if ((t_type == SCTP_TIMER_TYPE_SEND) && (stcb != NULL)) {
		stcb->asoc.num_send_timers_up--;
		if (stcb->asoc.num_send_timers_up < 0) {
			stcb->asoc.num_send_timers_up = 0;
		}
	}
	tmr->self = NULL;
	tmr->stopped_from = from;
	(void)SCTP_OS_TIMER_STOP(&tmr->timer);
	return;
}

uint32_t
sctp_calculate_len(struct mbuf *m)
{
	uint32_t tlen = 0;
	struct mbuf *at;

	at = m;
	while (at) {
		tlen += SCTP_BUF_LEN(at);
		at = SCTP_BUF_NEXT(at);
	}
	return (tlen);
}

void
sctp_mtu_size_reset(struct sctp_inpcb *inp,
    struct sctp_association *asoc, uint32_t mtu)
{
	/*
	 * Reset the P-MTU size on this association, this involves changing
	 * the asoc MTU, going through ANY chunk+overhead larger than mtu to
	 * allow the DF flag to be cleared.
	 */
	struct sctp_tmit_chunk *chk;
	unsigned int eff_mtu, ovh;

	asoc->smallest_mtu = mtu;
	if (inp->sctp_flags & SCTP_PCB_FLAGS_BOUND_V6) {
		ovh = SCTP_MIN_OVERHEAD;
	} else {
		ovh = SCTP_MIN_V4_OVERHEAD;
	}
	eff_mtu = mtu - ovh;
	TAILQ_FOREACH(chk, &asoc->send_queue, sctp_next) {
		if (chk->send_size > eff_mtu) {
			chk->flags |= CHUNK_FLAGS_FRAGMENT_OK;
		}
	}
	TAILQ_FOREACH(chk, &asoc->sent_queue, sctp_next) {
		if (chk->send_size > eff_mtu) {
			chk->flags |= CHUNK_FLAGS_FRAGMENT_OK;
		}
	}
}


/*
 * given an association and starting time of the current RTT period return
 * RTO in number of msecs net should point to the current network
 */

uint32_t
sctp_calculate_rto(struct sctp_tcb *stcb,
    struct sctp_association *asoc,
    struct sctp_nets *net,
    struct timeval *old,
    int rtt_from_sack)
{
	/*-
	 * given an association and the starting time of the current RTT
	 * period (in value1/value2) return RTO in number of msecs.
	 */
	int32_t rtt;		/* RTT in ms */
	uint32_t new_rto;
	int first_measure = 0;
	struct timeval now;

	/************************/
	/* 1. calculate new RTT */
	/************************/
	/* get the current time */
	if (stcb->asoc.use_precise_time) {
		(void)SCTP_GETPTIME_TIMEVAL(&now);
	} else {
		(void)SCTP_GETTIME_TIMEVAL(&now);
	}
	timevalsub(&now, old);
	/* store the current RTT in us */
	net->rtt = (uint64_t)1000000 * (uint64_t)now.tv_sec +
	    (uint64_t)now.tv_usec;
	/* compute rtt in ms */
	rtt = (int32_t)(net->rtt / 1000);
	if ((asoc->cc_functions.sctp_rtt_calculated) && (rtt_from_sack == SCTP_RTT_FROM_DATA)) {
		/*
		 * Tell the CC module that a new update has just occurred
		 * from a sack
		 */
		(*asoc->cc_functions.sctp_rtt_calculated) (stcb, net, &now);
	}
	/*
	 * Do we need to determine the lan? We do this only on sacks i.e.
	 * RTT being determined from data not non-data (HB/INIT->INITACK).
	 */
	if ((rtt_from_sack == SCTP_RTT_FROM_DATA) &&
	    (net->lan_type == SCTP_LAN_UNKNOWN)) {
		if (net->rtt > SCTP_LOCAL_LAN_RTT) {
			net->lan_type = SCTP_LAN_INTERNET;
		} else {
			net->lan_type = SCTP_LAN_LOCAL;
		}
	}

	/***************************/
	/* 2. update RTTVAR & SRTT */
	/***************************/
	/*-
	 * Compute the scaled average lastsa and the
	 * scaled variance lastsv as described in van Jacobson
	 * Paper "Congestion Avoidance and Control", Annex A.
	 *
	 * (net->lastsa >> SCTP_RTT_SHIFT) is the srtt
	 * (net->lastsa >> SCTP_RTT_VAR_SHIFT) is the rttvar
	 */
	if (net->RTO_measured) {
		rtt -= (net->lastsa >> SCTP_RTT_SHIFT);
		net->lastsa += rtt;
		if (rtt < 0) {
			rtt = -rtt;
		}
		rtt -= (net->lastsv >> SCTP_RTT_VAR_SHIFT);
		net->lastsv += rtt;
		if (SCTP_BASE_SYSCTL(sctp_logging_level) & SCTP_RTTVAR_LOGGING_ENABLE) {
			rto_logging(net, SCTP_LOG_RTTVAR);
		}
	} else {
		/* First RTO measurment */
		net->RTO_measured = 1;
		first_measure = 1;
		net->lastsa = rtt << SCTP_RTT_SHIFT;
		net->lastsv = (rtt / 2) << SCTP_RTT_VAR_SHIFT;
		if (SCTP_BASE_SYSCTL(sctp_logging_level) & SCTP_RTTVAR_LOGGING_ENABLE) {
			rto_logging(net, SCTP_LOG_INITIAL_RTT);
		}
	}
	if (net->lastsv == 0) {
		net->lastsv = SCTP_CLOCK_GRANULARITY;
	}
	new_rto = (net->lastsa >> SCTP_RTT_SHIFT) + net->lastsv;
	if ((new_rto > SCTP_SAT_NETWORK_MIN) &&
	    (stcb->asoc.sat_network_lockout == 0)) {
		stcb->asoc.sat_network = 1;
	} else if ((!first_measure) && stcb->asoc.sat_network) {
		stcb->asoc.sat_network = 0;
		stcb->asoc.sat_network_lockout = 1;
	}
	/* bound it, per C6/C7 in Section 5.3.1 */
	if (new_rto < stcb->asoc.minrto) {
		new_rto = stcb->asoc.minrto;
	}
	if (new_rto > stcb->asoc.maxrto) {
		new_rto = stcb->asoc.maxrto;
	}
	/* we are now returning the RTO */
	return (new_rto);
}

/*
 * return a pointer to a contiguous piece of data from the given mbuf chain
 * starting at 'off' for 'len' bytes.  If the desired piece spans more than
 * one mbuf, a copy is made at 'ptr'. caller must ensure that the buffer size
 * is >= 'len' returns NULL if there there isn't 'len' bytes in the chain.
 */
caddr_t
sctp_m_getptr(struct mbuf *m, int off, int len, uint8_t *in_ptr)
{
	uint32_t count;
	uint8_t *ptr;

	ptr = in_ptr;
	if ((off < 0) || (len <= 0))
		return (NULL);

	/* find the desired start location */
	while ((m != NULL) && (off > 0)) {
		if (off < SCTP_BUF_LEN(m))
			break;
		off -= SCTP_BUF_LEN(m);
		m = SCTP_BUF_NEXT(m);
	}
	if (m == NULL)
		return (NULL);

	/* is the current mbuf large enough (eg. contiguous)? */
	if ((SCTP_BUF_LEN(m) - off) >= len) {
		return (mtod(m, caddr_t)+off);
	} else {
		/* else, it spans more than one mbuf, so save a temp copy... */
		while ((m != NULL) && (len > 0)) {
			count = min(SCTP_BUF_LEN(m) - off, len);
			memcpy(ptr, mtod(m, caddr_t)+off, count);
			len -= count;
			ptr += count;
			off = 0;
			m = SCTP_BUF_NEXT(m);
		}
		if ((m == NULL) && (len > 0))
			return (NULL);
		else
			return ((caddr_t)in_ptr);
	}
}



struct sctp_paramhdr *
sctp_get_next_param(struct mbuf *m,
    int offset,
    struct sctp_paramhdr *pull,
    int pull_limit)
{
	/* This just provides a typed signature to Peter's Pull routine */
	return ((struct sctp_paramhdr *)sctp_m_getptr(m, offset, pull_limit,
	    (uint8_t *)pull));
}


struct mbuf *
sctp_add_pad_tombuf(struct mbuf *m, int padlen)
{
	struct mbuf *m_last;
	caddr_t dp;

	if (padlen > 3) {
		return (NULL);
	}
	if (padlen <= M_TRAILINGSPACE(m)) {
		/*
		 * The easy way. We hope the majority of the time we hit
		 * here :)
		 */
		m_last = m;
	} else {
		/* Hard way we must grow the mbuf chain */
		m_last = sctp_get_mbuf_for_msg(padlen, 0, M_NOWAIT, 1, MT_DATA);
		if (m_last == NULL) {
			return (NULL);
		}
		SCTP_BUF_LEN(m_last) = 0;
		SCTP_BUF_NEXT(m_last) = NULL;
		SCTP_BUF_NEXT(m) = m_last;
	}
	dp = mtod(m_last, caddr_t)+SCTP_BUF_LEN(m_last);
	SCTP_BUF_LEN(m_last) += padlen;
	memset(dp, 0, padlen);
	return (m_last);
}

struct mbuf *
sctp_pad_lastmbuf(struct mbuf *m, int padval, struct mbuf *last_mbuf)
{
	/* find the last mbuf in chain and pad it */
	struct mbuf *m_at;

	if (last_mbuf != NULL) {
		return (sctp_add_pad_tombuf(last_mbuf, padval));
	} else {
		for (m_at = m; m_at; m_at = SCTP_BUF_NEXT(m_at)) {
			if (SCTP_BUF_NEXT(m_at) == NULL) {
				return (sctp_add_pad_tombuf(m_at, padval));
			}
		}
	}
	return (NULL);
}

static void
sctp_notify_assoc_change(uint16_t state, struct sctp_tcb *stcb,
    uint16_t error, struct sctp_abort_chunk *abort, uint8_t from_peer, int so_locked
#if !defined(__APPLE__) && !defined(SCTP_SO_LOCK_TESTING)
    SCTP_UNUSED
#endif
)
{
	struct mbuf *m_notify;
	struct sctp_assoc_change *sac;
	struct sctp_queued_to_read *control;
	unsigned int notif_len;
	uint16_t abort_len;
	unsigned int i;
#if defined(__APPLE__) || defined(SCTP_SO_LOCK_TESTING)
	struct socket *so;
#endif

	if (stcb == NULL) {
		return;
	}
	if (sctp_stcb_is_feature_on(stcb->sctp_ep, stcb, SCTP_PCB_FLAGS_RECVASSOCEVNT)) {
		notif_len = (unsigned int)sizeof(struct sctp_assoc_change);
		if (abort != NULL) {
			abort_len = ntohs(abort->ch.chunk_length);
			/*
			 * Only SCTP_CHUNK_BUFFER_SIZE are guaranteed to be
			 * contiguous.
			 */
			if (abort_len > SCTP_CHUNK_BUFFER_SIZE) {
				abort_len = SCTP_CHUNK_BUFFER_SIZE;
			}
		} else {
			abort_len = 0;
		}
		if ((state == SCTP_COMM_UP) || (state == SCTP_RESTART)) {
			notif_len += SCTP_ASSOC_SUPPORTS_MAX;
		} else if ((state == SCTP_COMM_LOST) || (state == SCTP_CANT_STR_ASSOC)) {
			notif_len += abort_len;
		}
		m_notify = sctp_get_mbuf_for_msg(notif_len, 0, M_NOWAIT, 1, MT_DATA);
		if (m_notify == NULL) {
			/* Retry with smaller value. */
			notif_len = (unsigned int)sizeof(struct sctp_assoc_change);
			m_notify = sctp_get_mbuf_for_msg(notif_len, 0, M_NOWAIT, 1, MT_DATA);
			if (m_notify == NULL) {
				goto set_error;
			}
		}
		SCTP_BUF_NEXT(m_notify) = NULL;
		sac = mtod(m_notify, struct sctp_assoc_change *);
		memset(sac, 0, notif_len);
		sac->sac_type = SCTP_ASSOC_CHANGE;
		sac->sac_flags = 0;
		sac->sac_length = sizeof(struct sctp_assoc_change);
		sac->sac_state = state;
		sac->sac_error = error;
		/* XXX verify these stream counts */
		sac->sac_outbound_streams = stcb->asoc.streamoutcnt;
		sac->sac_inbound_streams = stcb->asoc.streamincnt;
		sac->sac_assoc_id = sctp_get_associd(stcb);
		if (notif_len > sizeof(struct sctp_assoc_change)) {
			if ((state == SCTP_COMM_UP) || (state == SCTP_RESTART)) {
				i = 0;
				if (stcb->asoc.prsctp_supported == 1) {
					sac->sac_info[i++] = SCTP_ASSOC_SUPPORTS_PR;
				}
				if (stcb->asoc.auth_supported == 1) {
					sac->sac_info[i++] = SCTP_ASSOC_SUPPORTS_AUTH;
				}
				if (stcb->asoc.asconf_supported == 1) {
					sac->sac_info[i++] = SCTP_ASSOC_SUPPORTS_ASCONF;
				}
				if (stcb->asoc.idata_supported == 1) {
					sac->sac_info[i++] = SCTP_ASSOC_SUPPORTS_INTERLEAVING;
				}
				sac->sac_info[i++] = SCTP_ASSOC_SUPPORTS_MULTIBUF;
				if (stcb->asoc.reconfig_supported == 1) {
					sac->sac_info[i++] = SCTP_ASSOC_SUPPORTS_RE_CONFIG;
				}
				sac->sac_length += i;
			} else if ((state == SCTP_COMM_LOST) || (state == SCTP_CANT_STR_ASSOC)) {
				memcpy(sac->sac_info, abort, abort_len);
				sac->sac_length += abort_len;
			}
		}
		SCTP_BUF_LEN(m_notify) = sac->sac_length;
		control = sctp_build_readq_entry(stcb, stcb->asoc.primary_destination,
		    0, 0, stcb->asoc.context, 0, 0, 0,
		    m_notify);
		if (control != NULL) {
			control->length = SCTP_BUF_LEN(m_notify);
			control->spec_flags = M_NOTIFICATION;
			/* not that we need this */
			control->tail_mbuf = m_notify;
			sctp_add_to_readq(stcb->sctp_ep, stcb,
			    control,
			    &stcb->sctp_socket->so_rcv, 1, SCTP_READ_LOCK_NOT_HELD,
			    so_locked);
		} else {
			sctp_m_freem(m_notify);
		}
	}
	/*
	 * For 1-to-1 style sockets, we send up and error when an ABORT
	 * comes in.
	 */
set_error:
	if (((stcb->sctp_ep->sctp_flags & SCTP_PCB_FLAGS_TCPTYPE) ||
	    (stcb->sctp_ep->sctp_flags & SCTP_PCB_FLAGS_IN_TCPPOOL)) &&
	    ((state == SCTP_COMM_LOST) || (state == SCTP_CANT_STR_ASSOC))) {
		SOCK_LOCK(stcb->sctp_socket);
		if (from_peer) {
			if (SCTP_GET_STATE(stcb) == SCTP_STATE_COOKIE_WAIT) {
				SCTP_LTRACE_ERR_RET(NULL, stcb, NULL, SCTP_FROM_SCTPUTIL, ECONNREFUSED);
				stcb->sctp_socket->so_error = ECONNREFUSED;
			} else {
				SCTP_LTRACE_ERR_RET(NULL, stcb, NULL, SCTP_FROM_SCTPUTIL, ECONNRESET);
				stcb->sctp_socket->so_error = ECONNRESET;
			}
		} else {
			if ((SCTP_GET_STATE(stcb) == SCTP_STATE_COOKIE_WAIT) ||
			    (SCTP_GET_STATE(stcb) == SCTP_STATE_COOKIE_ECHOED)) {
				SCTP_LTRACE_ERR_RET(NULL, stcb, NULL, SCTP_FROM_SCTPUTIL, ETIMEDOUT);
				stcb->sctp_socket->so_error = ETIMEDOUT;
			} else {
				SCTP_LTRACE_ERR_RET(NULL, stcb, NULL, SCTP_FROM_SCTPUTIL, ECONNABORTED);
				stcb->sctp_socket->so_error = ECONNABORTED;
			}
		}
		SOCK_UNLOCK(stcb->sctp_socket);
	}
	/* Wake ANY sleepers */
#if defined(__APPLE__) || defined(SCTP_SO_LOCK_TESTING)
	so = SCTP_INP_SO(stcb->sctp_ep);
	if (!so_locked) {
		atomic_add_int(&stcb->asoc.refcnt, 1);
		SCTP_TCB_UNLOCK(stcb);
		SCTP_SOCKET_LOCK(so, 1);
		SCTP_TCB_LOCK(stcb);
		atomic_subtract_int(&stcb->asoc.refcnt, 1);
		if (stcb->asoc.state & SCTP_STATE_CLOSED_SOCKET) {
			SCTP_SOCKET_UNLOCK(so, 1);
			return;
		}
	}
#endif
	if (((stcb->sctp_ep->sctp_flags & SCTP_PCB_FLAGS_TCPTYPE) ||
	    (stcb->sctp_ep->sctp_flags & SCTP_PCB_FLAGS_IN_TCPPOOL)) &&
	    ((state == SCTP_COMM_LOST) || (state == SCTP_CANT_STR_ASSOC))) {
		socantrcvmore(stcb->sctp_socket);
	}
	sorwakeup(stcb->sctp_socket);
	sowwakeup(stcb->sctp_socket);
#if defined(__APPLE__) || defined(SCTP_SO_LOCK_TESTING)
	if (!so_locked) {
		SCTP_SOCKET_UNLOCK(so, 1);
	}
#endif
}

static void
sctp_notify_peer_addr_change(struct sctp_tcb *stcb, uint32_t state,
    struct sockaddr *sa, uint32_t error, int so_locked
#if !defined(__APPLE__) && !defined(SCTP_SO_LOCK_TESTING)
    SCTP_UNUSED
#endif
)
{
	struct mbuf *m_notify;
	struct sctp_paddr_change *spc;
	struct sctp_queued_to_read *control;

	if ((stcb == NULL) ||
	    sctp_stcb_is_feature_off(stcb->sctp_ep, stcb, SCTP_PCB_FLAGS_RECVPADDREVNT)) {
		/* event not enabled */
		return;
	}
	m_notify = sctp_get_mbuf_for_msg(sizeof(struct sctp_paddr_change), 0, M_NOWAIT, 1, MT_DATA);
	if (m_notify == NULL)
		return;
	SCTP_BUF_LEN(m_notify) = 0;
	spc = mtod(m_notify, struct sctp_paddr_change *);
	memset(spc, 0, sizeof(struct sctp_paddr_change));
	spc->spc_type = SCTP_PEER_ADDR_CHANGE;
	spc->spc_flags = 0;
	spc->spc_length = sizeof(struct sctp_paddr_change);
	switch (sa->sa_family) {
#ifdef INET
	case AF_INET:
#ifdef INET6
		if (sctp_is_feature_on(stcb->sctp_ep, SCTP_PCB_FLAGS_NEEDS_MAPPED_V4)) {
			in6_sin_2_v4mapsin6((struct sockaddr_in *)sa,
			    (struct sockaddr_in6 *)&spc->spc_aaddr);
		} else {
			memcpy(&spc->spc_aaddr, sa, sizeof(struct sockaddr_in));
		}
#else
		memcpy(&spc->spc_aaddr, sa, sizeof(struct sockaddr_in));
#endif
		break;
#endif
#ifdef INET6
	case AF_INET6:
		{
			struct sockaddr_in6 *sin6;

			memcpy(&spc->spc_aaddr, sa, sizeof(struct sockaddr_in6));

			sin6 = (struct sockaddr_in6 *)&spc->spc_aaddr;
			if (IN6_IS_SCOPE_LINKLOCAL(&sin6->sin6_addr)) {
				if (sin6->sin6_scope_id == 0) {
					/* recover scope_id for user */
					(void)sa6_recoverscope(sin6);
				} else {
					/* clear embedded scope_id for user */
					in6_clearscope(&sin6->sin6_addr);
				}
			}
			break;
		}
#endif
	default:
		/* TSNH */
		break;
	}
	spc->spc_state = state;
	spc->spc_error = error;
	spc->spc_assoc_id = sctp_get_associd(stcb);

	SCTP_BUF_LEN(m_notify) = sizeof(struct sctp_paddr_change);
	SCTP_BUF_NEXT(m_notify) = NULL;

	/* append to socket */
	control = sctp_build_readq_entry(stcb, stcb->asoc.primary_destination,
	    0, 0, stcb->asoc.context, 0, 0, 0,
	    m_notify);
	if (control == NULL) {
		/* no memory */
		sctp_m_freem(m_notify);
		return;
	}
	control->length = SCTP_BUF_LEN(m_notify);
	control->spec_flags = M_NOTIFICATION;
	/* not that we need this */
	control->tail_mbuf = m_notify;
	sctp_add_to_readq(stcb->sctp_ep, stcb,
	    control,
	    &stcb->sctp_socket->so_rcv, 1,
	    SCTP_READ_LOCK_NOT_HELD,
	    so_locked);
}


static void
sctp_notify_send_failed(struct sctp_tcb *stcb, uint8_t sent, uint32_t error,
    struct sctp_tmit_chunk *chk, int so_locked
#if !defined(__APPLE__) && !defined(SCTP_SO_LOCK_TESTING)
    SCTP_UNUSED
#endif
)
{
	struct mbuf *m_notify;
	struct sctp_send_failed *ssf;
	struct sctp_send_failed_event *ssfe;
	struct sctp_queued_to_read *control;
	struct sctp_chunkhdr *chkhdr;
	int notifhdr_len, chk_len, chkhdr_len, padding_len, payload_len;

	if ((stcb == NULL) ||
	    (sctp_stcb_is_feature_off(stcb->sctp_ep, stcb, SCTP_PCB_FLAGS_RECVSENDFAILEVNT) &&
	    sctp_stcb_is_feature_off(stcb->sctp_ep, stcb, SCTP_PCB_FLAGS_RECVNSENDFAILEVNT))) {
		/* event not enabled */
		return;
	}

	if (sctp_stcb_is_feature_on(stcb->sctp_ep, stcb, SCTP_PCB_FLAGS_RECVNSENDFAILEVNT)) {
		notifhdr_len = sizeof(struct sctp_send_failed_event);
	} else {
		notifhdr_len = sizeof(struct sctp_send_failed);
	}
	m_notify = sctp_get_mbuf_for_msg(notifhdr_len, 0, M_NOWAIT, 1, MT_DATA);
	if (m_notify == NULL)
		/* no space left */
		return;
	SCTP_BUF_LEN(m_notify) = notifhdr_len;
	if (stcb->asoc.idata_supported) {
		chkhdr_len = sizeof(struct sctp_idata_chunk);
	} else {
		chkhdr_len = sizeof(struct sctp_data_chunk);
	}
	/* Use some defaults in case we can't access the chunk header */
	if (chk->send_size >= chkhdr_len) {
		payload_len = chk->send_size - chkhdr_len;
	} else {
		payload_len = 0;
	}
	padding_len = 0;
	if (chk->data != NULL) {
		chkhdr = mtod(chk->data, struct sctp_chunkhdr *);
		if (chkhdr != NULL) {
			chk_len = ntohs(chkhdr->chunk_length);
			if ((chk_len >= chkhdr_len) &&
			    (chk->send_size >= chk_len) &&
			    (chk->send_size - chk_len < 4)) {
				padding_len = chk->send_size - chk_len;
				payload_len = chk->send_size - chkhdr_len - padding_len;
			}
		}
	}
	if (sctp_stcb_is_feature_on(stcb->sctp_ep, stcb, SCTP_PCB_FLAGS_RECVNSENDFAILEVNT)) {
		ssfe = mtod(m_notify, struct sctp_send_failed_event *);
		memset(ssfe, 0, notifhdr_len);
		ssfe->ssfe_type = SCTP_SEND_FAILED_EVENT;
		if (sent) {
			ssfe->ssfe_flags = SCTP_DATA_SENT;
		} else {
			ssfe->ssfe_flags = SCTP_DATA_UNSENT;
		}
		ssfe->ssfe_length = (uint32_t)(notifhdr_len + payload_len);
		ssfe->ssfe_error = error;
		/* not exactly what the user sent in, but should be close :) */
		ssfe->ssfe_info.snd_sid = chk->rec.data.sid;
		ssfe->ssfe_info.snd_flags = chk->rec.data.rcv_flags;
		ssfe->ssfe_info.snd_ppid = chk->rec.data.ppid;
		ssfe->ssfe_info.snd_context = chk->rec.data.context;
		ssfe->ssfe_info.snd_assoc_id = sctp_get_associd(stcb);
		ssfe->ssfe_assoc_id = sctp_get_associd(stcb);
	} else {
		ssf = mtod(m_notify, struct sctp_send_failed *);
		memset(ssf, 0, notifhdr_len);
		ssf->ssf_type = SCTP_SEND_FAILED;
		if (sent) {
			ssf->ssf_flags = SCTP_DATA_SENT;
		} else {
			ssf->ssf_flags = SCTP_DATA_UNSENT;
		}
		ssf->ssf_length = (uint32_t)(notifhdr_len + payload_len);
		ssf->ssf_error = error;
		/* not exactly what the user sent in, but should be close :) */
		ssf->ssf_info.sinfo_stream = chk->rec.data.sid;
		ssf->ssf_info.sinfo_ssn = (uint16_t)chk->rec.data.mid;
		ssf->ssf_info.sinfo_flags = chk->rec.data.rcv_flags;
		ssf->ssf_info.sinfo_ppid = chk->rec.data.ppid;
		ssf->ssf_info.sinfo_context = chk->rec.data.context;
		ssf->ssf_info.sinfo_assoc_id = sctp_get_associd(stcb);
		ssf->ssf_assoc_id = sctp_get_associd(stcb);
	}
	if (chk->data != NULL) {
		/* Trim off the sctp chunk header (it should be there) */
		if (chk->send_size == chkhdr_len + payload_len + padding_len) {
			m_adj(chk->data, chkhdr_len);
			m_adj(chk->data, -padding_len);
			sctp_mbuf_crush(chk->data);
			chk->send_size -= (chkhdr_len + padding_len);
		}
	}
	SCTP_BUF_NEXT(m_notify) = chk->data;
	/* Steal off the mbuf */
	chk->data = NULL;
	/*
	 * For this case, we check the actual socket buffer, since the assoc
	 * is going away we don't want to overfill the socket buffer for a
	 * non-reader
	 */
	if (sctp_sbspace_failedmsgs(&stcb->sctp_socket->so_rcv) < SCTP_BUF_LEN(m_notify)) {
		sctp_m_freem(m_notify);
		return;
	}
	/* append to socket */
	control = sctp_build_readq_entry(stcb, stcb->asoc.primary_destination,
	    0, 0, stcb->asoc.context, 0, 0, 0,
	    m_notify);
	if (control == NULL) {
		/* no memory */
		sctp_m_freem(m_notify);
		return;
	}
	control->length = SCTP_BUF_LEN(m_notify);
	control->spec_flags = M_NOTIFICATION;
	/* not that we need this */
	control->tail_mbuf = m_notify;
	sctp_add_to_readq(stcb->sctp_ep, stcb,
	    control,
	    &stcb->sctp_socket->so_rcv, 1,
	    SCTP_READ_LOCK_NOT_HELD,
	    so_locked);
}


static void
sctp_notify_send_failed2(struct sctp_tcb *stcb, uint32_t error,
    struct sctp_stream_queue_pending *sp, int so_locked
#if !defined(__APPLE__) && !defined(SCTP_SO_LOCK_TESTING)
    SCTP_UNUSED
#endif
)
{
	struct mbuf *m_notify;
	struct sctp_send_failed *ssf;
	struct sctp_send_failed_event *ssfe;
	struct sctp_queued_to_read *control;
	int notifhdr_len;

	if ((stcb == NULL) ||
	    (sctp_stcb_is_feature_off(stcb->sctp_ep, stcb, SCTP_PCB_FLAGS_RECVSENDFAILEVNT) &&
	    sctp_stcb_is_feature_off(stcb->sctp_ep, stcb, SCTP_PCB_FLAGS_RECVNSENDFAILEVNT))) {
		/* event not enabled */
		return;
	}
	if (sctp_stcb_is_feature_on(stcb->sctp_ep, stcb, SCTP_PCB_FLAGS_RECVNSENDFAILEVNT)) {
		notifhdr_len = sizeof(struct sctp_send_failed_event);
	} else {
		notifhdr_len = sizeof(struct sctp_send_failed);
	}
	m_notify = sctp_get_mbuf_for_msg(notifhdr_len, 0, M_NOWAIT, 1, MT_DATA);
	if (m_notify == NULL) {
		/* no space left */
		return;
	}
	SCTP_BUF_LEN(m_notify) = notifhdr_len;
	if (sctp_stcb_is_feature_on(stcb->sctp_ep, stcb, SCTP_PCB_FLAGS_RECVNSENDFAILEVNT)) {
		ssfe = mtod(m_notify, struct sctp_send_failed_event *);
		memset(ssfe, 0, notifhdr_len);
		ssfe->ssfe_type = SCTP_SEND_FAILED_EVENT;
		ssfe->ssfe_flags = SCTP_DATA_UNSENT;
		ssfe->ssfe_length = (uint32_t)(notifhdr_len + sp->length);
		ssfe->ssfe_error = error;
		/* not exactly what the user sent in, but should be close :) */
		ssfe->ssfe_info.snd_sid = sp->sid;
		if (sp->some_taken) {
			ssfe->ssfe_info.snd_flags = SCTP_DATA_LAST_FRAG;
		} else {
			ssfe->ssfe_info.snd_flags = SCTP_DATA_NOT_FRAG;
		}
		ssfe->ssfe_info.snd_ppid = sp->ppid;
		ssfe->ssfe_info.snd_context = sp->context;
		ssfe->ssfe_info.snd_assoc_id = sctp_get_associd(stcb);
		ssfe->ssfe_assoc_id = sctp_get_associd(stcb);
	} else {
		ssf = mtod(m_notify, struct sctp_send_failed *);
		memset(ssf, 0, notifhdr_len);
		ssf->ssf_type = SCTP_SEND_FAILED;
		ssf->ssf_flags = SCTP_DATA_UNSENT;
		ssf->ssf_length = (uint32_t)(notifhdr_len + sp->length);
		ssf->ssf_error = error;
		/* not exactly what the user sent in, but should be close :) */
		ssf->ssf_info.sinfo_stream = sp->sid;
		ssf->ssf_info.sinfo_ssn = 0;
		if (sp->some_taken) {
			ssf->ssf_info.sinfo_flags = SCTP_DATA_LAST_FRAG;
		} else {
			ssf->ssf_info.sinfo_flags = SCTP_DATA_NOT_FRAG;
		}
		ssf->ssf_info.sinfo_ppid = sp->ppid;
		ssf->ssf_info.sinfo_context = sp->context;
		ssf->ssf_info.sinfo_assoc_id = sctp_get_associd(stcb);
		ssf->ssf_assoc_id = sctp_get_associd(stcb);
	}
	SCTP_BUF_NEXT(m_notify) = sp->data;

	/* Steal off the mbuf */
	sp->data = NULL;
	/*
	 * For this case, we check the actual socket buffer, since the assoc
	 * is going away we don't want to overfill the socket buffer for a
	 * non-reader
	 */
	if (sctp_sbspace_failedmsgs(&stcb->sctp_socket->so_rcv) < SCTP_BUF_LEN(m_notify)) {
		sctp_m_freem(m_notify);
		return;
	}
	/* append to socket */
	control = sctp_build_readq_entry(stcb, stcb->asoc.primary_destination,
	    0, 0, stcb->asoc.context, 0, 0, 0,
	    m_notify);
	if (control == NULL) {
		/* no memory */
		sctp_m_freem(m_notify);
		return;
	}
	control->length = SCTP_BUF_LEN(m_notify);
	control->spec_flags = M_NOTIFICATION;
	/* not that we need this */
	control->tail_mbuf = m_notify;
	sctp_add_to_readq(stcb->sctp_ep, stcb,
	    control,
	    &stcb->sctp_socket->so_rcv, 1, SCTP_READ_LOCK_NOT_HELD, so_locked);
}



static void
sctp_notify_adaptation_layer(struct sctp_tcb *stcb)
{
	struct mbuf *m_notify;
	struct sctp_adaptation_event *sai;
	struct sctp_queued_to_read *control;

	if ((stcb == NULL) ||
	    sctp_stcb_is_feature_off(stcb->sctp_ep, stcb, SCTP_PCB_FLAGS_ADAPTATIONEVNT)) {
		/* event not enabled */
		return;
	}

	m_notify = sctp_get_mbuf_for_msg(sizeof(struct sctp_adaption_event), 0, M_NOWAIT, 1, MT_DATA);
	if (m_notify == NULL)
		/* no space left */
		return;
	SCTP_BUF_LEN(m_notify) = 0;
	sai = mtod(m_notify, struct sctp_adaptation_event *);
	memset(sai, 0, sizeof(struct sctp_adaptation_event));
	sai->sai_type = SCTP_ADAPTATION_INDICATION;
	sai->sai_flags = 0;
	sai->sai_length = sizeof(struct sctp_adaptation_event);
	sai->sai_adaptation_ind = stcb->asoc.peers_adaptation;
	sai->sai_assoc_id = sctp_get_associd(stcb);

	SCTP_BUF_LEN(m_notify) = sizeof(struct sctp_adaptation_event);
	SCTP_BUF_NEXT(m_notify) = NULL;

	/* append to socket */
	control = sctp_build_readq_entry(stcb, stcb->asoc.primary_destination,
	    0, 0, stcb->asoc.context, 0, 0, 0,
	    m_notify);
	if (control == NULL) {
		/* no memory */
		sctp_m_freem(m_notify);
		return;
	}
	control->length = SCTP_BUF_LEN(m_notify);
	control->spec_flags = M_NOTIFICATION;
	/* not that we need this */
	control->tail_mbuf = m_notify;
	sctp_add_to_readq(stcb->sctp_ep, stcb,
	    control,
	    &stcb->sctp_socket->so_rcv, 1, SCTP_READ_LOCK_NOT_HELD, SCTP_SO_NOT_LOCKED);
}

/* This always must be called with the read-queue LOCKED in the INP */
static void
sctp_notify_partial_delivery_indication(struct sctp_tcb *stcb, uint32_t error,
    uint32_t val, int so_locked
#if !defined(__APPLE__) && !defined(SCTP_SO_LOCK_TESTING)
    SCTP_UNUSED
#endif
)
{
	struct mbuf *m_notify;
	struct sctp_pdapi_event *pdapi;
	struct sctp_queued_to_read *control;
	struct sockbuf *sb;

	if ((stcb == NULL) ||
	    sctp_stcb_is_feature_off(stcb->sctp_ep, stcb, SCTP_PCB_FLAGS_PDAPIEVNT)) {
		/* event not enabled */
		return;
	}
	if (stcb->sctp_ep->sctp_flags & SCTP_PCB_FLAGS_SOCKET_CANT_READ) {
		return;
	}

	m_notify = sctp_get_mbuf_for_msg(sizeof(struct sctp_pdapi_event), 0, M_NOWAIT, 1, MT_DATA);
	if (m_notify == NULL)
		/* no space left */
		return;
	SCTP_BUF_LEN(m_notify) = 0;
	pdapi = mtod(m_notify, struct sctp_pdapi_event *);
	memset(pdapi, 0, sizeof(struct sctp_pdapi_event));
	pdapi->pdapi_type = SCTP_PARTIAL_DELIVERY_EVENT;
	pdapi->pdapi_flags = 0;
	pdapi->pdapi_length = sizeof(struct sctp_pdapi_event);
	pdapi->pdapi_indication = error;
	pdapi->pdapi_stream = (val >> 16);
	pdapi->pdapi_seq = (val & 0x0000ffff);
	pdapi->pdapi_assoc_id = sctp_get_associd(stcb);

	SCTP_BUF_LEN(m_notify) = sizeof(struct sctp_pdapi_event);
	SCTP_BUF_NEXT(m_notify) = NULL;
	control = sctp_build_readq_entry(stcb, stcb->asoc.primary_destination,
	    0, 0, stcb->asoc.context, 0, 0, 0,
	    m_notify);
	if (control == NULL) {
		/* no memory */
		sctp_m_freem(m_notify);
		return;
	}
	control->length = SCTP_BUF_LEN(m_notify);
	control->spec_flags = M_NOTIFICATION;
	/* not that we need this */
	control->tail_mbuf = m_notify;
	sb = &stcb->sctp_socket->so_rcv;
	if (SCTP_BASE_SYSCTL(sctp_logging_level) & SCTP_SB_LOGGING_ENABLE) {
		sctp_sblog(sb, control->do_not_ref_stcb ? NULL : stcb, SCTP_LOG_SBALLOC, SCTP_BUF_LEN(m_notify));
	}
	sctp_sballoc(stcb, sb, m_notify);
	if (SCTP_BASE_SYSCTL(sctp_logging_level) & SCTP_SB_LOGGING_ENABLE) {
		sctp_sblog(sb, control->do_not_ref_stcb ? NULL : stcb, SCTP_LOG_SBRESULT, 0);
	}
	control->end_added = 1;
	if (stcb->asoc.control_pdapi)
		TAILQ_INSERT_AFTER(&stcb->sctp_ep->read_queue, stcb->asoc.control_pdapi, control, next);
	else {
		/* we really should not see this case */
		TAILQ_INSERT_TAIL(&stcb->sctp_ep->read_queue, control, next);
	}
	if (stcb->sctp_ep && stcb->sctp_socket) {
		/* This should always be the case */
#if defined(__APPLE__) || defined(SCTP_SO_LOCK_TESTING)
		struct socket *so;

		so = SCTP_INP_SO(stcb->sctp_ep);
		if (!so_locked) {
			atomic_add_int(&stcb->asoc.refcnt, 1);
			SCTP_TCB_UNLOCK(stcb);
			SCTP_SOCKET_LOCK(so, 1);
			SCTP_TCB_LOCK(stcb);
			atomic_subtract_int(&stcb->asoc.refcnt, 1);
			if (stcb->sctp_ep->sctp_flags & SCTP_PCB_FLAGS_SOCKET_GONE) {
				SCTP_SOCKET_UNLOCK(so, 1);
				return;
			}
		}
#endif
		sctp_sorwakeup(stcb->sctp_ep, stcb->sctp_socket);
#if defined(__APPLE__) || defined(SCTP_SO_LOCK_TESTING)
		if (!so_locked) {
			SCTP_SOCKET_UNLOCK(so, 1);
		}
#endif
	}
}

static void
sctp_notify_shutdown_event(struct sctp_tcb *stcb)
{
	struct mbuf *m_notify;
	struct sctp_shutdown_event *sse;
	struct sctp_queued_to_read *control;

	/*
	 * For TCP model AND UDP connected sockets we will send an error up
	 * when an SHUTDOWN completes
	 */
	if ((stcb->sctp_ep->sctp_flags & SCTP_PCB_FLAGS_TCPTYPE) ||
	    (stcb->sctp_ep->sctp_flags & SCTP_PCB_FLAGS_IN_TCPPOOL)) {
		/* mark socket closed for read/write and wakeup! */
#if defined(__APPLE__) || defined(SCTP_SO_LOCK_TESTING)
		struct socket *so;

		so = SCTP_INP_SO(stcb->sctp_ep);
		atomic_add_int(&stcb->asoc.refcnt, 1);
		SCTP_TCB_UNLOCK(stcb);
		SCTP_SOCKET_LOCK(so, 1);
		SCTP_TCB_LOCK(stcb);
		atomic_subtract_int(&stcb->asoc.refcnt, 1);
		if (stcb->asoc.state & SCTP_STATE_CLOSED_SOCKET) {
			SCTP_SOCKET_UNLOCK(so, 1);
			return;
		}
#endif
		socantsendmore(stcb->sctp_socket);
#if defined(__APPLE__) || defined(SCTP_SO_LOCK_TESTING)
		SCTP_SOCKET_UNLOCK(so, 1);
#endif
	}
	if (sctp_stcb_is_feature_off(stcb->sctp_ep, stcb, SCTP_PCB_FLAGS_RECVSHUTDOWNEVNT)) {
		/* event not enabled */
		return;
	}

	m_notify = sctp_get_mbuf_for_msg(sizeof(struct sctp_shutdown_event), 0, M_NOWAIT, 1, MT_DATA);
	if (m_notify == NULL)
		/* no space left */
		return;
	sse = mtod(m_notify, struct sctp_shutdown_event *);
	memset(sse, 0, sizeof(struct sctp_shutdown_event));
	sse->sse_type = SCTP_SHUTDOWN_EVENT;
	sse->sse_flags = 0;
	sse->sse_length = sizeof(struct sctp_shutdown_event);
	sse->sse_assoc_id = sctp_get_associd(stcb);

	SCTP_BUF_LEN(m_notify) = sizeof(struct sctp_shutdown_event);
	SCTP_BUF_NEXT(m_notify) = NULL;

	/* append to socket */
	control = sctp_build_readq_entry(stcb, stcb->asoc.primary_destination,
	    0, 0, stcb->asoc.context, 0, 0, 0,
	    m_notify);
	if (control == NULL) {
		/* no memory */
		sctp_m_freem(m_notify);
		return;
	}
	control->length = SCTP_BUF_LEN(m_notify);
	control->spec_flags = M_NOTIFICATION;
	/* not that we need this */
	control->tail_mbuf = m_notify;
	sctp_add_to_readq(stcb->sctp_ep, stcb,
	    control,
	    &stcb->sctp_socket->so_rcv, 1, SCTP_READ_LOCK_NOT_HELD, SCTP_SO_NOT_LOCKED);
}

static void
sctp_notify_sender_dry_event(struct sctp_tcb *stcb,
    int so_locked
#if !defined(__APPLE__) && !defined(SCTP_SO_LOCK_TESTING)
    SCTP_UNUSED
#endif
)
{
	struct mbuf *m_notify;
	struct sctp_sender_dry_event *event;
	struct sctp_queued_to_read *control;

	if ((stcb == NULL) ||
	    sctp_stcb_is_feature_off(stcb->sctp_ep, stcb, SCTP_PCB_FLAGS_DRYEVNT)) {
		/* event not enabled */
		return;
	}

	m_notify = sctp_get_mbuf_for_msg(sizeof(struct sctp_sender_dry_event), 0, M_NOWAIT, 1, MT_DATA);
	if (m_notify == NULL) {
		/* no space left */
		return;
	}
	SCTP_BUF_LEN(m_notify) = 0;
	event = mtod(m_notify, struct sctp_sender_dry_event *);
	memset(event, 0, sizeof(struct sctp_sender_dry_event));
	event->sender_dry_type = SCTP_SENDER_DRY_EVENT;
	event->sender_dry_flags = 0;
	event->sender_dry_length = sizeof(struct sctp_sender_dry_event);
	event->sender_dry_assoc_id = sctp_get_associd(stcb);

	SCTP_BUF_LEN(m_notify) = sizeof(struct sctp_sender_dry_event);
	SCTP_BUF_NEXT(m_notify) = NULL;

	/* append to socket */
	control = sctp_build_readq_entry(stcb, stcb->asoc.primary_destination,
	    0, 0, stcb->asoc.context, 0, 0, 0,
	    m_notify);
	if (control == NULL) {
		/* no memory */
		sctp_m_freem(m_notify);
		return;
	}
	control->length = SCTP_BUF_LEN(m_notify);
	control->spec_flags = M_NOTIFICATION;
	/* not that we need this */
	control->tail_mbuf = m_notify;
	sctp_add_to_readq(stcb->sctp_ep, stcb, control,
	    &stcb->sctp_socket->so_rcv, 1, SCTP_READ_LOCK_NOT_HELD, so_locked);
}


void
sctp_notify_stream_reset_add(struct sctp_tcb *stcb, uint16_t numberin, uint16_t numberout, int flag)
{
	struct mbuf *m_notify;
	struct sctp_queued_to_read *control;
	struct sctp_stream_change_event *stradd;

	if ((stcb == NULL) ||
	    (sctp_stcb_is_feature_off(stcb->sctp_ep, stcb, SCTP_PCB_FLAGS_STREAM_CHANGEEVNT))) {
		/* event not enabled */
		return;
	}
	if ((stcb->asoc.peer_req_out) && flag) {
		/* Peer made the request, don't tell the local user */
		stcb->asoc.peer_req_out = 0;
		return;
	}
	stcb->asoc.peer_req_out = 0;
	m_notify = sctp_get_mbuf_for_msg(sizeof(struct sctp_stream_change_event), 0, M_NOWAIT, 1, MT_DATA);
	if (m_notify == NULL)
		/* no space left */
		return;
	SCTP_BUF_LEN(m_notify) = 0;
	stradd = mtod(m_notify, struct sctp_stream_change_event *);
	memset(stradd, 0, sizeof(struct sctp_stream_change_event));
	stradd->strchange_type = SCTP_STREAM_CHANGE_EVENT;
	stradd->strchange_flags = flag;
	stradd->strchange_length = sizeof(struct sctp_stream_change_event);
	stradd->strchange_assoc_id = sctp_get_associd(stcb);
	stradd->strchange_instrms = numberin;
	stradd->strchange_outstrms = numberout;
	SCTP_BUF_LEN(m_notify) = sizeof(struct sctp_stream_change_event);
	SCTP_BUF_NEXT(m_notify) = NULL;
	if (sctp_sbspace(&stcb->asoc, &stcb->sctp_socket->so_rcv) < SCTP_BUF_LEN(m_notify)) {
		/* no space */
		sctp_m_freem(m_notify);
		return;
	}
	/* append to socket */
	control = sctp_build_readq_entry(stcb, stcb->asoc.primary_destination,
	    0, 0, stcb->asoc.context, 0, 0, 0,
	    m_notify);
	if (control == NULL) {
		/* no memory */
		sctp_m_freem(m_notify);
		return;
	}
	control->length = SCTP_BUF_LEN(m_notify);
	control->spec_flags = M_NOTIFICATION;
	/* not that we need this */
	control->tail_mbuf = m_notify;
	sctp_add_to_readq(stcb->sctp_ep, stcb,
	    control,
	    &stcb->sctp_socket->so_rcv, 1, SCTP_READ_LOCK_NOT_HELD, SCTP_SO_NOT_LOCKED);
}

void
sctp_notify_stream_reset_tsn(struct sctp_tcb *stcb, uint32_t sending_tsn, uint32_t recv_tsn, int flag)
{
	struct mbuf *m_notify;
	struct sctp_queued_to_read *control;
	struct sctp_assoc_reset_event *strasoc;

	if ((stcb == NULL) ||
	    (sctp_stcb_is_feature_off(stcb->sctp_ep, stcb, SCTP_PCB_FLAGS_ASSOC_RESETEVNT))) {
		/* event not enabled */
		return;
	}
	m_notify = sctp_get_mbuf_for_msg(sizeof(struct sctp_assoc_reset_event), 0, M_NOWAIT, 1, MT_DATA);
	if (m_notify == NULL)
		/* no space left */
		return;
	SCTP_BUF_LEN(m_notify) = 0;
	strasoc = mtod(m_notify, struct sctp_assoc_reset_event *);
	memset(strasoc, 0, sizeof(struct sctp_assoc_reset_event));
	strasoc->assocreset_type = SCTP_ASSOC_RESET_EVENT;
	strasoc->assocreset_flags = flag;
	strasoc->assocreset_length = sizeof(struct sctp_assoc_reset_event);
	strasoc->assocreset_assoc_id = sctp_get_associd(stcb);
	strasoc->assocreset_local_tsn = sending_tsn;
	strasoc->assocreset_remote_tsn = recv_tsn;
	SCTP_BUF_LEN(m_notify) = sizeof(struct sctp_assoc_reset_event);
	SCTP_BUF_NEXT(m_notify) = NULL;
	if (sctp_sbspace(&stcb->asoc, &stcb->sctp_socket->so_rcv) < SCTP_BUF_LEN(m_notify)) {
		/* no space */
		sctp_m_freem(m_notify);
		return;
	}
	/* append to socket */
	control = sctp_build_readq_entry(stcb, stcb->asoc.primary_destination,
	    0, 0, stcb->asoc.context, 0, 0, 0,
	    m_notify);
	if (control == NULL) {
		/* no memory */
		sctp_m_freem(m_notify);
		return;
	}
	control->length = SCTP_BUF_LEN(m_notify);
	control->spec_flags = M_NOTIFICATION;
	/* not that we need this */
	control->tail_mbuf = m_notify;
	sctp_add_to_readq(stcb->sctp_ep, stcb,
	    control,
	    &stcb->sctp_socket->so_rcv, 1, SCTP_READ_LOCK_NOT_HELD, SCTP_SO_NOT_LOCKED);
}



static void
sctp_notify_stream_reset(struct sctp_tcb *stcb,
    int number_entries, uint16_t *list, int flag)
{
	struct mbuf *m_notify;
	struct sctp_queued_to_read *control;
	struct sctp_stream_reset_event *strreset;
	int len;

	if ((stcb == NULL) ||
	    (sctp_stcb_is_feature_off(stcb->sctp_ep, stcb, SCTP_PCB_FLAGS_STREAM_RESETEVNT))) {
		/* event not enabled */
		return;
	}

	m_notify = sctp_get_mbuf_for_msg(MCLBYTES, 0, M_NOWAIT, 1, MT_DATA);
	if (m_notify == NULL)
		/* no space left */
		return;
	SCTP_BUF_LEN(m_notify) = 0;
	len = sizeof(struct sctp_stream_reset_event) + (number_entries * sizeof(uint16_t));
	if (len > M_TRAILINGSPACE(m_notify)) {
		/* never enough room */
		sctp_m_freem(m_notify);
		return;
	}
	strreset = mtod(m_notify, struct sctp_stream_reset_event *);
	memset(strreset, 0, len);
	strreset->strreset_type = SCTP_STREAM_RESET_EVENT;
	strreset->strreset_flags = flag;
	strreset->strreset_length = len;
	strreset->strreset_assoc_id = sctp_get_associd(stcb);
	if (number_entries) {
		int i;

		for (i = 0; i < number_entries; i++) {
			strreset->strreset_stream_list[i] = ntohs(list[i]);
		}
	}
	SCTP_BUF_LEN(m_notify) = len;
	SCTP_BUF_NEXT(m_notify) = NULL;
	if (sctp_sbspace(&stcb->asoc, &stcb->sctp_socket->so_rcv) < SCTP_BUF_LEN(m_notify)) {
		/* no space */
		sctp_m_freem(m_notify);
		return;
	}
	/* append to socket */
	control = sctp_build_readq_entry(stcb, stcb->asoc.primary_destination,
	    0, 0, stcb->asoc.context, 0, 0, 0,
	    m_notify);
	if (control == NULL) {
		/* no memory */
		sctp_m_freem(m_notify);
		return;
	}
	control->length = SCTP_BUF_LEN(m_notify);
	control->spec_flags = M_NOTIFICATION;
	/* not that we need this */
	control->tail_mbuf = m_notify;
	sctp_add_to_readq(stcb->sctp_ep, stcb,
	    control,
	    &stcb->sctp_socket->so_rcv, 1, SCTP_READ_LOCK_NOT_HELD, SCTP_SO_NOT_LOCKED);
}


static void
sctp_notify_remote_error(struct sctp_tcb *stcb, uint16_t error, struct sctp_error_chunk *chunk)
{
	struct mbuf *m_notify;
	struct sctp_remote_error *sre;
	struct sctp_queued_to_read *control;
	unsigned int notif_len;
	uint16_t chunk_len;

	if ((stcb == NULL) ||
	    sctp_stcb_is_feature_off(stcb->sctp_ep, stcb, SCTP_PCB_FLAGS_RECVPEERERR)) {
		return;
	}
	if (chunk != NULL) {
		chunk_len = ntohs(chunk->ch.chunk_length);
		/*
		 * Only SCTP_CHUNK_BUFFER_SIZE are guaranteed to be
		 * contiguous.
		 */
		if (chunk_len > SCTP_CHUNK_BUFFER_SIZE) {
			chunk_len = SCTP_CHUNK_BUFFER_SIZE;
		}
	} else {
		chunk_len = 0;
	}
	notif_len = (unsigned int)(sizeof(struct sctp_remote_error) + chunk_len);
	m_notify = sctp_get_mbuf_for_msg(notif_len, 0, M_NOWAIT, 1, MT_DATA);
	if (m_notify == NULL) {
		/* Retry with smaller value. */
		notif_len = (unsigned int)sizeof(struct sctp_remote_error);
		m_notify = sctp_get_mbuf_for_msg(notif_len, 0, M_NOWAIT, 1, MT_DATA);
		if (m_notify == NULL) {
			return;
		}
	}
	SCTP_BUF_NEXT(m_notify) = NULL;
	sre = mtod(m_notify, struct sctp_remote_error *);
	memset(sre, 0, notif_len);
	sre->sre_type = SCTP_REMOTE_ERROR;
	sre->sre_flags = 0;
	sre->sre_length = sizeof(struct sctp_remote_error);
	sre->sre_error = error;
	sre->sre_assoc_id = sctp_get_associd(stcb);
	if (notif_len > sizeof(struct sctp_remote_error)) {
		memcpy(sre->sre_data, chunk, chunk_len);
		sre->sre_length += chunk_len;
	}
	SCTP_BUF_LEN(m_notify) = sre->sre_length;
	control = sctp_build_readq_entry(stcb, stcb->asoc.primary_destination,
	    0, 0, stcb->asoc.context, 0, 0, 0,
	    m_notify);
	if (control != NULL) {
		control->length = SCTP_BUF_LEN(m_notify);
		control->spec_flags = M_NOTIFICATION;
		/* not that we need this */
		control->tail_mbuf = m_notify;
		sctp_add_to_readq(stcb->sctp_ep, stcb,
		    control,
		    &stcb->sctp_socket->so_rcv, 1,
		    SCTP_READ_LOCK_NOT_HELD, SCTP_SO_NOT_LOCKED);
	} else {
		sctp_m_freem(m_notify);
	}
}


void
sctp_ulp_notify(uint32_t notification, struct sctp_tcb *stcb,
    uint32_t error, void *data, int so_locked
#if !defined(__APPLE__) && !defined(SCTP_SO_LOCK_TESTING)
    SCTP_UNUSED
#endif
)
{
	if ((stcb == NULL) ||
	    (stcb->sctp_ep->sctp_flags & SCTP_PCB_FLAGS_SOCKET_GONE) ||
	    (stcb->sctp_ep->sctp_flags & SCTP_PCB_FLAGS_SOCKET_ALLGONE) ||
	    (stcb->asoc.state & SCTP_STATE_CLOSED_SOCKET)) {
		/* If the socket is gone we are out of here */
		return;
	}
	if (stcb->sctp_socket->so_rcv.sb_state & SBS_CANTRCVMORE) {
		return;
	}
	if ((SCTP_GET_STATE(stcb) == SCTP_STATE_COOKIE_WAIT) ||
	    (SCTP_GET_STATE(stcb) == SCTP_STATE_COOKIE_ECHOED)) {
		if ((notification == SCTP_NOTIFY_INTERFACE_DOWN) ||
		    (notification == SCTP_NOTIFY_INTERFACE_UP) ||
		    (notification == SCTP_NOTIFY_INTERFACE_CONFIRMED)) {
			/* Don't report these in front states */
			return;
		}
	}
	switch (notification) {
	case SCTP_NOTIFY_ASSOC_UP:
		if (stcb->asoc.assoc_up_sent == 0) {
			sctp_notify_assoc_change(SCTP_COMM_UP, stcb, error, NULL, 0, so_locked);
			stcb->asoc.assoc_up_sent = 1;
		}
		if (stcb->asoc.adaptation_needed && (stcb->asoc.adaptation_sent == 0)) {
			sctp_notify_adaptation_layer(stcb);
		}
		if (stcb->asoc.auth_supported == 0) {
			sctp_ulp_notify(SCTP_NOTIFY_NO_PEER_AUTH, stcb, 0,
			    NULL, so_locked);
		}
		break;
	case SCTP_NOTIFY_ASSOC_DOWN:
		sctp_notify_assoc_change(SCTP_SHUTDOWN_COMP, stcb, error, NULL, 0, so_locked);
		break;
	case SCTP_NOTIFY_INTERFACE_DOWN:
		{
			struct sctp_nets *net;

			net = (struct sctp_nets *)data;
			sctp_notify_peer_addr_change(stcb, SCTP_ADDR_UNREACHABLE,
			    (struct sockaddr *)&net->ro._l_addr, error, so_locked);
			break;
		}
	case SCTP_NOTIFY_INTERFACE_UP:
		{
			struct sctp_nets *net;

			net = (struct sctp_nets *)data;
			sctp_notify_peer_addr_change(stcb, SCTP_ADDR_AVAILABLE,
			    (struct sockaddr *)&net->ro._l_addr, error, so_locked);
			break;
		}
	case SCTP_NOTIFY_INTERFACE_CONFIRMED:
		{
			struct sctp_nets *net;

			net = (struct sctp_nets *)data;
			sctp_notify_peer_addr_change(stcb, SCTP_ADDR_CONFIRMED,
			    (struct sockaddr *)&net->ro._l_addr, error, so_locked);
			break;
		}
	case SCTP_NOTIFY_SPECIAL_SP_FAIL:
		sctp_notify_send_failed2(stcb, error,
		    (struct sctp_stream_queue_pending *)data, so_locked);
		break;
	case SCTP_NOTIFY_SENT_DG_FAIL:
		sctp_notify_send_failed(stcb, 1, error,
		    (struct sctp_tmit_chunk *)data, so_locked);
		break;
	case SCTP_NOTIFY_UNSENT_DG_FAIL:
		sctp_notify_send_failed(stcb, 0, error,
		    (struct sctp_tmit_chunk *)data, so_locked);
		break;
	case SCTP_NOTIFY_PARTIAL_DELVIERY_INDICATION:
		{
			uint32_t val;

			val = *((uint32_t *)data);

			sctp_notify_partial_delivery_indication(stcb, error, val, so_locked);
			break;
		}
	case SCTP_NOTIFY_ASSOC_LOC_ABORTED:
		if ((SCTP_GET_STATE(stcb) == SCTP_STATE_COOKIE_WAIT) ||
		    (SCTP_GET_STATE(stcb) == SCTP_STATE_COOKIE_ECHOED)) {
			sctp_notify_assoc_change(SCTP_CANT_STR_ASSOC, stcb, error, data, 0, so_locked);
		} else {
			sctp_notify_assoc_change(SCTP_COMM_LOST, stcb, error, data, 0, so_locked);
		}
		break;
	case SCTP_NOTIFY_ASSOC_REM_ABORTED:
		if ((SCTP_GET_STATE(stcb) == SCTP_STATE_COOKIE_WAIT) ||
		    (SCTP_GET_STATE(stcb) == SCTP_STATE_COOKIE_ECHOED)) {
			sctp_notify_assoc_change(SCTP_CANT_STR_ASSOC, stcb, error, data, 1, so_locked);
		} else {
			sctp_notify_assoc_change(SCTP_COMM_LOST, stcb, error, data, 1, so_locked);
		}
		break;
	case SCTP_NOTIFY_ASSOC_RESTART:
		sctp_notify_assoc_change(SCTP_RESTART, stcb, error, NULL, 0, so_locked);
		if (stcb->asoc.auth_supported == 0) {
			sctp_ulp_notify(SCTP_NOTIFY_NO_PEER_AUTH, stcb, 0,
			    NULL, so_locked);
		}
		break;
	case SCTP_NOTIFY_STR_RESET_SEND:
		sctp_notify_stream_reset(stcb, error, ((uint16_t *)data), SCTP_STREAM_RESET_OUTGOING_SSN);
		break;
	case SCTP_NOTIFY_STR_RESET_RECV:
		sctp_notify_stream_reset(stcb, error, ((uint16_t *)data), SCTP_STREAM_RESET_INCOMING);
		break;
	case SCTP_NOTIFY_STR_RESET_FAILED_OUT:
		sctp_notify_stream_reset(stcb, error, ((uint16_t *)data),
		    (SCTP_STREAM_RESET_OUTGOING_SSN | SCTP_STREAM_RESET_FAILED));
		break;
	case SCTP_NOTIFY_STR_RESET_DENIED_OUT:
		sctp_notify_stream_reset(stcb, error, ((uint16_t *)data),
		    (SCTP_STREAM_RESET_OUTGOING_SSN | SCTP_STREAM_RESET_DENIED));
		break;
	case SCTP_NOTIFY_STR_RESET_FAILED_IN:
		sctp_notify_stream_reset(stcb, error, ((uint16_t *)data),
		    (SCTP_STREAM_RESET_INCOMING | SCTP_STREAM_RESET_FAILED));
		break;
	case SCTP_NOTIFY_STR_RESET_DENIED_IN:
		sctp_notify_stream_reset(stcb, error, ((uint16_t *)data),
		    (SCTP_STREAM_RESET_INCOMING | SCTP_STREAM_RESET_DENIED));
		break;
	case SCTP_NOTIFY_ASCONF_ADD_IP:
		sctp_notify_peer_addr_change(stcb, SCTP_ADDR_ADDED, data,
		    error, so_locked);
		break;
	case SCTP_NOTIFY_ASCONF_DELETE_IP:
		sctp_notify_peer_addr_change(stcb, SCTP_ADDR_REMOVED, data,
		    error, so_locked);
		break;
	case SCTP_NOTIFY_ASCONF_SET_PRIMARY:
		sctp_notify_peer_addr_change(stcb, SCTP_ADDR_MADE_PRIM, data,
		    error, so_locked);
		break;
	case SCTP_NOTIFY_PEER_SHUTDOWN:
		sctp_notify_shutdown_event(stcb);
		break;
	case SCTP_NOTIFY_AUTH_NEW_KEY:
		sctp_notify_authentication(stcb, SCTP_AUTH_NEW_KEY, error,
		    (uint16_t)(uintptr_t)data,
		    so_locked);
		break;
	case SCTP_NOTIFY_AUTH_FREE_KEY:
		sctp_notify_authentication(stcb, SCTP_AUTH_FREE_KEY, error,
		    (uint16_t)(uintptr_t)data,
		    so_locked);
		break;
	case SCTP_NOTIFY_NO_PEER_AUTH:
		sctp_notify_authentication(stcb, SCTP_AUTH_NO_AUTH, error,
		    (uint16_t)(uintptr_t)data,
		    so_locked);
		break;
	case SCTP_NOTIFY_SENDER_DRY:
		sctp_notify_sender_dry_event(stcb, so_locked);
		break;
	case SCTP_NOTIFY_REMOTE_ERROR:
		sctp_notify_remote_error(stcb, error, data);
		break;
	default:
		SCTPDBG(SCTP_DEBUG_UTIL1, "%s: unknown notification %xh (%u)\n",
		    __func__, notification, notification);
		break;
	}			/* end switch */
}

void
sctp_report_all_outbound(struct sctp_tcb *stcb, uint16_t error, int holds_lock, int so_locked
#if !defined(__APPLE__) && !defined(SCTP_SO_LOCK_TESTING)
    SCTP_UNUSED
#endif
)
{
	struct sctp_association *asoc;
	struct sctp_stream_out *outs;
	struct sctp_tmit_chunk *chk, *nchk;
	struct sctp_stream_queue_pending *sp, *nsp;
	int i;

	if (stcb == NULL) {
		return;
	}
	asoc = &stcb->asoc;
	if (asoc->state & SCTP_STATE_ABOUT_TO_BE_FREED) {
		/* already being freed */
		return;
	}
	if ((stcb->sctp_ep->sctp_flags & SCTP_PCB_FLAGS_SOCKET_GONE) ||
	    (stcb->sctp_ep->sctp_flags & SCTP_PCB_FLAGS_SOCKET_ALLGONE) ||
	    (asoc->state & SCTP_STATE_CLOSED_SOCKET)) {
		return;
	}
	/* now through all the gunk freeing chunks */
	if (holds_lock == 0) {
		SCTP_TCB_SEND_LOCK(stcb);
	}
	/* sent queue SHOULD be empty */
	TAILQ_FOREACH_SAFE(chk, &asoc->sent_queue, sctp_next, nchk) {
		TAILQ_REMOVE(&asoc->sent_queue, chk, sctp_next);
		asoc->sent_queue_cnt--;
		if (chk->sent != SCTP_DATAGRAM_NR_ACKED) {
			if (asoc->strmout[chk->rec.data.sid].chunks_on_queues > 0) {
				asoc->strmout[chk->rec.data.sid].chunks_on_queues--;
#ifdef INVARIANTS
			} else {
				panic("No chunks on the queues for sid %u.", chk->rec.data.sid);
#endif
			}
		}
		if (chk->data != NULL) {
			sctp_free_bufspace(stcb, asoc, chk, 1);
			sctp_ulp_notify(SCTP_NOTIFY_SENT_DG_FAIL, stcb,
			    error, chk, so_locked);
			if (chk->data) {
				sctp_m_freem(chk->data);
				chk->data = NULL;
			}
		}
		sctp_free_a_chunk(stcb, chk, so_locked);
		/* sa_ignore FREED_MEMORY */
	}
	/* pending send queue SHOULD be empty */
	TAILQ_FOREACH_SAFE(chk, &asoc->send_queue, sctp_next, nchk) {
		TAILQ_REMOVE(&asoc->send_queue, chk, sctp_next);
		asoc->send_queue_cnt--;
		if (asoc->strmout[chk->rec.data.sid].chunks_on_queues > 0) {
			asoc->strmout[chk->rec.data.sid].chunks_on_queues--;
#ifdef INVARIANTS
		} else {
			panic("No chunks on the queues for sid %u.", chk->rec.data.sid);
#endif
		}
		if (chk->data != NULL) {
			sctp_free_bufspace(stcb, asoc, chk, 1);
			sctp_ulp_notify(SCTP_NOTIFY_UNSENT_DG_FAIL, stcb,
			    error, chk, so_locked);
			if (chk->data) {
				sctp_m_freem(chk->data);
				chk->data = NULL;
			}
		}
		sctp_free_a_chunk(stcb, chk, so_locked);
		/* sa_ignore FREED_MEMORY */
	}
	for (i = 0; i < asoc->streamoutcnt; i++) {
		/* For each stream */
		outs = &asoc->strmout[i];
		/* clean up any sends there */
		TAILQ_FOREACH_SAFE(sp, &outs->outqueue, next, nsp) {
			atomic_subtract_int(&asoc->stream_queue_cnt, 1);
			TAILQ_REMOVE(&outs->outqueue, sp, next);
			stcb->asoc.ss_functions.sctp_ss_remove_from_stream(stcb, asoc, outs, sp, 1);
			sctp_free_spbufspace(stcb, asoc, sp);
			if (sp->data) {
				sctp_ulp_notify(SCTP_NOTIFY_SPECIAL_SP_FAIL, stcb,
				    error, (void *)sp, so_locked);
				if (sp->data) {
					sctp_m_freem(sp->data);
					sp->data = NULL;
					sp->tail_mbuf = NULL;
					sp->length = 0;
				}
			}
			if (sp->net) {
				sctp_free_remote_addr(sp->net);
				sp->net = NULL;
			}
			/* Free the chunk */
			sctp_free_a_strmoq(stcb, sp, so_locked);
			/* sa_ignore FREED_MEMORY */
		}
	}

	if (holds_lock == 0) {
		SCTP_TCB_SEND_UNLOCK(stcb);
	}
}

void
sctp_abort_notification(struct sctp_tcb *stcb, uint8_t from_peer, uint16_t error,
    struct sctp_abort_chunk *abort, int so_locked
#if !defined(__APPLE__) && !defined(SCTP_SO_LOCK_TESTING)
    SCTP_UNUSED
#endif
)
{
	if (stcb == NULL) {
		return;
	}
	if ((stcb->sctp_ep->sctp_flags & SCTP_PCB_FLAGS_IN_TCPPOOL) ||
	    ((stcb->sctp_ep->sctp_flags & SCTP_PCB_FLAGS_TCPTYPE) &&
	    (stcb->sctp_ep->sctp_flags & SCTP_PCB_FLAGS_CONNECTED))) {
		stcb->sctp_ep->sctp_flags |= SCTP_PCB_FLAGS_WAS_ABORTED;
	}
	if ((stcb->sctp_ep->sctp_flags & SCTP_PCB_FLAGS_SOCKET_GONE) ||
	    (stcb->sctp_ep->sctp_flags & SCTP_PCB_FLAGS_SOCKET_ALLGONE) ||
	    (stcb->asoc.state & SCTP_STATE_CLOSED_SOCKET)) {
		return;
	}
	/* Tell them we lost the asoc */
	sctp_report_all_outbound(stcb, error, 1, so_locked);
	if (from_peer) {
		sctp_ulp_notify(SCTP_NOTIFY_ASSOC_REM_ABORTED, stcb, error, abort, so_locked);
	} else {
		sctp_ulp_notify(SCTP_NOTIFY_ASSOC_LOC_ABORTED, stcb, error, abort, so_locked);
	}
}

void
sctp_abort_association(struct sctp_inpcb *inp, struct sctp_tcb *stcb,
    struct mbuf *m, int iphlen,
    struct sockaddr *src, struct sockaddr *dst,
    struct sctphdr *sh, struct mbuf *op_err,
    uint8_t mflowtype, uint32_t mflowid,
    uint32_t vrf_id, uint16_t port)
{
	uint32_t vtag;
#if defined(__APPLE__) || defined(SCTP_SO_LOCK_TESTING)
	struct socket *so;
#endif

	vtag = 0;
	if (stcb != NULL) {
		vtag = stcb->asoc.peer_vtag;
		vrf_id = stcb->asoc.vrf_id;
	}
	sctp_send_abort(m, iphlen, src, dst, sh, vtag, op_err,
	    mflowtype, mflowid, inp->fibnum,
	    vrf_id, port);
	if (stcb != NULL) {
		/* We have a TCB to abort, send notification too */
		sctp_abort_notification(stcb, 0, 0, NULL, SCTP_SO_NOT_LOCKED);
		SCTP_ADD_SUBSTATE(stcb, SCTP_STATE_WAS_ABORTED);
		/* Ok, now lets free it */
#if defined(__APPLE__) || defined(SCTP_SO_LOCK_TESTING)
		so = SCTP_INP_SO(inp);
		atomic_add_int(&stcb->asoc.refcnt, 1);
		SCTP_TCB_UNLOCK(stcb);
		SCTP_SOCKET_LOCK(so, 1);
		SCTP_TCB_LOCK(stcb);
		atomic_subtract_int(&stcb->asoc.refcnt, 1);
#endif
		SCTP_STAT_INCR_COUNTER32(sctps_aborted);
		if ((SCTP_GET_STATE(stcb) == SCTP_STATE_OPEN) ||
		    (SCTP_GET_STATE(stcb) == SCTP_STATE_SHUTDOWN_RECEIVED)) {
			SCTP_STAT_DECR_GAUGE32(sctps_currestab);
		}
		(void)sctp_free_assoc(inp, stcb, SCTP_NORMAL_PROC,
		    SCTP_FROM_SCTPUTIL + SCTP_LOC_4);
#if defined(__APPLE__) || defined(SCTP_SO_LOCK_TESTING)
		SCTP_SOCKET_UNLOCK(so, 1);
#endif
	}
}
#ifdef SCTP_ASOCLOG_OF_TSNS
void
sctp_print_out_track_log(struct sctp_tcb *stcb)
{
#ifdef NOSIY_PRINTS
	int i;

	SCTP_PRINTF("Last ep reason:%x\n", stcb->sctp_ep->last_abort_code);
	SCTP_PRINTF("IN bound TSN log-aaa\n");
	if ((stcb->asoc.tsn_in_at == 0) && (stcb->asoc.tsn_in_wrapped == 0)) {
		SCTP_PRINTF("None rcvd\n");
		goto none_in;
	}
	if (stcb->asoc.tsn_in_wrapped) {
		for (i = stcb->asoc.tsn_in_at; i < SCTP_TSN_LOG_SIZE; i++) {
			SCTP_PRINTF("TSN:%x strm:%d seq:%d flags:%x sz:%d\n",
			    stcb->asoc.in_tsnlog[i].tsn,
			    stcb->asoc.in_tsnlog[i].strm,
			    stcb->asoc.in_tsnlog[i].seq,
			    stcb->asoc.in_tsnlog[i].flgs,
			    stcb->asoc.in_tsnlog[i].sz);
		}
	}
	if (stcb->asoc.tsn_in_at) {
		for (i = 0; i < stcb->asoc.tsn_in_at; i++) {
			SCTP_PRINTF("TSN:%x strm:%d seq:%d flags:%x sz:%d\n",
			    stcb->asoc.in_tsnlog[i].tsn,
			    stcb->asoc.in_tsnlog[i].strm,
			    stcb->asoc.in_tsnlog[i].seq,
			    stcb->asoc.in_tsnlog[i].flgs,
			    stcb->asoc.in_tsnlog[i].sz);
		}
	}
none_in:
	SCTP_PRINTF("OUT bound TSN log-aaa\n");
	if ((stcb->asoc.tsn_out_at == 0) &&
	    (stcb->asoc.tsn_out_wrapped == 0)) {
		SCTP_PRINTF("None sent\n");
	}
	if (stcb->asoc.tsn_out_wrapped) {
		for (i = stcb->asoc.tsn_out_at; i < SCTP_TSN_LOG_SIZE; i++) {
			SCTP_PRINTF("TSN:%x strm:%d seq:%d flags:%x sz:%d\n",
			    stcb->asoc.out_tsnlog[i].tsn,
			    stcb->asoc.out_tsnlog[i].strm,
			    stcb->asoc.out_tsnlog[i].seq,
			    stcb->asoc.out_tsnlog[i].flgs,
			    stcb->asoc.out_tsnlog[i].sz);
		}
	}
	if (stcb->asoc.tsn_out_at) {
		for (i = 0; i < stcb->asoc.tsn_out_at; i++) {
			SCTP_PRINTF("TSN:%x strm:%d seq:%d flags:%x sz:%d\n",
			    stcb->asoc.out_tsnlog[i].tsn,
			    stcb->asoc.out_tsnlog[i].strm,
			    stcb->asoc.out_tsnlog[i].seq,
			    stcb->asoc.out_tsnlog[i].flgs,
			    stcb->asoc.out_tsnlog[i].sz);
		}
	}
#endif
}
#endif

void
sctp_abort_an_association(struct sctp_inpcb *inp, struct sctp_tcb *stcb,
    struct mbuf *op_err,
    int so_locked
#if !defined(__APPLE__) && !defined(SCTP_SO_LOCK_TESTING)
    SCTP_UNUSED
#endif
)
{
#if defined(__APPLE__) || defined(SCTP_SO_LOCK_TESTING)
	struct socket *so;
#endif

#if defined(__APPLE__) || defined(SCTP_SO_LOCK_TESTING)
	so = SCTP_INP_SO(inp);
#endif
	if (stcb == NULL) {
		/* Got to have a TCB */
		if (inp->sctp_flags & SCTP_PCB_FLAGS_SOCKET_GONE) {
			if (LIST_EMPTY(&inp->sctp_asoc_list)) {
				sctp_inpcb_free(inp, SCTP_FREE_SHOULD_USE_ABORT,
				    SCTP_CALLED_DIRECTLY_NOCMPSET);
			}
		}
		return;
	} else {
		SCTP_ADD_SUBSTATE(stcb, SCTP_STATE_WAS_ABORTED);
	}
	/* notify the peer */
	sctp_send_abort_tcb(stcb, op_err, so_locked);
	SCTP_STAT_INCR_COUNTER32(sctps_aborted);
	if ((SCTP_GET_STATE(stcb) == SCTP_STATE_OPEN) ||
	    (SCTP_GET_STATE(stcb) == SCTP_STATE_SHUTDOWN_RECEIVED)) {
		SCTP_STAT_DECR_GAUGE32(sctps_currestab);
	}
	/* notify the ulp */
	if ((inp->sctp_flags & SCTP_PCB_FLAGS_SOCKET_GONE) == 0) {
		sctp_abort_notification(stcb, 0, 0, NULL, so_locked);
	}
	/* now free the asoc */
#ifdef SCTP_ASOCLOG_OF_TSNS
	sctp_print_out_track_log(stcb);
#endif
#if defined(__APPLE__) || defined(SCTP_SO_LOCK_TESTING)
	if (!so_locked) {
		atomic_add_int(&stcb->asoc.refcnt, 1);
		SCTP_TCB_UNLOCK(stcb);
		SCTP_SOCKET_LOCK(so, 1);
		SCTP_TCB_LOCK(stcb);
		atomic_subtract_int(&stcb->asoc.refcnt, 1);
	}
#endif
	(void)sctp_free_assoc(inp, stcb, SCTP_NORMAL_PROC,
	    SCTP_FROM_SCTPUTIL + SCTP_LOC_5);
#if defined(__APPLE__) || defined(SCTP_SO_LOCK_TESTING)
	if (!so_locked) {
		SCTP_SOCKET_UNLOCK(so, 1);
	}
#endif
}

void
sctp_handle_ootb(struct mbuf *m, int iphlen, int offset,
    struct sockaddr *src, struct sockaddr *dst,
    struct sctphdr *sh, struct sctp_inpcb *inp,
    struct mbuf *cause,
    uint8_t mflowtype, uint32_t mflowid, uint16_t fibnum,
    uint32_t vrf_id, uint16_t port)
{
	struct sctp_chunkhdr *ch, chunk_buf;
	unsigned int chk_length;
	int contains_init_chunk;

	SCTP_STAT_INCR_COUNTER32(sctps_outoftheblue);
	/* Generate a TO address for future reference */
	if (inp && (inp->sctp_flags & SCTP_PCB_FLAGS_SOCKET_GONE)) {
		if (LIST_EMPTY(&inp->sctp_asoc_list)) {
			sctp_inpcb_free(inp, SCTP_FREE_SHOULD_USE_ABORT,
			    SCTP_CALLED_DIRECTLY_NOCMPSET);
		}
	}
	contains_init_chunk = 0;
	ch = (struct sctp_chunkhdr *)sctp_m_getptr(m, offset,
	    sizeof(*ch), (uint8_t *)&chunk_buf);
	while (ch != NULL) {
		chk_length = ntohs(ch->chunk_length);
		if (chk_length < sizeof(*ch)) {
			/* break to abort land */
			break;
		}
		switch (ch->chunk_type) {
		case SCTP_INIT:
			contains_init_chunk = 1;
			break;
		case SCTP_PACKET_DROPPED:
			/* we don't respond to pkt-dropped */
			return;
		case SCTP_ABORT_ASSOCIATION:
			/* we don't respond with an ABORT to an ABORT */
			return;
		case SCTP_SHUTDOWN_COMPLETE:
			/*
			 * we ignore it since we are not waiting for it and
			 * peer is gone
			 */
			return;
		case SCTP_SHUTDOWN_ACK:
			sctp_send_shutdown_complete2(src, dst, sh,
			    mflowtype, mflowid, fibnum,
			    vrf_id, port);
			return;
		default:
			break;
		}
		offset += SCTP_SIZE32(chk_length);
		ch = (struct sctp_chunkhdr *)sctp_m_getptr(m, offset,
		    sizeof(*ch), (uint8_t *)&chunk_buf);
	}
	if ((SCTP_BASE_SYSCTL(sctp_blackhole) == 0) ||
	    ((SCTP_BASE_SYSCTL(sctp_blackhole) == 1) &&
	    (contains_init_chunk == 0))) {
		sctp_send_abort(m, iphlen, src, dst, sh, 0, cause,
		    mflowtype, mflowid, fibnum,
		    vrf_id, port);
	}
}

/*
 * check the inbound datagram to make sure there is not an abort inside it,
 * if there is return 1, else return 0.
 */
int
sctp_is_there_an_abort_here(struct mbuf *m, int iphlen, uint32_t *vtagfill)
{
	struct sctp_chunkhdr *ch;
	struct sctp_init_chunk *init_chk, chunk_buf;
	int offset;
	unsigned int chk_length;

	offset = iphlen + sizeof(struct sctphdr);
	ch = (struct sctp_chunkhdr *)sctp_m_getptr(m, offset, sizeof(*ch),
	    (uint8_t *)&chunk_buf);
	while (ch != NULL) {
		chk_length = ntohs(ch->chunk_length);
		if (chk_length < sizeof(*ch)) {
			/* packet is probably corrupt */
			break;
		}
		/* we seem to be ok, is it an abort? */
		if (ch->chunk_type == SCTP_ABORT_ASSOCIATION) {
			/* yep, tell them */
			return (1);
		}
		if (ch->chunk_type == SCTP_INITIATION) {
			/* need to update the Vtag */
			init_chk = (struct sctp_init_chunk *)sctp_m_getptr(m,
			    offset, sizeof(*init_chk), (uint8_t *)&chunk_buf);
			if (init_chk != NULL) {
				*vtagfill = ntohl(init_chk->init.initiate_tag);
			}
		}
		/* Nope, move to the next chunk */
		offset += SCTP_SIZE32(chk_length);
		ch = (struct sctp_chunkhdr *)sctp_m_getptr(m, offset,
		    sizeof(*ch), (uint8_t *)&chunk_buf);
	}
	return (0);
}

/*
 * currently (2/02), ifa_addr embeds scope_id's and don't have sin6_scope_id
 * set (i.e. it's 0) so, create this function to compare link local scopes
 */
#ifdef INET6
uint32_t
sctp_is_same_scope(struct sockaddr_in6 *addr1, struct sockaddr_in6 *addr2)
{
	struct sockaddr_in6 a, b;

	/* save copies */
	a = *addr1;
	b = *addr2;

	if (a.sin6_scope_id == 0)
		if (sa6_recoverscope(&a)) {
			/* can't get scope, so can't match */
			return (0);
		}
	if (b.sin6_scope_id == 0)
		if (sa6_recoverscope(&b)) {
			/* can't get scope, so can't match */
			return (0);
		}
	if (a.sin6_scope_id != b.sin6_scope_id)
		return (0);

	return (1);
}

/*
 * returns a sockaddr_in6 with embedded scope recovered and removed
 */
struct sockaddr_in6 *
sctp_recover_scope(struct sockaddr_in6 *addr, struct sockaddr_in6 *store)
{
	/* check and strip embedded scope junk */
	if (addr->sin6_family == AF_INET6) {
		if (IN6_IS_SCOPE_LINKLOCAL(&addr->sin6_addr)) {
			if (addr->sin6_scope_id == 0) {
				*store = *addr;
				if (!sa6_recoverscope(store)) {
					/* use the recovered scope */
					addr = store;
				}
			} else {
				/* else, return the original "to" addr */
				in6_clearscope(&addr->sin6_addr);
			}
		}
	}
	return (addr);
}
#endif

/*
 * are the two addresses the same?  currently a "scopeless" check returns: 1
 * if same, 0 if not
 */
int
sctp_cmpaddr(struct sockaddr *sa1, struct sockaddr *sa2)
{

	/* must be valid */
	if (sa1 == NULL || sa2 == NULL)
		return (0);

	/* must be the same family */
	if (sa1->sa_family != sa2->sa_family)
		return (0);

	switch (sa1->sa_family) {
#ifdef INET6
	case AF_INET6:
		{
			/* IPv6 addresses */
			struct sockaddr_in6 *sin6_1, *sin6_2;

			sin6_1 = (struct sockaddr_in6 *)sa1;
			sin6_2 = (struct sockaddr_in6 *)sa2;
			return (SCTP6_ARE_ADDR_EQUAL(sin6_1,
			    sin6_2));
		}
#endif
#ifdef INET
	case AF_INET:
		{
			/* IPv4 addresses */
			struct sockaddr_in *sin_1, *sin_2;

			sin_1 = (struct sockaddr_in *)sa1;
			sin_2 = (struct sockaddr_in *)sa2;
			return (sin_1->sin_addr.s_addr == sin_2->sin_addr.s_addr);
		}
#endif
	default:
		/* we don't do these... */
		return (0);
	}
}

void
sctp_print_address(struct sockaddr *sa)
{
#ifdef INET6
	char ip6buf[INET6_ADDRSTRLEN];
#endif

	switch (sa->sa_family) {
#ifdef INET6
	case AF_INET6:
		{
			struct sockaddr_in6 *sin6;

			sin6 = (struct sockaddr_in6 *)sa;
			SCTP_PRINTF("IPv6 address: %s:port:%d scope:%u\n",
			    ip6_sprintf(ip6buf, &sin6->sin6_addr),
			    ntohs(sin6->sin6_port),
			    sin6->sin6_scope_id);
			break;
		}
#endif
#ifdef INET
	case AF_INET:
		{
			struct sockaddr_in *sin;
			unsigned char *p;

			sin = (struct sockaddr_in *)sa;
			p = (unsigned char *)&sin->sin_addr;
			SCTP_PRINTF("IPv4 address: %u.%u.%u.%u:%d\n",
			    p[0], p[1], p[2], p[3], ntohs(sin->sin_port));
			break;
		}
#endif
	default:
		SCTP_PRINTF("?\n");
		break;
	}
}

void
sctp_pull_off_control_to_new_inp(struct sctp_inpcb *old_inp,
    struct sctp_inpcb *new_inp,
    struct sctp_tcb *stcb,
    int waitflags)
{
	/*
	 * go through our old INP and pull off any control structures that
	 * belong to stcb and move then to the new inp.
	 */
	struct socket *old_so, *new_so;
	struct sctp_queued_to_read *control, *nctl;
	struct sctp_readhead tmp_queue;
	struct mbuf *m;
	int error = 0;

	old_so = old_inp->sctp_socket;
	new_so = new_inp->sctp_socket;
	TAILQ_INIT(&tmp_queue);
	error = sblock(&old_so->so_rcv, waitflags);
	if (error) {
		/*
		 * Gak, can't get sblock, we have a problem. data will be
		 * left stranded.. and we don't dare look at it since the
		 * other thread may be reading something. Oh well, its a
		 * screwed up app that does a peeloff OR a accept while
		 * reading from the main socket... actually its only the
		 * peeloff() case, since I think read will fail on a
		 * listening socket..
		 */
		return;
	}
	/* lock the socket buffers */
	SCTP_INP_READ_LOCK(old_inp);
	TAILQ_FOREACH_SAFE(control, &old_inp->read_queue, next, nctl) {
		/* Pull off all for out target stcb */
		if (control->stcb == stcb) {
			/* remove it we want it */
			TAILQ_REMOVE(&old_inp->read_queue, control, next);
			TAILQ_INSERT_TAIL(&tmp_queue, control, next);
			m = control->data;
			while (m) {
				if (SCTP_BASE_SYSCTL(sctp_logging_level) & SCTP_SB_LOGGING_ENABLE) {
					sctp_sblog(&old_so->so_rcv, control->do_not_ref_stcb ? NULL : stcb, SCTP_LOG_SBFREE, SCTP_BUF_LEN(m));
				}
				sctp_sbfree(control, stcb, &old_so->so_rcv, m);
				if (SCTP_BASE_SYSCTL(sctp_logging_level) & SCTP_SB_LOGGING_ENABLE) {
					sctp_sblog(&old_so->so_rcv, control->do_not_ref_stcb ? NULL : stcb, SCTP_LOG_SBRESULT, 0);
				}
				m = SCTP_BUF_NEXT(m);
			}
		}
	}
	SCTP_INP_READ_UNLOCK(old_inp);
	/* Remove the sb-lock on the old socket */

	sbunlock(&old_so->so_rcv);
	/* Now we move them over to the new socket buffer */
	SCTP_INP_READ_LOCK(new_inp);
	TAILQ_FOREACH_SAFE(control, &tmp_queue, next, nctl) {
		TAILQ_INSERT_TAIL(&new_inp->read_queue, control, next);
		m = control->data;
		while (m) {
			if (SCTP_BASE_SYSCTL(sctp_logging_level) & SCTP_SB_LOGGING_ENABLE) {
				sctp_sblog(&new_so->so_rcv, control->do_not_ref_stcb ? NULL : stcb, SCTP_LOG_SBALLOC, SCTP_BUF_LEN(m));
			}
			sctp_sballoc(stcb, &new_so->so_rcv, m);
			if (SCTP_BASE_SYSCTL(sctp_logging_level) & SCTP_SB_LOGGING_ENABLE) {
				sctp_sblog(&new_so->so_rcv, control->do_not_ref_stcb ? NULL : stcb, SCTP_LOG_SBRESULT, 0);
			}
			m = SCTP_BUF_NEXT(m);
		}
	}
	SCTP_INP_READ_UNLOCK(new_inp);
}

void
sctp_wakeup_the_read_socket(struct sctp_inpcb *inp,
    struct sctp_tcb *stcb,
    int so_locked
#if !defined(__APPLE__) && !defined(SCTP_SO_LOCK_TESTING)
    SCTP_UNUSED
#endif
)
{
	if ((inp != NULL) && (inp->sctp_socket != NULL)) {
#if defined(__APPLE__) || defined(SCTP_SO_LOCK_TESTING)
		struct socket *so;

		so = SCTP_INP_SO(inp);
		if (!so_locked) {
			if (stcb) {
				atomic_add_int(&stcb->asoc.refcnt, 1);
				SCTP_TCB_UNLOCK(stcb);
			}
			SCTP_SOCKET_LOCK(so, 1);
			if (stcb) {
				SCTP_TCB_LOCK(stcb);
				atomic_subtract_int(&stcb->asoc.refcnt, 1);
			}
			if (inp->sctp_flags & SCTP_PCB_FLAGS_SOCKET_GONE) {
				SCTP_SOCKET_UNLOCK(so, 1);
				return;
			}
		}
#endif
		sctp_sorwakeup(inp, inp->sctp_socket);
#if defined(__APPLE__) || defined(SCTP_SO_LOCK_TESTING)
		if (!so_locked) {
			SCTP_SOCKET_UNLOCK(so, 1);
		}
#endif
	}
}

void
sctp_add_to_readq(struct sctp_inpcb *inp,
    struct sctp_tcb *stcb,
    struct sctp_queued_to_read *control,
    struct sockbuf *sb,
    int end,
    int inp_read_lock_held,
    int so_locked
#if !defined(__APPLE__) && !defined(SCTP_SO_LOCK_TESTING)
    SCTP_UNUSED
#endif
)
{
	/*
	 * Here we must place the control on the end of the socket read
	 * queue AND increment sb_cc so that select will work properly on
	 * read.
	 */
	struct mbuf *m, *prev = NULL;

	if (inp == NULL) {
		/* Gak, TSNH!! */
#ifdef INVARIANTS
		panic("Gak, inp NULL on add_to_readq");
#endif
		return;
	}
	if (inp_read_lock_held == 0)
		SCTP_INP_READ_LOCK(inp);
	if (inp->sctp_flags & SCTP_PCB_FLAGS_SOCKET_CANT_READ) {
		sctp_free_remote_addr(control->whoFrom);
		if (control->data) {
			sctp_m_freem(control->data);
			control->data = NULL;
		}
		sctp_free_a_readq(stcb, control);
		if (inp_read_lock_held == 0)
			SCTP_INP_READ_UNLOCK(inp);
		return;
	}
	if (!(control->spec_flags & M_NOTIFICATION)) {
		atomic_add_int(&inp->total_recvs, 1);
		if (!control->do_not_ref_stcb) {
			atomic_add_int(&stcb->total_recvs, 1);
		}
	}
	m = control->data;
	control->held_length = 0;
	control->length = 0;
	while (m) {
		if (SCTP_BUF_LEN(m) == 0) {
			/* Skip mbufs with NO length */
			if (prev == NULL) {
				/* First one */
				control->data = sctp_m_free(m);
				m = control->data;
			} else {
				SCTP_BUF_NEXT(prev) = sctp_m_free(m);
				m = SCTP_BUF_NEXT(prev);
			}
			if (m == NULL) {
				control->tail_mbuf = prev;
			}
			continue;
		}
		prev = m;
		if (SCTP_BASE_SYSCTL(sctp_logging_level) & SCTP_SB_LOGGING_ENABLE) {
			sctp_sblog(sb, control->do_not_ref_stcb ? NULL : stcb, SCTP_LOG_SBALLOC, SCTP_BUF_LEN(m));
		}
		sctp_sballoc(stcb, sb, m);
		if (SCTP_BASE_SYSCTL(sctp_logging_level) & SCTP_SB_LOGGING_ENABLE) {
			sctp_sblog(sb, control->do_not_ref_stcb ? NULL : stcb, SCTP_LOG_SBRESULT, 0);
		}
		atomic_add_int(&control->length, SCTP_BUF_LEN(m));
		m = SCTP_BUF_NEXT(m);
	}
	if (prev != NULL) {
		control->tail_mbuf = prev;
	} else {
		/* Everything got collapsed out?? */
		sctp_free_remote_addr(control->whoFrom);
		sctp_free_a_readq(stcb, control);
		if (inp_read_lock_held == 0)
			SCTP_INP_READ_UNLOCK(inp);
		return;
	}
	if (end) {
		control->end_added = 1;
	}
	TAILQ_INSERT_TAIL(&inp->read_queue, control, next);
	control->on_read_q = 1;
	if (inp_read_lock_held == 0)
		SCTP_INP_READ_UNLOCK(inp);
	if (inp && inp->sctp_socket) {
		sctp_wakeup_the_read_socket(inp, stcb, so_locked);
	}
}

/*************HOLD THIS COMMENT FOR PATCH FILE OF
 *************ALTERNATE ROUTING CODE
 */

/*************HOLD THIS COMMENT FOR END OF PATCH FILE OF
 *************ALTERNATE ROUTING CODE
 */

struct mbuf *
sctp_generate_cause(uint16_t code, char *info)
{
	struct mbuf *m;
	struct sctp_gen_error_cause *cause;
	size_t info_len;
	uint16_t len;

	if ((code == 0) || (info == NULL)) {
		return (NULL);
	}
	info_len = strlen(info);
	if (info_len > (SCTP_MAX_CAUSE_LENGTH - sizeof(struct sctp_paramhdr))) {
		return (NULL);
	}
	len = (uint16_t)(sizeof(struct sctp_paramhdr) + info_len);
	m = sctp_get_mbuf_for_msg(len, 0, M_NOWAIT, 1, MT_DATA);
	if (m != NULL) {
		SCTP_BUF_LEN(m) = len;
		cause = mtod(m, struct sctp_gen_error_cause *);
		cause->code = htons(code);
		cause->length = htons(len);
		memcpy(cause->info, info, info_len);
	}
	return (m);
}

struct mbuf *
sctp_generate_no_user_data_cause(uint32_t tsn)
{
	struct mbuf *m;
	struct sctp_error_no_user_data *no_user_data_cause;
	uint16_t len;

	len = (uint16_t)sizeof(struct sctp_error_no_user_data);
	m = sctp_get_mbuf_for_msg(len, 0, M_NOWAIT, 1, MT_DATA);
	if (m != NULL) {
		SCTP_BUF_LEN(m) = len;
		no_user_data_cause = mtod(m, struct sctp_error_no_user_data *);
		no_user_data_cause->cause.code = htons(SCTP_CAUSE_NO_USER_DATA);
		no_user_data_cause->cause.length = htons(len);
		no_user_data_cause->tsn = htonl(tsn);
	}
	return (m);
}

#ifdef SCTP_MBCNT_LOGGING
void
sctp_free_bufspace(struct sctp_tcb *stcb, struct sctp_association *asoc,
    struct sctp_tmit_chunk *tp1, int chk_cnt)
{
	if (tp1->data == NULL) {
		return;
	}
	asoc->chunks_on_out_queue -= chk_cnt;
	if (SCTP_BASE_SYSCTL(sctp_logging_level) & SCTP_MBCNT_LOGGING_ENABLE) {
		sctp_log_mbcnt(SCTP_LOG_MBCNT_DECREASE,
		    asoc->total_output_queue_size,
		    tp1->book_size,
		    0,
		    tp1->mbcnt);
	}
	if (asoc->total_output_queue_size >= tp1->book_size) {
		atomic_add_int(&asoc->total_output_queue_size, -tp1->book_size);
	} else {
		asoc->total_output_queue_size = 0;
	}

	if (stcb->sctp_socket && (((stcb->sctp_ep->sctp_flags & SCTP_PCB_FLAGS_IN_TCPPOOL)) ||
	    ((stcb->sctp_ep->sctp_flags & SCTP_PCB_FLAGS_TCPTYPE)))) {
		if (stcb->sctp_socket->so_snd.sb_cc >= tp1->book_size) {
			stcb->sctp_socket->so_snd.sb_cc -= tp1->book_size;
		} else {
			stcb->sctp_socket->so_snd.sb_cc = 0;

		}
	}
}

#endif

int
sctp_release_pr_sctp_chunk(struct sctp_tcb *stcb, struct sctp_tmit_chunk *tp1,
    uint8_t sent, int so_locked
#if !defined(__APPLE__) && !defined(SCTP_SO_LOCK_TESTING)
    SCTP_UNUSED
#endif
)
{
	struct sctp_stream_out *strq;
	struct sctp_tmit_chunk *chk = NULL, *tp2;
	struct sctp_stream_queue_pending *sp;
	uint32_t mid;
	uint16_t sid;
	uint8_t foundeom = 0;
	int ret_sz = 0;
	int notdone;
	int do_wakeup_routine = 0;

	sid = tp1->rec.data.sid;
	mid = tp1->rec.data.mid;
	if (sent || !(tp1->rec.data.rcv_flags & SCTP_DATA_FIRST_FRAG)) {
		stcb->asoc.abandoned_sent[0]++;
		stcb->asoc.abandoned_sent[PR_SCTP_POLICY(tp1->flags)]++;
		stcb->asoc.strmout[sid].abandoned_sent[0]++;
#if defined(SCTP_DETAILED_STR_STATS)
		stcb->asoc.strmout[sid].abandoned_sent[PR_SCTP_POLICY(tp1->flags)]++;
#endif
	} else {
		stcb->asoc.abandoned_unsent[0]++;
		stcb->asoc.abandoned_unsent[PR_SCTP_POLICY(tp1->flags)]++;
		stcb->asoc.strmout[sid].abandoned_unsent[0]++;
#if defined(SCTP_DETAILED_STR_STATS)
		stcb->asoc.strmout[sid].abandoned_unsent[PR_SCTP_POLICY(tp1->flags)]++;
#endif
	}
	do {
		ret_sz += tp1->book_size;
		if (tp1->data != NULL) {
			if (tp1->sent < SCTP_DATAGRAM_RESEND) {
				sctp_flight_size_decrease(tp1);
				sctp_total_flight_decrease(stcb, tp1);
			}
			sctp_free_bufspace(stcb, &stcb->asoc, tp1, 1);
			stcb->asoc.peers_rwnd += tp1->send_size;
			stcb->asoc.peers_rwnd += SCTP_BASE_SYSCTL(sctp_peer_chunk_oh);
			if (sent) {
				sctp_ulp_notify(SCTP_NOTIFY_SENT_DG_FAIL, stcb, 0, tp1, so_locked);
			} else {
				sctp_ulp_notify(SCTP_NOTIFY_UNSENT_DG_FAIL, stcb, 0, tp1, so_locked);
			}
			if (tp1->data) {
				sctp_m_freem(tp1->data);
				tp1->data = NULL;
			}
			do_wakeup_routine = 1;
			if (PR_SCTP_BUF_ENABLED(tp1->flags)) {
				stcb->asoc.sent_queue_cnt_removeable--;
			}
		}
		tp1->sent = SCTP_FORWARD_TSN_SKIP;
		if ((tp1->rec.data.rcv_flags & SCTP_DATA_NOT_FRAG) ==
		    SCTP_DATA_NOT_FRAG) {
			/* not frag'ed we ae done   */
			notdone = 0;
			foundeom = 1;
		} else if (tp1->rec.data.rcv_flags & SCTP_DATA_LAST_FRAG) {
			/* end of frag, we are done */
			notdone = 0;
			foundeom = 1;
		} else {
			/*
			 * Its a begin or middle piece, we must mark all of
			 * it
			 */
			notdone = 1;
			tp1 = TAILQ_NEXT(tp1, sctp_next);
		}
	} while (tp1 && notdone);
	if (foundeom == 0) {
		/*
		 * The multi-part message was scattered across the send and
		 * sent queue.
		 */
		TAILQ_FOREACH_SAFE(tp1, &stcb->asoc.send_queue, sctp_next, tp2) {
			if ((tp1->rec.data.sid != sid) ||
			    (!SCTP_MID_EQ(stcb->asoc.idata_supported, tp1->rec.data.mid, mid))) {
				break;
			}
			/*
			 * save to chk in case we have some on stream out
			 * queue. If so and we have an un-transmitted one we
			 * don't have to fudge the TSN.
			 */
			chk = tp1;
			ret_sz += tp1->book_size;
			sctp_free_bufspace(stcb, &stcb->asoc, tp1, 1);
			if (sent) {
				sctp_ulp_notify(SCTP_NOTIFY_SENT_DG_FAIL, stcb, 0, tp1, so_locked);
			} else {
				sctp_ulp_notify(SCTP_NOTIFY_UNSENT_DG_FAIL, stcb, 0, tp1, so_locked);
			}
			if (tp1->data) {
				sctp_m_freem(tp1->data);
				tp1->data = NULL;
			}
			/* No flight involved here book the size to 0 */
			tp1->book_size = 0;
			if (tp1->rec.data.rcv_flags & SCTP_DATA_LAST_FRAG) {
				foundeom = 1;
			}
			do_wakeup_routine = 1;
			tp1->sent = SCTP_FORWARD_TSN_SKIP;
			TAILQ_REMOVE(&stcb->asoc.send_queue, tp1, sctp_next);
			/*
			 * on to the sent queue so we can wait for it to be
			 * passed by.
			 */
			TAILQ_INSERT_TAIL(&stcb->asoc.sent_queue, tp1,
			    sctp_next);
			stcb->asoc.send_queue_cnt--;
			stcb->asoc.sent_queue_cnt++;
		}
	}
	if (foundeom == 0) {
		/*
		 * Still no eom found. That means there is stuff left on the
		 * stream out queue.. yuck.
		 */
		SCTP_TCB_SEND_LOCK(stcb);
		strq = &stcb->asoc.strmout[sid];
		sp = TAILQ_FIRST(&strq->outqueue);
		if (sp != NULL) {
			sp->discard_rest = 1;
			/*
			 * We may need to put a chunk on the queue that
			 * holds the TSN that would have been sent with the
			 * LAST bit.
			 */
			if (chk == NULL) {
				/* Yep, we have to */
				sctp_alloc_a_chunk(stcb, chk);
				if (chk == NULL) {
					/*
					 * we are hosed. All we can do is
					 * nothing.. which will cause an
					 * abort if the peer is paying
					 * attention.
					 */
					goto oh_well;
				}
				memset(chk, 0, sizeof(*chk));
				chk->rec.data.rcv_flags = 0;
				chk->sent = SCTP_FORWARD_TSN_SKIP;
				chk->asoc = &stcb->asoc;
				if (stcb->asoc.idata_supported == 0) {
					if (sp->sinfo_flags & SCTP_UNORDERED) {
						chk->rec.data.mid = 0;
					} else {
						chk->rec.data.mid = strq->next_mid_ordered;
					}
				} else {
					if (sp->sinfo_flags & SCTP_UNORDERED) {
						chk->rec.data.mid = strq->next_mid_unordered;
					} else {
						chk->rec.data.mid = strq->next_mid_ordered;
					}
				}
				chk->rec.data.sid = sp->sid;
				chk->rec.data.ppid = sp->ppid;
				chk->rec.data.context = sp->context;
				chk->flags = sp->act_flags;
				chk->whoTo = NULL;
				chk->rec.data.tsn = atomic_fetchadd_int(&stcb->asoc.sending_seq, 1);
				strq->chunks_on_queues++;
				TAILQ_INSERT_TAIL(&stcb->asoc.sent_queue, chk, sctp_next);
				stcb->asoc.sent_queue_cnt++;
				stcb->asoc.pr_sctp_cnt++;
			}
			chk->rec.data.rcv_flags |= SCTP_DATA_LAST_FRAG;
			if (sp->sinfo_flags & SCTP_UNORDERED) {
				chk->rec.data.rcv_flags |= SCTP_DATA_UNORDERED;
			}
			if (stcb->asoc.idata_supported == 0) {
				if ((sp->sinfo_flags & SCTP_UNORDERED) == 0) {
					strq->next_mid_ordered++;
				}
			} else {
				if (sp->sinfo_flags & SCTP_UNORDERED) {
					strq->next_mid_unordered++;
				} else {
					strq->next_mid_ordered++;
				}
			}
	oh_well:
			if (sp->data) {
				/*
				 * Pull any data to free up the SB and allow
				 * sender to "add more" while we will throw
				 * away :-)
				 */
				sctp_free_spbufspace(stcb, &stcb->asoc, sp);
				ret_sz += sp->length;
				do_wakeup_routine = 1;
				sp->some_taken = 1;
				sctp_m_freem(sp->data);
				sp->data = NULL;
				sp->tail_mbuf = NULL;
				sp->length = 0;
			}
		}
		SCTP_TCB_SEND_UNLOCK(stcb);
	}
	if (do_wakeup_routine) {
#if defined(__APPLE__) || defined(SCTP_SO_LOCK_TESTING)
		struct socket *so;

		so = SCTP_INP_SO(stcb->sctp_ep);
		if (!so_locked) {
			atomic_add_int(&stcb->asoc.refcnt, 1);
			SCTP_TCB_UNLOCK(stcb);
			SCTP_SOCKET_LOCK(so, 1);
			SCTP_TCB_LOCK(stcb);
			atomic_subtract_int(&stcb->asoc.refcnt, 1);
			if (stcb->asoc.state & SCTP_STATE_CLOSED_SOCKET) {
				/* assoc was freed while we were unlocked */
				SCTP_SOCKET_UNLOCK(so, 1);
				return (ret_sz);
			}
		}
#endif
		sctp_sowwakeup(stcb->sctp_ep, stcb->sctp_socket);
#if defined(__APPLE__) || defined(SCTP_SO_LOCK_TESTING)
		if (!so_locked) {
			SCTP_SOCKET_UNLOCK(so, 1);
		}
#endif
	}
	return (ret_sz);
}

/*
 * checks to see if the given address, sa, is one that is currently known by
 * the kernel note: can't distinguish the same address on multiple interfaces
 * and doesn't handle multiple addresses with different zone/scope id's note:
 * ifa_ifwithaddr() compares the entire sockaddr struct
 */
struct sctp_ifa *
sctp_find_ifa_in_ep(struct sctp_inpcb *inp, struct sockaddr *addr,
    int holds_lock)
{
	struct sctp_laddr *laddr;

	if (holds_lock == 0) {
		SCTP_INP_RLOCK(inp);
	}

	LIST_FOREACH(laddr, &inp->sctp_addr_list, sctp_nxt_addr) {
		if (laddr->ifa == NULL)
			continue;
		if (addr->sa_family != laddr->ifa->address.sa.sa_family)
			continue;
#ifdef INET
		if (addr->sa_family == AF_INET) {
			if (((struct sockaddr_in *)addr)->sin_addr.s_addr ==
			    laddr->ifa->address.sin.sin_addr.s_addr) {
				/* found him. */
				if (holds_lock == 0) {
					SCTP_INP_RUNLOCK(inp);
				}
				return (laddr->ifa);
				break;
			}
		}
#endif
#ifdef INET6
		if (addr->sa_family == AF_INET6) {
			if (SCTP6_ARE_ADDR_EQUAL((struct sockaddr_in6 *)addr,
			    &laddr->ifa->address.sin6)) {
				/* found him. */
				if (holds_lock == 0) {
					SCTP_INP_RUNLOCK(inp);
				}
				return (laddr->ifa);
				break;
			}
		}
#endif
	}
	if (holds_lock == 0) {
		SCTP_INP_RUNLOCK(inp);
	}
	return (NULL);
}

uint32_t
sctp_get_ifa_hash_val(struct sockaddr *addr)
{
	switch (addr->sa_family) {
#ifdef INET
	case AF_INET:
		{
			struct sockaddr_in *sin;

			sin = (struct sockaddr_in *)addr;
			return (sin->sin_addr.s_addr ^ (sin->sin_addr.s_addr >> 16));
		}
#endif
#ifdef INET6
	case AF_INET6:
		{
			struct sockaddr_in6 *sin6;
			uint32_t hash_of_addr;

			sin6 = (struct sockaddr_in6 *)addr;
			hash_of_addr = (sin6->sin6_addr.s6_addr32[0] +
			    sin6->sin6_addr.s6_addr32[1] +
			    sin6->sin6_addr.s6_addr32[2] +
			    sin6->sin6_addr.s6_addr32[3]);
			hash_of_addr = (hash_of_addr ^ (hash_of_addr >> 16));
			return (hash_of_addr);
		}
#endif
	default:
		break;
	}
	return (0);
}

struct sctp_ifa *
sctp_find_ifa_by_addr(struct sockaddr *addr, uint32_t vrf_id, int holds_lock)
{
	struct sctp_ifa *sctp_ifap;
	struct sctp_vrf *vrf;
	struct sctp_ifalist *hash_head;
	uint32_t hash_of_addr;

	if (holds_lock == 0)
		SCTP_IPI_ADDR_RLOCK();

	vrf = sctp_find_vrf(vrf_id);
	if (vrf == NULL) {
		if (holds_lock == 0)
			SCTP_IPI_ADDR_RUNLOCK();
		return (NULL);
	}

	hash_of_addr = sctp_get_ifa_hash_val(addr);

	hash_head = &vrf->vrf_addr_hash[(hash_of_addr & vrf->vrf_addr_hashmark)];
	if (hash_head == NULL) {
		SCTP_PRINTF("hash_of_addr:%x mask:%x table:%x - ",
		    hash_of_addr, (uint32_t)vrf->vrf_addr_hashmark,
		    (uint32_t)(hash_of_addr & vrf->vrf_addr_hashmark));
		sctp_print_address(addr);
		SCTP_PRINTF("No such bucket for address\n");
		if (holds_lock == 0)
			SCTP_IPI_ADDR_RUNLOCK();

		return (NULL);
	}
	LIST_FOREACH(sctp_ifap, hash_head, next_bucket) {
		if (addr->sa_family != sctp_ifap->address.sa.sa_family)
			continue;
#ifdef INET
		if (addr->sa_family == AF_INET) {
			if (((struct sockaddr_in *)addr)->sin_addr.s_addr ==
			    sctp_ifap->address.sin.sin_addr.s_addr) {
				/* found him. */
				if (holds_lock == 0)
					SCTP_IPI_ADDR_RUNLOCK();
				return (sctp_ifap);
				break;
			}
		}
#endif
#ifdef INET6
		if (addr->sa_family == AF_INET6) {
			if (SCTP6_ARE_ADDR_EQUAL((struct sockaddr_in6 *)addr,
			    &sctp_ifap->address.sin6)) {
				/* found him. */
				if (holds_lock == 0)
					SCTP_IPI_ADDR_RUNLOCK();
				return (sctp_ifap);
				break;
			}
		}
#endif
	}
	if (holds_lock == 0)
		SCTP_IPI_ADDR_RUNLOCK();
	return (NULL);
}

static void
sctp_user_rcvd(struct sctp_tcb *stcb, uint32_t *freed_so_far, int hold_rlock,
    uint32_t rwnd_req)
{
	/* User pulled some data, do we need a rwnd update? */
	int r_unlocked = 0;
	uint32_t dif, rwnd;
	struct socket *so = NULL;

	if (stcb == NULL)
		return;

	atomic_add_int(&stcb->asoc.refcnt, 1);

	if ((SCTP_GET_STATE(stcb) == SCTP_STATE_SHUTDOWN_ACK_SENT) ||
	    (stcb->asoc.state & (SCTP_STATE_ABOUT_TO_BE_FREED | SCTP_STATE_SHUTDOWN_RECEIVED))) {
		/* Pre-check If we are freeing no update */
		goto no_lock;
	}
	SCTP_INP_INCR_REF(stcb->sctp_ep);
	if ((stcb->sctp_ep->sctp_flags & SCTP_PCB_FLAGS_SOCKET_GONE) ||
	    (stcb->sctp_ep->sctp_flags & SCTP_PCB_FLAGS_SOCKET_ALLGONE)) {
		goto out;
	}
	so = stcb->sctp_socket;
	if (so == NULL) {
		goto out;
	}
	atomic_add_int(&stcb->freed_by_sorcv_sincelast, *freed_so_far);
	/* Have you have freed enough to look */
	*freed_so_far = 0;
	/* Yep, its worth a look and the lock overhead */

	/* Figure out what the rwnd would be */
	rwnd = sctp_calc_rwnd(stcb, &stcb->asoc);
	if (rwnd >= stcb->asoc.my_last_reported_rwnd) {
		dif = rwnd - stcb->asoc.my_last_reported_rwnd;
	} else {
		dif = 0;
	}
	if (dif >= rwnd_req) {
		if (hold_rlock) {
			SCTP_INP_READ_UNLOCK(stcb->sctp_ep);
			r_unlocked = 1;
		}
		if (stcb->asoc.state & SCTP_STATE_ABOUT_TO_BE_FREED) {
			/*
			 * One last check before we allow the guy possibly
			 * to get in. There is a race, where the guy has not
			 * reached the gate. In that case
			 */
			goto out;
		}
		SCTP_TCB_LOCK(stcb);
		if (stcb->asoc.state & SCTP_STATE_ABOUT_TO_BE_FREED) {
			/* No reports here */
			SCTP_TCB_UNLOCK(stcb);
			goto out;
		}
		SCTP_STAT_INCR(sctps_wu_sacks_sent);
		sctp_send_sack(stcb, SCTP_SO_LOCKED);

		sctp_chunk_output(stcb->sctp_ep, stcb,
		    SCTP_OUTPUT_FROM_USR_RCVD, SCTP_SO_LOCKED);
		/* make sure no timer is running */
		sctp_timer_stop(SCTP_TIMER_TYPE_RECV, stcb->sctp_ep, stcb, NULL,
		    SCTP_FROM_SCTPUTIL + SCTP_LOC_6);
		SCTP_TCB_UNLOCK(stcb);
	} else {
		/* Update how much we have pending */
		stcb->freed_by_sorcv_sincelast = dif;
	}
out:
	if (so && r_unlocked && hold_rlock) {
		SCTP_INP_READ_LOCK(stcb->sctp_ep);
	}

	SCTP_INP_DECR_REF(stcb->sctp_ep);
no_lock:
	atomic_add_int(&stcb->asoc.refcnt, -1);
	return;
}

int
sctp_sorecvmsg(struct socket *so,
    struct uio *uio,
    struct mbuf **mp,
    struct sockaddr *from,
    int fromlen,
    int *msg_flags,
    struct sctp_sndrcvinfo *sinfo,
    int filling_sinfo)
{
	/*
	 * MSG flags we will look at MSG_DONTWAIT - non-blocking IO.
	 * MSG_PEEK - Look don't touch :-D (only valid with OUT mbuf copy
	 * mp=NULL thus uio is the copy method to userland) MSG_WAITALL - ??
	 * On the way out we may send out any combination of:
	 * MSG_NOTIFICATION MSG_EOR
	 *
	 */
	struct sctp_inpcb *inp = NULL;
	ssize_t my_len = 0;
	ssize_t cp_len = 0;
	int error = 0;
	struct sctp_queued_to_read *control = NULL, *ctl = NULL, *nxt = NULL;
	struct mbuf *m = NULL;
	struct sctp_tcb *stcb = NULL;
	int wakeup_read_socket = 0;
	int freecnt_applied = 0;
	int out_flags = 0, in_flags = 0;
	int block_allowed = 1;
	uint32_t freed_so_far = 0;
	ssize_t copied_so_far = 0;
	int in_eeor_mode = 0;
	int no_rcv_needed = 0;
	uint32_t rwnd_req = 0;
	int hold_sblock = 0;
	int hold_rlock = 0;
	ssize_t slen = 0;
	uint32_t held_length = 0;
	int sockbuf_lock = 0;

	if (uio == NULL) {
		SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTPUTIL, EINVAL);
		return (EINVAL);
	}

	if (msg_flags) {
		in_flags = *msg_flags;
		if (in_flags & MSG_PEEK)
			SCTP_STAT_INCR(sctps_read_peeks);
	} else {
		in_flags = 0;
	}
	slen = uio->uio_resid;

	/* Pull in and set up our int flags */
	if (in_flags & MSG_OOB) {
		/* Out of band's NOT supported */
		return (EOPNOTSUPP);
	}
	if ((in_flags & MSG_PEEK) && (mp != NULL)) {
		SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTPUTIL, EINVAL);
		return (EINVAL);
	}
	if ((in_flags & (MSG_DONTWAIT
	    | MSG_NBIO
	    )) ||
	    SCTP_SO_IS_NBIO(so)) {
		block_allowed = 0;
	}
	/* setup the endpoint */
	inp = (struct sctp_inpcb *)so->so_pcb;
	if (inp == NULL) {
		SCTP_LTRACE_ERR_RET(NULL, NULL, NULL, SCTP_FROM_SCTPUTIL, EFAULT);
		return (EFAULT);
	}
	rwnd_req = (SCTP_SB_LIMIT_RCV(so) >> SCTP_RWND_HIWAT_SHIFT);
	/* Must be at least a MTU's worth */
	if (rwnd_req < SCTP_MIN_RWND)
		rwnd_req = SCTP_MIN_RWND;
	in_eeor_mode = sctp_is_feature_on(inp, SCTP_PCB_FLAGS_EXPLICIT_EOR);
	if (SCTP_BASE_SYSCTL(sctp_logging_level) & SCTP_RECV_RWND_LOGGING_ENABLE) {
		sctp_misc_ints(SCTP_SORECV_ENTER,
		    rwnd_req, in_eeor_mode, so->so_rcv.sb_cc, (uint32_t)uio->uio_resid);
	}
	if (SCTP_BASE_SYSCTL(sctp_logging_level) & SCTP_RECV_RWND_LOGGING_ENABLE) {
		sctp_misc_ints(SCTP_SORECV_ENTERPL,
		    rwnd_req, block_allowed, so->so_rcv.sb_cc, (uint32_t)uio->uio_resid);
	}


	error = sblock(&so->so_rcv, (block_allowed ? SBL_WAIT : 0));
	if (error) {
		goto release_unlocked;
	}
	sockbuf_lock = 1;
restart:


restart_nosblocks:
	if (hold_sblock == 0) {
		SOCKBUF_LOCK(&so->so_rcv);
		hold_sblock = 1;
	}
	if ((inp->sctp_flags & SCTP_PCB_FLAGS_SOCKET_GONE) ||
	    (inp->sctp_flags & SCTP_PCB_FLAGS_SOCKET_ALLGONE)) {
		goto out;
	}
	if ((so->so_rcv.sb_state & SBS_CANTRCVMORE) && (so->so_rcv.sb_cc == 0)) {
		if (so->so_error) {
			error = so->so_error;
			if ((in_flags & MSG_PEEK) == 0)
				so->so_error = 0;
			goto out;
		} else {
			if (so->so_rcv.sb_cc == 0) {
				/* indicate EOF */
				error = 0;
				goto out;
			}
		}
	}
	if (so->so_rcv.sb_cc <= held_length) {
		if (so->so_error) {
			error = so->so_error;
			if ((in_flags & MSG_PEEK) == 0) {
				so->so_error = 0;
			}
			goto out;
		}
		if ((so->so_rcv.sb_cc == 0) &&
		    ((inp->sctp_flags & SCTP_PCB_FLAGS_TCPTYPE) ||
		    (inp->sctp_flags & SCTP_PCB_FLAGS_IN_TCPPOOL))) {
			if ((inp->sctp_flags & SCTP_PCB_FLAGS_CONNECTED) == 0) {
				/*
				 * For active open side clear flags for
				 * re-use passive open is blocked by
				 * connect.
				 */
				if (inp->sctp_flags & SCTP_PCB_FLAGS_WAS_ABORTED) {
					/*
					 * You were aborted, passive side
					 * always hits here
					 */
					SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTPUTIL, ECONNRESET);
					error = ECONNRESET;
				}
				so->so_state &= ~(SS_ISCONNECTING |
				    SS_ISDISCONNECTING |
				    SS_ISCONFIRMING |
				    SS_ISCONNECTED);
				if (error == 0) {
					if ((inp->sctp_flags & SCTP_PCB_FLAGS_WAS_CONNECTED) == 0) {
						SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTPUTIL, ENOTCONN);
						error = ENOTCONN;
					}
				}
				goto out;
			}
		}
		if (block_allowed) {
			error = sbwait(&so->so_rcv);
			if (error) {
				goto out;
			}
			held_length = 0;
			goto restart_nosblocks;
		} else {
			SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTPUTIL, EWOULDBLOCK);
			error = EWOULDBLOCK;
			goto out;
		}
	}
	if (hold_sblock == 1) {
		SOCKBUF_UNLOCK(&so->so_rcv);
		hold_sblock = 0;
	}
	/* we possibly have data we can read */
	/* sa_ignore FREED_MEMORY */
	control = TAILQ_FIRST(&inp->read_queue);
	if (control == NULL) {
		/*
		 * This could be happening since the appender did the
		 * increment but as not yet did the tailq insert onto the
		 * read_queue
		 */
		if (hold_rlock == 0) {
			SCTP_INP_READ_LOCK(inp);
		}
		control = TAILQ_FIRST(&inp->read_queue);
		if ((control == NULL) && (so->so_rcv.sb_cc != 0)) {
#ifdef INVARIANTS
			panic("Huh, its non zero and nothing on control?");
#endif
			so->so_rcv.sb_cc = 0;
		}
		SCTP_INP_READ_UNLOCK(inp);
		hold_rlock = 0;
		goto restart;
	}

	if ((control->length == 0) &&
	    (control->do_not_ref_stcb)) {
		/*
		 * Clean up code for freeing assoc that left behind a
		 * pdapi.. maybe a peer in EEOR that just closed after
		 * sending and never indicated a EOR.
		 */
		if (hold_rlock == 0) {
			hold_rlock = 1;
			SCTP_INP_READ_LOCK(inp);
		}
		control->held_length = 0;
		if (control->data) {
			/* Hmm there is data here .. fix */
			struct mbuf *m_tmp;
			int cnt = 0;

			m_tmp = control->data;
			while (m_tmp) {
				cnt += SCTP_BUF_LEN(m_tmp);
				if (SCTP_BUF_NEXT(m_tmp) == NULL) {
					control->tail_mbuf = m_tmp;
					control->end_added = 1;
				}
				m_tmp = SCTP_BUF_NEXT(m_tmp);
			}
			control->length = cnt;
		} else {
			/* remove it */
			TAILQ_REMOVE(&inp->read_queue, control, next);
			/* Add back any hiddend data */
			sctp_free_remote_addr(control->whoFrom);
			sctp_free_a_readq(stcb, control);
		}
		if (hold_rlock) {
			hold_rlock = 0;
			SCTP_INP_READ_UNLOCK(inp);
		}
		goto restart;
	}
	if ((control->length == 0) &&
	    (control->end_added == 1)) {
		/*
		 * Do we also need to check for (control->pdapi_aborted ==
		 * 1)?
		 */
		if (hold_rlock == 0) {
			hold_rlock = 1;
			SCTP_INP_READ_LOCK(inp);
		}
		TAILQ_REMOVE(&inp->read_queue, control, next);
		if (control->data) {
#ifdef INVARIANTS
			panic("control->data not null but control->length == 0");
#else
			SCTP_PRINTF("Strange, data left in the control buffer. Cleaning up.\n");
			sctp_m_freem(control->data);
			control->data = NULL;
#endif
		}
		if (control->aux_data) {
			sctp_m_free(control->aux_data);
			control->aux_data = NULL;
		}
#ifdef INVARIANTS
		if (control->on_strm_q) {
			panic("About to free ctl:%p so:%p and its in %d",
			    control, so, control->on_strm_q);
		}
#endif
		sctp_free_remote_addr(control->whoFrom);
		sctp_free_a_readq(stcb, control);
		if (hold_rlock) {
			hold_rlock = 0;
			SCTP_INP_READ_UNLOCK(inp);
		}
		goto restart;
	}
	if (control->length == 0) {
		if ((sctp_is_feature_on(inp, SCTP_PCB_FLAGS_FRAG_INTERLEAVE)) &&
		    (filling_sinfo)) {
			/* find a more suitable one then this */
			ctl = TAILQ_NEXT(control, next);
			while (ctl) {
				if ((ctl->stcb != control->stcb) && (ctl->length) &&
				    (ctl->some_taken ||
				    (ctl->spec_flags & M_NOTIFICATION) ||
				    ((ctl->do_not_ref_stcb == 0) &&
				    (ctl->stcb->asoc.strmin[ctl->sinfo_stream].delivery_started == 0)))
				    ) {
					/*-
					 * If we have a different TCB next, and there is data
					 * present. If we have already taken some (pdapi), OR we can
					 * ref the tcb and no delivery as started on this stream, we
					 * take it. Note we allow a notification on a different
					 * assoc to be delivered..
					 */
					control = ctl;
					goto found_one;
				} else if ((sctp_is_feature_on(inp, SCTP_PCB_FLAGS_INTERLEAVE_STRMS)) &&
					    (ctl->length) &&
					    ((ctl->some_taken) ||
					    ((ctl->do_not_ref_stcb == 0) &&
					    ((ctl->spec_flags & M_NOTIFICATION) == 0) &&
				    (ctl->stcb->asoc.strmin[ctl->sinfo_stream].delivery_started == 0)))) {
					/*-
					 * If we have the same tcb, and there is data present, and we
					 * have the strm interleave feature present. Then if we have
					 * taken some (pdapi) or we can refer to tht tcb AND we have
					 * not started a delivery for this stream, we can take it.
					 * Note we do NOT allow a notificaiton on the same assoc to
					 * be delivered.
					 */
					control = ctl;
					goto found_one;
				}
				ctl = TAILQ_NEXT(ctl, next);
			}
		}
		/*
		 * if we reach here, not suitable replacement is available
		 * <or> fragment interleave is NOT on. So stuff the sb_cc
		 * into the our held count, and its time to sleep again.
		 */
		held_length = so->so_rcv.sb_cc;
		control->held_length = so->so_rcv.sb_cc;
		goto restart;
	}
	/* Clear the held length since there is something to read */
	control->held_length = 0;
found_one:
	/*
	 * If we reach here, control has a some data for us to read off.
	 * Note that stcb COULD be NULL.
	 */
	if (hold_rlock == 0) {
		hold_rlock = 1;
		SCTP_INP_READ_LOCK(inp);
	}
	control->some_taken++;
	stcb = control->stcb;
	if (stcb) {
		if ((control->do_not_ref_stcb == 0) &&
		    (stcb->asoc.state & SCTP_STATE_ABOUT_TO_BE_FREED)) {
			if (freecnt_applied == 0)
				stcb = NULL;
		} else if (control->do_not_ref_stcb == 0) {
			/* you can't free it on me please */
			/*
			 * The lock on the socket buffer protects us so the
			 * free code will stop. But since we used the
			 * socketbuf lock and the sender uses the tcb_lock
			 * to increment, we need to use the atomic add to
			 * the refcnt
			 */
			if (freecnt_applied) {
#ifdef INVARIANTS
				panic("refcnt already incremented");
#else
				SCTP_PRINTF("refcnt already incremented?\n");
#endif
			} else {
				atomic_add_int(&stcb->asoc.refcnt, 1);
				freecnt_applied = 1;
			}
			/*
			 * Setup to remember how much we have not yet told
			 * the peer our rwnd has opened up. Note we grab the
			 * value from the tcb from last time. Note too that
			 * sack sending clears this when a sack is sent,
			 * which is fine. Once we hit the rwnd_req, we then
			 * will go to the sctp_user_rcvd() that will not
			 * lock until it KNOWs it MUST send a WUP-SACK.
			 */
			freed_so_far = (uint32_t)stcb->freed_by_sorcv_sincelast;
			stcb->freed_by_sorcv_sincelast = 0;
		}
	}
	if (stcb &&
	    ((control->spec_flags & M_NOTIFICATION) == 0) &&
	    control->do_not_ref_stcb == 0) {
		stcb->asoc.strmin[control->sinfo_stream].delivery_started = 1;
	}

	/* First lets get off the sinfo and sockaddr info */
	if ((sinfo != NULL) && (filling_sinfo != 0)) {
		sinfo->sinfo_stream = control->sinfo_stream;
		sinfo->sinfo_ssn = (uint16_t)control->mid;
		sinfo->sinfo_flags = control->sinfo_flags;
		sinfo->sinfo_ppid = control->sinfo_ppid;
		sinfo->sinfo_context = control->sinfo_context;
		sinfo->sinfo_timetolive = control->sinfo_timetolive;
		sinfo->sinfo_tsn = control->sinfo_tsn;
		sinfo->sinfo_cumtsn = control->sinfo_cumtsn;
		sinfo->sinfo_assoc_id = control->sinfo_assoc_id;
		nxt = TAILQ_NEXT(control, next);
		if (sctp_is_feature_on(inp, SCTP_PCB_FLAGS_EXT_RCVINFO) ||
		    sctp_is_feature_on(inp, SCTP_PCB_FLAGS_RECVNXTINFO)) {
			struct sctp_extrcvinfo *s_extra;

			s_extra = (struct sctp_extrcvinfo *)sinfo;
			if ((nxt) &&
			    (nxt->length)) {
				s_extra->serinfo_next_flags = SCTP_NEXT_MSG_AVAIL;
				if (nxt->sinfo_flags & SCTP_UNORDERED) {
					s_extra->serinfo_next_flags |= SCTP_NEXT_MSG_IS_UNORDERED;
				}
				if (nxt->spec_flags & M_NOTIFICATION) {
					s_extra->serinfo_next_flags |= SCTP_NEXT_MSG_IS_NOTIFICATION;
				}
				s_extra->serinfo_next_aid = nxt->sinfo_assoc_id;
				s_extra->serinfo_next_length = nxt->length;
				s_extra->serinfo_next_ppid = nxt->sinfo_ppid;
				s_extra->serinfo_next_stream = nxt->sinfo_stream;
				if (nxt->tail_mbuf != NULL) {
					if (nxt->end_added) {
						s_extra->serinfo_next_flags |= SCTP_NEXT_MSG_ISCOMPLETE;
					}
				}
			} else {
				/*
				 * we explicitly 0 this, since the memcpy
				 * got some other things beyond the older
				 * sinfo_ that is on the control's structure
				 * :-D
				 */
				nxt = NULL;
				s_extra->serinfo_next_flags = SCTP_NO_NEXT_MSG;
				s_extra->serinfo_next_aid = 0;
				s_extra->serinfo_next_length = 0;
				s_extra->serinfo_next_ppid = 0;
				s_extra->serinfo_next_stream = 0;
			}
		}
		/*
		 * update off the real current cum-ack, if we have an stcb.
		 */
		if ((control->do_not_ref_stcb == 0) && stcb)
			sinfo->sinfo_cumtsn = stcb->asoc.cumulative_tsn;
		/*
		 * mask off the high bits, we keep the actual chunk bits in
		 * there.
		 */
		sinfo->sinfo_flags &= 0x00ff;
		if ((control->sinfo_flags >> 8) & SCTP_DATA_UNORDERED) {
			sinfo->sinfo_flags |= SCTP_UNORDERED;
		}
	}
#ifdef SCTP_ASOCLOG_OF_TSNS
	{
		int index, newindex;
		struct sctp_pcbtsn_rlog *entry;

		do {
			index = inp->readlog_index;
			newindex = index + 1;
			if (newindex >= SCTP_READ_LOG_SIZE) {
				newindex = 0;
			}
		} while (atomic_cmpset_int(&inp->readlog_index, index, newindex) == 0);
		entry = &inp->readlog[index];
		entry->vtag = control->sinfo_assoc_id;
		entry->strm = control->sinfo_stream;
		entry->seq = (uint16_t)control->mid;
		entry->sz = control->length;
		entry->flgs = control->sinfo_flags;
	}
#endif
	if ((fromlen > 0) && (from != NULL)) {
		union sctp_sockstore store;
		size_t len;

		switch (control->whoFrom->ro._l_addr.sa.sa_family) {
#ifdef INET6
		case AF_INET6:
			len = sizeof(struct sockaddr_in6);
			store.sin6 = control->whoFrom->ro._l_addr.sin6;
			store.sin6.sin6_port = control->port_from;
			break;
#endif
#ifdef INET
		case AF_INET:
#ifdef INET6
			if (sctp_is_feature_on(inp, SCTP_PCB_FLAGS_NEEDS_MAPPED_V4)) {
				len = sizeof(struct sockaddr_in6);
				in6_sin_2_v4mapsin6(&control->whoFrom->ro._l_addr.sin,
				    &store.sin6);
				store.sin6.sin6_port = control->port_from;
			} else {
				len = sizeof(struct sockaddr_in);
				store.sin = control->whoFrom->ro._l_addr.sin;
				store.sin.sin_port = control->port_from;
			}
#else
			len = sizeof(struct sockaddr_in);
			store.sin = control->whoFrom->ro._l_addr.sin;
			store.sin.sin_port = control->port_from;
#endif
			break;
#endif
		default:
			len = 0;
			break;
		}
		memcpy(from, &store, min((size_t)fromlen, len));
#ifdef INET6
		{
			struct sockaddr_in6 lsa6, *from6;

			from6 = (struct sockaddr_in6 *)from;
			sctp_recover_scope_mac(from6, (&lsa6));
		}
#endif
	}
	if (hold_rlock) {
		SCTP_INP_READ_UNLOCK(inp);
		hold_rlock = 0;
	}
	if (hold_sblock) {
		SOCKBUF_UNLOCK(&so->so_rcv);
		hold_sblock = 0;
	}
	/* now copy out what data we can */
	if (mp == NULL) {
		/* copy out each mbuf in the chain up to length */
get_more_data:
		m = control->data;
		while (m) {
			/* Move out all we can */
			cp_len = uio->uio_resid;
			my_len = SCTP_BUF_LEN(m);
			if (cp_len > my_len) {
				/* not enough in this buf */
				cp_len = my_len;
			}
			if (hold_rlock) {
				SCTP_INP_READ_UNLOCK(inp);
				hold_rlock = 0;
			}
			if (cp_len > 0)
				error = uiomove(mtod(m, char *), (int)cp_len, uio);
			/* re-read */
			if (inp->sctp_flags & SCTP_PCB_FLAGS_SOCKET_GONE) {
				goto release;
			}

			if ((control->do_not_ref_stcb == 0) && stcb &&
			    stcb->asoc.state & SCTP_STATE_ABOUT_TO_BE_FREED) {
				no_rcv_needed = 1;
			}
			if (error) {
				/* error we are out of here */
				goto release;
			}
			SCTP_INP_READ_LOCK(inp);
			hold_rlock = 1;
			if (cp_len == SCTP_BUF_LEN(m)) {
				if ((SCTP_BUF_NEXT(m) == NULL) &&
				    (control->end_added)) {
					out_flags |= MSG_EOR;
					if ((control->do_not_ref_stcb == 0) &&
					    (control->stcb != NULL) &&
					    ((control->spec_flags & M_NOTIFICATION) == 0))
						control->stcb->asoc.strmin[control->sinfo_stream].delivery_started = 0;
				}
				if (control->spec_flags & M_NOTIFICATION) {
					out_flags |= MSG_NOTIFICATION;
				}
				/* we ate up the mbuf */
				if (in_flags & MSG_PEEK) {
					/* just looking */
					m = SCTP_BUF_NEXT(m);
					copied_so_far += cp_len;
				} else {
					/* dispose of the mbuf */
					if (SCTP_BASE_SYSCTL(sctp_logging_level) & SCTP_SB_LOGGING_ENABLE) {
						sctp_sblog(&so->so_rcv,
						    control->do_not_ref_stcb ? NULL : stcb, SCTP_LOG_SBFREE, SCTP_BUF_LEN(m));
					}
					sctp_sbfree(control, stcb, &so->so_rcv, m);
					if (SCTP_BASE_SYSCTL(sctp_logging_level) & SCTP_SB_LOGGING_ENABLE) {
						sctp_sblog(&so->so_rcv,
						    control->do_not_ref_stcb ? NULL : stcb, SCTP_LOG_SBRESULT, 0);
					}
					copied_so_far += cp_len;
					freed_so_far += (uint32_t)cp_len;
					freed_so_far += MSIZE;
					atomic_subtract_int(&control->length, cp_len);
					control->data = sctp_m_free(m);
					m = control->data;
					/*
					 * been through it all, must hold sb
					 * lock ok to null tail
					 */
					if (control->data == NULL) {
#ifdef INVARIANTS
						if ((control->end_added == 0) ||
						    (TAILQ_NEXT(control, next) == NULL)) {
							/*
							 * If the end is not
							 * added, OR the
							 * next is NOT null
							 * we MUST have the
							 * lock.
							 */
							if (mtx_owned(&inp->inp_rdata_mtx) == 0) {
								panic("Hmm we don't own the lock?");
							}
						}
#endif
						control->tail_mbuf = NULL;
#ifdef INVARIANTS
						if ((control->end_added) && ((out_flags & MSG_EOR) == 0)) {
							panic("end_added, nothing left and no MSG_EOR");
						}
#endif
					}
				}
			} else {
				/* Do we need to trim the mbuf? */
				if (control->spec_flags & M_NOTIFICATION) {
					out_flags |= MSG_NOTIFICATION;
				}
				if ((in_flags & MSG_PEEK) == 0) {
					SCTP_BUF_RESV_UF(m, cp_len);
					SCTP_BUF_LEN(m) -= (int)cp_len;
					if (SCTP_BASE_SYSCTL(sctp_logging_level) & SCTP_SB_LOGGING_ENABLE) {
						sctp_sblog(&so->so_rcv, control->do_not_ref_stcb ? NULL : stcb, SCTP_LOG_SBFREE, (int)cp_len);
					}
					atomic_subtract_int(&so->so_rcv.sb_cc, cp_len);
					if ((control->do_not_ref_stcb == 0) &&
					    stcb) {
						atomic_subtract_int(&stcb->asoc.sb_cc, cp_len);
					}
					copied_so_far += cp_len;
					freed_so_far += (uint32_t)cp_len;
					freed_so_far += MSIZE;
					if (SCTP_BASE_SYSCTL(sctp_logging_level) & SCTP_SB_LOGGING_ENABLE) {
						sctp_sblog(&so->so_rcv, control->do_not_ref_stcb ? NULL : stcb,
						    SCTP_LOG_SBRESULT, 0);
					}
					atomic_subtract_int(&control->length, cp_len);
				} else {
					copied_so_far += cp_len;
				}
			}
			if ((out_flags & MSG_EOR) || (uio->uio_resid == 0)) {
				break;
			}
			if (((stcb) && (in_flags & MSG_PEEK) == 0) &&
			    (control->do_not_ref_stcb == 0) &&
			    (freed_so_far >= rwnd_req)) {
				sctp_user_rcvd(stcb, &freed_so_far, hold_rlock, rwnd_req);
			}
		}		/* end while(m) */
		/*
		 * At this point we have looked at it all and we either have
		 * a MSG_EOR/or read all the user wants... <OR>
		 * control->length == 0.
		 */
		if ((out_flags & MSG_EOR) && ((in_flags & MSG_PEEK) == 0)) {
			/* we are done with this control */
			if (control->length == 0) {
				if (control->data) {
#ifdef INVARIANTS
					panic("control->data not null at read eor?");
#else
					SCTP_PRINTF("Strange, data left in the control buffer .. invarients would panic?\n");
					sctp_m_freem(control->data);
					control->data = NULL;
#endif
				}
		done_with_control:
				if (hold_rlock == 0) {
					SCTP_INP_READ_LOCK(inp);
					hold_rlock = 1;
				}
				TAILQ_REMOVE(&inp->read_queue, control, next);
				/* Add back any hiddend data */
				if (control->held_length) {
					held_length = 0;
					control->held_length = 0;
					wakeup_read_socket = 1;
				}
				if (control->aux_data) {
					sctp_m_free(control->aux_data);
					control->aux_data = NULL;
				}
				no_rcv_needed = control->do_not_ref_stcb;
				sctp_free_remote_addr(control->whoFrom);
				control->data = NULL;
#ifdef INVARIANTS
				if (control->on_strm_q) {
					panic("About to free ctl:%p so:%p and its in %d",
					    control, so, control->on_strm_q);
				}
#endif
				sctp_free_a_readq(stcb, control);
				control = NULL;
				if ((freed_so_far >= rwnd_req) &&
				    (no_rcv_needed == 0))
					sctp_user_rcvd(stcb, &freed_so_far, hold_rlock, rwnd_req);

			} else {
				/*
				 * The user did not read all of this
				 * message, turn off the returned MSG_EOR
				 * since we are leaving more behind on the
				 * control to read.
				 */
#ifdef INVARIANTS
				if (control->end_added &&
				    (control->data == NULL) &&
				    (control->tail_mbuf == NULL)) {
					panic("Gak, control->length is corrupt?");
				}
#endif
				no_rcv_needed = control->do_not_ref_stcb;
				out_flags &= ~MSG_EOR;
			}
		}
		if (out_flags & MSG_EOR) {
			goto release;
		}
		if ((uio->uio_resid == 0) ||
		    ((in_eeor_mode) &&
		    (copied_so_far >= (uint32_t)max(so->so_rcv.sb_lowat, 1)))) {
			goto release;
		}
		/*
		 * If I hit here the receiver wants more and this message is
		 * NOT done (pd-api). So two questions. Can we block? if not
		 * we are done. Did the user NOT set MSG_WAITALL?
		 */
		if (block_allowed == 0) {
			goto release;
		}
		/*
		 * We need to wait for more data a few things: - We don't
		 * sbunlock() so we don't get someone else reading. - We
		 * must be sure to account for the case where what is added
		 * is NOT to our control when we wakeup.
		 */

		/*
		 * Do we need to tell the transport a rwnd update might be
		 * needed before we go to sleep?
		 */
		if (((stcb) && (in_flags & MSG_PEEK) == 0) &&
		    ((freed_so_far >= rwnd_req) &&
		    (control->do_not_ref_stcb == 0) &&
		    (no_rcv_needed == 0))) {
			sctp_user_rcvd(stcb, &freed_so_far, hold_rlock, rwnd_req);
		}
wait_some_more:
		if (so->so_rcv.sb_state & SBS_CANTRCVMORE) {
			goto release;
		}

		if (inp->sctp_flags & SCTP_PCB_FLAGS_SOCKET_GONE)
			goto release;

		if (hold_rlock == 1) {
			SCTP_INP_READ_UNLOCK(inp);
			hold_rlock = 0;
		}
		if (hold_sblock == 0) {
			SOCKBUF_LOCK(&so->so_rcv);
			hold_sblock = 1;
		}
		if ((copied_so_far) && (control->length == 0) &&
		    (sctp_is_feature_on(inp, SCTP_PCB_FLAGS_FRAG_INTERLEAVE))) {
			goto release;
		}
		if (so->so_rcv.sb_cc <= control->held_length) {
			error = sbwait(&so->so_rcv);
			if (error) {
				goto release;
			}
			control->held_length = 0;
		}
		if (hold_sblock) {
			SOCKBUF_UNLOCK(&so->so_rcv);
			hold_sblock = 0;
		}
		if (control->length == 0) {
			/* still nothing here */
			if (control->end_added == 1) {
				/* he aborted, or is done i.e.did a shutdown */
				out_flags |= MSG_EOR;
				if (control->pdapi_aborted) {
					if ((control->do_not_ref_stcb == 0) && ((control->spec_flags & M_NOTIFICATION) == 0))
						control->stcb->asoc.strmin[control->sinfo_stream].delivery_started = 0;

					out_flags |= MSG_TRUNC;
				} else {
					if ((control->do_not_ref_stcb == 0) && ((control->spec_flags & M_NOTIFICATION) == 0))
						control->stcb->asoc.strmin[control->sinfo_stream].delivery_started = 0;
				}
				goto done_with_control;
			}
			if (so->so_rcv.sb_cc > held_length) {
				control->held_length = so->so_rcv.sb_cc;
				held_length = 0;
			}
			goto wait_some_more;
		} else if (control->data == NULL) {
			/*
			 * we must re-sync since data is probably being
			 * added
			 */
			SCTP_INP_READ_LOCK(inp);
			if ((control->length > 0) && (control->data == NULL)) {
				/*
				 * big trouble.. we have the lock and its
				 * corrupt?
				 */
#ifdef INVARIANTS
				panic("Impossible data==NULL length !=0");
#endif
				out_flags |= MSG_EOR;
				out_flags |= MSG_TRUNC;
				control->length = 0;
				SCTP_INP_READ_UNLOCK(inp);
				goto done_with_control;
			}
			SCTP_INP_READ_UNLOCK(inp);
			/* We will fall around to get more data */
		}
		goto get_more_data;
	} else {
		/*-
		 * Give caller back the mbuf chain,
		 * store in uio_resid the length
		 */
		wakeup_read_socket = 0;
		if ((control->end_added == 0) ||
		    (TAILQ_NEXT(control, next) == NULL)) {
			/* Need to get rlock */
			if (hold_rlock == 0) {
				SCTP_INP_READ_LOCK(inp);
				hold_rlock = 1;
			}
		}
		if (control->end_added) {
			out_flags |= MSG_EOR;
			if ((control->do_not_ref_stcb == 0) &&
			    (control->stcb != NULL) &&
			    ((control->spec_flags & M_NOTIFICATION) == 0))
				control->stcb->asoc.strmin[control->sinfo_stream].delivery_started = 0;
		}
		if (control->spec_flags & M_NOTIFICATION) {
			out_flags |= MSG_NOTIFICATION;
		}
		uio->uio_resid = control->length;
		*mp = control->data;
		m = control->data;
		while (m) {
			if (SCTP_BASE_SYSCTL(sctp_logging_level) & SCTP_SB_LOGGING_ENABLE) {
				sctp_sblog(&so->so_rcv,
				    control->do_not_ref_stcb ? NULL : stcb, SCTP_LOG_SBFREE, SCTP_BUF_LEN(m));
			}
			sctp_sbfree(control, stcb, &so->so_rcv, m);
			freed_so_far += (uint32_t)SCTP_BUF_LEN(m);
			freed_so_far += MSIZE;
			if (SCTP_BASE_SYSCTL(sctp_logging_level) & SCTP_SB_LOGGING_ENABLE) {
				sctp_sblog(&so->so_rcv,
				    control->do_not_ref_stcb ? NULL : stcb, SCTP_LOG_SBRESULT, 0);
			}
			m = SCTP_BUF_NEXT(m);
		}
		control->data = control->tail_mbuf = NULL;
		control->length = 0;
		if (out_flags & MSG_EOR) {
			/* Done with this control */
			goto done_with_control;
		}
	}
release:
	if (hold_rlock == 1) {
		SCTP_INP_READ_UNLOCK(inp);
		hold_rlock = 0;
	}
	if (hold_sblock == 1) {
		SOCKBUF_UNLOCK(&so->so_rcv);
		hold_sblock = 0;
	}

	sbunlock(&so->so_rcv);
	sockbuf_lock = 0;

release_unlocked:
	if (hold_sblock) {
		SOCKBUF_UNLOCK(&so->so_rcv);
		hold_sblock = 0;
	}
	if ((stcb) && (in_flags & MSG_PEEK) == 0) {
		if ((freed_so_far >= rwnd_req) &&
		    (control && (control->do_not_ref_stcb == 0)) &&
		    (no_rcv_needed == 0))
			sctp_user_rcvd(stcb, &freed_so_far, hold_rlock, rwnd_req);
	}
out:
	if (msg_flags) {
		*msg_flags = out_flags;
	}
	if (((out_flags & MSG_EOR) == 0) &&
	    ((in_flags & MSG_PEEK) == 0) &&
	    (sinfo) &&
	    (sctp_is_feature_on(inp, SCTP_PCB_FLAGS_EXT_RCVINFO) ||
	    sctp_is_feature_on(inp, SCTP_PCB_FLAGS_RECVNXTINFO))) {
		struct sctp_extrcvinfo *s_extra;

		s_extra = (struct sctp_extrcvinfo *)sinfo;
		s_extra->serinfo_next_flags = SCTP_NO_NEXT_MSG;
	}
	if (hold_rlock == 1) {
		SCTP_INP_READ_UNLOCK(inp);
	}
	if (hold_sblock) {
		SOCKBUF_UNLOCK(&so->so_rcv);
	}
	if (sockbuf_lock) {
		sbunlock(&so->so_rcv);
	}

	if (freecnt_applied) {
		/*
		 * The lock on the socket buffer protects us so the free
		 * code will stop. But since we used the socketbuf lock and
		 * the sender uses the tcb_lock to increment, we need to use
		 * the atomic add to the refcnt.
		 */
		if (stcb == NULL) {
#ifdef INVARIANTS
			panic("stcb for refcnt has gone NULL?");
			goto stage_left;
#else
			goto stage_left;
#endif
		}
		/* Save the value back for next time */
		stcb->freed_by_sorcv_sincelast = freed_so_far;
		atomic_add_int(&stcb->asoc.refcnt, -1);
	}
	if (SCTP_BASE_SYSCTL(sctp_logging_level) & SCTP_RECV_RWND_LOGGING_ENABLE) {
		if (stcb) {
			sctp_misc_ints(SCTP_SORECV_DONE,
			    freed_so_far,
			    (uint32_t)((uio) ? (slen - uio->uio_resid) : slen),
			    stcb->asoc.my_rwnd,
			    so->so_rcv.sb_cc);
		} else {
			sctp_misc_ints(SCTP_SORECV_DONE,
			    freed_so_far,
			    (uint32_t)((uio) ? (slen - uio->uio_resid) : slen),
			    0,
			    so->so_rcv.sb_cc);
		}
	}
stage_left:
	if (wakeup_read_socket) {
		sctp_sorwakeup(inp, so);
	}
	return (error);
}


#ifdef SCTP_MBUF_LOGGING
struct mbuf *
sctp_m_free(struct mbuf *m)
{
	if (SCTP_BASE_SYSCTL(sctp_logging_level) & SCTP_MBUF_LOGGING_ENABLE) {
		sctp_log_mb(m, SCTP_MBUF_IFREE);
	}
	return (m_free(m));
}

void
sctp_m_freem(struct mbuf *mb)
{
	while (mb != NULL)
		mb = sctp_m_free(mb);
}

#endif

int
sctp_dynamic_set_primary(struct sockaddr *sa, uint32_t vrf_id)
{
	/*
	 * Given a local address. For all associations that holds the
	 * address, request a peer-set-primary.
	 */
	struct sctp_ifa *ifa;
	struct sctp_laddr *wi;

	ifa = sctp_find_ifa_by_addr(sa, vrf_id, 0);
	if (ifa == NULL) {
		SCTP_LTRACE_ERR_RET(NULL, NULL, NULL, SCTP_FROM_SCTPUTIL, EADDRNOTAVAIL);
		return (EADDRNOTAVAIL);
	}
	/*
	 * Now that we have the ifa we must awaken the iterator with this
	 * message.
	 */
	wi = SCTP_ZONE_GET(SCTP_BASE_INFO(ipi_zone_laddr), struct sctp_laddr);
	if (wi == NULL) {
		SCTP_LTRACE_ERR_RET(NULL, NULL, NULL, SCTP_FROM_SCTPUTIL, ENOMEM);
		return (ENOMEM);
	}
	/* Now incr the count and int wi structure */
	SCTP_INCR_LADDR_COUNT();
	memset(wi, 0, sizeof(*wi));
	(void)SCTP_GETTIME_TIMEVAL(&wi->start_time);
	wi->ifa = ifa;
	wi->action = SCTP_SET_PRIM_ADDR;
	atomic_add_int(&ifa->refcount, 1);

	/* Now add it to the work queue */
	SCTP_WQ_ADDR_LOCK();
	/*
	 * Should this really be a tailq? As it is we will process the
	 * newest first :-0
	 */
	LIST_INSERT_HEAD(&SCTP_BASE_INFO(addr_wq), wi, sctp_nxt_addr);
	sctp_timer_start(SCTP_TIMER_TYPE_ADDR_WQ,
	    (struct sctp_inpcb *)NULL,
	    (struct sctp_tcb *)NULL,
	    (struct sctp_nets *)NULL);
	SCTP_WQ_ADDR_UNLOCK();
	return (0);
}


int
sctp_soreceive(struct socket *so,
    struct sockaddr **psa,
    struct uio *uio,
    struct mbuf **mp0,
    struct mbuf **controlp,
    int *flagsp)
{
	int error, fromlen;
	uint8_t sockbuf[256];
	struct sockaddr *from;
	struct sctp_extrcvinfo sinfo;
	int filling_sinfo = 1;
	int flags;
	struct sctp_inpcb *inp;

	inp = (struct sctp_inpcb *)so->so_pcb;
	/* pickup the assoc we are reading from */
	if (inp == NULL) {
		SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTPUTIL, EINVAL);
		return (EINVAL);
	}
	if ((sctp_is_feature_off(inp, SCTP_PCB_FLAGS_RECVDATAIOEVNT) &&
	    sctp_is_feature_off(inp, SCTP_PCB_FLAGS_RECVRCVINFO) &&
	    sctp_is_feature_off(inp, SCTP_PCB_FLAGS_RECVNXTINFO)) ||
	    (controlp == NULL)) {
		/* user does not want the sndrcv ctl */
		filling_sinfo = 0;
	}
	if (psa) {
		from = (struct sockaddr *)sockbuf;
		fromlen = sizeof(sockbuf);
		from->sa_len = 0;
	} else {
		from = NULL;
		fromlen = 0;
	}

	if (filling_sinfo) {
		memset(&sinfo, 0, sizeof(struct sctp_extrcvinfo));
	}
	if (flagsp != NULL) {
		flags = *flagsp;
	} else {
		flags = 0;
	}
	error = sctp_sorecvmsg(so, uio, mp0, from, fromlen, &flags,
	    (struct sctp_sndrcvinfo *)&sinfo, filling_sinfo);
	if (flagsp != NULL) {
		*flagsp = flags;
	}
	if (controlp != NULL) {
		/* copy back the sinfo in a CMSG format */
		if (filling_sinfo && ((flags & MSG_NOTIFICATION) == 0)) {
			*controlp = sctp_build_ctl_nchunk(inp,
			    (struct sctp_sndrcvinfo *)&sinfo);
		} else {
			*controlp = NULL;
		}
	}
	if (psa) {
		/* copy back the address info */
		if (from && from->sa_len) {
			*psa = sodupsockaddr(from, M_NOWAIT);
		} else {
			*psa = NULL;
		}
	}
	return (error);
}





int
sctp_connectx_helper_add(struct sctp_tcb *stcb, struct sockaddr *addr,
    int totaddr, int *error)
{
	int added = 0;
	int i;
	struct sctp_inpcb *inp;
	struct sockaddr *sa;
	size_t incr = 0;
#ifdef INET
	struct sockaddr_in *sin;
#endif
#ifdef INET6
	struct sockaddr_in6 *sin6;
#endif

	sa = addr;
	inp = stcb->sctp_ep;
	*error = 0;
	for (i = 0; i < totaddr; i++) {
		switch (sa->sa_family) {
#ifdef INET
		case AF_INET:
			incr = sizeof(struct sockaddr_in);
			sin = (struct sockaddr_in *)sa;
			if ((sin->sin_addr.s_addr == INADDR_ANY) ||
			    (sin->sin_addr.s_addr == INADDR_BROADCAST) ||
			    IN_MULTICAST(ntohl(sin->sin_addr.s_addr))) {
				SCTP_LTRACE_ERR_RET(NULL, stcb, NULL, SCTP_FROM_SCTPUTIL, EINVAL);
				(void)sctp_free_assoc(inp, stcb, SCTP_NORMAL_PROC,
				    SCTP_FROM_SCTPUTIL + SCTP_LOC_7);
				*error = EINVAL;
				goto out_now;
			}
			if (sctp_add_remote_addr(stcb, sa, NULL, stcb->asoc.port,
			    SCTP_DONOT_SETSCOPE,
			    SCTP_ADDR_IS_CONFIRMED)) {
				/* assoc gone no un-lock */
				SCTP_LTRACE_ERR_RET(NULL, stcb, NULL, SCTP_FROM_SCTPUTIL, ENOBUFS);
				(void)sctp_free_assoc(inp, stcb, SCTP_NORMAL_PROC,
				    SCTP_FROM_SCTPUTIL + SCTP_LOC_8);
				*error = ENOBUFS;
				goto out_now;
			}
			added++;
			break;
#endif
#ifdef INET6
		case AF_INET6:
			incr = sizeof(struct sockaddr_in6);
			sin6 = (struct sockaddr_in6 *)sa;
			if (IN6_IS_ADDR_UNSPECIFIED(&sin6->sin6_addr) ||
			    IN6_IS_ADDR_MULTICAST(&sin6->sin6_addr)) {
				SCTP_LTRACE_ERR_RET(NULL, stcb, NULL, SCTP_FROM_SCTPUTIL, EINVAL);
				(void)sctp_free_assoc(inp, stcb, SCTP_NORMAL_PROC,
				    SCTP_FROM_SCTPUTIL + SCTP_LOC_9);
				*error = EINVAL;
				goto out_now;
			}
			if (sctp_add_remote_addr(stcb, sa, NULL, stcb->asoc.port,
			    SCTP_DONOT_SETSCOPE,
			    SCTP_ADDR_IS_CONFIRMED)) {
				/* assoc gone no un-lock */
				SCTP_LTRACE_ERR_RET(NULL, stcb, NULL, SCTP_FROM_SCTPUTIL, ENOBUFS);
				(void)sctp_free_assoc(inp, stcb, SCTP_NORMAL_PROC,
				    SCTP_FROM_SCTPUTIL + SCTP_LOC_10);
				*error = ENOBUFS;
				goto out_now;
			}
			added++;
			break;
#endif
		default:
			break;
		}
		sa = (struct sockaddr *)((caddr_t)sa + incr);
	}
out_now:
	return (added);
}

struct sctp_tcb *
sctp_connectx_helper_find(struct sctp_inpcb *inp, struct sockaddr *addr,
    unsigned int *totaddr,
    unsigned int *num_v4, unsigned int *num_v6, int *error,
    unsigned int limit, int *bad_addr)
{
	struct sockaddr *sa;
	struct sctp_tcb *stcb = NULL;
	unsigned int incr, at, i;

	at = 0;
	sa = addr;
	*error = *num_v6 = *num_v4 = 0;
	/* account and validate addresses */
	for (i = 0; i < *totaddr; i++) {
		switch (sa->sa_family) {
#ifdef INET
		case AF_INET:
			incr = (unsigned int)sizeof(struct sockaddr_in);
			if (sa->sa_len != incr) {
				SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTPUTIL, EINVAL);
				*error = EINVAL;
				*bad_addr = 1;
				return (NULL);
			}
			(*num_v4) += 1;
			break;
#endif
#ifdef INET6
		case AF_INET6:
			{
				struct sockaddr_in6 *sin6;

				sin6 = (struct sockaddr_in6 *)sa;
				if (IN6_IS_ADDR_V4MAPPED(&sin6->sin6_addr)) {
					/* Must be non-mapped for connectx */
					SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTPUTIL, EINVAL);
					*error = EINVAL;
					*bad_addr = 1;
					return (NULL);
				}
				incr = (unsigned int)sizeof(struct sockaddr_in6);
				if (sa->sa_len != incr) {
					SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTPUTIL, EINVAL);
					*error = EINVAL;
					*bad_addr = 1;
					return (NULL);
				}
				(*num_v6) += 1;
				break;
			}
#endif
		default:
			*totaddr = i;
			incr = 0;
			/* we are done */
			break;
		}
		if (i == *totaddr) {
			break;
		}
		SCTP_INP_INCR_REF(inp);
		stcb = sctp_findassociation_ep_addr(&inp, sa, NULL, NULL, NULL);
		if (stcb != NULL) {
			/* Already have or am bring up an association */
			return (stcb);
		} else {
			SCTP_INP_DECR_REF(inp);
		}
		if ((at + incr) > limit) {
			*totaddr = i;
			break;
		}
		sa = (struct sockaddr *)((caddr_t)sa + incr);
	}
	return ((struct sctp_tcb *)NULL);
}

/*
 * sctp_bindx(ADD) for one address.
 * assumes all arguments are valid/checked by caller.
 */
void
sctp_bindx_add_address(struct socket *so, struct sctp_inpcb *inp,
    struct sockaddr *sa, sctp_assoc_t assoc_id,
    uint32_t vrf_id, int *error, void *p)
{
	struct sockaddr *addr_touse;
#if defined(INET) && defined(INET6)
	struct sockaddr_in sin;
#endif

	/* see if we're bound all already! */
	if (inp->sctp_flags & SCTP_PCB_FLAGS_BOUNDALL) {
		SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTPUTIL, EINVAL);
		*error = EINVAL;
		return;
	}
	addr_touse = sa;
#ifdef INET6
	if (sa->sa_family == AF_INET6) {
#ifdef INET
		struct sockaddr_in6 *sin6;

#endif
		if (sa->sa_len != sizeof(struct sockaddr_in6)) {
			SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTPUTIL, EINVAL);
			*error = EINVAL;
			return;
		}
		if ((inp->sctp_flags & SCTP_PCB_FLAGS_BOUND_V6) == 0) {
			/* can only bind v6 on PF_INET6 sockets */
			SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTPUTIL, EINVAL);
			*error = EINVAL;
			return;
		}
#ifdef INET
		sin6 = (struct sockaddr_in6 *)addr_touse;
		if (IN6_IS_ADDR_V4MAPPED(&sin6->sin6_addr)) {
			if ((inp->sctp_flags & SCTP_PCB_FLAGS_BOUND_V6) &&
			    SCTP_IPV6_V6ONLY(inp)) {
				/* can't bind v4-mapped on PF_INET sockets */
				SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTPUTIL, EINVAL);
				*error = EINVAL;
				return;
			}
			in6_sin6_2_sin(&sin, sin6);
			addr_touse = (struct sockaddr *)&sin;
		}
#endif
	}
#endif
#ifdef INET
	if (sa->sa_family == AF_INET) {
		if (sa->sa_len != sizeof(struct sockaddr_in)) {
			SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTPUTIL, EINVAL);
			*error = EINVAL;
			return;
		}
		if ((inp->sctp_flags & SCTP_PCB_FLAGS_BOUND_V6) &&
		    SCTP_IPV6_V6ONLY(inp)) {
			/* can't bind v4 on PF_INET sockets */
			SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTPUTIL, EINVAL);
			*error = EINVAL;
			return;
		}
	}
#endif
	if (inp->sctp_flags & SCTP_PCB_FLAGS_UNBOUND) {
		if (p == NULL) {
			/* Can't get proc for Net/Open BSD */
			SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTPUTIL, EINVAL);
			*error = EINVAL;
			return;
		}
		*error = sctp_inpcb_bind(so, addr_touse, NULL, p);
		return;
	}
	/*
	 * No locks required here since bind and mgmt_ep_sa all do their own
	 * locking. If we do something for the FIX: below we may need to
	 * lock in that case.
	 */
	if (assoc_id == 0) {
		/* add the address */
		struct sctp_inpcb *lep;
		struct sockaddr_in *lsin = (struct sockaddr_in *)addr_touse;

		/* validate the incoming port */
		if ((lsin->sin_port != 0) &&
		    (lsin->sin_port != inp->sctp_lport)) {
			SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTPUTIL, EINVAL);
			*error = EINVAL;
			return;
		} else {
			/* user specified 0 port, set it to existing port */
			lsin->sin_port = inp->sctp_lport;
		}

		lep = sctp_pcb_findep(addr_touse, 1, 0, vrf_id);
		if (lep != NULL) {
			/*
			 * We must decrement the refcount since we have the
			 * ep already and are binding. No remove going on
			 * here.
			 */
			SCTP_INP_DECR_REF(lep);
		}
		if (lep == inp) {
			/* already bound to it.. ok */
			return;
		} else if (lep == NULL) {
			((struct sockaddr_in *)addr_touse)->sin_port = 0;
			*error = sctp_addr_mgmt_ep_sa(inp, addr_touse,
			    SCTP_ADD_IP_ADDRESS,
			    vrf_id, NULL);
		} else {
			*error = EADDRINUSE;
		}
		if (*error)
			return;
	} else {
		/*
		 * FIX: decide whether we allow assoc based bindx
		 */
	}
}

/*
 * sctp_bindx(DELETE) for one address.
 * assumes all arguments are valid/checked by caller.
 */
void
sctp_bindx_delete_address(struct sctp_inpcb *inp,
    struct sockaddr *sa, sctp_assoc_t assoc_id,
    uint32_t vrf_id, int *error)
{
	struct sockaddr *addr_touse;
#if defined(INET) && defined(INET6)
	struct sockaddr_in sin;
#endif

	/* see if we're bound all already! */
	if (inp->sctp_flags & SCTP_PCB_FLAGS_BOUNDALL) {
		SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTPUTIL, EINVAL);
		*error = EINVAL;
		return;
	}
	addr_touse = sa;
#ifdef INET6
	if (sa->sa_family == AF_INET6) {
#ifdef INET
		struct sockaddr_in6 *sin6;
#endif

		if (sa->sa_len != sizeof(struct sockaddr_in6)) {
			SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTPUTIL, EINVAL);
			*error = EINVAL;
			return;
		}
		if ((inp->sctp_flags & SCTP_PCB_FLAGS_BOUND_V6) == 0) {
			/* can only bind v6 on PF_INET6 sockets */
			SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTPUTIL, EINVAL);
			*error = EINVAL;
			return;
		}
#ifdef INET
		sin6 = (struct sockaddr_in6 *)addr_touse;
		if (IN6_IS_ADDR_V4MAPPED(&sin6->sin6_addr)) {
			if ((inp->sctp_flags & SCTP_PCB_FLAGS_BOUND_V6) &&
			    SCTP_IPV6_V6ONLY(inp)) {
				/* can't bind mapped-v4 on PF_INET sockets */
				SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTPUTIL, EINVAL);
				*error = EINVAL;
				return;
			}
			in6_sin6_2_sin(&sin, sin6);
			addr_touse = (struct sockaddr *)&sin;
		}
#endif
	}
#endif
#ifdef INET
	if (sa->sa_family == AF_INET) {
		if (sa->sa_len != sizeof(struct sockaddr_in)) {
			SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTPUTIL, EINVAL);
			*error = EINVAL;
			return;
		}
		if ((inp->sctp_flags & SCTP_PCB_FLAGS_BOUND_V6) &&
		    SCTP_IPV6_V6ONLY(inp)) {
			/* can't bind v4 on PF_INET sockets */
			SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTPUTIL, EINVAL);
			*error = EINVAL;
			return;
		}
	}
#endif
	/*
	 * No lock required mgmt_ep_sa does its own locking. If the FIX:
	 * below is ever changed we may need to lock before calling
	 * association level binding.
	 */
	if (assoc_id == 0) {
		/* delete the address */
		*error = sctp_addr_mgmt_ep_sa(inp, addr_touse,
		    SCTP_DEL_IP_ADDRESS,
		    vrf_id, NULL);
	} else {
		/*
		 * FIX: decide whether we allow assoc based bindx
		 */
	}
}

/*
 * returns the valid local address count for an assoc, taking into account
 * all scoping rules
 */
int
sctp_local_addr_count(struct sctp_tcb *stcb)
{
	int loopback_scope;
#if defined(INET)
	int ipv4_local_scope, ipv4_addr_legal;
#endif
#if defined (INET6)
	int local_scope, site_scope, ipv6_addr_legal;
#endif
	struct sctp_vrf *vrf;
	struct sctp_ifn *sctp_ifn;
	struct sctp_ifa *sctp_ifa;
	int count = 0;

	/* Turn on all the appropriate scopes */
	loopback_scope = stcb->asoc.scope.loopback_scope;
#if defined(INET)
	ipv4_local_scope = stcb->asoc.scope.ipv4_local_scope;
	ipv4_addr_legal = stcb->asoc.scope.ipv4_addr_legal;
#endif
#if defined(INET6)
	local_scope = stcb->asoc.scope.local_scope;
	site_scope = stcb->asoc.scope.site_scope;
	ipv6_addr_legal = stcb->asoc.scope.ipv6_addr_legal;
#endif
	SCTP_IPI_ADDR_RLOCK();
	vrf = sctp_find_vrf(stcb->asoc.vrf_id);
	if (vrf == NULL) {
		/* no vrf, no addresses */
		SCTP_IPI_ADDR_RUNLOCK();
		return (0);
	}

	if (stcb->sctp_ep->sctp_flags & SCTP_PCB_FLAGS_BOUNDALL) {
		/*
		 * bound all case: go through all ifns on the vrf
		 */
		LIST_FOREACH(sctp_ifn, &vrf->ifnlist, next_ifn) {
			if ((loopback_scope == 0) &&
			    SCTP_IFN_IS_IFT_LOOP(sctp_ifn)) {
				continue;
			}
			LIST_FOREACH(sctp_ifa, &sctp_ifn->ifalist, next_ifa) {
				if (sctp_is_addr_restricted(stcb, sctp_ifa))
					continue;
				switch (sctp_ifa->address.sa.sa_family) {
#ifdef INET
				case AF_INET:
					if (ipv4_addr_legal) {
						struct sockaddr_in *sin;

						sin = &sctp_ifa->address.sin;
						if (sin->sin_addr.s_addr == 0) {
							/*
							 * skip unspecified
							 * addrs
							 */
							continue;
						}
						if (prison_check_ip4(stcb->sctp_ep->ip_inp.inp.inp_cred,
						    &sin->sin_addr) != 0) {
							continue;
						}
						if ((ipv4_local_scope == 0) &&
						    (IN4_ISPRIVATE_ADDRESS(&sin->sin_addr))) {
							continue;
						}
						/* count this one */
						count++;
					} else {
						continue;
					}
					break;
#endif
#ifdef INET6
				case AF_INET6:
					if (ipv6_addr_legal) {
						struct sockaddr_in6 *sin6;

						sin6 = &sctp_ifa->address.sin6;
						if (IN6_IS_ADDR_UNSPECIFIED(&sin6->sin6_addr)) {
							continue;
						}
						if (prison_check_ip6(stcb->sctp_ep->ip_inp.inp.inp_cred,
						    &sin6->sin6_addr) != 0) {
							continue;
						}
						if (IN6_IS_ADDR_LINKLOCAL(&sin6->sin6_addr)) {
							if (local_scope == 0)
								continue;
							if (sin6->sin6_scope_id == 0) {
								if (sa6_recoverscope(sin6) != 0)
									/*
									 *
									 * bad
									 * link
									 *
									 * local
									 *
									 * address
									 */
									continue;
							}
						}
						if ((site_scope == 0) &&
						    (IN6_IS_ADDR_SITELOCAL(&sin6->sin6_addr))) {
							continue;
						}
						/* count this one */
						count++;
					}
					break;
#endif
				default:
					/* TSNH */
					break;
				}
			}
		}
	} else {
		/*
		 * subset bound case
		 */
		struct sctp_laddr *laddr;

		LIST_FOREACH(laddr, &stcb->sctp_ep->sctp_addr_list,
		    sctp_nxt_addr) {
			if (sctp_is_addr_restricted(stcb, laddr->ifa)) {
				continue;
			}
			/* count this one */
			count++;
		}
	}
	SCTP_IPI_ADDR_RUNLOCK();
	return (count);
}

#if defined(SCTP_LOCAL_TRACE_BUF)

void
sctp_log_trace(uint32_t subsys, const char *str SCTP_UNUSED, uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e, uint32_t f)
{
	uint32_t saveindex, newindex;

	do {
		saveindex = SCTP_BASE_SYSCTL(sctp_log).index;
		if (saveindex >= SCTP_MAX_LOGGING_SIZE) {
			newindex = 1;
		} else {
			newindex = saveindex + 1;
		}
	} while (atomic_cmpset_int(&SCTP_BASE_SYSCTL(sctp_log).index, saveindex, newindex) == 0);
	if (saveindex >= SCTP_MAX_LOGGING_SIZE) {
		saveindex = 0;
	}
	SCTP_BASE_SYSCTL(sctp_log).entry[saveindex].timestamp = SCTP_GET_CYCLECOUNT;
	SCTP_BASE_SYSCTL(sctp_log).entry[saveindex].subsys = subsys;
	SCTP_BASE_SYSCTL(sctp_log).entry[saveindex].params[0] = a;
	SCTP_BASE_SYSCTL(sctp_log).entry[saveindex].params[1] = b;
	SCTP_BASE_SYSCTL(sctp_log).entry[saveindex].params[2] = c;
	SCTP_BASE_SYSCTL(sctp_log).entry[saveindex].params[3] = d;
	SCTP_BASE_SYSCTL(sctp_log).entry[saveindex].params[4] = e;
	SCTP_BASE_SYSCTL(sctp_log).entry[saveindex].params[5] = f;
}

#endif
static void
sctp_recv_udp_tunneled_packet(struct mbuf *m, int off, struct inpcb *inp,
    const struct sockaddr *sa SCTP_UNUSED, void *ctx SCTP_UNUSED)
{
	struct ip *iph;
#ifdef INET6
	struct ip6_hdr *ip6;
#endif
	struct mbuf *sp, *last;
	struct udphdr *uhdr;
	uint16_t port;

	if ((m->m_flags & M_PKTHDR) == 0) {
		/* Can't handle one that is not a pkt hdr */
		goto out;
	}
	/* Pull the src port */
	iph = mtod(m, struct ip *);
	uhdr = (struct udphdr *)((caddr_t)iph + off);
	port = uhdr->uh_sport;
	/*
	 * Split out the mbuf chain. Leave the IP header in m, place the
	 * rest in the sp.
	 */
	sp = m_split(m, off, M_NOWAIT);
	if (sp == NULL) {
		/* Gak, drop packet, we can't do a split */
		goto out;
	}
	if (sp->m_pkthdr.len < sizeof(struct udphdr) + sizeof(struct sctphdr)) {
		/* Gak, packet can't have an SCTP header in it - too small */
		m_freem(sp);
		goto out;
	}
	/* Now pull up the UDP header and SCTP header together */
	sp = m_pullup(sp, sizeof(struct udphdr) + sizeof(struct sctphdr));
	if (sp == NULL) {
		/* Gak pullup failed */
		goto out;
	}
	/* Trim out the UDP header */
	m_adj(sp, sizeof(struct udphdr));

	/* Now reconstruct the mbuf chain */
	for (last = m; last->m_next; last = last->m_next);
	last->m_next = sp;
	m->m_pkthdr.len += sp->m_pkthdr.len;
	/*
	 * The CSUM_DATA_VALID flags indicates that the HW checked the UDP
	 * checksum and it was valid. Since CSUM_DATA_VALID ==
	 * CSUM_SCTP_VALID this would imply that the HW also verified the
	 * SCTP checksum. Therefore, clear the bit.
	 */
	SCTPDBG(SCTP_DEBUG_CRCOFFLOAD,
	    "sctp_recv_udp_tunneled_packet(): Packet of length %d received on %s with csum_flags 0x%b.\n",
	    m->m_pkthdr.len,
	    if_name(m->m_pkthdr.rcvif),
	    (int)m->m_pkthdr.csum_flags, CSUM_BITS);
	m->m_pkthdr.csum_flags &= ~CSUM_DATA_VALID;
	iph = mtod(m, struct ip *);
	switch (iph->ip_v) {
#ifdef INET
	case IPVERSION:
		iph->ip_len = htons(ntohs(iph->ip_len) - sizeof(struct udphdr));
		sctp_input_with_port(m, off, port);
		break;
#endif
#ifdef INET6
	case IPV6_VERSION >> 4:
		ip6 = mtod(m, struct ip6_hdr *);
		ip6->ip6_plen = htons(ntohs(ip6->ip6_plen) - sizeof(struct udphdr));
		sctp6_input_with_port(&m, &off, port);
		break;
#endif
	default:
		goto out;
		break;
	}
	return;
out:
	m_freem(m);
}

#ifdef INET
static void
sctp_recv_icmp_tunneled_packet(int cmd, struct sockaddr *sa, void *vip, void *ctx SCTP_UNUSED)
{
	struct ip *outer_ip, *inner_ip;
	struct sctphdr *sh;
	struct icmp *icmp;
	struct udphdr *udp;
	struct sctp_inpcb *inp;
	struct sctp_tcb *stcb;
	struct sctp_nets *net;
	struct sctp_init_chunk *ch;
	struct sockaddr_in src, dst;
	uint8_t type, code;

	inner_ip = (struct ip *)vip;
	icmp = (struct icmp *)((caddr_t)inner_ip -
	    (sizeof(struct icmp) - sizeof(struct ip)));
	outer_ip = (struct ip *)((caddr_t)icmp - sizeof(struct ip));
	if (ntohs(outer_ip->ip_len) <
	    sizeof(struct ip) + 8 + (inner_ip->ip_hl << 2) + sizeof(struct udphdr) + 8) {
		return;
	}
	udp = (struct udphdr *)((caddr_t)inner_ip + (inner_ip->ip_hl << 2));
	sh = (struct sctphdr *)(udp + 1);
	memset(&src, 0, sizeof(struct sockaddr_in));
	src.sin_family = AF_INET;
	src.sin_len = sizeof(struct sockaddr_in);
	src.sin_port = sh->src_port;
	src.sin_addr = inner_ip->ip_src;
	memset(&dst, 0, sizeof(struct sockaddr_in));
	dst.sin_family = AF_INET;
	dst.sin_len = sizeof(struct sockaddr_in);
	dst.sin_port = sh->dest_port;
	dst.sin_addr = inner_ip->ip_dst;
	/*
	 * 'dst' holds the dest of the packet that failed to be sent. 'src'
	 * holds our local endpoint address. Thus we reverse the dst and the
	 * src in the lookup.
	 */
	inp = NULL;
	net = NULL;
	stcb = sctp_findassociation_addr_sa((struct sockaddr *)&dst,
	    (struct sockaddr *)&src,
	    &inp, &net, 1,
	    SCTP_DEFAULT_VRFID);
	if ((stcb != NULL) &&
	    (net != NULL) &&
	    (inp != NULL)) {
		/* Check the UDP port numbers */
		if ((udp->uh_dport != net->port) ||
		    (udp->uh_sport != htons(SCTP_BASE_SYSCTL(sctp_udp_tunneling_port)))) {
			SCTP_TCB_UNLOCK(stcb);
			return;
		}
		/* Check the verification tag */
		if (ntohl(sh->v_tag) != 0) {
			/*
			 * This must be the verification tag used for
			 * sending out packets. We don't consider packets
			 * reflecting the verification tag.
			 */
			if (ntohl(sh->v_tag) != stcb->asoc.peer_vtag) {
				SCTP_TCB_UNLOCK(stcb);
				return;
			}
		} else {
			if (ntohs(outer_ip->ip_len) >=
			    sizeof(struct ip) +
			    8 + (inner_ip->ip_hl << 2) + 8 + 20) {
				/*
				 * In this case we can check if we got an
				 * INIT chunk and if the initiate tag
				 * matches.
				 */
				ch = (struct sctp_init_chunk *)(sh + 1);
				if ((ch->ch.chunk_type != SCTP_INITIATION) ||
				    (ntohl(ch->init.initiate_tag) != stcb->asoc.my_vtag)) {
					SCTP_TCB_UNLOCK(stcb);
					return;
				}
			} else {
				SCTP_TCB_UNLOCK(stcb);
				return;
			}
		}
		type = icmp->icmp_type;
		code = icmp->icmp_code;
		if ((type == ICMP_UNREACH) &&
		    (code == ICMP_UNREACH_PORT)) {
			code = ICMP_UNREACH_PROTOCOL;
		}
		sctp_notify(inp, stcb, net, type, code,
		    ntohs(inner_ip->ip_len),
		    (uint32_t)ntohs(icmp->icmp_nextmtu));
	} else {
		if ((stcb == NULL) && (inp != NULL)) {
			/* reduce ref-count */
			SCTP_INP_WLOCK(inp);
			SCTP_INP_DECR_REF(inp);
			SCTP_INP_WUNLOCK(inp);
		}
		if (stcb) {
			SCTP_TCB_UNLOCK(stcb);
		}
	}
	return;
}
#endif

#ifdef INET6
static void
sctp_recv_icmp6_tunneled_packet(int cmd, struct sockaddr *sa, void *d, void *ctx SCTP_UNUSED)
{
	struct ip6ctlparam *ip6cp;
	struct sctp_inpcb *inp;
	struct sctp_tcb *stcb;
	struct sctp_nets *net;
	struct sctphdr sh;
	struct udphdr udp;
	struct sockaddr_in6 src, dst;
	uint8_t type, code;

	ip6cp = (struct ip6ctlparam *)d;
	/*
	 * XXX: We assume that when IPV6 is non NULL, M and OFF are valid.
	 */
	if (ip6cp->ip6c_m == NULL) {
		return;
	}
	/*
	 * Check if we can safely examine the ports and the verification tag
	 * of the SCTP common header.
	 */
	if (ip6cp->ip6c_m->m_pkthdr.len <
	    ip6cp->ip6c_off + sizeof(struct udphdr) + offsetof(struct sctphdr, checksum)) {
		return;
	}
	/* Copy out the UDP header. */
	memset(&udp, 0, sizeof(struct udphdr));
	m_copydata(ip6cp->ip6c_m,
	    ip6cp->ip6c_off,
	    sizeof(struct udphdr),
	    (caddr_t)&udp);
	/* Copy out the port numbers and the verification tag. */
	memset(&sh, 0, sizeof(struct sctphdr));
	m_copydata(ip6cp->ip6c_m,
	    ip6cp->ip6c_off + sizeof(struct udphdr),
	    sizeof(uint16_t) + sizeof(uint16_t) + sizeof(uint32_t),
	    (caddr_t)&sh);
	memset(&src, 0, sizeof(struct sockaddr_in6));
	src.sin6_family = AF_INET6;
	src.sin6_len = sizeof(struct sockaddr_in6);
	src.sin6_port = sh.src_port;
	src.sin6_addr = ip6cp->ip6c_ip6->ip6_src;
	if (in6_setscope(&src.sin6_addr, ip6cp->ip6c_m->m_pkthdr.rcvif, NULL) != 0) {
		return;
	}
	memset(&dst, 0, sizeof(struct sockaddr_in6));
	dst.sin6_family = AF_INET6;
	dst.sin6_len = sizeof(struct sockaddr_in6);
	dst.sin6_port = sh.dest_port;
	dst.sin6_addr = ip6cp->ip6c_ip6->ip6_dst;
	if (in6_setscope(&dst.sin6_addr, ip6cp->ip6c_m->m_pkthdr.rcvif, NULL) != 0) {
		return;
	}
	inp = NULL;
	net = NULL;
	stcb = sctp_findassociation_addr_sa((struct sockaddr *)&dst,
	    (struct sockaddr *)&src,
	    &inp, &net, 1, SCTP_DEFAULT_VRFID);
	if ((stcb != NULL) &&
	    (net != NULL) &&
	    (inp != NULL)) {
		/* Check the UDP port numbers */
		if ((udp.uh_dport != net->port) ||
		    (udp.uh_sport != htons(SCTP_BASE_SYSCTL(sctp_udp_tunneling_port)))) {
			SCTP_TCB_UNLOCK(stcb);
			return;
		}
		/* Check the verification tag */
		if (ntohl(sh.v_tag) != 0) {
			/*
			 * This must be the verification tag used for
			 * sending out packets. We don't consider packets
			 * reflecting the verification tag.
			 */
			if (ntohl(sh.v_tag) != stcb->asoc.peer_vtag) {
				SCTP_TCB_UNLOCK(stcb);
				return;
			}
		} else {
			if (ip6cp->ip6c_m->m_pkthdr.len >=
			    ip6cp->ip6c_off + sizeof(struct udphdr) +
			    sizeof(struct sctphdr) +
			    sizeof(struct sctp_chunkhdr) +
			    offsetof(struct sctp_init, a_rwnd)) {
				/*
				 * In this case we can check if we got an
				 * INIT chunk and if the initiate tag
				 * matches.
				 */
				uint32_t initiate_tag;
				uint8_t chunk_type;

				m_copydata(ip6cp->ip6c_m,
				    ip6cp->ip6c_off +
				    sizeof(struct udphdr) +
				    sizeof(struct sctphdr),
				    sizeof(uint8_t),
				    (caddr_t)&chunk_type);
				m_copydata(ip6cp->ip6c_m,
				    ip6cp->ip6c_off +
				    sizeof(struct udphdr) +
				    sizeof(struct sctphdr) +
				    sizeof(struct sctp_chunkhdr),
				    sizeof(uint32_t),
				    (caddr_t)&initiate_tag);
				if ((chunk_type != SCTP_INITIATION) ||
				    (ntohl(initiate_tag) != stcb->asoc.my_vtag)) {
					SCTP_TCB_UNLOCK(stcb);
					return;
				}
			} else {
				SCTP_TCB_UNLOCK(stcb);
				return;
			}
		}
		type = ip6cp->ip6c_icmp6->icmp6_type;
		code = ip6cp->ip6c_icmp6->icmp6_code;
		if ((type == ICMP6_DST_UNREACH) &&
		    (code == ICMP6_DST_UNREACH_NOPORT)) {
			type = ICMP6_PARAM_PROB;
			code = ICMP6_PARAMPROB_NEXTHEADER;
		}
		sctp6_notify(inp, stcb, net, type, code,
		    ntohl(ip6cp->ip6c_icmp6->icmp6_mtu));
	} else {
		if ((stcb == NULL) && (inp != NULL)) {
			/* reduce inp's ref-count */
			SCTP_INP_WLOCK(inp);
			SCTP_INP_DECR_REF(inp);
			SCTP_INP_WUNLOCK(inp);
		}
		if (stcb) {
			SCTP_TCB_UNLOCK(stcb);
		}
	}
}
#endif

void
sctp_over_udp_stop(void)
{
	/*
	 * This function assumes sysctl caller holds sctp_sysctl_info_lock()
	 * for writting!
	 */
#ifdef INET
	if (SCTP_BASE_INFO(udp4_tun_socket) != NULL) {
		soclose(SCTP_BASE_INFO(udp4_tun_socket));
		SCTP_BASE_INFO(udp4_tun_socket) = NULL;
	}
#endif
#ifdef INET6
	if (SCTP_BASE_INFO(udp6_tun_socket) != NULL) {
		soclose(SCTP_BASE_INFO(udp6_tun_socket));
		SCTP_BASE_INFO(udp6_tun_socket) = NULL;
	}
#endif
}

int
sctp_over_udp_start(void)
{
	uint16_t port;
	int ret;
#ifdef INET
	struct sockaddr_in sin;
#endif
#ifdef INET6
	struct sockaddr_in6 sin6;
#endif
	/*
	 * This function assumes sysctl caller holds sctp_sysctl_info_lock()
	 * for writting!
	 */
	port = SCTP_BASE_SYSCTL(sctp_udp_tunneling_port);
	if (ntohs(port) == 0) {
		/* Must have a port set */
		return (EINVAL);
	}
#ifdef INET
	if (SCTP_BASE_INFO(udp4_tun_socket) != NULL) {
		/* Already running -- must stop first */
		return (EALREADY);
	}
#endif
#ifdef INET6
	if (SCTP_BASE_INFO(udp6_tun_socket) != NULL) {
		/* Already running -- must stop first */
		return (EALREADY);
	}
#endif
#ifdef INET
	if ((ret = socreate(PF_INET, &SCTP_BASE_INFO(udp4_tun_socket),
	    SOCK_DGRAM, IPPROTO_UDP,
	    curthread->td_ucred, curthread))) {
		sctp_over_udp_stop();
		return (ret);
	}
	/* Call the special UDP hook. */
	if ((ret = udp_set_kernel_tunneling(SCTP_BASE_INFO(udp4_tun_socket),
	    sctp_recv_udp_tunneled_packet,
	    sctp_recv_icmp_tunneled_packet,
	    NULL))) {
		sctp_over_udp_stop();
		return (ret);
	}
	/* Ok, we have a socket, bind it to the port. */
	memset(&sin, 0, sizeof(struct sockaddr_in));
	sin.sin_len = sizeof(struct sockaddr_in);
	sin.sin_family = AF_INET;
	sin.sin_port = htons(port);
	if ((ret = sobind(SCTP_BASE_INFO(udp4_tun_socket),
	    (struct sockaddr *)&sin, curthread))) {
		sctp_over_udp_stop();
		return (ret);
	}
#endif
#ifdef INET6
	if ((ret = socreate(PF_INET6, &SCTP_BASE_INFO(udp6_tun_socket),
	    SOCK_DGRAM, IPPROTO_UDP,
	    curthread->td_ucred, curthread))) {
		sctp_over_udp_stop();
		return (ret);
	}
	/* Call the special UDP hook. */
	if ((ret = udp_set_kernel_tunneling(SCTP_BASE_INFO(udp6_tun_socket),
	    sctp_recv_udp_tunneled_packet,
	    sctp_recv_icmp6_tunneled_packet,
	    NULL))) {
		sctp_over_udp_stop();
		return (ret);
	}
	/* Ok, we have a socket, bind it to the port. */
	memset(&sin6, 0, sizeof(struct sockaddr_in6));
	sin6.sin6_len = sizeof(struct sockaddr_in6);
	sin6.sin6_family = AF_INET6;
	sin6.sin6_port = htons(port);
	if ((ret = sobind(SCTP_BASE_INFO(udp6_tun_socket),
	    (struct sockaddr *)&sin6, curthread))) {
		sctp_over_udp_stop();
		return (ret);
	}
#endif
	return (0);
}

/*
 * sctp_min_mtu ()returns the minimum of all non-zero arguments.
 * If all arguments are zero, zero is returned.
 */
uint32_t
sctp_min_mtu(uint32_t mtu1, uint32_t mtu2, uint32_t mtu3)
{
	if (mtu1 > 0) {
		if (mtu2 > 0) {
			if (mtu3 > 0) {
				return (min(mtu1, min(mtu2, mtu3)));
			} else {
				return (min(mtu1, mtu2));
			}
		} else {
			if (mtu3 > 0) {
				return (min(mtu1, mtu3));
			} else {
				return (mtu1);
			}
		}
	} else {
		if (mtu2 > 0) {
			if (mtu3 > 0) {
				return (min(mtu2, mtu3));
			} else {
				return (mtu2);
			}
		} else {
			return (mtu3);
		}
	}
}

void
sctp_hc_set_mtu(union sctp_sockstore *addr, uint16_t fibnum, uint32_t mtu)
{
	struct in_conninfo inc;

	memset(&inc, 0, sizeof(struct in_conninfo));
	inc.inc_fibnum = fibnum;
	switch (addr->sa.sa_family) {
#ifdef INET
	case AF_INET:
		inc.inc_faddr = addr->sin.sin_addr;
		break;
#endif
#ifdef INET6
	case AF_INET6:
		inc.inc_flags |= INC_ISIPV6;
		inc.inc6_faddr = addr->sin6.sin6_addr;
		break;
#endif
	default:
		return;
	}
	tcp_hc_updatemtu(&inc, (u_long)mtu);
}

uint32_t
sctp_hc_get_mtu(union sctp_sockstore *addr, uint16_t fibnum)
{
	struct in_conninfo inc;

	memset(&inc, 0, sizeof(struct in_conninfo));
	inc.inc_fibnum = fibnum;
	switch (addr->sa.sa_family) {
#ifdef INET
	case AF_INET:
		inc.inc_faddr = addr->sin.sin_addr;
		break;
#endif
#ifdef INET6
	case AF_INET6:
		inc.inc_flags |= INC_ISIPV6;
		inc.inc6_faddr = addr->sin6.sin6_addr;
		break;
#endif
	default:
		return (0);
	}
	return ((uint32_t)tcp_hc_getmtu(&inc));
}

void
sctp_set_state(struct sctp_tcb *stcb, int new_state)
{
#if defined(KDTRACE_HOOKS)
	int old_state = stcb->asoc.state;
#endif

	KASSERT((new_state & ~SCTP_STATE_MASK) == 0,
	    ("sctp_set_state: Can't set substate (new_state = %x)",
	    new_state));
	stcb->asoc.state = (stcb->asoc.state & ~SCTP_STATE_MASK) | new_state;
	if ((new_state == SCTP_STATE_SHUTDOWN_RECEIVED) ||
	    (new_state == SCTP_STATE_SHUTDOWN_SENT) ||
	    (new_state == SCTP_STATE_SHUTDOWN_ACK_SENT)) {
		SCTP_CLEAR_SUBSTATE(stcb, SCTP_STATE_SHUTDOWN_PENDING);
	}
#if defined(KDTRACE_HOOKS)
	if (((old_state & SCTP_STATE_MASK) != new_state) &&
	    !(((old_state & SCTP_STATE_MASK) == SCTP_STATE_EMPTY) &&
	    (new_state == SCTP_STATE_INUSE))) {
		SCTP_PROBE6(state__change, NULL, stcb, NULL, stcb, NULL, old_state);
	}
#endif
}

void
sctp_add_substate(struct sctp_tcb *stcb, int substate)
{
#if defined(KDTRACE_HOOKS)
	int old_state = stcb->asoc.state;
#endif

	KASSERT((substate & SCTP_STATE_MASK) == 0,
	    ("sctp_add_substate: Can't set state (substate = %x)",
	    substate));
	stcb->asoc.state |= substate;
#if defined(KDTRACE_HOOKS)
	if (((substate & SCTP_STATE_ABOUT_TO_BE_FREED) &&
	    ((old_state & SCTP_STATE_ABOUT_TO_BE_FREED) == 0)) ||
	    ((substate & SCTP_STATE_SHUTDOWN_PENDING) &&
	    ((old_state & SCTP_STATE_SHUTDOWN_PENDING) == 0))) {
		SCTP_PROBE6(state__change, NULL, stcb, NULL, stcb, NULL, old_state);
	}
#endif
}
