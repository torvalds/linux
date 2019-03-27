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
 */

/*
 * File : ecore_l2.c
 */
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "bcm_osal.h"

#include "ecore.h"
#include "ecore_status.h"
#include "ecore_hsi_eth.h"
#include "ecore_chain.h"
#include "ecore_spq.h"
#include "ecore_init_fw_funcs.h"
#include "ecore_cxt.h"
#include "ecore_l2.h"
#include "ecore_sp_commands.h"
#include "ecore_gtt_reg_addr.h"
#include "ecore_iro.h"
#include "reg_addr.h"
#include "ecore_int.h"
#include "ecore_hw.h"
#include "ecore_vf.h"
#include "ecore_sriov.h"
#include "ecore_mcp.h"

#define ECORE_MAX_SGES_NUM 16
#define CRC32_POLY 0x1edc6f41

#ifdef _NTDDK_
#pragma warning(push)
#pragma warning(disable : 28167)
#pragma warning(disable : 28123)
#pragma warning(disable : 28121)
#endif

struct ecore_l2_info {
	u32 queues;
	unsigned long **pp_qid_usage;

	/* The lock is meant to synchronize access to the qid usage */
	osal_mutex_t lock;
};

enum _ecore_status_t ecore_l2_alloc(struct ecore_hwfn *p_hwfn)
{
	struct ecore_l2_info *p_l2_info;
	unsigned long **pp_qids;
	u32 i;

	if (!ECORE_IS_L2_PERSONALITY(p_hwfn))
		return ECORE_SUCCESS;

	p_l2_info = OSAL_VZALLOC(p_hwfn->p_dev, sizeof(*p_l2_info));
	if (!p_l2_info)
		return ECORE_NOMEM;
	p_hwfn->p_l2_info = p_l2_info;

	if (IS_PF(p_hwfn->p_dev)) {
		p_l2_info->queues = RESC_NUM(p_hwfn, ECORE_L2_QUEUE);
	} else {
		u8 rx = 0, tx = 0;

		ecore_vf_get_num_rxqs(p_hwfn, &rx);
		ecore_vf_get_num_txqs(p_hwfn, &tx);

		p_l2_info->queues = (u32)OSAL_MAX_T(u8, rx, tx);
	}

	pp_qids = OSAL_VZALLOC(p_hwfn->p_dev,
			       sizeof(unsigned long *) *
			       p_l2_info->queues);
	if (pp_qids == OSAL_NULL)
		return ECORE_NOMEM;
	p_l2_info->pp_qid_usage = pp_qids;

	for (i = 0; i < p_l2_info->queues; i++) {
		pp_qids[i] = OSAL_VZALLOC(p_hwfn->p_dev,
					  MAX_QUEUES_PER_QZONE / 8);
		if (pp_qids[i] == OSAL_NULL)
			return ECORE_NOMEM;
	}

#ifdef CONFIG_ECORE_LOCK_ALLOC
	if (OSAL_MUTEX_ALLOC(p_hwfn, &p_l2_info->lock))
		return ECORE_NOMEM;
#endif

	return ECORE_SUCCESS;
}

void ecore_l2_setup(struct ecore_hwfn *p_hwfn)
{
	if (!ECORE_IS_L2_PERSONALITY(p_hwfn))
		return;

	OSAL_MUTEX_INIT(&p_hwfn->p_l2_info->lock);
}

void ecore_l2_free(struct ecore_hwfn *p_hwfn)
{
	u32 i;

	if (!ECORE_IS_L2_PERSONALITY(p_hwfn))
		return;

	if (p_hwfn->p_l2_info == OSAL_NULL)
		return;

	if (p_hwfn->p_l2_info->pp_qid_usage == OSAL_NULL)
		goto out_l2_info;

	/* Free until hit first uninitialized entry */
	for (i = 0; i < p_hwfn->p_l2_info->queues; i++) {
		if (p_hwfn->p_l2_info->pp_qid_usage[i] == OSAL_NULL)
			break;
		OSAL_VFREE(p_hwfn->p_dev,
			   p_hwfn->p_l2_info->pp_qid_usage[i]);
		p_hwfn->p_l2_info->pp_qid_usage[i] = OSAL_NULL;
	}

#ifdef CONFIG_ECORE_LOCK_ALLOC
	/* Lock is last to initialize, if everything else was */
	if (i == p_hwfn->p_l2_info->queues)
		OSAL_MUTEX_DEALLOC(&p_hwfn->p_l2_info->lock);
#endif

	OSAL_VFREE(p_hwfn->p_dev, p_hwfn->p_l2_info->pp_qid_usage);
	p_hwfn->p_l2_info->pp_qid_usage = OSAL_NULL;

out_l2_info:
	OSAL_VFREE(p_hwfn->p_dev, p_hwfn->p_l2_info);
	p_hwfn->p_l2_info = OSAL_NULL;
}

/* TODO - we'll need locking around these... */
static bool ecore_eth_queue_qid_usage_add(struct ecore_hwfn *p_hwfn,
					  struct ecore_queue_cid *p_cid)
{
	struct ecore_l2_info *p_l2_info = p_hwfn->p_l2_info;
	u16 queue_id = p_cid->rel.queue_id;
	bool b_rc = true;
	u8 first;

	OSAL_MUTEX_ACQUIRE(&p_l2_info->lock);

	if (queue_id > p_l2_info->queues) {
		DP_NOTICE(p_hwfn, true,
			  "Requested to increase usage for qzone %04x out of %08x\n",
			  queue_id, p_l2_info->queues);
		b_rc = false;
		goto out;
	}

	first = (u8)OSAL_FIND_FIRST_ZERO_BIT(p_l2_info->pp_qid_usage[queue_id],
					     MAX_QUEUES_PER_QZONE);
	if (first >= MAX_QUEUES_PER_QZONE) {
		b_rc = false;
		goto out;
	}

	OSAL_SET_BIT(first, p_l2_info->pp_qid_usage[queue_id]);
	p_cid->qid_usage_idx = first;

out:
	OSAL_MUTEX_RELEASE(&p_l2_info->lock);
	return b_rc;
}

static void ecore_eth_queue_qid_usage_del(struct ecore_hwfn *p_hwfn,
					  struct ecore_queue_cid *p_cid)
{
	OSAL_MUTEX_ACQUIRE(&p_hwfn->p_l2_info->lock);

	OSAL_CLEAR_BIT(p_cid->qid_usage_idx,
		       p_hwfn->p_l2_info->pp_qid_usage[p_cid->rel.queue_id]);

	OSAL_MUTEX_RELEASE(&p_hwfn->p_l2_info->lock);
}

void ecore_eth_queue_cid_release(struct ecore_hwfn *p_hwfn,
				 struct ecore_queue_cid *p_cid)
{
	bool b_legacy_vf = !!(p_cid->vf_legacy &
			      ECORE_QCID_LEGACY_VF_CID);

	/* VFs' CIDs are 0-based in PF-view, and uninitialized on VF.
	 * For legacy vf-queues, the CID doesn't go through here.
	 */
	if (IS_PF(p_hwfn->p_dev) && !b_legacy_vf)
		_ecore_cxt_release_cid(p_hwfn, p_cid->cid, p_cid->vfid);

	/* VFs maintain the index inside queue-zone on their own */
	if (p_cid->vfid == ECORE_QUEUE_CID_PF)
		ecore_eth_queue_qid_usage_del(p_hwfn, p_cid);

	OSAL_VFREE(p_hwfn->p_dev, p_cid);
}

/* The internal is only meant to be directly called by PFs initializeing CIDs
 * for their VFs.
 */
static struct ecore_queue_cid *
_ecore_eth_queue_to_cid(struct ecore_hwfn *p_hwfn,
			u16 opaque_fid, u32 cid,
			struct ecore_queue_start_common_params *p_params,
			bool b_is_rx,
			struct ecore_queue_cid_vf_params *p_vf_params)
{
	struct ecore_queue_cid *p_cid;
	enum _ecore_status_t rc;

	p_cid = OSAL_VZALLOC(p_hwfn->p_dev, sizeof(*p_cid));
	if (p_cid == OSAL_NULL)
		return OSAL_NULL;

	p_cid->opaque_fid = opaque_fid;
	p_cid->cid = cid;
	p_cid->p_owner = p_hwfn;

	/* Fill in parameters */
	p_cid->rel.vport_id = p_params->vport_id;
	p_cid->rel.queue_id = p_params->queue_id;
	p_cid->rel.stats_id = p_params->stats_id;
	p_cid->sb_igu_id = p_params->p_sb->igu_sb_id;
	p_cid->b_is_rx = b_is_rx;
	p_cid->sb_idx = p_params->sb_idx;

	/* Fill-in bits related to VFs' queues if information was provided */
	if (p_vf_params != OSAL_NULL) {
		p_cid->vfid = p_vf_params->vfid;
		p_cid->vf_qid = p_vf_params->vf_qid;
		p_cid->vf_legacy = p_vf_params->vf_legacy;
	} else {
		p_cid->vfid = ECORE_QUEUE_CID_PF;
	}

	/* Don't try calculating the absolute indices for VFs */
	if (IS_VF(p_hwfn->p_dev)) {
		p_cid->abs = p_cid->rel;

		goto out;
	}

	/* Calculate the engine-absolute indices of the resources.
	 * This would guarantee they're valid later on.
	 * In some cases [SBs] we already have the right values.
	 */
	rc = ecore_fw_vport(p_hwfn, p_cid->rel.vport_id, &p_cid->abs.vport_id);
	if (rc != ECORE_SUCCESS)
		goto fail;

	rc = ecore_fw_l2_queue(p_hwfn, p_cid->rel.queue_id,
			       &p_cid->abs.queue_id);
	if (rc != ECORE_SUCCESS)
		goto fail;

	/* In case of a PF configuring its VF's queues, the stats-id is already
	 * absolute [since there's a single index that's suitable per-VF].
	 */
	if (p_cid->vfid == ECORE_QUEUE_CID_PF) {
		rc = ecore_fw_vport(p_hwfn, p_cid->rel.stats_id,
				    &p_cid->abs.stats_id);
		if (rc != ECORE_SUCCESS)
			goto fail;
	} else {
		p_cid->abs.stats_id = p_cid->rel.stats_id;
	}

out:
	/* VF-images have provided the qid_usage_idx on their own.
	 * Otherwise, we need to allocate a unique one.
	 */
	if (!p_vf_params) {
		if (!ecore_eth_queue_qid_usage_add(p_hwfn, p_cid))
			goto fail;
	} else {
		p_cid->qid_usage_idx = p_vf_params->qid_usage_idx;
	}

	DP_VERBOSE(p_hwfn, ECORE_MSG_SP,
		   "opaque_fid: %04x CID %08x vport %02x [%02x] qzone %04x.%02x [%04x] stats %02x [%02x] SB %04x PI %02x\n",
		   p_cid->opaque_fid, p_cid->cid,
		   p_cid->rel.vport_id, p_cid->abs.vport_id,
		   p_cid->rel.queue_id,	p_cid->qid_usage_idx,
		   p_cid->abs.queue_id,
		   p_cid->rel.stats_id, p_cid->abs.stats_id,
		   p_cid->sb_igu_id, p_cid->sb_idx);

	return p_cid;

fail:
	OSAL_VFREE(p_hwfn->p_dev, p_cid);
	return OSAL_NULL;
}

struct ecore_queue_cid *
ecore_eth_queue_to_cid(struct ecore_hwfn *p_hwfn, u16 opaque_fid,
		       struct ecore_queue_start_common_params *p_params,
		       bool b_is_rx,
		       struct ecore_queue_cid_vf_params *p_vf_params)
{
	struct ecore_queue_cid *p_cid;
	u8 vfid = ECORE_CXT_PF_CID;
	bool b_legacy_vf = false;
	u32 cid = 0;

	/* In case of legacy VFs, The CID can be derived from the additional
	 * VF parameters - the VF assumes queue X uses CID X, so we can simply
	 * use the vf_qid for this purpose as well.
	 */
	if (p_vf_params) {
		vfid = p_vf_params->vfid;

		if (p_vf_params->vf_legacy &
		    ECORE_QCID_LEGACY_VF_CID) {
			b_legacy_vf = true;
			cid = p_vf_params->vf_qid;
		}
	}

	/* Get a unique firmware CID for this queue, in case it's a PF.
	 * VF's don't need a CID as the queue configuration will be done
	 * by PF.
	 */
	if (IS_PF(p_hwfn->p_dev) && !b_legacy_vf) {
		if (_ecore_cxt_acquire_cid(p_hwfn, PROTOCOLID_ETH,
					   &cid, vfid) != ECORE_SUCCESS) {
			DP_NOTICE(p_hwfn, true, "Failed to acquire cid\n");
			return OSAL_NULL;
		}
	}

	p_cid = _ecore_eth_queue_to_cid(p_hwfn, opaque_fid, cid,
					p_params, b_is_rx, p_vf_params);
	if ((p_cid == OSAL_NULL) && IS_PF(p_hwfn->p_dev) && !b_legacy_vf)
		_ecore_cxt_release_cid(p_hwfn, cid, vfid);

	return p_cid;
}

