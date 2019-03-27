/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2012 Chelsio Communications, Inc.
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

#include <sys/param.h>
#include <sys/types.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/ktr.h>
#include <sys/lock.h>
#include <sys/limits.h>
#include <sys/module.h>
#include <sys/protosw.h>
#include <sys/domain.h>
#include <sys/refcount.h>
#include <sys/rmlock.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/taskqueue.h>
#include <net/if.h>
#include <net/if_var.h>
#include <net/if_types.h>
#include <net/if_vlan_var.h>
#include <netinet/in.h>
#include <netinet/in_pcb.h>
#include <netinet/in_var.h>
#include <netinet/ip.h>
#include <netinet/ip6.h>
#include <netinet6/scope6_var.h>
#define TCPSTATES
#include <netinet/tcp_fsm.h>
#include <netinet/tcp_timer.h>
#include <netinet/tcp_var.h>
#include <netinet/toecore.h>

#ifdef TCP_OFFLOAD
#include "common/common.h"
#include "common/t4_msg.h"
#include "common/t4_regs.h"
#include "common/t4_regs_values.h"
#include "common/t4_tcb.h"
#include "t4_clip.h"
#include "tom/t4_tom_l2t.h"
#include "tom/t4_tom.h"
#include "tom/t4_tls.h"

static struct protosw toe_protosw;
static struct pr_usrreqs toe_usrreqs;

static struct protosw toe6_protosw;
static struct pr_usrreqs toe6_usrreqs;

/* Module ops */
static int t4_tom_mod_load(void);
static int t4_tom_mod_unload(void);
static int t4_tom_modevent(module_t, int, void *);

/* ULD ops and helpers */
static int t4_tom_activate(struct adapter *);
static int t4_tom_deactivate(struct adapter *);

static struct uld_info tom_uld_info = {
	.uld_id = ULD_TOM,
	.activate = t4_tom_activate,
	.deactivate = t4_tom_deactivate,
};

static void release_offload_resources(struct toepcb *);
static int alloc_tid_tabs(struct tid_info *);
static void free_tid_tabs(struct tid_info *);
static void free_tom_data(struct adapter *, struct tom_data *);
static void reclaim_wr_resources(void *, int);

struct toepcb *
alloc_toepcb(struct vi_info *vi, int txqid, int rxqid, int flags)
{
	struct port_info *pi = vi->pi;
	struct adapter *sc = pi->adapter;
	struct toepcb *toep;
	int tx_credits, txsd_total, len;

	/*
	 * The firmware counts tx work request credits in units of 16 bytes
	 * each.  Reserve room for an ABORT_REQ so the driver never has to worry
	 * about tx credits if it wants to abort a connection.
	 */
	tx_credits = sc->params.ofldq_wr_cred;
	tx_credits -= howmany(sizeof(struct cpl_abort_req), 16);

	/*
	 * Shortest possible tx work request is a fw_ofld_tx_data_wr + 1 byte
	 * immediate payload, and firmware counts tx work request credits in
	 * units of 16 byte.  Calculate the maximum work requests possible.
	 */
	txsd_total = tx_credits /
	    howmany(sizeof(struct fw_ofld_tx_data_wr) + 1, 16);

	KASSERT(txqid >= vi->first_ofld_txq &&
	    txqid < vi->first_ofld_txq + vi->nofldtxq,
	    ("%s: txqid %d for vi %p (first %d, n %d)", __func__, txqid, vi,
		vi->first_ofld_txq, vi->nofldtxq));

	KASSERT(rxqid >= vi->first_ofld_rxq &&
	    rxqid < vi->first_ofld_rxq + vi->nofldrxq,
	    ("%s: rxqid %d for vi %p (first %d, n %d)", __func__, rxqid, vi,
		vi->first_ofld_rxq, vi->nofldrxq));

	len = offsetof(struct toepcb, txsd) +
	    txsd_total * sizeof(struct ofld_tx_sdesc);

	toep = malloc(len, M_CXGBE, M_ZERO | flags);
	if (toep == NULL)
		return (NULL);

	refcount_init(&toep->refcount, 1);
	toep->td = sc->tom_softc;
	toep->vi = vi;
	toep->tc_idx = -1;
	toep->tx_total = tx_credits;
	toep->tx_credits = tx_credits;
	toep->ofld_txq = &sc->sge.ofld_txq[txqid];
	toep->ofld_rxq = &sc->sge.ofld_rxq[rxqid];
	toep->ctrlq = &sc->sge.ctrlq[pi->port_id];
	mbufq_init(&toep->ulp_pduq, INT_MAX);
	mbufq_init(&toep->ulp_pdu_reclaimq, INT_MAX);
	toep->txsd_total = txsd_total;
	toep->txsd_avail = txsd_total;
	toep->txsd_pidx = 0;
	toep->txsd_cidx = 0;
	aiotx_init_toep(toep);

	return (toep);
}

struct toepcb *
hold_toepcb(struct toepcb *toep)
{

	refcount_acquire(&toep->refcount);
	return (toep);
}

void
free_toepcb(struct toepcb *toep)
{

	if (refcount_release(&toep->refcount) == 0)
		return;

	KASSERT(!(toep->flags & TPF_ATTACHED),
	    ("%s: attached to an inpcb", __func__));
	KASSERT(!(toep->flags & TPF_CPL_PENDING),
	    ("%s: CPL pending", __func__));

	if (toep->ulp_mode == ULP_MODE_TCPDDP)
		ddp_uninit_toep(toep);
	tls_uninit_toep(toep);
	free(toep, M_CXGBE);
}

/*
 * Set up the socket for TCP offload.
 */
