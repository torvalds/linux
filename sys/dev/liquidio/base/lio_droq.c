/*
 *   BSD LICENSE
 *
 *   Copyright(c) 2017 Cavium, Inc.. All rights reserved.
 *   All rights reserved.
 *
 *   Redistribution and use in source and binary forms, with or without
 *   modification, are permitted provided that the following conditions
 *   are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in
 *       the documentation and/or other materials provided with the
 *       distribution.
 *     * Neither the name of Cavium, Inc. nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 *   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 *   A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 *   OWNER(S) OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 *   LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 *   DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 *   THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 *   (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 *   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
/*$FreeBSD$*/

#include "lio_bsd.h"
#include "lio_common.h"
#include "lio_droq.h"
#include "lio_iq.h"
#include "lio_response_manager.h"
#include "lio_device.h"
#include "lio_main.h"
#include "cn23xx_pf_device.h"
#include "lio_network.h"

struct __dispatch {
	struct lio_stailq_node	node;
	struct lio_recv_info	*rinfo;
	lio_dispatch_fn_t	disp_fn;
};

void	*lio_get_dispatch_arg(struct octeon_device *oct,
			      uint16_t opcode, uint16_t subcode);

/*
 *  Get the argument that the user set when registering dispatch
 *  function for a given opcode/subcode.
 *  @param  octeon_dev - the octeon device pointer.
 *  @param  opcode     - the opcode for which the dispatch argument
 *                       is to be checked.
 *  @param  subcode    - the subcode for which the dispatch argument
 *                       is to be checked.
 *  @return  Success: void * (argument to the dispatch function)
 *  @return  Failure: NULL
 *
 */
void   *
lio_get_dispatch_arg(struct octeon_device *octeon_dev,
		     uint16_t opcode, uint16_t subcode)
{
	struct lio_stailq_node	*dispatch;
	void			*fn_arg = NULL;
	int			idx;
	uint16_t		combined_opcode;

	combined_opcode = LIO_OPCODE_SUBCODE(opcode, subcode);

	idx = combined_opcode & LIO_OPCODE_MASK;

	mtx_lock(&octeon_dev->dispatch.lock);

	if (octeon_dev->dispatch.count == 0) {
		mtx_unlock(&octeon_dev->dispatch.lock);
		return (NULL);
	}

	if (octeon_dev->dispatch.dlist[idx].opcode == combined_opcode) {
		fn_arg = octeon_dev->dispatch.dlist[idx].arg;
	} else {
		STAILQ_FOREACH(dispatch,
			       &octeon_dev->dispatch.dlist[idx].head, entries) {
			if (((struct lio_dispatch *)dispatch)->opcode ==
			    combined_opcode) {
				fn_arg = ((struct lio_dispatch *)dispatch)->arg;
				break;
			}
		}
	}

	mtx_unlock(&octeon_dev->dispatch.lock);
	return (fn_arg);
}

/*
 *  Check for packets on Droq. This function should be called with lock held.
 *  @param  droq - Droq on which count is checked.
 *  @return Returns packet count.
 */
uint32_t
lio_droq_check_hw_for_pkts(struct lio_droq *droq)
{
	struct octeon_device	*oct = droq->oct_dev;
	uint32_t		last_count;
	uint32_t		pkt_count = 0;

	pkt_count = lio_read_csr32(oct, droq->pkts_sent_reg);

	last_count = pkt_count - droq->pkt_count;
	droq->pkt_count = pkt_count;

	/* we shall write to cnts at the end of processing */
	if (last_count)
		atomic_add_int(&droq->pkts_pending, last_count);

	return (last_count);
}

static void
lio_droq_compute_max_packet_bufs(struct lio_droq *droq)
{
	uint32_t	count = 0;

	/*
	 * max_empty_descs is the max. no. of descs that can have no buffers.
	 * If the empty desc count goes beyond this value, we cannot safely
	 * read in a 64K packet sent by Octeon
	 * (64K is max pkt size from Octeon)
	 */
	droq->max_empty_descs = 0;

	do {
		droq->max_empty_descs++;
		count += droq->buffer_size;
	} while (count < (64 * 1024));

	droq->max_empty_descs = droq->max_count - droq->max_empty_descs;
}

