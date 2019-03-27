/*-
 * SPDX-License-Identifier: BSD-2-Clause OR GPL-2.0
 *
 * Copyright (c) 2006 Mellanox Technologies. All rights reserved
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "ipoib.h"

#ifdef CONFIG_INFINIBAND_IPOIB_CM

#include <netinet/ip.h>
#include <netinet/ip_icmp.h>
#include <netinet/icmp6.h>

#include <rdma/ib_cm.h>
#include <rdma/ib_cache.h>
#include <linux/delay.h>

int ipoib_max_conn_qp = 128;

module_param_named(max_nonsrq_conn_qp, ipoib_max_conn_qp, int, 0444);
MODULE_PARM_DESC(max_nonsrq_conn_qp,
		 "Max number of connected-mode QPs per interface "
		 "(applied only if shared receive queue is not available)");

#ifdef CONFIG_INFINIBAND_IPOIB_DEBUG_DATA
static int data_debug_level;

module_param_named(cm_data_debug_level, data_debug_level, int, 0644);
MODULE_PARM_DESC(cm_data_debug_level,
		 "Enable data path debug tracing for connected mode if > 0");
#endif

#define IPOIB_CM_IETF_ID 0x1000000000000000ULL

#define IPOIB_CM_RX_UPDATE_TIME (256 * HZ)
#define IPOIB_CM_RX_TIMEOUT     (2 * 256 * HZ)
#define IPOIB_CM_RX_DELAY       (3 * 256 * HZ)
#define IPOIB_CM_RX_UPDATE_MASK (0x3)

static struct ib_qp_attr ipoib_cm_err_attr = {
	.qp_state = IB_QPS_ERR
};

#define IPOIB_CM_RX_DRAIN_WRID 0xffffffff

static struct ib_send_wr ipoib_cm_rx_drain_wr = {
	.wr_id = IPOIB_CM_RX_DRAIN_WRID,
	.opcode = IB_WR_SEND,
};

static int ipoib_cm_tx_handler(struct ib_cm_id *cm_id,
			       struct ib_cm_event *event);

static void ipoib_cm_dma_unmap_rx(struct ipoib_dev_priv *priv, struct ipoib_cm_rx_buf *rx_req)
{

	ipoib_dma_unmap_rx(priv, (struct ipoib_rx_buf *)rx_req);

}

static int ipoib_cm_post_receive_srq(struct ipoib_dev_priv *priv, int id)
{
	struct ib_recv_wr *bad_wr;
	struct ipoib_rx_buf *rx_req;
	struct mbuf *m;
	int ret;
	int i;

	rx_req = (struct ipoib_rx_buf *)&priv->cm.srq_ring[id];
	for (m = rx_req->mb, i = 0; m != NULL; m = m->m_next, i++) {
		priv->cm.rx_sge[i].addr = rx_req->mapping[i];
		priv->cm.rx_sge[i].length = m->m_len;
	}

	priv->cm.rx_wr.num_sge = i;
	priv->cm.rx_wr.wr_id = id | IPOIB_OP_CM | IPOIB_OP_RECV;

	ret = ib_post_srq_recv(priv->cm.srq, &priv->cm.rx_wr, &bad_wr);
	if (unlikely(ret)) {
		ipoib_warn(priv, "post srq failed for buf %d (%d)\n", id, ret);
		ipoib_dma_unmap_rx(priv, rx_req);
		m_freem(priv->cm.srq_ring[id].mb);
		priv->cm.srq_ring[id].mb = NULL;
	}

	return ret;
}

static int ipoib_cm_post_receive_nonsrq(struct ipoib_dev_priv *priv,
					struct ipoib_cm_rx *rx,
					struct ib_recv_wr *wr,
					struct ib_sge *sge, int id)
{
	struct ipoib_rx_buf *rx_req;
	struct ib_recv_wr *bad_wr;
	struct mbuf *m;
	int ret;
	int i;

	rx_req = (struct ipoib_rx_buf *)&rx->rx_ring[id];
	for (m = rx_req->mb, i = 0; m != NULL; m = m->m_next, i++) {
		sge[i].addr = rx_req->mapping[i];
		sge[i].length = m->m_len;
	}

	wr->num_sge = i;
	wr->wr_id = id | IPOIB_OP_CM | IPOIB_OP_RECV;

	ret = ib_post_recv(rx->qp, wr, &bad_wr);
	if (unlikely(ret)) {
		ipoib_warn(priv, "post recv failed for buf %d (%d)\n", id, ret);
		ipoib_dma_unmap_rx(priv, rx_req);
		m_freem(rx->rx_ring[id].mb);
		rx->rx_ring[id].mb = NULL;
	}

	return ret;
}

static struct mbuf *
ipoib_cm_alloc_rx_mb(struct ipoib_dev_priv *priv, struct ipoib_cm_rx_buf *rx_req)
{
	return ipoib_alloc_map_mb(priv, (struct ipoib_rx_buf *)rx_req,
	    priv->cm.max_cm_mtu);
}

static void ipoib_cm_free_rx_ring(struct ipoib_dev_priv *priv,
				  struct ipoib_cm_rx_buf *rx_ring)
{
	int i;

	for (i = 0; i < ipoib_recvq_size; ++i)
		if (rx_ring[i].mb) {
			ipoib_cm_dma_unmap_rx(priv, &rx_ring[i]);
			m_freem(rx_ring[i].mb);
		}

	kfree(rx_ring);
}

static void ipoib_cm_start_rx_drain(struct ipoib_dev_priv *priv)
{
	struct ib_send_wr *bad_wr;
	struct ipoib_cm_rx *p;

	/* We only reserved 1 extra slot in CQ for drain WRs, so
	 * make sure we have at most 1 outstanding WR. */
	if (list_empty(&priv->cm.rx_flush_list) ||
	    !list_empty(&priv->cm.rx_drain_list))
		return;

	/*
	 * QPs on flush list are error state.  This way, a "flush
	 * error" WC will be immediately generated for each WR we post.
	 */
	p = list_entry(priv->cm.rx_flush_list.next, typeof(*p), list);
	if (ib_post_send(p->qp, &ipoib_cm_rx_drain_wr, &bad_wr))
		ipoib_warn(priv, "failed to post drain wr\n");

	list_splice_init(&priv->cm.rx_flush_list, &priv->cm.rx_drain_list);
}

static void ipoib_cm_rx_event_handler(struct ib_event *event, void *ctx)
{
	struct ipoib_cm_rx *p = ctx;
	struct ipoib_dev_priv *priv = p->priv;
	unsigned long flags;

	if (event->event != IB_EVENT_QP_LAST_WQE_REACHED)
		return;

	spin_lock_irqsave(&priv->lock, flags);
	list_move(&p->list, &priv->cm.rx_flush_list);
	p->state = IPOIB_CM_RX_FLUSH;
	ipoib_cm_start_rx_drain(priv);
	spin_unlock_irqrestore(&priv->lock, flags);
}

