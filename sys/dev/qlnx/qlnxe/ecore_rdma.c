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
 * File : ecore_rdma.c
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
#include "ecore_hsi_iwarp.h"
#include "ecore_ll2.h"
#include "ecore_ooo.h"
#ifndef LINUX_REMOVE
#include "ecore_tcp_ip.h"
#endif

enum _ecore_status_t ecore_rdma_bmap_alloc(struct ecore_hwfn *p_hwfn,
					   struct ecore_bmap *bmap,
					   u32		    max_count,
					   char              *name)
{
	u32 size_in_bytes;

	DP_VERBOSE(p_hwfn, ECORE_MSG_RDMA, "max_count = %08x\n", max_count);

	bmap->max_count = max_count;

	if (!max_count) {
		bmap->bitmap = OSAL_NULL;
		return ECORE_SUCCESS;
	}

	size_in_bytes = sizeof(unsigned long) *
		DIV_ROUND_UP(max_count, (sizeof(unsigned long) * 8));

	bmap->bitmap = OSAL_ZALLOC(p_hwfn->p_dev, GFP_KERNEL, size_in_bytes);
	if (!bmap->bitmap)
	{
		DP_NOTICE(p_hwfn, false,
			  "ecore bmap alloc failed: cannot allocate memory (bitmap). rc = %d\n",
			  ECORE_NOMEM);
		return ECORE_NOMEM;
	}

	OSAL_SNPRINTF(bmap->name, QEDR_MAX_BMAP_NAME, "%s", name);

	DP_VERBOSE(p_hwfn, ECORE_MSG_RDMA, "ECORE_SUCCESS\n");
	return ECORE_SUCCESS;
}

enum _ecore_status_t ecore_rdma_bmap_alloc_id(struct ecore_hwfn *p_hwfn,
					      struct ecore_bmap *bmap,
					      u32	       *id_num)
{
	*id_num = OSAL_FIND_FIRST_ZERO_BIT(bmap->bitmap, bmap->max_count);
	if (*id_num >= bmap->max_count)
		return ECORE_INVAL;

	OSAL_SET_BIT(*id_num, bmap->bitmap);

	DP_VERBOSE(p_hwfn, ECORE_MSG_RDMA, "%s bitmap: allocated id %d\n",
		   bmap->name, *id_num);

	return ECORE_SUCCESS;
}

void ecore_bmap_set_id(struct ecore_hwfn *p_hwfn,
		       struct ecore_bmap *bmap,
		       u32		id_num)
{
	if (id_num >= bmap->max_count) {
		DP_NOTICE(p_hwfn, true,
			  "%s bitmap: cannot set id %d max is %d\n",
			  bmap->name, id_num, bmap->max_count);

		return;
	}

	OSAL_SET_BIT(id_num, bmap->bitmap);

	DP_VERBOSE(p_hwfn, ECORE_MSG_RDMA, "%s bitmap: set id %d\n",
		   bmap->name, id_num);
}

void ecore_bmap_release_id(struct ecore_hwfn *p_hwfn,
			   struct ecore_bmap *bmap,
			   u32		    id_num)
{
	bool b_acquired;

	if (id_num >= bmap->max_count)
		return;

	b_acquired = OSAL_TEST_AND_CLEAR_BIT(id_num, bmap->bitmap);
	if (!b_acquired)
	{
		DP_NOTICE(p_hwfn, false, "%s bitmap: id %d already released\n",
			  bmap->name, id_num);
		return;
	}

	DP_VERBOSE(p_hwfn, ECORE_MSG_RDMA, "%s bitmap: released id %d\n",
		   bmap->name, id_num);
}

int ecore_bmap_test_id(struct ecore_hwfn *p_hwfn,
		       struct ecore_bmap *bmap,
		       u32		  id_num)
{
	if (id_num >= bmap->max_count) {
		DP_NOTICE(p_hwfn, true,
			  "%s bitmap: id %d too high. max is %d\n",
			  bmap->name, id_num, bmap->max_count);
		return -1;
	}

	DP_VERBOSE(p_hwfn, ECORE_MSG_RDMA, "%s bitmap: tested id %d\n",
		   bmap->name, id_num);

	return OSAL_TEST_BIT(id_num, bmap->bitmap);
}

static bool ecore_bmap_is_empty(struct ecore_bmap *bmap)
{
	return (bmap->max_count ==
		OSAL_FIND_FIRST_BIT(bmap->bitmap, bmap->max_count));
}

#ifndef LINUX_REMOVE
u32 ecore_rdma_get_sb_id(struct ecore_hwfn *p_hwfn, u32 rel_sb_id)
{
	/* first sb id for RoCE is after all the l2 sb */
	return FEAT_NUM(p_hwfn, ECORE_PF_L2_QUE) + rel_sb_id;
}

u32 ecore_rdma_query_cau_timer_res(void)
{
	return ECORE_CAU_DEF_RX_TIMER_RES;
}
#endif

enum _ecore_status_t ecore_rdma_info_alloc(struct ecore_hwfn    *p_hwfn)
{
	struct ecore_rdma_info *p_rdma_info;

	p_rdma_info = OSAL_ZALLOC(p_hwfn->p_dev, GFP_KERNEL, sizeof(*p_rdma_info));
	if (!p_rdma_info) {
		DP_NOTICE(p_hwfn, false,
			  "ecore rdma alloc failed: cannot allocate memory (rdma info).\n");
		return ECORE_NOMEM;
	}
	p_hwfn->p_rdma_info = p_rdma_info;

#ifdef CONFIG_ECORE_LOCK_ALLOC
	if (OSAL_SPIN_LOCK_ALLOC(p_hwfn, &p_rdma_info->lock)) {
		ecore_rdma_info_free(p_hwfn);
		return ECORE_NOMEM;
	}
#endif
	OSAL_SPIN_LOCK_INIT(&p_rdma_info->lock);

	return ECORE_SUCCESS;
}

void ecore_rdma_info_free(struct ecore_hwfn *p_hwfn)
{
#ifdef CONFIG_ECORE_LOCK_ALLOC
	OSAL_SPIN_LOCK_DEALLOC(&p_hwfn->p_rdma_info->lock);
#endif
	OSAL_FREE(p_hwfn->p_dev, p_hwfn->p_rdma_info);
	p_hwfn->p_rdma_info = OSAL_NULL;
}

static enum _ecore_status_t ecore_rdma_inc_ref_cnt(struct ecore_hwfn *p_hwfn)
{
	enum _ecore_status_t rc = ECORE_INVAL;

	OSAL_SPIN_LOCK(&p_hwfn->p_rdma_info->lock);
	if (p_hwfn->p_rdma_info->active) {
		p_hwfn->p_rdma_info->ref_cnt++;
		rc = ECORE_SUCCESS;
	} else {
		DP_INFO(p_hwfn, "Ref cnt requested for inactive rdma\n");
	}
	OSAL_SPIN_UNLOCK(&p_hwfn->p_rdma_info->lock);
	return rc;
}

static void ecore_rdma_dec_ref_cnt(struct ecore_hwfn *p_hwfn)
{
	OSAL_SPIN_LOCK(&p_hwfn->p_rdma_info->lock);
	p_hwfn->p_rdma_info->ref_cnt--;
	OSAL_SPIN_UNLOCK(&p_hwfn->p_rdma_info->lock);
}

static void ecore_rdma_activate(struct ecore_hwfn *p_hwfn)
{
	OSAL_SPIN_LOCK(&p_hwfn->p_rdma_info->lock);
	p_hwfn->p_rdma_info->active = true;
	OSAL_SPIN_UNLOCK(&p_hwfn->p_rdma_info->lock);
}

/* Part of deactivating rdma is letting all the relevant flows complete before
 * we start shutting down: Currently query-stats which can be called from MCP
 * context.
 */
/* The longest time it can take a rdma flow to complete */
#define ECORE_RDMA_MAX_FLOW_TIME (100)
static enum _ecore_status_t ecore_rdma_deactivate(struct ecore_hwfn *p_hwfn)
{
	int wait_count;

	OSAL_SPIN_LOCK(&p_hwfn->p_rdma_info->lock);
	p_hwfn->p_rdma_info->active = false;
	OSAL_SPIN_UNLOCK(&p_hwfn->p_rdma_info->lock);

	/* We'll give each flow it's time to complete... */
	wait_count = p_hwfn->p_rdma_info->ref_cnt;

	while (p_hwfn->p_rdma_info->ref_cnt) {
		OSAL_MSLEEP(ECORE_RDMA_MAX_FLOW_TIME);
		if (--wait_count == 0) {
			DP_NOTICE(p_hwfn, false,
				  "Timeout on refcnt=%d\n",
				  p_hwfn->p_rdma_info->ref_cnt);
			return ECORE_TIMEOUT;
		}
	}
	return ECORE_SUCCESS;
}

static enum _ecore_status_t ecore_rdma_alloc(struct ecore_hwfn *p_hwfn)
{
	struct ecore_rdma_info *p_rdma_info = p_hwfn->p_rdma_info;
	u32 num_cons, num_tasks;
	enum _ecore_status_t rc;

	DP_VERBOSE(p_hwfn, ECORE_MSG_RDMA, "Allocating RDMA\n");

	if (!p_rdma_info)
		return ECORE_INVAL;

	if (p_hwfn->hw_info.personality == ECORE_PCI_ETH_IWARP)
		p_rdma_info->proto = PROTOCOLID_IWARP;
	else
		p_rdma_info->proto = PROTOCOLID_ROCE;

	num_cons = ecore_cxt_get_proto_cid_count(p_hwfn, p_rdma_info->proto,
						 OSAL_NULL);

	if (IS_IWARP(p_hwfn))
		p_rdma_info->num_qps = num_cons;
	else
		p_rdma_info->num_qps = num_cons / 2;

	/* INTERNAL: RoCE & iWARP use the same taskid */
	num_tasks = ecore_cxt_get_proto_tid_count(p_hwfn, PROTOCOLID_ROCE);

	/* Each MR uses a single task */
	p_rdma_info->num_mrs = num_tasks;

	/* Queue zone lines are shared between RoCE and L2 in such a way that
	 * they can be used by each without obstructing the other.
	 */
	p_rdma_info->queue_zone_base = (u16) RESC_START(p_hwfn, ECORE_L2_QUEUE);
	p_rdma_info->max_queue_zones = (u16) RESC_NUM(p_hwfn, ECORE_L2_QUEUE);

	/* Allocate a struct with device params and fill it */
	p_rdma_info->dev = OSAL_ZALLOC(p_hwfn->p_dev, GFP_KERNEL, sizeof(*p_rdma_info->dev));
	if (!p_rdma_info->dev)
	{
		rc = ECORE_NOMEM;
		DP_NOTICE(p_hwfn, false,
			  "ecore rdma alloc failed: cannot allocate memory (rdma info dev). rc = %d\n",
			  rc);
		return rc;
	}

	/* Allocate a struct with port params and fill it */
	p_rdma_info->port = OSAL_ZALLOC(p_hwfn->p_dev, GFP_KERNEL, sizeof(*p_rdma_info->port));
	if (!p_rdma_info->port)
	{
		DP_NOTICE(p_hwfn, false,
			  "ecore rdma alloc failed: cannot allocate memory (rdma info port)\n");
		return ECORE_NOMEM;
	}

	/* Allocate bit map for pd's */
	rc = ecore_rdma_bmap_alloc(p_hwfn, &p_rdma_info->pd_map, RDMA_MAX_PDS,
				   "PD");
	if (rc != ECORE_SUCCESS)
	{
		DP_VERBOSE(p_hwfn, ECORE_MSG_RDMA,
			   "Failed to allocate pd_map,rc = %d\n",
			   rc);
		return rc;
	}

	/* Allocate bit map for XRC Domains */
	rc = ecore_rdma_bmap_alloc(p_hwfn, &p_rdma_info->xrcd_map,
				   ECORE_RDMA_MAX_XRCDS, "XRCD");
	if (rc != ECORE_SUCCESS)
	{
		DP_VERBOSE(p_hwfn, ECORE_MSG_RDMA,
			   "Failed to allocate xrcd_map,rc = %d\n",
			   rc);
		return rc;
	}

	/* Allocate DPI bitmap */
	rc = ecore_rdma_bmap_alloc(p_hwfn, &p_rdma_info->dpi_map,
				   p_hwfn->dpi_count, "DPI");
	if (rc != ECORE_SUCCESS)
	{
		DP_VERBOSE(p_hwfn, ECORE_MSG_RDMA,
			   "Failed to allocate DPI bitmap, rc = %d\n", rc);
		return rc;
	}

	/* Allocate bitmap for cq's. The maximum number of CQs is bounded to
	 * twice the number of QPs.
	 */
	rc = ecore_rdma_bmap_alloc(p_hwfn, &p_rdma_info->cq_map,
				   num_cons, "CQ");
	if (rc != ECORE_SUCCESS)
	{
		DP_VERBOSE(p_hwfn, ECORE_MSG_RDMA,
			   "Failed to allocate cq bitmap, rc = %d\n", rc);
		return rc;
	}

	/* Allocate bitmap for toggle bit for cq icids
	 * We toggle the bit every time we create or resize cq for a given icid.
	 * The maximum number of CQs is bounded to the number of connections we
	 * support. (num_qps in iWARP or num_qps/2 in RoCE).
	 */
	rc = ecore_rdma_bmap_alloc(p_hwfn, &p_rdma_info->toggle_bits,
				   num_cons, "Toggle");
	if (rc != ECORE_SUCCESS)
	{
		DP_VERBOSE(p_hwfn, ECORE_MSG_RDMA,
			   "Failed to allocate toogle bits, rc = %d\n", rc);
		return rc;
	}

	/* Allocate bitmap for itids */
	rc = ecore_rdma_bmap_alloc(p_hwfn, &p_rdma_info->tid_map,
				   p_rdma_info->num_mrs, "MR");
	if (rc != ECORE_SUCCESS)
	{
		DP_VERBOSE(p_hwfn, ECORE_MSG_RDMA,
			   "Failed to allocate itids bitmaps, rc = %d\n", rc);
		return rc;
	}

	/* Allocate bitmap for qps. */
	rc = ecore_rdma_bmap_alloc(p_hwfn, &p_rdma_info->qp_map,
				   p_rdma_info->num_qps, "QP");
	if (rc != ECORE_SUCCESS)
	{
		DP_VERBOSE(p_hwfn, ECORE_MSG_RDMA,
			   "Failed to allocate qp bitmap, rc = %d\n", rc);
		return rc;
	}

	/* Allocate bitmap for cids used for responders/requesters. */
	rc = ecore_rdma_bmap_alloc(p_hwfn, &p_rdma_info->cid_map, num_cons,
				   "REAL CID");
	if (rc != ECORE_SUCCESS)
	{
		DP_VERBOSE(p_hwfn, ECORE_MSG_RDMA,
			   "Failed to allocate cid bitmap, rc = %d\n", rc);
		return rc;
	}