void
offload_socket(struct socket *so, struct toepcb *toep)
{
	struct tom_data *td = toep->td;
	struct inpcb *inp = sotoinpcb(so);
	struct tcpcb *tp = intotcpcb(inp);
	struct sockbuf *sb;

	INP_WLOCK_ASSERT(inp);

	/* Update socket */
	sb = &so->so_snd;
	SOCKBUF_LOCK(sb);
	sb->sb_flags |= SB_NOCOALESCE;
	SOCKBUF_UNLOCK(sb);
	sb = &so->so_rcv;
	SOCKBUF_LOCK(sb);
	sb->sb_flags |= SB_NOCOALESCE;
	if (inp->inp_vflag & INP_IPV6)
		so->so_proto = &toe6_protosw;
	else
		so->so_proto = &toe_protosw;
	SOCKBUF_UNLOCK(sb);

	/* Update TCP PCB */
	tp->tod = &td->tod;
	tp->t_toe = toep;
	tp->t_flags |= TF_TOE;

	/* Install an extra hold on inp */
	toep->inp = inp;
	toep->flags |= TPF_ATTACHED;
	in_pcbref(inp);

	/* Add the TOE PCB to the active list */
	mtx_lock(&td->toep_list_lock);
	TAILQ_INSERT_HEAD(&td->toep_list, toep, link);
	mtx_unlock(&td->toep_list_lock);
}

/* This is _not_ the normal way to "unoffload" a socket. */
void
undo_offload_socket(struct socket *so)
{
	struct inpcb *inp = sotoinpcb(so);
	struct tcpcb *tp = intotcpcb(inp);
	struct toepcb *toep = tp->t_toe;
	struct tom_data *td = toep->td;
	struct sockbuf *sb;

	INP_WLOCK_ASSERT(inp);

	sb = &so->so_snd;
	SOCKBUF_LOCK(sb);
	sb->sb_flags &= ~SB_NOCOALESCE;
	SOCKBUF_UNLOCK(sb);
	sb = &so->so_rcv;
	SOCKBUF_LOCK(sb);
	sb->sb_flags &= ~SB_NOCOALESCE;
	SOCKBUF_UNLOCK(sb);

	tp->tod = NULL;
	tp->t_toe = NULL;
	tp->t_flags &= ~TF_TOE;

	toep->inp = NULL;
	toep->flags &= ~TPF_ATTACHED;
	if (in_pcbrele_wlocked(inp))
		panic("%s: inp freed.", __func__);

	mtx_lock(&td->toep_list_lock);
	TAILQ_REMOVE(&td->toep_list, toep, link);
	mtx_unlock(&td->toep_list_lock);
}

static void
release_offload_resources(struct toepcb *toep)
{
	struct tom_data *td = toep->td;
	struct adapter *sc = td_adapter(td);
	int tid = toep->tid;

	KASSERT(!(toep->flags & TPF_CPL_PENDING),
	    ("%s: %p has CPL pending.", __func__, toep));
	KASSERT(!(toep->flags & TPF_ATTACHED),
	    ("%s: %p is still attached.", __func__, toep));

	CTR5(KTR_CXGBE, "%s: toep %p (tid %d, l2te %p, ce %p)",
	    __func__, toep, tid, toep->l2te, toep->ce);

	/*
	 * These queues should have been emptied at approximately the same time
	 * that a normal connection's socket's so_snd would have been purged or
	 * drained.  Do _not_ clean up here.
	 */
	MPASS(mbufq_len(&toep->ulp_pduq) == 0);
	MPASS(mbufq_len(&toep->ulp_pdu_reclaimq) == 0);
#ifdef INVARIANTS
	if (toep->ulp_mode == ULP_MODE_TCPDDP)
		ddp_assert_empty(toep);
#endif

	if (toep->l2te)
		t4_l2t_release(toep->l2te);

	if (tid >= 0) {
		remove_tid(sc, tid, toep->ce ? 2 : 1);
		release_tid(sc, tid, toep->ctrlq);
	}

	if (toep->ce)
		t4_release_lip(sc, toep->ce);

	if (toep->tc_idx != -1)
		t4_release_cl_rl(sc, toep->vi->pi->port_id, toep->tc_idx);

	mtx_lock(&td->toep_list_lock);
	TAILQ_REMOVE(&td->toep_list, toep, link);
	mtx_unlock(&td->toep_list_lock);

	free_toepcb(toep);
}

/*
 * The kernel is done with the TCP PCB and this is our opportunity to unhook the
 * toepcb hanging off of it.  If the TOE driver is also done with the toepcb (no
 * pending CPL) then it is time to release all resources tied to the toepcb.
 *
 * Also gets called when an offloaded active open fails and the TOM wants the
 * kernel to take the TCP PCB back.
 */
static void
t4_pcb_detach(struct toedev *tod __unused, struct tcpcb *tp)
{
#if defined(KTR) || defined(INVARIANTS)
	struct inpcb *inp = tp->t_inpcb;
#endif
	struct toepcb *toep = tp->t_toe;

	INP_WLOCK_ASSERT(inp);

	KASSERT(toep != NULL, ("%s: toep is NULL", __func__));
	KASSERT(toep->flags & TPF_ATTACHED,
	    ("%s: not attached", __func__));

#ifdef KTR
	if (tp->t_state == TCPS_SYN_SENT) {
		CTR6(KTR_CXGBE, "%s: atid %d, toep %p (0x%x), inp %p (0x%x)",
		    __func__, toep->tid, toep, toep->flags, inp,
		    inp->inp_flags);
	} else {
		CTR6(KTR_CXGBE,
		    "t4_pcb_detach: tid %d (%s), toep %p (0x%x), inp %p (0x%x)",
		    toep->tid, tcpstates[tp->t_state], toep, toep->flags, inp,
		    inp->inp_flags);
	}
#endif

	tp->t_toe = NULL;
	tp->t_flags &= ~TF_TOE;
	toep->flags &= ~TPF_ATTACHED;

	if (!(toep->flags & TPF_CPL_PENDING))
		release_offload_resources(toep);
}

