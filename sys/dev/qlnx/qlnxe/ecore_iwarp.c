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
 * File : ecore_iwarp.c
 */
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "bcm_osal.h"
#include "ecore.h"
#include "ecore_status.h"
#include "ecore_sp_commands.h"
#include "ecore_cxt.h"
#include "ecore_rdma.h"
#include "reg_addr.h"
#include "ecore_hw.h"
#include "ecore_hsi_iwarp.h"
#include "ecore_ll2.h"
#include "ecore_ooo.h"
#ifndef LINUX_REMOVE
#include "ecore_tcp_ip.h"
#endif

#ifdef _NTDDK_
#pragma warning(push)
#pragma warning(disable : 28123)
#pragma warning(disable : 28167)
#endif

/* Default values used for MPA Rev 1 */
#define ECORE_IWARP_ORD_DEFAULT 32
#define ECORE_IWARP_IRD_DEFAULT 32

#define ECORE_IWARP_MAX_FW_MSS  4120

struct mpa_v2_hdr {
	__be16 ird;
	__be16 ord;
};

#define MPA_V2_PEER2PEER_MODEL	0x8000
#define MPA_V2_SEND_RTR		0x4000 /* on ird */
#define MPA_V2_READ_RTR		0x4000 /* on ord */
#define MPA_V2_WRITE_RTR	0x8000
#define MPA_V2_IRD_ORD_MASK	0x3FFF

#define MPA_REV2(_mpa_rev) (_mpa_rev == MPA_NEGOTIATION_TYPE_ENHANCED)

#define ECORE_IWARP_INVALID_TCP_CID 0xffffffff
/* How many times fin will be sent before FW aborts and send RST */
#define ECORE_IWARP_MAX_FIN_RT_DEFAULT 2
#define ECORE_IWARP_RCV_WND_SIZE_MIN (0xffff)
/* INTERNAL: These numbers are derived from BRB buffer sizes to obtain optimal performance */
#define ECORE_IWARP_RCV_WND_SIZE_BB_DEF_2_PORTS (200*1024)
#define ECORE_IWARP_RCV_WND_SIZE_BB_DEF_4_PORTS (100*1024)
#define ECORE_IWARP_RCV_WND_SIZE_AH_DEF_2_PORTS (150*1024)
#define ECORE_IWARP_RCV_WND_SIZE_AH_DEF_4_PORTS (90*1024)
#define ECORE_IWARP_MAX_WND_SCALE    (14)
/* Timestamp header is the length of the timestamp option (10):
 * kind:8 bit, length:8 bit, timestamp:32 bit, ack: 32bit
 * rounded up to a multiple of 4
 */
#define TIMESTAMP_HEADER_SIZE (12)

static enum _ecore_status_t
ecore_iwarp_async_event(struct ecore_hwfn *p_hwfn,
			u8 fw_event_code,
			u16 OSAL_UNUSED echo,
			union event_ring_data *data,
			u8 fw_return_code);

static enum _ecore_status_t
ecore_iwarp_empty_ramrod(struct ecore_hwfn *p_hwfn,
			 struct ecore_iwarp_listener *listener);

static OSAL_INLINE struct ecore_iwarp_fpdu *
ecore_iwarp_get_curr_fpdu(struct ecore_hwfn *p_hwfn, u16 cid);

/* Override devinfo with iWARP specific values */
void
ecore_iwarp_init_devinfo(struct ecore_hwfn *p_hwfn)
{
	struct ecore_rdma_device *dev = p_hwfn->p_rdma_info->dev;

	dev->max_inline = IWARP_REQ_MAX_INLINE_DATA_SIZE;
	dev->max_qp = OSAL_MIN_T(u64,
				 IWARP_MAX_QPS,
				 p_hwfn->p_rdma_info->num_qps) -
		ECORE_IWARP_PREALLOC_CNT;

	dev->max_cq = dev->max_qp;

	dev->max_qp_resp_rd_atomic_resc = ECORE_IWARP_IRD_DEFAULT;
	dev->max_qp_req_rd_atomic_resc = ECORE_IWARP_ORD_DEFAULT;
}

enum _ecore_status_t
ecore_iwarp_init_hw(struct ecore_hwfn *p_hwfn, struct ecore_ptt *p_ptt)
{
	p_hwfn->rdma_prs_search_reg = PRS_REG_SEARCH_TCP;
	ecore_wr(p_hwfn, p_ptt, p_hwfn->rdma_prs_search_reg, 1);
	p_hwfn->b_rdma_enabled_in_prs = true;

	return 0;
}

void
ecore_iwarp_init_fw_ramrod(struct ecore_hwfn *p_hwfn,
			   struct iwarp_init_func_ramrod_data *p_ramrod)
{
	DP_VERBOSE(p_hwfn, ECORE_MSG_RDMA,
		   "ooo handle = %d\n",
		   p_hwfn->p_rdma_info->iwarp.ll2_ooo_handle);

	p_ramrod->iwarp.ll2_ooo_q_index =
		p_hwfn->hw_info.resc_start[ECORE_LL2_QUEUE] +
		p_hwfn->p_rdma_info->iwarp.ll2_ooo_handle;

	p_ramrod->tcp.max_fin_rt = ECORE_IWARP_MAX_FIN_RT_DEFAULT;
	return;
}

static enum _ecore_status_t
ecore_iwarp_alloc_cid(struct ecore_hwfn *p_hwfn, u32 *cid)
{
	enum _ecore_status_t rc;

	OSAL_SPIN_LOCK(&p_hwfn->p_rdma_info->lock);

	rc = ecore_rdma_bmap_alloc_id(p_hwfn,
				      &p_hwfn->p_rdma_info->cid_map,
				      cid);

	OSAL_SPIN_UNLOCK(&p_hwfn->p_rdma_info->lock);
	*cid += ecore_cxt_get_proto_cid_start(p_hwfn,
					      p_hwfn->p_rdma_info->proto);
	if (rc != ECORE_SUCCESS) {
		DP_NOTICE(p_hwfn, false, "Failed in allocating iwarp cid\n");
		return rc;
	}

	rc = ecore_cxt_dynamic_ilt_alloc(p_hwfn, ECORE_ELEM_CXT, *cid);

	if (rc != ECORE_SUCCESS) {
		OSAL_SPIN_LOCK(&p_hwfn->p_rdma_info->lock);
		*cid -= ecore_cxt_get_proto_cid_start(p_hwfn,
					     p_hwfn->p_rdma_info->proto);

		ecore_bmap_release_id(p_hwfn,
				      &p_hwfn->p_rdma_info->cid_map,
				      *cid);

		OSAL_SPIN_UNLOCK(&p_hwfn->p_rdma_info->lock);
	}

	return rc;
}

static void
ecore_iwarp_set_tcp_cid(struct ecore_hwfn *p_hwfn, u32 cid)
{
	cid -= ecore_cxt_get_proto_cid_start(p_hwfn,
					     p_hwfn->p_rdma_info->proto);

	OSAL_SPIN_LOCK(&p_hwfn->p_rdma_info->lock);
	ecore_bmap_set_id(p_hwfn,
			  &p_hwfn->p_rdma_info->tcp_cid_map,
			  cid);
	OSAL_SPIN_UNLOCK(&p_hwfn->p_rdma_info->lock);
}

/* This function allocates a cid for passive tcp ( called from syn receive)
 * the reason it's separate from the regular cid allocation is because it
 * is assured that these cids already have ilt alloacted. They are preallocated
 * to ensure that we won't need to allocate memory during syn processing
 */
static enum _ecore_status_t
ecore_iwarp_alloc_tcp_cid(struct ecore_hwfn *p_hwfn, u32 *cid)
{
	enum _ecore_status_t rc;

	OSAL_SPIN_LOCK(&p_hwfn->p_rdma_info->lock);

	rc = ecore_rdma_bmap_alloc_id(p_hwfn,
				      &p_hwfn->p_rdma_info->tcp_cid_map,
				      cid);

	OSAL_SPIN_UNLOCK(&p_hwfn->p_rdma_info->lock);

	*cid += ecore_cxt_get_proto_cid_start(p_hwfn,
					      p_hwfn->p_rdma_info->proto);
	if (rc != ECORE_SUCCESS) {
		DP_VERBOSE(p_hwfn, ECORE_MSG_RDMA,
			   "can't allocate iwarp tcp cid max-count=%d\n",
			   p_hwfn->p_rdma_info->tcp_cid_map.max_count);

		*cid = ECORE_IWARP_INVALID_TCP_CID;
	}

	return rc;
}

/* We have two cid maps, one for tcp which should be used only from passive
 * syn processing and replacing a pre-allocated ep in the list. the second
 * for active tcp and for QPs.
 */
static void ecore_iwarp_cid_cleaned(struct ecore_hwfn *p_hwfn, u32 cid)
{
	cid -= ecore_cxt_get_proto_cid_start(p_hwfn,
					     p_hwfn->p_rdma_info->proto);

	OSAL_SPIN_LOCK(&p_hwfn->p_rdma_info->lock);

	if (cid < ECORE_IWARP_PREALLOC_CNT) {
		ecore_bmap_release_id(p_hwfn,
				      &p_hwfn->p_rdma_info->tcp_cid_map,
				      cid);
	} else {
		ecore_bmap_release_id(p_hwfn,
				      &p_hwfn->p_rdma_info->cid_map,
				      cid);
	}

	OSAL_SPIN_UNLOCK(&p_hwfn->p_rdma_info->lock);
}

enum _ecore_status_t
ecore_iwarp_create_qp(struct ecore_hwfn *p_hwfn,
		      struct ecore_rdma_qp *qp,
		      struct ecore_rdma_create_qp_out_params *out_params)
{
	struct iwarp_create_qp_ramrod_data *p_ramrod;
	struct ecore_sp_init_data init_data;
	struct ecore_spq_entry *p_ent;
	enum _ecore_status_t rc;
	u16 physical_queue;
	u32 cid;

	qp->shared_queue =
		OSAL_DMA_ALLOC_COHERENT(p_hwfn->p_dev,
					&qp->shared_queue_phys_addr,
					IWARP_SHARED_QUEUE_PAGE_SIZE);
	if (!qp->shared_queue) {
		DP_NOTICE(p_hwfn, false,
			  "ecore iwarp create qp failed: cannot allocate memory (shared queue).\n");
		return ECORE_NOMEM;
	} else {
		out_params->sq_pbl_virt = (u8 *)qp->shared_queue +
			IWARP_SHARED_QUEUE_PAGE_SQ_PBL_OFFSET;
		out_params->sq_pbl_phys = qp->shared_queue_phys_addr +
			IWARP_SHARED_QUEUE_PAGE_SQ_PBL_OFFSET;
		out_params->rq_pbl_virt = (u8 *)qp->shared_queue +
			IWARP_SHARED_QUEUE_PAGE_RQ_PBL_OFFSET;
		out_params->rq_pbl_phys = qp->shared_queue_phys_addr +
			IWARP_SHARED_QUEUE_PAGE_RQ_PBL_OFFSET;
	}

	rc = ecore_iwarp_alloc_cid(p_hwfn, &cid);
	if (rc != ECORE_SUCCESS)
		goto err1;

	qp->icid = (u16)cid;

	OSAL_MEMSET(&init_data, 0, sizeof(init_data));
	init_data.opaque_fid = p_hwfn->hw_info.opaque_fid;
	init_data.cid = qp->icid;
	init_data.comp_mode = ECORE_SPQ_MODE_EBLOCK;

	rc = ecore_sp_init_request(p_hwfn, &p_ent,
				   IWARP_RAMROD_CMD_ID_CREATE_QP,
				   PROTOCOLID_IWARP, &init_data);
	if (rc != ECORE_SUCCESS)
		return rc;

	p_ramrod = &p_ent->ramrod.iwarp_create_qp;

	SET_FIELD(p_ramrod->flags,
		  IWARP_CREATE_QP_RAMROD_DATA_FMR_AND_RESERVED_EN,
		  qp->fmr_and_reserved_lkey);

	SET_FIELD(p_ramrod->flags,
		  IWARP_CREATE_QP_RAMROD_DATA_SIGNALED_COMP,
		  qp->signal_all);

	SET_FIELD(p_ramrod->flags,
		  IWARP_CREATE_QP_RAMROD_DATA_RDMA_RD_EN,
		  qp->incoming_rdma_read_en);

	SET_FIELD(p_ramrod->flags,
		  IWARP_CREATE_QP_RAMROD_DATA_RDMA_WR_EN,
		  qp->incoming_rdma_write_en);

	SET_FIELD(p_ramrod->flags,
		  IWARP_CREATE_QP_RAMROD_DATA_ATOMIC_EN,
		  qp->incoming_atomic_en);

	SET_FIELD(p_ramrod->flags,
		  IWARP_CREATE_QP_RAMROD_DATA_SRQ_FLG,
		  qp->use_srq);

	p_ramrod->pd = qp->pd;
	p_ramrod->sq_num_pages = qp->sq_num_pages;
	p_ramrod->rq_num_pages = qp->rq_num_pages;

	p_ramrod->qp_handle_for_cqe.hi = OSAL_CPU_TO_LE32(qp->qp_handle.hi);
	p_ramrod->qp_handle_for_cqe.lo = OSAL_CPU_TO_LE32(qp->qp_handle.lo);

	p_ramrod->cq_cid_for_sq =
		OSAL_CPU_TO_LE32((p_hwfn->hw_info.opaque_fid << 16) |
				 qp->sq_cq_id);
	p_ramrod->cq_cid_for_rq =
		OSAL_CPU_TO_LE32((p_hwfn->hw_info.opaque_fid << 16) |
				 qp->rq_cq_id);

	p_ramrod->dpi = OSAL_CPU_TO_LE16(qp->dpi);

	physical_queue = ecore_get_cm_pq_idx(p_hwfn, PQ_FLAGS_OFLD);
	p_ramrod->physical_q0 = OSAL_CPU_TO_LE16(physical_queue);
	physical_queue = ecore_get_cm_pq_idx(p_hwfn, PQ_FLAGS_ACK);
	p_ramrod->physical_q1 = OSAL_CPU_TO_LE16(physical_queue);

	rc = ecore_spq_post(p_hwfn, p_ent, OSAL_NULL);

	if (rc != ECORE_SUCCESS)
		goto err1;

	return rc;

err1:
	OSAL_DMA_FREE_COHERENT(p_hwfn->p_dev,
			       qp->shared_queue,
			       qp->shared_queue_phys_addr,
			       IWARP_SHARED_QUEUE_PAGE_SIZE);

	return rc;
}

static enum _ecore_status_t
ecore_iwarp_modify_fw(struct ecore_hwfn *p_hwfn,
		      struct ecore_rdma_qp *qp)
{
	struct iwarp_modify_qp_ramrod_data *p_ramrod;
	struct ecore_sp_init_data init_data;
	struct ecore_spq_entry *p_ent;
	enum _ecore_status_t rc;

	/* Get SPQ entry */
	OSAL_MEMSET(&init_data, 0, sizeof(init_data));
	init_data.cid = qp->icid;
	init_data.opaque_fid = p_hwfn->hw_info.opaque_fid;
	init_data.comp_mode = ECORE_SPQ_MODE_EBLOCK;

	rc = ecore_sp_init_request(p_hwfn, &p_ent,
				   IWARP_RAMROD_CMD_ID_MODIFY_QP,
				   p_hwfn->p_rdma_info->proto,
				   &init_data);
	if (rc != ECORE_SUCCESS)
		return rc;

	p_ramrod = &p_ent->ramrod.iwarp_modify_qp;
	SET_FIELD(p_ramrod->flags, IWARP_MODIFY_QP_RAMROD_DATA_STATE_TRANS_EN,
		  0x1);
	if (qp->iwarp_state == ECORE_IWARP_QP_STATE_CLOSING)
		p_ramrod->transition_to_state = IWARP_MODIFY_QP_STATE_CLOSING;
	else
		p_ramrod->transition_to_state = IWARP_MODIFY_QP_STATE_ERROR;

	rc = ecore_spq_post(p_hwfn, p_ent, OSAL_NULL);

	DP_VERBOSE(p_hwfn, ECORE_MSG_RDMA, "QP(0x%x)rc=%d\n",
		   qp->icid, rc);

	return rc;
}

enum ecore_iwarp_qp_state
ecore_roce2iwarp_state(enum ecore_roce_qp_state state)
{
	switch (state) {
	case ECORE_ROCE_QP_STATE_RESET:
	case ECORE_ROCE_QP_STATE_INIT:
	case ECORE_ROCE_QP_STATE_RTR:
		return ECORE_IWARP_QP_STATE_IDLE;
	case ECORE_ROCE_QP_STATE_RTS:
		return ECORE_IWARP_QP_STATE_RTS;
	case ECORE_ROCE_QP_STATE_SQD:
		return ECORE_IWARP_QP_STATE_CLOSING;
	case ECORE_ROCE_QP_STATE_ERR:
		return ECORE_IWARP_QP_STATE_ERROR;
	case ECORE_ROCE_QP_STATE_SQE:
		return ECORE_IWARP_QP_STATE_TERMINATE;
	}
	return ECORE_IWARP_QP_STATE_ERROR;
}

static enum ecore_roce_qp_state
ecore_iwarp2roce_state(enum ecore_iwarp_qp_state state)
{
	switch (state) {
	case ECORE_IWARP_QP_STATE_IDLE:
		return ECORE_ROCE_QP_STATE_INIT;
	case ECORE_IWARP_QP_STATE_RTS:
		return ECORE_ROCE_QP_STATE_RTS;
	case ECORE_IWARP_QP_STATE_TERMINATE:
		return ECORE_ROCE_QP_STATE_SQE;
	case ECORE_IWARP_QP_STATE_CLOSING:
		return ECORE_ROCE_QP_STATE_SQD;
	case ECORE_IWARP_QP_STATE_ERROR:
		return ECORE_ROCE_QP_STATE_ERR;
	}
	return ECORE_ROCE_QP_STATE_ERR;
}

const char *iwarp_state_names[] = {
	"IDLE",
	"RTS",
	"TERMINATE",
	"CLOSING",
	"ERROR",
};

enum _ecore_status_t
ecore_iwarp_modify_qp(struct ecore_hwfn *p_hwfn,
		      struct ecore_rdma_qp *qp,
		      enum ecore_iwarp_qp_state new_state,
		      bool internal)
{
	enum ecore_iwarp_qp_state prev_iw_state;
	enum _ecore_status_t rc = 0;
	bool modify_fw = false;

	/* modify QP can be called from upper-layer or as a result of async
	 * RST/FIN... therefore need to protect
	 */
	OSAL_SPIN_LOCK(&p_hwfn->p_rdma_info->iwarp.qp_lock);
	prev_iw_state = qp->iwarp_state;

	if (prev_iw_state == new_state) {
		OSAL_SPIN_UNLOCK(&p_hwfn->p_rdma_info->iwarp.qp_lock);
		return ECORE_SUCCESS;
	}

	switch (prev_iw_state) {
	case ECORE_IWARP_QP_STATE_IDLE:
		switch (new_state) {
		case ECORE_IWARP_QP_STATE_RTS:
			qp->iwarp_state = ECORE_IWARP_QP_STATE_RTS;
			break;
		case ECORE_IWARP_QP_STATE_ERROR:
			qp->iwarp_state = ECORE_IWARP_QP_STATE_ERROR;
			if (!internal)
				modify_fw = true;
			break;
		default:
			break;
		}
		break;
	case ECORE_IWARP_QP_STATE_RTS:
		switch (new_state) {
		case ECORE_IWARP_QP_STATE_CLOSING:
			if (!internal)
				modify_fw = true;

			qp->iwarp_state = ECORE_IWARP_QP_STATE_CLOSING;
			break;
		case ECORE_IWARP_QP_STATE_ERROR:
			if (!internal)
				modify_fw = true;
			qp->iwarp_state = ECORE_IWARP_QP_STATE_ERROR;
			break;
		default:
			break;
		}
		break;
	case ECORE_IWARP_QP_STATE_ERROR:
		switch (new_state) {
		case ECORE_IWARP_QP_STATE_IDLE:
			/* TODO: destroy flow -> need to destroy EP&QP */
			qp->iwarp_state = new_state;
			break;
		case ECORE_IWARP_QP_STATE_CLOSING:
			/* could happen due to race... do nothing.... */
			break;
		default:
			rc = ECORE_INVAL;
		}
		break;
	case ECORE_IWARP_QP_STATE_TERMINATE:
	case ECORE_IWARP_QP_STATE_CLOSING:
		qp->iwarp_state = new_state;
		break;
	default:
		break;
	}

	DP_VERBOSE(p_hwfn, ECORE_MSG_RDMA, "QP(0x%x) %s --> %s %s\n",
		   qp->icid,
		   iwarp_state_names[prev_iw_state],
		   iwarp_state_names[qp->iwarp_state],
		   internal ? "internal" : " ");

	OSAL_SPIN_UNLOCK(&p_hwfn->p_rdma_info->iwarp.qp_lock);

	if (modify_fw)
		ecore_iwarp_modify_fw(p_hwfn, qp);

	return rc;
}

enum _ecore_status_t
ecore_iwarp_fw_destroy(struct ecore_hwfn *p_hwfn,
		       struct ecore_rdma_qp *qp)
{
	struct ecore_sp_init_data init_data;
	struct ecore_spq_entry *p_ent;
	enum _ecore_status_t rc;

	/* Get SPQ entry */
	OSAL_MEMSET(&init_data, 0, sizeof(init_data));
	init_data.cid = qp->icid;
	init_data.opaque_fid = p_hwfn->hw_info.opaque_fid;
	init_data.comp_mode = ECORE_SPQ_MODE_EBLOCK;

	rc = ecore_sp_init_request(p_hwfn, &p_ent,
				   IWARP_RAMROD_CMD_ID_DESTROY_QP,
				   p_hwfn->p_rdma_info->proto,
				   &init_data);
	if (rc != ECORE_SUCCESS)
		return rc;

