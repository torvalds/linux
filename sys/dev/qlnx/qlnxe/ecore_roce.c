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
 * File : ecore_roce.c
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
#include "ecore_rt_defs.h"
#include "ecore_init_ops.h"
#include "ecore_hw.h"
#include "ecore_mcp.h"
#include "ecore_init_fw_funcs.h"
#include "ecore_int.h"
#include "pcics_reg_driver.h"
#include "ecore_iro.h"
#include "ecore_gtt_reg_addr.h"
#ifndef LINUX_REMOVE
#include "ecore_tcp_ip.h"
#endif

#ifdef _NTDDK_
#pragma warning(push)
#pragma warning(disable : 28167)
#pragma warning(disable : 28123)
#pragma warning(disable : 28182)
#pragma warning(disable : 6011)
#endif

static void ecore_roce_free_icid(struct ecore_hwfn *p_hwfn, u16 icid);

static enum _ecore_status_t
ecore_roce_async_event(struct ecore_hwfn *p_hwfn,
		       u8 fw_event_code,
		       u16 OSAL_UNUSED echo,
		       union event_ring_data *data,
		       u8 OSAL_UNUSED fw_return_code)
{
	if (fw_event_code == ROCE_ASYNC_EVENT_DESTROY_QP_DONE) {
		u16 icid = (u16)OSAL_LE32_TO_CPU(
				data->rdma_data.rdma_destroy_qp_data.cid);

		/* icid release in this async event can occur only if the icid
		 * was offloaded to the FW. In case it wasn't offloaded this is
		 * handled in ecore_roce_sp_destroy_qp.
		 */
		ecore_roce_free_icid(p_hwfn, icid);
	} else
		p_hwfn->p_rdma_info->events.affiliated_event(
			p_hwfn->p_rdma_info->events.context,
			fw_event_code,
			(void *)&data->rdma_data.async_handle);

	return ECORE_SUCCESS;
}



#ifdef CONFIG_DCQCN
static enum _ecore_status_t ecore_roce_start_rl(
	struct ecore_hwfn *p_hwfn,
	struct ecore_roce_dcqcn_params *dcqcn_params)
{
	struct ecore_rl_update_params params;

	DP_VERBOSE(p_hwfn, ECORE_MSG_RDMA, "\n");
	OSAL_MEMSET(&params, 0, sizeof(params));

	params.rl_id_first = (u8)RESC_START(p_hwfn, ECORE_RL);
	params.rl_id_last = RESC_START(p_hwfn, ECORE_RL) +
		ecore_init_qm_get_num_pf_rls(p_hwfn);
	params.dcqcn_update_param_flg = 1;
	params.rl_init_flg = 1;
	params.rl_start_flg = 1;
	params.rl_stop_flg = 0;
	params.rl_dc_qcn_flg = 1;

	params.rl_bc_rate = dcqcn_params->rl_bc_rate;
	params.rl_max_rate = dcqcn_params->rl_max_rate;
	params.rl_r_ai = dcqcn_params->rl_r_ai;
	params.rl_r_hai = dcqcn_params->rl_r_hai;
	params.dcqcn_gd = dcqcn_params->dcqcn_gd;
	params.dcqcn_k_us = dcqcn_params->dcqcn_k_us;
	params.dcqcn_timeuot_us = dcqcn_params->dcqcn_timeout_us;

	return ecore_sp_rl_update(p_hwfn, &params);
}

enum _ecore_status_t ecore_roce_stop_rl(struct ecore_hwfn *p_hwfn)
{
	struct ecore_rl_update_params params;

	if (!p_hwfn->p_rdma_info->roce.dcqcn_reaction_point)
		return ECORE_SUCCESS;

	OSAL_MEMSET(&params, 0, sizeof(params));
	DP_VERBOSE(p_hwfn, ECORE_MSG_RDMA, "\n");

	params.rl_id_first = (u8)RESC_START(p_hwfn, ECORE_RL);
	params.rl_id_last = RESC_START(p_hwfn, ECORE_RL) +
		ecore_init_qm_get_num_pf_rls(p_hwfn);
	params.rl_stop_flg = 1;

	return ecore_sp_rl_update(p_hwfn, &params);
}

#define NIG_REG_ROCE_DUPLICATE_TO_HOST_BTH 2
#define NIG_REG_ROCE_DUPLICATE_TO_HOST_ECN 1

enum _ecore_status_t ecore_roce_dcqcn_cfg(
	struct ecore_hwfn			*p_hwfn,
	struct ecore_roce_dcqcn_params		*params,
	struct roce_init_func_ramrod_data	*p_ramrod,
	struct ecore_ptt			*p_ptt)
{
	u32 val = 0;
	enum _ecore_status_t rc = ECORE_SUCCESS;

	if (!p_hwfn->pf_params.rdma_pf_params.enable_dcqcn ||
	    p_hwfn->p_rdma_info->proto == PROTOCOLID_IWARP)
		return rc;

	p_hwfn->p_rdma_info->roce.dcqcn_enabled = 0;
	if (params->notification_point) {
		DP_VERBOSE(p_hwfn, ECORE_MSG_RDMA,
			   "Configuring dcqcn notification point: timeout = 0x%x\n",
			   params->cnp_send_timeout);
		p_ramrod->roce.cnp_send_timeout = params->cnp_send_timeout;
		p_hwfn->p_rdma_info->roce.dcqcn_enabled = 1;
		/* Configure NIG to duplicate to host and storm when:
		 *  - (ECN == 2'b11 (notification point)
		 */
		val |= 1 << NIG_REG_ROCE_DUPLICATE_TO_HOST_ECN;
	}

	if (params->reaction_point) {
		DP_VERBOSE(p_hwfn, ECORE_MSG_RDMA,
			   "Configuring dcqcn reaction point\n");
		p_hwfn->p_rdma_info->roce.dcqcn_enabled = 1;
		p_hwfn->p_rdma_info->roce.dcqcn_reaction_point = 1;
		/* Configure NIG to duplicate to host and storm when:
		 * - BTH opcode equals bth_hdr_flow_ctrl_opcode_2
		 * (reaction point)
		 */
		val |= 1 << NIG_REG_ROCE_DUPLICATE_TO_HOST_BTH;

		rc = ecore_roce_start_rl(p_hwfn, params);
	}

	if (rc)
		return rc;

	p_ramrod->roce.cnp_dscp = params->cnp_dscp;
	p_ramrod->roce.cnp_vlan_priority = params->cnp_vlan_priority;

	ecore_wr(p_hwfn,
		 p_ptt,
		 NIG_REG_ROCE_DUPLICATE_TO_HOST,
		 val);

	return rc;
}
#endif


enum _ecore_status_t ecore_roce_stop(struct ecore_hwfn *p_hwfn)
{
	struct ecore_bmap *cid_map = &p_hwfn->p_rdma_info->cid_map;
	int wait_count = 0;

	/* when destroying a_RoCE QP the control is returned to the
	 * user after the synchronous part. The asynchronous part may
	 * take a little longer. We delay for a short while if an
	 * asyn destroy QP is still expected. Beyond the added delay
	 * we clear the bitmap anyway.
	 */
	while (OSAL_BITMAP_WEIGHT(cid_map->bitmap, cid_map->max_count)) {
		OSAL_MSLEEP(100);
		if (wait_count++ > 20) {
			DP_NOTICE(p_hwfn, false,
				  "cid bitmap wait timed out\n");
			break;
		}
	}

	ecore_spq_unregister_async_cb(p_hwfn, PROTOCOLID_ROCE);

	return ECORE_SUCCESS;
}


static void ecore_rdma_copy_gids(struct ecore_rdma_qp *qp, __le32 *src_gid,
				 __le32 *dst_gid) {
	u32 i;

	if (qp->roce_mode == ROCE_V2_IPV4) {
		/* The IPv4 addresses shall be aligned to the highest word.
		 * The lower words must be zero.
		 */
		OSAL_MEMSET(src_gid, 0, sizeof(union ecore_gid));
		OSAL_MEMSET(dst_gid, 0, sizeof(union ecore_gid));
		src_gid[3] = OSAL_CPU_TO_LE32(qp->sgid.ipv4_addr);
		dst_gid[3] = OSAL_CPU_TO_LE32(qp->dgid.ipv4_addr);
	} else {
		/* RoCE, and RoCE v2 - IPv6: GIDs and IPv6 addresses coincide in
		 * location and size
		 */
		for (i = 0; i < OSAL_ARRAY_SIZE(qp->sgid.dwords); i++) {
			src_gid[i] = OSAL_CPU_TO_LE32(qp->sgid.dwords[i]);
			dst_gid[i] = OSAL_CPU_TO_LE32(qp->dgid.dwords[i]);
		}
	}
}

