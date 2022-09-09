// SPDX-License-Identifier: GPL-2.0
/*
 * Shared Memory Communications over RDMA (SMC-R) and RoCE
 *
 * Work Requests exploiting Infiniband API
 *
 * Work requests (WR) of type ib_post_send or ib_post_recv respectively
 * are submitted to either RC SQ or RC RQ respectively
 * (reliably connected send/receive queue)
 * and become work queue entries (WQEs).
 * While an SQ WR/WQE is pending, we track it until transmission completion.
 * Through a send or receive completion queue (CQ) respectively,
 * we get completion queue entries (CQEs) [aka work completions (WCs)].
 * Since the CQ callback is called from IRQ context, we split work by using
 * bottom halves implemented by tasklets.
 *
 * SMC uses this to exchange LLC (link layer control)
 * and CDC (connection data control) messages.
 *
 * Copyright IBM Corp. 2016
 *
 * Author(s):  Steffen Maier <maier@linux.vnet.ibm.com>
 */

#include <linux/atomic.h>
#include <linux/hashtable.h>
#include <linux/wait.h>
#include <rdma/ib_verbs.h>
#include <asm/div64.h>

#include "smc.h"
#include "smc_wr.h"

#define SMC_WR_MAX_POLL_CQE 10	/* max. # of compl. queue elements in 1 poll */

#define SMC_WR_RX_HASH_BITS 4
static DEFINE_HASHTABLE(smc_wr_rx_hash, SMC_WR_RX_HASH_BITS);
static DEFINE_SPINLOCK(smc_wr_rx_hash_lock);

struct smc_wr_tx_pend {	/* control data for a pending send request */
	u64			wr_id;		/* work request id sent */
	smc_wr_tx_handler	handler;
	enum ib_wc_status	wc_status;	/* CQE status */
	struct smc_link		*link;
	u32			idx;
	struct smc_wr_tx_pend_priv priv;
	u8			compl_requested;
};

/******************************** send queue *********************************/

/*------------------------------- completion --------------------------------*/

/* returns true if at least one tx work request is pending on the given link */
static inline bool smc_wr_is_tx_pend(struct smc_link *link)
{
	return !bitmap_empty(link->wr_tx_mask, link->wr_tx_cnt);
}

/* wait till all pending tx work requests on the given link are completed */
void smc_wr_tx_wait_no_pending_sends(struct smc_link *link)
{
	wait_event(link->wr_tx_wait, !smc_wr_is_tx_pend(link));
}

static inline int smc_wr_tx_find_pending_index(struct smc_link *link, u64 wr_id)
{
	u32 i;

	for (i = 0; i < link->wr_tx_cnt; i++) {
		if (link->wr_tx_pends[i].wr_id == wr_id)
			return i;
	}
	return link->wr_tx_cnt;
}

static inline void smc_wr_tx_process_cqe(struct ib_wc *wc)
{
	struct smc_wr_tx_pend pnd_snd;
	struct smc_link *link;
	u32 pnd_snd_idx;

	link = wc->qp->qp_context;

	if (wc->opcode == IB_WC_REG_MR) {
		if (wc->status)
			link->wr_reg_state = FAILED;
		else
			link->wr_reg_state = CONFIRMED;
		smc_wr_wakeup_reg_wait(link);
		return;
	}

	pnd_snd_idx = smc_wr_tx_find_pending_index(link, wc->wr_id);
	if (pnd_snd_idx == link->wr_tx_cnt) {
		if (link->lgr->smc_version != SMC_V2 ||
		    link->wr_tx_v2_pend->wr_id != wc->wr_id)
			return;
		link->wr_tx_v2_pend->wc_status = wc->status;
		memcpy(&pnd_snd, link->wr_tx_v2_pend, sizeof(pnd_snd));
		/* clear the full struct smc_wr_tx_pend including .priv */
		memset(link->wr_tx_v2_pend, 0,
		       sizeof(*link->wr_tx_v2_pend));
		memset(link->lgr->wr_tx_buf_v2, 0,
		       sizeof(*link->lgr->wr_tx_buf_v2));
	} else {
		link->wr_tx_pends[pnd_snd_idx].wc_status = wc->status;
		if (link->wr_tx_pends[pnd_snd_idx].compl_requested)
			complete(&link->wr_tx_compl[pnd_snd_idx]);
		memcpy(&pnd_snd, &link->wr_tx_pends[pnd_snd_idx],
		       sizeof(pnd_snd));
		/* clear the full struct smc_wr_tx_pend including .priv */
		memset(&link->wr_tx_pends[pnd_snd_idx], 0,
		       sizeof(link->wr_tx_pends[pnd_snd_idx]));
		memset(&link->wr_tx_bufs[pnd_snd_idx], 0,
		       sizeof(link->wr_tx_bufs[pnd_snd_idx]));
		if (!test_and_clear_bit(pnd_snd_idx, link->wr_tx_mask))
			return;
	}

	if (wc->status) {
		if (link->lgr->smc_version == SMC_V2) {
			memset(link->wr_tx_v2_pend, 0,
			       sizeof(*link->wr_tx_v2_pend));
			memset(link->lgr->wr_tx_buf_v2, 0,
			       sizeof(*link->lgr->wr_tx_buf_v2));
		}
		/* terminate link */
		smcr_link_down_cond_sched(link);
	}
	if (pnd_snd.handler)
		pnd_snd.handler(&pnd_snd.priv, link, wc->status);
	wake_up(&link->wr_tx_wait);
}

