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

#ifndef ENA_ETH_COM_H_
#define ENA_ETH_COM_H_

#if defined(__cplusplus)
extern "C" {
#endif
#include "ena_com.h"

/* head update threshold in units of (queue size / ENA_COMP_HEAD_THRESH) */
#define ENA_COMP_HEAD_THRESH 4

struct ena_com_tx_ctx {
	struct ena_com_tx_meta ena_meta;
	struct ena_com_buf *ena_bufs;
	/* For LLQ, header buffer - pushed to the device mem space */
	void *push_header;

	enum ena_eth_io_l3_proto_index l3_proto;
	enum ena_eth_io_l4_proto_index l4_proto;
	u16 num_bufs;
	u16 req_id;
	/* For regular queue, indicate the size of the header
	 * For LLQ, indicate the size of the pushed buffer
	 */
	u16 header_len;

	u8 meta_valid;
	u8 tso_enable;
	u8 l3_csum_enable;
	u8 l4_csum_enable;
	u8 l4_csum_partial;
	u8 df; /* Don't fragment */
};

struct ena_com_rx_ctx {
	struct ena_com_rx_buf_info *ena_bufs;
	enum ena_eth_io_l3_proto_index l3_proto;
	enum ena_eth_io_l4_proto_index l4_proto;
	bool l3_csum_err;
	bool l4_csum_err;
	/* fragmented packet */
	bool frag;
	u32 hash;
	u16 descs;
	int max_bufs;
};

int ena_com_prepare_tx(struct ena_com_io_sq *io_sq,
		       struct ena_com_tx_ctx *ena_tx_ctx,
		       int *nb_hw_desc);

int ena_com_rx_pkt(struct ena_com_io_cq *io_cq,
		   struct ena_com_io_sq *io_sq,
		   struct ena_com_rx_ctx *ena_rx_ctx);

int ena_com_add_single_rx_desc(struct ena_com_io_sq *io_sq,
			       struct ena_com_buf *ena_buf,
			       u16 req_id);

int ena_com_tx_comp_req_id_get(struct ena_com_io_cq *io_cq, u16 *req_id);

static inline void ena_com_unmask_intr(struct ena_com_io_cq *io_cq,
				       struct ena_eth_io_intr_reg *intr_reg)
{
	ENA_REG_WRITE32(io_cq->bus, intr_reg->intr_control, io_cq->unmask_reg);
}

static inline int ena_com_free_desc(struct ena_com_io_sq *io_sq)
{
	u16 tail, next_to_comp, cnt;

	next_to_comp = io_sq->next_to_comp;
	tail = io_sq->tail;
	cnt = tail - next_to_comp;

	return io_sq->q_depth - 1 - cnt;
}

/* Check if the submission queue has enough space to hold required_buffers */
static inline bool ena_com_sq_have_enough_space(struct ena_com_io_sq *io_sq,
						u16 required_buffers)
{
	int temp;

	if (io_sq->mem_queue_type == ENA_ADMIN_PLACEMENT_POLICY_HOST)
		return ena_com_free_desc(io_sq) >= required_buffers;

	/* This calculation doesn't need to be 100% accurate. So to reduce
	 * the calculation overhead just Subtract 2 lines from the free descs
	 * (one for the header line and one to compensate the devision
	 * down calculation.
	 */
	temp = required_buffers / io_sq->llq_info.descs_per_entry + 2;

	return ena_com_free_desc(io_sq) > temp;
}

static inline int ena_com_write_sq_doorbell(struct ena_com_io_sq *io_sq)
{
	u16 tail;

	tail = io_sq->tail;

	ena_trc_dbg("write submission queue doorbell for queue: %d tail: %d\n",
		    io_sq->qid, tail);

	ENA_REG_WRITE32(io_sq->bus, tail, io_sq->db_addr);

	return 0;
}

static inline int ena_com_update_dev_comp_head(struct ena_com_io_cq *io_cq)
{
	u16 unreported_comp, head;
	bool need_update;

	head = io_cq->head;
	unreported_comp = head - io_cq->last_head_update;
	need_update = unreported_comp > (io_cq->q_depth / ENA_COMP_HEAD_THRESH);

	if (io_cq->cq_head_db_reg && need_update) {
		ena_trc_dbg("Write completion queue doorbell for queue %d: head: %d\n",
			    io_cq->qid, head);
		ENA_REG_WRITE32(io_cq->bus, head, io_cq->cq_head_db_reg);
		io_cq->last_head_update = head;
	}

	return 0;
}

static inline void ena_com_update_numa_node(struct ena_com_io_cq *io_cq,
					    u8 numa_node)
{
	struct ena_eth_io_numa_node_cfg_reg numa_cfg;

	if (!io_cq->numa_node_cfg_reg)
		return;

	numa_cfg.numa_cfg = (numa_node & ENA_ETH_IO_NUMA_NODE_CFG_REG_NUMA_MASK)
		| ENA_ETH_IO_NUMA_NODE_CFG_REG_ENABLED_MASK;

	ENA_REG_WRITE32(io_cq->bus, numa_cfg.numa_cfg, io_cq->numa_node_cfg_reg);
}

static inline void ena_com_comp_ack(struct ena_com_io_sq *io_sq, u16 elem)
{
	io_sq->next_to_comp += elem;
}

#if defined(__cplusplus)
}
#endif
#endif /* ENA_ETH_COM_H_ */
