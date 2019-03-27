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


#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "bcm_osal.h"
#include "ecore.h"
#include "ecore_hsi_eth.h"
#include "ecore_sriov.h"
#include "ecore_l2_api.h"
#include "ecore_vf.h"
#include "ecore_vfpf_if.h"
#include "ecore_status.h"
#include "reg_addr.h"
#include "ecore_int.h"
#include "ecore_l2.h"
#include "ecore_mcp_api.h"
#include "ecore_vf_api.h"

#ifdef _NTDDK_
#pragma warning(push)
#pragma warning(disable : 28167)
#pragma warning(disable : 28123)
#pragma warning(disable : 28121)
#endif

static void *ecore_vf_pf_prep(struct ecore_hwfn *p_hwfn,
			      u16 type, u16 length)
{
	struct ecore_vf_iov *p_iov = p_hwfn->vf_iov_info;
	void *p_tlv;

	/* This lock is released when we receive PF's response
	 * in ecore_send_msg2pf().
	 * So, ecore_vf_pf_prep() and ecore_send_msg2pf()
	 * must come in sequence.
	 */
	OSAL_MUTEX_ACQUIRE(&(p_iov->mutex));

	DP_VERBOSE(p_hwfn, ECORE_MSG_IOV, "preparing to send %s tlv over vf pf channel\n",
		   ecore_channel_tlvs_string[type]);

	/* Reset Requst offset */
	p_iov->offset = (u8 *)p_iov->vf2pf_request;

	/* Clear mailbox - both request and reply */
	OSAL_MEMSET(p_iov->vf2pf_request, 0,
		    sizeof(union vfpf_tlvs));
	OSAL_MEMSET(p_iov->pf2vf_reply, 0,
		    sizeof(union pfvf_tlvs));

	/* Init type and length */
	p_tlv = ecore_add_tlv(&p_iov->offset, type, length);

	/* Init first tlv header */
	((struct vfpf_first_tlv *)p_tlv)->reply_address =
		(u64)p_iov->pf2vf_reply_phys;

	return p_tlv;
}

static void ecore_vf_pf_req_end(struct ecore_hwfn *p_hwfn,
				 enum _ecore_status_t req_status)
{
	union pfvf_tlvs *resp = p_hwfn->vf_iov_info->pf2vf_reply;

	DP_VERBOSE(p_hwfn, ECORE_MSG_IOV,
		   "VF request status = 0x%x, PF reply status = 0x%x\n",
		   req_status, resp->default_resp.hdr.status);

	OSAL_MUTEX_RELEASE(&(p_hwfn->vf_iov_info->mutex));
}

#ifdef CONFIG_ECORE_SW_CHANNEL
/* The SW channel implementation of Windows needs to know the 'exact'
 * response size of any given message. That means that for future
 * messages we'd be unable to send TLVs to PF if he'll be unable to
 * answer them if the |response| != |default response|.
 * We'd need to handshake in acquire capabilities for any such.
 */
#endif
static enum _ecore_status_t
ecore_send_msg2pf(struct ecore_hwfn *p_hwfn,
		  u8 *done, u32 resp_size)
{
	union vfpf_tlvs *p_req = p_hwfn->vf_iov_info->vf2pf_request;
	struct ustorm_trigger_vf_zone trigger;
	struct ustorm_vf_zone *zone_data;
	enum _ecore_status_t rc = ECORE_SUCCESS;
	int time = 100;

	zone_data = (struct ustorm_vf_zone *)PXP_VF_BAR0_START_USDM_ZONE_B;

	/* output tlvs list */
	ecore_dp_tlv_list(p_hwfn, p_req);

	/* need to add the END TLV to the message size */
	resp_size += sizeof(struct channel_list_end_tlv);

#ifdef CONFIG_ECORE_SW_CHANNEL
	if (!p_hwfn->vf_iov_info->b_hw_channel) {
		rc =  OSAL_VF_SEND_MSG2PF(p_hwfn->p_dev,
					   done,
					   p_req,
					   p_hwfn->vf_iov_info->pf2vf_reply,
					   sizeof(union vfpf_tlvs),
					   resp_size);
		/* TODO - no prints about message ? */
		return rc;
	}
#endif

	/* Send TLVs over HW channel */
	OSAL_MEMSET(&trigger, 0, sizeof(struct ustorm_trigger_vf_zone));
	trigger.vf_pf_msg_valid = 1;

	DP_VERBOSE(p_hwfn, ECORE_MSG_IOV,
		   "VF -> PF [%02x] message: [%08x, %08x] --> %p, %08x --> %p\n",
		   GET_FIELD(p_hwfn->hw_info.concrete_fid,
			     PXP_CONCRETE_FID_PFID),
		   U64_HI(p_hwfn->vf_iov_info->vf2pf_request_phys),
		   U64_LO(p_hwfn->vf_iov_info->vf2pf_request_phys),
		   &zone_data->non_trigger.vf_pf_msg_addr,
		   *((u32 *)&trigger),
		   &zone_data->trigger);

	REG_WR(p_hwfn,
	       (osal_uintptr_t)&zone_data->non_trigger.vf_pf_msg_addr.lo,
	       U64_LO(p_hwfn->vf_iov_info->vf2pf_request_phys));

	REG_WR(p_hwfn,
	       (osal_uintptr_t)&zone_data->non_trigger.vf_pf_msg_addr.hi,
	       U64_HI(p_hwfn->vf_iov_info->vf2pf_request_phys));

	/* The message data must be written first, to prevent trigger before
	 * data is written.
	 */
	OSAL_WMB(p_hwfn->p_dev);

	REG_WR(p_hwfn, (osal_uintptr_t)&zone_data->trigger, *((u32 *)&trigger));

	/* When PF would be done with the response, it would write back to the
	 * `done' address. Poll until then.
	 */
	while ((!*done) && time) {
		OSAL_MSLEEP(25);
		time--;
	}

	if (!*done) {
		DP_NOTICE(p_hwfn, true,
			  "VF <-- PF Timeout [Type %d]\n",
			  p_req->first_tlv.tl.type);
		rc =  ECORE_TIMEOUT;
	} else {
		if ((*done != PFVF_STATUS_SUCCESS) &&
		    (*done != PFVF_STATUS_NO_RESOURCE))
			DP_NOTICE(p_hwfn, false,
				  "PF response: %d [Type %d]\n",
				  *done, p_req->first_tlv.tl.type);
		else
			DP_VERBOSE(p_hwfn, ECORE_MSG_IOV,
				   "PF response: %d [Type %d]\n",
				   *done, p_req->first_tlv.tl.type);
	}

	return rc;
}

static void ecore_vf_pf_add_qid(struct ecore_hwfn *p_hwfn,
				struct ecore_queue_cid *p_cid)
{
	struct ecore_vf_iov *p_iov = p_hwfn->vf_iov_info;
	struct vfpf_qid_tlv *p_qid_tlv;

	/* Only add QIDs for the queue if it was negotiated with PF */
	if (!(p_iov->acquire_resp.pfdev_info.capabilities &
	      PFVF_ACQUIRE_CAP_QUEUE_QIDS))
		return;

	p_qid_tlv = ecore_add_tlv(&p_iov->offset,
				  CHANNEL_TLV_QID, sizeof(*p_qid_tlv));
	p_qid_tlv->qid = p_cid->qid_usage_idx;
}

static enum _ecore_status_t _ecore_vf_pf_release(struct ecore_hwfn *p_hwfn,
					  bool b_final)
{
	struct ecore_vf_iov *p_iov = p_hwfn->vf_iov_info;
	struct pfvf_def_resp_tlv *resp;
	struct vfpf_first_tlv *req;
	u32 size;
	enum _ecore_status_t rc;

	/* clear mailbox and prep first tlv */
	req = ecore_vf_pf_prep(p_hwfn, CHANNEL_TLV_RELEASE, sizeof(*req));

	/* add list termination tlv */
	ecore_add_tlv(&p_iov->offset,
		      CHANNEL_TLV_LIST_END,
		      sizeof(struct channel_list_end_tlv));

	resp = &p_iov->pf2vf_reply->default_resp;
	rc = ecore_send_msg2pf(p_hwfn, &resp->hdr.status, sizeof(*resp));

	if (rc == ECORE_SUCCESS && resp->hdr.status != PFVF_STATUS_SUCCESS)
		rc = ECORE_AGAIN;

	ecore_vf_pf_req_end(p_hwfn, rc);
	if (!b_final)
		return rc;

	p_hwfn->b_int_enabled = 0;

	if (p_iov->vf2pf_request)
		OSAL_DMA_FREE_COHERENT(p_hwfn->p_dev,
				       p_iov->vf2pf_request,
				       p_iov->vf2pf_request_phys,
				       sizeof(union vfpf_tlvs));
	if (p_iov->pf2vf_reply)
		OSAL_DMA_FREE_COHERENT(p_hwfn->p_dev,
				       p_iov->pf2vf_reply,
				       p_iov->pf2vf_reply_phys,
				       sizeof(union pfvf_tlvs));

	if (p_iov->bulletin.p_virt) {
		size = sizeof(struct ecore_bulletin_content);
		OSAL_DMA_FREE_COHERENT(p_hwfn->p_dev,
				       p_iov->bulletin.p_virt,
				       p_iov->bulletin.phys,
				       size);
	}

#ifdef CONFIG_ECORE_LOCK_ALLOC
	OSAL_MUTEX_DEALLOC(&p_iov->mutex);
#endif

	OSAL_FREE(p_hwfn->p_dev, p_hwfn->vf_iov_info);
	p_hwfn->vf_iov_info = OSAL_NULL;

	return rc;
}

enum _ecore_status_t ecore_vf_pf_release(struct ecore_hwfn *p_hwfn)
{
	return _ecore_vf_pf_release(p_hwfn, true);
}