static void smc_wr_tx_tasklet_fn(struct tasklet_struct *t)
{
	struct smc_ib_device *dev = from_tasklet(dev, t, send_tasklet);
	struct ib_wc wc[SMC_WR_MAX_POLL_CQE];
	int i = 0, rc;
	int polled = 0;

again:
	polled++;
	do {
		memset(&wc, 0, sizeof(wc));
		rc = ib_poll_cq(dev->roce_cq_send, SMC_WR_MAX_POLL_CQE, wc);
		if (polled == 1) {
			ib_req_notify_cq(dev->roce_cq_send,
					 IB_CQ_NEXT_COMP |
					 IB_CQ_REPORT_MISSED_EVENTS);
		}
		if (!rc)
			break;
		for (i = 0; i < rc; i++)
			smc_wr_tx_process_cqe(&wc[i]);
	} while (rc > 0);
	if (polled == 1)
		goto again;
}

void smc_wr_tx_cq_handler(struct ib_cq *ib_cq, void *cq_context)
{
	struct smc_ib_device *dev = (struct smc_ib_device *)cq_context;

	tasklet_schedule(&dev->send_tasklet);
}

/*---------------------------- request submission ---------------------------*/

static inline int smc_wr_tx_get_free_slot_index(struct smc_link *link, u32 *idx)
{
	*idx = link->wr_tx_cnt;
	if (!smc_link_sendable(link))
		return -ENOLINK;
	for_each_clear_bit(*idx, link->wr_tx_mask, link->wr_tx_cnt) {
		if (!test_and_set_bit(*idx, link->wr_tx_mask))
			return 0;
	}
	*idx = link->wr_tx_cnt;
	return -EBUSY;
}

/**
 * smc_wr_tx_get_free_slot() - returns buffer for message assembly,
 *			and sets info for pending transmit tracking
 * @link:		Pointer to smc_link used to later send the message.
 * @handler:		Send completion handler function pointer.
 * @wr_buf:		Out value returns pointer to message buffer.
 * @wr_rdma_buf:	Out value returns pointer to rdma work request.
 * @wr_pend_priv:	Out value returns pointer serving as handler context.
 *
 * Return: 0 on success, or -errno on error.
 */
int smc_wr_tx_get_free_slot(struct smc_link *link,
			    smc_wr_tx_handler handler,
			    struct smc_wr_buf **wr_buf,
			    struct smc_rdma_wr **wr_rdma_buf,
			    struct smc_wr_tx_pend_priv **wr_pend_priv)
{
	struct smc_link_group *lgr = smc_get_lgr(link);
	struct smc_wr_tx_pend *wr_pend;
	u32 idx = link->wr_tx_cnt;
	struct ib_send_wr *wr_ib;
	u64 wr_id;
	int rc;

	*wr_buf = NULL;
	*wr_pend_priv = NULL;
	if (in_softirq() || lgr->terminating) {
		rc = smc_wr_tx_get_free_slot_index(link, &idx);
		if (rc)
			return rc;
	} else {
		rc = wait_event_interruptible_timeout(
			link->wr_tx_wait,
			!smc_link_sendable(link) ||
			lgr->terminating ||
			(smc_wr_tx_get_free_slot_index(link, &idx) != -EBUSY),
			SMC_WR_TX_WAIT_FREE_SLOT_TIME);
		if (!rc) {
			/* timeout - terminate link */
			smcr_link_down_cond_sched(link);
			return -EPIPE;
		}
		if (idx == link->wr_tx_cnt)
			return -EPIPE;
	}
	wr_id = smc_wr_tx_get_next_wr_id(link);
	wr_pend = &link->wr_tx_pends[idx];
	wr_pend->wr_id = wr_id;
	wr_pend->handler = handler;
	wr_pend->link = link;
	wr_pend->idx = idx;
	wr_ib = &link->wr_tx_ibs[idx];
	wr_ib->wr_id = wr_id;
	*wr_buf = &link->wr_tx_bufs[idx];
	if (wr_rdma_buf)
		*wr_rdma_buf = &link->wr_tx_rdmas[idx];
	*wr_pend_priv = &wr_pend->priv;
	return 0;
}

int smc_wr_tx_get_v2_slot(struct smc_link *link,
			  smc_wr_tx_handler handler,
			  struct smc_wr_v2_buf **wr_buf,
			  struct smc_wr_tx_pend_priv **wr_pend_priv)
{
	struct smc_wr_tx_pend *wr_pend;
	struct ib_send_wr *wr_ib;
	u64 wr_id;

	if (link->wr_tx_v2_pend->idx == link->wr_tx_cnt)
		return -EBUSY;

