/*-
 * SPDX-License-Identifier: BSD-2-Clause OR GPL-2.0
 *
 * Copyright (c) 2004, 2005 Topspin Communications.  All rights reserved.
 * Copyright (c) 2005 Sun Microsystems, Inc. All rights reserved.
 * Copyright (c) 2005 Mellanox Technologies. All rights reserved.
 * Copyright (c) 2004, 2005 Voltaire, Inc. All rights reserved.
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

#include <rdma/ib_cache.h>

#include <security/mac/mac_framework.h>

#include <linux/delay.h>
#include <linux/dma-mapping.h>

#ifdef CONFIG_INFINIBAND_IPOIB_DEBUG_DATA
static int data_debug_level;

module_param(data_debug_level, int, 0644);
MODULE_PARM_DESC(data_debug_level,
		 "Enable data path debug tracing if > 0");
#endif

static DEFINE_MUTEX(pkey_mutex);

struct ipoib_ah *ipoib_create_ah(struct ipoib_dev_priv *priv,
				 struct ib_pd *pd, struct ib_ah_attr *attr)
{
	struct ipoib_ah *ah;

	ah = kmalloc(sizeof *ah, GFP_KERNEL);
	if (!ah)
		return NULL;

	ah->priv      = priv;
	ah->last_send = 0;
	kref_init(&ah->ref);

	ah->ah = ib_create_ah(pd, attr);
	if (IS_ERR(ah->ah)) {
		kfree(ah);
		ah = NULL;
	} else
		ipoib_dbg(priv, "Created ah %p\n", ah->ah);

	return ah;
}

void ipoib_free_ah(struct kref *kref)
{
	struct ipoib_ah *ah = container_of(kref, struct ipoib_ah, ref);
	struct ipoib_dev_priv *priv = ah->priv;

	unsigned long flags;

	spin_lock_irqsave(&priv->lock, flags);
	list_add_tail(&ah->list, &priv->dead_ahs);
	spin_unlock_irqrestore(&priv->lock, flags);
}

void
ipoib_dma_unmap_rx(struct ipoib_dev_priv *priv, struct ipoib_rx_buf *rx_req)
{
	struct mbuf *m;
	int i;

	for (i = 0, m = rx_req->mb; m != NULL; m = m->m_next, i++)
		ib_dma_unmap_single(priv->ca, rx_req->mapping[i], m->m_len,
		    DMA_FROM_DEVICE);
}

void
ipoib_dma_mb(struct ipoib_dev_priv *priv, struct mbuf *mb, unsigned int length)
{

	m_adj(mb, -(mb->m_pkthdr.len - length));
}

struct mbuf *
ipoib_alloc_map_mb(struct ipoib_dev_priv *priv, struct ipoib_rx_buf *rx_req,
    int size)
{
	struct mbuf *mb, *m;
	int i, j;

	rx_req->mb = NULL;
	mb = m_getm2(NULL, size, M_NOWAIT, MT_DATA, M_PKTHDR);
	if (mb == NULL)
		return (NULL);
	for (i = 0, m = mb; m != NULL; m = m->m_next, i++) {
		m->m_len = M_SIZE(m);
		mb->m_pkthdr.len += m->m_len;
		rx_req->mapping[i] = ib_dma_map_single(priv->ca,
		    mtod(m, void *), m->m_len, DMA_FROM_DEVICE);
		if (unlikely(ib_dma_mapping_error(priv->ca,
		    rx_req->mapping[i])))
			goto error;

	}
	rx_req->mb = mb;
	return (mb);
error:
	for (j = 0, m = mb; j < i; m = m->m_next, j++)
		ib_dma_unmap_single(priv->ca, rx_req->mapping[j], m->m_len,
		    DMA_FROM_DEVICE);
	m_freem(mb);
	return (NULL);

}

static int ipoib_ib_post_receive(struct ipoib_dev_priv *priv, int id)
{
	struct ipoib_rx_buf *rx_req;
	struct ib_recv_wr *bad_wr;
	struct mbuf *m;
	int ret;
	int i;

	rx_req = &priv->rx_ring[id];
	for (m = rx_req->mb, i = 0; m != NULL; m = m->m_next, i++) {
		priv->rx_sge[i].addr = rx_req->mapping[i];
		priv->rx_sge[i].length = m->m_len;
	}
	priv->rx_wr.num_sge = i;
	priv->rx_wr.wr_id = id | IPOIB_OP_RECV;

	ret = ib_post_recv(priv->qp, &priv->rx_wr, &bad_wr);
	if (unlikely(ret)) {
		ipoib_warn(priv, "receive failed for buf %d (%d)\n", id, ret);
		ipoib_dma_unmap_rx(priv, &priv->rx_ring[id]);
		m_freem(priv->rx_ring[id].mb);
		priv->rx_ring[id].mb = NULL;
	}

	return ret;
}

static struct mbuf *
ipoib_alloc_rx_mb(struct ipoib_dev_priv *priv, int id)
{

	return ipoib_alloc_map_mb(priv, &priv->rx_ring[id],
	    priv->max_ib_mtu + IB_GRH_BYTES);
}

static int ipoib_ib_post_receives(struct ipoib_dev_priv *priv)
{
	int i;

	for (i = 0; i < ipoib_recvq_size; ++i) {
		if (!ipoib_alloc_rx_mb(priv, i)) {
			ipoib_warn(priv, "failed to allocate receive buffer %d\n", i);
			return -ENOMEM;
		}
		if (ipoib_ib_post_receive(priv, i)) {
			ipoib_warn(priv, "ipoib_ib_post_receive failed for buf %d\n", i);
			return -EIO;
		}
	}

	return 0;
}

static void
ipoib_ib_handle_rx_wc(struct ipoib_dev_priv *priv, struct ib_wc *wc)
{
	struct ipoib_rx_buf saverx;
	unsigned int wr_id = wc->wr_id & ~IPOIB_OP_RECV;
	struct ifnet *dev = priv->dev;
	struct ipoib_header *eh;
	struct mbuf *mb;

	ipoib_dbg_data(priv, "recv completion: id %d, status: %d\n",
		       wr_id, wc->status);

	if (unlikely(wr_id >= ipoib_recvq_size)) {
		ipoib_warn(priv, "recv completion event with wrid %d (> %d)\n",
			   wr_id, ipoib_recvq_size);
		return;
	}

	mb  = priv->rx_ring[wr_id].mb;

	if (unlikely(wc->status != IB_WC_SUCCESS)) {
		if (wc->status != IB_WC_WR_FLUSH_ERR) {
			ipoib_warn(priv, "failed recv event "
				   "(status=%d, wrid=%d vend_err %x)\n",
				   wc->status, wr_id, wc->vendor_err);
			goto repost;
		}
		if (mb) {
			ipoib_dma_unmap_rx(priv, &priv->rx_ring[wr_id]);
			m_freem(mb);
			priv->rx_ring[wr_id].mb = NULL;
		}
		return;
	}

	/*
	 * Drop packets that this interface sent, ie multicast packets
	 * that the HCA has replicated.
	 */
	if (wc->slid == priv->local_lid && wc->src_qp == priv->qp->qp_num)
		goto repost;

	memcpy(&saverx, &priv->rx_ring[wr_id], sizeof(saverx));
	/*
	 * If we can't allocate a new RX buffer, dump
	 * this packet and reuse the old buffer.
	 */
	if (unlikely(!ipoib_alloc_rx_mb(priv, wr_id))) {
		memcpy(&priv->rx_ring[wr_id], &saverx, sizeof(saverx));
		if_inc_counter(dev, IFCOUNTER_IQDROPS, 1);
		goto repost;
	}

	ipoib_dbg_data(priv, "received %d bytes, SLID 0x%04x\n",
		       wc->byte_len, wc->slid);

	ipoib_dma_unmap_rx(priv, &saverx);
	ipoib_dma_mb(priv, mb, wc->byte_len);

	if_inc_counter(dev, IFCOUNTER_IPACKETS, 1);
	if_inc_counter(dev, IFCOUNTER_IBYTES, mb->m_pkthdr.len);
	mb->m_pkthdr.rcvif = dev;
	m_adj(mb, sizeof(struct ib_grh) - INFINIBAND_ALEN);
	eh = mtod(mb, struct ipoib_header *);
	bzero(eh->hwaddr, 4);	/* Zero the queue pair, only dgid is in grh */

	if (test_bit(IPOIB_FLAG_CSUM, &priv->flags) && likely(wc->wc_flags & IB_WC_IP_CSUM_OK))
		mb->m_pkthdr.csum_flags = CSUM_IP_CHECKED | CSUM_IP_VALID;

	dev->if_input(dev, mb);