#define VF_ACQUIRE_THRESH 3
static void ecore_vf_pf_acquire_reduce_resc(struct ecore_hwfn *p_hwfn,
					    struct vf_pf_resc_request *p_req,
					    struct pf_vf_resc *p_resp)
{
	DP_VERBOSE(p_hwfn, ECORE_MSG_IOV,
		   "PF unwilling to fullill resource request: rxq [%02x/%02x] txq [%02x/%02x] sbs [%02x/%02x] mac [%02x/%02x] vlan [%02x/%02x] mc [%02x/%02x] cids [%02x/%02x]. Try PF recommended amount\n",
		   p_req->num_rxqs, p_resp->num_rxqs,
		   p_req->num_rxqs, p_resp->num_txqs,
		   p_req->num_sbs, p_resp->num_sbs,
		   p_req->num_mac_filters, p_resp->num_mac_filters,
		   p_req->num_vlan_filters, p_resp->num_vlan_filters,
		   p_req->num_mc_filters, p_resp->num_mc_filters,
		   p_req->num_cids, p_resp->num_cids);

	/* humble our request */
	p_req->num_txqs = p_resp->num_txqs;
	p_req->num_rxqs = p_resp->num_rxqs;
	p_req->num_sbs = p_resp->num_sbs;
	p_req->num_mac_filters = p_resp->num_mac_filters;
	p_req->num_vlan_filters = p_resp->num_vlan_filters;
	p_req->num_mc_filters = p_resp->num_mc_filters;
	p_req->num_cids = p_resp->num_cids;
}

static enum _ecore_status_t ecore_vf_pf_acquire(struct ecore_hwfn *p_hwfn)
{
	struct ecore_vf_iov *p_iov = p_hwfn->vf_iov_info;
	struct pfvf_acquire_resp_tlv *resp = &p_iov->pf2vf_reply->acquire_resp;
	struct pf_vf_pfdev_info *pfdev_info = &resp->pfdev_info;
	struct ecore_vf_acquire_sw_info vf_sw_info;
	struct vf_pf_resc_request *p_resc;
	bool resources_acquired = false;
	struct vfpf_acquire_tlv *req;
	int attempts = 0;
	enum _ecore_status_t rc = ECORE_SUCCESS;
	int eth_hsi_minor_ver;

	/* clear mailbox and prep first tlv */
	req = ecore_vf_pf_prep(p_hwfn, CHANNEL_TLV_ACQUIRE, sizeof(*req));
	p_resc = &req->resc_request;

	/* @@@ TBD: PF may not be ready bnx2x_get_vf_id... */
	req->vfdev_info.opaque_fid = p_hwfn->hw_info.opaque_fid;

	p_resc->num_rxqs = ECORE_MAX_VF_CHAINS_PER_PF;
	p_resc->num_txqs = ECORE_MAX_VF_CHAINS_PER_PF;
	p_resc->num_sbs = ECORE_MAX_VF_CHAINS_PER_PF;
	p_resc->num_mac_filters = ECORE_ETH_VF_NUM_MAC_FILTERS;
	p_resc->num_vlan_filters = ECORE_ETH_VF_NUM_VLAN_FILTERS;
	p_resc->num_cids = ECORE_ETH_VF_DEFAULT_NUM_CIDS;

	OSAL_MEMSET(&vf_sw_info, 0, sizeof(vf_sw_info));
	OSAL_VF_FILL_ACQUIRE_RESC_REQ(p_hwfn, &req->resc_request, &vf_sw_info);

	req->vfdev_info.os_type = vf_sw_info.os_type;
	req->vfdev_info.driver_version = vf_sw_info.driver_version;
	req->vfdev_info.fw_major = FW_MAJOR_VERSION;
	req->vfdev_info.fw_minor = FW_MINOR_VERSION;
	req->vfdev_info.fw_revision = FW_REVISION_VERSION;
	req->vfdev_info.fw_engineering = FW_ENGINEERING_VERSION;
	req->vfdev_info.eth_fp_hsi_major = ETH_HSI_VER_MAJOR;
	req->vfdev_info.eth_fp_hsi_minor = ETH_HSI_VER_MINOR;

	/* Fill capability field with any non-deprecated config we support */
	req->vfdev_info.capabilities |= VFPF_ACQUIRE_CAP_100G;

	/* If we've mapped the doorbell bar, try using queue qids */
	if (p_iov->b_doorbell_bar) {
		req->vfdev_info.capabilities |= VFPF_ACQUIRE_CAP_PHYSICAL_BAR |
						VFPF_ACQUIRE_CAP_QUEUE_QIDS;
		p_resc->num_cids = ECORE_ETH_VF_MAX_NUM_CIDS;
	}

	/* pf 2 vf bulletin board address */
	req->bulletin_addr = p_iov->bulletin.phys;
	req->bulletin_size = p_iov->bulletin.size;

	/* add list termination tlv */
	ecore_add_tlv(&p_iov->offset,
		      CHANNEL_TLV_LIST_END,
		      sizeof(struct channel_list_end_tlv));

	while (!resources_acquired) {
		DP_VERBOSE(p_hwfn, ECORE_MSG_IOV, "attempting to acquire resources\n");

		/* Clear response buffer, as this might be a re-send */
		OSAL_MEMSET(p_iov->pf2vf_reply, 0,
			    sizeof(union pfvf_tlvs));

		/* send acquire request */
		rc = ecore_send_msg2pf(p_hwfn,
				       &resp->hdr.status,
				       sizeof(*resp));
		if (rc != ECORE_SUCCESS)
			goto exit;

		/* copy acquire response from buffer to p_hwfn */
		OSAL_MEMCPY(&p_iov->acquire_resp,
			    resp,
			    sizeof(p_iov->acquire_resp));

		attempts++;

		if (resp->hdr.status == PFVF_STATUS_SUCCESS) {
			/* PF agrees to allocate our resources */
			if (!(resp->pfdev_info.capabilities &
			      PFVF_ACQUIRE_CAP_POST_FW_OVERRIDE)) {
				/* It's possible legacy PF mistakenly accepted;
				 * but we don't care - simply mark it as
				 * legacy and continue.
				 */
				req->vfdev_info.capabilities |=
					VFPF_ACQUIRE_CAP_PRE_FP_HSI;
			}
			DP_VERBOSE(p_hwfn, ECORE_MSG_IOV, "resources acquired\n");
			resources_acquired = true;
		} /* PF refuses to allocate our resources */
		else if (resp->hdr.status == PFVF_STATUS_NO_RESOURCE &&
			 attempts < VF_ACQUIRE_THRESH) {
			ecore_vf_pf_acquire_reduce_resc(p_hwfn, p_resc,
							&resp->resc);

		} else if (resp->hdr.status == PFVF_STATUS_NOT_SUPPORTED) {
			if (pfdev_info->major_fp_hsi &&
			    (pfdev_info->major_fp_hsi != ETH_HSI_VER_MAJOR)) {
				DP_NOTICE(p_hwfn, false,
					  "PF uses an incompatible fastpath HSI %02x.%02x [VF requires %02x.%02x]. Please change to a VF driver using %02x.xx.\n",
					  pfdev_info->major_fp_hsi,
					  pfdev_info->minor_fp_hsi,
					  ETH_HSI_VER_MAJOR, ETH_HSI_VER_MINOR,
					  pfdev_info->major_fp_hsi);
				rc = ECORE_INVAL;
				goto exit;
			}

			if (!pfdev_info->major_fp_hsi) {
				if (req->vfdev_info.capabilities &
				    VFPF_ACQUIRE_CAP_PRE_FP_HSI) {
					DP_NOTICE(p_hwfn, false,
						  "PF uses very old drivers. Please change to a VF driver using no later than 8.8.x.x.\n");
					rc = ECORE_INVAL;
					goto exit;
				} else {
					DP_INFO(p_hwfn,
						"PF is old - try re-acquire to see if it supports FW-version override\n");
					req->vfdev_info.capabilities |=
						VFPF_ACQUIRE_CAP_PRE_FP_HSI;
					continue;
				}
			}

			/* If PF/VF are using same Major, PF must have had
			 * it's reasons. Simply fail.
			 */
			DP_NOTICE(p_hwfn, false,
				  "PF rejected acquisition by VF\n");
			rc = ECORE_INVAL;
			goto exit;
		} else {
			DP_ERR(p_hwfn, "PF returned error %d to VF acquisition request\n",
			       resp->hdr.status);
			rc = ECORE_AGAIN;
			goto exit;
		}
	}

	/* Mark the PF as legacy, if needed */
	if (req->vfdev_info.capabilities &
	    VFPF_ACQUIRE_CAP_PRE_FP_HSI)
		p_iov->b_pre_fp_hsi = true;

	/* In case PF doesn't support multi-queue Tx, update the number of
	 * CIDs to reflect the number of queues [older PFs didn't fill that
	 * field].
	 */
	if (!(resp->pfdev_info.capabilities &
	      PFVF_ACQUIRE_CAP_QUEUE_QIDS))
		resp->resc.num_cids = resp->resc.num_rxqs +
				      resp->resc.num_txqs;

#ifndef LINUX_REMOVE
	rc = OSAL_VF_UPDATE_ACQUIRE_RESC_RESP(p_hwfn, &resp->resc);
	if (rc) {
		DP_NOTICE(p_hwfn, true,
			  "VF_UPDATE_ACQUIRE_RESC_RESP Failed: status = 0x%x.\n",
			  rc);
		rc = ECORE_AGAIN;
		goto exit;
	}
#endif

	/* Update bulletin board size with response from PF */
	p_iov->bulletin.size = resp->bulletin_size;

	/* get HW info */
	p_hwfn->p_dev->type = resp->pfdev_info.dev_type;
	p_hwfn->p_dev->chip_rev = (u8) resp->pfdev_info.chip_rev;

	DP_INFO(p_hwfn, "Chip details - %s%d\n",
		ECORE_IS_BB(p_hwfn->p_dev) ? "BB" : "AH",
		CHIP_REV_IS_A0(p_hwfn->p_dev) ? 0 : 1);

	p_hwfn->p_dev->chip_num = pfdev_info->chip_num & 0xffff;

	/* Learn of the possibility of CMT */
	if (IS_LEAD_HWFN(p_hwfn)) {
		if (resp->pfdev_info.capabilities & PFVF_ACQUIRE_CAP_100G) {
			DP_NOTICE(p_hwfn, false, "100g VF\n");
			p_hwfn->p_dev->num_hwfns = 2;
		}
	}

	eth_hsi_minor_ver = ETH_HSI_VER_MINOR;