	*wr_buf = NULL;
	*wr_pend_priv = NULL;
	wr_id = smc_wr_tx_get_next_wr_id(link);
	wr_pend = link->wr_tx_v2_pend;
	wr_pend->wr_id = wr_id;
	wr_pend->handler = handler;
	wr_pend->link = link;
	wr_pend->idx = link->wr_tx_cnt;
	wr_ib = link->wr_tx_v2_ib;
	wr_ib->wr_id = wr_id;
	*wr_buf = link->lgr->wr_tx_buf_v2;
	*wr_pend_priv = &wr_pend->priv;
	return 0;
}

int smc_wr_tx_put_slot(struct smc_link *link,
		       struct smc_wr_tx_pend_priv *wr_pend_priv)
{
	struct smc_wr_tx_pend *pend;

	pend = container_of(wr_pend_priv, struct smc_wr_tx_pend, priv);
	if (pend->idx < link->wr_tx_cnt) {
		u32 idx = pend->idx;

		/* clear the full struct smc_wr_tx_pend including .priv */
		memset(&link->wr_tx_pends[idx], 0,
		       sizeof(link->wr_tx_pends[idx]));
		memset(&link->wr_tx_bufs[idx], 0,
		       sizeof(link->wr_tx_bufs[idx]));
		test_and_clear_bit(idx, link->wr_tx_mask);
		wake_up(&link->wr_tx_wait);
		return 1;
	} else if (link->lgr->smc_version == SMC_V2 &&
		   pend->idx == link->wr_tx_cnt) {
		/* Large v2 buffer */
		memset(&link->wr_tx_v2_pend, 0,
		       sizeof(link->wr_tx_v2_pend));
		memset(&link->lgr->wr_tx_buf_v2, 0,
		       sizeof(link->lgr->wr_tx_buf_v2));
		return 1;
	}

	return 0;
}

/* Send prepared WR slot via ib_post_send.
 * @priv: pointer to smc_wr_tx_pend_priv identifying prepared message buffer
 */
int smc_wr_tx_send(struct smc_link *link, struct smc_wr_tx_pend_priv *priv)
{
	struct smc_wr_tx_pend *pend;
	int rc;

	ib_req_notify_cq(link->smcibdev->roce_cq_send,
			 IB_CQ_NEXT_COMP | IB_CQ_REPORT_MISSED_EVENTS);
	pend = container_of(priv, struct smc_wr_tx_pend, priv);
	rc = ib_post_send(link->roce_qp, &link->wr_tx_ibs[pend->idx], NULL);
	if (rc) {
		smc_wr_tx_put_slot(link, priv);
		smcr_link_down_cond_sched(link);
	}
	return rc;
}

int smc_wr_tx_v2_send(struct smc_link *link, struct smc_wr_tx_pend_priv *priv,
		      int len)
{
	int rc;

	link->wr_tx_v2_ib->sg_list[0].length = len;
	ib_req_notify_cq(link->smcibdev->roce_cq_send,
			 IB_CQ_NEXT_COMP | IB_CQ_REPORT_MISSED_EVENTS);
	rc = ib_post_send(link->roce_qp, link->wr_tx_v2_ib, NULL);
	if (rc) {
		smc_wr_tx_put_slot(link, priv);
		smcr_link_down_cond_sched(link);
	}
	return rc;
}

/* Send prepared WR slot via ib_post_send and wait for send completion
 * notification.
 * @priv: pointer to smc_wr_tx_pend_priv identifying prepared message buffer
 */
int smc_wr_tx_send_wait(struct smc_link *link, struct smc_wr_tx_pend_priv *priv,
			unsigned long timeout)
{
	struct smc_wr_tx_pend *pend;
	u32 pnd_idx;
	int rc;

	pend = container_of(priv, struct smc_wr_tx_pend, priv);
	pend->compl_requested = 1;
	pnd_idx = pend->idx;
	init_completion(&link->wr_tx_compl[pnd_idx]);

	rc = smc_wr_tx_send(link, priv);
	if (rc)
		return rc;
	/* wait for completion by smc_wr_tx_process_cqe() */
	rc = wait_for_completion_interruptible_timeout(
					&link->wr_tx_compl[pnd_idx], timeout);
	if (rc <= 0)
		rc = -ENODATA;
	if (rc > 0)
		rc = 0;
	return rc;
}