repost:
	if (unlikely(ipoib_ib_post_receive(priv, wr_id)))
		ipoib_warn(priv, "ipoib_ib_post_receive failed "
			   "for buf %d\n", wr_id);
}

int ipoib_dma_map_tx(struct ib_device *ca, struct ipoib_tx_buf *tx_req, int max)
{
	struct mbuf *mb = tx_req->mb;
	u64 *mapping = tx_req->mapping;
	struct mbuf *m, *p;
	int error;
	int i;

	for (m = mb, p = NULL, i = 0; m != NULL; p = m, m = m->m_next, i++) {
		if (m->m_len != 0)
			continue;
		if (p == NULL)
			panic("ipoib_dma_map_tx: First mbuf empty\n");
		p->m_next = m_free(m);
		m = p;
		i--;
	}
	i--;
	if (i >= max) {
		tx_req->mb = mb = m_defrag(mb, M_NOWAIT);
		if (mb == NULL)
			return -EIO;
		for (m = mb, i = 0; m != NULL; m = m->m_next, i++);
		if (i >= max)
			return -EIO;
	}
	error = 0;
	for (m = mb, i = 0; m != NULL; m = m->m_next, i++) {
		mapping[i] = ib_dma_map_single(ca, mtod(m, void *),
					       m->m_len, DMA_TO_DEVICE);
		if (unlikely(ib_dma_mapping_error(ca, mapping[i]))) {
			error = -EIO;
			break;
		}
	}
	if (error) {
		int end;

		end = i;
		for (m = mb, i = 0; i < end; m = m->m_next, i++)
			ib_dma_unmap_single(ca, mapping[i], m->m_len,
					    DMA_TO_DEVICE);
	}
	return error;
}

