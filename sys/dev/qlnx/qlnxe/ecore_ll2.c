/*
 * Copyright (c) 2018-2019 Cavium, Inc.
 * All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 *  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 *  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 *  ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 *  LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 *  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 *  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 *  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 *  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 *  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *  POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * File : ecore_ll2.c
 */
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "bcm_osal.h"

#include "ecore.h"
#include "ecore_status.h"
#include "ecore_ll2.h"
#include "reg_addr.h"
#include "ecore_int.h"
#include "ecore_cxt.h"
#include "ecore_sp_commands.h"
#include "ecore_hw.h"
#include "reg_addr.h"
#include "ecore_dev_api.h"
#include "ecore_iro.h"
#include "ecore_gtt_reg_addr.h"
#include "ecore_ooo.h"
#include "ecore_hw.h"
#include "ecore_mcp.h"

#define ECORE_LL2_RX_REGISTERED(ll2)	((ll2)->rx_queue.b_cb_registred)
#define ECORE_LL2_TX_REGISTERED(ll2)	((ll2)->tx_queue.b_cb_registred)

#ifdef _NTDDK_
#pragma warning(push)
#pragma warning(disable : 28167)
#pragma warning(disable : 28123)
#pragma warning(disable : 28121)
#endif

static struct ecore_ll2_info *
__ecore_ll2_handle_sanity(struct ecore_hwfn *p_hwfn,
			  u8 connection_handle,
			  bool b_lock, bool b_only_active)
{
	struct ecore_ll2_info *p_ll2_conn, *p_ret = OSAL_NULL;

	if (connection_handle >= ECORE_MAX_NUM_OF_LL2_CONNECTIONS)
		return OSAL_NULL;

	if (!p_hwfn->p_ll2_info)
		return OSAL_NULL;

	/* TODO - is there really need for the locked vs. unlocked
	 * variant? I simply used what was already there.
	 */
	p_ll2_conn = &p_hwfn->p_ll2_info[connection_handle];

	if (b_only_active) {
		if (b_lock)
			OSAL_MUTEX_ACQUIRE(&p_ll2_conn->mutex);
		if (p_ll2_conn->b_active)
			p_ret = p_ll2_conn;
		if (b_lock)
			OSAL_MUTEX_RELEASE(&p_ll2_conn->mutex);
	} else {
		p_ret = p_ll2_conn;
	}

	return p_ret;
}

static struct ecore_ll2_info *
ecore_ll2_handle_sanity(struct ecore_hwfn *p_hwfn,
			u8 connection_handle)
{
	return __ecore_ll2_handle_sanity(p_hwfn, connection_handle,
					 false, true);
}

static struct ecore_ll2_info *
ecore_ll2_handle_sanity_lock(struct ecore_hwfn *p_hwfn,
			     u8 connection_handle)
{
	return __ecore_ll2_handle_sanity(p_hwfn, connection_handle,
					 true, true);
}

static struct ecore_ll2_info *
ecore_ll2_handle_sanity_inactive(struct ecore_hwfn *p_hwfn,
				 u8 connection_handle)
{
	return __ecore_ll2_handle_sanity(p_hwfn, connection_handle,
					 false, false);
}

#ifndef LINUX_REMOVE
/* TODO - is this really been used by anyone? Is it a on future todo list? */
enum _ecore_status_t
ecore_ll2_get_fragment_of_tx_packet(struct ecore_hwfn *p_hwfn,
				    u8 connection_handle,
				    dma_addr_t *p_addr,
				    bool *b_last_fragment)
{
	struct ecore_ll2_tx_packet *p_pkt;
	struct ecore_ll2_info *p_ll2_conn;
	u16 cur_frag_idx = 0;

	p_ll2_conn = ecore_ll2_handle_sanity(p_hwfn, connection_handle);
	if (p_ll2_conn == OSAL_NULL)
		return ECORE_INVAL;
	p_pkt = &p_ll2_conn->tx_queue.cur_completing_packet;

	if (!p_ll2_conn->tx_queue.b_completing_packet || !p_addr)
		return ECORE_INVAL;

	if (p_ll2_conn->tx_queue.cur_completing_bd_idx == p_pkt->bd_used)
		return ECORE_INVAL;

	/* Packet is available and has at least one more frag - provide it */
	cur_frag_idx = p_ll2_conn->tx_queue.cur_completing_bd_idx++;
	*p_addr = p_pkt->bds_set[cur_frag_idx].tx_frag;
	if (b_last_fragment)
		*b_last_fragment = p_pkt->bd_used ==
				   p_ll2_conn->tx_queue.cur_completing_bd_idx;

	return ECORE_SUCCESS;
}
#endif

static void ecore_ll2_txq_flush(struct ecore_hwfn *p_hwfn,
				u8 connection_handle)
{
	bool b_last_packet = false, b_last_frag = false;
	struct ecore_ll2_tx_packet *p_pkt = OSAL_NULL;
	struct ecore_ll2_info *p_ll2_conn;
	struct ecore_ll2_tx_queue *p_tx;
	unsigned long flags = 0;
	dma_addr_t tx_frag;

	p_ll2_conn = ecore_ll2_handle_sanity_inactive(p_hwfn,
						      connection_handle);
	if (p_ll2_conn == OSAL_NULL)
		return;
	p_tx = &p_ll2_conn->tx_queue;

	OSAL_SPIN_LOCK_IRQSAVE(&p_tx->lock, flags);
	while (!OSAL_LIST_IS_EMPTY(&p_tx->active_descq)) {
		p_pkt = OSAL_LIST_FIRST_ENTRY(&p_tx->active_descq,
					      struct ecore_ll2_tx_packet,
					      list_entry);

		if (p_pkt == OSAL_NULL)
			break;

#if defined(_NTDDK_)
#pragma warning(suppress : 6011 28182)
#endif
		OSAL_LIST_REMOVE_ENTRY(&p_pkt->list_entry,
				       &p_tx->active_descq);
		b_last_packet = OSAL_LIST_IS_EMPTY(&p_tx->active_descq);
		OSAL_LIST_PUSH_TAIL(&p_pkt->list_entry,
				    &p_tx->free_descq);
		OSAL_SPIN_UNLOCK_IRQSAVE(&p_tx->lock, flags);
		if (p_ll2_conn->input.conn_type == ECORE_LL2_TYPE_OOO) {
			struct ecore_ooo_buffer	*p_buffer;

			p_buffer = (struct ecore_ooo_buffer *)p_pkt->cookie;
			ecore_ooo_put_free_buffer(p_hwfn->p_ooo_info, p_buffer);
		} else {
			p_tx->cur_completing_packet = *p_pkt;
			p_tx->cur_completing_bd_idx = 1;
			b_last_frag = p_tx->cur_completing_bd_idx ==
				      p_pkt->bd_used;

			tx_frag = p_pkt->bds_set[0].tx_frag;
			p_ll2_conn->cbs.tx_release_cb(p_ll2_conn->cbs.cookie,
						      p_ll2_conn->my_id,
						      p_pkt->cookie,
						      tx_frag,
						      b_last_frag,
						      b_last_packet);
		}
		OSAL_SPIN_LOCK_IRQSAVE(&p_tx->lock, flags);
	}
	OSAL_SPIN_UNLOCK_IRQSAVE(&p_tx->lock, flags);
}

static enum _ecore_status_t
ecore_ll2_txq_completion(struct ecore_hwfn *p_hwfn,
			 void *p_cookie)
{
	struct ecore_ll2_info *p_ll2_conn = (struct ecore_ll2_info*)p_cookie;
	struct ecore_ll2_tx_queue *p_tx = &p_ll2_conn->tx_queue;
	u16 new_idx = 0, num_bds = 0, num_bds_in_packet = 0;
	struct ecore_ll2_tx_packet *p_pkt;
	bool b_last_frag = false;
	unsigned long flags;
	enum _ecore_status_t rc = ECORE_INVAL;

	OSAL_SPIN_LOCK_IRQSAVE(&p_tx->lock, flags);
	if (p_tx->b_completing_packet) {
		/* TODO - this looks completely unnecessary to me - the only
		 * way we can re-enter is by the DPC calling us again, but this
		 * would only happen AFTER we return, and we unset this at end
		 * of the function.
		 */
		rc = ECORE_BUSY;
		goto out;
	}

	new_idx = OSAL_LE16_TO_CPU(*p_tx->p_fw_cons);
	num_bds = ((s16)new_idx - (s16)p_tx->bds_idx);
	while (num_bds) {
		if (OSAL_LIST_IS_EMPTY(&p_tx->active_descq))
			goto out;

		p_pkt = OSAL_LIST_FIRST_ENTRY(&p_tx->active_descq,
					      struct ecore_ll2_tx_packet,
					      list_entry);
		if (!p_pkt)
			goto out;

		p_tx->b_completing_packet = true;
		p_tx->cur_completing_packet = *p_pkt;
		num_bds_in_packet = p_pkt->bd_used;
#if defined(_NTDDK_)
#pragma warning(suppress : 6011 28182)
#endif
		OSAL_LIST_REMOVE_ENTRY(&p_pkt->list_entry,
				       &p_tx->active_descq);

		if (num_bds < num_bds_in_packet) {
			DP_NOTICE(p_hwfn, true,
				  "Rest of BDs does not cover whole packet\n");
			goto out;
		}

		num_bds -= num_bds_in_packet;
		p_tx->bds_idx += num_bds_in_packet;
		while (num_bds_in_packet--)
			ecore_chain_consume(&p_tx->txq_chain);

		p_tx->cur_completing_bd_idx = 1;
		b_last_frag = p_tx->cur_completing_bd_idx ==
			      p_pkt->bd_used;
		OSAL_LIST_PUSH_TAIL(&p_pkt->list_entry,
				    &p_tx->free_descq);

		OSAL_SPIN_UNLOCK_IRQSAVE(&p_tx->lock, flags);

		p_ll2_conn->cbs.tx_comp_cb(p_ll2_conn->cbs.cookie,
					   p_ll2_conn->my_id,
					   p_pkt->cookie,
					   p_pkt->bds_set[0].tx_frag,
					   b_last_frag,
					   !num_bds);

		OSAL_SPIN_LOCK_IRQSAVE(&p_tx->lock, flags);
	}

	p_tx->b_completing_packet = false;
	rc = ECORE_SUCCESS;
out:
	OSAL_SPIN_UNLOCK_IRQSAVE(&p_tx->lock, flags);
	return rc;
}

static void ecore_ll2_rxq_parse_gsi(union core_rx_cqe_union *p_cqe,
				    struct ecore_ll2_comp_rx_data *data)
{
	data->parse_flags =
		OSAL_LE16_TO_CPU(p_cqe->rx_cqe_gsi.parse_flags.flags);
	data->length.data_length =
		OSAL_LE16_TO_CPU(p_cqe->rx_cqe_gsi.data_length);
	data->vlan =
		OSAL_LE16_TO_CPU(p_cqe->rx_cqe_gsi.vlan);
	data->opaque_data_0 =
		OSAL_LE32_TO_CPU(p_cqe->rx_cqe_gsi.src_mac_addrhi);
	data->opaque_data_1 =
		OSAL_LE16_TO_CPU(p_cqe->rx_cqe_gsi.src_mac_addrlo);
	data->u.data_length_error =
		p_cqe->rx_cqe_gsi.data_length_error;
	data->qp_id = OSAL_LE16_TO_CPU(p_cqe->rx_cqe_gsi.qp_id);

	data->src_qp = OSAL_LE32_TO_CPU(p_cqe->rx_cqe_gsi.src_qp);
}

static void ecore_ll2_rxq_parse_reg(union core_rx_cqe_union *p_cqe,
				    struct ecore_ll2_comp_rx_data *data)
{
	data->parse_flags =
		OSAL_LE16_TO_CPU(p_cqe->rx_cqe_fp.parse_flags.flags);
	data->err_flags =
		OSAL_LE16_TO_CPU(p_cqe->rx_cqe_fp.err_flags.flags);
	data->length.packet_length =
		OSAL_LE16_TO_CPU(p_cqe->rx_cqe_fp.packet_length);
	data->vlan =
		OSAL_LE16_TO_CPU(p_cqe->rx_cqe_fp.vlan);
	data->opaque_data_0 =
		OSAL_LE32_TO_CPU(p_cqe->rx_cqe_fp.opaque_data.data[0]);
	data->opaque_data_1 =
		OSAL_LE32_TO_CPU(p_cqe->rx_cqe_fp.opaque_data.data[1]);
	data->u.placement_offset =
		p_cqe->rx_cqe_fp.placement_offset;
}

