/*
 * Copyright (c) 2017-2018 Cavium, Inc. 
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
 *
 * $FreeBSD$
 *
 */

#ifndef __ECORE_OOO_H__
#define __ECORE_OOO_H__

#include "ecore.h"

#define ECORE_MAX_NUM_ISLES	256
#define ECORE_MAX_NUM_OOO_HISTORY_ENTRIES	512

#define ECORE_OOO_LEFT_BUF	0
#define ECORE_OOO_RIGHT_BUF	1

struct ecore_ooo_buffer {
	osal_list_entry_t	list_entry;
	void			*rx_buffer_virt_addr;
	dma_addr_t		rx_buffer_phys_addr;
	u32			rx_buffer_size;
	u16			packet_length;
	u16			parse_flags;
	u16			vlan;
	u8			placement_offset;
};

struct ecore_ooo_isle {
	osal_list_entry_t	list_entry;
	osal_list_t		buffers_list;
};

struct ecore_ooo_archipelago {
	osal_list_t		isles_list;
};

struct ecore_ooo_history {
	struct ooo_opaque	*p_cqes;
	u32			head_idx;
	u32			num_of_cqes;
};

struct ecore_ooo_info {
	osal_list_t	 free_buffers_list;
	osal_list_t	 ready_buffers_list;
	osal_list_t	 free_isles_list;
	struct ecore_ooo_archipelago	*p_archipelagos_mem;
	struct ecore_ooo_isle	*p_isles_mem;
	struct ecore_ooo_history	ooo_history;
	u32		cur_isles_number;
	u32		max_isles_number;
	u32		gen_isles_number;
	u16		max_num_archipelagos;
	u16		cid_base;
};

#if defined(CONFIG_ECORE_ISCSI) || defined(CONFIG_ECORE_IWARP)
enum _ecore_status_t ecore_ooo_alloc(struct ecore_hwfn *p_hwfn);

void ecore_ooo_setup(struct ecore_hwfn *p_hwfn);

void ecore_ooo_free(struct ecore_hwfn *p_hwfn);
#else
static inline enum _ecore_status_t
ecore_ooo_alloc(struct ecore_hwfn OSAL_UNUSED *p_hwfn)
{
	return ECORE_INVAL;
}

static inline void
ecore_ooo_setup(struct ecore_hwfn OSAL_UNUSED *p_hwfn) {}

static inline void
ecore_ooo_free(struct ecore_hwfn OSAL_UNUSED *p_hwfn) {}
#endif


void ecore_ooo_save_history_entry(struct ecore_ooo_info *p_ooo_info,
				  struct ooo_opaque *p_cqe);

void ecore_ooo_release_connection_isles(struct ecore_ooo_info *p_ooo_info,
					u32 cid);

void ecore_ooo_release_all_isles(struct ecore_ooo_info *p_ooo_info);

void ecore_ooo_put_free_buffer(struct ecore_ooo_info *p_ooo_info,
			       struct ecore_ooo_buffer *p_buffer);

struct ecore_ooo_buffer *
ecore_ooo_get_free_buffer(struct ecore_ooo_info *p_ooo_info);

void ecore_ooo_put_ready_buffer(struct ecore_ooo_info *p_ooo_info,
				struct ecore_ooo_buffer *p_buffer, u8 on_tail);

struct ecore_ooo_buffer *
ecore_ooo_get_ready_buffer(struct ecore_ooo_info *p_ooo_info);

void ecore_ooo_delete_isles(struct ecore_hwfn	*p_hwfn,
			   struct ecore_ooo_info *p_ooo_info,
			   u32 cid,
			   u8 drop_isle,
			   u8 drop_size);

void ecore_ooo_add_new_isle(struct ecore_hwfn	*p_hwfn,
			   struct ecore_ooo_info *p_ooo_info,
			   u32 cid,
			   u8 ooo_isle,
			   struct ecore_ooo_buffer *p_buffer);

void ecore_ooo_add_new_buffer(struct ecore_hwfn	*p_hwfn,
			     struct ecore_ooo_info *p_ooo_info,
			     u32 cid,
			     u8 ooo_isle,
			     struct ecore_ooo_buffer *p_buffer,
		             u8 buffer_side);

void ecore_ooo_join_isles(struct ecore_hwfn	*p_hwfn,
			 struct ecore_ooo_info *p_ooo_info,
			 u32 cid,
			 u8 left_isle);

void ecore_ooo_dump_rx_event(struct ecore_hwfn	*p_hwfn,
			     struct ooo_opaque *iscsi_ooo,
			     struct ecore_ooo_buffer *p_buffer);

#endif  /*__ECORE_OOO_H__*/