void ipoib_dma_unmap_tx(struct ib_device *ca, struct ipoib_tx_buf *tx_req)
{
	struct mbuf *mb = tx_req->mb;
	u64 *mapping = tx_req->mapping;
	struct mbuf *m;
	int i;

	for (m = mb, i = 0; m != NULL; m = m->m_next, i++)
		ib_dma_unmap_single(ca, mapping[i], m->m_len, DMA_TO_DEVICE);
}

static void ipoib_ib_handle_tx_wc(struct ipoib_dev_priv *priv, struct ib_wc *wc)
{
	struct ifnet *dev = priv->dev;
	unsigned int wr_id = wc->wr_id;
	struct ipoib_tx_buf *tx_req;

	ipoib_dbg_data(priv, "send completion: id %d, status: %d\n",
		       wr_id, wc->status);

	if (unlikely(wr_id >= ipoib_sendq_size)) {
		ipoib_warn(priv, "send completion event with wrid %d (> %d)\n",
			   wr_id, ipoib_sendq_size);
		return;
	}

	tx_req = &priv->tx_ring[wr_id];

	ipoib_dma_unmap_tx(priv->ca, tx_req);

	if_inc_counter(dev, IFCOUNTER_OPACKETS, 1);

	m_freem(tx_req->mb);

	++priv->tx_tail;
	if (unlikely(--priv->tx_outstanding == ipoib_sendq_size >> 1) &&
	    (dev->if_drv_flags & IFF_DRV_OACTIVE) &&
	    test_bit(IPOIB_FLAG_ADMIN_UP, &priv->flags))
		dev->if_drv_flags &= ~IFF_DRV_OACTIVE;

	if (wc->status != IB_WC_SUCCESS &&
	    wc->status != IB_WC_WR_FLUSH_ERR)
		ipoib_warn(priv, "failed send event "
			   "(status=%d, wrid=%d vend_err %x)\n",
			   wc->status, wr_id, wc->vendor_err);
}

int
ipoib_poll_tx(struct ipoib_dev_priv *priv)
{
	int n, i;

	n = ib_poll_cq(priv->send_cq, MAX_SEND_CQE, priv->send_wc);
	for (i = 0; i < n; ++i) {
		struct ib_wc *wc = priv->send_wc + i;
		if (wc->wr_id & IPOIB_OP_CM)
			ipoib_cm_handle_tx_wc(priv, wc);
		else
			ipoib_ib_handle_tx_wc(priv, wc);
	}

	return n == MAX_SEND_CQE;
}

static void
ipoib_poll(struct ipoib_dev_priv *priv)
{
	int n, i;

poll_more:
	spin_lock(&priv->drain_lock);
	for (;;) {
		n = ib_poll_cq(priv->recv_cq, IPOIB_NUM_WC, priv->ibwc);
		for (i = 0; i < n; i++) {
			struct ib_wc *wc = priv->ibwc + i;

			if ((wc->wr_id & IPOIB_OP_RECV) == 0)
				panic("ipoib_poll: Bad wr_id 0x%jX\n",
				    (intmax_t)wc->wr_id);
			if (wc->wr_id & IPOIB_OP_CM)
				ipoib_cm_handle_rx_wc(priv, wc);
			else
				ipoib_ib_handle_rx_wc(priv, wc);
		}

		if (n != IPOIB_NUM_WC)
			break;
	}
	spin_unlock(&priv->drain_lock);

	if (ib_req_notify_cq(priv->recv_cq,
	    IB_CQ_NEXT_COMP | IB_CQ_REPORT_MISSED_EVENTS))
		goto poll_more;
}