#if defined(_NTDDK_)
#pragma warning(suppress : 28167 26110)
#endif
static enum _ecore_status_t
ecore_ll2_handle_slowpath(struct ecore_hwfn *p_hwfn,
			  struct ecore_ll2_info *p_ll2_conn,
			  union core_rx_cqe_union *p_cqe,
			  unsigned long *p_lock_flags)
{
	struct ecore_ll2_rx_queue *p_rx = &p_ll2_conn->rx_queue;
	struct core_rx_slow_path_cqe *sp_cqe;

	sp_cqe = &p_cqe->rx_cqe_sp;
	if (sp_cqe->ramrod_cmd_id != CORE_RAMROD_RX_QUEUE_FLUSH) {
		DP_NOTICE(p_hwfn, true,
			  "LL2 - unexpected Rx CQE slowpath ramrod_cmd_id:%d\n",
			  sp_cqe->ramrod_cmd_id);
		return ECORE_INVAL;
	}

	if (p_ll2_conn->cbs.slowpath_cb == OSAL_NULL) {
		DP_NOTICE(p_hwfn, true,
			  "LL2 - received RX_QUEUE_FLUSH but no callback was provided\n");
		return ECORE_INVAL;
	}

	OSAL_SPIN_UNLOCK_IRQSAVE(&p_rx->lock, *p_lock_flags);

	p_ll2_conn->cbs.slowpath_cb(p_ll2_conn->cbs.cookie,
				    p_ll2_conn->my_id,
				    OSAL_LE32_TO_CPU(sp_cqe->opaque_data.data[0]),
				    OSAL_LE32_TO_CPU(sp_cqe->opaque_data.data[1]));

	OSAL_SPIN_LOCK_IRQSAVE(&p_rx->lock, *p_lock_flags);

	return ECORE_SUCCESS;
}

static enum _ecore_status_t
ecore_ll2_rxq_handle_completion(struct ecore_hwfn *p_hwfn,
				struct ecore_ll2_info *p_ll2_conn,
				union core_rx_cqe_union *p_cqe,
				unsigned long *p_lock_flags,
				bool b_last_cqe)
{
	struct ecore_ll2_rx_queue *p_rx = &p_ll2_conn->rx_queue;
	struct ecore_ll2_rx_packet *p_pkt = OSAL_NULL;
	struct ecore_ll2_comp_rx_data data;

	if (!OSAL_LIST_IS_EMPTY(&p_rx->active_descq))
		p_pkt = OSAL_LIST_FIRST_ENTRY(&p_rx->active_descq,
					      struct ecore_ll2_rx_packet,
					      list_entry);
	if (!p_pkt) {
		DP_NOTICE(p_hwfn, false,
			  "[%d] LL2 Rx completion but active_descq is empty\n",
			  p_ll2_conn->input.conn_type);

		return ECORE_IO;
	}

	OSAL_LIST_REMOVE_ENTRY(&p_pkt->list_entry, &p_rx->active_descq);

	if (p_cqe->rx_cqe_sp.type == CORE_RX_CQE_TYPE_REGULAR)
		ecore_ll2_rxq_parse_reg(p_cqe, &data);
	else
		ecore_ll2_rxq_parse_gsi(p_cqe, &data);

	if (ecore_chain_consume(&p_rx->rxq_chain) != p_pkt->rxq_bd) {
		DP_NOTICE(p_hwfn, false,
			  "Mismatch between active_descq and the LL2 Rx chain\n");
		/* TODO - didn't return error value since this wasn't handled
		 * before, but this is obviously lacking.
		 */
	}

	OSAL_LIST_PUSH_TAIL(&p_pkt->list_entry, &p_rx->free_descq);

	data.connection_handle = p_ll2_conn->my_id;
	data.cookie = p_pkt->cookie;
	data.rx_buf_addr = p_pkt->rx_buf_addr;
	data.b_last_packet = b_last_cqe;

	OSAL_SPIN_UNLOCK_IRQSAVE(&p_rx->lock, *p_lock_flags);
	p_ll2_conn->cbs.rx_comp_cb(p_ll2_conn->cbs.cookie,
				   &data);

	OSAL_SPIN_LOCK_IRQSAVE(&p_rx->lock, *p_lock_flags);

	return ECORE_SUCCESS;
}

static enum _ecore_status_t ecore_ll2_rxq_completion(struct ecore_hwfn *p_hwfn,
						     void *cookie)
{
	struct ecore_ll2_info *p_ll2_conn = (struct ecore_ll2_info*)cookie;
	struct ecore_ll2_rx_queue *p_rx = &p_ll2_conn->rx_queue;
	union core_rx_cqe_union *cqe = OSAL_NULL;
	u16 cq_new_idx = 0, cq_old_idx = 0;
	unsigned long flags = 0;
	enum _ecore_status_t rc = ECORE_SUCCESS;

	OSAL_SPIN_LOCK_IRQSAVE(&p_rx->lock, flags);
	cq_new_idx = OSAL_LE16_TO_CPU(*p_rx->p_fw_cons);
	cq_old_idx = ecore_chain_get_cons_idx(&p_rx->rcq_chain);

	while (cq_new_idx != cq_old_idx) {
		bool b_last_cqe = (cq_new_idx == cq_old_idx);

		cqe = (union core_rx_cqe_union *)ecore_chain_consume(&p_rx->rcq_chain);
		cq_old_idx = ecore_chain_get_cons_idx(&p_rx->rcq_chain);

		DP_VERBOSE(p_hwfn, ECORE_MSG_LL2,
			   "LL2 [sw. cons %04x, fw. at %04x] - Got Packet of type %02x\n",
			   cq_old_idx, cq_new_idx, cqe->rx_cqe_sp.type);

		switch (cqe->rx_cqe_sp.type) {
		case CORE_RX_CQE_TYPE_SLOW_PATH:
			rc = ecore_ll2_handle_slowpath(p_hwfn, p_ll2_conn,
						       cqe, &flags);
			break;
		case CORE_RX_CQE_TYPE_GSI_OFFLOAD:
		case CORE_RX_CQE_TYPE_REGULAR:
			rc = ecore_ll2_rxq_handle_completion(p_hwfn, p_ll2_conn,
							     cqe, &flags,
							     b_last_cqe);
			break;
		default:
			rc = ECORE_IO;
		}
	}

	OSAL_SPIN_UNLOCK_IRQSAVE(&p_rx->lock, flags);
	return rc;
}

static void ecore_ll2_rxq_flush(struct ecore_hwfn *p_hwfn,
			 u8 connection_handle)
{
	struct ecore_ll2_info *p_ll2_conn = OSAL_NULL;
	struct ecore_ll2_rx_packet *p_pkt = OSAL_NULL;
	struct ecore_ll2_rx_queue *p_rx;
	unsigned long flags = 0;

	p_ll2_conn = ecore_ll2_handle_sanity_inactive(p_hwfn,
						      connection_handle);
	if (p_ll2_conn == OSAL_NULL)
		return;
	p_rx = &p_ll2_conn->rx_queue;

	OSAL_SPIN_LOCK_IRQSAVE(&p_rx->lock, flags);
	while (!OSAL_LIST_IS_EMPTY(&p_rx->active_descq)) {
		bool b_last;
		p_pkt = OSAL_LIST_FIRST_ENTRY(&p_rx->active_descq,
					      struct ecore_ll2_rx_packet,
					      list_entry);
		if (p_pkt == OSAL_NULL)
			break;
#if defined(_NTDDK_)
#pragma warning(suppress : 6011 28182)
#endif
		OSAL_LIST_REMOVE_ENTRY(&p_pkt->list_entry,
				       &p_rx->active_descq);
		OSAL_LIST_PUSH_TAIL(&p_pkt->list_entry,
				    &p_rx->free_descq);
		b_last = OSAL_LIST_IS_EMPTY(&p_rx->active_descq);
		OSAL_SPIN_UNLOCK_IRQSAVE(&p_rx->lock, flags);

		if (p_ll2_conn->input.conn_type == ECORE_LL2_TYPE_OOO) {
			struct ecore_ooo_buffer	*p_buffer;

			p_buffer = (struct ecore_ooo_buffer *)p_pkt->cookie;
			ecore_ooo_put_free_buffer(p_hwfn->p_ooo_info, p_buffer);
		} else {
			dma_addr_t rx_buf_addr = p_pkt->rx_buf_addr;
			void *cookie  = p_pkt->cookie;

			p_ll2_conn->cbs.rx_release_cb(p_ll2_conn->cbs.cookie,
						      p_ll2_conn->my_id,
						      cookie,
						      rx_buf_addr,
						      b_last);
		}
		OSAL_SPIN_LOCK_IRQSAVE(&p_rx->lock, flags);
	}
	OSAL_SPIN_UNLOCK_IRQSAVE(&p_rx->lock, flags);
}

static bool
ecore_ll2_lb_rxq_handler_slowpath(struct ecore_hwfn *p_hwfn,
				  struct core_rx_slow_path_cqe *p_cqe)
{
	struct ooo_opaque *iscsi_ooo;
	u32 cid;

	if (p_cqe->ramrod_cmd_id != CORE_RAMROD_RX_QUEUE_FLUSH)
		return false;

	iscsi_ooo = (struct ooo_opaque *)&p_cqe->opaque_data;
	if (iscsi_ooo->ooo_opcode != TCP_EVENT_DELETE_ISLES)
		return false;

	/* Need to make a flush */
	cid = OSAL_LE32_TO_CPU(iscsi_ooo->cid);
	ecore_ooo_release_connection_isles(p_hwfn->p_ooo_info, cid);

	return true;
}

static enum _ecore_status_t
ecore_ll2_lb_rxq_handler(struct ecore_hwfn *p_hwfn,
			 struct ecore_ll2_info *p_ll2_conn)
{
	struct ecore_ll2_rx_queue *p_rx = &p_ll2_conn->rx_queue;
	u16 packet_length = 0, parse_flags = 0, vlan = 0;
	struct ecore_ll2_rx_packet *p_pkt = OSAL_NULL;
	u32 num_ooo_add_to_peninsula = 0, cid;
	union core_rx_cqe_union *cqe = OSAL_NULL;
	u16 cq_new_idx = 0, cq_old_idx = 0;
	struct ecore_ooo_buffer	*p_buffer;
	struct ooo_opaque *iscsi_ooo;
	u8 placement_offset = 0;
	u8 cqe_type;

	cq_new_idx = OSAL_LE16_TO_CPU(*p_rx->p_fw_cons);
	cq_old_idx = ecore_chain_get_cons_idx(&p_rx->rcq_chain);
	if (cq_new_idx == cq_old_idx)
		return ECORE_SUCCESS;

