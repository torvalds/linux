/*-
 * BSD LICENSE
 *
 * Copyright (c) 2015-2017 Amazon.com, Inc. or its affiliates.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 * * Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in
 * the documentation and/or other materials provided with the
 * distribution.
 * * Neither the name of copyright holder nor the names of its
 * contributors may be used to endorse or promote products derived
 * from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "ena_eth_com.h"

static inline struct ena_eth_io_rx_cdesc_base *ena_com_get_next_rx_cdesc(
	struct ena_com_io_cq *io_cq)
{
	struct ena_eth_io_rx_cdesc_base *cdesc;
	u16 expected_phase, head_masked;
	u16 desc_phase;

	head_masked = io_cq->head & (io_cq->q_depth - 1);
	expected_phase = io_cq->phase;

	cdesc = (struct ena_eth_io_rx_cdesc_base *)(io_cq->cdesc_addr.virt_addr
			+ (head_masked * io_cq->cdesc_entry_size_in_bytes));

	desc_phase = (READ_ONCE(cdesc->status) & ENA_ETH_IO_RX_CDESC_BASE_PHASE_MASK) >>
			ENA_ETH_IO_RX_CDESC_BASE_PHASE_SHIFT;

	if (desc_phase != expected_phase)
		return NULL;

	return cdesc;
}

static inline void ena_com_cq_inc_head(struct ena_com_io_cq *io_cq)
{
	io_cq->head++;

	/* Switch phase bit in case of wrap around */
	if (unlikely((io_cq->head & (io_cq->q_depth - 1)) == 0))
		io_cq->phase ^= 1;
}

static inline void *get_sq_desc_regular_queue(struct ena_com_io_sq *io_sq)
{
	u16 tail_masked;
	u32 offset;

	tail_masked = io_sq->tail & (io_sq->q_depth - 1);

	offset = tail_masked * io_sq->desc_entry_size;

	return (void *)((uintptr_t)io_sq->desc_addr.virt_addr + offset);
}

static inline void ena_com_write_bounce_buffer_to_dev(struct ena_com_io_sq *io_sq,
						      u8 *bounce_buffer)
{
	struct ena_com_llq_info *llq_info = &io_sq->llq_info;

	u16 dst_tail_mask;
	u32 dst_offset;

	dst_tail_mask = io_sq->tail & (io_sq->q_depth - 1);
	dst_offset = dst_tail_mask * llq_info->desc_list_entry_size;

	/* Make sure everything was written into the bounce buffer before
	 * writing the bounce buffer to the device
	 */
	wmb();

	/* The line is completed. Copy it to dev */
	ENA_MEMCPY_TO_DEVICE_64(io_sq->desc_addr.pbuf_dev_addr + dst_offset,
				bounce_buffer,
				llq_info->desc_list_entry_size);

	io_sq->tail++;

	/* Switch phase bit in case of wrap around */
	if (unlikely((io_sq->tail & (io_sq->q_depth - 1)) == 0))
		io_sq->phase ^= 1;
}

static inline int ena_com_write_header_to_bounce(struct ena_com_io_sq *io_sq,
						 u8 *header_src,
						 u16 header_len)
{
	struct ena_com_llq_pkt_ctrl *pkt_ctrl = &io_sq->llq_buf_ctrl;
	struct ena_com_llq_info *llq_info = &io_sq->llq_info;
	u8 *bounce_buffer = pkt_ctrl->curr_bounce_buf;
	u16 header_offset;

	if (io_sq->mem_queue_type == ENA_ADMIN_PLACEMENT_POLICY_HOST)
		return 0;

	header_offset =
		llq_info->descs_num_before_header * io_sq->desc_entry_size;

	if (unlikely((header_offset + header_len) >  llq_info->desc_list_entry_size)) {
		ena_trc_err("trying to write header larger than llq entry can accommodate\n");
		return ENA_COM_FAULT;
	}

	if (unlikely(!bounce_buffer)) {
		ena_trc_err("bounce buffer is NULL\n");
		return ENA_COM_FAULT;
	}

	memcpy(bounce_buffer + header_offset, header_src, header_len);

	return 0;
}

static inline void *get_sq_desc_llq(struct ena_com_io_sq *io_sq)
{
	struct ena_com_llq_pkt_ctrl *pkt_ctrl = &io_sq->llq_buf_ctrl;
	u8 *bounce_buffer;
	void *sq_desc;

	bounce_buffer = pkt_ctrl->curr_bounce_buf;

	if (unlikely(!bounce_buffer)) {
		ena_trc_err("bounce buffer is NULL\n");
		return NULL;
	}

	sq_desc = bounce_buffer + pkt_ctrl->idx * io_sq->desc_entry_size;
	pkt_ctrl->idx++;
	pkt_ctrl->descs_left_in_line--;

	return sq_desc;
}