static void
lio_droq_reset_indices(struct lio_droq *droq)
{

	droq->read_idx = 0;
	droq->refill_idx = 0;
	droq->refill_count = 0;
	atomic_store_rel_int(&droq->pkts_pending, 0);
}

static void
lio_droq_destroy_ring_buffers(struct octeon_device *oct,
			      struct lio_droq *droq)
{
	uint32_t	i;

	for (i = 0; i < droq->max_count; i++) {
		if (droq->recv_buf_list[i].buffer != NULL) {
			lio_recv_buffer_free(droq->recv_buf_list[i].buffer);
			droq->recv_buf_list[i].buffer = NULL;
		}
	}

	lio_droq_reset_indices(droq);
}

static int
lio_droq_setup_ring_buffers(struct octeon_device *oct,
			    struct lio_droq *droq)
{
	struct lio_droq_desc	*desc_ring = droq->desc_ring;
	void			*buf;
	uint32_t		i;

	for (i = 0; i < droq->max_count; i++) {
		buf = lio_recv_buffer_alloc(droq->buffer_size);

		if (buf == NULL) {
			lio_dev_err(oct, "%s buffer alloc failed\n",
				    __func__);
			droq->stats.rx_alloc_failure++;
			return (-ENOMEM);
		}

		droq->recv_buf_list[i].buffer = buf;
		droq->recv_buf_list[i].data = ((struct mbuf *)buf)->m_data;
		desc_ring[i].info_ptr = 0;
		desc_ring[i].buffer_ptr =
			lio_map_ring(oct->device, droq->recv_buf_list[i].buffer,
				     droq->buffer_size);
	}

	lio_droq_reset_indices(droq);

	lio_droq_compute_max_packet_bufs(droq);

	return (0);
}

int
lio_delete_droq(struct octeon_device *oct, uint32_t q_no)
{
	struct lio_droq	*droq = oct->droq[q_no];

	lio_dev_dbg(oct, "%s[%d]\n", __func__, q_no);

	while (taskqueue_cancel(droq->droq_taskqueue, &droq->droq_task, NULL))
		taskqueue_drain(droq->droq_taskqueue, &droq->droq_task);

	taskqueue_free(droq->droq_taskqueue);
	droq->droq_taskqueue = NULL;

	lio_droq_destroy_ring_buffers(oct, droq);
	free(droq->recv_buf_list, M_DEVBUF);

	if (droq->desc_ring != NULL)
		lio_dma_free((droq->max_count * LIO_DROQ_DESC_SIZE),
			     droq->desc_ring);

	oct->io_qmask.oq &= ~(1ULL << q_no);
	bzero(oct->droq[q_no], sizeof(struct lio_droq));
	oct->num_oqs--;

	return (0);
}

void
lio_droq_bh(void *ptr, int pending __unused)
{
	struct lio_droq		*droq = ptr;
	struct octeon_device	*oct = droq->oct_dev;
	struct lio_instr_queue	*iq = oct->instr_queue[droq->q_no];
	int	reschedule, tx_done = 1;

	reschedule = lio_droq_process_packets(oct, droq, oct->rx_budget);

	if (atomic_load_acq_int(&iq->instr_pending))
		tx_done = lio_flush_iq(oct, iq, oct->tx_budget);

	if (reschedule || !tx_done)
		taskqueue_enqueue(droq->droq_taskqueue, &droq->droq_task);
	else
		lio_enable_irq(droq, iq);
}