	/* The first SRQ follows the last XRC SRQ. This means that the
	 * SRQ IDs start from an offset equals to max_xrc_srqs.
	 */
	p_rdma_info->srq_id_offset = (u16)ecore_cxt_get_xrc_srq_count(p_hwfn);
	rc = ecore_rdma_bmap_alloc(p_hwfn, &p_rdma_info->xrc_srq_map,
				   p_rdma_info->srq_id_offset, "XRC SRQ");
	if (rc != ECORE_SUCCESS) {
		DP_VERBOSE(p_hwfn, ECORE_MSG_RDMA,
			   "Failed to allocate xrc srq bitmap, rc = %d\n", rc);
		return rc;
	}

	/* Allocate bitmap for srqs */
	p_rdma_info->num_srqs = ecore_cxt_get_srq_count(p_hwfn);
	rc = ecore_rdma_bmap_alloc(p_hwfn, &p_rdma_info->srq_map,
				   p_rdma_info->num_srqs,
				   "SRQ");
	if (rc != ECORE_SUCCESS) {
		DP_VERBOSE(p_hwfn, ECORE_MSG_RDMA,
			   "Failed to allocate srq bitmap, rc = %d\n", rc);

		return rc;
	}

	if (IS_IWARP(p_hwfn))
		rc = ecore_iwarp_alloc(p_hwfn);

	DP_VERBOSE(p_hwfn, ECORE_MSG_RDMA, "rc = %d\n", rc);

	return rc;
}

void ecore_rdma_bmap_free(struct ecore_hwfn *p_hwfn,
			  struct ecore_bmap *bmap,
			  bool check)
{
	int weight, line, item, last_line, last_item;
	u64 *pmap;

	if (!bmap || !bmap->bitmap)
		return;

	if (!check)
		goto end;

	weight = OSAL_BITMAP_WEIGHT(bmap->bitmap, bmap->max_count);
	if (!weight)
		goto end;

	DP_NOTICE(p_hwfn, false,
		  "%s bitmap not free - size=%d, weight=%d, 512 bits per line\n",
		  bmap->name, bmap->max_count, weight);

	pmap = (u64 *)bmap->bitmap;
	last_line = bmap->max_count / (64*8);
	last_item = last_line * 8 + (((bmap->max_count % (64*8)) + 63) / 64);

	/* print aligned non-zero lines, if any */
	for (item = 0, line = 0; line < last_line; line++, item += 8) {
		if (OSAL_BITMAP_WEIGHT((unsigned long *)&pmap[item], 64*8))
			DP_NOTICE(p_hwfn, false,
				  "line 0x%04x: 0x%016llx 0x%016llx 0x%016llx 0x%016llx 0x%016llx 0x%016llx 0x%016llx 0x%016llx\n",
				  line, (unsigned long long)pmap[item],
				(unsigned long long)pmap[item+1],
				(unsigned long long)pmap[item+2],
				  (unsigned long long)pmap[item+3],
				(unsigned long long)pmap[item+4],
				(unsigned long long)pmap[item+5],
				  (unsigned long long)pmap[item+6],
				(unsigned long long)pmap[item+7]);
	}

	/* print last unaligned non-zero line, if any */
	if ((bmap->max_count % (64*8)) &&
	    (OSAL_BITMAP_WEIGHT((unsigned long *)&pmap[item],
				bmap->max_count-item*64))) {
		u8 str_last_line[200] = { 0 };
		int  offset;

		offset = OSAL_SPRINTF(str_last_line, "line 0x%04x: ", line);
		for (; item < last_item; item++) {
			offset += OSAL_SPRINTF(str_last_line+offset,
					       "0x%016llx ",
				(unsigned long long)pmap[item]);
		}
		DP_NOTICE(p_hwfn, false, "%s\n", str_last_line);
	}

end:
	OSAL_FREE(p_hwfn->p_dev, bmap->bitmap);
	bmap->bitmap = OSAL_NULL;
}


void ecore_rdma_resc_free(struct ecore_hwfn *p_hwfn)
{
	if (IS_IWARP(p_hwfn))
		ecore_iwarp_resc_free(p_hwfn);

	ecore_rdma_bmap_free(p_hwfn, &p_hwfn->p_rdma_info->cid_map, 1);
	ecore_rdma_bmap_free(p_hwfn, &p_hwfn->p_rdma_info->qp_map, 1);
	ecore_rdma_bmap_free(p_hwfn, &p_hwfn->p_rdma_info->pd_map, 1);
	ecore_rdma_bmap_free(p_hwfn, &p_hwfn->p_rdma_info->xrcd_map, 1);
	ecore_rdma_bmap_free(p_hwfn, &p_hwfn->p_rdma_info->dpi_map, 1);
	ecore_rdma_bmap_free(p_hwfn, &p_hwfn->p_rdma_info->cq_map, 1);
	ecore_rdma_bmap_free(p_hwfn, &p_hwfn->p_rdma_info->toggle_bits, 0);
	ecore_rdma_bmap_free(p_hwfn, &p_hwfn->p_rdma_info->tid_map, 1);
	ecore_rdma_bmap_free(p_hwfn, &p_hwfn->p_rdma_info->srq_map, 1);
	ecore_rdma_bmap_free(p_hwfn, &p_hwfn->p_rdma_info->xrc_srq_map, 1);

	OSAL_FREE(p_hwfn->p_dev, p_hwfn->p_rdma_info->port);
	p_hwfn->p_rdma_info->port = OSAL_NULL;

	OSAL_FREE(p_hwfn->p_dev, p_hwfn->p_rdma_info->dev);
	p_hwfn->p_rdma_info->dev = OSAL_NULL;
}

static OSAL_INLINE void ecore_rdma_free_reserved_lkey(struct ecore_hwfn *p_hwfn)
{
	ecore_rdma_free_tid(p_hwfn, p_hwfn->p_rdma_info->dev->reserved_lkey);
}

static void ecore_rdma_free_ilt(struct ecore_hwfn *p_hwfn)
{
	/* Free Connection CXT */
	ecore_cxt_free_ilt_range(
		p_hwfn, ECORE_ELEM_CXT,
		ecore_cxt_get_proto_cid_start(p_hwfn,
					      p_hwfn->p_rdma_info->proto),
		ecore_cxt_get_proto_cid_count(p_hwfn,
					      p_hwfn->p_rdma_info->proto,
					      OSAL_NULL));

	/* Free Task CXT ( Intentionally RoCE as task-id is shared between
	 * RoCE and iWARP
	 */
	ecore_cxt_free_ilt_range(p_hwfn, ECORE_ELEM_TASK, 0,
				 ecore_cxt_get_proto_tid_count(
					 p_hwfn, PROTOCOLID_ROCE));

	/* Free TSDM CXT */
	ecore_cxt_free_ilt_range(p_hwfn, ECORE_ELEM_SRQ, 0,
				 ecore_cxt_get_srq_count(p_hwfn));
}

static void ecore_rdma_free(struct ecore_hwfn *p_hwfn)
{
	DP_VERBOSE(p_hwfn, ECORE_MSG_RDMA, "\n");

	ecore_rdma_free_reserved_lkey(p_hwfn);

	ecore_rdma_resc_free(p_hwfn);

	ecore_rdma_free_ilt(p_hwfn);
}

static void ecore_rdma_get_guid(struct ecore_hwfn *p_hwfn, u8 *guid)
{
	u8 mac_addr[6];

	OSAL_MEMCPY(&mac_addr[0], &p_hwfn->hw_info.hw_mac_addr[0], ETH_ALEN);
	guid[0] = mac_addr[0] ^ 2;
	guid[1] = mac_addr[1];
	guid[2] = mac_addr[2];
	guid[3] = 0xff;
	guid[4] = 0xfe;
	guid[5] = mac_addr[3];
	guid[6] = mac_addr[4];
	guid[7] = mac_addr[5];
}


static void ecore_rdma_init_events(
	struct ecore_hwfn *p_hwfn,
	struct ecore_rdma_start_in_params *params)
{
	struct ecore_rdma_events *events;

	events = &p_hwfn->p_rdma_info->events;

	events->unaffiliated_event = params->events->unaffiliated_event;
	events->affiliated_event = params->events->affiliated_event;
	events->context = params->events->context;
}

static void ecore_rdma_init_devinfo(
	struct ecore_hwfn *p_hwfn,
	struct ecore_rdma_start_in_params *params)
{
	struct ecore_rdma_device *dev = p_hwfn->p_rdma_info->dev;
	u32 pci_status_control;

	/* Vendor specific information */
	dev->vendor_id = p_hwfn->p_dev->vendor_id;
	dev->vendor_part_id = p_hwfn->p_dev->device_id;
	dev->hw_ver = 0;
	dev->fw_ver = STORM_FW_VERSION;

	ecore_rdma_get_guid(p_hwfn, (u8 *)(&dev->sys_image_guid));
	dev->node_guid = dev->sys_image_guid;

	dev->max_sge = OSAL_MIN_T(u32, RDMA_MAX_SGE_PER_SQ_WQE,
				  RDMA_MAX_SGE_PER_RQ_WQE);

	if (p_hwfn->p_dev->rdma_max_sge) {
		dev->max_sge = OSAL_MIN_T(u32,
				     p_hwfn->p_dev->rdma_max_sge,
				     dev->max_sge);
	}

	/* Set these values according to configuration
	 * MAX SGE for SRQ is not defined by FW for now
	 * define it in driver.
	 * TODO: Get this value from FW.
	 */
	dev->max_srq_sge = ECORE_RDMA_MAX_SGE_PER_SRQ_WQE;
	if (p_hwfn->p_dev->rdma_max_srq_sge) {
		dev->max_srq_sge = OSAL_MIN_T(u32,
				     p_hwfn->p_dev->rdma_max_srq_sge,
				     dev->max_srq_sge);
	}

	dev->max_inline = ROCE_REQ_MAX_INLINE_DATA_SIZE;
	dev->max_inline = (p_hwfn->p_dev->rdma_max_inline) ?
		OSAL_MIN_T(u32,
			   p_hwfn->p_dev->rdma_max_inline,
			   dev->max_inline) :
			dev->max_inline;

	dev->max_wqe = ECORE_RDMA_MAX_WQE;
	dev->max_cnq = (u8)FEAT_NUM(p_hwfn, ECORE_RDMA_CNQ);

	/* The number of QPs may be higher than ECORE_ROCE_MAX_QPS. because
	 * it is up-aligned to 16 and then to ILT page size within ecore cxt.
	 * This is OK in terms of ILT but we don't want to configure the FW
	 * above its abilities
	 */
	dev->max_qp = OSAL_MIN_T(u64, ROCE_MAX_QPS,
			     p_hwfn->p_rdma_info->num_qps);

	/* CQs uses the same icids that QPs use hence they are limited by the
	 * number of icids. There are two icids per QP.
	 */
	dev->max_cq = dev->max_qp * 2;

	/* The number of mrs is smaller by 1 since the first is reserved */
	dev->max_mr = p_hwfn->p_rdma_info->num_mrs - 1;
	dev->max_mr_size = ECORE_RDMA_MAX_MR_SIZE;
	/* The maximum CQE capacity per CQ supported */
	/* max number of cqes will be in two layer pbl,
	 * 8 is the pointer size in bytes
	 * 32 is the size of cq element in bytes
	 */
	if (params->roce.cq_mode == ECORE_RDMA_CQ_MODE_32_BITS)
		dev->max_cqe = ECORE_RDMA_MAX_CQE_32_BIT;
	else
		dev->max_cqe = ECORE_RDMA_MAX_CQE_16_BIT;

	dev->max_mw = 0;
	dev->max_fmr = ECORE_RDMA_MAX_FMR;
	dev->max_mr_mw_fmr_pbl = (OSAL_PAGE_SIZE/8) * (OSAL_PAGE_SIZE/8);
	dev->max_mr_mw_fmr_size = dev->max_mr_mw_fmr_pbl * OSAL_PAGE_SIZE;
	dev->max_pkey = ECORE_RDMA_MAX_P_KEY;
	/* Right now we dont take any parameters from user
	 * So assign predefined max_srq to num_srqs.
	 */
	dev->max_srq = p_hwfn->p_rdma_info->num_srqs;

	/* SRQ WQE size */
	dev->max_srq_wr = ECORE_RDMA_MAX_SRQ_WQE_ELEM;

	dev->max_qp_resp_rd_atomic_resc = RDMA_RING_PAGE_SIZE /
					  (RDMA_RESP_RD_ATOMIC_ELM_SIZE*2);
	dev->max_qp_req_rd_atomic_resc = RDMA_RING_PAGE_SIZE /
					 RDMA_REQ_RD_ATOMIC_ELM_SIZE;

	dev->max_dev_resp_rd_atomic_resc =
		dev->max_qp_resp_rd_atomic_resc * p_hwfn->p_rdma_info->num_qps;
	dev->page_size_caps = ECORE_RDMA_PAGE_SIZE_CAPS;
	dev->dev_ack_delay = ECORE_RDMA_ACK_DELAY;
	dev->max_pd = RDMA_MAX_PDS;
	dev->max_ah = dev->max_qp;
	dev->max_stats_queues = (u8)RESC_NUM(p_hwfn, ECORE_RDMA_STATS_QUEUE);

	/* Set capablities */
	dev->dev_caps = 0;
	SET_FIELD(dev->dev_caps, ECORE_RDMA_DEV_CAP_RNR_NAK, 1);
	SET_FIELD(dev->dev_caps, ECORE_RDMA_DEV_CAP_PORT_ACTIVE_EVENT, 1);
	SET_FIELD(dev->dev_caps, ECORE_RDMA_DEV_CAP_PORT_CHANGE_EVENT, 1);
	SET_FIELD(dev->dev_caps, ECORE_RDMA_DEV_CAP_RESIZE_CQ, 1);
	SET_FIELD(dev->dev_caps, ECORE_RDMA_DEV_CAP_BASE_MEMORY_EXT, 1);
	SET_FIELD(dev->dev_caps, ECORE_RDMA_DEV_CAP_BASE_QUEUE_EXT, 1);
	SET_FIELD(dev->dev_caps, ECORE_RDMA_DEV_CAP_ZBVA, 1);
	SET_FIELD(dev->dev_caps, ECORE_RDMA_DEV_CAP_LOCAL_INV_FENCE, 1);

	/* Check atomic operations support in PCI configuration space. */
	OSAL_PCI_READ_CONFIG_DWORD(p_hwfn->p_dev,
				   PCICFG_DEVICE_STATUS_CONTROL_2,
				   &pci_status_control);

	if (pci_status_control &
	    PCICFG_DEVICE_STATUS_CONTROL_2_ATOMIC_REQ_ENABLE)
		SET_FIELD(dev->dev_caps, ECORE_RDMA_DEV_CAP_ATOMIC_OP, 1);

	if (IS_IWARP(p_hwfn))
		ecore_iwarp_init_devinfo(p_hwfn);
}

