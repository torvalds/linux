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
};

/******************************** send queue *********************************/

/*------------------------------- completion --------------------------------*/

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
	int i;

	link = wc->qp->qp_context;

	if (wc->opcode == IB_WC_REG_MR) {
		if (wc->status)
			link->wr_reg_state = FAILED;
		else
			link->wr_reg_state = CONFIRMED;
		wake_up(&link->wr_reg_wait);
		return;
	}

	pnd_snd_idx = smc_wr_tx_find_pending_index(link, wc->wr_id);
	if (pnd_snd_idx == link->wr_tx_cnt)
		return;
	link->wr_tx_pends[pnd_snd_idx].wc_status = wc->status;
	memcpy(&pnd_snd, &link->wr_tx_pends[pnd_snd_idx], sizeof(pnd_snd));
	/* clear the full struct smc_wr_tx_pend including .priv */
	memset(&link->wr_tx_pends[pnd_snd_idx], 0,
	       sizeof(link->wr_tx_pends[pnd_snd_idx]));
	memset(&link->wr_tx_bufs[pnd_snd_idx], 0,
	       sizeof(link->wr_tx_bufs[pnd_snd_idx]));
	if (!test_and_clear_bit(pnd_snd_idx, link->wr_tx_mask))
		return;
	if (wc->status) {
		for_each_set_bit(i, link->wr_tx_mask, link->wr_tx_cnt) {
			/* clear full struct smc_wr_tx_pend including .priv */
			memset(&link->wr_tx_pends[i], 0,
			       sizeof(link->wr_tx_pends[i]));
			memset(&link->wr_tx_bufs[i], 0,
			       sizeof(link->wr_tx_bufs[i]));
			clear_bit(i, link->wr_tx_mask);
		}
		/* terminate connections of this link group abnormally */
		smc_lgr_terminate_sched(smc_get_lgr(link));
	}
	if (pnd_snd.handler)
		pnd_snd.handler(&pnd_snd.priv, link, wc->status);
	wake_up(&link->wr_tx_wait);
}

static void smc_wr_tx_tasklet_fn(unsigned long data)
{
	struct smc_ib_device *dev = (struct smc_ib_device *)data;
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
	struct smc_wr_tx_pend *wr_pend;
	u32 idx = link->wr_tx_cnt;
	struct ib_send_wr *wr_ib;
	u64 wr_id;
	int rc;

	*wr_buf = NULL;
	*wr_pend_priv = NULL;
	if (in_softirq()) {
		rc = smc_wr_tx_get_free_slot_index(link, &idx);
		if (rc)
			return rc;
	} else {
		rc = wait_event_timeout(
			link->wr_tx_wait,
			link->state == SMC_LNK_INACTIVE ||
			(smc_wr_tx_get_free_slot_index(link, &idx) != -EBUSY),
			SMC_WR_TX_WAIT_FREE_SLOT_TIME);
		if (!rc) {
			/* timeout - terminate connections */
			smc_lgr_terminate_sched(smc_get_lgr(link));
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
		smc_lgr_terminate_sched(smc_get_lgr(link));
	}
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

	rc = wait_event_interruptible_timeout(link->wr_reg_wait,
					      (link->wr_reg_state != POSTED),
					      SMC_WR_REG_MR_WAIT_TIME);
	if (!rc) {
		/* timeout - terminate connections */
		smc_lgr_terminate_sched(smc_get_lgr(link));
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

void smc_wr_tx_dismiss_slots(struct smc_link *link, u8 wr_tx_hdr_type,
			     smc_wr_tx_filter filter,
			     smc_wr_tx_dismisser dismisser,
			     unsigned long data)
{
	struct smc_wr_tx_pend_priv *tx_pend;
	struct smc_wr_rx_hdr *wr_tx;
	int i;

	for_each_set_bit(i, link->wr_tx_mask, link->wr_tx_cnt) {
		wr_tx = (struct smc_wr_rx_hdr *)&link->wr_tx_bufs[i];
		if (wr_tx->type != wr_tx_hdr_type)
			continue;
		tx_pend = &link->wr_tx_pends[i].priv;
		if (filter(tx_pend, data))
			dismisser(tx_pend);
	}
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
				/* terminate connections of this link group
				 * abnormally
				 */
				smc_lgr_terminate_sched(smc_get_lgr(link));
				break;
			default:
				smc_wr_rx_post(link); /* refill WR RX */
				break;
			}
		}
	}
}

