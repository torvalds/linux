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

#ifndef _ECORE_CID_
#define _ECORE_CID_

#include "ecore_hsi_common.h"
#include "ecore_proto_if.h"
#include "ecore_cxt_api.h"

/* Tasks segments definitions  */
#define ECORE_CXT_ISCSI_TID_SEG			PROTOCOLID_ISCSI	/* 0 */
#define ECORE_CXT_FCOE_TID_SEG			PROTOCOLID_FCOE		/* 1 */
#define ECORE_CXT_ROCE_TID_SEG			PROTOCOLID_ROCE		/* 2 */

enum ecore_cxt_elem_type {
	ECORE_ELEM_CXT,
	ECORE_ELEM_SRQ,
	ECORE_ELEM_TASK,
	ECORE_ELEM_XRC_SRQ,
};

enum ilt_clients {
	ILT_CLI_CDUC,
	ILT_CLI_CDUT,
	ILT_CLI_QM,
	ILT_CLI_TM,
	ILT_CLI_SRC,
	ILT_CLI_TSDM,
	ILT_CLI_MAX
};

u32 ecore_cxt_get_proto_cid_count(struct ecore_hwfn *p_hwfn,
				  enum protocol_type type,
				  u32 *vf_cid);

u32 ecore_cxt_get_proto_tid_count(struct ecore_hwfn *p_hwfn,
				  enum protocol_type type);

u32 ecore_cxt_get_proto_cid_start(struct ecore_hwfn *p_hwfn,
				  enum protocol_type type);

u32 ecore_cxt_get_srq_count(struct ecore_hwfn *p_hwfn);

u32 ecore_cxt_get_xrc_srq_count(struct ecore_hwfn *p_hwfn);

/**
 * @brief ecore_cxt_set_pf_params - Set the PF params for cxt init
 *
 * @param p_hwfn
 *
 * @return enum _ecore_status_t
 */
enum _ecore_status_t ecore_cxt_set_pf_params(struct ecore_hwfn *p_hwfn,
					     u32 rdma_tasks);

/**
 * @brief ecore_cxt_cfg_ilt_compute - compute ILT init parameters
 *
 * @param p_hwfn
 * @param last_line
 *
 * @return enum _ecore_status_t
 */
enum _ecore_status_t ecore_cxt_cfg_ilt_compute(struct ecore_hwfn *p_hwfn,
					       u32 *last_line);

/**
 * @brief ecore_cxt_cfg_ilt_compute_excess - how many lines can be decreased
 *
 * @param p_hwfn
 * @param used_lines
 */
u32 ecore_cxt_cfg_ilt_compute_excess(struct ecore_hwfn *p_hwfn, u32 used_lines);

/**
 * @brief ecore_cxt_mngr_alloc - Allocate and init the context manager struct
 *
 * @param p_hwfn
 *
 * @return enum _ecore_status_t
 */
enum _ecore_status_t ecore_cxt_mngr_alloc(struct ecore_hwfn *p_hwfn);

/**
 * @brief ecore_cxt_mngr_free
 *
 * @param p_hwfn
 */
void ecore_cxt_mngr_free(struct ecore_hwfn *p_hwfn);

/**
 * @brief ecore_cxt_tables_alloc - Allocate ILT shadow, Searcher T2, acquired map
 *
 * @param p_hwfn
 *
 * @return enum _ecore_status_t
 */
enum _ecore_status_t ecore_cxt_tables_alloc(struct ecore_hwfn *p_hwfn);

/**
 * @brief ecore_cxt_mngr_setup - Reset the acquired CIDs
 *
 * @param p_hwfn
 */
void ecore_cxt_mngr_setup(struct ecore_hwfn *p_hwfn);

/**
 * @brief ecore_cxt_hw_init_common - Initailze ILT and DQ, common phase, per path.
 *
 * @param p_hwfn
 */
void ecore_cxt_hw_init_common(struct ecore_hwfn *p_hwfn);