/* Register a memory region and wait for result. */
int smc_wr_reg_send(struct smc_link *link, struct ib_mr *mr)
{
	int rc;

	ib_req_notify_cq(link->smcibdev->roce_cq_send,
			 IB_CQ_NEXT_COMP | IB_CQ_REPORT_MISSED_EVENTS);
	link->wr_reg_state = POSTED;
	link->wr_reg.wr.wr_id = (u64)(uintptr_t)mr;
	link->wr_reg.mr = mr;
	link->wr_reg.key = mr->rkey;
	rc = ib_post_send(link->roce_qp, &link->wr_reg.wr, NULL);
	if (rc)
		return rc;

	atomic_inc(&link->wr_reg_refcnt);
	rc = wait_event_interruptible_timeout(link->wr_reg_wait,
					      (link->wr_reg_state != POSTED),
					      SMC_WR_REG_MR_WAIT_TIME);
	if (atomic_dec_and_test(&link->wr_reg_refcnt))
		wake_up_all(&link->wr_reg_wait);
	if (!rc) {
		/* timeout - terminate link */
		smcr_link_down_cond_sched(link);
		return -EPIPE;
	}
	if (rc == -ERESTARTSYS)
		return -EINTR;
	switch (link->wr_reg_state) {
	case CONFIRMED:
		rc = 0;
		break;
	case FAILED:
		rc = -EIO;
		break;
	case POSTED:
		rc = -EPIPE;
		break;
	}
	return rc;
}

/****************************** receive queue ********************************/

int smc_wr_rx_register_handler(struct smc_wr_rx_handler *handler)
{
	struct smc_wr_rx_handler *h_iter;
	int rc = 0;

	spin_lock(&smc_wr_rx_hash_lock);
	hash_for_each_possible(smc_wr_rx_hash, h_iter, list, handler->type) {
		if (h_iter->type == handler->type) {
			rc = -EEXIST;
			goto out_unlock;
		}
	}
	hash_add(smc_wr_rx_hash, &handler->list, handler->type);
out_unlock:
	spin_unlock(&smc_wr_rx_hash_lock);
	return rc;
}

/* Demultiplex a received work request based on the message type to its handler.
 * Relies on smc_wr_rx_hash having been completely filled before any IB WRs,
 * and not being modified any more afterwards so we don't need to lock it.
 */
static inline void smc_wr_rx_demultiplex(struct ib_wc *wc)
{
	struct smc_link *link = (struct smc_link *)wc->qp->qp_context;
	struct smc_wr_rx_handler *handler;
	struct smc_wr_rx_hdr *wr_rx;
	u64 temp_wr_id;
	u32 index;

	if (wc->byte_len < sizeof(*wr_rx))
		return; /* short message */
	temp_wr_id = wc->wr_id;
	index = do_div(temp_wr_id, link->wr_rx_cnt);
	wr_rx = (struct smc_wr_rx_hdr *)&link->wr_rx_bufs[index];
	hash_for_each_possible(smc_wr_rx_hash, handler, list, wr_rx->type) {
		if (handler->type == wr_rx->type)
			handler->handler(wc, wr_rx);
	}
}

static inline void smc_wr_rx_process_cqes(struct ib_wc wc[], int num)
{
	struct smc_link *link;
	int i;

	for (i = 0; i < num; i++) {
		link = wc[i].qp->qp_context;
		link->wr_rx_id_compl = wc[i].wr_id;
		if (wc[i].status == IB_WC_SUCCESS) {
			link->wr_rx_tstamp = jiffies;
			smc_wr_rx_demultiplex(&wc[i]);
			smc_wr_rx_post(link); /* refill WR RX */
		} else {
			/* handle status errors */
			switch (wc[i].status) {
			case IB_WC_RETRY_EXC_ERR:
			case IB_WC_RNR_RETRY_EXC_ERR:
			case IB_WC_WR_FLUSH_ERR:
				smcr_link_down_cond_sched(link);
				if (link->wr_rx_id_compl == link->wr_rx_id)
					wake_up(&link->wr_rx_empty_wait);
				break;
			default:
				smc_wr_rx_post(link); /* refill WR RX */
				break;
			}
		}
	}
}

static void smc_wr_rx_tasklet_fn(struct tasklet_struct *t)
{
	struct smc_ib_device *dev = from_tasklet(dev, t, recv_tasklet);
	struct ib_wc wc[SMC_WR_MAX_POLL_CQE];
	int polled = 0;
	int rc;

again:
	polled++;
	do {
		memset(&wc, 0, sizeof(wc));
		rc = ib_poll_cq(dev->roce_cq_recv, SMC_WR_MAX_POLL_CQE, wc);
		if (polled == 1) {
			ib_req_notify_cq(dev->roce_cq_recv,
					 IB_CQ_SOLICITED_MASK
					 | IB_CQ_REPORT_MISSED_EVENTS);
		}
		if (!rc)
			break;
		smc_wr_rx_process_cqes(&wc[0], rc);
	} while (rc > 0);
	if (polled == 1)
		goto again;
}

void smc_wr_rx_cq_handler(struct ib_cq *ib_cq, void *cq_context)
{
	struct smc_ib_device *dev = (struct smc_ib_device *)cq_context;

	tasklet_schedule(&dev->recv_tasklet);
}

int smc_wr_rx_post_init(struct smc_link *link)
{
	u32 i;
	int rc = 0;

	for (i = 0; i < link->wr_rx_cnt; i++)
		rc = smc_wr_rx_post(link);
	return rc;
}

/***************************** init, exit, misc ******************************/