int
lio_init_droq(struct octeon_device *oct, uint32_t q_no,
	      uint32_t num_descs, uint32_t desc_size, void *app_ctx)
{
	struct lio_droq	*droq;
	unsigned long	size;
	uint32_t	c_buf_size = 0, c_num_descs = 0, c_pkts_per_intr = 0;
	uint32_t	c_refill_threshold = 0, desc_ring_size = 0;

	lio_dev_dbg(oct, "%s[%d]\n", __func__, q_no);

	droq = oct->droq[q_no];
	bzero(droq, LIO_DROQ_SIZE);

	droq->oct_dev = oct;
	droq->q_no = q_no;
	if (app_ctx != NULL)
		droq->app_ctx = app_ctx;
	else
		droq->app_ctx = (void *)(size_t)q_no;

	c_num_descs = num_descs;
	c_buf_size = desc_size;
	if (LIO_CN23XX_PF(oct)) {
		struct lio_config *conf23 = LIO_CHIP_CONF(oct, cn23xx_pf);

		c_pkts_per_intr =
			(uint32_t)LIO_GET_OQ_PKTS_PER_INTR_CFG(conf23);
		c_refill_threshold =
			(uint32_t)LIO_GET_OQ_REFILL_THRESHOLD_CFG(conf23);
	} else {
		return (1);
	}

	droq->max_count = c_num_descs;
	droq->buffer_size = c_buf_size;

	desc_ring_size = droq->max_count * LIO_DROQ_DESC_SIZE;
	droq->desc_ring = lio_dma_alloc(desc_ring_size, &droq->desc_ring_dma);
	if (droq->desc_ring == NULL) {
		lio_dev_err(oct, "Output queue %d ring alloc failed\n", q_no);
		return (1);
	}

	lio_dev_dbg(oct, "droq[%d]: desc_ring: virt: 0x%p, dma: %llx\n", q_no,
		    droq->desc_ring, LIO_CAST64(droq->desc_ring_dma));
	lio_dev_dbg(oct, "droq[%d]: num_desc: %d\n", q_no, droq->max_count);

	size = droq->max_count * LIO_DROQ_RECVBUF_SIZE;
	droq->recv_buf_list =
		(struct lio_recv_buffer *)malloc(size, M_DEVBUF,
						 M_NOWAIT | M_ZERO);
	if (droq->recv_buf_list == NULL) {
		lio_dev_err(oct, "Output queue recv buf list alloc failed\n");
		goto init_droq_fail;
	}

	if (lio_droq_setup_ring_buffers(oct, droq))
		goto init_droq_fail;

	droq->pkts_per_intr = c_pkts_per_intr;
	droq->refill_threshold = c_refill_threshold;

	lio_dev_dbg(oct, "DROQ INIT: max_empty_descs: %d\n",
		    droq->max_empty_descs);

	mtx_init(&droq->lock, "droq_lock", NULL, MTX_DEF);

	STAILQ_INIT(&droq->dispatch_stq_head);

	oct->fn_list.setup_oq_regs(oct, q_no);

	oct->io_qmask.oq |= BIT_ULL(q_no);

	/*
	 * Initialize the taskqueue that handles
	 * output queue packet processing.
	 */
	lio_dev_dbg(oct, "Initializing droq%d taskqueue\n", q_no);
	TASK_INIT(&droq->droq_task, 0, lio_droq_bh, (void *)droq);

	droq->droq_taskqueue = taskqueue_create_fast("lio_droq_task", M_NOWAIT,
						     taskqueue_thread_enqueue,
						     &droq->droq_taskqueue);
	taskqueue_start_threads_cpuset(&droq->droq_taskqueue, 1, PI_NET,
				       &oct->ioq_vector[q_no].affinity_mask,
				       "lio%d_droq%d_task", oct->octeon_id,
				       q_no);

	return (0);

init_droq_fail:
	lio_delete_droq(oct, q_no);
	return (1);
}

/*
 * lio_create_recv_info
 * Parameters:
 *  octeon_dev - pointer to the octeon device structure
 *  droq       - droq in which the packet arrived.
 *  buf_cnt    - no. of buffers used by the packet.
 *  idx        - index in the descriptor for the first buffer in the packet.
 * Description:
 *  Allocates a recv_info_t and copies the buffer addresses for packet data
 *  into the recv_pkt space which starts at an 8B offset from recv_info_t.
 *  Flags the descriptors for refill later. If available descriptors go
 *  below the threshold to receive a 64K pkt, new buffers are first allocated
 *  before the recv_pkt_t is created.
 *  This routine will be called in interrupt context.
 * Returns:
 *  Success: Pointer to recv_info_t
 *  Failure: NULL.
 * Locks:
 *  The droq->lock is held when this routine is called.
 */