static enum roce_flavor ecore_roce_mode_to_flavor(enum roce_mode roce_mode)
{
	enum roce_flavor flavor;

	switch (roce_mode) {
	case ROCE_V1:
		flavor = PLAIN_ROCE;
		break;
	case ROCE_V2_IPV4:
		flavor = RROCE_IPV4;
		break;
	case ROCE_V2_IPV6:
		flavor = (enum roce_flavor)ROCE_V2_IPV6;
		break;
	default:
		flavor = (enum roce_flavor)MAX_ROCE_MODE;
		break;
	}
	return flavor;
}

#if 0
static void ecore_roce_free_cid_pair(struct ecore_hwfn *p_hwfn, u16 cid)
{
	OSAL_SPIN_LOCK(&p_hwfn->p_rdma_info->lock);
	ecore_bmap_release_id(p_hwfn, &p_hwfn->p_rdma_info->qp_map, cid);
	ecore_bmap_release_id(p_hwfn, &p_hwfn->p_rdma_info->qp_map, cid + 1);
	OSAL_SPIN_UNLOCK(&p_hwfn->p_rdma_info->lock);
}
#endif

static void ecore_roce_free_qp(struct ecore_hwfn *p_hwfn, u16 qp_idx)
{
	OSAL_SPIN_LOCK(&p_hwfn->p_rdma_info->lock);
	ecore_bmap_release_id(p_hwfn, &p_hwfn->p_rdma_info->qp_map, qp_idx);
	OSAL_SPIN_UNLOCK(&p_hwfn->p_rdma_info->lock);
}

#define ECORE_ROCE_CREATE_QP_ATTEMPTS		(20)
#define ECORE_ROCE_CREATE_QP_MSLEEP		(10)

static enum _ecore_status_t ecore_roce_wait_free_cids(struct ecore_hwfn *p_hwfn, u32 qp_idx)
{
	struct ecore_rdma_info *p_rdma_info = p_hwfn->p_rdma_info;
	bool cids_free = false;
	u32 icid, iter = 0;
	int req, resp;

	icid = ECORE_ROCE_QP_TO_ICID(qp_idx);

	/* Make sure that the cids that were used by the QP index are free.
	 * This is necessary because the destroy flow returns to the user before
	 * the device finishes clean up.
	 * It can happen in the following flows:
	 * (1) ib_destroy_qp followed by an ib_create_qp
	 * (2) ib_modify_qp to RESET followed (not immediately), by an
	 *     ib_modify_qp to RTR
	 */

	do {
		OSAL_SPIN_LOCK(&p_rdma_info->lock);
		resp = ecore_bmap_test_id(p_hwfn, &p_rdma_info->cid_map, icid);
		req = ecore_bmap_test_id(p_hwfn, &p_rdma_info->cid_map, icid + 1);
		if (!resp && !req)
			cids_free = true;

		OSAL_SPIN_UNLOCK(&p_rdma_info->lock);

		if (!cids_free) {
			OSAL_MSLEEP(ECORE_ROCE_CREATE_QP_MSLEEP);
			iter++;
		}
	} while (!cids_free && iter < ECORE_ROCE_CREATE_QP_ATTEMPTS);

	if (!cids_free) {
		DP_ERR(p_hwfn->p_dev,
		       "responder and/or requester CIDs are still in use. resp=%d, req=%d\n",
		       resp, req);
		return ECORE_AGAIN;
	}

	return ECORE_SUCCESS;
}

enum _ecore_status_t ecore_roce_alloc_qp_idx(
		struct ecore_hwfn *p_hwfn, u16 *qp_idx16)
{
	struct ecore_rdma_info *p_rdma_info = p_hwfn->p_rdma_info;
	u32 start_cid, icid, cid, qp_idx;
	enum _ecore_status_t rc;

	OSAL_SPIN_LOCK(&p_rdma_info->lock);
	rc = ecore_rdma_bmap_alloc_id(p_hwfn, &p_rdma_info->qp_map, &qp_idx);
	if (rc != ECORE_SUCCESS) {
		DP_NOTICE(p_hwfn, false, "failed to allocate qp\n");
		OSAL_SPIN_UNLOCK(&p_rdma_info->lock);
		return rc;
	}

	OSAL_SPIN_UNLOCK(&p_rdma_info->lock);

	/* Verify the cid bits that of this qp index are clear */
	rc = ecore_roce_wait_free_cids(p_hwfn, qp_idx);
	if (rc) {
		rc = ECORE_UNKNOWN_ERROR;
		goto err;
	}

	/* Allocate a DMA-able context for an ILT page, if not existing, for the
	 * associated iids.
	 * Note: If second allocation fails there's no need to free the first as
	 *       it will be used in the future.
	 */
	icid = ECORE_ROCE_QP_TO_ICID(qp_idx);
	start_cid = ecore_cxt_get_proto_cid_start(p_hwfn, p_rdma_info->proto);
	cid = start_cid + icid;

	rc = ecore_cxt_dynamic_ilt_alloc(p_hwfn, ECORE_ELEM_CXT, cid);
	if (rc != ECORE_SUCCESS)
		goto err;

	rc = ecore_cxt_dynamic_ilt_alloc(p_hwfn, ECORE_ELEM_CXT, cid + 1);
	if (rc != ECORE_SUCCESS)
		goto err;

	/* qp index is under 2^16 */
	*qp_idx16 = (u16)qp_idx;

	return ECORE_SUCCESS;

err:
	ecore_roce_free_qp(p_hwfn, (u16)qp_idx);

	DP_VERBOSE(p_hwfn, ECORE_MSG_RDMA, "rc = %d\n", rc);

	return rc;
}

static void ecore_roce_set_cid(struct ecore_hwfn *p_hwfn,
			     u32 cid)
{
	OSAL_SPIN_LOCK(&p_hwfn->p_rdma_info->lock);
	ecore_bmap_set_id(p_hwfn,
			  &p_hwfn->p_rdma_info->cid_map,
			  cid);
	OSAL_SPIN_UNLOCK(&p_hwfn->p_rdma_info->lock);
}

static enum _ecore_status_t ecore_roce_sp_create_responder(
	struct ecore_hwfn    *p_hwfn,
	struct ecore_rdma_qp *qp)
{
	struct roce_create_qp_resp_ramrod_data *p_ramrod;
	u16 regular_latency_queue, low_latency_queue;
	struct ecore_sp_init_data init_data;
	enum roce_flavor roce_flavor;
	struct ecore_spq_entry *p_ent;
	enum _ecore_status_t rc;
	u32 cid_start;
	u16 fw_srq_id;
	bool is_xrc;

	if (!qp->has_resp)
		return ECORE_SUCCESS;

	DP_VERBOSE(p_hwfn, ECORE_MSG_RDMA, "qp_idx = %08x\n", qp->qp_idx);

	/* Allocate DMA-able memory for IRQ */
	qp->irq_num_pages = 1;
	qp->irq = OSAL_DMA_ALLOC_COHERENT(p_hwfn->p_dev,
					  &qp->irq_phys_addr,
					  RDMA_RING_PAGE_SIZE);
	if (!qp->irq) {
		rc = ECORE_NOMEM;
		DP_NOTICE(p_hwfn, false,
			  "ecore create responder failed: cannot allocate memory (irq). rc = %d\n",
			  rc);
		return rc;
	}

	/* Get SPQ entry */
	OSAL_MEMSET(&init_data, 0, sizeof(init_data));
	init_data.cid = qp->icid;
	init_data.opaque_fid = p_hwfn->hw_info.opaque_fid;
	init_data.comp_mode = ECORE_SPQ_MODE_EBLOCK;

	rc = ecore_sp_init_request(p_hwfn, &p_ent, ROCE_RAMROD_CREATE_QP,
				   PROTOCOLID_ROCE, &init_data);
	if (rc != ECORE_SUCCESS)
		goto err;

	p_ramrod = &p_ent->ramrod.roce_create_qp_resp;

	p_ramrod->flags = 0;

	roce_flavor = ecore_roce_mode_to_flavor(qp->roce_mode);
	SET_FIELD(p_ramrod->flags,
		  ROCE_CREATE_QP_RESP_RAMROD_DATA_ROCE_FLAVOR,
		  roce_flavor);

	SET_FIELD(p_ramrod->flags,
		  ROCE_CREATE_QP_RESP_RAMROD_DATA_RDMA_RD_EN,
		  qp->incoming_rdma_read_en);