void smc_wr_remember_qp_attr(struct smc_link *lnk)
{
	struct ib_qp_attr *attr = &lnk->qp_attr;
	struct ib_qp_init_attr init_attr;

	memset(attr, 0, sizeof(*attr));
	memset(&init_attr, 0, sizeof(init_attr));
	ib_query_qp(lnk->roce_qp, attr,
		    IB_QP_STATE |
		    IB_QP_CUR_STATE |
		    IB_QP_PKEY_INDEX |
		    IB_QP_PORT |
		    IB_QP_QKEY |
		    IB_QP_AV |
		    IB_QP_PATH_MTU |
		    IB_QP_TIMEOUT |
		    IB_QP_RETRY_CNT |
		    IB_QP_RNR_RETRY |
		    IB_QP_RQ_PSN |
		    IB_QP_ALT_PATH |
		    IB_QP_MIN_RNR_TIMER |
		    IB_QP_SQ_PSN |
		    IB_QP_PATH_MIG_STATE |
		    IB_QP_CAP |
		    IB_QP_DEST_QPN,
		    &init_attr);

	lnk->wr_tx_cnt = min_t(size_t, SMC_WR_BUF_CNT,
			       lnk->qp_attr.cap.max_send_wr);
	lnk->wr_rx_cnt = min_t(size_t, SMC_WR_BUF_CNT * 3,
			       lnk->qp_attr.cap.max_recv_wr);
}

static void smc_wr_init_sge(struct smc_link *lnk)
{
	int sges_per_buf = (lnk->lgr->smc_version == SMC_V2) ? 2 : 1;
	bool send_inline = (lnk->qp_attr.cap.max_inline_data > SMC_WR_TX_SIZE);
	u32 i;

	for (i = 0; i < lnk->wr_tx_cnt; i++) {
		lnk->wr_tx_sges[i].addr = send_inline ? (uintptr_t)(&lnk->wr_tx_bufs[i]) :
			lnk->wr_tx_dma_addr + i * SMC_WR_BUF_SIZE;
		lnk->wr_tx_sges[i].length = SMC_WR_TX_SIZE;
		lnk->wr_tx_sges[i].lkey = lnk->roce_pd->local_dma_lkey;
		lnk->wr_tx_rdma_sges[i].tx_rdma_sge[0].wr_tx_rdma_sge[0].lkey =
			lnk->roce_pd->local_dma_lkey;
		lnk->wr_tx_rdma_sges[i].tx_rdma_sge[0].wr_tx_rdma_sge[1].lkey =
			lnk->roce_pd->local_dma_lkey;
		lnk->wr_tx_rdma_sges[i].tx_rdma_sge[1].wr_tx_rdma_sge[0].lkey =
			lnk->roce_pd->local_dma_lkey;
		lnk->wr_tx_rdma_sges[i].tx_rdma_sge[1].wr_tx_rdma_sge[1].lkey =
			lnk->roce_pd->local_dma_lkey;
		lnk->wr_tx_ibs[i].next = NULL;
		lnk->wr_tx_ibs[i].sg_list = &lnk->wr_tx_sges[i];
		lnk->wr_tx_ibs[i].num_sge = 1;
		lnk->wr_tx_ibs[i].opcode = IB_WR_SEND;
		lnk->wr_tx_ibs[i].send_flags =
			IB_SEND_SIGNALED | IB_SEND_SOLICITED;
		if (send_inline)
			lnk->wr_tx_ibs[i].send_flags |= IB_SEND_INLINE;
		lnk->wr_tx_rdmas[i].wr_tx_rdma[0].wr.opcode = IB_WR_RDMA_WRITE;
		lnk->wr_tx_rdmas[i].wr_tx_rdma[1].wr.opcode = IB_WR_RDMA_WRITE;
		lnk->wr_tx_rdmas[i].wr_tx_rdma[0].wr.sg_list =
			lnk->wr_tx_rdma_sges[i].tx_rdma_sge[0].wr_tx_rdma_sge;
		lnk->wr_tx_rdmas[i].wr_tx_rdma[1].wr.sg_list =
			lnk->wr_tx_rdma_sges[i].tx_rdma_sge[1].wr_tx_rdma_sge;
	}

	if (lnk->lgr->smc_version == SMC_V2) {
		lnk->wr_tx_v2_sge->addr = lnk->wr_tx_v2_dma_addr;
		lnk->wr_tx_v2_sge->length = SMC_WR_BUF_V2_SIZE;
		lnk->wr_tx_v2_sge->lkey = lnk->roce_pd->local_dma_lkey;

		lnk->wr_tx_v2_ib->next = NULL;
		lnk->wr_tx_v2_ib->sg_list = lnk->wr_tx_v2_sge;
		lnk->wr_tx_v2_ib->num_sge = 1;
		lnk->wr_tx_v2_ib->opcode = IB_WR_SEND;
		lnk->wr_tx_v2_ib->send_flags =
			IB_SEND_SIGNALED | IB_SEND_SOLICITED;
	}

	/* With SMC-Rv2 there can be messages larger than SMC_WR_TX_SIZE.
	 * Each ib_recv_wr gets 2 sges, the second one is a spillover buffer
	 * and the same buffer for all sges. When a larger message arrived then
	 * the content of the first small sge is copied to the beginning of
	 * the larger spillover buffer, allowing easy data mapping.
	 */
	for (i = 0; i < lnk->wr_rx_cnt; i++) {
		int x = i * sges_per_buf;

		lnk->wr_rx_sges[x].addr =
			lnk->wr_rx_dma_addr + i * SMC_WR_BUF_SIZE;
		lnk->wr_rx_sges[x].length = SMC_WR_TX_SIZE;
		lnk->wr_rx_sges[x].lkey = lnk->roce_pd->local_dma_lkey;
		if (lnk->lgr->smc_version == SMC_V2) {
			lnk->wr_rx_sges[x + 1].addr =
					lnk->wr_rx_v2_dma_addr + SMC_WR_TX_SIZE;
			lnk->wr_rx_sges[x + 1].length =
					SMC_WR_BUF_V2_SIZE - SMC_WR_TX_SIZE;
			lnk->wr_rx_sges[x + 1].lkey =
					lnk->roce_pd->local_dma_lkey;
		}
		lnk->wr_rx_ibs[i].next = NULL;
		lnk->wr_rx_ibs[i].sg_list = &lnk->wr_rx_sges[x];
		lnk->wr_rx_ibs[i].num_sge = sges_per_buf;
	}
	lnk->wr_reg.wr.next = NULL;
	lnk->wr_reg.wr.num_sge = 0;
	lnk->wr_reg.wr.send_flags = IB_SEND_SIGNALED;
	lnk->wr_reg.wr.opcode = IB_WR_REG_MR;
	lnk->wr_reg.access = IB_ACCESS_LOCAL_WRITE | IB_ACCESS_REMOTE_WRITE;
}

