// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *   Copyright (C) 2017, Microsoft Corporation.
 *   Copyright (c) 2025, Stefan Metzmacher
 */

#include "smbdirect_internal.h"
#include <linux/folio_queue.h>

struct smbdirect_map_sges {
	struct ib_sge *sge;
	size_t num_sge;
	size_t max_sge;
	struct ib_device *device;
	u32 local_dma_lkey;
	enum dma_data_direction direction;
};

static ssize_t smbdirect_map_sges_from_iter(struct iov_iter *iter, size_t len,
					    struct smbdirect_map_sges *state);

__maybe_unused /* this is temporary while this file is included in others */
static void smbdirect_connection_qp_event_handler(struct ib_event *event, void *context)
{
	struct smbdirect_socket *sc = context;

	smbdirect_log_rdma_event(sc, SMBDIRECT_LOG_ERR,
		"%s on device %.*s socket %p (cm_id=%p) status %s first_error %1pe\n",
		ib_event_msg(event->event),
		IB_DEVICE_NAME_MAX,
		event->device->name,
		sc, sc->rdma.cm_id,
		smbdirect_socket_status_string(sc->status),
		SMBDIRECT_DEBUG_ERR_PTR(sc->first_error));

	switch (event->event) {
	case IB_EVENT_CQ_ERR:
	case IB_EVENT_QP_FATAL:
		smbdirect_socket_schedule_cleanup(sc, -ECONNABORTED);
		break;

	default:
		break;
	}
}

static u32 smbdirect_rdma_rw_send_wrs(struct ib_device *dev,
				      const struct ib_qp_init_attr *attr)
{
	/*
	 * This could be split out of rdma_rw_init_qp()
	 * and be a helper function next to rdma_rw_mr_factor()
	 *
	 * We can't check unlikely(rdma_rw_force_mr) here,
	 * but that is most likely 0 anyway.
	 */
	u32 factor;

	WARN_ON_ONCE(attr->port_num == 0);

	/*
	 * Each context needs at least one RDMA READ or WRITE WR.
	 *
	 * For some hardware we might need more, eventually we should ask the
	 * HCA driver for a multiplier here.
	 */
	factor = 1;

	/*
	 * If the device needs MRs to perform RDMA READ or WRITE operations,
	 * we'll need two additional MRs for the registrations and the
	 * invalidation.
	 */
	if (rdma_protocol_iwarp(dev, attr->port_num) || dev->attrs.max_sgl_rd)
		factor += 2;	/* inv + reg */

	return factor * attr->cap.max_rdma_ctxs;
}

static void smbdirect_connection_destroy_qp(struct smbdirect_socket *sc);