	SET_FIELD(p_ramrod->flags,
		  ROCE_CREATE_QP_RESP_RAMROD_DATA_RDMA_WR_EN,
		  qp->incoming_rdma_write_en);

	SET_FIELD(p_ramrod->flags,
		  ROCE_CREATE_QP_RESP_RAMROD_DATA_ATOMIC_EN,
		  qp->incoming_atomic_en);

	SET_FIELD(p_ramrod->flags,
		  ROCE_CREATE_QP_RESP_RAMROD_DATA_E2E_FLOW_CONTROL_EN,
		  qp->e2e_flow_control_en);

	SET_FIELD(p_ramrod->flags,
		  ROCE_CREATE_QP_RESP_RAMROD_DATA_SRQ_FLG,
		  qp->use_srq);

	SET_FIELD(p_ramrod->flags,
		  ROCE_CREATE_QP_RESP_RAMROD_DATA_RESERVED_KEY_EN,
		  qp->fmr_and_reserved_lkey);

	SET_FIELD(p_ramrod->flags,
		  ROCE_CREATE_QP_RESP_RAMROD_DATA_XRC_FLAG,
		  ecore_rdma_is_xrc_qp(qp));

	/* TBD: future use only
	 * #define ROCE_CREATE_QP_RESP_RAMROD_DATA_PRI_MASK
	 * #define ROCE_CREATE_QP_RESP_RAMROD_DATA_PRI_SHIFT
	 */
	SET_FIELD(p_ramrod->flags,
		  ROCE_CREATE_QP_RESP_RAMROD_DATA_MIN_RNR_NAK_TIMER,
		  qp->min_rnr_nak_timer);

	p_ramrod->max_ird =
		qp->max_rd_atomic_resp;
	p_ramrod->traffic_class = qp->traffic_class_tos;
	p_ramrod->hop_limit = qp->hop_limit_ttl;
	p_ramrod->irq_num_pages = qp->irq_num_pages;
	p_ramrod->p_key = OSAL_CPU_TO_LE16(qp->pkey);
	p_ramrod->flow_label = OSAL_CPU_TO_LE32(qp->flow_label);
	p_ramrod->dst_qp_id = OSAL_CPU_TO_LE32(qp->dest_qp);
	p_ramrod->mtu = OSAL_CPU_TO_LE16(qp->mtu);
	p_ramrod->initial_psn = OSAL_CPU_TO_LE32(qp->rq_psn);
	p_ramrod->pd = OSAL_CPU_TO_LE16(qp->pd);
	p_ramrod->rq_num_pages = OSAL_CPU_TO_LE16(qp->rq_num_pages);
	DMA_REGPAIR_LE(p_ramrod->rq_pbl_addr, qp->rq_pbl_ptr);
	DMA_REGPAIR_LE(p_ramrod->irq_pbl_addr, qp->irq_phys_addr);
	ecore_rdma_copy_gids(qp, p_ramrod->src_gid, p_ramrod->dst_gid);
	p_ramrod->qp_handle_for_async.hi =
			OSAL_CPU_TO_LE32(qp->qp_handle_async.hi);
	p_ramrod->qp_handle_for_async.lo =
			OSAL_CPU_TO_LE32(qp->qp_handle_async.lo);
	p_ramrod->qp_handle_for_cqe.hi = OSAL_CPU_TO_LE32(qp->qp_handle.hi);
	p_ramrod->qp_handle_for_cqe.lo = OSAL_CPU_TO_LE32(qp->qp_handle.lo);
	p_ramrod->cq_cid = OSAL_CPU_TO_LE32((p_hwfn->hw_info.opaque_fid << 16) | qp->rq_cq_id);
	p_ramrod->xrc_domain = OSAL_CPU_TO_LE16(qp->xrcd_id);

#ifdef CONFIG_DCQCN
	/* when dcqcn is enabled physical queues are determined accoridng to qp id */
	if (p_hwfn->p_rdma_info->roce.dcqcn_enabled)
		regular_latency_queue =
			ecore_get_cm_pq_idx_rl(p_hwfn,
					       (qp->icid >> 1) %
							ROCE_DCQCN_RP_MAX_QPS);
	else
#endif
		regular_latency_queue = ecore_get_cm_pq_idx(p_hwfn, PQ_FLAGS_OFLD);
	low_latency_queue = ecore_get_cm_pq_idx(p_hwfn, PQ_FLAGS_LLT);

	p_ramrod->regular_latency_phy_queue = OSAL_CPU_TO_LE16(regular_latency_queue);
	p_ramrod->low_latency_phy_queue = OSAL_CPU_TO_LE16(low_latency_queue);
	p_ramrod->dpi = OSAL_CPU_TO_LE16(qp->dpi);

	ecore_rdma_set_fw_mac(p_ramrod->remote_mac_addr, qp->remote_mac_addr);
	ecore_rdma_set_fw_mac(p_ramrod->local_mac_addr, qp->local_mac_addr);

	p_ramrod->udp_src_port = qp->udp_src_port;
	p_ramrod->vlan_id = OSAL_CPU_TO_LE16(qp->vlan_id);
	is_xrc = ecore_rdma_is_xrc_qp(qp);
	fw_srq_id = ecore_rdma_get_fw_srq_id(p_hwfn, qp->srq_id, is_xrc);
	p_ramrod->srq_id.srq_idx = OSAL_CPU_TO_LE16(fw_srq_id);
	p_ramrod->srq_id.opaque_fid = OSAL_CPU_TO_LE16(p_hwfn->hw_info.opaque_fid);

	p_ramrod->stats_counter_id = RESC_START(p_hwfn, ECORE_RDMA_STATS_QUEUE) +
				     qp->stats_queue;

	rc = ecore_spq_post(p_hwfn, p_ent, OSAL_NULL);

	DP_VERBOSE(p_hwfn, ECORE_MSG_RDMA, "rc = %d regular physical queue = 0x%x, low latency physical queue 0x%x\n",
		   rc, regular_latency_queue, low_latency_queue);

	if (rc != ECORE_SUCCESS)
		goto err;

	qp->resp_offloaded = true;
	qp->cq_prod.resp = 0;

	cid_start = ecore_cxt_get_proto_cid_start(p_hwfn,
						  p_hwfn->p_rdma_info->proto);
	ecore_roce_set_cid(p_hwfn, qp->icid - cid_start);

	return rc;

err:
	DP_NOTICE(p_hwfn, false, "create responder - failed, rc = %d\n", rc);
	OSAL_DMA_FREE_COHERENT(p_hwfn->p_dev,
			       qp->irq,
			       qp->irq_phys_addr,
			       qp->irq_num_pages *
			       RDMA_RING_PAGE_SIZE);

	return rc;
}

static enum _ecore_status_t ecore_roce_sp_create_requester(
	struct ecore_hwfn *p_hwfn,
	struct ecore_rdma_qp *qp)
{
	struct roce_create_qp_req_ramrod_data *p_ramrod;
	u16 regular_latency_queue, low_latency_queue;
	struct ecore_sp_init_data init_data;
	enum roce_flavor roce_flavor;
	struct ecore_spq_entry *p_ent;
	enum _ecore_status_t rc;
	u32 cid_start;

	if (!qp->has_req)
		return ECORE_SUCCESS;

	DP_VERBOSE(p_hwfn, ECORE_MSG_RDMA, "icid = %08x\n", qp->icid);

	/* Allocate DMA-able memory for ORQ */
	qp->orq_num_pages = 1;
	qp->orq = OSAL_DMA_ALLOC_COHERENT(p_hwfn->p_dev,
					  &qp->orq_phys_addr,
					  RDMA_RING_PAGE_SIZE);
	if (!qp->orq)
	{
		rc = ECORE_NOMEM;
		DP_NOTICE(p_hwfn, false,
			  "ecore create requester failed: cannot allocate memory (orq). rc = %d\n",
			  rc);
		return rc;
	}

	/* Get SPQ entry */
	OSAL_MEMSET(&init_data, 0, sizeof(init_data));
	init_data.cid = qp->icid + 1;
	init_data.opaque_fid = p_hwfn->hw_info.opaque_fid;
	init_data.comp_mode = ECORE_SPQ_MODE_EBLOCK;

	rc = ecore_sp_init_request(p_hwfn, &p_ent,
				   ROCE_RAMROD_CREATE_QP,
				   PROTOCOLID_ROCE, &init_data);
	if (rc != ECORE_SUCCESS)
		goto err;

	p_ramrod = &p_ent->ramrod.roce_create_qp_req;

	p_ramrod->flags = 0;

