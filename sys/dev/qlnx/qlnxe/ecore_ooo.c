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
 * File : ecore_ooo.c
 */
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "bcm_osal.h"

#include "ecore.h"
#include "ecore_status.h"
#include "ecore_ll2.h"
#include "ecore_ooo.h"
#include "ecore_iscsi.h"
#include "ecore_cxt.h"
/*
 * Static OOO functions
 */

static struct ecore_ooo_archipelago *
ecore_ooo_seek_archipelago(struct ecore_ooo_info *p_ooo_info, u32 cid)
{
	u32 idx = (cid & 0xffff) - p_ooo_info->cid_base;
	struct ecore_ooo_archipelago *p_archipelago;

	if (idx >= p_ooo_info->max_num_archipelagos)
		return OSAL_NULL;

	p_archipelago = &p_ooo_info->p_archipelagos_mem[idx];

	if (OSAL_LIST_IS_EMPTY(&p_archipelago->isles_list))
		return OSAL_NULL;

	return p_archipelago;
}

static struct ecore_ooo_isle *ecore_ooo_seek_isle(struct ecore_hwfn *p_hwfn,
						  struct ecore_ooo_info *p_ooo_info,
						  u32 cid, u8 isle)
{
	struct ecore_ooo_archipelago *p_archipelago = OSAL_NULL;
	struct ecore_ooo_isle *p_isle = OSAL_NULL;
	u8 the_num_of_isle = 1;

	p_archipelago = ecore_ooo_seek_archipelago(p_ooo_info, cid);
	if (!p_archipelago) {
		DP_NOTICE(p_hwfn, true,
			 "Connection %d is not found in OOO list\n", cid);
		return OSAL_NULL;
	}

	OSAL_LIST_FOR_EACH_ENTRY(p_isle,
				 &p_archipelago->isles_list,
				 list_entry, struct ecore_ooo_isle) {
		if (the_num_of_isle == isle)
			return p_isle;
		the_num_of_isle++;
	}

	return OSAL_NULL;
}

void ecore_ooo_save_history_entry(struct ecore_ooo_info *p_ooo_info,
				  struct ooo_opaque *p_cqe)
{
	struct ecore_ooo_history *p_history = &p_ooo_info->ooo_history;

	if (p_history->head_idx == p_history->num_of_cqes)
			p_history->head_idx = 0;
	p_history->p_cqes[p_history->head_idx] = *p_cqe;
	p_history->head_idx++;
}

//#ifdef CONFIG_ECORE_ISCSI
#if defined(CONFIG_ECORE_ISCSI) || defined(CONFIG_ECORE_IWARP)
enum _ecore_status_t ecore_ooo_alloc(struct ecore_hwfn *p_hwfn)
{
	u16 max_num_archipelagos = 0, cid_base;
	struct ecore_ooo_info *p_ooo_info;
	u16 max_num_isles = 0;
	u32 i;

	switch (p_hwfn->hw_info.personality) {
	case ECORE_PCI_ISCSI:
		max_num_archipelagos =
			p_hwfn->pf_params.iscsi_pf_params.num_cons;
		cid_base =(u16)ecore_cxt_get_proto_cid_start(p_hwfn,
							     PROTOCOLID_ISCSI);
		break;
	case ECORE_PCI_ETH_RDMA:
	case ECORE_PCI_ETH_IWARP:
		max_num_archipelagos =
			(u16)ecore_cxt_get_proto_cid_count(p_hwfn,
							   PROTOCOLID_IWARP,
							   OSAL_NULL);
		cid_base = (u16)ecore_cxt_get_proto_cid_start(p_hwfn,
							      PROTOCOLID_IWARP);
		break;
	default:
		DP_NOTICE(p_hwfn, true,
			  "Failed to allocate ecore_ooo_info: unknown personalization\n");
		return ECORE_INVAL;
	}

	max_num_isles = ECORE_MAX_NUM_ISLES + max_num_archipelagos;

	if (!max_num_archipelagos) {
		DP_NOTICE(p_hwfn, true,
			  "Failed to allocate ecore_ooo_info: unknown amount of connections\n");
		return ECORE_INVAL;
	}