static inline void ena_com_close_bounce_buffer(struct ena_com_io_sq *io_sq)
{
	struct ena_com_llq_pkt_ctrl *pkt_ctrl = &io_sq->llq_buf_ctrl;
	struct ena_com_llq_info *llq_info = &io_sq->llq_info;

	if (io_sq->mem_queue_type == ENA_ADMIN_PLACEMENT_POLICY_HOST)
		return;

	/* bounce buffer was used, so write it and get a new one */
	if (pkt_ctrl->idx) {
		ena_com_write_bounce_buffer_to_dev(io_sq,
						   pkt_ctrl->curr_bounce_buf);
		pkt_ctrl->curr_bounce_buf =
			ena_com_get_next_bounce_buffer(&io_sq->bounce_buf_ctrl);
			memset(io_sq->llq_buf_ctrl.curr_bounce_buf,
			       0x0, llq_info->desc_list_entry_size);
	}

	pkt_ctrl->idx = 0;
	pkt_ctrl->descs_left_in_line = llq_info->descs_num_before_header;
}

static inline void *get_sq_desc(struct ena_com_io_sq *io_sq)
{
	if (io_sq->mem_queue_type == ENA_ADMIN_PLACEMENT_POLICY_DEV)
		return get_sq_desc_llq(io_sq);

	return get_sq_desc_regular_queue(io_sq);
}

static inline void ena_com_sq_update_llq_tail(struct ena_com_io_sq *io_sq)
{
	struct ena_com_llq_pkt_ctrl *pkt_ctrl = &io_sq->llq_buf_ctrl;
	struct ena_com_llq_info *llq_info = &io_sq->llq_info;

	if (!pkt_ctrl->descs_left_in_line) {
		ena_com_write_bounce_buffer_to_dev(io_sq,
						   pkt_ctrl->curr_bounce_buf);

		pkt_ctrl->curr_bounce_buf =
			ena_com_get_next_bounce_buffer(&io_sq->bounce_buf_ctrl);
			memset(io_sq->llq_buf_ctrl.curr_bounce_buf,
			       0x0, llq_info->desc_list_entry_size);

		pkt_ctrl->idx = 0;
		if (llq_info->desc_stride_ctrl == ENA_ADMIN_SINGLE_DESC_PER_ENTRY)
			pkt_ctrl->descs_left_in_line = 1;
		else
			pkt_ctrl->descs_left_in_line =
			llq_info->desc_list_entry_size / io_sq->desc_entry_size;
	}
}

static inline void ena_com_sq_update_tail(struct ena_com_io_sq *io_sq)
{

	if (io_sq->mem_queue_type == ENA_ADMIN_PLACEMENT_POLICY_DEV) {
		ena_com_sq_update_llq_tail(io_sq);
		return;
	}

	io_sq->tail++;

	/* Switch phase bit in case of wrap around */
	if (unlikely((io_sq->tail & (io_sq->q_depth - 1)) == 0))
		io_sq->phase ^= 1;
}

static inline struct ena_eth_io_rx_cdesc_base *
	ena_com_rx_cdesc_idx_to_ptr(struct ena_com_io_cq *io_cq, u16 idx)
{
	idx &= (io_cq->q_depth - 1);
	return (struct ena_eth_io_rx_cdesc_base *)
		((uintptr_t)io_cq->cdesc_addr.virt_addr +
		idx * io_cq->cdesc_entry_size_in_bytes);
}

static inline u16 ena_com_cdesc_rx_pkt_get(struct ena_com_io_cq *io_cq,
					   u16 *first_cdesc_idx)
{
	struct ena_eth_io_rx_cdesc_base *cdesc;
	u16 count = 0, head_masked;
	u32 last = 0;

	do {
		cdesc = ena_com_get_next_rx_cdesc(io_cq);
		if (!cdesc)
			break;

		ena_com_cq_inc_head(io_cq);
		count++;
		last = (READ_ONCE(cdesc->status) & ENA_ETH_IO_RX_CDESC_BASE_LAST_MASK) >>
			ENA_ETH_IO_RX_CDESC_BASE_LAST_SHIFT;
	} while (!last);

	if (last) {
		*first_cdesc_idx = io_cq->cur_rx_pkt_cdesc_start_idx;
		count += io_cq->cur_rx_pkt_cdesc_count;

		head_masked = io_cq->head & (io_cq->q_depth - 1);

		io_cq->cur_rx_pkt_cdesc_count = 0;
		io_cq->cur_rx_pkt_cdesc_start_idx = head_masked;

		ena_trc_dbg("ena q_id: %d packets were completed. first desc idx %u descs# %d\n",
			    io_cq->qid, *first_cdesc_idx, count);
	} else {
		io_cq->cur_rx_pkt_cdesc_count += count;
		count = 0;
	}

	return count;
}