/*
 * setsockopt handler.
 */
static void
t4_ctloutput(struct toedev *tod, struct tcpcb *tp, int dir, int name)
{
	struct adapter *sc = tod->tod_softc;
	struct toepcb *toep = tp->t_toe;

	if (dir == SOPT_GET)
		return;

	CTR4(KTR_CXGBE, "%s: tp %p, dir %u, name %u", __func__, tp, dir, name);

	switch (name) {
	case TCP_NODELAY:
		if (tp->t_state != TCPS_ESTABLISHED)
			break;
		t4_set_tcb_field(sc, toep->ctrlq, toep, W_TCB_T_FLAGS,
		    V_TF_NAGLE(1), V_TF_NAGLE(tp->t_flags & TF_NODELAY ? 0 : 1),
		    0, 0);
		break;
	default:
		break;
	}
}

static inline int
get_tcb_bit(u_char *tcb, int bit)
{
	int ix, shift;

	ix = 127 - (bit >> 3);
	shift = bit & 0x7;

	return ((tcb[ix] >> shift) & 1);
}

static inline uint64_t
get_tcb_bits(u_char *tcb, int hi, int lo)
{
	uint64_t rc = 0;

	while (hi >= lo) {
		rc = (rc << 1) | get_tcb_bit(tcb, hi);
		--hi;
	}

	return (rc);
}

/*
 * Called by the kernel to allow the TOE driver to "refine" values filled up in
 * the tcp_info for an offloaded connection.
 */
static void
t4_tcp_info(struct toedev *tod, struct tcpcb *tp, struct tcp_info *ti)
{
	int i, j, k, rc;
	struct adapter *sc = tod->tod_softc;
	struct toepcb *toep = tp->t_toe;
	uint32_t addr, v;
	uint32_t buf[TCB_SIZE / sizeof(uint32_t)];
	u_char *tcb, tmp;

	INP_WLOCK_ASSERT(tp->t_inpcb);
	MPASS(ti != NULL);

	ti->tcpi_toe_tid = toep->tid;

	addr = t4_read_reg(sc, A_TP_CMM_TCB_BASE) + toep->tid * TCB_SIZE;
	rc = read_via_memwin(sc, 2, addr, &buf[0], TCB_SIZE);
	if (rc != 0)
		return;

	tcb = (u_char *)&buf[0];
	for (i = 0, j = TCB_SIZE - 16; i < j; i += 16, j -= 16) {
		for (k = 0; k < 16; k++) {
			tmp = tcb[i + k];
			tcb[i + k] = tcb[j + k];
			tcb[j + k] = tmp;
		}
	}

	ti->tcpi_state = get_tcb_bits(tcb, 115, 112);

	v = get_tcb_bits(tcb, 271, 256);
	ti->tcpi_rtt = tcp_ticks_to_us(sc, v);

	v = get_tcb_bits(tcb, 287, 272);
	ti->tcpi_rttvar = tcp_ticks_to_us(sc, v);

	ti->tcpi_snd_ssthresh = get_tcb_bits(tcb, 487, 460);
	ti->tcpi_snd_cwnd = get_tcb_bits(tcb, 459, 432);
	ti->tcpi_rcv_nxt = get_tcb_bits(tcb, 553, 522);

	ti->tcpi_snd_nxt = get_tcb_bits(tcb, 319, 288) -
	    get_tcb_bits(tcb, 375, 348);

	/* Receive window being advertised by us. */
	ti->tcpi_rcv_space = get_tcb_bits(tcb, 581, 554);

	/* Send window ceiling. */
	v = get_tcb_bits(tcb, 159, 144) << get_tcb_bits(tcb, 131, 128);
	ti->tcpi_snd_wnd = min(v, ti->tcpi_snd_cwnd);
}

/*
 * The TOE driver will not receive any more CPLs for the tid associated with the
 * toepcb; release the hold on the inpcb.
 */
void
final_cpl_received(struct toepcb *toep)
{
	struct inpcb *inp = toep->inp;

	KASSERT(inp != NULL, ("%s: inp is NULL", __func__));
	INP_WLOCK_ASSERT(inp);
	KASSERT(toep->flags & TPF_CPL_PENDING,
	    ("%s: CPL not pending already?", __func__));

	CTR6(KTR_CXGBE, "%s: tid %d, toep %p (0x%x), inp %p (0x%x)",
	    __func__, toep->tid, toep, toep->flags, inp, inp->inp_flags);

	if (toep->ulp_mode == ULP_MODE_TCPDDP)
		release_ddp_resources(toep);
	toep->inp = NULL;
	toep->flags &= ~TPF_CPL_PENDING;
	mbufq_drain(&toep->ulp_pdu_reclaimq);

	if (!(toep->flags & TPF_ATTACHED))
		release_offload_resources(toep);

	if (!in_pcbrele_wlocked(inp))
		INP_WUNLOCK(inp);
}

void
insert_tid(struct adapter *sc, int tid, void *ctx, int ntids)
{
	struct tid_info *t = &sc->tids;

	MPASS(tid >= t->tid_base);
	MPASS(tid - t->tid_base < t->ntids);

	t->tid_tab[tid - t->tid_base] = ctx;
	atomic_add_int(&t->tids_in_use, ntids);
}