	while (cq_new_idx != cq_old_idx) {
		struct core_rx_fast_path_cqe *p_cqe_fp;

		cqe = (union core_rx_cqe_union *)ecore_chain_consume(&p_rx->rcq_chain);
		cq_old_idx = ecore_chain_get_cons_idx(&p_rx->rcq_chain);
		cqe_type = cqe->rx_cqe_sp.type;

		if (cqe_type == CORE_RX_CQE_TYPE_SLOW_PATH)
			if (ecore_ll2_lb_rxq_handler_slowpath(p_hwfn,
							      &cqe->rx_cqe_sp))
				continue;

		if (cqe_type != CORE_RX_CQE_TYPE_REGULAR) {
			DP_NOTICE(p_hwfn, true,
				  "Got a non-regular LB LL2 completion [type 0x%02x]\n",
				  cqe_type);
			return ECORE_INVAL;
		}
		p_cqe_fp = &cqe->rx_cqe_fp;

		placement_offset = p_cqe_fp->placement_offset;
		parse_flags = OSAL_LE16_TO_CPU(p_cqe_fp->parse_flags.flags);
		packet_length = OSAL_LE16_TO_CPU(p_cqe_fp->packet_length);
		vlan = OSAL_LE16_TO_CPU(p_cqe_fp->vlan);
		iscsi_ooo = (struct ooo_opaque *)&p_cqe_fp->opaque_data;
		ecore_ooo_save_history_entry(p_hwfn->p_ooo_info, iscsi_ooo);
		cid = OSAL_LE32_TO_CPU(iscsi_ooo->cid);

		/* Process delete isle first*/
		if (iscsi_ooo->drop_size)
			ecore_ooo_delete_isles(p_hwfn, p_hwfn->p_ooo_info, cid,
					       iscsi_ooo->drop_isle,
					       iscsi_ooo->drop_size);

		if (iscsi_ooo->ooo_opcode == TCP_EVENT_NOP)
			continue;

		/* Now process create/add/join isles */
		if (OSAL_LIST_IS_EMPTY(&p_rx->active_descq)) {
			DP_NOTICE(p_hwfn, true,
				  "LL2 OOO RX chain has no submitted buffers\n");
			return ECORE_IO;
		}

		p_pkt = OSAL_LIST_FIRST_ENTRY(&p_rx->active_descq,
					      struct ecore_ll2_rx_packet,
					      list_entry);

		if ((iscsi_ooo->ooo_opcode == TCP_EVENT_ADD_NEW_ISLE) ||
		    (iscsi_ooo->ooo_opcode == TCP_EVENT_ADD_ISLE_RIGHT) ||
		    (iscsi_ooo->ooo_opcode == TCP_EVENT_ADD_ISLE_LEFT) ||
		    (iscsi_ooo->ooo_opcode == TCP_EVENT_ADD_PEN) ||
		    (iscsi_ooo->ooo_opcode == TCP_EVENT_JOIN)) {
			if (!p_pkt) {
				DP_NOTICE(p_hwfn, true,
					  "LL2 OOO RX packet is not valid\n");
				return ECORE_IO;
			}
#if defined(_NTDDK_)
#pragma warning(suppress : 6011 28182)
#endif
			OSAL_LIST_REMOVE_ENTRY(&p_pkt->list_entry,
					       &p_rx->active_descq);
			p_buffer = (struct ecore_ooo_buffer *)p_pkt->cookie;
			p_buffer->packet_length = packet_length;
			p_buffer->parse_flags = parse_flags;
			p_buffer->vlan = vlan;
			p_buffer->placement_offset = placement_offset;
			if (ecore_chain_consume(&p_rx->rxq_chain) !=
			    p_pkt->rxq_bd) {
				/**/
			}
			ecore_ooo_dump_rx_event(p_hwfn, iscsi_ooo, p_buffer);
			OSAL_LIST_PUSH_TAIL(&p_pkt->list_entry,
					    &p_rx->free_descq);

			switch (iscsi_ooo->ooo_opcode) {
			case TCP_EVENT_ADD_NEW_ISLE:
				ecore_ooo_add_new_isle(p_hwfn,
						       p_hwfn->p_ooo_info,
						       cid,
						       iscsi_ooo->ooo_isle,
						       p_buffer);
				break;
			case TCP_EVENT_ADD_ISLE_RIGHT:
				ecore_ooo_add_new_buffer(p_hwfn,
							 p_hwfn->p_ooo_info,
							 cid,
							 iscsi_ooo->ooo_isle,
							 p_buffer,
							 ECORE_OOO_RIGHT_BUF);
				break;
			case TCP_EVENT_ADD_ISLE_LEFT:
				ecore_ooo_add_new_buffer(p_hwfn,
							 p_hwfn->p_ooo_info,
							 cid,
							 iscsi_ooo->ooo_isle,
							 p_buffer,
							 ECORE_OOO_LEFT_BUF);
				break;
			case TCP_EVENT_JOIN:
				ecore_ooo_add_new_buffer(p_hwfn,
							 p_hwfn->p_ooo_info,
							 cid,
							 iscsi_ooo->ooo_isle +
							 1,
							 p_buffer,
							 ECORE_OOO_LEFT_BUF);
				ecore_ooo_join_isles(p_hwfn,
						     p_hwfn->p_ooo_info,
						     cid,
						     iscsi_ooo->ooo_isle);
				break;
			case TCP_EVENT_ADD_PEN:
				num_ooo_add_to_peninsula++;
				ecore_ooo_put_ready_buffer(p_hwfn->p_ooo_info,
							   p_buffer, true);
				break;
			}
		} else {
			DP_NOTICE(p_hwfn, true,
				  "Unexpected event (%d) TX OOO completion\n",
				  iscsi_ooo->ooo_opcode);
		}
	}

	return ECORE_SUCCESS;
}

static void
ecore_ooo_submit_tx_buffers(struct ecore_hwfn *p_hwfn,
			    struct ecore_ll2_info *p_ll2_conn)
{
	struct ecore_ll2_tx_pkt_info tx_pkt;
	struct ecore_ooo_buffer *p_buffer;
	dma_addr_t first_frag;
	u16 l4_hdr_offset_w;
	u8 bd_flags;
	enum _ecore_status_t rc;

	/* Submit Tx buffers here */
	while ((p_buffer = ecore_ooo_get_ready_buffer(p_hwfn->p_ooo_info))) {
		l4_hdr_offset_w = 0;
		bd_flags = 0;

		first_frag = p_buffer->rx_buffer_phys_addr +
			     p_buffer->placement_offset;
		SET_FIELD(bd_flags, CORE_TX_BD_DATA_FORCE_VLAN_MODE, 1);
		SET_FIELD(bd_flags, CORE_TX_BD_DATA_L4_PROTOCOL, 1);

		OSAL_MEM_ZERO(&tx_pkt, sizeof(tx_pkt));
		tx_pkt.num_of_bds = 1;
		tx_pkt.vlan = p_buffer->vlan;
		tx_pkt.bd_flags = bd_flags;
		tx_pkt.l4_hdr_offset_w = l4_hdr_offset_w;
		tx_pkt.tx_dest = (enum ecore_ll2_tx_dest)p_ll2_conn->tx_dest;
		tx_pkt.first_frag = first_frag;
		tx_pkt.first_frag_len = p_buffer->packet_length;
		tx_pkt.cookie = p_buffer;

		rc = ecore_ll2_prepare_tx_packet(p_hwfn, p_ll2_conn->my_id,
						 &tx_pkt, true);
		if (rc != ECORE_SUCCESS) {
			ecore_ooo_put_ready_buffer(p_hwfn->p_ooo_info,
						   p_buffer, false);
			break;
		}
	}
}

static void
ecore_ooo_submit_rx_buffers(struct ecore_hwfn *p_hwfn,
			    struct ecore_ll2_info *p_ll2_conn)
{
	struct ecore_ooo_buffer *p_buffer;
	enum _ecore_status_t rc;

	while ((p_buffer = ecore_ooo_get_free_buffer(p_hwfn->p_ooo_info))) {
		rc = ecore_ll2_post_rx_buffer(p_hwfn,
					    p_ll2_conn->my_id,
					    p_buffer->rx_buffer_phys_addr,
					    0, p_buffer, true);
		if (rc != ECORE_SUCCESS) {
			ecore_ooo_put_free_buffer(p_hwfn->p_ooo_info, p_buffer);
			break;
		}
	}
}

static enum _ecore_status_t
ecore_ll2_lb_rxq_completion(struct ecore_hwfn *p_hwfn,
			    void *p_cookie)
{
	struct ecore_ll2_info *p_ll2_conn = (struct ecore_ll2_info *)p_cookie;
	enum _ecore_status_t rc;

	rc = ecore_ll2_lb_rxq_handler(p_hwfn, p_ll2_conn);
	if (rc != ECORE_SUCCESS)
		return rc;

	ecore_ooo_submit_rx_buffers(p_hwfn, p_ll2_conn);
	ecore_ooo_submit_tx_buffers(p_hwfn, p_ll2_conn);

	return 0;
}

static enum _ecore_status_t
ecore_ll2_lb_txq_completion(struct ecore_hwfn *p_hwfn,
			    void *p_cookie)
{
	struct ecore_ll2_info *p_ll2_conn = (struct ecore_ll2_info *)p_cookie;
	struct ecore_ll2_tx_queue *p_tx = &p_ll2_conn->tx_queue;
	struct ecore_ll2_tx_packet *p_pkt = OSAL_NULL;
	struct ecore_ooo_buffer	*p_buffer;
	bool b_dont_submit_rx = false;
	u16 new_idx = 0, num_bds = 0;
	enum _ecore_status_t rc;

	new_idx = OSAL_LE16_TO_CPU(*p_tx->p_fw_cons);
	num_bds = ((s16)new_idx - (s16)p_tx->bds_idx);

	if (!num_bds)
		return ECORE_SUCCESS;

	while (num_bds) {

		if (OSAL_LIST_IS_EMPTY(&p_tx->active_descq))
			return ECORE_INVAL;

		p_pkt = OSAL_LIST_FIRST_ENTRY(&p_tx->active_descq,
					      struct ecore_ll2_tx_packet,
					      list_entry);
		if (!p_pkt)
			return ECORE_INVAL;

		if (p_pkt->bd_used != 1) {
			DP_NOTICE(p_hwfn, true,
				  "Unexpectedly many BDs(%d) in TX OOO completion\n",
				  p_pkt->bd_used);
			return ECORE_INVAL;
		}

		OSAL_LIST_REMOVE_ENTRY(&p_pkt->list_entry,
				       &p_tx->active_descq);

		num_bds--;
		p_tx->bds_idx++;
		ecore_chain_consume(&p_tx->txq_chain);

		p_buffer = (struct ecore_ooo_buffer *)p_pkt->cookie;
		OSAL_LIST_PUSH_TAIL(&p_pkt->list_entry,
				    &p_tx->free_descq);

		if (b_dont_submit_rx) {
			ecore_ooo_put_free_buffer(p_hwfn->p_ooo_info, p_buffer);
			continue;
		}

		rc = ecore_ll2_post_rx_buffer(p_hwfn, p_ll2_conn->my_id,
					      p_buffer->rx_buffer_phys_addr, 0,
					      p_buffer, true);
		if (rc != ECORE_SUCCESS) {
			ecore_ooo_put_free_buffer(p_hwfn->p_ooo_info, p_buffer);
			b_dont_submit_rx = true;
		}
	}

	ecore_ooo_submit_tx_buffers(p_hwfn, p_ll2_conn);

	return ECORE_SUCCESS;
}

static enum _ecore_status_t ecore_sp_ll2_rx_queue_start(struct ecore_hwfn *p_hwfn,
							struct ecore_ll2_info *p_ll2_conn,
							u8 action_on_error)
{
	enum ecore_ll2_conn_type conn_type = p_ll2_conn->input.conn_type;
	struct ecore_ll2_rx_queue *p_rx = &p_ll2_conn->rx_queue;
	struct core_rx_start_ramrod_data *p_ramrod = OSAL_NULL;
	struct ecore_spq_entry *p_ent = OSAL_NULL;
	struct ecore_sp_init_data init_data;
	u16 cqe_pbl_size;
	enum _ecore_status_t rc	= ECORE_SUCCESS;

	/* Get SPQ entry */
	OSAL_MEMSET(&init_data, 0, sizeof(init_data));
	init_data.cid = p_ll2_conn->cid;
	init_data.opaque_fid = p_hwfn->hw_info.opaque_fid;
	init_data.comp_mode = ECORE_SPQ_MODE_EBLOCK;

	rc = ecore_sp_init_request(p_hwfn, &p_ent,
				   CORE_RAMROD_RX_QUEUE_START,
				   PROTOCOLID_CORE, &init_data);
	if (rc != ECORE_SUCCESS)
		return rc;

	p_ramrod = &p_ent->ramrod.core_rx_queue_start;

	p_ramrod->sb_id = OSAL_CPU_TO_LE16(ecore_int_get_sp_sb_id(p_hwfn));
	p_ramrod->sb_index = p_rx->rx_sb_index;
	p_ramrod->complete_event_flg = 1;

	p_ramrod->mtu = OSAL_CPU_TO_LE16(p_ll2_conn->input.mtu);
	DMA_REGPAIR_LE(p_ramrod->bd_base,
		       p_rx->rxq_chain.p_phys_addr);
	cqe_pbl_size = (u16)ecore_chain_get_page_cnt(&p_rx->rcq_chain);
	p_ramrod->num_of_pbl_pages = OSAL_CPU_TO_LE16(cqe_pbl_size);
	DMA_REGPAIR_LE(p_ramrod->cqe_pbl_addr,
		       ecore_chain_get_pbl_phys(&p_rx->rcq_chain));

	p_ramrod->drop_ttl0_flg = p_ll2_conn->input.rx_drop_ttl0_flg;
	p_ramrod->inner_vlan_stripping_en =
		p_ll2_conn->input.rx_vlan_removal_en;

	if (OSAL_TEST_BIT(ECORE_MF_UFP_SPECIFIC, &p_hwfn->p_dev->mf_bits) &&
	    (p_ll2_conn->input.conn_type == ECORE_LL2_TYPE_FCOE))
		p_ramrod->report_outer_vlan = 1;
	p_ramrod->queue_id = p_ll2_conn->queue_id;
	p_ramrod->main_func_queue = p_ll2_conn->main_func_queue;

	if (OSAL_TEST_BIT(ECORE_MF_LL2_NON_UNICAST,
			  &p_hwfn->p_dev->mf_bits) &&
	    p_ramrod->main_func_queue &&
	    ((conn_type != ECORE_LL2_TYPE_ROCE) &&
	     (conn_type != ECORE_LL2_TYPE_IWARP))) {
		p_ramrod->mf_si_bcast_accept_all = 1;
		p_ramrod->mf_si_mcast_accept_all = 1;
	} else {
		p_ramrod->mf_si_bcast_accept_all = 0;
		p_ramrod->mf_si_mcast_accept_all = 0;
	}

	p_ramrod->action_on_error.error_type = action_on_error;
	p_ramrod->gsi_offload_flag = p_ll2_conn->input.gsi_enable;
	return ecore_spq_post(p_hwfn, p_ent, OSAL_NULL);
}

