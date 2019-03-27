/*-
 * Copyright (c) 2012 Chelsio Communications, Inc.
 * All rights reserved.
 *
 * Chelsio T5xx iSCSI driver
 *
 * Written by: Sreenivasa Honnur <shonnur@chelsio.com>
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

#include <sys/types.h>
#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/systm.h>

#ifdef TCP_OFFLOAD
#include <sys/errno.h>
#include <sys/kthread.h>
#include <sys/smp.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/mbuf.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/condvar.h>

#include <netinet/in.h>
#include <netinet/in_pcb.h>
#include <netinet/toecore.h>
#include <netinet/tcp_var.h>
#include <netinet/tcp_fsm.h>

#include <cam/scsi/scsi_all.h>
#include <cam/scsi/scsi_da.h>
#include <cam/ctl/ctl_io.h>
#include <cam/ctl/ctl.h>
#include <cam/ctl/ctl_backend.h>
#include <cam/ctl/ctl_error.h>
#include <cam/ctl/ctl_frontend.h>
#include <cam/ctl/ctl_debug.h>
#include <cam/ctl/ctl_ha.h>
#include <cam/ctl/ctl_ioctl.h>

#include <dev/iscsi/icl.h>
#include <dev/iscsi/iscsi_proto.h>
#include <dev/iscsi/iscsi_ioctl.h>
#include <dev/iscsi/iscsi.h>
#include <cam/ctl/ctl_frontend_iscsi.h>

#include <cam/cam.h>
#include <cam/cam_ccb.h>
#include <cam/cam_xpt.h>
#include <cam/cam_debug.h>
#include <cam/cam_sim.h>
#include <cam/cam_xpt_sim.h>
#include <cam/cam_xpt_periph.h>
#include <cam/cam_periph.h>
#include <cam/cam_compat.h>
#include <cam/scsi/scsi_message.h>

#include "common/common.h"
#include "common/t4_msg.h"
#include "common/t4_regs.h"     /* for PCIE_MEM_ACCESS */
#include "tom/t4_tom.h"
#include "cxgbei.h"

static int worker_thread_count;
static struct cxgbei_worker_thread_softc *cwt_softc;
static struct proc *cxgbei_proc;

/* XXXNP some header instead. */
struct icl_pdu *icl_cxgbei_new_pdu(int);
void icl_cxgbei_new_pdu_set_conn(struct icl_pdu *, struct icl_conn *);
void icl_cxgbei_conn_pdu_free(struct icl_conn *, struct icl_pdu *);

static void
free_ci_counters(struct cxgbei_data *ci)
{

#define FREE_CI_COUNTER(x) do { \
	if (ci->x != NULL) { \
		counter_u64_free(ci->x); \
		ci->x = NULL; \
	} \
} while (0)

	FREE_CI_COUNTER(ddp_setup_ok);
	FREE_CI_COUNTER(ddp_setup_error);
	FREE_CI_COUNTER(ddp_bytes);
	FREE_CI_COUNTER(ddp_pdus);
	FREE_CI_COUNTER(fl_bytes);
	FREE_CI_COUNTER(fl_pdus);
#undef FREE_CI_COUNTER
}

static int
alloc_ci_counters(struct cxgbei_data *ci)
{

#define ALLOC_CI_COUNTER(x) do { \
	ci->x = counter_u64_alloc(M_WAITOK); \
	if (ci->x == NULL) \
		goto fail; \
} while (0)

	ALLOC_CI_COUNTER(ddp_setup_ok);
	ALLOC_CI_COUNTER(ddp_setup_error);
	ALLOC_CI_COUNTER(ddp_bytes);
	ALLOC_CI_COUNTER(ddp_pdus);
	ALLOC_CI_COUNTER(fl_bytes);
	ALLOC_CI_COUNTER(fl_pdus);
#undef ALLOC_CI_COUNTER

	return (0);
fail:
	free_ci_counters(ci);
	return (ENOMEM);
}