	rc = ecore_spq_post(p_hwfn, p_ent, OSAL_NULL);

	DP_VERBOSE(p_hwfn, ECORE_MSG_RDMA, "QP(0x%x) rc = %d\n",  qp->icid, rc);

	return rc;
}

static void ecore_iwarp_destroy_ep(struct ecore_hwfn *p_hwfn,
				   struct ecore_iwarp_ep *ep,
				   bool remove_from_active_list)
{
	OSAL_DMA_FREE_COHERENT(p_hwfn->p_dev,
			       ep->ep_buffer_virt,
			       ep->ep_buffer_phys,
			       sizeof(*ep->ep_buffer_virt));

	if (remove_from_active_list) {
		OSAL_SPIN_LOCK(&p_hwfn->p_rdma_info->iwarp.iw_lock);

		OSAL_LIST_REMOVE_ENTRY(&ep->list_entry,
				       &p_hwfn->p_rdma_info->iwarp.ep_list);

		OSAL_SPIN_UNLOCK(&p_hwfn->p_rdma_info->iwarp.iw_lock);
	}

	if (ep->qp)
		ep->qp->ep = OSAL_NULL;

	OSAL_FREE(p_hwfn->p_dev, ep);
}

enum _ecore_status_t
ecore_iwarp_destroy_qp(struct ecore_hwfn *p_hwfn,
		       struct ecore_rdma_qp *qp)
{
	enum _ecore_status_t rc = ECORE_SUCCESS;
	struct ecore_iwarp_ep *ep = qp->ep;
	struct ecore_iwarp_fpdu *fpdu;
	int wait_count = 0;

	fpdu = ecore_iwarp_get_curr_fpdu(p_hwfn, qp->icid);
	if (fpdu && fpdu->incomplete_bytes)
		DP_NOTICE(p_hwfn, false,
			  "Pending Partial fpdu with incomplete bytes=%d\n",
			  fpdu->incomplete_bytes);

	if (qp->iwarp_state != ECORE_IWARP_QP_STATE_ERROR) {

		rc = ecore_iwarp_modify_qp(p_hwfn, qp,
					   ECORE_IWARP_QP_STATE_ERROR,
					   false);

		if (rc != ECORE_SUCCESS)
			return rc;
	}

	/* Make sure ep is closed before returning and freeing memory. */
	if (ep) {
		while (ep->state != ECORE_IWARP_EP_CLOSED) {
			DP_VERBOSE(p_hwfn, ECORE_MSG_RDMA,
				   "Waiting for ep->state to be closed...state=%x\n",
				   ep->state);

			OSAL_MSLEEP(100);
			if (wait_count++ > 200) {
				DP_NOTICE(p_hwfn, false, "ep state close timeout state=%x\n",
					  ep->state);
				break;
			}
		}

		ecore_iwarp_destroy_ep(p_hwfn, ep, false);
	}

	rc = ecore_iwarp_fw_destroy(p_hwfn, qp);

	if (qp->shared_queue)
		OSAL_DMA_FREE_COHERENT(p_hwfn->p_dev,
				       qp->shared_queue,
				       qp->shared_queue_phys_addr,
				       IWARP_SHARED_QUEUE_PAGE_SIZE);

	return rc;
}

static enum _ecore_status_t
ecore_iwarp_create_ep(struct ecore_hwfn *p_hwfn,
		      struct ecore_iwarp_ep **ep_out)
{
	struct ecore_iwarp_ep *ep;
	enum _ecore_status_t rc;

	ep = OSAL_ZALLOC(p_hwfn->p_dev, GFP_KERNEL, sizeof(*ep));
	if (!ep) {
		DP_NOTICE(p_hwfn, false,
			  "ecore create ep failed: cannot allocate memory (ep). rc = %d\n",
			  ECORE_NOMEM);
		return ECORE_NOMEM;
	}

	ep->state = ECORE_IWARP_EP_INIT;

	/* ep_buffer is allocated once and is structured as follows:
	 * [MAX_PRIV_DATA_LEN][MAX_PRIV_DATA_LEN][union async_output]
	 * We could have allocated this in three calls but since all together
	 * it is less than a page, we do one allocation and initialize pointers
	 * accordingly
	 */
	ep->ep_buffer_virt = OSAL_DMA_ALLOC_COHERENT(
		p_hwfn->p_dev,
		&ep->ep_buffer_phys,
		sizeof(*ep->ep_buffer_virt));

	if (!ep->ep_buffer_virt) {
		DP_NOTICE(p_hwfn, false,
			  "ecore create ep failed: cannot allocate memory (ulp buffer). rc = %d\n",
			  ECORE_NOMEM);
		rc = ECORE_NOMEM;
		goto err;
	}

	ep->sig = 0xdeadbeef;

	*ep_out = ep;

	return ECORE_SUCCESS;

err:
	OSAL_FREE(p_hwfn->p_dev, ep);
	return rc;
}

static void
ecore_iwarp_print_tcp_ramrod(struct ecore_hwfn *p_hwfn,
			     struct iwarp_tcp_offload_ramrod_data *p_tcp_ramrod)
{
	DP_VERBOSE(p_hwfn, ECORE_MSG_RDMA, ">>> PRINT TCP RAMROD\n");

	DP_VERBOSE(p_hwfn, ECORE_MSG_RDMA, "local_mac=%x %x %x\n",
		   p_tcp_ramrod->tcp.local_mac_addr_lo,
		   p_tcp_ramrod->tcp.local_mac_addr_mid,
		   p_tcp_ramrod->tcp.local_mac_addr_hi);

	DP_VERBOSE(p_hwfn, ECORE_MSG_RDMA, "remote_mac=%x %x %x\n",
		   p_tcp_ramrod->tcp.remote_mac_addr_lo,
		   p_tcp_ramrod->tcp.remote_mac_addr_mid,
		   p_tcp_ramrod->tcp.remote_mac_addr_hi);

	DP_VERBOSE(p_hwfn, ECORE_MSG_RDMA, "vlan_id=%x\n",
		   p_tcp_ramrod->tcp.vlan_id);
	DP_VERBOSE(p_hwfn, ECORE_MSG_RDMA, "flags=%x\n",
		   p_tcp_ramrod->tcp.flags);

	DP_VERBOSE(p_hwfn, ECORE_MSG_RDMA, "ip_version=%x\n",
		   p_tcp_ramrod->tcp.ip_version);
	DP_VERBOSE(p_hwfn, ECORE_MSG_RDMA, "local_ip=%x.%x.%x.%x\n",
		   p_tcp_ramrod->tcp.local_ip[0],
		   p_tcp_ramrod->tcp.local_ip[1],
		   p_tcp_ramrod->tcp.local_ip[2],
		   p_tcp_ramrod->tcp.local_ip[3]);
	DP_VERBOSE(p_hwfn, ECORE_MSG_RDMA, "remote_ip=%x.%x.%x.%x\n",
		   p_tcp_ramrod->tcp.remote_ip[0],
		   p_tcp_ramrod->tcp.remote_ip[1],
		   p_tcp_ramrod->tcp.remote_ip[2],
		   p_tcp_ramrod->tcp.remote_ip[3]);
	DP_VERBOSE(p_hwfn, ECORE_MSG_RDMA, "flow_label=%x\n",
		   p_tcp_ramrod->tcp.flow_label);
	DP_VERBOSE(p_hwfn, ECORE_MSG_RDMA, "ttl=%x\n",
		   p_tcp_ramrod->tcp.ttl);
	DP_VERBOSE(p_hwfn, ECORE_MSG_RDMA, "tos_or_tc=%x\n",
		   p_tcp_ramrod->tcp.tos_or_tc);
	DP_VERBOSE(p_hwfn, ECORE_MSG_RDMA, "local_port=%x\n",
		   p_tcp_ramrod->tcp.local_port);
	DP_VERBOSE(p_hwfn, ECORE_MSG_RDMA, "remote_port=%x\n",
		   p_tcp_ramrod->tcp.remote_port);
	DP_VERBOSE(p_hwfn, ECORE_MSG_RDMA, "mss=%x\n",
		   p_tcp_ramrod->tcp.mss);
	DP_VERBOSE(p_hwfn, ECORE_MSG_RDMA, "rcv_wnd_scale=%x\n",
		   p_tcp_ramrod->tcp.rcv_wnd_scale);
	DP_VERBOSE(p_hwfn, ECORE_MSG_RDMA, "connect_mode=%x\n",
		   p_tcp_ramrod->tcp.connect_mode);
	DP_VERBOSE(p_hwfn, ECORE_MSG_RDMA, "syn_ip_payload_length=%x\n",
		   p_tcp_ramrod->tcp.syn_ip_payload_length);
	DP_VERBOSE(p_hwfn, ECORE_MSG_RDMA, "syn_phy_addr_lo=%x\n",
		   p_tcp_ramrod->tcp.syn_phy_addr_lo);
	DP_VERBOSE(p_hwfn, ECORE_MSG_RDMA, "syn_phy_addr_hi=%x\n",
		   p_tcp_ramrod->tcp.syn_phy_addr_hi);

	DP_VERBOSE(p_hwfn, ECORE_MSG_RDMA, "<<<f  PRINT TCP RAMROD\n");
}

/* Default values for tcp option2 */
#define ECORE_IWARP_DEF_MAX_RT_TIME (0)
#define ECORE_IWARP_DEF_CWND_FACTOR (4)
#define ECORE_IWARP_DEF_KA_MAX_PROBE_CNT (5)
#define ECORE_IWARP_DEF_KA_TIMEOUT (1200000) /* 20 min */
#define ECORE_IWARP_DEF_KA_INTERVAL (1000) /* 1 sec */

static enum _ecore_status_t
ecore_iwarp_tcp_offload(struct ecore_hwfn *p_hwfn,
			struct ecore_iwarp_ep *ep)
{
	struct ecore_iwarp_info *iwarp_info = &p_hwfn->p_rdma_info->iwarp;
	struct iwarp_tcp_offload_ramrod_data *p_tcp_ramrod;
	struct ecore_sp_init_data init_data;
	struct ecore_spq_entry *p_ent;
	dma_addr_t async_output_phys;
	dma_addr_t in_pdata_phys;
	enum _ecore_status_t rc;
	u16 physical_q;
	u8 tcp_flags;
	int i;

	OSAL_MEMSET(&init_data, 0, sizeof(init_data));
	init_data.cid = ep->tcp_cid;
	init_data.opaque_fid = p_hwfn->hw_info.opaque_fid;

	if (ep->connect_mode == TCP_CONNECT_PASSIVE) {
		init_data.comp_mode = ECORE_SPQ_MODE_CB;
	} else {
		init_data.comp_mode = ECORE_SPQ_MODE_EBLOCK;
	}

	rc = ecore_sp_init_request(p_hwfn, &p_ent,
				   IWARP_RAMROD_CMD_ID_TCP_OFFLOAD,
				   PROTOCOLID_IWARP, &init_data);
	if (rc != ECORE_SUCCESS)
		return rc;

	p_tcp_ramrod = &p_ent->ramrod.iwarp_tcp_offload;

	/* Point to the "second half" of the ulp buffer */
	in_pdata_phys = ep->ep_buffer_phys +
		OFFSETOF(struct ecore_iwarp_ep_memory, in_pdata);
	p_tcp_ramrod->iwarp.incoming_ulp_buffer.addr.hi =
		DMA_HI_LE(in_pdata_phys);
	p_tcp_ramrod->iwarp.incoming_ulp_buffer.addr.lo =
		DMA_LO_LE(in_pdata_phys);
	p_tcp_ramrod->iwarp.incoming_ulp_buffer.len =
		OSAL_CPU_TO_LE16(sizeof(ep->ep_buffer_virt->in_pdata));

	async_output_phys = ep->ep_buffer_phys +
		OFFSETOF(struct ecore_iwarp_ep_memory, async_output);

	p_tcp_ramrod->iwarp.async_eqe_output_buf.hi =
		DMA_HI_LE(async_output_phys);
	p_tcp_ramrod->iwarp.async_eqe_output_buf.lo =
		DMA_LO_LE(async_output_phys);
	p_tcp_ramrod->iwarp.handle_for_async.hi = OSAL_CPU_TO_LE32(PTR_HI(ep));
	p_tcp_ramrod->iwarp.handle_for_async.lo = OSAL_CPU_TO_LE32(PTR_LO(ep));

	physical_q = ecore_get_cm_pq_idx(p_hwfn, PQ_FLAGS_OFLD);
	p_tcp_ramrod->iwarp.physical_q0 = OSAL_CPU_TO_LE16(physical_q);
	physical_q = ecore_get_cm_pq_idx(p_hwfn, PQ_FLAGS_ACK);
	p_tcp_ramrod->iwarp.physical_q1 = OSAL_CPU_TO_LE16(physical_q);
	p_tcp_ramrod->iwarp.mpa_mode = iwarp_info->mpa_rev;

	ecore_set_fw_mac_addr(&p_tcp_ramrod->tcp.remote_mac_addr_hi,
			      &p_tcp_ramrod->tcp.remote_mac_addr_mid,
			      &p_tcp_ramrod->tcp.remote_mac_addr_lo,
			      ep->remote_mac_addr);
	ecore_set_fw_mac_addr(&p_tcp_ramrod->tcp.local_mac_addr_hi,
			      &p_tcp_ramrod->tcp.local_mac_addr_mid,
			      &p_tcp_ramrod->tcp.local_mac_addr_lo,
			      ep->local_mac_addr);

	p_tcp_ramrod->tcp.vlan_id = OSAL_CPU_TO_LE16(ep->cm_info.vlan);

	tcp_flags = p_hwfn->p_rdma_info->iwarp.tcp_flags;
	p_tcp_ramrod->tcp.flags = 0;
	SET_FIELD(p_tcp_ramrod->tcp.flags,
		  TCP_OFFLOAD_PARAMS_OPT2_TS_EN,
		  !!(tcp_flags & ECORE_IWARP_TS_EN));

	SET_FIELD(p_tcp_ramrod->tcp.flags,
		  TCP_OFFLOAD_PARAMS_OPT2_DA_EN,
		  !!(tcp_flags & ECORE_IWARP_DA_EN));

	p_tcp_ramrod->tcp.ip_version = ep->cm_info.ip_version;

	for (i = 0; i < 4; i++) {
		p_tcp_ramrod->tcp.remote_ip[i] =
			OSAL_CPU_TO_LE32(ep->cm_info.remote_ip[i]);
		p_tcp_ramrod->tcp.local_ip[i] =
			OSAL_CPU_TO_LE32(ep->cm_info.local_ip[i]);
	}

	p_tcp_ramrod->tcp.remote_port =
		OSAL_CPU_TO_LE16(ep->cm_info.remote_port);
	p_tcp_ramrod->tcp.local_port = OSAL_CPU_TO_LE16(ep->cm_info.local_port);
	p_tcp_ramrod->tcp.mss = OSAL_CPU_TO_LE16(ep->mss);
	p_tcp_ramrod->tcp.flow_label = 0;
	p_tcp_ramrod->tcp.ttl = 0x40;
	p_tcp_ramrod->tcp.tos_or_tc = 0;

	p_tcp_ramrod->tcp.max_rt_time = ECORE_IWARP_DEF_MAX_RT_TIME;
	p_tcp_ramrod->tcp.cwnd = ECORE_IWARP_DEF_CWND_FACTOR * p_tcp_ramrod->tcp.mss;
	p_tcp_ramrod->tcp.ka_max_probe_cnt = ECORE_IWARP_DEF_KA_MAX_PROBE_CNT;
	p_tcp_ramrod->tcp.ka_timeout = ECORE_IWARP_DEF_KA_TIMEOUT;
	p_tcp_ramrod->tcp.ka_interval = ECORE_IWARP_DEF_KA_INTERVAL;

	p_tcp_ramrod->tcp.rcv_wnd_scale =
		(u8)p_hwfn->p_rdma_info->iwarp.rcv_wnd_scale;
	p_tcp_ramrod->tcp.connect_mode = ep->connect_mode;

	if (ep->connect_mode == TCP_CONNECT_PASSIVE) {
		p_tcp_ramrod->tcp.syn_ip_payload_length =
			OSAL_CPU_TO_LE16(ep->syn_ip_payload_length);
		p_tcp_ramrod->tcp.syn_phy_addr_hi =
			DMA_HI_LE(ep->syn_phy_addr);
		p_tcp_ramrod->tcp.syn_phy_addr_lo =
			DMA_LO_LE(ep->syn_phy_addr);
	}

	ecore_iwarp_print_tcp_ramrod(p_hwfn, p_tcp_ramrod);

	rc = ecore_spq_post(p_hwfn, p_ent, OSAL_NULL);

	DP_VERBOSE(p_hwfn, ECORE_MSG_RDMA,
		   "EP(0x%x) Offload completed rc=%d\n" , ep->tcp_cid, rc);

	return rc;
}

/* This function should be called after IWARP_EVENT_TYPE_ASYNC_CONNECT_COMPLETE
 * is received. it will be called from the dpc context.
 */
static enum _ecore_status_t
ecore_iwarp_mpa_offload(struct ecore_hwfn *p_hwfn,
			struct ecore_iwarp_ep *ep)
{
	struct iwarp_mpa_offload_ramrod_data *p_mpa_ramrod;
	struct ecore_iwarp_info *iwarp_info;
	struct ecore_sp_init_data init_data;
	struct ecore_spq_entry *p_ent;
	dma_addr_t async_output_phys;
	dma_addr_t out_pdata_phys;
	dma_addr_t in_pdata_phys;
	struct ecore_rdma_qp *qp;
	bool reject;
	enum _ecore_status_t rc;

	if (!ep)
		return ECORE_INVAL;

	qp = ep->qp;
	reject = (qp == OSAL_NULL);

	OSAL_MEMSET(&init_data, 0, sizeof(init_data));
	init_data.cid = reject ? ep->tcp_cid : qp->icid;
	init_data.opaque_fid = p_hwfn->hw_info.opaque_fid;

	if (ep->connect_mode == TCP_CONNECT_ACTIVE || !ep->event_cb)
		init_data.comp_mode = ECORE_SPQ_MODE_CB;
	else
		init_data.comp_mode = ECORE_SPQ_MODE_EBLOCK;

	rc = ecore_sp_init_request(p_hwfn, &p_ent,
				   IWARP_RAMROD_CMD_ID_MPA_OFFLOAD,
				   PROTOCOLID_IWARP, &init_data);

	if (rc != ECORE_SUCCESS)
		return rc;

	p_mpa_ramrod = &p_ent->ramrod.iwarp_mpa_offload;
	out_pdata_phys = ep->ep_buffer_phys +
		OFFSETOF(struct ecore_iwarp_ep_memory, out_pdata);
	p_mpa_ramrod->common.outgoing_ulp_buffer.addr.hi =
		DMA_HI_LE(out_pdata_phys);
	p_mpa_ramrod->common.outgoing_ulp_buffer.addr.lo =
		DMA_LO_LE(out_pdata_phys);
	p_mpa_ramrod->common.outgoing_ulp_buffer.len =
		ep->cm_info.private_data_len;
	p_mpa_ramrod->common.crc_needed = p_hwfn->p_rdma_info->iwarp.crc_needed;

	p_mpa_ramrod->common.out_rq.ord = ep->cm_info.ord;
	p_mpa_ramrod->common.out_rq.ird = ep->cm_info.ird;

	p_mpa_ramrod->tcp_cid = p_hwfn->hw_info.opaque_fid << 16 | ep->tcp_cid;

	in_pdata_phys = ep->ep_buffer_phys +
		OFFSETOF(struct ecore_iwarp_ep_memory, in_pdata);
	p_mpa_ramrod->tcp_connect_side = ep->connect_mode;
	p_mpa_ramrod->incoming_ulp_buffer.addr.hi =
		DMA_HI_LE(in_pdata_phys);
	p_mpa_ramrod->incoming_ulp_buffer.addr.lo =
		DMA_LO_LE(in_pdata_phys);
	p_mpa_ramrod->incoming_ulp_buffer.len =
		OSAL_CPU_TO_LE16(sizeof(ep->ep_buffer_virt->in_pdata));
	async_output_phys = ep->ep_buffer_phys +
		OFFSETOF(struct ecore_iwarp_ep_memory, async_output);
	p_mpa_ramrod->async_eqe_output_buf.hi =
		DMA_HI_LE(async_output_phys);
	p_mpa_ramrod->async_eqe_output_buf.lo =
		DMA_LO_LE(async_output_phys);
	p_mpa_ramrod->handle_for_async.hi = OSAL_CPU_TO_LE32(PTR_HI(ep));
	p_mpa_ramrod->handle_for_async.lo = OSAL_CPU_TO_LE32(PTR_LO(ep));

	if (!reject) {
		p_mpa_ramrod->shared_queue_addr.hi =
			DMA_HI_LE(qp->shared_queue_phys_addr);
		p_mpa_ramrod->shared_queue_addr.lo =
			DMA_LO_LE(qp->shared_queue_phys_addr);

		p_mpa_ramrod->stats_counter_id =
			RESC_START(p_hwfn, ECORE_RDMA_STATS_QUEUE) +
			qp->stats_queue;
	} else {
		p_mpa_ramrod->common.reject = 1;
	}

	iwarp_info = &p_hwfn->p_rdma_info->iwarp;
	p_mpa_ramrod->rcv_wnd = iwarp_info->rcv_wnd_size;
	p_mpa_ramrod->mode = ep->mpa_rev;
	SET_FIELD(p_mpa_ramrod->rtr_pref,
		  IWARP_MPA_OFFLOAD_RAMROD_DATA_RTR_SUPPORTED,
		  ep->rtr_type);

	ep->state = ECORE_IWARP_EP_MPA_OFFLOADED;
	rc = ecore_spq_post(p_hwfn, p_ent, OSAL_NULL);
	if (!reject)
		ep->cid = qp->icid; /* Now they're migrated. */