static enum _ecore_status_t ecore_sp_ll2_tx_queue_start(struct ecore_hwfn *p_hwfn,
							struct ecore_ll2_info *p_ll2_conn)
{
	enum ecore_ll2_conn_type conn_type = p_ll2_conn->input.conn_type;
	struct ecore_ll2_tx_queue *p_tx = &p_ll2_conn->tx_queue;
	struct core_tx_start_ramrod_data *p_ramrod = OSAL_NULL;
	struct ecore_spq_entry *p_ent = OSAL_NULL;
	struct ecore_sp_init_data init_data;
	u16 pq_id = 0, pbl_size;
	enum _ecore_status_t rc = ECORE_NOTIMPL;

	if (!ECORE_LL2_TX_REGISTERED(p_ll2_conn))
		return ECORE_SUCCESS;

	if (p_ll2_conn->input.conn_type == ECORE_LL2_TYPE_OOO)
		p_ll2_conn->tx_stats_en = 0;
	else
		p_ll2_conn->tx_stats_en = 1;

	/* Get SPQ entry */
	OSAL_MEMSET(&init_data, 0, sizeof(init_data));
	init_data.cid = p_ll2_conn->cid;
	init_data.opaque_fid = p_hwfn->hw_info.opaque_fid;
	init_data.comp_mode = ECORE_SPQ_MODE_EBLOCK;

	rc = ecore_sp_init_request(p_hwfn, &p_ent,
				   CORE_RAMROD_TX_QUEUE_START,
				   PROTOCOLID_CORE, &init_data);
	if (rc != ECORE_SUCCESS)
		return rc;

	p_ramrod = &p_ent->ramrod.core_tx_queue_start;

	p_ramrod->sb_id = OSAL_CPU_TO_LE16(ecore_int_get_sp_sb_id(p_hwfn));
	p_ramrod->sb_index = p_tx->tx_sb_index;
	p_ramrod->mtu = OSAL_CPU_TO_LE16(p_ll2_conn->input.mtu);
	p_ramrod->stats_en = p_ll2_conn->tx_stats_en;
	p_ramrod->stats_id = p_ll2_conn->tx_stats_id;

	DMA_REGPAIR_LE(p_ramrod->pbl_base_addr,
		       ecore_chain_get_pbl_phys(&p_tx->txq_chain));
	pbl_size = (u16)ecore_chain_get_page_cnt(&p_tx->txq_chain);
	p_ramrod->pbl_size = OSAL_CPU_TO_LE16(pbl_size);

	/* TODO RESC_ALLOC pq for ll2 */
	switch (p_ll2_conn->input.tx_tc) {
	case PURE_LB_TC:
		pq_id = ecore_get_cm_pq_idx(p_hwfn, PQ_FLAGS_LB);
		break;
	case PKT_LB_TC:
		pq_id = ecore_get_cm_pq_idx(p_hwfn, PQ_FLAGS_OOO);
		break;
	default:
		pq_id = ecore_get_cm_pq_idx(p_hwfn, PQ_FLAGS_OFLD);
	}

	p_ramrod->qm_pq_id = OSAL_CPU_TO_LE16(pq_id);

	switch (conn_type) {
	case ECORE_LL2_TYPE_FCOE:
		p_ramrod->conn_type = PROTOCOLID_FCOE;
		break;
	case ECORE_LL2_TYPE_ISCSI:
		p_ramrod->conn_type = PROTOCOLID_ISCSI;
		break;
	case ECORE_LL2_TYPE_ROCE:
		p_ramrod->conn_type = PROTOCOLID_ROCE;
		break;
	case ECORE_LL2_TYPE_IWARP:
		p_ramrod->conn_type = PROTOCOLID_IWARP;
		break;
	case ECORE_LL2_TYPE_OOO:
		if (p_hwfn->hw_info.personality == ECORE_PCI_ISCSI) {
			p_ramrod->conn_type = PROTOCOLID_ISCSI;
		} else {
			p_ramrod->conn_type = PROTOCOLID_IWARP;
		}
		break;
	default:
		p_ramrod->conn_type = PROTOCOLID_ETH;
		DP_NOTICE(p_hwfn, false, "Unknown connection type: %d\n",
			  conn_type);
	}

	p_ramrod->gsi_offload_flag = p_ll2_conn->input.gsi_enable;

	rc = ecore_spq_post(p_hwfn, p_ent, OSAL_NULL);
	if (rc != ECORE_SUCCESS)
		return rc;

	rc = ecore_db_recovery_add(p_hwfn->p_dev, p_tx->doorbell_addr,
				   &p_tx->db_msg, DB_REC_WIDTH_32B,
				   DB_REC_KERNEL);
	return rc;
}

static enum _ecore_status_t ecore_sp_ll2_rx_queue_stop(struct ecore_hwfn *p_hwfn,
						       struct ecore_ll2_info *p_ll2_conn)
{
	struct core_rx_stop_ramrod_data	*p_ramrod = OSAL_NULL;
	struct ecore_spq_entry *p_ent = OSAL_NULL;
	struct ecore_sp_init_data init_data;
	enum _ecore_status_t rc = ECORE_NOTIMPL;

	/* Get SPQ entry */
	OSAL_MEMSET(&init_data, 0, sizeof(init_data));
	init_data.cid = p_ll2_conn->cid;
	init_data.opaque_fid = p_hwfn->hw_info.opaque_fid;
	init_data.comp_mode = ECORE_SPQ_MODE_EBLOCK;

	rc = ecore_sp_init_request(p_hwfn, &p_ent,
				   CORE_RAMROD_RX_QUEUE_STOP,
				   PROTOCOLID_CORE, &init_data);
	if (rc != ECORE_SUCCESS)
		return rc;

	p_ramrod = &p_ent->ramrod.core_rx_queue_stop;

	p_ramrod->complete_event_flg = 1;
	p_ramrod->queue_id = p_ll2_conn->queue_id;

	return ecore_spq_post(p_hwfn, p_ent, OSAL_NULL);
}

static enum _ecore_status_t ecore_sp_ll2_tx_queue_stop(struct ecore_hwfn *p_hwfn,
						       struct ecore_ll2_info *p_ll2_conn)
{
	struct ecore_ll2_tx_queue *p_tx = &p_ll2_conn->tx_queue;
	struct ecore_spq_entry *p_ent = OSAL_NULL;
	struct ecore_sp_init_data init_data;
	enum _ecore_status_t rc = ECORE_NOTIMPL;

	ecore_db_recovery_del(p_hwfn->p_dev, p_tx->doorbell_addr,
			      &p_tx->db_msg);

	/* Get SPQ entry */
	OSAL_MEMSET(&init_data, 0, sizeof(init_data));
	init_data.cid = p_ll2_conn->cid;
	init_data.opaque_fid = p_hwfn->hw_info.opaque_fid;
	init_data.comp_mode = ECORE_SPQ_MODE_EBLOCK;

	rc = ecore_sp_init_request(p_hwfn, &p_ent,
				   CORE_RAMROD_TX_QUEUE_STOP,
				   PROTOCOLID_CORE, &init_data);
	if (rc != ECORE_SUCCESS)
		return rc;

	return ecore_spq_post(p_hwfn, p_ent, OSAL_NULL);
}

static enum _ecore_status_t
ecore_ll2_acquire_connection_rx(struct ecore_hwfn *p_hwfn,
				struct ecore_ll2_info *p_ll2_info)
{
	struct ecore_ll2_rx_packet *p_descq;
	u32 capacity;
	enum _ecore_status_t rc = ECORE_SUCCESS;

	if (!p_ll2_info->input.rx_num_desc)
		goto out;

	rc = ecore_chain_alloc(p_hwfn->p_dev,
			       ECORE_CHAIN_USE_TO_CONSUME_PRODUCE,
			       ECORE_CHAIN_MODE_NEXT_PTR,
			       ECORE_CHAIN_CNT_TYPE_U16,
			       p_ll2_info->input.rx_num_desc,
			       sizeof(struct core_rx_bd),
			       &p_ll2_info->rx_queue.rxq_chain, OSAL_NULL);
	if (rc) {
		DP_NOTICE(p_hwfn, false,
			  "Failed to allocate ll2 rxq chain\n");
		goto out;
	}

	capacity = ecore_chain_get_capacity(&p_ll2_info->rx_queue.rxq_chain);
	p_descq = OSAL_ZALLOC(p_hwfn->p_dev, GFP_KERNEL,
			      capacity * sizeof(struct ecore_ll2_rx_packet));
	if (!p_descq) {
		rc = ECORE_NOMEM;
		DP_NOTICE(p_hwfn, false,
			  "Failed to allocate ll2 Rx desc\n");
		goto out;
	}
	p_ll2_info->rx_queue.descq_array = p_descq;

	rc = ecore_chain_alloc(p_hwfn->p_dev,
			       ECORE_CHAIN_USE_TO_CONSUME_PRODUCE,
			       ECORE_CHAIN_MODE_PBL,
			       ECORE_CHAIN_CNT_TYPE_U16,
			       p_ll2_info->input.rx_num_desc,
			       sizeof(struct core_rx_fast_path_cqe),
			       &p_ll2_info->rx_queue.rcq_chain, OSAL_NULL);
	if (rc != ECORE_SUCCESS) {
		DP_NOTICE(p_hwfn, false,
			  "Failed to allocate ll2 rcq chain\n");
		goto out;
	}

	DP_VERBOSE(p_hwfn, ECORE_MSG_LL2,
		   "Allocated LL2 Rxq [Type %08x] with 0x%08x buffers\n",
		   p_ll2_info->input.conn_type,
		   p_ll2_info->input.rx_num_desc);

out:
	return rc;
}

static enum _ecore_status_t
ecore_ll2_acquire_connection_tx(struct ecore_hwfn *p_hwfn,
				struct ecore_ll2_info *p_ll2_info)
{
	struct ecore_ll2_tx_packet *p_descq;
	u32 capacity;
	enum _ecore_status_t rc = ECORE_SUCCESS;
	u32 desc_size;

	if (!p_ll2_info->input.tx_num_desc)
		goto out;

	rc = ecore_chain_alloc(p_hwfn->p_dev,
			       ECORE_CHAIN_USE_TO_CONSUME_PRODUCE,
			       ECORE_CHAIN_MODE_PBL,
			       ECORE_CHAIN_CNT_TYPE_U16,
			       p_ll2_info->input.tx_num_desc,
			       sizeof(struct core_tx_bd),
			       &p_ll2_info->tx_queue.txq_chain, OSAL_NULL);
	if (rc != ECORE_SUCCESS)
		goto out;

	capacity = ecore_chain_get_capacity(&p_ll2_info->tx_queue.txq_chain);
	desc_size = (sizeof(*p_descq) +
		     (p_ll2_info->input.tx_max_bds_per_packet - 1) *
		     sizeof(p_descq->bds_set));

	p_descq = OSAL_ZALLOC(p_hwfn->p_dev, GFP_KERNEL,
			      capacity * desc_size);
	if (!p_descq) {
		rc = ECORE_NOMEM;
		goto out;
	}
	p_ll2_info->tx_queue.descq_array = p_descq;

	DP_VERBOSE(p_hwfn, ECORE_MSG_LL2,
		   "Allocated LL2 Txq [Type %08x] with 0x%08x buffers\n",
		   p_ll2_info->input.conn_type,
		   p_ll2_info->input.tx_num_desc);

out:
	if (rc != ECORE_SUCCESS)
		DP_NOTICE(p_hwfn, false,
			  "Can't allocate memory for Tx LL2 with 0x%08x buffers\n",
			  p_ll2_info->input.tx_num_desc);
	return rc;
}