static void
read_pdu_limits(struct adapter *sc, uint32_t *max_tx_pdu_len,
    uint32_t *max_rx_pdu_len)
{
	uint32_t tx_len, rx_len, r, v;

	rx_len = t4_read_reg(sc, A_TP_PMM_RX_PAGE_SIZE);
	tx_len = t4_read_reg(sc, A_TP_PMM_TX_PAGE_SIZE);

	r = t4_read_reg(sc, A_TP_PARA_REG2);
	rx_len = min(rx_len, G_MAXRXDATA(r));
	tx_len = min(tx_len, G_MAXRXDATA(r));

	r = t4_read_reg(sc, A_TP_PARA_REG7);
	v = min(G_PMMAXXFERLEN0(r), G_PMMAXXFERLEN1(r));
	rx_len = min(rx_len, v);
	tx_len = min(tx_len, v);

	/* Remove after FW_FLOWC_MNEM_TXDATAPLEN_MAX fix in firmware. */
	tx_len = min(tx_len, 3 * 4096);

	*max_tx_pdu_len = rounddown2(tx_len, 512);
	*max_rx_pdu_len = rounddown2(rx_len, 512);
}

/*
 * Initialize the software state of the iSCSI ULP driver.
 *
 * ENXIO means firmware didn't set up something that it was supposed to.
 */
static int
cxgbei_init(struct adapter *sc, struct cxgbei_data *ci)
{
	struct sysctl_oid *oid;
	struct sysctl_oid_list *children;
	struct ppod_region *pr;
	uint32_t r;
	int rc;

	MPASS(sc->vres.iscsi.size > 0);
	MPASS(ci != NULL);

	rc = alloc_ci_counters(ci);
	if (rc != 0)
		return (rc);

	read_pdu_limits(sc, &ci->max_tx_pdu_len, &ci->max_rx_pdu_len);

	pr = &ci->pr;
	r = t4_read_reg(sc, A_ULP_RX_ISCSI_PSZ);
	rc = t4_init_ppod_region(pr, &sc->vres.iscsi, r, "iSCSI page pods");
	if (rc != 0) {
		device_printf(sc->dev,
		    "%s: failed to initialize the iSCSI page pod region: %u.\n",
		    __func__, rc);
		free_ci_counters(ci);
		return (rc);
	}

	r = t4_read_reg(sc, A_ULP_RX_ISCSI_TAGMASK);
	r &= V_ISCSITAGMASK(M_ISCSITAGMASK);
	if (r != pr->pr_tag_mask) {
		/*
		 * Recent firmwares are supposed to set up the iSCSI tagmask
		 * but we'll do it ourselves it the computed value doesn't match
		 * what's in the register.
		 */
		device_printf(sc->dev,
		    "tagmask 0x%08x does not match computed mask 0x%08x.\n", r,
		    pr->pr_tag_mask);
		t4_set_reg_field(sc, A_ULP_RX_ISCSI_TAGMASK,
		    V_ISCSITAGMASK(M_ISCSITAGMASK), pr->pr_tag_mask);
	}

	sysctl_ctx_init(&ci->ctx);
	oid = device_get_sysctl_tree(sc->dev);	/* dev.t5nex.X */
	children = SYSCTL_CHILDREN(oid);

	oid = SYSCTL_ADD_NODE(&ci->ctx, children, OID_AUTO, "iscsi", CTLFLAG_RD,
	    NULL, "iSCSI ULP statistics");
	children = SYSCTL_CHILDREN(oid);

	SYSCTL_ADD_COUNTER_U64(&ci->ctx, children, OID_AUTO, "ddp_setup_ok",
	    CTLFLAG_RD, &ci->ddp_setup_ok,
	    "# of times DDP buffer was setup successfully.");

	SYSCTL_ADD_COUNTER_U64(&ci->ctx, children, OID_AUTO, "ddp_setup_error",
	    CTLFLAG_RD, &ci->ddp_setup_error,
	    "# of times DDP buffer setup failed.");

	SYSCTL_ADD_COUNTER_U64(&ci->ctx, children, OID_AUTO, "ddp_bytes",
	    CTLFLAG_RD, &ci->ddp_bytes, "# of bytes placed directly");

	SYSCTL_ADD_COUNTER_U64(&ci->ctx, children, OID_AUTO, "ddp_pdus",
	    CTLFLAG_RD, &ci->ddp_pdus, "# of PDUs with data placed directly.");

	SYSCTL_ADD_COUNTER_U64(&ci->ctx, children, OID_AUTO, "fl_bytes",
	    CTLFLAG_RD, &ci->fl_bytes, "# of data bytes delivered in freelist");

	SYSCTL_ADD_COUNTER_U64(&ci->ctx, children, OID_AUTO, "fl_pdus",
	    CTLFLAG_RD, &ci->fl_pdus,
	    "# of PDUs with data delivered in freelist");

	ci->ddp_threshold = 2048;
	SYSCTL_ADD_UINT(&ci->ctx, children, OID_AUTO, "ddp_threshold",
	    CTLFLAG_RW, &ci->ddp_threshold, 0, "Rx zero copy threshold");

	return (0);
}

