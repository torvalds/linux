/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2012, 2015 Chelsio Communications, Inc.
 * All rights reserved.
 * Written by: Navdeep Parhar <np@FreeBSD.org>
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_inet.h"
#include "opt_inet6.h"
#include "opt_ratelimit.h"

#ifdef TCP_OFFLOAD
#include <sys/param.h>
#include <sys/aio.h>
#include <sys/file.h>
#include <sys/kernel.h>
#include <sys/ktr.h>
#include <sys/module.h>
#include <sys/proc.h>
#include <sys/protosw.h>
#include <sys/domain.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/sglist.h>
#include <sys/taskqueue.h>
#include <netinet/in.h>
#include <netinet/in_pcb.h>
#include <netinet/ip.h>
#include <netinet/ip6.h>
#define TCPSTATES
#include <netinet/tcp_fsm.h>
#include <netinet/tcp_seq.h>
#include <netinet/tcp_var.h>
#include <netinet/toecore.h>

#include <security/mac/mac_framework.h>

#include <vm/vm.h>
#include <vm/vm_extern.h>
#include <vm/pmap.h>
#include <vm/vm_map.h>
#include <vm/vm_page.h>

#include "common/common.h"
#include "common/t4_msg.h"
#include "common/t4_regs.h"
#include "common/t4_tcb.h"
#include "tom/t4_tom_l2t.h"
#include "tom/t4_tom.h"

static void	t4_aiotx_cancel(struct kaiocb *job);
static void	t4_aiotx_queue_toep(struct toepcb *toep);

static size_t
aiotx_mbuf_pgoff(struct mbuf *m)
{
	struct aiotx_buffer *ab;

	MPASS(IS_AIOTX_MBUF(m));
	ab = m->m_ext.ext_arg1;
	return ((ab->ps.offset + (uintptr_t)m->m_ext.ext_arg2) % PAGE_SIZE);
}

static vm_page_t *
aiotx_mbuf_pages(struct mbuf *m)
{
	struct aiotx_buffer *ab;
	int npages;

	MPASS(IS_AIOTX_MBUF(m));
	ab = m->m_ext.ext_arg1;
	npages = (ab->ps.offset + (uintptr_t)m->m_ext.ext_arg2) / PAGE_SIZE;
	return (ab->ps.pages + npages);
}

void
send_flowc_wr(struct toepcb *toep, struct flowc_tx_params *ftxp)
{
	struct wrqe *wr;
	struct fw_flowc_wr *flowc;
	unsigned int nparams, flowclen, paramidx;
	struct vi_info *vi = toep->vi;
	struct port_info *pi = vi->pi;
	struct adapter *sc = pi->adapter;
	unsigned int pfvf = sc->pf << S_FW_VIID_PFN;
	struct ofld_tx_sdesc *txsd = &toep->txsd[toep->txsd_pidx];

	KASSERT(!(toep->flags & TPF_FLOWC_WR_SENT),
	    ("%s: flowc for tid %u sent already", __func__, toep->tid));

	if (ftxp != NULL)
		nparams = 8;
	else
		nparams = 6;
	if (toep->ulp_mode == ULP_MODE_TLS)
		nparams++;
	if (toep->tls.fcplenmax != 0)
		nparams++;
	if (toep->tc_idx != -1) {
		MPASS(toep->tc_idx >= 0 &&
		    toep->tc_idx < sc->chip_params->nsched_cls);
		nparams++;
	}

	flowclen = sizeof(*flowc) + nparams * sizeof(struct fw_flowc_mnemval);

	wr = alloc_wrqe(roundup2(flowclen, 16), toep->ofld_txq);
	if (wr == NULL) {
		/* XXX */
		panic("%s: allocation failure.", __func__);
	}
	flowc = wrtod(wr);
	memset(flowc, 0, wr->wr_len);

	flowc->op_to_nparams = htobe32(V_FW_WR_OP(FW_FLOWC_WR) |
	    V_FW_FLOWC_WR_NPARAMS(nparams));
	flowc->flowid_len16 = htonl(V_FW_WR_LEN16(howmany(flowclen, 16)) |
	    V_FW_WR_FLOWID(toep->tid));

#define FLOWC_PARAM(__m, __v) \
	do { \
		flowc->mnemval[paramidx].mnemonic = FW_FLOWC_MNEM_##__m; \
		flowc->mnemval[paramidx].val = htobe32(__v); \
		paramidx++; \
	} while (0)

	paramidx = 0;

	FLOWC_PARAM(PFNVFN, pfvf);
	FLOWC_PARAM(CH, pi->tx_chan);
	FLOWC_PARAM(PORT, pi->tx_chan);
	FLOWC_PARAM(IQID, toep->ofld_rxq->iq.abs_id);
	if (ftxp) {
		uint32_t sndbuf = min(ftxp->snd_space, sc->tt.sndbuf);

		FLOWC_PARAM(SNDNXT, ftxp->snd_nxt);
		FLOWC_PARAM(RCVNXT, ftxp->rcv_nxt);
		FLOWC_PARAM(SNDBUF, sndbuf);
		FLOWC_PARAM(MSS, ftxp->mss);

		CTR6(KTR_CXGBE,
		    "%s: tid %u, mss %u, sndbuf %u, snd_nxt 0x%x, rcv_nxt 0x%x",
		    __func__, toep->tid, ftxp->mss, sndbuf, ftxp->snd_nxt,
		    ftxp->rcv_nxt);
	} else {
		FLOWC_PARAM(SNDBUF, 512);
		FLOWC_PARAM(MSS, 512);

		CTR2(KTR_CXGBE, "%s: tid %u", __func__, toep->tid);
	}
	if (toep->ulp_mode == ULP_MODE_TLS)
		FLOWC_PARAM(ULP_MODE, toep->ulp_mode);
	if (toep->tls.fcplenmax != 0)
		FLOWC_PARAM(TXDATAPLEN_MAX, toep->tls.fcplenmax);
	if (toep->tc_idx != -1)
		FLOWC_PARAM(SCHEDCLASS, toep->tc_idx);
#undef FLOWC_PARAM

	KASSERT(paramidx == nparams, ("nparams mismatch"));

	txsd->tx_credits = howmany(flowclen, 16);
	txsd->plen = 0;
	KASSERT(toep->tx_credits >= txsd->tx_credits && toep->txsd_avail > 0,
	    ("%s: not enough credits (%d)", __func__, toep->tx_credits));
	toep->tx_credits -= txsd->tx_credits;
	if (__predict_false(++toep->txsd_pidx == toep->txsd_total))
		toep->txsd_pidx = 0;
	toep->txsd_avail--;

	toep->flags |= TPF_FLOWC_WR_SENT;
        t4_wrq_tx(sc, wr);
}

#ifdef RATELIMIT
/*
 * Input is Bytes/second (so_max_pacing_rate), chip counts in Kilobits/second.
 */
static int
update_tx_rate_limit(struct adapter *sc, struct toepcb *toep, u_int Bps)
{
	int tc_idx, rc;
	const u_int kbps = (u_int) (uint64_t)Bps * 8ULL / 1000;
	const int port_id = toep->vi->pi->port_id;

	CTR3(KTR_CXGBE, "%s: tid %u, rate %uKbps", __func__, toep->tid, kbps);

	if (kbps == 0) {
		/* unbind */
		tc_idx = -1;
	} else {
		rc = t4_reserve_cl_rl_kbps(sc, port_id, kbps, &tc_idx);
		if (rc != 0)
			return (rc);
		MPASS(tc_idx >= 0 && tc_idx < sc->chip_params->nsched_cls);
	}

	if (toep->tc_idx != tc_idx) {
		struct wrqe *wr;
		struct fw_flowc_wr *flowc;
		int nparams = 1, flowclen, flowclen16;
		struct ofld_tx_sdesc *txsd = &toep->txsd[toep->txsd_pidx];

		flowclen = sizeof(*flowc) + nparams * sizeof(struct
		    fw_flowc_mnemval);
		flowclen16 = howmany(flowclen, 16);
		if (toep->tx_credits < flowclen16 || toep->txsd_avail == 0 ||
		    (wr = alloc_wrqe(roundup2(flowclen, 16), toep->ofld_txq)) == NULL) {
			if (tc_idx >= 0)
				t4_release_cl_rl(sc, port_id, tc_idx);
			return (ENOMEM);
		}

		flowc = wrtod(wr);
		memset(flowc, 0, wr->wr_len);

		flowc->op_to_nparams = htobe32(V_FW_WR_OP(FW_FLOWC_WR) |
		    V_FW_FLOWC_WR_NPARAMS(nparams));
		flowc->flowid_len16 = htonl(V_FW_WR_LEN16(flowclen16) |
		    V_FW_WR_FLOWID(toep->tid));

		flowc->mnemval[0].mnemonic = FW_FLOWC_MNEM_SCHEDCLASS;
		if (tc_idx == -1)
			flowc->mnemval[0].val = htobe32(0xff);
		else
			flowc->mnemval[0].val = htobe32(tc_idx);

		txsd->tx_credits = flowclen16;
		txsd->plen = 0;
		toep->tx_credits -= txsd->tx_credits;
		if (__predict_false(++toep->txsd_pidx == toep->txsd_total))
			toep->txsd_pidx = 0;
		toep->txsd_avail--;
		t4_wrq_tx(sc, wr);
	}

	if (toep->tc_idx >= 0)
		t4_release_cl_rl(sc, port_id, toep->tc_idx);
	toep->tc_idx = tc_idx;

	return (0);
}
#endif

void
send_reset(struct adapter *sc, struct toepcb *toep, uint32_t snd_nxt)
{
	struct wrqe *wr;
	struct cpl_abort_req *req;
	int tid = toep->tid;
	struct inpcb *inp = toep->inp;
	struct tcpcb *tp = intotcpcb(inp);	/* don't use if INP_DROPPED */

	INP_WLOCK_ASSERT(inp);

	CTR6(KTR_CXGBE, "%s: tid %d (%s), toep_flags 0x%x, inp_flags 0x%x%s",
	    __func__, toep->tid,
	    inp->inp_flags & INP_DROPPED ? "inp dropped" :
	    tcpstates[tp->t_state],
	    toep->flags, inp->inp_flags,
	    toep->flags & TPF_ABORT_SHUTDOWN ?
	    " (abort already in progress)" : "");

	if (toep->flags & TPF_ABORT_SHUTDOWN)
		return;	/* abort already in progress */

	toep->flags |= TPF_ABORT_SHUTDOWN;

	KASSERT(toep->flags & TPF_FLOWC_WR_SENT,
	    ("%s: flowc_wr not sent for tid %d.", __func__, tid));

	wr = alloc_wrqe(sizeof(*req), toep->ofld_txq);
	if (wr == NULL) {
		/* XXX */
		panic("%s: allocation failure.", __func__);
	}
	req = wrtod(wr);

	INIT_TP_WR_MIT_CPL(req, CPL_ABORT_REQ, tid);
	if (inp->inp_flags & INP_DROPPED)
		req->rsvd0 = htobe32(snd_nxt);
	else
		req->rsvd0 = htobe32(tp->snd_nxt);
	req->rsvd1 = !(toep->flags & TPF_TX_DATA_SENT);
	req->cmd = CPL_ABORT_SEND_RST;

	/*
	 * XXX: What's the correct way to tell that the inp hasn't been detached
	 * from its socket?  Should I even be flushing the snd buffer here?
	 */
	if ((inp->inp_flags & (INP_DROPPED | INP_TIMEWAIT)) == 0) {
		struct socket *so = inp->inp_socket;

		if (so != NULL)	/* because I'm not sure.  See comment above */
			sbflush(&so->so_snd);
	}

	t4_l2t_send(sc, wr, toep->l2te);
}