void *
lookup_tid(struct adapter *sc, int tid)
{
	struct tid_info *t = &sc->tids;

	return (t->tid_tab[tid - t->tid_base]);
}

void
update_tid(struct adapter *sc, int tid, void *ctx)
{
	struct tid_info *t = &sc->tids;

	t->tid_tab[tid - t->tid_base] = ctx;
}

void
remove_tid(struct adapter *sc, int tid, int ntids)
{
	struct tid_info *t = &sc->tids;

	t->tid_tab[tid - t->tid_base] = NULL;
	atomic_subtract_int(&t->tids_in_use, ntids);
}

/*
 * What mtu_idx to use, given a 4-tuple.  Note that both s->mss and tcp_mssopt
 * have the MSS that we should advertise in our SYN.  Advertised MSS doesn't
 * account for any TCP options so the effective MSS (only payload, no headers or
 * options) could be different.  We fill up tp->t_maxseg with the effective MSS
 * at the end of the 3-way handshake.
 */
int
find_best_mtu_idx(struct adapter *sc, struct in_conninfo *inc,
    struct offload_settings *s)
{
	unsigned short *mtus = &sc->params.mtus[0];
	int i, mss, mtu;

	MPASS(inc != NULL);

	mss = s->mss > 0 ? s->mss : tcp_mssopt(inc);
	if (inc->inc_flags & INC_ISIPV6)
		mtu = mss + sizeof(struct ip6_hdr) + sizeof(struct tcphdr);
	else
		mtu = mss + sizeof(struct ip) + sizeof(struct tcphdr);

	for (i = 0; i < NMTUS - 1 && mtus[i + 1] <= mtu; i++)
		continue;

	return (i);
}

/*
 * Determine the receive window size for a socket.
 */
u_long
select_rcv_wnd(struct socket *so)
{
	unsigned long wnd;

	SOCKBUF_LOCK_ASSERT(&so->so_rcv);

	wnd = sbspace(&so->so_rcv);
	if (wnd < MIN_RCV_WND)
		wnd = MIN_RCV_WND;

	return min(wnd, MAX_RCV_WND);
}

int
select_rcv_wscale(void)
{
	int wscale = 0;
	unsigned long space = sb_max;

	if (space > MAX_RCV_WND)
		space = MAX_RCV_WND;

	while (wscale < TCP_MAX_WINSHIFT && (TCP_MAXWIN << wscale) < space)
		wscale++;

	return (wscale);
}

/*
 * socket so could be a listening socket too.
 */
uint64_t
calc_opt0(struct socket *so, struct vi_info *vi, struct l2t_entry *e,
    int mtu_idx, int rscale, int rx_credits, int ulp_mode,
    struct offload_settings *s)
{
	int keepalive;
	uint64_t opt0;

	MPASS(so != NULL);
	MPASS(vi != NULL);
	KASSERT(rx_credits <= M_RCV_BUFSIZ,
	    ("%s: rcv_bufsiz too high", __func__));

	opt0 = F_TCAM_BYPASS | V_WND_SCALE(rscale) | V_MSS_IDX(mtu_idx) |
	    V_ULP_MODE(ulp_mode) | V_RCV_BUFSIZ(rx_credits) |
	    V_L2T_IDX(e->idx) | V_SMAC_SEL(vi->smt_idx) |
	    V_TX_CHAN(vi->pi->tx_chan);

	keepalive = tcp_always_keepalive || so_options_get(so) & SO_KEEPALIVE;
	opt0 |= V_KEEP_ALIVE(keepalive != 0);

	if (s->nagle < 0) {
		struct inpcb *inp = sotoinpcb(so);
		struct tcpcb *tp = intotcpcb(inp);

		opt0 |= V_NAGLE((tp->t_flags & TF_NODELAY) == 0);
	} else
		opt0 |= V_NAGLE(s->nagle != 0);

	return htobe64(opt0);
}

uint64_t
select_ntuple(struct vi_info *vi, struct l2t_entry *e)
{
	struct adapter *sc = vi->pi->adapter;
	struct tp_params *tp = &sc->params.tp;
	uint64_t ntuple = 0;

	/*
	 * Initialize each of the fields which we care about which are present
	 * in the Compressed Filter Tuple.
	 */
	if (tp->vlan_shift >= 0 && EVL_VLANOFTAG(e->vlan) != CPL_L2T_VLAN_NONE)
		ntuple |= (uint64_t)(F_FT_VLAN_VLD | e->vlan) << tp->vlan_shift;

	if (tp->port_shift >= 0)
		ntuple |= (uint64_t)e->lport << tp->port_shift;

	if (tp->protocol_shift >= 0)
		ntuple |= (uint64_t)IPPROTO_TCP << tp->protocol_shift;

	if (tp->vnic_shift >= 0 && tp->ingress_config & F_VNIC) {
		ntuple |= (uint64_t)(V_FT_VNID_ID_VF(vi->vin) |
		    V_FT_VNID_ID_PF(sc->pf) | V_FT_VNID_ID_VLD(vi->vfvld)) <<
		    tp->vnic_shift;
	}

	if (is_t4(sc))
		return (htobe32((uint32_t)ntuple));
	else
		return (htobe64(V_FILTER_TUPLE(ntuple)));
}