static struct ecore_queue_cid *
ecore_eth_queue_to_cid_pf(struct ecore_hwfn *p_hwfn, u16 opaque_fid,
			  bool b_is_rx,
			  struct ecore_queue_start_common_params *p_params)
{
	return ecore_eth_queue_to_cid(p_hwfn, opaque_fid, p_params, b_is_rx,
				      OSAL_NULL);
}

enum _ecore_status_t ecore_sp_eth_vport_start(struct ecore_hwfn *p_hwfn,
					      struct ecore_sp_vport_start_params *p_params)
{
	struct vport_start_ramrod_data *p_ramrod = OSAL_NULL;
	struct ecore_spq_entry *p_ent = OSAL_NULL;
	struct ecore_sp_init_data init_data;
	struct eth_vport_tpa_param *p_tpa;
	u16 rx_mode = 0, tx_err = 0;
	u8 abs_vport_id = 0;
	enum _ecore_status_t rc = ECORE_NOTIMPL;

	rc = ecore_fw_vport(p_hwfn, p_params->vport_id, &abs_vport_id);
	if (rc != ECORE_SUCCESS)
		return rc;

	/* Get SPQ entry */
	OSAL_MEMSET(&init_data, 0, sizeof(init_data));
	init_data.cid = ecore_spq_get_cid(p_hwfn);
	init_data.opaque_fid = p_params->opaque_fid;
	init_data.comp_mode = ECORE_SPQ_MODE_EBLOCK;

	rc = ecore_sp_init_request(p_hwfn, &p_ent,
				   ETH_RAMROD_VPORT_START,
				   PROTOCOLID_ETH, &init_data);
	if (rc != ECORE_SUCCESS)
		return rc;

	p_ramrod = &p_ent->ramrod.vport_start;
	p_ramrod->vport_id = abs_vport_id;

	p_ramrod->mtu = OSAL_CPU_TO_LE16(p_params->mtu);
	p_ramrod->handle_ptp_pkts = p_params->handle_ptp_pkts;
	p_ramrod->inner_vlan_removal_en	= p_params->remove_inner_vlan;
	p_ramrod->drop_ttl0_en	= p_params->drop_ttl0;
	p_ramrod->untagged = p_params->only_untagged;
	p_ramrod->zero_placement_offset = p_params->zero_placement_offset;

	SET_FIELD(rx_mode, ETH_VPORT_RX_MODE_UCAST_DROP_ALL, 1);
	SET_FIELD(rx_mode, ETH_VPORT_RX_MODE_MCAST_DROP_ALL, 1);

	p_ramrod->rx_mode.state	= OSAL_CPU_TO_LE16(rx_mode);

	/* Handle requests for strict behavior on transmission errors */
	SET_FIELD(tx_err, ETH_TX_ERR_VALS_ILLEGAL_VLAN_MODE,
		  p_params->b_err_illegal_vlan_mode ?
		  ETH_TX_ERR_ASSERT_MALICIOUS : 0);
	SET_FIELD(tx_err, ETH_TX_ERR_VALS_PACKET_TOO_SMALL,
		  p_params->b_err_small_pkt ?
		  ETH_TX_ERR_ASSERT_MALICIOUS : 0);
	SET_FIELD(tx_err, ETH_TX_ERR_VALS_ANTI_SPOOFING_ERR,
		  p_params->b_err_anti_spoof ?
		  ETH_TX_ERR_ASSERT_MALICIOUS : 0);
	SET_FIELD(tx_err, ETH_TX_ERR_VALS_ILLEGAL_INBAND_TAGS,
		  p_params->b_err_illegal_inband_mode ?
		  ETH_TX_ERR_ASSERT_MALICIOUS : 0);
	SET_FIELD(tx_err, ETH_TX_ERR_VALS_VLAN_INSERTION_W_INBAND_TAG,
		  p_params->b_err_vlan_insert_with_inband ?
		  ETH_TX_ERR_ASSERT_MALICIOUS : 0);
	SET_FIELD(tx_err, ETH_TX_ERR_VALS_MTU_VIOLATION,
		  p_params->b_err_big_pkt ?
		  ETH_TX_ERR_ASSERT_MALICIOUS : 0);
	SET_FIELD(tx_err, ETH_TX_ERR_VALS_ILLEGAL_CONTROL_FRAME,
		  p_params->b_err_ctrl_frame ?
		  ETH_TX_ERR_ASSERT_MALICIOUS : 0);
	p_ramrod->tx_err_behav.values = OSAL_CPU_TO_LE16(tx_err);

	/* TPA related fields */
	p_tpa = &p_ramrod->tpa_param;
	OSAL_MEMSET(p_tpa, 0, sizeof(struct eth_vport_tpa_param));
	p_tpa->max_buff_num = p_params->max_buffers_per_cqe;

	switch (p_params->tpa_mode) {
	case ECORE_TPA_MODE_GRO:
		p_tpa->tpa_max_aggs_num = ETH_TPA_MAX_AGGS_NUM;
		p_tpa->tpa_max_size = (u16)-1;
		p_tpa->tpa_min_size_to_cont = p_params->mtu/2;
		p_tpa->tpa_min_size_to_start = p_params->mtu/2;
		p_tpa->tpa_ipv4_en_flg = 1;
		p_tpa->tpa_ipv6_en_flg = 1;
		p_tpa->tpa_ipv4_tunn_en_flg = 1;
		p_tpa->tpa_ipv6_tunn_en_flg = 1;
		p_tpa->tpa_pkt_split_flg = 1;
		p_tpa->tpa_gro_consistent_flg = 1;
		break;
	default:
		break;
	}

	p_ramrod->tx_switching_en = p_params->tx_switching;
#ifndef ASIC_ONLY
	if (CHIP_REV_IS_SLOW(p_hwfn->p_dev))
		p_ramrod->tx_switching_en = 0;
#endif

	p_ramrod->ctl_frame_mac_check_en = !!p_params->check_mac;
	p_ramrod->ctl_frame_ethtype_check_en = !!p_params->check_ethtype;

	/* Software Function ID in hwfn (PFs are 0 - 15, VFs are 16 - 135) */
	p_ramrod->sw_fid = ecore_concrete_to_sw_fid(p_params->concrete_fid);

	return ecore_spq_post(p_hwfn, p_ent, OSAL_NULL);
}

enum _ecore_status_t ecore_sp_vport_start(struct ecore_hwfn *p_hwfn,
					  struct ecore_sp_vport_start_params *p_params)
{
	if (IS_VF(p_hwfn->p_dev))
		return ecore_vf_pf_vport_start(p_hwfn, p_params->vport_id,
					       p_params->mtu,
					       p_params->remove_inner_vlan,
					       p_params->tpa_mode,
					       p_params->max_buffers_per_cqe,
					       p_params->only_untagged,
					       p_params->zero_placement_offset);

	return ecore_sp_eth_vport_start(p_hwfn, p_params);
}

static enum _ecore_status_t
ecore_sp_vport_update_rss(struct ecore_hwfn *p_hwfn,
			  struct vport_update_ramrod_data *p_ramrod,
			  struct ecore_rss_params *p_rss)
{
	struct eth_vport_rss_config *p_config;
	u16 capabilities = 0;
	int i, table_size;
	enum _ecore_status_t rc = ECORE_SUCCESS;

	if (!p_rss) {
		p_ramrod->common.update_rss_flg = 0;
		return rc;
	}
	p_config = &p_ramrod->rss_config;

	OSAL_BUILD_BUG_ON(ECORE_RSS_IND_TABLE_SIZE !=
			   ETH_RSS_IND_TABLE_ENTRIES_NUM);

	rc = ecore_fw_rss_eng(p_hwfn, p_rss->rss_eng_id,
			      &p_config->rss_id);
	if (rc != ECORE_SUCCESS)
		return rc;

	p_ramrod->common.update_rss_flg = p_rss->update_rss_config;
	p_config->update_rss_capabilities = p_rss->update_rss_capabilities;
	p_config->update_rss_ind_table = p_rss->update_rss_ind_table;
	p_config->update_rss_key = p_rss->update_rss_key;

	p_config->rss_mode = p_rss->rss_enable ?
			     ETH_VPORT_RSS_MODE_REGULAR :
			     ETH_VPORT_RSS_MODE_DISABLED;

	p_config->capabilities = 0;

	SET_FIELD(capabilities,
		  ETH_VPORT_RSS_CONFIG_IPV4_CAPABILITY,
		  !!(p_rss->rss_caps & ECORE_RSS_IPV4));
	SET_FIELD(capabilities,
		  ETH_VPORT_RSS_CONFIG_IPV6_CAPABILITY,
		  !!(p_rss->rss_caps & ECORE_RSS_IPV6));
	SET_FIELD(capabilities,
		  ETH_VPORT_RSS_CONFIG_IPV4_TCP_CAPABILITY,
		  !!(p_rss->rss_caps & ECORE_RSS_IPV4_TCP));
	SET_FIELD(capabilities,
		  ETH_VPORT_RSS_CONFIG_IPV6_TCP_CAPABILITY,
		  !!(p_rss->rss_caps & ECORE_RSS_IPV6_TCP));
	SET_FIELD(capabilities,
		  ETH_VPORT_RSS_CONFIG_IPV4_UDP_CAPABILITY,
		  !!(p_rss->rss_caps & ECORE_RSS_IPV4_UDP));
	SET_FIELD(capabilities,
		  ETH_VPORT_RSS_CONFIG_IPV6_UDP_CAPABILITY,
		  !!(p_rss->rss_caps & ECORE_RSS_IPV6_UDP));
	p_config->tbl_size = p_rss->rss_table_size_log;
	p_config->capabilities = OSAL_CPU_TO_LE16(capabilities);

	DP_VERBOSE(p_hwfn, ECORE_MSG_IFUP,
		   "update rss flag %d, rss_mode = %d, update_caps = %d, capabilities = %d, update_ind = %d, update_rss_key = %d\n",
		   p_ramrod->common.update_rss_flg,
		   p_config->rss_mode,
		   p_config->update_rss_capabilities,
		   p_config->capabilities,
		   p_config->update_rss_ind_table,
		   p_config->update_rss_key);

	table_size = OSAL_MIN_T(int, ECORE_RSS_IND_TABLE_SIZE,
				1 << p_config->tbl_size);
	for (i = 0; i < table_size; i++) {
		struct ecore_queue_cid *p_queue = p_rss->rss_ind_table[i];

		if (!p_queue)
			return ECORE_INVAL;

		p_config->indirection_table[i] =
				OSAL_CPU_TO_LE16(p_queue->abs.queue_id);
	}

	DP_VERBOSE(p_hwfn, ECORE_MSG_IFUP,
		   "Configured RSS indirection table [%d entries]:\n",
		   table_size);
	for (i = 0; i < ECORE_RSS_IND_TABLE_SIZE; i += 0x10) {
		DP_VERBOSE(p_hwfn, ECORE_MSG_IFUP,
			   "%04x %04x %04x %04x %04x %04x %04x %04x %04x %04x %04x %04x %04x %04x %04x %04x\n",
			   OSAL_LE16_TO_CPU(p_config->indirection_table[i]),
			   OSAL_LE16_TO_CPU(p_config->indirection_table[i + 1]),
			   OSAL_LE16_TO_CPU(p_config->indirection_table[i + 2]),
			   OSAL_LE16_TO_CPU(p_config->indirection_table[i + 3]),
			   OSAL_LE16_TO_CPU(p_config->indirection_table[i + 4]),
			   OSAL_LE16_TO_CPU(p_config->indirection_table[i + 5]),
			   OSAL_LE16_TO_CPU(p_config->indirection_table[i + 6]),
			   OSAL_LE16_TO_CPU(p_config->indirection_table[i + 7]),
			   OSAL_LE16_TO_CPU(p_config->indirection_table[i + 8]),
			   OSAL_LE16_TO_CPU(p_config->indirection_table[i + 9]),
			   OSAL_LE16_TO_CPU(p_config->indirection_table[i + 10]),
			   OSAL_LE16_TO_CPU(p_config->indirection_table[i + 11]),
			   OSAL_LE16_TO_CPU(p_config->indirection_table[i + 12]),
			   OSAL_LE16_TO_CPU(p_config->indirection_table[i + 13]),
			   OSAL_LE16_TO_CPU(p_config->indirection_table[i + 14]),
			   OSAL_LE16_TO_CPU(p_config->indirection_table[i + 15]));
	}

	for (i = 0; i <  10; i++)
		p_config->rss_key[i] = OSAL_CPU_TO_LE32(p_rss->rss_key[i]);

	return rc;
}

static void
ecore_sp_update_accept_mode(struct ecore_hwfn *p_hwfn,
			    struct vport_update_ramrod_data *p_ramrod,
			    struct ecore_filter_accept_flags accept_flags)
{
	p_ramrod->common.update_rx_mode_flg =
					accept_flags.update_rx_mode_config;
	p_ramrod->common.update_tx_mode_flg =
					accept_flags.update_tx_mode_config;

#ifndef ASIC_ONLY
	/* On B0 emulation we cannot enable Tx, since this would cause writes
	 * to PVFC HW block which isn't implemented in emulation.
	 */
	if (CHIP_REV_IS_SLOW(p_hwfn->p_dev)) {
		DP_VERBOSE(p_hwfn, ECORE_MSG_SP,
			   "Non-Asic - prevent Tx mode in vport update\n");
		p_ramrod->common.update_tx_mode_flg = 0;
	}
#endif