	p_ooo_info = OSAL_ZALLOC(p_hwfn->p_dev, GFP_KERNEL,
				 sizeof(*p_ooo_info));
	if (!p_ooo_info) {
		DP_NOTICE(p_hwfn, true, "Failed to allocate ecore_ooo_info\n");
		return ECORE_NOMEM;
	}
	p_ooo_info->cid_base = cid_base; /* We look only at the icid */
	p_ooo_info->max_num_archipelagos = max_num_archipelagos;

	OSAL_LIST_INIT(&p_ooo_info->free_buffers_list);
	OSAL_LIST_INIT(&p_ooo_info->ready_buffers_list);
	OSAL_LIST_INIT(&p_ooo_info->free_isles_list);

	p_ooo_info->p_isles_mem =
		OSAL_ZALLOC(p_hwfn->p_dev, GFP_KERNEL,
			    sizeof(struct ecore_ooo_isle) *
			    max_num_isles);
	if (!p_ooo_info->p_isles_mem) {
		DP_NOTICE(p_hwfn,true,
			  "Failed to allocate ecore_ooo_info (isles)\n");
		goto no_isles_mem;
	}

	for (i = 0; i < max_num_isles; i++) {
		OSAL_LIST_INIT(&p_ooo_info->p_isles_mem[i].buffers_list);
		OSAL_LIST_PUSH_TAIL(&p_ooo_info->p_isles_mem[i].list_entry,
				    &p_ooo_info->free_isles_list);
	}

	p_ooo_info->p_archipelagos_mem =
		OSAL_ZALLOC(p_hwfn->p_dev, GFP_KERNEL,
			    sizeof(struct ecore_ooo_archipelago) *
			    max_num_archipelagos);
	if (!p_ooo_info->p_archipelagos_mem) {
		DP_NOTICE(p_hwfn,true,
			 "Failed to allocate ecore_ooo_info(archpelagos)\n");
		goto no_archipelagos_mem;
	}

	for (i = 0; i < max_num_archipelagos; i++) {
		OSAL_LIST_INIT(&p_ooo_info->p_archipelagos_mem[i].isles_list);
	}

	p_ooo_info->ooo_history.p_cqes =
		OSAL_ZALLOC(p_hwfn->p_dev, GFP_KERNEL,
			    sizeof(struct ooo_opaque) *
			    ECORE_MAX_NUM_OOO_HISTORY_ENTRIES);
	if (!p_ooo_info->ooo_history.p_cqes) {
		DP_NOTICE(p_hwfn,true,
			  "Failed to allocate ecore_ooo_info(history)\n");
		goto no_history_mem;
	}
	p_ooo_info->ooo_history.num_of_cqes =
		ECORE_MAX_NUM_OOO_HISTORY_ENTRIES;

	p_hwfn->p_ooo_info = p_ooo_info;
	return ECORE_SUCCESS;

no_history_mem:
	OSAL_FREE(p_hwfn->p_dev, p_ooo_info->p_archipelagos_mem);
no_archipelagos_mem:
	OSAL_FREE(p_hwfn->p_dev, p_ooo_info->p_isles_mem);
no_isles_mem:
	OSAL_FREE(p_hwfn->p_dev, p_ooo_info);
	return ECORE_NOMEM;
}
#endif

void ecore_ooo_release_connection_isles(struct ecore_ooo_info *p_ooo_info,
					u32 cid)
{
	struct ecore_ooo_archipelago *p_archipelago;
	struct ecore_ooo_buffer *p_buffer;
	struct ecore_ooo_isle *p_isle;

	p_archipelago = ecore_ooo_seek_archipelago(p_ooo_info, cid);
	if (!p_archipelago)
		return;

	while (!OSAL_LIST_IS_EMPTY(&p_archipelago->isles_list)) {
		p_isle = OSAL_LIST_FIRST_ENTRY(
				&p_archipelago->isles_list,
				struct ecore_ooo_isle, list_entry);

#if defined(_NTDDK_)
#pragma warning(suppress : 6011 28182)
#endif
		OSAL_LIST_REMOVE_ENTRY(&p_isle->list_entry,
				       &p_archipelago->isles_list);

		while (!OSAL_LIST_IS_EMPTY(&p_isle->buffers_list)) {
			p_buffer =
				OSAL_LIST_FIRST_ENTRY(
				&p_isle->buffers_list ,
				struct ecore_ooo_buffer, list_entry);

			if (p_buffer == OSAL_NULL)
				break;
#if defined(_NTDDK_)
#pragma warning(suppress : 6011 28182)
#endif
			OSAL_LIST_REMOVE_ENTRY(&p_buffer->list_entry,
				      	       &p_isle->buffers_list);
				OSAL_LIST_PUSH_TAIL(&p_buffer->list_entry,
						&p_ooo_info->free_buffers_list);
			}
			OSAL_LIST_PUSH_TAIL(&p_isle->list_entry,
					&p_ooo_info->free_isles_list);
		}

}