static inline bool ena_com_meta_desc_changed(struct ena_com_io_sq *io_sq,
					     struct ena_com_tx_ctx *ena_tx_ctx)
{
	int rc;

	if (ena_tx_ctx->meta_valid) {
		rc = memcmp(&io_sq->cached_tx_meta,
			    &ena_tx_ctx->ena_meta,
			    sizeof(struct ena_com_tx_meta));

		if (unlikely(rc != 0))
			return true;
	}

	return false;
}

static inline void ena_com_create_and_store_tx_meta_desc(struct ena_com_io_sq *io_sq,
							 struct ena_com_tx_ctx *ena_tx_ctx)
{
	struct ena_eth_io_tx_meta_desc *meta_desc = NULL;
	struct ena_com_tx_meta *ena_meta = &ena_tx_ctx->ena_meta;

	meta_desc = get_sq_desc(io_sq);
	memset(meta_desc, 0x0, sizeof(struct ena_eth_io_tx_meta_desc));

	meta_desc->len_ctrl |= ENA_ETH_IO_TX_META_DESC_META_DESC_MASK;

	meta_desc->len_ctrl |= ENA_ETH_IO_TX_META_DESC_EXT_VALID_MASK;

	/* bits 0-9 of the mss */
	meta_desc->word2 |= (ena_meta->mss <<
		ENA_ETH_IO_TX_META_DESC_MSS_LO_SHIFT) &
		ENA_ETH_IO_TX_META_DESC_MSS_LO_MASK;
	/* bits 10-13 of the mss */
	meta_desc->len_ctrl |= ((ena_meta->mss >> 10) <<
		ENA_ETH_IO_TX_META_DESC_MSS_HI_SHIFT) &
		ENA_ETH_IO_TX_META_DESC_MSS_HI_MASK;

	/* Extended meta desc */
	meta_desc->len_ctrl |= ENA_ETH_IO_TX_META_DESC_ETH_META_TYPE_MASK;
	meta_desc->len_ctrl |= ENA_ETH_IO_TX_META_DESC_META_STORE_MASK;
	meta_desc->len_ctrl |= (io_sq->phase <<
		ENA_ETH_IO_TX_META_DESC_PHASE_SHIFT) &
		ENA_ETH_IO_TX_META_DESC_PHASE_MASK;

	meta_desc->len_ctrl |= ENA_ETH_IO_TX_META_DESC_FIRST_MASK;
	meta_desc->word2 |= ena_meta->l3_hdr_len &
		ENA_ETH_IO_TX_META_DESC_L3_HDR_LEN_MASK;
	meta_desc->word2 |= (ena_meta->l3_hdr_offset <<
		ENA_ETH_IO_TX_META_DESC_L3_HDR_OFF_SHIFT) &
		ENA_ETH_IO_TX_META_DESC_L3_HDR_OFF_MASK;

	meta_desc->word2 |= (ena_meta->l4_hdr_len <<
		ENA_ETH_IO_TX_META_DESC_L4_HDR_LEN_IN_WORDS_SHIFT) &
		ENA_ETH_IO_TX_META_DESC_L4_HDR_LEN_IN_WORDS_MASK;

	meta_desc->len_ctrl |= ENA_ETH_IO_TX_META_DESC_META_STORE_MASK;

	/* Cached the meta desc */
	memcpy(&io_sq->cached_tx_meta, ena_meta,
	       sizeof(struct ena_com_tx_meta));

	ena_com_sq_update_tail(io_sq);
}