	/* Set Rx mode accept flags */
	if (p_ramrod->common.update_rx_mode_flg) {
		u8 accept_filter = accept_flags.rx_accept_filter;
		u16 state = 0;

		SET_FIELD(state, ETH_VPORT_RX_MODE_UCAST_DROP_ALL,
			  !(!!(accept_filter & ECORE_ACCEPT_UCAST_MATCHED) ||
			   !!(accept_filter & ECORE_ACCEPT_UCAST_UNMATCHED)));

		SET_FIELD(state, ETH_VPORT_RX_MODE_UCAST_ACCEPT_UNMATCHED,
			  !!(accept_filter & ECORE_ACCEPT_UCAST_UNMATCHED));

		SET_FIELD(state, ETH_VPORT_RX_MODE_MCAST_DROP_ALL,
			  !(!!(accept_filter & ECORE_ACCEPT_MCAST_MATCHED) ||
			   !!(accept_filter & ECORE_ACCEPT_MCAST_UNMATCHED)));

		SET_FIELD(state, ETH_VPORT_RX_MODE_MCAST_ACCEPT_ALL,
			  (!!(accept_filter & ECORE_ACCEPT_MCAST_MATCHED) &&
			   !!(accept_filter & ECORE_ACCEPT_MCAST_UNMATCHED)));

		SET_FIELD(state, ETH_VPORT_RX_MODE_BCAST_ACCEPT_ALL,
			  !!(accept_filter & ECORE_ACCEPT_BCAST));

		p_ramrod->rx_mode.state = OSAL_CPU_TO_LE16(state);
		DP_VERBOSE(p_hwfn, ECORE_MSG_SP,
			   "vport[%02x] p_ramrod->rx_mode.state = 0x%x\n",
			   p_ramrod->common.vport_id, state);
	}

	/* Set Tx mode accept flags */
	if (p_ramrod->common.update_tx_mode_flg) {
		u8 accept_filter = accept_flags.tx_accept_filter;
		u16 state = 0;

		SET_FIELD(state, ETH_VPORT_TX_MODE_UCAST_DROP_ALL,
			  !!(accept_filter & ECORE_ACCEPT_NONE));

		SET_FIELD(state, ETH_VPORT_TX_MODE_MCAST_DROP_ALL,
			  !!(accept_filter & ECORE_ACCEPT_NONE));

		SET_FIELD(state, ETH_VPORT_TX_MODE_MCAST_ACCEPT_ALL,
			  (!!(accept_filter & ECORE_ACCEPT_MCAST_MATCHED) &&
			   !!(accept_filter & ECORE_ACCEPT_MCAST_UNMATCHED)));

		SET_FIELD(state, ETH_VPORT_TX_MODE_BCAST_ACCEPT_ALL,
			  !!(accept_filter & ECORE_ACCEPT_BCAST));

		p_ramrod->tx_mode.state = OSAL_CPU_TO_LE16(state);
		DP_VERBOSE(p_hwfn, ECORE_MSG_SP,
			   "vport[%02x] p_ramrod->tx_mode.state = 0x%x\n",
			   p_ramrod->common.vport_id, state);
	}
}

static void
ecore_sp_vport_update_sge_tpa(struct vport_update_ramrod_data *p_ramrod,
			      struct ecore_sge_tpa_params *p_params)
{
	struct eth_vport_tpa_param *p_tpa;
	u16 val;

	if (!p_params) {
		p_ramrod->common.update_tpa_param_flg = 0;
		p_ramrod->common.update_tpa_en_flg = 0;
		p_ramrod->common.update_tpa_param_flg = 0;
		return;
	}

	p_ramrod->common.update_tpa_en_flg = p_params->update_tpa_en_flg;
	p_tpa = &p_ramrod->tpa_param;
	p_tpa->tpa_ipv4_en_flg = p_params->tpa_ipv4_en_flg;
	p_tpa->tpa_ipv6_en_flg = p_params->tpa_ipv6_en_flg;
	p_tpa->tpa_ipv4_tunn_en_flg = p_params->tpa_ipv4_tunn_en_flg;
	p_tpa->tpa_ipv6_tunn_en_flg = p_params->tpa_ipv6_tunn_en_flg;

	p_ramrod->common.update_tpa_param_flg = p_params->update_tpa_param_flg;
	p_tpa->max_buff_num = p_params->max_buffers_per_cqe;
	p_tpa->tpa_pkt_split_flg = p_params->tpa_pkt_split_flg;
	p_tpa->tpa_hdr_data_split_flg = p_params->tpa_hdr_data_split_flg;
	p_tpa->tpa_gro_consistent_flg = p_params->tpa_gro_consistent_flg;
	p_tpa->tpa_max_aggs_num = p_params->tpa_max_aggs_num;
	val = p_params->tpa_max_size;
	p_tpa->tpa_max_size = OSAL_CPU_TO_LE16(val);
	val = p_params->tpa_min_size_to_start;
	p_tpa->tpa_min_size_to_start = OSAL_CPU_TO_LE16(val);
	val = p_params->tpa_min_size_to_cont;
	p_tpa->tpa_min_size_to_cont = OSAL_CPU_TO_LE16(val);
}

static void
ecore_sp_update_mcast_bin(struct vport_update_ramrod_data *p_ramrod,
			  struct ecore_sp_vport_update_params *p_params)
{
	int i;

	OSAL_MEMSET(&p_ramrod->approx_mcast.bins, 0,
		    sizeof(p_ramrod->approx_mcast.bins));

	if (!p_params->update_approx_mcast_flg)
		return;

	p_ramrod->common.update_approx_mcast_flg = 1;
	for (i = 0; i < ETH_MULTICAST_MAC_BINS_IN_REGS; i++) {
		u32 *p_bins = p_params->bins;

		p_ramrod->approx_mcast.bins[i] = OSAL_CPU_TO_LE32(p_bins[i]);
	}
}

enum _ecore_status_t ecore_sp_vport_update(struct ecore_hwfn *p_hwfn,
					   struct ecore_sp_vport_update_params *p_params,
					   enum spq_mode comp_mode,
					   struct ecore_spq_comp_cb *p_comp_data)
{
	struct ecore_rss_params *p_rss_params = p_params->rss_params;
	struct vport_update_ramrod_data_cmn *p_cmn;
	struct ecore_sp_init_data init_data;
	struct vport_update_ramrod_data *p_ramrod = OSAL_NULL;
	struct ecore_spq_entry *p_ent = OSAL_NULL;
	u8 abs_vport_id = 0, val;
	enum _ecore_status_t rc = ECORE_NOTIMPL;

	if (IS_VF(p_hwfn->p_dev)) {
		rc = ecore_vf_pf_vport_update(p_hwfn, p_params);
		return rc;
	}

	rc = ecore_fw_vport(p_hwfn, p_params->vport_id, &abs_vport_id);
	if (rc != ECORE_SUCCESS)
		return rc;

	/* Get SPQ entry */
	OSAL_MEMSET(&init_data, 0, sizeof(init_data));
	init_data.cid = ecore_spq_get_cid(p_hwfn);
	init_data.opaque_fid = p_params->opaque_fid;
	init_data.comp_mode = comp_mode;
	init_data.p_comp_data = p_comp_data;

	rc = ecore_sp_init_request(p_hwfn, &p_ent,
				   ETH_RAMROD_VPORT_UPDATE,
				   PROTOCOLID_ETH, &init_data);
	if (rc != ECORE_SUCCESS)
		return rc;

	/* Copy input params to ramrod according to FW struct */
	p_ramrod = &p_ent->ramrod.vport_update;
	p_cmn = &p_ramrod->common;

	p_cmn->vport_id = abs_vport_id;

	p_cmn->rx_active_flg = p_params->vport_active_rx_flg;
	p_cmn->update_rx_active_flg = p_params->update_vport_active_rx_flg;
	p_cmn->tx_active_flg = p_params->vport_active_tx_flg;
	p_cmn->update_tx_active_flg = p_params->update_vport_active_tx_flg;

	p_cmn->accept_any_vlan = p_params->accept_any_vlan;
	val = p_params->update_accept_any_vlan_flg;
	p_cmn->update_accept_any_vlan_flg = val;

	p_cmn->inner_vlan_removal_en = p_params->inner_vlan_removal_flg;
	val = p_params->update_inner_vlan_removal_flg;
	p_cmn->update_inner_vlan_removal_en_flg = val;

	p_cmn->default_vlan_en = p_params->default_vlan_enable_flg;
	val = p_params->update_default_vlan_enable_flg;
	p_cmn->update_default_vlan_en_flg = val;

	p_cmn->default_vlan = OSAL_CPU_TO_LE16(p_params->default_vlan);
	p_cmn->update_default_vlan_flg = p_params->update_default_vlan_flg;

	p_cmn->silent_vlan_removal_en = p_params->silent_vlan_removal_flg;

	p_ramrod->common.tx_switching_en = p_params->tx_switching_flg;
#ifndef ASIC_ONLY
	if (CHIP_REV_IS_FPGA(p_hwfn->p_dev))
		if (p_ramrod->common.tx_switching_en ||
		    p_ramrod->common.update_tx_switching_en_flg) {
			DP_NOTICE(p_hwfn, false, "FPGA - why are we seeing tx-switching? Overriding it\n");
			p_ramrod->common.tx_switching_en = 0;
			p_ramrod->common.update_tx_switching_en_flg = 1;
		}
#endif
	p_cmn->update_tx_switching_en_flg = p_params->update_tx_switching_flg;

	p_cmn->anti_spoofing_en = p_params->anti_spoofing_en;
	val = p_params->update_anti_spoofing_en_flg;
	p_ramrod->common.update_anti_spoofing_en_flg = val;
	
	rc = ecore_sp_vport_update_rss(p_hwfn, p_ramrod, p_rss_params);
	if (rc != ECORE_SUCCESS) {
		/* Return spq entry which is taken in ecore_sp_init_request()*/
		ecore_spq_return_entry(p_hwfn, p_ent);
		return rc;
	}

	/* Update mcast bins for VFs, PF doesn't use this functionality */
	ecore_sp_update_mcast_bin(p_ramrod, p_params);

	ecore_sp_update_accept_mode(p_hwfn, p_ramrod, p_params->accept_flags);
	ecore_sp_vport_update_sge_tpa(p_ramrod, p_params->sge_tpa_params);
	return ecore_spq_post(p_hwfn, p_ent, OSAL_NULL);
}

enum _ecore_status_t ecore_sp_vport_stop(struct ecore_hwfn *p_hwfn,
					  u16 opaque_fid,
					  u8 vport_id)
{
	struct vport_stop_ramrod_data *p_ramrod;
	struct ecore_sp_init_data init_data;
	struct ecore_spq_entry *p_ent;
	u8 abs_vport_id = 0;
	enum _ecore_status_t rc;

	if (IS_VF(p_hwfn->p_dev))
		return ecore_vf_pf_vport_stop(p_hwfn);

	rc = ecore_fw_vport(p_hwfn, vport_id, &abs_vport_id);
	if (rc != ECORE_SUCCESS)
		return rc;

	/* Get SPQ entry */
	OSAL_MEMSET(&init_data, 0, sizeof(init_data));
	init_data.cid = ecore_spq_get_cid(p_hwfn);
	init_data.opaque_fid = opaque_fid;
	init_data.comp_mode = ECORE_SPQ_MODE_EBLOCK;

	rc = ecore_sp_init_request(p_hwfn, &p_ent,
				   ETH_RAMROD_VPORT_STOP,
				   PROTOCOLID_ETH, &init_data);
	if (rc != ECORE_SUCCESS)
		return rc;

	p_ramrod = &p_ent->ramrod.vport_stop;
	p_ramrod->vport_id = abs_vport_id;

	return ecore_spq_post(p_hwfn, p_ent, OSAL_NULL);
}

static enum _ecore_status_t
ecore_vf_pf_accept_flags(struct ecore_hwfn *p_hwfn,
			 struct ecore_filter_accept_flags *p_accept_flags)
{
	struct ecore_sp_vport_update_params s_params;

	OSAL_MEMSET(&s_params, 0, sizeof(s_params));
	OSAL_MEMCPY(&s_params.accept_flags, p_accept_flags,
		    sizeof(struct ecore_filter_accept_flags));

	return ecore_vf_pf_vport_update(p_hwfn, &s_params);
}

enum _ecore_status_t ecore_filter_accept_cmd(struct ecore_dev *p_dev,
					     u8 vport,
					     struct ecore_filter_accept_flags accept_flags,
					     u8 update_accept_any_vlan,
					     u8 accept_any_vlan,
					     enum spq_mode comp_mode,
					     struct ecore_spq_comp_cb *p_comp_data)
{
	struct ecore_sp_vport_update_params vport_update_params;
	int i, rc;