void ecore_ooo_release_all_isles(struct ecore_ooo_info *p_ooo_info)
{
	struct ecore_ooo_archipelago *p_archipelago;
	struct ecore_ooo_buffer *p_buffer;
	struct ecore_ooo_isle *p_isle;
	u32 i;

	for (i = 0; i < p_ooo_info->max_num_archipelagos; i++) {
		p_archipelago = &(p_ooo_info->p_archipelagos_mem[i]);

#if defined(_NTDDK_)
#pragma warning(suppress : 6011 28182)
#endif
		while (!OSAL_LIST_IS_EMPTY(&p_archipelago->isles_list)) {
			p_isle = OSAL_LIST_FIRST_ENTRY(
					&p_archipelago->isles_list,
					struct ecore_ooo_isle, list_entry);

#if defined(_NTDDK_)
#pragma warning(suppress : 6011 28182)
#endif
			OSAL_LIST_REMOVE_ENTRY(&p_isle->list_entry,
					       &p_archipelago->isles_list);

			while (!OSAL_LIST_IS_EMPTY(&p_isle->buffers_list)) {
				p_buffer =
					OSAL_LIST_FIRST_ENTRY(
					&p_isle->buffers_list ,
					struct ecore_ooo_buffer, list_entry);

				if (p_buffer == OSAL_NULL)
					break;
#if defined(_NTDDK_)
#pragma warning(suppress : 6011 28182)
#endif
				OSAL_LIST_REMOVE_ENTRY(&p_buffer->list_entry,
						      &p_isle->buffers_list);
				OSAL_LIST_PUSH_TAIL(&p_buffer->list_entry,
					&p_ooo_info->free_buffers_list);
			}
			OSAL_LIST_PUSH_TAIL(&p_isle->list_entry,
				&p_ooo_info->free_isles_list);
		}
	}
	if (!OSAL_LIST_IS_EMPTY(&p_ooo_info->ready_buffers_list)) {
		OSAL_LIST_SPLICE_TAIL_INIT(&p_ooo_info->ready_buffers_list,
					  &p_ooo_info->free_buffers_list);
	}
}

//#ifdef CONFIG_ECORE_ISCSI
#if defined(CONFIG_ECORE_ISCSI) || defined(CONFIG_ECORE_IWARP)
void ecore_ooo_setup(struct ecore_hwfn *p_hwfn)
{
	ecore_ooo_release_all_isles(p_hwfn->p_ooo_info);
	OSAL_MEM_ZERO(p_hwfn->p_ooo_info->ooo_history.p_cqes,
		      p_hwfn->p_ooo_info->ooo_history.num_of_cqes *
		      sizeof(struct ooo_opaque));
	p_hwfn->p_ooo_info->ooo_history.head_idx = 0;
}

