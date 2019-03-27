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

#ifndef __ECORE_ISCSI_H__
#define __ECORE_ISCSI_H__

#include "ecore.h"
#include "ecore_chain.h"
#include "ecore_hsi_common.h"
#include "tcp_common.h"
#include "ecore_hsi_iscsi.h"
#include "ecore_sp_commands.h"
#include "ecore_iscsi_api.h"

#ifndef __EXTRACT__LINUX__H__
struct ecore_iscsi_info {
	osal_spinlock_t	 lock;
	osal_list_t	 free_list;
	u16		 max_num_outstanding_tasks;
	void		 *event_context;
	iscsi_event_cb_t event_cb;
};

#ifdef CONFIG_ECORE_ISCSI
enum _ecore_status_t ecore_iscsi_alloc(struct ecore_hwfn *p_hwfn);

void ecore_iscsi_setup(struct ecore_hwfn *p_hwfn);

void ecore_iscsi_free(struct ecore_hwfn *p_hwfn);
#else
static inline enum _ecore_status_t
ecore_iscsi_alloc(struct ecore_hwfn OSAL_UNUSED *p_hwfn)
{
	return ECORE_INVAL;
}

static inline void
ecore_iscsi_setup(struct ecore_hwfn OSAL_UNUSED *p_hwfn) {}

static inline void
ecore_iscsi_free(struct ecore_hwfn OSAL_UNUSED *p_hwfn) {}
#endif
#endif

void ecore_iscsi_free_connection(struct ecore_hwfn *p_hwfn,
				 struct ecore_iscsi_conn *p_conn);

/**
 * @brief ecore_sp_iscsi_conn_offload - iSCSI connection offload
 *
 * This ramrod offloads iSCSI connection to FW
 *
 * @param p_path
 * @param p_conn
 * @param comp_mode
 * @param comp_addr
 *
 * @return enum _ecore_status_t
 */
enum _ecore_status_t
ecore_sp_iscsi_conn_offload(struct ecore_hwfn *p_hwfn,
			    struct ecore_iscsi_conn *p_conn,
			    enum spq_mode comp_mode,
			    struct ecore_spq_comp_cb *p_comp_addr);

/**
 * @brief ecore_sp_iscsi_conn_update - iSCSI connection update
 *
 * This ramrod updatess iSCSI ofloadedconnection in FW
 *
 * @param p_path
 * @param p_conn
 * @param comp_mode
 * @param comp_addr
 *
 * @return enum _ecore_status_t
 */
enum _ecore_status_t
ecore_sp_iscsi_conn_update(struct ecore_hwfn *p_hwfn,
			   struct ecore_iscsi_conn *p_conn,
			   enum spq_mode comp_mode,
			   struct ecore_spq_comp_cb *p_comp_addr);

/**
 * @brief ecore_sp_iscsi_mac_update - iSCSI connection's MAC update
 *
 * This ramrod updates remote MAC for iSCSI offloaded connection in FW
 *
 * @param p_path
 * @param p_conn
 * @param comp_mode
 * @param comp_addr
 *
 * @return enum _ecore_status_t
 */
enum _ecore_status_t
ecore_sp_iscsi_mac_update(struct ecore_hwfn *p_hwfn,
			  struct ecore_iscsi_conn *p_conn,
			  enum spq_mode comp_mode,
			  struct ecore_spq_comp_cb *p_comp_addr);

  /**
 * @brief ecore_sp_iscsi_mac_update - iSCSI connection's MAC update
 *
 * This ramrod updates remote MAC for iSCSI offloaded connection in FW
 *
 * @param p_path
 * @param p_conn
 * @param reset
 * @param comp_mode
 * @param comp_addr
 *
 * @return enum _ecore_status_t
 */
enum _ecore_status_t
ecore_sp_iscsi_stats_tcp_update(struct ecore_hwfn *p_hwfn,
				struct ecore_iscsi_conn *p_conn,
				bool reset,
				enum spq_mode comp_mode,
				struct ecore_spq_comp_cb *p_comp_addr);

/**
 * @brief ecore_sp_iscsi_conn_terminate - iSCSI connection
 *        terminate
 *
 * This ramrod deletes iSCSI offloaded connection in FW
 *
 * @param p_path
 * @param p_conn
 * @param comp_mode
 * @param comp_addr
 *
 * @return enum _ecore_status_t
 */
enum _ecore_status_t
ecore_sp_iscsi_conn_terminate(struct ecore_hwfn *p_hwfn,
			      struct ecore_iscsi_conn *p_conn,
			      enum spq_mode comp_mode,
			      struct ecore_spq_comp_cb *p_comp_addr);

/**
 * @brief ecore_sp_iscsi_conn_clear_sq - iSCSI connection
 *        clear SQ
 *
 * This ramrod clears connection's SQ in FW
 *
 * @param p_path
 * @param p_conn
 * @param comp_mode
 * @param comp_addr
 *
 * @return enum _ecore_status_t
 */
enum _ecore_status_t
ecore_sp_iscsi_conn_clear_sq(struct ecore_hwfn *p_hwfn,
			     struct ecore_iscsi_conn *p_conn,
			     enum spq_mode comp_mode,
			     struct ecore_spq_comp_cb *p_comp_addr);

#endif  /*__ECORE_ISCSI_H__*/