static int
do_rx_iscsi_hdr(struct sge_iq *iq, const struct rss_header *rss, struct mbuf *m)
{
	struct adapter *sc = iq->adapter;
	struct cpl_iscsi_hdr *cpl = mtod(m, struct cpl_iscsi_hdr *);
	u_int tid = GET_TID(cpl);
	struct toepcb *toep = lookup_tid(sc, tid);
	struct icl_pdu *ip;
	struct icl_cxgbei_pdu *icp;
	uint16_t len_ddp = be16toh(cpl->pdu_len_ddp);
	uint16_t len = be16toh(cpl->len);

	M_ASSERTPKTHDR(m);
	MPASS(m->m_pkthdr.len == len + sizeof(*cpl));

	ip = icl_cxgbei_new_pdu(M_NOWAIT);
	if (ip == NULL)
		CXGBE_UNIMPLEMENTED("PDU allocation failure");
	m_copydata(m, sizeof(*cpl), ISCSI_BHS_SIZE, (caddr_t)ip->ip_bhs);
	ip->ip_data_len = G_ISCSI_PDU_LEN(len_ddp) - len;
	icp = ip_to_icp(ip);
	icp->icp_seq = ntohl(cpl->seq);
	icp->icp_flags = ICPF_RX_HDR;

	/* This is the start of a new PDU.  There should be no old state. */
	MPASS(toep->ulpcb2 == NULL);
	toep->ulpcb2 = icp;

#if 0
	CTR5(KTR_CXGBE, "%s: tid %u, cpl->len %u, pdu_len_ddp 0x%04x, icp %p",
	    __func__, tid, len, len_ddp, icp);
#endif

	m_freem(m);
	return (0);
}

static int
do_rx_iscsi_data(struct sge_iq *iq, const struct rss_header *rss, struct mbuf *m)
{
	struct adapter *sc = iq->adapter;
	struct cxgbei_data *ci = sc->iscsi_ulp_softc;
	struct cpl_iscsi_data *cpl =  mtod(m, struct cpl_iscsi_data *);
	u_int tid = GET_TID(cpl);
	struct toepcb *toep = lookup_tid(sc, tid);
	struct icl_cxgbei_pdu *icp = toep->ulpcb2;

	M_ASSERTPKTHDR(m);
	MPASS(m->m_pkthdr.len == be16toh(cpl->len) + sizeof(*cpl));

	/* Must already have received the header (but not the data). */
	MPASS(icp != NULL);
	MPASS(icp->icp_flags == ICPF_RX_HDR);
	MPASS(icp->ip.ip_data_mbuf == NULL);


	m_adj(m, sizeof(*cpl));
	MPASS(icp->ip.ip_data_len == m->m_pkthdr.len);

	icp->icp_flags |= ICPF_RX_FLBUF;
	icp->ip.ip_data_mbuf = m;
	counter_u64_add(ci->fl_pdus, 1);
	counter_u64_add(ci->fl_bytes, m->m_pkthdr.len);

#if 0
	CTR3(KTR_CXGBE, "%s: tid %u, cpl->len %u", __func__, tid,
	    be16toh(cpl->len));
#endif

	return (0);
}

