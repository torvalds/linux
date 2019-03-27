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

#ifndef __ECORE_LL2_H__
#define __ECORE_LL2_H__

#include "ecore.h"
#include "ecore_hsi_eth.h"
#include "ecore_chain.h"
#include "ecore_hsi_common.h"
#include "ecore_ll2_api.h"
#include "ecore_sp_api.h"

/* ECORE LL2: internal structures and functions*/
#define ECORE_MAX_NUM_OF_LL2_CONNECTIONS                    (4)

static OSAL_INLINE u8 ecore_ll2_handle_to_queue_id(struct ecore_hwfn *p_hwfn,
					      u8 handle)
{
	return p_hwfn->hw_info.resc_start[ECORE_LL2_QUEUE] + handle;
}

struct ecore_ll2_rx_packet
{
	osal_list_entry_t   list_entry;
	struct core_rx_bd_with_buff_len   *rxq_bd;
	dma_addr_t          rx_buf_addr;
	u16                 buf_length;
	void                *cookie;
	u8                  placement_offset;
	u16                 parse_flags;
	u16                 packet_length;
	u16                 vlan;
	u32                 opaque_data[2];
};

struct ecore_ll2_tx_packet
{
	osal_list_entry_t       list_entry;
	u16                     bd_used;
	bool                    notify_fw;
	void                    *cookie;
	struct {
		struct core_tx_bd       *txq_bd;
		dma_addr_t              tx_frag;
		u16                     frag_len;
	}   bds_set[1];
	/* Flexible Array of bds_set determined by max_bds_per_packet */
};

struct ecore_ll2_rx_queue {
	osal_spinlock_t		lock;
	struct ecore_chain	rxq_chain;
	struct ecore_chain	rcq_chain;
	u8			rx_sb_index;
	bool			b_cb_registred;
	__le16			*p_fw_cons;
	osal_list_t		active_descq;
	osal_list_t		free_descq;
	osal_list_t		posting_descq;
	struct ecore_ll2_rx_packet	*descq_array;
	void OSAL_IOMEM		*set_prod_addr;
};

struct ecore_ll2_tx_queue {
	osal_spinlock_t			lock;
	struct ecore_chain		txq_chain;
	u8				tx_sb_index;
	bool				b_cb_registred;
	__le16				*p_fw_cons;
	osal_list_t			active_descq;
	osal_list_t			free_descq;
	osal_list_t			sending_descq;
	struct ecore_ll2_tx_packet	*descq_array;
	struct ecore_ll2_tx_packet	*cur_send_packet;
	struct ecore_ll2_tx_packet	cur_completing_packet;
	u16				cur_completing_bd_idx;
	void OSAL_IOMEM			*doorbell_addr;
	struct core_db_data		db_msg;
	u16				bds_idx;
	u16				cur_send_frag_num;
	u16				cur_completing_frag_num;
	bool				b_completing_packet;
};

struct ecore_ll2_info {
	osal_mutex_t			mutex;

	struct ecore_ll2_acquire_data_inputs input;
	u32				cid;
	u8				my_id;
	u8				queue_id;
	u8				tx_stats_id;
	bool				b_active;
	enum core_tx_dest		tx_dest;
	u8				tx_stats_en;
	u8				main_func_queue;
	struct ecore_ll2_rx_queue	rx_queue;
	struct ecore_ll2_tx_queue	tx_queue;
	struct ecore_ll2_cbs		cbs;
};

/**
* @brief ecore_ll2_alloc - Allocates LL2 connections set
*
* @param p_hwfn
*
* @return enum _ecore_status_t
*/
enum _ecore_status_t ecore_ll2_alloc(struct ecore_hwfn *p_hwfn);

/**
* @brief ecore_ll2_setup - Inits LL2 connections set
*
* @param p_hwfn
*
*/
void ecore_ll2_setup(struct ecore_hwfn *p_hwfn);

/**
* @brief ecore_ll2_free - Releases LL2 connections set
*
* @param p_hwfn
*
*/
void ecore_ll2_free(struct ecore_hwfn *p_hwfn);

#ifndef LINUX_REMOVE
/**
 * @brief ecore_ll2_get_fragment_of_tx_packet
 *
 * @param p_hwfn
 * @param connection_handle    LL2 connection's handle
 *                              obtained from
 *                              ecore_ll2_require_connection
 * @param addr
 * @param last_fragment)
 *
 * @return enum _ecore_status_t
 */
enum _ecore_status_t
ecore_ll2_get_fragment_of_tx_packet(struct ecore_hwfn *p_hwfn,
				    u8 connection_handle,
				    dma_addr_t *addr,
				    bool *last_fragment);
#endif

#endif /*__ECORE_LL2_H__*/