__maybe_unused /* this is temporary while this file is included in others */
static int smbdirect_connection_create_qp(struct smbdirect_socket *sc)
{
	const struct smbdirect_socket_parameters *sp = &sc->parameters;
	struct ib_qp_init_attr qp_attr;
	struct ib_qp_cap qp_cap;
	u32 rdma_send_wr;
	u32 max_send_wr;
	int ret;

	/*
	 * Note that {rdma,ib}_create_qp() will call
	 * rdma_rw_init_qp() if max_rdma_ctxs is not 0.
	 * It will adjust max_send_wr to the required
	 * number of additional WRs for the RDMA RW operations.
	 * It will cap max_send_wr to the device limit.
	 *
	 * We use allocate sp->responder_resources * 2 MRs
	 * and each MR needs WRs for REG and INV, so
	 * we use '* 4'.
	 *
	 * +1 for ib_drain_qp()
	 */
	memset(&qp_cap, 0, sizeof(qp_cap));
	qp_cap.max_send_wr = sp->send_credit_target + sp->responder_resources * 4 + 1;
	qp_cap.max_recv_wr = sp->recv_credit_max + 1;
	qp_cap.max_send_sge = SMBDIRECT_SEND_IO_MAX_SGE;
	qp_cap.max_recv_sge = SMBDIRECT_RECV_IO_MAX_SGE;
	qp_cap.max_inline_data = 0;
	qp_cap.max_rdma_ctxs = sc->rw_io.credits.max;

	/*
	 * Find out the number of max_send_wr
	 * after rdma_rw_init_qp() adjusted it.
	 *
	 * We only do it on a temporary variable,
	 * as rdma_create_qp() will trigger
	 * rdma_rw_init_qp() again.
	 */
	memset(&qp_attr, 0, sizeof(qp_attr));
	qp_attr.cap = qp_cap;
	qp_attr.port_num = sc->rdma.cm_id->port_num;
	rdma_send_wr = smbdirect_rdma_rw_send_wrs(sc->ib.dev, &qp_attr);
	max_send_wr = qp_cap.max_send_wr + rdma_send_wr;

	if (qp_cap.max_send_wr > sc->ib.dev->attrs.max_cqe ||
	    qp_cap.max_send_wr > sc->ib.dev->attrs.max_qp_wr) {
		pr_err("Possible CQE overrun: max_send_wr %d\n",
		       qp_cap.max_send_wr);
		pr_err("device %.*s reporting max_cqe %d max_qp_wr %d\n",
		       IB_DEVICE_NAME_MAX,
		       sc->ib.dev->name,
		       sc->ib.dev->attrs.max_cqe,
		       sc->ib.dev->attrs.max_qp_wr);
		pr_err("consider lowering send_credit_target = %d\n",
		       sp->send_credit_target);
		return -EINVAL;
	}

	if (qp_cap.max_rdma_ctxs &&
	    (max_send_wr >= sc->ib.dev->attrs.max_cqe ||
	     max_send_wr >= sc->ib.dev->attrs.max_qp_wr)) {
		pr_err("Possible CQE overrun: rdma_send_wr %d + max_send_wr %d = %d\n",
		       rdma_send_wr, qp_cap.max_send_wr, max_send_wr);
		pr_err("device %.*s reporting max_cqe %d max_qp_wr %d\n",
		       IB_DEVICE_NAME_MAX,
		       sc->ib.dev->name,
		       sc->ib.dev->attrs.max_cqe,
		       sc->ib.dev->attrs.max_qp_wr);
		pr_err("consider lowering send_credit_target = %d, max_rdma_ctxs = %d\n",
		       sp->send_credit_target, qp_cap.max_rdma_ctxs);
		return -EINVAL;
	}

	if (qp_cap.max_recv_wr > sc->ib.dev->attrs.max_cqe ||
	    qp_cap.max_recv_wr > sc->ib.dev->attrs.max_qp_wr) {
		pr_err("Possible CQE overrun: max_recv_wr %d\n",
		       qp_cap.max_recv_wr);
		pr_err("device %.*s reporting max_cqe %d max_qp_wr %d\n",
		       IB_DEVICE_NAME_MAX,
		       sc->ib.dev->name,
		       sc->ib.dev->attrs.max_cqe,
		       sc->ib.dev->attrs.max_qp_wr);
		pr_err("consider lowering receive_credit_max = %d\n",
		       sp->recv_credit_max);
		return -EINVAL;
	}

	if (qp_cap.max_send_sge > sc->ib.dev->attrs.max_send_sge ||
	    qp_cap.max_recv_sge > sc->ib.dev->attrs.max_recv_sge) {
		pr_err("device %.*s max_send_sge/max_recv_sge = %d/%d too small\n",
		       IB_DEVICE_NAME_MAX,
		       sc->ib.dev->name,
		       sc->ib.dev->attrs.max_send_sge,
		       sc->ib.dev->attrs.max_recv_sge);
		return -EINVAL;
	}

	sc->ib.pd = ib_alloc_pd(sc->ib.dev, 0);
	if (IS_ERR(sc->ib.pd)) {
		pr_err("Can't create RDMA PD: %1pe\n", sc->ib.pd);
		ret = PTR_ERR(sc->ib.pd);
		sc->ib.pd = NULL;
		return ret;
	}

	sc->ib.send_cq = ib_alloc_cq_any(sc->ib.dev, sc,
					 max_send_wr,
					 sc->ib.poll_ctx);
	if (IS_ERR(sc->ib.send_cq)) {
		pr_err("Can't create RDMA send CQ: %1pe\n", sc->ib.send_cq);
		ret = PTR_ERR(sc->ib.send_cq);
		sc->ib.send_cq = NULL;
		goto err;
	}

	sc->ib.recv_cq = ib_alloc_cq_any(sc->ib.dev, sc,
					 qp_cap.max_recv_wr,
					 sc->ib.poll_ctx);
	if (IS_ERR(sc->ib.recv_cq)) {
		pr_err("Can't create RDMA recv CQ: %1pe\n", sc->ib.recv_cq);
		ret = PTR_ERR(sc->ib.recv_cq);
		sc->ib.recv_cq = NULL;
		goto err;
	}

	/*
	 * We reset completely here!
	 * As the above use was just temporary
	 * to calc max_send_wr and rdma_send_wr.
	 *
	 * rdma_create_qp() will trigger rdma_rw_init_qp()
	 * again if max_rdma_ctxs is not 0.
	 */
	memset(&qp_attr, 0, sizeof(qp_attr));
	qp_attr.event_handler = smbdirect_connection_qp_event_handler;
	qp_attr.qp_context = sc;
	qp_attr.cap = qp_cap;
	qp_attr.sq_sig_type = IB_SIGNAL_REQ_WR;
	qp_attr.qp_type = IB_QPT_RC;
	qp_attr.send_cq = sc->ib.send_cq;
	qp_attr.recv_cq = sc->ib.recv_cq;
	qp_attr.port_num = ~0;

	ret = rdma_create_qp(sc->rdma.cm_id, sc->ib.pd, &qp_attr);
	if (ret) {
		pr_err("Can't create RDMA QP: %1pe\n",
		       SMBDIRECT_DEBUG_ERR_PTR(ret));
		goto err;
	}
	sc->ib.qp = sc->rdma.cm_id->qp;

	return 0;
err:
	smbdirect_connection_destroy_qp(sc);
	return ret;
}