	DP_VERBOSE(p_hwfn, ECORE_MSG_RDMA,
		   "QP(0x%x) EP(0x%x) MPA Offload rc = %d IRD=0x%x ORD=0x%x rtr_type=%d mpa_rev=%d reject=%d\n",
		   reject ? 0xffff : qp->icid, ep->tcp_cid, rc, ep->cm_info.ird,
		   ep->cm_info.ord, ep->rtr_type, ep->mpa_rev, reject);
	return rc;
}

static void
ecore_iwarp_mpa_received(struct ecore_hwfn *p_hwfn,
			 struct ecore_iwarp_ep *ep)
{
	struct ecore_iwarp_info *iwarp_info = &p_hwfn->p_rdma_info->iwarp;
	struct ecore_iwarp_cm_event_params params;
	struct mpa_v2_hdr *mpa_v2_params;
	union async_output *async_data;
	u16 mpa_ord, mpa_ird;
	u8 mpa_hdr_size = 0;
	u8 mpa_rev;

	async_data = &ep->ep_buffer_virt->async_output;

	mpa_rev = async_data->mpa_request.mpa_handshake_mode;
	DP_VERBOSE(p_hwfn, ECORE_MSG_RDMA,
		   "private_data_len=%x handshake_mode=%x private_data=(%x)\n",
		   async_data->mpa_request.ulp_data_len,
		   mpa_rev,
		   *((u32 *)((u8 *)ep->ep_buffer_virt->in_pdata)));

	if (ep->listener->state > ECORE_IWARP_LISTENER_STATE_UNPAUSE) {
		/* MPA reject initiated by ecore */
		OSAL_MEMSET(&ep->cm_info, 0, sizeof(ep->cm_info));
		ep->event_cb = OSAL_NULL;
		ecore_iwarp_mpa_offload(p_hwfn, ep);
		return;
	}

	if (mpa_rev == MPA_NEGOTIATION_TYPE_ENHANCED) {
		if (iwarp_info->mpa_rev == MPA_NEGOTIATION_TYPE_BASIC) {
			DP_ERR(p_hwfn, "MPA_NEGOTIATE Received MPA rev 2 on driver supporting only MPA rev 1\n");
			/* MPA_REV2 ToDo: close the tcp connection. */
			return;
		}

		/* Read ord/ird values from private data buffer */
		mpa_v2_params =
			(struct mpa_v2_hdr *)(ep->ep_buffer_virt->in_pdata);
		mpa_hdr_size = sizeof(*mpa_v2_params);

		mpa_ord = ntohs(mpa_v2_params->ord);
		mpa_ird = ntohs(mpa_v2_params->ird);

		/* Temprary store in cm_info incoming ord/ird requested, later
		 * replace with negotiated value during accept
		 */
		ep->cm_info.ord = (u8)OSAL_MIN_T(u16,
						(mpa_ord & MPA_V2_IRD_ORD_MASK),
						ECORE_IWARP_ORD_DEFAULT);

		ep->cm_info.ird = (u8)OSAL_MIN_T(u16,
						(mpa_ird & MPA_V2_IRD_ORD_MASK),
						ECORE_IWARP_IRD_DEFAULT);

		/* Peer2Peer negotiation */
		ep->rtr_type = MPA_RTR_TYPE_NONE;
		if (mpa_ird & MPA_V2_PEER2PEER_MODEL) {
			if (mpa_ord & MPA_V2_WRITE_RTR)
				ep->rtr_type |= MPA_RTR_TYPE_ZERO_WRITE;

			if (mpa_ord & MPA_V2_READ_RTR)
				ep->rtr_type |= MPA_RTR_TYPE_ZERO_READ;

			if (mpa_ird & MPA_V2_SEND_RTR)
				ep->rtr_type |= MPA_RTR_TYPE_ZERO_SEND;

			ep->rtr_type &= iwarp_info->rtr_type;
			/* if we're left with no match send our capabilities */
			if (ep->rtr_type == MPA_RTR_TYPE_NONE)
				ep->rtr_type = iwarp_info->rtr_type;

			/* prioritize write over send and read */
			if (ep->rtr_type & MPA_RTR_TYPE_ZERO_WRITE)
					ep->rtr_type = MPA_RTR_TYPE_ZERO_WRITE;
		}

		ep->mpa_rev = MPA_NEGOTIATION_TYPE_ENHANCED;
	} else {
		ep->cm_info.ord = ECORE_IWARP_ORD_DEFAULT;
		ep->cm_info.ird = ECORE_IWARP_IRD_DEFAULT;
		ep->mpa_rev = MPA_NEGOTIATION_TYPE_BASIC;
	}

	DP_VERBOSE(p_hwfn, ECORE_MSG_RDMA,
		   "MPA_NEGOTIATE (v%d): ORD: 0x%x IRD: 0x%x rtr:0x%x ulp_data_len = %x mpa_hdr_size = %x\n",
		   mpa_rev, ep->cm_info.ord, ep->cm_info.ird, ep->rtr_type,
		   async_data->mpa_request.ulp_data_len,
		   mpa_hdr_size);

	/* Strip mpa v2 hdr from private data before sending to upper layer */
	ep->cm_info.private_data =
		ep->ep_buffer_virt->in_pdata + mpa_hdr_size;

	ep->cm_info.private_data_len =
		async_data->mpa_request.ulp_data_len - mpa_hdr_size;

	params.event = ECORE_IWARP_EVENT_MPA_REQUEST;
	params.cm_info = &ep->cm_info;
	params.ep_context = ep;
	params.status = ECORE_SUCCESS;

	ep->state = ECORE_IWARP_EP_MPA_REQ_RCVD;
	ep->event_cb(ep->cb_context, &params);
}

static void
ecore_iwarp_move_to_ep_list(struct ecore_hwfn *p_hwfn,
			    osal_list_t *list, struct ecore_iwarp_ep *ep)
{
	OSAL_SPIN_LOCK(&ep->listener->lock);
	OSAL_LIST_REMOVE_ENTRY(&ep->list_entry, &ep->listener->ep_list);
	OSAL_SPIN_UNLOCK(&ep->listener->lock);
	OSAL_SPIN_LOCK(&p_hwfn->p_rdma_info->iwarp.iw_lock);
	OSAL_LIST_PUSH_TAIL(&ep->list_entry, list);
	OSAL_SPIN_UNLOCK(&p_hwfn->p_rdma_info->iwarp.iw_lock);
}

static void
ecore_iwarp_return_ep(struct ecore_hwfn *p_hwfn,
		      struct ecore_iwarp_ep *ep)
{
	ep->state = ECORE_IWARP_EP_INIT;
	if (ep->qp)
		ep->qp->ep = OSAL_NULL;
	ep->qp = OSAL_NULL;
	OSAL_MEMSET(&ep->cm_info, 0, sizeof(ep->cm_info));

	if (ep->tcp_cid == ECORE_IWARP_INVALID_TCP_CID) {
		/* We don't care about the return code, it's ok if tcp_cid
		 * remains invalid...in this case we'll defer allocation
		 */
		ecore_iwarp_alloc_tcp_cid(p_hwfn, &ep->tcp_cid);
	}

	ecore_iwarp_move_to_ep_list(p_hwfn,
				    &p_hwfn->p_rdma_info->iwarp.ep_free_list,
				    ep);
}

static void
ecore_iwarp_parse_private_data(struct ecore_hwfn *p_hwfn,
			       struct ecore_iwarp_ep *ep)
{
	struct mpa_v2_hdr *mpa_v2_params;
	union async_output *async_data;
	u16 mpa_ird, mpa_ord;
	u8 mpa_data_size = 0;

	if (MPA_REV2(p_hwfn->p_rdma_info->iwarp.mpa_rev)) {
		mpa_v2_params = (struct mpa_v2_hdr *)
		((u8 *)ep->ep_buffer_virt->in_pdata);
		mpa_data_size = sizeof(*mpa_v2_params);
		mpa_ird = ntohs(mpa_v2_params->ird);
		mpa_ord = ntohs(mpa_v2_params->ord);

		ep->cm_info.ird = (u8)(mpa_ord & MPA_V2_IRD_ORD_MASK);
		ep->cm_info.ord = (u8)(mpa_ird & MPA_V2_IRD_ORD_MASK);
	} /* else: Ord / Ird already configured */

	async_data = &ep->ep_buffer_virt->async_output;

	ep->cm_info.private_data = ep->ep_buffer_virt->in_pdata + mpa_data_size;
	ep->cm_info.private_data_len =
		async_data->mpa_response.ulp_data_len - mpa_data_size;
}

static void
ecore_iwarp_mpa_reply_arrived(struct ecore_hwfn *p_hwfn,
			      struct ecore_iwarp_ep *ep)
{
	struct ecore_iwarp_cm_event_params params;

	if (ep->connect_mode == TCP_CONNECT_PASSIVE) {
		DP_NOTICE(p_hwfn, true, "MPA reply event not expected on passive side!\n");
		return;
	}

	params.event = ECORE_IWARP_EVENT_ACTIVE_MPA_REPLY;

	ecore_iwarp_parse_private_data(p_hwfn, ep);

	DP_VERBOSE(p_hwfn, ECORE_MSG_RDMA,
		   "MPA_NEGOTIATE (v%d): ORD: 0x%x IRD: 0x%x\n",
		   ep->mpa_rev, ep->cm_info.ord, ep->cm_info.ird);

	params.cm_info = &ep->cm_info;
	params.ep_context = ep;
	params.status = ECORE_SUCCESS;

	ep->mpa_reply_processed = true;

	ep->event_cb(ep->cb_context, &params);
}

#define ECORE_IWARP_CONNECT_MODE_STRING(ep) \
	(ep->connect_mode == TCP_CONNECT_PASSIVE) ? "Passive" : "Active"

/* Called as a result of the event:
 * IWARP_EVENT_TYPE_ASYNC_MPA_HANDSHAKE_COMPLETE
 */
static void
ecore_iwarp_mpa_complete(struct ecore_hwfn *p_hwfn,
			 struct ecore_iwarp_ep *ep,
			 u8 fw_return_code)
{
	struct ecore_iwarp_cm_event_params params;

	if (ep->connect_mode == TCP_CONNECT_ACTIVE)
		params.event = ECORE_IWARP_EVENT_ACTIVE_COMPLETE;
	else
		params.event = ECORE_IWARP_EVENT_PASSIVE_COMPLETE;

	if (ep->connect_mode == TCP_CONNECT_ACTIVE &&
	    !ep->mpa_reply_processed) {
		ecore_iwarp_parse_private_data(p_hwfn, ep);
	}

	DP_VERBOSE(p_hwfn, ECORE_MSG_RDMA,
		   "MPA_NEGOTIATE (v%d): ORD: 0x%x IRD: 0x%x\n",
		   ep->mpa_rev, ep->cm_info.ord, ep->cm_info.ird);

	params.cm_info = &ep->cm_info;

	params.ep_context = ep;

	if ((ep->connect_mode == TCP_CONNECT_PASSIVE) &&
	    (ep->state != ECORE_IWARP_EP_MPA_OFFLOADED)) {
		/* This is a FW bug. Shouldn't get complete without offload */
		DP_NOTICE(p_hwfn, false, "%s(0x%x) ERROR: Got MPA complete without MPA offload fw_return_code=%d ep->state=%d\n",
			  ECORE_IWARP_CONNECT_MODE_STRING(ep), ep->tcp_cid,
			  fw_return_code, ep->state);
		ep->state = ECORE_IWARP_EP_CLOSED;
		return;
	}

	if ((ep->connect_mode == TCP_CONNECT_PASSIVE) &&
	    (ep->state == ECORE_IWARP_EP_ABORTING))
		return;

	ep->state = ECORE_IWARP_EP_CLOSED;

	switch (fw_return_code) {
	case RDMA_RETURN_OK:
		ep->qp->max_rd_atomic_req = ep->cm_info.ord;
		ep->qp->max_rd_atomic_resp = ep->cm_info.ird;
		ecore_iwarp_modify_qp(p_hwfn, ep->qp,
				      ECORE_IWARP_QP_STATE_RTS,
				      1);
		ep->state = ECORE_IWARP_EP_ESTABLISHED;
		params.status = ECORE_SUCCESS;
		break;
	case IWARP_CONN_ERROR_MPA_TIMEOUT:
		DP_NOTICE(p_hwfn, false, "%s(0x%x) MPA timeout\n",
			  ECORE_IWARP_CONNECT_MODE_STRING(ep), ep->cid);
		params.status = ECORE_TIMEOUT;
		break;
	case IWARP_CONN_ERROR_MPA_ERROR_REJECT:
		DP_NOTICE(p_hwfn, false, "%s(0x%x) MPA Reject\n",
			  ECORE_IWARP_CONNECT_MODE_STRING(ep), ep->cid);
		params.status = ECORE_CONN_REFUSED;
		break;
	case IWARP_CONN_ERROR_MPA_RST:
		DP_NOTICE(p_hwfn, false, "%s(0x%x) MPA reset(tcp cid: 0x%x)\n",
			  ECORE_IWARP_CONNECT_MODE_STRING(ep), ep->cid,
			  ep->tcp_cid);
		params.status = ECORE_CONN_RESET;
		break;
	case IWARP_CONN_ERROR_MPA_FIN:
		DP_NOTICE(p_hwfn, false, "%s(0x%x) MPA received FIN\n",
			  ECORE_IWARP_CONNECT_MODE_STRING(ep), ep->cid);
		params.status = ECORE_CONN_REFUSED;
		break;
	case IWARP_CONN_ERROR_MPA_INSUF_IRD:
		DP_NOTICE(p_hwfn, false, "%s(0x%x) MPA insufficient ird\n",
			  ECORE_IWARP_CONNECT_MODE_STRING(ep), ep->cid);
		params.status = ECORE_CONN_REFUSED;
		break;
	case IWARP_CONN_ERROR_MPA_RTR_MISMATCH:
		DP_NOTICE(p_hwfn, false, "%s(0x%x) MPA RTR MISMATCH\n",
			  ECORE_IWARP_CONNECT_MODE_STRING(ep), ep->cid);
		params.status = ECORE_CONN_REFUSED;
		break;
	case IWARP_CONN_ERROR_MPA_INVALID_PACKET:
		DP_NOTICE(p_hwfn, false, "%s(0x%x) MPA Invalid Packet\n",
			  ECORE_IWARP_CONNECT_MODE_STRING(ep), ep->cid);
		params.status = ECORE_CONN_REFUSED;
		break;
	case IWARP_CONN_ERROR_MPA_LOCAL_ERROR:
		DP_NOTICE(p_hwfn, false, "%s(0x%x) MPA Local Error\n",
			  ECORE_IWARP_CONNECT_MODE_STRING(ep), ep->cid);
		params.status = ECORE_CONN_REFUSED;
		break;
	case IWARP_CONN_ERROR_MPA_TERMINATE:
		DP_NOTICE(p_hwfn, false, "%s(0x%x) MPA TERMINATE\n",
			  ECORE_IWARP_CONNECT_MODE_STRING(ep), ep->cid);
		params.status = ECORE_CONN_REFUSED;
		break;
	default:
		params.status = ECORE_CONN_RESET;
		break;
	}

	if (ep->event_cb)
		ep->event_cb(ep->cb_context, &params);

	/* on passive side, if there is no associated QP (REJECT) we need to
	 * return the ep to the pool, otherwise we wait for QP to release it.
	 * Since we add an element in accept instead of this one. in anycase
	 * we need to remove it from the ep_list (active connections)...
	 */
	if (fw_return_code != RDMA_RETURN_OK) {
		ep->tcp_cid = ECORE_IWARP_INVALID_TCP_CID;
		if ((ep->connect_mode == TCP_CONNECT_PASSIVE) &&
		    (ep->qp == OSAL_NULL)) { /* Rejected */
			ecore_iwarp_return_ep(p_hwfn, ep);
		} else {
			OSAL_SPIN_LOCK(&p_hwfn->p_rdma_info->iwarp.iw_lock);
			OSAL_LIST_REMOVE_ENTRY(
				&ep->list_entry,
				&p_hwfn->p_rdma_info->iwarp.ep_list);
			OSAL_SPIN_UNLOCK(&p_hwfn->p_rdma_info->iwarp.iw_lock);
		}
	}
}

static void
ecore_iwarp_mpa_v2_set_private(struct ecore_hwfn *p_hwfn,
			       struct ecore_iwarp_ep *ep,
			       u8 *mpa_data_size)
{
	struct mpa_v2_hdr *mpa_v2_params;
	u16 mpa_ird, mpa_ord;

	*mpa_data_size = 0;
	if (MPA_REV2(ep->mpa_rev)) {
		mpa_v2_params =
			(struct mpa_v2_hdr *)ep->ep_buffer_virt->out_pdata;
		*mpa_data_size = sizeof(*mpa_v2_params);

		mpa_ird = (u16)ep->cm_info.ird;
		mpa_ord = (u16)ep->cm_info.ord;

		if (ep->rtr_type != MPA_RTR_TYPE_NONE) {
			mpa_ird |= MPA_V2_PEER2PEER_MODEL;

			if (ep->rtr_type & MPA_RTR_TYPE_ZERO_SEND)
				mpa_ird |= MPA_V2_SEND_RTR;

			if (ep->rtr_type & MPA_RTR_TYPE_ZERO_WRITE)
				mpa_ord |= MPA_V2_WRITE_RTR;

			if (ep->rtr_type & MPA_RTR_TYPE_ZERO_READ)
				mpa_ord |= MPA_V2_READ_RTR;
		}

		mpa_v2_params->ird = htons(mpa_ird);
		mpa_v2_params->ord = htons(mpa_ord);

		DP_VERBOSE(p_hwfn, ECORE_MSG_RDMA,
			   "MPA_NEGOTIATE Header: [%x ord:%x ird] %x ord:%x ird:%x peer2peer:%x rtr_send:%x rtr_write:%x rtr_read:%x\n",
			   mpa_v2_params->ird,
			   mpa_v2_params->ord,
			   *((u32 *)mpa_v2_params),
			   mpa_ord & MPA_V2_IRD_ORD_MASK,
			   mpa_ird & MPA_V2_IRD_ORD_MASK,
			   !!(mpa_ird & MPA_V2_PEER2PEER_MODEL),
			   !!(mpa_ird & MPA_V2_SEND_RTR),
			   !!(mpa_ord & MPA_V2_WRITE_RTR),
			   !!(mpa_ord & MPA_V2_READ_RTR));
	}
}

enum _ecore_status_t
ecore_iwarp_connect(void *rdma_cxt,
		    struct ecore_iwarp_connect_in *iparams,
		    struct ecore_iwarp_connect_out *oparams)
{
	struct ecore_hwfn *p_hwfn = (struct ecore_hwfn *)rdma_cxt;
	struct ecore_iwarp_info *iwarp_info;
	struct ecore_iwarp_ep *ep;
	enum _ecore_status_t rc;
	u8 mpa_data_size = 0;
	u8 ts_hdr_size = 0;
	u32 cid;

	if ((iparams->cm_info.ord > ECORE_IWARP_ORD_DEFAULT) ||
	    (iparams->cm_info.ird > ECORE_IWARP_IRD_DEFAULT)) {
		DP_VERBOSE(p_hwfn, ECORE_MSG_RDMA,
			   "QP(0x%x) ERROR: Invalid ord(0x%x)/ird(0x%x)\n",
			   iparams->qp->icid, iparams->cm_info.ord,
			   iparams->cm_info.ird);

		return ECORE_INVAL;
	}

	iwarp_info = &p_hwfn->p_rdma_info->iwarp;

	/* Allocate ep object */
	rc = ecore_iwarp_alloc_cid(p_hwfn, &cid);
	if (rc != ECORE_SUCCESS)
		return rc;

	if (iparams->qp->ep == OSAL_NULL) {
		rc = ecore_iwarp_create_ep(p_hwfn, &ep);
		if (rc != ECORE_SUCCESS)
			return rc;
	} else {
		ep = iparams->qp->ep;
		DP_ERR(p_hwfn, "Note re-use of QP for different connect\n");
		ep->state = ECORE_IWARP_EP_INIT;
	}

	ep->tcp_cid = cid;

	OSAL_SPIN_LOCK(&p_hwfn->p_rdma_info->iwarp.iw_lock);
	OSAL_LIST_PUSH_TAIL(&ep->list_entry,
			    &p_hwfn->p_rdma_info->iwarp.ep_list);
	OSAL_SPIN_UNLOCK(&p_hwfn->p_rdma_info->iwarp.iw_lock);

	ep->qp = iparams->qp;
	ep->qp->ep = ep;
	OSAL_MEMCPY(ep->remote_mac_addr,
		    iparams->remote_mac_addr,
		    ETH_ALEN);
	OSAL_MEMCPY(ep->local_mac_addr,
		    iparams->local_mac_addr,
		    ETH_ALEN);
	OSAL_MEMCPY(&ep->cm_info, &iparams->cm_info, sizeof(ep->cm_info));

	ep->cm_info.ord = iparams->cm_info.ord;
	ep->cm_info.ird = iparams->cm_info.ird;

	ep->rtr_type = iwarp_info->rtr_type;
	if (iwarp_info->peer2peer == 0)
		ep->rtr_type = MPA_RTR_TYPE_NONE;

	if ((ep->rtr_type & MPA_RTR_TYPE_ZERO_READ) &&
	    (ep->cm_info.ord == 0))
		ep->cm_info.ord = 1;

	ep->mpa_rev = iwarp_info->mpa_rev;

	ecore_iwarp_mpa_v2_set_private(p_hwfn, ep, &mpa_data_size);

	ep->cm_info.private_data = (u8 *)ep->ep_buffer_virt->out_pdata;
	ep->cm_info.private_data_len =
		iparams->cm_info.private_data_len + mpa_data_size;