	if (!p_iov->b_pre_fp_hsi &&
	    (eth_hsi_minor_ver) &&
	    (resp->pfdev_info.minor_fp_hsi < ETH_HSI_VER_MINOR)) {
		DP_INFO(p_hwfn,
			"PF is using older fastpath HSI; %02x.%02x is configured\n",
			ETH_HSI_VER_MAJOR,
			resp->pfdev_info.minor_fp_hsi);
	}

exit:
	ecore_vf_pf_req_end(p_hwfn, rc);

	return rc;
}

u32 ecore_vf_hw_bar_size(struct ecore_hwfn *p_hwfn,
			 enum BAR_ID bar_id)
{
	u32 bar_size;

	/* Regview size is fixed */
	if (bar_id == BAR_ID_0)
		return 1 << 17;

	/* Doorbell is received from PF */
	bar_size = p_hwfn->vf_iov_info->acquire_resp.pfdev_info.bar_size;
	if (bar_size)
		return 1 << bar_size;
	return 0;
}

enum _ecore_status_t ecore_vf_hw_prepare(struct ecore_hwfn *p_hwfn)
{
	struct ecore_hwfn *p_lead = ECORE_LEADING_HWFN(p_hwfn->p_dev);
	struct ecore_vf_iov *p_iov;
	u32 reg;
	enum _ecore_status_t rc;

	/* Set number of hwfns - might be overriden once leading hwfn learns
	 * actual configuration from PF.
	 */
	if (IS_LEAD_HWFN(p_hwfn))
		p_hwfn->p_dev->num_hwfns = 1;

	reg = PXP_VF_BAR0_ME_OPAQUE_ADDRESS;
	p_hwfn->hw_info.opaque_fid = (u16)REG_RD(p_hwfn, reg);

	reg = PXP_VF_BAR0_ME_CONCRETE_ADDRESS;
	p_hwfn->hw_info.concrete_fid = REG_RD(p_hwfn, reg);

	/* Allocate vf sriov info */
	p_iov = OSAL_ZALLOC(p_hwfn->p_dev, GFP_KERNEL, sizeof(*p_iov));
	if (!p_iov) {
		DP_NOTICE(p_hwfn, true, "Failed to allocate `struct ecore_sriov'\n");
		return ECORE_NOMEM;
	}

	/* Doorbells are tricky; Upper-layer has alreday set the hwfn doorbell
	 * value, but there are several incompatibily scenarios where that
	 * would be incorrect and we'd need to override it.
	 */
	if (p_hwfn->doorbells == OSAL_NULL) {
		p_hwfn->doorbells = (u8 OSAL_IOMEM*)p_hwfn->regview +
						    PXP_VF_BAR0_START_DQ;
#ifndef LINUX_REMOVE
		p_hwfn->db_offset = (u8 *)p_hwfn->doorbells -
					(u8 *)p_hwfn->p_dev->doorbells;
#endif

	} else if (p_hwfn == p_lead) {
		/* For leading hw-function, value is always correct, but need
		 * to handle scenario where legacy PF would not support 100g
		 * mapped bars later.
		 */
		p_iov->b_doorbell_bar = true;
	} else {
		/* here, value would be correct ONLY if the leading hwfn
		 * received indication that mapped-bars are supported.
		 */
		if (p_lead->vf_iov_info->b_doorbell_bar)
			p_iov->b_doorbell_bar = true;
#ifdef LINUX_REMOVE
		else
			p_hwfn->doorbells = (u8 OSAL_IOMEM*)
					    p_hwfn->regview +
					    PXP_VF_BAR0_START_DQ;
#else
		else {
			p_hwfn->doorbells = (u8 OSAL_IOMEM*)p_hwfn->regview +
						PXP_VF_BAR0_START_DQ;
			p_hwfn->db_offset = (u8 *)p_hwfn->doorbells -
						(u8 *)p_hwfn->p_dev->doorbells;
		}
#endif

	}

	/* Allocate vf2pf msg */
	p_iov->vf2pf_request = OSAL_DMA_ALLOC_COHERENT(p_hwfn->p_dev,
							 &p_iov->vf2pf_request_phys,
							 sizeof(union vfpf_tlvs));
	if (!p_iov->vf2pf_request) {
		DP_NOTICE(p_hwfn, true, "Failed to allocate `vf2pf_request' DMA memory\n");
		goto free_p_iov;
	}

	p_iov->pf2vf_reply = OSAL_DMA_ALLOC_COHERENT(p_hwfn->p_dev,
						       &p_iov->pf2vf_reply_phys,
						       sizeof(union pfvf_tlvs));
	if (!p_iov->pf2vf_reply) {
		DP_NOTICE(p_hwfn, true, "Failed to allocate `pf2vf_reply' DMA memory\n");
		goto free_vf2pf_request;
	}

	DP_VERBOSE(p_hwfn, ECORE_MSG_IOV,
		   "VF's Request mailbox [%p virt 0x%llx phys], Response mailbox [%p virt 0x%llx phys]\n",
		   p_iov->vf2pf_request,
		   (unsigned long long)p_iov->vf2pf_request_phys,
		   p_iov->pf2vf_reply,
		   (unsigned long long)p_iov->pf2vf_reply_phys);

	/* Allocate Bulletin board */
	p_iov->bulletin.size = sizeof(struct ecore_bulletin_content);
	p_iov->bulletin.p_virt = OSAL_DMA_ALLOC_COHERENT(p_hwfn->p_dev,
							   &p_iov->bulletin.phys,
							   p_iov->bulletin.size);
	DP_VERBOSE(p_hwfn, ECORE_MSG_IOV,
		   "VF's bulletin Board [%p virt 0x%llx phys 0x%08x bytes]\n",
		   p_iov->bulletin.p_virt,
		   (unsigned long long)p_iov->bulletin.phys,
		   p_iov->bulletin.size);

#ifdef CONFIG_ECORE_LOCK_ALLOC
	OSAL_MUTEX_ALLOC(p_hwfn, &p_iov->mutex);
#endif
	OSAL_MUTEX_INIT(&p_iov->mutex);

	p_hwfn->vf_iov_info = p_iov;

	p_hwfn->hw_info.personality = ECORE_PCI_ETH;

	rc = ecore_vf_pf_acquire(p_hwfn);

	/* If VF is 100g using a mapped bar and PF is too old to support that,
	 * acquisition would succeed - but the VF would have no way knowing
	 * the size of the doorbell bar configured in HW and thus will not
	 * know how to split it for 2nd hw-function.
	 * In this case we re-try without the indication of the mapped
	 * doorbell.
	 */
	if (rc == ECORE_SUCCESS &&
	    p_iov->b_doorbell_bar &&
	    !ecore_vf_hw_bar_size(p_hwfn, BAR_ID_1) &&
	    ECORE_IS_CMT(p_hwfn->p_dev)) {
		rc = _ecore_vf_pf_release(p_hwfn, false);
		if (rc != ECORE_SUCCESS)
			return rc;

		p_iov->b_doorbell_bar = false;
		p_hwfn->doorbells = (u8 OSAL_IOMEM*)p_hwfn->regview +
						    PXP_VF_BAR0_START_DQ;
#ifndef LINUX_REMOVE
		p_hwfn->db_offset = (u8 *)p_hwfn->doorbells -
						(u8 *)p_hwfn->p_dev->doorbells;
#endif
		rc = ecore_vf_pf_acquire(p_hwfn);
	}

	DP_VERBOSE(p_hwfn, ECORE_MSG_IOV,
		   "Regview [%p], Doorbell [%p], Device-doorbell [%p]\n",
		   p_hwfn->regview, p_hwfn->doorbells,
		   p_hwfn->p_dev->doorbells);

	return rc;

free_vf2pf_request:
	OSAL_DMA_FREE_COHERENT(p_hwfn->p_dev, p_iov->vf2pf_request,
			       p_iov->vf2pf_request_phys,
			       sizeof(union vfpf_tlvs));
free_p_iov:
	OSAL_FREE(p_hwfn->p_dev, p_iov);

	return ECORE_NOMEM;
}

#define TSTORM_QZONE_START   PXP_VF_BAR0_START_SDM_ZONE_A
#define MSTORM_QZONE_START(dev)   (TSTORM_QZONE_START + \
				   (TSTORM_QZONE_SIZE * NUM_OF_L2_QUEUES(dev)))

static void
__ecore_vf_prep_tunn_req_tlv(struct vfpf_update_tunn_param_tlv *p_req,
			     struct ecore_tunn_update_type *p_src,
			     enum ecore_tunn_clss mask, u8 *p_cls)
{
	if (p_src->b_update_mode) {
		p_req->tun_mode_update_mask |= (1 << mask);

		if (p_src->b_mode_enabled)
			p_req->tunn_mode |= (1 << mask);
	}

	*p_cls = p_src->tun_cls;
}

static void
ecore_vf_prep_tunn_req_tlv(struct vfpf_update_tunn_param_tlv *p_req,
			   struct ecore_tunn_update_type *p_src,
			   enum ecore_tunn_clss mask, u8 *p_cls,
			   struct ecore_tunn_update_udp_port *p_port,
			   u8 *p_update_port, u16 *p_udp_port)
{
	if (p_port->b_update_port) {
		*p_update_port = 1;
		*p_udp_port = p_port->port;
	}

	__ecore_vf_prep_tunn_req_tlv(p_req, p_src, mask, p_cls);
}

void ecore_vf_set_vf_start_tunn_update_param(struct ecore_tunnel_info *p_tun)
{
	if (p_tun->vxlan.b_mode_enabled)
		p_tun->vxlan.b_update_mode = true;
	if (p_tun->l2_geneve.b_mode_enabled)
		p_tun->l2_geneve.b_update_mode = true;
	if (p_tun->ip_geneve.b_mode_enabled)
		p_tun->ip_geneve.b_update_mode = true;
	if (p_tun->l2_gre.b_mode_enabled)
		p_tun->l2_gre.b_update_mode = true;
	if (p_tun->ip_gre.b_mode_enabled)
		p_tun->ip_gre.b_update_mode = true;

	p_tun->b_update_rx_cls = true;
	p_tun->b_update_tx_cls = true;
}