static inline struct lio_recv_info *
lio_create_recv_info(struct octeon_device *octeon_dev, struct lio_droq *droq,
		     uint32_t buf_cnt, uint32_t idx)
{
	struct lio_droq_info	*info;
	struct lio_recv_pkt	*recv_pkt;
	struct lio_recv_info	*recv_info;
	uint32_t		bytes_left, i;

	info = (struct lio_droq_info *)droq->recv_buf_list[idx].data;

	recv_info = lio_alloc_recv_info(sizeof(struct __dispatch));
	if (recv_info == NULL)
		return (NULL);

	recv_pkt = recv_info->recv_pkt;
	recv_pkt->rh = info->rh;
	recv_pkt->length = (uint32_t)info->length;
	recv_pkt->buffer_count = (uint16_t)buf_cnt;
	recv_pkt->octeon_id = (uint16_t)octeon_dev->octeon_id;

	i = 0;
	bytes_left = (uint32_t)info->length;

	while (buf_cnt) {
		recv_pkt->buffer_size[i] = (bytes_left >= droq->buffer_size) ?
			droq->buffer_size : bytes_left;

		recv_pkt->buffer_ptr[i] = droq->recv_buf_list[idx].buffer;
		droq->recv_buf_list[idx].buffer = NULL;

		idx = lio_incr_index(idx, 1, droq->max_count);
		bytes_left -= droq->buffer_size;
		i++;
		buf_cnt--;
	}

	return (recv_info);
}

/*
 * If we were not able to refill all buffers, try to move around
 * the buffers that were not dispatched.
 */
static inline uint32_t
lio_droq_refill_pullup_descs(struct lio_droq *droq,
			     struct lio_droq_desc *desc_ring)
{
	uint32_t	desc_refilled = 0;
	uint32_t	refill_index = droq->refill_idx;

	while (refill_index != droq->read_idx) {
		if (droq->recv_buf_list[refill_index].buffer != NULL) {
			droq->recv_buf_list[droq->refill_idx].buffer =
				droq->recv_buf_list[refill_index].buffer;
			droq->recv_buf_list[droq->refill_idx].data =
				droq->recv_buf_list[refill_index].data;
			desc_ring[droq->refill_idx].buffer_ptr =
				desc_ring[refill_index].buffer_ptr;
			droq->recv_buf_list[refill_index].buffer = NULL;
			desc_ring[refill_index].buffer_ptr = 0;
			do {
				droq->refill_idx =
					lio_incr_index(droq->refill_idx, 1,
						       droq->max_count);
				desc_refilled++;
				droq->refill_count--;
			} while (droq->recv_buf_list[droq->refill_idx].buffer !=
				 NULL);
		}
		refill_index = lio_incr_index(refill_index, 1, droq->max_count);
	}	/* while */
	return (desc_refilled);
}

/*
 * lio_droq_refill
 * Parameters:
 *  droq       - droq in which descriptors require new buffers.
 * Description:
 *  Called during normal DROQ processing in interrupt mode or by the poll
 *  thread to refill the descriptors from which buffers were dispatched
 *  to upper layers. Attempts to allocate new buffers. If that fails, moves
 *  up buffers (that were not dispatched) to form a contiguous ring.
 * Returns:
 *  No of descriptors refilled.
 * Locks:
 *  This routine is called with droq->lock held.
 */