static void ecore_rdma_init_port(
	struct ecore_hwfn *p_hwfn)
{
	struct ecore_rdma_port *port = p_hwfn->p_rdma_info->port;
	struct ecore_rdma_device *dev = p_hwfn->p_rdma_info->dev;

	port->port_state = p_hwfn->mcp_info->link_output.link_up ?
		ECORE_RDMA_PORT_UP : ECORE_RDMA_PORT_DOWN;

	port->max_msg_size = OSAL_MIN_T(u64,
				   (dev->max_mr_mw_fmr_size *
				    p_hwfn->p_dev->rdma_max_sge),
					((u64)1 << 31));

	port->pkey_bad_counter = 0;
}

static enum _ecore_status_t ecore_rdma_init_hw(
	struct ecore_hwfn *p_hwfn,
	struct ecore_ptt *p_ptt)
{
	u32 ll2_ethertype_en;

	DP_VERBOSE(p_hwfn, ECORE_MSG_RDMA, "Initializing HW\n");
	p_hwfn->b_rdma_enabled_in_prs = false;

	if (IS_IWARP(p_hwfn))
		return ecore_iwarp_init_hw(p_hwfn, p_ptt);

	ecore_wr(p_hwfn,
		 p_ptt,
		 PRS_REG_ROCE_DEST_QP_MAX_PF,
		 0);

	p_hwfn->rdma_prs_search_reg = PRS_REG_SEARCH_ROCE;

	/* We delay writing to this reg until first cid is allocated. See
	 * ecore_cxt_dynamic_ilt_alloc function for more details
	 */

	ll2_ethertype_en = ecore_rd(p_hwfn,
			     p_ptt,
			     PRS_REG_LIGHT_L2_ETHERTYPE_EN);
	ecore_wr(p_hwfn, p_ptt, PRS_REG_LIGHT_L2_ETHERTYPE_EN,
		 (ll2_ethertype_en | 0x01));

#ifndef REAL_ASIC_ONLY
	if (ECORE_IS_BB_A0(p_hwfn->p_dev) && ECORE_IS_CMT(p_hwfn->p_dev)) {
		ecore_wr(p_hwfn,
			 p_ptt,
			 NIG_REG_LLH_ENG_CLS_ENG_ID_TBL,
			 0);
		ecore_wr(p_hwfn,
			 p_ptt,
			 NIG_REG_LLH_ENG_CLS_ENG_ID_TBL + 4,
			 0);
	}
#endif

	if (ecore_cxt_get_proto_cid_start(p_hwfn, PROTOCOLID_ROCE) % 2)
	{
		DP_NOTICE(p_hwfn,
			  true,
			  "The first RoCE's cid should be even\n");
		return ECORE_UNKNOWN_ERROR;
	}

	DP_VERBOSE(p_hwfn, ECORE_MSG_RDMA, "Initializing HW - Done\n");
	return ECORE_SUCCESS;
}

static enum _ecore_status_t
ecore_rdma_start_fw(struct ecore_hwfn *p_hwfn,
#ifdef CONFIG_DCQCN
		    struct ecore_ptt *p_ptt,
#else
		    struct ecore_ptt OSAL_UNUSED *p_ptt,
#endif
		    struct ecore_rdma_start_in_params *params)
{
	struct rdma_init_func_ramrod_data *p_ramrod;
	struct rdma_init_func_hdr *pheader;
	struct ecore_rdma_info *p_rdma_info;
	struct ecore_sp_init_data init_data;
	struct ecore_spq_entry *p_ent;
	u16 igu_sb_id, sb_id;
	u8 ll2_queue_id;
	u32 cnq_id;
	enum _ecore_status_t rc;

	DP_VERBOSE(p_hwfn, ECORE_MSG_RDMA, "Starting FW\n");

	p_rdma_info = p_hwfn->p_rdma_info;

	/* Save the number of cnqs for the function close ramrod */
	p_rdma_info->num_cnqs = params->desired_cnq;

	/* Get SPQ entry */
	OSAL_MEMSET(&init_data, 0, sizeof(init_data));
	init_data.opaque_fid = p_hwfn->hw_info.opaque_fid;
	init_data.comp_mode = ECORE_SPQ_MODE_EBLOCK;

	rc = ecore_sp_init_request(p_hwfn, &p_ent, RDMA_RAMROD_FUNC_INIT,
				   p_rdma_info->proto, &init_data);
	if (rc != ECORE_SUCCESS)
		return rc;

	if (IS_IWARP(p_hwfn)) {
		ecore_iwarp_init_fw_ramrod(p_hwfn,
					   &p_ent->ramrod.iwarp_init_func);
		p_ramrod = &p_ent->ramrod.iwarp_init_func.rdma;
	} else {

#ifdef CONFIG_DCQCN
		rc = ecore_roce_dcqcn_cfg(p_hwfn, &params->roce.dcqcn_params,
					  &p_ent->ramrod.roce_init_func, p_ptt);
		if (rc != ECORE_SUCCESS) {
			DP_NOTICE(p_hwfn, false,
				  "Failed to configure DCQCN. rc = %d.\n", rc);
			return rc;
		}
#endif
		p_ramrod = &p_ent->ramrod.roce_init_func.rdma;

		/* The ll2_queue_id is used only for UD QPs */
		ll2_queue_id = ecore_ll2_handle_to_queue_id(
			p_hwfn, params->roce.ll2_handle);
		p_ent->ramrod.roce_init_func.roce.ll2_queue_id = ll2_queue_id;

	}

	pheader = &p_ramrod->params_header;
	pheader->cnq_start_offset = (u8)RESC_START(p_hwfn, ECORE_RDMA_CNQ_RAM);
	pheader->num_cnqs = params->desired_cnq;

	/* The first SRQ ILT page is used for XRC SRQs and all the following
	 * pages contain regular SRQs. Hence the first regular SRQ ID is the
	 * maximum number XRC SRQs.
	 */
	pheader->first_reg_srq_id = p_rdma_info->srq_id_offset;
	pheader->reg_srq_base_addr =
		ecore_cxt_get_ilt_page_size(p_hwfn, ILT_CLI_TSDM);

	if (params->roce.cq_mode == ECORE_RDMA_CQ_MODE_16_BITS)
		pheader->cq_ring_mode = 1; /* 1=16 bits */
	else
		pheader->cq_ring_mode = 0; /* 0=32 bits */

	for (cnq_id = 0; cnq_id < params->desired_cnq; cnq_id++)
	{
		sb_id = (u16)OSAL_GET_RDMA_SB_ID(p_hwfn, cnq_id);
		igu_sb_id = ecore_get_igu_sb_id(p_hwfn, sb_id);
		p_ramrod->cnq_params[cnq_id].sb_num =
			OSAL_CPU_TO_LE16(igu_sb_id);

		p_ramrod->cnq_params[cnq_id].sb_index =
			p_hwfn->pf_params.rdma_pf_params.gl_pi;

		p_ramrod->cnq_params[cnq_id].num_pbl_pages =
			params->cnq_pbl_list[cnq_id].num_pbl_pages;

		p_ramrod->cnq_params[cnq_id].pbl_base_addr.hi =
			DMA_HI_LE(params->cnq_pbl_list[cnq_id].pbl_ptr);
		p_ramrod->cnq_params[cnq_id].pbl_base_addr.lo =
			DMA_LO_LE(params->cnq_pbl_list[cnq_id].pbl_ptr);

		/* we arbitrarily decide that cnq_id will be as qz_offset */
		p_ramrod->cnq_params[cnq_id].queue_zone_num =
			OSAL_CPU_TO_LE16(p_rdma_info->queue_zone_base + cnq_id);
	}

	rc = ecore_spq_post(p_hwfn, p_ent, OSAL_NULL);

	return rc;
}

enum _ecore_status_t ecore_rdma_alloc_tid(void	*rdma_cxt,
					  u32	*itid)
{
	struct ecore_hwfn *p_hwfn = (struct ecore_hwfn *)rdma_cxt;
	enum _ecore_status_t rc;

	DP_VERBOSE(p_hwfn, ECORE_MSG_RDMA, "Allocate TID\n");

	OSAL_SPIN_LOCK(&p_hwfn->p_rdma_info->lock);
	rc = ecore_rdma_bmap_alloc_id(p_hwfn,
				      &p_hwfn->p_rdma_info->tid_map,
				      itid);
	OSAL_SPIN_UNLOCK(&p_hwfn->p_rdma_info->lock);
	if (rc != ECORE_SUCCESS) {
		DP_NOTICE(p_hwfn, false, "Failed in allocating tid\n");
		goto out;
	}

	rc = ecore_cxt_dynamic_ilt_alloc(p_hwfn, ECORE_ELEM_TASK, *itid);
out:
	DP_VERBOSE(p_hwfn, ECORE_MSG_RDMA, "Allocate TID - done, rc = %d\n", rc);
	return rc;
}

static OSAL_INLINE enum _ecore_status_t ecore_rdma_reserve_lkey(
		struct ecore_hwfn *p_hwfn)
{
	struct ecore_rdma_device *dev = p_hwfn->p_rdma_info->dev;

	/* Tid 0 will be used as the key for "reserved MR".
	 * The driver should allocate memory for it so it can be loaded but no
	 * ramrod should be passed on it.
	 */
	ecore_rdma_alloc_tid(p_hwfn, &dev->reserved_lkey);
	if (dev->reserved_lkey != RDMA_RESERVED_LKEY)
	{
		DP_NOTICE(p_hwfn, true,
			  "Reserved lkey should be equal to RDMA_RESERVED_LKEY\n");
		return ECORE_INVAL;
	}

	return ECORE_SUCCESS;
}

static enum _ecore_status_t ecore_rdma_setup(struct ecore_hwfn    *p_hwfn,
				struct ecore_ptt                  *p_ptt,
				struct ecore_rdma_start_in_params *params)
{
	enum _ecore_status_t rc = 0;

	DP_VERBOSE(p_hwfn, ECORE_MSG_RDMA, "RDMA setup\n");

	ecore_rdma_init_devinfo(p_hwfn, params);
	ecore_rdma_init_port(p_hwfn);
	ecore_rdma_init_events(p_hwfn, params);

	rc = ecore_rdma_reserve_lkey(p_hwfn);
	if (rc != ECORE_SUCCESS)
		return rc;

	rc = ecore_rdma_init_hw(p_hwfn, p_ptt);
	if (rc != ECORE_SUCCESS)
		return rc;

	if (IS_IWARP(p_hwfn)) {
		rc = ecore_iwarp_setup(p_hwfn, params);
		if (rc != ECORE_SUCCESS)
			return rc;
	} else {
		rc = ecore_roce_setup(p_hwfn);
		if (rc != ECORE_SUCCESS)
			return rc;
	}

	return ecore_rdma_start_fw(p_hwfn, p_ptt, params);
}


enum _ecore_status_t ecore_rdma_stop(void *rdma_cxt)
{
	struct ecore_hwfn *p_hwfn = (struct ecore_hwfn *)rdma_cxt;
	struct rdma_close_func_ramrod_data *p_ramrod;
	struct ecore_sp_init_data init_data;
	struct ecore_spq_entry *p_ent;
	struct ecore_ptt *p_ptt;
	u32 ll2_ethertype_en;
	enum _ecore_status_t rc = ECORE_TIMEOUT;

	DP_VERBOSE(p_hwfn, ECORE_MSG_RDMA, "RDMA stop\n");

	rc = ecore_rdma_deactivate(p_hwfn);
	if (rc != ECORE_SUCCESS)
		return rc;

	p_ptt = ecore_ptt_acquire(p_hwfn);
	if (!p_ptt) {
		DP_VERBOSE(p_hwfn, ECORE_MSG_RDMA, "Failed to acquire PTT\n");
		return rc;
	}

#ifdef CONFIG_DCQCN
	ecore_roce_stop_rl(p_hwfn);
#endif

	/* Disable RoCE search */
	ecore_wr(p_hwfn, p_ptt, p_hwfn->rdma_prs_search_reg, 0);
	p_hwfn->b_rdma_enabled_in_prs = false;

	ecore_wr(p_hwfn,
		 p_ptt,
		 PRS_REG_ROCE_DEST_QP_MAX_PF,
		 0);

	ll2_ethertype_en = ecore_rd(p_hwfn,
				    p_ptt,
				    PRS_REG_LIGHT_L2_ETHERTYPE_EN);

	ecore_wr(p_hwfn, p_ptt, PRS_REG_LIGHT_L2_ETHERTYPE_EN,
		 (ll2_ethertype_en & 0xFFFE));

#ifndef REAL_ASIC_ONLY
	/* INTERNAL: In CMT mode, re-initialize nig to direct packets to both
	 * enginesfor L2 performance, Roce requires all traffic to go just to
	 * engine 0.
	 */
	if (ECORE_IS_BB_A0(p_hwfn->p_dev) && ECORE_IS_CMT(p_hwfn->p_dev)) {
		DP_ERR(p_hwfn->p_dev,
		       "On Everest 4 Big Bear Board revision A0 when RoCE driver is loaded L2 performance is sub-optimal (all traffic is routed to engine 0). For optimal L2 results either remove RoCE driver or use board revision B0\n");

		ecore_wr(p_hwfn,
			 p_ptt,
			 NIG_REG_LLH_ENG_CLS_ENG_ID_TBL,
			 0x55555555);
		ecore_wr(p_hwfn,
			 p_ptt,
			 NIG_REG_LLH_ENG_CLS_ENG_ID_TBL + 0x4,
			 0x55555555);
	}
#endif

	if (IS_IWARP(p_hwfn)) {
		rc = ecore_iwarp_stop(p_hwfn);
		if (rc != ECORE_SUCCESS) {
			ecore_ptt_release(p_hwfn, p_ptt);
			return 0;
		}
	} else {
		rc = ecore_roce_stop(p_hwfn);
		if (rc != ECORE_SUCCESS) {
			ecore_ptt_release(p_hwfn, p_ptt);
			return 0;
		}
	}

	ecore_ptt_release(p_hwfn, p_ptt);

	/* Get SPQ entry */
	OSAL_MEMSET(&init_data, 0, sizeof(init_data));
	init_data.opaque_fid = p_hwfn->hw_info.opaque_fid;
	init_data.comp_mode = ECORE_SPQ_MODE_EBLOCK;

	/* Stop RoCE */
	rc = ecore_sp_init_request(p_hwfn, &p_ent, RDMA_RAMROD_FUNC_CLOSE,
				   p_hwfn->p_rdma_info->proto, &init_data);
	if (rc != ECORE_SUCCESS)
		goto out;

	p_ramrod = &p_ent->ramrod.rdma_close_func;

	p_ramrod->num_cnqs = p_hwfn->p_rdma_info->num_cnqs;
	p_ramrod->cnq_start_offset = (u8)RESC_START(p_hwfn, ECORE_RDMA_CNQ_RAM);

	rc = ecore_spq_post(p_hwfn, p_ent, OSAL_NULL);

out:
	ecore_rdma_free(p_hwfn);