static void
__ecore_vf_update_tunn_param(struct ecore_tunn_update_type *p_tun,
			     u16 feature_mask, u8 tunn_mode, u8 tunn_cls,
			     enum ecore_tunn_mode val)
{
	if (feature_mask & (1 << val)) {
		p_tun->b_mode_enabled = tunn_mode;
		p_tun->tun_cls = tunn_cls;
	} else {
		p_tun->b_mode_enabled = false;
	}
}

static void
ecore_vf_update_tunn_param(struct ecore_hwfn *p_hwfn,
			   struct ecore_tunnel_info *p_tun,
			   struct pfvf_update_tunn_param_tlv *p_resp)
{
	/* Update mode and classes provided by PF */
	u16 feat_mask = p_resp->tunn_feature_mask;

	__ecore_vf_update_tunn_param(&p_tun->vxlan, feat_mask,
				     p_resp->vxlan_mode, p_resp->vxlan_clss,
				     ECORE_MODE_VXLAN_TUNN);
	__ecore_vf_update_tunn_param(&p_tun->l2_geneve, feat_mask,
				     p_resp->l2geneve_mode,
				     p_resp->l2geneve_clss,
				     ECORE_MODE_L2GENEVE_TUNN);
	__ecore_vf_update_tunn_param(&p_tun->ip_geneve, feat_mask,
				     p_resp->ipgeneve_mode,
				     p_resp->ipgeneve_clss,
				     ECORE_MODE_IPGENEVE_TUNN);
	__ecore_vf_update_tunn_param(&p_tun->l2_gre, feat_mask,
				     p_resp->l2gre_mode, p_resp->l2gre_clss,
				     ECORE_MODE_L2GRE_TUNN);
	__ecore_vf_update_tunn_param(&p_tun->ip_gre, feat_mask,
				     p_resp->ipgre_mode, p_resp->ipgre_clss,
				     ECORE_MODE_IPGRE_TUNN);
	p_tun->geneve_port.port = p_resp->geneve_udp_port;
	p_tun->vxlan_port.port = p_resp->vxlan_udp_port;

	DP_VERBOSE(p_hwfn, ECORE_MSG_IOV,
		   "tunn mode: vxlan=0x%x, l2geneve=0x%x, ipgeneve=0x%x, l2gre=0x%x, ipgre=0x%x",
		   p_tun->vxlan.b_mode_enabled, p_tun->l2_geneve.b_mode_enabled,
		   p_tun->ip_geneve.b_mode_enabled,
		   p_tun->l2_gre.b_mode_enabled,
		   p_tun->ip_gre.b_mode_enabled);
}

enum _ecore_status_t
ecore_vf_pf_tunnel_param_update(struct ecore_hwfn *p_hwfn,
				struct ecore_tunnel_info *p_src)
{
	struct ecore_tunnel_info *p_tun = &p_hwfn->p_dev->tunnel;
	struct ecore_vf_iov *p_iov = p_hwfn->vf_iov_info;
	struct pfvf_update_tunn_param_tlv *p_resp;
	struct vfpf_update_tunn_param_tlv *p_req;
	enum _ecore_status_t rc;

	p_req = ecore_vf_pf_prep(p_hwfn, CHANNEL_TLV_UPDATE_TUNN_PARAM,
				 sizeof(*p_req));

	if (p_src->b_update_rx_cls && p_src->b_update_tx_cls)
		p_req->update_tun_cls = 1;

	ecore_vf_prep_tunn_req_tlv(p_req, &p_src->vxlan, (enum ecore_tunn_clss)ECORE_MODE_VXLAN_TUNN,
				   &p_req->vxlan_clss, &p_src->vxlan_port,
				   &p_req->update_vxlan_port,
				   &p_req->vxlan_port);
	ecore_vf_prep_tunn_req_tlv(p_req, &p_src->l2_geneve,
				   (enum ecore_tunn_clss)ECORE_MODE_L2GENEVE_TUNN,
				   &p_req->l2geneve_clss, &p_src->geneve_port,
				   &p_req->update_geneve_port,
				   &p_req->geneve_port);
	__ecore_vf_prep_tunn_req_tlv(p_req, &p_src->ip_geneve,
				     (enum ecore_tunn_clss)ECORE_MODE_IPGENEVE_TUNN,
				     &p_req->ipgeneve_clss);
	__ecore_vf_prep_tunn_req_tlv(p_req, &p_src->l2_gre,
				     (enum ecore_tunn_clss)ECORE_MODE_L2GRE_TUNN, &p_req->l2gre_clss);
	__ecore_vf_prep_tunn_req_tlv(p_req, &p_src->ip_gre,
				     (enum ecore_tunn_clss)ECORE_MODE_IPGRE_TUNN, &p_req->ipgre_clss);

	/* add list termination tlv */
	ecore_add_tlv(&p_iov->offset,
		      CHANNEL_TLV_LIST_END,
		      sizeof(struct channel_list_end_tlv));

	p_resp = &p_iov->pf2vf_reply->tunn_param_resp;
	rc = ecore_send_msg2pf(p_hwfn, &p_resp->hdr.status, sizeof(*p_resp));

	if (rc)
		goto exit;

	if (p_resp->hdr.status != PFVF_STATUS_SUCCESS) {
		DP_VERBOSE(p_hwfn, ECORE_MSG_IOV,
			   "Failed to update tunnel parameters\n");
		rc = ECORE_INVAL;
	}

	ecore_vf_update_tunn_param(p_hwfn, p_tun, p_resp);
exit:
	ecore_vf_pf_req_end(p_hwfn, rc);
	return rc;
}

enum _ecore_status_t
ecore_vf_pf_rxq_start(struct ecore_hwfn *p_hwfn,
		      struct ecore_queue_cid *p_cid,
		      u16 bd_max_bytes,
		      dma_addr_t bd_chain_phys_addr,
		      dma_addr_t cqe_pbl_addr,
		      u16 cqe_pbl_size,
		      void OSAL_IOMEM **pp_prod)
{
	struct ecore_vf_iov *p_iov = p_hwfn->vf_iov_info;
	struct pfvf_start_queue_resp_tlv *resp;
	struct vfpf_start_rxq_tlv *req;
	u16 rx_qid = p_cid->rel.queue_id;
	enum _ecore_status_t rc;

	/* clear mailbox and prep first tlv */
	req = ecore_vf_pf_prep(p_hwfn, CHANNEL_TLV_START_RXQ, sizeof(*req));

	req->rx_qid = rx_qid;
	req->cqe_pbl_addr = cqe_pbl_addr;
	req->cqe_pbl_size = cqe_pbl_size;
	req->rxq_addr = bd_chain_phys_addr;
	req->hw_sb = p_cid->sb_igu_id;
	req->sb_index = p_cid->sb_idx;
	req->bd_max_bytes = bd_max_bytes;
	req->stat_id = -1; /* Keep initialized, for future compatibility */

	/* If PF is legacy, we'll need to calculate producers ourselves
	 * as well as clean them.
	 */
	if (p_iov->b_pre_fp_hsi) {
		u8 hw_qid = p_iov->acquire_resp.resc.hw_qid[rx_qid];
		u32 init_prod_val = 0;

		*pp_prod = (u8 OSAL_IOMEM*)
			   p_hwfn->regview +
			   MSTORM_QZONE_START(p_hwfn->p_dev) +
			   hw_qid * MSTORM_QZONE_SIZE;

		/* Init the rcq, rx bd and rx sge (if valid) producers to 0 */
		__internal_ram_wr(p_hwfn, *pp_prod, sizeof(u32),
				  (u32 *)(&init_prod_val));
	}

	ecore_vf_pf_add_qid(p_hwfn, p_cid);

	/* add list termination tlv */
	ecore_add_tlv(&p_iov->offset,
		      CHANNEL_TLV_LIST_END,
		      sizeof(struct channel_list_end_tlv));

	resp = &p_iov->pf2vf_reply->queue_start;
	rc = ecore_send_msg2pf(p_hwfn, &resp->hdr.status, sizeof(*resp));
	if (rc)
		goto exit;

	if (resp->hdr.status != PFVF_STATUS_SUCCESS) {
		rc = ECORE_INVAL;
		goto exit;
	}

	/* Learn the address of the producer from the response */
	if (!p_iov->b_pre_fp_hsi) {
		u32 init_prod_val = 0;

		*pp_prod = (u8 OSAL_IOMEM *)p_hwfn->regview + resp->offset;
		DP_VERBOSE(p_hwfn, ECORE_MSG_IOV,
			   "Rxq[0x%02x]: producer at %p [offset 0x%08x]\n",
			   rx_qid, *pp_prod, resp->offset);

		/* Init the rcq, rx bd and rx sge (if valid) producers to 0.
		 * It was actually the PF's responsibility, but since some
		 * old PFs might fail to do so, we do this as well.
		 */
		OSAL_BUILD_BUG_ON(ETH_HSI_VER_MAJOR != 3);
		__internal_ram_wr(p_hwfn, *pp_prod, sizeof(u32),
				  (u32 *)&init_prod_val);
	}

exit:
	ecore_vf_pf_req_end(p_hwfn, rc);

	return rc;
}

enum _ecore_status_t ecore_vf_pf_rxq_stop(struct ecore_hwfn *p_hwfn,
					  struct ecore_queue_cid *p_cid,
					  bool cqe_completion)
{
	struct ecore_vf_iov *p_iov = p_hwfn->vf_iov_info;
	struct vfpf_stop_rxqs_tlv *req;
	struct pfvf_def_resp_tlv *resp;
	enum _ecore_status_t rc;

	/* clear mailbox and prep first tlv */
	req = ecore_vf_pf_prep(p_hwfn, CHANNEL_TLV_STOP_RXQS, sizeof(*req));

	req->rx_qid = p_cid->rel.queue_id;
	req->num_rxqs = 1;
	req->cqe_completion = cqe_completion;

	ecore_vf_pf_add_qid(p_hwfn, p_cid);

	/* add list termination tlv */
	ecore_add_tlv(&p_iov->offset,
		      CHANNEL_TLV_LIST_END,
		      sizeof(struct channel_list_end_tlv));

	resp = &p_iov->pf2vf_reply->default_resp;
	rc = ecore_send_msg2pf(p_hwfn, &resp->hdr.status, sizeof(*resp));
	if (rc)
		goto exit;

	if (resp->hdr.status != PFVF_STATUS_SUCCESS) {
		rc = ECORE_INVAL;
		goto exit;
	}

exit:
	ecore_vf_pf_req_end(p_hwfn, rc);

	return rc;
}