	OSAL_MEMCPY((u8 *)(u8 *)ep->ep_buffer_virt->out_pdata + mpa_data_size,
		    iparams->cm_info.private_data,
		    iparams->cm_info.private_data_len);

	if (p_hwfn->p_rdma_info->iwarp.tcp_flags & ECORE_IWARP_TS_EN)
		ts_hdr_size = TIMESTAMP_HEADER_SIZE;

	ep->mss = iparams->mss - ts_hdr_size;
	ep->mss = OSAL_MIN_T(u16, ECORE_IWARP_MAX_FW_MSS, ep->mss);

	ep->event_cb = iparams->event_cb;
	ep->cb_context = iparams->cb_context;
	ep->connect_mode = TCP_CONNECT_ACTIVE;

	oparams->ep_context = ep;

	rc = ecore_iwarp_tcp_offload(p_hwfn, ep);

	DP_VERBOSE(p_hwfn, ECORE_MSG_RDMA, "QP(0x%x) EP(0x%x) rc = %d\n",
		   iparams->qp->icid, ep->tcp_cid, rc);

	if (rc != ECORE_SUCCESS)
		ecore_iwarp_destroy_ep(p_hwfn, ep, true);

	return rc;
}

static struct ecore_iwarp_ep *
ecore_iwarp_get_free_ep(struct ecore_hwfn *p_hwfn)
{
	struct ecore_iwarp_ep *ep = OSAL_NULL;
	enum _ecore_status_t rc;

	OSAL_SPIN_LOCK(&p_hwfn->p_rdma_info->iwarp.iw_lock);

	if (OSAL_LIST_IS_EMPTY(&p_hwfn->p_rdma_info->iwarp.ep_free_list)) {
		DP_ERR(p_hwfn, "Ep list is empty\n");
		goto out;
	}

	ep = OSAL_LIST_FIRST_ENTRY(&p_hwfn->p_rdma_info->iwarp.ep_free_list,
				   struct ecore_iwarp_ep,
				   list_entry);

	/* in some cases we could have failed allocating a tcp cid when added
	 * from accept / failure... retry now..this is not the common case.
	 */
	if (ep->tcp_cid == ECORE_IWARP_INVALID_TCP_CID) {
		rc = ecore_iwarp_alloc_tcp_cid(p_hwfn, &ep->tcp_cid);
		/* if we fail we could look for another entry with a valid
		 * tcp_cid, but since we don't expect to reach this anyway
		 * it's not worth the handling
		 */
		if (rc) {
			ep->tcp_cid = ECORE_IWARP_INVALID_TCP_CID;
			ep = OSAL_NULL;
			goto out;
		}
	}

	OSAL_LIST_REMOVE_ENTRY(&ep->list_entry,
			       &p_hwfn->p_rdma_info->iwarp.ep_free_list);

out:
	OSAL_SPIN_UNLOCK(&p_hwfn->p_rdma_info->iwarp.iw_lock);
	return ep;
}

/* takes into account timer scan ~20 ms and interrupt/dpc overhead */
#define ECORE_IWARP_MAX_CID_CLEAN_TIME  100
/* Technically we shouldn't reach this count with 100 ms iteration sleep */
#define ECORE_IWARP_MAX_NO_PROGRESS_CNT 5

/* This function waits for all the bits of a bmap to be cleared, as long as
 * there is progress ( i.e. the number of bits left to be cleared decreases )
 * the function continues.
 */
static enum _ecore_status_t
ecore_iwarp_wait_cid_map_cleared(struct ecore_hwfn *p_hwfn,
				 struct ecore_bmap *bmap)
{
	int prev_weight = 0;
	int wait_count = 0;
	int weight = 0;

	weight = OSAL_BITMAP_WEIGHT(bmap->bitmap, bmap->max_count);
	prev_weight = weight;

	while (weight) {
		OSAL_MSLEEP(ECORE_IWARP_MAX_CID_CLEAN_TIME);

		weight = OSAL_BITMAP_WEIGHT(bmap->bitmap, bmap->max_count);

		if (prev_weight == weight) {
			wait_count++;
		} else {
			prev_weight = weight;
			wait_count = 0;
		}

		if (wait_count > ECORE_IWARP_MAX_NO_PROGRESS_CNT) {
			DP_NOTICE(p_hwfn, false,
				  "%s bitmap wait timed out (%d cids pending)\n",
				  bmap->name, weight);
			return ECORE_TIMEOUT;
		}
	}
	return ECORE_SUCCESS;
}

static enum _ecore_status_t
ecore_iwarp_wait_for_all_cids(struct ecore_hwfn *p_hwfn)
{
	enum _ecore_status_t rc;
	int i;

	rc = ecore_iwarp_wait_cid_map_cleared(
		p_hwfn, &p_hwfn->p_rdma_info->tcp_cid_map);
	if (rc)
		return rc;

	/* Now free the tcp cids from the main cid map */
	for (i = 0; i < ECORE_IWARP_PREALLOC_CNT; i++) {
		ecore_bmap_release_id(p_hwfn,
				      &p_hwfn->p_rdma_info->cid_map,
				      i);
	}

	/* Now wait for all cids to be completed */
	rc = ecore_iwarp_wait_cid_map_cleared(
		p_hwfn, &p_hwfn->p_rdma_info->cid_map);

	return rc;
}

static void
ecore_iwarp_free_prealloc_ep(struct ecore_hwfn *p_hwfn)
{
	struct ecore_iwarp_ep *ep;
	u32 cid;

	while (!OSAL_LIST_IS_EMPTY(&p_hwfn->p_rdma_info->iwarp.ep_free_list)) {
		OSAL_SPIN_LOCK(&p_hwfn->p_rdma_info->iwarp.iw_lock);

		ep = OSAL_LIST_FIRST_ENTRY(
			&p_hwfn->p_rdma_info->iwarp.ep_free_list,
			struct ecore_iwarp_ep, list_entry);

		if (ep == OSAL_NULL) {
			OSAL_SPIN_UNLOCK(&p_hwfn->p_rdma_info->iwarp.iw_lock);
			break;
		}

#ifdef _NTDDK_
#pragma warning(suppress : 6011)
#endif
		OSAL_LIST_REMOVE_ENTRY(
			&ep->list_entry,
			&p_hwfn->p_rdma_info->iwarp.ep_free_list);

		OSAL_SPIN_UNLOCK(&p_hwfn->p_rdma_info->iwarp.iw_lock);

		if (ep->tcp_cid != ECORE_IWARP_INVALID_TCP_CID) {
			cid = ep->tcp_cid - ecore_cxt_get_proto_cid_start(
				p_hwfn, p_hwfn->p_rdma_info->proto);

			OSAL_SPIN_LOCK(&p_hwfn->p_rdma_info->lock);

			ecore_bmap_release_id(p_hwfn,
					      &p_hwfn->p_rdma_info->tcp_cid_map,
					      cid);

			OSAL_SPIN_UNLOCK(&p_hwfn->p_rdma_info->lock);
		}

		ecore_iwarp_destroy_ep(p_hwfn, ep, false);
	}
}

static enum _ecore_status_t
ecore_iwarp_prealloc_ep(struct ecore_hwfn *p_hwfn, bool init)
{
	struct ecore_iwarp_ep *ep;
	int rc = ECORE_SUCCESS;
	u32 cid;
	int count;
	int i;

	if (init)
		count = ECORE_IWARP_PREALLOC_CNT;
	else
		count = 1;

	for (i = 0; i < count; i++) {
		rc = ecore_iwarp_create_ep(p_hwfn, &ep);
		if (rc != ECORE_SUCCESS)
			return rc;

		/* During initialization we allocate from the main pool,
		 * afterwards we allocate only from the tcp_cid.
		 */
		if (init) {
			rc = ecore_iwarp_alloc_cid(p_hwfn, &cid);
			if (rc != ECORE_SUCCESS)
				goto err;
			ecore_iwarp_set_tcp_cid(p_hwfn, cid);
		} else {
			/* We don't care about the return code, it's ok if
			 * tcp_cid remains invalid...in this case we'll
			 * defer allocation
			 */
			ecore_iwarp_alloc_tcp_cid(p_hwfn, &cid);
		}

		ep->tcp_cid = cid;

		OSAL_SPIN_LOCK(&p_hwfn->p_rdma_info->iwarp.iw_lock);
		OSAL_LIST_PUSH_TAIL(&ep->list_entry,
				    &p_hwfn->p_rdma_info->iwarp.ep_free_list);
		OSAL_SPIN_UNLOCK(&p_hwfn->p_rdma_info->iwarp.iw_lock);
	}

	return rc;

err:
	ecore_iwarp_destroy_ep(p_hwfn, ep, false);

	return rc;
}

enum _ecore_status_t
ecore_iwarp_alloc(struct ecore_hwfn *p_hwfn)
{
	enum _ecore_status_t rc;

#ifdef CONFIG_ECORE_LOCK_ALLOC
	OSAL_SPIN_LOCK_ALLOC(p_hwfn, &p_hwfn->p_rdma_info->iwarp.iw_lock);
	OSAL_SPIN_LOCK_ALLOC(p_hwfn, &p_hwfn->p_rdma_info->iwarp.qp_lock);
#endif
	OSAL_SPIN_LOCK_INIT(&p_hwfn->p_rdma_info->iwarp.iw_lock);
	OSAL_SPIN_LOCK_INIT(&p_hwfn->p_rdma_info->iwarp.qp_lock);

	/* Allocate bitmap for tcp cid. These are used by passive side
	 * to ensure it can allocate a tcp cid during dpc that was
	 * pre-acquired and doesn't require dynamic allocation of ilt
	 */
	rc = ecore_rdma_bmap_alloc(p_hwfn, &p_hwfn->p_rdma_info->tcp_cid_map,
				   ECORE_IWARP_PREALLOC_CNT,
				   "TCP_CID");
	if (rc != ECORE_SUCCESS) {
		DP_VERBOSE(p_hwfn, ECORE_MSG_RDMA,
			   "Failed to allocate tcp cid, rc = %d\n",
			   rc);
		return rc;
	}

	OSAL_LIST_INIT(&p_hwfn->p_rdma_info->iwarp.ep_free_list);
//DAVIDS	OSAL_SPIN_LOCK_INIT(&p_hwfn->p_rdma_info->iwarp.iw_lock);
	rc = ecore_iwarp_prealloc_ep(p_hwfn, true);
	if (rc != ECORE_SUCCESS) {
		DP_VERBOSE(p_hwfn, ECORE_MSG_RDMA,
			   "ecore_iwarp_prealloc_ep failed, rc = %d\n",
			   rc);
		return rc;
	}
	DP_VERBOSE(p_hwfn, ECORE_MSG_RDMA,
			   "ecore_iwarp_prealloc_ep success, rc = %d\n",
			   rc);

	return ecore_ooo_alloc(p_hwfn);
}

void
ecore_iwarp_resc_free(struct ecore_hwfn *p_hwfn)
{
	struct ecore_iwarp_info *iwarp_info = &p_hwfn->p_rdma_info->iwarp;

#ifdef CONFIG_ECORE_LOCK_ALLOC
	OSAL_SPIN_LOCK_DEALLOC(iwarp_info->iw_lock);
	OSAL_SPIN_LOCK_DEALLOC(iwarp_info->qp_lock);
#endif
	ecore_ooo_free(p_hwfn);
	if (iwarp_info->partial_fpdus)
		OSAL_FREE(p_hwfn->p_dev, iwarp_info->partial_fpdus);
	if (iwarp_info->mpa_bufs)
		OSAL_FREE(p_hwfn->p_dev, iwarp_info->mpa_bufs);
	if (iwarp_info->mpa_intermediate_buf)
		OSAL_FREE(p_hwfn->p_dev, iwarp_info->mpa_intermediate_buf);

	ecore_rdma_bmap_free(p_hwfn, &p_hwfn->p_rdma_info->tcp_cid_map, 1);
}


enum _ecore_status_t
ecore_iwarp_accept(void *rdma_cxt,
		   struct ecore_iwarp_accept_in *iparams)
{
	struct ecore_hwfn *p_hwfn = (struct ecore_hwfn *)rdma_cxt;
	struct ecore_iwarp_ep *ep;
	u8 mpa_data_size = 0;
	enum _ecore_status_t rc;

	ep = (struct ecore_iwarp_ep *)iparams->ep_context;
	if (!ep) {
		DP_ERR(p_hwfn, "Ep Context receive in accept is NULL\n");
		return ECORE_INVAL;
	}

	DP_VERBOSE(p_hwfn, ECORE_MSG_RDMA, "QP(0x%x) EP(0x%x)\n",
		   iparams->qp->icid, ep->tcp_cid);

	if ((iparams->ord > ECORE_IWARP_ORD_DEFAULT) ||
	    (iparams->ird > ECORE_IWARP_IRD_DEFAULT)) {
		DP_VERBOSE(p_hwfn, ECORE_MSG_RDMA,
			   "QP(0x%x) EP(0x%x) ERROR: Invalid ord(0x%x)/ird(0x%x)\n",
			   iparams->qp->icid, ep->tcp_cid,
			   iparams->ord, iparams->ord);
		return ECORE_INVAL;
	}

	/* We could reach qp->ep != OSAL NULL if we do accept on the same qp */
	if (iparams->qp->ep == OSAL_NULL) {
		/* We need to add a replacement for the ep to the free list */
		ecore_iwarp_prealloc_ep(p_hwfn, false);
	} else {
		DP_VERBOSE(p_hwfn, ECORE_MSG_RDMA,
			   "Note re-use of QP for different connect\n");
		/* Return the old ep to the free_pool */
		ecore_iwarp_return_ep(p_hwfn, iparams->qp->ep);
	}

	ecore_iwarp_move_to_ep_list(p_hwfn,
				    &p_hwfn->p_rdma_info->iwarp.ep_list,
				    ep);
	ep->listener = OSAL_NULL;
	ep->cb_context = iparams->cb_context;
	ep->qp = iparams->qp;
	ep->qp->ep = ep;

	if (ep->mpa_rev == MPA_NEGOTIATION_TYPE_ENHANCED) {
		/* Negotiate ord/ird: if upperlayer requested ord larger than
		 * ird advertised by remote, we need to decrease our ord
		 * to match remote ord
		 */
		if (iparams->ord > ep->cm_info.ird) {
			iparams->ord = ep->cm_info.ird;
		}

		/* For chelsio compatability, if rtr_zero read is requested
		 * we can't set ird to zero
		 */
		if ((ep->rtr_type & MPA_RTR_TYPE_ZERO_READ) &&
		    (iparams->ird == 0))
			iparams->ird = 1;
	}

	/* Update cm_info ord/ird to be negotiated values */
	ep->cm_info.ord = iparams->ord;
	ep->cm_info.ird = iparams->ird;

	ecore_iwarp_mpa_v2_set_private(p_hwfn, ep, &mpa_data_size);

	ep->cm_info.private_data = ep->ep_buffer_virt->out_pdata;
	ep->cm_info.private_data_len =
		iparams->private_data_len + mpa_data_size;

	OSAL_MEMCPY((u8 *)ep->ep_buffer_virt->out_pdata + mpa_data_size,
		    iparams->private_data,
		    iparams->private_data_len);

	if (ep->state == ECORE_IWARP_EP_CLOSED) {
		DP_NOTICE(p_hwfn, false,
			  "(0x%x) Accept called on EP in CLOSED state\n",
			  ep->tcp_cid);
		ep->tcp_cid = ECORE_IWARP_INVALID_TCP_CID;
		ecore_iwarp_return_ep(p_hwfn, ep);
		return ECORE_CONN_RESET;
	}

	rc = ecore_iwarp_mpa_offload(p_hwfn, ep);
	if (rc) {
		ecore_iwarp_modify_qp(p_hwfn,
				      iparams->qp,
				      ECORE_IWARP_QP_STATE_ERROR,
				      1);
	}

	return rc;
}

enum _ecore_status_t
ecore_iwarp_reject(void *rdma_cxt,
		   struct ecore_iwarp_reject_in *iparams)
{
	struct ecore_hwfn *p_hwfn = (struct ecore_hwfn *)rdma_cxt;
	struct ecore_iwarp_ep *ep;
	u8 mpa_data_size = 0;
	enum _ecore_status_t rc;

	ep = (struct ecore_iwarp_ep *)iparams->ep_context;
	if (!ep) {
		DP_ERR(p_hwfn, "Ep Context receive in reject is NULL\n");
		return ECORE_INVAL;
	}

	DP_VERBOSE(p_hwfn, ECORE_MSG_RDMA, "EP(0x%x)\n", ep->tcp_cid);

	ep->cb_context = iparams->cb_context;
	ep->qp = OSAL_NULL;

	ecore_iwarp_mpa_v2_set_private(p_hwfn, ep, &mpa_data_size);

	ep->cm_info.private_data = ep->ep_buffer_virt->out_pdata;
	ep->cm_info.private_data_len =
		iparams->private_data_len + mpa_data_size;

	OSAL_MEMCPY((u8 *)ep->ep_buffer_virt->out_pdata + mpa_data_size,
		    iparams->private_data,
		    iparams->private_data_len);

	if (ep->state == ECORE_IWARP_EP_CLOSED) {
		DP_NOTICE(p_hwfn, false,
			  "(0x%x) Reject called on EP in CLOSED state\n",
			  ep->tcp_cid);
		ep->tcp_cid = ECORE_IWARP_INVALID_TCP_CID;
		ecore_iwarp_return_ep(p_hwfn, ep);
		return ECORE_CONN_RESET;
	}

	rc = ecore_iwarp_mpa_offload(p_hwfn, ep);
	return rc;
}

static void
ecore_iwarp_print_cm_info(struct ecore_hwfn *p_hwfn,
			  struct ecore_iwarp_cm_info *cm_info)
{
	DP_VERBOSE(p_hwfn, ECORE_MSG_RDMA, "ip_version = %d\n",
		   cm_info->ip_version);
	DP_VERBOSE(p_hwfn, ECORE_MSG_RDMA, "remote_ip %x.%x.%x.%x\n",
		   cm_info->remote_ip[0],
		   cm_info->remote_ip[1],
		   cm_info->remote_ip[2],
		   cm_info->remote_ip[3]);
	DP_VERBOSE(p_hwfn, ECORE_MSG_RDMA, "local_ip %x.%x.%x.%x\n",
		   cm_info->local_ip[0],
		   cm_info->local_ip[1],
		   cm_info->local_ip[2],
		   cm_info->local_ip[3]);
	DP_VERBOSE(p_hwfn, ECORE_MSG_RDMA, "remote_port = %x\n",
		   cm_info->remote_port);
	DP_VERBOSE(p_hwfn, ECORE_MSG_RDMA, "local_port = %x\n",
		   cm_info->local_port);
	DP_VERBOSE(p_hwfn, ECORE_MSG_RDMA, "vlan = %x\n",
		   cm_info->vlan);
	DP_VERBOSE(p_hwfn, ECORE_MSG_RDMA, "private_data_len = %x\n",
		   cm_info->private_data_len);
	DP_VERBOSE(p_hwfn, ECORE_MSG_RDMA, "ord = %d\n",
		   cm_info->ord);
	DP_VERBOSE(p_hwfn, ECORE_MSG_RDMA, "ird = %d\n",
		   cm_info->ird);
}

static int
ecore_iwarp_ll2_post_rx(struct ecore_hwfn *p_hwfn,
			struct ecore_iwarp_ll2_buff *buf,
			u8 handle)
{
	enum _ecore_status_t rc;

	rc = ecore_ll2_post_rx_buffer(
		p_hwfn,
		handle,
		buf->data_phys_addr,
		(u16)buf->buff_size,
		buf, 1);

	if (rc) {
		DP_NOTICE(p_hwfn, false,
			  "Failed to repost rx buffer to ll2 rc = %d, handle=%d\n",
			  rc, handle);
		OSAL_DMA_FREE_COHERENT(
			p_hwfn->p_dev,
			buf->data,
			buf->data_phys_addr,
			buf->buff_size);
		OSAL_FREE(p_hwfn->p_dev, buf);
	}

	return rc;
}

static bool
ecore_iwarp_ep_exists(struct ecore_hwfn *p_hwfn,
		      struct ecore_iwarp_listener *listener,
		      struct ecore_iwarp_cm_info *cm_info)
{
	struct ecore_iwarp_ep *ep = OSAL_NULL;
	bool found = false;

	OSAL_SPIN_LOCK(&listener->lock);
	OSAL_LIST_FOR_EACH_ENTRY(ep, &listener->ep_list,
				 list_entry, struct ecore_iwarp_ep) {
		if ((ep->cm_info.local_port == cm_info->local_port) &&
		    (ep->cm_info.remote_port == cm_info->remote_port) &&
		    (ep->cm_info.vlan == cm_info->vlan) &&
		    !OSAL_MEMCMP(&(ep->cm_info.local_ip), cm_info->local_ip,
				 sizeof(cm_info->local_ip)) &&
		    !OSAL_MEMCMP(&(ep->cm_info.remote_ip), cm_info->remote_ip,
				 sizeof(cm_info->remote_ip))) {
				found = true;
				break;
		}
	}

	OSAL_SPIN_UNLOCK(&listener->lock);

	if (found) {
		DP_NOTICE(p_hwfn, false, "SYN received on active connection - dropping\n");
		ecore_iwarp_print_cm_info(p_hwfn, cm_info);

		return true;
	}

	return false;
}

static struct ecore_iwarp_listener *
ecore_iwarp_get_listener(struct ecore_hwfn *p_hwfn,
			 struct ecore_iwarp_cm_info *cm_info)
{
	struct ecore_iwarp_listener *listener = OSAL_NULL;
	static const u32 ip_zero[4] = {0, 0, 0, 0};
	bool found = false;

	ecore_iwarp_print_cm_info(p_hwfn, cm_info);