/**
 * @brief ecore_cxt_hw_init_pf - Initailze ILT and DQ, PF phase, per path.
 *
 * @param p_hwfn
 * @param p_ptt
 */
void ecore_cxt_hw_init_pf(struct ecore_hwfn *p_hwfn, struct ecore_ptt *p_ptt);

/**
 * @brief ecore_qm_init_pf - Initailze the QM PF phase, per path
 *
 * @param p_hwfn
 * @param p_ptt
 * @param is_pf_loading
 */
void ecore_qm_init_pf(struct ecore_hwfn *p_hwfn, struct ecore_ptt *p_ptt,
		      bool is_pf_loading);

 /**
 * @brief Reconfigures QM pf on the fly
 *
 * @param p_hwfn
 * @param p_ptt
 *
 * @return enum _ecore_status_t
 */
enum _ecore_status_t ecore_qm_reconf(struct ecore_hwfn *p_hwfn,
				     struct ecore_ptt *p_ptt);

#define ECORE_CXT_PF_CID (0xff)

/**
 * @brief ecore_cxt_release - Release a cid
 *
 * @param p_hwfn
 * @param cid
 */
void ecore_cxt_release_cid(struct ecore_hwfn *p_hwfn, u32 cid);

/**
 * @brief ecore_cxt_release - Release a cid belonging to a vf-queue
 *
 * @param p_hwfn
 * @param cid
 * @param vfid - engine relative index. ECORE_CXT_PF_CID if belongs to PF
 */
void _ecore_cxt_release_cid(struct ecore_hwfn *p_hwfn,
			    u32 cid, u8 vfid);

/**
 * @brief ecore_cxt_acquire - Acquire a new cid of a specific protocol type
 *
 * @param p_hwfn
 * @param type
 * @param p_cid
 *
 * @return enum _ecore_status_t
 */
enum _ecore_status_t ecore_cxt_acquire_cid(struct ecore_hwfn *p_hwfn,
					   enum protocol_type type,
					   u32 *p_cid);

/**
 * @brief _ecore_cxt_acquire - Acquire a new cid of a specific protocol type
 *                             for a vf-queue
 *
 * @param p_hwfn
 * @param type
 * @param p_cid
 * @param vfid - engine relative index. ECORE_CXT_PF_CID if belongs to PF
 *
 * @return enum _ecore_status_t
 */
enum _ecore_status_t _ecore_cxt_acquire_cid(struct ecore_hwfn *p_hwfn,
					    enum protocol_type type,
					    u32 *p_cid, u8 vfid);

/**
 * @brief ecore_cxt_get_tid_mem_info - function checks if the
 *        page containing the iid in the ilt is already
 *        allocated, if it is not it allocates the page.
 *
 * @param p_hwfn
 * @param elem_type
 * @param iid
 *
 * @return enum _ecore_status_t
 */
enum _ecore_status_t
ecore_cxt_dynamic_ilt_alloc(struct ecore_hwfn *p_hwfn,
			    enum ecore_cxt_elem_type elem_type,
			    u32 iid);

/**
 * @brief ecore_cxt_free_ilt_range - function frees ilt pages
 *        associated with the protocol and element type passed.
 *
 * @param p_hwfn
 * @param proto
 *
 * @return enum _ecore_status_t
 */
enum _ecore_status_t
ecore_cxt_free_ilt_range(struct ecore_hwfn *p_hwfn,
			 enum ecore_cxt_elem_type elem_type,
			 u32 start_iid, u32 count);

#define ECORE_CTX_WORKING_MEM 0
#define ECORE_CTX_FL_MEM 1
enum _ecore_status_t ecore_cxt_get_task_ctx(struct ecore_hwfn *p_hwfn,
					    u32 tid,
					    u8 ctx_type,
					    void **task_ctx);

u32 ecore_cxt_get_ilt_page_size(struct ecore_hwfn *p_hwfn,
				enum ilt_clients ilt_client);

u32 ecore_cxt_get_total_srq_count(struct ecore_hwfn *p_hwfn);

#endif /* _ECORE_CID_ */