	/* Prepare and send the vport rx_mode change */
	OSAL_MEMSET(&vport_update_params, 0, sizeof(vport_update_params));
	vport_update_params.vport_id = vport;
	vport_update_params.accept_flags = accept_flags;
	vport_update_params.update_accept_any_vlan_flg = update_accept_any_vlan;
	vport_update_params.accept_any_vlan = accept_any_vlan;

	for_each_hwfn(p_dev, i) {
		struct ecore_hwfn *p_hwfn = &p_dev->hwfns[i];

		vport_update_params.opaque_fid = p_hwfn->hw_info.opaque_fid;

		if (IS_VF(p_dev)) {
			rc = ecore_vf_pf_accept_flags(p_hwfn, &accept_flags);
			if (rc != ECORE_SUCCESS)
				return rc;
			continue;
		}

		rc = ecore_sp_vport_update(p_hwfn, &vport_update_params,
					   comp_mode, p_comp_data);
		if (rc != ECORE_SUCCESS) {
			DP_ERR(p_dev, "Update rx_mode failed %d\n", rc);
			return rc;
		}

		DP_VERBOSE(p_hwfn, ECORE_MSG_SP,
			   "Accept filter configured, flags = [Rx]%x [Tx]%x\n",
			   accept_flags.rx_accept_filter,
			   accept_flags.tx_accept_filter);

		if (update_accept_any_vlan)
			DP_VERBOSE(p_hwfn, ECORE_MSG_SP,
				   "accept_any_vlan=%d configured\n",
				   accept_any_vlan);
	}

	return 0;
}

enum _ecore_status_t
ecore_eth_rxq_start_ramrod(struct ecore_hwfn *p_hwfn,
			   struct ecore_queue_cid *p_cid,
			   u16 bd_max_bytes,
			   dma_addr_t bd_chain_phys_addr,
			   dma_addr_t cqe_pbl_addr,
			   u16 cqe_pbl_size)
{
	struct rx_queue_start_ramrod_data *p_ramrod = OSAL_NULL;
	struct ecore_spq_entry *p_ent = OSAL_NULL;
	struct ecore_sp_init_data init_data;
	enum _ecore_status_t rc = ECORE_NOTIMPL;

	DP_VERBOSE(p_hwfn, ECORE_MSG_SP, "opaque_fid=0x%x, cid=0x%x, rx_qzone=0x%x, vport_id=0x%x, sb_id=0x%x\n",
		   p_cid->opaque_fid, p_cid->cid, p_cid->abs.queue_id,
		   p_cid->abs.vport_id, p_cid->sb_igu_id);

	/* Get SPQ entry */
	OSAL_MEMSET(&init_data, 0, sizeof(init_data));
	init_data.cid = p_cid->cid;
	init_data.opaque_fid = p_cid->opaque_fid;
	init_data.comp_mode = ECORE_SPQ_MODE_EBLOCK;

	rc = ecore_sp_init_request(p_hwfn, &p_ent,
				   ETH_RAMROD_RX_QUEUE_START,
				   PROTOCOLID_ETH, &init_data);
	if (rc != ECORE_SUCCESS)
		return rc;

	p_ramrod = &p_ent->ramrod.rx_queue_start;

	p_ramrod->sb_id = OSAL_CPU_TO_LE16(p_cid->sb_igu_id);
	p_ramrod->sb_index = p_cid->sb_idx;
	p_ramrod->vport_id = p_cid->abs.vport_id;
	p_ramrod->stats_counter_id = p_cid->abs.stats_id;
	p_ramrod->rx_queue_id = OSAL_CPU_TO_LE16(p_cid->abs.queue_id);
	p_ramrod->complete_cqe_flg = 0;
	p_ramrod->complete_event_flg = 1;

	p_ramrod->bd_max_bytes = OSAL_CPU_TO_LE16(bd_max_bytes);
	DMA_REGPAIR_LE(p_ramrod->bd_base, bd_chain_phys_addr);

	p_ramrod->num_of_pbl_pages = OSAL_CPU_TO_LE16(cqe_pbl_size);
	DMA_REGPAIR_LE(p_ramrod->cqe_pbl_addr, cqe_pbl_addr);

	if (p_cid->vfid != ECORE_QUEUE_CID_PF) {
		bool b_legacy_vf = !!(p_cid->vf_legacy &
				      ECORE_QCID_LEGACY_VF_RX_PROD);

		p_ramrod->vf_rx_prod_index = p_cid->vf_qid;
		DP_VERBOSE(p_hwfn, ECORE_MSG_SP, "Queue%s is meant for VF rxq[%02x]\n",
			   b_legacy_vf ? " [legacy]" : "",
			   p_cid->vf_qid);
		p_ramrod->vf_rx_prod_use_zone_a = b_legacy_vf;
	}

	return ecore_spq_post(p_hwfn, p_ent, OSAL_NULL);
}

static enum _ecore_status_t
ecore_eth_pf_rx_queue_start(struct ecore_hwfn *p_hwfn,
			    struct ecore_queue_cid *p_cid,
			    u16 bd_max_bytes,
			    dma_addr_t bd_chain_phys_addr,
			    dma_addr_t cqe_pbl_addr,
			    u16 cqe_pbl_size,
			    void OSAL_IOMEM **pp_prod)
{
	u32 init_prod_val = 0;

	*pp_prod = (u8 OSAL_IOMEM*)
		    p_hwfn->regview +
		    GTT_BAR0_MAP_REG_MSDM_RAM +
		    MSTORM_ETH_PF_PRODS_OFFSET(p_cid->abs.queue_id);

	/* Init the rcq, rx bd and rx sge (if valid) producers to 0 */
	__internal_ram_wr(p_hwfn, *pp_prod, sizeof(u32),
			  (u32 *)(&init_prod_val));

	return ecore_eth_rxq_start_ramrod(p_hwfn, p_cid,
					  bd_max_bytes,
					  bd_chain_phys_addr,
					  cqe_pbl_addr, cqe_pbl_size);
}

enum _ecore_status_t
ecore_eth_rx_queue_start(struct ecore_hwfn *p_hwfn,
			 u16 opaque_fid,
			 struct ecore_queue_start_common_params *p_params,
			 u16 bd_max_bytes,
			 dma_addr_t bd_chain_phys_addr,
			 dma_addr_t cqe_pbl_addr,
			 u16 cqe_pbl_size,
			 struct ecore_rxq_start_ret_params *p_ret_params)
{
	struct ecore_queue_cid *p_cid;
	enum _ecore_status_t rc;

	/* Allocate a CID for the queue */
	p_cid = ecore_eth_queue_to_cid_pf(p_hwfn, opaque_fid, true, p_params);
	if (p_cid == OSAL_NULL)
		return ECORE_NOMEM;

	if (IS_PF(p_hwfn->p_dev))
		rc = ecore_eth_pf_rx_queue_start(p_hwfn, p_cid,
						 bd_max_bytes,
						 bd_chain_phys_addr,
						 cqe_pbl_addr, cqe_pbl_size,
						 &p_ret_params->p_prod);
	else
		rc = ecore_vf_pf_rxq_start(p_hwfn, p_cid,
					   bd_max_bytes,
					   bd_chain_phys_addr,
					   cqe_pbl_addr,
					   cqe_pbl_size,
					   &p_ret_params->p_prod);

	/* Provide the caller with a reference to as handler */
	if (rc != ECORE_SUCCESS)
		ecore_eth_queue_cid_release(p_hwfn, p_cid);
	else
		p_ret_params->p_handle = (void *)p_cid;

	return rc;
}

enum _ecore_status_t ecore_sp_eth_rx_queues_update(struct ecore_hwfn *p_hwfn,
						   void **pp_rxq_handles,
						   u8 num_rxqs,
						   u8 complete_cqe_flg,
						   u8 complete_event_flg,
						   enum spq_mode comp_mode,
						   struct ecore_spq_comp_cb *p_comp_data)
{
	struct rx_queue_update_ramrod_data *p_ramrod = OSAL_NULL;
	struct ecore_spq_entry *p_ent = OSAL_NULL;
	struct ecore_sp_init_data init_data;
	struct ecore_queue_cid *p_cid;
	enum _ecore_status_t rc = ECORE_NOTIMPL;
	u8 i;

#ifndef LINUX_REMOVE
	if (IS_VF(p_hwfn->p_dev))
		return ecore_vf_pf_rxqs_update(p_hwfn,
					       (struct ecore_queue_cid **)
					       pp_rxq_handles,
					       num_rxqs,
					       complete_cqe_flg,
					       complete_event_flg);
#endif

	OSAL_MEMSET(&init_data, 0, sizeof(init_data));
	init_data.comp_mode = comp_mode;
	init_data.p_comp_data = p_comp_data;

	for (i = 0; i < num_rxqs; i++) {
		p_cid = ((struct ecore_queue_cid **)pp_rxq_handles)[i];

		/* Get SPQ entry */
		init_data.cid = p_cid->cid;
		init_data.opaque_fid = p_cid->opaque_fid;

		rc = ecore_sp_init_request(p_hwfn, &p_ent,
					   ETH_RAMROD_RX_QUEUE_UPDATE,
					   PROTOCOLID_ETH, &init_data);
		if (rc != ECORE_SUCCESS)
			return rc;

		p_ramrod = &p_ent->ramrod.rx_queue_update;
		p_ramrod->vport_id = p_cid->abs.vport_id;

		p_ramrod->rx_queue_id = OSAL_CPU_TO_LE16(p_cid->abs.queue_id);
		p_ramrod->complete_cqe_flg = complete_cqe_flg;
		p_ramrod->complete_event_flg = complete_event_flg;

		rc = ecore_spq_post(p_hwfn, p_ent, OSAL_NULL);
		if (rc != ECORE_SUCCESS)
			return rc;
	}

	return rc;
}

enum _ecore_status_t
ecore_sp_eth_rx_queues_set_default(struct ecore_hwfn *p_hwfn,
				   void *p_rxq_handler,
				   enum spq_mode comp_mode,
				   struct ecore_spq_comp_cb *p_comp_data)
{
	struct rx_queue_update_ramrod_data *p_ramrod = OSAL_NULL;
	struct ecore_spq_entry *p_ent = OSAL_NULL;
	struct ecore_sp_init_data init_data;
	struct ecore_queue_cid *p_cid;
	enum _ecore_status_t rc = ECORE_SUCCESS;

	if (IS_VF(p_hwfn->p_dev))
		return ECORE_NOTIMPL;

	OSAL_MEMSET(&init_data, 0, sizeof(init_data));
	init_data.comp_mode = comp_mode;
	init_data.p_comp_data = p_comp_data;

	p_cid = (struct ecore_queue_cid *)p_rxq_handler;

	/* Get SPQ entry */
	init_data.cid = p_cid->cid;
	init_data.opaque_fid = p_cid->opaque_fid;

	rc = ecore_sp_init_request(p_hwfn, &p_ent,
				   ETH_RAMROD_RX_QUEUE_UPDATE,
				   PROTOCOLID_ETH, &init_data);
	if (rc != ECORE_SUCCESS)
		return rc;

	p_ramrod = &p_ent->ramrod.rx_queue_update;
	p_ramrod->vport_id = p_cid->abs.vport_id;

	p_ramrod->rx_queue_id = OSAL_CPU_TO_LE16(p_cid->abs.queue_id);
	p_ramrod->complete_cqe_flg = 0;
	p_ramrod->complete_event_flg = 1;
	p_ramrod->set_default_rss_queue = 1;

	rc = ecore_spq_post(p_hwfn, p_ent, OSAL_NULL);

	return rc;
}

static enum _ecore_status_t
ecore_eth_pf_rx_queue_stop(struct ecore_hwfn *p_hwfn,
			   struct ecore_queue_cid *p_cid,
			   bool b_eq_completion_only,
			   bool b_cqe_completion)
{
	struct rx_queue_stop_ramrod_data *p_ramrod = OSAL_NULL;
	struct ecore_spq_entry *p_ent = OSAL_NULL;
	struct ecore_sp_init_data init_data;
	enum _ecore_status_t rc;

	OSAL_MEMSET(&init_data, 0, sizeof(init_data));
	init_data.cid = p_cid->cid;
	init_data.opaque_fid = p_cid->opaque_fid;
	init_data.comp_mode = ECORE_SPQ_MODE_EBLOCK;

	rc = ecore_sp_init_request(p_hwfn, &p_ent,
				   ETH_RAMROD_RX_QUEUE_STOP,
				   PROTOCOLID_ETH, &init_data);
	if (rc != ECORE_SUCCESS)
		return rc;

	p_ramrod = &p_ent->ramrod.rx_queue_stop;
	p_ramrod->vport_id = p_cid->abs.vport_id;
	p_ramrod->rx_queue_id = OSAL_CPU_TO_LE16(p_cid->abs.queue_id);

	/* Cleaning the queue requires the completion to arrive there.
	 * In addition, VFs require the answer to come as eqe to PF.
	 */
	p_ramrod->complete_cqe_flg = ((p_cid->vfid == ECORE_QUEUE_CID_PF) &&
				      !b_eq_completion_only) ||
				     b_cqe_completion;
	p_ramrod->complete_event_flg = (p_cid->vfid != ECORE_QUEUE_CID_PF) ||
				       b_eq_completion_only;

	return ecore_spq_post(p_hwfn, p_ent, OSAL_NULL);
}