	DP_VERBOSE(p_hwfn, ECORE_MSG_RDMA, "RDMA stop done, rc = %d\n", rc);
	return rc;
}

enum _ecore_status_t ecore_rdma_add_user(void		      *rdma_cxt,
			struct ecore_rdma_add_user_out_params *out_params)
{
	struct ecore_hwfn *p_hwfn = (struct ecore_hwfn *)rdma_cxt;
	u32 dpi_start_offset;
	u32 returned_id = 0;
	enum _ecore_status_t rc;

	DP_VERBOSE(p_hwfn, ECORE_MSG_RDMA, "Adding User\n");

	/* Allocate DPI */
	OSAL_SPIN_LOCK(&p_hwfn->p_rdma_info->lock);
	rc = ecore_rdma_bmap_alloc_id(p_hwfn, &p_hwfn->p_rdma_info->dpi_map,
				      &returned_id);
	OSAL_SPIN_UNLOCK(&p_hwfn->p_rdma_info->lock);

	if (rc != ECORE_SUCCESS)
		DP_NOTICE(p_hwfn, false, "Failed in allocating dpi\n");

	out_params->dpi = (u16)returned_id;

	/* Calculate the corresponding DPI address */
	dpi_start_offset = p_hwfn->dpi_start_offset;

	out_params->dpi_addr = (u64)(osal_int_ptr_t)((u8 OSAL_IOMEM*)p_hwfn->doorbells +
						     dpi_start_offset +
						     ((out_params->dpi) * p_hwfn->dpi_size));

	out_params->dpi_phys_addr = p_hwfn->db_phys_addr + dpi_start_offset +
				    out_params->dpi * p_hwfn->dpi_size;

	out_params->dpi_size = p_hwfn->dpi_size;
	out_params->wid_count = p_hwfn->wid_count;

	DP_VERBOSE(p_hwfn, ECORE_MSG_RDMA, "Adding user - done, rc = %d\n", rc);
	return rc;
}

struct ecore_rdma_port *ecore_rdma_query_port(void	*rdma_cxt)
{
	struct ecore_hwfn *p_hwfn = (struct ecore_hwfn *)rdma_cxt;
	struct ecore_rdma_port *p_port = p_hwfn->p_rdma_info->port;
	struct ecore_mcp_link_state *p_link_output;

	DP_VERBOSE(p_hwfn, ECORE_MSG_RDMA, "RDMA Query port\n");

	/* The link state is saved only for the leading hwfn */
	p_link_output =
		&ECORE_LEADING_HWFN(p_hwfn->p_dev)->mcp_info->link_output;

	/* Link may have changed... */
	p_port->port_state = p_link_output->link_up ? ECORE_RDMA_PORT_UP
						    : ECORE_RDMA_PORT_DOWN;

	p_port->link_speed = p_link_output->speed;

	p_port->max_msg_size = RDMA_MAX_DATA_SIZE_IN_WQE;

	return p_port;
}

struct ecore_rdma_device *ecore_rdma_query_device(void	*rdma_cxt)
{
	struct ecore_hwfn *p_hwfn = (struct ecore_hwfn *)rdma_cxt;

	DP_VERBOSE(p_hwfn, ECORE_MSG_RDMA, "Query device\n");

	/* Return struct with device parameters */
	return p_hwfn->p_rdma_info->dev;
}

void ecore_rdma_free_tid(void	*rdma_cxt,
			 u32	itid)
{
	struct ecore_hwfn *p_hwfn = (struct ecore_hwfn *)rdma_cxt;

	DP_VERBOSE(p_hwfn, ECORE_MSG_RDMA, "itid = %08x\n", itid);

	OSAL_SPIN_LOCK(&p_hwfn->p_rdma_info->lock);
	ecore_bmap_release_id(p_hwfn,
			      &p_hwfn->p_rdma_info->tid_map,
			      itid);
	OSAL_SPIN_UNLOCK(&p_hwfn->p_rdma_info->lock);
}

void ecore_rdma_cnq_prod_update(void *rdma_cxt, u8 qz_offset, u16 prod)
{
	struct ecore_hwfn *p_hwfn;
	u16 qz_num;
	u32 addr;

	p_hwfn = (struct ecore_hwfn *)rdma_cxt;

	if (qz_offset > p_hwfn->p_rdma_info->max_queue_zones) {
		DP_NOTICE(p_hwfn, false,
			  "queue zone offset %d is too large (max is %d)\n",
			  qz_offset, p_hwfn->p_rdma_info->max_queue_zones);
		return;
	}

	qz_num = p_hwfn->p_rdma_info->queue_zone_base + qz_offset;
	addr = GTT_BAR0_MAP_REG_USDM_RAM +
	       USTORM_COMMON_QUEUE_CONS_OFFSET(qz_num);

	REG_WR16(p_hwfn, addr, prod);

	/* keep prod updates ordered */
	OSAL_WMB(p_hwfn->p_dev);
}

enum _ecore_status_t ecore_rdma_alloc_pd(void	*rdma_cxt,
					 u16	*pd)
{
	struct ecore_hwfn *p_hwfn = (struct ecore_hwfn *)rdma_cxt;
	u32                  returned_id;
	enum _ecore_status_t rc;

	DP_VERBOSE(p_hwfn, ECORE_MSG_RDMA, "Alloc PD\n");

	/* Allocates an unused protection domain */
	OSAL_SPIN_LOCK(&p_hwfn->p_rdma_info->lock);
	rc = ecore_rdma_bmap_alloc_id(p_hwfn,
				      &p_hwfn->p_rdma_info->pd_map,
				      &returned_id);
	OSAL_SPIN_UNLOCK(&p_hwfn->p_rdma_info->lock);
	if (rc != ECORE_SUCCESS)
		DP_NOTICE(p_hwfn, false, "Failed in allocating pd id\n");

	*pd = (u16)returned_id;

	DP_VERBOSE(p_hwfn, ECORE_MSG_RDMA, "Alloc PD - done, rc = %d\n", rc);
	return rc;
}

void ecore_rdma_free_pd(void	*rdma_cxt,
			u16	pd)
{
	struct ecore_hwfn *p_hwfn = (struct ecore_hwfn *)rdma_cxt;

	DP_VERBOSE(p_hwfn, ECORE_MSG_RDMA, "pd = %08x\n", pd);

	/* Returns a previously allocated protection domain for reuse */
	OSAL_SPIN_LOCK(&p_hwfn->p_rdma_info->lock);
	ecore_bmap_release_id(p_hwfn, &p_hwfn->p_rdma_info->pd_map, pd);
	OSAL_SPIN_UNLOCK(&p_hwfn->p_rdma_info->lock);
}

enum _ecore_status_t ecore_rdma_alloc_xrcd(void	*rdma_cxt,
					   u16	*xrcd_id)
{
	struct ecore_hwfn *p_hwfn = (struct ecore_hwfn *)rdma_cxt;
	u32                  returned_id;
	enum _ecore_status_t rc;

	DP_VERBOSE(p_hwfn, ECORE_MSG_RDMA, "Alloc XRCD\n");

	/* Allocates an unused XRC domain */
	OSAL_SPIN_LOCK(&p_hwfn->p_rdma_info->lock);
	rc = ecore_rdma_bmap_alloc_id(p_hwfn,
				      &p_hwfn->p_rdma_info->xrcd_map,
				      &returned_id);
	OSAL_SPIN_UNLOCK(&p_hwfn->p_rdma_info->lock);
	if (rc != ECORE_SUCCESS)
		DP_NOTICE(p_hwfn, false, "Failed in allocating xrcd id\n");

	*xrcd_id = (u16)returned_id;

	DP_VERBOSE(p_hwfn, ECORE_MSG_RDMA, "Alloc XRCD - done, rc = %d\n", rc);
	return rc;
}

void ecore_rdma_free_xrcd(void	*rdma_cxt,
			  u16	xrcd_id)
{
	struct ecore_hwfn *p_hwfn = (struct ecore_hwfn *)rdma_cxt;

	DP_VERBOSE(p_hwfn, ECORE_MSG_RDMA, "xrcd_id = %08x\n", xrcd_id);

	/* Returns a previously allocated protection domain for reuse */
	OSAL_SPIN_LOCK(&p_hwfn->p_rdma_info->lock);
	ecore_bmap_release_id(p_hwfn, &p_hwfn->p_rdma_info->xrcd_map, xrcd_id);
	OSAL_SPIN_UNLOCK(&p_hwfn->p_rdma_info->lock);
}

static enum ecore_rdma_toggle_bit
ecore_rdma_toggle_bit_create_resize_cq(struct ecore_hwfn *p_hwfn,
				       u16 icid)
{
	struct ecore_rdma_info *p_info = p_hwfn->p_rdma_info;
	enum ecore_rdma_toggle_bit toggle_bit;
	u32 bmap_id;

	DP_VERBOSE(p_hwfn, ECORE_MSG_RDMA, "icid = %08x\n", icid);

	/* the function toggle the bit that is related to a given icid
	 * and returns the new toggle bit's value
	 */
	bmap_id = icid - ecore_cxt_get_proto_cid_start(p_hwfn, p_info->proto);

	OSAL_SPIN_LOCK(&p_info->lock);
	toggle_bit = !OSAL_TEST_AND_FLIP_BIT(bmap_id, p_info->toggle_bits.bitmap);
	OSAL_SPIN_UNLOCK(&p_info->lock);

	DP_VERBOSE(p_hwfn, ECORE_MSG_RDMA, "ECORE_RDMA_TOGGLE_BIT_= %d\n",
		   toggle_bit);

	return toggle_bit;
}

enum _ecore_status_t ecore_rdma_create_cq(void			      *rdma_cxt,
				struct ecore_rdma_create_cq_in_params *params,
				u16                                   *icid)
{
	struct ecore_hwfn *p_hwfn = (struct ecore_hwfn *)rdma_cxt;
	struct ecore_rdma_info *p_info = p_hwfn->p_rdma_info;
	struct rdma_create_cq_ramrod_data	*p_ramrod;
	enum ecore_rdma_toggle_bit		toggle_bit;
	struct ecore_sp_init_data		init_data;
	struct ecore_spq_entry			*p_ent;
	enum _ecore_status_t			rc;
	u32					returned_id;

	DP_VERBOSE(p_hwfn, ECORE_MSG_RDMA, "cq_handle = %08x%08x\n",
		   params->cq_handle_hi, params->cq_handle_lo);

	/* Allocate icid */
	OSAL_SPIN_LOCK(&p_info->lock);
	rc = ecore_rdma_bmap_alloc_id(p_hwfn, &p_info->cq_map, &returned_id);
	OSAL_SPIN_UNLOCK(&p_info->lock);

	if (rc != ECORE_SUCCESS)
	{
		DP_NOTICE(p_hwfn, false, "Can't create CQ, rc = %d\n", rc);
		return rc;
	}

	*icid = (u16)(returned_id +
		      ecore_cxt_get_proto_cid_start(
			      p_hwfn, p_info->proto));

	/* Check if icid requires a page allocation */
	rc = ecore_cxt_dynamic_ilt_alloc(p_hwfn, ECORE_ELEM_CXT, *icid);
	if (rc != ECORE_SUCCESS)
		goto err;

	/* Get SPQ entry */
	OSAL_MEMSET(&init_data, 0, sizeof(init_data));
	init_data.cid = *icid;
	init_data.opaque_fid = p_hwfn->hw_info.opaque_fid;
	init_data.comp_mode = ECORE_SPQ_MODE_EBLOCK;

	/* Send create CQ ramrod */
	rc = ecore_sp_init_request(p_hwfn, &p_ent,
				   RDMA_RAMROD_CREATE_CQ,
				   p_info->proto, &init_data);
	if (rc != ECORE_SUCCESS)
		goto err;

	p_ramrod = &p_ent->ramrod.rdma_create_cq;

	p_ramrod->cq_handle.hi = OSAL_CPU_TO_LE32(params->cq_handle_hi);
	p_ramrod->cq_handle.lo = OSAL_CPU_TO_LE32(params->cq_handle_lo);
	p_ramrod->dpi = OSAL_CPU_TO_LE16(params->dpi);
	p_ramrod->is_two_level_pbl = params->pbl_two_level;
	p_ramrod->max_cqes = OSAL_CPU_TO_LE32(params->cq_size);
	DMA_REGPAIR_LE(p_ramrod->pbl_addr, params->pbl_ptr);
	p_ramrod->pbl_num_pages = OSAL_CPU_TO_LE16(params->pbl_num_pages);
	p_ramrod->cnq_id = (u8)RESC_START(p_hwfn, ECORE_RDMA_CNQ_RAM)
			+ params->cnq_id;
	p_ramrod->int_timeout = params->int_timeout;
	/* INTERNAL: Two layer PBL is currently not supported, ignoring next line */
	/* INTERNAL: p_ramrod->pbl_log_page_size = params->pbl_page_size_log - 12; */

	/* toggle the bit for every resize or create cq for a given icid */
	toggle_bit = ecore_rdma_toggle_bit_create_resize_cq(p_hwfn, *icid);

	p_ramrod->toggle_bit = toggle_bit;

	rc = ecore_spq_post(p_hwfn, p_ent, OSAL_NULL);
	if (rc != ECORE_SUCCESS) {
		/* restore toggle bit */
		ecore_rdma_toggle_bit_create_resize_cq(p_hwfn, *icid);
		goto err;
	}

	DP_VERBOSE(p_hwfn, ECORE_MSG_RDMA, "Created CQ, rc = %d\n", rc);
	return rc;

err:
	/* release allocated icid */
	OSAL_SPIN_LOCK(&p_info->lock);
	ecore_bmap_release_id(p_hwfn, &p_info->cq_map, returned_id);
	OSAL_SPIN_UNLOCK(&p_info->lock);

	DP_NOTICE(p_hwfn, false, "Create CQ failed, rc = %d\n", rc);

	return rc;
}

enum _ecore_status_t ecore_rdma_destroy_cq(void			*rdma_cxt,
			struct ecore_rdma_destroy_cq_in_params	*in_params,
			struct ecore_rdma_destroy_cq_out_params	*out_params)
{
	struct ecore_hwfn *p_hwfn = (struct ecore_hwfn *)rdma_cxt;
	struct rdma_destroy_cq_output_params *p_ramrod_res;
	struct rdma_destroy_cq_ramrod_data	*p_ramrod;
	struct ecore_sp_init_data		init_data;
	struct ecore_spq_entry			*p_ent;
	dma_addr_t				ramrod_res_phys;
	enum _ecore_status_t			rc = ECORE_NOMEM;

	DP_VERBOSE(p_hwfn, ECORE_MSG_RDMA, "icid = %08x\n", in_params->icid);

	p_ramrod_res = (struct rdma_destroy_cq_output_params *)
			OSAL_DMA_ALLOC_COHERENT(p_hwfn->p_dev, &ramrod_res_phys,
				sizeof(struct rdma_destroy_cq_output_params));
	if (!p_ramrod_res)
	{
		DP_NOTICE(p_hwfn, false,
			  "ecore destroy cq failed: cannot allocate memory (ramrod)\n");
		return rc;
	}