static void smc_wr_rx_tasklet_fn(unsigned long data)
{
	struct smc_ib_device *dev = (struct smc_ib_device *)data;
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
	u32 i;

	for (i = 0; i < lnk->wr_tx_cnt; i++) {
		lnk->wr_tx_sges[i].addr =
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
		lnk->wr_tx_rdmas[i].wr_tx_rdma[0].wr.opcode = IB_WR_RDMA_WRITE;
		lnk->wr_tx_rdmas[i].wr_tx_rdma[1].wr.opcode = IB_WR_RDMA_WRITE;
		lnk->wr_tx_rdmas[i].wr_tx_rdma[0].wr.sg_list =
			lnk->wr_tx_rdma_sges[i].tx_rdma_sge[0].wr_tx_rdma_sge;
		lnk->wr_tx_rdmas[i].wr_tx_rdma[1].wr.sg_list =
			lnk->wr_tx_rdma_sges[i].tx_rdma_sge[1].wr_tx_rdma_sge;
	}
	for (i = 0; i < lnk->wr_rx_cnt; i++) {
		lnk->wr_rx_sges[i].addr =
			lnk->wr_rx_dma_addr + i * SMC_WR_BUF_SIZE;
		lnk->wr_rx_sges[i].length = SMC_WR_BUF_SIZE;
		lnk->wr_rx_sges[i].lkey = lnk->roce_pd->local_dma_lkey;
		lnk->wr_rx_ibs[i].next = NULL;
		lnk->wr_rx_ibs[i].sg_list = &lnk->wr_rx_sges[i];
		lnk->wr_rx_ibs[i].num_sge = 1;
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

	memset(lnk->wr_tx_mask, 0,
	       BITS_TO_LONGS(SMC_WR_BUF_CNT) * sizeof(*lnk->wr_tx_mask));

	if (!lnk->smcibdev)
		return;
	ibdev = lnk->smcibdev->ibdev;

	if (lnk->wr_rx_dma_addr) {
		ib_dma_unmap_single(ibdev, lnk->wr_rx_dma_addr,
				    SMC_WR_BUF_SIZE * lnk->wr_rx_cnt,
				    DMA_FROM_DEVICE);
		lnk->wr_rx_dma_addr = 0;
	}
	if (lnk->wr_tx_dma_addr) {
		ib_dma_unmap_single(ibdev, lnk->wr_tx_dma_addr,
				    SMC_WR_BUF_SIZE * lnk->wr_tx_cnt,
				    DMA_TO_DEVICE);
		lnk->wr_tx_dma_addr = 0;
	}
}

void smc_wr_free_link_mem(struct smc_link *lnk)
{
	kfree(lnk->wr_tx_pends);
	lnk->wr_tx_pends = NULL;
	kfree(lnk->wr_tx_mask);
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

int smc_wr_alloc_link_mem(struct smc_link *link)
{
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
				   sizeof(link->wr_rx_sges[0]),
				   GFP_KERNEL);
	if (!link->wr_rx_sges)
		goto no_mem_wr_tx_sges;
	link->wr_tx_mask = kcalloc(BITS_TO_LONGS(SMC_WR_BUF_CNT),
				   sizeof(*link->wr_tx_mask),
				   GFP_KERNEL);
	if (!link->wr_tx_mask)
		goto no_mem_wr_rx_sges;
	link->wr_tx_pends = kcalloc(SMC_WR_BUF_CNT,
				    sizeof(link->wr_tx_pends[0]),
				    GFP_KERNEL);
	if (!link->wr_tx_pends)
		goto no_mem_wr_tx_mask;
	return 0;

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
	tasklet_init(&smcibdev->recv_tasklet, smc_wr_rx_tasklet_fn,
		     (unsigned long)smcibdev);
	tasklet_init(&smcibdev->send_tasklet, smc_wr_tx_tasklet_fn,
		     (unsigned long)smcibdev);
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
	lnk->wr_tx_dma_addr = ib_dma_map_single(
		ibdev, lnk->wr_tx_bufs,	SMC_WR_BUF_SIZE * lnk->wr_tx_cnt,
		DMA_TO_DEVICE);
	if (ib_dma_mapping_error(ibdev, lnk->wr_tx_dma_addr)) {
		rc = -EIO;
		goto dma_unmap;
	}
	smc_wr_init_sge(lnk);
	memset(lnk->wr_tx_mask, 0,
	       BITS_TO_LONGS(SMC_WR_BUF_CNT) * sizeof(*lnk->wr_tx_mask));
	init_waitqueue_head(&lnk->wr_tx_wait);
	init_waitqueue_head(&lnk->wr_reg_wait);
	return rc;

dma_unmap:
	ib_dma_unmap_single(ibdev, lnk->wr_rx_dma_addr,
			    SMC_WR_BUF_SIZE * lnk->wr_rx_cnt,
			    DMA_FROM_DEVICE);
	lnk->wr_rx_dma_addr = 0;
out:
	return rc;
}