static struct ib_qp *ipoib_cm_create_rx_qp(struct ipoib_dev_priv *priv,
					   struct ipoib_cm_rx *p)
{
	struct ib_qp_init_attr attr = {
		.event_handler = ipoib_cm_rx_event_handler,
		.send_cq = priv->recv_cq, /* For drain WR */
		.recv_cq = priv->recv_cq,
		.srq = priv->cm.srq,
		.cap.max_send_wr = 1, /* For drain WR */
		.cap.max_send_sge = 1,
		.sq_sig_type = IB_SIGNAL_ALL_WR,
		.qp_type = IB_QPT_RC,
		.qp_context = p,
	};

	if (!ipoib_cm_has_srq(priv)) {
		attr.cap.max_recv_wr  = ipoib_recvq_size;
		attr.cap.max_recv_sge = priv->cm.num_frags;
	}

	return ib_create_qp(priv->pd, &attr);
}

static int ipoib_cm_modify_rx_qp(struct ipoib_dev_priv *priv,
				 struct ib_cm_id *cm_id, struct ib_qp *qp,
				 unsigned psn)
{
	struct ib_qp_attr qp_attr;
	int qp_attr_mask, ret;

	qp_attr.qp_state = IB_QPS_INIT;
	ret = ib_cm_init_qp_attr(cm_id, &qp_attr, &qp_attr_mask);
	if (ret) {
		ipoib_warn(priv, "failed to init QP attr for INIT: %d\n", ret);
		return ret;
	}
	ret = ib_modify_qp(qp, &qp_attr, qp_attr_mask);
	if (ret) {
		ipoib_warn(priv, "failed to modify QP to INIT: %d\n", ret);
		return ret;
	}
	qp_attr.qp_state = IB_QPS_RTR;
	ret = ib_cm_init_qp_attr(cm_id, &qp_attr, &qp_attr_mask);
	if (ret) {
		ipoib_warn(priv, "failed to init QP attr for RTR: %d\n", ret);
		return ret;
	}
	qp_attr.rq_psn = psn;
	ret = ib_modify_qp(qp, &qp_attr, qp_attr_mask);
	if (ret) {
		ipoib_warn(priv, "failed to modify QP to RTR: %d\n", ret);
		return ret;
	}

	/*
	 * Current Mellanox HCA firmware won't generate completions
	 * with error for drain WRs unless the QP has been moved to
	 * RTS first. This work-around leaves a window where a QP has
	 * moved to error asynchronously, but this will eventually get
	 * fixed in firmware, so let's not error out if modify QP
	 * fails.
	 */
	qp_attr.qp_state = IB_QPS_RTS;
	ret = ib_cm_init_qp_attr(cm_id, &qp_attr, &qp_attr_mask);
	if (ret) {
		ipoib_warn(priv, "failed to init QP attr for RTS: %d\n", ret);
		return 0;
	}
	ret = ib_modify_qp(qp, &qp_attr, qp_attr_mask);
	if (ret) {
		ipoib_warn(priv, "failed to modify QP to RTS: %d\n", ret);
		return 0;
	}

	return 0;
}

static void ipoib_cm_init_rx_wr(struct ipoib_dev_priv *priv,
				struct ib_recv_wr *wr,
				struct ib_sge *sge)
{
	int i;

	for (i = 0; i < IPOIB_CM_RX_SG; i++)
		sge[i].lkey = priv->pd->local_dma_lkey;

	wr->next    = NULL;
	wr->sg_list = sge;
	wr->num_sge = 1;
}

static int ipoib_cm_nonsrq_init_rx(struct ipoib_dev_priv *priv,
    struct ib_cm_id *cm_id, struct ipoib_cm_rx *rx)
{
	struct {
		struct ib_recv_wr wr;
		struct ib_sge sge[IPOIB_CM_RX_SG];
	} *t;
	int ret;
	int i;

	rx->rx_ring = kzalloc(ipoib_recvq_size * sizeof *rx->rx_ring, GFP_KERNEL);
	if (!rx->rx_ring) {
		printk(KERN_WARNING "%s: failed to allocate CM non-SRQ ring (%d entries)\n",
		       priv->ca->name, ipoib_recvq_size);
		return -ENOMEM;
	}

	memset(rx->rx_ring, 0, ipoib_recvq_size * sizeof *rx->rx_ring);

	t = kmalloc(sizeof *t, GFP_KERNEL);
	if (!t) {
		ret = -ENOMEM;
		goto err_free;
	}

	ipoib_cm_init_rx_wr(priv, &t->wr, t->sge);

	spin_lock_irq(&priv->lock);

	if (priv->cm.nonsrq_conn_qp >= ipoib_max_conn_qp) {
		spin_unlock_irq(&priv->lock);
		ib_send_cm_rej(cm_id, IB_CM_REJ_NO_QP, NULL, 0, NULL, 0);
		ret = -EINVAL;
		goto err_free;
	} else
		++priv->cm.nonsrq_conn_qp;

	spin_unlock_irq(&priv->lock);

	for (i = 0; i < ipoib_recvq_size; ++i) {
		if (!ipoib_cm_alloc_rx_mb(priv, &rx->rx_ring[i])) {
			ipoib_warn(priv, "failed to allocate receive buffer %d\n", i);
				ret = -ENOMEM;
				goto err_count;
		}
		ret = ipoib_cm_post_receive_nonsrq(priv, rx, &t->wr, t->sge, i);
		if (ret) {
			ipoib_warn(priv, "ipoib_cm_post_receive_nonsrq "
				   "failed for buf %d\n", i);
			ret = -EIO;
			goto err_count;
		}
	}

	rx->recv_count = ipoib_recvq_size;

	kfree(t);

	return 0;

err_count:
	spin_lock_irq(&priv->lock);
	--priv->cm.nonsrq_conn_qp;
	spin_unlock_irq(&priv->lock);

err_free:
	kfree(t);
	ipoib_cm_free_rx_ring(priv, rx->rx_ring);

	return ret;
}

static int ipoib_cm_send_rep(struct ipoib_dev_priv *priv, struct ib_cm_id *cm_id,
			     struct ib_qp *qp, struct ib_cm_req_event_param *req,
			     unsigned psn)
{
	struct ipoib_cm_data data = {};
	struct ib_cm_rep_param rep = {};

	data.qpn = cpu_to_be32(priv->qp->qp_num);
	data.mtu = cpu_to_be32(priv->cm.max_cm_mtu);

	rep.private_data = &data;
	rep.private_data_len = sizeof data;
	rep.flow_control = 0;
	rep.rnr_retry_count = req->rnr_retry_count;
	rep.srq = ipoib_cm_has_srq(priv);
	rep.qp_num = qp->qp_num;
	rep.starting_psn = psn;
	return ib_send_cm_rep(cm_id, &rep);
}