enum _ecore_status_t ecore_eth_rx_queue_stop(struct ecore_hwfn *p_hwfn,
					     void *p_rxq,
					     bool eq_completion_only,
					     bool cqe_completion)
{
	struct ecore_queue_cid *p_cid = (struct ecore_queue_cid *)p_rxq;
	enum _ecore_status_t rc = ECORE_NOTIMPL;

	if (IS_PF(p_hwfn->p_dev))
		rc = ecore_eth_pf_rx_queue_stop(p_hwfn, p_cid,
						eq_completion_only,
						cqe_completion);
	else
		rc = ecore_vf_pf_rxq_stop(p_hwfn, p_cid, cqe_completion);

	if (rc == ECORE_SUCCESS)
		ecore_eth_queue_cid_release(p_hwfn, p_cid);
	return rc;
}

enum _ecore_status_t
ecore_eth_txq_start_ramrod(struct ecore_hwfn *p_hwfn,
			   struct ecore_queue_cid *p_cid,
			   dma_addr_t pbl_addr, u16 pbl_size,
			   u16 pq_id)
{
	struct tx_queue_start_ramrod_data *p_ramrod = OSAL_NULL;
	struct ecore_spq_entry *p_ent = OSAL_NULL;
	struct ecore_sp_init_data init_data;
	enum _ecore_status_t rc = ECORE_NOTIMPL;

	/* Get SPQ entry */
	OSAL_MEMSET(&init_data, 0, sizeof(init_data));
	init_data.cid = p_cid->cid;
	init_data.opaque_fid = p_cid->opaque_fid;
	init_data.comp_mode = ECORE_SPQ_MODE_EBLOCK;

	rc = ecore_sp_init_request(p_hwfn, &p_ent,
				   ETH_RAMROD_TX_QUEUE_START,
				   PROTOCOLID_ETH, &init_data);
	if (rc != ECORE_SUCCESS)
		return rc;

	p_ramrod = &p_ent->ramrod.tx_queue_start;
	p_ramrod->vport_id = p_cid->abs.vport_id;

	p_ramrod->sb_id = OSAL_CPU_TO_LE16(p_cid->sb_igu_id);
	p_ramrod->sb_index = p_cid->sb_idx;
	p_ramrod->stats_counter_id = p_cid->abs.stats_id;

	p_ramrod->queue_zone_id = OSAL_CPU_TO_LE16(p_cid->abs.queue_id);
	p_ramrod->same_as_last_id = OSAL_CPU_TO_LE16(p_cid->abs.queue_id);

	p_ramrod->pbl_size = OSAL_CPU_TO_LE16(pbl_size);
	DMA_REGPAIR_LE(p_ramrod->pbl_base_addr, pbl_addr);

	p_ramrod->qm_pq_id = OSAL_CPU_TO_LE16(pq_id);

	return ecore_spq_post(p_hwfn, p_ent, OSAL_NULL);
}

static enum _ecore_status_t
ecore_eth_pf_tx_queue_start(struct ecore_hwfn *p_hwfn,
			    struct ecore_queue_cid *p_cid,
			    u8 tc,
			    dma_addr_t pbl_addr, u16 pbl_size,
			    void OSAL_IOMEM **pp_doorbell)
{
	enum _ecore_status_t rc;

	/* TODO - set tc in the pq_params for multi-cos */
	rc = ecore_eth_txq_start_ramrod(p_hwfn, p_cid,
					pbl_addr, pbl_size,
					ecore_get_cm_pq_idx_mcos(p_hwfn, tc));
	if (rc != ECORE_SUCCESS)
		return rc;

	/* Provide the caller with the necessary return values */
	*pp_doorbell = (u8 OSAL_IOMEM *)
		       p_hwfn->doorbells +
		       DB_ADDR(p_cid->cid, DQ_DEMS_LEGACY);

	return ECORE_SUCCESS;
}

enum _ecore_status_t
ecore_eth_tx_queue_start(struct ecore_hwfn *p_hwfn, u16 opaque_fid,
			 struct ecore_queue_start_common_params *p_params,
			 u8 tc,
			 dma_addr_t pbl_addr, u16 pbl_size,
			 struct ecore_txq_start_ret_params *p_ret_params)
{
	struct ecore_queue_cid *p_cid;
	enum _ecore_status_t rc;

	p_cid = ecore_eth_queue_to_cid_pf(p_hwfn, opaque_fid, false, p_params);
	if (p_cid == OSAL_NULL)
		return ECORE_INVAL;

	if (IS_PF(p_hwfn->p_dev))
		rc = ecore_eth_pf_tx_queue_start(p_hwfn, p_cid, tc,
						 pbl_addr, pbl_size,
						 &p_ret_params->p_doorbell);
	else
		rc = ecore_vf_pf_txq_start(p_hwfn, p_cid,
					   pbl_addr, pbl_size,
					   &p_ret_params->p_doorbell);

	if (rc != ECORE_SUCCESS)
		ecore_eth_queue_cid_release(p_hwfn, p_cid);
	else
		p_ret_params->p_handle = (void *)p_cid;

	return rc;
}

static enum _ecore_status_t
ecore_eth_pf_tx_queue_stop(struct ecore_hwfn *p_hwfn,
			   struct ecore_queue_cid *p_cid)
{
	struct ecore_spq_entry *p_ent = OSAL_NULL;
	struct ecore_sp_init_data init_data;
	enum _ecore_status_t rc;

	OSAL_MEMSET(&init_data, 0, sizeof(init_data));
	init_data.cid = p_cid->cid;
	init_data.opaque_fid = p_cid->opaque_fid;
	init_data.comp_mode = ECORE_SPQ_MODE_EBLOCK;

	rc = ecore_sp_init_request(p_hwfn, &p_ent,
				   ETH_RAMROD_TX_QUEUE_STOP,
				   PROTOCOLID_ETH, &init_data);
	if (rc != ECORE_SUCCESS)
		return rc;

	return ecore_spq_post(p_hwfn, p_ent, OSAL_NULL);
}

enum _ecore_status_t ecore_eth_tx_queue_stop(struct ecore_hwfn *p_hwfn,
					     void *p_handle)
{
	struct ecore_queue_cid *p_cid = (struct ecore_queue_cid *)p_handle;
	enum _ecore_status_t rc;

	if (IS_PF(p_hwfn->p_dev))
		rc = ecore_eth_pf_tx_queue_stop(p_hwfn, p_cid);
	else
		rc = ecore_vf_pf_txq_stop(p_hwfn, p_cid);

	if (rc == ECORE_SUCCESS)
		ecore_eth_queue_cid_release(p_hwfn, p_cid);
	return rc;
}

static enum eth_filter_action ecore_filter_action(enum ecore_filter_opcode opcode)
{
	enum eth_filter_action action = MAX_ETH_FILTER_ACTION;

	switch (opcode) {
	case ECORE_FILTER_ADD:
		action = ETH_FILTER_ACTION_ADD;
		break;
	case ECORE_FILTER_REMOVE:
		action = ETH_FILTER_ACTION_REMOVE;
		break;
	case ECORE_FILTER_FLUSH:
		action = ETH_FILTER_ACTION_REMOVE_ALL;
		break;
	default:
		action = MAX_ETH_FILTER_ACTION;
	}

	return action;
}

static enum _ecore_status_t
ecore_filter_ucast_common(struct ecore_hwfn *p_hwfn,
			  u16 opaque_fid,
			  struct ecore_filter_ucast *p_filter_cmd,
			  struct vport_filter_update_ramrod_data **pp_ramrod,
			  struct ecore_spq_entry **pp_ent,
			  enum spq_mode comp_mode,
			  struct ecore_spq_comp_cb *p_comp_data)
{
	u8 vport_to_add_to = 0, vport_to_remove_from = 0;
	struct vport_filter_update_ramrod_data *p_ramrod;
	struct eth_filter_cmd *p_first_filter;
	struct eth_filter_cmd *p_second_filter;
	struct ecore_sp_init_data init_data;
	enum eth_filter_action action;
	enum _ecore_status_t rc;

	rc = ecore_fw_vport(p_hwfn, p_filter_cmd->vport_to_remove_from,
			    &vport_to_remove_from);
	if (rc != ECORE_SUCCESS)
		return rc;

	rc = ecore_fw_vport(p_hwfn, p_filter_cmd->vport_to_add_to,
			    &vport_to_add_to);
	if (rc != ECORE_SUCCESS)
		return rc;

	/* Get SPQ entry */
	OSAL_MEMSET(&init_data, 0, sizeof(init_data));
	init_data.cid = ecore_spq_get_cid(p_hwfn);
	init_data.opaque_fid = opaque_fid;
	init_data.comp_mode = comp_mode;
	init_data.p_comp_data = p_comp_data;

	rc = ecore_sp_init_request(p_hwfn, pp_ent,
				   ETH_RAMROD_FILTERS_UPDATE,
				   PROTOCOLID_ETH, &init_data);
	if (rc != ECORE_SUCCESS)
		return rc;

	*pp_ramrod = &(*pp_ent)->ramrod.vport_filter_update;
	p_ramrod = *pp_ramrod;
	p_ramrod->filter_cmd_hdr.rx = p_filter_cmd->is_rx_filter ? 1 : 0;
	p_ramrod->filter_cmd_hdr.tx = p_filter_cmd->is_tx_filter ? 1 : 0;

#ifndef ASIC_ONLY
	if (CHIP_REV_IS_SLOW(p_hwfn->p_dev)) {
		DP_VERBOSE(p_hwfn, ECORE_MSG_SP,
			   "Non-Asic - prevent Tx filters\n");
		p_ramrod->filter_cmd_hdr.tx = 0;
	}

#endif

	switch (p_filter_cmd->opcode) {
	case ECORE_FILTER_REPLACE:
	case ECORE_FILTER_MOVE:
		p_ramrod->filter_cmd_hdr.cmd_cnt = 2; break;
	default:
		p_ramrod->filter_cmd_hdr.cmd_cnt = 1; break;
	}

	p_first_filter = &p_ramrod->filter_cmds[0];
	p_second_filter = &p_ramrod->filter_cmds[1];

	switch (p_filter_cmd->type) {
	case ECORE_FILTER_MAC:
		p_first_filter->type = ETH_FILTER_TYPE_MAC; break;
	case ECORE_FILTER_VLAN:
		p_first_filter->type = ETH_FILTER_TYPE_VLAN; break;
	case ECORE_FILTER_MAC_VLAN:
		p_first_filter->type = ETH_FILTER_TYPE_PAIR; break;
	case ECORE_FILTER_INNER_MAC:
		p_first_filter->type = ETH_FILTER_TYPE_INNER_MAC; break;
	case ECORE_FILTER_INNER_VLAN:
		p_first_filter->type = ETH_FILTER_TYPE_INNER_VLAN; break;
	case ECORE_FILTER_INNER_PAIR:
		p_first_filter->type = ETH_FILTER_TYPE_INNER_PAIR; break;
	case ECORE_FILTER_INNER_MAC_VNI_PAIR:
		p_first_filter->type = ETH_FILTER_TYPE_INNER_MAC_VNI_PAIR;
		break;
	case ECORE_FILTER_MAC_VNI_PAIR:
		p_first_filter->type = ETH_FILTER_TYPE_MAC_VNI_PAIR; break;
	case ECORE_FILTER_VNI:
		p_first_filter->type = ETH_FILTER_TYPE_VNI; break;
	}

	if ((p_first_filter->type == ETH_FILTER_TYPE_MAC) ||
	    (p_first_filter->type == ETH_FILTER_TYPE_PAIR) ||
	    (p_first_filter->type == ETH_FILTER_TYPE_INNER_MAC) ||
	    (p_first_filter->type == ETH_FILTER_TYPE_INNER_PAIR) ||
	    (p_first_filter->type == ETH_FILTER_TYPE_INNER_MAC_VNI_PAIR) ||
	    (p_first_filter->type == ETH_FILTER_TYPE_MAC_VNI_PAIR))
		ecore_set_fw_mac_addr(&p_first_filter->mac_msb,
				      &p_first_filter->mac_mid,
				      &p_first_filter->mac_lsb,
				      (u8 *)p_filter_cmd->mac);

	if ((p_first_filter->type == ETH_FILTER_TYPE_VLAN) ||
	    (p_first_filter->type == ETH_FILTER_TYPE_PAIR) ||
	    (p_first_filter->type == ETH_FILTER_TYPE_INNER_VLAN) ||
	    (p_first_filter->type == ETH_FILTER_TYPE_INNER_PAIR))
		p_first_filter->vlan_id = OSAL_CPU_TO_LE16(p_filter_cmd->vlan);

	if ((p_first_filter->type == ETH_FILTER_TYPE_INNER_MAC_VNI_PAIR) ||
	    (p_first_filter->type == ETH_FILTER_TYPE_MAC_VNI_PAIR) ||
	    (p_first_filter->type == ETH_FILTER_TYPE_VNI))
		p_first_filter->vni = OSAL_CPU_TO_LE32(p_filter_cmd->vni);