	/* Get SPQ entry */
	OSAL_MEMSET(&init_data, 0, sizeof(init_data));
	init_data.cid =  in_params->icid;
	init_data.opaque_fid = p_hwfn->hw_info.opaque_fid;
	init_data.comp_mode = ECORE_SPQ_MODE_EBLOCK;

	/* Send destroy CQ ramrod */
	rc = ecore_sp_init_request(p_hwfn, &p_ent,
				   RDMA_RAMROD_DESTROY_CQ,
				   p_hwfn->p_rdma_info->proto, &init_data);
	if (rc != ECORE_SUCCESS)
		goto err;

	p_ramrod = &p_ent->ramrod.rdma_destroy_cq;
	DMA_REGPAIR_LE(p_ramrod->output_params_addr, ramrod_res_phys);

	rc = ecore_spq_post(p_hwfn, p_ent, OSAL_NULL);
	if (rc != ECORE_SUCCESS)
		goto err;

	out_params->num_cq_notif =
		OSAL_LE16_TO_CPU(p_ramrod_res->cnq_num);

	OSAL_DMA_FREE_COHERENT(p_hwfn->p_dev, p_ramrod_res, ramrod_res_phys,
			       sizeof(struct rdma_destroy_cq_output_params));

	/* Free icid */
	OSAL_SPIN_LOCK(&p_hwfn->p_rdma_info->lock);

	ecore_bmap_release_id(p_hwfn,
			      &p_hwfn->p_rdma_info->cq_map,
		(in_params->icid - ecore_cxt_get_proto_cid_start(
			p_hwfn, p_hwfn->p_rdma_info->proto)));

	OSAL_SPIN_UNLOCK(&p_hwfn->p_rdma_info->lock);

	DP_VERBOSE(p_hwfn, ECORE_MSG_RDMA, "Destroyed CQ, rc = %d\n", rc);
	return rc;

err:
	OSAL_DMA_FREE_COHERENT(p_hwfn->p_dev, p_ramrod_res, ramrod_res_phys,
			       sizeof(struct rdma_destroy_cq_output_params));

	return rc;
}

void ecore_rdma_set_fw_mac(u16 *p_fw_mac, u8 *p_ecore_mac)
{
	p_fw_mac[0] = OSAL_CPU_TO_LE16((p_ecore_mac[0] << 8) + p_ecore_mac[1]);
	p_fw_mac[1] = OSAL_CPU_TO_LE16((p_ecore_mac[2] << 8) + p_ecore_mac[3]);
	p_fw_mac[2] = OSAL_CPU_TO_LE16((p_ecore_mac[4] << 8) + p_ecore_mac[5]);
}


enum _ecore_status_t ecore_rdma_query_qp(void			*rdma_cxt,
			struct ecore_rdma_qp			*qp,
			struct ecore_rdma_query_qp_out_params	*out_params)

{
	struct ecore_hwfn *p_hwfn = (struct ecore_hwfn *)rdma_cxt;
	enum _ecore_status_t rc = ECORE_SUCCESS;

	DP_VERBOSE(p_hwfn, ECORE_MSG_RDMA, "icid = %08x\n", qp->icid);

	/* The following fields are filled in from qp and not FW as they can't
	 * be modified by FW
	 */
	out_params->mtu = qp->mtu;
	out_params->dest_qp = qp->dest_qp;
	out_params->incoming_atomic_en = qp->incoming_atomic_en;
	out_params->e2e_flow_control_en = qp->e2e_flow_control_en;
	out_params->incoming_rdma_read_en = qp->incoming_rdma_read_en;
	out_params->incoming_rdma_write_en = qp->incoming_rdma_write_en;
	out_params->dgid = qp->dgid;
	out_params->flow_label = qp->flow_label;
	out_params->hop_limit_ttl = qp->hop_limit_ttl;
	out_params->traffic_class_tos = qp->traffic_class_tos;
	out_params->timeout = qp->ack_timeout;
	out_params->rnr_retry = qp->rnr_retry_cnt;
	out_params->retry_cnt = qp->retry_cnt;
	out_params->min_rnr_nak_timer = qp->min_rnr_nak_timer;
	out_params->pkey_index = 0;
	out_params->max_rd_atomic = qp->max_rd_atomic_req;
	out_params->max_dest_rd_atomic = qp->max_rd_atomic_resp;
	out_params->sqd_async = qp->sqd_async;

	if (IS_IWARP(p_hwfn))
		rc = ecore_iwarp_query_qp(qp, out_params);
	else
		rc = ecore_roce_query_qp(p_hwfn, qp, out_params);

	DP_VERBOSE(p_hwfn, ECORE_MSG_RDMA, "Query QP, rc = %d\n", rc);
	return rc;
}


enum _ecore_status_t ecore_rdma_destroy_qp(void *rdma_cxt,
					   struct ecore_rdma_qp *qp,
					   struct ecore_rdma_destroy_qp_out_params *out_params)
{
	struct ecore_hwfn *p_hwfn = (struct ecore_hwfn *)rdma_cxt;
	enum _ecore_status_t rc = ECORE_SUCCESS;

	if (!rdma_cxt || !qp) {
		DP_ERR(p_hwfn,
		       "ecore rdma destroy qp failed: invalid NULL input. rdma_cxt=%p, qp=%p\n",
		       rdma_cxt, qp);
		return ECORE_INVAL;
	}

	DP_VERBOSE(p_hwfn, ECORE_MSG_RDMA, "QP(0x%x)\n", qp->icid);

	if (IS_IWARP(p_hwfn))
		rc = ecore_iwarp_destroy_qp(p_hwfn, qp);
	else
		rc = ecore_roce_destroy_qp(p_hwfn, qp, out_params);

	/* free qp params struct */
	OSAL_FREE(p_hwfn->p_dev, qp);

	return rc;
}


struct ecore_rdma_qp *ecore_rdma_create_qp(void			*rdma_cxt,
			struct ecore_rdma_create_qp_in_params	*in_params,
			struct ecore_rdma_create_qp_out_params	*out_params)
{
	struct ecore_hwfn *p_hwfn = (struct ecore_hwfn *)rdma_cxt;
	struct ecore_rdma_qp *qp;
	u8 max_stats_queues;
	enum _ecore_status_t rc = 0;

	if (!rdma_cxt || !in_params || !out_params || !p_hwfn->p_rdma_info) {
		DP_ERR(p_hwfn->p_dev,
		       "ecore roce create qp failed due to NULL entry (rdma_cxt=%p, in=%p, out=%p, roce_info=?\n",
		       rdma_cxt,
		       in_params,
		       out_params);
		return OSAL_NULL;
	}

	/* Some sanity checks... */
	max_stats_queues = p_hwfn->p_rdma_info->dev->max_stats_queues;
	if (in_params->stats_queue >= max_stats_queues) {
		DP_ERR(p_hwfn->p_dev,
		       "ecore rdma create qp failed due to invalid statistics queue %d. maximum is %d\n",
		       in_params->stats_queue, max_stats_queues);
		return OSAL_NULL;
	}

	if (IS_IWARP(p_hwfn)) {
		if (in_params->sq_num_pages*sizeof(struct regpair) >
		    IWARP_SHARED_QUEUE_PAGE_SQ_PBL_MAX_SIZE) {
			DP_NOTICE(p_hwfn->p_dev, true, "Sq num pages: %d exceeds maximum\n",
				  in_params->sq_num_pages);
			return OSAL_NULL;
		}
		if (in_params->rq_num_pages*sizeof(struct regpair) >
		    IWARP_SHARED_QUEUE_PAGE_RQ_PBL_MAX_SIZE) {
			DP_NOTICE(p_hwfn->p_dev, true,
				  "Rq num pages: %d exceeds maximum\n",
				  in_params->rq_num_pages);
			return OSAL_NULL;
		}
	}

	qp = OSAL_ZALLOC(p_hwfn->p_dev,
			 GFP_KERNEL,
			 sizeof(struct ecore_rdma_qp));
	if (!qp)
	{
		DP_NOTICE(p_hwfn, false, "Failed to allocate ecore_rdma_qp\n");
		return OSAL_NULL;
	}

	qp->cur_state = ECORE_ROCE_QP_STATE_RESET;
#ifdef CONFIG_ECORE_IWARP
	qp->iwarp_state = ECORE_IWARP_QP_STATE_IDLE;
#endif
	qp->qp_handle.hi = OSAL_CPU_TO_LE32(in_params->qp_handle_hi);
	qp->qp_handle.lo = OSAL_CPU_TO_LE32(in_params->qp_handle_lo);
	qp->qp_handle_async.hi = OSAL_CPU_TO_LE32(in_params->qp_handle_async_hi);
	qp->qp_handle_async.lo = OSAL_CPU_TO_LE32(in_params->qp_handle_async_lo);
	qp->use_srq = in_params->use_srq;
	qp->signal_all = in_params->signal_all;
	qp->fmr_and_reserved_lkey = in_params->fmr_and_reserved_lkey;
	qp->pd = in_params->pd;
	qp->dpi = in_params->dpi;
	qp->sq_cq_id = in_params->sq_cq_id;
	qp->sq_num_pages = in_params->sq_num_pages;
	qp->sq_pbl_ptr = in_params->sq_pbl_ptr;
	qp->rq_cq_id = in_params->rq_cq_id;
	qp->rq_num_pages = in_params->rq_num_pages;
	qp->rq_pbl_ptr = in_params->rq_pbl_ptr;
	qp->srq_id = in_params->srq_id;
	qp->req_offloaded = false;
	qp->resp_offloaded = false;
	/* e2e_flow_control cannot be done in case of S-RQ.
	 * Refer to 9.7.7.2 End-to-End Flow Control section of IB spec
	 */
	qp->e2e_flow_control_en = qp->use_srq ? false : true;
	qp->stats_queue = in_params->stats_queue;
	qp->qp_type = in_params->qp_type;
	qp->xrcd_id = in_params->xrcd_id;

	if (IS_IWARP(p_hwfn)) {
		rc = ecore_iwarp_create_qp(p_hwfn, qp, out_params);
		qp->qpid = qp->icid;
	} else {
		rc = ecore_roce_alloc_qp_idx(p_hwfn, &qp->qp_idx);
		qp->icid = ECORE_ROCE_QP_TO_ICID(qp->qp_idx);
		qp->qpid = ((0xFF << 16) | qp->icid);
	}

	if (rc != ECORE_SUCCESS) {
		OSAL_FREE(p_hwfn->p_dev, qp);
		return OSAL_NULL;
	}

	out_params->icid = qp->icid;
	out_params->qp_id = qp->qpid;

	/* INTERNAL: max_sq_sges future use only*/

	DP_VERBOSE(p_hwfn, ECORE_MSG_RDMA, "Create QP, rc = %d\n", rc);
	return qp;
}

#define ECORE_RDMA_ECN_SHIFT 0
#define ECORE_RDMA_ECN_MASK 0x3
#define ECORE_RDMA_DSCP_SHIFT 2
#define ECORE_RDMA_DSCP_MASK 0x3f
#define ECORE_RDMA_VLAN_PRIO_SHIFT 13
#define ECORE_RDMA_VLAN_PRIO_MASK 0x7
enum _ecore_status_t ecore_rdma_modify_qp(
	void *rdma_cxt,
	struct ecore_rdma_qp *qp,
	struct ecore_rdma_modify_qp_in_params *params)
{
	struct ecore_hwfn *p_hwfn = (struct ecore_hwfn *)rdma_cxt;
	enum ecore_roce_qp_state prev_state;
	enum _ecore_status_t     rc = ECORE_SUCCESS;

	if (GET_FIELD(params->modify_flags,
		      ECORE_RDMA_MODIFY_QP_VALID_RDMA_OPS_EN))
	{
		qp->incoming_rdma_read_en = params->incoming_rdma_read_en;
		qp->incoming_rdma_write_en = params->incoming_rdma_write_en;
		qp->incoming_atomic_en = params->incoming_atomic_en;
	}

	/* Update QP structure with the updated values */
	if (GET_FIELD(params->modify_flags,
		      ECORE_ROCE_MODIFY_QP_VALID_ROCE_MODE))
	{
		qp->roce_mode = params->roce_mode;
	}
	if (GET_FIELD(params->modify_flags, ECORE_ROCE_MODIFY_QP_VALID_PKEY))
	{
		qp->pkey = params->pkey;
	}
	if (GET_FIELD(params->modify_flags,
		      ECORE_ROCE_MODIFY_QP_VALID_E2E_FLOW_CONTROL_EN))
	{
		qp->e2e_flow_control_en = params->e2e_flow_control_en;
	}
	if (GET_FIELD(params->modify_flags,
		      ECORE_ROCE_MODIFY_QP_VALID_DEST_QP))
	{
		qp->dest_qp = params->dest_qp;
	}
	if (GET_FIELD(params->modify_flags,
		      ECORE_ROCE_MODIFY_QP_VALID_ADDRESS_VECTOR))
	{
		/* Indicates that the following parameters have changed:
		 * Traffic class, flow label, hop limit, source GID,
		 * destination GID, loopback indicator
		 */
		qp->flow_label = params->flow_label;
		qp->hop_limit_ttl = params->hop_limit_ttl;

		qp->sgid = params->sgid;
		qp->dgid = params->dgid;
		qp->udp_src_port = params->udp_src_port;
		qp->vlan_id = params->vlan_id;
		qp->traffic_class_tos = params->traffic_class_tos;

		/* apply global override values */
		if (p_hwfn->p_rdma_info->glob_cfg.vlan_pri_en)
			SET_FIELD(qp->vlan_id, ECORE_RDMA_VLAN_PRIO,
				  p_hwfn->p_rdma_info->glob_cfg.vlan_pri);

		if (p_hwfn->p_rdma_info->glob_cfg.ecn_en)
			SET_FIELD(qp->traffic_class_tos, ECORE_RDMA_ECN,
				  p_hwfn->p_rdma_info->glob_cfg.ecn);

		if (p_hwfn->p_rdma_info->glob_cfg.dscp_en)
			SET_FIELD(qp->traffic_class_tos, ECORE_RDMA_DSCP,
				  p_hwfn->p_rdma_info->glob_cfg.dscp);

		qp->mtu = params->mtu;

		OSAL_MEMCPY((u8 *)&qp->remote_mac_addr[0],
			    (u8 *)&params->remote_mac_addr[0], ETH_ALEN);
		if (params->use_local_mac) {
			OSAL_MEMCPY((u8 *)&qp->local_mac_addr[0],
				    (u8 *)&params->local_mac_addr[0],
				    ETH_ALEN);
		} else {
			OSAL_MEMCPY((u8 *)&qp->local_mac_addr[0],
				    (u8 *)&p_hwfn->hw_info.hw_mac_addr,
				    ETH_ALEN);
		}
	}
	if (GET_FIELD(params->modify_flags, ECORE_ROCE_MODIFY_QP_VALID_RQ_PSN))
	{
		qp->rq_psn = params->rq_psn;
	}
	if (GET_FIELD(params->modify_flags, ECORE_ROCE_MODIFY_QP_VALID_SQ_PSN))
	{
		qp->sq_psn = params->sq_psn;
	}
	if (GET_FIELD(params->modify_flags,
		      ECORE_RDMA_MODIFY_QP_VALID_MAX_RD_ATOMIC_REQ))
	{
		qp->max_rd_atomic_req = params->max_rd_atomic_req;
	}
	if (GET_FIELD(params->modify_flags,
		      ECORE_RDMA_MODIFY_QP_VALID_MAX_RD_ATOMIC_RESP))
	{
		qp->max_rd_atomic_resp = params->max_rd_atomic_resp;
	}
	if (GET_FIELD(params->modify_flags,
		      ECORE_ROCE_MODIFY_QP_VALID_ACK_TIMEOUT))
	{
		qp->ack_timeout = params->ack_timeout;
	}
	if (GET_FIELD(params->modify_flags,
		      ECORE_ROCE_MODIFY_QP_VALID_RETRY_CNT))
	{
		qp->retry_cnt = params->retry_cnt;
	}
	if (GET_FIELD(params->modify_flags,
		      ECORE_ROCE_MODIFY_QP_VALID_RNR_RETRY_CNT))
	{
		qp->rnr_retry_cnt = params->rnr_retry_cnt;
	}
	if (GET_FIELD(params->modify_flags,
		      ECORE_ROCE_MODIFY_QP_VALID_MIN_RNR_NAK_TIMER))
	{
		qp->min_rnr_nak_timer = params->min_rnr_nak_timer;
	}