static int ipoib_cm_req_handler(struct ib_cm_id *cm_id, struct ib_cm_event *event)
{
	struct ipoib_dev_priv *priv = cm_id->context;
	struct ipoib_cm_rx *p;
	unsigned psn;
	int ret;

	ipoib_dbg(priv, "REQ arrived\n");
	p = kzalloc(sizeof *p, GFP_KERNEL);
	if (!p)
		return -ENOMEM;
	p->priv = priv;
	p->id = cm_id;
	cm_id->context = p;
	p->state = IPOIB_CM_RX_LIVE;
	p->jiffies = jiffies;
	INIT_LIST_HEAD(&p->list);

	p->qp = ipoib_cm_create_rx_qp(priv, p);
	if (IS_ERR(p->qp)) {
		ret = PTR_ERR(p->qp);
		goto err_qp;
	}

	psn = random() & 0xffffff;
	ret = ipoib_cm_modify_rx_qp(priv, cm_id, p->qp, psn);
	if (ret)
		goto err_modify;

	if (!ipoib_cm_has_srq(priv)) {
		ret = ipoib_cm_nonsrq_init_rx(priv, cm_id, p);
		if (ret)
			goto err_modify;
	}

	spin_lock_irq(&priv->lock);
	queue_delayed_work(ipoib_workqueue,
			   &priv->cm.stale_task, IPOIB_CM_RX_DELAY);
	/* Add this entry to passive ids list head, but do not re-add it
	 * if IB_EVENT_QP_LAST_WQE_REACHED has moved it to flush list. */
	p->jiffies = jiffies;
	if (p->state == IPOIB_CM_RX_LIVE)
		list_move(&p->list, &priv->cm.passive_ids);
	spin_unlock_irq(&priv->lock);

	ret = ipoib_cm_send_rep(priv, cm_id, p->qp, &event->param.req_rcvd, psn);
	if (ret) {
		ipoib_warn(priv, "failed to send REP: %d\n", ret);
		if (ib_modify_qp(p->qp, &ipoib_cm_err_attr, IB_QP_STATE))
			ipoib_warn(priv, "unable to move qp to error state\n");
	}
	return 0;

err_modify:
	ib_destroy_qp(p->qp);
err_qp:
	kfree(p);
	return ret;
}

static int ipoib_cm_rx_handler(struct ib_cm_id *cm_id,
			       struct ib_cm_event *event)
{
	struct ipoib_cm_rx *p;
	struct ipoib_dev_priv *priv;

	switch (event->event) {
	case IB_CM_REQ_RECEIVED:
		return ipoib_cm_req_handler(cm_id, event);
	case IB_CM_DREQ_RECEIVED:
		p = cm_id->context;
		ib_send_cm_drep(cm_id, NULL, 0);
		/* Fall through */
	case IB_CM_REJ_RECEIVED:
		p = cm_id->context;
		priv = p->priv;
		if (ib_modify_qp(p->qp, &ipoib_cm_err_attr, IB_QP_STATE))
			ipoib_warn(priv, "unable to move qp to error state\n");
		/* Fall through */
	default:
		return 0;
	}
}

void ipoib_cm_handle_rx_wc(struct ipoib_dev_priv *priv, struct ib_wc *wc)
{
	struct ipoib_cm_rx_buf saverx;
	struct ipoib_cm_rx_buf *rx_ring;
	unsigned int wr_id = wc->wr_id & ~(IPOIB_OP_CM | IPOIB_OP_RECV);
	struct ifnet *dev = priv->dev;
	struct mbuf *mb, *newmb;
	struct ipoib_cm_rx *p;
	int has_srq;
	u_short proto;

	CURVNET_SET_QUIET(dev->if_vnet);

	ipoib_dbg_data(priv, "cm recv completion: id %d, status: %d\n",
		       wr_id, wc->status);

	if (unlikely(wr_id >= ipoib_recvq_size)) {
		if (wr_id == (IPOIB_CM_RX_DRAIN_WRID & ~(IPOIB_OP_CM | IPOIB_OP_RECV))) {
			spin_lock(&priv->lock);
			list_splice_init(&priv->cm.rx_drain_list, &priv->cm.rx_reap_list);
			ipoib_cm_start_rx_drain(priv);
			if (priv->cm.id != NULL)
				queue_work(ipoib_workqueue,
				    &priv->cm.rx_reap_task);
			spin_unlock(&priv->lock);
		} else
			ipoib_warn(priv, "cm recv completion event with wrid %d (> %d)\n",
				   wr_id, ipoib_recvq_size);
		goto done;
	}

	p = wc->qp->qp_context;

	has_srq = ipoib_cm_has_srq(priv);
	rx_ring = has_srq ? priv->cm.srq_ring : p->rx_ring;

	mb = rx_ring[wr_id].mb;

	if (unlikely(wc->status != IB_WC_SUCCESS)) {
		ipoib_dbg(priv, "cm recv error "
			   "(status=%d, wrid=%d vend_err %x)\n",
			   wc->status, wr_id, wc->vendor_err);
		if_inc_counter(dev, IFCOUNTER_IERRORS, 1);
		if (has_srq)
			goto repost;
		else {
			if (!--p->recv_count) {
				spin_lock(&priv->lock);
				list_move(&p->list, &priv->cm.rx_reap_list);
				queue_work(ipoib_workqueue, &priv->cm.rx_reap_task);
				spin_unlock(&priv->lock);
			}
			goto done;
		}
	}

	if (unlikely(!(wr_id & IPOIB_CM_RX_UPDATE_MASK))) {
		if (p && time_after_eq(jiffies, p->jiffies + IPOIB_CM_RX_UPDATE_TIME)) {
			p->jiffies = jiffies;
			/* Move this entry to list head, but do not re-add it
			 * if it has been moved out of list. */
			if (p->state == IPOIB_CM_RX_LIVE)
				list_move(&p->list, &priv->cm.passive_ids);
		}
	}

	memcpy(&saverx, &rx_ring[wr_id], sizeof(saverx));
	newmb = ipoib_cm_alloc_rx_mb(priv, &rx_ring[wr_id]);
	if (unlikely(!newmb)) {
		/*
		 * If we can't allocate a new RX buffer, dump
		 * this packet and reuse the old buffer.
		 */
		ipoib_dbg(priv, "failed to allocate receive buffer %d\n", wr_id);
		if_inc_counter(dev, IFCOUNTER_IERRORS, 1);
		memcpy(&rx_ring[wr_id], &saverx, sizeof(saverx));
		goto repost;
	}

	ipoib_cm_dma_unmap_rx(priv, &saverx);

	ipoib_dbg_data(priv, "received %d bytes, SLID 0x%04x\n",
		       wc->byte_len, wc->slid);

	ipoib_dma_mb(priv, mb, wc->byte_len);

	if_inc_counter(dev, IFCOUNTER_IPACKETS, 1);
	if_inc_counter(dev, IFCOUNTER_IBYTES, mb->m_pkthdr.len);

	mb->m_pkthdr.rcvif = dev;
	proto = *mtod(mb, uint16_t *);
	m_adj(mb, IPOIB_ENCAP_LEN);

	IPOIB_MTAP_PROTO(dev, mb, proto);
	ipoib_demux(dev, mb, ntohs(proto));

repost:
	if (has_srq) {
		if (unlikely(ipoib_cm_post_receive_srq(priv, wr_id)))
			ipoib_warn(priv, "ipoib_cm_post_receive_srq failed "
				   "for buf %d\n", wr_id);
	} else {
		if (unlikely(ipoib_cm_post_receive_nonsrq(priv, p,
							  &priv->cm.rx_wr,
							  priv->cm.rx_sge,
							  wr_id))) {
			--p->recv_count;
			ipoib_warn(priv, "ipoib_cm_post_receive_nonsrq failed "
				   "for buf %d\n", wr_id);
		}
	}
done:
	CURVNET_RESTORE();
	return;
}