	if (p_filter_cmd->opcode == ECORE_FILTER_MOVE) {
		p_second_filter->type = p_first_filter->type;
		p_second_filter->mac_msb = p_first_filter->mac_msb;
		p_second_filter->mac_mid = p_first_filter->mac_mid;
		p_second_filter->mac_lsb = p_first_filter->mac_lsb;
		p_second_filter->vlan_id = p_first_filter->vlan_id;
		p_second_filter->vni = p_first_filter->vni;

		p_first_filter->action = ETH_FILTER_ACTION_REMOVE;

		p_first_filter->vport_id = vport_to_remove_from;

		p_second_filter->action = ETH_FILTER_ACTION_ADD;
		p_second_filter->vport_id = vport_to_add_to;
	} else if (p_filter_cmd->opcode == ECORE_FILTER_REPLACE) {
		p_first_filter->vport_id = vport_to_add_to;
		OSAL_MEMCPY(p_second_filter, p_first_filter,
			    sizeof(*p_second_filter));
		p_first_filter->action = ETH_FILTER_ACTION_REMOVE_ALL;
		p_second_filter->action = ETH_FILTER_ACTION_ADD;
	} else {
		action = ecore_filter_action(p_filter_cmd->opcode);

		if (action == MAX_ETH_FILTER_ACTION) {
			DP_NOTICE(p_hwfn, true,
				  "%d is not supported yet\n",
				  p_filter_cmd->opcode);
			return ECORE_NOTIMPL;
		}

		p_first_filter->action = action;
		p_first_filter->vport_id =
			(p_filter_cmd->opcode == ECORE_FILTER_REMOVE) ?
			vport_to_remove_from : vport_to_add_to;
	}

	return ECORE_SUCCESS;
}

enum _ecore_status_t ecore_sp_eth_filter_ucast(struct ecore_hwfn *p_hwfn,
					       u16 opaque_fid,
					       struct ecore_filter_ucast *p_filter_cmd,
					       enum spq_mode comp_mode,
					       struct ecore_spq_comp_cb *p_comp_data)
{
	struct vport_filter_update_ramrod_data *p_ramrod = OSAL_NULL;
	struct ecore_spq_entry *p_ent = OSAL_NULL;
	struct eth_filter_cmd_header *p_header;
	enum _ecore_status_t rc;

	rc = ecore_filter_ucast_common(p_hwfn, opaque_fid, p_filter_cmd,
				       &p_ramrod, &p_ent,
				       comp_mode, p_comp_data);
	if (rc != ECORE_SUCCESS) {
		DP_ERR(p_hwfn, "Uni. filter command failed %d\n", rc);
		return rc;
	}
	p_header = &p_ramrod->filter_cmd_hdr;
	p_header->assert_on_error = p_filter_cmd->assert_on_error;

	rc = ecore_spq_post(p_hwfn, p_ent, OSAL_NULL);
	if (rc != ECORE_SUCCESS) {
		DP_ERR(p_hwfn,
		       "Unicast filter ADD command failed %d\n",
		       rc);
		return rc;
	}

	DP_VERBOSE(p_hwfn, ECORE_MSG_SP,
		   "Unicast filter configured, opcode = %s, type = %s, cmd_cnt = %d, is_rx_filter = %d, is_tx_filter = %d\n",
		   (p_filter_cmd->opcode == ECORE_FILTER_ADD) ? "ADD" :
		    ((p_filter_cmd->opcode == ECORE_FILTER_REMOVE) ?
		     "REMOVE" :
		     ((p_filter_cmd->opcode == ECORE_FILTER_MOVE) ?
		      "MOVE" : "REPLACE")),
		   (p_filter_cmd->type == ECORE_FILTER_MAC) ? "MAC" :
		    ((p_filter_cmd->type == ECORE_FILTER_VLAN) ?
		     "VLAN" : "MAC & VLAN"),
		   p_ramrod->filter_cmd_hdr.cmd_cnt,
		   p_filter_cmd->is_rx_filter,
		   p_filter_cmd->is_tx_filter);
	DP_VERBOSE(p_hwfn, ECORE_MSG_SP,
		   "vport_to_add_to = %d, vport_to_remove_from = %d, mac = %2x:%2x:%2x:%2x:%2x:%2x, vlan = %d\n",
		   p_filter_cmd->vport_to_add_to,
		   p_filter_cmd->vport_to_remove_from,
		   p_filter_cmd->mac[0], p_filter_cmd->mac[1],
		   p_filter_cmd->mac[2], p_filter_cmd->mac[3],
		   p_filter_cmd->mac[4], p_filter_cmd->mac[5],
		   p_filter_cmd->vlan);

	return ECORE_SUCCESS;
}

/*******************************************************************************
 * Description:
 *         Calculates crc 32 on a buffer
 *         Note: crc32_length MUST be aligned to 8
 * Return:
 ******************************************************************************/
static u32 ecore_calc_crc32c(u8 *crc32_packet, u32 crc32_length, u32 crc32_seed)
{
	u32 byte = 0, bit = 0, crc32_result = crc32_seed;
	u8  msb = 0, current_byte = 0;

	if ((crc32_packet == OSAL_NULL) ||
	    (crc32_length == 0) ||
	    ((crc32_length % 8) != 0)) {
		return crc32_result;
	}

	for (byte = 0; byte < crc32_length; byte++) {
		current_byte = crc32_packet[byte];
		for (bit = 0; bit < 8; bit++) {
			msb = (u8)(crc32_result >> 31);
			crc32_result = crc32_result << 1;
			if (msb != (0x1 & (current_byte >> bit))) {
				crc32_result = crc32_result ^ CRC32_POLY;
				crc32_result |= 1; /*crc32_result[0] = 1;*/
			}
		}
	}

	return crc32_result;
}

static u32 ecore_crc32c_le(u32 seed, u8 *mac)
{
	u32 packet_buf[2] = {0};

	OSAL_MEMCPY((u8 *)(&packet_buf[0]), &mac[0], 6);
	return ecore_calc_crc32c((u8 *)packet_buf, 8, seed);
}

u8 ecore_mcast_bin_from_mac(u8 *mac)
{
	u32 crc = ecore_crc32c_le(ETH_MULTICAST_BIN_FROM_MAC_SEED, mac);

	return crc & 0xff;
}

static enum _ecore_status_t
ecore_sp_eth_filter_mcast(struct ecore_hwfn *p_hwfn,
			  struct ecore_filter_mcast *p_filter_cmd,
			  enum spq_mode comp_mode,
			  struct ecore_spq_comp_cb *p_comp_data)
{
	struct vport_update_ramrod_data *p_ramrod = OSAL_NULL;
	u32 bins[ETH_MULTICAST_MAC_BINS_IN_REGS];
	struct ecore_spq_entry *p_ent = OSAL_NULL;
	struct ecore_sp_init_data init_data;
	u8 abs_vport_id = 0;
	enum _ecore_status_t rc;
	int i;

	if (p_filter_cmd->opcode == ECORE_FILTER_ADD)
		rc = ecore_fw_vport(p_hwfn, p_filter_cmd->vport_to_add_to,
				    &abs_vport_id);
	else
		rc = ecore_fw_vport(p_hwfn, p_filter_cmd->vport_to_remove_from,
				    &abs_vport_id);
	if (rc != ECORE_SUCCESS)
		return rc;

	/* Get SPQ entry */
	OSAL_MEMSET(&init_data, 0, sizeof(init_data));
	init_data.cid = ecore_spq_get_cid(p_hwfn);
	init_data.opaque_fid = p_hwfn->hw_info.opaque_fid;
	init_data.comp_mode = comp_mode;
	init_data.p_comp_data = p_comp_data;

	rc = ecore_sp_init_request(p_hwfn, &p_ent,
				   ETH_RAMROD_VPORT_UPDATE,
				   PROTOCOLID_ETH, &init_data);
	if (rc != ECORE_SUCCESS) {
		DP_ERR(p_hwfn, "Multi-cast command failed %d\n", rc);
		return rc;
	}

	p_ramrod = &p_ent->ramrod.vport_update;
	p_ramrod->common.update_approx_mcast_flg = 1;

	/* explicitly clear out the entire vector */
	OSAL_MEMSET(&p_ramrod->approx_mcast.bins,
		    0, sizeof(p_ramrod->approx_mcast.bins));
	OSAL_MEMSET(bins, 0, sizeof(u32) * ETH_MULTICAST_MAC_BINS_IN_REGS);
	/* filter ADD op is explicit set op and it removes
	*  any existing filters for the vport.
	*/
	if (p_filter_cmd->opcode == ECORE_FILTER_ADD) {
		for (i = 0; i < p_filter_cmd->num_mc_addrs; i++) {
			u32 bit;

			bit = ecore_mcast_bin_from_mac(p_filter_cmd->mac[i]);
			bins[bit / 32] |= 1 << (bit % 32);
		}

		/* Convert to correct endianity */
		for (i = 0; i < ETH_MULTICAST_MAC_BINS_IN_REGS; i++) {
			struct vport_update_ramrod_mcast *p_ramrod_bins;

			p_ramrod_bins = &p_ramrod->approx_mcast;
			p_ramrod_bins->bins[i] = OSAL_CPU_TO_LE32(bins[i]);
		}
	}

	p_ramrod->common.vport_id = abs_vport_id;

	rc = ecore_spq_post(p_hwfn, p_ent, OSAL_NULL);
	if (rc != ECORE_SUCCESS)
		DP_ERR(p_hwfn, "Multicast filter command failed %d\n", rc);

	return rc;
}

enum _ecore_status_t ecore_filter_mcast_cmd(struct ecore_dev *p_dev,
					    struct ecore_filter_mcast *p_filter_cmd,
					    enum spq_mode comp_mode,
					    struct ecore_spq_comp_cb *p_comp_data)
{
	enum _ecore_status_t rc = ECORE_SUCCESS;
	int i;

	/* only ADD and REMOVE operations are supported for multi-cast */
	if ((p_filter_cmd->opcode != ECORE_FILTER_ADD  &&
	     (p_filter_cmd->opcode != ECORE_FILTER_REMOVE)) ||
	     (p_filter_cmd->num_mc_addrs > ECORE_MAX_MC_ADDRS)) {
		return ECORE_INVAL;
	}

	for_each_hwfn(p_dev, i) {
		struct ecore_hwfn *p_hwfn = &p_dev->hwfns[i];

		if (IS_VF(p_dev)) {
			ecore_vf_pf_filter_mcast(p_hwfn, p_filter_cmd);
			continue;
		}

		rc = ecore_sp_eth_filter_mcast(p_hwfn,
					       p_filter_cmd,
					       comp_mode,
					       p_comp_data);
		if (rc != ECORE_SUCCESS)
			break;
	}

	return rc;
}

enum _ecore_status_t ecore_filter_ucast_cmd(struct ecore_dev *p_dev,
					    struct ecore_filter_ucast *p_filter_cmd,
					    enum spq_mode comp_mode,
					    struct ecore_spq_comp_cb *p_comp_data)
{
	enum _ecore_status_t rc = ECORE_SUCCESS;
	int i;

	for_each_hwfn(p_dev, i) {
		struct ecore_hwfn *p_hwfn = &p_dev->hwfns[i];
		u16 opaque_fid;

		if (IS_VF(p_dev)) {
			rc = ecore_vf_pf_filter_ucast(p_hwfn, p_filter_cmd);
			continue;
		}

		opaque_fid = p_hwfn->hw_info.opaque_fid;
		rc = ecore_sp_eth_filter_ucast(p_hwfn,
					       opaque_fid,
					       p_filter_cmd,
					       comp_mode,
					       p_comp_data);
		if (rc != ECORE_SUCCESS)
			break;
	}

	return rc;
}

/* Statistics related code */
static void __ecore_get_vport_pstats_addrlen(struct ecore_hwfn *p_hwfn,
					     u32 *p_addr, u32 *p_len,
					     u16 statistics_bin)
{
	if (IS_PF(p_hwfn->p_dev)) {
		*p_addr = BAR0_MAP_REG_PSDM_RAM +
			  PSTORM_QUEUE_STAT_OFFSET(statistics_bin);
		*p_len = sizeof(struct eth_pstorm_per_queue_stat);
	} else {
		struct ecore_vf_iov *p_iov = p_hwfn->vf_iov_info;
		struct pfvf_acquire_resp_tlv *p_resp = &p_iov->acquire_resp;

		*p_addr = p_resp->pfdev_info.stats_info.pstats.address;
		*p_len = p_resp->pfdev_info.stats_info.pstats.len;
	}
}

static void __ecore_get_vport_pstats(struct ecore_hwfn *p_hwfn,
				     struct ecore_ptt *p_ptt,
				     struct ecore_eth_stats *p_stats,
				     u16 statistics_bin)
{
	struct eth_pstorm_per_queue_stat pstats;
	u32 pstats_addr = 0, pstats_len = 0;

	__ecore_get_vport_pstats_addrlen(p_hwfn, &pstats_addr, &pstats_len,
					 statistics_bin);

	OSAL_MEMSET(&pstats, 0, sizeof(pstats));
	ecore_memcpy_from(p_hwfn, p_ptt, &pstats,
			  pstats_addr, pstats_len);

	p_stats->common.tx_ucast_bytes +=
		HILO_64_REGPAIR(pstats.sent_ucast_bytes);
	p_stats->common.tx_mcast_bytes +=
		HILO_64_REGPAIR(pstats.sent_mcast_bytes);
	p_stats->common.tx_bcast_bytes +=
		HILO_64_REGPAIR(pstats.sent_bcast_bytes);
	p_stats->common.tx_ucast_pkts +=
		HILO_64_REGPAIR(pstats.sent_ucast_pkts);
	p_stats->common.tx_mcast_pkts +=
		HILO_64_REGPAIR(pstats.sent_mcast_pkts);
	p_stats->common.tx_bcast_pkts +=
		HILO_64_REGPAIR(pstats.sent_bcast_pkts);
	p_stats->common.tx_err_drop_pkts +=
		HILO_64_REGPAIR(pstats.error_drop_pkts);
}

