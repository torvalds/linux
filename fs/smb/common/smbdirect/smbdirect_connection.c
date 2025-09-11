// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *   Copyright (C) 2017, Microsoft Corporation.
 *   Copyright (c) 2025, Stefan Metzmacher
 */

#include "smbdirect_internal.h"

__maybe_unused /* this is temporary while this file is included in others */
static struct smbdirect_send_io *smbdirect_connection_alloc_send_io(struct smbdirect_socket *sc)
{
	struct smbdirect_send_io *msg;

	msg = mempool_alloc(sc->send_io.mem.pool, sc->send_io.mem.gfp_mask);
	if (!msg)
		return ERR_PTR(-ENOMEM);
	msg->socket = sc;
	INIT_LIST_HEAD(&msg->sibling_list);
	msg->num_sge = 0;

	return msg;
}

__maybe_unused /* this is temporary while this file is included in others */
static void smbdirect_connection_free_send_io(struct smbdirect_send_io *msg)
{
	struct smbdirect_socket *sc = msg->socket;
	size_t i;

	/*
	 * The list needs to be empty!
	 * The caller should take care of it.
	 */
	WARN_ON_ONCE(!list_empty(&msg->sibling_list));

	/*
	 * Note we call ib_dma_unmap_page(), even if some sges are mapped using
	 * ib_dma_map_single().
	 *
	 * The difference between _single() and _page() only matters for the
	 * ib_dma_map_*() case.
	 *
	 * For the ib_dma_unmap_*() case it does not matter as both take the
	 * dma_addr_t and dma_unmap_single_attrs() is just an alias to
	 * dma_unmap_page_attrs().
	 */
	for (i = 0; i < msg->num_sge; i++)
		ib_dma_unmap_page(sc->ib.dev,
				  msg->sge[i].addr,
				  msg->sge[i].length,
				  DMA_TO_DEVICE);

	mempool_free(msg, sc->send_io.mem.pool);
}

__maybe_unused /* this is temporary while this file is included in others */
static struct smbdirect_recv_io *smbdirect_connection_get_recv_io(struct smbdirect_socket *sc)
{
	struct smbdirect_recv_io *msg = NULL;
	unsigned long flags;

	spin_lock_irqsave(&sc->recv_io.free.lock, flags);
	if (likely(!sc->first_error))
		msg = list_first_entry_or_null(&sc->recv_io.free.list,
					       struct smbdirect_recv_io,
					       list);
	if (likely(msg)) {
		list_del(&msg->list);
		sc->statistics.get_receive_buffer++;
	}
	spin_unlock_irqrestore(&sc->recv_io.free.lock, flags);

	return msg;
}

__maybe_unused /* this is temporary while this file is included in others */
static void smbdirect_connection_put_recv_io(struct smbdirect_recv_io *msg)
{
	struct smbdirect_socket *sc = msg->socket;
	unsigned long flags;

	if (likely(msg->sge.length != 0)) {
		ib_dma_unmap_single(sc->ib.dev,
				    msg->sge.addr,
				    msg->sge.length,
				    DMA_FROM_DEVICE);
		msg->sge.length = 0;
	}

	spin_lock_irqsave(&sc->recv_io.free.lock, flags);
	list_add_tail(&msg->list, &sc->recv_io.free.list);
	sc->statistics.put_receive_buffer++;
	spin_unlock_irqrestore(&sc->recv_io.free.lock, flags);

	queue_work(sc->workqueue, &sc->recv_io.posted.refill_work);
}

__maybe_unused /* this is temporary while this file is included in others */
static void smbdirect_connection_reassembly_append_recv_io(struct smbdirect_socket *sc,
							   struct smbdirect_recv_io *msg,
							   u32 data_length)
{
	unsigned long flags;

	spin_lock_irqsave(&sc->recv_io.reassembly.lock, flags);
	list_add_tail(&msg->list, &sc->recv_io.reassembly.list);
	sc->recv_io.reassembly.queue_length++;
	/*
	 * Make sure reassembly_data_length is updated after list and
	 * reassembly_queue_length are updated. On the dequeue side
	 * reassembly_data_length is checked without a lock to determine
	 * if reassembly_queue_length and list is up to date
	 */
	virt_wmb();
	sc->recv_io.reassembly.data_length += data_length;
	spin_unlock_irqrestore(&sc->recv_io.reassembly.lock, flags);
	sc->statistics.enqueue_reassembly_queue++;
}