	roce_flavor = ecore_roce_mode_to_flavor(qp->roce_mode);
	SET_FIELD(p_ramrod->flags,
		  ROCE_CREATE_QP_REQ_RAMROD_DATA_ROCE_FLAVOR,
		  roce_flavor);

	SET_FIELD(p_ramrod->flags,
		  ROCE_CREATE_QP_REQ_RAMROD_DATA_FMR_AND_RESERVED_EN,
		  qp->fmr_and_reserved_lkey);

	SET_FIELD(p_ramrod->flags,
		  ROCE_CREATE_QP_REQ_RAMROD_DATA_SIGNALED_COMP,
		  qp->signal_all);

	/* TBD:
	 * future use only
	 * #define ROCE_CREATE_QP_REQ_RAMROD_DATA_PRI_MASK
	 * #define ROCE_CREATE_QP_REQ_RAMROD_DATA_PRI_SHIFT
	 */
	SET_FIELD(p_ramrod->flags,
		  ROCE_CREATE_QP_REQ_RAMROD_DATA_ERR_RETRY_CNT,
		  qp->retry_cnt);

	SET_FIELD(p_ramrod->flags,
		  ROCE_CREATE_QP_REQ_RAMROD_DATA_RNR_NAK_CNT,
		  qp->rnr_retry_cnt);

	SET_FIELD(p_ramrod->flags,
		  ROCE_CREATE_QP_REQ_RAMROD_DATA_XRC_FLAG,
		  ecore_rdma_is_xrc_qp(qp));

	p_ramrod->max_ord = qp->max_rd_atomic_req;
	p_ramrod->traffic_class = qp->traffic_class_tos;
	p_ramrod->hop_limit = qp->hop_limit_ttl;
	p_ramrod->orq_num_pages = qp->orq_num_pages;
	p_ramrod->p_key = OSAL_CPU_TO_LE16(qp->pkey);
	p_ramrod->flow_label = OSAL_CPU_TO_LE32(qp->flow_label);
	p_ramrod->dst_qp_id = OSAL_CPU_TO_LE32(qp->dest_qp);
	p_ramrod->ack_timeout_val = OSAL_CPU_TO_LE32(qp->ack_timeout);
	p_ramrod->mtu = OSAL_CPU_TO_LE16(qp->mtu);
	p_ramrod->initial_psn = OSAL_CPU_TO_LE32(qp->sq_psn);
	p_ramrod->pd = OSAL_CPU_TO_LE16(qp->pd);
	p_ramrod->sq_num_pages = OSAL_CPU_TO_LE16(qp->sq_num_pages);
	DMA_REGPAIR_LE(p_ramrod->sq_pbl_addr, qp->sq_pbl_ptr);
	DMA_REGPAIR_LE(p_ramrod->orq_pbl_addr, qp->orq_phys_addr);
	ecore_rdma_copy_gids(qp, p_ramrod->src_gid, p_ramrod->dst_gid);
	p_ramrod->qp_handle_for_async.hi =
			OSAL_CPU_TO_LE32(qp->qp_handle_async.hi);
	p_ramrod->qp_handle_for_async.lo =
			OSAL_CPU_TO_LE32(qp->qp_handle_async.lo);
	p_ramrod->qp_handle_for_cqe.hi = OSAL_CPU_TO_LE32(qp->qp_handle.hi);
	p_ramrod->qp_handle_for_cqe.lo = OSAL_CPU_TO_LE32(qp->qp_handle.lo);
	p_ramrod->cq_cid = OSAL_CPU_TO_LE32((p_hwfn->hw_info.opaque_fid << 16) |
				       qp->sq_cq_id);

#ifdef CONFIG_DCQCN
	/* when dcqcn is enabled physical queues are determined accoridng to qp id */
	if (p_hwfn->p_rdma_info->roce.dcqcn_enabled)
		regular_latency_queue =
			ecore_get_cm_pq_idx_rl(p_hwfn,
					       (qp->icid >> 1) %
							ROCE_DCQCN_RP_MAX_QPS);
	else
#endif
		regular_latency_queue = ecore_get_cm_pq_idx(p_hwfn, PQ_FLAGS_OFLD);
	low_latency_queue = ecore_get_cm_pq_idx(p_hwfn, PQ_FLAGS_LLT);

	p_ramrod->regular_latency_phy_queue = OSAL_CPU_TO_LE16(regular_latency_queue);
	p_ramrod->low_latency_phy_queue = OSAL_CPU_TO_LE16(low_latency_queue);
	p_ramrod->dpi = OSAL_CPU_TO_LE16(qp->dpi);

	ecore_rdma_set_fw_mac(p_ramrod->remote_mac_addr, qp->remote_mac_addr);
	ecore_rdma_set_fw_mac(p_ramrod->local_mac_addr, qp->local_mac_addr);

	p_ramrod->udp_src_port = qp->udp_src_port;
	p_ramrod->vlan_id = OSAL_CPU_TO_LE16(qp->vlan_id);
	p_ramrod->stats_counter_id = RESC_START(p_hwfn, ECORE_RDMA_STATS_QUEUE) +
				     qp->stats_queue;

	rc = ecore_spq_post(p_hwfn, p_ent, OSAL_NULL);

	DP_VERBOSE(p_hwfn, ECORE_MSG_RDMA, "rc = %d\n", rc);

	if (rc != ECORE_SUCCESS)
		goto err;

	qp->req_offloaded = true;
	qp->cq_prod.req = 0;

	cid_start = ecore_cxt_get_proto_cid_start(p_hwfn,
						  p_hwfn->p_rdma_info->proto);
	ecore_roce_set_cid(p_hwfn, qp->icid + 1 - cid_start);

	return rc;

err:
	DP_NOTICE(p_hwfn, false, "Create requested - failed, rc = %d\n", rc);
	OSAL_DMA_FREE_COHERENT(p_hwfn->p_dev,
			       qp->orq,
			       qp->orq_phys_addr,
			       qp->orq_num_pages *
			       RDMA_RING_PAGE_SIZE);
	return rc;
}

static enum _ecore_status_t ecore_roce_sp_modify_responder(
	struct ecore_hwfn	*p_hwfn,
	struct ecore_rdma_qp	*qp,
	bool			move_to_err,
	u32			modify_flags)
{
	struct roce_modify_qp_resp_ramrod_data	*p_ramrod;
	struct ecore_sp_init_data		init_data;
	struct ecore_spq_entry			*p_ent;
	enum _ecore_status_t			rc;

	if (!qp->has_resp)
		return ECORE_SUCCESS;

	DP_VERBOSE(p_hwfn, ECORE_MSG_RDMA, "icid = %08x\n", qp->icid);

	if (move_to_err && !qp->resp_offloaded)
		return ECORE_SUCCESS;

	/* Get SPQ entry */
	OSAL_MEMSET(&init_data, 0, sizeof(init_data));
	init_data.cid = qp->icid;
	init_data.opaque_fid = p_hwfn->hw_info.opaque_fid;
	init_data.comp_mode = ECORE_SPQ_MODE_EBLOCK;

	rc = ecore_sp_init_request(p_hwfn, &p_ent,
				   ROCE_EVENT_MODIFY_QP,
				   PROTOCOLID_ROCE, &init_data);
	if (rc != ECORE_SUCCESS)
	{
		DP_NOTICE(p_hwfn, false, "rc = %d\n", rc);
		return rc;
	}

	p_ramrod = &p_ent->ramrod.roce_modify_qp_resp;

	p_ramrod->flags = 0;

	SET_FIELD(p_ramrod->flags,
		  ROCE_MODIFY_QP_RESP_RAMROD_DATA_MOVE_TO_ERR_FLG,
		  move_to_err);

	SET_FIELD(p_ramrod->flags,
		  ROCE_MODIFY_QP_RESP_RAMROD_DATA_RDMA_RD_EN,
		  qp->incoming_rdma_read_en);

	SET_FIELD(p_ramrod->flags,
		  ROCE_MODIFY_QP_RESP_RAMROD_DATA_RDMA_WR_EN,
		  qp->incoming_rdma_write_en);

	SET_FIELD(p_ramrod->flags,
		  ROCE_MODIFY_QP_RESP_RAMROD_DATA_ATOMIC_EN,
		  qp->incoming_atomic_en);

	SET_FIELD(p_ramrod->flags,
		  ROCE_CREATE_QP_RESP_RAMROD_DATA_E2E_FLOW_CONTROL_EN,
		  qp->e2e_flow_control_en);

	SET_FIELD(p_ramrod->flags,
		  ROCE_MODIFY_QP_RESP_RAMROD_DATA_RDMA_OPS_EN_FLG,
		  GET_FIELD(modify_flags,
			    ECORE_RDMA_MODIFY_QP_VALID_RDMA_OPS_EN));

	SET_FIELD(p_ramrod->flags,
		  ROCE_MODIFY_QP_RESP_RAMROD_DATA_P_KEY_FLG,
		  GET_FIELD(modify_flags, ECORE_ROCE_MODIFY_QP_VALID_PKEY));

	SET_FIELD(p_ramrod->flags,
		  ROCE_MODIFY_QP_RESP_RAMROD_DATA_ADDRESS_VECTOR_FLG,
		  GET_FIELD(modify_flags,
			    ECORE_ROCE_MODIFY_QP_VALID_ADDRESS_VECTOR));

	SET_FIELD(p_ramrod->flags,
		  ROCE_MODIFY_QP_RESP_RAMROD_DATA_MAX_IRD_FLG,
		  GET_FIELD(modify_flags,
			    ECORE_RDMA_MODIFY_QP_VALID_MAX_RD_ATOMIC_RESP));

	/* TBD: future use only
	 * #define ROCE_MODIFY_QP_RESP_RAMROD_DATA_PRI_FLG_MASK
	 * #define ROCE_MODIFY_QP_RESP_RAMROD_DATA_PRI_FLG_SHIFT
	 */

	SET_FIELD(p_ramrod->flags,
		  ROCE_MODIFY_QP_RESP_RAMROD_DATA_MIN_RNR_NAK_TIMER_FLG,
		  GET_FIELD(modify_flags,
			    ECORE_ROCE_MODIFY_QP_VALID_MIN_RNR_NAK_TIMER));

	p_ramrod->fields = 0;
	SET_FIELD(p_ramrod->fields,
		  ROCE_MODIFY_QP_RESP_RAMROD_DATA_MIN_RNR_NAK_TIMER,
		  qp->min_rnr_nak_timer);

	p_ramrod->max_ird = qp->max_rd_atomic_resp;
	p_ramrod->traffic_class = qp->traffic_class_tos;
	p_ramrod->hop_limit = qp->hop_limit_ttl;
	p_ramrod->p_key = OSAL_CPU_TO_LE16(qp->pkey);
	p_ramrod->flow_label = OSAL_CPU_TO_LE32(qp->flow_label);
	p_ramrod->mtu = OSAL_CPU_TO_LE16(qp->mtu);
	ecore_rdma_copy_gids(qp, p_ramrod->src_gid, p_ramrod->dst_gid);
	rc = ecore_spq_post(p_hwfn, p_ent, OSAL_NULL);

	DP_VERBOSE(p_hwfn, ECORE_MSG_RDMA, "Modify responder, rc = %d\n", rc);
	return rc;
}