void ipoib_ib_completion(struct ib_cq *cq, void *dev_ptr)
{
	struct ipoib_dev_priv *priv = dev_ptr;

	ipoib_poll(priv);
}

static void drain_tx_cq(struct ipoib_dev_priv *priv)
{
	struct ifnet *dev = priv->dev;

	spin_lock(&priv->lock);
	while (ipoib_poll_tx(priv))
		; /* nothing */

	if (dev->if_drv_flags & IFF_DRV_OACTIVE)
		mod_timer(&priv->poll_timer, jiffies + 1);

	spin_unlock(&priv->lock);
}

void ipoib_send_comp_handler(struct ib_cq *cq, void *dev_ptr)
{
	struct ipoib_dev_priv *priv = dev_ptr;

	mod_timer(&priv->poll_timer, jiffies);
}

static inline int
post_send(struct ipoib_dev_priv *priv, unsigned int wr_id,
    struct ib_ah *address, u32 qpn, struct ipoib_tx_buf *tx_req, void *head,
    int hlen)
{
	struct ib_send_wr *bad_wr;
	struct mbuf *mb = tx_req->mb;
	u64 *mapping = tx_req->mapping;
	struct mbuf *m;
	int i;

	for (m = mb, i = 0; m != NULL; m = m->m_next, i++) {
		priv->tx_sge[i].addr         = mapping[i];
		priv->tx_sge[i].length       = m->m_len;
	}
	priv->tx_wr.wr.num_sge	= i;
	priv->tx_wr.wr.wr_id	= wr_id;
	priv->tx_wr.remote_qpn	= qpn;
	priv->tx_wr.ah		= address;

	if (head) {
		priv->tx_wr.mss		= 0; /* XXX mb_shinfo(mb)->gso_size; */
		priv->tx_wr.header	= head;
		priv->tx_wr.hlen	= hlen;
		priv->tx_wr.wr.opcode	= IB_WR_LSO;
	} else
		priv->tx_wr.wr.opcode	= IB_WR_SEND;

	return ib_post_send(priv->qp, &priv->tx_wr.wr, &bad_wr);
}

void
ipoib_send(struct ipoib_dev_priv *priv, struct mbuf *mb,
    struct ipoib_ah *address, u32 qpn)
{
	struct ifnet *dev = priv->dev;
	struct ipoib_tx_buf *tx_req;
	int hlen;
	void *phead;

	if (unlikely(priv->tx_outstanding > MAX_SEND_CQE))
		while (ipoib_poll_tx(priv))
			; /* nothing */

	m_adj(mb, sizeof (struct ipoib_pseudoheader));
	if (0 /* XXX segment offload mb_is_gso(mb) */) {
		/* XXX hlen = mb_transport_offset(mb) + tcp_hdrlen(mb); */
		phead = mtod(mb, void *);
		if (mb->m_len < hlen) {
			ipoib_warn(priv, "linear data too small\n");
			if_inc_counter(dev, IFCOUNTER_OERRORS, 1);
			m_freem(mb);
			return;
		}
		m_adj(mb, hlen);
	} else {
		if (unlikely(mb->m_pkthdr.len - IPOIB_ENCAP_LEN > priv->mcast_mtu)) {
			ipoib_warn(priv, "packet len %d (> %d) too long to send, dropping\n",
				   mb->m_pkthdr.len, priv->mcast_mtu);
			if_inc_counter(dev, IFCOUNTER_OERRORS, 1);
			ipoib_cm_mb_too_long(priv, mb, priv->mcast_mtu);
			return;
		}
		phead = NULL;
		hlen  = 0;
	}

	ipoib_dbg_data(priv, "sending packet, length=%d address=%p qpn=0x%06x\n",
		       mb->m_pkthdr.len, address, qpn);

	/*
	 * We put the mb into the tx_ring _before_ we call post_send()
	 * because it's entirely possible that the completion handler will
	 * run before we execute anything after the post_send().  That
	 * means we have to make sure everything is properly recorded and
	 * our state is consistent before we call post_send().
	 */
	tx_req = &priv->tx_ring[priv->tx_head & (ipoib_sendq_size - 1)];
	tx_req->mb = mb;
	if (unlikely(ipoib_dma_map_tx(priv->ca, tx_req, IPOIB_UD_TX_SG))) {
		if_inc_counter(dev, IFCOUNTER_OERRORS, 1);
		if (tx_req->mb)
			m_freem(tx_req->mb);
		return;
	}