void ecore_ooo_free(struct ecore_hwfn *p_hwfn)
{
	struct ecore_ooo_info *p_ooo_info = p_hwfn->p_ooo_info;
	struct ecore_ooo_buffer *p_buffer;

	if (!p_ooo_info)
		return;

	ecore_ooo_release_all_isles(p_ooo_info);
	while (!OSAL_LIST_IS_EMPTY(&p_ooo_info->free_buffers_list)) {
		p_buffer = OSAL_LIST_FIRST_ENTRY(&p_ooo_info->
						 free_buffers_list,
						 struct ecore_ooo_buffer,
						 list_entry);
		if (p_buffer == OSAL_NULL)
			break;
#if defined(_NTDDK_)
#pragma warning(suppress : 6011 28182)
#endif
		OSAL_LIST_REMOVE_ENTRY(&p_buffer->list_entry,
				       &p_ooo_info->free_buffers_list);
		OSAL_DMA_FREE_COHERENT(p_hwfn->p_dev,
				       p_buffer->rx_buffer_virt_addr,
				       p_buffer->rx_buffer_phys_addr,
				       p_buffer->rx_buffer_size);
		OSAL_FREE(p_hwfn->p_dev, p_buffer);
	}

	OSAL_FREE(p_hwfn->p_dev, p_ooo_info->p_isles_mem);
	OSAL_FREE(p_hwfn->p_dev, p_ooo_info->p_archipelagos_mem);
	OSAL_FREE(p_hwfn->p_dev, p_ooo_info->ooo_history.p_cqes);
	OSAL_FREE(p_hwfn->p_dev, p_ooo_info);
	p_hwfn->p_ooo_info = OSAL_NULL;
}
#endif

void ecore_ooo_put_free_buffer(struct ecore_ooo_info *p_ooo_info,
			       struct ecore_ooo_buffer *p_buffer)
{
	OSAL_LIST_PUSH_TAIL(&p_buffer->list_entry,
			    &p_ooo_info->free_buffers_list);
}

struct ecore_ooo_buffer *
ecore_ooo_get_free_buffer(struct ecore_ooo_info *p_ooo_info)
{
	struct ecore_ooo_buffer	*p_buffer = OSAL_NULL;

	if (!OSAL_LIST_IS_EMPTY(&p_ooo_info->free_buffers_list)) {
		p_buffer =
			OSAL_LIST_FIRST_ENTRY(
			&p_ooo_info->free_buffers_list,
			struct ecore_ooo_buffer, list_entry);

		OSAL_LIST_REMOVE_ENTRY(&p_buffer->list_entry,
				      &p_ooo_info->free_buffers_list);
	}

	return p_buffer;
}

void ecore_ooo_put_ready_buffer(struct ecore_ooo_info *p_ooo_info,
				struct ecore_ooo_buffer *p_buffer, u8 on_tail)
{
	if (on_tail) {
		OSAL_LIST_PUSH_TAIL(&p_buffer->list_entry,
				   &p_ooo_info->ready_buffers_list);
	} else {
		OSAL_LIST_PUSH_HEAD(&p_buffer->list_entry,
				   &p_ooo_info->ready_buffers_list);
	}
}

struct ecore_ooo_buffer *
ecore_ooo_get_ready_buffer(struct ecore_ooo_info *p_ooo_info)
{
	struct ecore_ooo_buffer	*p_buffer = OSAL_NULL;

	if (!OSAL_LIST_IS_EMPTY(&p_ooo_info->ready_buffers_list)) {
		p_buffer =
			OSAL_LIST_FIRST_ENTRY(
			&p_ooo_info->ready_buffers_list,
			struct ecore_ooo_buffer, list_entry);

		OSAL_LIST_REMOVE_ENTRY(&p_buffer->list_entry,
				      &p_ooo_info->ready_buffers_list);
	}

	return p_buffer;
}