/*
 * Called when a connection is established to translate the TCP options
 * reported by HW to FreeBSD's native format.
 */
static void
assign_rxopt(struct tcpcb *tp, unsigned int opt)
{
	struct toepcb *toep = tp->t_toe;
	struct inpcb *inp = tp->t_inpcb;
	struct adapter *sc = td_adapter(toep->td);
	int n;

	INP_LOCK_ASSERT(inp);

	if (inp->inp_inc.inc_flags & INC_ISIPV6)
		n = sizeof(struct ip6_hdr) + sizeof(struct tcphdr);
	else
		n = sizeof(struct ip) + sizeof(struct tcphdr);
	tp->t_maxseg = sc->params.mtus[G_TCPOPT_MSS(opt)] - n;

	if (G_TCPOPT_TSTAMP(opt)) {
		tp->t_flags |= TF_RCVD_TSTMP;	/* timestamps ok */
		tp->ts_recent = 0;		/* hmmm */
		tp->ts_recent_age = tcp_ts_getticks();
		tp->t_maxseg -= TCPOLEN_TSTAMP_APPA;
	}

	CTR5(KTR_CXGBE, "%s: tid %d, mtu_idx %u (%u), mss %u", __func__,
	    toep->tid, G_TCPOPT_MSS(opt), sc->params.mtus[G_TCPOPT_MSS(opt)],
	    tp->t_maxseg);

	if (G_TCPOPT_SACK(opt))
		tp->t_flags |= TF_SACK_PERMIT;	/* should already be set */
	else
		tp->t_flags &= ~TF_SACK_PERMIT;	/* sack disallowed by peer */

	if (G_TCPOPT_WSCALE_OK(opt))
		tp->t_flags |= TF_RCVD_SCALE;

	/* Doing window scaling? */
	if ((tp->t_flags & (TF_RCVD_SCALE | TF_REQ_SCALE)) ==
	    (TF_RCVD_SCALE | TF_REQ_SCALE)) {
		tp->rcv_scale = tp->request_r_scale;
		tp->snd_scale = G_TCPOPT_SND_WSCALE(opt);
	}
}

/*
 * Completes some final bits of initialization for just established connections
 * and changes their state to TCPS_ESTABLISHED.
 *
 * The ISNs are from the exchange of SYNs.
 */
void
make_established(struct toepcb *toep, uint32_t iss, uint32_t irs, uint16_t opt)
{
	struct inpcb *inp = toep->inp;
	struct socket *so = inp->inp_socket;
	struct tcpcb *tp = intotcpcb(inp);
	long bufsize;
	uint16_t tcpopt = be16toh(opt);
	struct flowc_tx_params ftxp;

	INP_WLOCK_ASSERT(inp);
	KASSERT(tp->t_state == TCPS_SYN_SENT ||
	    tp->t_state == TCPS_SYN_RECEIVED,
	    ("%s: TCP state %s", __func__, tcpstates[tp->t_state]));

	CTR6(KTR_CXGBE, "%s: tid %d, so %p, inp %p, tp %p, toep %p",
	    __func__, toep->tid, so, inp, tp, toep);

	tcp_state_change(tp, TCPS_ESTABLISHED);
	tp->t_starttime = ticks;
	TCPSTAT_INC(tcps_connects);

	tp->irs = irs;
	tcp_rcvseqinit(tp);
	tp->rcv_wnd = toep->rx_credits << 10;
	tp->rcv_adv += tp->rcv_wnd;
	tp->last_ack_sent = tp->rcv_nxt;

	/*
	 * If we were unable to send all rx credits via opt0, save the remainder
	 * in rx_credits so that they can be handed over with the next credit
	 * update.
	 */
	SOCKBUF_LOCK(&so->so_rcv);
	bufsize = select_rcv_wnd(so);
	SOCKBUF_UNLOCK(&so->so_rcv);
	toep->rx_credits = bufsize - tp->rcv_wnd;

	tp->iss = iss;
	tcp_sendseqinit(tp);
	tp->snd_una = iss + 1;
	tp->snd_nxt = iss + 1;
	tp->snd_max = iss + 1;

	assign_rxopt(tp, tcpopt);

	SOCKBUF_LOCK(&so->so_snd);
	if (so->so_snd.sb_flags & SB_AUTOSIZE && V_tcp_do_autosndbuf)
		bufsize = V_tcp_autosndbuf_max;
	else
		bufsize = sbspace(&so->so_snd);
	SOCKBUF_UNLOCK(&so->so_snd);

	ftxp.snd_nxt = tp->snd_nxt;
	ftxp.rcv_nxt = tp->rcv_nxt;
	ftxp.snd_space = bufsize;
	ftxp.mss = tp->t_maxseg;
	send_flowc_wr(toep, &ftxp);

	soisconnected(so);
}

int
send_rx_credits(struct adapter *sc, struct toepcb *toep, int credits)
{
	struct wrqe *wr;
	struct cpl_rx_data_ack *req;
	uint32_t dack = F_RX_DACK_CHANGE | V_RX_DACK_MODE(1);

	KASSERT(credits >= 0, ("%s: %d credits", __func__, credits));

	wr = alloc_wrqe(sizeof(*req), toep->ctrlq);
	if (wr == NULL)
		return (0);
	req = wrtod(wr);

	INIT_TP_WR_MIT_CPL(req, CPL_RX_DATA_ACK, toep->tid);
	req->credit_dack = htobe32(dack | V_RX_CREDITS(credits));

	t4_wrq_tx(sc, wr);
	return (credits);
}

void
send_rx_modulate(struct adapter *sc, struct toepcb *toep)
{
	struct wrqe *wr;
	struct cpl_rx_data_ack *req;

	wr = alloc_wrqe(sizeof(*req), toep->ctrlq);
	if (wr == NULL)
		return;
	req = wrtod(wr);

	INIT_TP_WR_MIT_CPL(req, CPL_RX_DATA_ACK, toep->tid);
	req->credit_dack = htobe32(F_RX_MODULATE_RX);

	t4_wrq_tx(sc, wr);
}

void
t4_rcvd_locked(struct toedev *tod, struct tcpcb *tp)
{
	struct adapter *sc = tod->tod_softc;
	struct inpcb *inp = tp->t_inpcb;
	struct socket *so = inp->inp_socket;
	struct sockbuf *sb = &so->so_rcv;
	struct toepcb *toep = tp->t_toe;
	int credits;

	INP_WLOCK_ASSERT(inp);

	SOCKBUF_LOCK_ASSERT(sb);
	KASSERT(toep->sb_cc >= sbused(sb),
	    ("%s: sb %p has more data (%d) than last time (%d).",
	    __func__, sb, sbused(sb), toep->sb_cc));

	credits = toep->sb_cc - sbused(sb);
	toep->sb_cc = sbused(sb);
	if (toep->ulp_mode == ULP_MODE_TLS) {
		if (toep->tls.rcv_over >= credits) {
			toep->tls.rcv_over -= credits;
			credits = 0;
		} else {
			credits -= toep->tls.rcv_over;
			toep->tls.rcv_over = 0;
		}
	}
	toep->rx_credits += credits;

	if (toep->rx_credits > 0 &&
	    (tp->rcv_wnd <= 32 * 1024 || toep->rx_credits >= 64 * 1024 ||
	    (toep->rx_credits >= 16 * 1024 && tp->rcv_wnd <= 128 * 1024) ||
	    toep->sb_cc + tp->rcv_wnd < sb->sb_lowat)) {

		credits = send_rx_credits(sc, toep, toep->rx_credits);
		toep->rx_credits -= credits;
		tp->rcv_wnd += credits;
		tp->rcv_adv += credits;
	} else if (toep->flags & TPF_FORCE_CREDITS)
		send_rx_modulate(sc, toep);
}

void
t4_rcvd(struct toedev *tod, struct tcpcb *tp)
{
	struct inpcb *inp = tp->t_inpcb;
	struct socket *so = inp->inp_socket;
	struct sockbuf *sb = &so->so_rcv;

	SOCKBUF_LOCK(sb);
	t4_rcvd_locked(tod, tp);
	SOCKBUF_UNLOCK(sb);
}

/*
 * Close a connection by sending a CPL_CLOSE_CON_REQ message.
 */
int
t4_close_conn(struct adapter *sc, struct toepcb *toep)
{
	struct wrqe *wr;
	struct cpl_close_con_req *req;
	unsigned int tid = toep->tid;

	CTR3(KTR_CXGBE, "%s: tid %u%s", __func__, toep->tid,
	    toep->flags & TPF_FIN_SENT ? ", IGNORED" : "");

	if (toep->flags & TPF_FIN_SENT)
		return (0);

	KASSERT(toep->flags & TPF_FLOWC_WR_SENT,
	    ("%s: flowc_wr not sent for tid %u.", __func__, tid));

	wr = alloc_wrqe(sizeof(*req), toep->ofld_txq);
	if (wr == NULL) {
		/* XXX */
		panic("%s: allocation failure.", __func__);
	}
	req = wrtod(wr);

        req->wr.wr_hi = htonl(V_FW_WR_OP(FW_TP_WR) |
	    V_FW_WR_IMMDLEN(sizeof(*req) - sizeof(req->wr)));
	req->wr.wr_mid = htonl(V_FW_WR_LEN16(howmany(sizeof(*req), 16)) |
	    V_FW_WR_FLOWID(tid));
        req->wr.wr_lo = cpu_to_be64(0);
        OPCODE_TID(req) = htonl(MK_OPCODE_TID(CPL_CLOSE_CON_REQ, tid));
	req->rsvd = 0;

	toep->flags |= TPF_FIN_SENT;
	toep->flags &= ~TPF_SEND_FIN;
	t4_l2t_send(sc, wr, toep->l2te);

	return (0);
}

#define MAX_OFLD_TX_CREDITS (SGE_MAX_WR_LEN / 16)
#define MIN_OFLD_TX_CREDITS (howmany(sizeof(struct fw_ofld_tx_data_wr) + 1, 16))