	if (mb->m_pkthdr.csum_flags & (CSUM_IP|CSUM_TCP|CSUM_UDP))
		priv->tx_wr.wr.send_flags |= IB_SEND_IP_CSUM;
	else
		priv->tx_wr.wr.send_flags &= ~IB_SEND_IP_CSUM;

	if (++priv->tx_outstanding == ipoib_sendq_size) {
		ipoib_dbg(priv, "TX ring full, stopping kernel net queue\n");
		if (ib_req_notify_cq(priv->send_cq, IB_CQ_NEXT_COMP))
			ipoib_warn(priv, "request notify on send CQ failed\n");
		dev->if_drv_flags |= IFF_DRV_OACTIVE;
	}

	if (unlikely(post_send(priv,
	    priv->tx_head & (ipoib_sendq_size - 1), address->ah, qpn,
	    tx_req, phead, hlen))) {
		ipoib_warn(priv, "post_send failed\n");
		if_inc_counter(dev, IFCOUNTER_OERRORS, 1);
		--priv->tx_outstanding;
		ipoib_dma_unmap_tx(priv->ca, tx_req);
		m_freem(mb);
		if (dev->if_drv_flags & IFF_DRV_OACTIVE)
			dev->if_drv_flags &= ~IFF_DRV_OACTIVE;
	} else {
		address->last_send = priv->tx_head;
		++priv->tx_head;
	}
}

static void __ipoib_reap_ah(struct ipoib_dev_priv *priv)
{
	struct ipoib_ah *ah, *tah;
	LIST_HEAD(remove_list);
	unsigned long flags;

	spin_lock_irqsave(&priv->lock, flags);

	list_for_each_entry_safe(ah, tah, &priv->dead_ahs, list)
		if ((int) priv->tx_tail - (int) ah->last_send >= 0) {
			list_del(&ah->list);
			ib_destroy_ah(ah->ah);
			kfree(ah);
		}

	spin_unlock_irqrestore(&priv->lock, flags);
}

void ipoib_reap_ah(struct work_struct *work)
{
	struct ipoib_dev_priv *priv =
		container_of(work, struct ipoib_dev_priv, ah_reap_task.work);

	__ipoib_reap_ah(priv);

	if (!test_bit(IPOIB_STOP_REAPER, &priv->flags))
		queue_delayed_work(ipoib_workqueue, &priv->ah_reap_task,
				   HZ);
}

static void ipoib_ah_dev_cleanup(struct ipoib_dev_priv *priv)
{
	unsigned long begin;

	begin = jiffies;

	while (!list_empty(&priv->dead_ahs)) {
		__ipoib_reap_ah(priv);

		if (time_after(jiffies, begin + HZ)) {
			ipoib_warn(priv, "timing out; will leak address handles\n");
			break;
		}

		msleep(1);
	}
}

static void ipoib_ib_tx_timer_func(unsigned long ctx)
{
	drain_tx_cq((struct ipoib_dev_priv *)ctx);
}

int ipoib_ib_dev_open(struct ipoib_dev_priv *priv)
{
	int ret;

	if (ib_find_pkey(priv->ca, priv->port, priv->pkey, &priv->pkey_index)) {
		ipoib_warn(priv, "P_Key 0x%04x not found\n", priv->pkey);
		clear_bit(IPOIB_PKEY_ASSIGNED, &priv->flags);
		return -1;
	}
	set_bit(IPOIB_PKEY_ASSIGNED, &priv->flags);

	ret = ipoib_init_qp(priv);
	if (ret) {
		ipoib_warn(priv, "ipoib_init_qp returned %d\n", ret);
		return -1;
	}

	ret = ipoib_ib_post_receives(priv);
	if (ret) {
		ipoib_warn(priv, "ipoib_ib_post_receives returned %d\n", ret);
		ipoib_ib_dev_stop(priv, 1);
		return -1;
	}

	ret = ipoib_cm_dev_open(priv);
	if (ret) {
		ipoib_warn(priv, "ipoib_cm_dev_open returned %d\n", ret);
		ipoib_ib_dev_stop(priv, 1);
		return -1;
	}

	clear_bit(IPOIB_STOP_REAPER, &priv->flags);
	queue_delayed_work(ipoib_workqueue, &priv->ah_reap_task, HZ);

	set_bit(IPOIB_FLAG_INITIALIZED, &priv->flags);

	return 0;
}