void ecore_ooo_delete_isles(struct ecore_hwfn *p_hwfn,
			   struct ecore_ooo_info *p_ooo_info,
			   u32 cid,
			   u8 drop_isle,
			   u8 drop_size)
{
	struct ecore_ooo_archipelago *p_archipelago = OSAL_NULL;
	struct ecore_ooo_isle *p_isle = OSAL_NULL;
	u8 isle_idx;

	p_archipelago = ecore_ooo_seek_archipelago(p_ooo_info, cid);
	for (isle_idx = 0; isle_idx < drop_size; isle_idx++) {
		p_isle = ecore_ooo_seek_isle(p_hwfn, p_ooo_info,
					    cid, drop_isle);
		if (!p_isle) {
			DP_NOTICE(p_hwfn, true,
				 "Isle %d is not found(cid %d)\n",
				 drop_isle, cid);
			return;
		}
		if (OSAL_LIST_IS_EMPTY(&p_isle->buffers_list)) {
			DP_NOTICE(p_hwfn, true,
				 "Isle %d is empty(cid %d)\n",
				 drop_isle, cid);
		} else {
			OSAL_LIST_SPLICE_TAIL_INIT(&p_isle->buffers_list,
					&p_ooo_info->free_buffers_list);
		}
#if defined(_NTDDK_)
#pragma warning(suppress : 6011)
#endif
		OSAL_LIST_REMOVE_ENTRY(&p_isle->list_entry,
				      &p_archipelago->isles_list);
		p_ooo_info->cur_isles_number--;
		OSAL_LIST_PUSH_HEAD(&p_isle->list_entry,
				   &p_ooo_info->free_isles_list);
	}
}

void ecore_ooo_add_new_isle(struct ecore_hwfn *p_hwfn,
			   struct ecore_ooo_info *p_ooo_info,
			   u32 cid, u8 ooo_isle,
			   struct ecore_ooo_buffer *p_buffer)
{
	struct ecore_ooo_archipelago *p_archipelago = OSAL_NULL;
	struct ecore_ooo_isle *p_prev_isle = OSAL_NULL;
	struct ecore_ooo_isle *p_isle = OSAL_NULL;

	if (ooo_isle > 1) {
		p_prev_isle = ecore_ooo_seek_isle(p_hwfn, p_ooo_info, cid, ooo_isle - 1);
		if (!p_prev_isle) {
			DP_NOTICE(p_hwfn, true,
				 "Isle %d is not found(cid %d)\n",
				 ooo_isle - 1, cid);
			return;
		}
	}
	p_archipelago = ecore_ooo_seek_archipelago(p_ooo_info, cid);
	if (!p_archipelago && (ooo_isle != 1)) {
		DP_NOTICE(p_hwfn, true,
			 "Connection %d is not found in OOO list\n", cid);
		return;
	}

	if (!OSAL_LIST_IS_EMPTY(&p_ooo_info->free_isles_list)) {
		p_isle =
			OSAL_LIST_FIRST_ENTRY(
			&p_ooo_info->free_isles_list,
			struct ecore_ooo_isle, list_entry);

		OSAL_LIST_REMOVE_ENTRY(&p_isle->list_entry,
				      &p_ooo_info->free_isles_list);
		if (!OSAL_LIST_IS_EMPTY(&p_isle->buffers_list)) {
			DP_NOTICE(p_hwfn, true, "Free isle is not empty\n");
			OSAL_LIST_INIT(&p_isle->buffers_list);
		}
	} else {
		DP_NOTICE(p_hwfn, true, "No more free isles\n");
		return;
	}

	if (!p_archipelago) {
		u32 idx = (cid & 0xffff) - p_ooo_info->cid_base;

		p_archipelago = &p_ooo_info->p_archipelagos_mem[idx];
	}
	OSAL_LIST_PUSH_HEAD(&p_buffer->list_entry, &p_isle->buffers_list);
	p_ooo_info->cur_isles_number++;
	p_ooo_info->gen_isles_number++;
	if (p_ooo_info->cur_isles_number > p_ooo_info->max_isles_number)
		p_ooo_info->max_isles_number = p_ooo_info->cur_isles_number;
	if (!p_prev_isle) {
		OSAL_LIST_PUSH_HEAD(&p_isle->list_entry, &p_archipelago->isles_list);
	} else {
		OSAL_LIST_INSERT_ENTRY_AFTER(&p_isle->list_entry,
			                    &p_prev_isle->list_entry,
					    &p_archipelago->isles_list);
	}
}