static enum _ecore_status_t ecore_roce_sp_modify_requester(
	struct ecore_hwfn	*p_hwfn,
	struct ecore_rdma_qp	*qp,
	bool			move_to_sqd,
	bool			move_to_err,
	u32			modify_flags)
{
	struct roce_modify_qp_req_ramrod_data	*p_ramrod;
	struct ecore_sp_init_data		init_data;
	struct ecore_spq_entry			*p_ent;
	enum _ecore_status_t			rc;

	if (!qp->has_req)
		return ECORE_SUCCESS;

	DP_VERBOSE(p_hwfn, ECORE_MSG_RDMA, "icid = %08x\n", qp->icid);

	if (move_to_err && !(qp->req_offloaded))
		return ECORE_SUCCESS;

	/* Get SPQ entry */
	OSAL_MEMSET(&init_data, 0, sizeof(init_data));
	init_data.cid = qp->icid + 1;
	init_data.opaque_fid = p_hwfn->hw_info.opaque_fid;
	init_data.comp_mode = ECORE_SPQ_MODE_EBLOCK;

	rc = ecore_sp_init_request(p_hwfn, &p_ent,
				   ROCE_EVENT_MODIFY_QP,
				   PROTOCOLID_ROCE, &init_data);
	if (rc != ECORE_SUCCESS) {
		DP_NOTICE(p_hwfn, false, "rc = %d\n", rc);
		return rc;
	}

	p_ramrod = &p_ent->ramrod.roce_modify_qp_req;

	p_ramrod->flags = 0;

	SET_FIELD(p_ramrod->flags,
		  ROCE_MODIFY_QP_REQ_RAMROD_DATA_MOVE_TO_ERR_FLG,
		  move_to_err);

	SET_FIELD(p_ramrod->flags,
		  ROCE_MODIFY_QP_REQ_RAMROD_DATA_MOVE_TO_SQD_FLG,
		  move_to_sqd);

	SET_FIELD(p_ramrod->flags,
		  ROCE_MODIFY_QP_REQ_RAMROD_DATA_EN_SQD_ASYNC_NOTIFY,
		  qp->sqd_async);

	SET_FIELD(p_ramrod->flags,
		  ROCE_MODIFY_QP_REQ_RAMROD_DATA_P_KEY_FLG,
		  GET_FIELD(modify_flags, ECORE_ROCE_MODIFY_QP_VALID_PKEY));

	SET_FIELD(p_ramrod->flags,
		  ROCE_MODIFY_QP_REQ_RAMROD_DATA_ADDRESS_VECTOR_FLG,
		  GET_FIELD(modify_flags,
			    ECORE_ROCE_MODIFY_QP_VALID_ADDRESS_VECTOR));

	SET_FIELD(p_ramrod->flags,
		  ROCE_MODIFY_QP_REQ_RAMROD_DATA_MAX_ORD_FLG,
		  GET_FIELD(modify_flags,
			    ECORE_RDMA_MODIFY_QP_VALID_MAX_RD_ATOMIC_REQ));

	SET_FIELD(p_ramrod->flags,
		  ROCE_MODIFY_QP_REQ_RAMROD_DATA_RNR_NAK_CNT_FLG,
		  GET_FIELD(modify_flags,
			    ECORE_ROCE_MODIFY_QP_VALID_RNR_RETRY_CNT));

	SET_FIELD(p_ramrod->flags,
		  ROCE_MODIFY_QP_REQ_RAMROD_DATA_ERR_RETRY_CNT_FLG,
		  GET_FIELD(modify_flags,
			    ECORE_ROCE_MODIFY_QP_VALID_RETRY_CNT));

	SET_FIELD(p_ramrod->flags,
		  ROCE_MODIFY_QP_REQ_RAMROD_DATA_ACK_TIMEOUT_FLG,
		  GET_FIELD(modify_flags,
			    ECORE_ROCE_MODIFY_QP_VALID_ACK_TIMEOUT));

	/* TBD: future use only
	 * #define ROCE_MODIFY_QP_REQ_RAMROD_DATA_PRI_FLG_MASK
	 * #define ROCE_MODIFY_QP_REQ_RAMROD_DATA_PRI_FLG_SHIFT
	 */

	p_ramrod->fields = 0;
	SET_FIELD(p_ramrod->fields,
		  ROCE_MODIFY_QP_REQ_RAMROD_DATA_ERR_RETRY_CNT,
		  qp->retry_cnt);

	SET_FIELD(p_ramrod->fields,
		  ROCE_MODIFY_QP_REQ_RAMROD_DATA_RNR_NAK_CNT,
		  qp->rnr_retry_cnt);

	p_ramrod->max_ord = qp->max_rd_atomic_req;
	p_ramrod->traffic_class = qp->traffic_class_tos;
	p_ramrod->hop_limit = qp->hop_limit_ttl;
	p_ramrod->p_key = OSAL_CPU_TO_LE16(qp->pkey);
	p_ramrod->flow_label = OSAL_CPU_TO_LE32(qp->flow_label);
	p_ramrod->ack_timeout_val = OSAL_CPU_TO_LE32(qp->ack_timeout);
	p_ramrod->mtu = OSAL_CPU_TO_LE16(qp->mtu);
	ecore_rdma_copy_gids(qp, p_ramrod->src_gid, p_ramrod->dst_gid);
	rc = ecore_spq_post(p_hwfn, p_ent, OSAL_NULL);

	DP_VERBOSE(p_hwfn, ECORE_MSG_RDMA, "Modify requester, rc = %d\n", rc);
	return rc;
}