static void ipoib_pkey_dev_check_presence(struct ipoib_dev_priv *priv)
{
	u16 pkey_index = 0;

	if (ib_find_pkey(priv->ca, priv->port, priv->pkey, &pkey_index))
		clear_bit(IPOIB_PKEY_ASSIGNED, &priv->flags);
	else
		set_bit(IPOIB_PKEY_ASSIGNED, &priv->flags);
}

int ipoib_ib_dev_up(struct ipoib_dev_priv *priv)
{

	ipoib_pkey_dev_check_presence(priv);

	if (!test_bit(IPOIB_PKEY_ASSIGNED, &priv->flags)) {
		ipoib_dbg(priv, "PKEY is not assigned.\n");
		return 0;
	}

	set_bit(IPOIB_FLAG_OPER_UP, &priv->flags);

	return ipoib_mcast_start_thread(priv);
}

int ipoib_ib_dev_down(struct ipoib_dev_priv *priv, int flush)
{

	ipoib_dbg(priv, "downing ib_dev\n");

	clear_bit(IPOIB_FLAG_OPER_UP, &priv->flags);
	if_link_state_change(priv->dev, LINK_STATE_DOWN);

	/* Shutdown the P_Key thread if still active */
	if (!test_bit(IPOIB_PKEY_ASSIGNED, &priv->flags)) {
		mutex_lock(&pkey_mutex);
		set_bit(IPOIB_PKEY_STOP, &priv->flags);
		cancel_delayed_work(&priv->pkey_poll_task);
		mutex_unlock(&pkey_mutex);
		if (flush)
			flush_workqueue(ipoib_workqueue);
	}

	ipoib_mcast_stop_thread(priv, flush);
	ipoib_mcast_dev_flush(priv);

	ipoib_flush_paths(priv);

	return 0;
}

static int recvs_pending(struct ipoib_dev_priv *priv)
{
	int pending = 0;
	int i;

	for (i = 0; i < ipoib_recvq_size; ++i)
		if (priv->rx_ring[i].mb)
			++pending;

	return pending;
}

static void check_qp_movement_and_print(struct ipoib_dev_priv *priv,
					struct ib_qp *qp,
					enum ib_qp_state new_state)
{
	struct ib_qp_attr qp_attr;
	struct ib_qp_init_attr query_init_attr;
	int ret;

	ret = ib_query_qp(qp, &qp_attr, IB_QP_STATE, &query_init_attr);
	if (ret) {
		ipoib_warn(priv, "%s: Failed to query QP (%d)\n", __func__, ret);
		return;
	}

	/* print according to the new-state and the previous state */
	if (new_state == IB_QPS_ERR && qp_attr.qp_state == IB_QPS_RESET) {
		ipoib_dbg(priv, "Failed to modify QP %d->%d, acceptable\n",
			  qp_attr.qp_state, new_state);
	} else {
		ipoib_warn(priv, "Failed to modify QP %d->%d\n",
			   qp_attr.qp_state, new_state);
	}
}

void ipoib_drain_cq(struct ipoib_dev_priv *priv)
{
	int i, n;

	spin_lock(&priv->drain_lock);
	do {
		n = ib_poll_cq(priv->recv_cq, IPOIB_NUM_WC, priv->ibwc);
		for (i = 0; i < n; ++i) {
			/*
			 * Convert any successful completions to flush
			 * errors to avoid passing packets up the
			 * stack after bringing the device down.
			 */
			if (priv->ibwc[i].status == IB_WC_SUCCESS)
				priv->ibwc[i].status = IB_WC_WR_FLUSH_ERR;

			if ((priv->ibwc[i].wr_id & IPOIB_OP_RECV) == 0)
				panic("ipoib_drain_cq:  Bad wrid 0x%jX\n",
				    (intmax_t)priv->ibwc[i].wr_id);
			if (priv->ibwc[i].wr_id & IPOIB_OP_CM)
				ipoib_cm_handle_rx_wc(priv, priv->ibwc + i);
			else
				ipoib_ib_handle_rx_wc(priv, priv->ibwc + i);
		}
	} while (n == IPOIB_NUM_WC);
	spin_unlock(&priv->drain_lock);

	spin_lock(&priv->lock);
	while (ipoib_poll_tx(priv))
		; /* nothing */

	spin_unlock(&priv->lock);
}