static inline void ena_com_rx_set_flags(struct ena_com_rx_ctx *ena_rx_ctx,
					struct ena_eth_io_rx_cdesc_base *cdesc)
{
	ena_rx_ctx->l3_proto = cdesc->status &
		ENA_ETH_IO_RX_CDESC_BASE_L3_PROTO_IDX_MASK;
	ena_rx_ctx->l4_proto =
		(cdesc->status & ENA_ETH_IO_RX_CDESC_BASE_L4_PROTO_IDX_MASK) >>
		ENA_ETH_IO_RX_CDESC_BASE_L4_PROTO_IDX_SHIFT;
	ena_rx_ctx->l3_csum_err =
		(cdesc->status & ENA_ETH_IO_RX_CDESC_BASE_L3_CSUM_ERR_MASK) >>
		ENA_ETH_IO_RX_CDESC_BASE_L3_CSUM_ERR_SHIFT;
	ena_rx_ctx->l4_csum_err =
		(cdesc->status & ENA_ETH_IO_RX_CDESC_BASE_L4_CSUM_ERR_MASK) >>
		ENA_ETH_IO_RX_CDESC_BASE_L4_CSUM_ERR_SHIFT;
	ena_rx_ctx->hash = cdesc->hash;
	ena_rx_ctx->frag =
		(cdesc->status & ENA_ETH_IO_RX_CDESC_BASE_IPV4_FRAG_MASK) >>
		ENA_ETH_IO_RX_CDESC_BASE_IPV4_FRAG_SHIFT;

	ena_trc_dbg("ena_rx_ctx->l3_proto %d ena_rx_ctx->l4_proto %d\nena_rx_ctx->l3_csum_err %d ena_rx_ctx->l4_csum_err %d\nhash frag %d frag: %d cdesc_status: %x\n",
		    ena_rx_ctx->l3_proto,
		    ena_rx_ctx->l4_proto,
		    ena_rx_ctx->l3_csum_err,
		    ena_rx_ctx->l4_csum_err,
		    ena_rx_ctx->hash,
		    ena_rx_ctx->frag,
		    cdesc->status);
}

/*****************************************************************************/
/*****************************     API      **********************************/
/*****************************************************************************/