	qp->sqd_async = params->sqd_async;

	prev_state = qp->cur_state;
	if (GET_FIELD(params->modify_flags,
		      ECORE_RDMA_MODIFY_QP_VALID_NEW_STATE))
	{
		qp->cur_state = params->new_state;
		DP_VERBOSE(p_hwfn, ECORE_MSG_RDMA, "qp->cur_state=%d\n",
			   qp->cur_state);
	}

	if (qp->qp_type == ECORE_RDMA_QP_TYPE_XRC_INI) {
		qp->has_req = 1;
	} else if (qp->qp_type == ECORE_RDMA_QP_TYPE_XRC_TGT)
	{
		qp->has_resp = 1;
	} else {
		qp->has_req = 1;
		qp->has_resp = 1;
	}

	if (IS_IWARP(p_hwfn)) {
		enum ecore_iwarp_qp_state new_state =
			ecore_roce2iwarp_state(qp->cur_state);

		rc = ecore_iwarp_modify_qp(p_hwfn, qp, new_state, 0);
	} else {
		rc = ecore_roce_modify_qp(p_hwfn, qp, prev_state, params);
	}

	DP_VERBOSE(p_hwfn, ECORE_MSG_RDMA, "Modify QP, rc = %d\n", rc);
	return rc;
}

enum _ecore_status_t ecore_rdma_register_tid(void		 *rdma_cxt,
			struct ecore_rdma_register_tid_in_params *params)
{
	struct ecore_hwfn *p_hwfn = (struct ecore_hwfn *)rdma_cxt;
	struct rdma_register_tid_ramrod_data *p_ramrod;
	struct ecore_sp_init_data	     init_data;
	struct ecore_spq_entry               *p_ent;
	enum rdma_tid_type                   tid_type;
	u8                                   fw_return_code;
	enum _ecore_status_t                 rc;

	DP_VERBOSE(p_hwfn, ECORE_MSG_RDMA, "itid = %08x\n", params->itid);

	/* Get SPQ entry */
	OSAL_MEMSET(&init_data, 0, sizeof(init_data));
	init_data.opaque_fid = p_hwfn->hw_info.opaque_fid;
	init_data.comp_mode = ECORE_SPQ_MODE_EBLOCK;

	rc = ecore_sp_init_request(p_hwfn, &p_ent, RDMA_RAMROD_REGISTER_MR,
				   p_hwfn->p_rdma_info->proto, &init_data);
	if (rc != ECORE_SUCCESS) {
		DP_VERBOSE(p_hwfn, ECORE_MSG_RDMA, "rc = %d\n", rc);
		return rc;
	}

	if (p_hwfn->p_rdma_info->last_tid < params->itid) {
		p_hwfn->p_rdma_info->last_tid = params->itid;
	}

	p_ramrod = &p_ent->ramrod.rdma_register_tid;

	p_ramrod->flags = 0;
	SET_FIELD(p_ramrod->flags,
		  RDMA_REGISTER_TID_RAMROD_DATA_TWO_LEVEL_PBL,
		  params->pbl_two_level);

	SET_FIELD(p_ramrod->flags,
		  RDMA_REGISTER_TID_RAMROD_DATA_ZERO_BASED,
		  params->zbva);

	SET_FIELD(p_ramrod->flags,
		  RDMA_REGISTER_TID_RAMROD_DATA_PHY_MR,
		  params->phy_mr);

	/* Don't initialize D/C field, as it may override other bits. */
	if (!(params->tid_type == ECORE_RDMA_TID_FMR) &&
	    !(params->dma_mr))
		SET_FIELD(p_ramrod->flags,
			  RDMA_REGISTER_TID_RAMROD_DATA_PAGE_SIZE_LOG,
			  params->page_size_log - 12);

	SET_FIELD(p_ramrod->flags,
		  RDMA_REGISTER_TID_RAMROD_DATA_REMOTE_READ,
		  params->remote_read);

	SET_FIELD(p_ramrod->flags,
		  RDMA_REGISTER_TID_RAMROD_DATA_REMOTE_WRITE,
		  params->remote_write);

	SET_FIELD(p_ramrod->flags,
		  RDMA_REGISTER_TID_RAMROD_DATA_REMOTE_ATOMIC,
		  params->remote_atomic);

	SET_FIELD(p_ramrod->flags,
		  RDMA_REGISTER_TID_RAMROD_DATA_LOCAL_WRITE,
		  params->local_write);

	SET_FIELD(p_ramrod->flags,
		  RDMA_REGISTER_TID_RAMROD_DATA_LOCAL_READ,
		  params->local_read);

	SET_FIELD(p_ramrod->flags,
		  RDMA_REGISTER_TID_RAMROD_DATA_ENABLE_MW_BIND,
		  params->mw_bind);

	SET_FIELD(p_ramrod->flags1,
		  RDMA_REGISTER_TID_RAMROD_DATA_PBL_PAGE_SIZE_LOG,
		  params->pbl_page_size_log - 12);

	SET_FIELD(p_ramrod->flags2,
		  RDMA_REGISTER_TID_RAMROD_DATA_DMA_MR,
		  params->dma_mr);

	switch (params->tid_type)
	{
	case ECORE_RDMA_TID_REGISTERED_MR:
		tid_type = RDMA_TID_REGISTERED_MR;
		break;
	case ECORE_RDMA_TID_FMR:
		tid_type = RDMA_TID_FMR;
		break;
	case ECORE_RDMA_TID_MW_TYPE1:
		tid_type = RDMA_TID_MW_TYPE1;
		break;
	case ECORE_RDMA_TID_MW_TYPE2A:
		tid_type = RDMA_TID_MW_TYPE2A;
		break;
	default:
		rc = ECORE_INVAL;
		DP_VERBOSE(p_hwfn, ECORE_MSG_RDMA, "rc = %d\n", rc);
		return rc;
	}
	SET_FIELD(p_ramrod->flags1,
		  RDMA_REGISTER_TID_RAMROD_DATA_TID_TYPE,
		  tid_type);

	p_ramrod->itid = OSAL_CPU_TO_LE32(params->itid);
	p_ramrod->key = params->key;
	p_ramrod->pd = OSAL_CPU_TO_LE16(params->pd);
	p_ramrod->length_hi = (u8)(params->length >> 32);
	p_ramrod->length_lo = DMA_LO_LE(params->length);
	if (params->zbva)
	{
		/* Lower 32 bits of the registered MR address.
		 * In case of zero based MR, will hold FBO
		 */
		p_ramrod->va.hi = 0;
		p_ramrod->va.lo = OSAL_CPU_TO_LE32(params->fbo);
	} else {
		DMA_REGPAIR_LE(p_ramrod->va, params->vaddr);
	}
	DMA_REGPAIR_LE(p_ramrod->pbl_base, params->pbl_ptr);

	/* DIF */
	if (params->dif_enabled) {
		SET_FIELD(p_ramrod->flags2,
			  RDMA_REGISTER_TID_RAMROD_DATA_DIF_ON_HOST_FLG, 1);
		DMA_REGPAIR_LE(p_ramrod->dif_error_addr,
			       params->dif_error_addr);
		DMA_REGPAIR_LE(p_ramrod->dif_runt_addr, params->dif_runt_addr);
	}

	rc = ecore_spq_post(p_hwfn, p_ent, &fw_return_code);
	if (rc)
		return rc;

	if (fw_return_code != RDMA_RETURN_OK) {
		DP_NOTICE(p_hwfn, true, "fw_return_code = %d\n", fw_return_code);
		return ECORE_UNKNOWN_ERROR;
	}

	DP_VERBOSE(p_hwfn, ECORE_MSG_RDMA, "Register TID, rc = %d\n", rc);
	return rc;
}

static OSAL_INLINE int ecore_rdma_send_deregister_tid_ramrod(
		struct ecore_hwfn *p_hwfn,
		u32 itid,
		u8 *fw_return_code)
{
	struct ecore_sp_init_data              init_data;
	struct rdma_deregister_tid_ramrod_data *p_ramrod;
	struct ecore_spq_entry                 *p_ent;
	enum _ecore_status_t                   rc;

	/* Get SPQ entry */
	OSAL_MEMSET(&init_data, 0, sizeof(init_data));
	init_data.opaque_fid = p_hwfn->hw_info.opaque_fid;
	init_data.comp_mode = ECORE_SPQ_MODE_EBLOCK;

	rc = ecore_sp_init_request(p_hwfn, &p_ent,
				   RDMA_RAMROD_DEREGISTER_MR,
				   p_hwfn->p_rdma_info->proto, &init_data);
	if (rc != ECORE_SUCCESS) {
		DP_VERBOSE(p_hwfn, ECORE_MSG_RDMA, "rc = %d\n", rc);
		return rc;
	}

	p_ramrod = &p_ent->ramrod.rdma_deregister_tid;
	p_ramrod->itid = OSAL_CPU_TO_LE32(itid);

	rc = ecore_spq_post(p_hwfn, p_ent, fw_return_code);
	if (rc != ECORE_SUCCESS)
	{
		DP_VERBOSE(p_hwfn, ECORE_MSG_RDMA, "rc = %d\n", rc);
		return rc;
	}

	return rc;
}

#define ECORE_RDMA_DEREGISTER_TIMEOUT_MSEC	(1)

enum _ecore_status_t ecore_rdma_deregister_tid(void	*rdma_cxt,
					       u32	itid)
{
	enum _ecore_status_t                   rc;
	u8                                     fw_ret_code;
	struct ecore_ptt                       *p_ptt;
	struct ecore_hwfn *p_hwfn = (struct ecore_hwfn *)rdma_cxt;

	/* First attempt */
	rc = ecore_rdma_send_deregister_tid_ramrod(p_hwfn, itid, &fw_ret_code);
	if (rc != ECORE_SUCCESS)
		return rc;

	if (fw_ret_code != RDMA_RETURN_NIG_DRAIN_REQ)
		goto done;

	/* Second attempt, after 1msec, if device still holds data.
	 * This can occur since 'destroy QP' returns to the caller rather fast.
	 * The synchronous part of it returns after freeing a few of the
	 * resources but not all of them, allowing the consumer to continue its
	 * flow. All of the resources will be freed after the asynchronous part
	 * of the destroy QP is complete.
	 */
	OSAL_MSLEEP(ECORE_RDMA_DEREGISTER_TIMEOUT_MSEC);
	rc = ecore_rdma_send_deregister_tid_ramrod(p_hwfn, itid, &fw_ret_code);
	if (rc != ECORE_SUCCESS)
		return rc;

	if (fw_ret_code != RDMA_RETURN_NIG_DRAIN_REQ)
		goto done;

	/* Third and last attempt, perform NIG drain and resend the ramrod */
	p_ptt = ecore_ptt_acquire(p_hwfn);
	if (!p_ptt)
		return ECORE_TIMEOUT;

	rc = ecore_mcp_drain(p_hwfn, p_ptt);
	if (rc != ECORE_SUCCESS) {
		ecore_ptt_release(p_hwfn, p_ptt);
		return rc;
	}

	ecore_ptt_release(p_hwfn, p_ptt);

	rc = ecore_rdma_send_deregister_tid_ramrod(p_hwfn, itid, &fw_ret_code);
	if (rc != ECORE_SUCCESS)
		return rc;

done:
	if (fw_ret_code == RDMA_RETURN_OK) {
		DP_VERBOSE(p_hwfn, ECORE_MSG_RDMA, "De-registered itid=%d\n",
			   itid);
		return ECORE_SUCCESS;
	} else if (fw_ret_code == RDMA_RETURN_DEREGISTER_MR_BAD_STATE_ERR) {
		/* INTERNAL: This error is returned in case trying to deregister
		 * a MR that is not allocated. We define "allocated" as either:
		 * 1. Registered.
		 * 2. This is an FMR MR type, which is not currently registered
		 *    but can accept FMR WQEs on SQ.
		 */
		DP_NOTICE(p_hwfn, false, "itid=%d, fw_ret_code=%d\n", itid,
			  fw_ret_code);
		return ECORE_INVAL;
	} else { /* fw_ret_code == RDMA_RETURN_NIG_DRAIN_REQ */
		DP_NOTICE(p_hwfn, true,
			  "deregister failed after three attempts. itid=%d, fw_ret_code=%d\n",
			  itid, fw_ret_code);
		return ECORE_UNKNOWN_ERROR;
	}
}

static struct ecore_bmap *ecore_rdma_get_srq_bmap(struct ecore_hwfn *p_hwfn, bool is_xrc)
{
	if (is_xrc)
		return &p_hwfn->p_rdma_info->xrc_srq_map;

	return &p_hwfn->p_rdma_info->srq_map;
}

u16 ecore_rdma_get_fw_srq_id(struct ecore_hwfn *p_hwfn, u16 id, bool is_xrc)
{
	if (is_xrc)
		return id;

	return id + p_hwfn->p_rdma_info->srq_id_offset;
}

enum _ecore_status_t
ecore_rdma_modify_srq(void *rdma_cxt,
		      struct ecore_rdma_modify_srq_in_params *in_params)
{
	struct ecore_hwfn *p_hwfn = (struct ecore_hwfn *)rdma_cxt;
	struct rdma_srq_modify_ramrod_data *p_ramrod;
	struct ecore_sp_init_data init_data;
	struct ecore_spq_entry *p_ent;
	u16 opaque_fid, fw_srq_id;
	enum _ecore_status_t rc;