	OSAL_LIST_FOR_EACH_ENTRY(listener,
				 &p_hwfn->p_rdma_info->iwarp.listen_list,
				 list_entry, struct ecore_iwarp_listener) {

		if (listener->port == cm_info->local_port) {
			/* Any IP (i.e. 0.0.0.0 ) will be treated as any vlan */
			if (!OSAL_MEMCMP(listener->ip_addr,
					 ip_zero,
					 sizeof(ip_zero))) {
				found = true;
				break;
			}

			/* If not any IP -> check vlan as well */
			if (!OSAL_MEMCMP(listener->ip_addr,
					 cm_info->local_ip,
					 sizeof(cm_info->local_ip)) &&

			     (listener->vlan == cm_info->vlan)) {
				found = true;
				break;
			}
		}
	}

	if (found && listener->state == ECORE_IWARP_LISTENER_STATE_ACTIVE) {
		DP_VERBOSE(p_hwfn, ECORE_MSG_RDMA, "listener found = %p\n",
			   listener);
		return listener;
	}
	DP_VERBOSE(p_hwfn, ECORE_MSG_RDMA, "listener not found\n");
	return OSAL_NULL;
}

static enum _ecore_status_t
ecore_iwarp_parse_rx_pkt(struct ecore_hwfn *p_hwfn,
			 struct ecore_iwarp_cm_info *cm_info,
			 void *buf,
			 u8 *remote_mac_addr,
			 u8 *local_mac_addr,
			 int *payload_len,
			 int *tcp_start_offset)
{
	struct ecore_vlan_ethhdr *vethh;
	struct ecore_ethhdr *ethh;
	struct ecore_iphdr *iph;
	struct ecore_ipv6hdr *ip6h;
	struct ecore_tcphdr *tcph;
	bool vlan_valid = false;
	int eth_hlen, ip_hlen;
	u16 eth_type;
	int i;

	ethh = (struct ecore_ethhdr *)buf;
	eth_type = ntohs(ethh->h_proto);
	if (eth_type == ETH_P_8021Q) {
		vlan_valid = true;
		vethh = (struct ecore_vlan_ethhdr *)ethh;
		cm_info->vlan = ntohs(vethh->h_vlan_TCI) & VLAN_VID_MASK;
		eth_type = ntohs(vethh->h_vlan_encapsulated_proto);
	}

	eth_hlen = ETH_HLEN + (vlan_valid ? sizeof(u32) : 0);

	OSAL_MEMCPY(remote_mac_addr,
		    ethh->h_source,
		    ETH_ALEN);

	OSAL_MEMCPY(local_mac_addr,
		    ethh->h_dest,
		    ETH_ALEN);

	DP_VERBOSE(p_hwfn, ECORE_MSG_RDMA, "eth_type =%d Source mac: [0x%x]:[0x%x]:[0x%x]:[0x%x]:[0x%x]:[0x%x]\n",
		   eth_type, ethh->h_source[0], ethh->h_source[1],
		   ethh->h_source[2], ethh->h_source[3],
		   ethh->h_source[4], ethh->h_source[5]);

	DP_VERBOSE(p_hwfn, ECORE_MSG_RDMA, "eth_hlen=%d destination mac: [0x%x]:[0x%x]:[0x%x]:[0x%x]:[0x%x]:[0x%x]\n",
		   eth_hlen, ethh->h_dest[0], ethh->h_dest[1],
		   ethh->h_dest[2], ethh->h_dest[3],
		   ethh->h_dest[4], ethh->h_dest[5]);

	iph = (struct ecore_iphdr *)((u8 *)(ethh) + eth_hlen);

	if (eth_type == ETH_P_IP) {
		if (iph->protocol != IPPROTO_TCP) {
			DP_NOTICE(p_hwfn, false,
				  "Unexpected ip protocol on ll2 %x\n",
				  iph->protocol);
			return ECORE_INVAL;
		}

		cm_info->local_ip[0] = ntohl(iph->daddr);
		cm_info->remote_ip[0] = ntohl(iph->saddr);
		cm_info->ip_version = (enum ecore_tcp_ip_version)TCP_IPV4;

		ip_hlen = (iph->ihl)*sizeof(u32);
		*payload_len = ntohs(iph->tot_len) - ip_hlen;

	} else if (eth_type == ETH_P_IPV6) {
		ip6h = (struct ecore_ipv6hdr *)iph;

		if (ip6h->nexthdr != IPPROTO_TCP) {
			DP_NOTICE(p_hwfn, false,
				  "Unexpected ip protocol on ll2 %x\n",
				  iph->protocol);
			return ECORE_INVAL;
		}

		for (i = 0; i < 4; i++) {
			cm_info->local_ip[i] =
				ntohl(ip6h->daddr.in6_u.u6_addr32[i]);
			cm_info->remote_ip[i] =
				ntohl(ip6h->saddr.in6_u.u6_addr32[i]);
		}
		cm_info->ip_version = (enum ecore_tcp_ip_version)TCP_IPV6;

		ip_hlen = sizeof(*ip6h);
		*payload_len = ntohs(ip6h->payload_len);
	} else {
		DP_NOTICE(p_hwfn, false,
			  "Unexpected ethertype on ll2 %x\n", eth_type);
		return ECORE_INVAL;
	}

	tcph = (struct ecore_tcphdr *)((u8 *)iph + ip_hlen);

	if (!tcph->syn) {
		DP_NOTICE(p_hwfn, false,
			  "Only SYN type packet expected on this ll2 conn, iph->ihl=%d source=%d dest=%d\n",
			  iph->ihl, tcph->source, tcph->dest);
		return ECORE_INVAL;
	}

	cm_info->local_port = ntohs(tcph->dest);
	cm_info->remote_port = ntohs(tcph->source);

	ecore_iwarp_print_cm_info(p_hwfn, cm_info);

	*tcp_start_offset = eth_hlen + ip_hlen;

	return ECORE_SUCCESS;
}

static struct ecore_iwarp_fpdu *
ecore_iwarp_get_curr_fpdu(struct ecore_hwfn *p_hwfn, u16 cid)
{
	struct ecore_iwarp_info *iwarp_info = &p_hwfn->p_rdma_info->iwarp;
	struct ecore_iwarp_fpdu *partial_fpdu;
	u32 idx = cid - ecore_cxt_get_proto_cid_start(p_hwfn, PROTOCOLID_IWARP);

	if (idx >= iwarp_info->max_num_partial_fpdus) {
		DP_ERR(p_hwfn, "Invalid cid %x max_num_partial_fpdus=%x\n", cid,
		       iwarp_info->max_num_partial_fpdus);
		return OSAL_NULL;
	}

	partial_fpdu = &iwarp_info->partial_fpdus[idx];

	return partial_fpdu;
}

enum ecore_iwarp_mpa_pkt_type {
	ECORE_IWARP_MPA_PKT_PACKED,
	ECORE_IWARP_MPA_PKT_PARTIAL,
	ECORE_IWARP_MPA_PKT_UNALIGNED
};

#define ECORE_IWARP_INVALID_FPDU_LENGTH 0xffff
#define ECORE_IWARP_MPA_FPDU_LENGTH_SIZE (2)
#define ECORE_IWARP_MPA_CRC32_DIGEST_SIZE (4)

/* Pad to multiple of 4 */
#define ECORE_IWARP_PDU_DATA_LEN_WITH_PAD(data_len) (((data_len) + 3) & ~3)

#define ECORE_IWARP_FPDU_LEN_WITH_PAD(_mpa_len) \
	(ECORE_IWARP_PDU_DATA_LEN_WITH_PAD(_mpa_len + \
					   ECORE_IWARP_MPA_FPDU_LENGTH_SIZE) + \
					   ECORE_IWARP_MPA_CRC32_DIGEST_SIZE)

/* fpdu can be fragmented over maximum 3 bds: header, partial mpa, unaligned */
#define ECORE_IWARP_MAX_BDS_PER_FPDU 3

char *pkt_type_str[] = {
	"ECORE_IWARP_MPA_PKT_PACKED",
	"ECORE_IWARP_MPA_PKT_PARTIAL",
	"ECORE_IWARP_MPA_PKT_UNALIGNED"
};

static enum _ecore_status_t
ecore_iwarp_recycle_pkt(struct ecore_hwfn *p_hwfn,
			struct ecore_iwarp_fpdu *fpdu,
			struct ecore_iwarp_ll2_buff *buf);

static enum ecore_iwarp_mpa_pkt_type
ecore_iwarp_mpa_classify(struct ecore_hwfn *p_hwfn,
			 struct ecore_iwarp_fpdu *fpdu,
			 u16 tcp_payload_len,
			 u8 *mpa_data)

{
	enum ecore_iwarp_mpa_pkt_type pkt_type;
	u16 mpa_len;

	if (fpdu->incomplete_bytes) {
		pkt_type = ECORE_IWARP_MPA_PKT_UNALIGNED;
		goto out;
	}

	/* special case of one byte remaining... */
	if (tcp_payload_len == 1) {
		/* lower byte will be read next packet */
		fpdu->fpdu_length = *mpa_data << 8;
		pkt_type = ECORE_IWARP_MPA_PKT_PARTIAL;
		goto out;
	}

	mpa_len = ntohs(*((u16 *)(mpa_data)));
	fpdu->fpdu_length = ECORE_IWARP_FPDU_LEN_WITH_PAD(mpa_len);

	if (fpdu->fpdu_length <= tcp_payload_len)
		pkt_type = ECORE_IWARP_MPA_PKT_PACKED;
	else
		pkt_type = ECORE_IWARP_MPA_PKT_PARTIAL;

out:
	DP_VERBOSE(p_hwfn, ECORE_MSG_RDMA,
		   "MPA_ALIGN: %s: fpdu_length=0x%x tcp_payload_len:0x%x\n",
		   pkt_type_str[pkt_type], fpdu->fpdu_length, tcp_payload_len);

	return pkt_type;
}

static void
ecore_iwarp_init_fpdu(struct ecore_iwarp_ll2_buff *buf,
		      struct ecore_iwarp_fpdu *fpdu,
		      struct unaligned_opaque_data *pkt_data,
		      u16 tcp_payload_size, u8 placement_offset)
{
	fpdu->mpa_buf = buf;
	fpdu->pkt_hdr = buf->data_phys_addr + placement_offset;
	fpdu->pkt_hdr_size = pkt_data->tcp_payload_offset;

	fpdu->mpa_frag = buf->data_phys_addr + pkt_data->first_mpa_offset;
	fpdu->mpa_frag_virt = (u8 *)(buf->data) + pkt_data->first_mpa_offset;

	if (tcp_payload_size == 1)
		fpdu->incomplete_bytes = ECORE_IWARP_INVALID_FPDU_LENGTH;
	else if (tcp_payload_size < fpdu->fpdu_length)
		fpdu->incomplete_bytes = fpdu->fpdu_length - tcp_payload_size;
	else
		fpdu->incomplete_bytes = 0; /* complete fpdu */

	fpdu->mpa_frag_len = fpdu->fpdu_length - fpdu->incomplete_bytes;
}

static enum _ecore_status_t
ecore_iwarp_copy_fpdu(struct ecore_hwfn *p_hwfn,
		      struct ecore_iwarp_fpdu *fpdu,
		      struct unaligned_opaque_data *pkt_data,
		      struct ecore_iwarp_ll2_buff *buf,
		      u16 tcp_payload_size)

{
	u8 *tmp_buf = p_hwfn->p_rdma_info->iwarp.mpa_intermediate_buf;
	enum _ecore_status_t rc;

	/* need to copy the data from the partial packet stored in fpdu
	 * to the new buf, for this we also need to move the data currently
	 * placed on the buf. The assumption is that the buffer is big enough
	 * since fpdu_length <= mss, we use an intermediate buffer since
	 * we may need to copy the new data to an overlapping location
	 */
	if ((fpdu->mpa_frag_len + tcp_payload_size) > (u16)buf->buff_size) {
		DP_ERR(p_hwfn,
		       "MPA ALIGN: Unexpected: buffer is not large enough for split fpdu buff_size = %d mpa_frag_len = %d, tcp_payload_size = %d, incomplete_bytes = %d\n",
		       buf->buff_size, fpdu->mpa_frag_len, tcp_payload_size,
		       fpdu->incomplete_bytes);
		return ECORE_INVAL;
	}

	DP_VERBOSE(p_hwfn, ECORE_MSG_RDMA,
		   "MPA ALIGN Copying fpdu: [%p, %d] [%p, %d]\n",
		   fpdu->mpa_frag_virt, fpdu->mpa_frag_len,
		   (u8 *)(buf->data) + pkt_data->first_mpa_offset,
		   tcp_payload_size);

	OSAL_MEMCPY(tmp_buf, fpdu->mpa_frag_virt, fpdu->mpa_frag_len);
	OSAL_MEMCPY(tmp_buf + fpdu->mpa_frag_len,
		    (u8 *)(buf->data) + pkt_data->first_mpa_offset,
		    tcp_payload_size);

	rc = ecore_iwarp_recycle_pkt(p_hwfn, fpdu, fpdu->mpa_buf);
	if (rc)
		return rc;

	/* If we managed to post the buffer copy the data to the new buffer
	 * o/w this will occur in the next round...
	 */
	OSAL_MEMCPY((u8 *)(buf->data), tmp_buf,
		    fpdu->mpa_frag_len + tcp_payload_size);

	fpdu->mpa_buf = buf;
	/* fpdu->pkt_hdr remains as is */
	/* fpdu->mpa_frag is overriden with new buf */
	fpdu->mpa_frag = buf->data_phys_addr;
	fpdu->mpa_frag_virt = buf->data;
	fpdu->mpa_frag_len += tcp_payload_size;

	fpdu->incomplete_bytes -= tcp_payload_size;

	DP_VERBOSE(p_hwfn, ECORE_MSG_RDMA,
		   "MPA ALIGN: split fpdu buff_size = %d mpa_frag_len = %d, tcp_payload_size = %d, incomplete_bytes = %d\n",
		   buf->buff_size, fpdu->mpa_frag_len, tcp_payload_size,
		   fpdu->incomplete_bytes);

	return 0;
}

static void
ecore_iwarp_update_fpdu_length(struct ecore_hwfn *p_hwfn,
			       struct ecore_iwarp_fpdu *fpdu,
			       u8 *mpa_data)
{
	u16 mpa_len;

	/* Update incomplete packets if needed */
	if (fpdu->incomplete_bytes == ECORE_IWARP_INVALID_FPDU_LENGTH) {
		mpa_len = fpdu->fpdu_length | *mpa_data;
		fpdu->fpdu_length = ECORE_IWARP_FPDU_LEN_WITH_PAD(mpa_len);
		fpdu->mpa_frag_len = fpdu->fpdu_length;
		/* one byte of hdr */
		fpdu->incomplete_bytes = fpdu->fpdu_length - 1;
		DP_VERBOSE(p_hwfn,
			   ECORE_MSG_RDMA,
			   "MPA_ALIGN: Partial header mpa_len=%x fpdu_length=%x incomplete_bytes=%x\n",
			   mpa_len, fpdu->fpdu_length, fpdu->incomplete_bytes);
	}
}

#define ECORE_IWARP_IS_RIGHT_EDGE(_curr_pkt) \
	(GET_FIELD(_curr_pkt->flags, \
		   UNALIGNED_OPAQUE_DATA_PKT_REACHED_WIN_RIGHT_EDGE))

/* This function is used to recycle a buffer using the ll2 drop option. It
 * uses the mechanism to ensure that all buffers posted to tx before this one
 * were completed. The buffer sent here will be sent as a cookie in the tx
 * completion function and can then be reposted to rx chain when done. The flow
 * that requires this is the flow where a FPDU splits over more than 3 tcp
 * segments. In this case the driver needs to re-post a rx buffer instead of
 * the one received, but driver can't simply repost a buffer it copied from
 * as there is a case where the buffer was originally a packed FPDU, and is
 * partially posted to FW. Driver needs to ensure FW is done with it.
 */
static enum _ecore_status_t
ecore_iwarp_recycle_pkt(struct ecore_hwfn *p_hwfn,
			struct ecore_iwarp_fpdu *fpdu,
			struct ecore_iwarp_ll2_buff *buf)
{
	struct ecore_ll2_tx_pkt_info tx_pkt;
	enum _ecore_status_t rc;
	u8 ll2_handle;

	OSAL_MEM_ZERO(&tx_pkt, sizeof(tx_pkt));
	tx_pkt.num_of_bds = 1;
	tx_pkt.tx_dest = ECORE_LL2_TX_DEST_DROP;
	tx_pkt.l4_hdr_offset_w = fpdu->pkt_hdr_size >> 2;
	tx_pkt.first_frag = fpdu->pkt_hdr;
	tx_pkt.first_frag_len = fpdu->pkt_hdr_size;
	buf->piggy_buf = OSAL_NULL;
	tx_pkt.cookie = buf;

	ll2_handle = p_hwfn->p_rdma_info->iwarp.ll2_mpa_handle;

	rc = ecore_ll2_prepare_tx_packet(p_hwfn,
					 ll2_handle,
					 &tx_pkt, true);

	DP_VERBOSE(p_hwfn, ECORE_MSG_RDMA,
		   "MPA_ALIGN: send drop tx packet [%lx, 0x%x], buf=%p, rc=%d\n",
		   (long unsigned int)tx_pkt.first_frag,
		   tx_pkt.first_frag_len, buf, rc);

	if (rc)
		DP_VERBOSE(p_hwfn, ECORE_MSG_RDMA,
			   "Can't drop packet rc=%d\n", rc);

	return rc;
}

static enum _ecore_status_t
ecore_iwarp_win_right_edge(struct ecore_hwfn *p_hwfn,
			   struct ecore_iwarp_fpdu *fpdu)
{
	struct ecore_ll2_tx_pkt_info tx_pkt;
	enum _ecore_status_t rc;
	u8 ll2_handle;

	OSAL_MEM_ZERO(&tx_pkt, sizeof(tx_pkt));
	tx_pkt.num_of_bds = 1;
	tx_pkt.tx_dest = ECORE_LL2_TX_DEST_LB;
	tx_pkt.l4_hdr_offset_w = fpdu->pkt_hdr_size >> 2;

	tx_pkt.first_frag = fpdu->pkt_hdr;
	tx_pkt.first_frag_len = fpdu->pkt_hdr_size;
	tx_pkt.enable_ip_cksum = true;
	tx_pkt.enable_l4_cksum = true;
	tx_pkt.calc_ip_len = true;
	/* vlan overload with enum iwarp_ll2_tx_queues */
	tx_pkt.vlan = IWARP_LL2_ALIGNED_RIGHT_TRIMMED_TX_QUEUE;

	ll2_handle = p_hwfn->p_rdma_info->iwarp.ll2_mpa_handle;

	rc = ecore_ll2_prepare_tx_packet(p_hwfn,
					 ll2_handle,
					 &tx_pkt, true);

	DP_VERBOSE(p_hwfn, ECORE_MSG_RDMA,
		   "MPA_ALIGN: Sent right edge FPDU num_bds=%d [%lx, 0x%x], rc=%d\n",
		   tx_pkt.num_of_bds, (long unsigned int)tx_pkt.first_frag,
		   tx_pkt.first_frag_len, rc);

	if (rc)
		DP_VERBOSE(p_hwfn, ECORE_MSG_RDMA,
			   "Can't send right edge rc=%d\n", rc);

	return rc;
}

static enum _ecore_status_t
ecore_iwarp_send_fpdu(struct ecore_hwfn *p_hwfn,
		      struct ecore_iwarp_fpdu *fpdu,
		      struct unaligned_opaque_data *curr_pkt,
		      struct ecore_iwarp_ll2_buff *buf,
		      u16 tcp_payload_size,
		      enum ecore_iwarp_mpa_pkt_type pkt_type)
{
	struct ecore_ll2_tx_pkt_info tx_pkt;
	enum _ecore_status_t rc;
	u8 ll2_handle;

	OSAL_MEM_ZERO(&tx_pkt, sizeof(tx_pkt));

	tx_pkt.num_of_bds = (pkt_type == ECORE_IWARP_MPA_PKT_UNALIGNED) ? 3 : 2;
	tx_pkt.tx_dest = ECORE_LL2_TX_DEST_LB;
	tx_pkt.l4_hdr_offset_w = fpdu->pkt_hdr_size >> 2;

	/* Send the mpa_buf only with the last fpdu (in case of packed) */
	if ((pkt_type == ECORE_IWARP_MPA_PKT_UNALIGNED) ||
	    (tcp_payload_size <= fpdu->fpdu_length))
		tx_pkt.cookie = fpdu->mpa_buf;

	tx_pkt.first_frag = fpdu->pkt_hdr;
	tx_pkt.first_frag_len = fpdu->pkt_hdr_size;
	tx_pkt.enable_ip_cksum = true;
	tx_pkt.enable_l4_cksum = true;
	tx_pkt.calc_ip_len = true;
	/* vlan overload with enum iwarp_ll2_tx_queues */
	tx_pkt.vlan = IWARP_LL2_ALIGNED_TX_QUEUE;

	/* special case of unaligned packet and not packed, need to send
	 * both buffers as cookie to release.
	 */
	if (tcp_payload_size == fpdu->incomplete_bytes) {
		fpdu->mpa_buf->piggy_buf = buf;
	}

	ll2_handle = p_hwfn->p_rdma_info->iwarp.ll2_mpa_handle;

	rc = ecore_ll2_prepare_tx_packet(p_hwfn,
					 ll2_handle,
					 &tx_pkt, true);
	if (rc)
		goto err;

	rc = ecore_ll2_set_fragment_of_tx_packet(p_hwfn, ll2_handle,
						 fpdu->mpa_frag,
						 fpdu->mpa_frag_len);
	if (rc)
		goto err;