int ena_com_prepare_tx(struct ena_com_io_sq *io_sq,
		       struct ena_com_tx_ctx *ena_tx_ctx,
		       int *nb_hw_desc)
{
	struct ena_eth_io_tx_desc *desc = NULL;
	struct ena_com_buf *ena_bufs = ena_tx_ctx->ena_bufs;
	void *buffer_to_push = ena_tx_ctx->push_header;
	u16 header_len = ena_tx_ctx->header_len;
	u16 num_bufs = ena_tx_ctx->num_bufs;
	u16 start_tail = io_sq->tail;
	int i, rc;
	bool have_meta;
	u64 addr_hi;

	ENA_WARN(io_sq->direction != ENA_COM_IO_QUEUE_DIRECTION_TX,
		 "wrong Q type");

	/* num_bufs +1 for potential meta desc */
	if (!ena_com_sq_have_enough_space(io_sq, num_bufs + 1)) {
		ena_trc_dbg("Not enough space in the tx queue\n");
		return ENA_COM_NO_MEM;
	}

	if (unlikely(header_len > io_sq->tx_max_header_size)) {
		ena_trc_err("header size is too large %d max header: %d\n",
			    header_len, io_sq->tx_max_header_size);
		return ENA_COM_INVAL;
	}

	if (unlikely((io_sq->mem_queue_type == ENA_ADMIN_PLACEMENT_POLICY_DEV) && !buffer_to_push))
		return ENA_COM_INVAL;

	rc = ena_com_write_header_to_bounce(io_sq, buffer_to_push, header_len);
	if (unlikely(rc))
		return rc;

	have_meta = ena_tx_ctx->meta_valid && ena_com_meta_desc_changed(io_sq,
			ena_tx_ctx);
	if (have_meta)
		ena_com_create_and_store_tx_meta_desc(io_sq, ena_tx_ctx);

	/* If the caller doesn't want send packets */
	if (unlikely(!num_bufs && !header_len)) {
		ena_com_close_bounce_buffer(io_sq);
		*nb_hw_desc = io_sq->tail - start_tail;
		return 0;
	}

	desc = get_sq_desc(io_sq);
	if (unlikely(!desc))
		return ENA_COM_FAULT;
	memset(desc, 0x0, sizeof(struct ena_eth_io_tx_desc));

	/* Set first desc when we don't have meta descriptor */
	if (!have_meta)
		desc->len_ctrl |= ENA_ETH_IO_TX_DESC_FIRST_MASK;

	desc->buff_addr_hi_hdr_sz |= (header_len <<
		ENA_ETH_IO_TX_DESC_HEADER_LENGTH_SHIFT) &
		ENA_ETH_IO_TX_DESC_HEADER_LENGTH_MASK;
	desc->len_ctrl |= (io_sq->phase << ENA_ETH_IO_TX_DESC_PHASE_SHIFT) &
		ENA_ETH_IO_TX_DESC_PHASE_MASK;

	desc->len_ctrl |= ENA_ETH_IO_TX_DESC_COMP_REQ_MASK;

	/* Bits 0-9 */
	desc->meta_ctrl |= (ena_tx_ctx->req_id <<
		ENA_ETH_IO_TX_DESC_REQ_ID_LO_SHIFT) &
		ENA_ETH_IO_TX_DESC_REQ_ID_LO_MASK;

	desc->meta_ctrl |= (ena_tx_ctx->df <<
		ENA_ETH_IO_TX_DESC_DF_SHIFT) &
		ENA_ETH_IO_TX_DESC_DF_MASK;

	/* Bits 10-15 */
	desc->len_ctrl |= ((ena_tx_ctx->req_id >> 10) <<
		ENA_ETH_IO_TX_DESC_REQ_ID_HI_SHIFT) &
		ENA_ETH_IO_TX_DESC_REQ_ID_HI_MASK;

	if (ena_tx_ctx->meta_valid) {
		desc->meta_ctrl |= (ena_tx_ctx->tso_enable <<
			ENA_ETH_IO_TX_DESC_TSO_EN_SHIFT) &
			ENA_ETH_IO_TX_DESC_TSO_EN_MASK;
		desc->meta_ctrl |= ena_tx_ctx->l3_proto &
			ENA_ETH_IO_TX_DESC_L3_PROTO_IDX_MASK;
		desc->meta_ctrl |= (ena_tx_ctx->l4_proto <<
			ENA_ETH_IO_TX_DESC_L4_PROTO_IDX_SHIFT) &
			ENA_ETH_IO_TX_DESC_L4_PROTO_IDX_MASK;
		desc->meta_ctrl |= (ena_tx_ctx->l3_csum_enable <<
			ENA_ETH_IO_TX_DESC_L3_CSUM_EN_SHIFT) &
			ENA_ETH_IO_TX_DESC_L3_CSUM_EN_MASK;
		desc->meta_ctrl |= (ena_tx_ctx->l4_csum_enable <<
			ENA_ETH_IO_TX_DESC_L4_CSUM_EN_SHIFT) &
			ENA_ETH_IO_TX_DESC_L4_CSUM_EN_MASK;
		desc->meta_ctrl |= (ena_tx_ctx->l4_csum_partial <<
			ENA_ETH_IO_TX_DESC_L4_CSUM_PARTIAL_SHIFT) &
			ENA_ETH_IO_TX_DESC_L4_CSUM_PARTIAL_MASK;
	}

	for (i = 0; i < num_bufs; i++) {
		/* The first desc share the same desc as the header */
		if (likely(i != 0)) {
			ena_com_sq_update_tail(io_sq);

			desc = get_sq_desc(io_sq);
			if (unlikely(!desc))
				return ENA_COM_FAULT;

			memset(desc, 0x0, sizeof(struct ena_eth_io_tx_desc));

			desc->len_ctrl |= (io_sq->phase <<
				ENA_ETH_IO_TX_DESC_PHASE_SHIFT) &
				ENA_ETH_IO_TX_DESC_PHASE_MASK;
		}

		desc->len_ctrl |= ena_bufs->len &
			ENA_ETH_IO_TX_DESC_LENGTH_MASK;

		addr_hi = ((ena_bufs->paddr &
			GENMASK_ULL(io_sq->dma_addr_bits - 1, 32)) >> 32);

		desc->buff_addr_lo = (u32)ena_bufs->paddr;
		desc->buff_addr_hi_hdr_sz |= addr_hi &
			ENA_ETH_IO_TX_DESC_ADDR_HI_MASK;
		ena_bufs++;
	}

	/* set the last desc indicator */
	desc->len_ctrl |= ENA_ETH_IO_TX_DESC_LAST_MASK;

	ena_com_sq_update_tail(io_sq);

	ena_com_close_bounce_buffer(io_sq);

	*nb_hw_desc = io_sq->tail - start_tail;
	return 0;
}

int ena_com_rx_pkt(struct ena_com_io_cq *io_cq,
		   struct ena_com_io_sq *io_sq,
		   struct ena_com_rx_ctx *ena_rx_ctx)
{
	struct ena_com_rx_buf_info *ena_buf = &ena_rx_ctx->ena_bufs[0];
	struct ena_eth_io_rx_cdesc_base *cdesc = NULL;
	u16 cdesc_idx = 0;
	u16 nb_hw_desc;
	u16 i;