enum _ecore_status_t
ecore_vf_pf_txq_start(struct ecore_hwfn *p_hwfn,
		      struct ecore_queue_cid *p_cid,
		      dma_addr_t pbl_addr, u16 pbl_size,
		      void OSAL_IOMEM **pp_doorbell)
{
	struct ecore_vf_iov *p_iov = p_hwfn->vf_iov_info;
	struct pfvf_start_queue_resp_tlv *resp;
	struct vfpf_start_txq_tlv *req;
	u16 qid = p_cid->rel.queue_id;
	enum _ecore_status_t rc;

	/* clear mailbox and prep first tlv */
	req = ecore_vf_pf_prep(p_hwfn, CHANNEL_TLV_START_TXQ, sizeof(*req));

	req->tx_qid = qid;

	/* Tx */
	req->pbl_addr = pbl_addr;
	req->pbl_size = pbl_size;
	req->hw_sb = p_cid->sb_igu_id;
	req->sb_index = p_cid->sb_idx;

	ecore_vf_pf_add_qid(p_hwfn, p_cid);

	/* add list termination tlv */
	ecore_add_tlv(&p_iov->offset,
		      CHANNEL_TLV_LIST_END,
		      sizeof(struct channel_list_end_tlv));

	resp  = &p_iov->pf2vf_reply->queue_start;
	rc = ecore_send_msg2pf(p_hwfn, &resp->hdr.status, sizeof(*resp));
	if (rc)
		goto exit;

	if (resp->hdr.status != PFVF_STATUS_SUCCESS) {
		rc = ECORE_INVAL;
		goto exit;
	}

	/* Modern PFs provide the actual offsets, while legacy
	 * provided only the queue id.
	 */
	if (!p_iov->b_pre_fp_hsi) {
		*pp_doorbell = (u8 OSAL_IOMEM*)p_hwfn->doorbells +
					       resp->offset;
	} else {
		u8 cid = p_iov->acquire_resp.resc.cid[qid];

		*pp_doorbell = (u8 OSAL_IOMEM*)p_hwfn->doorbells +
					       DB_ADDR_VF(cid, DQ_DEMS_LEGACY);
	}

	DP_VERBOSE(p_hwfn, ECORE_MSG_IOV,
		   "Txq[0x%02x.%02x]: doorbell at %p [offset 0x%08x]\n",
		   qid, p_cid->qid_usage_idx, *pp_doorbell, resp->offset);
exit:
	ecore_vf_pf_req_end(p_hwfn, rc);

	return rc;
}

enum _ecore_status_t ecore_vf_pf_txq_stop(struct ecore_hwfn *p_hwfn,
					  struct ecore_queue_cid *p_cid)
{
	struct ecore_vf_iov *p_iov = p_hwfn->vf_iov_info;
	struct vfpf_stop_txqs_tlv *req;
	struct pfvf_def_resp_tlv *resp;
	enum _ecore_status_t rc;

	/* clear mailbox and prep first tlv */
	req = ecore_vf_pf_prep(p_hwfn, CHANNEL_TLV_STOP_TXQS, sizeof(*req));

	req->tx_qid = p_cid->rel.queue_id;
	req->num_txqs = 1;

	ecore_vf_pf_add_qid(p_hwfn, p_cid);

	/* add list termination tlv */
	ecore_add_tlv(&p_iov->offset,
		      CHANNEL_TLV_LIST_END,
		      sizeof(struct channel_list_end_tlv));

	resp = &p_iov->pf2vf_reply->default_resp;
	rc = ecore_send_msg2pf(p_hwfn, &resp->hdr.status, sizeof(*resp));
	if (rc)
		goto exit;

	if (resp->hdr.status != PFVF_STATUS_SUCCESS) {
		rc = ECORE_INVAL;
		goto exit;
	}

exit:
	ecore_vf_pf_req_end(p_hwfn, rc);

	return rc;
}

#ifndef LINUX_REMOVE
enum _ecore_status_t ecore_vf_pf_rxqs_update(struct ecore_hwfn *p_hwfn,
					     struct ecore_queue_cid **pp_cid,
					     u8 num_rxqs,
					     u8 comp_cqe_flg,
					     u8 comp_event_flg)
{
	struct ecore_vf_iov *p_iov = p_hwfn->vf_iov_info;
	struct pfvf_def_resp_tlv *resp = &p_iov->pf2vf_reply->default_resp;
	struct vfpf_update_rxq_tlv *req;
	enum _ecore_status_t rc;

	/* Starting with CHANNEL_TLV_QID and the need for additional queue
	 * information, this API stopped supporting multiple rxqs.
	 * TODO - remove this and change the API to accept a single queue-cid
	 * in a follow-up patch.
	 */
	if (num_rxqs != 1) {
		DP_NOTICE(p_hwfn, true,
			  "VFs can no longer update more than a single queue\n");
		return ECORE_INVAL;
	}

	/* clear mailbox and prep first tlv */
	req = ecore_vf_pf_prep(p_hwfn, CHANNEL_TLV_UPDATE_RXQ, sizeof(*req));

	req->rx_qid = (*pp_cid)->rel.queue_id;
	req->num_rxqs = 1;

	if (comp_cqe_flg)
		req->flags |= VFPF_RXQ_UPD_COMPLETE_CQE_FLAG;
	if (comp_event_flg)
		req->flags |= VFPF_RXQ_UPD_COMPLETE_EVENT_FLAG;

	ecore_vf_pf_add_qid(p_hwfn, *pp_cid);

	/* add list termination tlv */
	ecore_add_tlv(&p_iov->offset,
		      CHANNEL_TLV_LIST_END,
		      sizeof(struct channel_list_end_tlv));

	rc = ecore_send_msg2pf(p_hwfn, &resp->hdr.status, sizeof(*resp));
	if (rc)
		goto exit;

	if (resp->hdr.status != PFVF_STATUS_SUCCESS) {
		rc = ECORE_INVAL;
		goto exit;
	}

exit:
	ecore_vf_pf_req_end(p_hwfn, rc);
	return rc;
}
#endif

enum _ecore_status_t ecore_vf_pf_vport_start(struct ecore_hwfn *p_hwfn,
					     u8 vport_id,
					     u16 mtu,
					     u8 inner_vlan_removal,
					     enum ecore_tpa_mode tpa_mode,
					     u8 max_buffers_per_cqe,
					     u8 only_untagged,
					     u8 zero_placement_offset)
{
	struct ecore_vf_iov *p_iov = p_hwfn->vf_iov_info;
	struct vfpf_vport_start_tlv *req;
	struct pfvf_def_resp_tlv *resp;
	enum _ecore_status_t rc;
	int i;

	/* clear mailbox and prep first tlv */
	req = ecore_vf_pf_prep(p_hwfn, CHANNEL_TLV_VPORT_START, sizeof(*req));

	req->mtu = mtu;
	req->vport_id = vport_id;
	req->inner_vlan_removal = inner_vlan_removal;
	req->tpa_mode = tpa_mode;
	req->max_buffers_per_cqe = max_buffers_per_cqe;
	req->only_untagged = only_untagged;
	req->zero_placement_offset = zero_placement_offset;

	/* status blocks */
	for (i = 0; i < p_hwfn->vf_iov_info->acquire_resp.resc.num_sbs; i++) {
		struct ecore_sb_info *p_sb = p_hwfn->vf_iov_info->sbs_info[i];

		if (p_sb)
			req->sb_addr[i] = p_sb->sb_phys;
	}

	/* add list termination tlv */
	ecore_add_tlv(&p_iov->offset,
		      CHANNEL_TLV_LIST_END,
		      sizeof(struct channel_list_end_tlv));

	resp  = &p_iov->pf2vf_reply->default_resp;
	rc = ecore_send_msg2pf(p_hwfn, &resp->hdr.status, sizeof(*resp));
	if (rc)
		goto exit;

	if (resp->hdr.status != PFVF_STATUS_SUCCESS) {
		rc = ECORE_INVAL;
		goto exit;
	}

exit:
	ecore_vf_pf_req_end(p_hwfn, rc);

	return rc;
}

enum _ecore_status_t ecore_vf_pf_vport_stop(struct ecore_hwfn *p_hwfn)
{
	struct ecore_vf_iov *p_iov = p_hwfn->vf_iov_info;
	struct pfvf_def_resp_tlv *resp  = &p_iov->pf2vf_reply->default_resp;
	enum _ecore_status_t rc;

	/* clear mailbox and prep first tlv */
	ecore_vf_pf_prep(p_hwfn, CHANNEL_TLV_VPORT_TEARDOWN,
			 sizeof(struct vfpf_first_tlv));

	/* add list termination tlv */
	ecore_add_tlv(&p_iov->offset,
		      CHANNEL_TLV_LIST_END,
		      sizeof(struct channel_list_end_tlv));

	rc = ecore_send_msg2pf(p_hwfn, &resp->hdr.status, sizeof(*resp));
	if (rc)
		goto exit;

	if (resp->hdr.status != PFVF_STATUS_SUCCESS) {
		rc = ECORE_INVAL;
		goto exit;
	}

exit:
	ecore_vf_pf_req_end(p_hwfn, rc);

	return rc;
}