void smc_wr_free_link(struct smc_link *lnk)
{
	struct ib_device *ibdev;

	if (!lnk->smcibdev)
		return;
	ibdev = lnk->smcibdev->ibdev;

	smc_wr_drain_cq(lnk);
	smc_wr_wakeup_reg_wait(lnk);
	smc_wr_wakeup_tx_wait(lnk);

	smc_wr_tx_wait_no_pending_sends(lnk);
	wait_event(lnk->wr_reg_wait, (!atomic_read(&lnk->wr_reg_refcnt)));
	wait_event(lnk->wr_tx_wait, (!atomic_read(&lnk->wr_tx_refcnt)));

	if (lnk->wr_rx_dma_addr) {
		ib_dma_unmap_single(ibdev, lnk->wr_rx_dma_addr,
				    SMC_WR_BUF_SIZE * lnk->wr_rx_cnt,
				    DMA_FROM_DEVICE);
		lnk->wr_rx_dma_addr = 0;
	}
	if (lnk->wr_rx_v2_dma_addr) {
		ib_dma_unmap_single(ibdev, lnk->wr_rx_v2_dma_addr,
				    SMC_WR_BUF_V2_SIZE,
				    DMA_FROM_DEVICE);
		lnk->wr_rx_v2_dma_addr = 0;
	}
	if (lnk->wr_tx_dma_addr) {
		ib_dma_unmap_single(ibdev, lnk->wr_tx_dma_addr,
				    SMC_WR_BUF_SIZE * lnk->wr_tx_cnt,
				    DMA_TO_DEVICE);
		lnk->wr_tx_dma_addr = 0;
	}
	if (lnk->wr_tx_v2_dma_addr) {
		ib_dma_unmap_single(ibdev, lnk->wr_tx_v2_dma_addr,
				    SMC_WR_BUF_V2_SIZE,
				    DMA_TO_DEVICE);
		lnk->wr_tx_v2_dma_addr = 0;
	}
}

void smc_wr_free_lgr_mem(struct smc_link_group *lgr)
{
	if (lgr->smc_version < SMC_V2)
		return;

	kfree(lgr->wr_rx_buf_v2);
	lgr->wr_rx_buf_v2 = NULL;
	kfree(lgr->wr_tx_buf_v2);
	lgr->wr_tx_buf_v2 = NULL;
}

void smc_wr_free_link_mem(struct smc_link *lnk)
{
	kfree(lnk->wr_tx_v2_ib);
	lnk->wr_tx_v2_ib = NULL;
	kfree(lnk->wr_tx_v2_sge);
	lnk->wr_tx_v2_sge = NULL;
	kfree(lnk->wr_tx_v2_pend);
	lnk->wr_tx_v2_pend = NULL;
	kfree(lnk->wr_tx_compl);
	lnk->wr_tx_compl = NULL;
	kfree(lnk->wr_tx_pends);
	lnk->wr_tx_pends = NULL;
	bitmap_free(lnk->wr_tx_mask);
	lnk->wr_tx_mask = NULL;
	kfree(lnk->wr_tx_sges);
	lnk->wr_tx_sges = NULL;
	kfree(lnk->wr_tx_rdma_sges);
	lnk->wr_tx_rdma_sges = NULL;
	kfree(lnk->wr_rx_sges);
	lnk->wr_rx_sges = NULL;
	kfree(lnk->wr_tx_rdmas);
	lnk->wr_tx_rdmas = NULL;
	kfree(lnk->wr_rx_ibs);
	lnk->wr_rx_ibs = NULL;
	kfree(lnk->wr_tx_ibs);
	lnk->wr_tx_ibs = NULL;
	kfree(lnk->wr_tx_bufs);
	lnk->wr_tx_bufs = NULL;
	kfree(lnk->wr_rx_bufs);
	lnk->wr_rx_bufs = NULL;
}