/* Maximum amount of immediate data we could stuff in a WR */
static inline int
max_imm_payload(int tx_credits)
{
	const int n = 2;	/* Use only up to 2 desc for imm. data WR */

	KASSERT(tx_credits >= 0 &&
		tx_credits <= MAX_OFLD_TX_CREDITS,
		("%s: %d credits", __func__, tx_credits));

	if (tx_credits < MIN_OFLD_TX_CREDITS)
		return (0);

	if (tx_credits >= (n * EQ_ESIZE) / 16)
		return ((n * EQ_ESIZE) - sizeof(struct fw_ofld_tx_data_wr));
	else
		return (tx_credits * 16 - sizeof(struct fw_ofld_tx_data_wr));
}

/* Maximum number of SGL entries we could stuff in a WR */
static inline int
max_dsgl_nsegs(int tx_credits)
{
	int nseg = 1;	/* ulptx_sgl has room for 1, rest ulp_tx_sge_pair */
	int sge_pair_credits = tx_credits - MIN_OFLD_TX_CREDITS;

	KASSERT(tx_credits >= 0 &&
		tx_credits <= MAX_OFLD_TX_CREDITS,
		("%s: %d credits", __func__, tx_credits));

	if (tx_credits < MIN_OFLD_TX_CREDITS)
		return (0);

	nseg += 2 * (sge_pair_credits * 16 / 24);
	if ((sge_pair_credits * 16) % 24 == 16)
		nseg++;

	return (nseg);
}

static inline void
write_tx_wr(void *dst, struct toepcb *toep, unsigned int immdlen,
    unsigned int plen, uint8_t credits, int shove, int ulp_submode, int txalign)
{
	struct fw_ofld_tx_data_wr *txwr = dst;

	txwr->op_to_immdlen = htobe32(V_WR_OP(FW_OFLD_TX_DATA_WR) |
	    V_FW_WR_IMMDLEN(immdlen));
	txwr->flowid_len16 = htobe32(V_FW_WR_FLOWID(toep->tid) |
	    V_FW_WR_LEN16(credits));
	txwr->lsodisable_to_flags = htobe32(V_TX_ULP_MODE(toep->ulp_mode) |
	    V_TX_ULP_SUBMODE(ulp_submode) | V_TX_URG(0) | V_TX_SHOVE(shove));
	txwr->plen = htobe32(plen);

	if (txalign > 0) {
		struct tcpcb *tp = intotcpcb(toep->inp);

		if (plen < 2 * tp->t_maxseg)
			txwr->lsodisable_to_flags |=
			    htobe32(F_FW_OFLD_TX_DATA_WR_LSODISABLE);
		else
			txwr->lsodisable_to_flags |=
			    htobe32(F_FW_OFLD_TX_DATA_WR_ALIGNPLD |
				(tp->t_flags & TF_NODELAY ? 0 :
				F_FW_OFLD_TX_DATA_WR_ALIGNPLDSHOVE));
	}
}

/*
 * Generate a DSGL from a starting mbuf.  The total number of segments and the
 * maximum segments in any one mbuf are provided.
 */
static void
write_tx_sgl(void *dst, struct mbuf *start, struct mbuf *stop, int nsegs, int n)
{
	struct mbuf *m;
	struct ulptx_sgl *usgl = dst;
	int i, j, rc;
	struct sglist sg;
	struct sglist_seg segs[n];

	KASSERT(nsegs > 0, ("%s: nsegs 0", __func__));

	sglist_init(&sg, n, segs);
	usgl->cmd_nsge = htobe32(V_ULPTX_CMD(ULP_TX_SC_DSGL) |
	    V_ULPTX_NSGE(nsegs));

	i = -1;
	for (m = start; m != stop; m = m->m_next) {
		if (IS_AIOTX_MBUF(m))
			rc = sglist_append_vmpages(&sg, aiotx_mbuf_pages(m),
			    aiotx_mbuf_pgoff(m), m->m_len);
		else
			rc = sglist_append(&sg, mtod(m, void *), m->m_len);
		if (__predict_false(rc != 0))
			panic("%s: sglist_append %d", __func__, rc);

		for (j = 0; j < sg.sg_nseg; i++, j++) {
			if (i < 0) {
				usgl->len0 = htobe32(segs[j].ss_len);
				usgl->addr0 = htobe64(segs[j].ss_paddr);
			} else {
				usgl->sge[i / 2].len[i & 1] =
				    htobe32(segs[j].ss_len);
				usgl->sge[i / 2].addr[i & 1] =
				    htobe64(segs[j].ss_paddr);
			}
#ifdef INVARIANTS
			nsegs--;
#endif
		}
		sglist_reset(&sg);
	}
	if (i & 1)
		usgl->sge[i / 2].len[1] = htobe32(0);
	KASSERT(nsegs == 0, ("%s: nsegs %d, start %p, stop %p",
	    __func__, nsegs, start, stop));
}

/*
 * Max number of SGL entries an offload tx work request can have.  This is 41
 * (1 + 40) for a full 512B work request.
 * fw_ofld_tx_data_wr(16B) + ulptx_sgl(16B, 1) + ulptx_sge_pair(480B, 40)
 */
#define OFLD_SGL_LEN (41)

/*
 * Send data and/or a FIN to the peer.
 *
 * The socket's so_snd buffer consists of a stream of data starting with sb_mb
 * and linked together with m_next.  sb_sndptr, if set, is the last mbuf that
 * was transmitted.
 *
 * drop indicates the number of bytes that should be dropped from the head of
 * the send buffer.  It is an optimization that lets do_fw4_ack avoid creating
 * contention on the send buffer lock (before this change it used to do
 * sowwakeup and then t4_push_frames right after that when recovering from tx
 * stalls).  When drop is set this function MUST drop the bytes and wake up any
 * writers.
 */
void
t4_push_frames(struct adapter *sc, struct toepcb *toep, int drop)
{
	struct mbuf *sndptr, *m, *sb_sndptr;
	struct fw_ofld_tx_data_wr *txwr;
	struct wrqe *wr;
	u_int plen, nsegs, credits, max_imm, max_nsegs, max_nsegs_1mbuf;
	struct inpcb *inp = toep->inp;
	struct tcpcb *tp = intotcpcb(inp);
	struct socket *so = inp->inp_socket;
	struct sockbuf *sb = &so->so_snd;
	int tx_credits, shove, compl, sowwakeup;
	struct ofld_tx_sdesc *txsd;
	bool aiotx_mbuf_seen;

	INP_WLOCK_ASSERT(inp);
	KASSERT(toep->flags & TPF_FLOWC_WR_SENT,
	    ("%s: flowc_wr not sent for tid %u.", __func__, toep->tid));

	KASSERT(toep->ulp_mode == ULP_MODE_NONE ||
	    toep->ulp_mode == ULP_MODE_TCPDDP ||
	    toep->ulp_mode == ULP_MODE_TLS ||
	    toep->ulp_mode == ULP_MODE_RDMA,
	    ("%s: ulp_mode %u for toep %p", __func__, toep->ulp_mode, toep));

#ifdef VERBOSE_TRACES
	CTR4(KTR_CXGBE, "%s: tid %d toep flags %#x tp flags %#x drop %d",
	    __func__, toep->tid, toep->flags, tp->t_flags);
#endif
	if (__predict_false(toep->flags & TPF_ABORT_SHUTDOWN))
		return;

#ifdef RATELIMIT
	if (__predict_false(inp->inp_flags2 & INP_RATE_LIMIT_CHANGED) &&
	    (update_tx_rate_limit(sc, toep, so->so_max_pacing_rate) == 0)) {
		inp->inp_flags2 &= ~INP_RATE_LIMIT_CHANGED;
	}
#endif

	/*
	 * This function doesn't resume by itself.  Someone else must clear the
	 * flag and call this function.
	 */
	if (__predict_false(toep->flags & TPF_TX_SUSPENDED)) {
		KASSERT(drop == 0,
		    ("%s: drop (%d) != 0 but tx is suspended", __func__, drop));
		return;
	}

	txsd = &toep->txsd[toep->txsd_pidx];
	do {
		tx_credits = min(toep->tx_credits, MAX_OFLD_TX_CREDITS);
		max_imm = max_imm_payload(tx_credits);
		max_nsegs = max_dsgl_nsegs(tx_credits);

		SOCKBUF_LOCK(sb);
		sowwakeup = drop;
		if (drop) {
			sbdrop_locked(sb, drop);
			drop = 0;
		}
		sb_sndptr = sb->sb_sndptr;
		sndptr = sb_sndptr ? sb_sndptr->m_next : sb->sb_mb;
		plen = 0;
		nsegs = 0;
		max_nsegs_1mbuf = 0; /* max # of SGL segments in any one mbuf */
		aiotx_mbuf_seen = false;
		for (m = sndptr; m != NULL; m = m->m_next) {
			int n;

			if (IS_AIOTX_MBUF(m))
				n = sglist_count_vmpages(aiotx_mbuf_pages(m),
				    aiotx_mbuf_pgoff(m), m->m_len);
			else
				n = sglist_count(mtod(m, void *), m->m_len);

			nsegs += n;
			plen += m->m_len;

			/* This mbuf sent us _over_ the nsegs limit, back out */
			if (plen > max_imm && nsegs > max_nsegs) {
				nsegs -= n;
				plen -= m->m_len;
				if (plen == 0) {
					/* Too few credits */
					toep->flags |= TPF_TX_SUSPENDED;
					if (sowwakeup) {
						if (!TAILQ_EMPTY(
						    &toep->aiotx_jobq))
							t4_aiotx_queue_toep(
							    toep);
						sowwakeup_locked(so);
					} else
						SOCKBUF_UNLOCK(sb);
					SOCKBUF_UNLOCK_ASSERT(sb);
					return;
				}
				break;
			}

			if (IS_AIOTX_MBUF(m))
				aiotx_mbuf_seen = true;
			if (max_nsegs_1mbuf < n)
				max_nsegs_1mbuf = n;
			sb_sndptr = m;	/* new sb->sb_sndptr if all goes well */

			/* This mbuf put us right at the max_nsegs limit */
			if (plen > max_imm && nsegs == max_nsegs) {
				m = m->m_next;
				break;
			}
		}

		if (sbused(sb) > sb->sb_hiwat * 5 / 8 &&
		    toep->plen_nocompl + plen >= sb->sb_hiwat / 4)
			compl = 1;
		else
			compl = 0;

		if (sb->sb_flags & SB_AUTOSIZE &&
		    V_tcp_do_autosndbuf &&
		    sb->sb_hiwat < V_tcp_autosndbuf_max &&
		    sbused(sb) >= sb->sb_hiwat * 7 / 8) {
			int newsize = min(sb->sb_hiwat + V_tcp_autosndbuf_inc,
			    V_tcp_autosndbuf_max);

			if (!sbreserve_locked(sb, newsize, so, NULL))
				sb->sb_flags &= ~SB_AUTOSIZE;
			else
				sowwakeup = 1;	/* room available */
		}
		if (sowwakeup) {
			if (!TAILQ_EMPTY(&toep->aiotx_jobq))
				t4_aiotx_queue_toep(toep);
			sowwakeup_locked(so);
		} else
			SOCKBUF_UNLOCK(sb);
		SOCKBUF_UNLOCK_ASSERT(sb);

		/* nothing to send */
		if (plen == 0) {
			KASSERT(m == NULL,
			    ("%s: nothing to send, but m != NULL", __func__));
			break;
		}

		if (__predict_false(toep->flags & TPF_FIN_SENT))
			panic("%s: excess tx.", __func__);

		shove = m == NULL && !(tp->t_flags & TF_MORETOCOME);
		if (plen <= max_imm && !aiotx_mbuf_seen) {

			/* Immediate data tx */

			wr = alloc_wrqe(roundup2(sizeof(*txwr) + plen, 16),
					toep->ofld_txq);
			if (wr == NULL) {
				/* XXX: how will we recover from this? */
				toep->flags |= TPF_TX_SUSPENDED;
				return;
			}
			txwr = wrtod(wr);
			credits = howmany(wr->wr_len, 16);
			write_tx_wr(txwr, toep, plen, plen, credits, shove, 0,
			    sc->tt.tx_align);
			m_copydata(sndptr, 0, plen, (void *)(txwr + 1));
			nsegs = 0;
		} else {
			int wr_len;

			/* DSGL tx */

			wr_len = sizeof(*txwr) + sizeof(struct ulptx_sgl) +
			    ((3 * (nsegs - 1)) / 2 + ((nsegs - 1) & 1)) * 8;
			wr = alloc_wrqe(roundup2(wr_len, 16), toep->ofld_txq);
			if (wr == NULL) {
				/* XXX: how will we recover from this? */
				toep->flags |= TPF_TX_SUSPENDED;
				return;
			}
			txwr = wrtod(wr);
			credits = howmany(wr_len, 16);
			write_tx_wr(txwr, toep, 0, plen, credits, shove, 0,
			    sc->tt.tx_align);
			write_tx_sgl(txwr + 1, sndptr, m, nsegs,
			    max_nsegs_1mbuf);
			if (wr_len & 0xf) {
				uint64_t *pad = (uint64_t *)
				    ((uintptr_t)txwr + wr_len);
				*pad = 0;
			}
		}

		KASSERT(toep->tx_credits >= credits,
			("%s: not enough credits", __func__));

		toep->tx_credits -= credits;
		toep->tx_nocompl += credits;
		toep->plen_nocompl += plen;
		if (toep->tx_credits <= toep->tx_total * 3 / 8 &&
		    toep->tx_nocompl >= toep->tx_total / 4)
			compl = 1;

		if (compl || toep->ulp_mode == ULP_MODE_RDMA) {
			txwr->op_to_immdlen |= htobe32(F_FW_WR_COMPL);
			toep->tx_nocompl = 0;
			toep->plen_nocompl = 0;
		}

		tp->snd_nxt += plen;
		tp->snd_max += plen;

		SOCKBUF_LOCK(sb);
		KASSERT(sb_sndptr, ("%s: sb_sndptr is NULL", __func__));
		sb->sb_sndptr = sb_sndptr;
		SOCKBUF_UNLOCK(sb);

		toep->flags |= TPF_TX_DATA_SENT;
		if (toep->tx_credits < MIN_OFLD_TX_CREDITS)
			toep->flags |= TPF_TX_SUSPENDED;

		KASSERT(toep->txsd_avail > 0, ("%s: no txsd", __func__));
		txsd->plen = plen;
		txsd->tx_credits = credits;
		txsd++;
		if (__predict_false(++toep->txsd_pidx == toep->txsd_total)) {
			toep->txsd_pidx = 0;
			txsd = &toep->txsd[0];
		}
		toep->txsd_avail--;

		t4_l2t_send(sc, wr, toep->l2te);
	} while (m != NULL);

	/* Send a FIN if requested, but only if there's no more data to send */
	if (m == NULL && toep->flags & TPF_SEND_FIN)
		t4_close_conn(sc, toep);
}