static int
is_tls_sock(struct socket *so, struct adapter *sc)
{
	struct inpcb *inp = sotoinpcb(so);
	int i, rc;

	/* XXX: Eventually add a SO_WANT_TLS socket option perhaps? */
	rc = 0;
	ADAPTER_LOCK(sc);
	for (i = 0; i < sc->tt.num_tls_rx_ports; i++) {
		if (inp->inp_lport == htons(sc->tt.tls_rx_ports[i]) ||
		    inp->inp_fport == htons(sc->tt.tls_rx_ports[i])) {
			rc = 1;
			break;
		}
	}
	ADAPTER_UNLOCK(sc);
	return (rc);
}

int
select_ulp_mode(struct socket *so, struct adapter *sc,
    struct offload_settings *s)
{

	if (can_tls_offload(sc) &&
	    (s->tls > 0 || (s->tls < 0 && is_tls_sock(so, sc))))
		return (ULP_MODE_TLS);
	else if (s->ddp > 0 ||
	    (s->ddp < 0 && sc->tt.ddp && (so->so_options & SO_NO_DDP) == 0))
		return (ULP_MODE_TCPDDP);
	else
		return (ULP_MODE_NONE);
}

void
set_ulp_mode(struct toepcb *toep, int ulp_mode)
{

	CTR4(KTR_CXGBE, "%s: toep %p (tid %d) ulp_mode %d",
	    __func__, toep, toep->tid, ulp_mode);
	toep->ulp_mode = ulp_mode;
	tls_init_toep(toep);
	if (toep->ulp_mode == ULP_MODE_TCPDDP)
		ddp_init_toep(toep);
}

int
negative_advice(int status)
{

	return (status == CPL_ERR_RTX_NEG_ADVICE ||
	    status == CPL_ERR_PERSIST_NEG_ADVICE ||
	    status == CPL_ERR_KEEPALV_NEG_ADVICE);
}

static int
alloc_tid_tab(struct tid_info *t, int flags)
{

	MPASS(t->ntids > 0);
	MPASS(t->tid_tab == NULL);

	t->tid_tab = malloc(t->ntids * sizeof(*t->tid_tab), M_CXGBE,
	    M_ZERO | flags);
	if (t->tid_tab == NULL)
		return (ENOMEM);
	atomic_store_rel_int(&t->tids_in_use, 0);

	return (0);
}

static void
free_tid_tab(struct tid_info *t)
{

	KASSERT(t->tids_in_use == 0,
	    ("%s: %d tids still in use.", __func__, t->tids_in_use));

	free(t->tid_tab, M_CXGBE);
	t->tid_tab = NULL;
}

static int
alloc_stid_tab(struct tid_info *t, int flags)
{

	MPASS(t->nstids > 0);
	MPASS(t->stid_tab == NULL);

	t->stid_tab = malloc(t->nstids * sizeof(*t->stid_tab), M_CXGBE,
	    M_ZERO | flags);
	if (t->stid_tab == NULL)
		return (ENOMEM);
	mtx_init(&t->stid_lock, "stid lock", NULL, MTX_DEF);
	t->stids_in_use = 0;
	TAILQ_INIT(&t->stids);
	t->nstids_free_head = t->nstids;

	return (0);
}

static void
free_stid_tab(struct tid_info *t)
{

	KASSERT(t->stids_in_use == 0,
	    ("%s: %d tids still in use.", __func__, t->stids_in_use));

	if (mtx_initialized(&t->stid_lock))
		mtx_destroy(&t->stid_lock);
	free(t->stid_tab, M_CXGBE);
	t->stid_tab = NULL;
}

static void
free_tid_tabs(struct tid_info *t)
{

	free_tid_tab(t);
	free_atid_tab(t);
	free_stid_tab(t);
}

static int
alloc_tid_tabs(struct tid_info *t)
{
	int rc;

	rc = alloc_tid_tab(t, M_NOWAIT);
	if (rc != 0)
		goto failed;

	rc = alloc_atid_tab(t, M_NOWAIT);
	if (rc != 0)
		goto failed;

	rc = alloc_stid_tab(t, M_NOWAIT);
	if (rc != 0)
		goto failed;

	return (0);
failed:
	free_tid_tabs(t);
	return (rc);
}

static void
free_tom_data(struct adapter *sc, struct tom_data *td)
{

	ASSERT_SYNCHRONIZED_OP(sc);

	KASSERT(TAILQ_EMPTY(&td->toep_list),
	    ("%s: TOE PCB list is not empty.", __func__));
	KASSERT(td->lctx_count == 0,
	    ("%s: lctx hash table is not empty.", __func__));

	t4_free_ppod_region(&td->pr);

	if (td->listen_mask != 0)
		hashdestroy(td->listen_hash, M_CXGBE, td->listen_mask);

	if (mtx_initialized(&td->unsent_wr_lock))
		mtx_destroy(&td->unsent_wr_lock);
	if (mtx_initialized(&td->lctx_hash_lock))
		mtx_destroy(&td->lctx_hash_lock);
	if (mtx_initialized(&td->toep_list_lock))
		mtx_destroy(&td->toep_list_lock);

	free_tid_tabs(&sc->tids);
	free(td, M_CXGBE);
}