	if (fpdu->incomplete_bytes) {
		rc = ecore_ll2_set_fragment_of_tx_packet(
			p_hwfn, ll2_handle,
			buf->data_phys_addr + curr_pkt->first_mpa_offset,
			fpdu->incomplete_bytes);

		if (rc)
			goto err;
	}

err:
	DP_VERBOSE(p_hwfn, ECORE_MSG_RDMA,
		   "MPA_ALIGN: Sent FPDU num_bds=%d [%lx, 0x%x], [0x%lx, 0x%x], [0x%lx, 0x%x] (cookie %p) rc=%d\n",
		   tx_pkt.num_of_bds, (long unsigned int)tx_pkt.first_frag,
		   tx_pkt.first_frag_len, (long unsigned int)fpdu->mpa_frag,
		   fpdu->mpa_frag_len, (long unsigned int)buf->data_phys_addr +
		   curr_pkt->first_mpa_offset, fpdu->incomplete_bytes,
		   tx_pkt.cookie, rc);

	return rc;
}

static void
ecore_iwarp_mpa_get_data(struct ecore_hwfn *p_hwfn,
			 struct unaligned_opaque_data *curr_pkt,
			 u32 opaque_data0, u32 opaque_data1)
{
	u64 opaque_data;

	opaque_data = HILO_64(opaque_data1, opaque_data0);
	*curr_pkt = *((struct unaligned_opaque_data *)&opaque_data);

	/* fix endianity */
	curr_pkt->first_mpa_offset = curr_pkt->tcp_payload_offset +
		OSAL_LE16_TO_CPU(curr_pkt->first_mpa_offset);
	curr_pkt->cid = OSAL_LE32_TO_CPU(curr_pkt->cid);

	DP_VERBOSE(p_hwfn, ECORE_MSG_RDMA,
		   "OPAQUE0=0x%x OPAQUE1=0x%x first_mpa_offset:0x%x\ttcp_payload_offset:0x%x\tflags:0x%x\tcid:0x%x\n",
		   opaque_data0, opaque_data1, curr_pkt->first_mpa_offset,
		   curr_pkt->tcp_payload_offset, curr_pkt->flags,
		   curr_pkt->cid);
}

static void
ecore_iwarp_mpa_print_tcp_seq(struct ecore_hwfn *p_hwfn,
			      void *buf)
{
	struct ecore_vlan_ethhdr *vethh;
	struct ecore_ethhdr *ethh;
	struct ecore_iphdr *iph;
	struct ecore_ipv6hdr *ip6h;
	struct ecore_tcphdr *tcph;
	bool vlan_valid = false;
	int eth_hlen, ip_hlen;
	u16 eth_type;

	if ((p_hwfn->dp_level > ECORE_LEVEL_VERBOSE) ||
	    !(p_hwfn->dp_module & ECORE_MSG_RDMA))
		return;

	ethh = (struct ecore_ethhdr *)buf;
	eth_type = ntohs(ethh->h_proto);
	if (eth_type == ETH_P_8021Q) {
		vlan_valid = true;
		vethh = (struct ecore_vlan_ethhdr *)ethh;
		eth_type = ntohs(vethh->h_vlan_encapsulated_proto);
	}

	eth_hlen = ETH_HLEN + (vlan_valid ? sizeof(u32) : 0);

	iph = (struct ecore_iphdr *)((u8 *)(ethh) + eth_hlen);

	if (eth_type == ETH_P_IP) {
		ip_hlen = (iph->ihl)*sizeof(u32);
	} else if (eth_type == ETH_P_IPV6) {
		ip6h = (struct ecore_ipv6hdr *)iph;
		ip_hlen = sizeof(*ip6h);
	} else {
		DP_ERR(p_hwfn, "Unexpected ethertype on ll2 %x\n", eth_type);
		return;
	}

	tcph = (struct ecore_tcphdr *)((u8 *)iph + ip_hlen);

	DP_VERBOSE(p_hwfn, ECORE_MSG_RDMA, "Processing MPA PKT: tcp_seq=0x%x tcp_ack_seq=0x%x\n",
		   ntohl(tcph->seq), ntohl(tcph->ack_seq));

	DP_VERBOSE(p_hwfn, ECORE_MSG_RDMA, "eth_type =%d Source mac: [0x%x]:[0x%x]:[0x%x]:[0x%x]:[0x%x]:[0x%x]\n",
		   eth_type, ethh->h_source[0], ethh->h_source[1],
		   ethh->h_source[2], ethh->h_source[3],
		   ethh->h_source[4], ethh->h_source[5]);

	DP_VERBOSE(p_hwfn, ECORE_MSG_RDMA, "eth_hlen=%d destination mac: [0x%x]:[0x%x]:[0x%x]:[0x%x]:[0x%x]:[0x%x]\n",
		   eth_hlen, ethh->h_dest[0], ethh->h_dest[1],
		   ethh->h_dest[2], ethh->h_dest[3],
		   ethh->h_dest[4], ethh->h_dest[5]);

	return;
}

/* This function is called when an unaligned or incomplete MPA packet arrives
 * driver needs to align the packet, perhaps using previous data and send
 * it down to FW once it is aligned.
 */
static enum _ecore_status_t
ecore_iwarp_process_mpa_pkt(struct ecore_hwfn *p_hwfn,
			    struct ecore_iwarp_ll2_mpa_buf *mpa_buf)
{
	struct ecore_iwarp_ll2_buff *buf = mpa_buf->ll2_buf;
	enum ecore_iwarp_mpa_pkt_type pkt_type;
	struct unaligned_opaque_data *curr_pkt = &mpa_buf->data;
	struct ecore_iwarp_fpdu *fpdu;
	u8 *mpa_data;
	enum _ecore_status_t rc = ECORE_SUCCESS;

	ecore_iwarp_mpa_print_tcp_seq(
		p_hwfn, (u8 *)(buf->data) + mpa_buf->placement_offset);

	fpdu = ecore_iwarp_get_curr_fpdu(p_hwfn, curr_pkt->cid & 0xffff);
	if (!fpdu) {/* something corrupt with cid, post rx back */
		DP_ERR(p_hwfn, "Invalid cid, drop and post back to rx cid=%x\n",
		       curr_pkt->cid);
		rc = ecore_iwarp_ll2_post_rx(
			p_hwfn, buf, p_hwfn->p_rdma_info->iwarp.ll2_mpa_handle);

		if (rc) { /* not much we can do here except log and free */
			DP_ERR(p_hwfn, "Post rx buffer failed\n");

			/* we don't expect any failures from rx, not even
			 * busy since we allocate #bufs=#descs
			 */
			rc = ECORE_UNKNOWN_ERROR;
		}
		return rc;
	}

	do {
		mpa_data = ((u8 *)(buf->data) + curr_pkt->first_mpa_offset);

		pkt_type = ecore_iwarp_mpa_classify(p_hwfn, fpdu,
						    mpa_buf->tcp_payload_len,
						    mpa_data);

		switch (pkt_type) {
		case ECORE_IWARP_MPA_PKT_PARTIAL:
			ecore_iwarp_init_fpdu(buf, fpdu,
					      curr_pkt,
					      mpa_buf->tcp_payload_len,
					      mpa_buf->placement_offset);

			if (!ECORE_IWARP_IS_RIGHT_EDGE(curr_pkt)) {
				mpa_buf->tcp_payload_len = 0;
				break;
			}

			rc = ecore_iwarp_win_right_edge(p_hwfn, fpdu);

			if (rc) {
				DP_VERBOSE(p_hwfn, ECORE_MSG_RDMA,
					   "Can't send FPDU:reset rc=%d\n", rc);
				OSAL_MEM_ZERO(fpdu, sizeof(*fpdu));
				break;
			}

			mpa_buf->tcp_payload_len = 0;
			break;
		case ECORE_IWARP_MPA_PKT_PACKED:
			if (fpdu->fpdu_length == 8) {
				DP_ERR(p_hwfn, "SUSPICIOUS fpdu_length = 0x%x: assuming bug...aborting this packet...\n",
				       fpdu->fpdu_length);
				mpa_buf->tcp_payload_len = 0;
				break;
			}

			ecore_iwarp_init_fpdu(buf, fpdu,
					      curr_pkt,
					      mpa_buf->tcp_payload_len,
					      mpa_buf->placement_offset);

			rc = ecore_iwarp_send_fpdu(p_hwfn, fpdu, curr_pkt, buf,
						   mpa_buf->tcp_payload_len,
						   pkt_type);
			if (rc) {
				DP_VERBOSE(p_hwfn, ECORE_MSG_RDMA,
					   "Can't send FPDU:reset rc=%d\n", rc);
				OSAL_MEM_ZERO(fpdu, sizeof(*fpdu));
				break;
			}
			mpa_buf->tcp_payload_len -= fpdu->fpdu_length;
			curr_pkt->first_mpa_offset += fpdu->fpdu_length;
			break;
		case ECORE_IWARP_MPA_PKT_UNALIGNED:
			ecore_iwarp_update_fpdu_length(p_hwfn, fpdu, mpa_data);
			if (mpa_buf->tcp_payload_len < fpdu->incomplete_bytes) {
				/* special handling of fpdu split over more
				 * than 2 segments
				 */
				if (ECORE_IWARP_IS_RIGHT_EDGE(curr_pkt)) {
					rc = ecore_iwarp_win_right_edge(p_hwfn,
									fpdu);
					/* packet will be re-processed later */
					if (rc)
						return rc;
				}

				rc = ecore_iwarp_copy_fpdu(
					p_hwfn, fpdu, curr_pkt,
					buf, mpa_buf->tcp_payload_len);

				/* packet will be re-processed later */
				if (rc)
					return rc;

				mpa_buf->tcp_payload_len = 0;

				break;
			}

			rc = ecore_iwarp_send_fpdu(p_hwfn, fpdu, curr_pkt, buf,
						   mpa_buf->tcp_payload_len,
						   pkt_type);
			if (rc) {
				DP_VERBOSE(p_hwfn, ECORE_MSG_RDMA,
					   "Can't send FPDU:delay rc=%d\n", rc);
				/* don't reset fpdu -> we need it for next
				 * classify
				 */
				break;
			}
			mpa_buf->tcp_payload_len -= fpdu->incomplete_bytes;
			curr_pkt->first_mpa_offset += fpdu->incomplete_bytes;
			/* The framed PDU was sent - no more incomplete bytes */
			fpdu->incomplete_bytes = 0;
			break;
		}

	} while (mpa_buf->tcp_payload_len && !rc);

	return rc;
}

static void
ecore_iwarp_process_pending_pkts(struct ecore_hwfn *p_hwfn)
{
	struct ecore_iwarp_info *iwarp_info = &p_hwfn->p_rdma_info->iwarp;
	struct ecore_iwarp_ll2_mpa_buf *mpa_buf = OSAL_NULL;
	enum _ecore_status_t rc;

	while (!OSAL_LIST_IS_EMPTY(&iwarp_info->mpa_buf_pending_list)) {
		mpa_buf = OSAL_LIST_FIRST_ENTRY(
			&iwarp_info->mpa_buf_pending_list,
			struct ecore_iwarp_ll2_mpa_buf,
			list_entry);

		rc = ecore_iwarp_process_mpa_pkt(p_hwfn, mpa_buf);

		 /* busy means break and continue processing later, don't
		  * remove the buf from the pending list.
		  */
		if (rc == ECORE_BUSY)
			break;

#ifdef _NTDDK_
#pragma warning(suppress : 6011)
#pragma warning(suppress : 28182)
#endif
		OSAL_LIST_REMOVE_ENTRY(
			&mpa_buf->list_entry,
			&iwarp_info->mpa_buf_pending_list);

		OSAL_LIST_PUSH_TAIL(&mpa_buf->list_entry,
				    &iwarp_info->mpa_buf_list);

		if (rc) { /* different error, don't continue */
			DP_NOTICE(p_hwfn, false, "process pkts failed rc=%d\n",
				  rc);
			break;
		}
	}
}

static void
ecore_iwarp_ll2_comp_mpa_pkt(void *cxt,
			     struct ecore_ll2_comp_rx_data *data)
{
	struct ecore_hwfn *p_hwfn = (struct ecore_hwfn *)cxt;
	struct ecore_iwarp_info *iwarp_info = &p_hwfn->p_rdma_info->iwarp;
	struct ecore_iwarp_ll2_mpa_buf *mpa_buf;

	iwarp_info->unalign_rx_comp++;

	mpa_buf = OSAL_LIST_FIRST_ENTRY(&iwarp_info->mpa_buf_list,
					struct ecore_iwarp_ll2_mpa_buf,
					list_entry);

	DP_VERBOSE(p_hwfn, ECORE_MSG_RDMA,
		   "LL2 MPA CompRx buf=%p placement_offset=%d, payload_len=0x%x mpa_buf=%p\n",
		   data->cookie, data->u.placement_offset,
		   data->length.packet_length, mpa_buf);

	if (!mpa_buf) {
		DP_ERR(p_hwfn, "no free mpa buf. this is a driver bug.\n");
		return;
	}
	OSAL_LIST_REMOVE_ENTRY(&mpa_buf->list_entry, &iwarp_info->mpa_buf_list);

	ecore_iwarp_mpa_get_data(p_hwfn, &mpa_buf->data,
				 data->opaque_data_0, data->opaque_data_1);

	mpa_buf->tcp_payload_len = data->length.packet_length -
		 mpa_buf->data.first_mpa_offset;
	mpa_buf->ll2_buf = (struct ecore_iwarp_ll2_buff *)data->cookie;
	mpa_buf->data.first_mpa_offset += data->u.placement_offset;
	mpa_buf->placement_offset = data->u.placement_offset;

	OSAL_LIST_PUSH_TAIL(&mpa_buf->list_entry,
			    &iwarp_info->mpa_buf_pending_list);

	ecore_iwarp_process_pending_pkts(p_hwfn);
}

static void
ecore_iwarp_ll2_comp_syn_pkt(void *cxt, struct ecore_ll2_comp_rx_data *data)
{
	struct ecore_hwfn *p_hwfn = (struct ecore_hwfn *)cxt;
	struct ecore_iwarp_ll2_buff *buf =
		(struct ecore_iwarp_ll2_buff *)data->cookie;
	struct ecore_iwarp_listener *listener;
	struct ecore_iwarp_cm_info cm_info;
	struct ecore_ll2_tx_pkt_info tx_pkt;
	u8 remote_mac_addr[ETH_ALEN];
	u8 local_mac_addr[ETH_ALEN];
	struct ecore_iwarp_ep *ep;
	enum _ecore_status_t rc;
	int tcp_start_offset;
	u8 ts_hdr_size = 0;
	int payload_len;
	u32 hdr_size;

	OSAL_MEM_ZERO(&cm_info, sizeof(cm_info));

	/* Check if packet was received with errors... */
	if (data->err_flags != 0) {
		DP_NOTICE(p_hwfn, false, "Error received on SYN packet: 0x%x\n",
			  data->err_flags);
		goto err;
	}

	if (GET_FIELD(data->parse_flags,
		      PARSING_AND_ERR_FLAGS_L4CHKSMWASCALCULATED) &&
	    GET_FIELD(data->parse_flags,
		      PARSING_AND_ERR_FLAGS_L4CHKSMERROR)) {
		DP_NOTICE(p_hwfn, false, "Syn packet received with checksum error\n");
		goto err;
	}

	rc = ecore_iwarp_parse_rx_pkt(
		p_hwfn, &cm_info, (u8 *)(buf->data) + data->u.placement_offset,
		remote_mac_addr, local_mac_addr, &payload_len,
		&tcp_start_offset);
	if (rc)
		goto err;

	/* Check if there is a listener for this 4-tuple */
	listener = ecore_iwarp_get_listener(p_hwfn, &cm_info);
	if (!listener) {
		DP_VERBOSE(p_hwfn, ECORE_MSG_RDMA,
			   "SYN received on tuple not listened on parse_flags=%d packet len=%d\n",
			   data->parse_flags, data->length.packet_length);

		OSAL_MEMSET(&tx_pkt, 0, sizeof(tx_pkt));
		tx_pkt.num_of_bds = 1;
		tx_pkt.bd_flags = 0;
		tx_pkt.l4_hdr_offset_w = (data->length.packet_length) >> 2;
		tx_pkt.tx_dest = ECORE_LL2_TX_DEST_LB;
		tx_pkt.first_frag = buf->data_phys_addr +
			data->u.placement_offset;
		tx_pkt.first_frag_len = data->length.packet_length;
		tx_pkt.cookie = buf;

		rc = ecore_ll2_prepare_tx_packet(
			p_hwfn,
			p_hwfn->p_rdma_info->iwarp.ll2_syn_handle,
			&tx_pkt, true);

		if (rc) {
			DP_NOTICE(p_hwfn, false,
				  "Can't post SYN back to chip rc=%d\n", rc);
			goto err;
		}
		return;
	}

	DP_VERBOSE(p_hwfn, ECORE_MSG_RDMA, "Received syn on listening port\n");

	/* For debugging purpose... */
	if (listener->drop)
		goto err;

	/* There may be an open ep on this connection if this is a syn
	 * retrasnmit... need to make sure there isn't...
	 */
	if (ecore_iwarp_ep_exists(p_hwfn, listener, &cm_info))
		goto err;

	ep = ecore_iwarp_get_free_ep(p_hwfn);
	if (ep == OSAL_NULL)
		goto err;

	OSAL_SPIN_LOCK(&listener->lock);
	OSAL_LIST_PUSH_TAIL(&ep->list_entry, &listener->ep_list);
	OSAL_SPIN_UNLOCK(&listener->lock);

	OSAL_MEMCPY(ep->remote_mac_addr,
		    remote_mac_addr,
		    ETH_ALEN);
	OSAL_MEMCPY(ep->local_mac_addr,
		    local_mac_addr,
		    ETH_ALEN);

	OSAL_MEMCPY(&ep->cm_info, &cm_info, sizeof(ep->cm_info));

	if (p_hwfn->p_rdma_info->iwarp.tcp_flags & ECORE_IWARP_TS_EN)
		ts_hdr_size = TIMESTAMP_HEADER_SIZE;

	hdr_size = ((cm_info.ip_version == ECORE_TCP_IPV4) ? 40 : 60) +
		ts_hdr_size;
	ep->mss = p_hwfn->p_rdma_info->iwarp.max_mtu - hdr_size;
	ep->mss = OSAL_MIN_T(u16, ECORE_IWARP_MAX_FW_MSS, ep->mss);

	ep->listener = listener;
	ep->event_cb = listener->event_cb;
	ep->cb_context = listener->cb_context;
	ep->connect_mode = TCP_CONNECT_PASSIVE;

	ep->syn = buf;
	ep->syn_ip_payload_length = (u16)payload_len;
	ep->syn_phy_addr = buf->data_phys_addr + data->u.placement_offset +
		tcp_start_offset;

	rc = ecore_iwarp_tcp_offload(p_hwfn, ep);
	if (rc != ECORE_SUCCESS) {
		ecore_iwarp_return_ep(p_hwfn, ep);
		goto err;
	}
	return;

err:
	ecore_iwarp_ll2_post_rx(
		p_hwfn, buf, p_hwfn->p_rdma_info->iwarp.ll2_syn_handle);
}

static void
ecore_iwarp_ll2_rel_rx_pkt(void *cxt,
			   u8 OSAL_UNUSED connection_handle,
			   void *cookie,
			   dma_addr_t OSAL_UNUSED rx_buf_addr,
			   bool OSAL_UNUSED b_last_packet)
{
	struct ecore_hwfn *p_hwfn = (struct ecore_hwfn *)cxt;
	struct ecore_iwarp_ll2_buff *buffer =
		(struct ecore_iwarp_ll2_buff *)cookie;

	OSAL_DMA_FREE_COHERENT(p_hwfn->p_dev,
			       buffer->data,
			       buffer->data_phys_addr,
			       buffer->buff_size);

	OSAL_FREE(p_hwfn->p_dev, buffer);
}

static void
ecore_iwarp_ll2_comp_tx_pkt(void *cxt,
			    u8 connection_handle,
			    void *cookie,
			    dma_addr_t OSAL_UNUSED first_frag_addr,
			    bool OSAL_UNUSED b_last_fragment,
			    bool OSAL_UNUSED b_last_packet)
{
	struct ecore_hwfn *p_hwfn = (struct ecore_hwfn *)cxt;
	struct ecore_iwarp_ll2_buff *buffer =
		(struct ecore_iwarp_ll2_buff *)cookie;
	struct ecore_iwarp_ll2_buff *piggy;

	if (!buffer) /* can happen in packed mpa unaligned... */
		return;

	DP_VERBOSE(p_hwfn, ECORE_MSG_RDMA,
		   "LL2 CompTX buf=%p piggy_buf=%p handle=%d\n",
		   buffer, buffer->piggy_buf, connection_handle);

	/* we got a tx packet -> this was originally a rx packet... now we
	 * can post it back...
	 */
	piggy = buffer->piggy_buf;
	if (piggy) {
		buffer->piggy_buf = OSAL_NULL;
		ecore_iwarp_ll2_post_rx(p_hwfn, piggy,
					connection_handle);
	}

	ecore_iwarp_ll2_post_rx(p_hwfn, buffer,
				connection_handle);

	if (connection_handle == p_hwfn->p_rdma_info->iwarp.ll2_mpa_handle)
		ecore_iwarp_process_pending_pkts(p_hwfn);

	return;
}

static void
ecore_iwarp_ll2_rel_tx_pkt(void *cxt,
			   u8 OSAL_UNUSED connection_handle,
			   void *cookie,
			   dma_addr_t OSAL_UNUSED first_frag_addr,
			   bool OSAL_UNUSED b_last_fragment,
			   bool OSAL_UNUSED b_last_packet)
{
	struct ecore_hwfn *p_hwfn = (struct ecore_hwfn *)cxt;
	struct ecore_iwarp_ll2_buff *buffer =
		(struct ecore_iwarp_ll2_buff *)cookie;

	if (!buffer)
		return;

	if (buffer->piggy_buf) {
		OSAL_DMA_FREE_COHERENT(
			p_hwfn->p_dev,
			buffer->piggy_buf->data,
			buffer->piggy_buf->data_phys_addr,
			buffer->piggy_buf->buff_size);

		OSAL_FREE(p_hwfn->p_dev, buffer->piggy_buf);
	}

	OSAL_DMA_FREE_COHERENT(p_hwfn->p_dev,
			       buffer->data,
			       buffer->data_phys_addr,
			       buffer->buff_size);

	OSAL_FREE(p_hwfn->p_dev, buffer);
	return;
}