static int
do_rx_iscsi_ddp(struct sge_iq *iq, const struct rss_header *rss, struct mbuf *m)
{
	struct adapter *sc = iq->adapter;
	struct cxgbei_data *ci = sc->iscsi_ulp_softc;
	const struct cpl_rx_data_ddp *cpl = (const void *)(rss + 1);
	u_int tid = GET_TID(cpl);
	struct toepcb *toep = lookup_tid(sc, tid);
	struct inpcb *inp = toep->inp;
	struct socket *so;
	struct sockbuf *sb;
	struct tcpcb *tp;
	struct icl_cxgbei_conn *icc;
	struct icl_conn *ic;
	struct icl_cxgbei_pdu *icp = toep->ulpcb2;
	struct icl_pdu *ip;
	u_int pdu_len, val;
	struct epoch_tracker et;

	MPASS(m == NULL);

	/* Must already be assembling a PDU. */
	MPASS(icp != NULL);
	MPASS(icp->icp_flags & ICPF_RX_HDR);	/* Data is optional. */
	MPASS((icp->icp_flags & ICPF_RX_STATUS) == 0);

	pdu_len = be16toh(cpl->len);	/* includes everything. */
	val = be32toh(cpl->ddpvld);

#if 0
	CTR5(KTR_CXGBE,
	    "%s: tid %u, cpl->len %u, ddpvld 0x%08x, icp_flags 0x%08x",
	    __func__, tid, pdu_len, val, icp->icp_flags);
#endif

	icp->icp_flags |= ICPF_RX_STATUS;
	ip = &icp->ip;
	if (val & F_DDP_PADDING_ERR)
		icp->icp_flags |= ICPF_PAD_ERR;
	if (val & F_DDP_HDRCRC_ERR)
		icp->icp_flags |= ICPF_HCRC_ERR;
	if (val & F_DDP_DATACRC_ERR)
		icp->icp_flags |= ICPF_DCRC_ERR;
	if (val & F_DDP_PDU && ip->ip_data_mbuf == NULL) {
		MPASS((icp->icp_flags & ICPF_RX_FLBUF) == 0);
		MPASS(ip->ip_data_len > 0);
		icp->icp_flags |= ICPF_RX_DDP;
		counter_u64_add(ci->ddp_pdus, 1);
		counter_u64_add(ci->ddp_bytes, ip->ip_data_len);
	}

	INP_WLOCK(inp);
	if (__predict_false(inp->inp_flags & (INP_DROPPED | INP_TIMEWAIT))) {
		CTR4(KTR_CXGBE, "%s: tid %u, rx (%d bytes), inp_flags 0x%x",
		    __func__, tid, pdu_len, inp->inp_flags);
		INP_WUNLOCK(inp);
		icl_cxgbei_conn_pdu_free(NULL, ip);
#ifdef INVARIANTS
		toep->ulpcb2 = NULL;
#endif
		return (0);
	}

	tp = intotcpcb(inp);
	MPASS(icp->icp_seq == tp->rcv_nxt);
	MPASS(tp->rcv_wnd >= pdu_len);
	tp->rcv_nxt += pdu_len;
	tp->rcv_wnd -= pdu_len;
	tp->t_rcvtime = ticks;

	/* update rx credits */
	toep->rx_credits += pdu_len;
	t4_rcvd(&toep->td->tod, tp);	/* XXX: sc->tom_softc.tod */

	so = inp->inp_socket;
	sb = &so->so_rcv;
	SOCKBUF_LOCK(sb);

	icc = toep->ulpcb;
	if (__predict_false(icc == NULL || sb->sb_state & SBS_CANTRCVMORE)) {
		CTR5(KTR_CXGBE,
		    "%s: tid %u, excess rx (%d bytes), icc %p, sb_state 0x%x",
		    __func__, tid, pdu_len, icc, sb->sb_state);
		SOCKBUF_UNLOCK(sb);
		INP_WUNLOCK(inp);

		INP_INFO_RLOCK_ET(&V_tcbinfo, et);
		INP_WLOCK(inp);
		tp = tcp_drop(tp, ECONNRESET);
		if (tp)
			INP_WUNLOCK(inp);
		INP_INFO_RUNLOCK_ET(&V_tcbinfo, et);

		icl_cxgbei_conn_pdu_free(NULL, ip);
#ifdef INVARIANTS
		toep->ulpcb2 = NULL;
#endif
		return (0);
	}
	MPASS(icc->icc_signature == CXGBEI_CONN_SIGNATURE);
	ic = &icc->ic;
	icl_cxgbei_new_pdu_set_conn(ip, ic);

	MPASS(m == NULL); /* was unused, we'll use it now. */
	m = sbcut_locked(sb, sbused(sb)); /* XXXNP: toep->sb_cc accounting? */
	if (__predict_false(m != NULL)) {
		int len = m_length(m, NULL);

		/*
		 * PDUs were received before the tid transitioned to ULP mode.
		 * Convert them to icl_cxgbei_pdus and send them to ICL before
		 * the PDU in icp/ip.
		 */
		CTR3(KTR_CXGBE, "%s: tid %u, %u bytes in so_rcv", __func__, tid,
		    len);

		/* XXXNP: needs to be rewritten. */
		if (len == sizeof(struct iscsi_bhs) || len == 4 + sizeof(struct
		    iscsi_bhs)) {
			struct icl_cxgbei_pdu *icp0;
			struct icl_pdu *ip0;

			ip0 = icl_cxgbei_new_pdu(M_NOWAIT);
			if (ip0 == NULL)
				CXGBE_UNIMPLEMENTED("PDU allocation failure");
			icl_cxgbei_new_pdu_set_conn(ip0, ic);
			icp0 = ip_to_icp(ip0);
			icp0->icp_seq = 0; /* XXX */
			icp0->icp_flags = ICPF_RX_HDR | ICPF_RX_STATUS;
			m_copydata(m, 0, sizeof(struct iscsi_bhs), (void *)ip0->ip_bhs);
			STAILQ_INSERT_TAIL(&icc->rcvd_pdus, ip0, ip_next);
		}
		m_freem(m);
	}

	STAILQ_INSERT_TAIL(&icc->rcvd_pdus, ip, ip_next);
	if ((icc->rx_flags & RXF_ACTIVE) == 0) {
		struct cxgbei_worker_thread_softc *cwt = &cwt_softc[icc->cwt];

		mtx_lock(&cwt->cwt_lock);
		icc->rx_flags |= RXF_ACTIVE;
		TAILQ_INSERT_TAIL(&cwt->rx_head, icc, rx_link);
		if (cwt->cwt_state == CWT_SLEEPING) {
			cwt->cwt_state = CWT_RUNNING;
			cv_signal(&cwt->cwt_cv);
		}
		mtx_unlock(&cwt->cwt_lock);
	}
	SOCKBUF_UNLOCK(sb);
	INP_WUNLOCK(inp);

#ifdef INVARIANTS
	toep->ulpcb2 = NULL;
#endif

	return (0);
}