static char *
prepare_pkt(int open_type, uint16_t vtag, struct inpcb *inp, int *pktlen,
    int *buflen)
{
	char *pkt;
	struct tcphdr *th;
	int ipv6, len;
	const int maxlen =
	    max(sizeof(struct ether_header), sizeof(struct ether_vlan_header)) +
	    max(sizeof(struct ip), sizeof(struct ip6_hdr)) +
	    sizeof(struct tcphdr);

	MPASS(open_type == OPEN_TYPE_ACTIVE || open_type == OPEN_TYPE_LISTEN);

	pkt = malloc(maxlen, M_CXGBE, M_ZERO | M_NOWAIT);
	if (pkt == NULL)
		return (NULL);

	ipv6 = inp->inp_vflag & INP_IPV6;
	len = 0;

	if (EVL_VLANOFTAG(vtag) == 0xfff) {
		struct ether_header *eh = (void *)pkt;

		if (ipv6)
			eh->ether_type = htons(ETHERTYPE_IPV6);
		else
			eh->ether_type = htons(ETHERTYPE_IP);

		len += sizeof(*eh);
	} else {
		struct ether_vlan_header *evh = (void *)pkt;

		evh->evl_encap_proto = htons(ETHERTYPE_VLAN);
		evh->evl_tag = htons(vtag);
		if (ipv6)
			evh->evl_proto = htons(ETHERTYPE_IPV6);
		else
			evh->evl_proto = htons(ETHERTYPE_IP);

		len += sizeof(*evh);
	}

	if (ipv6) {
		struct ip6_hdr *ip6 = (void *)&pkt[len];

		ip6->ip6_vfc = IPV6_VERSION;
		ip6->ip6_plen = htons(sizeof(struct tcphdr));
		ip6->ip6_nxt = IPPROTO_TCP;
		if (open_type == OPEN_TYPE_ACTIVE) {
			ip6->ip6_src = inp->in6p_laddr;
			ip6->ip6_dst = inp->in6p_faddr;
		} else if (open_type == OPEN_TYPE_LISTEN) {
			ip6->ip6_src = inp->in6p_laddr;
			ip6->ip6_dst = ip6->ip6_src;
		}

		len += sizeof(*ip6);
	} else {
		struct ip *ip = (void *)&pkt[len];

		ip->ip_v = IPVERSION;
		ip->ip_hl = sizeof(*ip) >> 2;
		ip->ip_tos = inp->inp_ip_tos;
		ip->ip_len = htons(sizeof(struct ip) + sizeof(struct tcphdr));
		ip->ip_ttl = inp->inp_ip_ttl;
		ip->ip_p = IPPROTO_TCP;
		if (open_type == OPEN_TYPE_ACTIVE) {
			ip->ip_src = inp->inp_laddr;
			ip->ip_dst = inp->inp_faddr;
		} else if (open_type == OPEN_TYPE_LISTEN) {
			ip->ip_src = inp->inp_laddr;
			ip->ip_dst = ip->ip_src;
		}

		len += sizeof(*ip);
	}

	th = (void *)&pkt[len];
	if (open_type == OPEN_TYPE_ACTIVE) {
		th->th_sport = inp->inp_lport;	/* network byte order already */
		th->th_dport = inp->inp_fport;	/* ditto */
	} else if (open_type == OPEN_TYPE_LISTEN) {
		th->th_sport = inp->inp_lport;	/* network byte order already */
		th->th_dport = th->th_sport;
	}
	len += sizeof(th);

	*pktlen = *buflen = len;
	return (pkt);
}

const struct offload_settings *
lookup_offload_policy(struct adapter *sc, int open_type, struct mbuf *m,
    uint16_t vtag, struct inpcb *inp)
{
	const struct t4_offload_policy *op;
	char *pkt;
	struct offload_rule *r;
	int i, matched, pktlen, buflen;
	static const struct offload_settings allow_offloading_settings = {
		.offload = 1,
		.rx_coalesce = -1,
		.cong_algo = -1,
		.sched_class = -1,
		.tstamp = -1,
		.sack = -1,
		.nagle = -1,
		.ecn = -1,
		.ddp = -1,
		.tls = -1,
		.txq = -1,
		.rxq = -1,
		.mss = -1,
	};
	static const struct offload_settings disallow_offloading_settings = {
		.offload = 0,
		/* rest is irrelevant when offload is off. */
	};

	rw_assert(&sc->policy_lock, RA_LOCKED);

	/*
	 * If there's no Connection Offloading Policy attached to the device
	 * then we need to return a default static policy.  If
	 * "cop_managed_offloading" is true, then we need to disallow
	 * offloading until a COP is attached to the device.  Otherwise we
	 * allow offloading ...
	 */
	op = sc->policy;
	if (op == NULL) {
		if (sc->tt.cop_managed_offloading)
			return (&disallow_offloading_settings);
		else
			return (&allow_offloading_settings);
	}

	switch (open_type) {
	case OPEN_TYPE_ACTIVE:
	case OPEN_TYPE_LISTEN:
		pkt = prepare_pkt(open_type, vtag, inp, &pktlen, &buflen);
		break;
	case OPEN_TYPE_PASSIVE:
		MPASS(m != NULL);
		pkt = mtod(m, char *);
		MPASS(*pkt == CPL_PASS_ACCEPT_REQ);
		pkt += sizeof(struct cpl_pass_accept_req);
		pktlen = m->m_pkthdr.len - sizeof(struct cpl_pass_accept_req);
		buflen = m->m_len - sizeof(struct cpl_pass_accept_req);
		break;
	default:
		MPASS(0);
		return (&disallow_offloading_settings);
	}

	if (pkt == NULL || pktlen == 0 || buflen == 0)
		return (&disallow_offloading_settings);

	matched = 0;
	r = &op->rule[0];
	for (i = 0; i < op->nrules; i++, r++) {
		if (r->open_type != open_type &&
		    r->open_type != OPEN_TYPE_DONTCARE) {
			continue;
		}
		matched = bpf_filter(r->bpf_prog.bf_insns, pkt, pktlen, buflen);
		if (matched)
			break;
	}

	if (open_type == OPEN_TYPE_ACTIVE || open_type == OPEN_TYPE_LISTEN)
		free(pkt, M_CXGBE);

	return (matched ? &r->settings : &disallow_offloading_settings);
}