static inline void
rqdrop_locked(struct mbufq *q, int plen)
{
	struct mbuf *m;

	while (plen > 0) {
		m = mbufq_dequeue(q);

		/* Too many credits. */
		MPASS(m != NULL);
		M_ASSERTPKTHDR(m);

		/* Partial credits. */
		MPASS(plen >= m->m_pkthdr.len);

		plen -= m->m_pkthdr.len;
		m_freem(m);
	}
}

void
t4_push_pdus(struct adapter *sc, struct toepcb *toep, int drop)
{
	struct mbuf *sndptr, *m;
	struct fw_ofld_tx_data_wr *txwr;
	struct wrqe *wr;
	u_int plen, nsegs, credits, max_imm, max_nsegs, max_nsegs_1mbuf;
	u_int adjusted_plen, ulp_submode;
	struct inpcb *inp = toep->inp;
	struct tcpcb *tp = intotcpcb(inp);
	int tx_credits, shove;
	struct ofld_tx_sdesc *txsd = &toep->txsd[toep->txsd_pidx];
	struct mbufq *pduq = &toep->ulp_pduq;
	static const u_int ulp_extra_len[] = {0, 4, 4, 8};

	INP_WLOCK_ASSERT(inp);
	KASSERT(toep->flags & TPF_FLOWC_WR_SENT,
	    ("%s: flowc_wr not sent for tid %u.", __func__, toep->tid));
	KASSERT(toep->ulp_mode == ULP_MODE_ISCSI,
	    ("%s: ulp_mode %u for toep %p", __func__, toep->ulp_mode, toep));

	if (__predict_false(toep->flags & TPF_ABORT_SHUTDOWN))
		return;

	/*
	 * This function doesn't resume by itself.  Someone else must clear the
	 * flag and call this function.
	 */
	if (__predict_false(toep->flags & TPF_TX_SUSPENDED)) {
		KASSERT(drop == 0,
		    ("%s: drop (%d) != 0 but tx is suspended", __func__, drop));
		return;
	}

	if (drop)
		rqdrop_locked(&toep->ulp_pdu_reclaimq, drop);

	while ((sndptr = mbufq_first(pduq)) != NULL) {
		M_ASSERTPKTHDR(sndptr);

		tx_credits = min(toep->tx_credits, MAX_OFLD_TX_CREDITS);
		max_imm = max_imm_payload(tx_credits);
		max_nsegs = max_dsgl_nsegs(tx_credits);

		plen = 0;
		nsegs = 0;
		max_nsegs_1mbuf = 0; /* max # of SGL segments in any one mbuf */
		for (m = sndptr; m != NULL; m = m->m_next) {
			int n = sglist_count(mtod(m, void *), m->m_len);

			nsegs += n;
			plen += m->m_len;

			/*
			 * This mbuf would send us _over_ the nsegs limit.
			 * Suspend tx because the PDU can't be sent out.
			 */
			if (plen > max_imm && nsegs > max_nsegs) {
				toep->flags |= TPF_TX_SUSPENDED;
				return;
			}

			if (max_nsegs_1mbuf < n)
				max_nsegs_1mbuf = n;
		}

		if (__predict_false(toep->flags & TPF_FIN_SENT))
			panic("%s: excess tx.", __func__);

		/*
		 * We have a PDU to send.  All of it goes out in one WR so 'm'
		 * is NULL.  A PDU's length is always a multiple of 4.
		 */
		MPASS(m == NULL);
		MPASS((plen & 3) == 0);
		MPASS(sndptr->m_pkthdr.len == plen);

		shove = !(tp->t_flags & TF_MORETOCOME);
		ulp_submode = mbuf_ulp_submode(sndptr);
		MPASS(ulp_submode < nitems(ulp_extra_len));

		/*
		 * plen doesn't include header and data digests, which are
		 * generated and inserted in the right places by the TOE, but
		 * they do occupy TCP sequence space and need to be accounted
		 * for.
		 */
		adjusted_plen = plen + ulp_extra_len[ulp_submode];
		if (plen <= max_imm) {

			/* Immediate data tx */

			wr = alloc_wrqe(roundup2(sizeof(*txwr) + plen, 16),
					toep->ofld_txq);
			if (wr == NULL) {
				/* XXX: how will we recover from this? */
				toep->flags |= TPF_TX_SUSPENDED;
				return;
			}
			txwr = wrtod(wr);
			credits = howmany(wr->wr_len, 16);
			write_tx_wr(txwr, toep, plen, adjusted_plen, credits,
			    shove, ulp_submode, sc->tt.tx_align);
			m_copydata(sndptr, 0, plen, (void *)(txwr + 1));
			nsegs = 0;
		} else {
			int wr_len;

			/* DSGL tx */
			wr_len = sizeof(*txwr) + sizeof(struct ulptx_sgl) +
			    ((3 * (nsegs - 1)) / 2 + ((nsegs - 1) & 1)) * 8;
			wr = alloc_wrqe(roundup2(wr_len, 16), toep->ofld_txq);
			if (wr == NULL) {
				/* XXX: how will we recover from this? */
				toep->flags |= TPF_TX_SUSPENDED;
				return;
			}
			txwr = wrtod(wr);
			credits = howmany(wr_len, 16);
			write_tx_wr(txwr, toep, 0, adjusted_plen, credits,
			    shove, ulp_submode, sc->tt.tx_align);
			write_tx_sgl(txwr + 1, sndptr, m, nsegs,
			    max_nsegs_1mbuf);
			if (wr_len & 0xf) {
				uint64_t *pad = (uint64_t *)
				    ((uintptr_t)txwr + wr_len);
				*pad = 0;
			}
		}

		KASSERT(toep->tx_credits >= credits,
			("%s: not enough credits", __func__));

		m = mbufq_dequeue(pduq);
		MPASS(m == sndptr);
		mbufq_enqueue(&toep->ulp_pdu_reclaimq, m);

		toep->tx_credits -= credits;
		toep->tx_nocompl += credits;
		toep->plen_nocompl += plen;
		if (toep->tx_credits <= toep->tx_total * 3 / 8 &&
		    toep->tx_nocompl >= toep->tx_total / 4) {
			txwr->op_to_immdlen |= htobe32(F_FW_WR_COMPL);
			toep->tx_nocompl = 0;
			toep->plen_nocompl = 0;
		}

		tp->snd_nxt += adjusted_plen;
		tp->snd_max += adjusted_plen;

		toep->flags |= TPF_TX_DATA_SENT;
		if (toep->tx_credits < MIN_OFLD_TX_CREDITS)
			toep->flags |= TPF_TX_SUSPENDED;

		KASSERT(toep->txsd_avail > 0, ("%s: no txsd", __func__));
		txsd->plen = plen;
		txsd->tx_credits = credits;
		txsd++;
		if (__predict_false(++toep->txsd_pidx == toep->txsd_total)) {
			toep->txsd_pidx = 0;
			txsd = &toep->txsd[0];
		}
		toep->txsd_avail--;

		t4_l2t_send(sc, wr, toep->l2te);
	}

	/* Send a FIN if requested, but only if there are no more PDUs to send */
	if (mbufq_first(pduq) == NULL && toep->flags & TPF_SEND_FIN)
		t4_close_conn(sc, toep);
}