int ipoib_ib_dev_stop(struct ipoib_dev_priv *priv, int flush)
{
	struct ib_qp_attr qp_attr;
	unsigned long begin;
	struct ipoib_tx_buf *tx_req;
	int i;

	clear_bit(IPOIB_FLAG_INITIALIZED, &priv->flags);

	ipoib_cm_dev_stop(priv);

	/*
	 * Move our QP to the error state and then reinitialize in
	 * when all work requests have completed or have been flushed.
	 */
	qp_attr.qp_state = IB_QPS_ERR;
	if (ib_modify_qp(priv->qp, &qp_attr, IB_QP_STATE))
		check_qp_movement_and_print(priv, priv->qp, IB_QPS_ERR);

	/* Wait for all sends and receives to complete */
	begin = jiffies;

	while (priv->tx_head != priv->tx_tail || recvs_pending(priv)) {
		if (time_after(jiffies, begin + 5 * HZ)) {
			ipoib_warn(priv, "timing out; %d sends %d receives not completed\n",
				   priv->tx_head - priv->tx_tail, recvs_pending(priv));

			/*
			 * assume the HW is wedged and just free up
			 * all our pending work requests.
			 */
			while ((int) priv->tx_tail - (int) priv->tx_head < 0) {
				tx_req = &priv->tx_ring[priv->tx_tail &
							(ipoib_sendq_size - 1)];
				ipoib_dma_unmap_tx(priv->ca, tx_req);
				m_freem(tx_req->mb);
				++priv->tx_tail;
				--priv->tx_outstanding;
			}

			for (i = 0; i < ipoib_recvq_size; ++i) {
				struct ipoib_rx_buf *rx_req;

				rx_req = &priv->rx_ring[i];
				if (!rx_req->mb)
					continue;
				ipoib_dma_unmap_rx(priv, &priv->rx_ring[i]);
				m_freem(rx_req->mb);
				rx_req->mb = NULL;
			}

			goto timeout;
		}

		ipoib_drain_cq(priv);

		msleep(1);
	}

	ipoib_dbg(priv, "All sends and receives done.\n");

timeout:
	del_timer_sync(&priv->poll_timer);
	qp_attr.qp_state = IB_QPS_RESET;
	if (ib_modify_qp(priv->qp, &qp_attr, IB_QP_STATE))
		ipoib_warn(priv, "Failed to modify QP to RESET state\n");

	/* Wait for all AHs to be reaped */
	set_bit(IPOIB_STOP_REAPER, &priv->flags);
	cancel_delayed_work(&priv->ah_reap_task);
	if (flush)
		flush_workqueue(ipoib_workqueue);

	ipoib_ah_dev_cleanup(priv);

	ib_req_notify_cq(priv->recv_cq, IB_CQ_NEXT_COMP);

	return 0;
}

int ipoib_ib_dev_init(struct ipoib_dev_priv *priv, struct ib_device *ca, int port)
{
	struct ifnet *dev = priv->dev;

	priv->ca = ca;
	priv->port = port;
	priv->qp = NULL;

	if (ipoib_transport_dev_init(priv, ca)) {
		printk(KERN_WARNING "%s: ipoib_transport_dev_init failed\n", ca->name);
		return -ENODEV;
	}

	setup_timer(&priv->poll_timer, ipoib_ib_tx_timer_func,
		    (unsigned long) priv);

	if (dev->if_flags & IFF_UP) {
		if (ipoib_ib_dev_open(priv)) {
			ipoib_transport_dev_cleanup(priv);
			return -ENODEV;
		}
	}

	return 0;
}

static void __ipoib_ib_dev_flush(struct ipoib_dev_priv *priv,
				enum ipoib_flush_level level)
{
	struct ipoib_dev_priv *cpriv;
	u16 new_index;

	mutex_lock(&priv->vlan_mutex);

	/*
	 * Flush any child interfaces too -- they might be up even if
	 * the parent is down.
	 */
	list_for_each_entry(cpriv, &priv->child_intfs, list)
		__ipoib_ib_dev_flush(cpriv, level);

	mutex_unlock(&priv->vlan_mutex);

	if (!test_bit(IPOIB_FLAG_INITIALIZED, &priv->flags)) {
		ipoib_dbg(priv, "Not flushing - IPOIB_FLAG_INITIALIZED not set.\n");
		return;
	}

	if (!test_bit(IPOIB_FLAG_ADMIN_UP, &priv->flags)) {
		ipoib_dbg(priv, "Not flushing - IPOIB_FLAG_ADMIN_UP not set.\n");
		return;
	}