static void
reclaim_wr_resources(void *arg, int count)
{
	struct tom_data *td = arg;
	STAILQ_HEAD(, wrqe) twr_list = STAILQ_HEAD_INITIALIZER(twr_list);
	struct cpl_act_open_req *cpl;
	u_int opcode, atid, tid;
	struct wrqe *wr;
	struct adapter *sc = td_adapter(td);

	mtx_lock(&td->unsent_wr_lock);
	STAILQ_SWAP(&td->unsent_wr_list, &twr_list, wrqe);
	mtx_unlock(&td->unsent_wr_lock);

	while ((wr = STAILQ_FIRST(&twr_list)) != NULL) {
		STAILQ_REMOVE_HEAD(&twr_list, link);

		cpl = wrtod(wr);
		opcode = GET_OPCODE(cpl);

		switch (opcode) {
		case CPL_ACT_OPEN_REQ:
		case CPL_ACT_OPEN_REQ6:
			atid = G_TID_TID(be32toh(OPCODE_TID(cpl)));
			CTR2(KTR_CXGBE, "%s: atid %u ", __func__, atid);
			act_open_failure_cleanup(sc, atid, EHOSTUNREACH);
			free(wr, M_CXGBE);
			break;
		case CPL_PASS_ACCEPT_RPL:
			tid = GET_TID(cpl);
			CTR2(KTR_CXGBE, "%s: tid %u ", __func__, tid);
			synack_failure_cleanup(sc, tid);
			free(wr, M_CXGBE);
			break;
		default:
			log(LOG_ERR, "%s: leaked work request %p, wr_len %d, "
			    "opcode %x\n", __func__, wr, wr->wr_len, opcode);
			/* WR not freed here; go look at it with a debugger.  */
		}
	}
}

/*
 * Ground control to Major TOM
 * Commencing countdown, engines on
 */
static int
t4_tom_activate(struct adapter *sc)
{
	struct tom_data *td;
	struct toedev *tod;
	struct vi_info *vi;
	int i, rc, v;

	ASSERT_SYNCHRONIZED_OP(sc);

	/* per-adapter softc for TOM */
	td = malloc(sizeof(*td), M_CXGBE, M_ZERO | M_NOWAIT);
	if (td == NULL)
		return (ENOMEM);

	/* List of TOE PCBs and associated lock */
	mtx_init(&td->toep_list_lock, "PCB list lock", NULL, MTX_DEF);
	TAILQ_INIT(&td->toep_list);

	/* Listen context */
	mtx_init(&td->lctx_hash_lock, "lctx hash lock", NULL, MTX_DEF);
	td->listen_hash = hashinit_flags(LISTEN_HASH_SIZE, M_CXGBE,
	    &td->listen_mask, HASH_NOWAIT);

	/* List of WRs for which L2 resolution failed */
	mtx_init(&td->unsent_wr_lock, "Unsent WR list lock", NULL, MTX_DEF);
	STAILQ_INIT(&td->unsent_wr_list);
	TASK_INIT(&td->reclaim_wr_resources, 0, reclaim_wr_resources, td);

	/* TID tables */
	rc = alloc_tid_tabs(&sc->tids);
	if (rc != 0)
		goto done;

	rc = t4_init_ppod_region(&td->pr, &sc->vres.ddp,
	    t4_read_reg(sc, A_ULP_RX_TDDP_PSZ), "TDDP page pods");
	if (rc != 0)
		goto done;
	t4_set_reg_field(sc, A_ULP_RX_TDDP_TAGMASK,
	    V_TDDPTAGMASK(M_TDDPTAGMASK), td->pr.pr_tag_mask);

	/* toedev ops */
	tod = &td->tod;
	init_toedev(tod);
	tod->tod_softc = sc;
	tod->tod_connect = t4_connect;
	tod->tod_listen_start = t4_listen_start;
	tod->tod_listen_stop = t4_listen_stop;
	tod->tod_rcvd = t4_rcvd;
	tod->tod_output = t4_tod_output;
	tod->tod_send_rst = t4_send_rst;
	tod->tod_send_fin = t4_send_fin;
	tod->tod_pcb_detach = t4_pcb_detach;
	tod->tod_l2_update = t4_l2_update;
	tod->tod_syncache_added = t4_syncache_added;
	tod->tod_syncache_removed = t4_syncache_removed;
	tod->tod_syncache_respond = t4_syncache_respond;
	tod->tod_offload_socket = t4_offload_socket;
	tod->tod_ctloutput = t4_ctloutput;
	tod->tod_tcp_info = t4_tcp_info;

	for_each_port(sc, i) {
		for_each_vi(sc->port[i], v, vi) {
			TOEDEV(vi->ifp) = &td->tod;
		}
	}

	sc->tom_softc = td;
	register_toedev(sc->tom_softc);

done:
	if (rc != 0)
		free_tom_data(sc, td);
	return (rc);
}

static int
t4_tom_deactivate(struct adapter *sc)
{
	int rc = 0;
	struct tom_data *td = sc->tom_softc;

	ASSERT_SYNCHRONIZED_OP(sc);

	if (td == NULL)
		return (0);	/* XXX. KASSERT? */

	if (sc->offload_map != 0)
		return (EBUSY);	/* at least one port has IFCAP_TOE enabled */

	if (uld_active(sc, ULD_IWARP) || uld_active(sc, ULD_ISCSI))
		return (EBUSY);	/* both iWARP and iSCSI rely on the TOE. */

	mtx_lock(&td->toep_list_lock);
	if (!TAILQ_EMPTY(&td->toep_list))
		rc = EBUSY;
	mtx_unlock(&td->toep_list_lock);

	mtx_lock(&td->lctx_hash_lock);
	if (td->lctx_count > 0)
		rc = EBUSY;
	mtx_unlock(&td->lctx_hash_lock);

	taskqueue_drain(taskqueue_thread, &td->reclaim_wr_resources);
	mtx_lock(&td->unsent_wr_lock);
	if (!STAILQ_EMPTY(&td->unsent_wr_list))
		rc = EBUSY;
	mtx_unlock(&td->unsent_wr_lock);

	if (rc == 0) {
		unregister_toedev(sc->tom_softc);
		free_tom_data(sc, td);
		sc->tom_softc = NULL;
	}

	return (rc);
}