static enum _ecore_status_t
ecore_ll2_acquire_connection_ooo(struct ecore_hwfn *p_hwfn,
				 struct ecore_ll2_info *p_ll2_info, u16 mtu)
{
	struct ecore_ooo_buffer *p_buf = OSAL_NULL;
	u32 rx_buffer_size = 0;
	void *p_virt;
	u16 buf_idx;
	enum _ecore_status_t rc = ECORE_SUCCESS;

	if (p_ll2_info->input.conn_type != ECORE_LL2_TYPE_OOO)
		return rc;

	/* Correct number of requested OOO buffers if needed */
	if (!p_ll2_info->input.rx_num_ooo_buffers) {
		u16 num_desc = p_ll2_info->input.rx_num_desc;

		if (!num_desc)
			return ECORE_INVAL;
		p_ll2_info->input.rx_num_ooo_buffers = num_desc * 2;
	}

	/* TODO - use some defines for buffer size */
	rx_buffer_size = mtu + 14 + 4 + 8 + ETH_CACHE_LINE_SIZE;
	rx_buffer_size = (rx_buffer_size + ETH_CACHE_LINE_SIZE - 1) &
			 ~(ETH_CACHE_LINE_SIZE - 1);

	for (buf_idx = 0; buf_idx < p_ll2_info->input.rx_num_ooo_buffers;
	     buf_idx++) {
		p_buf = OSAL_ZALLOC(p_hwfn->p_dev, GFP_KERNEL, sizeof(*p_buf));
		if (!p_buf) {
			DP_NOTICE(p_hwfn, false,
				  "Failed to allocate ooo descriptor\n");
			rc = ECORE_NOMEM;
			goto out;
		}

		p_buf->rx_buffer_size = rx_buffer_size;
		p_virt = OSAL_DMA_ALLOC_COHERENT(p_hwfn->p_dev,
						 &p_buf->rx_buffer_phys_addr,
						 p_buf->rx_buffer_size);
		if (!p_virt) {
			DP_NOTICE(p_hwfn, false,
				  "Failed to allocate ooo buffer\n");
			OSAL_FREE(p_hwfn->p_dev, p_buf);
			rc = ECORE_NOMEM;
			goto out;
		}
		p_buf->rx_buffer_virt_addr = p_virt;
		ecore_ooo_put_free_buffer(p_hwfn->p_ooo_info, p_buf);
	}

	DP_VERBOSE(p_hwfn, ECORE_MSG_LL2,
		   "Allocated [%04x] LL2 OOO buffers [each of size 0x%08x]\n",
		   p_ll2_info->input.rx_num_ooo_buffers, rx_buffer_size);

out:
	return rc;
}

static enum _ecore_status_t
ecore_ll2_set_cbs(struct ecore_ll2_info *p_ll2_info,
		    const struct ecore_ll2_cbs *cbs)
{
	if (!cbs || (!cbs->rx_comp_cb ||
		     !cbs->rx_release_cb ||
		     !cbs->tx_comp_cb ||
		     !cbs->tx_release_cb ||
		     !cbs->cookie))
		return ECORE_INVAL;

	p_ll2_info->cbs.rx_comp_cb = cbs->rx_comp_cb;
	p_ll2_info->cbs.rx_release_cb = cbs->rx_release_cb;
	p_ll2_info->cbs.tx_comp_cb = cbs->tx_comp_cb;
	p_ll2_info->cbs.tx_release_cb = cbs->tx_release_cb;
	p_ll2_info->cbs.slowpath_cb = cbs->slowpath_cb;
	p_ll2_info->cbs.cookie = cbs->cookie;

	return ECORE_SUCCESS;
}

static enum core_error_handle
ecore_ll2_get_error_choice(enum ecore_ll2_error_handle err)
{
	switch (err) {
	case ECORE_LL2_DROP_PACKET:
		return LL2_DROP_PACKET;
	case ECORE_LL2_DO_NOTHING:
		return LL2_DO_NOTHING;
	case ECORE_LL2_ASSERT:
		return LL2_ASSERT;
	default:
		return LL2_DO_NOTHING;
	}
}

enum _ecore_status_t
ecore_ll2_acquire_connection(void *cxt,
			     struct ecore_ll2_acquire_data *data)
{
	struct ecore_hwfn *p_hwfn = (struct ecore_hwfn *)cxt;
	ecore_int_comp_cb_t comp_rx_cb, comp_tx_cb;
	struct ecore_ll2_info *p_ll2_info = OSAL_NULL;
	enum _ecore_status_t rc;
	u8 i, *p_tx_max;

	if (!data->p_connection_handle || !p_hwfn->p_ll2_info) {
		DP_NOTICE(p_hwfn, false, "Invalid connection handle, ll2_info not allocated\n");
		return ECORE_INVAL;
	}

	/* Find a free connection to be used */
	for (i = 0; (i < ECORE_MAX_NUM_OF_LL2_CONNECTIONS); i++) {
		OSAL_MUTEX_ACQUIRE(&p_hwfn->p_ll2_info[i].mutex);
		if (p_hwfn->p_ll2_info[i].b_active) {
			OSAL_MUTEX_RELEASE(&p_hwfn->p_ll2_info[i].mutex);
			continue;
		}

		p_hwfn->p_ll2_info[i].b_active = true;
		p_ll2_info = &p_hwfn->p_ll2_info[i];
		OSAL_MUTEX_RELEASE(&p_hwfn->p_ll2_info[i].mutex);
		break;
	}
	if (p_ll2_info == OSAL_NULL) {
		DP_NOTICE(p_hwfn, false, "No available ll2 connection\n");
		return ECORE_BUSY;
	}

	OSAL_MEMCPY(&p_ll2_info->input, &data->input,
		    sizeof(p_ll2_info->input));

	switch (data->input.tx_dest) {
	case ECORE_LL2_TX_DEST_NW:
		p_ll2_info->tx_dest = CORE_TX_DEST_NW;
		break;
	case ECORE_LL2_TX_DEST_LB:
		p_ll2_info->tx_dest = CORE_TX_DEST_LB;
		break;
	case ECORE_LL2_TX_DEST_DROP:
		p_ll2_info->tx_dest = CORE_TX_DEST_DROP;
		break;
	default:
		return ECORE_INVAL;
	}

	if ((data->input.conn_type == ECORE_LL2_TYPE_OOO) ||
	    data->input.secondary_queue)
		p_ll2_info->main_func_queue = false;
	else
		p_ll2_info->main_func_queue = true;

	/* Correct maximum number of Tx BDs */
	p_tx_max = &p_ll2_info->input.tx_max_bds_per_packet;
	if (*p_tx_max == 0)
		*p_tx_max = CORE_LL2_TX_MAX_BDS_PER_PACKET;
	else
		*p_tx_max = OSAL_MIN_T(u8, *p_tx_max,
				       CORE_LL2_TX_MAX_BDS_PER_PACKET);

	rc = ecore_ll2_set_cbs(p_ll2_info, data->cbs);
	if (rc) {
		DP_NOTICE(p_hwfn, false, "Invalid callback functions\n");
		goto q_allocate_fail;
	}

	rc = ecore_ll2_acquire_connection_rx(p_hwfn, p_ll2_info);
	if (rc != ECORE_SUCCESS) {
		DP_NOTICE(p_hwfn, false, "ll2 acquire rx connection failed\n");
		goto q_allocate_fail;
	}

	rc = ecore_ll2_acquire_connection_tx(p_hwfn, p_ll2_info);
	if (rc != ECORE_SUCCESS) {
		DP_NOTICE(p_hwfn, false, "ll2 acquire tx connection failed\n");
		goto q_allocate_fail;
	}

	rc = ecore_ll2_acquire_connection_ooo(p_hwfn, p_ll2_info,
					      data->input.mtu);
	if (rc != ECORE_SUCCESS) {
		DP_NOTICE(p_hwfn, false, "ll2 acquire ooo connection failed\n");
		goto q_allocate_fail;
	}

	/* Register callbacks for the Rx/Tx queues */
	if (data->input.conn_type == ECORE_LL2_TYPE_OOO) {
		comp_rx_cb = ecore_ll2_lb_rxq_completion;
		comp_tx_cb = ecore_ll2_lb_txq_completion;

	} else {
		comp_rx_cb = ecore_ll2_rxq_completion;
		comp_tx_cb = ecore_ll2_txq_completion;
	}

	if (data->input.rx_num_desc) {
		ecore_int_register_cb(p_hwfn, comp_rx_cb,
				      &p_hwfn->p_ll2_info[i],
				      &p_ll2_info->rx_queue.rx_sb_index,
				      &p_ll2_info->rx_queue.p_fw_cons);
		p_ll2_info->rx_queue.b_cb_registred   = true;
	}

	if (data->input.tx_num_desc) {
		ecore_int_register_cb(p_hwfn,
				      comp_tx_cb,
				      &p_hwfn->p_ll2_info[i],
				      &p_ll2_info->tx_queue.tx_sb_index,
				      &p_ll2_info->tx_queue.p_fw_cons);
		p_ll2_info->tx_queue.b_cb_registred   = true;
	}

	*(data->p_connection_handle) = i;
	return rc;

q_allocate_fail:
	ecore_ll2_release_connection(p_hwfn, i);
	return ECORE_NOMEM;
}

static enum _ecore_status_t ecore_ll2_establish_connection_rx(struct ecore_hwfn *p_hwfn,
							      struct ecore_ll2_info *p_ll2_conn)
{
	enum ecore_ll2_error_handle error_input;
	enum core_error_handle error_mode;
	u8 action_on_error = 0;

	if (!ECORE_LL2_RX_REGISTERED(p_ll2_conn))
		return ECORE_SUCCESS;

	DIRECT_REG_WR(p_hwfn, p_ll2_conn->rx_queue.set_prod_addr, 0x0);
	error_input = p_ll2_conn->input.ai_err_packet_too_big;
	error_mode = ecore_ll2_get_error_choice(error_input);
	SET_FIELD(action_on_error,
		  CORE_RX_ACTION_ON_ERROR_PACKET_TOO_BIG, error_mode);
	error_input = p_ll2_conn->input.ai_err_no_buf;
	error_mode = ecore_ll2_get_error_choice(error_input);
	SET_FIELD(action_on_error,
		  CORE_RX_ACTION_ON_ERROR_NO_BUFF, error_mode);

	return ecore_sp_ll2_rx_queue_start(p_hwfn, p_ll2_conn, action_on_error);
}

static void
ecore_ll2_establish_connection_ooo(struct ecore_hwfn *p_hwfn,
				   struct ecore_ll2_info *p_ll2_conn)
{
	if (p_ll2_conn->input.conn_type != ECORE_LL2_TYPE_OOO)
		return;

	ecore_ooo_release_all_isles(p_hwfn->p_ooo_info);
	ecore_ooo_submit_rx_buffers(p_hwfn, p_ll2_conn);
}