static bool
ecore_vf_handle_vp_update_is_needed(struct ecore_hwfn *p_hwfn,
				    struct ecore_sp_vport_update_params *p_data,
				    u16 tlv)
{
	switch (tlv) {
	case CHANNEL_TLV_VPORT_UPDATE_ACTIVATE:
		return !!(p_data->update_vport_active_rx_flg ||
			  p_data->update_vport_active_tx_flg);
	case CHANNEL_TLV_VPORT_UPDATE_TX_SWITCH:
#ifndef ASIC_ONLY
		/* FPGA doesn't have PVFC and so can't support tx-switching */
		return !!(p_data->update_tx_switching_flg &&
			  !CHIP_REV_IS_FPGA(p_hwfn->p_dev));
#else
		return !!p_data->update_tx_switching_flg;
#endif
	case CHANNEL_TLV_VPORT_UPDATE_VLAN_STRIP:
		return !!p_data->update_inner_vlan_removal_flg;
	case CHANNEL_TLV_VPORT_UPDATE_ACCEPT_ANY_VLAN:
		return !!p_data->update_accept_any_vlan_flg;
	case CHANNEL_TLV_VPORT_UPDATE_MCAST:
		return !!p_data->update_approx_mcast_flg;
	case CHANNEL_TLV_VPORT_UPDATE_ACCEPT_PARAM:
		return !!(p_data->accept_flags.update_rx_mode_config ||
			  p_data->accept_flags.update_tx_mode_config);
	case CHANNEL_TLV_VPORT_UPDATE_RSS:
		return !!p_data->rss_params;
	case CHANNEL_TLV_VPORT_UPDATE_SGE_TPA:
		return !!p_data->sge_tpa_params;
	default:
		DP_INFO(p_hwfn, "Unexpected vport-update TLV[%d] %s\n",
			tlv, ecore_channel_tlvs_string[tlv]);
		return false;
	}
}

static void
ecore_vf_handle_vp_update_tlvs_resp(struct ecore_hwfn *p_hwfn,
				    struct ecore_sp_vport_update_params *p_data)
{
	struct ecore_vf_iov *p_iov = p_hwfn->vf_iov_info;
	struct pfvf_def_resp_tlv *p_resp;
	u16 tlv;

	for (tlv = CHANNEL_TLV_VPORT_UPDATE_ACTIVATE;
	     tlv < CHANNEL_TLV_VPORT_UPDATE_MAX;
	     tlv++) {
		if (!ecore_vf_handle_vp_update_is_needed(p_hwfn, p_data, tlv))
			continue;

		p_resp = (struct pfvf_def_resp_tlv *)
			 ecore_iov_search_list_tlvs(p_hwfn, p_iov->pf2vf_reply,
						    tlv);
		if (p_resp && p_resp->hdr.status)
			DP_VERBOSE(p_hwfn, ECORE_MSG_IOV,
				   "TLV[%d] type %s Configuration %s\n",
				   tlv, ecore_channel_tlvs_string[tlv],
				   (p_resp && p_resp->hdr.status) ? "succeeded"
								  : "failed");
	}
}

enum _ecore_status_t ecore_vf_pf_vport_update(struct ecore_hwfn *p_hwfn,
					      struct ecore_sp_vport_update_params *p_params)
{
	struct ecore_vf_iov *p_iov = p_hwfn->vf_iov_info;
	struct vfpf_vport_update_tlv *req;
	struct pfvf_def_resp_tlv *resp;
	u8 update_rx, update_tx;
	u32 resp_size = 0;
	u16 size, tlv;
	enum _ecore_status_t rc;

	resp = &p_iov->pf2vf_reply->default_resp;
	resp_size = sizeof(*resp);

	update_rx = p_params->update_vport_active_rx_flg;
	update_tx = p_params->update_vport_active_tx_flg;

	/* clear mailbox and prep header tlv */
	ecore_vf_pf_prep(p_hwfn, CHANNEL_TLV_VPORT_UPDATE, sizeof(*req));

	/* Prepare extended tlvs */
	if (update_rx || update_tx) {
		struct vfpf_vport_update_activate_tlv *p_act_tlv;

		size = sizeof(struct vfpf_vport_update_activate_tlv);
		p_act_tlv = ecore_add_tlv(&p_iov->offset,
					  CHANNEL_TLV_VPORT_UPDATE_ACTIVATE,
					  size);
		resp_size += sizeof(struct pfvf_def_resp_tlv);

		if (update_rx) {
			p_act_tlv->update_rx = update_rx;
			p_act_tlv->active_rx = p_params->vport_active_rx_flg;
		}

		if (update_tx) {
			p_act_tlv->update_tx = update_tx;
			p_act_tlv->active_tx = p_params->vport_active_tx_flg;
		}
	}

#ifndef ECORE_UPSTREAM
	if (p_params->update_inner_vlan_removal_flg) {
		struct vfpf_vport_update_vlan_strip_tlv *p_vlan_tlv;

		size = sizeof(struct vfpf_vport_update_vlan_strip_tlv);
		p_vlan_tlv = ecore_add_tlv(&p_iov->offset,
					   CHANNEL_TLV_VPORT_UPDATE_VLAN_STRIP,
					   size);
		resp_size += sizeof(struct pfvf_def_resp_tlv);

		p_vlan_tlv->remove_vlan = p_params->inner_vlan_removal_flg;
	}
#endif

	if (p_params->update_tx_switching_flg) {
		struct vfpf_vport_update_tx_switch_tlv *p_tx_switch_tlv;

		size = sizeof(struct vfpf_vport_update_tx_switch_tlv);
		tlv = CHANNEL_TLV_VPORT_UPDATE_TX_SWITCH;
		p_tx_switch_tlv = ecore_add_tlv(&p_iov->offset,
						tlv, size);
		resp_size += sizeof(struct pfvf_def_resp_tlv);

		p_tx_switch_tlv->tx_switching = p_params->tx_switching_flg;
	}

	if (p_params->update_approx_mcast_flg) {
		struct vfpf_vport_update_mcast_bin_tlv *p_mcast_tlv;

		size = sizeof(struct vfpf_vport_update_mcast_bin_tlv);
		p_mcast_tlv = ecore_add_tlv(&p_iov->offset,
					    CHANNEL_TLV_VPORT_UPDATE_MCAST,
					    size);
		resp_size += sizeof(struct pfvf_def_resp_tlv);

		OSAL_MEMCPY(p_mcast_tlv->bins, p_params->bins,
			    sizeof(u32) * ETH_MULTICAST_MAC_BINS_IN_REGS);
	}

	update_rx = p_params->accept_flags.update_rx_mode_config;
	update_tx = p_params->accept_flags.update_tx_mode_config;

	if (update_rx || update_tx) {
		struct vfpf_vport_update_accept_param_tlv *p_accept_tlv;

		tlv = CHANNEL_TLV_VPORT_UPDATE_ACCEPT_PARAM;
		size = sizeof(struct vfpf_vport_update_accept_param_tlv);
		p_accept_tlv = ecore_add_tlv(&p_iov->offset, tlv, size);
		resp_size += sizeof(struct pfvf_def_resp_tlv);

		if (update_rx) {
			p_accept_tlv->update_rx_mode = update_rx;
			p_accept_tlv->rx_accept_filter =
					p_params->accept_flags.rx_accept_filter;
		}

		if (update_tx) {
			p_accept_tlv->update_tx_mode = update_tx;
			p_accept_tlv->tx_accept_filter =
					p_params->accept_flags.tx_accept_filter;
		}
	}

	if (p_params->rss_params) {
		struct ecore_rss_params *rss_params = p_params->rss_params;
		struct vfpf_vport_update_rss_tlv *p_rss_tlv;
		int i, table_size;

		size = sizeof(struct vfpf_vport_update_rss_tlv);
		p_rss_tlv = ecore_add_tlv(&p_iov->offset,
					  CHANNEL_TLV_VPORT_UPDATE_RSS, size);
		resp_size += sizeof(struct pfvf_def_resp_tlv);

		if (rss_params->update_rss_config)
			p_rss_tlv->update_rss_flags |=
				VFPF_UPDATE_RSS_CONFIG_FLAG;
		if (rss_params->update_rss_capabilities)
			p_rss_tlv->update_rss_flags |=
				VFPF_UPDATE_RSS_CAPS_FLAG;
		if (rss_params->update_rss_ind_table)
			p_rss_tlv->update_rss_flags |=
				VFPF_UPDATE_RSS_IND_TABLE_FLAG;
		if (rss_params->update_rss_key)
			p_rss_tlv->update_rss_flags |=
				VFPF_UPDATE_RSS_KEY_FLAG;

		p_rss_tlv->rss_enable = rss_params->rss_enable;
		p_rss_tlv->rss_caps = rss_params->rss_caps;
		p_rss_tlv->rss_table_size_log = rss_params->rss_table_size_log;

		table_size = OSAL_MIN_T(int, T_ETH_INDIRECTION_TABLE_SIZE,
					1 << p_rss_tlv->rss_table_size_log);
		for (i = 0; i < table_size; i++) {
			struct ecore_queue_cid *p_queue;

			p_queue = rss_params->rss_ind_table[i];
			p_rss_tlv->rss_ind_table[i] = p_queue->rel.queue_id;
		}

		OSAL_MEMCPY(p_rss_tlv->rss_key, rss_params->rss_key,
			    sizeof(rss_params->rss_key));
	}