	ENA_WARN(io_cq->direction != ENA_COM_IO_QUEUE_DIRECTION_RX,
		 "wrong Q type");

	nb_hw_desc = ena_com_cdesc_rx_pkt_get(io_cq, &cdesc_idx);
	if (nb_hw_desc == 0) {
		ena_rx_ctx->descs = nb_hw_desc;
		return 0;
	}

	ena_trc_dbg("fetch rx packet: queue %d completed desc: %d\n",
		    io_cq->qid, nb_hw_desc);

	if (unlikely(nb_hw_desc > ena_rx_ctx->max_bufs)) {
		ena_trc_err("Too many RX cdescs (%d) > MAX(%d)\n",
			    nb_hw_desc, ena_rx_ctx->max_bufs);
		return ENA_COM_NO_SPACE;
	}

	for (i = 0; i < nb_hw_desc; i++) {
		cdesc = ena_com_rx_cdesc_idx_to_ptr(io_cq, cdesc_idx + i);

		ena_buf->len = cdesc->length;
		ena_buf->req_id = cdesc->req_id;
		ena_buf++;
	}

	/* Update SQ head ptr */
	io_sq->next_to_comp += nb_hw_desc;

	ena_trc_dbg("[%s][QID#%d] Updating SQ head to: %d\n", __func__,
		    io_sq->qid, io_sq->next_to_comp);

	/* Get rx flags from the last pkt */
	ena_com_rx_set_flags(ena_rx_ctx, cdesc);

	ena_rx_ctx->descs = nb_hw_desc;
	return 0;
}

int ena_com_add_single_rx_desc(struct ena_com_io_sq *io_sq,
			       struct ena_com_buf *ena_buf,
			       u16 req_id)
{
	struct ena_eth_io_rx_desc *desc;

	ENA_WARN(io_sq->direction != ENA_COM_IO_QUEUE_DIRECTION_RX,
		 "wrong Q type");

	if (unlikely(!ena_com_sq_have_enough_space(io_sq, 1)))
		return ENA_COM_NO_SPACE;

	desc = get_sq_desc(io_sq);
	if (unlikely(!desc))
		return ENA_COM_FAULT;

	memset(desc, 0x0, sizeof(struct ena_eth_io_rx_desc));

	desc->length = ena_buf->len;

	desc->ctrl |= ENA_ETH_IO_RX_DESC_FIRST_MASK;
	desc->ctrl |= ENA_ETH_IO_RX_DESC_LAST_MASK;
	desc->ctrl |= io_sq->phase & ENA_ETH_IO_RX_DESC_PHASE_MASK;
	desc->ctrl |= ENA_ETH_IO_RX_DESC_COMP_REQ_MASK;

	desc->req_id = req_id;

	desc->buff_addr_lo = (u32)ena_buf->paddr;
	desc->buff_addr_hi =
		((ena_buf->paddr & GENMASK_ULL(io_sq->dma_addr_bits - 1, 32)) >> 32);

	ena_com_sq_update_tail(io_sq);

	return 0;
}

int ena_com_tx_comp_req_id_get(struct ena_com_io_cq *io_cq, u16 *req_id)
{
	u8 expected_phase, cdesc_phase;
	struct ena_eth_io_tx_cdesc *cdesc;
	u16 masked_head;

	masked_head = io_cq->head & (io_cq->q_depth - 1);
	expected_phase = io_cq->phase;

	cdesc = (struct ena_eth_io_tx_cdesc *)
		((uintptr_t)io_cq->cdesc_addr.virt_addr +
		(masked_head * io_cq->cdesc_entry_size_in_bytes));

	/* When the current completion descriptor phase isn't the same as the
	 * expected, it mean that the device still didn't update
	 * this completion.
	 */
	cdesc_phase = READ_ONCE(cdesc->flags) & ENA_ETH_IO_TX_CDESC_PHASE_MASK;
	if (cdesc_phase != expected_phase)
		return ENA_COM_TRY_AGAIN;

	if (unlikely(cdesc->req_id >= io_cq->q_depth)) {
		ena_trc_err("Invalid req id %d\n", cdesc->req_id);
		return ENA_COM_INVAL;
	}

	ena_com_cq_inc_head(io_cq);

	*req_id = READ_ONCE(cdesc->req_id);

	return 0;
}