int smc_wr_alloc_lgr_mem(struct smc_link_group *lgr)
{
	if (lgr->smc_version < SMC_V2)
		return 0;

	lgr->wr_rx_buf_v2 = kzalloc(SMC_WR_BUF_V2_SIZE, GFP_KERNEL);
	if (!lgr->wr_rx_buf_v2)
		return -ENOMEM;
	lgr->wr_tx_buf_v2 = kzalloc(SMC_WR_BUF_V2_SIZE, GFP_KERNEL);
	if (!lgr->wr_tx_buf_v2) {
		kfree(lgr->wr_rx_buf_v2);
		return -ENOMEM;
	}
	return 0;
}

int smc_wr_alloc_link_mem(struct smc_link *link)
{
	int sges_per_buf = link->lgr->smc_version == SMC_V2 ? 2 : 1;

	/* allocate link related memory */
	link->wr_tx_bufs = kcalloc(SMC_WR_BUF_CNT, SMC_WR_BUF_SIZE, GFP_KERNEL);
	if (!link->wr_tx_bufs)
		goto no_mem;
	link->wr_rx_bufs = kcalloc(SMC_WR_BUF_CNT * 3, SMC_WR_BUF_SIZE,
				   GFP_KERNEL);
	if (!link->wr_rx_bufs)
		goto no_mem_wr_tx_bufs;
	link->wr_tx_ibs = kcalloc(SMC_WR_BUF_CNT, sizeof(link->wr_tx_ibs[0]),
				  GFP_KERNEL);
	if (!link->wr_tx_ibs)
		goto no_mem_wr_rx_bufs;
	link->wr_rx_ibs = kcalloc(SMC_WR_BUF_CNT * 3,
				  sizeof(link->wr_rx_ibs[0]),
				  GFP_KERNEL);
	if (!link->wr_rx_ibs)
		goto no_mem_wr_tx_ibs;
	link->wr_tx_rdmas = kcalloc(SMC_WR_BUF_CNT,
				    sizeof(link->wr_tx_rdmas[0]),
				    GFP_KERNEL);
	if (!link->wr_tx_rdmas)
		goto no_mem_wr_rx_ibs;
	link->wr_tx_rdma_sges = kcalloc(SMC_WR_BUF_CNT,
					sizeof(link->wr_tx_rdma_sges[0]),
					GFP_KERNEL);
	if (!link->wr_tx_rdma_sges)
		goto no_mem_wr_tx_rdmas;
	link->wr_tx_sges = kcalloc(SMC_WR_BUF_CNT, sizeof(link->wr_tx_sges[0]),
				   GFP_KERNEL);
	if (!link->wr_tx_sges)
		goto no_mem_wr_tx_rdma_sges;
	link->wr_rx_sges = kcalloc(SMC_WR_BUF_CNT * 3,
				   sizeof(link->wr_rx_sges[0]) * sges_per_buf,
				   GFP_KERNEL);
	if (!link->wr_rx_sges)
		goto no_mem_wr_tx_sges;
	link->wr_tx_mask = bitmap_zalloc(SMC_WR_BUF_CNT, GFP_KERNEL);
	if (!link->wr_tx_mask)
		goto no_mem_wr_rx_sges;
	link->wr_tx_pends = kcalloc(SMC_WR_BUF_CNT,
				    sizeof(link->wr_tx_pends[0]),
				    GFP_KERNEL);
	if (!link->wr_tx_pends)
		goto no_mem_wr_tx_mask;
	link->wr_tx_compl = kcalloc(SMC_WR_BUF_CNT,
				    sizeof(link->wr_tx_compl[0]),
				    GFP_KERNEL);
	if (!link->wr_tx_compl)
		goto no_mem_wr_tx_pends;

	if (link->lgr->smc_version == SMC_V2) {
		link->wr_tx_v2_ib = kzalloc(sizeof(*link->wr_tx_v2_ib),
					    GFP_KERNEL);
		if (!link->wr_tx_v2_ib)
			goto no_mem_tx_compl;
		link->wr_tx_v2_sge = kzalloc(sizeof(*link->wr_tx_v2_sge),
					     GFP_KERNEL);
		if (!link->wr_tx_v2_sge)
			goto no_mem_v2_ib;
		link->wr_tx_v2_pend = kzalloc(sizeof(*link->wr_tx_v2_pend),
					      GFP_KERNEL);
		if (!link->wr_tx_v2_pend)
			goto no_mem_v2_sge;
	}
	return 0;

no_mem_v2_sge:
	kfree(link->wr_tx_v2_sge);
no_mem_v2_ib:
	kfree(link->wr_tx_v2_ib);
no_mem_tx_compl:
	kfree(link->wr_tx_compl);
no_mem_wr_tx_pends:
	kfree(link->wr_tx_pends);
no_mem_wr_tx_mask:
	kfree(link->wr_tx_mask);
no_mem_wr_rx_sges:
	kfree(link->wr_rx_sges);
no_mem_wr_tx_sges:
	kfree(link->wr_tx_sges);
no_mem_wr_tx_rdma_sges:
	kfree(link->wr_tx_rdma_sges);
no_mem_wr_tx_rdmas:
	kfree(link->wr_tx_rdmas);
no_mem_wr_rx_ibs:
	kfree(link->wr_rx_ibs);
no_mem_wr_tx_ibs:
	kfree(link->wr_tx_ibs);
no_mem_wr_rx_bufs:
	kfree(link->wr_rx_bufs);
no_mem_wr_tx_bufs:
	kfree(link->wr_tx_bufs);
no_mem:
	return -ENOMEM;
}