	OSAL_MEMSET(&init_data, 0, sizeof(init_data));
	init_data.opaque_fid = p_hwfn->hw_info.opaque_fid;
	init_data.comp_mode = ECORE_SPQ_MODE_EBLOCK;
	/* Send modify SRQ ramrod */
	rc = ecore_sp_init_request(p_hwfn, &p_ent,
				   RDMA_RAMROD_MODIFY_SRQ,
				   p_hwfn->p_rdma_info->proto, &init_data);
	if (rc != ECORE_SUCCESS)
		return rc;

	p_ramrod = &p_ent->ramrod.rdma_modify_srq;

	fw_srq_id = ecore_rdma_get_fw_srq_id(p_hwfn, in_params->srq_id,
					     in_params->is_xrc);
	p_ramrod->srq_id.srq_idx = OSAL_CPU_TO_LE16(fw_srq_id);
	opaque_fid = p_hwfn->hw_info.opaque_fid;
	p_ramrod->srq_id.opaque_fid = OSAL_CPU_TO_LE16(opaque_fid);
	p_ramrod->wqe_limit = OSAL_CPU_TO_LE16(in_params->wqe_limit);

	rc = ecore_spq_post(p_hwfn, p_ent, OSAL_NULL);
	if (rc != ECORE_SUCCESS)
		return rc;

	DP_VERBOSE(p_hwfn, ECORE_MSG_RDMA, "modified SRQ id = %x, is_xrc=%u\n",
		   in_params->srq_id, in_params->is_xrc);

	return rc;
}

enum _ecore_status_t
ecore_rdma_destroy_srq(void *rdma_cxt,
		       struct ecore_rdma_destroy_srq_in_params *in_params)
{
	struct ecore_hwfn *p_hwfn = (struct ecore_hwfn *)rdma_cxt;
	struct rdma_srq_destroy_ramrod_data *p_ramrod;
	struct ecore_sp_init_data init_data;
	struct ecore_spq_entry *p_ent;
	u16 opaque_fid, fw_srq_id;
	struct ecore_bmap *bmap;
	enum _ecore_status_t rc;

	opaque_fid = p_hwfn->hw_info.opaque_fid;

	OSAL_MEMSET(&init_data, 0, sizeof(init_data));
	init_data.opaque_fid = opaque_fid;
	init_data.comp_mode = ECORE_SPQ_MODE_EBLOCK;

	/* Send destroy SRQ ramrod */
	rc = ecore_sp_init_request(p_hwfn, &p_ent,
				   RDMA_RAMROD_DESTROY_SRQ,
				   p_hwfn->p_rdma_info->proto, &init_data);
	if (rc != ECORE_SUCCESS)
		return rc;

	p_ramrod = &p_ent->ramrod.rdma_destroy_srq;

	fw_srq_id = ecore_rdma_get_fw_srq_id(p_hwfn, in_params->srq_id,
					     in_params->is_xrc);
	p_ramrod->srq_id.srq_idx = OSAL_CPU_TO_LE16(fw_srq_id);
	p_ramrod->srq_id.opaque_fid = OSAL_CPU_TO_LE16(opaque_fid);

	rc = ecore_spq_post(p_hwfn, p_ent, OSAL_NULL);

	if (rc != ECORE_SUCCESS)
		return rc;

	bmap = ecore_rdma_get_srq_bmap(p_hwfn, in_params->is_xrc);

	OSAL_SPIN_LOCK(&p_hwfn->p_rdma_info->lock);
	ecore_bmap_release_id(p_hwfn, bmap, in_params->srq_id);
	OSAL_SPIN_UNLOCK(&p_hwfn->p_rdma_info->lock);

	DP_VERBOSE(p_hwfn, ECORE_MSG_RDMA,
		   "XRC/SRQ destroyed Id = %x, is_xrc=%u\n",
		   in_params->srq_id, in_params->is_xrc);

	return rc;
}

enum _ecore_status_t
ecore_rdma_create_srq(void *rdma_cxt,
		      struct ecore_rdma_create_srq_in_params *in_params,
		      struct ecore_rdma_create_srq_out_params *out_params)
{
	struct ecore_hwfn *p_hwfn = (struct ecore_hwfn *)rdma_cxt;
	struct rdma_srq_create_ramrod_data *p_ramrod;
	struct ecore_sp_init_data init_data;
	enum ecore_cxt_elem_type elem_type;
	struct ecore_spq_entry *p_ent;
	u16 opaque_fid, fw_srq_id;
	struct ecore_bmap *bmap;
	u32 returned_id;
	enum _ecore_status_t rc;

	/* Allocate XRC/SRQ ID */
	bmap = ecore_rdma_get_srq_bmap(p_hwfn, in_params->is_xrc);
	OSAL_SPIN_LOCK(&p_hwfn->p_rdma_info->lock);
	rc = ecore_rdma_bmap_alloc_id(p_hwfn, bmap, &returned_id);
	OSAL_SPIN_UNLOCK(&p_hwfn->p_rdma_info->lock);

	if (rc != ECORE_SUCCESS) {
		DP_NOTICE(p_hwfn, false,
			  "failed to allocate xrc/srq id (is_xrc=%u)\n",
			  in_params->is_xrc);
		return rc;
	}
	/* Allocate XRC/SRQ ILT page */
	elem_type = (in_params->is_xrc) ? (ECORE_ELEM_XRC_SRQ) : (ECORE_ELEM_SRQ);
	rc = ecore_cxt_dynamic_ilt_alloc(p_hwfn, elem_type, returned_id);
	if (rc != ECORE_SUCCESS)
		goto err;

	OSAL_MEMSET(&init_data, 0, sizeof(init_data));
	opaque_fid = p_hwfn->hw_info.opaque_fid;
	init_data.opaque_fid = opaque_fid;
	init_data.comp_mode = ECORE_SPQ_MODE_EBLOCK;

	/* Create XRC/SRQ ramrod */
	rc = ecore_sp_init_request(p_hwfn, &p_ent,
				   RDMA_RAMROD_CREATE_SRQ,
				   p_hwfn->p_rdma_info->proto, &init_data);
	if (rc != ECORE_SUCCESS)
		goto err;

	p_ramrod = &p_ent->ramrod.rdma_create_srq;

	p_ramrod->pbl_base_addr.hi = DMA_HI_LE(in_params->pbl_base_addr);
	p_ramrod->pbl_base_addr.lo = DMA_LO_LE(in_params->pbl_base_addr);
	p_ramrod->pages_in_srq_pbl = OSAL_CPU_TO_LE16(in_params->num_pages);
	p_ramrod->pd_id = OSAL_CPU_TO_LE16(in_params->pd_id);
	p_ramrod->srq_id.opaque_fid = OSAL_CPU_TO_LE16(opaque_fid);
	p_ramrod->page_size = OSAL_CPU_TO_LE16(in_params->page_size);
	p_ramrod->producers_addr.hi = DMA_HI_LE(in_params->prod_pair_addr);
	p_ramrod->producers_addr.lo = DMA_LO_LE(in_params->prod_pair_addr);
	fw_srq_id = ecore_rdma_get_fw_srq_id(p_hwfn, (u16) returned_id,
					     in_params->is_xrc);
	p_ramrod->srq_id.srq_idx = OSAL_CPU_TO_LE16(fw_srq_id);

	if (in_params->is_xrc) {
		SET_FIELD(p_ramrod->flags,
			  RDMA_SRQ_CREATE_RAMROD_DATA_XRC_FLAG,
			  1);
		SET_FIELD(p_ramrod->flags,
			  RDMA_SRQ_CREATE_RAMROD_DATA_RESERVED_KEY_EN,
			  in_params->reserved_key_en);
		p_ramrod->xrc_srq_cq_cid = OSAL_CPU_TO_LE32(in_params->cq_cid);
		p_ramrod->xrc_domain = OSAL_CPU_TO_LE16(in_params->xrcd_id);
	}

	rc = ecore_spq_post(p_hwfn, p_ent, OSAL_NULL);

	if (rc != ECORE_SUCCESS)
		goto err;

	out_params->srq_id = (u16)returned_id;

	DP_VERBOSE(p_hwfn, ECORE_MSG_RDMA, "XRC/SRQ created Id = %x (is_xrc=%u)\n",
		   out_params->srq_id, in_params->is_xrc);
	return rc;

err:
	OSAL_SPIN_LOCK(&p_hwfn->p_rdma_info->lock);
	ecore_bmap_release_id(p_hwfn, bmap, returned_id);
	OSAL_SPIN_UNLOCK(&p_hwfn->p_rdma_info->lock);

	return rc;
}

bool ecore_rdma_allocated_qps(struct ecore_hwfn *p_hwfn)
{
	bool result;

	/* if rdma info has not been allocated, naturally there are no qps */
	if (!p_hwfn->p_rdma_info)
		return false;

	OSAL_SPIN_LOCK(&p_hwfn->p_rdma_info->lock);
	if (!p_hwfn->p_rdma_info->qp_map.bitmap)
		result = false;
	else
		result = !ecore_bmap_is_empty(&p_hwfn->p_rdma_info->qp_map);
	OSAL_SPIN_UNLOCK(&p_hwfn->p_rdma_info->lock);
	return result;
}

enum _ecore_status_t ecore_rdma_resize_cq(void			*rdma_cxt,
			struct ecore_rdma_resize_cq_in_params	*in_params,
			struct ecore_rdma_resize_cq_out_params	*out_params)
{
	enum _ecore_status_t			rc;
	enum ecore_rdma_toggle_bit		toggle_bit;
	struct ecore_spq_entry			*p_ent;
	struct rdma_resize_cq_ramrod_data	*p_ramrod;
	u8                                      fw_return_code;
	struct ecore_hwfn *p_hwfn = (struct ecore_hwfn *)rdma_cxt;
	dma_addr_t							ramrod_res_phys;
	struct rdma_resize_cq_output_params	*p_ramrod_res;
	struct ecore_sp_init_data		init_data;

	DP_VERBOSE(p_hwfn, ECORE_MSG_RDMA, "icid = %08x\n", in_params->icid);

	/* Send resize CQ ramrod */

	p_ramrod_res = (struct rdma_resize_cq_output_params *)
			OSAL_DMA_ALLOC_COHERENT(p_hwfn->p_dev, &ramrod_res_phys,
				sizeof(*p_ramrod_res));
	if (!p_ramrod_res)
	{
		rc = ECORE_NOMEM;
		DP_NOTICE(p_hwfn, false,
			  "ecore resize cq failed: cannot allocate memory (ramrod). rc = %d\n",
			  rc);
		return rc;
	}

	/* Get SPQ entry */
	OSAL_MEMSET(&init_data, 0, sizeof(init_data));
	init_data.cid = in_params->icid;
	init_data.opaque_fid = p_hwfn->hw_info.opaque_fid;
	init_data.comp_mode = ECORE_SPQ_MODE_EBLOCK;

	rc = ecore_sp_init_request(p_hwfn, &p_ent,
				   RDMA_RAMROD_RESIZE_CQ,
				   p_hwfn->p_rdma_info->proto, &init_data);
	if (rc != ECORE_SUCCESS)
		goto err;

	p_ramrod = &p_ent->ramrod.rdma_resize_cq;

	p_ramrod->flags = 0;

	/* toggle the bit for every resize or create cq for a given icid */
	toggle_bit = ecore_rdma_toggle_bit_create_resize_cq(p_hwfn,
							    in_params->icid);

	SET_FIELD(p_ramrod->flags,
		  RDMA_RESIZE_CQ_RAMROD_DATA_TOGGLE_BIT,
		  toggle_bit);

	SET_FIELD(p_ramrod->flags,
		  RDMA_RESIZE_CQ_RAMROD_DATA_IS_TWO_LEVEL_PBL,
		  in_params->pbl_two_level);

	p_ramrod->pbl_log_page_size = in_params->pbl_page_size_log - 12;
	p_ramrod->pbl_num_pages = OSAL_CPU_TO_LE16(in_params->pbl_num_pages);
	p_ramrod->max_cqes = OSAL_CPU_TO_LE32(in_params->cq_size);
	p_ramrod->pbl_addr.hi = DMA_HI_LE(in_params->pbl_ptr);
	p_ramrod->pbl_addr.lo = DMA_LO_LE(in_params->pbl_ptr);

	p_ramrod->output_params_addr.hi = DMA_HI_LE(ramrod_res_phys);
	p_ramrod->output_params_addr.lo = DMA_LO_LE(ramrod_res_phys);

	rc = ecore_spq_post(p_hwfn, p_ent, &fw_return_code);
	if (rc != ECORE_SUCCESS)
		goto err;

	if (fw_return_code != RDMA_RETURN_OK)
	{
		DP_NOTICE(p_hwfn, fw_return_code != RDMA_RETURN_RESIZE_CQ_ERR,
			  "fw_return_code = %d\n", fw_return_code);
		DP_NOTICE(p_hwfn,
			  true, "fw_return_code = %d\n", fw_return_code);
		rc = ECORE_UNKNOWN_ERROR;
		goto err;
	}

	out_params->prod = OSAL_LE32_TO_CPU(p_ramrod_res->old_cq_prod);
	out_params->cons = OSAL_LE32_TO_CPU(p_ramrod_res->old_cq_cons);

	OSAL_DMA_FREE_COHERENT(p_hwfn->p_dev, p_ramrod_res, ramrod_res_phys,
			       sizeof(*p_ramrod_res));

	DP_VERBOSE(p_hwfn, ECORE_MSG_RDMA, "rc = %d\n", rc);

	return rc;

err:
	OSAL_DMA_FREE_COHERENT(p_hwfn->p_dev, p_ramrod_res, ramrod_res_phys,
			       sizeof(*p_ramrod_res));
	DP_NOTICE(p_hwfn, false, "rc = %d\n", rc);

	return rc;
}

enum _ecore_status_t ecore_rdma_start(void *rdma_cxt,
				struct ecore_rdma_start_in_params *params)
{
	struct ecore_hwfn *p_hwfn = (struct ecore_hwfn *)rdma_cxt;
	struct ecore_ptt *p_ptt;
	enum _ecore_status_t rc = ECORE_TIMEOUT;

	DP_VERBOSE(p_hwfn, ECORE_MSG_RDMA,
		   "desired_cnq = %08x\n", params->desired_cnq);

	p_ptt = ecore_ptt_acquire(p_hwfn);
	if (!p_ptt)
		goto err;

	rc = ecore_rdma_alloc(p_hwfn);
	if (rc)
		goto err1;

	rc = ecore_rdma_setup(p_hwfn, p_ptt, params);
	if (rc)
		goto err2;

	ecore_ptt_release(p_hwfn, p_ptt);

	ecore_rdma_activate(p_hwfn);
	return rc;

err2:
	ecore_rdma_free(p_hwfn);
err1:
	ecore_ptt_release(p_hwfn, p_ptt);
err:
	DP_VERBOSE(p_hwfn, ECORE_MSG_RDMA, "RDMA start - error, rc = %d\n", rc);
	return rc;
}