static int
cxgbei_activate(struct adapter *sc)
{
	struct cxgbei_data *ci;
	int rc;

	ASSERT_SYNCHRONIZED_OP(sc);

	if (uld_active(sc, ULD_ISCSI)) {
		KASSERT(0, ("%s: iSCSI offload already enabled on adapter %p",
		    __func__, sc));
		return (0);
	}

	if (sc->iscsicaps == 0 || sc->vres.iscsi.size == 0) {
		device_printf(sc->dev,
		    "not iSCSI offload capable, or capability disabled.\n");
		return (ENOSYS);
	}

	/* per-adapter softc for iSCSI */
	ci = malloc(sizeof(*ci), M_CXGBE, M_ZERO | M_WAITOK);
	if (ci == NULL)
		return (ENOMEM);

	rc = cxgbei_init(sc, ci);
	if (rc != 0) {
		free(ci, M_CXGBE);
		return (rc);
	}

	sc->iscsi_ulp_softc = ci;

	return (0);
}

static int
cxgbei_deactivate(struct adapter *sc)
{
	struct cxgbei_data *ci = sc->iscsi_ulp_softc;

	ASSERT_SYNCHRONIZED_OP(sc);

	if (ci != NULL) {
		sysctl_ctx_free(&ci->ctx);
		t4_free_ppod_region(&ci->pr);
		free_ci_counters(ci);
		free(ci, M_CXGBE);
		sc->iscsi_ulp_softc = NULL;
	}

	return (0);
}

static void
cxgbei_activate_all(struct adapter *sc, void *arg __unused)
{

	if (begin_synchronized_op(sc, NULL, SLEEP_OK | INTR_OK, "t4isact") != 0)
		return;

	/* Activate iSCSI if any port on this adapter has IFCAP_TOE enabled. */
	if (sc->offload_map && !uld_active(sc, ULD_ISCSI))
		(void) t4_activate_uld(sc, ULD_ISCSI);

	end_synchronized_op(sc, 0);
}

static void
cxgbei_deactivate_all(struct adapter *sc, void *arg __unused)
{

	if (begin_synchronized_op(sc, NULL, SLEEP_OK | INTR_OK, "t4isdea") != 0)
		return;

	if (uld_active(sc, ULD_ISCSI))
	    (void) t4_deactivate_uld(sc, ULD_ISCSI);

	end_synchronized_op(sc, 0);
}

static struct uld_info cxgbei_uld_info = {
	.uld_id = ULD_ISCSI,
	.activate = cxgbei_activate,
	.deactivate = cxgbei_deactivate,
};