	if (level == IPOIB_FLUSH_HEAVY) {
		if (ib_find_pkey(priv->ca, priv->port, priv->pkey, &new_index)) {
			clear_bit(IPOIB_PKEY_ASSIGNED, &priv->flags);
			ipoib_ib_dev_down(priv, 0);
			ipoib_ib_dev_stop(priv, 0);
			if (ipoib_pkey_dev_delay_open(priv))
				return;
		}

		/* restart QP only if P_Key index is changed */
		if (test_and_set_bit(IPOIB_PKEY_ASSIGNED, &priv->flags) &&
		    new_index == priv->pkey_index) {
			ipoib_dbg(priv, "Not flushing - P_Key index not changed.\n");
			return;
		}
		priv->pkey_index = new_index;
	}

	if (level == IPOIB_FLUSH_LIGHT) {
		ipoib_mark_paths_invalid(priv);
		ipoib_mcast_dev_flush(priv);
	}

	if (level >= IPOIB_FLUSH_NORMAL)
		ipoib_ib_dev_down(priv, 0);

	if (level == IPOIB_FLUSH_HEAVY) {
		ipoib_ib_dev_stop(priv, 0);
		ipoib_ib_dev_open(priv);
	}

	/*
	 * The device could have been brought down between the start and when
	 * we get here, don't bring it back up if it's not configured up
	 */
	if (test_bit(IPOIB_FLAG_ADMIN_UP, &priv->flags)) {
		if (level >= IPOIB_FLUSH_NORMAL)
			ipoib_ib_dev_up(priv);
		ipoib_mcast_restart_task(&priv->restart_task);
	}
}

void ipoib_ib_dev_flush_light(struct work_struct *work)
{
	struct ipoib_dev_priv *priv =
		container_of(work, struct ipoib_dev_priv, flush_light);

	__ipoib_ib_dev_flush(priv, IPOIB_FLUSH_LIGHT);
}

void ipoib_ib_dev_flush_normal(struct work_struct *work)
{
	struct ipoib_dev_priv *priv =
		container_of(work, struct ipoib_dev_priv, flush_normal);

	__ipoib_ib_dev_flush(priv, IPOIB_FLUSH_NORMAL);
}

void ipoib_ib_dev_flush_heavy(struct work_struct *work)
{
	struct ipoib_dev_priv *priv =
		container_of(work, struct ipoib_dev_priv, flush_heavy);

	__ipoib_ib_dev_flush(priv, IPOIB_FLUSH_HEAVY);
}

void ipoib_ib_dev_cleanup(struct ipoib_dev_priv *priv)
{

	ipoib_dbg(priv, "cleaning up ib_dev\n");

	ipoib_mcast_stop_thread(priv, 1);
	ipoib_mcast_dev_flush(priv);

	ipoib_ah_dev_cleanup(priv);
	ipoib_transport_dev_cleanup(priv);
}

/*
 * Delayed P_Key Assigment Interim Support
 *
 * The following is initial implementation of delayed P_Key assigment
 * mechanism. It is using the same approach implemented for the multicast
 * group join. The single goal of this implementation is to quickly address
 * Bug #2507. This implementation will probably be removed when the P_Key
 * change async notification is available.
 */

void ipoib_pkey_poll(struct work_struct *work)
{
	struct ipoib_dev_priv *priv =
		container_of(work, struct ipoib_dev_priv, pkey_poll_task.work);

	ipoib_pkey_dev_check_presence(priv);

	if (test_bit(IPOIB_PKEY_ASSIGNED, &priv->flags))
		ipoib_open(priv);
	else {
		mutex_lock(&pkey_mutex);
		if (!test_bit(IPOIB_PKEY_STOP, &priv->flags))
			queue_delayed_work(ipoib_workqueue,
					   &priv->pkey_poll_task,
					   HZ);
		mutex_unlock(&pkey_mutex);
	}
}

int ipoib_pkey_dev_delay_open(struct ipoib_dev_priv *priv)
{

	/* Look for the interface pkey value in the IB Port P_Key table and */
	/* set the interface pkey assigment flag                            */
	ipoib_pkey_dev_check_presence(priv);

	/* P_Key value not assigned yet - start polling */
	if (!test_bit(IPOIB_PKEY_ASSIGNED, &priv->flags)) {
		mutex_lock(&pkey_mutex);
		clear_bit(IPOIB_PKEY_STOP, &priv->flags);
		queue_delayed_work(ipoib_workqueue,
				   &priv->pkey_poll_task,
				   HZ);
		mutex_unlock(&pkey_mutex);
		return 1;
	}

	return 0;
}