uint32_t
lio_droq_refill(struct octeon_device *octeon_dev, struct lio_droq *droq)
{
	struct lio_droq_desc	*desc_ring;
	void			*buf = NULL;
	uint32_t		desc_refilled = 0;
	uint8_t			*data;

	desc_ring = droq->desc_ring;

	while (droq->refill_count && (desc_refilled < droq->max_count)) {
		/*
		 * If a valid buffer exists (happens if there is no dispatch),
		 * reuse
		 * the buffer, else allocate.
		 */
		if (droq->recv_buf_list[droq->refill_idx].buffer == NULL) {
			buf = lio_recv_buffer_alloc(droq->buffer_size);
			/*
			 * If a buffer could not be allocated, no point in
			 * continuing
			 */
			if (buf == NULL) {
				droq->stats.rx_alloc_failure++;
				break;
			}

			droq->recv_buf_list[droq->refill_idx].buffer = buf;
			data = ((struct mbuf *)buf)->m_data;
		} else {
			data = ((struct mbuf *)droq->recv_buf_list
				[droq->refill_idx].buffer)->m_data;
		}

		droq->recv_buf_list[droq->refill_idx].data = data;

		desc_ring[droq->refill_idx].buffer_ptr =
		    lio_map_ring(octeon_dev->device,
				 droq->recv_buf_list[droq->refill_idx].buffer,
				 droq->buffer_size);

		droq->refill_idx = lio_incr_index(droq->refill_idx, 1,
						  droq->max_count);
		desc_refilled++;
		droq->refill_count--;
	}

	if (droq->refill_count)
		desc_refilled += lio_droq_refill_pullup_descs(droq, desc_ring);

	/*
	 * if droq->refill_count
	 * The refill count would not change in pass two. We only moved buffers
	 * to close the gap in the ring, but we would still have the same no. of
	 * buffers to refill.
	 */
	return (desc_refilled);
}

static inline uint32_t
lio_droq_get_bufcount(uint32_t buf_size, uint32_t total_len)
{

	return ((total_len + buf_size - 1) / buf_size);
}

static int
lio_droq_dispatch_pkt(struct octeon_device *oct, struct lio_droq *droq,
		      union octeon_rh *rh, struct lio_droq_info *info)
{
	struct lio_recv_info	*rinfo;
	lio_dispatch_fn_t	disp_fn;
	uint32_t		cnt;

	cnt = lio_droq_get_bufcount(droq->buffer_size, (uint32_t)info->length);

	disp_fn = lio_get_dispatch(oct, (uint16_t)rh->r.opcode,
				   (uint16_t)rh->r.subcode);
	if (disp_fn) {
		rinfo = lio_create_recv_info(oct, droq, cnt, droq->read_idx);
		if (rinfo != NULL) {
			struct __dispatch *rdisp = rinfo->rsvd;

			rdisp->rinfo = rinfo;
			rdisp->disp_fn = disp_fn;
			rinfo->recv_pkt->rh = *rh;
			STAILQ_INSERT_TAIL(&droq->dispatch_stq_head,
					   &rdisp->node, entries);
		} else {
			droq->stats.dropped_nomem++;
		}
	} else {
		lio_dev_err(oct, "DROQ: No dispatch function (opcode %u/%u)\n",
			    (unsigned int)rh->r.opcode,
			    (unsigned int)rh->r.subcode);
		droq->stats.dropped_nodispatch++;
	}

	return (cnt);
}

static inline void
lio_droq_drop_packets(struct octeon_device *oct, struct lio_droq *droq,
		      uint32_t cnt)
{
	struct lio_droq_info	*info;
	uint32_t		i = 0, buf_cnt;

	for (i = 0; i < cnt; i++) {
		info = (struct lio_droq_info *)
			droq->recv_buf_list[droq->read_idx].data;

		lio_swap_8B_data((uint64_t *)info, 2);

		if (info->length) {
			info->length += 8;
			droq->stats.bytes_received += info->length;
			buf_cnt = lio_droq_get_bufcount(droq->buffer_size,
							(uint32_t)info->length);
		} else {
			lio_dev_err(oct, "DROQ: In drop: pkt with len 0\n");
			buf_cnt = 1;
		}

		droq->read_idx = lio_incr_index(droq->read_idx, buf_cnt,
						droq->max_count);
		droq->refill_count += buf_cnt;
	}
}