static inline int post_send(struct ipoib_dev_priv *priv,
			    struct ipoib_cm_tx *tx,
			    struct ipoib_cm_tx_buf *tx_req,
			    unsigned int wr_id)
{
	struct ib_send_wr *bad_wr;
	struct mbuf *mb = tx_req->mb;
	u64 *mapping = tx_req->mapping;
	struct mbuf *m;
	int i;

	for (m = mb, i = 0; m != NULL; m = m->m_next, i++) {
		priv->tx_sge[i].addr = mapping[i];
		priv->tx_sge[i].length = m->m_len;
	}
	priv->tx_wr.wr.num_sge = i;
	priv->tx_wr.wr.wr_id = wr_id | IPOIB_OP_CM;
	priv->tx_wr.wr.opcode = IB_WR_SEND;

	return ib_post_send(tx->qp, &priv->tx_wr.wr, &bad_wr);
}

void ipoib_cm_send(struct ipoib_dev_priv *priv, struct mbuf *mb, struct ipoib_cm_tx *tx)
{
	struct ipoib_cm_tx_buf *tx_req;
	struct ifnet *dev = priv->dev;

	if (unlikely(priv->tx_outstanding > MAX_SEND_CQE))
		while (ipoib_poll_tx(priv)); /* nothing */

	m_adj(mb, sizeof(struct ipoib_pseudoheader));
	if (unlikely(mb->m_pkthdr.len > tx->mtu)) {
		ipoib_warn(priv, "packet len %d (> %d) too long to send, dropping\n",
			   mb->m_pkthdr.len, tx->mtu);
		if_inc_counter(dev, IFCOUNTER_OERRORS, 1);
		ipoib_cm_mb_too_long(priv, mb, IPOIB_CM_MTU(tx->mtu));
		return;
	}

	ipoib_dbg_data(priv, "sending packet: head 0x%x length %d connection 0x%x\n",
		       tx->tx_head, mb->m_pkthdr.len, tx->qp->qp_num);


	/*
	 * We put the mb into the tx_ring _before_ we call post_send()
	 * because it's entirely possible that the completion handler will
	 * run before we execute anything after the post_send().  That
	 * means we have to make sure everything is properly recorded and
	 * our state is consistent before we call post_send().
	 */
	tx_req = &tx->tx_ring[tx->tx_head & (ipoib_sendq_size - 1)];
	tx_req->mb = mb;
	if (unlikely(ipoib_dma_map_tx(priv->ca, (struct ipoib_tx_buf *)tx_req,
	    priv->cm.num_frags))) {
		if_inc_counter(dev, IFCOUNTER_OERRORS, 1);
		if (tx_req->mb)
			m_freem(tx_req->mb);
		return;
	}

	if (unlikely(post_send(priv, tx, tx_req, tx->tx_head & (ipoib_sendq_size - 1)))) {
		ipoib_warn(priv, "post_send failed\n");
		if_inc_counter(dev, IFCOUNTER_OERRORS, 1);
		ipoib_dma_unmap_tx(priv->ca, (struct ipoib_tx_buf *)tx_req);
		m_freem(mb);
	} else {
		++tx->tx_head;

		if (++priv->tx_outstanding == ipoib_sendq_size) {
			ipoib_dbg(priv, "TX ring 0x%x full, stopping kernel net queue\n",
				  tx->qp->qp_num);
			if (ib_req_notify_cq(priv->send_cq, IB_CQ_NEXT_COMP))
				ipoib_warn(priv, "request notify on send CQ failed\n");
			dev->if_drv_flags |= IFF_DRV_OACTIVE;
		}
	}

}

void ipoib_cm_handle_tx_wc(struct ipoib_dev_priv *priv, struct ib_wc *wc)
{
	struct ipoib_cm_tx *tx = wc->qp->qp_context;
	unsigned int wr_id = wc->wr_id & ~IPOIB_OP_CM;
	struct ifnet *dev = priv->dev;
	struct ipoib_cm_tx_buf *tx_req;

	ipoib_dbg_data(priv, "cm send completion: id %d, status: %d\n",
		       wr_id, wc->status);

	if (unlikely(wr_id >= ipoib_sendq_size)) {
		ipoib_warn(priv, "cm send completion event with wrid %d (> %d)\n",
			   wr_id, ipoib_sendq_size);
		return;
	}

	tx_req = &tx->tx_ring[wr_id];

	ipoib_dma_unmap_tx(priv->ca, (struct ipoib_tx_buf *)tx_req);

	/* FIXME: is this right? Shouldn't we only increment on success? */
	if_inc_counter(dev, IFCOUNTER_OPACKETS, 1);

	m_freem(tx_req->mb);

	++tx->tx_tail;
	if (unlikely(--priv->tx_outstanding == ipoib_sendq_size >> 1) &&
	    (dev->if_drv_flags & IFF_DRV_OACTIVE) != 0 &&
	    test_bit(IPOIB_FLAG_ADMIN_UP, &priv->flags))
		dev->if_drv_flags &= ~IFF_DRV_OACTIVE;

	if (wc->status != IB_WC_SUCCESS &&
	    wc->status != IB_WC_WR_FLUSH_ERR) {
		struct ipoib_path *path;

		ipoib_dbg(priv, "failed cm send event "
			   "(status=%d, wrid=%d vend_err %x)\n",
			   wc->status, wr_id, wc->vendor_err);

		path = tx->path;

		if (path) {
			path->cm = NULL;
			rb_erase(&path->rb_node, &priv->path_tree);
			list_del(&path->list);
		}

		if (test_and_clear_bit(IPOIB_FLAG_INITIALIZED, &tx->flags)) {
			list_move(&tx->list, &priv->cm.reap_list);
			queue_work(ipoib_workqueue, &priv->cm.reap_task);
		}

		clear_bit(IPOIB_FLAG_OPER_UP, &tx->flags);
	}

}