static enum _ecore_status_t ecore_roce_sp_destroy_qp_responder(
	struct ecore_hwfn    *p_hwfn,
	struct ecore_rdma_qp *qp,
	u32                  *num_invalidated_mw,
	u32                  *cq_prod)
{
	struct roce_destroy_qp_resp_output_params	*p_ramrod_res;
	struct roce_destroy_qp_resp_ramrod_data *p_ramrod;
	struct ecore_sp_init_data		init_data;
	struct ecore_spq_entry			*p_ent;
	dma_addr_t				ramrod_res_phys;
	enum _ecore_status_t			rc;

	if (!qp->has_resp) {
		*num_invalidated_mw = 0;
		*cq_prod = 0;
		return ECORE_SUCCESS;
	}

	DP_VERBOSE(p_hwfn, ECORE_MSG_RDMA, "icid = %08x\n", qp->icid);

	*num_invalidated_mw = 0;

	if (!qp->resp_offloaded) {
		*cq_prod = qp->cq_prod.resp;
		return ECORE_SUCCESS;
	}

	/* Get SPQ entry */
	OSAL_MEMSET(&init_data, 0, sizeof(init_data));
	init_data.cid = qp->icid;
	init_data.opaque_fid = p_hwfn->hw_info.opaque_fid;
	init_data.comp_mode = ECORE_SPQ_MODE_EBLOCK;

	rc = ecore_sp_init_request(p_hwfn, &p_ent,
				   ROCE_RAMROD_DESTROY_QP,
				   PROTOCOLID_ROCE, &init_data);
	if (rc != ECORE_SUCCESS)
		return rc;

	p_ramrod = &p_ent->ramrod.roce_destroy_qp_resp;

	p_ramrod_res = (struct roce_destroy_qp_resp_output_params *)OSAL_DMA_ALLOC_COHERENT(p_hwfn->p_dev,
		&ramrod_res_phys, sizeof(*p_ramrod_res));

	if (!p_ramrod_res)
	{
		rc = ECORE_NOMEM;
		DP_NOTICE(p_hwfn, false,
			  "ecore destroy responder failed: cannot allocate memory (ramrod). rc = %d\n",
			  rc);
		return rc;
	}

	DMA_REGPAIR_LE(p_ramrod->output_params_addr, ramrod_res_phys);

	rc = ecore_spq_post(p_hwfn, p_ent, OSAL_NULL);
	if (rc != ECORE_SUCCESS)
		goto err;

	*num_invalidated_mw
		= OSAL_LE32_TO_CPU(p_ramrod_res->num_invalidated_mw);
	*cq_prod = OSAL_LE32_TO_CPU(p_ramrod_res->cq_prod);
	qp->cq_prod.resp = *cq_prod;

	/* Free IRQ - only if ramrod succeeded, in case FW is still using it */
	OSAL_DMA_FREE_COHERENT(p_hwfn->p_dev,
			       qp->irq,
			       qp->irq_phys_addr,
			       qp->irq_num_pages *
			       RDMA_RING_PAGE_SIZE);

	qp->resp_offloaded = false;

	DP_VERBOSE(p_hwfn, ECORE_MSG_RDMA, "Destroy responder, rc = %d\n", rc);

	/* "fall through" */

err:
	OSAL_DMA_FREE_COHERENT(p_hwfn->p_dev, p_ramrod_res, ramrod_res_phys,
		sizeof(*p_ramrod_res));

	return rc;
}

static enum _ecore_status_t ecore_roce_sp_destroy_qp_requester(
	struct ecore_hwfn    *p_hwfn,
	struct ecore_rdma_qp *qp,
	u32                  *num_bound_mw,
	u32                  *cq_prod)
{
	struct roce_destroy_qp_req_output_params	*p_ramrod_res;
	struct roce_destroy_qp_req_ramrod_data	*p_ramrod;
	struct ecore_sp_init_data		init_data;
	struct ecore_spq_entry			*p_ent;
	dma_addr_t				ramrod_res_phys;
	enum _ecore_status_t			rc;

	if (!qp->has_req) {
		*num_bound_mw = 0;
		*cq_prod = 0;
		return ECORE_SUCCESS;
	}

	DP_VERBOSE(p_hwfn, ECORE_MSG_RDMA, "icid = %08x\n", qp->icid);

	if (!qp->req_offloaded) {
		*cq_prod = qp->cq_prod.req;
		return ECORE_SUCCESS;
	}

	p_ramrod_res = (struct roce_destroy_qp_req_output_params *)
			OSAL_DMA_ALLOC_COHERENT(p_hwfn->p_dev, &ramrod_res_phys,
				sizeof(*p_ramrod_res));
	if (!p_ramrod_res)
	{
		DP_NOTICE(p_hwfn, false,
			  "ecore destroy requester failed: cannot allocate memory (ramrod)\n");
		return ECORE_NOMEM;
	}

	/* Get SPQ entry */
	OSAL_MEMSET(&init_data, 0, sizeof(init_data));
	init_data.cid = qp->icid + 1;
	init_data.opaque_fid = p_hwfn->hw_info.opaque_fid;
	init_data.comp_mode = ECORE_SPQ_MODE_EBLOCK;

	rc = ecore_sp_init_request(p_hwfn, &p_ent, ROCE_RAMROD_DESTROY_QP,
				   PROTOCOLID_ROCE, &init_data);
	if (rc != ECORE_SUCCESS)
		goto err;

	p_ramrod = &p_ent->ramrod.roce_destroy_qp_req;
	DMA_REGPAIR_LE(p_ramrod->output_params_addr, ramrod_res_phys);

	rc = ecore_spq_post(p_hwfn, p_ent, OSAL_NULL);
	if (rc != ECORE_SUCCESS)
		goto err;

	*num_bound_mw = OSAL_LE32_TO_CPU(p_ramrod_res->num_bound_mw);
	*cq_prod = OSAL_LE32_TO_CPU(p_ramrod_res->cq_prod);
	qp->cq_prod.req = *cq_prod;

	/* Free ORQ - only if ramrod succeeded, in case FW is still using it */
	OSAL_DMA_FREE_COHERENT(p_hwfn->p_dev,
			       qp->orq,
			       qp->orq_phys_addr,
			       qp->orq_num_pages *
			       RDMA_RING_PAGE_SIZE);

	qp->req_offloaded = false;

	DP_VERBOSE(p_hwfn, ECORE_MSG_RDMA, "Destroy requester, rc = %d\n", rc);

	/* "fall through" */

err:
	OSAL_DMA_FREE_COHERENT(p_hwfn->p_dev, p_ramrod_res, ramrod_res_phys,
			       sizeof(*p_ramrod_res));

	return rc;
}

static OSAL_INLINE enum _ecore_status_t ecore_roce_sp_query_responder(
	struct ecore_hwfn *p_hwfn,
	struct ecore_rdma_qp *qp,
	struct ecore_rdma_query_qp_out_params *out_params)
{
	struct roce_query_qp_resp_output_params	*p_resp_ramrod_res;
	struct roce_query_qp_resp_ramrod_data	*p_resp_ramrod;
	struct ecore_sp_init_data		init_data;
	dma_addr_t				resp_ramrod_res_phys;
	struct ecore_spq_entry			*p_ent;
	enum _ecore_status_t			rc = ECORE_SUCCESS;
	bool					error_flag;

	if (!qp->resp_offloaded) {
		/* Don't send query qp for the responder */
		out_params->rq_psn = qp->rq_psn;

		return  ECORE_SUCCESS;
	}

	/* Send a query responder ramrod to the FW */
	p_resp_ramrod_res = (struct roce_query_qp_resp_output_params *)
		OSAL_DMA_ALLOC_COHERENT(p_hwfn->p_dev, &resp_ramrod_res_phys,
					sizeof(*p_resp_ramrod_res));
	if (!p_resp_ramrod_res)
	{
		DP_NOTICE(p_hwfn, false,
			  "ecore query qp failed: cannot allocate memory (ramrod)\n");
		return ECORE_NOMEM;
	}

	/* Get SPQ entry */
	OSAL_MEMSET(&init_data, 0, sizeof(init_data));
	init_data.cid = qp->icid;
	init_data.opaque_fid = p_hwfn->hw_info.opaque_fid;
	init_data.comp_mode = ECORE_SPQ_MODE_EBLOCK;
	rc = ecore_sp_init_request(p_hwfn, &p_ent, ROCE_RAMROD_QUERY_QP,
				   PROTOCOLID_ROCE, &init_data);
	if (rc != ECORE_SUCCESS)
		goto err;

	p_resp_ramrod = &p_ent->ramrod.roce_query_qp_resp;
	DMA_REGPAIR_LE(p_resp_ramrod->output_params_addr, resp_ramrod_res_phys);

	rc = ecore_spq_post(p_hwfn, p_ent, OSAL_NULL);
	if (rc != ECORE_SUCCESS)
		goto err;