__maybe_unused /* this is temporary while this file is included in others */
static struct smbdirect_recv_io *
smbdirect_connection_reassembly_first_recv_io(struct smbdirect_socket *sc)
{
	struct smbdirect_recv_io *msg;

	msg = list_first_entry_or_null(&sc->recv_io.reassembly.list,
				       struct smbdirect_recv_io,
				       list);

	return msg;
}

static void smbdirect_connection_idle_timer_work(struct work_struct *work)
{
	struct smbdirect_socket *sc =
		container_of(work, struct smbdirect_socket, idle.timer_work.work);
	const struct smbdirect_socket_parameters *sp = &sc->parameters;

	if (sc->idle.keepalive != SMBDIRECT_KEEPALIVE_NONE) {
		smbdirect_log_keep_alive(sc, SMBDIRECT_LOG_ERR,
			"%s => timeout sc->idle.keepalive=%s\n",
			smbdirect_socket_status_string(sc->status),
			sc->idle.keepalive == SMBDIRECT_KEEPALIVE_SENT ?
			"SENT" : "PENDING");
		smbdirect_socket_schedule_cleanup(sc, -ETIMEDOUT);
		return;
	}

	if (sc->status != SMBDIRECT_SOCKET_CONNECTED)
		return;

	/*
	 * Now use the keepalive timeout (instead of keepalive interval)
	 * in order to wait for a response
	 */
	sc->idle.keepalive = SMBDIRECT_KEEPALIVE_PENDING;
	mod_delayed_work(sc->workqueue, &sc->idle.timer_work,
			 msecs_to_jiffies(sp->keepalive_timeout_msec));
	smbdirect_log_keep_alive(sc, SMBDIRECT_LOG_INFO,
		"schedule send of empty idle message\n");
	queue_work(sc->workqueue, &sc->idle.immediate_work);
}

__maybe_unused /* this is temporary while this file is included in others */
static void smbdirect_connection_send_io_done(struct ib_cq *cq, struct ib_wc *wc)
{
	struct smbdirect_send_io *msg =
		container_of(wc->wr_cqe, struct smbdirect_send_io, cqe);
	struct smbdirect_socket *sc = msg->socket;
	struct smbdirect_send_io *sibling, *next;
	int lcredits = 0;

	smbdirect_log_rdma_send(sc, SMBDIRECT_LOG_INFO,
		"smbdirect_send_io completed. status='%s (%d)', opcode=%d\n",
		ib_wc_status_msg(wc->status), wc->status, wc->opcode);

	if (unlikely(!(msg->wr.send_flags & IB_SEND_SIGNALED))) {
		/*
		 * This happens when smbdirect_send_io is a sibling
		 * before the final message, it is signaled on
		 * error anyway, so we need to skip
		 * smbdirect_connection_free_send_io here,
		 * otherwise is will destroy the memory
		 * of the siblings too, which will cause
		 * use after free problems for the others
		 * triggered from ib_drain_qp().
		 */
		if (wc->status != IB_WC_SUCCESS)
			goto skip_free;

		/*
		 * This should not happen!
		 * But we better just close the
		 * connection...
		 */
		smbdirect_log_rdma_send(sc, SMBDIRECT_LOG_ERR,
			"unexpected send completion wc->status=%s (%d) wc->opcode=%d\n",
			ib_wc_status_msg(wc->status), wc->status, wc->opcode);
		smbdirect_socket_schedule_cleanup(sc, -ECONNABORTED);
		return;
	}

	/*
	 * Free possible siblings and then the main send_io
	 */
	list_for_each_entry_safe(sibling, next, &msg->sibling_list, sibling_list) {
		list_del_init(&sibling->sibling_list);
		smbdirect_connection_free_send_io(sibling);
		lcredits += 1;
	}
	/* Note this frees wc->wr_cqe, but not wc */
	smbdirect_connection_free_send_io(msg);
	lcredits += 1;

	if (unlikely(wc->status != IB_WC_SUCCESS || WARN_ON_ONCE(wc->opcode != IB_WC_SEND))) {
skip_free:
		if (wc->status != IB_WC_WR_FLUSH_ERR)
			smbdirect_log_rdma_send(sc, SMBDIRECT_LOG_ERR,
				"wc->status=%s (%d) wc->opcode=%d\n",
				ib_wc_status_msg(wc->status), wc->status, wc->opcode);
		smbdirect_socket_schedule_cleanup(sc, -ECONNABORTED);
		return;
	}

	atomic_add(lcredits, &sc->send_io.lcredits.count);
	wake_up(&sc->send_io.lcredits.wait_queue);

	if (atomic_dec_and_test(&sc->send_io.pending.count))
		wake_up(&sc->send_io.pending.zero_wait_queue);

	wake_up(&sc->send_io.pending.dec_wait_queue);
}