/* Current known slowpath for iwarp ll2 is unalign flush. When this completion
 * is received, need to reset the FPDU.
 */
static void
ecore_iwarp_ll2_slowpath(void *cxt,
			 u8 OSAL_UNUSED connection_handle,
			 u32 opaque_data_0,
			 u32 opaque_data_1)
{
	struct ecore_hwfn *p_hwfn = (struct ecore_hwfn *)cxt;
	struct unaligned_opaque_data unalign_data;
	struct ecore_iwarp_fpdu *fpdu;

	ecore_iwarp_mpa_get_data(p_hwfn, &unalign_data,
				 opaque_data_0, opaque_data_1);

	DP_VERBOSE(p_hwfn, ECORE_MSG_RDMA, "(0x%x) Flush fpdu\n",
		   unalign_data.cid);

	fpdu = ecore_iwarp_get_curr_fpdu(p_hwfn, (u16)unalign_data.cid);
	if (fpdu)
		OSAL_MEM_ZERO(fpdu, sizeof(*fpdu));
}

static int
ecore_iwarp_ll2_stop(struct ecore_hwfn *p_hwfn)
{
	struct ecore_iwarp_info *iwarp_info = &p_hwfn->p_rdma_info->iwarp;
	int rc = 0;

	if (iwarp_info->ll2_syn_handle != ECORE_IWARP_HANDLE_INVAL) {

		rc = ecore_ll2_terminate_connection(p_hwfn,
						    iwarp_info->ll2_syn_handle);
		if (rc)
			DP_INFO(p_hwfn, "Failed to terminate syn connection\n");

		ecore_ll2_release_connection(p_hwfn,
					     iwarp_info->ll2_syn_handle);
		iwarp_info->ll2_syn_handle = ECORE_IWARP_HANDLE_INVAL;
	}

	if (iwarp_info->ll2_ooo_handle != ECORE_IWARP_HANDLE_INVAL) {
		rc = ecore_ll2_terminate_connection(p_hwfn,
						    iwarp_info->ll2_ooo_handle);
		if (rc)
			DP_INFO(p_hwfn, "Failed to terminate ooo connection\n");

		ecore_ll2_release_connection(p_hwfn,
					     iwarp_info->ll2_ooo_handle);
		iwarp_info->ll2_ooo_handle = ECORE_IWARP_HANDLE_INVAL;
	}

	if (iwarp_info->ll2_mpa_handle != ECORE_IWARP_HANDLE_INVAL) {
		rc = ecore_ll2_terminate_connection(p_hwfn,
						    iwarp_info->ll2_mpa_handle);
		if (rc)
			DP_INFO(p_hwfn, "Failed to terminate mpa connection\n");

		ecore_ll2_release_connection(p_hwfn,
					     iwarp_info->ll2_mpa_handle);
		iwarp_info->ll2_mpa_handle = ECORE_IWARP_HANDLE_INVAL;
	}

	ecore_llh_remove_mac_filter(p_hwfn->p_dev, 0,
				    p_hwfn->p_rdma_info->iwarp.mac_addr);

	return rc;
}

static int
ecore_iwarp_ll2_alloc_buffers(struct ecore_hwfn *p_hwfn,
			      int num_rx_bufs,
			      int buff_size,
			      u8 ll2_handle)
{
	struct ecore_iwarp_ll2_buff *buffer;
	int rc = 0;
	int i;

	for (i = 0; i < num_rx_bufs; i++) {
		buffer = OSAL_ZALLOC(p_hwfn->p_dev,
				     GFP_KERNEL, sizeof(*buffer));
		if (!buffer) {
			DP_INFO(p_hwfn, "Failed to allocate LL2 buffer desc\n");
			break;
		}

		buffer->data =
			OSAL_DMA_ALLOC_COHERENT(p_hwfn->p_dev,
						&buffer->data_phys_addr,
						buff_size);

		if (!buffer->data) {
			DP_INFO(p_hwfn, "Failed to allocate LL2 buffers\n");
			OSAL_FREE(p_hwfn->p_dev, buffer);
			rc = ECORE_NOMEM;
			break;
		}

		buffer->buff_size = buff_size;
		rc = ecore_iwarp_ll2_post_rx(p_hwfn, buffer, ll2_handle);

		if (rc)
			break; /* buffers will be deallocated by ecore_ll2 */
	}
	return rc;
}

#define ECORE_IWARP_CACHE_PADDING(size) \
	(((size) + ETH_CACHE_LINE_SIZE - 1) & ~(ETH_CACHE_LINE_SIZE - 1))

#define ECORE_IWARP_MAX_BUF_SIZE(mtu) \
	ECORE_IWARP_CACHE_PADDING(mtu + ETH_HLEN + 2*VLAN_HLEN + 2 +\
				  ETH_CACHE_LINE_SIZE)

static int
ecore_iwarp_ll2_start(struct ecore_hwfn *p_hwfn,
		      struct ecore_rdma_start_in_params *params)
{
	struct ecore_iwarp_info *iwarp_info;
	struct ecore_ll2_acquire_data data;
	struct ecore_ll2_cbs cbs;
	u32 mpa_buff_size;
	int rc = ECORE_SUCCESS;
	u16 n_ooo_bufs;
	int i;

	iwarp_info = &p_hwfn->p_rdma_info->iwarp;
	iwarp_info->ll2_syn_handle = ECORE_IWARP_HANDLE_INVAL;
	iwarp_info->ll2_ooo_handle = ECORE_IWARP_HANDLE_INVAL;
	iwarp_info->ll2_mpa_handle = ECORE_IWARP_HANDLE_INVAL;

	iwarp_info->max_mtu = params->max_mtu;

	OSAL_MEMCPY(p_hwfn->p_rdma_info->iwarp.mac_addr, params->mac_addr,
		    ETH_ALEN);

	rc = ecore_llh_add_mac_filter(p_hwfn->p_dev, 0, params->mac_addr);
	if (rc != ECORE_SUCCESS)
		return rc;

	/* Start SYN connection */
	cbs.rx_comp_cb = ecore_iwarp_ll2_comp_syn_pkt;
	cbs.rx_release_cb = ecore_iwarp_ll2_rel_rx_pkt;
	cbs.tx_comp_cb = ecore_iwarp_ll2_comp_tx_pkt;
	cbs.tx_release_cb = ecore_iwarp_ll2_rel_tx_pkt;
	cbs.cookie = p_hwfn;

	OSAL_MEMSET(&data, 0, sizeof(data));
	data.input.conn_type = ECORE_LL2_TYPE_IWARP;
	data.input.mtu = ECORE_IWARP_MAX_SYN_PKT_SIZE;
	data.input.rx_num_desc = ECORE_IWARP_LL2_SYN_RX_SIZE;
	data.input.tx_num_desc = ECORE_IWARP_LL2_SYN_TX_SIZE;
	data.input.tx_max_bds_per_packet = 1; /* will never be fragmented */
	data.input.tx_tc = PKT_LB_TC;
	data.input.tx_dest = ECORE_LL2_TX_DEST_LB;
	data.p_connection_handle = &iwarp_info->ll2_syn_handle;
	data.cbs = &cbs;

	rc = ecore_ll2_acquire_connection(p_hwfn, &data);
	if (rc) {
		DP_NOTICE(p_hwfn, false, "Failed to acquire LL2 connection\n");
		ecore_llh_remove_mac_filter(p_hwfn->p_dev, 0, params->mac_addr);
		return rc;
	}

	rc = ecore_ll2_establish_connection(p_hwfn, iwarp_info->ll2_syn_handle);
	if (rc) {
		DP_NOTICE(p_hwfn, false,
			  "Failed to establish LL2 connection\n");
		goto err;
	}

	rc = ecore_iwarp_ll2_alloc_buffers(p_hwfn,
					   ECORE_IWARP_LL2_SYN_RX_SIZE,
					   ECORE_IWARP_MAX_SYN_PKT_SIZE,
					   iwarp_info->ll2_syn_handle);
	if (rc)
		goto err;

	/* Start OOO connection */
	data.input.conn_type = ECORE_LL2_TYPE_OOO;
	data.input.mtu = params->max_mtu;

	n_ooo_bufs = params->iwarp.ooo_num_rx_bufs;

	if (n_ooo_bufs > ECORE_IWARP_LL2_OOO_MAX_RX_SIZE)
		n_ooo_bufs = ECORE_IWARP_LL2_OOO_MAX_RX_SIZE;

	data.input.rx_num_desc = n_ooo_bufs;
	data.input.rx_num_ooo_buffers = n_ooo_bufs;

	p_hwfn->p_rdma_info->iwarp.num_ooo_rx_bufs = data.input.rx_num_desc;
	data.input.tx_max_bds_per_packet = 1; /* will never be fragmented */
	data.input.tx_num_desc = ECORE_IWARP_LL2_OOO_DEF_TX_SIZE;
	data.p_connection_handle = &iwarp_info->ll2_ooo_handle;
	data.input.secondary_queue = true;

	rc = ecore_ll2_acquire_connection(p_hwfn, &data);
	if (rc)
		goto err;

	rc = ecore_ll2_establish_connection(p_hwfn, iwarp_info->ll2_ooo_handle);
	if (rc)
		goto err;

	/* Start MPA connection */
	cbs.rx_comp_cb = ecore_iwarp_ll2_comp_mpa_pkt;
	cbs.slowpath_cb = ecore_iwarp_ll2_slowpath;

	OSAL_MEMSET(&data, 0, sizeof(data));
	data.input.conn_type = ECORE_LL2_TYPE_IWARP;
	data.input.mtu = params->max_mtu;
	data.input.rx_num_desc = n_ooo_bufs * 2;
	/* we allocate the same amount for TX to reduce the chance we
	 * run out of tx descriptors
	 */
	data.input.tx_num_desc = data.input.rx_num_desc;
	data.input.tx_max_bds_per_packet = ECORE_IWARP_MAX_BDS_PER_FPDU;
	data.p_connection_handle = &iwarp_info->ll2_mpa_handle;
	data.input.secondary_queue = true;
	data.cbs = &cbs;

	rc = ecore_ll2_acquire_connection(p_hwfn, &data);
	if (rc)
		goto err;

	rc = ecore_ll2_establish_connection(p_hwfn, iwarp_info->ll2_mpa_handle);
	if (rc)
		goto err;

	mpa_buff_size = ECORE_IWARP_MAX_BUF_SIZE(params->max_mtu);
	rc = ecore_iwarp_ll2_alloc_buffers(p_hwfn,
					   data.input.rx_num_desc,
					   mpa_buff_size,
					   iwarp_info->ll2_mpa_handle);
	if (rc)
		goto err;

	iwarp_info->partial_fpdus =
		OSAL_ZALLOC(p_hwfn->p_dev, GFP_KERNEL,
			    sizeof(*iwarp_info->partial_fpdus) *
			    (u16)p_hwfn->p_rdma_info->num_qps);

	if (!iwarp_info->partial_fpdus) {
		DP_NOTICE(p_hwfn, false,
			  "Failed to allocate ecore_iwarp_info(partial_fpdus)\n");
		goto err;
	}

	iwarp_info->max_num_partial_fpdus = (u16)p_hwfn->p_rdma_info->num_qps;

	/* The mpa_bufs array serves for pending RX packets received on the
	 * mpa ll2 that don't have place on the tx ring and require later
	 * processing. We can't fail on allocation of such a struct therefore
	 * we allocate enough to take care of all rx packets
	 */
	iwarp_info->mpa_bufs =
		OSAL_ZALLOC(p_hwfn->p_dev, GFP_KERNEL,
			    sizeof(*iwarp_info->mpa_bufs) *
				   data.input.rx_num_desc);

	if (!iwarp_info->mpa_bufs) {
		DP_NOTICE(p_hwfn, false,
			  "Failed to allocate mpa_bufs array mem_size=%d\n",
			  (u32)(sizeof(*iwarp_info->mpa_bufs) *
				data.input.rx_num_desc));
		goto err;
	}

	iwarp_info->mpa_intermediate_buf =
		OSAL_ZALLOC(p_hwfn->p_dev, GFP_KERNEL, mpa_buff_size);
	if (!iwarp_info->mpa_intermediate_buf) {
		DP_NOTICE(p_hwfn, false,
			  "Failed to allocate mpa_intermediate_buf mem_size=%d\n",
			  mpa_buff_size);
		goto err;
	}

	OSAL_LIST_INIT(&iwarp_info->mpa_buf_pending_list);
	OSAL_LIST_INIT(&iwarp_info->mpa_buf_list);
	for (i = 0; i < data.input.rx_num_desc; i++) {
		OSAL_LIST_PUSH_TAIL(&iwarp_info->mpa_bufs[i].list_entry,
				    &iwarp_info->mpa_buf_list);
	}

	return rc;

err:
	ecore_iwarp_ll2_stop(p_hwfn);

	return rc;
}

static void
ecore_iwarp_set_defaults(struct ecore_hwfn *p_hwfn,
			 struct ecore_rdma_start_in_params *params)
{
	u32 rcv_wnd_size;
	u32 n_ooo_bufs;

	/* rcv_wnd_size = 0: use defaults */
	rcv_wnd_size = params->iwarp.rcv_wnd_size;
	if (!rcv_wnd_size) {
		if (ecore_device_num_ports(p_hwfn->p_dev) == 4) {
			rcv_wnd_size = ECORE_IS_AH(p_hwfn->p_dev) ?
				       ECORE_IWARP_RCV_WND_SIZE_AH_DEF_4_PORTS :
				       ECORE_IWARP_RCV_WND_SIZE_BB_DEF_4_PORTS;
		} else {
			rcv_wnd_size = ECORE_IS_AH(p_hwfn->p_dev) ?
				       ECORE_IWARP_RCV_WND_SIZE_AH_DEF_2_PORTS :
				       ECORE_IWARP_RCV_WND_SIZE_BB_DEF_2_PORTS;
		}
		params->iwarp.rcv_wnd_size = rcv_wnd_size;
	}

	n_ooo_bufs = params->iwarp.ooo_num_rx_bufs;
	if (!n_ooo_bufs) {
		n_ooo_bufs = (u32)(((u64)ECORE_MAX_OOO *
			      params->iwarp.rcv_wnd_size) /
			      params->max_mtu);
		n_ooo_bufs = OSAL_MIN_T(u32, n_ooo_bufs, USHRT_MAX);
		params->iwarp.ooo_num_rx_bufs = (u16)n_ooo_bufs;
	}
}

enum _ecore_status_t
ecore_iwarp_setup(struct ecore_hwfn		    *p_hwfn,
		  struct ecore_rdma_start_in_params *params)
{
	enum _ecore_status_t rc = ECORE_SUCCESS;
	struct ecore_iwarp_info *iwarp_info;
	u32 rcv_wnd_size;

	iwarp_info = &(p_hwfn->p_rdma_info->iwarp);

	if (!params->iwarp.rcv_wnd_size || !params->iwarp.ooo_num_rx_bufs)
		ecore_iwarp_set_defaults(p_hwfn, params);

	/* Scale 0 will set window of 0xFFFC (64K -4).
	 * Scale x will set window of 0xFFFC << (x)
	 * Therefore we subtract log2(64K) so that result is 0
	 */
	rcv_wnd_size = params->iwarp.rcv_wnd_size;
	if (rcv_wnd_size < ECORE_IWARP_RCV_WND_SIZE_MIN)
		rcv_wnd_size = ECORE_IWARP_RCV_WND_SIZE_MIN;

	iwarp_info->rcv_wnd_scale = OSAL_MIN_T(u32, OSAL_LOG2(rcv_wnd_size) -
		OSAL_LOG2(ECORE_IWARP_RCV_WND_SIZE_MIN), ECORE_IWARP_MAX_WND_SCALE);
	iwarp_info->rcv_wnd_size = rcv_wnd_size >> iwarp_info->rcv_wnd_scale;

	iwarp_info->tcp_flags = params->iwarp.flags;
	iwarp_info->crc_needed = params->iwarp.crc_needed;
	switch (params->iwarp.mpa_rev) {
	case ECORE_MPA_REV1:
		iwarp_info->mpa_rev = MPA_NEGOTIATION_TYPE_BASIC;
		break;
	case ECORE_MPA_REV2:
		iwarp_info->mpa_rev = MPA_NEGOTIATION_TYPE_ENHANCED;
		break;
	}

	iwarp_info->peer2peer = params->iwarp.mpa_peer2peer;
	iwarp_info->rtr_type = MPA_RTR_TYPE_NONE;

	if (params->iwarp.mpa_rtr & ECORE_MPA_RTR_TYPE_ZERO_SEND)
		iwarp_info->rtr_type |= MPA_RTR_TYPE_ZERO_SEND;

	if (params->iwarp.mpa_rtr & ECORE_MPA_RTR_TYPE_ZERO_WRITE)
		iwarp_info->rtr_type |= MPA_RTR_TYPE_ZERO_WRITE;

	if (params->iwarp.mpa_rtr & ECORE_MPA_RTR_TYPE_ZERO_READ)
		iwarp_info->rtr_type |= MPA_RTR_TYPE_ZERO_READ;

	//DAVIDS OSAL_SPIN_LOCK_INIT(&p_hwfn->p_rdma_info->iwarp.qp_lock);
	OSAL_LIST_INIT(&p_hwfn->p_rdma_info->iwarp.ep_list);
	OSAL_LIST_INIT(&p_hwfn->p_rdma_info->iwarp.listen_list);

	ecore_spq_register_async_cb(p_hwfn, PROTOCOLID_IWARP,
				    ecore_iwarp_async_event);
	ecore_ooo_setup(p_hwfn);

	rc = ecore_iwarp_ll2_start(p_hwfn, params);

	DP_VERBOSE(p_hwfn, ECORE_MSG_RDMA,
		   "MPA_REV = %d. peer2peer=%d rtr=%x\n",
		   iwarp_info->mpa_rev,
		   iwarp_info->peer2peer,
		   iwarp_info->rtr_type);

	return rc;
}

enum _ecore_status_t
ecore_iwarp_stop(struct ecore_hwfn *p_hwfn)
{
	enum _ecore_status_t rc;

	ecore_iwarp_free_prealloc_ep(p_hwfn);
	rc = ecore_iwarp_wait_for_all_cids(p_hwfn);
	if (rc != ECORE_SUCCESS)
		return rc;

	ecore_spq_unregister_async_cb(p_hwfn, PROTOCOLID_IWARP);

	return ecore_iwarp_ll2_stop(p_hwfn);
}

static void
ecore_iwarp_qp_in_error(struct ecore_hwfn *p_hwfn,
			struct ecore_iwarp_ep *ep,
			u8 fw_return_code)
{
	struct ecore_iwarp_cm_event_params params;

	ecore_iwarp_modify_qp(p_hwfn, ep->qp, ECORE_IWARP_QP_STATE_ERROR, true);

	params.event = ECORE_IWARP_EVENT_CLOSE;
	params.ep_context = ep;
	params.cm_info = &ep->cm_info;
	params.status = (fw_return_code == IWARP_QP_IN_ERROR_GOOD_CLOSE) ?
		ECORE_SUCCESS : ECORE_CONN_RESET;

	ep->state = ECORE_IWARP_EP_CLOSED;
	OSAL_SPIN_LOCK(&p_hwfn->p_rdma_info->iwarp.iw_lock);
	OSAL_LIST_REMOVE_ENTRY(&ep->list_entry,
			       &p_hwfn->p_rdma_info->iwarp.ep_list);
	OSAL_SPIN_UNLOCK(&p_hwfn->p_rdma_info->iwarp.iw_lock);

	ep->event_cb(ep->cb_context, &params);
}

static void
ecore_iwarp_exception_received(struct ecore_hwfn *p_hwfn,
			       struct ecore_iwarp_ep *ep,
			       int fw_ret_code)
{
	struct ecore_iwarp_cm_event_params params;
	bool event_cb = false;

	DP_VERBOSE(p_hwfn, ECORE_MSG_RDMA, "EP(0x%x) fw_ret_code=%d\n",
		   ep->cid, fw_ret_code);

	switch (fw_ret_code) {
	case IWARP_EXCEPTION_DETECTED_LLP_CLOSED:
		params.status = ECORE_SUCCESS;
		params.event = ECORE_IWARP_EVENT_DISCONNECT;
		event_cb = true;
		break;
	case IWARP_EXCEPTION_DETECTED_LLP_RESET:
		params.status = ECORE_CONN_RESET;
		params.event = ECORE_IWARP_EVENT_DISCONNECT;
		event_cb = true;
		break;
	case IWARP_EXCEPTION_DETECTED_RQ_EMPTY:
		params.event = ECORE_IWARP_EVENT_RQ_EMPTY;
		event_cb = true;
		break;
	case IWARP_EXCEPTION_DETECTED_IRQ_FULL:
		params.event = ECORE_IWARP_EVENT_IRQ_FULL;
		event_cb = true;
		break;
	case IWARP_EXCEPTION_DETECTED_LLP_TIMEOUT:
		params.event = ECORE_IWARP_EVENT_LLP_TIMEOUT;
		event_cb = true;
		break;
	case IWARP_EXCEPTION_DETECTED_REMOTE_PROTECTION_ERROR:
		params.event = ECORE_IWARP_EVENT_REMOTE_PROTECTION_ERROR;
		event_cb = true;
		break;
	case IWARP_EXCEPTION_DETECTED_CQ_OVERFLOW:
		params.event = ECORE_IWARP_EVENT_CQ_OVERFLOW;
		event_cb = true;
		break;
	case IWARP_EXCEPTION_DETECTED_LOCAL_CATASTROPHIC:
		params.event = ECORE_IWARP_EVENT_QP_CATASTROPHIC;
		event_cb = true;
		break;
	case IWARP_EXCEPTION_DETECTED_LOCAL_ACCESS_ERROR:
		params.event = ECORE_IWARP_EVENT_LOCAL_ACCESS_ERROR;
		event_cb = true;
		break;
	case IWARP_EXCEPTION_DETECTED_REMOTE_OPERATION_ERROR:
		params.event = ECORE_IWARP_EVENT_REMOTE_OPERATION_ERROR;
		event_cb = true;
		break;
	case IWARP_EXCEPTION_DETECTED_TERMINATE_RECEIVED:
		params.event = ECORE_IWARP_EVENT_TERMINATE_RECEIVED;
		event_cb = true;
		break;
	default:
		DP_VERBOSE(p_hwfn, ECORE_MSG_RDMA,
			   "Unhandled exception received...\n");
		break;
	}

	if (event_cb) {
		params.ep_context = ep;
		params.cm_info = &ep->cm_info;
		ep->event_cb(ep->cb_context, &params);
	}
}