static void __ecore_get_vport_tstats(struct ecore_hwfn *p_hwfn,
				     struct ecore_ptt *p_ptt,
				     struct ecore_eth_stats *p_stats)
{
	struct tstorm_per_port_stat tstats;
	u32 tstats_addr, tstats_len;

	if (IS_PF(p_hwfn->p_dev)) {
		tstats_addr = BAR0_MAP_REG_TSDM_RAM +
			      TSTORM_PORT_STAT_OFFSET(MFW_PORT(p_hwfn));
		tstats_len = sizeof(struct tstorm_per_port_stat);
	} else {
		struct ecore_vf_iov *p_iov = p_hwfn->vf_iov_info;
		struct pfvf_acquire_resp_tlv *p_resp = &p_iov->acquire_resp;

		tstats_addr = p_resp->pfdev_info.stats_info.tstats.address;
		tstats_len = p_resp->pfdev_info.stats_info.tstats.len;
	}

	OSAL_MEMSET(&tstats, 0, sizeof(tstats));
	ecore_memcpy_from(p_hwfn, p_ptt, &tstats,
			  tstats_addr, tstats_len);

	p_stats->common.mftag_filter_discards +=
		HILO_64_REGPAIR(tstats.mftag_filter_discard);
	p_stats->common.mac_filter_discards +=
		HILO_64_REGPAIR(tstats.eth_mac_filter_discard);
}

static void __ecore_get_vport_ustats_addrlen(struct ecore_hwfn *p_hwfn,
					     u32 *p_addr, u32 *p_len,
					     u16 statistics_bin)
{
	if (IS_PF(p_hwfn->p_dev)) {
		*p_addr = BAR0_MAP_REG_USDM_RAM +
			  USTORM_QUEUE_STAT_OFFSET(statistics_bin);
		*p_len = sizeof(struct eth_ustorm_per_queue_stat);
	} else {
		struct ecore_vf_iov *p_iov = p_hwfn->vf_iov_info;
		struct pfvf_acquire_resp_tlv *p_resp = &p_iov->acquire_resp;

		*p_addr = p_resp->pfdev_info.stats_info.ustats.address;
		*p_len = p_resp->pfdev_info.stats_info.ustats.len;
	}
}

static void __ecore_get_vport_ustats(struct ecore_hwfn *p_hwfn,
				     struct ecore_ptt *p_ptt,
				     struct ecore_eth_stats *p_stats,
				     u16 statistics_bin)
{
	struct eth_ustorm_per_queue_stat ustats;
	u32 ustats_addr = 0, ustats_len = 0;

	__ecore_get_vport_ustats_addrlen(p_hwfn, &ustats_addr, &ustats_len,
					 statistics_bin);

	OSAL_MEMSET(&ustats, 0, sizeof(ustats));
	ecore_memcpy_from(p_hwfn, p_ptt, &ustats,
			  ustats_addr, ustats_len);

	p_stats->common.rx_ucast_bytes +=
		HILO_64_REGPAIR(ustats.rcv_ucast_bytes);
	p_stats->common.rx_mcast_bytes +=
		HILO_64_REGPAIR(ustats.rcv_mcast_bytes);
	p_stats->common.rx_bcast_bytes +=
		HILO_64_REGPAIR(ustats.rcv_bcast_bytes);
	p_stats->common.rx_ucast_pkts +=
		HILO_64_REGPAIR(ustats.rcv_ucast_pkts);
	p_stats->common.rx_mcast_pkts +=
		HILO_64_REGPAIR(ustats.rcv_mcast_pkts);
	p_stats->common.rx_bcast_pkts +=
		HILO_64_REGPAIR(ustats.rcv_bcast_pkts);
}

static void __ecore_get_vport_mstats_addrlen(struct ecore_hwfn *p_hwfn,
					     u32 *p_addr, u32 *p_len,
					     u16 statistics_bin)
{
	if (IS_PF(p_hwfn->p_dev)) {
		*p_addr = BAR0_MAP_REG_MSDM_RAM +
			  MSTORM_QUEUE_STAT_OFFSET(statistics_bin);
		*p_len = sizeof(struct eth_mstorm_per_queue_stat);
	} else {
		struct ecore_vf_iov *p_iov = p_hwfn->vf_iov_info;
		struct pfvf_acquire_resp_tlv *p_resp = &p_iov->acquire_resp;

		*p_addr = p_resp->pfdev_info.stats_info.mstats.address;
		*p_len = p_resp->pfdev_info.stats_info.mstats.len;
	}
}

static void __ecore_get_vport_mstats(struct ecore_hwfn *p_hwfn,
				     struct ecore_ptt *p_ptt,
				     struct ecore_eth_stats *p_stats,
				     u16 statistics_bin)
{
	struct eth_mstorm_per_queue_stat mstats;
	u32 mstats_addr = 0, mstats_len = 0;

	__ecore_get_vport_mstats_addrlen(p_hwfn, &mstats_addr, &mstats_len,
					 statistics_bin);

	OSAL_MEMSET(&mstats, 0, sizeof(mstats));
	ecore_memcpy_from(p_hwfn, p_ptt, &mstats,
			  mstats_addr, mstats_len);

	p_stats->common.no_buff_discards +=
		HILO_64_REGPAIR(mstats.no_buff_discard);
	p_stats->common.packet_too_big_discard +=
		HILO_64_REGPAIR(mstats.packet_too_big_discard);
	p_stats->common.ttl0_discard +=
		HILO_64_REGPAIR(mstats.ttl0_discard);
	p_stats->common.tpa_coalesced_pkts +=
		HILO_64_REGPAIR(mstats.tpa_coalesced_pkts);
	p_stats->common.tpa_coalesced_events +=
		HILO_64_REGPAIR(mstats.tpa_coalesced_events);
	p_stats->common.tpa_aborts_num +=
		HILO_64_REGPAIR(mstats.tpa_aborts_num);
	p_stats->common.tpa_coalesced_bytes +=
		HILO_64_REGPAIR(mstats.tpa_coalesced_bytes);
}

static void __ecore_get_vport_port_stats(struct ecore_hwfn *p_hwfn,
					 struct ecore_ptt *p_ptt,
					 struct ecore_eth_stats *p_stats)
{
	struct ecore_eth_stats_common *p_common = &p_stats->common;
	struct port_stats port_stats;
	int j;

	OSAL_MEMSET(&port_stats, 0, sizeof(port_stats));

	ecore_memcpy_from(p_hwfn, p_ptt, &port_stats,
			  p_hwfn->mcp_info->port_addr +
			  OFFSETOF(struct public_port, stats),
			  sizeof(port_stats));

	p_common->rx_64_byte_packets += port_stats.eth.r64;
	p_common->rx_65_to_127_byte_packets += port_stats.eth.r127;
	p_common->rx_128_to_255_byte_packets += port_stats.eth.r255;
	p_common->rx_256_to_511_byte_packets += port_stats.eth.r511;
	p_common->rx_512_to_1023_byte_packets += port_stats.eth.r1023;
	p_common->rx_1024_to_1518_byte_packets += port_stats.eth.r1518;
	p_common->rx_crc_errors += port_stats.eth.rfcs;
	p_common->rx_mac_crtl_frames += port_stats.eth.rxcf;
	p_common->rx_pause_frames += port_stats.eth.rxpf;
	p_common->rx_pfc_frames += port_stats.eth.rxpp;
	p_common->rx_align_errors += port_stats.eth.raln;
	p_common->rx_carrier_errors += port_stats.eth.rfcr;
	p_common->rx_oversize_packets += port_stats.eth.rovr;
	p_common->rx_jabbers += port_stats.eth.rjbr;
	p_common->rx_undersize_packets += port_stats.eth.rund;
	p_common->rx_fragments += port_stats.eth.rfrg;
	p_common->tx_64_byte_packets += port_stats.eth.t64;
	p_common->tx_65_to_127_byte_packets += port_stats.eth.t127;
	p_common->tx_128_to_255_byte_packets += port_stats.eth.t255;
	p_common->tx_256_to_511_byte_packets += port_stats.eth.t511;
	p_common->tx_512_to_1023_byte_packets += port_stats.eth.t1023;
	p_common->tx_1024_to_1518_byte_packets += port_stats.eth.t1518;
	p_common->tx_pause_frames += port_stats.eth.txpf;
	p_common->tx_pfc_frames += port_stats.eth.txpp;
	p_common->rx_mac_bytes += port_stats.eth.rbyte;
	p_common->rx_mac_uc_packets += port_stats.eth.rxuca;
	p_common->rx_mac_mc_packets += port_stats.eth.rxmca;
	p_common->rx_mac_bc_packets += port_stats.eth.rxbca;
	p_common->rx_mac_frames_ok += port_stats.eth.rxpok;
	p_common->tx_mac_bytes += port_stats.eth.tbyte;
	p_common->tx_mac_uc_packets += port_stats.eth.txuca;
	p_common->tx_mac_mc_packets += port_stats.eth.txmca;
	p_common->tx_mac_bc_packets += port_stats.eth.txbca;
	p_common->tx_mac_ctrl_frames += port_stats.eth.txcf;
	for (j = 0; j < 8; j++) {
		p_common->brb_truncates += port_stats.brb.brb_truncate[j];
		p_common->brb_discards += port_stats.brb.brb_discard[j];
	}

	if (ECORE_IS_BB(p_hwfn->p_dev)) {
		struct ecore_eth_stats_bb *p_bb = &p_stats->bb;

		p_bb->rx_1519_to_1522_byte_packets +=
			port_stats.eth.u0.bb0.r1522;
		p_bb->rx_1519_to_2047_byte_packets +=
			port_stats.eth.u0.bb0.r2047;
		p_bb->rx_2048_to_4095_byte_packets +=
			port_stats.eth.u0.bb0.r4095;
		p_bb->rx_4096_to_9216_byte_packets +=
			port_stats.eth.u0.bb0.r9216;
		p_bb->rx_9217_to_16383_byte_packets +=
			port_stats.eth.u0.bb0.r16383;
		p_bb->tx_1519_to_2047_byte_packets +=
			port_stats.eth.u1.bb1.t2047;
		p_bb->tx_2048_to_4095_byte_packets +=
			port_stats.eth.u1.bb1.t4095;
		p_bb->tx_4096_to_9216_byte_packets +=
			port_stats.eth.u1.bb1.t9216;
		p_bb->tx_9217_to_16383_byte_packets +=
			port_stats.eth.u1.bb1.t16383;
		p_bb->tx_lpi_entry_count += port_stats.eth.u2.bb2.tlpiec;
		p_bb->tx_total_collisions += port_stats.eth.u2.bb2.tncl;
	} else {
		struct ecore_eth_stats_ah *p_ah = &p_stats->ah;

		p_ah->rx_1519_to_max_byte_packets +=
			port_stats.eth.u0.ah0.r1519_to_max;
		p_ah->tx_1519_to_max_byte_packets =
			port_stats.eth.u1.ah1.t1519_to_max;
	}

	p_common->link_change_count = ecore_rd(p_hwfn, p_ptt,
					       p_hwfn->mcp_info->port_addr +
					       OFFSETOF(struct public_port,
							link_change_count));
}

void __ecore_get_vport_stats(struct ecore_hwfn *p_hwfn,
			     struct ecore_ptt *p_ptt,
			     struct ecore_eth_stats *stats,
			     u16 statistics_bin, bool b_get_port_stats)
{
	__ecore_get_vport_mstats(p_hwfn, p_ptt, stats, statistics_bin);
	__ecore_get_vport_ustats(p_hwfn, p_ptt, stats, statistics_bin);
	__ecore_get_vport_tstats(p_hwfn, p_ptt, stats);
	__ecore_get_vport_pstats(p_hwfn, p_ptt, stats, statistics_bin);

#ifndef ASIC_ONLY
	/* Avoid getting PORT stats for emulation.*/
	if (CHIP_REV_IS_EMUL(p_hwfn->p_dev))
		return;
#endif

	if (b_get_port_stats && p_hwfn->mcp_info)
		__ecore_get_vport_port_stats(p_hwfn, p_ptt, stats);
}

static void _ecore_get_vport_stats(struct ecore_dev *p_dev,
				   struct ecore_eth_stats *stats)
{
	u8 fw_vport = 0;
	int i;

	OSAL_MEMSET(stats, 0, sizeof(*stats));