static void smbdirect_connection_destroy_qp(struct smbdirect_socket *sc)
{
	if (sc->ib.qp) {
		ib_drain_qp(sc->ib.qp);
		sc->ib.qp = NULL;
		rdma_destroy_qp(sc->rdma.cm_id);
	}
	if (sc->ib.recv_cq) {
		ib_destroy_cq(sc->ib.recv_cq);
		sc->ib.recv_cq = NULL;
	}
	if (sc->ib.send_cq) {
		ib_destroy_cq(sc->ib.send_cq);
		sc->ib.send_cq = NULL;
	}
	if (sc->ib.pd) {
		ib_dealloc_pd(sc->ib.pd);
		sc->ib.pd = NULL;
	}
}

static void smbdirect_connection_destroy_mem_pools(struct smbdirect_socket *sc);

__maybe_unused /* this is temporary while this file is included in others */
static int smbdirect_connection_create_mem_pools(struct smbdirect_socket *sc)
{
	const struct smbdirect_socket_parameters *sp = &sc->parameters;
	char name[80];
	size_t i;

	/*
	 * We use sizeof(struct smbdirect_negotiate_resp) for the
	 * payload size as it is larger as
	 * sizeof(struct smbdirect_data_transfer).
	 *
	 * This will fit client and server usage for now.
	 */
	snprintf(name, sizeof(name), "smbdirect_send_io_cache_%p", sc);
	struct kmem_cache_args send_io_args = {
		.align		= __alignof__(struct smbdirect_send_io),
	};
	sc->send_io.mem.cache = kmem_cache_create(name,
						  sizeof(struct smbdirect_send_io) +
						  sizeof(struct smbdirect_negotiate_resp),
						  &send_io_args,
						  SLAB_HWCACHE_ALIGN);
	if (!sc->send_io.mem.cache)
		goto err;

	sc->send_io.mem.pool = mempool_create_slab_pool(sp->send_credit_target,
							sc->send_io.mem.cache);
	if (!sc->send_io.mem.pool)
		goto err;

	/*
	 * A payload size of sp->max_recv_size should fit
	 * any message.
	 *
	 * For smbdirect_data_transfer messages the whole
	 * buffer might be exposed to userspace
	 * (currently on the client side...)
	 * The documentation says data_offset = 0 would be
	 * strange but valid.
	 */
	snprintf(name, sizeof(name), "smbdirect_recv_io_cache_%p", sc);
	struct kmem_cache_args recv_io_args = {
		.align		= __alignof__(struct smbdirect_recv_io),
		.useroffset	= sizeof(struct smbdirect_recv_io),
		.usersize	= sp->max_recv_size,
	};
	sc->recv_io.mem.cache = kmem_cache_create(name,
						  sizeof(struct smbdirect_recv_io) +
						  sp->max_recv_size,
						  &recv_io_args,
						  SLAB_HWCACHE_ALIGN);
	if (!sc->recv_io.mem.cache)
		goto err;

	sc->recv_io.mem.pool = mempool_create_slab_pool(sp->recv_credit_max,
							sc->recv_io.mem.cache);
	if (!sc->recv_io.mem.pool)
		goto err;

	for (i = 0; i < sp->recv_credit_max; i++) {
		struct smbdirect_recv_io *recv_io;

		recv_io = mempool_alloc(sc->recv_io.mem.pool,
					sc->recv_io.mem.gfp_mask);
		if (!recv_io)
			goto err;
		recv_io->socket = sc;
		recv_io->sge.length = 0;
		list_add_tail(&recv_io->list, &sc->recv_io.free.list);
	}

	return 0;
err:
	smbdirect_connection_destroy_mem_pools(sc);
	return -ENOMEM;
}