int ipoib_cm_dev_open(struct ipoib_dev_priv *priv)
{
	int ret;

	if (!IPOIB_CM_SUPPORTED(IF_LLADDR(priv->dev)))
		return 0;

	priv->cm.id = ib_create_cm_id(priv->ca, ipoib_cm_rx_handler, priv);
	if (IS_ERR(priv->cm.id)) {
		printk(KERN_WARNING "%s: failed to create CM ID\n", priv->ca->name);
		ret = PTR_ERR(priv->cm.id);
		goto err_cm;
	}

	ret = ib_cm_listen(priv->cm.id, cpu_to_be64(IPOIB_CM_IETF_ID | priv->qp->qp_num), 0);
	if (ret) {
		printk(KERN_WARNING "%s: failed to listen on ID 0x%llx\n", priv->ca->name,
		       IPOIB_CM_IETF_ID | priv->qp->qp_num);
		goto err_listen;
	}

	return 0;

err_listen:
	ib_destroy_cm_id(priv->cm.id);
err_cm:
	priv->cm.id = NULL;
	return ret;
}

static void ipoib_cm_free_rx_reap_list(struct ipoib_dev_priv *priv)
{
	struct ipoib_cm_rx *rx, *n;
	LIST_HEAD(list);

	spin_lock_irq(&priv->lock);
	list_splice_init(&priv->cm.rx_reap_list, &list);
	spin_unlock_irq(&priv->lock);

	list_for_each_entry_safe(rx, n, &list, list) {
		ib_destroy_cm_id(rx->id);
		ib_destroy_qp(rx->qp);
		if (!ipoib_cm_has_srq(priv)) {
			ipoib_cm_free_rx_ring(priv, rx->rx_ring);
			spin_lock_irq(&priv->lock);
			--priv->cm.nonsrq_conn_qp;
			spin_unlock_irq(&priv->lock);
		}
		kfree(rx);
	}
}

void ipoib_cm_dev_stop(struct ipoib_dev_priv *priv)
{
	struct ipoib_cm_rx *p;
	unsigned long begin;
	int ret;

	if (!IPOIB_CM_SUPPORTED(IF_LLADDR(priv->dev)) || !priv->cm.id)
		return;

	ib_destroy_cm_id(priv->cm.id);
	priv->cm.id = NULL;

	cancel_work_sync(&priv->cm.rx_reap_task);

	spin_lock_irq(&priv->lock);
	while (!list_empty(&priv->cm.passive_ids)) {
		p = list_entry(priv->cm.passive_ids.next, typeof(*p), list);
		list_move(&p->list, &priv->cm.rx_error_list);
		p->state = IPOIB_CM_RX_ERROR;
		spin_unlock_irq(&priv->lock);
		ret = ib_modify_qp(p->qp, &ipoib_cm_err_attr, IB_QP_STATE);
		if (ret)
			ipoib_warn(priv, "unable to move qp to error state: %d\n", ret);
		spin_lock_irq(&priv->lock);
	}

	/* Wait for all RX to be drained */
	begin = jiffies;

	while (!list_empty(&priv->cm.rx_error_list) ||
	       !list_empty(&priv->cm.rx_flush_list) ||
	       !list_empty(&priv->cm.rx_drain_list)) {
		if (time_after(jiffies, begin + 5 * HZ)) {
			ipoib_warn(priv, "RX drain timing out\n");

			/*
			 * assume the HW is wedged and just free up everything.
			 */
			list_splice_init(&priv->cm.rx_flush_list,
					 &priv->cm.rx_reap_list);
			list_splice_init(&priv->cm.rx_error_list,
					 &priv->cm.rx_reap_list);
			list_splice_init(&priv->cm.rx_drain_list,
					 &priv->cm.rx_reap_list);
			break;
		}
		spin_unlock_irq(&priv->lock);
		msleep(1);
		ipoib_drain_cq(priv);
		spin_lock_irq(&priv->lock);
	}

	spin_unlock_irq(&priv->lock);

	ipoib_cm_free_rx_reap_list(priv);

	cancel_delayed_work_sync(&priv->cm.stale_task);
}

static int ipoib_cm_rep_handler(struct ib_cm_id *cm_id, struct ib_cm_event *event)
{
	struct ipoib_cm_tx *p = cm_id->context;
	struct ipoib_dev_priv *priv = p->priv;
	struct ipoib_cm_data *data = event->private_data;
	struct ifqueue mbqueue;
	struct ib_qp_attr qp_attr;
	int qp_attr_mask, ret;
	struct mbuf *mb;

	ipoib_dbg(priv, "cm rep handler\n");
	p->mtu = be32_to_cpu(data->mtu);

	if (p->mtu <= IPOIB_ENCAP_LEN) {
		ipoib_warn(priv, "Rejecting connection: mtu %d <= %d\n",
			   p->mtu, IPOIB_ENCAP_LEN);
		return -EINVAL;
	}

	qp_attr.qp_state = IB_QPS_RTR;
	ret = ib_cm_init_qp_attr(cm_id, &qp_attr, &qp_attr_mask);
	if (ret) {
		ipoib_warn(priv, "failed to init QP attr for RTR: %d\n", ret);
		return ret;
	}

	qp_attr.rq_psn = 0 /* FIXME */;
	ret = ib_modify_qp(p->qp, &qp_attr, qp_attr_mask);
	if (ret) {
		ipoib_warn(priv, "failed to modify QP to RTR: %d\n", ret);
		return ret;
	}

	qp_attr.qp_state = IB_QPS_RTS;
	ret = ib_cm_init_qp_attr(cm_id, &qp_attr, &qp_attr_mask);
	if (ret) {
		ipoib_warn(priv, "failed to init QP attr for RTS: %d\n", ret);
		return ret;
	}
	ret = ib_modify_qp(p->qp, &qp_attr, qp_attr_mask);
	if (ret) {
		ipoib_warn(priv, "failed to modify QP to RTS: %d\n", ret);
		return ret;
	}

	bzero(&mbqueue, sizeof(mbqueue));

	spin_lock_irq(&priv->lock);
	set_bit(IPOIB_FLAG_OPER_UP, &p->flags);
	if (p->path)
		for (;;) {
			_IF_DEQUEUE(&p->path->queue, mb);
			if (mb == NULL)
				break;
			_IF_ENQUEUE(&mbqueue, mb);
		}
	spin_unlock_irq(&priv->lock);

	for (;;) {
		struct ifnet *dev = p->priv->dev;
		_IF_DEQUEUE(&mbqueue, mb);
		if (mb == NULL)
			break;
		mb->m_pkthdr.rcvif = dev;
		if (dev->if_transmit(dev, mb))
			ipoib_warn(priv, "dev_queue_xmit failed "
				   "to requeue packet\n");
	}

	ret = ib_send_cm_rtu(cm_id, NULL, 0);
	if (ret) {
		ipoib_warn(priv, "failed to send RTU: %d\n", ret);
		return ret;
	}
	return 0;
}