	out_params->rq_psn = OSAL_LE32_TO_CPU(p_resp_ramrod_res->psn);
	error_flag = GET_FIELD(
			OSAL_LE32_TO_CPU(p_resp_ramrod_res->err_flag),
			ROCE_QUERY_QP_RESP_OUTPUT_PARAMS_ERROR_FLG);
	if (error_flag)
		qp->cur_state = ECORE_ROCE_QP_STATE_ERR;

err:
	OSAL_DMA_FREE_COHERENT(p_hwfn->p_dev, p_resp_ramrod_res,
			       resp_ramrod_res_phys,
			       sizeof(*p_resp_ramrod_res));

	return rc;
}

static OSAL_INLINE enum _ecore_status_t ecore_roce_sp_query_requester(
	struct ecore_hwfn *p_hwfn,
	struct ecore_rdma_qp *qp,
	struct ecore_rdma_query_qp_out_params *out_params,
	bool *sq_draining)
{
	struct roce_query_qp_req_output_params	*p_req_ramrod_res;
	struct roce_query_qp_req_ramrod_data	*p_req_ramrod;
	struct ecore_sp_init_data		init_data;
	dma_addr_t				req_ramrod_res_phys;
	struct ecore_spq_entry			*p_ent;
	enum _ecore_status_t			rc = ECORE_SUCCESS;
	bool					error_flag;

	if (!qp->req_offloaded)
	{
		/* Don't send query qp for the requester */
		out_params->sq_psn = qp->sq_psn;
		out_params->draining = false;

		*sq_draining = 0;

		return ECORE_SUCCESS;
	}

	/* Send a query requester ramrod to the FW */
	p_req_ramrod_res = (struct roce_query_qp_req_output_params *)
		OSAL_DMA_ALLOC_COHERENT(p_hwfn->p_dev, &req_ramrod_res_phys,
					sizeof(*p_req_ramrod_res));
	if (!p_req_ramrod_res)
	{
		DP_NOTICE(p_hwfn, false,
			  "ecore query qp failed: cannot allocate memory (ramrod). rc = %d\n",
			  rc);
		return ECORE_NOMEM;
	}

	/* Get SPQ entry */
	init_data.cid = qp->icid + 1;
	rc = ecore_sp_init_request(p_hwfn, &p_ent, ROCE_RAMROD_QUERY_QP,
				   PROTOCOLID_ROCE, &init_data);
	if (rc != ECORE_SUCCESS)
		goto err;

	p_req_ramrod = &p_ent->ramrod.roce_query_qp_req;
	DMA_REGPAIR_LE(p_req_ramrod->output_params_addr, req_ramrod_res_phys);

	rc = ecore_spq_post(p_hwfn, p_ent, OSAL_NULL);
	if (rc != ECORE_SUCCESS)
		goto err;

	out_params->sq_psn = OSAL_LE32_TO_CPU(p_req_ramrod_res->psn);
	error_flag = GET_FIELD(OSAL_LE32_TO_CPU(p_req_ramrod_res->flags),
			       ROCE_QUERY_QP_REQ_OUTPUT_PARAMS_ERR_FLG);
	if (error_flag)
		qp->cur_state = ECORE_ROCE_QP_STATE_ERR;
	else
		*sq_draining = GET_FIELD(
			OSAL_LE32_TO_CPU(p_req_ramrod_res->flags),
			ROCE_QUERY_QP_REQ_OUTPUT_PARAMS_SQ_DRAINING_FLG);

err:
	OSAL_DMA_FREE_COHERENT(p_hwfn->p_dev, p_req_ramrod_res,
			       req_ramrod_res_phys, sizeof(*p_req_ramrod_res));

	return rc;
}

enum _ecore_status_t ecore_roce_query_qp(
	struct ecore_hwfn *p_hwfn,
	struct ecore_rdma_qp *qp,
	struct ecore_rdma_query_qp_out_params *out_params)
{
	enum _ecore_status_t	rc;

	rc = ecore_roce_sp_query_responder(p_hwfn, qp, out_params);
	if (rc)
		return rc;

	rc = ecore_roce_sp_query_requester(p_hwfn, qp, out_params,
					   &out_params->draining);
	if (rc)
		return rc;

	out_params->state = qp->cur_state;

	return ECORE_SUCCESS;
}

enum _ecore_status_t ecore_roce_destroy_qp(struct ecore_hwfn *p_hwfn,
					   struct ecore_rdma_qp *qp,
					   struct ecore_rdma_destroy_qp_out_params *out_params)
{
	u32 cq_prod_resp = qp->cq_prod.resp, cq_prod_req = qp->cq_prod.req;
	u32 num_invalidated_mw = 0;
	u32 num_bound_mw = 0;
	enum _ecore_status_t rc;

	/* Destroys the specified QP
	 * Note: if qp state != RESET/ERR/INIT then upper driver first need to
	 * call modify qp to move the qp to ERR state
	 */
	if ((qp->cur_state != ECORE_ROCE_QP_STATE_RESET) &&
	    (qp->cur_state != ECORE_ROCE_QP_STATE_ERR) &&
	    (qp->cur_state != ECORE_ROCE_QP_STATE_INIT))
	{
		DP_NOTICE(p_hwfn,
			  true,
		   "QP must be in error, reset or init state before destroying it\n");
		return ECORE_INVAL;
	}

	if (qp->cur_state != ECORE_ROCE_QP_STATE_RESET) {
		rc = ecore_roce_sp_destroy_qp_responder(p_hwfn,
							qp,
							&num_invalidated_mw,
							&cq_prod_resp);
		if (rc != ECORE_SUCCESS)
			return rc;

		/* Send destroy requester ramrod */
		rc = ecore_roce_sp_destroy_qp_requester(p_hwfn, qp,
							&num_bound_mw,
							&cq_prod_req);
		if (rc != ECORE_SUCCESS)
			return rc;

		/* resp_ofload was true, num_invalidated_mw is valid */
		if (num_invalidated_mw != num_bound_mw) {
			DP_NOTICE(p_hwfn,
				  true,
				  "number of invalidate memory windows is different from bounded ones\n");
			return ECORE_INVAL;
		}
	}

	ecore_roce_free_qp(p_hwfn, qp->qp_idx);

	out_params->rq_cq_prod = cq_prod_resp;
	out_params->sq_cq_prod = cq_prod_req;

	return ECORE_SUCCESS;
}

enum _ecore_status_t ecore_roce_destroy_ud_qp(void *rdma_cxt, u16 cid)
{
	struct ecore_hwfn *p_hwfn = (struct ecore_hwfn *)rdma_cxt;
	struct ecore_sp_init_data init_data;
	struct ecore_spq_entry *p_ent;
	enum _ecore_status_t rc;

	if (!rdma_cxt) {
		DP_ERR(p_hwfn->p_dev,
		       "destroy ud qp failed due to NULL rdma_cxt\n");
		return ECORE_INVAL;
	}

	/* Get SPQ entry */
	OSAL_MEMSET(&init_data, 0, sizeof(init_data));
	init_data.cid = cid;
	init_data.opaque_fid = p_hwfn->hw_info.opaque_fid;
	init_data.comp_mode = ECORE_SPQ_MODE_EBLOCK;
	rc = ecore_sp_init_request(p_hwfn, &p_ent, ROCE_RAMROD_DESTROY_UD_QP,
				   PROTOCOLID_ROCE, &init_data);
	if (rc != ECORE_SUCCESS)
		goto err;

	rc = ecore_spq_post(p_hwfn, p_ent, OSAL_NULL);
	if (rc != ECORE_SUCCESS)
		goto err;

	ecore_roce_free_qp(p_hwfn, ECORE_ROCE_ICID_TO_QP(cid));

	DP_VERBOSE(p_hwfn, ECORE_MSG_RDMA, "freed a ud qp with cid=%d\n", cid);

	return ECORE_SUCCESS;

err:
	DP_ERR(p_hwfn, "failed destroying a ud qp with cid=%d\n", cid);

	return rc;
}


enum _ecore_status_t ecore_roce_create_ud_qp(void		*rdma_cxt,
			struct ecore_rdma_create_qp_out_params	*out_params)
{
	struct ecore_hwfn *p_hwfn = (struct ecore_hwfn *)rdma_cxt;
	struct ecore_sp_init_data init_data;
	struct ecore_spq_entry *p_ent;
	enum _ecore_status_t rc;
	u16 icid, qp_idx;

	if (!rdma_cxt || !out_params) {
		DP_ERR(p_hwfn->p_dev,
		       "ecore roce create ud qp failed due to NULL entry (rdma_cxt=%p, out=%p)\n",
		       rdma_cxt, out_params);
		return ECORE_INVAL;
	}

	rc = ecore_roce_alloc_qp_idx(p_hwfn, &qp_idx);
	if (rc != ECORE_SUCCESS)
		goto err;