static void smbdirect_connection_destroy_mem_pools(struct smbdirect_socket *sc)
{
	struct smbdirect_recv_io *recv_io, *next_io;

	list_for_each_entry_safe(recv_io, next_io, &sc->recv_io.free.list, list) {
		list_del(&recv_io->list);
		mempool_free(recv_io, sc->recv_io.mem.pool);
	}

	/*
	 * Note mempool_destroy() and kmem_cache_destroy()
	 * work fine with a NULL pointer
	 */

	mempool_destroy(sc->recv_io.mem.pool);
	sc->recv_io.mem.pool = NULL;

	kmem_cache_destroy(sc->recv_io.mem.cache);
	sc->recv_io.mem.cache = NULL;

	mempool_destroy(sc->send_io.mem.pool);
	sc->send_io.mem.pool = NULL;

	kmem_cache_destroy(sc->send_io.mem.cache);
	sc->send_io.mem.cache = NULL;
}

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

__maybe_unused /* this is temporary while this file is included in others */
static void smbdirect_connection_negotiate_rdma_resources(struct smbdirect_socket *sc,
							  u8 peer_initiator_depth,
							  u8 peer_responder_resources,
							  const struct rdma_conn_param *param)
{
	struct smbdirect_socket_parameters *sp = &sc->parameters;

	if (rdma_protocol_iwarp(sc->ib.dev, sc->rdma.cm_id->port_num) &&
	    param->private_data_len == 8) {
		/*
		 * Legacy clients with only iWarp MPA v1 support
		 * need a private blob in order to negotiate
		 * the IRD/ORD values.
		 */
		const __be32 *ird_ord_hdr = param->private_data;
		u32 ird32 = be32_to_cpu(ird_ord_hdr[0]);
		u32 ord32 = be32_to_cpu(ird_ord_hdr[1]);

		/*
		 * cifs.ko sends the legacy IRD/ORD negotiation
		 * event if iWarp MPA v2 was used.
		 *
		 * Here we check that the values match and only
		 * mark the client as legacy if they don't match.
		 */
		if ((u32)param->initiator_depth != ird32 ||
		    (u32)param->responder_resources != ord32) {
			/*
			 * There are broken clients (old cifs.ko)
			 * using little endian and also
			 * struct rdma_conn_param only uses u8
			 * for initiator_depth and responder_resources,
			 * so we truncate the value to U8_MAX.
			 *
			 * smb_direct_accept_client() will then
			 * do the real negotiation in order to
			 * select the minimum between client and
			 * server.
			 */
			ird32 = min_t(u32, ird32, U8_MAX);
			ord32 = min_t(u32, ord32, U8_MAX);

			sc->rdma.legacy_iwarp = true;
			peer_initiator_depth = (u8)ird32;
			peer_responder_resources = (u8)ord32;
		}
	}

	/*
	 * negotiate the value by using the minimum
	 * between client and server if the client provided
	 * non 0 values.
	 */
	if (peer_initiator_depth != 0)
		sp->initiator_depth = min_t(u8, sp->initiator_depth,
					    peer_initiator_depth);
	if (peer_responder_resources != 0)
		sp->responder_resources = min_t(u8, sp->responder_resources,
						peer_responder_resources);
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

__maybe_unused /* this is temporary while this file is included in others */
static int smbdirect_connection_post_recv_io(struct smbdirect_recv_io *msg)
{
	struct smbdirect_socket *sc = msg->socket;
	const struct smbdirect_socket_parameters *sp = &sc->parameters;
	struct ib_recv_wr recv_wr = {
		.wr_cqe = &msg->cqe,
		.sg_list = &msg->sge,
		.num_sge = 1,
	};
	int ret;

	if (unlikely(sc->first_error))
		return sc->first_error;

	msg->sge.addr = ib_dma_map_single(sc->ib.dev,
					  msg->packet,
					  sp->max_recv_size,
					  DMA_FROM_DEVICE);
	ret = ib_dma_mapping_error(sc->ib.dev, msg->sge.addr);
	if (ret)
		return ret;

	msg->sge.length = sp->max_recv_size;
	msg->sge.lkey = sc->ib.pd->local_dma_lkey;

	ret = ib_post_recv(sc->ib.qp, &recv_wr, NULL);
	if (ret) {
		smbdirect_log_rdma_recv(sc, SMBDIRECT_LOG_ERR,
			"ib_post_recv failed ret=%d (%1pe)\n",
			ret, SMBDIRECT_DEBUG_ERR_PTR(ret));
		ib_dma_unmap_single(sc->ib.dev,
				    msg->sge.addr,
				    msg->sge.length,
				    DMA_FROM_DEVICE);
		msg->sge.length = 0;
		smbdirect_socket_schedule_cleanup(sc, ret);
	}

	return ret;
}

static int smbdirect_connection_recv_io_refill(struct smbdirect_socket *sc)
{
	int missing;
	int posted = 0;

	if (unlikely(sc->first_error))
		return sc->first_error;

	/*
	 * Find out how much smbdirect_recv_io buffers we should post.
	 *
	 * Note that sc->recv_io.credits.target is the value
	 * from the peer and it can in theory change over time,
	 * but it is forced to be at least 1 and at max
	 * sp->recv_credit_max.
	 *
	 * So it can happen that missing will be lower than 0,
	 * which means the peer has recently lowered its desired
	 * target, while be already granted a higher number of credits.
	 *
	 * Note 'posted' is the number of smbdirect_recv_io buffers
	 * posted within this function, while sc->recv_io.posted.count
	 * is the overall value of posted smbdirect_recv_io buffers.
	 *
	 * We try to post as much buffers as missing, but
	 * this is limited if a lot of smbdirect_recv_io buffers
	 * are still in the sc->recv_io.reassembly.list instead of
	 * the sc->recv_io.free.list.
	 *
	 */
	missing = (int)sc->recv_io.credits.target - atomic_read(&sc->recv_io.posted.count);
	while (posted < missing) {
		struct smbdirect_recv_io *recv_io;
		int ret;

		/*
		 * It's ok if smbdirect_connection_get_recv_io()
		 * returns NULL, it means smbdirect_recv_io structures
		 * are still be in the reassembly.list.
		 */
		recv_io = smbdirect_connection_get_recv_io(sc);
		if (!recv_io)
			break;

		recv_io->first_segment = false;

		ret = smbdirect_connection_post_recv_io(recv_io);
		if (ret) {
			smbdirect_log_rdma_recv(sc, SMBDIRECT_LOG_ERR,
				"smbdirect_connection_post_recv_io failed rc=%d (%1pe)\n",
				ret, SMBDIRECT_DEBUG_ERR_PTR(ret));
			smbdirect_connection_put_recv_io(recv_io);
			return ret;
		}

		atomic_inc(&sc->recv_io.posted.count);
		posted += 1;
	}

	/* If nothing was posted we're done */
	if (posted == 0)
		return 0;

	atomic_add(posted, &sc->recv_io.credits.available);

	/*
	 * If we posted at least one smbdirect_recv_io buffer,
	 * we need to inform the peer about it and grant
	 * additional credits.
	 *
	 * However there is one case where we don't want to
	 * do that.
	 *
	 * If only a single credit was missing before
	 * reaching the requested target, we should not
	 * post an immediate send, as that would cause
	 * endless ping pong once a keep alive exchange
	 * is started.
	 *
	 * However if sc->recv_io.credits.target is only 1,
	 * the peer has no credit left and we need to
	 * grant the credit anyway.
	 */
	if (missing == 1 && sc->recv_io.credits.target != 1)
		return 0;

	return posted;
}

__maybe_unused /* this is temporary while this file is included in others */
static void smbdirect_connection_recv_io_refill_work(struct work_struct *work)
{
	struct smbdirect_socket *sc =
		container_of(work, struct smbdirect_socket, recv_io.posted.refill_work);
	int posted;

	posted = smbdirect_connection_recv_io_refill(sc);
	if (unlikely(posted < 0)) {
		smbdirect_socket_schedule_cleanup(sc, posted);
		return;
	}
	if (posted > 0) {
		smbdirect_log_keep_alive(sc, SMBDIRECT_LOG_INFO,
			"schedule send of an empty message\n");
		queue_work(sc->workqueue, &sc->idle.immediate_work);
	}
}

static bool smbdirect_map_sges_single_page(struct smbdirect_map_sges *state,
					   struct page *page, size_t off, size_t len)
{
	struct ib_sge *sge;
	u64 addr;

	if (state->num_sge >= state->max_sge)
		return false;

	addr = ib_dma_map_page(state->device, page,
			       off, len, state->direction);
	if (ib_dma_mapping_error(state->device, addr))
		return false;

	sge = &state->sge[state->num_sge++];
	sge->addr   = addr;
	sge->length = len;
	sge->lkey   = state->local_dma_lkey;

	return true;
}

/*
 * Extract page fragments from a BVEC-class iterator and add them to an ib_sge
 * list.  The pages are not pinned.
 */
static ssize_t smbdirect_map_sges_from_bvec(struct iov_iter *iter,
					    struct smbdirect_map_sges *state,
					    ssize_t maxsize)
{
	const struct bio_vec *bv = iter->bvec;
	unsigned long start = iter->iov_offset;
	unsigned int i;
	ssize_t ret = 0;

	for (i = 0; i < iter->nr_segs; i++) {
		size_t off, len;
		bool ok;

		len = bv[i].bv_len;
		if (start >= len) {
			start -= len;
			continue;
		}

		len = min_t(size_t, maxsize, len - start);
		off = bv[i].bv_offset + start;

		ok = smbdirect_map_sges_single_page(state,
						    bv[i].bv_page,
						    off,
						    len);
		if (!ok)
			return -EIO;

		ret += len;
		maxsize -= len;
		if (state->num_sge >= state->max_sge || maxsize <= 0)
			break;
		start = 0;
	}

	if (ret > 0)
		iov_iter_advance(iter, ret);
	return ret;
}

/*
 * Extract fragments from a KVEC-class iterator and add them to an ib_sge list.
 * This can deal with vmalloc'd buffers as well as kmalloc'd or static buffers.
 * The pages are not pinned.
 */
static ssize_t smbdirect_map_sges_from_kvec(struct iov_iter *iter,
					    struct smbdirect_map_sges *state,
					    ssize_t maxsize)
{
	const struct kvec *kv = iter->kvec;
	unsigned long start = iter->iov_offset;
	unsigned int i;
	ssize_t ret = 0;

	for (i = 0; i < iter->nr_segs; i++) {
		struct page *page;
		unsigned long kaddr;
		size_t off, len, seg;

		len = kv[i].iov_len;
		if (start >= len) {
			start -= len;
			continue;
		}

		kaddr = (unsigned long)kv[i].iov_base + start;
		off = kaddr & ~PAGE_MASK;
		len = min_t(size_t, maxsize, len - start);
		kaddr &= PAGE_MASK;

		maxsize -= len;
		do {
			bool ok;

			seg = min_t(size_t, len, PAGE_SIZE - off);

			if (is_vmalloc_or_module_addr((void *)kaddr))
				page = vmalloc_to_page((void *)kaddr);
			else
				page = virt_to_page((void *)kaddr);

			ok = smbdirect_map_sges_single_page(state, page, off, seg);
			if (!ok)
				return -EIO;

			ret += seg;
			len -= seg;
			kaddr += PAGE_SIZE;
			off = 0;
		} while (len > 0 && state->num_sge < state->max_sge);

		if (state->num_sge >= state->max_sge || maxsize <= 0)
			break;
		start = 0;
	}

	if (ret > 0)
		iov_iter_advance(iter, ret);
	return ret;
}

/*
 * Extract folio fragments from a FOLIOQ-class iterator and add them to an
 * ib_sge list.  The folios are not pinned.
 */
static ssize_t smbdirect_map_sges_from_folioq(struct iov_iter *iter,
					      struct smbdirect_map_sges *state,
					      ssize_t maxsize)
{
	const struct folio_queue *folioq = iter->folioq;
	unsigned int slot = iter->folioq_slot;
	ssize_t ret = 0;
	size_t offset = iter->iov_offset;

	if (WARN_ON_ONCE(!folioq))
		return -EIO;

	if (slot >= folioq_nr_slots(folioq)) {
		folioq = folioq->next;
		if (WARN_ON_ONCE(!folioq))
			return -EIO;
		slot = 0;
	}

	do {
		struct folio *folio = folioq_folio(folioq, slot);
		size_t fsize = folioq_folio_size(folioq, slot);

		if (offset < fsize) {
			size_t part = umin(maxsize, fsize - offset);
			bool ok;

			ok = smbdirect_map_sges_single_page(state,
							    folio_page(folio, 0),
							    offset,
							    part);
			if (!ok)
				return -EIO;

			offset += part;
			ret += part;
			maxsize -= part;
		}

		if (offset >= fsize) {
			offset = 0;
			slot++;
			if (slot >= folioq_nr_slots(folioq)) {
				if (!folioq->next) {
					WARN_ON_ONCE(ret < iter->count);
					break;
				}
				folioq = folioq->next;
				slot = 0;
			}
		}
	} while (state->num_sge < state->max_sge && maxsize > 0);

	iter->folioq = folioq;
	iter->folioq_slot = slot;
	iter->iov_offset = offset;
	iter->count -= ret;
	return ret;
}

/*
 * Extract page fragments from up to the given amount of the source iterator
 * and build up an ib_sge list that refers to all of those bits.  The ib_sge list
 * is appended to, up to the maximum number of elements set in the parameter
 * block.
 *
 * The extracted page fragments are not pinned or ref'd in any way; if an
 * IOVEC/UBUF-type iterator is to be used, it should be converted to a
 * BVEC-type iterator and the pages pinned, ref'd or otherwise held in some
 * way.
 */
__maybe_unused /* this is temporary while this file is included in others */
static ssize_t smbdirect_map_sges_from_iter(struct iov_iter *iter, size_t len,
					    struct smbdirect_map_sges *state)
{
	ssize_t ret;
	size_t before = state->num_sge;

	if (WARN_ON_ONCE(iov_iter_rw(iter) != ITER_SOURCE))
		return -EIO;

	switch (iov_iter_type(iter)) {
	case ITER_BVEC:
		ret = smbdirect_map_sges_from_bvec(iter, state, len);
		break;
	case ITER_KVEC:
		ret = smbdirect_map_sges_from_kvec(iter, state, len);
		break;
	case ITER_FOLIOQ:
		ret = smbdirect_map_sges_from_folioq(iter, state, len);
		break;
	default:
		WARN_ONCE(1, "iov_iter_type[%u]\n", iov_iter_type(iter));
		return -EIO;
	}

	if (ret < 0) {
		while (state->num_sge > before) {
			struct ib_sge *sge = &state->sge[state->num_sge--];

			ib_dma_unmap_page(state->device,
					  sge->addr,
					  sge->length,
					  state->direction);
		}
	}

	return ret;
}