static struct ib_qp *ipoib_cm_create_tx_qp(struct ipoib_dev_priv *priv,
    struct ipoib_cm_tx *tx)
{
	struct ib_qp_init_attr attr = {
		.send_cq		= priv->send_cq,
		.recv_cq		= priv->recv_cq,
		.srq			= priv->cm.srq,
		.cap.max_send_wr	= ipoib_sendq_size,
		.cap.max_send_sge	= priv->cm.num_frags,
		.sq_sig_type		= IB_SIGNAL_ALL_WR,
		.qp_type		= IB_QPT_RC,
		.qp_context		= tx
	};

	return ib_create_qp(priv->pd, &attr);
}

static int ipoib_cm_send_req(struct ipoib_dev_priv *priv,
			     struct ib_cm_id *id, struct ib_qp *qp,
			     u32 qpn,
			     struct ib_sa_path_rec *pathrec)
{
	struct ipoib_cm_data data = {};
	struct ib_cm_req_param req = {};

	ipoib_dbg(priv, "cm send req\n");

	data.qpn = cpu_to_be32(priv->qp->qp_num);
	data.mtu = cpu_to_be32(priv->cm.max_cm_mtu);

	req.primary_path		= pathrec;
	req.alternate_path		= NULL;
	req.service_id			= cpu_to_be64(IPOIB_CM_IETF_ID | qpn);
	req.qp_num			= qp->qp_num;
	req.qp_type			= qp->qp_type;
	req.private_data		= &data;
	req.private_data_len		= sizeof data;
	req.flow_control		= 0;

	req.starting_psn		= 0; /* FIXME */

	/*
	 * Pick some arbitrary defaults here; we could make these
	 * module parameters if anyone cared about setting them.
	 */
	req.responder_resources		= 4;
	req.remote_cm_response_timeout	= 20;
	req.local_cm_response_timeout	= 20;
	req.retry_count			= 0; /* RFC draft warns against retries */
	req.rnr_retry_count		= 0; /* RFC draft warns against retries */
	req.max_cm_retries		= 15;
	req.srq				= ipoib_cm_has_srq(priv);
	return ib_send_cm_req(id, &req);
}

static int ipoib_cm_modify_tx_init(struct ipoib_dev_priv *priv,
				  struct ib_cm_id *cm_id, struct ib_qp *qp)
{
	struct ib_qp_attr qp_attr;
	int qp_attr_mask, ret;
	ret = ib_find_pkey(priv->ca, priv->port, priv->pkey, &qp_attr.pkey_index);
	if (ret) {
		ipoib_warn(priv, "pkey 0x%x not found: %d\n", priv->pkey, ret);
		return ret;
	}

	qp_attr.qp_state = IB_QPS_INIT;
	qp_attr.qp_access_flags = IB_ACCESS_LOCAL_WRITE;
	qp_attr.port_num = priv->port;
	qp_attr_mask = IB_QP_STATE | IB_QP_ACCESS_FLAGS | IB_QP_PKEY_INDEX | IB_QP_PORT;

	ret = ib_modify_qp(qp, &qp_attr, qp_attr_mask);
	if (ret) {
		ipoib_warn(priv, "failed to modify tx QP to INIT: %d\n", ret);
		return ret;
	}
	return 0;
}

static int ipoib_cm_tx_init(struct ipoib_cm_tx *p, u32 qpn,
			    struct ib_sa_path_rec *pathrec)
{
	struct ipoib_dev_priv *priv = p->priv;
	int ret;

	p->tx_ring = kzalloc(ipoib_sendq_size * sizeof *p->tx_ring, GFP_KERNEL);
	if (!p->tx_ring) {
		ipoib_warn(priv, "failed to allocate tx ring\n");
		ret = -ENOMEM;
		goto err_tx;
	}
	memset(p->tx_ring, 0, ipoib_sendq_size * sizeof *p->tx_ring);

	p->qp = ipoib_cm_create_tx_qp(p->priv, p);
	if (IS_ERR(p->qp)) {
		ret = PTR_ERR(p->qp);
		ipoib_warn(priv, "failed to allocate tx qp: %d\n", ret);
		goto err_qp;
	}

	p->id = ib_create_cm_id(priv->ca, ipoib_cm_tx_handler, p);
	if (IS_ERR(p->id)) {
		ret = PTR_ERR(p->id);
		ipoib_warn(priv, "failed to create tx cm id: %d\n", ret);
		goto err_id;
	}

	ret = ipoib_cm_modify_tx_init(p->priv, p->id,  p->qp);
	if (ret) {
		ipoib_warn(priv, "failed to modify tx qp to rtr: %d\n", ret);
		goto err_modify;
	}

	ret = ipoib_cm_send_req(p->priv, p->id, p->qp, qpn, pathrec);
	if (ret) {
		ipoib_warn(priv, "failed to send cm req: %d\n", ret);
		goto err_send_cm;
	}

	ipoib_dbg(priv, "Request connection 0x%x for gid %pI6 qpn 0x%x\n",
		  p->qp->qp_num, pathrec->dgid.raw, qpn);

	return 0;

err_send_cm:
err_modify:
	ib_destroy_cm_id(p->id);
err_id:
	p->id = NULL;
	ib_destroy_qp(p->qp);
err_qp:
	p->qp = NULL;
	kfree(p->tx_ring);
err_tx:
	return ret;
}

static void ipoib_cm_tx_destroy(struct ipoib_cm_tx *p)
{
	struct ipoib_dev_priv *priv = p->priv;
	struct ifnet *dev = priv->dev;
	struct ipoib_cm_tx_buf *tx_req;
	unsigned long begin;

	ipoib_dbg(priv, "Destroy active connection 0x%x head 0x%x tail 0x%x\n",
		  p->qp ? p->qp->qp_num : 0, p->tx_head, p->tx_tail);

	if (p->path)
		ipoib_path_free(priv, p->path);

	if (p->id)
		ib_destroy_cm_id(p->id);

	if (p->tx_ring) {
		/* Wait for all sends to complete */
		begin = jiffies;
		while ((int) p->tx_tail - (int) p->tx_head < 0) {
			if (time_after(jiffies, begin + 5 * HZ)) {
				ipoib_warn(priv, "timing out; %d sends not completed\n",
					   p->tx_head - p->tx_tail);
				goto timeout;
			}

			msleep(1);
		}
	}

timeout:

	while ((int) p->tx_tail - (int) p->tx_head < 0) {
		tx_req = &p->tx_ring[p->tx_tail & (ipoib_sendq_size - 1)];
		ipoib_dma_unmap_tx(priv->ca, (struct ipoib_tx_buf *)tx_req);
		m_freem(tx_req->mb);
		++p->tx_tail;
		if (unlikely(--priv->tx_outstanding == ipoib_sendq_size >> 1) &&
		    (dev->if_drv_flags & IFF_DRV_OACTIVE) != 0 &&
		    test_bit(IPOIB_FLAG_ADMIN_UP, &priv->flags))
			dev->if_drv_flags &= ~IFF_DRV_OACTIVE;
	}

	if (p->qp)
		ib_destroy_qp(p->qp);

	kfree(p->tx_ring);
	kfree(p);
}