static void
cwt_main(void *arg)
{
	struct cxgbei_worker_thread_softc *cwt = arg;
	struct icl_cxgbei_conn *icc = NULL;
	struct icl_conn *ic;
	struct icl_pdu *ip;
	struct sockbuf *sb;
	STAILQ_HEAD(, icl_pdu) rx_pdus = STAILQ_HEAD_INITIALIZER(rx_pdus);

	MPASS(cwt != NULL);

	mtx_lock(&cwt->cwt_lock);
	MPASS(cwt->cwt_state == 0);
	cwt->cwt_state = CWT_RUNNING;
	cv_signal(&cwt->cwt_cv);

	while (__predict_true(cwt->cwt_state != CWT_STOP)) {
		cwt->cwt_state = CWT_RUNNING;
		while ((icc = TAILQ_FIRST(&cwt->rx_head)) != NULL) {
			TAILQ_REMOVE(&cwt->rx_head, icc, rx_link);
			mtx_unlock(&cwt->cwt_lock);

			ic = &icc->ic;
			sb = &ic->ic_socket->so_rcv;

			SOCKBUF_LOCK(sb);
			MPASS(icc->rx_flags & RXF_ACTIVE);
			if (__predict_true(!(sb->sb_state & SBS_CANTRCVMORE))) {
				MPASS(STAILQ_EMPTY(&rx_pdus));
				STAILQ_SWAP(&icc->rcvd_pdus, &rx_pdus, icl_pdu);
				SOCKBUF_UNLOCK(sb);

				/* Hand over PDUs to ICL. */
				while ((ip = STAILQ_FIRST(&rx_pdus)) != NULL) {
					STAILQ_REMOVE_HEAD(&rx_pdus, ip_next);
					ic->ic_receive(ip);
				}

				SOCKBUF_LOCK(sb);
				MPASS(STAILQ_EMPTY(&rx_pdus));
			}
			MPASS(icc->rx_flags & RXF_ACTIVE);
			if (STAILQ_EMPTY(&icc->rcvd_pdus) ||
			    __predict_false(sb->sb_state & SBS_CANTRCVMORE)) {
				icc->rx_flags &= ~RXF_ACTIVE;
			} else {
				/*
				 * More PDUs were received while we were busy
				 * handing over the previous batch to ICL.
				 * Re-add this connection to the end of the
				 * queue.
				 */
				mtx_lock(&cwt->cwt_lock);
				TAILQ_INSERT_TAIL(&cwt->rx_head, icc,
				    rx_link);
				mtx_unlock(&cwt->cwt_lock);
			}
			SOCKBUF_UNLOCK(sb);

			mtx_lock(&cwt->cwt_lock);
		}

		/* Inner loop doesn't check for CWT_STOP, do that first. */
		if (__predict_false(cwt->cwt_state == CWT_STOP))
			break;
		cwt->cwt_state = CWT_SLEEPING;
		cv_wait(&cwt->cwt_cv, &cwt->cwt_lock);
	}

	MPASS(TAILQ_FIRST(&cwt->rx_head) == NULL);
	mtx_assert(&cwt->cwt_lock, MA_OWNED);
	cwt->cwt_state = CWT_STOPPED;
	cv_signal(&cwt->cwt_cv);
	mtx_unlock(&cwt->cwt_lock);
	kthread_exit();
}

static int
start_worker_threads(void)
{
	int i, rc;
	struct cxgbei_worker_thread_softc *cwt;

	worker_thread_count = min(mp_ncpus, 32);
	cwt_softc = malloc(worker_thread_count * sizeof(*cwt), M_CXGBE,
	    M_WAITOK | M_ZERO);

	MPASS(cxgbei_proc == NULL);
	for (i = 0, cwt = &cwt_softc[0]; i < worker_thread_count; i++, cwt++) {
		mtx_init(&cwt->cwt_lock, "cwt lock", NULL, MTX_DEF);
		cv_init(&cwt->cwt_cv, "cwt cv");
		TAILQ_INIT(&cwt->rx_head);
		rc = kproc_kthread_add(cwt_main, cwt, &cxgbei_proc, NULL, 0, 0,
		    "cxgbei", "%d", i);
		if (rc != 0) {
			printf("cxgbei: failed to start thread #%d/%d (%d)\n",
			    i + 1, worker_thread_count, rc);
			mtx_destroy(&cwt->cwt_lock);
			cv_destroy(&cwt->cwt_cv);
			bzero(cwt, sizeof(*cwt));
			if (i == 0) {
				free(cwt_softc, M_CXGBE);
				worker_thread_count = 0;

				return (rc);
			}

			/* Not fatal, carry on with fewer threads. */
			worker_thread_count = i;
			rc = 0;
			break;
		}

		/* Wait for thread to start before moving on to the next one. */
		mtx_lock(&cwt->cwt_lock);
		while (cwt->cwt_state == 0)
			cv_wait(&cwt->cwt_cv, &cwt->cwt_lock);
		mtx_unlock(&cwt->cwt_lock);
	}

	MPASS(cwt_softc != NULL);
	MPASS(worker_thread_count > 0);
	return (0);
}