enum _ecore_status_t ecore_ll2_establish_connection(void *cxt,
						    u8 connection_handle)
{
	struct ecore_hwfn *p_hwfn = (struct ecore_hwfn *)cxt;
	struct e4_core_conn_context *p_cxt;
	struct ecore_ll2_info *p_ll2_conn;
	struct ecore_cxt_info cxt_info;
	struct ecore_ll2_rx_queue *p_rx;
	struct ecore_ll2_tx_queue *p_tx;
	struct ecore_ll2_tx_packet *p_pkt;
	struct ecore_ptt *p_ptt;
	enum _ecore_status_t rc = ECORE_NOTIMPL;
	u32 i, capacity;
	u32 desc_size;
	u8 qid;

	p_ptt = ecore_ptt_acquire(p_hwfn);
	if (!p_ptt)
		return ECORE_AGAIN;

	p_ll2_conn = ecore_ll2_handle_sanity_lock(p_hwfn, connection_handle);
	if (p_ll2_conn == OSAL_NULL) {
		rc = ECORE_INVAL;
		goto out;
	}

	p_rx = &p_ll2_conn->rx_queue;
	p_tx = &p_ll2_conn->tx_queue;

	ecore_chain_reset(&p_rx->rxq_chain);
	ecore_chain_reset(&p_rx->rcq_chain);
	OSAL_LIST_INIT(&p_rx->active_descq);
	OSAL_LIST_INIT(&p_rx->free_descq);
	OSAL_LIST_INIT(&p_rx->posting_descq);
	OSAL_SPIN_LOCK_INIT(&p_rx->lock);
	capacity = ecore_chain_get_capacity(&p_rx->rxq_chain);
	for (i = 0; i < capacity; i++)
		OSAL_LIST_PUSH_TAIL(&p_rx->descq_array[i].list_entry,
				    &p_rx->free_descq);
	*p_rx->p_fw_cons = 0;

	ecore_chain_reset(&p_tx->txq_chain);
	OSAL_LIST_INIT(&p_tx->active_descq);
	OSAL_LIST_INIT(&p_tx->free_descq);
	OSAL_LIST_INIT(&p_tx->sending_descq);
	OSAL_SPIN_LOCK_INIT(&p_tx->lock);
	capacity = ecore_chain_get_capacity(&p_tx->txq_chain);
	/* The size of the element in descq_array is flexible */
	desc_size = (sizeof(*p_pkt) +
		     (p_ll2_conn->input.tx_max_bds_per_packet - 1) *
		     sizeof(p_pkt->bds_set));

	for (i = 0; i < capacity; i++) {
		p_pkt = (struct ecore_ll2_tx_packet *)((u8 *)p_tx->descq_array +
						       desc_size*i);
		OSAL_LIST_PUSH_TAIL(&p_pkt->list_entry,
				    &p_tx->free_descq);
	}
	p_tx->cur_completing_bd_idx = 0;
	p_tx->bds_idx = 0;
	p_tx->b_completing_packet = false;
	p_tx->cur_send_packet = OSAL_NULL;
	p_tx->cur_send_frag_num = 0;
	p_tx->cur_completing_frag_num = 0;
	*p_tx->p_fw_cons = 0;

	rc = ecore_cxt_acquire_cid(p_hwfn, PROTOCOLID_CORE, &p_ll2_conn->cid);
	if (rc)
		goto out;
	cxt_info.iid = p_ll2_conn->cid;
	rc = ecore_cxt_get_cid_info(p_hwfn, &cxt_info);
	if (rc) {
		DP_NOTICE(p_hwfn, true, "Cannot find context info for cid=%d\n",
			  p_ll2_conn->cid);
		goto out;
	}

	p_cxt = cxt_info.p_cxt;

	/* @@@TBD we zero the context until we have ilt_reset implemented. */
	OSAL_MEM_ZERO(p_cxt, sizeof(*p_cxt));

	qid = ecore_ll2_handle_to_queue_id(p_hwfn, connection_handle);
	p_ll2_conn->queue_id = qid;
	p_ll2_conn->tx_stats_id = qid;
	p_rx->set_prod_addr = (u8 OSAL_IOMEM*)p_hwfn->regview +
					      GTT_BAR0_MAP_REG_TSDM_RAM +
					      TSTORM_LL2_RX_PRODS_OFFSET(qid);
	p_tx->doorbell_addr = (u8 OSAL_IOMEM*)p_hwfn->doorbells +
					      DB_ADDR(p_ll2_conn->cid,
						      DQ_DEMS_LEGACY);

	/* prepare db data */
	SET_FIELD(p_tx->db_msg.params, CORE_DB_DATA_DEST, DB_DEST_XCM);
	SET_FIELD(p_tx->db_msg.params, CORE_DB_DATA_AGG_CMD,
		  DB_AGG_CMD_SET);
	SET_FIELD(p_tx->db_msg.params, CORE_DB_DATA_AGG_VAL_SEL,
		  DQ_XCM_CORE_TX_BD_PROD_CMD);
	p_tx->db_msg.agg_flags = DQ_XCM_CORE_DQ_CF_CMD;

	rc = ecore_ll2_establish_connection_rx(p_hwfn, p_ll2_conn);
	if (rc)
		goto out;

	rc = ecore_sp_ll2_tx_queue_start(p_hwfn, p_ll2_conn);
	if (rc)
		goto out;

	if (!ECORE_IS_RDMA_PERSONALITY(p_hwfn))
		ecore_wr(p_hwfn, p_ptt, PRS_REG_USE_LIGHT_L2, 1);

	ecore_ll2_establish_connection_ooo(p_hwfn, p_ll2_conn);

	if (p_ll2_conn->input.conn_type == ECORE_LL2_TYPE_FCOE) {
		if (!OSAL_TEST_BIT(ECORE_MF_UFP_SPECIFIC,
				   &p_hwfn->p_dev->mf_bits))
			ecore_llh_add_protocol_filter(p_hwfn->p_dev, 0,
						      ECORE_LLH_FILTER_ETHERTYPE,
						      0x8906, 0);
		ecore_llh_add_protocol_filter(p_hwfn->p_dev, 0,
					      ECORE_LLH_FILTER_ETHERTYPE,
					      0x8914, 0);
	}

out:
	ecore_ptt_release(p_hwfn, p_ptt);

	return rc;
}

static void ecore_ll2_post_rx_buffer_notify_fw(struct ecore_hwfn *p_hwfn,
					       struct ecore_ll2_rx_queue *p_rx,
					       struct ecore_ll2_rx_packet *p_curp)
{
	struct ecore_ll2_rx_packet *p_posting_packet = OSAL_NULL;
	struct core_ll2_rx_prod rx_prod = {0, 0, 0};
	bool b_notify_fw = false;
	u16 bd_prod, cq_prod;

	/* This handles the flushing of already posted buffers */
	while (!OSAL_LIST_IS_EMPTY(&p_rx->posting_descq)) {
		p_posting_packet = OSAL_LIST_FIRST_ENTRY(&p_rx->posting_descq,
							 struct ecore_ll2_rx_packet,
							 list_entry);
#if defined(_NTDDK_)
#pragma warning(suppress : 6011 28182)
#endif
		OSAL_LIST_REMOVE_ENTRY(&p_posting_packet->list_entry, &p_rx->posting_descq);
		OSAL_LIST_PUSH_TAIL(&p_posting_packet->list_entry, &p_rx->active_descq);
		b_notify_fw = true;
	}

	/* This handles the supplied packet [if there is one] */
	if (p_curp) {
		OSAL_LIST_PUSH_TAIL(&p_curp->list_entry,
				    &p_rx->active_descq);
		b_notify_fw = true;
	}

	if (!b_notify_fw)
		return;

	bd_prod = ecore_chain_get_prod_idx(&p_rx->rxq_chain);
	cq_prod = ecore_chain_get_prod_idx(&p_rx->rcq_chain);
	rx_prod.bd_prod = OSAL_CPU_TO_LE16(bd_prod);
	rx_prod.cqe_prod = OSAL_CPU_TO_LE16(cq_prod);
	DIRECT_REG_WR(p_hwfn, p_rx->set_prod_addr, *((u32 *)&rx_prod));
}

enum _ecore_status_t ecore_ll2_post_rx_buffer(void *cxt,
					      u8 connection_handle,
					      dma_addr_t addr,
					      u16 buf_len,
					      void *cookie,
					      u8 notify_fw)
{
	struct ecore_hwfn *p_hwfn = (struct ecore_hwfn *)cxt;
	struct core_rx_bd_with_buff_len *p_curb = OSAL_NULL;
	struct ecore_ll2_rx_packet *p_curp = OSAL_NULL;
	struct ecore_ll2_info *p_ll2_conn;
	struct ecore_ll2_rx_queue *p_rx;
	unsigned long flags;
	void *p_data;
	enum _ecore_status_t rc = ECORE_SUCCESS;

	p_ll2_conn = ecore_ll2_handle_sanity(p_hwfn, connection_handle);
	if (p_ll2_conn == OSAL_NULL)
		return ECORE_INVAL;
	p_rx = &p_ll2_conn->rx_queue;
	if (p_rx->set_prod_addr == OSAL_NULL)
		return ECORE_IO;

	OSAL_SPIN_LOCK_IRQSAVE(&p_rx->lock, flags);
	if (!OSAL_LIST_IS_EMPTY(&p_rx->free_descq))
		p_curp = OSAL_LIST_FIRST_ENTRY(&p_rx->free_descq,
					       struct ecore_ll2_rx_packet,
					       list_entry);
	if (p_curp) {
		if (ecore_chain_get_elem_left(&p_rx->rxq_chain) &&
		    ecore_chain_get_elem_left(&p_rx->rcq_chain)) {
			p_data = ecore_chain_produce(&p_rx->rxq_chain);
			p_curb = (struct core_rx_bd_with_buff_len *)p_data;
			ecore_chain_produce(&p_rx->rcq_chain);
		}
	}

	/* If we're lacking entires, let's try to flush buffers to FW */
	if (!p_curp || !p_curb) {
		rc =  ECORE_BUSY;
		p_curp = OSAL_NULL;
		goto out_notify;
	}

	/* We have an Rx packet we can fill */
	DMA_REGPAIR_LE(p_curb->addr, addr);
	p_curb->buff_length = OSAL_CPU_TO_LE16(buf_len);
	p_curp->rx_buf_addr = addr;
	p_curp->cookie = cookie;
	p_curp->rxq_bd = p_curb;
	p_curp->buf_length = buf_len;
	OSAL_LIST_REMOVE_ENTRY(&p_curp->list_entry,
			       &p_rx->free_descq);

	/* Check if we only want to enqueue this packet without informing FW */
	if (!notify_fw) {
		OSAL_LIST_PUSH_TAIL(&p_curp->list_entry,
				    &p_rx->posting_descq);
		goto out;
	}

out_notify:
	ecore_ll2_post_rx_buffer_notify_fw(p_hwfn, p_rx, p_curp);
out:
	OSAL_SPIN_UNLOCK_IRQSAVE(&p_rx->lock, flags);
	return rc;
}

static void ecore_ll2_prepare_tx_packet_set(struct ecore_ll2_tx_queue *p_tx,
					    struct ecore_ll2_tx_packet *p_curp,
					    struct ecore_ll2_tx_pkt_info *pkt,
					    u8 notify_fw)
{
	OSAL_LIST_REMOVE_ENTRY(&p_curp->list_entry,
			       &p_tx->free_descq);
	p_curp->cookie = pkt->cookie;
	p_curp->bd_used = pkt->num_of_bds;
	p_curp->notify_fw = notify_fw;
	p_tx->cur_send_packet = p_curp;
	p_tx->cur_send_frag_num = 0;

	p_curp->bds_set[p_tx->cur_send_frag_num].tx_frag = pkt->first_frag;
	p_curp->bds_set[p_tx->cur_send_frag_num].frag_len = pkt->first_frag_len;
	p_tx->cur_send_frag_num++;
}

static void ecore_ll2_prepare_tx_packet_set_bd(
		struct ecore_hwfn *p_hwfn,
		struct ecore_ll2_info *p_ll2,
		struct ecore_ll2_tx_packet *p_curp,
		struct ecore_ll2_tx_pkt_info *pkt)
{
	struct ecore_chain *p_tx_chain = &p_ll2->tx_queue.txq_chain;
	u16 prod_idx = ecore_chain_get_prod_idx(p_tx_chain);
	struct core_tx_bd *start_bd = OSAL_NULL;
	enum core_roce_flavor_type roce_flavor;
	enum core_tx_dest tx_dest;
	u16 bd_data = 0, frag_idx;

	roce_flavor = (pkt->ecore_roce_flavor == ECORE_LL2_ROCE) ?
		CORE_ROCE : CORE_RROCE;

	switch (pkt->tx_dest) {
	case ECORE_LL2_TX_DEST_NW:
		tx_dest = CORE_TX_DEST_NW;
		break;
	case ECORE_LL2_TX_DEST_LB:
		tx_dest = CORE_TX_DEST_LB;
		break;
	case ECORE_LL2_TX_DEST_DROP:
		tx_dest = CORE_TX_DEST_DROP;
		break;
	default:
		tx_dest = CORE_TX_DEST_LB;
		break;
	}

	start_bd = (struct core_tx_bd*)ecore_chain_produce(p_tx_chain);

	if (ECORE_IS_IWARP_PERSONALITY(p_hwfn) &&
	    (p_ll2->input.conn_type == ECORE_LL2_TYPE_OOO)) {
		start_bd->nw_vlan_or_lb_echo =
			OSAL_CPU_TO_LE16(IWARP_LL2_IN_ORDER_TX_QUEUE);
	} else {
		start_bd->nw_vlan_or_lb_echo = OSAL_CPU_TO_LE16(pkt->vlan);
		if (OSAL_TEST_BIT(ECORE_MF_UFP_SPECIFIC, &p_hwfn->p_dev->mf_bits) &&
		    (p_ll2->input.conn_type == ECORE_LL2_TYPE_FCOE))
			pkt->remove_stag = true;
	}