	for_each_hwfn(p_dev, i) {
		struct ecore_hwfn *p_hwfn = &p_dev->hwfns[i];
		struct ecore_ptt *p_ptt = IS_PF(p_dev) ?
					  ecore_ptt_acquire(p_hwfn) : OSAL_NULL;
		bool b_get_port_stats;

		if (IS_PF(p_dev)) {
			/* The main vport index is relative first */
			if (ecore_fw_vport(p_hwfn, 0, &fw_vport)) {
				DP_ERR(p_hwfn, "No vport available!\n");
				goto out;
			}
		}

		if (IS_PF(p_dev) && !p_ptt) {
			DP_ERR(p_hwfn, "Failed to acquire ptt\n");
			continue;
		}

		b_get_port_stats = IS_PF(p_dev) && IS_LEAD_HWFN(p_hwfn);
		__ecore_get_vport_stats(p_hwfn, p_ptt, stats, fw_vport,
					b_get_port_stats);

out:
		if (IS_PF(p_dev) && p_ptt)
			ecore_ptt_release(p_hwfn, p_ptt);
	}
}

void ecore_get_vport_stats(struct ecore_dev *p_dev,
			   struct ecore_eth_stats *stats)
{
	u32 i;

	if (!p_dev) {
		OSAL_MEMSET(stats, 0, sizeof(*stats));
		return;
	}

	_ecore_get_vport_stats(p_dev, stats);

	if (!p_dev->reset_stats)
		return;

	/* Reduce the statistics baseline */
	for (i = 0; i < sizeof(struct ecore_eth_stats) / sizeof(u64); i++)
		((u64 *)stats)[i] -= ((u64 *)p_dev->reset_stats)[i];
}

/* zeroes V-PORT specific portion of stats (Port stats remains untouched) */
void ecore_reset_vport_stats(struct ecore_dev *p_dev)
{
	int i;

	for_each_hwfn(p_dev, i) {
		struct ecore_hwfn *p_hwfn = &p_dev->hwfns[i];
		struct eth_mstorm_per_queue_stat mstats;
		struct eth_ustorm_per_queue_stat ustats;
		struct eth_pstorm_per_queue_stat pstats;
		struct ecore_ptt *p_ptt = IS_PF(p_dev) ?
					  ecore_ptt_acquire(p_hwfn) : OSAL_NULL;
		u32 addr = 0, len = 0;

		if (IS_PF(p_dev) && !p_ptt) {
			DP_ERR(p_hwfn, "Failed to acquire ptt\n");
			continue;
		}

		OSAL_MEMSET(&mstats, 0, sizeof(mstats));
		__ecore_get_vport_mstats_addrlen(p_hwfn, &addr, &len, 0);
		ecore_memcpy_to(p_hwfn, p_ptt, addr, &mstats, len);

		OSAL_MEMSET(&ustats, 0, sizeof(ustats));
		__ecore_get_vport_ustats_addrlen(p_hwfn, &addr, &len, 0);
		ecore_memcpy_to(p_hwfn, p_ptt, addr, &ustats, len);

		OSAL_MEMSET(&pstats, 0, sizeof(pstats));
		__ecore_get_vport_pstats_addrlen(p_hwfn, &addr, &len, 0);
		ecore_memcpy_to(p_hwfn, p_ptt, addr, &pstats, len);

		if (IS_PF(p_dev))
			ecore_ptt_release(p_hwfn, p_ptt);
	}

	/* PORT statistics are not necessarily reset, so we need to
	 * read and create a baseline for future statistics.
	 * Link change stat is maintained by MFW, return its value as is.
	 */
	if (!p_dev->reset_stats)
		DP_INFO(p_dev, "Reset stats not allocated\n");
	else {
		_ecore_get_vport_stats(p_dev, p_dev->reset_stats);
		p_dev->reset_stats->common.link_change_count = 0;
	}
}

static enum gft_profile_type
ecore_arfs_mode_to_hsi(enum ecore_filter_config_mode mode)
{
	if (mode == ECORE_FILTER_CONFIG_MODE_5_TUPLE)
		return GFT_PROFILE_TYPE_4_TUPLE;
	if (mode == ECORE_FILTER_CONFIG_MODE_IP_DEST)
		return GFT_PROFILE_TYPE_IP_DST_ADDR;
	return GFT_PROFILE_TYPE_L4_DST_PORT;
}

void ecore_arfs_mode_configure(struct ecore_hwfn *p_hwfn,
			       struct ecore_ptt *p_ptt,
			       struct ecore_arfs_config_params *p_cfg_params)
{
	if (OSAL_TEST_BIT(ECORE_MF_DISABLE_ARFS, &p_hwfn->p_dev->mf_bits))
		return;

	if (p_cfg_params->mode != ECORE_FILTER_CONFIG_MODE_DISABLE) {
		ecore_gft_config(p_hwfn, p_ptt, p_hwfn->rel_pf_id,
				 p_cfg_params->tcp,
				 p_cfg_params->udp,
				 p_cfg_params->ipv4,
				 p_cfg_params->ipv6,
				 ecore_arfs_mode_to_hsi(p_cfg_params->mode));
		DP_VERBOSE(p_hwfn, ECORE_MSG_SP,
			   "Configured Filtering: tcp = %s, udp = %s, ipv4 = %s, ipv6 =%s mode=%08x\n",
			   p_cfg_params->tcp ? "Enable" : "Disable",
			   p_cfg_params->udp ? "Enable" : "Disable",
			   p_cfg_params->ipv4 ? "Enable" : "Disable",
			   p_cfg_params->ipv6 ? "Enable" : "Disable",
			   (u32)p_cfg_params->mode);
	} else {
		DP_VERBOSE(p_hwfn, ECORE_MSG_SP, "Disabled Filtering\n");
		ecore_gft_disable(p_hwfn, p_ptt, p_hwfn->rel_pf_id);
	}
}

enum _ecore_status_t
ecore_configure_rfs_ntuple_filter(struct ecore_hwfn *p_hwfn,
				  struct ecore_spq_comp_cb *p_cb,
				  struct ecore_ntuple_filter_params *p_params)
{
	struct rx_update_gft_filter_data *p_ramrod = OSAL_NULL;
	struct ecore_spq_entry *p_ent = OSAL_NULL;
	struct ecore_sp_init_data init_data;
	u16 abs_rx_q_id = 0;
	u8 abs_vport_id = 0;
	enum _ecore_status_t rc = ECORE_NOTIMPL;

	rc = ecore_fw_vport(p_hwfn, p_params->vport_id, &abs_vport_id);
	if (rc != ECORE_SUCCESS)
		return rc;

	if (p_params->qid != ECORE_RFS_NTUPLE_QID_RSS) {
		rc = ecore_fw_l2_queue(p_hwfn, p_params->qid, &abs_rx_q_id);
		if (rc != ECORE_SUCCESS)
			return rc;
	}

	/* Get SPQ entry */
	OSAL_MEMSET(&init_data, 0, sizeof(init_data));
	init_data.cid = ecore_spq_get_cid(p_hwfn);

	init_data.opaque_fid = p_hwfn->hw_info.opaque_fid;

	if (p_cb) {
		init_data.comp_mode = ECORE_SPQ_MODE_CB;
		init_data.p_comp_data = p_cb;
	} else {
		init_data.comp_mode = ECORE_SPQ_MODE_EBLOCK;
	}

	rc = ecore_sp_init_request(p_hwfn, &p_ent,
				   ETH_RAMROD_GFT_UPDATE_FILTER,
				   PROTOCOLID_ETH, &init_data);
	if (rc != ECORE_SUCCESS)
		return rc;

	p_ramrod = &p_ent->ramrod.rx_update_gft;

	DMA_REGPAIR_LE(p_ramrod->pkt_hdr_addr, p_params->addr);
	p_ramrod->pkt_hdr_length = OSAL_CPU_TO_LE16(p_params->length);

	if (p_params->qid != ECORE_RFS_NTUPLE_QID_RSS) {
		p_ramrod->rx_qid_valid = 1;
		p_ramrod->rx_qid = OSAL_CPU_TO_LE16(abs_rx_q_id);
	}

	p_ramrod->flow_id_valid = 0;
	p_ramrod->flow_id = 0;

	p_ramrod->vport_id = OSAL_CPU_TO_LE16 ((u16)abs_vport_id);
	p_ramrod->filter_action = p_params->b_is_add ? GFT_ADD_FILTER
						     : GFT_DELETE_FILTER;

	DP_VERBOSE(p_hwfn, ECORE_MSG_SP,
		   "V[%0x], Q[%04x] - %s filter from 0x%llx [length %04xb]\n",
		   abs_vport_id, abs_rx_q_id,
		   p_params->b_is_add ? "Adding" : "Removing",
		   (unsigned long long)p_params->addr, p_params->length);

	return ecore_spq_post(p_hwfn, p_ent, OSAL_NULL);
}

int ecore_get_rxq_coalesce(struct ecore_hwfn *p_hwfn,
			   struct ecore_ptt *p_ptt,
			   struct ecore_queue_cid *p_cid,
			   u16 *p_rx_coal)
{
	u32 coalesce, address, is_valid;
	struct cau_sb_entry sb_entry;
	u8 timer_res;
	enum _ecore_status_t rc;

	rc = ecore_dmae_grc2host(p_hwfn, p_ptt, CAU_REG_SB_VAR_MEMORY +
				 p_cid->sb_igu_id * sizeof(u64),
				 (u64)(osal_uintptr_t)&sb_entry, 2,
				 OSAL_NULL /* default parameters */);
	if (rc != ECORE_SUCCESS) {
		DP_ERR(p_hwfn, "dmae_grc2host failed %d\n", rc);
		return rc;
	}

	timer_res = GET_FIELD(sb_entry.params, CAU_SB_ENTRY_TIMER_RES0);

	address = BAR0_MAP_REG_USDM_RAM +
		  USTORM_ETH_QUEUE_ZONE_OFFSET(p_cid->abs.queue_id);
	coalesce = ecore_rd(p_hwfn, p_ptt, address);

	is_valid = GET_FIELD(coalesce, COALESCING_TIMESET_VALID);
	if (!is_valid)
		return ECORE_INVAL;

	coalesce = GET_FIELD(coalesce, COALESCING_TIMESET_TIMESET);
	*p_rx_coal = (u16)(coalesce << timer_res);

	return ECORE_SUCCESS;
}

int ecore_get_txq_coalesce(struct ecore_hwfn *p_hwfn,
			   struct ecore_ptt *p_ptt,
			   struct ecore_queue_cid *p_cid,
			   u16 *p_tx_coal)
{
	u32 coalesce, address, is_valid;
	struct cau_sb_entry sb_entry;
	u8 timer_res;
	enum _ecore_status_t rc;

	rc = ecore_dmae_grc2host(p_hwfn, p_ptt, CAU_REG_SB_VAR_MEMORY +
				 p_cid->sb_igu_id * sizeof(u64),
				 (u64)(osal_uintptr_t)&sb_entry, 2,
				 OSAL_NULL /* default parameters */);
	if (rc != ECORE_SUCCESS) {
		DP_ERR(p_hwfn, "dmae_grc2host failed %d\n", rc);
		return rc;
	}

	timer_res = GET_FIELD(sb_entry.params, CAU_SB_ENTRY_TIMER_RES1);

	address = BAR0_MAP_REG_XSDM_RAM +
		  XSTORM_ETH_QUEUE_ZONE_OFFSET(p_cid->abs.queue_id);
	coalesce = ecore_rd(p_hwfn, p_ptt, address);

	is_valid = GET_FIELD(coalesce, COALESCING_TIMESET_VALID);
	if (!is_valid)
		return ECORE_INVAL;

	coalesce = GET_FIELD(coalesce, COALESCING_TIMESET_TIMESET);
	*p_tx_coal = (u16)(coalesce << timer_res);

	return ECORE_SUCCESS;
}

enum _ecore_status_t
ecore_get_queue_coalesce(struct ecore_hwfn *p_hwfn, u16 *p_coal,
			 void *handle)
{
	struct ecore_queue_cid *p_cid = (struct ecore_queue_cid *)handle;
	enum _ecore_status_t rc = ECORE_SUCCESS;
	struct ecore_ptt *p_ptt;

#ifdef CONFIG_ECORE_SRIOV
	if (IS_VF(p_hwfn->p_dev)) {
		rc = ecore_vf_pf_get_coalesce(p_hwfn, p_coal, p_cid);
		if (rc != ECORE_SUCCESS)
			DP_NOTICE(p_hwfn, false,
				  "Unable to read queue calescing\n");

		return rc;
	}
#endif

	p_ptt = ecore_ptt_acquire(p_hwfn);
	if (!p_ptt)
		return ECORE_AGAIN;

	if (p_cid->b_is_rx) {
		rc = ecore_get_rxq_coalesce(p_hwfn, p_ptt, p_cid, p_coal);
		if (rc != ECORE_SUCCESS)
			goto out;
	} else {
		rc = ecore_get_txq_coalesce(p_hwfn, p_ptt, p_cid, p_coal);
		if (rc != ECORE_SUCCESS)
			goto out;
	}

out:
	ecore_ptt_release(p_hwfn, p_ptt);

	return rc;
}
#ifdef _NTDDK_
#pragma warning(pop)
#endif