static uint32_t
lio_droq_fast_process_packets(struct octeon_device *oct, struct lio_droq *droq,
			      uint32_t pkts_to_process)
{
	struct lio_droq_info	*info;
	union			octeon_rh *rh;
	uint32_t		pkt, pkt_count, total_len = 0;

	pkt_count = pkts_to_process;

	for (pkt = 0; pkt < pkt_count; pkt++) {
		struct mbuf	*nicbuf = NULL;
		uint32_t	pkt_len = 0;

		info = (struct lio_droq_info *)
		    droq->recv_buf_list[droq->read_idx].data;

		lio_swap_8B_data((uint64_t *)info, 2);

		if (!info->length) {
			lio_dev_err(oct,
				    "DROQ[%d] idx: %d len:0, pkt_cnt: %d\n",
				    droq->q_no, droq->read_idx, pkt_count);
			hexdump((uint8_t *)info, LIO_DROQ_INFO_SIZE, NULL,
				HD_OMIT_CHARS);
			pkt++;
			lio_incr_index(droq->read_idx, 1, droq->max_count);
			droq->refill_count++;
			break;
		}

		rh = &info->rh;

		info->length += 8;
		rh->r_dh.len += (LIO_DROQ_INFO_SIZE + 7) / 8;

		total_len += (uint32_t)info->length;
		if (lio_opcode_slow_path(rh)) {
			uint32_t	buf_cnt;

			buf_cnt = lio_droq_dispatch_pkt(oct, droq, rh, info);
			droq->read_idx = lio_incr_index(droq->read_idx,	buf_cnt,
							droq->max_count);
			droq->refill_count += buf_cnt;
		} else {
			if (info->length <= droq->buffer_size) {
				pkt_len = (uint32_t)info->length;
				nicbuf = droq->recv_buf_list[
						       droq->read_idx].buffer;
				nicbuf->m_len = pkt_len;
				droq->recv_buf_list[droq->read_idx].buffer =
					NULL;

				droq->read_idx =
					lio_incr_index(droq->read_idx,
						       1, droq->max_count);
				droq->refill_count++;
			} else {
				bool	secondary_frag = false;

				pkt_len = 0;

				while (pkt_len < info->length) {
					int	frag_len, idx = droq->read_idx;
					struct mbuf	*buffer;

					frag_len =
						((pkt_len + droq->buffer_size) >
						 info->length) ?
						((uint32_t)info->length -
						 pkt_len) : droq->buffer_size;

					buffer = ((struct mbuf *)
						  droq->recv_buf_list[idx].
						  buffer);
					buffer->m_len = frag_len;
					if (__predict_true(secondary_frag)) {
						m_cat(nicbuf, buffer);
					} else {
						nicbuf = buffer;
						secondary_frag = true;
					}

					droq->recv_buf_list[droq->read_idx].
						buffer = NULL;

					pkt_len += frag_len;
					droq->read_idx =
						lio_incr_index(droq->read_idx,
							       1,
							       droq->max_count);
					droq->refill_count++;
				}
			}

			if (nicbuf != NULL) {
				if (droq->ops.fptr != NULL) {
					droq->ops.fptr(nicbuf, pkt_len, rh,
						       droq, droq->ops.farg);
				} else {
					lio_recv_buffer_free(nicbuf);
				}
			}
		}

		if (droq->refill_count >= droq->refill_threshold) {
			int desc_refilled = lio_droq_refill(oct, droq);

			/*
			 * Flush the droq descriptor data to memory to be sure
			 * that when we update the credits the data in memory
			 * is accurate.
			 */
			wmb();
			lio_write_csr32(oct, droq->pkts_credit_reg,
					desc_refilled);
			/* make sure mmio write completes */
			__compiler_membar();
		}
	}	/* for (each packet)... */

	/* Increment refill_count by the number of buffers processed. */
	droq->stats.pkts_received += pkt;
	droq->stats.bytes_received += total_len;

	tcp_lro_flush_all(&droq->lro);

	if ((droq->ops.drop_on_max) && (pkts_to_process - pkt)) {
		lio_droq_drop_packets(oct, droq, (pkts_to_process - pkt));

		droq->stats.dropped_toomany += (pkts_to_process - pkt);
		return (pkts_to_process);
	}

	return (pkt);
}