static int ipoib_cm_tx_handler(struct ib_cm_id *cm_id,
			       struct ib_cm_event *event)
{
	struct ipoib_cm_tx *tx = cm_id->context;
	struct ipoib_dev_priv *priv = tx->priv;
	struct ipoib_path *path;
	unsigned long flags;
	int ret;

	switch (event->event) {
	case IB_CM_DREQ_RECEIVED:
		ipoib_dbg(priv, "DREQ received.\n");
		ib_send_cm_drep(cm_id, NULL, 0);
		break;
	case IB_CM_REP_RECEIVED:
		ipoib_dbg(priv, "REP received.\n");
		ret = ipoib_cm_rep_handler(cm_id, event);
		if (ret)
			ib_send_cm_rej(cm_id, IB_CM_REJ_CONSUMER_DEFINED,
				       NULL, 0, NULL, 0);
		break;
	case IB_CM_REQ_ERROR:
	case IB_CM_REJ_RECEIVED:
	case IB_CM_TIMEWAIT_EXIT:
		ipoib_dbg(priv, "CM error %d.\n", event->event);
		spin_lock_irqsave(&priv->lock, flags);
		path = tx->path;

		if (path) {
			path->cm = NULL;
			tx->path = NULL;
			rb_erase(&path->rb_node, &priv->path_tree);
			list_del(&path->list);
		}

		if (test_and_clear_bit(IPOIB_FLAG_INITIALIZED, &tx->flags)) {
			list_move(&tx->list, &priv->cm.reap_list);
			queue_work(ipoib_workqueue, &priv->cm.reap_task);
		}

		spin_unlock_irqrestore(&priv->lock, flags);
		if (path)
			ipoib_path_free(tx->priv, path);
		break;
	default:
		break;
	}

	return 0;
}

struct ipoib_cm_tx *ipoib_cm_create_tx(struct ipoib_dev_priv *priv,
    struct ipoib_path *path)
{
	struct ipoib_cm_tx *tx;

	tx = kzalloc(sizeof *tx, GFP_ATOMIC);
	if (!tx)
		return NULL;

	ipoib_dbg(priv, "Creating cm tx\n");
	path->cm = tx;
	tx->path = path;
	tx->priv = priv;
	list_add(&tx->list, &priv->cm.start_list);
	set_bit(IPOIB_FLAG_INITIALIZED, &tx->flags);
	queue_work(ipoib_workqueue, &priv->cm.start_task);
	return tx;
}

void ipoib_cm_destroy_tx(struct ipoib_cm_tx *tx)
{
	struct ipoib_dev_priv *priv = tx->priv;
	if (test_and_clear_bit(IPOIB_FLAG_INITIALIZED, &tx->flags)) {
		spin_lock(&priv->lock);
		list_move(&tx->list, &priv->cm.reap_list);
		spin_unlock(&priv->lock);
		queue_work(ipoib_workqueue, &priv->cm.reap_task);
		ipoib_dbg(priv, "Reap connection for gid %pI6\n",
			  tx->path->pathrec.dgid.raw);
		tx->path = NULL;
	}
}

static void ipoib_cm_tx_start(struct work_struct *work)
{
	struct ipoib_dev_priv *priv = container_of(work, struct ipoib_dev_priv,
						   cm.start_task);
	struct ipoib_path *path;
	struct ipoib_cm_tx *p;
	unsigned long flags;
	int ret;

	struct ib_sa_path_rec pathrec;
	u32 qpn;

	ipoib_dbg(priv, "cm start task\n");
	spin_lock_irqsave(&priv->lock, flags);

	while (!list_empty(&priv->cm.start_list)) {
		p = list_entry(priv->cm.start_list.next, typeof(*p), list);
		list_del_init(&p->list);
		path = p->path;
		qpn = IPOIB_QPN(path->hwaddr);
		memcpy(&pathrec, &p->path->pathrec, sizeof pathrec);

		spin_unlock_irqrestore(&priv->lock, flags);

		ret = ipoib_cm_tx_init(p, qpn, &pathrec);

		spin_lock_irqsave(&priv->lock, flags);

		if (ret) {
			path = p->path;
			if (path) {
				path->cm = NULL;
				rb_erase(&path->rb_node, &priv->path_tree);
				list_del(&path->list);
				ipoib_path_free(priv, path);
			}
			list_del(&p->list);
			kfree(p);
		}
	}

	spin_unlock_irqrestore(&priv->lock, flags);
}

static void ipoib_cm_tx_reap(struct work_struct *work)
{
	struct ipoib_dev_priv *priv = container_of(work, struct ipoib_dev_priv,
						   cm.reap_task);
	struct ipoib_cm_tx *p;
	unsigned long flags;

	spin_lock_irqsave(&priv->lock, flags);

	while (!list_empty(&priv->cm.reap_list)) {
		p = list_entry(priv->cm.reap_list.next, typeof(*p), list);
		list_del(&p->list);
		spin_unlock_irqrestore(&priv->lock, flags);
		ipoib_cm_tx_destroy(p);
		spin_lock_irqsave(&priv->lock, flags);
	}

	spin_unlock_irqrestore(&priv->lock, flags);
}

static void ipoib_cm_mb_reap(struct work_struct *work)
{
	struct ipoib_dev_priv *priv = container_of(work, struct ipoib_dev_priv,
						   cm.mb_task);
	struct mbuf *mb;
	unsigned long flags;
#if defined(INET) || defined(INET6)
	unsigned mtu = priv->mcast_mtu;
#endif
	uint16_t proto;

	spin_lock_irqsave(&priv->lock, flags);

	for (;;) {
		IF_DEQUEUE(&priv->cm.mb_queue, mb);
		if (mb == NULL)
			break;
		spin_unlock_irqrestore(&priv->lock, flags);

		proto = htons(*mtod(mb, uint16_t *));
		m_adj(mb, IPOIB_ENCAP_LEN);
		switch (proto) {
#if defined(INET)
		case ETHERTYPE_IP:
			icmp_error(mb, ICMP_UNREACH, ICMP_UNREACH_NEEDFRAG, 0, mtu);
			break;
#endif
#if defined(INET6)
		case ETHERTYPE_IPV6:
			icmp6_error(mb, ICMP6_PACKET_TOO_BIG, 0, mtu);
			break;
#endif
		default:
			m_freem(mb);
		}

		spin_lock_irqsave(&priv->lock, flags);
	}

	spin_unlock_irqrestore(&priv->lock, flags);
}