static int
t4_aio_queue_tom(struct socket *so, struct kaiocb *job)
{
	struct tcpcb *tp = so_sototcpcb(so);
	struct toepcb *toep = tp->t_toe;
	int error;

	if (toep->ulp_mode == ULP_MODE_TCPDDP) {
		error = t4_aio_queue_ddp(so, job);
		if (error != EOPNOTSUPP)
			return (error);
	}

	return (t4_aio_queue_aiotx(so, job));
}

static int
t4_ctloutput_tom(struct socket *so, struct sockopt *sopt)
{

	if (sopt->sopt_level != IPPROTO_TCP)
		return (tcp_ctloutput(so, sopt));

	switch (sopt->sopt_name) {
	case TCP_TLSOM_SET_TLS_CONTEXT:
	case TCP_TLSOM_GET_TLS_TOM:
	case TCP_TLSOM_CLR_TLS_TOM:
	case TCP_TLSOM_CLR_QUIES:
		return (t4_ctloutput_tls(so, sopt));
	default:
		return (tcp_ctloutput(so, sopt));
	}
}

static int
t4_tom_mod_load(void)
{
	struct protosw *tcp_protosw, *tcp6_protosw;

	/* CPL handlers */
	t4_register_shared_cpl_handler(CPL_L2T_WRITE_RPL, do_l2t_write_rpl2,
	    CPL_COOKIE_TOM);
	t4_init_connect_cpl_handlers();
	t4_init_listen_cpl_handlers();
	t4_init_cpl_io_handlers();

	t4_ddp_mod_load();
	t4_tls_mod_load();

	tcp_protosw = pffindproto(PF_INET, IPPROTO_TCP, SOCK_STREAM);
	if (tcp_protosw == NULL)
		return (ENOPROTOOPT);
	bcopy(tcp_protosw, &toe_protosw, sizeof(toe_protosw));
	bcopy(tcp_protosw->pr_usrreqs, &toe_usrreqs, sizeof(toe_usrreqs));
	toe_usrreqs.pru_aio_queue = t4_aio_queue_tom;
	toe_protosw.pr_ctloutput = t4_ctloutput_tom;
	toe_protosw.pr_usrreqs = &toe_usrreqs;

	tcp6_protosw = pffindproto(PF_INET6, IPPROTO_TCP, SOCK_STREAM);
	if (tcp6_protosw == NULL)
		return (ENOPROTOOPT);
	bcopy(tcp6_protosw, &toe6_protosw, sizeof(toe6_protosw));
	bcopy(tcp6_protosw->pr_usrreqs, &toe6_usrreqs, sizeof(toe6_usrreqs));
	toe6_usrreqs.pru_aio_queue = t4_aio_queue_tom;
	toe6_protosw.pr_ctloutput = t4_ctloutput_tom;
	toe6_protosw.pr_usrreqs = &toe6_usrreqs;

	return (t4_register_uld(&tom_uld_info));
}

static void
tom_uninit(struct adapter *sc, void *arg __unused)
{
	if (begin_synchronized_op(sc, NULL, SLEEP_OK | INTR_OK, "t4tomun"))
		return;

	/* Try to free resources (works only if no port has IFCAP_TOE) */
	if (uld_active(sc, ULD_TOM))
		t4_deactivate_uld(sc, ULD_TOM);

	end_synchronized_op(sc, 0);
}

static int
t4_tom_mod_unload(void)
{
	t4_iterate(tom_uninit, NULL);

	if (t4_unregister_uld(&tom_uld_info) == EBUSY)
		return (EBUSY);

	t4_tls_mod_unload();
	t4_ddp_mod_unload();

	t4_uninit_connect_cpl_handlers();
	t4_uninit_listen_cpl_handlers();
	t4_uninit_cpl_io_handlers();
	t4_register_shared_cpl_handler(CPL_L2T_WRITE_RPL, NULL, CPL_COOKIE_TOM);

	return (0);
}
#endif	/* TCP_OFFLOAD */

static int
t4_tom_modevent(module_t mod, int cmd, void *arg)
{
	int rc = 0;

#ifdef TCP_OFFLOAD
	switch (cmd) {
	case MOD_LOAD:
		rc = t4_tom_mod_load();
		break;

	case MOD_UNLOAD:
		rc = t4_tom_mod_unload();
		break;

	default:
		rc = EINVAL;
	}
#else
	printf("t4_tom: compiled without TCP_OFFLOAD support.\n");
	rc = EOPNOTSUPP;
#endif
	return (rc);
}

static moduledata_t t4_tom_moddata= {
	"t4_tom",
	t4_tom_modevent,
	0
};

MODULE_VERSION(t4_tom, 1);
MODULE_DEPEND(t4_tom, toecore, 1, 1, 1);
MODULE_DEPEND(t4_tom, t4nex, 1, 1, 1);
DECLARE_MODULE(t4_tom, t4_tom_moddata, SI_SUB_EXEC, SI_ORDER_ANY);