int
t4_tod_output(struct toedev *tod, struct tcpcb *tp)
{
	struct adapter *sc = tod->tod_softc;
#ifdef INVARIANTS
	struct inpcb *inp = tp->t_inpcb;
#endif
	struct toepcb *toep = tp->t_toe;

	INP_WLOCK_ASSERT(inp);
	KASSERT((inp->inp_flags & INP_DROPPED) == 0,
	    ("%s: inp %p dropped.", __func__, inp));
	KASSERT(toep != NULL, ("%s: toep is NULL", __func__));

	if (toep->ulp_mode == ULP_MODE_ISCSI)
		t4_push_pdus(sc, toep, 0);
	else if (tls_tx_key(toep))
		t4_push_tls_records(sc, toep, 0);
	else
		t4_push_frames(sc, toep, 0);

	return (0);
}

int
t4_send_fin(struct toedev *tod, struct tcpcb *tp)
{
	struct adapter *sc = tod->tod_softc;
#ifdef INVARIANTS
	struct inpcb *inp = tp->t_inpcb;
#endif
	struct toepcb *toep = tp->t_toe;

	INP_WLOCK_ASSERT(inp);
	KASSERT((inp->inp_flags & INP_DROPPED) == 0,
	    ("%s: inp %p dropped.", __func__, inp));
	KASSERT(toep != NULL, ("%s: toep is NULL", __func__));

	toep->flags |= TPF_SEND_FIN;
	if (tp->t_state >= TCPS_ESTABLISHED) {
		if (toep->ulp_mode == ULP_MODE_ISCSI)
			t4_push_pdus(sc, toep, 0);
		else if (tls_tx_key(toep))
			t4_push_tls_records(sc, toep, 0);
		else
			t4_push_frames(sc, toep, 0);
	}

	return (0);
}

int
t4_send_rst(struct toedev *tod, struct tcpcb *tp)
{
	struct adapter *sc = tod->tod_softc;
#if defined(INVARIANTS)
	struct inpcb *inp = tp->t_inpcb;
#endif
	struct toepcb *toep = tp->t_toe;

	INP_WLOCK_ASSERT(inp);
	KASSERT((inp->inp_flags & INP_DROPPED) == 0,
	    ("%s: inp %p dropped.", __func__, inp));
	KASSERT(toep != NULL, ("%s: toep is NULL", __func__));

	/* hmmmm */
	KASSERT(toep->flags & TPF_FLOWC_WR_SENT,
	    ("%s: flowc for tid %u [%s] not sent already",
	    __func__, toep->tid, tcpstates[tp->t_state]));

	send_reset(sc, toep, 0);
	return (0);
}

/*
 * Peer has sent us a FIN.
 */
static int
do_peer_close(struct sge_iq *iq, const struct rss_header *rss, struct mbuf *m)
{
	struct adapter *sc = iq->adapter;
	const struct cpl_peer_close *cpl = (const void *)(rss + 1);
	unsigned int tid = GET_TID(cpl);
	struct toepcb *toep = lookup_tid(sc, tid);
	struct inpcb *inp = toep->inp;
	struct tcpcb *tp = NULL;
	struct socket *so;
	struct epoch_tracker et;
#ifdef INVARIANTS
	unsigned int opcode = G_CPL_OPCODE(be32toh(OPCODE_TID(cpl)));
#endif

	KASSERT(opcode == CPL_PEER_CLOSE,
	    ("%s: unexpected opcode 0x%x", __func__, opcode));
	KASSERT(m == NULL, ("%s: wasn't expecting payload", __func__));

	if (__predict_false(toep->flags & TPF_SYNQE)) {
		/*
		 * do_pass_establish must have run before do_peer_close and if
		 * this is still a synqe instead of a toepcb then the connection
		 * must be getting aborted.
		 */
		MPASS(toep->flags & TPF_ABORT_SHUTDOWN);
		CTR4(KTR_CXGBE, "%s: tid %u, synqe %p (0x%x)", __func__, tid,
		    toep, toep->flags);
		return (0);
	}

	KASSERT(toep->tid == tid, ("%s: toep tid mismatch", __func__));

	CURVNET_SET(toep->vnet);
	INP_INFO_RLOCK_ET(&V_tcbinfo, et);
	INP_WLOCK(inp);
	tp = intotcpcb(inp);

	CTR5(KTR_CXGBE, "%s: tid %u (%s), toep_flags 0x%x, inp %p", __func__,
	    tid, tp ? tcpstates[tp->t_state] : "no tp", toep->flags, inp);

	if (toep->flags & TPF_ABORT_SHUTDOWN)
		goto done;

	tp->rcv_nxt++;	/* FIN */

	so = inp->inp_socket;
	if (toep->ulp_mode == ULP_MODE_TCPDDP) {
		DDP_LOCK(toep);
		if (__predict_false(toep->ddp.flags &
		    (DDP_BUF0_ACTIVE | DDP_BUF1_ACTIVE)))
			handle_ddp_close(toep, tp, cpl->rcv_nxt);
		DDP_UNLOCK(toep);
	}
	socantrcvmore(so);

	if (toep->ulp_mode != ULP_MODE_RDMA) {
		KASSERT(tp->rcv_nxt == be32toh(cpl->rcv_nxt),
	    		("%s: rcv_nxt mismatch: %u %u", __func__, tp->rcv_nxt,
	    		be32toh(cpl->rcv_nxt)));
	}

	switch (tp->t_state) {
	case TCPS_SYN_RECEIVED:
		tp->t_starttime = ticks;
		/* FALLTHROUGH */ 

	case TCPS_ESTABLISHED:
		tcp_state_change(tp, TCPS_CLOSE_WAIT);
		break;

	case TCPS_FIN_WAIT_1:
		tcp_state_change(tp, TCPS_CLOSING);
		break;

	case TCPS_FIN_WAIT_2:
		tcp_twstart(tp);
		INP_UNLOCK_ASSERT(inp);	 /* safe, we have a ref on the inp */
		INP_INFO_RUNLOCK_ET(&V_tcbinfo, et);
		CURVNET_RESTORE();

		INP_WLOCK(inp);
		final_cpl_received(toep);
		return (0);

	default:
		log(LOG_ERR, "%s: TID %u received CPL_PEER_CLOSE in state %d\n",
		    __func__, tid, tp->t_state);
	}
done:
	INP_WUNLOCK(inp);
	INP_INFO_RUNLOCK_ET(&V_tcbinfo, et);
	CURVNET_RESTORE();
	return (0);
}

/*
 * Peer has ACK'd our FIN.
 */
static int
do_close_con_rpl(struct sge_iq *iq, const struct rss_header *rss,
    struct mbuf *m)
{
	struct adapter *sc = iq->adapter;
	const struct cpl_close_con_rpl *cpl = (const void *)(rss + 1);
	unsigned int tid = GET_TID(cpl);
	struct toepcb *toep = lookup_tid(sc, tid);
	struct inpcb *inp = toep->inp;
	struct tcpcb *tp = NULL;
	struct socket *so = NULL;
	struct epoch_tracker et;
#ifdef INVARIANTS
	unsigned int opcode = G_CPL_OPCODE(be32toh(OPCODE_TID(cpl)));
#endif

	KASSERT(opcode == CPL_CLOSE_CON_RPL,
	    ("%s: unexpected opcode 0x%x", __func__, opcode));
	KASSERT(m == NULL, ("%s: wasn't expecting payload", __func__));
	KASSERT(toep->tid == tid, ("%s: toep tid mismatch", __func__));

	CURVNET_SET(toep->vnet);
	INP_INFO_RLOCK_ET(&V_tcbinfo, et);
	INP_WLOCK(inp);
	tp = intotcpcb(inp);

	CTR4(KTR_CXGBE, "%s: tid %u (%s), toep_flags 0x%x",
	    __func__, tid, tp ? tcpstates[tp->t_state] : "no tp", toep->flags);

	if (toep->flags & TPF_ABORT_SHUTDOWN)
		goto done;

	so = inp->inp_socket;
	tp->snd_una = be32toh(cpl->snd_nxt) - 1;	/* exclude FIN */

	switch (tp->t_state) {
	case TCPS_CLOSING:	/* see TCPS_FIN_WAIT_2 in do_peer_close too */
		tcp_twstart(tp);
release:
		INP_UNLOCK_ASSERT(inp);	/* safe, we have a ref on the  inp */
		INP_INFO_RUNLOCK_ET(&V_tcbinfo, et);
		CURVNET_RESTORE();

		INP_WLOCK(inp);
		final_cpl_received(toep);	/* no more CPLs expected */

		return (0);
	case TCPS_LAST_ACK:
		if (tcp_close(tp))
			INP_WUNLOCK(inp);
		goto release;

	case TCPS_FIN_WAIT_1:
		if (so->so_rcv.sb_state & SBS_CANTRCVMORE)
			soisdisconnected(so);
		tcp_state_change(tp, TCPS_FIN_WAIT_2);
		break;

	default:
		log(LOG_ERR,
		    "%s: TID %u received CPL_CLOSE_CON_RPL in state %s\n",
		    __func__, tid, tcpstates[tp->t_state]);
	}
done:
	INP_WUNLOCK(inp);
	INP_INFO_RUNLOCK_ET(&V_tcbinfo, et);
	CURVNET_RESTORE();
	return (0);
}

void
send_abort_rpl(struct adapter *sc, struct sge_wrq *ofld_txq, int tid,
    int rst_status)
{
	struct wrqe *wr;
	struct cpl_abort_rpl *cpl;

	wr = alloc_wrqe(sizeof(*cpl), ofld_txq);
	if (wr == NULL) {
		/* XXX */
		panic("%s: allocation failure.", __func__);
	}
	cpl = wrtod(wr);

	INIT_TP_WR_MIT_CPL(cpl, CPL_ABORT_RPL, tid);
	cpl->cmd = rst_status;

	t4_wrq_tx(sc, wr);
}

static int
abort_status_to_errno(struct tcpcb *tp, unsigned int abort_reason)
{
	switch (abort_reason) {
	case CPL_ERR_BAD_SYN:
	case CPL_ERR_CONN_RESET:
		return (tp->t_state == TCPS_CLOSE_WAIT ? EPIPE : ECONNRESET);
	case CPL_ERR_XMIT_TIMEDOUT:
	case CPL_ERR_PERSIST_TIMEDOUT:
	case CPL_ERR_FINWAIT2_TIMEDOUT:
	case CPL_ERR_KEEPALIVE_TIMEDOUT:
		return (ETIMEDOUT);
	default:
		return (EIO);
	}
}