enum _ecore_status_t ecore_rdma_query_stats(void *rdma_cxt, u8 stats_queue,
				struct ecore_rdma_stats_out_params *out_params)
{
	struct ecore_hwfn *p_hwfn = (struct ecore_hwfn *)rdma_cxt;
	u8 abs_stats_queue, max_stats_queues;
	u32 pstats_addr, tstats_addr, addr;
	struct ecore_rdma_info *info;
	struct ecore_ptt *p_ptt;
#ifdef CONFIG_ECORE_IWARP
	u32 xstats_addr;
#endif
	enum _ecore_status_t rc = ECORE_SUCCESS;

	if (!p_hwfn)
		return ECORE_INVAL;

	if (!p_hwfn->p_rdma_info) {
		DP_INFO(p_hwfn->p_dev, "ecore rdma query stats failed due to NULL rdma_info\n");
		return ECORE_INVAL;
	}

	info = p_hwfn->p_rdma_info;

	rc = ecore_rdma_inc_ref_cnt(p_hwfn);
	if (rc != ECORE_SUCCESS)
		return rc;

	max_stats_queues = p_hwfn->p_rdma_info->dev->max_stats_queues;
	if (stats_queue >= max_stats_queues) {
		DP_ERR(p_hwfn->p_dev,
		       "ecore rdma query stats failed due to invalid statistics queue %d. maximum is %d\n",
		       stats_queue, max_stats_queues);
		rc = ECORE_INVAL;
		goto err;
	}

	/* Statistics collected in statistics queues (for PF/VF) */
	abs_stats_queue = RESC_START(p_hwfn, ECORE_RDMA_STATS_QUEUE) +
			    stats_queue;
	pstats_addr = BAR0_MAP_REG_PSDM_RAM +
		      PSTORM_RDMA_QUEUE_STAT_OFFSET(abs_stats_queue);
	tstats_addr = BAR0_MAP_REG_TSDM_RAM +
		      TSTORM_RDMA_QUEUE_STAT_OFFSET(abs_stats_queue);

#ifdef CONFIG_ECORE_IWARP
	/* Statistics per PF ID */
	xstats_addr = BAR0_MAP_REG_XSDM_RAM +
		      XSTORM_IWARP_RXMIT_STATS_OFFSET(p_hwfn->rel_pf_id);
#endif

	OSAL_MEMSET(&info->rdma_sent_pstats, 0, sizeof(info->rdma_sent_pstats));
	OSAL_MEMSET(&info->rdma_rcv_tstats, 0, sizeof(info->rdma_rcv_tstats));
	OSAL_MEMSET(&info->roce.event_stats, 0, sizeof(info->roce.event_stats));
	OSAL_MEMSET(&info->roce.dcqcn_rx_stats, 0,sizeof(info->roce.dcqcn_rx_stats));
	OSAL_MEMSET(&info->roce.dcqcn_tx_stats, 0,sizeof(info->roce.dcqcn_tx_stats));
#ifdef CONFIG_ECORE_IWARP
	OSAL_MEMSET(&info->iwarp.stats, 0, sizeof(info->iwarp.stats));
#endif

	p_ptt = ecore_ptt_acquire(p_hwfn);

	if (!p_ptt) {
		rc = ECORE_TIMEOUT;
		DP_VERBOSE(p_hwfn, ECORE_MSG_RDMA, "rc = %d\n", rc);
		goto err;
	}

	ecore_memcpy_from(p_hwfn, p_ptt, &info->rdma_sent_pstats,
			  pstats_addr, sizeof(struct rdma_sent_stats));

	ecore_memcpy_from(p_hwfn, p_ptt, &info->rdma_rcv_tstats,
			  tstats_addr, sizeof(struct rdma_rcv_stats));

	addr = BAR0_MAP_REG_TSDM_RAM +
	       TSTORM_ROCE_EVENTS_STAT_OFFSET(p_hwfn->rel_pf_id);
	ecore_memcpy_from(p_hwfn, p_ptt, &info->roce.event_stats, addr,
			  sizeof(struct roce_events_stats));

	addr = BAR0_MAP_REG_YSDM_RAM +
		YSTORM_ROCE_DCQCN_RECEIVED_STATS_OFFSET(p_hwfn->rel_pf_id);
	ecore_memcpy_from(p_hwfn, p_ptt, &info->roce.dcqcn_rx_stats, addr,
			  sizeof(struct roce_dcqcn_received_stats));

	addr = BAR0_MAP_REG_PSDM_RAM +
	       PSTORM_ROCE_DCQCN_SENT_STATS_OFFSET(p_hwfn->rel_pf_id);
	ecore_memcpy_from(p_hwfn, p_ptt, &info->roce.dcqcn_tx_stats, addr,
			  sizeof(struct roce_dcqcn_sent_stats));

#ifdef CONFIG_ECORE_IWARP
	ecore_memcpy_from(p_hwfn, p_ptt, &info->iwarp.stats,
			  xstats_addr, sizeof(struct iwarp_rxmit_stats_drv));
#endif

	ecore_ptt_release(p_hwfn, p_ptt);

	OSAL_MEMSET(out_params, 0, sizeof(*out_params));

	out_params->sent_bytes =
		HILO_64_REGPAIR(info->rdma_sent_pstats.sent_bytes);
	out_params->sent_pkts =
		HILO_64_REGPAIR(info->rdma_sent_pstats.sent_pkts);
	out_params->rcv_bytes =
		HILO_64_REGPAIR(info->rdma_rcv_tstats.rcv_bytes);
	out_params->rcv_pkts =
		HILO_64_REGPAIR(info->rdma_rcv_tstats.rcv_pkts);

	out_params->silent_drops =
		OSAL_LE16_TO_CPU(info->roce.event_stats.silent_drops);
	out_params->rnr_nacks_sent =
		OSAL_LE16_TO_CPU(info->roce.event_stats.rnr_naks_sent);
	out_params->icrc_errors =
		OSAL_LE32_TO_CPU(info->roce.event_stats.icrc_error_count);
	out_params->retransmit_events =
		OSAL_LE32_TO_CPU(info->roce.event_stats.retransmit_count);
	out_params->ecn_pkt_rcv =
		HILO_64_REGPAIR(info->roce.dcqcn_rx_stats.ecn_pkt_rcv);
	out_params->cnp_pkt_rcv =
		HILO_64_REGPAIR(info->roce.dcqcn_rx_stats.cnp_pkt_rcv);
	out_params->cnp_pkt_sent =
		HILO_64_REGPAIR(info->roce.dcqcn_tx_stats.cnp_pkt_sent);

#ifdef CONFIG_ECORE_IWARP
	out_params->iwarp_tx_fast_rxmit_cnt =
		HILO_64_REGPAIR(info->iwarp.stats.tx_fast_retransmit_event_cnt);
	out_params->iwarp_tx_slow_start_cnt =
		HILO_64_REGPAIR(
			info->iwarp.stats.tx_go_to_slow_start_event_cnt);
	out_params->unalign_rx_comp = info->iwarp.unalign_rx_comp;
#endif

err:
	ecore_rdma_dec_ref_cnt(p_hwfn);

	return rc;
}

enum _ecore_status_t
ecore_rdma_query_counters(void *rdma_cxt,
			  struct ecore_rdma_counters_out_params *out_params)
{
	struct ecore_hwfn *p_hwfn = (struct ecore_hwfn *)rdma_cxt;
	unsigned long *bitmap;
	unsigned int nbits;

	if (!p_hwfn->p_rdma_info)
		return ECORE_INVAL;

	OSAL_MEMSET(out_params, 0, sizeof(*out_params));

	bitmap = p_hwfn->p_rdma_info->pd_map.bitmap;
	nbits = p_hwfn->p_rdma_info->pd_map.max_count;
	out_params->pd_count = OSAL_BITMAP_WEIGHT(bitmap, nbits);
	out_params->max_pd = nbits;

	bitmap = p_hwfn->p_rdma_info->dpi_map.bitmap;
	nbits = p_hwfn->p_rdma_info->dpi_map.max_count;
	out_params->dpi_count = OSAL_BITMAP_WEIGHT(bitmap, nbits);
	out_params->max_dpi = nbits;

	bitmap = p_hwfn->p_rdma_info->cq_map.bitmap;
	nbits = p_hwfn->p_rdma_info->cq_map.max_count;
	out_params->cq_count = OSAL_BITMAP_WEIGHT(bitmap, nbits);
	out_params->max_cq = nbits;

	bitmap = p_hwfn->p_rdma_info->qp_map.bitmap;
	nbits = p_hwfn->p_rdma_info->qp_map.max_count;
	out_params->qp_count = OSAL_BITMAP_WEIGHT(bitmap, nbits);
	out_params->max_qp = nbits;

	bitmap = p_hwfn->p_rdma_info->tid_map.bitmap;
	nbits = p_hwfn->p_rdma_info->tid_map.max_count;
	out_params->tid_count = OSAL_BITMAP_WEIGHT(bitmap, nbits);
	out_params->max_tid = nbits;

	bitmap = p_hwfn->p_rdma_info->srq_map.bitmap;
	nbits = p_hwfn->p_rdma_info->srq_map.max_count;
	out_params->srq_count = OSAL_BITMAP_WEIGHT(bitmap, nbits);
	out_params->max_srq = nbits;

	bitmap = p_hwfn->p_rdma_info->xrc_srq_map.bitmap;
	nbits = p_hwfn->p_rdma_info->xrc_srq_map.max_count;
	out_params->xrc_srq_count = OSAL_BITMAP_WEIGHT(bitmap, nbits);
	out_params->max_xrc_srq = nbits;

	bitmap = p_hwfn->p_rdma_info->xrcd_map.bitmap;
	nbits = p_hwfn->p_rdma_info->xrcd_map.max_count;
	out_params->xrcd_count = OSAL_BITMAP_WEIGHT(bitmap, nbits);
	out_params->max_xrcd = nbits;

	return ECORE_SUCCESS;
}

enum _ecore_status_t ecore_rdma_resize_cnq(void			      *rdma_cxt,
				struct ecore_rdma_resize_cnq_in_params *params)
{
	struct ecore_hwfn *p_hwfn = (struct ecore_hwfn *)rdma_cxt;

	DP_VERBOSE(p_hwfn, ECORE_MSG_RDMA, "cnq_id = %08x\n", params->cnq_id);

	/* @@@TBD: waiting for fw (there is no ramrod yet) */
	return ECORE_NOTIMPL;
}

void ecore_rdma_remove_user(void	*rdma_cxt,
			    u16		dpi)
{
	struct ecore_hwfn *p_hwfn = (struct ecore_hwfn *)rdma_cxt;

	DP_VERBOSE(p_hwfn, ECORE_MSG_RDMA, "dpi = %08x\n", dpi);

	OSAL_SPIN_LOCK(&p_hwfn->p_rdma_info->lock);
	ecore_bmap_release_id(p_hwfn, &p_hwfn->p_rdma_info->dpi_map, dpi);
	OSAL_SPIN_UNLOCK(&p_hwfn->p_rdma_info->lock);
}

#ifndef LINUX_REMOVE
enum _ecore_status_t
ecore_rdma_set_glob_cfg(struct ecore_hwfn *p_hwfn,
			struct ecore_rdma_glob_cfg *in_params,
			u32 glob_cfg_bits)
{
	struct ecore_rdma_glob_cfg glob_cfg;
	enum _ecore_status_t rc = ECORE_SUCCESS;

	DP_VERBOSE(p_hwfn->p_dev, ECORE_MSG_RDMA,
		   "dscp %d dscp en %d ecn %d ecn en %d vlan pri %d vlan_pri_en %d\n",
		   in_params->dscp, in_params->dscp_en,
		   in_params->ecn, in_params->ecn_en, in_params->vlan_pri,
		   in_params->vlan_pri_en);

	/* Read global cfg to local */
	OSAL_MEMCPY(&glob_cfg, &p_hwfn->p_rdma_info->glob_cfg,
		    sizeof(glob_cfg));

	if (glob_cfg_bits & ECORE_RDMA_DCSP_BIT_MASK) {
		if (in_params->dscp > MAX_DSCP) {
			DP_ERR(p_hwfn->p_dev, "invalid glob dscp %d\n",
			       in_params->dscp);
			return ECORE_INVAL;
		}
		glob_cfg.dscp = in_params->dscp;
	}

	if (glob_cfg_bits & ECORE_RDMA_DCSP_EN_BIT_MASK) {
		if (in_params->dscp_en > 1) {
			DP_ERR(p_hwfn->p_dev, "invalid glob_dscp_en %d\n",
			       in_params->dscp_en);
			return ECORE_INVAL;
		}
		glob_cfg.dscp_en = in_params->dscp_en;
	}

	if (glob_cfg_bits & ECORE_RDMA_ECN_BIT_MASK) {
		if (in_params->ecn > INET_ECN_ECT_0) {
			DP_ERR(p_hwfn->p_dev, "invalid glob ecn %d\n",
			       in_params->ecn);
			return ECORE_INVAL;
		}
		glob_cfg.ecn = in_params->ecn;
	}

	if (glob_cfg_bits & ECORE_RDMA_ECN_EN_BIT_MASK) {
		if (in_params->ecn_en > 1) {
			DP_ERR(p_hwfn->p_dev, "invalid glob ecn en %d\n",
			       in_params->ecn_en);
			return ECORE_INVAL;
		}
		glob_cfg.ecn_en = in_params->ecn_en;
	}

	if (glob_cfg_bits & ECORE_RDMA_VLAN_PRIO_BIT_MASK) {
		if (in_params->vlan_pri > MAX_VLAN_PRIO) {
			DP_ERR(p_hwfn->p_dev, "invalid glob vlan pri %d\n",
			       in_params->vlan_pri);
			return ECORE_INVAL;
		}
		glob_cfg.vlan_pri = in_params->vlan_pri;
	}

	if (glob_cfg_bits & ECORE_RDMA_VLAN_PRIO_EN_BIT_MASK) {
		if (in_params->vlan_pri_en > 1) {
			DP_ERR(p_hwfn->p_dev, "invalid glob vlan pri en %d\n",
			       in_params->vlan_pri_en);
			return ECORE_INVAL;
		}
		glob_cfg.vlan_pri_en = in_params->vlan_pri_en;
	}

	/* Write back local cfg to global */
	OSAL_MEMCPY(&p_hwfn->p_rdma_info->glob_cfg, &glob_cfg,
		    sizeof(glob_cfg));

	return rc;
}

enum _ecore_status_t
ecore_rdma_get_glob_cfg(struct ecore_hwfn *p_hwfn,
			struct ecore_rdma_glob_cfg *out_params)
{
	OSAL_MEMCPY(out_params, &p_hwfn->p_rdma_info->glob_cfg,
		    sizeof(struct ecore_rdma_glob_cfg));

	return ECORE_SUCCESS;
}
#endif /* LINUX_REMOVE */