int
lio_droq_process_packets(struct octeon_device *oct, struct lio_droq *droq,
			 uint32_t budget)
{
	struct lio_stailq_node	*tmp, *tmp2;
	uint32_t		pkt_count = 0, pkts_processed = 0;

	/* Grab the droq lock */
	mtx_lock(&droq->lock);

	lio_droq_check_hw_for_pkts(droq);
	pkt_count = atomic_load_acq_int(&droq->pkts_pending);

	if (!pkt_count) {
		mtx_unlock(&droq->lock);
		return (0);
	}
	if (pkt_count > budget)
		pkt_count = budget;

	pkts_processed = lio_droq_fast_process_packets(oct, droq, pkt_count);

	atomic_subtract_int(&droq->pkts_pending, pkts_processed);

	/* Release the lock */
	mtx_unlock(&droq->lock);

	STAILQ_FOREACH_SAFE(tmp, &droq->dispatch_stq_head, entries, tmp2) {
		struct __dispatch *rdisp = (struct __dispatch *)tmp;

		STAILQ_REMOVE_HEAD(&droq->dispatch_stq_head, entries);
		rdisp->disp_fn(rdisp->rinfo, lio_get_dispatch_arg(oct,
			(uint16_t)rdisp->rinfo->recv_pkt->rh.r.opcode,
			(uint16_t)rdisp->rinfo->recv_pkt->rh.r.subcode));
	}

	/* If there are packets pending. schedule tasklet again */
	if (atomic_load_acq_int(&droq->pkts_pending))
		return (1);

	return (0);
}

int
lio_register_droq_ops(struct octeon_device *oct, uint32_t q_no,
		      struct lio_droq_ops *ops)
{
	struct lio_droq		*droq;
	struct lio_config	*lio_cfg = NULL;

	lio_cfg = lio_get_conf(oct);

	if (lio_cfg == NULL)
		return (-EINVAL);

	if (ops == NULL) {
		lio_dev_err(oct, "%s: droq_ops pointer is NULL\n", __func__);
		return (-EINVAL);
	}

	if (q_no >= LIO_GET_OQ_MAX_Q_CFG(lio_cfg)) {
		lio_dev_err(oct, "%s: droq id (%d) exceeds MAX (%d)\n",
			    __func__, q_no, (oct->num_oqs - 1));
		return (-EINVAL);
	}
	droq = oct->droq[q_no];

	mtx_lock(&droq->lock);

	memcpy(&droq->ops, ops, sizeof(struct lio_droq_ops));

	mtx_unlock(&droq->lock);

	return (0);
}

int
lio_unregister_droq_ops(struct octeon_device *oct, uint32_t q_no)
{
	struct lio_droq		*droq;
	struct lio_config	*lio_cfg = NULL;

	lio_cfg = lio_get_conf(oct);

	if (lio_cfg == NULL)
		return (-EINVAL);

	if (q_no >= LIO_GET_OQ_MAX_Q_CFG(lio_cfg)) {
		lio_dev_err(oct, "%s: droq id (%d) exceeds MAX (%d)\n",
			    __func__, q_no, oct->num_oqs - 1);
		return (-EINVAL);
	}

	droq = oct->droq[q_no];

	if (droq == NULL) {
		lio_dev_info(oct, "Droq id (%d) not available.\n", q_no);
		return (0);
	}

	mtx_lock(&droq->lock);

	droq->ops.fptr = NULL;
	droq->ops.farg = NULL;
	droq->ops.drop_on_max = 0;

	mtx_unlock(&droq->lock);

	return (0);
}

int
lio_create_droq(struct octeon_device *oct, uint32_t q_no, uint32_t num_descs,
		uint32_t desc_size, void *app_ctx)
{

	if (oct->droq[q_no]->oct_dev != NULL) {
		lio_dev_dbg(oct, "Droq already in use. Cannot create droq %d again\n",
			    q_no);
		return (1);
	}

	/* Initialize the Droq */
	if (lio_init_droq(oct, q_no, num_descs, desc_size, app_ctx)) {
		bzero(oct->droq[q_no], sizeof(struct lio_droq));
		goto create_droq_fail;
	}

	oct->num_oqs++;

	lio_dev_dbg(oct, "%s: Total number of OQ: %d\n", __func__,
		    oct->num_oqs);

	/* Global Droq register settings */

	/*
	 * As of now not required, as setting are done for all 32 Droqs at
	 * the same time.
	 */
	return (0);

create_droq_fail:
	return (-ENOMEM);
}