/*
 * TCP RST from the peer, timeout, or some other such critical error.
 */
static int
do_abort_req(struct sge_iq *iq, const struct rss_header *rss, struct mbuf *m)
{
	struct adapter *sc = iq->adapter;
	const struct cpl_abort_req_rss *cpl = (const void *)(rss + 1);
	unsigned int tid = GET_TID(cpl);
	struct toepcb *toep = lookup_tid(sc, tid);
	struct sge_wrq *ofld_txq = toep->ofld_txq;
	struct inpcb *inp;
	struct tcpcb *tp;
	struct epoch_tracker et;
#ifdef INVARIANTS
	unsigned int opcode = G_CPL_OPCODE(be32toh(OPCODE_TID(cpl)));
#endif

	KASSERT(opcode == CPL_ABORT_REQ_RSS,
	    ("%s: unexpected opcode 0x%x", __func__, opcode));
	KASSERT(m == NULL, ("%s: wasn't expecting payload", __func__));

	if (toep->flags & TPF_SYNQE)
		return (do_abort_req_synqe(iq, rss, m));

	KASSERT(toep->tid == tid, ("%s: toep tid mismatch", __func__));

	if (negative_advice(cpl->status)) {
		CTR4(KTR_CXGBE, "%s: negative advice %d for tid %d (0x%x)",
		    __func__, cpl->status, tid, toep->flags);
		return (0);	/* Ignore negative advice */
	}

	inp = toep->inp;
	CURVNET_SET(toep->vnet);
	INP_INFO_RLOCK_ET(&V_tcbinfo, et);	/* for tcp_close */
	INP_WLOCK(inp);

	tp = intotcpcb(inp);

	CTR6(KTR_CXGBE,
	    "%s: tid %d (%s), toep_flags 0x%x, inp_flags 0x%x, status %d",
	    __func__, tid, tp ? tcpstates[tp->t_state] : "no tp", toep->flags,
	    inp->inp_flags, cpl->status);

	/*
	 * If we'd initiated an abort earlier the reply to it is responsible for
	 * cleaning up resources.  Otherwise we tear everything down right here
	 * right now.  We owe the T4 a CPL_ABORT_RPL no matter what.
	 */
	if (toep->flags & TPF_ABORT_SHUTDOWN) {
		INP_WUNLOCK(inp);
		goto done;
	}
	toep->flags |= TPF_ABORT_SHUTDOWN;

	if ((inp->inp_flags & (INP_DROPPED | INP_TIMEWAIT)) == 0) {
		struct socket *so = inp->inp_socket;

		if (so != NULL)
			so_error_set(so, abort_status_to_errno(tp,
			    cpl->status));
		tp = tcp_close(tp);
		if (tp == NULL)
			INP_WLOCK(inp);	/* re-acquire */
	}

	final_cpl_received(toep);
done:
	INP_INFO_RUNLOCK_ET(&V_tcbinfo, et);
	CURVNET_RESTORE();
	send_abort_rpl(sc, ofld_txq, tid, CPL_ABORT_NO_RST);
	return (0);
}

/*
 * Reply to the CPL_ABORT_REQ (send_reset)
 */
static int
do_abort_rpl(struct sge_iq *iq, const struct rss_header *rss, struct mbuf *m)
{
	struct adapter *sc = iq->adapter;
	const struct cpl_abort_rpl_rss *cpl = (const void *)(rss + 1);
	unsigned int tid = GET_TID(cpl);
	struct toepcb *toep = lookup_tid(sc, tid);
	struct inpcb *inp = toep->inp;
#ifdef INVARIANTS
	unsigned int opcode = G_CPL_OPCODE(be32toh(OPCODE_TID(cpl)));
#endif

	KASSERT(opcode == CPL_ABORT_RPL_RSS,
	    ("%s: unexpected opcode 0x%x", __func__, opcode));
	KASSERT(m == NULL, ("%s: wasn't expecting payload", __func__));

	if (toep->flags & TPF_SYNQE)
		return (do_abort_rpl_synqe(iq, rss, m));

	KASSERT(toep->tid == tid, ("%s: toep tid mismatch", __func__));

	CTR5(KTR_CXGBE, "%s: tid %u, toep %p, inp %p, status %d",
	    __func__, tid, toep, inp, cpl->status);

	KASSERT(toep->flags & TPF_ABORT_SHUTDOWN,
	    ("%s: wasn't expecting abort reply", __func__));

	INP_WLOCK(inp);
	final_cpl_received(toep);

	return (0);
}

static int
do_rx_data(struct sge_iq *iq, const struct rss_header *rss, struct mbuf *m)
{
	struct adapter *sc = iq->adapter;
	const struct cpl_rx_data *cpl = mtod(m, const void *);
	unsigned int tid = GET_TID(cpl);
	struct toepcb *toep = lookup_tid(sc, tid);
	struct inpcb *inp = toep->inp;
	struct tcpcb *tp;
	struct socket *so;
	struct sockbuf *sb;
	struct epoch_tracker et;
	int len;
	uint32_t ddp_placed = 0;

	if (__predict_false(toep->flags & TPF_SYNQE)) {
		/*
		 * do_pass_establish must have run before do_rx_data and if this
		 * is still a synqe instead of a toepcb then the connection must
		 * be getting aborted.
		 */
		MPASS(toep->flags & TPF_ABORT_SHUTDOWN);
		CTR4(KTR_CXGBE, "%s: tid %u, synqe %p (0x%x)", __func__, tid,
		    toep, toep->flags);
		m_freem(m);
		return (0);
	}

	KASSERT(toep->tid == tid, ("%s: toep tid mismatch", __func__));

	/* strip off CPL header */
	m_adj(m, sizeof(*cpl));
	len = m->m_pkthdr.len;

	INP_WLOCK(inp);
	if (inp->inp_flags & (INP_DROPPED | INP_TIMEWAIT)) {
		CTR4(KTR_CXGBE, "%s: tid %u, rx (%d bytes), inp_flags 0x%x",
		    __func__, tid, len, inp->inp_flags);
		INP_WUNLOCK(inp);
		m_freem(m);
		return (0);
	}

	tp = intotcpcb(inp);

	if (__predict_false(tp->rcv_nxt != be32toh(cpl->seq)))
		ddp_placed = be32toh(cpl->seq) - tp->rcv_nxt;

	tp->rcv_nxt += len;
	if (tp->rcv_wnd < len) {
		KASSERT(toep->ulp_mode == ULP_MODE_RDMA,
				("%s: negative window size", __func__));
	}

	tp->rcv_wnd -= len;
	tp->t_rcvtime = ticks;

	if (toep->ulp_mode == ULP_MODE_TCPDDP)
		DDP_LOCK(toep);
	so = inp_inpcbtosocket(inp);
	sb = &so->so_rcv;
	SOCKBUF_LOCK(sb);

	if (__predict_false(sb->sb_state & SBS_CANTRCVMORE)) {
		CTR3(KTR_CXGBE, "%s: tid %u, excess rx (%d bytes)",
		    __func__, tid, len);
		m_freem(m);
		SOCKBUF_UNLOCK(sb);
		if (toep->ulp_mode == ULP_MODE_TCPDDP)
			DDP_UNLOCK(toep);
		INP_WUNLOCK(inp);

		CURVNET_SET(toep->vnet);
		INP_INFO_RLOCK_ET(&V_tcbinfo, et);
		INP_WLOCK(inp);
		tp = tcp_drop(tp, ECONNRESET);
		if (tp)
			INP_WUNLOCK(inp);
		INP_INFO_RUNLOCK_ET(&V_tcbinfo, et);
		CURVNET_RESTORE();

		return (0);
	}

	/* receive buffer autosize */
	MPASS(toep->vnet == so->so_vnet);
	CURVNET_SET(toep->vnet);
	if (sb->sb_flags & SB_AUTOSIZE &&
	    V_tcp_do_autorcvbuf &&
	    sb->sb_hiwat < V_tcp_autorcvbuf_max &&
	    len > (sbspace(sb) / 8 * 7)) {
		unsigned int hiwat = sb->sb_hiwat;
		unsigned int newsize = min(hiwat + V_tcp_autorcvbuf_inc,
		    V_tcp_autorcvbuf_max);

		if (!sbreserve_locked(sb, newsize, so, NULL))
			sb->sb_flags &= ~SB_AUTOSIZE;
		else
			toep->rx_credits += newsize - hiwat;
	}

	if (toep->ulp_mode == ULP_MODE_TCPDDP) {
		int changed = !(toep->ddp.flags & DDP_ON) ^ cpl->ddp_off;

		if (toep->ddp.waiting_count != 0 || toep->ddp.active_count != 0)
			CTR3(KTR_CXGBE, "%s: tid %u, non-ddp rx (%d bytes)",
			    __func__, tid, len);

		if (changed) {
			if (toep->ddp.flags & DDP_SC_REQ)
				toep->ddp.flags ^= DDP_ON | DDP_SC_REQ;
			else {
				KASSERT(cpl->ddp_off == 1,
				    ("%s: DDP switched on by itself.",
				    __func__));

				/* Fell out of DDP mode */
				toep->ddp.flags &= ~DDP_ON;
				CTR1(KTR_CXGBE, "%s: fell out of DDP mode",
				    __func__);

				insert_ddp_data(toep, ddp_placed);
			}
		}

		if (toep->ddp.flags & DDP_ON) {
			/*
			 * CPL_RX_DATA with DDP on can only be an indicate.
			 * Start posting queued AIO requests via DDP.  The
			 * payload that arrived in this indicate is appended
			 * to the socket buffer as usual.
			 */
			handle_ddp_indicate(toep);
		}
	}

	KASSERT(toep->sb_cc >= sbused(sb),
	    ("%s: sb %p has more data (%d) than last time (%d).",
	    __func__, sb, sbused(sb), toep->sb_cc));
	toep->rx_credits += toep->sb_cc - sbused(sb);
	sbappendstream_locked(sb, m, 0);
	toep->sb_cc = sbused(sb);
	if (toep->rx_credits > 0 && toep->sb_cc + tp->rcv_wnd < sb->sb_lowat) {
		int credits;

		credits = send_rx_credits(sc, toep, toep->rx_credits);
		toep->rx_credits -= credits;
		tp->rcv_wnd += credits;
		tp->rcv_adv += credits;
	}

	if (toep->ulp_mode == ULP_MODE_TCPDDP && toep->ddp.waiting_count > 0 &&
	    sbavail(sb) != 0) {
		CTR2(KTR_CXGBE, "%s: tid %u queueing AIO task", __func__,
		    tid);
		ddp_queue_toep(toep);
	}
	sorwakeup_locked(so);
	SOCKBUF_UNLOCK_ASSERT(sb);
	if (toep->ulp_mode == ULP_MODE_TCPDDP)
		DDP_UNLOCK(toep);

	INP_WUNLOCK(inp);
	CURVNET_RESTORE();
	return (0);
}