static void
stop_worker_threads(void)
{
	int i;
	struct cxgbei_worker_thread_softc *cwt = &cwt_softc[0];

	MPASS(worker_thread_count >= 0);

	for (i = 0, cwt = &cwt_softc[0]; i < worker_thread_count; i++, cwt++) {
		mtx_lock(&cwt->cwt_lock);
		MPASS(cwt->cwt_state == CWT_RUNNING ||
		    cwt->cwt_state == CWT_SLEEPING);
		cwt->cwt_state = CWT_STOP;
		cv_signal(&cwt->cwt_cv);
		do {
			cv_wait(&cwt->cwt_cv, &cwt->cwt_lock);
		} while (cwt->cwt_state != CWT_STOPPED);
		mtx_unlock(&cwt->cwt_lock);
	}
	free(cwt_softc, M_CXGBE);
}

/* Select a worker thread for a connection. */
u_int
cxgbei_select_worker_thread(struct icl_cxgbei_conn *icc)
{
	struct adapter *sc = icc->sc;
	struct toepcb *toep = icc->toep;
	u_int i, n;

	n = worker_thread_count / sc->sge.nofldrxq;
	if (n > 0)
		i = toep->vi->pi->port_id * n + arc4random() % n;
	else
		i = arc4random() % worker_thread_count;

	CTR3(KTR_CXGBE, "%s: tid %u, cwt %u", __func__, toep->tid, i);

	return (i);
}

static int
cxgbei_mod_load(void)
{
	int rc;

	t4_register_cpl_handler(CPL_ISCSI_HDR, do_rx_iscsi_hdr);
	t4_register_cpl_handler(CPL_ISCSI_DATA, do_rx_iscsi_data);
	t4_register_cpl_handler(CPL_RX_ISCSI_DDP, do_rx_iscsi_ddp);

	rc = start_worker_threads();
	if (rc != 0)
		return (rc);

	rc = t4_register_uld(&cxgbei_uld_info);
	if (rc != 0) {
		stop_worker_threads();
		return (rc);
	}

	t4_iterate(cxgbei_activate_all, NULL);

	return (rc);
}

static int
cxgbei_mod_unload(void)
{

	t4_iterate(cxgbei_deactivate_all, NULL);

	if (t4_unregister_uld(&cxgbei_uld_info) == EBUSY)
		return (EBUSY);

	stop_worker_threads();

	t4_register_cpl_handler(CPL_ISCSI_HDR, NULL);
	t4_register_cpl_handler(CPL_ISCSI_DATA, NULL);
	t4_register_cpl_handler(CPL_RX_ISCSI_DDP, NULL);

	return (0);
}
#endif

static int
cxgbei_modevent(module_t mod, int cmd, void *arg)
{
	int rc = 0;

#ifdef TCP_OFFLOAD
	switch (cmd) {
	case MOD_LOAD:
		rc = cxgbei_mod_load();
		if (rc == 0)
			rc = icl_cxgbei_mod_load();
		break;

	case MOD_UNLOAD:
		rc = icl_cxgbei_mod_unload();
		if (rc == 0)
			rc = cxgbei_mod_unload();
		break;

	default:
		rc = EINVAL;
	}
#else
	printf("cxgbei: compiled without TCP_OFFLOAD support.\n");
	rc = EOPNOTSUPP;
#endif

	return (rc);
}

static moduledata_t cxgbei_mod = {
	"cxgbei",
	cxgbei_modevent,
	NULL,
};

MODULE_VERSION(cxgbei, 1);
DECLARE_MODULE(cxgbei, cxgbei_mod, SI_SUB_EXEC, SI_ORDER_ANY);
MODULE_DEPEND(cxgbei, t4_tom, 1, 1, 1);
MODULE_DEPEND(cxgbei, cxgbe, 1, 1, 1);
MODULE_DEPEND(cxgbei, icl, 1, 1, 1);
