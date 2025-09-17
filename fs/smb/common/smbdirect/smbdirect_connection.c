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