	icid = ECORE_ROCE_QP_TO_ICID(qp_idx);

	/* Get SPQ entry */
	OSAL_MEMSET(&init_data, 0, sizeof(init_data));
	init_data.cid = icid;
	init_data.opaque_fid = p_hwfn->hw_info.opaque_fid;
	init_data.comp_mode = ECORE_SPQ_MODE_EBLOCK;
	rc = ecore_sp_init_request(p_hwfn, &p_ent, ROCE_RAMROD_CREATE_UD_QP,
				   PROTOCOLID_ROCE, &init_data);
	if (rc != ECORE_SUCCESS)
		goto err1;

	rc = ecore_spq_post(p_hwfn, p_ent, OSAL_NULL);
	if (rc != ECORE_SUCCESS)
		goto err1;

	out_params->icid = icid;
	out_params->qp_id = ((0xFF << 16) | icid);

	DP_VERBOSE(p_hwfn, ECORE_MSG_RDMA, "created a ud qp with icid=%d\n",
		   icid);

	return ECORE_SUCCESS;

err1:
	ecore_roce_free_qp(p_hwfn, qp_idx);

err:
	DP_VERBOSE(p_hwfn, ECORE_MSG_RDMA, "failed creating a ud qp\n");

	return rc;
}


enum _ecore_status_t
ecore_roce_modify_qp(struct ecore_hwfn *p_hwfn,
		     struct ecore_rdma_qp *qp,
		     enum ecore_roce_qp_state prev_state,
		     struct ecore_rdma_modify_qp_in_params *params)
{
	u32 num_invalidated_mw = 0, num_bound_mw = 0;
	enum _ecore_status_t rc = ECORE_SUCCESS;

	/* Perform additional operations according to the current state and the
	 * next state
	 */
	if (((prev_state == ECORE_ROCE_QP_STATE_INIT) ||
	     (prev_state == ECORE_ROCE_QP_STATE_RESET)) &&
	    (qp->cur_state == ECORE_ROCE_QP_STATE_RTR))
	{
		/* Init->RTR or Reset->RTR */

		/* Verify the cid bits that of this qp index are clear */
		rc = ecore_roce_wait_free_cids(p_hwfn, qp->qp_idx);
		if (rc)
			return rc;

		rc = ecore_roce_sp_create_responder(p_hwfn, qp);
		return rc;

	} else if ((prev_state == ECORE_ROCE_QP_STATE_RTR) &&
		   (qp->cur_state == ECORE_ROCE_QP_STATE_RTS))
	{
		/* RTR-> RTS */
		rc = ecore_roce_sp_create_requester(p_hwfn, qp);
		if (rc != ECORE_SUCCESS)
			return rc;

		/* Send modify responder ramrod */
		rc = ecore_roce_sp_modify_responder(p_hwfn, qp, false,
						    params->modify_flags);
		return rc;

	} else if ((prev_state == ECORE_ROCE_QP_STATE_RTS) &&
		   (qp->cur_state == ECORE_ROCE_QP_STATE_RTS))
	{
		/* RTS->RTS */
		rc = ecore_roce_sp_modify_responder(p_hwfn, qp, false,
						    params->modify_flags);
		if (rc != ECORE_SUCCESS)
			return rc;

		rc = ecore_roce_sp_modify_requester(p_hwfn, qp, false, false,
						    params->modify_flags);
		return rc;

	} else if ((prev_state == ECORE_ROCE_QP_STATE_RTS) &&
		   (qp->cur_state == ECORE_ROCE_QP_STATE_SQD))
	{
		/* RTS->SQD */
		rc = ecore_roce_sp_modify_requester(p_hwfn, qp, true, false,
						    params->modify_flags);
		return rc;

	} else if ((prev_state == ECORE_ROCE_QP_STATE_SQD) &&
		   (qp->cur_state == ECORE_ROCE_QP_STATE_SQD))
	{
		/* SQD->SQD */
		rc = ecore_roce_sp_modify_responder(p_hwfn, qp, false,
						    params->modify_flags);
		if (rc != ECORE_SUCCESS)
			return rc;

		rc = ecore_roce_sp_modify_requester(p_hwfn,  qp, false, false,
						    params->modify_flags);
		return rc;

	} else if ((prev_state == ECORE_ROCE_QP_STATE_SQD) &&
		 (qp->cur_state == ECORE_ROCE_QP_STATE_RTS))
	{
		/* SQD->RTS */
		rc = ecore_roce_sp_modify_responder(p_hwfn,  qp, false,
						    params->modify_flags);
		if (rc != ECORE_SUCCESS)
			return rc;

		rc = ecore_roce_sp_modify_requester(p_hwfn,  qp, false, false,
						    params->modify_flags);

		return rc;
	} else if (qp->cur_state == ECORE_ROCE_QP_STATE_ERR) {
		/* ->ERR */
		rc = ecore_roce_sp_modify_responder(p_hwfn, qp, true,
						    params->modify_flags);
		if (rc != ECORE_SUCCESS)
			return rc;

		rc = ecore_roce_sp_modify_requester(p_hwfn, qp, false, true,
						    params->modify_flags);
		return rc;

	} else if (qp->cur_state == ECORE_ROCE_QP_STATE_RESET) {
		/* Any state -> RESET */

		/* Send destroy responder ramrod */
		rc = ecore_roce_sp_destroy_qp_responder(p_hwfn, qp,
							&num_invalidated_mw,
							&qp->cq_prod.resp);

		if (rc != ECORE_SUCCESS)
			return rc;

		rc = ecore_roce_sp_destroy_qp_requester(p_hwfn, qp,
							&num_bound_mw,
							&qp->cq_prod.req);


		if (rc != ECORE_SUCCESS)
			return rc;

		if (num_invalidated_mw != num_bound_mw) {
			DP_NOTICE(p_hwfn,
				  true,
				  "number of invalidate memory windows is different from bounded ones\n");
			return ECORE_INVAL;
		}
	} else {
		DP_VERBOSE(p_hwfn, ECORE_MSG_RDMA, "ECORE_SUCCESS\n");
	}

	return rc;
}

static void ecore_roce_free_icid(struct ecore_hwfn *p_hwfn, u16 icid)
{
	struct ecore_rdma_info *p_rdma_info = p_hwfn->p_rdma_info;
	u32 start_cid, cid;

	start_cid = ecore_cxt_get_proto_cid_start(p_hwfn, p_rdma_info->proto);
	cid = icid - start_cid;

	OSAL_SPIN_LOCK(&p_rdma_info->lock);

	ecore_bmap_release_id(p_hwfn, &p_rdma_info->cid_map, cid);

	OSAL_SPIN_UNLOCK(&p_hwfn->p_rdma_info->lock);
}

static void ecore_rdma_dpm_conf(struct ecore_hwfn *p_hwfn,
				struct ecore_ptt *p_ptt)
{
	u32 val;

	val = (p_hwfn->dcbx_no_edpm || p_hwfn->db_bar_no_edpm) ? 0 : 1;

	ecore_wr(p_hwfn, p_ptt, DORQ_REG_PF_DPM_ENABLE, val);
	DP_VERBOSE(p_hwfn, (ECORE_MSG_DCB | ECORE_MSG_RDMA),
		   "Changing DPM_EN state to %d (DCBX=%d, DB_BAR=%d)\n",
		   val, p_hwfn->dcbx_no_edpm, p_hwfn->db_bar_no_edpm);
}

/* This function disables EDPM due to DCBx considerations */
void ecore_roce_dpm_dcbx(struct ecore_hwfn *p_hwfn, struct ecore_ptt *p_ptt)
{
	u8 val;

	/* if any QPs are already active, we want to disable DPM, since their
	 * context information contains information from before the latest DCBx
	 * update. Otherwise enable it.
	 */
	val = (ecore_rdma_allocated_qps(p_hwfn)) ? true : false;
	p_hwfn->dcbx_no_edpm = (u8)val;

	ecore_rdma_dpm_conf(p_hwfn, p_ptt);
}

/* This function disables EDPM due to doorbell bar considerations */
void ecore_rdma_dpm_bar(struct ecore_hwfn *p_hwfn, struct ecore_ptt *p_ptt)
{
	p_hwfn->db_bar_no_edpm = true;

	ecore_rdma_dpm_conf(p_hwfn, p_ptt);
}

enum _ecore_status_t ecore_roce_setup(struct ecore_hwfn *p_hwfn)
{
	return ecore_spq_register_async_cb(p_hwfn, PROTOCOLID_ROCE,
					   ecore_roce_async_event);
}

#ifdef _NTDDK_
#pragma warning(pop)
#endif