	SET_FIELD(start_bd->bitfield1, CORE_TX_BD_L4_HDR_OFFSET_W,
		  OSAL_CPU_TO_LE16(pkt->l4_hdr_offset_w));
	SET_FIELD(start_bd->bitfield1, CORE_TX_BD_TX_DST, tx_dest);
	bd_data |= pkt->bd_flags;
	SET_FIELD(bd_data, CORE_TX_BD_DATA_START_BD, 0x1);
	SET_FIELD(bd_data, CORE_TX_BD_DATA_NBDS, pkt->num_of_bds);
	SET_FIELD(bd_data, CORE_TX_BD_DATA_ROCE_FLAV, roce_flavor);
	SET_FIELD(bd_data, CORE_TX_BD_DATA_IP_CSUM, !!(pkt->enable_ip_cksum));
	SET_FIELD(bd_data, CORE_TX_BD_DATA_L4_CSUM, !!(pkt->enable_l4_cksum));
	SET_FIELD(bd_data, CORE_TX_BD_DATA_IP_LEN, !!(pkt->calc_ip_len));
	SET_FIELD(bd_data, CORE_TX_BD_DATA_DISABLE_STAG_INSERTION,
		  !!(pkt->remove_stag));

	start_bd->bd_data.as_bitfield = OSAL_CPU_TO_LE16(bd_data);
	DMA_REGPAIR_LE(start_bd->addr, pkt->first_frag);
	start_bd->nbytes = OSAL_CPU_TO_LE16(pkt->first_frag_len);

	DP_VERBOSE(p_hwfn, (ECORE_MSG_TX_QUEUED | ECORE_MSG_LL2),
		   "LL2 [q 0x%02x cid 0x%08x type 0x%08x] Tx Producer at [0x%04x] - set with a %04x bytes %02x BDs buffer at %08x:%08x\n",
		   p_ll2->queue_id, p_ll2->cid, p_ll2->input.conn_type,
		   prod_idx, pkt->first_frag_len, pkt->num_of_bds,
		   OSAL_LE32_TO_CPU(start_bd->addr.hi),
		   OSAL_LE32_TO_CPU(start_bd->addr.lo));

	if (p_ll2->tx_queue.cur_send_frag_num == pkt->num_of_bds)
		return;

	/* Need to provide the packet with additional BDs for frags */
	for (frag_idx = p_ll2->tx_queue.cur_send_frag_num;
	     frag_idx < pkt->num_of_bds; frag_idx++) {
		struct core_tx_bd **p_bd = &p_curp->bds_set[frag_idx].txq_bd;

		*p_bd = (struct core_tx_bd *)ecore_chain_produce(p_tx_chain);
		(*p_bd)->bd_data.as_bitfield = 0;
		(*p_bd)->bitfield1 = 0;
		p_curp->bds_set[frag_idx].tx_frag = 0;
		p_curp->bds_set[frag_idx].frag_len = 0;
	}
}

/* This should be called while the Txq spinlock is being held */
static void ecore_ll2_tx_packet_notify(struct ecore_hwfn *p_hwfn,
				       struct ecore_ll2_info *p_ll2_conn)
{
	bool b_notify = p_ll2_conn->tx_queue.cur_send_packet->notify_fw;
	struct ecore_ll2_tx_queue *p_tx = &p_ll2_conn->tx_queue;
	struct ecore_ll2_tx_packet *p_pkt = OSAL_NULL;
	u16 bd_prod;

	/* If there are missing BDs, don't do anything now */
	if (p_ll2_conn->tx_queue.cur_send_frag_num !=
	    p_ll2_conn->tx_queue.cur_send_packet->bd_used)
		return;


	/* Push the current packet to the list and clean after it */
	OSAL_LIST_PUSH_TAIL(&p_ll2_conn->tx_queue.cur_send_packet->list_entry,
			    &p_ll2_conn->tx_queue.sending_descq);
	p_ll2_conn->tx_queue.cur_send_packet = OSAL_NULL;
	p_ll2_conn->tx_queue.cur_send_frag_num = 0;

	/* Notify FW of packet only if requested to */
	if (!b_notify)
		return;

	bd_prod = ecore_chain_get_prod_idx(&p_ll2_conn->tx_queue.txq_chain);

	while (!OSAL_LIST_IS_EMPTY(&p_tx->sending_descq)) {
		p_pkt = OSAL_LIST_FIRST_ENTRY(&p_tx->sending_descq,
					      struct ecore_ll2_tx_packet,
					      list_entry);
		if (p_pkt == OSAL_NULL)
			break;
#if defined(_NTDDK_)
#pragma warning(suppress : 6011 28182)
#endif
		OSAL_LIST_REMOVE_ENTRY(&p_pkt->list_entry,
				       &p_tx->sending_descq);
		OSAL_LIST_PUSH_TAIL(&p_pkt->list_entry, &p_tx->active_descq);
	}

	p_tx->db_msg.spq_prod = OSAL_CPU_TO_LE16(bd_prod);

	/* Make sure the BDs data is updated before ringing the doorbell */
	OSAL_WMB(p_hwfn->p_dev);

	//DIRECT_REG_WR(p_hwfn, p_tx->doorbell_addr, *((u32 *)&p_tx->db_msg));
	DIRECT_REG_WR_DB(p_hwfn, p_tx->doorbell_addr, *((u32 *)&p_tx->db_msg));

	DP_VERBOSE(p_hwfn, (ECORE_MSG_TX_QUEUED | ECORE_MSG_LL2),
		   "LL2 [q 0x%02x cid 0x%08x type 0x%08x] Doorbelled [producer 0x%04x]\n",
		   p_ll2_conn->queue_id, p_ll2_conn->cid,
		   p_ll2_conn->input.conn_type,
		   p_tx->db_msg.spq_prod);
}

enum _ecore_status_t ecore_ll2_prepare_tx_packet(
		void *cxt,
		u8 connection_handle,
		struct ecore_ll2_tx_pkt_info *pkt,
		bool notify_fw)
{
	struct ecore_hwfn *p_hwfn = (struct ecore_hwfn *)cxt;
	struct ecore_ll2_tx_packet *p_curp = OSAL_NULL;
	struct ecore_ll2_info *p_ll2_conn = OSAL_NULL;
	struct ecore_ll2_tx_queue *p_tx;
	struct ecore_chain *p_tx_chain;
	unsigned long flags;
	enum _ecore_status_t rc = ECORE_SUCCESS;

	p_ll2_conn = ecore_ll2_handle_sanity(p_hwfn, connection_handle);
	if (p_ll2_conn == OSAL_NULL)
		return ECORE_INVAL;
	p_tx = &p_ll2_conn->tx_queue;
	p_tx_chain = &p_tx->txq_chain;

	if (pkt->num_of_bds > p_ll2_conn->input.tx_max_bds_per_packet)
		return ECORE_IO; /* coalescing is requireed */

	OSAL_SPIN_LOCK_IRQSAVE(&p_tx->lock, flags);
	if (p_tx->cur_send_packet) {
		rc = ECORE_EXISTS;
		goto out;
	}

	/* Get entry, but only if we have tx elements for it */
	if (!OSAL_LIST_IS_EMPTY(&p_tx->free_descq))
		p_curp = OSAL_LIST_FIRST_ENTRY(&p_tx->free_descq,
					       struct ecore_ll2_tx_packet,
					       list_entry);
	if (p_curp && ecore_chain_get_elem_left(p_tx_chain) < pkt->num_of_bds)
		p_curp = OSAL_NULL;

	if (!p_curp) {
		rc = ECORE_BUSY;
		goto out;
	}

	/* Prepare packet and BD, and perhaps send a doorbell to FW */
	ecore_ll2_prepare_tx_packet_set(p_tx, p_curp, pkt, notify_fw);

	ecore_ll2_prepare_tx_packet_set_bd(p_hwfn, p_ll2_conn, p_curp,
					   pkt);

	ecore_ll2_tx_packet_notify(p_hwfn, p_ll2_conn);

out:
	OSAL_SPIN_UNLOCK_IRQSAVE(&p_tx->lock, flags);
	return rc;
}

enum _ecore_status_t ecore_ll2_set_fragment_of_tx_packet(void *cxt,
							 u8 connection_handle,
							 dma_addr_t addr,
							 u16 nbytes)
{
	struct ecore_ll2_tx_packet *p_cur_send_packet = OSAL_NULL;
	struct ecore_hwfn *p_hwfn = (struct ecore_hwfn *)cxt;
	struct ecore_ll2_info *p_ll2_conn = OSAL_NULL;
	u16 cur_send_frag_num = 0;
	struct core_tx_bd *p_bd;
	unsigned long flags;

	p_ll2_conn = ecore_ll2_handle_sanity(p_hwfn, connection_handle);
	if (p_ll2_conn == OSAL_NULL)
		return ECORE_INVAL;

	if (!p_ll2_conn->tx_queue.cur_send_packet)
		return ECORE_INVAL;

	p_cur_send_packet = p_ll2_conn->tx_queue.cur_send_packet;
	cur_send_frag_num = p_ll2_conn->tx_queue.cur_send_frag_num;

	if (cur_send_frag_num >= p_cur_send_packet->bd_used)
		return ECORE_INVAL;

	/* Fill the BD information, and possibly notify FW */
	p_bd = p_cur_send_packet->bds_set[cur_send_frag_num].txq_bd;
	DMA_REGPAIR_LE(p_bd->addr, addr);
	p_bd->nbytes = OSAL_CPU_TO_LE16(nbytes);
	p_cur_send_packet->bds_set[cur_send_frag_num].tx_frag = addr;
	p_cur_send_packet->bds_set[cur_send_frag_num].frag_len = nbytes;

	p_ll2_conn->tx_queue.cur_send_frag_num++;

	OSAL_SPIN_LOCK_IRQSAVE(&p_ll2_conn->tx_queue.lock, flags);
	ecore_ll2_tx_packet_notify(p_hwfn, p_ll2_conn);
	OSAL_SPIN_UNLOCK_IRQSAVE(&p_ll2_conn->tx_queue.lock, flags);

	return ECORE_SUCCESS;
}

enum _ecore_status_t ecore_ll2_terminate_connection(void *cxt,
						    u8 connection_handle)
{
	struct ecore_hwfn *p_hwfn = (struct ecore_hwfn *)cxt;
	struct ecore_ll2_info *p_ll2_conn = OSAL_NULL;
	enum _ecore_status_t rc = ECORE_NOTIMPL;
	struct ecore_ptt *p_ptt;

	p_ptt = ecore_ptt_acquire(p_hwfn);
	if (!p_ptt)
		return ECORE_AGAIN;

	p_ll2_conn = ecore_ll2_handle_sanity_lock(p_hwfn, connection_handle);
	if (p_ll2_conn == OSAL_NULL) {
		rc = ECORE_INVAL;
		goto out;
	}

	/* Stop Tx & Rx of connection, if needed */
	if (ECORE_LL2_TX_REGISTERED(p_ll2_conn)) {
		rc = ecore_sp_ll2_tx_queue_stop(p_hwfn, p_ll2_conn);
		if (rc != ECORE_SUCCESS)
			goto out;
		ecore_ll2_txq_flush(p_hwfn, connection_handle);
	}

	if (ECORE_LL2_RX_REGISTERED(p_ll2_conn)) {
		rc = ecore_sp_ll2_rx_queue_stop(p_hwfn, p_ll2_conn);
		if (rc)
			goto out;
		ecore_ll2_rxq_flush(p_hwfn, connection_handle);
	}

	if (p_ll2_conn->input.conn_type == ECORE_LL2_TYPE_OOO)
		ecore_ooo_release_all_isles(p_hwfn->p_ooo_info);

	if (p_ll2_conn->input.conn_type == ECORE_LL2_TYPE_FCOE) {
		if (!OSAL_TEST_BIT(ECORE_MF_UFP_SPECIFIC,
				   &p_hwfn->p_dev->mf_bits))
			ecore_llh_remove_protocol_filter(p_hwfn->p_dev, 0,
							 ECORE_LLH_FILTER_ETHERTYPE,
							 0x8906, 0);
		ecore_llh_remove_protocol_filter(p_hwfn->p_dev, 0,
						 ECORE_LLH_FILTER_ETHERTYPE,
						 0x8914, 0);
	}

out:
	ecore_ptt_release(p_hwfn, p_ptt);

	return rc;
}