	if (p_params->update_accept_any_vlan_flg) {
		struct vfpf_vport_update_accept_any_vlan_tlv *p_any_vlan_tlv;

		size = sizeof(struct vfpf_vport_update_accept_any_vlan_tlv);
		tlv = CHANNEL_TLV_VPORT_UPDATE_ACCEPT_ANY_VLAN;
		p_any_vlan_tlv = ecore_add_tlv(&p_iov->offset, tlv, size);

		resp_size += sizeof(struct pfvf_def_resp_tlv);
		p_any_vlan_tlv->accept_any_vlan = p_params->accept_any_vlan;
		p_any_vlan_tlv->update_accept_any_vlan_flg =
					p_params->update_accept_any_vlan_flg;
	}

#ifndef LINUX_REMOVE
	if (p_params->sge_tpa_params) {
		struct ecore_sge_tpa_params *sge_tpa_params;
		struct vfpf_vport_update_sge_tpa_tlv *p_sge_tpa_tlv;

		sge_tpa_params = p_params->sge_tpa_params;
		size = sizeof(struct vfpf_vport_update_sge_tpa_tlv);
		p_sge_tpa_tlv = ecore_add_tlv(&p_iov->offset,
					      CHANNEL_TLV_VPORT_UPDATE_SGE_TPA,
					      size);
		resp_size += sizeof(struct pfvf_def_resp_tlv);

		if (sge_tpa_params->update_tpa_en_flg)
			p_sge_tpa_tlv->update_sge_tpa_flags |=
				VFPF_UPDATE_TPA_EN_FLAG;
		if (sge_tpa_params->update_tpa_param_flg)
			p_sge_tpa_tlv->update_sge_tpa_flags |=
				VFPF_UPDATE_TPA_PARAM_FLAG;

		if (sge_tpa_params->tpa_ipv4_en_flg)
			p_sge_tpa_tlv->sge_tpa_flags |=
				VFPF_TPA_IPV4_EN_FLAG;
		if (sge_tpa_params->tpa_ipv6_en_flg)
			p_sge_tpa_tlv->sge_tpa_flags |=
				VFPF_TPA_IPV6_EN_FLAG;
		if (sge_tpa_params->tpa_pkt_split_flg)
			p_sge_tpa_tlv->sge_tpa_flags |=
				VFPF_TPA_PKT_SPLIT_FLAG;
		if (sge_tpa_params->tpa_hdr_data_split_flg)
			p_sge_tpa_tlv->sge_tpa_flags |=
				VFPF_TPA_HDR_DATA_SPLIT_FLAG;
		if (sge_tpa_params->tpa_gro_consistent_flg)
			p_sge_tpa_tlv->sge_tpa_flags |=
				VFPF_TPA_GRO_CONSIST_FLAG;

		p_sge_tpa_tlv->tpa_max_aggs_num =
			sge_tpa_params->tpa_max_aggs_num;
		p_sge_tpa_tlv->tpa_max_size = sge_tpa_params->tpa_max_size;
		p_sge_tpa_tlv->tpa_min_size_to_start =
			sge_tpa_params->tpa_min_size_to_start;
		p_sge_tpa_tlv->tpa_min_size_to_cont =
			sge_tpa_params->tpa_min_size_to_cont;

		p_sge_tpa_tlv->max_buffers_per_cqe =
			sge_tpa_params->max_buffers_per_cqe;
	}
#endif

	/* add list termination tlv */
	ecore_add_tlv(&p_iov->offset,
		      CHANNEL_TLV_LIST_END,
		      sizeof(struct channel_list_end_tlv));

	rc = ecore_send_msg2pf(p_hwfn, &resp->hdr.status, resp_size);
	if (rc)
		goto exit;

	if (resp->hdr.status != PFVF_STATUS_SUCCESS) {
		rc = ECORE_INVAL;
		goto exit;
	}

	ecore_vf_handle_vp_update_tlvs_resp(p_hwfn, p_params);

exit:
	ecore_vf_pf_req_end(p_hwfn, rc);

	return rc;
}

enum _ecore_status_t ecore_vf_pf_reset(struct ecore_hwfn *p_hwfn)
{
	struct ecore_vf_iov *p_iov = p_hwfn->vf_iov_info;
	struct pfvf_def_resp_tlv *resp;
	struct vfpf_first_tlv *req;
	enum _ecore_status_t rc;

	/* clear mailbox and prep first tlv */
	req = ecore_vf_pf_prep(p_hwfn, CHANNEL_TLV_CLOSE, sizeof(*req));

	/* add list termination tlv */
	ecore_add_tlv(&p_iov->offset,
		      CHANNEL_TLV_LIST_END,
		      sizeof(struct channel_list_end_tlv));

	resp = &p_iov->pf2vf_reply->default_resp;
	rc = ecore_send_msg2pf(p_hwfn, &resp->hdr.status, sizeof(*resp));
	if (rc)
		goto exit;

	if (resp->hdr.status != PFVF_STATUS_SUCCESS) {
		rc = ECORE_AGAIN;
		goto exit;
	}

	p_hwfn->b_int_enabled = 0;

exit:
	ecore_vf_pf_req_end(p_hwfn, rc);

	return rc;
}

void ecore_vf_pf_filter_mcast(struct ecore_hwfn *p_hwfn,
			      struct ecore_filter_mcast *p_filter_cmd)
{
	struct ecore_sp_vport_update_params sp_params;
	int i;

	OSAL_MEMSET(&sp_params, 0, sizeof(sp_params));
	sp_params.update_approx_mcast_flg = 1;

	if (p_filter_cmd->opcode == ECORE_FILTER_ADD) {
		for (i = 0; i < p_filter_cmd->num_mc_addrs; i++) {
			u32 bit;

			bit = ecore_mcast_bin_from_mac(p_filter_cmd->mac[i]);
			sp_params.bins[bit / 32] |= 1 << (bit % 32);
		}
	}

	ecore_vf_pf_vport_update(p_hwfn, &sp_params);
}

enum _ecore_status_t ecore_vf_pf_filter_ucast(struct ecore_hwfn *p_hwfn,
					      struct ecore_filter_ucast *p_ucast)
{
	struct ecore_vf_iov *p_iov = p_hwfn->vf_iov_info;
	struct vfpf_ucast_filter_tlv *req;
	struct pfvf_def_resp_tlv *resp;
	enum _ecore_status_t rc;

#ifndef LINUX_REMOVE
	/* Sanitize */
	if (p_ucast->opcode == ECORE_FILTER_MOVE) {
		DP_NOTICE(p_hwfn, true, "VFs don't support Moving of filters\n");
		return ECORE_INVAL;
	}
#endif

	/* clear mailbox and prep first tlv */
	req = ecore_vf_pf_prep(p_hwfn, CHANNEL_TLV_UCAST_FILTER, sizeof(*req));
	req->opcode = (u8)p_ucast->opcode;
	req->type = (u8)p_ucast->type;
	OSAL_MEMCPY(req->mac, p_ucast->mac, ETH_ALEN);
	req->vlan = p_ucast->vlan;

	/* add list termination tlv */
	ecore_add_tlv(&p_iov->offset,
		      CHANNEL_TLV_LIST_END,
		      sizeof(struct channel_list_end_tlv));

	resp = &p_iov->pf2vf_reply->default_resp;
	rc = ecore_send_msg2pf(p_hwfn, &resp->hdr.status, sizeof(*resp));
	if (rc)
		goto exit;

	if (resp->hdr.status != PFVF_STATUS_SUCCESS) {
		rc = ECORE_AGAIN;
		goto exit;
	}

exit:
	ecore_vf_pf_req_end(p_hwfn, rc);

	return rc;
}

enum _ecore_status_t ecore_vf_pf_int_cleanup(struct ecore_hwfn *p_hwfn)
{
	struct ecore_vf_iov *p_iov = p_hwfn->vf_iov_info;
	struct pfvf_def_resp_tlv *resp  = &p_iov->pf2vf_reply->default_resp;
	enum _ecore_status_t rc;

	/* clear mailbox and prep first tlv */
	ecore_vf_pf_prep(p_hwfn, CHANNEL_TLV_INT_CLEANUP,
			 sizeof(struct vfpf_first_tlv));

	/* add list termination tlv */
	ecore_add_tlv(&p_iov->offset,
		      CHANNEL_TLV_LIST_END,
		      sizeof(struct channel_list_end_tlv));

	rc = ecore_send_msg2pf(p_hwfn, &resp->hdr.status, sizeof(*resp));
	if (rc)
		goto exit;

	if (resp->hdr.status != PFVF_STATUS_SUCCESS) {
		rc = ECORE_INVAL;
		goto exit;
	}

exit:
	ecore_vf_pf_req_end(p_hwfn, rc);

	return rc;
}

enum _ecore_status_t ecore_vf_pf_get_coalesce(struct ecore_hwfn *p_hwfn,
					      u16 *p_coal,
					      struct ecore_queue_cid *p_cid)
{
	struct ecore_vf_iov *p_iov = p_hwfn->vf_iov_info;
	struct pfvf_read_coal_resp_tlv *resp;
	struct vfpf_read_coal_req_tlv *req;
	enum _ecore_status_t rc;

	/* clear mailbox and prep header tlv */
	req = ecore_vf_pf_prep(p_hwfn, CHANNEL_TLV_COALESCE_READ,
			       sizeof(*req));
	req->qid = p_cid->rel.queue_id;
	req->is_rx = p_cid->b_is_rx ? 1 : 0;

	ecore_add_tlv(&p_iov->offset, CHANNEL_TLV_LIST_END,
		      sizeof(struct channel_list_end_tlv));
	resp = &p_iov->pf2vf_reply->read_coal_resp;

	rc = ecore_send_msg2pf(p_hwfn, &resp->hdr.status, sizeof(*resp));
	if (rc != ECORE_SUCCESS)
		goto exit;

	if (resp->hdr.status != PFVF_STATUS_SUCCESS)
		goto exit;

	*p_coal = resp->coal;
exit:
	ecore_vf_pf_req_end(p_hwfn, rc);

	return rc;
}

enum _ecore_status_t
ecore_vf_pf_set_coalesce(struct ecore_hwfn *p_hwfn, u16 rx_coal, u16 tx_coal,
			 struct ecore_queue_cid     *p_cid)
{
	struct ecore_vf_iov *p_iov = p_hwfn->vf_iov_info;
	struct vfpf_update_coalesce *req;
	struct pfvf_def_resp_tlv *resp;
	enum _ecore_status_t rc;

	/* clear mailbox and prep header tlv */
	req = ecore_vf_pf_prep(p_hwfn, CHANNEL_TLV_COALESCE_UPDATE,
			       sizeof(*req));

	req->rx_coal = rx_coal;
	req->tx_coal = tx_coal;
	req->qid = p_cid->rel.queue_id;

	DP_VERBOSE(p_hwfn, ECORE_MSG_IOV,
		   "Setting coalesce rx_coal = %d, tx_coal = %d at queue = %d\n",
		   rx_coal, tx_coal, req->qid);

	/* add list termination tlv */
	ecore_add_tlv(&p_iov->offset, CHANNEL_TLV_LIST_END,
		      sizeof(struct channel_list_end_tlv));

	resp = &p_iov->pf2vf_reply->default_resp;
	rc = ecore_send_msg2pf(p_hwfn, &resp->hdr.status, sizeof(*resp));

	if (rc != ECORE_SUCCESS)
		goto exit;

	if (resp->hdr.status != PFVF_STATUS_SUCCESS)
		goto exit;

	p_hwfn->p_dev->rx_coalesce_usecs = rx_coal;
	p_hwfn->p_dev->tx_coalesce_usecs = tx_coal;

exit:
	ecore_vf_pf_req_end(p_hwfn, rc);
	return rc;
}