void ecore_ooo_add_new_buffer(struct ecore_hwfn	*p_hwfn,
			     struct ecore_ooo_info *p_ooo_info,
			     u32 cid,
			     u8 ooo_isle,
			     struct ecore_ooo_buffer *p_buffer,
		             u8 buffer_side)
{
	struct ecore_ooo_isle	* p_isle = OSAL_NULL;
	p_isle = ecore_ooo_seek_isle(p_hwfn, p_ooo_info, cid, ooo_isle);
	if (!p_isle) {
		DP_NOTICE(p_hwfn, true,
			 "Isle %d is not found(cid %d)\n",
			 ooo_isle, cid);
		return;
	}
	if (buffer_side == ECORE_OOO_LEFT_BUF) {
		OSAL_LIST_PUSH_HEAD(&p_buffer->list_entry,
				   &p_isle->buffers_list);
	} else {
		OSAL_LIST_PUSH_TAIL(&p_buffer->list_entry,
				   &p_isle->buffers_list);
	}
}

void ecore_ooo_join_isles(struct ecore_hwfn *p_hwfn,
			  struct ecore_ooo_info *p_ooo_info,
			  u32 cid, u8 left_isle)
{
	struct ecore_ooo_archipelago *p_archipelago = OSAL_NULL;
	struct ecore_ooo_isle *p_right_isle = OSAL_NULL;
	struct ecore_ooo_isle *p_left_isle = OSAL_NULL;

	p_right_isle = ecore_ooo_seek_isle(p_hwfn, p_ooo_info, cid,
					  left_isle + 1);
	if (!p_right_isle) {
		DP_NOTICE(p_hwfn, true,
			 "Right isle %d is not found(cid %d)\n",
			 left_isle + 1, cid);
		return;
	}
	p_archipelago = ecore_ooo_seek_archipelago(p_ooo_info, cid);
	OSAL_LIST_REMOVE_ENTRY(&p_right_isle->list_entry,
			      &p_archipelago->isles_list);
	p_ooo_info->cur_isles_number--;
	if (left_isle) {
		p_left_isle = ecore_ooo_seek_isle(p_hwfn, p_ooo_info, cid,
						 left_isle);
		if (!p_left_isle) {
			DP_NOTICE(p_hwfn, true,
				 "Left isle %d is not found(cid %d)\n",
				 left_isle, cid);
			return;
		}
		OSAL_LIST_SPLICE_TAIL_INIT(&p_right_isle->buffers_list,
					  &p_left_isle->buffers_list);
	} else {
		OSAL_LIST_SPLICE_TAIL_INIT(&p_right_isle->buffers_list,
					  &p_ooo_info->ready_buffers_list);
	}
	OSAL_LIST_PUSH_TAIL(&p_right_isle->list_entry,
			   &p_ooo_info->free_isles_list);
}

void ecore_ooo_dump_rx_event(struct ecore_hwfn	*p_hwfn,
			     struct ooo_opaque *iscsi_ooo,
			     struct ecore_ooo_buffer *p_buffer)
{
	int i;
	u32 dp_module = ECORE_MSG_OOO;
	u32 ph_hi, ph_lo;
	u8 *packet_buffer = 0;

	if (p_hwfn->dp_level > ECORE_LEVEL_VERBOSE)
		return;
	if (!(p_hwfn->dp_module & dp_module))
		return;

	packet_buffer = (u8 *)p_buffer->rx_buffer_virt_addr +
		p_buffer->placement_offset;
	DP_VERBOSE(p_hwfn, dp_module,
		   "******************************************************\n");
	ph_hi = DMA_HI(p_buffer->rx_buffer_phys_addr);
	ph_lo = DMA_LO(p_buffer->rx_buffer_phys_addr);
	DP_VERBOSE(p_hwfn, dp_module,
		   "0x%x-%x: CID 0x%x, OP 0x%x, ISLE 0x%x\n",
		   ph_hi, ph_lo,
		   iscsi_ooo->cid, iscsi_ooo->ooo_opcode, iscsi_ooo->ooo_isle);
	for (i = 0; i < 64; i = i + 8) {
		DP_VERBOSE(p_hwfn, dp_module,
			   "0x%x-%x:  0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x\n",
			   ph_hi, ph_lo,
			   packet_buffer[i],
			   packet_buffer[i + 1],
			   packet_buffer[i + 2],
			   packet_buffer[i + 3],
			   packet_buffer[i + 4],
			   packet_buffer[i + 5],
			   packet_buffer[i + 6],
			   packet_buffer[i + 7]);
	}
}