static int
do_fw4_ack(struct sge_iq *iq, const struct rss_header *rss, struct mbuf *m)
{
	struct adapter *sc = iq->adapter;
	const struct cpl_fw4_ack *cpl = (const void *)(rss + 1);
	unsigned int tid = G_CPL_FW4_ACK_FLOWID(be32toh(OPCODE_TID(cpl)));
	struct toepcb *toep = lookup_tid(sc, tid);
	struct inpcb *inp;
	struct tcpcb *tp;
	struct socket *so;
	uint8_t credits = cpl->credits;
	struct ofld_tx_sdesc *txsd;
	int plen;
#ifdef INVARIANTS
	unsigned int opcode = G_CPL_FW4_ACK_OPCODE(be32toh(OPCODE_TID(cpl)));
#endif

	/*
	 * Very unusual case: we'd sent a flowc + abort_req for a synq entry and
	 * now this comes back carrying the credits for the flowc.
	 */
	if (__predict_false(toep->flags & TPF_SYNQE)) {
		KASSERT(toep->flags & TPF_ABORT_SHUTDOWN,
		    ("%s: credits for a synq entry %p", __func__, toep));
		return (0);
	}

	inp = toep->inp;

	KASSERT(opcode == CPL_FW4_ACK,
	    ("%s: unexpected opcode 0x%x", __func__, opcode));
	KASSERT(m == NULL, ("%s: wasn't expecting payload", __func__));
	KASSERT(toep->tid == tid, ("%s: toep tid mismatch", __func__));

	INP_WLOCK(inp);

	if (__predict_false(toep->flags & TPF_ABORT_SHUTDOWN)) {
		INP_WUNLOCK(inp);
		return (0);
	}

	KASSERT((inp->inp_flags & (INP_TIMEWAIT | INP_DROPPED)) == 0,
	    ("%s: inp_flags 0x%x", __func__, inp->inp_flags));

	tp = intotcpcb(inp);

	if (cpl->flags & CPL_FW4_ACK_FLAGS_SEQVAL) {
		tcp_seq snd_una = be32toh(cpl->snd_una);

#ifdef INVARIANTS
		if (__predict_false(SEQ_LT(snd_una, tp->snd_una))) {
			log(LOG_ERR,
			    "%s: unexpected seq# %x for TID %u, snd_una %x\n",
			    __func__, snd_una, toep->tid, tp->snd_una);
		}
#endif

		if (tp->snd_una != snd_una) {
			tp->snd_una = snd_una;
			tp->ts_recent_age = tcp_ts_getticks();
		}
	}

#ifdef VERBOSE_TRACES
	CTR3(KTR_CXGBE, "%s: tid %d credits %u", __func__, tid, credits);
#endif
	so = inp->inp_socket;
	txsd = &toep->txsd[toep->txsd_cidx];
	plen = 0;
	while (credits) {
		KASSERT(credits >= txsd->tx_credits,
		    ("%s: too many (or partial) credits", __func__));
		credits -= txsd->tx_credits;
		toep->tx_credits += txsd->tx_credits;
		plen += txsd->plen;
		if (txsd->iv_buffer) {
			free(txsd->iv_buffer, M_CXGBE);
			txsd->iv_buffer = NULL;
		}
		txsd++;
		toep->txsd_avail++;
		KASSERT(toep->txsd_avail <= toep->txsd_total,
		    ("%s: txsd avail > total", __func__));
		if (__predict_false(++toep->txsd_cidx == toep->txsd_total)) {
			txsd = &toep->txsd[0];
			toep->txsd_cidx = 0;
		}
	}

	if (toep->tx_credits == toep->tx_total) {
		toep->tx_nocompl = 0;
		toep->plen_nocompl = 0;
	}

	if (toep->flags & TPF_TX_SUSPENDED &&
	    toep->tx_credits >= toep->tx_total / 4) {
#ifdef VERBOSE_TRACES
		CTR2(KTR_CXGBE, "%s: tid %d calling t4_push_frames", __func__,
		    tid);
#endif
		toep->flags &= ~TPF_TX_SUSPENDED;
		CURVNET_SET(toep->vnet);
		if (toep->ulp_mode == ULP_MODE_ISCSI)
			t4_push_pdus(sc, toep, plen);
		else if (tls_tx_key(toep))
			t4_push_tls_records(sc, toep, plen);
		else
			t4_push_frames(sc, toep, plen);
		CURVNET_RESTORE();
	} else if (plen > 0) {
		struct sockbuf *sb = &so->so_snd;
		int sbu;

		SOCKBUF_LOCK(sb);
		sbu = sbused(sb);
		if (toep->ulp_mode == ULP_MODE_ISCSI) {

			if (__predict_false(sbu > 0)) {
				/*
				 * The data trasmitted before the tid's ULP mode
				 * changed to ISCSI is still in so_snd.
				 * Incoming credits should account for so_snd
				 * first.
				 */
				sbdrop_locked(sb, min(sbu, plen));
				plen -= min(sbu, plen);
			}
			sowwakeup_locked(so);	/* unlocks so_snd */
			rqdrop_locked(&toep->ulp_pdu_reclaimq, plen);
		} else {
#ifdef VERBOSE_TRACES
			CTR3(KTR_CXGBE, "%s: tid %d dropped %d bytes", __func__,
			    tid, plen);
#endif
			sbdrop_locked(sb, plen);
			if (tls_tx_key(toep)) {
				struct tls_ofld_info *tls_ofld = &toep->tls;

				MPASS(tls_ofld->sb_off >= plen);
				tls_ofld->sb_off -= plen;
			}
			if (!TAILQ_EMPTY(&toep->aiotx_jobq))
				t4_aiotx_queue_toep(toep);
			sowwakeup_locked(so);	/* unlocks so_snd */
		}
		SOCKBUF_UNLOCK_ASSERT(sb);
	}

	INP_WUNLOCK(inp);

	return (0);
}

void
t4_set_tcb_field(struct adapter *sc, struct sge_wrq *wrq, struct toepcb *toep,
    uint16_t word, uint64_t mask, uint64_t val, int reply, int cookie)
{
	struct wrqe *wr;
	struct cpl_set_tcb_field *req;
	struct ofld_tx_sdesc *txsd;

	MPASS((cookie & ~M_COOKIE) == 0);
	if (reply) {
		MPASS(cookie != CPL_COOKIE_RESERVED);
	}

	wr = alloc_wrqe(sizeof(*req), wrq);
	if (wr == NULL) {
		/* XXX */
		panic("%s: allocation failure.", __func__);
	}
	req = wrtod(wr);

	INIT_TP_WR_MIT_CPL(req, CPL_SET_TCB_FIELD, toep->tid);
	req->reply_ctrl = htobe16(V_QUEUENO(toep->ofld_rxq->iq.abs_id));
	if (reply == 0)
		req->reply_ctrl |= htobe16(F_NO_REPLY);
	req->word_cookie = htobe16(V_WORD(word) | V_COOKIE(cookie));
	req->mask = htobe64(mask);
	req->val = htobe64(val);
	if ((wrq->eq.flags & EQ_TYPEMASK) == EQ_OFLD) {
		txsd = &toep->txsd[toep->txsd_pidx];
		txsd->tx_credits = howmany(sizeof(*req), 16);
		txsd->plen = 0;
		KASSERT(toep->tx_credits >= txsd->tx_credits &&
		    toep->txsd_avail > 0,
		    ("%s: not enough credits (%d)", __func__,
		    toep->tx_credits));
		toep->tx_credits -= txsd->tx_credits;
		if (__predict_false(++toep->txsd_pidx == toep->txsd_total))
			toep->txsd_pidx = 0;
		toep->txsd_avail--;
	}

	t4_wrq_tx(sc, wr);
}

void
t4_init_cpl_io_handlers(void)
{

	t4_register_cpl_handler(CPL_PEER_CLOSE, do_peer_close);
	t4_register_cpl_handler(CPL_CLOSE_CON_RPL, do_close_con_rpl);
	t4_register_cpl_handler(CPL_ABORT_REQ_RSS, do_abort_req);
	t4_register_shared_cpl_handler(CPL_ABORT_RPL_RSS, do_abort_rpl,
	    CPL_COOKIE_TOM);
	t4_register_cpl_handler(CPL_RX_DATA, do_rx_data);
	t4_register_shared_cpl_handler(CPL_FW4_ACK, do_fw4_ack, CPL_COOKIE_TOM);
}

void
t4_uninit_cpl_io_handlers(void)
{

	t4_register_cpl_handler(CPL_PEER_CLOSE, NULL);
	t4_register_cpl_handler(CPL_CLOSE_CON_RPL, NULL);
	t4_register_cpl_handler(CPL_ABORT_REQ_RSS, NULL);
	t4_register_shared_cpl_handler(CPL_ABORT_RPL_RSS, NULL, CPL_COOKIE_TOM);
	t4_register_cpl_handler(CPL_RX_DATA, NULL);
	t4_register_shared_cpl_handler(CPL_FW4_ACK, NULL, CPL_COOKIE_TOM);
}

/*
 * Use the 'backend3' field in AIO jobs to store the amount of data
 * sent by the AIO job so far and the 'backend4' field to hold an
 * error that should be reported when the job is completed.
 */
#define	aio_sent	backend3
#define	aio_error	backend4

#define	jobtotid(job)							\
	(((struct toepcb *)(so_sototcpcb((job)->fd_file->f_data)->t_toe))->tid)
	
static void
free_aiotx_buffer(struct aiotx_buffer *ab)
{
	struct kaiocb *job;
	long status;
	int error;

	if (refcount_release(&ab->refcount) == 0)
		return;

	job = ab->job;
	error = job->aio_error;
	status = job->aio_sent;
	vm_page_unhold_pages(ab->ps.pages, ab->ps.npages);
	free(ab, M_CXGBE);
#ifdef VERBOSE_TRACES
	CTR5(KTR_CXGBE, "%s: tid %d completed %p len %ld, error %d", __func__,
	    jobtotid(job), job, status, error);
#endif
	if (error == ECANCELED && status != 0)
		error = 0;
	if (error == ECANCELED)
		aio_cancel(job);
	else if (error)
		aio_complete(job, -1, error);
	else
		aio_complete(job, status, 0);
}

static void
t4_aiotx_mbuf_free(struct mbuf *m)
{
	struct aiotx_buffer *ab = m->m_ext.ext_arg1;

#ifdef VERBOSE_TRACES
	CTR3(KTR_CXGBE, "%s: completed %d bytes for tid %d", __func__,
	    m->m_len, jobtotid(ab->job));
#endif
	free_aiotx_buffer(ab);
}

/*
 * Hold the buffer backing an AIO request and return an AIO transmit
 * buffer.
 */
