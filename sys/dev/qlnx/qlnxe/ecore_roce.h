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

#ifndef __ECORE_ROCE_H__
#define __ECORE_ROCE_H__

#include "ecore_status.h"

#define ECORE_ROCE_QP_TO_ICID(qp_idx)		((qp_idx)*2)
#define ECORE_ROCE_ICID_TO_QP(icid)		((icid)/2)

/* functions for enabling/disabling edpm in rdma PFs according to existence of
 * qps during DCBx update or bar size
 */
void
ecore_roce_dpm_dcbx(struct ecore_hwfn *p_hwfn, struct ecore_ptt *p_ptt);

void
ecore_rdma_dpm_bar(struct ecore_hwfn *p_hwfn, struct ecore_ptt *p_ptt);

enum _ecore_status_t
ecore_roce_dcqcn_cfg(struct ecore_hwfn *p_hwfn,
		     struct ecore_roce_dcqcn_params *params,
		     struct roce_init_func_ramrod_data *p_ramrod,
		     struct ecore_ptt *p_ptt);

enum _ecore_status_t
ecore_roce_setup(struct ecore_hwfn *p_hwfn);

enum _ecore_status_t
ecore_roce_stop_rl(struct ecore_hwfn *p_hwfn);

enum _ecore_status_t
ecore_roce_stop(struct ecore_hwfn *p_hwfn);

enum _ecore_status_t
ecore_roce_query_qp(struct ecore_hwfn *p_hwfn,
		    struct ecore_rdma_qp *qp,
		    struct ecore_rdma_query_qp_out_params *out_params);

enum _ecore_status_t
ecore_roce_destroy_qp(struct ecore_hwfn *p_hwfn,
		      struct ecore_rdma_qp *qp,
		      struct ecore_rdma_destroy_qp_out_params *out_params);

enum _ecore_status_t
ecore_roce_alloc_qp_idx(struct ecore_hwfn *p_hwfn,
			u16 *qp_idx16);

#define IS_ECORE_DCQCN(p_hwfn)	\
	(!!(p_hwfn->pf_params.rdma_pf_params.enable_dcqcn))

struct ecore_roce_info {
	struct roce_events_stats	event_stats;
	struct roce_dcqcn_received_stats dcqcn_rx_stats;
	struct roce_dcqcn_sent_stats	dcqcn_tx_stats;

	u8				dcqcn_enabled;
	u8				dcqcn_reaction_point;
};

enum _ecore_status_t
ecore_roce_modify_qp(struct ecore_hwfn *p_hwfn,
		     struct ecore_rdma_qp *qp,
		     enum ecore_roce_qp_state prev_state,
		     struct ecore_rdma_modify_qp_in_params *params);

#endif /*__ECORE_ROCE_H__*/