u16 ecore_vf_get_igu_sb_id(struct ecore_hwfn *p_hwfn,
			   u16               sb_id)
{
	struct ecore_vf_iov *p_iov = p_hwfn->vf_iov_info;

	if (!p_iov) {
		DP_NOTICE(p_hwfn, true, "vf_sriov_info isn't initialized\n");
		return 0;
	}

	return p_iov->acquire_resp.resc.hw_sbs[sb_id].hw_sb_id;
}

void ecore_vf_set_sb_info(struct ecore_hwfn *p_hwfn,
			  u16 sb_id, struct ecore_sb_info *p_sb)
{
	struct ecore_vf_iov *p_iov = p_hwfn->vf_iov_info;

	if (!p_iov) {
		DP_NOTICE(p_hwfn, true, "vf_sriov_info isn't initialized\n");
		return;
	}

	if (sb_id >= PFVF_MAX_SBS_PER_VF) {
		DP_NOTICE(p_hwfn, true, "Can't configure SB %04x\n", sb_id);
		return;
	}

	p_iov->sbs_info[sb_id] = p_sb;
}

enum _ecore_status_t ecore_vf_read_bulletin(struct ecore_hwfn *p_hwfn,
					    u8 *p_change)
{
	struct ecore_vf_iov *p_iov = p_hwfn->vf_iov_info;
	struct ecore_bulletin_content shadow;
	u32 crc, crc_size;

	crc_size = sizeof(p_iov->bulletin.p_virt->crc);
	*p_change = 0;

	/* Need to guarantee PF is not in the middle of writing it */
	OSAL_MEMCPY(&shadow, p_iov->bulletin.p_virt, p_iov->bulletin.size);

	/* If version did not update, no need to do anything */
	if (shadow.version == p_iov->bulletin_shadow.version)
		return ECORE_SUCCESS;

	/* Verify the bulletin we see is valid */
	crc = OSAL_CRC32(0, (u8 *)&shadow + crc_size,
			 p_iov->bulletin.size - crc_size);
	if (crc != shadow.crc)
		return ECORE_AGAIN;

	/* Set the shadow bulletin and process it */
	OSAL_MEMCPY(&p_iov->bulletin_shadow, &shadow, p_iov->bulletin.size);

	DP_VERBOSE(p_hwfn, ECORE_MSG_IOV,
		   "Read a bulletin update %08x\n", shadow.version);

	*p_change = 1;

	return ECORE_SUCCESS;
}

void __ecore_vf_get_link_params(struct ecore_mcp_link_params *p_params,
				struct ecore_bulletin_content *p_bulletin)
{
	OSAL_MEMSET(p_params, 0, sizeof(*p_params));

	p_params->speed.autoneg = p_bulletin->req_autoneg;
	p_params->speed.advertised_speeds = p_bulletin->req_adv_speed;
	p_params->speed.forced_speed = p_bulletin->req_forced_speed;
	p_params->pause.autoneg = p_bulletin->req_autoneg_pause;
	p_params->pause.forced_rx = p_bulletin->req_forced_rx;
	p_params->pause.forced_tx = p_bulletin->req_forced_tx;
	p_params->loopback_mode = p_bulletin->req_loopback;
}

void ecore_vf_get_link_params(struct ecore_hwfn *p_hwfn,
			      struct ecore_mcp_link_params *params)
{
	__ecore_vf_get_link_params(params,
				   &(p_hwfn->vf_iov_info->bulletin_shadow));
}

void __ecore_vf_get_link_state(struct ecore_mcp_link_state *p_link,
			       struct ecore_bulletin_content *p_bulletin)
{
	OSAL_MEMSET(p_link, 0, sizeof(*p_link));

	p_link->link_up = p_bulletin->link_up;
	p_link->speed = p_bulletin->speed;
	p_link->full_duplex = p_bulletin->full_duplex;
	p_link->an = p_bulletin->autoneg;
	p_link->an_complete = p_bulletin->autoneg_complete;
	p_link->parallel_detection = p_bulletin->parallel_detection;
	p_link->pfc_enabled = p_bulletin->pfc_enabled;
	p_link->partner_adv_speed = p_bulletin->partner_adv_speed;
	p_link->partner_tx_flow_ctrl_en = p_bulletin->partner_tx_flow_ctrl_en;
	p_link->partner_rx_flow_ctrl_en = p_bulletin->partner_rx_flow_ctrl_en;
	p_link->partner_adv_pause = p_bulletin->partner_adv_pause;
	p_link->sfp_tx_fault = p_bulletin->sfp_tx_fault;
}

void ecore_vf_get_link_state(struct ecore_hwfn *p_hwfn,
			     struct ecore_mcp_link_state *link)
{
	__ecore_vf_get_link_state(link,
				  &(p_hwfn->vf_iov_info->bulletin_shadow));
}

void __ecore_vf_get_link_caps(struct ecore_mcp_link_capabilities *p_link_caps,
			      struct ecore_bulletin_content *p_bulletin)
{
	OSAL_MEMSET(p_link_caps, 0, sizeof(*p_link_caps));
	p_link_caps->speed_capabilities = p_bulletin->capability_speed;
}

void ecore_vf_get_link_caps(struct ecore_hwfn *p_hwfn,
			    struct ecore_mcp_link_capabilities *p_link_caps)
{
	__ecore_vf_get_link_caps(p_link_caps,
				 &(p_hwfn->vf_iov_info->bulletin_shadow));
}

void ecore_vf_get_num_rxqs(struct ecore_hwfn *p_hwfn,
			   u8 *num_rxqs)
{
	*num_rxqs = p_hwfn->vf_iov_info->acquire_resp.resc.num_rxqs;
}

void ecore_vf_get_num_txqs(struct ecore_hwfn *p_hwfn,
			   u8 *num_txqs)
{
	*num_txqs = p_hwfn->vf_iov_info->acquire_resp.resc.num_txqs;
}

void ecore_vf_get_num_cids(struct ecore_hwfn *p_hwfn,
			   u8 *num_cids)
{
	*num_cids = p_hwfn->vf_iov_info->acquire_resp.resc.num_cids;
}

void ecore_vf_get_port_mac(struct ecore_hwfn *p_hwfn,
			   u8 *port_mac)
{
	OSAL_MEMCPY(port_mac,
		    p_hwfn->vf_iov_info->acquire_resp.pfdev_info.port_mac,
		    ETH_ALEN);
}

void ecore_vf_get_num_vlan_filters(struct ecore_hwfn *p_hwfn,
				   u8 *num_vlan_filters)
{
	struct ecore_vf_iov *p_vf;

	p_vf = p_hwfn->vf_iov_info;
	*num_vlan_filters = p_vf->acquire_resp.resc.num_vlan_filters;
}

void ecore_vf_get_num_mac_filters(struct ecore_hwfn *p_hwfn,
				  u8 *num_mac_filters)
{
	struct ecore_vf_iov *p_vf = p_hwfn->vf_iov_info;

	*num_mac_filters = p_vf->acquire_resp.resc.num_mac_filters;
}

bool ecore_vf_check_mac(struct ecore_hwfn *p_hwfn, u8 *mac)
{
	struct ecore_bulletin_content *bulletin;

	bulletin = &p_hwfn->vf_iov_info->bulletin_shadow;
	if (!(bulletin->valid_bitmap & (1 << MAC_ADDR_FORCED)))
		return true;

	/* Forbid VF from changing a MAC enforced by PF */
	if (OSAL_MEMCMP(bulletin->mac, mac, ETH_ALEN))
		return false;

	return false;
}

bool ecore_vf_bulletin_get_forced_mac(struct ecore_hwfn *hwfn, u8 *dst_mac,
				      u8 *p_is_forced)
{
	struct ecore_bulletin_content *bulletin;

	bulletin = &hwfn->vf_iov_info->bulletin_shadow;

	if (bulletin->valid_bitmap & (1 << MAC_ADDR_FORCED)) {
		if (p_is_forced)
			*p_is_forced = 1;
	} else if (bulletin->valid_bitmap & (1 << VFPF_BULLETIN_MAC_ADDR)) {
		if (p_is_forced)
			*p_is_forced = 0;
	} else {
		return false;
	}

	OSAL_MEMCPY(dst_mac, bulletin->mac, ETH_ALEN);

	return true;
}

void ecore_vf_bulletin_get_udp_ports(struct ecore_hwfn *p_hwfn,
				     u16 *p_vxlan_port,
				     u16 *p_geneve_port)
{
	struct ecore_bulletin_content *p_bulletin;

	p_bulletin = &p_hwfn->vf_iov_info->bulletin_shadow;

	*p_vxlan_port = p_bulletin->vxlan_udp_port;
	*p_geneve_port = p_bulletin->geneve_udp_port;
}

#ifndef LINUX_REMOVE
bool ecore_vf_bulletin_get_forced_vlan(struct ecore_hwfn *hwfn, u16 *dst_pvid)
{
	struct ecore_bulletin_content *bulletin;

	bulletin = &hwfn->vf_iov_info->bulletin_shadow;

	if (!(bulletin->valid_bitmap & (1 << VLAN_ADDR_FORCED)))
		return false;

	if (dst_pvid)
		*dst_pvid = bulletin->pvid;

	return true;
}

bool ecore_vf_get_pre_fp_hsi(struct ecore_hwfn *p_hwfn)
{
	return p_hwfn->vf_iov_info->b_pre_fp_hsi;
}
#endif

void ecore_vf_get_fw_version(struct ecore_hwfn *p_hwfn,
			     u16 *fw_major, u16 *fw_minor, u16 *fw_rev,
			     u16 *fw_eng)
{
	struct pf_vf_pfdev_info *info;

	info = &p_hwfn->vf_iov_info->acquire_resp.pfdev_info;

	*fw_major = info->fw_major;
	*fw_minor = info->fw_minor;
	*fw_rev = info->fw_rev;
	*fw_eng = info->fw_eng;
}

#ifdef CONFIG_ECORE_SW_CHANNEL
void ecore_vf_set_hw_channel(struct ecore_hwfn *p_hwfn, bool b_is_hw)
{
	p_hwfn->vf_iov_info->b_hw_channel = b_is_hw;
}
#endif

#ifdef _NTDDK_
#pragma warning(pop)
#endif