static void ecore_ll2_release_connection_ooo(struct ecore_hwfn *p_hwfn,
					     struct ecore_ll2_info *p_ll2_conn)
{
	struct ecore_ooo_buffer *p_buffer;

	if (p_ll2_conn->input.conn_type != ECORE_LL2_TYPE_OOO)
		return;

	ecore_ooo_release_all_isles(p_hwfn->p_ooo_info);
	while ((p_buffer = ecore_ooo_get_free_buffer(p_hwfn->p_ooo_info))) {
		OSAL_DMA_FREE_COHERENT(p_hwfn->p_dev,
				       p_buffer->rx_buffer_virt_addr,
				       p_buffer->rx_buffer_phys_addr,
				       p_buffer->rx_buffer_size);
		OSAL_FREE(p_hwfn->p_dev, p_buffer);
	}
}

void ecore_ll2_release_connection(void *cxt,
				  u8 connection_handle)
{
	struct ecore_hwfn *p_hwfn = (struct ecore_hwfn *)cxt;
	struct ecore_ll2_info *p_ll2_conn = OSAL_NULL;

	p_ll2_conn = ecore_ll2_handle_sanity(p_hwfn, connection_handle);
	if (p_ll2_conn == OSAL_NULL)
		return;

	if (ECORE_LL2_RX_REGISTERED(p_ll2_conn)) {
		p_ll2_conn->rx_queue.b_cb_registred = false;
		ecore_int_unregister_cb(p_hwfn,
					p_ll2_conn->rx_queue.rx_sb_index);
	}

	if (ECORE_LL2_TX_REGISTERED(p_ll2_conn)) {
		p_ll2_conn->tx_queue.b_cb_registred = false;
		ecore_int_unregister_cb(p_hwfn,
					p_ll2_conn->tx_queue.tx_sb_index);
	}

	OSAL_FREE(p_hwfn->p_dev, p_ll2_conn->tx_queue.descq_array);
	ecore_chain_free(p_hwfn->p_dev, &p_ll2_conn->tx_queue.txq_chain);

	OSAL_FREE(p_hwfn->p_dev, p_ll2_conn->rx_queue.descq_array);
	ecore_chain_free(p_hwfn->p_dev, &p_ll2_conn->rx_queue.rxq_chain);
	ecore_chain_free(p_hwfn->p_dev, &p_ll2_conn->rx_queue.rcq_chain);

	ecore_cxt_release_cid(p_hwfn, p_ll2_conn->cid);

	ecore_ll2_release_connection_ooo(p_hwfn, p_ll2_conn);

	OSAL_MUTEX_ACQUIRE(&p_ll2_conn->mutex);
	p_ll2_conn->b_active = false;
	OSAL_MUTEX_RELEASE(&p_ll2_conn->mutex);
}

/* ECORE LL2: internal functions */

enum _ecore_status_t ecore_ll2_alloc(struct ecore_hwfn *p_hwfn)
{
	struct ecore_ll2_info *p_ll2_info;
	u8 i;

	/* Allocate LL2's set struct */
	p_ll2_info = OSAL_ZALLOC(p_hwfn->p_dev, GFP_KERNEL,
				 sizeof(struct ecore_ll2_info) *
				 ECORE_MAX_NUM_OF_LL2_CONNECTIONS);
	if (!p_ll2_info) {
		DP_NOTICE(p_hwfn, false,
			  "Failed to allocate `struct ecore_ll2'\n");
		return ECORE_NOMEM;
	}

	p_hwfn->p_ll2_info = p_ll2_info;

	for (i = 0; i < ECORE_MAX_NUM_OF_LL2_CONNECTIONS; i++) {
#ifdef CONFIG_ECORE_LOCK_ALLOC
		if (OSAL_MUTEX_ALLOC(p_hwfn, &p_ll2_info[i].mutex))
			goto handle_err;
		if (OSAL_SPIN_LOCK_ALLOC(p_hwfn, &p_ll2_info[i].rx_queue.lock))
			goto handle_err;
		if (OSAL_SPIN_LOCK_ALLOC(p_hwfn, &p_ll2_info[i].tx_queue.lock))
			goto handle_err;
#endif
		p_ll2_info[i].my_id = i;
	}

	return ECORE_SUCCESS;
#ifdef CONFIG_ECORE_LOCK_ALLOC
handle_err:
	ecore_ll2_free(p_hwfn);
	return ECORE_NOMEM;
#endif
}

void ecore_ll2_setup(struct ecore_hwfn *p_hwfn)
{
	int  i;

	for (i = 0; i < ECORE_MAX_NUM_OF_LL2_CONNECTIONS; i++)
		OSAL_MUTEX_INIT(&p_hwfn->p_ll2_info[i].mutex);
}

void ecore_ll2_free(struct ecore_hwfn *p_hwfn)
{
#ifdef CONFIG_ECORE_LOCK_ALLOC
	int i;
#endif
	if (!p_hwfn->p_ll2_info)
		return;

#ifdef CONFIG_ECORE_LOCK_ALLOC
	for (i = 0; i < ECORE_MAX_NUM_OF_LL2_CONNECTIONS; i++) {
		OSAL_SPIN_LOCK_DEALLOC(&p_hwfn->p_ll2_info[i].rx_queue.lock);
		OSAL_SPIN_LOCK_DEALLOC(&p_hwfn->p_ll2_info[i].tx_queue.lock);
		OSAL_MUTEX_DEALLOC(&p_hwfn->p_ll2_info[i].mutex);
	}
#endif
	OSAL_FREE(p_hwfn->p_dev, p_hwfn->p_ll2_info);
	p_hwfn->p_ll2_info = OSAL_NULL;
}

static void _ecore_ll2_get_port_stats(struct ecore_hwfn *p_hwfn,
				      struct ecore_ptt *p_ptt,
				      struct ecore_ll2_stats *p_stats)
{
	struct core_ll2_port_stats port_stats;

	OSAL_MEMSET(&port_stats, 0, sizeof(port_stats));
	ecore_memcpy_from(p_hwfn, p_ptt, &port_stats,
			  BAR0_MAP_REG_TSDM_RAM +
			  TSTORM_LL2_PORT_STAT_OFFSET(MFW_PORT(p_hwfn)),
			  sizeof(port_stats));

	p_stats->gsi_invalid_hdr +=
		HILO_64_REGPAIR(port_stats.gsi_invalid_hdr);
	p_stats->gsi_invalid_pkt_length +=
		HILO_64_REGPAIR(port_stats.gsi_invalid_pkt_length);
	p_stats->gsi_unsupported_pkt_typ +=
		HILO_64_REGPAIR(port_stats.gsi_unsupported_pkt_typ);
	p_stats->gsi_crcchksm_error +=
		HILO_64_REGPAIR(port_stats.gsi_crcchksm_error);
}

static void _ecore_ll2_get_tstats(struct ecore_hwfn *p_hwfn,
				  struct ecore_ptt *p_ptt,
				  struct ecore_ll2_info *p_ll2_conn,
				  struct ecore_ll2_stats *p_stats)
{
	struct core_ll2_tstorm_per_queue_stat tstats;
	u8 qid = p_ll2_conn->queue_id;
	u32 tstats_addr;

	OSAL_MEMSET(&tstats, 0, sizeof(tstats));
	tstats_addr = BAR0_MAP_REG_TSDM_RAM +
		      CORE_LL2_TSTORM_PER_QUEUE_STAT_OFFSET(qid);
	ecore_memcpy_from(p_hwfn, p_ptt, &tstats,
			  tstats_addr,
			  sizeof(tstats));

	p_stats->packet_too_big_discard +=
		HILO_64_REGPAIR(tstats.packet_too_big_discard);
	p_stats->no_buff_discard +=
		HILO_64_REGPAIR(tstats.no_buff_discard);
}

static void _ecore_ll2_get_ustats(struct ecore_hwfn *p_hwfn,
				  struct ecore_ptt *p_ptt,
				  struct ecore_ll2_info *p_ll2_conn,
				  struct ecore_ll2_stats *p_stats)
{
	struct core_ll2_ustorm_per_queue_stat ustats;
	u8 qid = p_ll2_conn->queue_id;
	u32 ustats_addr;

	OSAL_MEMSET(&ustats, 0, sizeof(ustats));
	ustats_addr = BAR0_MAP_REG_USDM_RAM +
		      CORE_LL2_USTORM_PER_QUEUE_STAT_OFFSET(qid);
	ecore_memcpy_from(p_hwfn, p_ptt, &ustats,
			  ustats_addr,
			  sizeof(ustats));

	p_stats->rcv_ucast_bytes += HILO_64_REGPAIR(ustats.rcv_ucast_bytes);
	p_stats->rcv_mcast_bytes += HILO_64_REGPAIR(ustats.rcv_mcast_bytes);
	p_stats->rcv_bcast_bytes += HILO_64_REGPAIR(ustats.rcv_bcast_bytes);
	p_stats->rcv_ucast_pkts += HILO_64_REGPAIR(ustats.rcv_ucast_pkts);
	p_stats->rcv_mcast_pkts += HILO_64_REGPAIR(ustats.rcv_mcast_pkts);
	p_stats->rcv_bcast_pkts += HILO_64_REGPAIR(ustats.rcv_bcast_pkts);
}

static void _ecore_ll2_get_pstats(struct ecore_hwfn *p_hwfn,
				  struct ecore_ptt *p_ptt,
				  struct ecore_ll2_info *p_ll2_conn,
				  struct ecore_ll2_stats *p_stats)
{
	struct core_ll2_pstorm_per_queue_stat pstats;
	u8 stats_id = p_ll2_conn->tx_stats_id;
	u32 pstats_addr;

	OSAL_MEMSET(&pstats, 0, sizeof(pstats));
	pstats_addr = BAR0_MAP_REG_PSDM_RAM +
		      CORE_LL2_PSTORM_PER_QUEUE_STAT_OFFSET(stats_id);
	ecore_memcpy_from(p_hwfn, p_ptt, &pstats,
			  pstats_addr,
			  sizeof(pstats));

	p_stats->sent_ucast_bytes += HILO_64_REGPAIR(pstats.sent_ucast_bytes);
	p_stats->sent_mcast_bytes += HILO_64_REGPAIR(pstats.sent_mcast_bytes);
	p_stats->sent_bcast_bytes += HILO_64_REGPAIR(pstats.sent_bcast_bytes);
	p_stats->sent_ucast_pkts += HILO_64_REGPAIR(pstats.sent_ucast_pkts);
	p_stats->sent_mcast_pkts += HILO_64_REGPAIR(pstats.sent_mcast_pkts);
	p_stats->sent_bcast_pkts += HILO_64_REGPAIR(pstats.sent_bcast_pkts);
}

enum _ecore_status_t __ecore_ll2_get_stats(void *cxt,
					   u8 connection_handle,
					   struct ecore_ll2_stats *p_stats)
{
	struct ecore_hwfn *p_hwfn = (struct ecore_hwfn *)cxt;
	struct ecore_ll2_info *p_ll2_conn = OSAL_NULL;
	struct ecore_ptt *p_ptt;

	if ((connection_handle >= ECORE_MAX_NUM_OF_LL2_CONNECTIONS) ||
	    !p_hwfn->p_ll2_info) {
		return ECORE_INVAL;
	}

	p_ll2_conn = &p_hwfn->p_ll2_info[connection_handle];

	p_ptt = ecore_ptt_acquire(p_hwfn);
	if (!p_ptt) {
		DP_ERR(p_hwfn, "Failed to acquire ptt\n");
		return ECORE_INVAL;
	}

	if (p_ll2_conn->input.gsi_enable)
		_ecore_ll2_get_port_stats(p_hwfn, p_ptt, p_stats);

	_ecore_ll2_get_tstats(p_hwfn, p_ptt, p_ll2_conn, p_stats);

	_ecore_ll2_get_ustats(p_hwfn, p_ptt, p_ll2_conn, p_stats);

	if (p_ll2_conn->tx_stats_en)
		_ecore_ll2_get_pstats(p_hwfn, p_ptt, p_ll2_conn, p_stats);

	ecore_ptt_release(p_hwfn, p_ptt);

	return ECORE_SUCCESS;
}

enum _ecore_status_t ecore_ll2_get_stats(void *cxt,
					 u8 connection_handle,
					 struct ecore_ll2_stats	*p_stats)
{
	OSAL_MEMSET(p_stats, 0, sizeof(*p_stats));

	return __ecore_ll2_get_stats(cxt, connection_handle, p_stats);
}

/**/

#ifdef _NTDDK_
#pragma warning(pop)
#endif