static void
ecore_iwarp_tcp_connect_unsuccessful(struct ecore_hwfn *p_hwfn,
				     struct ecore_iwarp_ep *ep,
				     u8 fw_return_code)
{
	struct ecore_iwarp_cm_event_params params;

	OSAL_MEM_ZERO(&params, sizeof(params));
	params.event = ECORE_IWARP_EVENT_ACTIVE_COMPLETE;
	params.ep_context = ep;
	params.cm_info = &ep->cm_info;
	ep->state = ECORE_IWARP_EP_CLOSED;

	switch (fw_return_code) {
	case IWARP_CONN_ERROR_TCP_CONNECT_INVALID_PACKET:
		DP_VERBOSE(p_hwfn, ECORE_MSG_RDMA,
			   "%s(0x%x) TCP connect got invalid packet\n",
			   ECORE_IWARP_CONNECT_MODE_STRING(ep),
			   ep->tcp_cid);
		params.status = ECORE_CONN_RESET;
		break;
	case IWARP_CONN_ERROR_TCP_CONNECTION_RST:
		DP_VERBOSE(p_hwfn, ECORE_MSG_RDMA,
			   "%s(0x%x) TCP Connection Reset\n",
			   ECORE_IWARP_CONNECT_MODE_STRING(ep),
			   ep->tcp_cid);
		params.status = ECORE_CONN_RESET;
		break;
	case IWARP_CONN_ERROR_TCP_CONNECT_TIMEOUT:
		DP_NOTICE(p_hwfn, false, "%s(0x%x) TCP timeout\n",
			  ECORE_IWARP_CONNECT_MODE_STRING(ep),
			  ep->tcp_cid);
		params.status = ECORE_TIMEOUT;
		break;
	case IWARP_CONN_ERROR_MPA_NOT_SUPPORTED_VER:
		DP_NOTICE(p_hwfn, false, "%s(0x%x) MPA not supported VER\n",
			  ECORE_IWARP_CONNECT_MODE_STRING(ep),
			  ep->tcp_cid);
		params.status = ECORE_CONN_REFUSED;
		break;
	case IWARP_CONN_ERROR_MPA_INVALID_PACKET:
		DP_NOTICE(p_hwfn, false, "%s(0x%x) MPA Invalid Packet\n",
			  ECORE_IWARP_CONNECT_MODE_STRING(ep), ep->tcp_cid);
		params.status = ECORE_CONN_RESET;
		break;
	default:
		DP_ERR(p_hwfn, "%s(0x%x) Unexpected return code tcp connect: %d\n",
		       ECORE_IWARP_CONNECT_MODE_STRING(ep), ep->tcp_cid,
		       fw_return_code);
		params.status = ECORE_CONN_RESET;
		break;
	}

	if (ep->connect_mode == TCP_CONNECT_PASSIVE) {
		ep->tcp_cid = ECORE_IWARP_INVALID_TCP_CID;
		ecore_iwarp_return_ep(p_hwfn, ep);
	} else {
		ep->event_cb(ep->cb_context, &params);
		OSAL_SPIN_LOCK(&p_hwfn->p_rdma_info->iwarp.iw_lock);
		OSAL_LIST_REMOVE_ENTRY(&ep->list_entry,
				       &p_hwfn->p_rdma_info->iwarp.ep_list);
		OSAL_SPIN_UNLOCK(&p_hwfn->p_rdma_info->iwarp.iw_lock);
	}
}

static void
ecore_iwarp_connect_complete(struct ecore_hwfn *p_hwfn,
			     struct ecore_iwarp_ep *ep,
			     u8 fw_return_code)
{
	if (ep->connect_mode == TCP_CONNECT_PASSIVE) {
		/* Done with the SYN packet, post back to ll2 rx */
		ecore_iwarp_ll2_post_rx(
			p_hwfn, ep->syn,
			p_hwfn->p_rdma_info->iwarp.ll2_syn_handle);

		ep->syn = OSAL_NULL;

		if (ep->state == ECORE_IWARP_EP_ABORTING)
			return;

		/* If connect failed - upper layer doesn't know about it */
		if (fw_return_code == RDMA_RETURN_OK)
			ecore_iwarp_mpa_received(p_hwfn, ep);
		else
			ecore_iwarp_tcp_connect_unsuccessful(p_hwfn, ep,
							     fw_return_code);

	} else {
		if (fw_return_code == RDMA_RETURN_OK)
			ecore_iwarp_mpa_offload(p_hwfn, ep);
		else
			ecore_iwarp_tcp_connect_unsuccessful(p_hwfn, ep,
							     fw_return_code);
	}
}

static OSAL_INLINE bool
ecore_iwarp_check_ep_ok(struct ecore_hwfn *p_hwfn,
			struct ecore_iwarp_ep *ep)
{
	if (ep == OSAL_NULL) {
		DP_ERR(p_hwfn, "ERROR ON ASYNC ep=%p\n", ep);
		return false;
	}

	if (ep->sig != 0xdeadbeef) {
		DP_ERR(p_hwfn, "ERROR ON ASYNC ep=%p\n", ep);
		return false;
	}

	return true;
}

static enum _ecore_status_t
ecore_iwarp_async_event(struct ecore_hwfn *p_hwfn,
			u8 fw_event_code,
			u16 OSAL_UNUSED echo,
			union event_ring_data *data,
			u8 fw_return_code)
{
	struct regpair *fw_handle = &data->rdma_data.async_handle;
	struct ecore_iwarp_ep *ep = OSAL_NULL;
	u16 cid;

	ep = (struct ecore_iwarp_ep *)(osal_uintptr_t)HILO_64(fw_handle->hi,
							      fw_handle->lo);

	switch (fw_event_code) {
	/* Async completion after TCP 3-way handshake */
	case IWARP_EVENT_TYPE_ASYNC_CONNECT_COMPLETE:
		if (!ecore_iwarp_check_ep_ok(p_hwfn, ep))
			return ECORE_INVAL;
		DP_VERBOSE(p_hwfn, ECORE_MSG_RDMA,
			   "EP(0x%x) IWARP_EVENT_TYPE_ASYNC_CONNECT_COMPLETE fw_ret_code=%d\n",
			   ep->tcp_cid, fw_return_code);
		ecore_iwarp_connect_complete(p_hwfn, ep, fw_return_code);
		break;
	case IWARP_EVENT_TYPE_ASYNC_EXCEPTION_DETECTED:
		if (!ecore_iwarp_check_ep_ok(p_hwfn, ep))
			return ECORE_INVAL;
		DP_VERBOSE(p_hwfn, ECORE_MSG_RDMA,
			   "QP(0x%x) IWARP_EVENT_TYPE_ASYNC_EXCEPTION_DETECTED fw_ret_code=%d\n",
			   ep->cid, fw_return_code);
		ecore_iwarp_exception_received(p_hwfn, ep, fw_return_code);
		break;
	/* Async completion for Close Connection ramrod */
	case IWARP_EVENT_TYPE_ASYNC_QP_IN_ERROR_STATE:
		if (!ecore_iwarp_check_ep_ok(p_hwfn, ep))
			return ECORE_INVAL;
		DP_VERBOSE(p_hwfn, ECORE_MSG_RDMA,
			   "QP(0x%x) IWARP_EVENT_TYPE_ASYNC_QP_IN_ERROR_STATE fw_ret_code=%d\n",
			   ep->cid, fw_return_code);
		ecore_iwarp_qp_in_error(p_hwfn, ep, fw_return_code);
		break;
	/* Async event for active side only */
	case IWARP_EVENT_TYPE_ASYNC_ENHANCED_MPA_REPLY_ARRIVED:
		if (!ecore_iwarp_check_ep_ok(p_hwfn, ep))
			return ECORE_INVAL;
		DP_VERBOSE(p_hwfn, ECORE_MSG_RDMA,
			   "QP(0x%x) IWARP_EVENT_TYPE_ASYNC_MPA_HANDSHAKE_MPA_REPLY_ARRIVED fw_ret_code=%d\n",
			   ep->cid, fw_return_code);
		ecore_iwarp_mpa_reply_arrived(p_hwfn, ep);
		break;
	/* MPA Negotiations completed */
	case IWARP_EVENT_TYPE_ASYNC_MPA_HANDSHAKE_COMPLETE:
		if (!ecore_iwarp_check_ep_ok(p_hwfn, ep))
			return ECORE_INVAL;
		DP_VERBOSE(p_hwfn, ECORE_MSG_RDMA,
			   "QP(0x%x) IWARP_EVENT_TYPE_ASYNC_MPA_HANDSHAKE_COMPLETE fw_ret_code=%d\n",
			   ep->cid, fw_return_code);
		ecore_iwarp_mpa_complete(p_hwfn, ep, fw_return_code);
		break;
	case IWARP_EVENT_TYPE_ASYNC_CID_CLEANED:
		cid = (u16)OSAL_LE32_TO_CPU(fw_handle->lo);
		DP_VERBOSE(p_hwfn, ECORE_MSG_RDMA,
			   "(0x%x)IWARP_EVENT_TYPE_ASYNC_CID_CLEANED\n",
			   cid);
		ecore_iwarp_cid_cleaned(p_hwfn, cid);

		break;
	case IWARP_EVENT_TYPE_ASYNC_CQ_OVERFLOW:
		DP_NOTICE(p_hwfn, false,
			  "IWARP_EVENT_TYPE_ASYNC_CQ_OVERFLOW\n");

		p_hwfn->p_rdma_info->events.affiliated_event(
			p_hwfn->p_rdma_info->events.context,
			ECORE_IWARP_EVENT_CQ_OVERFLOW,
			(void *)fw_handle);
		break;
	default:
		DP_ERR(p_hwfn, "Received unexpected async iwarp event %d\n",
		       fw_event_code);
		return ECORE_INVAL;
	}
	return ECORE_SUCCESS;
}

enum _ecore_status_t
ecore_iwarp_create_listen(void *rdma_cxt,
			  struct ecore_iwarp_listen_in *iparams,
			  struct ecore_iwarp_listen_out *oparams)
{
	struct ecore_hwfn *p_hwfn = (struct ecore_hwfn *)rdma_cxt;
	struct ecore_iwarp_listener *listener;

	listener = OSAL_ZALLOC(p_hwfn->p_dev, GFP_KERNEL, sizeof(*listener));

	if (!listener) {
		DP_NOTICE(p_hwfn,
			  false,
			  "ecore iwarp create listener failed: cannot allocate memory (listener). rc = %d\n",
			  ECORE_NOMEM);
		return ECORE_NOMEM;
	}
	listener->ip_version = iparams->ip_version;
	OSAL_MEMCPY(listener->ip_addr,
		    iparams->ip_addr,
		    sizeof(listener->ip_addr));
	listener->port = iparams->port;
	listener->vlan = iparams->vlan;

	listener->event_cb = iparams->event_cb;
	listener->cb_context = iparams->cb_context;
	listener->max_backlog = iparams->max_backlog;
	listener->state = ECORE_IWARP_LISTENER_STATE_ACTIVE;
	oparams->handle = listener;

	OSAL_SPIN_LOCK_INIT(&listener->lock);
	OSAL_LIST_INIT(&listener->ep_list);
	OSAL_SPIN_LOCK(&p_hwfn->p_rdma_info->iwarp.iw_lock);
	OSAL_LIST_PUSH_TAIL(&listener->list_entry,
			    &p_hwfn->p_rdma_info->iwarp.listen_list);
	OSAL_SPIN_UNLOCK(&p_hwfn->p_rdma_info->iwarp.iw_lock);

	DP_VERBOSE(p_hwfn, ECORE_MSG_RDMA, "callback=%p handle=%p ip=%x:%x:%x:%x port=0x%x vlan=0x%x\n",
		   listener->event_cb,
		   listener,
		   listener->ip_addr[0],
		   listener->ip_addr[1],
		   listener->ip_addr[2],
		   listener->ip_addr[3],
		   listener->port,
		   listener->vlan);

	return ECORE_SUCCESS;
}

static void
ecore_iwarp_pause_complete(struct ecore_iwarp_listener *listener)
{
	struct ecore_iwarp_cm_event_params params;

	if (listener->state == ECORE_IWARP_LISTENER_STATE_UNPAUSE)
		listener->state = ECORE_IWARP_LISTENER_STATE_ACTIVE;

	params.event = ECORE_IWARP_EVENT_LISTEN_PAUSE_COMP;
	listener->event_cb(listener->cb_context, &params);
}

static void
ecore_iwarp_tcp_abort_comp(struct ecore_hwfn *p_hwfn, void *cookie,
			   union event_ring_data OSAL_UNUSED *data,
			   u8 OSAL_UNUSED fw_return_code)
{
	struct ecore_iwarp_ep *ep = (struct ecore_iwarp_ep *)cookie;
	struct ecore_iwarp_listener *listener = ep->listener;

	ecore_iwarp_return_ep(p_hwfn, ep);

	if (OSAL_LIST_IS_EMPTY(&listener->ep_list))
		listener->done = true;
}

static void
ecore_iwarp_abort_inflight_connections(struct ecore_hwfn *p_hwfn,
				       struct ecore_iwarp_listener *listener)
{
	struct ecore_spq_entry *p_ent = OSAL_NULL;
	struct ecore_iwarp_ep *ep = OSAL_NULL;
	struct ecore_sp_init_data init_data;
	struct ecore_spq_comp_cb comp_data;
	enum _ecore_status_t rc;

	/* remove listener from list before destroying listener */
	OSAL_LIST_REMOVE_ENTRY(&listener->list_entry,
			       &p_hwfn->p_rdma_info->iwarp.listen_list);
	if (OSAL_LIST_IS_EMPTY(&listener->ep_list)) {
		listener->done = true;
		return;
	}
	OSAL_MEMSET(&init_data, 0, sizeof(init_data));
	init_data.p_comp_data = &comp_data;
	init_data.opaque_fid = p_hwfn->hw_info.opaque_fid;
	init_data.comp_mode = ECORE_SPQ_MODE_CB;
	init_data.p_comp_data->function = ecore_iwarp_tcp_abort_comp;

	OSAL_LIST_FOR_EACH_ENTRY(ep, &listener->ep_list,
				 list_entry, struct ecore_iwarp_ep) {
		ep->state = ECORE_IWARP_EP_ABORTING;
		init_data.p_comp_data->cookie = ep;
		init_data.cid = ep->tcp_cid;
		rc = ecore_sp_init_request(p_hwfn, &p_ent,
					   IWARP_RAMROD_CMD_ID_ABORT_TCP_OFFLOAD,
					   PROTOCOLID_IWARP,
					   &init_data);
		if (rc == ECORE_SUCCESS)
			ecore_spq_post(p_hwfn, p_ent, OSAL_NULL);
	}
}

static void
ecore_iwarp_listener_state_transition(struct ecore_hwfn *p_hwfn, void *cookie,
				      union event_ring_data OSAL_UNUSED *data,
				      u8 OSAL_UNUSED fw_return_code)
{
	struct ecore_iwarp_listener *listener = (struct ecore_iwarp_listener *)cookie;

	switch (listener->state) {
	case ECORE_IWARP_LISTENER_STATE_PAUSE:
	case ECORE_IWARP_LISTENER_STATE_UNPAUSE:
		ecore_iwarp_pause_complete(listener);
		break;
	case ECORE_IWARP_LISTENER_STATE_DESTROYING:
		ecore_iwarp_abort_inflight_connections(p_hwfn, listener);
		break;
	default:
		break;
	}
}

static enum _ecore_status_t
ecore_iwarp_empty_ramrod(struct ecore_hwfn *p_hwfn,
			 struct ecore_iwarp_listener *listener)
{
	struct ecore_spq_entry *p_ent = OSAL_NULL;
	struct ecore_spq_comp_cb comp_data;
	struct ecore_sp_init_data init_data;
	enum _ecore_status_t rc;

	OSAL_MEMSET(&init_data, 0, sizeof(init_data));
	init_data.p_comp_data = &comp_data;
	init_data.cid = ecore_spq_get_cid(p_hwfn);
	init_data.opaque_fid = p_hwfn->hw_info.opaque_fid;
	init_data.comp_mode = ECORE_SPQ_MODE_CB;
	init_data.p_comp_data->function = ecore_iwarp_listener_state_transition;
	init_data.p_comp_data->cookie = listener;
	rc = ecore_sp_init_request(p_hwfn, &p_ent,
				   COMMON_RAMROD_EMPTY,
				   PROTOCOLID_COMMON,
				   &init_data);
	if (rc != ECORE_SUCCESS)
		return rc;

	rc = ecore_spq_post(p_hwfn, p_ent, OSAL_NULL);
	if (rc != ECORE_SUCCESS)
		return rc;

	return rc;
}

enum _ecore_status_t
ecore_iwarp_pause_listen(void *rdma_cxt, void *handle,
			 bool pause, bool comp)
{
	struct ecore_hwfn *p_hwfn = (struct ecore_hwfn *)rdma_cxt;
	struct ecore_iwarp_listener *listener =
		(struct ecore_iwarp_listener *)handle;
	enum _ecore_status_t rc;

	listener->state = pause ?
		ECORE_IWARP_LISTENER_STATE_PAUSE :
		ECORE_IWARP_LISTENER_STATE_UNPAUSE;
	if (!comp)
		return ECORE_SUCCESS;

	rc = ecore_iwarp_empty_ramrod(p_hwfn, listener);
	if (rc != ECORE_SUCCESS)
		return rc;

	DP_VERBOSE(p_hwfn, ECORE_MSG_RDMA, "listener=%p, state=%d\n",
		   listener, listener->state);

	return ECORE_PENDING;
}

enum _ecore_status_t
ecore_iwarp_destroy_listen(void *rdma_cxt, void *handle)
{
	struct ecore_hwfn *p_hwfn = (struct ecore_hwfn *)rdma_cxt;
	struct ecore_iwarp_listener *listener =
		(struct ecore_iwarp_listener *)handle;
	enum _ecore_status_t rc;
	int wait_count = 0;

	DP_VERBOSE(p_hwfn, ECORE_MSG_RDMA, "handle=%p\n", handle);

	listener->state = ECORE_IWARP_LISTENER_STATE_DESTROYING;
	rc = ecore_iwarp_empty_ramrod(p_hwfn, listener);
	if (rc != ECORE_SUCCESS)
		return rc;

	while (!listener->done) {
		DP_VERBOSE(p_hwfn, ECORE_MSG_RDMA,
			   "Waiting for ep list to be empty...\n");
		OSAL_MSLEEP(100);
		if (wait_count++ > 200) {
			DP_NOTICE(p_hwfn, false, "ep list close timeout\n");
			break;
		}
	}

	OSAL_FREE(p_hwfn->p_dev, listener);

	return ECORE_SUCCESS;
}

enum _ecore_status_t
ecore_iwarp_send_rtr(void *rdma_cxt, struct ecore_iwarp_send_rtr_in *iparams)
{
	struct ecore_hwfn *p_hwfn = (struct ecore_hwfn *)rdma_cxt;
	struct ecore_sp_init_data init_data;
	struct ecore_spq_entry *p_ent;
	struct ecore_rdma_qp *qp;
	struct ecore_iwarp_ep *ep;
	enum _ecore_status_t rc;

	ep = (struct ecore_iwarp_ep *)iparams->ep_context;
	if (!ep) {
		DP_ERR(p_hwfn, "Ep Context receive in send_rtr is NULL\n");
		return ECORE_INVAL;
	}

	qp = ep->qp;

	DP_VERBOSE(p_hwfn, ECORE_MSG_RDMA, "QP(0x%x) EP(0x%x)\n",
		   qp->icid, ep->tcp_cid);

	OSAL_MEMSET(&init_data, 0, sizeof(init_data));
	init_data.cid = qp->icid;
	init_data.opaque_fid = p_hwfn->hw_info.opaque_fid;
	init_data.comp_mode = ECORE_SPQ_MODE_CB;

	rc = ecore_sp_init_request(p_hwfn, &p_ent,
				   IWARP_RAMROD_CMD_ID_MPA_OFFLOAD_SEND_RTR,
				   PROTOCOLID_IWARP, &init_data);

	if (rc != ECORE_SUCCESS)
		return rc;

	rc = ecore_spq_post(p_hwfn, p_ent, OSAL_NULL);

	DP_VERBOSE(p_hwfn, ECORE_MSG_RDMA, "ecore_iwarp_send_rtr, rc = 0x%x\n",
		   rc);

	return rc;
}

enum _ecore_status_t
ecore_iwarp_query_qp(struct ecore_rdma_qp *qp,
		     struct ecore_rdma_query_qp_out_params *out_params)
{
	out_params->state = ecore_iwarp2roce_state(qp->iwarp_state);
	return ECORE_SUCCESS;
}

#ifdef _NTDDK_
#pragma warning(pop)
#endif