void smc_wr_remove_dev(struct smc_ib_device *smcibdev)
{
	tasklet_kill(&smcibdev->recv_tasklet);
	tasklet_kill(&smcibdev->send_tasklet);
}

void smc_wr_add_dev(struct smc_ib_device *smcibdev)
{
	tasklet_setup(&smcibdev->recv_tasklet, smc_wr_rx_tasklet_fn);
	tasklet_setup(&smcibdev->send_tasklet, smc_wr_tx_tasklet_fn);
}

int smc_wr_create_link(struct smc_link *lnk)
{
	struct ib_device *ibdev = lnk->smcibdev->ibdev;
	int rc = 0;

	smc_wr_tx_set_wr_id(&lnk->wr_tx_id, 0);
	lnk->wr_rx_id = 0;
	lnk->wr_rx_dma_addr = ib_dma_map_single(
		ibdev, lnk->wr_rx_bufs,	SMC_WR_BUF_SIZE * lnk->wr_rx_cnt,
		DMA_FROM_DEVICE);
	if (ib_dma_mapping_error(ibdev, lnk->wr_rx_dma_addr)) {
		lnk->wr_rx_dma_addr = 0;
		rc = -EIO;
		goto out;
	}
	if (lnk->lgr->smc_version == SMC_V2) {
		lnk->wr_rx_v2_dma_addr = ib_dma_map_single(ibdev,
			lnk->lgr->wr_rx_buf_v2, SMC_WR_BUF_V2_SIZE,
			DMA_FROM_DEVICE);
		if (ib_dma_mapping_error(ibdev, lnk->wr_rx_v2_dma_addr)) {
			lnk->wr_rx_v2_dma_addr = 0;
			rc = -EIO;
			goto dma_unmap;
		}
		lnk->wr_tx_v2_dma_addr = ib_dma_map_single(ibdev,
			lnk->lgr->wr_tx_buf_v2, SMC_WR_BUF_V2_SIZE,
			DMA_TO_DEVICE);
		if (ib_dma_mapping_error(ibdev, lnk->wr_tx_v2_dma_addr)) {
			lnk->wr_tx_v2_dma_addr = 0;
			rc = -EIO;
			goto dma_unmap;
		}
	}
	lnk->wr_tx_dma_addr = ib_dma_map_single(
		ibdev, lnk->wr_tx_bufs,	SMC_WR_BUF_SIZE * lnk->wr_tx_cnt,
		DMA_TO_DEVICE);
	if (ib_dma_mapping_error(ibdev, lnk->wr_tx_dma_addr)) {
		rc = -EIO;
		goto dma_unmap;
	}
	smc_wr_init_sge(lnk);
	bitmap_zero(lnk->wr_tx_mask, SMC_WR_BUF_CNT);
	init_waitqueue_head(&lnk->wr_tx_wait);
	atomic_set(&lnk->wr_tx_refcnt, 0);
	init_waitqueue_head(&lnk->wr_reg_wait);
	atomic_set(&lnk->wr_reg_refcnt, 0);
	init_waitqueue_head(&lnk->wr_rx_empty_wait);
	return rc;

dma_unmap:
	if (lnk->wr_rx_v2_dma_addr) {
		ib_dma_unmap_single(ibdev, lnk->wr_rx_v2_dma_addr,
				    SMC_WR_BUF_V2_SIZE,
				    DMA_FROM_DEVICE);
		lnk->wr_rx_v2_dma_addr = 0;
	}
	if (lnk->wr_tx_v2_dma_addr) {
		ib_dma_unmap_single(ibdev, lnk->wr_tx_v2_dma_addr,
				    SMC_WR_BUF_V2_SIZE,
				    DMA_TO_DEVICE);
		lnk->wr_tx_v2_dma_addr = 0;
	}
	ib_dma_unmap_single(ibdev, lnk->wr_rx_dma_addr,
			    SMC_WR_BUF_SIZE * lnk->wr_rx_cnt,
			    DMA_FROM_DEVICE);
	lnk->wr_rx_dma_addr = 0;
out:
	return rc;
}