void
ipoib_cm_mb_too_long(struct ipoib_dev_priv *priv, struct mbuf *mb, unsigned int mtu)
{
	int e = priv->cm.mb_queue.ifq_len; 

	IF_ENQUEUE(&priv->cm.mb_queue, mb);
	if (e == 0)
		queue_work(ipoib_workqueue, &priv->cm.mb_task);
}

static void ipoib_cm_rx_reap(struct work_struct *work)
{
	ipoib_cm_free_rx_reap_list(container_of(work, struct ipoib_dev_priv,
						cm.rx_reap_task));
}

static void ipoib_cm_stale_task(struct work_struct *work)
{
	struct ipoib_dev_priv *priv = container_of(work, struct ipoib_dev_priv,
						   cm.stale_task.work);
	struct ipoib_cm_rx *p;
	int ret;

	spin_lock_irq(&priv->lock);
	while (!list_empty(&priv->cm.passive_ids)) {
		/* List is sorted by LRU, start from tail,
		 * stop when we see a recently used entry */
		p = list_entry(priv->cm.passive_ids.prev, typeof(*p), list);
		if (time_before_eq(jiffies, p->jiffies + IPOIB_CM_RX_TIMEOUT))
			break;
		list_move(&p->list, &priv->cm.rx_error_list);
		p->state = IPOIB_CM_RX_ERROR;
		spin_unlock_irq(&priv->lock);
		ret = ib_modify_qp(p->qp, &ipoib_cm_err_attr, IB_QP_STATE);
		if (ret)
			ipoib_warn(priv, "unable to move qp to error state: %d\n", ret);
		spin_lock_irq(&priv->lock);
	}

	if (!list_empty(&priv->cm.passive_ids))
		queue_delayed_work(ipoib_workqueue,
				   &priv->cm.stale_task, IPOIB_CM_RX_DELAY);
	spin_unlock_irq(&priv->lock);
}


static void ipoib_cm_create_srq(struct ipoib_dev_priv *priv, int max_sge)
{
	struct ib_srq_init_attr srq_init_attr = {
		.attr = {
			.max_wr  = ipoib_recvq_size,
			.max_sge = max_sge
		}
	};

	priv->cm.srq = ib_create_srq(priv->pd, &srq_init_attr);
	if (IS_ERR(priv->cm.srq)) {
		if (PTR_ERR(priv->cm.srq) != -ENOSYS)
			printk(KERN_WARNING "%s: failed to allocate SRQ, error %ld\n",
			       priv->ca->name, PTR_ERR(priv->cm.srq));
		priv->cm.srq = NULL;
		return;
	}

	priv->cm.srq_ring = kzalloc(ipoib_recvq_size * sizeof *priv->cm.srq_ring, GFP_KERNEL);
	if (!priv->cm.srq_ring) {
		printk(KERN_WARNING "%s: failed to allocate CM SRQ ring (%d entries)\n",
		       priv->ca->name, ipoib_recvq_size);
		ib_destroy_srq(priv->cm.srq);
		priv->cm.srq = NULL;
		return;
	}

	memset(priv->cm.srq_ring, 0, ipoib_recvq_size * sizeof *priv->cm.srq_ring);
}

int ipoib_cm_dev_init(struct ipoib_dev_priv *priv)
{
	struct ifnet *dev = priv->dev;
	int i;
	int max_srq_sge;

	INIT_LIST_HEAD(&priv->cm.passive_ids);
	INIT_LIST_HEAD(&priv->cm.reap_list);
	INIT_LIST_HEAD(&priv->cm.start_list);
	INIT_LIST_HEAD(&priv->cm.rx_error_list);
	INIT_LIST_HEAD(&priv->cm.rx_flush_list);
	INIT_LIST_HEAD(&priv->cm.rx_drain_list);
	INIT_LIST_HEAD(&priv->cm.rx_reap_list);
	INIT_WORK(&priv->cm.start_task, ipoib_cm_tx_start);
	INIT_WORK(&priv->cm.reap_task, ipoib_cm_tx_reap);
	INIT_WORK(&priv->cm.mb_task, ipoib_cm_mb_reap);
	INIT_WORK(&priv->cm.rx_reap_task, ipoib_cm_rx_reap);
	INIT_DELAYED_WORK(&priv->cm.stale_task, ipoib_cm_stale_task);

	bzero(&priv->cm.mb_queue, sizeof(priv->cm.mb_queue));
	mtx_init(&priv->cm.mb_queue.ifq_mtx,
	    dev->if_xname, "if send queue", MTX_DEF);

	max_srq_sge = priv->ca->attrs.max_srq_sge;

	ipoib_dbg(priv, "max_srq_sge=%d\n", max_srq_sge);

	max_srq_sge = min_t(int, IPOIB_CM_RX_SG, max_srq_sge);
	ipoib_cm_create_srq(priv, max_srq_sge);
	if (ipoib_cm_has_srq(priv)) {
		priv->cm.max_cm_mtu = max_srq_sge * MJUMPAGESIZE;
		priv->cm.num_frags  = max_srq_sge;
		ipoib_dbg(priv, "max_cm_mtu = 0x%x, num_frags=%d\n",
			  priv->cm.max_cm_mtu, priv->cm.num_frags);
	} else {
		priv->cm.max_cm_mtu = IPOIB_CM_MAX_MTU;
		priv->cm.num_frags  = IPOIB_CM_RX_SG;
	}

	ipoib_cm_init_rx_wr(priv, &priv->cm.rx_wr, priv->cm.rx_sge);

	if (ipoib_cm_has_srq(priv)) {
		for (i = 0; i < ipoib_recvq_size; ++i) {
			if (!ipoib_cm_alloc_rx_mb(priv, &priv->cm.srq_ring[i])) {
				ipoib_warn(priv, "failed to allocate "
					   "receive buffer %d\n", i);
				ipoib_cm_dev_cleanup(priv);
				return -ENOMEM;
			}

			if (ipoib_cm_post_receive_srq(priv, i)) {
				ipoib_warn(priv, "ipoib_cm_post_receive_srq "
					   "failed for buf %d\n", i);
				ipoib_cm_dev_cleanup(priv);
				return -EIO;
			}
		}
	}

	IF_LLADDR(priv->dev)[0] = IPOIB_FLAGS_RC;
	return 0;
}

void ipoib_cm_dev_cleanup(struct ipoib_dev_priv *priv)
{
	int ret;

	if (!priv->cm.srq)
		return;

	ipoib_dbg(priv, "Cleanup ipoib connected mode.\n");

	ret = ib_destroy_srq(priv->cm.srq);
	if (ret)
		ipoib_warn(priv, "ib_destroy_srq failed: %d\n", ret);

	priv->cm.srq = NULL;
	if (!priv->cm.srq_ring)
		return;

	ipoib_cm_free_rx_ring(priv, priv->cm.srq_ring);
	priv->cm.srq_ring = NULL;

	mtx_destroy(&priv->cm.mb_queue.ifq_mtx);
}

#endif /* CONFIG_INFINIBAND_IPOIB_CM */