static int
hold_aio(struct kaiocb *job)
{
	struct aiotx_buffer *ab;
	struct vmspace *vm;
	vm_map_t map;
	vm_offset_t start, end, pgoff;
	int n;

	MPASS(job->backend1 == NULL);

	/*
	 * The AIO subsystem will cancel and drain all requests before
	 * permitting a process to exit or exec, so p_vmspace should
	 * be stable here.
	 */
	vm = job->userproc->p_vmspace;
	map = &vm->vm_map;
	start = (uintptr_t)job->uaiocb.aio_buf;
	pgoff = start & PAGE_MASK;
	end = round_page(start + job->uaiocb.aio_nbytes);
	start = trunc_page(start);
	n = atop(end - start);

	ab = malloc(sizeof(*ab) + n * sizeof(vm_page_t), M_CXGBE, M_WAITOK |
	    M_ZERO);
	refcount_init(&ab->refcount, 1);
	ab->ps.pages = (vm_page_t *)(ab + 1);
	ab->ps.npages = vm_fault_quick_hold_pages(map, start, end - start,
	    VM_PROT_WRITE, ab->ps.pages, n);
	if (ab->ps.npages < 0) {
		free(ab, M_CXGBE);
		return (EFAULT);
	}

	KASSERT(ab->ps.npages == n,
	    ("hold_aio: page count mismatch: %d vs %d", ab->ps.npages, n));

	ab->ps.offset = pgoff;
	ab->ps.len = job->uaiocb.aio_nbytes;
	ab->job = job;
	job->backend1 = ab;
#ifdef VERBOSE_TRACES
	CTR5(KTR_CXGBE, "%s: tid %d, new pageset %p for job %p, npages %d",
	    __func__, jobtotid(job), &ab->ps, job, ab->ps.npages);
#endif
	return (0);
}

static void
t4_aiotx_process_job(struct toepcb *toep, struct socket *so, struct kaiocb *job)
{
	struct adapter *sc;
	struct sockbuf *sb;
	struct file *fp;
	struct aiotx_buffer *ab;
	struct inpcb *inp;
	struct tcpcb *tp;
	struct mbuf *m;
	int error;
	bool moretocome, sendmore;

	sc = td_adapter(toep->td);
	sb = &so->so_snd;
	SOCKBUF_UNLOCK(sb);
	fp = job->fd_file;
	ab = job->backend1;
	m = NULL;

#ifdef MAC
	error = mac_socket_check_send(fp->f_cred, so);
	if (error != 0)
		goto out;
#endif

	if (ab == NULL) {
		error = hold_aio(job);
		if (error != 0)
			goto out;
		ab = job->backend1;
	}

	/* Inline sosend_generic(). */

	job->msgsnd = 1;

	error = sblock(sb, SBL_WAIT);
	MPASS(error == 0);

sendanother:
	m = m_get(M_WAITOK, MT_DATA);

	SOCKBUF_LOCK(sb);
	if (so->so_snd.sb_state & SBS_CANTSENDMORE) {
		SOCKBUF_UNLOCK(sb);
		sbunlock(sb);
		if ((so->so_options & SO_NOSIGPIPE) == 0) {
			PROC_LOCK(job->userproc);
			kern_psignal(job->userproc, SIGPIPE);
			PROC_UNLOCK(job->userproc);
		}
		error = EPIPE;
		goto out;
	}
	if (so->so_error) {
		error = so->so_error;
		so->so_error = 0;
		SOCKBUF_UNLOCK(sb);
		sbunlock(sb);
		goto out;
	}
	if ((so->so_state & SS_ISCONNECTED) == 0) {
		SOCKBUF_UNLOCK(sb);
		sbunlock(sb);
		error = ENOTCONN;
		goto out;
	}
	if (sbspace(sb) < sb->sb_lowat) {
		MPASS(job->aio_sent == 0 || !(so->so_state & SS_NBIO));

		/*
		 * Don't block if there is too little room in the socket
		 * buffer.  Instead, requeue the request.
		 */
		if (!aio_set_cancel_function(job, t4_aiotx_cancel)) {
			SOCKBUF_UNLOCK(sb);
			sbunlock(sb);
			error = ECANCELED;
			goto out;
		}
		TAILQ_INSERT_HEAD(&toep->aiotx_jobq, job, list);
		SOCKBUF_UNLOCK(sb);
		sbunlock(sb);
		goto out;
	}

	/*
	 * Write as much data as the socket permits, but no more than a
	 * a single sndbuf at a time.
	 */
	m->m_len = sbspace(sb);
	if (m->m_len > ab->ps.len - job->aio_sent) {
		m->m_len = ab->ps.len - job->aio_sent;
		moretocome = false;
	} else
		moretocome = true;
	if (m->m_len > sc->tt.sndbuf) {
		m->m_len = sc->tt.sndbuf;
		sendmore = true;
	} else
		sendmore = false;

	if (!TAILQ_EMPTY(&toep->aiotx_jobq))
		moretocome = true;
	SOCKBUF_UNLOCK(sb);
	MPASS(m->m_len != 0);

	/* Inlined tcp_usr_send(). */

	inp = toep->inp;
	INP_WLOCK(inp);
	if (inp->inp_flags & (INP_TIMEWAIT | INP_DROPPED)) {
		INP_WUNLOCK(inp);
		sbunlock(sb);
		error = ECONNRESET;
		goto out;
	}

	refcount_acquire(&ab->refcount);
	m_extadd(m, NULL, ab->ps.len, t4_aiotx_mbuf_free, ab,
	    (void *)(uintptr_t)job->aio_sent, 0, EXT_NET_DRV);
	m->m_ext.ext_flags |= EXT_FLAG_AIOTX;
	job->aio_sent += m->m_len;
	
	sbappendstream(sb, m, 0);
	m = NULL;

	if (!(inp->inp_flags & INP_DROPPED)) {
		tp = intotcpcb(inp);
		if (moretocome)
			tp->t_flags |= TF_MORETOCOME;
		error = tp->t_fb->tfb_tcp_output(tp);
		if (moretocome)
			tp->t_flags &= ~TF_MORETOCOME;
	}

	INP_WUNLOCK(inp);
	if (sendmore)
		goto sendanother;
	sbunlock(sb);

	if (error)
		goto out;

	/*
	 * If this is a non-blocking socket and the request has not
	 * been fully completed, requeue it until the socket is ready
	 * again.
	 */
	if (job->aio_sent < job->uaiocb.aio_nbytes &&
	    !(so->so_state & SS_NBIO)) {
		SOCKBUF_LOCK(sb);
		if (!aio_set_cancel_function(job, t4_aiotx_cancel)) {
			SOCKBUF_UNLOCK(sb);
			error = ECANCELED;
			goto out;
		}
		TAILQ_INSERT_HEAD(&toep->aiotx_jobq, job, list);
		return;
	}

	/*
	 * If the request will not be requeued, drop a reference on
	 * the aiotx buffer.  Any mbufs in flight should still
	 * contain a reference, but this drops the reference that the
	 * job owns while it is waiting to queue mbufs to the socket.
	 */
	free_aiotx_buffer(ab);

out:
	if (error) {
		if (ab != NULL) {
			job->aio_error = error;
			free_aiotx_buffer(ab);
		} else {
			MPASS(job->aio_sent == 0);
			aio_complete(job, -1, error);
		}
	}
	if (m != NULL)
		m_free(m);
	SOCKBUF_LOCK(sb);
}

static void
t4_aiotx_task(void *context, int pending)
{
	struct toepcb *toep = context;
	struct inpcb *inp = toep->inp;
	struct socket *so = inp->inp_socket;
	struct kaiocb *job;

	CURVNET_SET(toep->vnet);
	SOCKBUF_LOCK(&so->so_snd);
	while (!TAILQ_EMPTY(&toep->aiotx_jobq) && sowriteable(so)) {
		job = TAILQ_FIRST(&toep->aiotx_jobq);
		TAILQ_REMOVE(&toep->aiotx_jobq, job, list);
		if (!aio_clear_cancel_function(job))
			continue;

		t4_aiotx_process_job(toep, so, job);
	}
	toep->aiotx_task_active = false;
	SOCKBUF_UNLOCK(&so->so_snd);
	CURVNET_RESTORE();

	free_toepcb(toep);
}

static void
t4_aiotx_queue_toep(struct toepcb *toep)
{

	SOCKBUF_LOCK_ASSERT(&toep->inp->inp_socket->so_snd);
#ifdef VERBOSE_TRACES
	CTR3(KTR_CXGBE, "%s: queueing aiotx task for tid %d, active = %s",
	    __func__, toep->tid, toep->aiotx_task_active ? "true" : "false");
#endif
	if (toep->aiotx_task_active)
		return;
	toep->aiotx_task_active = true;
	hold_toepcb(toep);
	soaio_enqueue(&toep->aiotx_task);
}

static void
t4_aiotx_cancel(struct kaiocb *job)
{
	struct aiotx_buffer *ab;
	struct socket *so;
	struct sockbuf *sb;
	struct tcpcb *tp;
	struct toepcb *toep;

	so = job->fd_file->f_data;
	tp = so_sototcpcb(so);
	toep = tp->t_toe;
	MPASS(job->uaiocb.aio_lio_opcode == LIO_WRITE);
	sb = &so->so_snd;

	SOCKBUF_LOCK(sb);
	if (!aio_cancel_cleared(job))
		TAILQ_REMOVE(&toep->aiotx_jobq, job, list);
	SOCKBUF_UNLOCK(sb);

	ab = job->backend1;
	if (ab != NULL)
		free_aiotx_buffer(ab);
	else
		aio_cancel(job);
}

int
t4_aio_queue_aiotx(struct socket *so, struct kaiocb *job)
{
	struct tcpcb *tp = so_sototcpcb(so);
	struct toepcb *toep = tp->t_toe;
	struct adapter *sc = td_adapter(toep->td);

	/* This only handles writes. */
	if (job->uaiocb.aio_lio_opcode != LIO_WRITE)
		return (EOPNOTSUPP);

	if (!sc->tt.tx_zcopy)
		return (EOPNOTSUPP);

	if (tls_tx_key(toep))
		return (EOPNOTSUPP);

	SOCKBUF_LOCK(&so->so_snd);
#ifdef VERBOSE_TRACES
	CTR2(KTR_CXGBE, "%s: queueing %p", __func__, job);
#endif
	if (!aio_set_cancel_function(job, t4_aiotx_cancel))
		panic("new job was cancelled");
	TAILQ_INSERT_TAIL(&toep->aiotx_jobq, job, list);
	if (sowriteable(so))
		t4_aiotx_queue_toep(toep);
	SOCKBUF_UNLOCK(&so->so_snd);
	return (0);
}

void
aiotx_init_toep(struct toepcb *toep)
{

	TAILQ_INIT(&toep->aiotx_jobq);
	TASK_INIT(&toep->aiotx_task, 0, t4_aiotx_task, toep);
}
#endif
