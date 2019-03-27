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
 * File : ecore_dev.c
 */
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "bcm_osal.h"
#include "reg_addr.h"
#include "ecore_gtt_reg_addr.h"
#include "ecore.h"
#include "ecore_chain.h"
#include "ecore_status.h"
#include "ecore_hw.h"
#include "ecore_rt_defs.h"
#include "ecore_init_ops.h"
#include "ecore_int.h"
#include "ecore_cxt.h"
#include "ecore_spq.h"
#include "ecore_init_fw_funcs.h"
#include "ecore_sp_commands.h"
#include "ecore_dev_api.h"
#include "ecore_sriov.h"
#include "ecore_vf.h"
#include "ecore_ll2.h"
#include "ecore_fcoe.h"
#include "ecore_iscsi.h"
#include "ecore_ooo.h"
#include "ecore_mcp.h"
#include "ecore_hw_defs.h"
#include "mcp_public.h"
#include "ecore_rdma.h"
#include "ecore_iro.h"
#include "nvm_cfg.h"
#include "ecore_dev_api.h"
#include "ecore_dcbx.h"
#include "pcics_reg_driver.h"
#include "ecore_l2.h"
#ifndef LINUX_REMOVE
#include "ecore_tcp_ip.h"
#endif

#ifdef _NTDDK_
#pragma warning(push)
#pragma warning(disable : 28167)
#pragma warning(disable : 28123)
#endif

/* TODO - there's a bug in DCBx re-configuration flows in MF, as the QM
 * registers involved are not split and thus configuration is a race where
 * some of the PFs configuration might be lost.
 * Eventually, this needs to move into a MFW-covered HW-lock as arbitration
 * mechanism as this doesn't cover some cases [E.g., PDA or scenarios where
 * there's more than a single compiled ecore component in system].
 */
static osal_spinlock_t qm_lock;
static u32 qm_lock_ref_cnt;

void ecore_set_ilt_page_size(struct ecore_dev *p_dev, u8 ilt_page_size)
{
	p_dev->ilt_page_size = ilt_page_size;
}

/******************** Doorbell Recovery *******************/
/* The doorbell recovery mechanism consists of a list of entries which represent
 * doorbelling entities (l2 queues, roce sq/rq/cqs, the slowpath spq, etc). Each
 * entity needs to register with the mechanism and provide the parameters
 * describing it's doorbell, including a location where last used doorbell data
 * can be found. The doorbell execute function will traverse the list and
 * doorbell all of the registered entries.
 */
struct ecore_db_recovery_entry {
	osal_list_entry_t	list_entry;
	void OSAL_IOMEM		*db_addr;
	void			*db_data;
	enum ecore_db_rec_width	db_width;
	enum ecore_db_rec_space	db_space;
	u8			hwfn_idx;
};

/* display a single doorbell recovery entry */
static void ecore_db_recovery_dp_entry(struct ecore_hwfn *p_hwfn,
				struct ecore_db_recovery_entry *db_entry,
				char *action)
{
	DP_VERBOSE(p_hwfn, ECORE_MSG_SPQ, "(%s: db_entry %p, addr %p, data %p, width %s, %s space, hwfn %d)\n",
		   action, db_entry, db_entry->db_addr, db_entry->db_data,
		   db_entry->db_width == DB_REC_WIDTH_32B ? "32b" : "64b",
		   db_entry->db_space == DB_REC_USER ? "user" : "kernel",
		   db_entry->hwfn_idx);
}

/* doorbell address sanity (address within doorbell bar range) */
static bool ecore_db_rec_sanity(struct ecore_dev *p_dev, void OSAL_IOMEM *db_addr,
			 void *db_data)
{
	/* make sure doorbell address  is within the doorbell bar */
	if (db_addr < p_dev->doorbells || (u8 *)db_addr >
			(u8 *)p_dev->doorbells + p_dev->db_size) {
		OSAL_WARN(true,
			  "Illegal doorbell address: %p. Legal range for doorbell addresses is [%p..%p]\n",
			  db_addr, p_dev->doorbells,
			  (u8 *)p_dev->doorbells + p_dev->db_size);
		return false;
	}

	/* make sure doorbell data pointer is not null */
	if (!db_data) {
		OSAL_WARN(true, "Illegal doorbell data pointer: %p", db_data);
		return false;
	}

	return true;
}

/* find hwfn according to the doorbell address */
static struct ecore_hwfn *ecore_db_rec_find_hwfn(struct ecore_dev *p_dev,
					  void OSAL_IOMEM *db_addr)
{
	struct ecore_hwfn *p_hwfn;

	/* in CMT doorbell bar is split down the middle between engine 0 and enigne 1 */
	if (ECORE_IS_CMT(p_dev))
		p_hwfn = db_addr < p_dev->hwfns[1].doorbells ?
			&p_dev->hwfns[0] : &p_dev->hwfns[1];
	else
		p_hwfn = ECORE_LEADING_HWFN(p_dev);

	return p_hwfn;
}

/* add a new entry to the doorbell recovery mechanism */
enum _ecore_status_t ecore_db_recovery_add(struct ecore_dev *p_dev,
					   void OSAL_IOMEM *db_addr,
					   void *db_data,
					   enum ecore_db_rec_width db_width,
					   enum ecore_db_rec_space db_space)
{
	struct ecore_db_recovery_entry *db_entry;
	struct ecore_hwfn *p_hwfn;

	/* shortcircuit VFs, for now */
	if (IS_VF(p_dev)) {
		DP_VERBOSE(p_dev, ECORE_MSG_IOV, "db recovery - skipping VF doorbell\n");
		return ECORE_SUCCESS;
	}

	/* sanitize doorbell address */
	if (!ecore_db_rec_sanity(p_dev, db_addr, db_data))
		return ECORE_INVAL;

	/* obtain hwfn from doorbell address */
	p_hwfn = ecore_db_rec_find_hwfn(p_dev, db_addr);

	/* create entry */
	db_entry = OSAL_ZALLOC(p_hwfn->p_dev, GFP_KERNEL, sizeof(*db_entry));
	if (!db_entry) {
		DP_NOTICE(p_dev, false, "Failed to allocate a db recovery entry\n");
		return ECORE_NOMEM;
	}

	/* populate entry */
	db_entry->db_addr = db_addr;
	db_entry->db_data = db_data;
	db_entry->db_width = db_width;
	db_entry->db_space = db_space;
	db_entry->hwfn_idx = p_hwfn->my_id;

	/* display */
	ecore_db_recovery_dp_entry(p_hwfn, db_entry, "Adding");

	/* protect the list */
	OSAL_SPIN_LOCK(&p_hwfn->db_recovery_info.lock);
	OSAL_LIST_PUSH_TAIL(&db_entry->list_entry,
			    &p_hwfn->db_recovery_info.list);
	OSAL_SPIN_UNLOCK(&p_hwfn->db_recovery_info.lock);

	return ECORE_SUCCESS;
}

/* remove an entry from the doorbell recovery mechanism */
enum _ecore_status_t ecore_db_recovery_del(struct ecore_dev *p_dev,
					   void OSAL_IOMEM *db_addr,
					   void *db_data)
{
	struct ecore_db_recovery_entry *db_entry = OSAL_NULL;
	enum _ecore_status_t rc = ECORE_INVAL;
	struct ecore_hwfn *p_hwfn;

	/* shortcircuit VFs, for now */
	if (IS_VF(p_dev)) {
		DP_VERBOSE(p_dev, ECORE_MSG_IOV, "db recovery - skipping VF doorbell\n");
		return ECORE_SUCCESS;
	}

	/* sanitize doorbell address */
	if (!ecore_db_rec_sanity(p_dev, db_addr, db_data))
		return ECORE_INVAL;

	/* obtain hwfn from doorbell address */
	p_hwfn = ecore_db_rec_find_hwfn(p_dev, db_addr);

	/* protect the list */
	OSAL_SPIN_LOCK(&p_hwfn->db_recovery_info.lock);
	OSAL_LIST_FOR_EACH_ENTRY(db_entry,
				 &p_hwfn->db_recovery_info.list,
				 list_entry,
				 struct ecore_db_recovery_entry) {

		/* search according to db_data addr since db_addr is not unique (roce) */
		if (db_entry->db_data == db_data) {
			ecore_db_recovery_dp_entry(p_hwfn, db_entry, "Deleting");
			OSAL_LIST_REMOVE_ENTRY(&db_entry->list_entry,
					       &p_hwfn->db_recovery_info.list);
			rc = ECORE_SUCCESS;
			break;
		}
	}

	OSAL_SPIN_UNLOCK(&p_hwfn->db_recovery_info.lock);

	if (rc == ECORE_INVAL) {
		/*OSAL_WARN(true,*/
		DP_NOTICE(p_hwfn, false,
			  "Failed to find element in list. Key (db_data addr) was %p. db_addr was %p\n",
			  db_data, db_addr);
	} else
		OSAL_FREE(p_dev, db_entry);

	return rc;
}

/* initialize the doorbell recovery mechanism */
static enum _ecore_status_t ecore_db_recovery_setup(struct ecore_hwfn *p_hwfn)
{
	DP_VERBOSE(p_hwfn, ECORE_MSG_SPQ, "Setting up db recovery\n");

	/* make sure db_size was set in p_dev */
	if (!p_hwfn->p_dev->db_size) {
		DP_ERR(p_hwfn->p_dev, "db_size not set\n");
		return ECORE_INVAL;
	}

	OSAL_LIST_INIT(&p_hwfn->db_recovery_info.list);
#ifdef CONFIG_ECORE_LOCK_ALLOC
	if (OSAL_SPIN_LOCK_ALLOC(p_hwfn, &p_hwfn->db_recovery_info.lock))
		return ECORE_NOMEM;
#endif
	OSAL_SPIN_LOCK_INIT(&p_hwfn->db_recovery_info.lock);
	p_hwfn->db_recovery_info.db_recovery_counter = 0;

	return ECORE_SUCCESS;
}

/* destroy the doorbell recovery mechanism */
static void ecore_db_recovery_teardown(struct ecore_hwfn *p_hwfn)
{
	struct ecore_db_recovery_entry *db_entry = OSAL_NULL;

	DP_VERBOSE(p_hwfn, ECORE_MSG_SPQ, "Tearing down db recovery\n");
	if (!OSAL_LIST_IS_EMPTY(&p_hwfn->db_recovery_info.list)) {
		DP_VERBOSE(p_hwfn, false, "Doorbell Recovery teardown found the doorbell recovery list was not empty (Expected in disorderly driver unload (e.g. recovery) otherwise this probably means some flow forgot to db_recovery_del). Prepare to purge doorbell recovery list...\n");
		while (!OSAL_LIST_IS_EMPTY(&p_hwfn->db_recovery_info.list)) {
			db_entry = OSAL_LIST_FIRST_ENTRY(&p_hwfn->db_recovery_info.list,
							 struct ecore_db_recovery_entry,
							 list_entry);
			ecore_db_recovery_dp_entry(p_hwfn, db_entry, "Purging");
			OSAL_LIST_REMOVE_ENTRY(&db_entry->list_entry,
					       &p_hwfn->db_recovery_info.list);
			OSAL_FREE(p_hwfn->p_dev, db_entry);
		}
	}
#ifdef CONFIG_ECORE_LOCK_ALLOC
	OSAL_SPIN_LOCK_DEALLOC(&p_hwfn->db_recovery_info.lock);
#endif
	p_hwfn->db_recovery_info.db_recovery_counter = 0;
}

/* print the content of the doorbell recovery mechanism */
void ecore_db_recovery_dp(struct ecore_hwfn *p_hwfn)
{
	struct ecore_db_recovery_entry *db_entry = OSAL_NULL;

	DP_NOTICE(p_hwfn, false,
		  "Dispalying doorbell recovery database. Counter was %d\n",
		  p_hwfn->db_recovery_info.db_recovery_counter);

	/* protect the list */
	OSAL_SPIN_LOCK(&p_hwfn->db_recovery_info.lock);
	OSAL_LIST_FOR_EACH_ENTRY(db_entry,
				 &p_hwfn->db_recovery_info.list,
				 list_entry,
				 struct ecore_db_recovery_entry) {
		ecore_db_recovery_dp_entry(p_hwfn, db_entry, "Printing");
	}

	OSAL_SPIN_UNLOCK(&p_hwfn->db_recovery_info.lock);
}

/* ring the doorbell of a single doorbell recovery entry */
static void ecore_db_recovery_ring(struct ecore_hwfn *p_hwfn,
			    struct ecore_db_recovery_entry *db_entry,
			    enum ecore_db_rec_exec db_exec)
{
	if (db_exec != DB_REC_ONCE) {
		/* Print according to width */
		if (db_entry->db_width == DB_REC_WIDTH_32B)
			DP_VERBOSE(p_hwfn, ECORE_MSG_SPQ,
				   "%s doorbell address %p data %x\n",
				   db_exec == DB_REC_DRY_RUN ?
				   "would have rung" : "ringing",
				   db_entry->db_addr,
				   *(u32 *)db_entry->db_data);
		else
			DP_VERBOSE(p_hwfn, ECORE_MSG_SPQ,
				   "%s doorbell address %p data %llx\n",
				   db_exec == DB_REC_DRY_RUN ?
				   "would have rung" : "ringing",
				   db_entry->db_addr,
				   (unsigned long long)*(u64 *)(db_entry->db_data));
	}

	/* Sanity */
	if (!ecore_db_rec_sanity(p_hwfn->p_dev, db_entry->db_addr,
				 db_entry->db_data))
		return;

	/* Flush the write combined buffer. Since there are multiple doorbelling
	 * entities using the same address, if we don't flush, a transaction
	 * could be lost.
	 */
	OSAL_WMB(p_hwfn->p_dev);

	/* Ring the doorbell */
	if (db_exec == DB_REC_REAL_DEAL || db_exec == DB_REC_ONCE) {
		if (db_entry->db_width == DB_REC_WIDTH_32B)
			DIRECT_REG_WR(p_hwfn, db_entry->db_addr, *(u32 *)(db_entry->db_data));
		else
			DIRECT_REG_WR64(p_hwfn, db_entry->db_addr, *(u64 *)(db_entry->db_data));
	}

	/* Flush the write combined buffer. Next doorbell may come from a
	 * different entity to the same address...
	 */
	OSAL_WMB(p_hwfn->p_dev);
}

/* traverse the doorbell recovery entry list and ring all the doorbells */
void ecore_db_recovery_execute(struct ecore_hwfn *p_hwfn,
			       enum ecore_db_rec_exec db_exec)
{
	struct ecore_db_recovery_entry *db_entry = OSAL_NULL;

	if (db_exec != DB_REC_ONCE) {
		DP_NOTICE(p_hwfn, false, "Executing doorbell recovery. Counter was %d\n",
			  p_hwfn->db_recovery_info.db_recovery_counter);

		/* track amount of times recovery was executed */
		p_hwfn->db_recovery_info.db_recovery_counter++;
	}

	/* protect the list */
	OSAL_SPIN_LOCK(&p_hwfn->db_recovery_info.lock);
	OSAL_LIST_FOR_EACH_ENTRY(db_entry,
				 &p_hwfn->db_recovery_info.list,
				 list_entry,
				 struct ecore_db_recovery_entry) {
		ecore_db_recovery_ring(p_hwfn, db_entry, db_exec);
		if (db_exec == DB_REC_ONCE)
			break;
	}

	OSAL_SPIN_UNLOCK(&p_hwfn->db_recovery_info.lock);
}
/******************** Doorbell Recovery end ****************/

/********************************** NIG LLH ***********************************/

enum ecore_llh_filter_type {
	ECORE_LLH_FILTER_TYPE_MAC,
	ECORE_LLH_FILTER_TYPE_PROTOCOL,
};

struct ecore_llh_mac_filter {
	u8 addr[ETH_ALEN];
};

struct ecore_llh_protocol_filter {
	enum ecore_llh_prot_filter_type_t type;
	u16 source_port_or_eth_type;
	u16 dest_port;
};

union ecore_llh_filter {
	struct ecore_llh_mac_filter mac;
	struct ecore_llh_protocol_filter protocol;
};

struct ecore_llh_filter_info {
	bool b_enabled;
	u32 ref_cnt;
	enum ecore_llh_filter_type type;
	union ecore_llh_filter filter;
};

struct ecore_llh_info {
	/* Number of LLH filters banks */
	u8 num_ppfid;

#define MAX_NUM_PPFID	8
	u8 ppfid_array[MAX_NUM_PPFID];

	/* Array of filters arrays:
	 * "num_ppfid" elements of filters banks, where each is an array of
	 * "NIG_REG_LLH_FUNC_FILTER_EN_SIZE" filters.
	 */
	struct ecore_llh_filter_info **pp_filters;
};

static void ecore_llh_free(struct ecore_dev *p_dev)
{
	struct ecore_llh_info *p_llh_info = p_dev->p_llh_info;
	u32 i;

	if (p_llh_info != OSAL_NULL) {
		if (p_llh_info->pp_filters != OSAL_NULL) {
			for (i = 0; i < p_llh_info->num_ppfid; i++)
				OSAL_FREE(p_dev, p_llh_info->pp_filters[i]);
		}

		OSAL_FREE(p_dev, p_llh_info->pp_filters);
	}

	OSAL_FREE(p_dev, p_llh_info);
	p_dev->p_llh_info = OSAL_NULL;
}

static enum _ecore_status_t ecore_llh_alloc(struct ecore_dev *p_dev)
{
	struct ecore_llh_info *p_llh_info;
	u32 size; u8 i;

	p_llh_info = OSAL_ZALLOC(p_dev, GFP_KERNEL, sizeof(*p_llh_info));
	if (!p_llh_info)
		return ECORE_NOMEM;
	p_dev->p_llh_info = p_llh_info;

	for (i = 0; i < MAX_NUM_PPFID; i++) {
		if (!(p_dev->ppfid_bitmap & (0x1 << i)))
			continue;

		p_llh_info->ppfid_array[p_llh_info->num_ppfid] = i;
		DP_VERBOSE(p_dev, ECORE_MSG_SP, "ppfid_array[%d] = %hhd\n",
			   p_llh_info->num_ppfid, i);
		p_llh_info->num_ppfid++;
	}

	size = p_llh_info->num_ppfid * sizeof(*p_llh_info->pp_filters);
	p_llh_info->pp_filters = OSAL_ZALLOC(p_dev, GFP_KERNEL, size);
	if (!p_llh_info->pp_filters)
		return ECORE_NOMEM;

	size = NIG_REG_LLH_FUNC_FILTER_EN_SIZE *
	       sizeof(**p_llh_info->pp_filters);
	for (i = 0; i < p_llh_info->num_ppfid; i++) {
		p_llh_info->pp_filters[i] = OSAL_ZALLOC(p_dev, GFP_KERNEL,
							size);
		if (!p_llh_info->pp_filters[i])
			return ECORE_NOMEM;
	}

	return ECORE_SUCCESS;
}

static enum _ecore_status_t ecore_llh_shadow_sanity(struct ecore_dev *p_dev,
						    u8 ppfid, u8 filter_idx,
						    const char *action)
{
	struct ecore_llh_info *p_llh_info = p_dev->p_llh_info;

	if (ppfid >= p_llh_info->num_ppfid) {
		DP_NOTICE(p_dev, false,
			  "LLH shadow [%s]: using ppfid %d while only %d ppfids are available\n",
			  action, ppfid, p_llh_info->num_ppfid);
		return ECORE_INVAL;
	}

	if (filter_idx >= NIG_REG_LLH_FUNC_FILTER_EN_SIZE) {
		DP_NOTICE(p_dev, false,
			  "LLH shadow [%s]: using filter_idx %d while only %d filters are available\n",
			  action, filter_idx, NIG_REG_LLH_FUNC_FILTER_EN_SIZE);
		return ECORE_INVAL;
	}

	return ECORE_SUCCESS;
}

#define ECORE_LLH_INVALID_FILTER_IDX	0xff

static enum _ecore_status_t
ecore_llh_shadow_search_filter(struct ecore_dev *p_dev, u8 ppfid,
			       union ecore_llh_filter *p_filter,
			       u8 *p_filter_idx)
{
	struct ecore_llh_info *p_llh_info = p_dev->p_llh_info;
	struct ecore_llh_filter_info *p_filters;
	enum _ecore_status_t rc;
	u8 i;

	rc = ecore_llh_shadow_sanity(p_dev, ppfid, 0, "search");
	if (rc != ECORE_SUCCESS)
		return rc;

	*p_filter_idx = ECORE_LLH_INVALID_FILTER_IDX;

	p_filters = p_llh_info->pp_filters[ppfid];
	for (i = 0; i < NIG_REG_LLH_FUNC_FILTER_EN_SIZE; i++) {
		if (!OSAL_MEMCMP(p_filter, &p_filters[i].filter,
				 sizeof(*p_filter))) {
			*p_filter_idx = i;
			break;
		}
	}

	return ECORE_SUCCESS;
}

static enum _ecore_status_t
ecore_llh_shadow_get_free_idx(struct ecore_dev *p_dev, u8 ppfid,
			      u8 *p_filter_idx)
{
	struct ecore_llh_info *p_llh_info = p_dev->p_llh_info;
	struct ecore_llh_filter_info *p_filters;
	enum _ecore_status_t rc;
	u8 i;

	rc = ecore_llh_shadow_sanity(p_dev, ppfid, 0, "get_free_idx");
	if (rc != ECORE_SUCCESS)
		return rc;

	*p_filter_idx = ECORE_LLH_INVALID_FILTER_IDX;

	p_filters = p_llh_info->pp_filters[ppfid];
	for (i = 0; i < NIG_REG_LLH_FUNC_FILTER_EN_SIZE; i++) {
		if (!p_filters[i].b_enabled) {
			*p_filter_idx = i;
			break;
		}
	}

	return ECORE_SUCCESS;
}

static enum _ecore_status_t
__ecore_llh_shadow_add_filter(struct ecore_dev *p_dev, u8 ppfid, u8 filter_idx,
			      enum ecore_llh_filter_type type,
			      union ecore_llh_filter *p_filter, u32 *p_ref_cnt)
{
	struct ecore_llh_info *p_llh_info = p_dev->p_llh_info;
	struct ecore_llh_filter_info *p_filters;
	enum _ecore_status_t rc;

	rc = ecore_llh_shadow_sanity(p_dev, ppfid, filter_idx, "add");
	if (rc != ECORE_SUCCESS)
		return rc;

	p_filters = p_llh_info->pp_filters[ppfid];
	if (!p_filters[filter_idx].ref_cnt) {
		p_filters[filter_idx].b_enabled = true;
		p_filters[filter_idx].type = type;
		OSAL_MEMCPY(&p_filters[filter_idx].filter, p_filter,
			    sizeof(p_filters[filter_idx].filter));
	}

	*p_ref_cnt = ++p_filters[filter_idx].ref_cnt;

	return ECORE_SUCCESS;
}

static enum _ecore_status_t
ecore_llh_shadow_add_filter(struct ecore_dev *p_dev, u8 ppfid,
			    enum ecore_llh_filter_type type,
			    union ecore_llh_filter *p_filter,
			    u8 *p_filter_idx, u32 *p_ref_cnt)
{
	enum _ecore_status_t rc;

	/* Check if the same filter already exist */
	rc = ecore_llh_shadow_search_filter(p_dev, ppfid, p_filter,
					    p_filter_idx);
	if (rc != ECORE_SUCCESS)
		return rc;

	/* Find a new entry in case of a new filter */
	if (*p_filter_idx == ECORE_LLH_INVALID_FILTER_IDX) {
		rc = ecore_llh_shadow_get_free_idx(p_dev, ppfid, p_filter_idx);
		if (rc != ECORE_SUCCESS)
			return rc;
	}

	/* No free entry was found */
	if (*p_filter_idx == ECORE_LLH_INVALID_FILTER_IDX) {
		DP_NOTICE(p_dev, false,
			  "Failed to find an empty LLH filter to utilize [ppfid %d]\n",
			  ppfid);
		return ECORE_NORESOURCES;
	}

	return __ecore_llh_shadow_add_filter(p_dev, ppfid, *p_filter_idx, type,
					     p_filter, p_ref_cnt);
}

static enum _ecore_status_t
__ecore_llh_shadow_remove_filter(struct ecore_dev *p_dev, u8 ppfid,
				 u8 filter_idx, u32 *p_ref_cnt)
{
	struct ecore_llh_info *p_llh_info = p_dev->p_llh_info;
	struct ecore_llh_filter_info *p_filters;
	enum _ecore_status_t rc;

	rc = ecore_llh_shadow_sanity(p_dev, ppfid, filter_idx, "remove");
	if (rc != ECORE_SUCCESS)
		return rc;

	p_filters = p_llh_info->pp_filters[ppfid];
	if (!p_filters[filter_idx].ref_cnt) {
		DP_NOTICE(p_dev, false,
			  "LLH shadow: trying to remove a filter with ref_cnt=0\n");
		return ECORE_INVAL;
	}

	*p_ref_cnt = --p_filters[filter_idx].ref_cnt;
	if (!p_filters[filter_idx].ref_cnt)
		OSAL_MEM_ZERO(&p_filters[filter_idx],
			      sizeof(p_filters[filter_idx]));

	return ECORE_SUCCESS;
}

static enum _ecore_status_t
ecore_llh_shadow_remove_filter(struct ecore_dev *p_dev, u8 ppfid,
			       union ecore_llh_filter *p_filter,
			       u8 *p_filter_idx, u32 *p_ref_cnt)
{
	enum _ecore_status_t rc;

	rc = ecore_llh_shadow_search_filter(p_dev, ppfid, p_filter,
					    p_filter_idx);
	if (rc != ECORE_SUCCESS)
		return rc;

	/* No matching filter was found */
	if (*p_filter_idx == ECORE_LLH_INVALID_FILTER_IDX) {
		DP_NOTICE(p_dev, false,
			  "Failed to find a filter in the LLH shadow\n");
		return ECORE_INVAL;
	}

	return __ecore_llh_shadow_remove_filter(p_dev, ppfid, *p_filter_idx,
						p_ref_cnt);
}

static enum _ecore_status_t
ecore_llh_shadow_remove_all_filters(struct ecore_dev *p_dev, u8 ppfid)
{
	struct ecore_llh_info *p_llh_info = p_dev->p_llh_info;
	struct ecore_llh_filter_info *p_filters;
	enum _ecore_status_t rc;

	rc = ecore_llh_shadow_sanity(p_dev, ppfid, 0, "remove_all");
	if (rc != ECORE_SUCCESS)
		return rc;

	p_filters = p_llh_info->pp_filters[ppfid];
	OSAL_MEM_ZERO(p_filters,
		      NIG_REG_LLH_FUNC_FILTER_EN_SIZE * sizeof(*p_filters));

	return ECORE_SUCCESS;
}

static enum _ecore_status_t ecore_abs_ppfid(struct ecore_dev *p_dev,
					    u8 rel_ppfid, u8 *p_abs_ppfid)
{
	struct ecore_llh_info *p_llh_info = p_dev->p_llh_info;

	if (rel_ppfid >= p_llh_info->num_ppfid) {
		DP_NOTICE(p_dev, false,
			  "rel_ppfid %d is not valid, available indices are 0..%hhd\n",
			  rel_ppfid, (u8)(p_llh_info->num_ppfid - 1));
		return ECORE_INVAL;
	}

	*p_abs_ppfid = p_llh_info->ppfid_array[rel_ppfid];

	return ECORE_SUCCESS;
}

static enum _ecore_status_t
__ecore_llh_set_engine_affin(struct ecore_hwfn *p_hwfn, struct ecore_ptt *p_ptt)
{
	struct ecore_dev *p_dev = p_hwfn->p_dev;
	enum ecore_eng eng;
	u8 ppfid;
	enum _ecore_status_t rc;

	rc = ecore_mcp_get_engine_config(p_hwfn, p_ptt);
	if (rc != ECORE_SUCCESS && rc != ECORE_NOTIMPL) {
		DP_NOTICE(p_hwfn, false,
			  "Failed to get the engine affinity configuration\n");
		return rc;
	}

	/* RoCE PF is bound to a single engine */
	if (ECORE_IS_ROCE_PERSONALITY(p_hwfn)) {
		eng = p_dev->fir_affin ? ECORE_ENG1 : ECORE_ENG0;
		rc = ecore_llh_set_roce_affinity(p_dev, eng);
		if (rc != ECORE_SUCCESS) {
			DP_NOTICE(p_dev, false,
				  "Failed to set the RoCE engine affinity\n");
			return rc;
		}

		DP_VERBOSE(p_dev, ECORE_MSG_SP,
			   "LLH: Set the engine affinity of RoCE packets as %d\n",
			   eng);
	}

	/* Storage PF is bound to a single engine while L2 PF uses both */
	if (ECORE_IS_FCOE_PERSONALITY(p_hwfn) ||
	    ECORE_IS_ISCSI_PERSONALITY(p_hwfn))
		eng = p_dev->fir_affin ? ECORE_ENG1 : ECORE_ENG0;
	else /* L2_PERSONALITY */
		eng = ECORE_BOTH_ENG;

	for (ppfid = 0; ppfid < p_dev->p_llh_info->num_ppfid; ppfid++) {
		rc = ecore_llh_set_ppfid_affinity(p_dev, ppfid, eng);
		if (rc != ECORE_SUCCESS) {
			DP_NOTICE(p_dev, false,
				  "Failed to set the engine affinity of ppfid %d\n",
				  ppfid);
			return rc;
		}
	}

	DP_VERBOSE(p_dev, ECORE_MSG_SP,
		   "LLH: Set the engine affinity of non-RoCE packets as %d\n",
		   eng);

	return ECORE_SUCCESS;
}

static enum _ecore_status_t
ecore_llh_set_engine_affin(struct ecore_hwfn *p_hwfn, struct ecore_ptt *p_ptt,
			   bool avoid_eng_affin)
{
	struct ecore_dev *p_dev = p_hwfn->p_dev;
	enum _ecore_status_t rc;

	/* Backwards compatible mode:
	 * - RoCE packets     - Use engine 0.
	 * - Non-RoCE packets - Use connection based classification for L2 PFs,
	 *                      and engine 0 otherwise.
	 */
	if (avoid_eng_affin) {
		enum ecore_eng eng;
		u8 ppfid;

		if (ECORE_IS_ROCE_PERSONALITY(p_hwfn)) {
			eng = ECORE_ENG0;
			rc = ecore_llh_set_roce_affinity(p_dev, eng);
			if (rc != ECORE_SUCCESS) {
				DP_NOTICE(p_dev, false,
					  "Failed to set the RoCE engine affinity\n");
				return rc;
			}

			DP_VERBOSE(p_dev, ECORE_MSG_SP,
				   "LLH [backwards compatible mode]: Set the engine affinity of RoCE packets as %d\n",
				   eng);
		}

		eng = (ECORE_IS_FCOE_PERSONALITY(p_hwfn) ||
		       ECORE_IS_ISCSI_PERSONALITY(p_hwfn)) ? ECORE_ENG0
							   : ECORE_BOTH_ENG;
		for (ppfid = 0; ppfid < p_dev->p_llh_info->num_ppfid; ppfid++) {
			rc = ecore_llh_set_ppfid_affinity(p_dev, ppfid, eng);
			if (rc != ECORE_SUCCESS) {
				DP_NOTICE(p_dev, false,
					  "Failed to set the engine affinity of ppfid %d\n",
					  ppfid);
				return rc;
			}
		}

		DP_VERBOSE(p_dev, ECORE_MSG_SP,
			   "LLH [backwards compatible mode]: Set the engine affinity of non-RoCE packets as %d\n",
			   eng);

		return ECORE_SUCCESS;
	}

	return __ecore_llh_set_engine_affin(p_hwfn, p_ptt);
}

static enum _ecore_status_t ecore_llh_hw_init_pf(struct ecore_hwfn *p_hwfn,
						 struct ecore_ptt *p_ptt,
						 bool avoid_eng_affin)
{
	struct ecore_dev *p_dev = p_hwfn->p_dev;
	u8 ppfid, abs_ppfid;
	enum _ecore_status_t rc;

	for (ppfid = 0; ppfid < p_dev->p_llh_info->num_ppfid; ppfid++) {
		u32 addr;

		rc = ecore_abs_ppfid(p_dev, ppfid, &abs_ppfid);
		if (rc != ECORE_SUCCESS)
			return rc;

		addr = NIG_REG_LLH_PPFID2PFID_TBL_0 + abs_ppfid * 0x4;
		ecore_wr(p_hwfn, p_ptt, addr, p_hwfn->rel_pf_id);
	}

	if (OSAL_TEST_BIT(ECORE_MF_LLH_MAC_CLSS, &p_dev->mf_bits) &&
	    !ECORE_IS_FCOE_PERSONALITY(p_hwfn)) {
		rc = ecore_llh_add_mac_filter(p_dev, 0,
					      p_hwfn->hw_info.hw_mac_addr);
		if (rc != ECORE_SUCCESS)
			DP_NOTICE(p_dev, false,
				  "Failed to add an LLH filter with the primary MAC\n");
	}

	if (ECORE_IS_CMT(p_dev)) {
		rc = ecore_llh_set_engine_affin(p_hwfn, p_ptt, avoid_eng_affin);
		if (rc != ECORE_SUCCESS)
			return rc;
	}

	return ECORE_SUCCESS;
}

u8 ecore_llh_get_num_ppfid(struct ecore_dev *p_dev)
{
	return p_dev->p_llh_info->num_ppfid;
}

enum ecore_eng ecore_llh_get_l2_affinity_hint(struct ecore_dev *p_dev)
{
	return p_dev->l2_affin_hint ? ECORE_ENG1 : ECORE_ENG0;
}

/* TBD - should be removed when these definitions are available in reg_addr.h */
#define NIG_REG_PPF_TO_ENGINE_SEL_ROCE_MASK		0x3
#define NIG_REG_PPF_TO_ENGINE_SEL_ROCE_SHIFT		0
#define NIG_REG_PPF_TO_ENGINE_SEL_NON_ROCE_MASK		0x3
#define NIG_REG_PPF_TO_ENGINE_SEL_NON_ROCE_SHIFT	2

enum _ecore_status_t ecore_llh_set_ppfid_affinity(struct ecore_dev *p_dev,
						  u8 ppfid, enum ecore_eng eng)
{
	struct ecore_hwfn *p_hwfn = ECORE_LEADING_HWFN(p_dev);
	struct ecore_ptt *p_ptt = ecore_ptt_acquire(p_hwfn);
	u32 addr, val, eng_sel;
	enum _ecore_status_t rc = ECORE_SUCCESS;
	u8 abs_ppfid;

	if (p_ptt == OSAL_NULL)
		return ECORE_AGAIN;

	if (!ECORE_IS_CMT(p_dev))
		goto out;

	rc = ecore_abs_ppfid(p_dev, ppfid, &abs_ppfid);
	if (rc != ECORE_SUCCESS)
		goto out;

	switch (eng) {
	case ECORE_ENG0:
		eng_sel = 0;
		break;
	case ECORE_ENG1:
		eng_sel = 1;
		break;
	case ECORE_BOTH_ENG:
		eng_sel = 2;
		break;
	default:
		DP_NOTICE(p_dev, false,
			  "Invalid affinity value for ppfid [%d]\n", eng);
		rc = ECORE_INVAL;
		goto out;
	}

	addr = NIG_REG_PPF_TO_ENGINE_SEL + abs_ppfid * 0x4;
	val = ecore_rd(p_hwfn, p_ptt, addr);
	SET_FIELD(val, NIG_REG_PPF_TO_ENGINE_SEL_NON_ROCE, eng_sel);
	ecore_wr(p_hwfn, p_ptt, addr, val);

	/* The iWARP affinity is set as the affinity of ppfid 0 */
	if (!ppfid && ECORE_IS_IWARP_PERSONALITY(p_hwfn))
		p_dev->iwarp_affin = (eng == ECORE_ENG1) ? 1 : 0;
out:
	ecore_ptt_release(p_hwfn, p_ptt);

	return rc;
}

enum _ecore_status_t ecore_llh_set_roce_affinity(struct ecore_dev *p_dev,
						 enum ecore_eng eng)
{
	struct ecore_hwfn *p_hwfn = ECORE_LEADING_HWFN(p_dev);
	struct ecore_ptt *p_ptt = ecore_ptt_acquire(p_hwfn);
	u32 addr, val, eng_sel;
	enum _ecore_status_t rc = ECORE_SUCCESS;
	u8 ppfid, abs_ppfid;

	if (p_ptt == OSAL_NULL)
		return ECORE_AGAIN;

	if (!ECORE_IS_CMT(p_dev))
		goto out;

	switch (eng) {
	case ECORE_ENG0:
		eng_sel = 0;
		break;
	case ECORE_ENG1:
		eng_sel = 1;
		break;
	case ECORE_BOTH_ENG:
		eng_sel = 2;
		ecore_wr(p_hwfn, p_ptt, NIG_REG_LLH_ENG_CLS_ROCE_QP_SEL,
			 0xf /* QP bit 15 */);
		break;
	default:
		DP_NOTICE(p_dev, false,
			  "Invalid affinity value for RoCE [%d]\n", eng);
		rc = ECORE_INVAL;
		goto out;
	}

	for (ppfid = 0; ppfid < p_dev->p_llh_info->num_ppfid; ppfid++) {
		rc = ecore_abs_ppfid(p_dev, ppfid, &abs_ppfid);
		if (rc != ECORE_SUCCESS)
			goto out;

		addr = NIG_REG_PPF_TO_ENGINE_SEL + abs_ppfid * 0x4;
		val = ecore_rd(p_hwfn, p_ptt, addr);
		SET_FIELD(val, NIG_REG_PPF_TO_ENGINE_SEL_ROCE, eng_sel);
		ecore_wr(p_hwfn, p_ptt, addr, val);
	}
out:
	ecore_ptt_release(p_hwfn, p_ptt);

	return rc;
}

struct ecore_llh_filter_e4_details {
	u64 value;
	u32 mode;
	u32 protocol_type;
	u32 hdr_sel;
	u32 enable;
};

static enum _ecore_status_t
ecore_llh_access_filter_e4(struct ecore_hwfn *p_hwfn,
			   struct ecore_ptt *p_ptt, u8 abs_ppfid, u8 filter_idx,
			   struct ecore_llh_filter_e4_details *p_details,
			   bool b_write_access)
{
	u8 pfid = ECORE_PFID_BY_PPFID(p_hwfn, abs_ppfid);
	struct ecore_dmae_params params;
	enum _ecore_status_t rc;
	u32 addr;

	/* The NIG/LLH registers that are accessed in this function have only 16
	 * rows which are exposed to a PF. I.e. only the 16 filters of its
	 * default ppfid
	 * Accessing filters of other ppfids requires pretending to other PFs,
	 * and thus the usage of the ecore_ppfid_rd/wr() functions.
	 */

	/* Filter enable - should be done first when removing a filter */
	if (b_write_access && !p_details->enable) {
		addr = NIG_REG_LLH_FUNC_FILTER_EN_BB_K2 + filter_idx * 0x4;
		ecore_ppfid_wr(p_hwfn, p_ptt, abs_ppfid, addr,
			       p_details->enable);
	}

	/* Filter value */
	addr = NIG_REG_LLH_FUNC_FILTER_VALUE_BB_K2 + 2 * filter_idx * 0x4;
	OSAL_MEMSET(&params, 0, sizeof(params));

	if (b_write_access) {
		params.flags = ECORE_DMAE_FLAG_PF_DST;
		params.dst_pfid = pfid;
		rc = ecore_dmae_host2grc(p_hwfn, p_ptt,
					 (u64)(osal_uintptr_t)&p_details->value,
					 addr, 2 /* size_in_dwords */, &params);
	} else {
		params.flags = ECORE_DMAE_FLAG_PF_SRC |
			       ECORE_DMAE_FLAG_COMPLETION_DST;
		params.src_pfid = pfid;
		rc = ecore_dmae_grc2host(p_hwfn, p_ptt, addr,
					 (u64)(osal_uintptr_t)&p_details->value,
					 2 /* size_in_dwords */, &params);
	}

	if (rc != ECORE_SUCCESS)
		return rc;

	/* Filter mode */
	addr = NIG_REG_LLH_FUNC_FILTER_MODE_BB_K2 + filter_idx * 0x4;
	if (b_write_access)
		ecore_ppfid_wr(p_hwfn, p_ptt, abs_ppfid, addr, p_details->mode);
	else
		p_details->mode = ecore_ppfid_rd(p_hwfn, p_ptt, abs_ppfid,
						 addr);

	/* Filter protocol type */
	addr = NIG_REG_LLH_FUNC_FILTER_PROTOCOL_TYPE_BB_K2 + filter_idx * 0x4;
	if (b_write_access)
		ecore_ppfid_wr(p_hwfn, p_ptt, abs_ppfid, addr,
			       p_details->protocol_type);
	else
		p_details->protocol_type = ecore_ppfid_rd(p_hwfn, p_ptt,
							  abs_ppfid, addr);

	/* Filter header select */
	addr = NIG_REG_LLH_FUNC_FILTER_HDR_SEL_BB_K2 + filter_idx * 0x4;
	if (b_write_access)
		ecore_ppfid_wr(p_hwfn, p_ptt, abs_ppfid, addr,
			       p_details->hdr_sel);
	else
		p_details->hdr_sel = ecore_ppfid_rd(p_hwfn, p_ptt, abs_ppfid,
						    addr);

	/* Filter enable - should be done last when adding a filter */
	if (!b_write_access || p_details->enable) {
		addr = NIG_REG_LLH_FUNC_FILTER_EN_BB_K2 + filter_idx * 0x4;
		if (b_write_access)
			ecore_ppfid_wr(p_hwfn, p_ptt, abs_ppfid, addr,
				       p_details->enable);
		else
			p_details->enable = ecore_ppfid_rd(p_hwfn, p_ptt,
							   abs_ppfid, addr);
	}

	return ECORE_SUCCESS;
}

static enum _ecore_status_t
ecore_llh_add_filter_e4(struct ecore_hwfn *p_hwfn, struct ecore_ptt *p_ptt,
			u8 abs_ppfid, u8 filter_idx, u8 filter_prot_type,
			u32 high, u32 low)
{
	struct ecore_llh_filter_e4_details filter_details;

	filter_details.enable = 1;
	filter_details.value = ((u64)high << 32) | low;
	filter_details.hdr_sel = 0;
	filter_details.protocol_type = filter_prot_type;
	filter_details.mode = filter_prot_type ?
			      1 : /* protocol-based classification */
			      0;  /* MAC-address based classification */

	return ecore_llh_access_filter_e4(p_hwfn, p_ptt, abs_ppfid, filter_idx,
					  &filter_details,
					  true /* write access */);
}

static enum _ecore_status_t
ecore_llh_remove_filter_e4(struct ecore_hwfn *p_hwfn,
			   struct ecore_ptt *p_ptt, u8 abs_ppfid, u8 filter_idx)
{
	struct ecore_llh_filter_e4_details filter_details;

	OSAL_MEMSET(&filter_details, 0, sizeof(filter_details));

	return ecore_llh_access_filter_e4(p_hwfn, p_ptt, abs_ppfid, filter_idx,
					  &filter_details,
					  true /* write access */);
}

/* OSAL_UNUSED is temporary used to avoid unused-parameter compilation warnings.
 * Should be removed when the function is implemented.
 */
static enum _ecore_status_t
ecore_llh_add_filter_e5(struct ecore_hwfn OSAL_UNUSED *p_hwfn,
			struct ecore_ptt OSAL_UNUSED *p_ptt,
			u8 OSAL_UNUSED abs_ppfid, u8 OSAL_UNUSED filter_idx,
			u8 OSAL_UNUSED filter_prot_type, u32 OSAL_UNUSED high,
			u32 OSAL_UNUSED low)
{
	ECORE_E5_MISSING_CODE;

	return ECORE_NOTIMPL;
}

/* OSAL_UNUSED is temporary used to avoid unused-parameter compilation warnings.
 * Should be removed when the function is implemented.
 */
static enum _ecore_status_t
ecore_llh_remove_filter_e5(struct ecore_hwfn OSAL_UNUSED *p_hwfn,
			   struct ecore_ptt OSAL_UNUSED *p_ptt,
			   u8 OSAL_UNUSED abs_ppfid,
			   u8 OSAL_UNUSED filter_idx)
{
	ECORE_E5_MISSING_CODE;

	return ECORE_NOTIMPL;
}

static enum _ecore_status_t
ecore_llh_add_filter(struct ecore_hwfn *p_hwfn, struct ecore_ptt *p_ptt,
		     u8 abs_ppfid, u8 filter_idx, u8 filter_prot_type, u32 high,
		     u32 low)
{
	if (ECORE_IS_E4(p_hwfn->p_dev))
		return ecore_llh_add_filter_e4(p_hwfn, p_ptt, abs_ppfid,
					       filter_idx, filter_prot_type,
					       high, low);
	else /* E5 */
		return ecore_llh_add_filter_e5(p_hwfn, p_ptt, abs_ppfid,
					       filter_idx, filter_prot_type,
					       high, low);
}

static enum _ecore_status_t
ecore_llh_remove_filter(struct ecore_hwfn *p_hwfn, struct ecore_ptt *p_ptt,
			u8 abs_ppfid, u8 filter_idx)
{
	if (ECORE_IS_E4(p_hwfn->p_dev))
		return ecore_llh_remove_filter_e4(p_hwfn, p_ptt, abs_ppfid,
						  filter_idx);
	else /* E5 */
		return ecore_llh_remove_filter_e5(p_hwfn, p_ptt, abs_ppfid,
						  filter_idx);
}

enum _ecore_status_t ecore_llh_add_mac_filter(struct ecore_dev *p_dev, u8 ppfid,
					      u8 mac_addr[ETH_ALEN])
{
	struct ecore_hwfn *p_hwfn = ECORE_LEADING_HWFN(p_dev);
	struct ecore_ptt *p_ptt = ecore_ptt_acquire(p_hwfn);
	union ecore_llh_filter filter;
	u8 filter_idx, abs_ppfid;
	u32 high, low, ref_cnt;
	enum _ecore_status_t rc = ECORE_SUCCESS;

	if (p_ptt == OSAL_NULL)
		return ECORE_AGAIN;

	if (!OSAL_TEST_BIT(ECORE_MF_LLH_MAC_CLSS, &p_dev->mf_bits))
		goto out;

	OSAL_MEM_ZERO(&filter, sizeof(filter));
	OSAL_MEMCPY(filter.mac.addr, mac_addr, ETH_ALEN);
	rc = ecore_llh_shadow_add_filter(p_dev, ppfid,
					 ECORE_LLH_FILTER_TYPE_MAC,
					 &filter, &filter_idx, &ref_cnt);
	if (rc != ECORE_SUCCESS)
		goto err;

	rc = ecore_abs_ppfid(p_dev, ppfid, &abs_ppfid);
	if (rc != ECORE_SUCCESS)
		goto err;

	/* Configure the LLH only in case of a new the filter */
	if (ref_cnt == 1) {
		high = mac_addr[1] | (mac_addr[0] << 8);
		low = mac_addr[5] | (mac_addr[4] << 8) | (mac_addr[3] << 16) |
		      (mac_addr[2] << 24);
		rc = ecore_llh_add_filter(p_hwfn, p_ptt, abs_ppfid, filter_idx,
					  0, high, low);
		if (rc != ECORE_SUCCESS)
			goto err;
	}

	DP_VERBOSE(p_dev, ECORE_MSG_SP,
		   "LLH: Added MAC filter [%02hhx:%02hhx:%02hhx:%02hhx:%02hhx:%02hhx] to ppfid %hhd [abs %hhd] at idx %hhd [ref_cnt %d]\n",
		   mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3],
		   mac_addr[4], mac_addr[5], ppfid, abs_ppfid, filter_idx,
		   ref_cnt);

	goto out;

err:
	DP_NOTICE(p_dev, false,
		  "LLH: Failed to add MAC filter [%02hhx:%02hhx:%02hhx:%02hhx:%02hhx:%02hhx] to ppfid %hhd\n",
		  mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3],
		  mac_addr[4], mac_addr[5], ppfid);
out:
	ecore_ptt_release(p_hwfn, p_ptt);

	return rc;
}

static enum _ecore_status_t
ecore_llh_protocol_filter_stringify(struct ecore_dev *p_dev,
				    enum ecore_llh_prot_filter_type_t type,
				    u16 source_port_or_eth_type, u16 dest_port,
				    u8 *str, osal_size_t str_len)
{
	switch (type) {
	case ECORE_LLH_FILTER_ETHERTYPE:
		OSAL_SNPRINTF(str, str_len, "Ethertype 0x%04x",
			      source_port_or_eth_type);
		break;
	case ECORE_LLH_FILTER_TCP_SRC_PORT:
		OSAL_SNPRINTF(str, str_len, "TCP src port 0x%04x",
			      source_port_or_eth_type);
		break;
	case ECORE_LLH_FILTER_UDP_SRC_PORT:
		OSAL_SNPRINTF(str, str_len, "UDP src port 0x%04x",
			      source_port_or_eth_type);
		break;
	case ECORE_LLH_FILTER_TCP_DEST_PORT:
		OSAL_SNPRINTF(str, str_len, "TCP dst port 0x%04x", dest_port);
		break;
	case ECORE_LLH_FILTER_UDP_DEST_PORT:
		OSAL_SNPRINTF(str, str_len, "UDP dst port 0x%04x", dest_port);
		break;
	case ECORE_LLH_FILTER_TCP_SRC_AND_DEST_PORT:
		OSAL_SNPRINTF(str, str_len, "TCP src/dst ports 0x%04x/0x%04x",
			      source_port_or_eth_type, dest_port);
		break;
	case ECORE_LLH_FILTER_UDP_SRC_AND_DEST_PORT:
		OSAL_SNPRINTF(str, str_len, "UDP src/dst ports 0x%04x/0x%04x",
			      source_port_or_eth_type, dest_port);
		break;
	default:
		DP_NOTICE(p_dev, true,
			  "Non valid LLH protocol filter type %d\n", type);
		return ECORE_INVAL;
	}

	return ECORE_SUCCESS;
}

static enum _ecore_status_t
ecore_llh_protocol_filter_to_hilo(struct ecore_dev *p_dev,
				  enum ecore_llh_prot_filter_type_t type,
				  u16 source_port_or_eth_type, u16 dest_port,
				  u32 *p_high, u32 *p_low)
{
	*p_high = 0;
	*p_low = 0;

	switch (type) {
	case ECORE_LLH_FILTER_ETHERTYPE:
		*p_high = source_port_or_eth_type;
		break;
	case ECORE_LLH_FILTER_TCP_SRC_PORT:
	case ECORE_LLH_FILTER_UDP_SRC_PORT:
		*p_low = source_port_or_eth_type << 16;
		break;
	case ECORE_LLH_FILTER_TCP_DEST_PORT:
	case ECORE_LLH_FILTER_UDP_DEST_PORT:
		*p_low = dest_port;
		break;
	case ECORE_LLH_FILTER_TCP_SRC_AND_DEST_PORT:
	case ECORE_LLH_FILTER_UDP_SRC_AND_DEST_PORT:
		*p_low = (source_port_or_eth_type << 16) | dest_port;
		break;
	default:
		DP_NOTICE(p_dev, true,
			  "Non valid LLH protocol filter type %d\n", type);
		return ECORE_INVAL;
	}

	return ECORE_SUCCESS;
}

enum _ecore_status_t
ecore_llh_add_protocol_filter(struct ecore_dev *p_dev, u8 ppfid,
			      enum ecore_llh_prot_filter_type_t type,
			      u16 source_port_or_eth_type, u16 dest_port)
{
	struct ecore_hwfn *p_hwfn = ECORE_LEADING_HWFN(p_dev);
	struct ecore_ptt *p_ptt = ecore_ptt_acquire(p_hwfn);
	u8 filter_idx, abs_ppfid, str[32], type_bitmap;
	union ecore_llh_filter filter;
	u32 high, low, ref_cnt;
	enum _ecore_status_t rc = ECORE_SUCCESS;

	if (p_ptt == OSAL_NULL)
		return ECORE_AGAIN;

	if (!OSAL_TEST_BIT(ECORE_MF_LLH_PROTO_CLSS, &p_dev->mf_bits))
		goto out;

	rc = ecore_llh_protocol_filter_stringify(p_dev, type,
						 source_port_or_eth_type,
						 dest_port, str, sizeof(str));
	if (rc != ECORE_SUCCESS)
		goto err;

	OSAL_MEM_ZERO(&filter, sizeof(filter));
	filter.protocol.type = type;
	filter.protocol.source_port_or_eth_type = source_port_or_eth_type;
	filter.protocol.dest_port = dest_port;
	rc = ecore_llh_shadow_add_filter(p_dev, ppfid,
					 ECORE_LLH_FILTER_TYPE_PROTOCOL,
					 &filter, &filter_idx, &ref_cnt);
	if (rc != ECORE_SUCCESS)
		goto err;

	rc = ecore_abs_ppfid(p_dev, ppfid, &abs_ppfid);
	if (rc != ECORE_SUCCESS)
		goto err;

	/* Configure the LLH only in case of a new the filter */
	if (ref_cnt == 1) {
		rc = ecore_llh_protocol_filter_to_hilo(p_dev, type,
						       source_port_or_eth_type,
						       dest_port, &high, &low);
		if (rc != ECORE_SUCCESS)
			goto err;

		type_bitmap = 0x1 << type;
		rc = ecore_llh_add_filter(p_hwfn, p_ptt, abs_ppfid, filter_idx,
					  type_bitmap, high, low);
		if (rc != ECORE_SUCCESS)
			goto err;
	}

	DP_VERBOSE(p_dev, ECORE_MSG_SP,
		   "LLH: Added protocol filter [%s] to ppfid %hhd [abs %hhd] at idx %hhd [ref_cnt %d]\n",
		   str, ppfid, abs_ppfid, filter_idx, ref_cnt);

	goto out;

err:
	DP_NOTICE(p_hwfn, false,
		  "LLH: Failed to add protocol filter [%s] to ppfid %hhd\n",
		  str, ppfid);
out:
	ecore_ptt_release(p_hwfn, p_ptt);

	return rc;
}

void ecore_llh_remove_mac_filter(struct ecore_dev *p_dev, u8 ppfid,
				 u8 mac_addr[ETH_ALEN])
{
	struct ecore_hwfn *p_hwfn = ECORE_LEADING_HWFN(p_dev);
	struct ecore_ptt *p_ptt = ecore_ptt_acquire(p_hwfn);
	union ecore_llh_filter filter;
	u8 filter_idx, abs_ppfid;
	enum _ecore_status_t rc = ECORE_SUCCESS;
	u32 ref_cnt;

	if (p_ptt == OSAL_NULL)
		return;

	if (!OSAL_TEST_BIT(ECORE_MF_LLH_MAC_CLSS, &p_dev->mf_bits))
		goto out;

	OSAL_MEM_ZERO(&filter, sizeof(filter));
	OSAL_MEMCPY(filter.mac.addr, mac_addr, ETH_ALEN);
	rc = ecore_llh_shadow_remove_filter(p_dev, ppfid, &filter, &filter_idx,
					    &ref_cnt);
	if (rc != ECORE_SUCCESS)
		goto err;

	rc = ecore_abs_ppfid(p_dev, ppfid, &abs_ppfid);
	if (rc != ECORE_SUCCESS)
		goto err;

	/* Remove from the LLH in case the filter is not in use */
	if (!ref_cnt) {
		rc = ecore_llh_remove_filter(p_hwfn, p_ptt, abs_ppfid,
					     filter_idx);
		if (rc != ECORE_SUCCESS)
			goto err;
	}

	DP_VERBOSE(p_dev, ECORE_MSG_SP,
		   "LLH: Removed MAC filter [%02hhx:%02hhx:%02hhx:%02hhx:%02hhx:%02hhx] from ppfid %hhd [abs %hhd] at idx %hhd [ref_cnt %d]\n",
		   mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3],
		   mac_addr[4], mac_addr[5], ppfid, abs_ppfid, filter_idx,
		   ref_cnt);

	goto out;

err:
	DP_NOTICE(p_dev, false,
		  "LLH: Failed to remove MAC filter [%02hhx:%02hhx:%02hhx:%02hhx:%02hhx:%02hhx] from ppfid %hhd\n",
		  mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3],
		  mac_addr[4], mac_addr[5], ppfid);
out:
	ecore_ptt_release(p_hwfn, p_ptt);
}

void ecore_llh_remove_protocol_filter(struct ecore_dev *p_dev, u8 ppfid,
				      enum ecore_llh_prot_filter_type_t type,
				      u16 source_port_or_eth_type,
				      u16 dest_port)
{
	struct ecore_hwfn *p_hwfn = ECORE_LEADING_HWFN(p_dev);
	struct ecore_ptt *p_ptt = ecore_ptt_acquire(p_hwfn);
	u8 filter_idx, abs_ppfid, str[32];
	union ecore_llh_filter filter;
	enum _ecore_status_t rc = ECORE_SUCCESS;
	u32 ref_cnt;

	if (p_ptt == OSAL_NULL)
		return;

	if (!OSAL_TEST_BIT(ECORE_MF_LLH_PROTO_CLSS, &p_dev->mf_bits))
		goto out;

	rc = ecore_llh_protocol_filter_stringify(p_dev, type,
						 source_port_or_eth_type,
						 dest_port, str, sizeof(str));
	if (rc != ECORE_SUCCESS)
		goto err;

	OSAL_MEM_ZERO(&filter, sizeof(filter));
	filter.protocol.type = type;
	filter.protocol.source_port_or_eth_type = source_port_or_eth_type;
	filter.protocol.dest_port = dest_port;
	rc = ecore_llh_shadow_remove_filter(p_dev, ppfid, &filter, &filter_idx,
					    &ref_cnt);
	if (rc != ECORE_SUCCESS)
		goto err;

	rc = ecore_abs_ppfid(p_dev, ppfid, &abs_ppfid);
	if (rc != ECORE_SUCCESS)
		goto err;

	/* Remove from the LLH in case the filter is not in use */
	if (!ref_cnt) {
		rc = ecore_llh_remove_filter(p_hwfn, p_ptt, abs_ppfid,
					     filter_idx);
		if (rc != ECORE_SUCCESS)
			goto err;
	}

	DP_VERBOSE(p_dev, ECORE_MSG_SP,
		   "LLH: Removed protocol filter [%s] from ppfid %hhd [abs %hhd] at idx %hhd [ref_cnt %d]\n",
		   str, ppfid, abs_ppfid, filter_idx, ref_cnt);

	goto out;

err:
	DP_NOTICE(p_dev, false,
		  "LLH: Failed to remove protocol filter [%s] from ppfid %hhd\n",
		  str, ppfid);
out:
	ecore_ptt_release(p_hwfn, p_ptt);
}

void ecore_llh_clear_ppfid_filters(struct ecore_dev *p_dev, u8 ppfid)
{
	struct ecore_hwfn *p_hwfn = ECORE_LEADING_HWFN(p_dev);
	struct ecore_ptt *p_ptt = ecore_ptt_acquire(p_hwfn);
	u8 filter_idx, abs_ppfid;
	enum _ecore_status_t rc = ECORE_SUCCESS;

	if (p_ptt == OSAL_NULL)
		return;

	if (!OSAL_TEST_BIT(ECORE_MF_LLH_PROTO_CLSS, &p_dev->mf_bits) &&
	    !OSAL_TEST_BIT(ECORE_MF_LLH_MAC_CLSS, &p_dev->mf_bits))
		goto out;

	rc = ecore_abs_ppfid(p_dev, ppfid, &abs_ppfid);
	if (rc != ECORE_SUCCESS)
		goto out;

	rc = ecore_llh_shadow_remove_all_filters(p_dev, ppfid);
	if (rc != ECORE_SUCCESS)
		goto out;

	for (filter_idx = 0; filter_idx < NIG_REG_LLH_FUNC_FILTER_EN_SIZE;
	     filter_idx++) {
		if (ECORE_IS_E4(p_dev))
			rc = ecore_llh_remove_filter_e4(p_hwfn, p_ptt,
							abs_ppfid, filter_idx);
		else /* E5 */
			rc = ecore_llh_remove_filter_e5(p_hwfn, p_ptt,
							abs_ppfid, filter_idx);
		if (rc != ECORE_SUCCESS)
			goto out;
	}
out:
	ecore_ptt_release(p_hwfn, p_ptt);
}

void ecore_llh_clear_all_filters(struct ecore_dev *p_dev)
{
	u8 ppfid;

	if (!OSAL_TEST_BIT(ECORE_MF_LLH_PROTO_CLSS, &p_dev->mf_bits) &&
	    !OSAL_TEST_BIT(ECORE_MF_LLH_MAC_CLSS, &p_dev->mf_bits))
		return;

	for (ppfid = 0; ppfid < p_dev->p_llh_info->num_ppfid; ppfid++)
		ecore_llh_clear_ppfid_filters(p_dev, ppfid);
}

enum _ecore_status_t ecore_all_ppfids_wr(struct ecore_hwfn *p_hwfn,
					 struct ecore_ptt *p_ptt, u32 addr,
					 u32 val)
{
	struct ecore_dev *p_dev = p_hwfn->p_dev;
	u8 ppfid, abs_ppfid;
	enum _ecore_status_t rc;

	for (ppfid = 0; ppfid < p_dev->p_llh_info->num_ppfid; ppfid++) {
		rc = ecore_abs_ppfid(p_dev, ppfid, &abs_ppfid);
		if (rc != ECORE_SUCCESS)
			return rc;

		ecore_ppfid_wr(p_hwfn, p_ptt, abs_ppfid, addr, val);
	}

	return ECORE_SUCCESS;
}

static enum _ecore_status_t
ecore_llh_dump_ppfid_e4(struct ecore_hwfn *p_hwfn, struct ecore_ptt *p_ptt,
			u8 ppfid)
{
	struct ecore_llh_filter_e4_details filter_details;
	u8 abs_ppfid, filter_idx;
	u32 addr;
	enum _ecore_status_t rc;

	rc = ecore_abs_ppfid(p_hwfn->p_dev, ppfid, &abs_ppfid);
	if (rc != ECORE_SUCCESS)
		return rc;

	addr = NIG_REG_PPF_TO_ENGINE_SEL + abs_ppfid * 0x4;
	DP_NOTICE(p_hwfn, false,
		  "[rel_pf_id %hhd, ppfid={rel %hhd, abs %hhd}, engine_sel 0x%x]\n",
		  p_hwfn->rel_pf_id, ppfid, abs_ppfid,
		  ecore_rd(p_hwfn, p_ptt, addr));

	for (filter_idx = 0; filter_idx < NIG_REG_LLH_FUNC_FILTER_EN_SIZE;
	     filter_idx++) {
		OSAL_MEMSET(&filter_details, 0, sizeof(filter_details));
		rc =  ecore_llh_access_filter_e4(p_hwfn, p_ptt, abs_ppfid,
						 filter_idx, &filter_details,
						 false /* read access */);
		if (rc != ECORE_SUCCESS)
			return rc;

		DP_NOTICE(p_hwfn, false,
			  "filter %2hhd: enable %d, value 0x%016llx, mode %d, protocol_type 0x%x, hdr_sel 0x%x\n",
			  filter_idx, filter_details.enable,
			  (unsigned long long)filter_details.value, filter_details.mode,
			  filter_details.protocol_type, filter_details.hdr_sel);
	}

	return ECORE_SUCCESS;
}

static enum _ecore_status_t
ecore_llh_dump_ppfid_e5(struct ecore_hwfn OSAL_UNUSED *p_hwfn,
			struct ecore_ptt OSAL_UNUSED *p_ptt,
			u8 OSAL_UNUSED ppfid)
{
	ECORE_E5_MISSING_CODE;

	return ECORE_NOTIMPL;
}

enum _ecore_status_t ecore_llh_dump_ppfid(struct ecore_dev *p_dev, u8 ppfid)
{
	struct ecore_hwfn *p_hwfn = ECORE_LEADING_HWFN(p_dev);
	struct ecore_ptt *p_ptt = ecore_ptt_acquire(p_hwfn);
	enum _ecore_status_t rc;

	if (p_ptt == OSAL_NULL)
		return ECORE_AGAIN;

	if (ECORE_IS_E4(p_dev))
		rc = ecore_llh_dump_ppfid_e4(p_hwfn, p_ptt, ppfid);
	else /* E5 */
		rc = ecore_llh_dump_ppfid_e5(p_hwfn, p_ptt, ppfid);

	ecore_ptt_release(p_hwfn, p_ptt);

	return rc;
}

enum _ecore_status_t ecore_llh_dump_all(struct ecore_dev *p_dev)
{
	u8 ppfid;
	enum _ecore_status_t rc;

	for (ppfid = 0; ppfid < p_dev->p_llh_info->num_ppfid; ppfid++) {
		rc = ecore_llh_dump_ppfid(p_dev, ppfid);
		if (rc != ECORE_SUCCESS)
			return rc;
	}

	return ECORE_SUCCESS;
}

/******************************* NIG LLH - End ********************************/

/* Configurable */
#define ECORE_MIN_DPIS		(4)  /* The minimal number of DPIs required to
				      * load the driver. The number was
				      * arbitrarily set.
				      */

/* Derived */
#define ECORE_MIN_PWM_REGION	(ECORE_WID_SIZE * ECORE_MIN_DPIS)

static u32 ecore_hw_bar_size(struct ecore_hwfn *p_hwfn,
			     struct ecore_ptt *p_ptt,
			     enum BAR_ID bar_id)
{
	u32 bar_reg = (bar_id == BAR_ID_0 ?
		       PGLUE_B_REG_PF_BAR0_SIZE : PGLUE_B_REG_PF_BAR1_SIZE);
	u32 val;

	if (IS_VF(p_hwfn->p_dev))
		return ecore_vf_hw_bar_size(p_hwfn, bar_id);

	val = ecore_rd(p_hwfn, p_ptt, bar_reg);
	if (val)
		return 1 << (val + 15);

	/* The above registers were updated in the past only in CMT mode. Since
	 * they were found to be useful MFW started updating them from 8.7.7.0.
	 * In older MFW versions they are set to 0 which means disabled.
	 */
	if (ECORE_IS_CMT(p_hwfn->p_dev)) {
		DP_INFO(p_hwfn,
			"BAR size not configured. Assuming BAR size of 256kB for GRC and 512kB for DB\n");
		return BAR_ID_0 ? 256 * 1024 : 512 * 1024;
	} else {
		DP_INFO(p_hwfn,
			"BAR size not configured. Assuming BAR size of 512kB for GRC and 512kB for DB\n");
		return 512 * 1024;
	}
}

void ecore_init_dp(struct ecore_dev	*p_dev,
		   u32			dp_module,
		   u8			dp_level,
		   void		 *dp_ctx)
{
	u32 i;

	p_dev->dp_level = dp_level;
	p_dev->dp_module = dp_module;
	p_dev->dp_ctx = dp_ctx;
	for (i = 0; i < MAX_HWFNS_PER_DEVICE; i++) {
		struct ecore_hwfn *p_hwfn = &p_dev->hwfns[i];

		p_hwfn->dp_level = dp_level;
		p_hwfn->dp_module = dp_module;
		p_hwfn->dp_ctx = dp_ctx;
	}
}

enum _ecore_status_t ecore_init_struct(struct ecore_dev *p_dev)
{
	u8 i;

	for (i = 0; i < MAX_HWFNS_PER_DEVICE; i++) {
		struct ecore_hwfn *p_hwfn = &p_dev->hwfns[i];

		p_hwfn->p_dev = p_dev;
		p_hwfn->my_id = i;
		p_hwfn->b_active = false;

#ifdef CONFIG_ECORE_LOCK_ALLOC
		if (OSAL_SPIN_LOCK_ALLOC(p_hwfn, &p_hwfn->dmae_info.lock))
			goto handle_err;
#endif
		OSAL_SPIN_LOCK_INIT(&p_hwfn->dmae_info.lock);
	}

	/* hwfn 0 is always active */
	p_dev->hwfns[0].b_active = true;

	/* set the default cache alignment to 128 (may be overridden later) */
	p_dev->cache_shift = 7;

	p_dev->ilt_page_size = ECORE_DEFAULT_ILT_PAGE_SIZE;

	return ECORE_SUCCESS;
#ifdef CONFIG_ECORE_LOCK_ALLOC
handle_err:
	while (--i) {
		struct ecore_hwfn *p_hwfn = OSAL_NULL;

		p_hwfn = &p_dev->hwfns[i];
		OSAL_SPIN_LOCK_DEALLOC(&p_hwfn->dmae_info.lock);
	}
	return ECORE_NOMEM;
#endif
}

static void ecore_qm_info_free(struct ecore_hwfn *p_hwfn)
{
	struct ecore_qm_info *qm_info = &p_hwfn->qm_info;

	OSAL_FREE(p_hwfn->p_dev, qm_info->qm_pq_params);
	qm_info->qm_pq_params = OSAL_NULL;
	OSAL_FREE(p_hwfn->p_dev, qm_info->qm_vport_params);
	qm_info->qm_vport_params = OSAL_NULL;
	OSAL_FREE(p_hwfn->p_dev, qm_info->qm_port_params);
	qm_info->qm_port_params = OSAL_NULL;
	OSAL_FREE(p_hwfn->p_dev, qm_info->wfq_data);
	qm_info->wfq_data = OSAL_NULL;
}

void ecore_resc_free(struct ecore_dev *p_dev)
{
	int i;

	if (IS_VF(p_dev)) {
		for_each_hwfn(p_dev, i)
			ecore_l2_free(&p_dev->hwfns[i]);
		return;
	}

	OSAL_FREE(p_dev, p_dev->fw_data);
	p_dev->fw_data = OSAL_NULL;

	OSAL_FREE(p_dev, p_dev->reset_stats);
	p_dev->reset_stats = OSAL_NULL;

	ecore_llh_free(p_dev);

	for_each_hwfn(p_dev, i) {
		struct ecore_hwfn *p_hwfn = &p_dev->hwfns[i];

		ecore_cxt_mngr_free(p_hwfn);
		ecore_qm_info_free(p_hwfn);
		ecore_spq_free(p_hwfn);
		ecore_eq_free(p_hwfn);
		ecore_consq_free(p_hwfn);
		ecore_int_free(p_hwfn);
#ifdef CONFIG_ECORE_LL2
		ecore_ll2_free(p_hwfn);
#endif
		if (p_hwfn->hw_info.personality == ECORE_PCI_FCOE)
			ecore_fcoe_free(p_hwfn);

		if (p_hwfn->hw_info.personality == ECORE_PCI_ISCSI) {
			ecore_iscsi_free(p_hwfn);
			ecore_ooo_free(p_hwfn);
		}

#ifdef CONFIG_ECORE_ROCE
		if (ECORE_IS_RDMA_PERSONALITY(p_hwfn))
			ecore_rdma_info_free(p_hwfn);
#endif
		ecore_iov_free(p_hwfn);
		ecore_l2_free(p_hwfn);
		ecore_dmae_info_free(p_hwfn);
		ecore_dcbx_info_free(p_hwfn);
		/* @@@TBD Flush work-queue ?*/

		/* destroy doorbell recovery mechanism */
		ecore_db_recovery_teardown(p_hwfn);
	}
}

/******************** QM initialization *******************/
/* bitmaps for indicating active traffic classes. Special case for Arrowhead 4 port */
#define ACTIVE_TCS_BMAP 0x9f /* 0..3 actualy used, 4 serves OOO, 7 serves high priority stuff (e.g. DCQCN) */
#define ACTIVE_TCS_BMAP_4PORT_K2 0xf /* 0..3 actually used, OOO and high priority stuff all use 3 */

/* determines the physical queue flags for a given PF. */
static u32 ecore_get_pq_flags(struct ecore_hwfn *p_hwfn)
{
	u32 flags;

	/* common flags */
	flags = PQ_FLAGS_LB;

	/* feature flags */
	if (IS_ECORE_SRIOV(p_hwfn->p_dev))
		flags |= PQ_FLAGS_VFS;
	if (IS_ECORE_DCQCN(p_hwfn))
		flags |= PQ_FLAGS_RLS;

	/* protocol flags */
	switch (p_hwfn->hw_info.personality) {
	case ECORE_PCI_ETH:
		flags |= PQ_FLAGS_MCOS;
		break;
	case ECORE_PCI_FCOE:
		flags |= PQ_FLAGS_OFLD;
		break;
	case ECORE_PCI_ISCSI:
		flags |= PQ_FLAGS_ACK | PQ_FLAGS_OOO | PQ_FLAGS_OFLD;
		break;
	case ECORE_PCI_ETH_ROCE:
		flags |= PQ_FLAGS_MCOS | PQ_FLAGS_OFLD | PQ_FLAGS_LLT;
		break;
	case ECORE_PCI_ETH_IWARP:
		flags |= PQ_FLAGS_MCOS | PQ_FLAGS_ACK | PQ_FLAGS_OOO | PQ_FLAGS_OFLD;
		break;
	default:
		DP_ERR(p_hwfn, "unknown personality %d\n", p_hwfn->hw_info.personality);
		return 0;
	}

	return flags;
}


/* Getters for resource amounts necessary for qm initialization */
u8 ecore_init_qm_get_num_tcs(struct ecore_hwfn *p_hwfn)
{
	return p_hwfn->hw_info.num_hw_tc;
}

u16 ecore_init_qm_get_num_vfs(struct ecore_hwfn *p_hwfn)
{
	return IS_ECORE_SRIOV(p_hwfn->p_dev) ? p_hwfn->p_dev->p_iov_info->total_vfs : 0;
}

#define NUM_DEFAULT_RLS 1

u16 ecore_init_qm_get_num_pf_rls(struct ecore_hwfn *p_hwfn)
{
	u16 num_pf_rls, num_vfs = ecore_init_qm_get_num_vfs(p_hwfn);

	/* num RLs can't exceed resource amount of rls or vports or the dcqcn qps */
	num_pf_rls = (u16)OSAL_MIN_T(u32, RESC_NUM(p_hwfn, ECORE_RL),
				     (u16)OSAL_MIN_T(u32, RESC_NUM(p_hwfn, ECORE_VPORT),
						     ROCE_DCQCN_RP_MAX_QPS));

	/* make sure after we reserve the default and VF rls we'll have something left */
	if (num_pf_rls < num_vfs + NUM_DEFAULT_RLS) {
		if (IS_ECORE_DCQCN(p_hwfn))
			DP_NOTICE(p_hwfn, false, "no rate limiters left for PF rate limiting [num_pf_rls %d num_vfs %d]\n", num_pf_rls, num_vfs);
		return 0;
	}

	/* subtract rls necessary for VFs and one default one for the PF */
	num_pf_rls -= num_vfs + NUM_DEFAULT_RLS;

	return num_pf_rls;
}

u16 ecore_init_qm_get_num_vports(struct ecore_hwfn *p_hwfn)
{
	u32 pq_flags = ecore_get_pq_flags(p_hwfn);

	/* all pqs share the same vport (hence the 1 below), except for vfs and pf_rl pqs */
	return (!!(PQ_FLAGS_RLS & pq_flags)) * ecore_init_qm_get_num_pf_rls(p_hwfn) +
	       (!!(PQ_FLAGS_VFS & pq_flags)) * ecore_init_qm_get_num_vfs(p_hwfn) + 1;
}

/* calc amount of PQs according to the requested flags */
u16 ecore_init_qm_get_num_pqs(struct ecore_hwfn *p_hwfn)
{
	u32 pq_flags = ecore_get_pq_flags(p_hwfn);

	return (!!(PQ_FLAGS_RLS & pq_flags)) * ecore_init_qm_get_num_pf_rls(p_hwfn) +
	       (!!(PQ_FLAGS_MCOS & pq_flags)) * ecore_init_qm_get_num_tcs(p_hwfn) +
	       (!!(PQ_FLAGS_LB & pq_flags)) +
	       (!!(PQ_FLAGS_OOO & pq_flags)) +
	       (!!(PQ_FLAGS_ACK & pq_flags)) +
	       (!!(PQ_FLAGS_OFLD & pq_flags)) +
	       (!!(PQ_FLAGS_LLT & pq_flags)) +
	       (!!(PQ_FLAGS_VFS & pq_flags)) * ecore_init_qm_get_num_vfs(p_hwfn);
}

/* initialize the top level QM params */
static void ecore_init_qm_params(struct ecore_hwfn *p_hwfn)
{
	struct ecore_qm_info *qm_info = &p_hwfn->qm_info;
	bool four_port;

	/* pq and vport bases for this PF */
	qm_info->start_pq = (u16)RESC_START(p_hwfn, ECORE_PQ);
	qm_info->start_vport = (u8)RESC_START(p_hwfn, ECORE_VPORT);

	/* rate limiting and weighted fair queueing are always enabled */
	qm_info->vport_rl_en = 1;
	qm_info->vport_wfq_en = 1;

	/* TC config is different for AH 4 port */
	four_port = p_hwfn->p_dev->num_ports_in_engine == MAX_NUM_PORTS_K2;

	/* in AH 4 port we have fewer TCs per port */
	qm_info->max_phys_tcs_per_port = four_port ? NUM_PHYS_TCS_4PORT_K2 : NUM_OF_PHYS_TCS;

	/* unless MFW indicated otherwise, ooo_tc should be 3 for AH 4 port and 4 otherwise */
	if (!qm_info->ooo_tc)
		qm_info->ooo_tc = four_port ? DCBX_TCP_OOO_K2_4PORT_TC : DCBX_TCP_OOO_TC;
}

/* initialize qm vport params */
static void ecore_init_qm_vport_params(struct ecore_hwfn *p_hwfn)
{
	struct ecore_qm_info *qm_info = &p_hwfn->qm_info;
	u8 i;

	/* all vports participate in weighted fair queueing */
	for (i = 0; i < ecore_init_qm_get_num_vports(p_hwfn); i++)
		qm_info->qm_vport_params[i].vport_wfq = 1;
}

/* initialize qm port params */
static void ecore_init_qm_port_params(struct ecore_hwfn *p_hwfn)
{
	/* Initialize qm port parameters */
	u8 i, active_phys_tcs, num_ports = p_hwfn->p_dev->num_ports_in_engine;

	/* indicate how ooo and high pri traffic is dealt with */
	active_phys_tcs = num_ports == MAX_NUM_PORTS_K2 ?
		ACTIVE_TCS_BMAP_4PORT_K2 : ACTIVE_TCS_BMAP;

	for (i = 0; i < num_ports; i++) {
		struct init_qm_port_params *p_qm_port =
			&p_hwfn->qm_info.qm_port_params[i];

		p_qm_port->active = 1;
		p_qm_port->active_phys_tcs = active_phys_tcs;
		p_qm_port->num_pbf_cmd_lines = PBF_MAX_CMD_LINES_E4 / num_ports;
		p_qm_port->num_btb_blocks = BTB_MAX_BLOCKS / num_ports;
	}
}

/* Reset the params which must be reset for qm init. QM init may be called as
 * a result of flows other than driver load (e.g. dcbx renegotiation). Other
 * params may be affected by the init but would simply recalculate to the same
 * values. The allocations made for QM init, ports, vports, pqs and vfqs are not
 * affected as these amounts stay the same.
 */
static void ecore_init_qm_reset_params(struct ecore_hwfn *p_hwfn)
{
	struct ecore_qm_info *qm_info = &p_hwfn->qm_info;

	qm_info->num_pqs = 0;
	qm_info->num_vports = 0;
	qm_info->num_pf_rls = 0;
	qm_info->num_vf_pqs = 0;
	qm_info->first_vf_pq = 0;
	qm_info->first_mcos_pq = 0;
	qm_info->first_rl_pq = 0;
}

static void ecore_init_qm_advance_vport(struct ecore_hwfn *p_hwfn)
{
	struct ecore_qm_info *qm_info = &p_hwfn->qm_info;

	qm_info->num_vports++;

	if (qm_info->num_vports > ecore_init_qm_get_num_vports(p_hwfn))
		DP_ERR(p_hwfn, "vport overflow! qm_info->num_vports %d, qm_init_get_num_vports() %d\n", qm_info->num_vports, ecore_init_qm_get_num_vports(p_hwfn));
}

/* initialize a single pq and manage qm_info resources accounting.
 * The pq_init_flags param determines whether the PQ is rate limited (for VF or PF)
 * and whether a new vport is allocated to the pq or not (i.e. vport will be shared)
 */

/* flags for pq init */
#define PQ_INIT_SHARE_VPORT	(1 << 0)
#define PQ_INIT_PF_RL		(1 << 1)
#define PQ_INIT_VF_RL		(1 << 2)

/* defines for pq init */
#define PQ_INIT_DEFAULT_WRR_GROUP	1
#define PQ_INIT_DEFAULT_TC		0
#define PQ_INIT_OFLD_TC			(p_hwfn->hw_info.offload_tc)

static void ecore_init_qm_pq(struct ecore_hwfn *p_hwfn,
			     struct ecore_qm_info *qm_info,
			     u8 tc, u32 pq_init_flags)
{
	u16 pq_idx = qm_info->num_pqs, max_pq = ecore_init_qm_get_num_pqs(p_hwfn);

	if (pq_idx > max_pq)
		DP_ERR(p_hwfn, "pq overflow! pq %d, max pq %d\n", pq_idx, max_pq);

	/* init pq params */
	qm_info->qm_pq_params[pq_idx].vport_id = qm_info->start_vport + qm_info->num_vports;
	qm_info->qm_pq_params[pq_idx].tc_id = tc;
	qm_info->qm_pq_params[pq_idx].wrr_group = PQ_INIT_DEFAULT_WRR_GROUP;
	qm_info->qm_pq_params[pq_idx].rl_valid =
		(pq_init_flags & PQ_INIT_PF_RL || pq_init_flags & PQ_INIT_VF_RL);

	/* qm params accounting */
	qm_info->num_pqs++;
	if (!(pq_init_flags & PQ_INIT_SHARE_VPORT))
		qm_info->num_vports++;

	if (pq_init_flags & PQ_INIT_PF_RL)
		qm_info->num_pf_rls++;

	if (qm_info->num_vports > ecore_init_qm_get_num_vports(p_hwfn))
		DP_ERR(p_hwfn, "vport overflow! qm_info->num_vports %d, qm_init_get_num_vports() %d\n", qm_info->num_vports, ecore_init_qm_get_num_vports(p_hwfn));

	if (qm_info->num_pf_rls > ecore_init_qm_get_num_pf_rls(p_hwfn))
		DP_ERR(p_hwfn, "rl overflow! qm_info->num_pf_rls %d, qm_init_get_num_pf_rls() %d\n", qm_info->num_pf_rls, ecore_init_qm_get_num_pf_rls(p_hwfn));
}

/* get pq index according to PQ_FLAGS */
static u16 *ecore_init_qm_get_idx_from_flags(struct ecore_hwfn *p_hwfn,
					     u32 pq_flags)
{
	struct ecore_qm_info *qm_info = &p_hwfn->qm_info;

	/* Can't have multiple flags set here */
	if (OSAL_BITMAP_WEIGHT((unsigned long *)&pq_flags, sizeof(pq_flags)) > 1)
		goto err;

	switch (pq_flags) {
	case PQ_FLAGS_RLS:
		return &qm_info->first_rl_pq;
	case PQ_FLAGS_MCOS:
		return &qm_info->first_mcos_pq;
	case PQ_FLAGS_LB:
		return &qm_info->pure_lb_pq;
	case PQ_FLAGS_OOO:
		return &qm_info->ooo_pq;
	case PQ_FLAGS_ACK:
		return &qm_info->pure_ack_pq;
	case PQ_FLAGS_OFLD:
		return &qm_info->offload_pq;
	case PQ_FLAGS_LLT:
		return &qm_info->low_latency_pq;
	case PQ_FLAGS_VFS:
		return &qm_info->first_vf_pq;
	default:
		goto err;
	}

err:
	DP_ERR(p_hwfn, "BAD pq flags %d\n", pq_flags);
	return OSAL_NULL;
}

/* save pq index in qm info */
static void ecore_init_qm_set_idx(struct ecore_hwfn *p_hwfn,
				  u32 pq_flags, u16 pq_val)
{
	u16 *base_pq_idx = ecore_init_qm_get_idx_from_flags(p_hwfn, pq_flags);

	*base_pq_idx = p_hwfn->qm_info.start_pq + pq_val;
}

/* get tx pq index, with the PQ TX base already set (ready for context init) */
u16 ecore_get_cm_pq_idx(struct ecore_hwfn *p_hwfn, u32 pq_flags)
{
	u16 *base_pq_idx = ecore_init_qm_get_idx_from_flags(p_hwfn, pq_flags);

	return *base_pq_idx + CM_TX_PQ_BASE;
}

u16 ecore_get_cm_pq_idx_mcos(struct ecore_hwfn *p_hwfn, u8 tc)
{
	u8 max_tc = ecore_init_qm_get_num_tcs(p_hwfn);

	if (tc > max_tc)
		DP_ERR(p_hwfn, "tc %d must be smaller than %d\n", tc, max_tc);

	return ecore_get_cm_pq_idx(p_hwfn, PQ_FLAGS_MCOS) + tc;
}

u16 ecore_get_cm_pq_idx_vf(struct ecore_hwfn *p_hwfn, u16 vf)
{
	u16 max_vf = ecore_init_qm_get_num_vfs(p_hwfn);

	if (vf > max_vf)
		DP_ERR(p_hwfn, "vf %d must be smaller than %d\n", vf, max_vf);

	return ecore_get_cm_pq_idx(p_hwfn, PQ_FLAGS_VFS) + vf;
}

u16 ecore_get_cm_pq_idx_rl(struct ecore_hwfn *p_hwfn, u8 rl)
{
	u16 max_rl = ecore_init_qm_get_num_pf_rls(p_hwfn);

	if (rl > max_rl)
		DP_ERR(p_hwfn, "rl %d must be smaller than %d\n", rl, max_rl);

	return ecore_get_cm_pq_idx(p_hwfn, PQ_FLAGS_RLS) + rl;
}

/* Functions for creating specific types of pqs */
static void ecore_init_qm_lb_pq(struct ecore_hwfn *p_hwfn)
{
	struct ecore_qm_info *qm_info = &p_hwfn->qm_info;

	if (!(ecore_get_pq_flags(p_hwfn) & PQ_FLAGS_LB))
		return;

	ecore_init_qm_set_idx(p_hwfn, PQ_FLAGS_LB, qm_info->num_pqs);
	ecore_init_qm_pq(p_hwfn, qm_info, PURE_LB_TC, PQ_INIT_SHARE_VPORT);
}

static void ecore_init_qm_ooo_pq(struct ecore_hwfn *p_hwfn)
{
	struct ecore_qm_info *qm_info = &p_hwfn->qm_info;

	if (!(ecore_get_pq_flags(p_hwfn) & PQ_FLAGS_OOO))
		return;

	ecore_init_qm_set_idx(p_hwfn, PQ_FLAGS_OOO, qm_info->num_pqs);
	ecore_init_qm_pq(p_hwfn, qm_info, qm_info->ooo_tc, PQ_INIT_SHARE_VPORT);
}

static void ecore_init_qm_pure_ack_pq(struct ecore_hwfn *p_hwfn)
{
	struct ecore_qm_info *qm_info = &p_hwfn->qm_info;

	if (!(ecore_get_pq_flags(p_hwfn) & PQ_FLAGS_ACK))
		return;

	ecore_init_qm_set_idx(p_hwfn, PQ_FLAGS_ACK, qm_info->num_pqs);
	ecore_init_qm_pq(p_hwfn, qm_info, PQ_INIT_OFLD_TC, PQ_INIT_SHARE_VPORT);
}

static void ecore_init_qm_offload_pq(struct ecore_hwfn *p_hwfn)
{
	struct ecore_qm_info *qm_info = &p_hwfn->qm_info;

	if (!(ecore_get_pq_flags(p_hwfn) & PQ_FLAGS_OFLD))
		return;

	ecore_init_qm_set_idx(p_hwfn, PQ_FLAGS_OFLD, qm_info->num_pqs);
	ecore_init_qm_pq(p_hwfn, qm_info, PQ_INIT_OFLD_TC, PQ_INIT_SHARE_VPORT);
}

static void ecore_init_qm_low_latency_pq(struct ecore_hwfn *p_hwfn)
{
	struct ecore_qm_info *qm_info = &p_hwfn->qm_info;

	if (!(ecore_get_pq_flags(p_hwfn) & PQ_FLAGS_LLT))
		return;

	ecore_init_qm_set_idx(p_hwfn, PQ_FLAGS_LLT, qm_info->num_pqs);
	ecore_init_qm_pq(p_hwfn, qm_info, PQ_INIT_OFLD_TC, PQ_INIT_SHARE_VPORT);
}

static void ecore_init_qm_mcos_pqs(struct ecore_hwfn *p_hwfn)
{
	struct ecore_qm_info *qm_info = &p_hwfn->qm_info;
	u8 tc_idx;

	if (!(ecore_get_pq_flags(p_hwfn) & PQ_FLAGS_MCOS))
		return;

	ecore_init_qm_set_idx(p_hwfn, PQ_FLAGS_MCOS, qm_info->num_pqs);
	for (tc_idx = 0; tc_idx < ecore_init_qm_get_num_tcs(p_hwfn); tc_idx++)
		ecore_init_qm_pq(p_hwfn, qm_info, tc_idx, PQ_INIT_SHARE_VPORT);
}

static void ecore_init_qm_vf_pqs(struct ecore_hwfn *p_hwfn)
{
	struct ecore_qm_info *qm_info = &p_hwfn->qm_info;
	u16 vf_idx, num_vfs = ecore_init_qm_get_num_vfs(p_hwfn);

	if (!(ecore_get_pq_flags(p_hwfn) & PQ_FLAGS_VFS))
		return;

	ecore_init_qm_set_idx(p_hwfn, PQ_FLAGS_VFS, qm_info->num_pqs);
	qm_info->num_vf_pqs = num_vfs;
	for (vf_idx = 0; vf_idx < num_vfs; vf_idx++)
		ecore_init_qm_pq(p_hwfn, qm_info, PQ_INIT_DEFAULT_TC, PQ_INIT_VF_RL);
}

static void ecore_init_qm_rl_pqs(struct ecore_hwfn *p_hwfn)
{
	u16 pf_rls_idx, num_pf_rls = ecore_init_qm_get_num_pf_rls(p_hwfn);
	struct ecore_qm_info *qm_info = &p_hwfn->qm_info;

	if (!(ecore_get_pq_flags(p_hwfn) & PQ_FLAGS_RLS))
		return;

	ecore_init_qm_set_idx(p_hwfn, PQ_FLAGS_RLS, qm_info->num_pqs);
	for (pf_rls_idx = 0; pf_rls_idx < num_pf_rls; pf_rls_idx++)
		ecore_init_qm_pq(p_hwfn, qm_info, PQ_INIT_OFLD_TC, PQ_INIT_PF_RL);
}

static void ecore_init_qm_pq_params(struct ecore_hwfn *p_hwfn)
{
	/* rate limited pqs, must come first (FW assumption) */
	ecore_init_qm_rl_pqs(p_hwfn);

	/* pqs for multi cos */
	ecore_init_qm_mcos_pqs(p_hwfn);

	/* pure loopback pq */
	ecore_init_qm_lb_pq(p_hwfn);

	/* out of order pq */
	ecore_init_qm_ooo_pq(p_hwfn);

	/* pure ack pq */
	ecore_init_qm_pure_ack_pq(p_hwfn);

	/* pq for offloaded protocol */
	ecore_init_qm_offload_pq(p_hwfn);

	/* low latency pq */
	ecore_init_qm_low_latency_pq(p_hwfn);

	/* done sharing vports */
	ecore_init_qm_advance_vport(p_hwfn);

	/* pqs for vfs */
	ecore_init_qm_vf_pqs(p_hwfn);
}

/* compare values of getters against resources amounts */
static enum _ecore_status_t ecore_init_qm_sanity(struct ecore_hwfn *p_hwfn)
{
	if (ecore_init_qm_get_num_vports(p_hwfn) > RESC_NUM(p_hwfn, ECORE_VPORT)) {
		DP_ERR(p_hwfn, "requested amount of vports exceeds resource\n");
		return ECORE_INVAL;
	}

	if (ecore_init_qm_get_num_pqs(p_hwfn) > RESC_NUM(p_hwfn, ECORE_PQ)) {
		DP_ERR(p_hwfn, "requested amount of pqs exceeds resource\n");
		return ECORE_INVAL;
	}

	return ECORE_SUCCESS;
}

/*
 * Function for verbose printing of the qm initialization results
 */
static void ecore_dp_init_qm_params(struct ecore_hwfn *p_hwfn)
{
	struct ecore_qm_info *qm_info = &p_hwfn->qm_info;
	struct init_qm_vport_params *vport;
	struct init_qm_port_params *port;
	struct init_qm_pq_params *pq;
	int i, tc;

	/* top level params */
	DP_VERBOSE(p_hwfn, ECORE_MSG_HW, "qm init top level params: start_pq %d, start_vport %d, pure_lb_pq %d, offload_pq %d, pure_ack_pq %d\n",
		   qm_info->start_pq, qm_info->start_vport, qm_info->pure_lb_pq, qm_info->offload_pq, qm_info->pure_ack_pq);
	DP_VERBOSE(p_hwfn, ECORE_MSG_HW, "ooo_pq %d, first_vf_pq %d, num_pqs %d, num_vf_pqs %d, num_vports %d, max_phys_tcs_per_port %d\n",
		   qm_info->ooo_pq, qm_info->first_vf_pq, qm_info->num_pqs, qm_info->num_vf_pqs, qm_info->num_vports, qm_info->max_phys_tcs_per_port);
	DP_VERBOSE(p_hwfn, ECORE_MSG_HW, "pf_rl_en %d, pf_wfq_en %d, vport_rl_en %d, vport_wfq_en %d, pf_wfq %d, pf_rl %d, num_pf_rls %d, pq_flags %x\n",
		   qm_info->pf_rl_en, qm_info->pf_wfq_en, qm_info->vport_rl_en, qm_info->vport_wfq_en, qm_info->pf_wfq, qm_info->pf_rl, qm_info->num_pf_rls, ecore_get_pq_flags(p_hwfn));

	/* port table */
	for (i = 0; i < p_hwfn->p_dev->num_ports_in_engine; i++) {
		port = &(qm_info->qm_port_params[i]);
		DP_VERBOSE(p_hwfn, ECORE_MSG_HW, "port idx %d, active %d, active_phys_tcs %d, num_pbf_cmd_lines %d, num_btb_blocks %d, reserved %d\n",
			   i, port->active, port->active_phys_tcs, port->num_pbf_cmd_lines, port->num_btb_blocks, port->reserved);
	}

	/* vport table */
	for (i = 0; i < qm_info->num_vports; i++) {
		vport = &(qm_info->qm_vport_params[i]);
		DP_VERBOSE(p_hwfn, ECORE_MSG_HW, "vport idx %d, vport_rl %d, wfq %d, first_tx_pq_id [ ",
			   qm_info->start_vport + i, vport->vport_rl, vport->vport_wfq);
		for (tc = 0; tc < NUM_OF_TCS; tc++)
			DP_VERBOSE(p_hwfn, ECORE_MSG_HW, "%d ", vport->first_tx_pq_id[tc]);
		DP_VERBOSE(p_hwfn, ECORE_MSG_HW, "]\n");
	}

	/* pq table */
	for (i = 0; i < qm_info->num_pqs; i++) {
		pq = &(qm_info->qm_pq_params[i]);
		DP_VERBOSE(p_hwfn, ECORE_MSG_HW, "pq idx %d, vport_id %d, tc %d, wrr_grp %d, rl_valid %d\n",
			   qm_info->start_pq + i, pq->vport_id, pq->tc_id, pq->wrr_group, pq->rl_valid);
	}
}

static void ecore_init_qm_info(struct ecore_hwfn *p_hwfn)
{
	/* reset params required for init run */
	ecore_init_qm_reset_params(p_hwfn);

	/* init QM top level params */
	ecore_init_qm_params(p_hwfn);

	/* init QM port params */
	ecore_init_qm_port_params(p_hwfn);

	/* init QM vport params */
	ecore_init_qm_vport_params(p_hwfn);

	/* init QM physical queue params */
	ecore_init_qm_pq_params(p_hwfn);

	/* display all that init */
	ecore_dp_init_qm_params(p_hwfn);
}

/* This function reconfigures the QM pf on the fly.
 * For this purpose we:
 * 1. reconfigure the QM database
 * 2. set new values to runtime array
 * 3. send an sdm_qm_cmd through the rbc interface to stop the QM
 * 4. activate init tool in QM_PF stage
 * 5. send an sdm_qm_cmd through rbc interface to release the QM
 */
enum _ecore_status_t ecore_qm_reconf(struct ecore_hwfn *p_hwfn,
				     struct ecore_ptt *p_ptt)
{
	struct ecore_qm_info *qm_info = &p_hwfn->qm_info;
	bool b_rc;
	enum _ecore_status_t rc;

	/* initialize ecore's qm data structure */
	ecore_init_qm_info(p_hwfn);

	/* stop PF's qm queues */
	OSAL_SPIN_LOCK(&qm_lock);
	b_rc = ecore_send_qm_stop_cmd(p_hwfn, p_ptt, false, true,
				      qm_info->start_pq, qm_info->num_pqs);
	OSAL_SPIN_UNLOCK(&qm_lock);
	if (!b_rc)
		return ECORE_INVAL;

	/* clear the QM_PF runtime phase leftovers from previous init */
	ecore_init_clear_rt_data(p_hwfn);

	/* prepare QM portion of runtime array */
	ecore_qm_init_pf(p_hwfn, p_ptt, false);

	/* activate init tool on runtime array */
	rc = ecore_init_run(p_hwfn, p_ptt, PHASE_QM_PF, p_hwfn->rel_pf_id,
			    p_hwfn->hw_info.hw_mode);
	if (rc != ECORE_SUCCESS)
		return rc;

	/* start PF's qm queues */
	OSAL_SPIN_LOCK(&qm_lock);
	b_rc = ecore_send_qm_stop_cmd(p_hwfn, p_ptt, true, true,
				      qm_info->start_pq, qm_info->num_pqs);
	OSAL_SPIN_UNLOCK(&qm_lock);
	if (!b_rc)
		return ECORE_INVAL;

	return ECORE_SUCCESS;
}

static enum _ecore_status_t ecore_alloc_qm_data(struct ecore_hwfn *p_hwfn)
{
	struct ecore_qm_info *qm_info = &p_hwfn->qm_info;
	enum _ecore_status_t rc;

	rc = ecore_init_qm_sanity(p_hwfn);
	if (rc != ECORE_SUCCESS)
		goto alloc_err;

	qm_info->qm_pq_params = OSAL_ZALLOC(p_hwfn->p_dev, GFP_KERNEL,
					    sizeof(struct init_qm_pq_params) *
					    ecore_init_qm_get_num_pqs(p_hwfn));
	if (!qm_info->qm_pq_params)
		goto alloc_err;

	qm_info->qm_vport_params = OSAL_ZALLOC(p_hwfn->p_dev, GFP_KERNEL,
					       sizeof(struct init_qm_vport_params) *
					       ecore_init_qm_get_num_vports(p_hwfn));
	if (!qm_info->qm_vport_params)
		goto alloc_err;

	qm_info->qm_port_params = OSAL_ZALLOC(p_hwfn->p_dev, GFP_KERNEL,
					      sizeof(struct init_qm_port_params) *
					      p_hwfn->p_dev->num_ports_in_engine);
	if (!qm_info->qm_port_params)
		goto alloc_err;

	qm_info->wfq_data = OSAL_ZALLOC(p_hwfn->p_dev, GFP_KERNEL,
					sizeof(struct ecore_wfq_data) *
					ecore_init_qm_get_num_vports(p_hwfn));
	if (!qm_info->wfq_data)
		goto alloc_err;

	return ECORE_SUCCESS;

alloc_err:
	DP_NOTICE(p_hwfn, false, "Failed to allocate memory for QM params\n");
	ecore_qm_info_free(p_hwfn);
	return ECORE_NOMEM;
}
/******************** End QM initialization ***************/

enum _ecore_status_t ecore_resc_alloc(struct ecore_dev *p_dev)
{
	u32 rdma_tasks, excess_tasks;
	u32 line_count;
	enum _ecore_status_t rc = ECORE_SUCCESS;
	int i;

	if (IS_VF(p_dev)) {
		for_each_hwfn(p_dev, i) {
			rc = ecore_l2_alloc(&p_dev->hwfns[i]);
			if (rc != ECORE_SUCCESS)
				return rc;
		}
		return rc;
	}

	p_dev->fw_data = OSAL_ZALLOC(p_dev, GFP_KERNEL,
				     sizeof(*p_dev->fw_data));
	if (!p_dev->fw_data)
		return ECORE_NOMEM;

	for_each_hwfn(p_dev, i) {
		struct ecore_hwfn *p_hwfn = &p_dev->hwfns[i];
		u32 n_eqes, num_cons;

		/* initialize the doorbell recovery mechanism */
		rc = ecore_db_recovery_setup(p_hwfn);
		if (rc)
			goto alloc_err;

		/* First allocate the context manager structure */
		rc = ecore_cxt_mngr_alloc(p_hwfn);
		if (rc)
			goto alloc_err;

		/* Set the HW cid/tid numbers (in the context manager)
		 * Must be done prior to any further computations.
		 */
		rc = ecore_cxt_set_pf_params(p_hwfn, RDMA_MAX_TIDS);
		if (rc)
			goto alloc_err;

		rc = ecore_alloc_qm_data(p_hwfn);
		if (rc)
			goto alloc_err;

		/* init qm info */
		ecore_init_qm_info(p_hwfn);

		/* Compute the ILT client partition */
		rc = ecore_cxt_cfg_ilt_compute(p_hwfn, &line_count);
		if (rc) {
			DP_NOTICE(p_hwfn, false, "too many ILT lines; re-computing with less lines\n");
			/* In case there are not enough ILT lines we reduce the
			 * number of RDMA tasks and re-compute.
			 */
			excess_tasks = ecore_cxt_cfg_ilt_compute_excess(
					p_hwfn, line_count);
			if (!excess_tasks)
				goto alloc_err;

			rdma_tasks = RDMA_MAX_TIDS - excess_tasks;
			rc = ecore_cxt_set_pf_params(p_hwfn, rdma_tasks);
			if (rc)
				goto alloc_err;

			rc = ecore_cxt_cfg_ilt_compute(p_hwfn, &line_count);
			if (rc) {
				DP_ERR(p_hwfn, "failed ILT compute. Requested too many lines: %u\n",
				       line_count);

				goto alloc_err;
			}
		}

		/* CID map / ILT shadow table / T2
		 * The talbes sizes are determined by the computations above
		 */
		rc = ecore_cxt_tables_alloc(p_hwfn);
		if (rc)
			goto alloc_err;

		/* SPQ, must follow ILT because initializes SPQ context */
		rc = ecore_spq_alloc(p_hwfn);
		if (rc)
			goto alloc_err;

		/* SP status block allocation */
		p_hwfn->p_dpc_ptt = ecore_get_reserved_ptt(p_hwfn,
							   RESERVED_PTT_DPC);

		rc = ecore_int_alloc(p_hwfn, p_hwfn->p_main_ptt);
		if (rc)
			goto alloc_err;

		rc = ecore_iov_alloc(p_hwfn);
		if (rc)
			goto alloc_err;

		/* EQ */
		n_eqes = ecore_chain_get_capacity(&p_hwfn->p_spq->chain);
		if (ECORE_IS_RDMA_PERSONALITY(p_hwfn)) {
			u32 n_srq = ecore_cxt_get_total_srq_count(p_hwfn);

			/* Calculate the EQ size
			 * ---------------------
			 * Each ICID may generate up to one event at a time i.e.
			 * the event must be handled/cleared before a new one
			 * can be generated. We calculate the sum of events per
			 * protocol and create an EQ deep enough to handle the
			 * worst case:
			 * - Core - according to SPQ.
			 * - RoCE - per QP there are a couple of ICIDs, one
			 *	  responder and one requester, each can
			 *	  generate max 2 EQE (err+qp_destroyed) =>
			 *	  n_eqes_qp = 4 * n_qp.
			 *	  Each CQ can generate an EQE. There are 2 CQs
			 *	  per QP => n_eqes_cq = 2 * n_qp.
			 *	  Hence the RoCE total is 6 * n_qp or
			 *	  3 * num_cons.
			 *	  On top of that one eqe shoule be added for
			 *	  each XRC SRQ and SRQ.
			 * - iWARP - can generate three async per QP (error
			 *	  detected and qp in error) and an
			 	  additional error per CQ. 4* num_cons.
			 	  On top of that one eqe shoule be added for
			 *	  each SRQ and XRC SRQ.
			 * - ENet - There can be up to two events per VF. One
			 *	  for VF-PF channel and another for VF FLR
			 *	  initial cleanup. The number of VFs is
			 *	  bounded by MAX_NUM_VFS_BB, and is much
			 *	  smaller than RoCE's so we avoid exact
			 *	  calculation.
			 */
			if (p_hwfn->hw_info.personality == ECORE_PCI_ETH_ROCE) {
				num_cons = ecore_cxt_get_proto_cid_count(
					p_hwfn, PROTOCOLID_ROCE, OSAL_NULL);
				num_cons *= 3;
			} else {
				num_cons = ecore_cxt_get_proto_cid_count(
						p_hwfn, PROTOCOLID_IWARP,
						OSAL_NULL);
				num_cons *= 4;
			}
			n_eqes += num_cons + 2 * MAX_NUM_VFS_BB + n_srq;
		} else if (p_hwfn->hw_info.personality == ECORE_PCI_ISCSI) {
			num_cons = ecore_cxt_get_proto_cid_count(
					p_hwfn, PROTOCOLID_ISCSI, OSAL_NULL);
			n_eqes += 2 * num_cons;
		}

		if (n_eqes > 0xFF00) {
			DP_ERR(p_hwfn, "EQs maxing out at 0xFF00 elements\n");
			n_eqes = 0xFF00;
		}

		rc = ecore_eq_alloc(p_hwfn, (u16)n_eqes);
		if (rc)
			goto alloc_err;

		rc = ecore_consq_alloc(p_hwfn);
		if (rc)
			goto alloc_err;

		rc = ecore_l2_alloc(p_hwfn);
		if (rc != ECORE_SUCCESS)
			goto alloc_err;

#ifdef CONFIG_ECORE_LL2
		if (p_hwfn->using_ll2) {
			rc = ecore_ll2_alloc(p_hwfn);
			if (rc)
				goto alloc_err;
		}
#endif
		if (p_hwfn->hw_info.personality == ECORE_PCI_FCOE) {
			rc = ecore_fcoe_alloc(p_hwfn);
			if (rc)
				goto alloc_err;
		}

		if (p_hwfn->hw_info.personality == ECORE_PCI_ISCSI) {
			rc = ecore_iscsi_alloc(p_hwfn);
			if (rc)
				goto alloc_err;

			rc = ecore_ooo_alloc(p_hwfn);
			if (rc)
				goto alloc_err;
		}
#ifdef CONFIG_ECORE_ROCE
		if (ECORE_IS_RDMA_PERSONALITY(p_hwfn)) {
			rc = ecore_rdma_info_alloc(p_hwfn);
			if (rc)
				goto alloc_err;
		}
#endif

		/* DMA info initialization */
		rc = ecore_dmae_info_alloc(p_hwfn);
		if (rc) {
			DP_NOTICE(p_hwfn, false,
				  "Failed to allocate memory for dmae_info structure\n");
			goto alloc_err;
		}

		/* DCBX initialization */
		rc = ecore_dcbx_info_alloc(p_hwfn);
		if (rc) {
			DP_NOTICE(p_hwfn, false,
				  "Failed to allocate memory for dcbx structure\n");
			goto alloc_err;
		}
	}

	rc = ecore_llh_alloc(p_dev);
	if (rc != ECORE_SUCCESS) {
		DP_NOTICE(p_dev, false,
			  "Failed to allocate memory for the llh_info structure\n");
		goto alloc_err;
	}

	p_dev->reset_stats = OSAL_ZALLOC(p_dev, GFP_KERNEL,
					 sizeof(*p_dev->reset_stats));
	if (!p_dev->reset_stats) {
		DP_NOTICE(p_dev, false,
			  "Failed to allocate reset statistics\n");
		goto alloc_no_mem;
	}

	return ECORE_SUCCESS;

alloc_no_mem:
	rc = ECORE_NOMEM;
alloc_err:
	ecore_resc_free(p_dev);
	return rc;
}

void ecore_resc_setup(struct ecore_dev *p_dev)
{
	int i;

	if (IS_VF(p_dev)) {
		for_each_hwfn(p_dev, i)
			ecore_l2_setup(&p_dev->hwfns[i]);
		return;
	}

	for_each_hwfn(p_dev, i) {
		struct ecore_hwfn *p_hwfn = &p_dev->hwfns[i];

		ecore_cxt_mngr_setup(p_hwfn);
		ecore_spq_setup(p_hwfn);
		ecore_eq_setup(p_hwfn);
		ecore_consq_setup(p_hwfn);

		/* Read shadow of current MFW mailbox */
		ecore_mcp_read_mb(p_hwfn, p_hwfn->p_main_ptt);
		OSAL_MEMCPY(p_hwfn->mcp_info->mfw_mb_shadow,
			    p_hwfn->mcp_info->mfw_mb_cur,
			    p_hwfn->mcp_info->mfw_mb_length);

		ecore_int_setup(p_hwfn, p_hwfn->p_main_ptt);

		ecore_l2_setup(p_hwfn);
		ecore_iov_setup(p_hwfn);
#ifdef CONFIG_ECORE_LL2
		if (p_hwfn->using_ll2)
			ecore_ll2_setup(p_hwfn);
#endif
		if (p_hwfn->hw_info.personality == ECORE_PCI_FCOE)
			ecore_fcoe_setup(p_hwfn);

		if (p_hwfn->hw_info.personality == ECORE_PCI_ISCSI) {
			ecore_iscsi_setup(p_hwfn);
			ecore_ooo_setup(p_hwfn);
		}
	}
}

#define FINAL_CLEANUP_POLL_CNT	(100)
#define FINAL_CLEANUP_POLL_TIME	(10)
enum _ecore_status_t ecore_final_cleanup(struct ecore_hwfn *p_hwfn,
					 struct ecore_ptt *p_ptt,
					 u16 id, bool is_vf)
{
	u32 command = 0, addr, count = FINAL_CLEANUP_POLL_CNT;
	enum _ecore_status_t rc = ECORE_TIMEOUT;

#ifndef ASIC_ONLY
	if (CHIP_REV_IS_TEDIBEAR(p_hwfn->p_dev) ||
	    CHIP_REV_IS_SLOW(p_hwfn->p_dev)) {
		DP_INFO(p_hwfn, "Skipping final cleanup for non-ASIC\n");
		return ECORE_SUCCESS;
	}
#endif

	addr = GTT_BAR0_MAP_REG_USDM_RAM +
	       USTORM_FLR_FINAL_ACK_OFFSET(p_hwfn->rel_pf_id);

	if (is_vf)
		id += 0x10;

	command |= X_FINAL_CLEANUP_AGG_INT <<
		   SDM_AGG_INT_COMP_PARAMS_AGG_INT_INDEX_SHIFT;
	command |= 1 << SDM_AGG_INT_COMP_PARAMS_AGG_VECTOR_ENABLE_SHIFT;
	command |= id << SDM_AGG_INT_COMP_PARAMS_AGG_VECTOR_BIT_SHIFT;
	command |= SDM_COMP_TYPE_AGG_INT << SDM_OP_GEN_COMP_TYPE_SHIFT;

	/* Make sure notification is not set before initiating final cleanup */
	if (REG_RD(p_hwfn, addr)) {
		DP_NOTICE(p_hwfn, false,
			  "Unexpected; Found final cleanup notification before initiating final cleanup\n");
		REG_WR(p_hwfn, addr, 0);
	}

	DP_VERBOSE(p_hwfn, ECORE_MSG_IOV,
		   "Sending final cleanup for PFVF[%d] [Command %08x]\n",
		   id, command);

	ecore_wr(p_hwfn, p_ptt, XSDM_REG_OPERATION_GEN, command);

	/* Poll until completion */
	while (!REG_RD(p_hwfn, addr) && count--)
		OSAL_MSLEEP(FINAL_CLEANUP_POLL_TIME);

	if (REG_RD(p_hwfn, addr))
		rc = ECORE_SUCCESS;
	else
		DP_NOTICE(p_hwfn, true, "Failed to receive FW final cleanup notification\n");

	/* Cleanup afterwards */
	REG_WR(p_hwfn, addr, 0);

	return rc;
}

static enum _ecore_status_t ecore_calc_hw_mode(struct ecore_hwfn *p_hwfn)
{
	int hw_mode = 0;

	if (ECORE_IS_BB_B0(p_hwfn->p_dev)) {
		hw_mode |= 1 << MODE_BB;
	} else if (ECORE_IS_AH(p_hwfn->p_dev)) {
		hw_mode |= 1 << MODE_K2;
	} else if (ECORE_IS_E5(p_hwfn->p_dev)) {
		hw_mode |= 1 << MODE_E5;
	} else {
		DP_NOTICE(p_hwfn, true, "Unknown chip type %#x\n",
			  p_hwfn->p_dev->type);
		return ECORE_INVAL;
	}

	/* Ports per engine is based on the values in CNIG_REG_NW_PORT_MODE*/
	switch (p_hwfn->p_dev->num_ports_in_engine) {
	case 1:
		hw_mode |= 1 << MODE_PORTS_PER_ENG_1;
		break;
	case 2:
		hw_mode |= 1 << MODE_PORTS_PER_ENG_2;
		break;
	case 4:
		hw_mode |= 1 << MODE_PORTS_PER_ENG_4;
		break;
	default:
		DP_NOTICE(p_hwfn, true, "num_ports_in_engine = %d not supported\n",
			  p_hwfn->p_dev->num_ports_in_engine);
		return ECORE_INVAL;
	}

	if (OSAL_TEST_BIT(ECORE_MF_OVLAN_CLSS,
			  &p_hwfn->p_dev->mf_bits))
		hw_mode |= 1 << MODE_MF_SD;
	else
		hw_mode |= 1 << MODE_MF_SI;

#ifndef ASIC_ONLY
	if (CHIP_REV_IS_SLOW(p_hwfn->p_dev)) {
		if (CHIP_REV_IS_FPGA(p_hwfn->p_dev)) {
			hw_mode |= 1 << MODE_FPGA;
		} else {
			if (p_hwfn->p_dev->b_is_emul_full)
				hw_mode |= 1 << MODE_EMUL_FULL;
			else
				hw_mode |= 1 << MODE_EMUL_REDUCED;
		}
	} else
#endif
	hw_mode |= 1 << MODE_ASIC;

	if (ECORE_IS_CMT(p_hwfn->p_dev))
		hw_mode |= 1 << MODE_100G;

	p_hwfn->hw_info.hw_mode = hw_mode;

	DP_VERBOSE(p_hwfn, (ECORE_MSG_PROBE | ECORE_MSG_IFUP),
		   "Configuring function for hw_mode: 0x%08x\n",
		   p_hwfn->hw_info.hw_mode);

	return ECORE_SUCCESS;
}

#ifndef ASIC_ONLY
/* MFW-replacement initializations for non-ASIC */
static enum _ecore_status_t ecore_hw_init_chip(struct ecore_hwfn *p_hwfn,
					       struct ecore_ptt *p_ptt)
{
	struct ecore_dev *p_dev = p_hwfn->p_dev;
	u32 pl_hv = 1;
	int i;

	if (CHIP_REV_IS_EMUL(p_dev)) {
		if (ECORE_IS_AH(p_dev))
			pl_hv |= 0x600;
		else if (ECORE_IS_E5(p_dev))
			ECORE_E5_MISSING_CODE;
	}

	ecore_wr(p_hwfn, p_ptt, MISCS_REG_RESET_PL_HV + 4, pl_hv);

	if (CHIP_REV_IS_EMUL(p_dev) &&
	    (ECORE_IS_AH(p_dev) || ECORE_IS_E5(p_dev)))
		ecore_wr(p_hwfn, p_ptt, MISCS_REG_RESET_PL_HV_2_K2_E5,
			 0x3ffffff);

	/* initialize port mode to 4x10G_E (10G with 4x10 SERDES) */
	/* CNIG_REG_NW_PORT_MODE is same for A0 and B0 */
	if (!CHIP_REV_IS_EMUL(p_dev) || ECORE_IS_BB(p_dev))
		ecore_wr(p_hwfn, p_ptt, CNIG_REG_NW_PORT_MODE_BB, 4);

	if (CHIP_REV_IS_EMUL(p_dev)) {
		if (ECORE_IS_AH(p_dev)) {
			/* 2 for 4-port, 1 for 2-port, 0 for 1-port */
			ecore_wr(p_hwfn, p_ptt, MISC_REG_PORT_MODE,
				 (p_dev->num_ports_in_engine >> 1));

			ecore_wr(p_hwfn, p_ptt, MISC_REG_BLOCK_256B_EN,
				 p_dev->num_ports_in_engine == 4 ? 0 : 3);
		} else if (ECORE_IS_E5(p_dev)) {
			ECORE_E5_MISSING_CODE;
		}

		/* Poll on RBC */
		ecore_wr(p_hwfn, p_ptt, PSWRQ2_REG_RBC_DONE, 1);
		for (i = 0; i < 100; i++) {
			OSAL_UDELAY(50);
			if (ecore_rd(p_hwfn, p_ptt, PSWRQ2_REG_CFG_DONE) == 1)
				break;
		}
		if (i == 100)
			DP_NOTICE(p_hwfn, true,
				  "RBC done failed to complete in PSWRQ2\n");
	}

	return ECORE_SUCCESS;
}
#endif

/* Init run time data for all PFs and their VFs on an engine.
 * TBD - for VFs - Once we have parent PF info for each VF in
 * shmem available as CAU requires knowledge of parent PF for each VF.
 */
static void ecore_init_cau_rt_data(struct ecore_dev *p_dev)
{
	u32 offset = CAU_REG_SB_VAR_MEMORY_RT_OFFSET;
	int i, igu_sb_id;

	for_each_hwfn(p_dev, i) {
		struct ecore_hwfn *p_hwfn = &p_dev->hwfns[i];
		struct ecore_igu_info *p_igu_info;
		struct ecore_igu_block *p_block;
		struct cau_sb_entry sb_entry;

		p_igu_info = p_hwfn->hw_info.p_igu_info;

		for (igu_sb_id = 0;
		     igu_sb_id < ECORE_MAPPING_MEMORY_SIZE(p_dev);
		     igu_sb_id++) {
			p_block = &p_igu_info->entry[igu_sb_id];

			if (!p_block->is_pf)
				continue;

			ecore_init_cau_sb_entry(p_hwfn, &sb_entry,
						p_block->function_id,
						0, 0);
			STORE_RT_REG_AGG(p_hwfn, offset + igu_sb_id * 2,
					 sb_entry);
		}
	}
}

static void ecore_init_cache_line_size(struct ecore_hwfn *p_hwfn,
				       struct ecore_ptt *p_ptt)
{
	u32 val, wr_mbs, cache_line_size;

	val = ecore_rd(p_hwfn, p_ptt, PSWRQ2_REG_WR_MBS0);
	switch (val) {
	case 0:
		wr_mbs = 128;
		break;
	case 1:
		wr_mbs = 256;
		break;
	case 2:
		wr_mbs = 512;
		break;
	default:
		DP_INFO(p_hwfn,
			"Unexpected value of PSWRQ2_REG_WR_MBS0 [0x%x]. Avoid configuring PGLUE_B_REG_CACHE_LINE_SIZE.\n",
			val);
		return;
	}

	cache_line_size = OSAL_MIN_T(u32, OSAL_CACHE_LINE_SIZE, wr_mbs);
	switch (cache_line_size) {
	case 32:
		val = 0;
		break;
	case 64:
		val = 1;
		break;
	case 128:
		val = 2;
		break;
	case 256:
		val = 3;
		break;
	default:
		DP_INFO(p_hwfn,
			"Unexpected value of cache line size [0x%x]. Avoid configuring PGLUE_B_REG_CACHE_LINE_SIZE.\n",
			cache_line_size);
	}

	if (OSAL_CACHE_LINE_SIZE > wr_mbs)
		DP_INFO(p_hwfn,
			"The cache line size for padding is suboptimal for performance [OS cache line size 0x%x, wr mbs 0x%x]\n",
			OSAL_CACHE_LINE_SIZE, wr_mbs);

	STORE_RT_REG(p_hwfn, PGLUE_REG_B_CACHE_LINE_SIZE_RT_OFFSET, val);
	if (val > 0) {
		STORE_RT_REG(p_hwfn, PSWRQ2_REG_DRAM_ALIGN_WR_RT_OFFSET, val);
		STORE_RT_REG(p_hwfn, PSWRQ2_REG_DRAM_ALIGN_RD_RT_OFFSET, val);
	}
}

static enum _ecore_status_t ecore_hw_init_common(struct ecore_hwfn *p_hwfn,
						 struct ecore_ptt *p_ptt,
						 int hw_mode)
{
	struct ecore_qm_info *qm_info = &p_hwfn->qm_info;
	struct ecore_dev *p_dev = p_hwfn->p_dev;
	u8 vf_id, max_num_vfs;
	u16 num_pfs, pf_id;
	u32 concrete_fid;
	enum _ecore_status_t rc	= ECORE_SUCCESS;

	ecore_init_cau_rt_data(p_dev);

	/* Program GTT windows */
	ecore_gtt_init(p_hwfn, p_ptt);

#ifndef ASIC_ONLY
	if (CHIP_REV_IS_EMUL(p_dev)) {
		rc = ecore_hw_init_chip(p_hwfn, p_ptt);
		if (rc != ECORE_SUCCESS)
			return rc;
	}
#endif

	if (p_hwfn->mcp_info) {
		if (p_hwfn->mcp_info->func_info.bandwidth_max)
			qm_info->pf_rl_en = 1;
		if (p_hwfn->mcp_info->func_info.bandwidth_min)
			qm_info->pf_wfq_en = 1;
	}

	ecore_qm_common_rt_init(p_hwfn,
				p_dev->num_ports_in_engine,
				qm_info->max_phys_tcs_per_port,
				qm_info->pf_rl_en, qm_info->pf_wfq_en,
				qm_info->vport_rl_en, qm_info->vport_wfq_en,
				qm_info->qm_port_params);

	ecore_cxt_hw_init_common(p_hwfn);

	ecore_init_cache_line_size(p_hwfn, p_ptt);

	rc = ecore_init_run(p_hwfn, p_ptt, PHASE_ENGINE, ECORE_PATH_ID(p_hwfn),
			    hw_mode);
	if (rc != ECORE_SUCCESS)
		return rc;

	/* @@TBD MichalK - should add VALIDATE_VFID to init tool...
	 * need to decide with which value, maybe runtime
	 */
	ecore_wr(p_hwfn, p_ptt, PSWRQ2_REG_L2P_VALIDATE_VFID, 0);
	ecore_wr(p_hwfn, p_ptt, PGLUE_B_REG_USE_CLIENTID_IN_TAG, 1);

	if (ECORE_IS_BB(p_dev)) {
		/* Workaround clears ROCE search for all functions to prevent
		 * involving non initialized function in processing ROCE packet.
		 */
		num_pfs = NUM_OF_ENG_PFS(p_dev);
		for (pf_id = 0; pf_id < num_pfs; pf_id++) {
			ecore_fid_pretend(p_hwfn, p_ptt, pf_id);
			ecore_wr(p_hwfn, p_ptt, PRS_REG_SEARCH_ROCE, 0x0);
			ecore_wr(p_hwfn, p_ptt, PRS_REG_SEARCH_TCP, 0x0);
		}
		/* pretend to original PF */
		ecore_fid_pretend(p_hwfn, p_ptt, p_hwfn->rel_pf_id);
	}

	/* Workaround for avoiding CCFC execution error when getting packets
	 * with CRC errors, and allowing instead the invoking of the FW error
	 * handler.
	 * This is not done inside the init tool since it currently can't
	 * perform a pretending to VFs.
	 */
	max_num_vfs = ECORE_IS_AH(p_dev) ? MAX_NUM_VFS_K2 : MAX_NUM_VFS_BB;
	for (vf_id = 0; vf_id < max_num_vfs; vf_id++) {
		concrete_fid = ecore_vfid_to_concrete(p_hwfn, vf_id);
		ecore_fid_pretend(p_hwfn, p_ptt, (u16)concrete_fid);
		ecore_wr(p_hwfn, p_ptt, CCFC_REG_STRONG_ENABLE_VF, 0x1);
		ecore_wr(p_hwfn, p_ptt, CCFC_REG_WEAK_ENABLE_VF, 0x0);
		ecore_wr(p_hwfn, p_ptt, TCFC_REG_STRONG_ENABLE_VF, 0x1);
		ecore_wr(p_hwfn, p_ptt, TCFC_REG_WEAK_ENABLE_VF, 0x0);
	}
	/* pretend to original PF */
	ecore_fid_pretend(p_hwfn, p_ptt, p_hwfn->rel_pf_id);

	return rc;
}

#ifndef ASIC_ONLY
#define MISC_REG_RESET_REG_2_XMAC_BIT (1<<4)
#define MISC_REG_RESET_REG_2_XMAC_SOFT_BIT (1<<5)

#define PMEG_IF_BYTE_COUNT	8

static void ecore_wr_nw_port(struct ecore_hwfn	*p_hwfn,
			     struct ecore_ptt	*p_ptt,
			     u32		addr,
			     u64		data,
			     u8			reg_type,
			     u8			port)
{
	DP_VERBOSE(p_hwfn, ECORE_MSG_LINK,
		   "CMD: %08x, ADDR: 0x%08x, DATA: %08x:%08x\n",
		   ecore_rd(p_hwfn, p_ptt, CNIG_REG_PMEG_IF_CMD_BB) |
		   (8 << PMEG_IF_BYTE_COUNT),
		   (reg_type << 25) | (addr << 8) | port,
		   (u32)((data >> 32) & 0xffffffff),
		   (u32)(data & 0xffffffff));

	ecore_wr(p_hwfn, p_ptt, CNIG_REG_PMEG_IF_CMD_BB,
		 (ecore_rd(p_hwfn, p_ptt, CNIG_REG_PMEG_IF_CMD_BB) &
		  0xffff00fe) |
		 (8 << PMEG_IF_BYTE_COUNT));
	ecore_wr(p_hwfn, p_ptt, CNIG_REG_PMEG_IF_ADDR_BB,
		 (reg_type << 25) | (addr << 8) | port);
	ecore_wr(p_hwfn, p_ptt, CNIG_REG_PMEG_IF_WRDATA_BB, data & 0xffffffff);
	ecore_wr(p_hwfn, p_ptt, CNIG_REG_PMEG_IF_WRDATA_BB,
		 (data >> 32) & 0xffffffff);
}

#define XLPORT_MODE_REG	(0x20a)
#define XLPORT_MAC_CONTROL (0x210)
#define XLPORT_FLOW_CONTROL_CONFIG (0x207)
#define XLPORT_ENABLE_REG (0x20b)

#define XLMAC_CTRL (0x600)
#define XLMAC_MODE (0x601)
#define XLMAC_RX_MAX_SIZE (0x608)
#define XLMAC_TX_CTRL (0x604)
#define XLMAC_PAUSE_CTRL (0x60d)
#define XLMAC_PFC_CTRL (0x60e)

static void ecore_emul_link_init_bb(struct ecore_hwfn *p_hwfn,
				    struct ecore_ptt *p_ptt)
{
	u8 loopback = 0, port = p_hwfn->port_id * 2;

	DP_INFO(p_hwfn->p_dev, "Configurating Emulation Link %02x\n", port);

	ecore_wr_nw_port(p_hwfn, p_ptt, XLPORT_MODE_REG,
			 (0x4 << 4) | 0x4, 1, port); /* XLPORT MAC MODE */ /* 0 Quad, 4 Single... */
	ecore_wr_nw_port(p_hwfn, p_ptt, XLPORT_MAC_CONTROL, 0, 1, port);
	ecore_wr_nw_port(p_hwfn, p_ptt, XLMAC_CTRL,
			 0x40, 0, port); /*XLMAC: SOFT RESET */
	ecore_wr_nw_port(p_hwfn, p_ptt, XLMAC_MODE,
			 0x40, 0, port); /*XLMAC: Port Speed >= 10Gbps */
	ecore_wr_nw_port(p_hwfn, p_ptt, XLMAC_RX_MAX_SIZE,
			 0x3fff, 0, port); /* XLMAC: Max Size */
	ecore_wr_nw_port(p_hwfn, p_ptt, XLMAC_TX_CTRL,
			 0x01000000800ULL | (0xa << 12) | ((u64)1 << 38),
			 0, port);
	ecore_wr_nw_port(p_hwfn, p_ptt, XLMAC_PAUSE_CTRL,
			 0x7c000, 0, port);
	ecore_wr_nw_port(p_hwfn, p_ptt, XLMAC_PFC_CTRL,
			 0x30ffffc000ULL, 0, port);
	ecore_wr_nw_port(p_hwfn, p_ptt, XLMAC_CTRL, 0x3 | (loopback << 2),
			 0, port); /* XLMAC: TX_EN, RX_EN */
	ecore_wr_nw_port(p_hwfn, p_ptt, XLMAC_CTRL, 0x1003 | (loopback << 2),
			 0, port); /* XLMAC: TX_EN, RX_EN, SW_LINK_STATUS */
	ecore_wr_nw_port(p_hwfn, p_ptt, XLPORT_FLOW_CONTROL_CONFIG,
			 1, 0, port); /* Enabled Parallel PFC interface */
	ecore_wr_nw_port(p_hwfn, p_ptt, XLPORT_ENABLE_REG,
			 0xf, 1, port); /* XLPORT port enable */
}

static void ecore_emul_link_init_ah_e5(struct ecore_hwfn *p_hwfn,
				       struct ecore_ptt *p_ptt)
{
	u8 port = p_hwfn->port_id;
	u32 mac_base = NWM_REG_MAC0_K2_E5 + (port << 2) * NWM_REG_MAC0_SIZE;

	DP_INFO(p_hwfn->p_dev, "Configurating Emulation Link %02x\n", port);

	ecore_wr(p_hwfn, p_ptt, CNIG_REG_NIG_PORT0_CONF_K2_E5 + (port << 2),
		 (1 << CNIG_REG_NIG_PORT0_CONF_NIG_PORT_ENABLE_0_K2_E5_SHIFT) |
		 (port <<
		  CNIG_REG_NIG_PORT0_CONF_NIG_PORT_NWM_PORT_MAP_0_K2_E5_SHIFT) |
		 (0 << CNIG_REG_NIG_PORT0_CONF_NIG_PORT_RATE_0_K2_E5_SHIFT));

	ecore_wr(p_hwfn, p_ptt, mac_base + ETH_MAC_REG_XIF_MODE_K2_E5,
		 1 << ETH_MAC_REG_XIF_MODE_XGMII_K2_E5_SHIFT);

	ecore_wr(p_hwfn, p_ptt, mac_base + ETH_MAC_REG_FRM_LENGTH_K2_E5,
		 9018 << ETH_MAC_REG_FRM_LENGTH_FRM_LENGTH_K2_E5_SHIFT);

	ecore_wr(p_hwfn, p_ptt, mac_base + ETH_MAC_REG_TX_IPG_LENGTH_K2_E5,
		 0xc << ETH_MAC_REG_TX_IPG_LENGTH_TXIPG_K2_E5_SHIFT);

	ecore_wr(p_hwfn, p_ptt, mac_base + ETH_MAC_REG_RX_FIFO_SECTIONS_K2_E5,
		 8 << ETH_MAC_REG_RX_FIFO_SECTIONS_RX_SECTION_FULL_K2_E5_SHIFT);

	ecore_wr(p_hwfn, p_ptt, mac_base + ETH_MAC_REG_TX_FIFO_SECTIONS_K2_E5,
		 (0xA <<
		  ETH_MAC_REG_TX_FIFO_SECTIONS_TX_SECTION_EMPTY_K2_E5_SHIFT) |
		 (8 <<
		  ETH_MAC_REG_TX_FIFO_SECTIONS_TX_SECTION_FULL_K2_E5_SHIFT));

	ecore_wr(p_hwfn, p_ptt, mac_base + ETH_MAC_REG_COMMAND_CONFIG_K2_E5,
		 0xa853);
}

static void ecore_emul_link_init(struct ecore_hwfn *p_hwfn,
				 struct ecore_ptt *p_ptt)
{
	if (ECORE_IS_AH(p_hwfn->p_dev) || ECORE_IS_E5(p_hwfn->p_dev))
		ecore_emul_link_init_ah_e5(p_hwfn, p_ptt);
	else /* BB */
		ecore_emul_link_init_bb(p_hwfn, p_ptt);

	return;
}

static void ecore_link_init_bb(struct ecore_hwfn *p_hwfn,
			       struct ecore_ptt *p_ptt,  u8 port)
{
	int port_offset = port ? 0x800 : 0;
	u32 xmac_rxctrl	= 0;

	/* Reset of XMAC */
	/* FIXME: move to common start */
	ecore_wr(p_hwfn, p_ptt, MISC_REG_RESET_PL_PDA_VAUX + 2*sizeof(u32),
		 MISC_REG_RESET_REG_2_XMAC_BIT); /* Clear */
	OSAL_MSLEEP(1);
	ecore_wr(p_hwfn, p_ptt, MISC_REG_RESET_PL_PDA_VAUX + sizeof(u32),
		 MISC_REG_RESET_REG_2_XMAC_BIT); /* Set */

	ecore_wr(p_hwfn, p_ptt, MISC_REG_XMAC_CORE_PORT_MODE_BB, 1);

	/* Set the number of ports on the Warp Core to 10G */
	ecore_wr(p_hwfn, p_ptt, MISC_REG_XMAC_PHY_PORT_MODE_BB, 3);

	/* Soft reset of XMAC */
	ecore_wr(p_hwfn, p_ptt, MISC_REG_RESET_PL_PDA_VAUX + 2 * sizeof(u32),
		 MISC_REG_RESET_REG_2_XMAC_SOFT_BIT);
	OSAL_MSLEEP(1);
	ecore_wr(p_hwfn, p_ptt, MISC_REG_RESET_PL_PDA_VAUX + sizeof(u32),
		 MISC_REG_RESET_REG_2_XMAC_SOFT_BIT);

	/* FIXME: move to common end */
	if (CHIP_REV_IS_FPGA(p_hwfn->p_dev))
		ecore_wr(p_hwfn, p_ptt, XMAC_REG_MODE_BB + port_offset, 0x20);

	/* Set Max packet size: initialize XMAC block register for port 0 */
	ecore_wr(p_hwfn, p_ptt, XMAC_REG_RX_MAX_SIZE_BB + port_offset, 0x2710);

	/* CRC append for Tx packets: init XMAC block register for port 1 */
	ecore_wr(p_hwfn, p_ptt, XMAC_REG_TX_CTRL_LO_BB + port_offset, 0xC800);

	/* Enable TX and RX: initialize XMAC block register for port 1 */
	ecore_wr(p_hwfn, p_ptt, XMAC_REG_CTRL_BB + port_offset,
		 XMAC_REG_CTRL_TX_EN_BB | XMAC_REG_CTRL_RX_EN_BB);
	xmac_rxctrl = ecore_rd(p_hwfn, p_ptt,
			       XMAC_REG_RX_CTRL_BB + port_offset);
	xmac_rxctrl |= XMAC_REG_RX_CTRL_PROCESS_VARIABLE_PREAMBLE_BB;
	ecore_wr(p_hwfn, p_ptt, XMAC_REG_RX_CTRL_BB + port_offset, xmac_rxctrl);
}
#endif

static enum _ecore_status_t
ecore_hw_init_dpi_size(struct ecore_hwfn *p_hwfn,
		       struct ecore_ptt *p_ptt,
		       u32 pwm_region_size,
		       u32 n_cpus)
{
	u32 dpi_bit_shift, dpi_count, dpi_page_size;
	u32 min_dpis;
	u32 n_wids;

	/* Calculate DPI size
	 * ------------------
	 * The PWM region contains Doorbell Pages. The first is reserverd for
	 * the kernel for, e.g, L2. The others are free to be used by non-
	 * trusted applications, typically from user space. Each page, called a
	 * doorbell page is sectioned into windows that allow doorbells to be
	 * issued in parallel by the kernel/application. The size of such a
	 * window (a.k.a. WID) is 1kB.
	 * Summary:
	 *    1kB WID x N WIDS = DPI page size
	 *    DPI page size x N DPIs = PWM region size
	 * Notes:
	 * The size of the DPI page size must be in multiples of OSAL_PAGE_SIZE
	 * in order to ensure that two applications won't share the same page.
	 * It also must contain at least one WID per CPU to allow parallelism.
	 * It also must be a power of 2, since it is stored as a bit shift.
	 *
	 * The DPI page size is stored in a register as 'dpi_bit_shift' so that
	 * 0 is 4kB, 1 is 8kB and etc. Hence the minimum size is 4,096
	 * containing 4 WIDs.
	 */
	n_wids = OSAL_MAX_T(u32, ECORE_MIN_WIDS, n_cpus);
	dpi_page_size = ECORE_WID_SIZE * OSAL_ROUNDUP_POW_OF_TWO(n_wids);
	dpi_page_size = (dpi_page_size + OSAL_PAGE_SIZE - 1) & ~(OSAL_PAGE_SIZE - 1);
	dpi_bit_shift = OSAL_LOG2(dpi_page_size / 4096);
	dpi_count = pwm_region_size / dpi_page_size;

	min_dpis = p_hwfn->pf_params.rdma_pf_params.min_dpis;
	min_dpis = OSAL_MAX_T(u32, ECORE_MIN_DPIS, min_dpis);

	/* Update hwfn */
	p_hwfn->dpi_size = dpi_page_size;
	p_hwfn->dpi_count = dpi_count;

	/* Update registers */
	ecore_wr(p_hwfn, p_ptt, DORQ_REG_PF_DPI_BIT_SHIFT, dpi_bit_shift);

	if (dpi_count < min_dpis)
		return ECORE_NORESOURCES;

	return ECORE_SUCCESS;
}

enum ECORE_ROCE_EDPM_MODE {
	ECORE_ROCE_EDPM_MODE_ENABLE	= 0,
	ECORE_ROCE_EDPM_MODE_FORCE_ON	= 1,
	ECORE_ROCE_EDPM_MODE_DISABLE	= 2,
};

static enum _ecore_status_t
ecore_hw_init_pf_doorbell_bar(struct ecore_hwfn *p_hwfn,
			      struct ecore_ptt *p_ptt)
{
	struct ecore_rdma_pf_params *p_rdma_pf_params;
	u32 pwm_regsize, norm_regsize;
	u32 non_pwm_conn, min_addr_reg1;
	u32 db_bar_size, n_cpus = 1;
	u32 roce_edpm_mode;
	u32 pf_dems_shift;
	enum _ecore_status_t rc = ECORE_SUCCESS;
	u8 cond;

	db_bar_size = ecore_hw_bar_size(p_hwfn, p_ptt, BAR_ID_1);
	if (ECORE_IS_CMT(p_hwfn->p_dev))
		db_bar_size /= 2;

	/* Calculate doorbell regions
	 * -----------------------------------
	 * The doorbell BAR is made of two regions. The first is called normal
	 * region and the second is called PWM region. In the normal region
	 * each ICID has its own set of addresses so that writing to that
	 * specific address identifies the ICID. In the Process Window Mode
	 * region the ICID is given in the data written to the doorbell. The
	 * above per PF register denotes the offset in the doorbell BAR in which
	 * the PWM region begins.
	 * The normal region has ECORE_PF_DEMS_SIZE bytes per ICID, that is per
	 * non-PWM connection. The calculation below computes the total non-PWM
	 * connections. The DORQ_REG_PF_MIN_ADDR_REG1 register is
	 * in units of 4,096 bytes.
	 */
	non_pwm_conn = ecore_cxt_get_proto_cid_start(p_hwfn, PROTOCOLID_CORE) +
		       ecore_cxt_get_proto_cid_count(p_hwfn, PROTOCOLID_CORE,
						     OSAL_NULL) +
		       ecore_cxt_get_proto_cid_count(p_hwfn, PROTOCOLID_ETH,
						     OSAL_NULL);
	norm_regsize = ROUNDUP(ECORE_PF_DEMS_SIZE * non_pwm_conn, OSAL_PAGE_SIZE);
	min_addr_reg1 = norm_regsize / 4096;
	pwm_regsize = db_bar_size - norm_regsize;

	/* Check that the normal and PWM sizes are valid */
	if (db_bar_size < norm_regsize) {
		DP_ERR(p_hwfn->p_dev,
		       "Doorbell BAR size 0x%x is too small (normal region is 0x%0x )\n",
		       db_bar_size, norm_regsize);
		return ECORE_NORESOURCES;
	}
	if (pwm_regsize < ECORE_MIN_PWM_REGION) {
		DP_ERR(p_hwfn->p_dev,
		       "PWM region size 0x%0x is too small. Should be at least 0x%0x (Doorbell BAR size is 0x%x and normal region size is 0x%0x)\n",
		       pwm_regsize, ECORE_MIN_PWM_REGION, db_bar_size,
		       norm_regsize);
		return ECORE_NORESOURCES;
	}

	p_rdma_pf_params = &p_hwfn->pf_params.rdma_pf_params;

	/* Calculate number of DPIs */
	if (ECORE_IS_IWARP_PERSONALITY(p_hwfn))
		p_rdma_pf_params->roce_edpm_mode =  ECORE_ROCE_EDPM_MODE_DISABLE;

	if (p_rdma_pf_params->roce_edpm_mode <= ECORE_ROCE_EDPM_MODE_DISABLE) {
		roce_edpm_mode = p_rdma_pf_params->roce_edpm_mode;
	} else {
		DP_ERR(p_hwfn->p_dev,
		       "roce edpm mode was configured to an illegal value of %u. Resetting it to 0-Enable EDPM if BAR size is adequate\n",
		       p_rdma_pf_params->roce_edpm_mode);
		roce_edpm_mode = 0;
	}

	if ((roce_edpm_mode == ECORE_ROCE_EDPM_MODE_ENABLE) ||
	    ((roce_edpm_mode == ECORE_ROCE_EDPM_MODE_FORCE_ON))) {
		/* Either EDPM is mandatory, or we are attempting to allocate a
		 * WID per CPU.
		 */
		n_cpus = OSAL_NUM_CPUS();
		rc = ecore_hw_init_dpi_size(p_hwfn, p_ptt, pwm_regsize, n_cpus);
	}

	cond = ((rc != ECORE_SUCCESS) &&
		(roce_edpm_mode == ECORE_ROCE_EDPM_MODE_ENABLE)) ||
		(roce_edpm_mode == ECORE_ROCE_EDPM_MODE_DISABLE);
	if (cond || p_hwfn->dcbx_no_edpm) {
		/* Either EDPM is disabled from user configuration, or it is
		 * disabled via DCBx, or it is not mandatory and we failed to
		 * allocated a WID per CPU.
		 */
		n_cpus = 1;
		rc = ecore_hw_init_dpi_size(p_hwfn, p_ptt, pwm_regsize, n_cpus);

#ifdef CONFIG_ECORE_ROCE
		/* If we entered this flow due to DCBX then the DPM register is
		 * already configured.
		 */
		if (cond)
			ecore_rdma_dpm_bar(p_hwfn, p_ptt);
#endif
	}

	p_hwfn->wid_count = (u16)n_cpus;

	/* Check return codes from above calls */
	if (rc != ECORE_SUCCESS) {
#ifndef LINUX_REMOVE
		DP_ERR(p_hwfn,
		       "Failed to allocate enough DPIs. Allocated %d but the current minimum is set to %d. You can reduce this minimum down to %d via user configuration min_dpis or by disabling EDPM via user configuration roce_edpm_mode\n",
		       p_hwfn->dpi_count, p_rdma_pf_params->min_dpis,
		       ECORE_MIN_DPIS);
#else
		DP_ERR(p_hwfn,
		       "Failed to allocate enough DPIs. Allocated %d but the current minimum is set to %d. You can reduce this minimum down to %d via the module parameter min_rdma_dpis or by disabling EDPM by setting the module parameter roce_edpm to 2\n",
		       p_hwfn->dpi_count, p_rdma_pf_params->min_dpis,
		       ECORE_MIN_DPIS);
#endif
		DP_ERR(p_hwfn,
		       "doorbell bar: normal_region_size=%d, pwm_region_size=%d, dpi_size=%d, dpi_count=%d, roce_edpm=%s, page_size=%lu\n",
		       norm_regsize, pwm_regsize, p_hwfn->dpi_size,
		       p_hwfn->dpi_count,
		       ((p_hwfn->dcbx_no_edpm) || (p_hwfn->db_bar_no_edpm)) ?
		       "disabled" : "enabled", (unsigned long)OSAL_PAGE_SIZE);

		return ECORE_NORESOURCES;
	}

	DP_INFO(p_hwfn,
		"doorbell bar: normal_region_size=%d, pwm_region_size=%d, dpi_size=%d, dpi_count=%d, roce_edpm=%s, page_size=%lu\n",
		norm_regsize, pwm_regsize, p_hwfn->dpi_size, p_hwfn->dpi_count,
		((p_hwfn->dcbx_no_edpm) || (p_hwfn->db_bar_no_edpm)) ?
		"disabled" : "enabled", (unsigned long)OSAL_PAGE_SIZE);

	/* Update hwfn */
	p_hwfn->dpi_start_offset = norm_regsize; /* this is later used to
						      * calculate the doorbell
						      * address
						      */

	/* Update registers */
	/* DEMS size is configured log2 of DWORDs, hence the division by 4 */
	pf_dems_shift = OSAL_LOG2(ECORE_PF_DEMS_SIZE / 4);
	ecore_wr(p_hwfn, p_ptt, DORQ_REG_PF_ICID_BIT_SHIFT_NORM, pf_dems_shift);
	ecore_wr(p_hwfn, p_ptt, DORQ_REG_PF_MIN_ADDR_REG1, min_addr_reg1);

	return ECORE_SUCCESS;
}

static enum _ecore_status_t ecore_hw_init_port(struct ecore_hwfn *p_hwfn,
					       struct ecore_ptt *p_ptt,
					       int hw_mode)
{
	enum _ecore_status_t rc	= ECORE_SUCCESS;

	/* In CMT the gate should be cleared by the 2nd hwfn */
	if (!ECORE_IS_CMT(p_hwfn->p_dev) || !IS_LEAD_HWFN(p_hwfn))
		STORE_RT_REG(p_hwfn, NIG_REG_BRB_GATE_DNTFWD_PORT_RT_OFFSET, 0);

	rc = ecore_init_run(p_hwfn, p_ptt, PHASE_PORT, p_hwfn->port_id,
			    hw_mode);
	if (rc != ECORE_SUCCESS)
		return rc;

	ecore_wr(p_hwfn, p_ptt, PGLUE_B_REG_MASTER_WRITE_PAD_ENABLE, 0);

#ifndef ASIC_ONLY
	if (CHIP_REV_IS_ASIC(p_hwfn->p_dev))
		return ECORE_SUCCESS;

	if (CHIP_REV_IS_FPGA(p_hwfn->p_dev)) {
		if (ECORE_IS_AH(p_hwfn->p_dev))
			return ECORE_SUCCESS;
		else if (ECORE_IS_BB(p_hwfn->p_dev))
			ecore_link_init_bb(p_hwfn, p_ptt, p_hwfn->port_id);
		else /* E5 */
			ECORE_E5_MISSING_CODE;
	} else if (CHIP_REV_IS_EMUL(p_hwfn->p_dev)) {
		if (ECORE_IS_CMT(p_hwfn->p_dev)) {
			/* Activate OPTE in CMT */
			u32 val;

			val = ecore_rd(p_hwfn, p_ptt, MISCS_REG_RESET_PL_HV);
			val |= 0x10;
			ecore_wr(p_hwfn, p_ptt, MISCS_REG_RESET_PL_HV, val);
			ecore_wr(p_hwfn, p_ptt, MISC_REG_CLK_100G_MODE, 1);
			ecore_wr(p_hwfn, p_ptt, MISCS_REG_CLK_100G_MODE, 1);
			ecore_wr(p_hwfn, p_ptt, MISC_REG_OPTE_MODE, 1);
			ecore_wr(p_hwfn, p_ptt,
				 NIG_REG_LLH_ENG_CLS_TCP_4_TUPLE_SEARCH, 1);
			ecore_wr(p_hwfn, p_ptt,
				 NIG_REG_LLH_ENG_CLS_ENG_ID_TBL, 0x55555555);
			ecore_wr(p_hwfn, p_ptt,
				 NIG_REG_LLH_ENG_CLS_ENG_ID_TBL + 0x4,
				 0x55555555);
		}

		ecore_emul_link_init(p_hwfn, p_ptt);
	} else {
		DP_INFO(p_hwfn->p_dev, "link is not being configured\n");
	}
#endif

	return rc;
}

static enum _ecore_status_t
ecore_hw_init_pf(struct ecore_hwfn *p_hwfn, struct ecore_ptt *p_ptt,
		 int hw_mode, struct ecore_hw_init_params *p_params)
{
	u8 rel_pf_id = p_hwfn->rel_pf_id;
	u32 prs_reg;
	enum _ecore_status_t rc	= ECORE_SUCCESS;
	u16 ctrl;
	int pos;

	if (p_hwfn->mcp_info) {
		struct ecore_mcp_function_info *p_info;

		p_info = &p_hwfn->mcp_info->func_info;
		if (p_info->bandwidth_min)
			p_hwfn->qm_info.pf_wfq = p_info->bandwidth_min;

		/* Update rate limit once we'll actually have a link */
		p_hwfn->qm_info.pf_rl = 100000;
	}
	ecore_cxt_hw_init_pf(p_hwfn, p_ptt);

	ecore_int_igu_init_rt(p_hwfn);

	/* Set VLAN in NIG if needed */
	if (hw_mode & (1 << MODE_MF_SD)) {
		DP_VERBOSE(p_hwfn, ECORE_MSG_HW, "Configuring LLH_FUNC_TAG\n");
		STORE_RT_REG(p_hwfn, NIG_REG_LLH_FUNC_TAG_EN_RT_OFFSET, 1);
		STORE_RT_REG(p_hwfn, NIG_REG_LLH_FUNC_TAG_VALUE_RT_OFFSET,
			     p_hwfn->hw_info.ovlan);

		DP_VERBOSE(p_hwfn, ECORE_MSG_HW,
			   "Configuring LLH_FUNC_FILTER_HDR_SEL\n");
		STORE_RT_REG(p_hwfn, NIG_REG_LLH_FUNC_FILTER_HDR_SEL_RT_OFFSET,
			     1);
	}

	/* Enable classification by MAC if needed */
	if (hw_mode & (1 << MODE_MF_SI)) {
		DP_VERBOSE(p_hwfn, ECORE_MSG_HW, "Configuring TAGMAC_CLS_TYPE\n");
		STORE_RT_REG(p_hwfn,
			     NIG_REG_LLH_FUNC_TAGMAC_CLS_TYPE_RT_OFFSET, 1);
	}

	/* Protocl Configuration  - @@@TBD - should we set 0 otherwise?*/
	STORE_RT_REG(p_hwfn, PRS_REG_SEARCH_TCP_RT_OFFSET,
		     (p_hwfn->hw_info.personality == ECORE_PCI_ISCSI) ? 1 : 0);
	STORE_RT_REG(p_hwfn, PRS_REG_SEARCH_FCOE_RT_OFFSET,
		     (p_hwfn->hw_info.personality == ECORE_PCI_FCOE) ? 1 : 0);
	STORE_RT_REG(p_hwfn, PRS_REG_SEARCH_ROCE_RT_OFFSET, 0);

	/* perform debug configuration when chip is out of reset */
	OSAL_BEFORE_PF_START((void *)p_hwfn->p_dev, p_hwfn->my_id);

	/* Sanity check before the PF init sequence that uses DMAE */
	rc = ecore_dmae_sanity(p_hwfn, p_ptt, "pf_phase");
	if (rc)
		return rc;

	/* PF Init sequence */
	rc = ecore_init_run(p_hwfn, p_ptt, PHASE_PF, rel_pf_id, hw_mode);
	if (rc)
		return rc;

	/* QM_PF Init sequence (may be invoked separately e.g. for DCB) */
	rc = ecore_init_run(p_hwfn, p_ptt, PHASE_QM_PF, rel_pf_id, hw_mode);
	if (rc)
		return rc;

	/* Pure runtime initializations - directly to the HW  */
	ecore_int_igu_init_pure_rt(p_hwfn, p_ptt, true, true);

	/* PCI relaxed ordering is generally beneficial for performance,
	 * but can hurt performance or lead to instability on some setups.
	 * If management FW is taking care of it go with that, otherwise
	 * disable to be on the safe side.
	 */
	pos = OSAL_PCI_FIND_CAPABILITY(p_hwfn->p_dev, PCI_CAP_ID_EXP);
	if (!pos) {
		DP_NOTICE(p_hwfn, true,
			  "Failed to find the PCI Express Capability structure in the PCI config space\n");
		return ECORE_IO;
	}

	OSAL_PCI_READ_CONFIG_WORD(p_hwfn->p_dev, pos + PCI_EXP_DEVCTL, &ctrl);

	if (p_params->pci_rlx_odr_mode == ECORE_ENABLE_RLX_ODR) {
		ctrl |= PCI_EXP_DEVCTL_RELAX_EN;
		OSAL_PCI_WRITE_CONFIG_WORD(p_hwfn->p_dev,
					   pos + PCI_EXP_DEVCTL, ctrl);
	} else if (p_params->pci_rlx_odr_mode == ECORE_DISABLE_RLX_ODR) {
		ctrl &= ~PCI_EXP_DEVCTL_RELAX_EN;
		OSAL_PCI_WRITE_CONFIG_WORD(p_hwfn->p_dev,
					   pos + PCI_EXP_DEVCTL, ctrl);
	} else if (ecore_mcp_rlx_odr_supported(p_hwfn)) {
		DP_INFO(p_hwfn, "PCI relax ordering configured by MFW\n");
	} else {
		ctrl &= ~PCI_EXP_DEVCTL_RELAX_EN;
		OSAL_PCI_WRITE_CONFIG_WORD(p_hwfn->p_dev,
					   pos + PCI_EXP_DEVCTL, ctrl);
	}

	rc = ecore_hw_init_pf_doorbell_bar(p_hwfn, p_ptt);
	if (rc != ECORE_SUCCESS)
		return rc;

	/* Use the leading hwfn since in CMT only NIG #0 is operational */
	if (IS_LEAD_HWFN(p_hwfn)) {
		rc = ecore_llh_hw_init_pf(p_hwfn, p_ptt,
					  p_params->avoid_eng_affin);
		if (rc != ECORE_SUCCESS)
			return rc;
	}

	if (p_params->b_hw_start) {
		/* enable interrupts */
		rc = ecore_int_igu_enable(p_hwfn, p_ptt, p_params->int_mode);
		if (rc != ECORE_SUCCESS)
			return rc;

		/* send function start command */
		rc = ecore_sp_pf_start(p_hwfn, p_ptt, p_params->p_tunn,
				       p_params->allow_npar_tx_switch);
		if (rc) {
			DP_NOTICE(p_hwfn, true, "Function start ramrod failed\n");
			return rc;
		}
		prs_reg = ecore_rd(p_hwfn, p_ptt, PRS_REG_SEARCH_TAG1);
		DP_VERBOSE(p_hwfn, ECORE_MSG_STORAGE,
				"PRS_REG_SEARCH_TAG1: %x\n", prs_reg);

		if (p_hwfn->hw_info.personality == ECORE_PCI_FCOE)
		{
			ecore_wr(p_hwfn, p_ptt, PRS_REG_SEARCH_TAG1,
					(1 << 2));
			ecore_wr(p_hwfn, p_ptt,
					PRS_REG_PKT_LEN_STAT_TAGS_NOT_COUNTED_FIRST,
					0x100);
		}
		DP_VERBOSE(p_hwfn, ECORE_MSG_STORAGE,
				"PRS_REG_SEARCH registers after start PFn\n");
		prs_reg = ecore_rd(p_hwfn, p_ptt, PRS_REG_SEARCH_TCP);
		DP_VERBOSE(p_hwfn, ECORE_MSG_STORAGE,
				"PRS_REG_SEARCH_TCP: %x\n", prs_reg);
		prs_reg = ecore_rd(p_hwfn, p_ptt, PRS_REG_SEARCH_UDP);
		DP_VERBOSE(p_hwfn, ECORE_MSG_STORAGE,
				"PRS_REG_SEARCH_UDP: %x\n", prs_reg);
		prs_reg = ecore_rd(p_hwfn, p_ptt, PRS_REG_SEARCH_FCOE);
		DP_VERBOSE(p_hwfn, ECORE_MSG_STORAGE,
				"PRS_REG_SEARCH_FCOE: %x\n", prs_reg);
		prs_reg = ecore_rd(p_hwfn, p_ptt, PRS_REG_SEARCH_ROCE);
		DP_VERBOSE(p_hwfn, ECORE_MSG_STORAGE,
				"PRS_REG_SEARCH_ROCE: %x\n", prs_reg);
		prs_reg = ecore_rd(p_hwfn, p_ptt,
				PRS_REG_SEARCH_TCP_FIRST_FRAG);
		DP_VERBOSE(p_hwfn, ECORE_MSG_STORAGE,
				"PRS_REG_SEARCH_TCP_FIRST_FRAG: %x\n",
				prs_reg);
		prs_reg = ecore_rd(p_hwfn, p_ptt, PRS_REG_SEARCH_TAG1);
		DP_VERBOSE(p_hwfn, ECORE_MSG_STORAGE,
				"PRS_REG_SEARCH_TAG1: %x\n", prs_reg);
	}
	return ECORE_SUCCESS;
}

enum _ecore_status_t ecore_pglueb_set_pfid_enable(struct ecore_hwfn *p_hwfn,
						  struct ecore_ptt *p_ptt,
						  bool b_enable)
{
	u32 delay_idx = 0, val, set_val = b_enable ? 1 : 0;

	/* Configure the PF's internal FID_enable for master transactions */
	ecore_wr(p_hwfn, p_ptt,
		 PGLUE_B_REG_INTERNAL_PFID_ENABLE_MASTER, set_val);

	/* Wait until value is set - try for 1 second every 50us */
	for (delay_idx = 0; delay_idx < 20000; delay_idx++) {
		val = ecore_rd(p_hwfn, p_ptt,
			       PGLUE_B_REG_INTERNAL_PFID_ENABLE_MASTER);
		if (val == set_val)
			break;

		OSAL_UDELAY(50);
	}

	if (val != set_val) {
		DP_NOTICE(p_hwfn, true,
			  "PFID_ENABLE_MASTER wasn't changed after a second\n");
		return ECORE_UNKNOWN_ERROR;
	}

	return ECORE_SUCCESS;
}

static void ecore_reset_mb_shadow(struct ecore_hwfn *p_hwfn,
			struct ecore_ptt *p_main_ptt)
{
	/* Read shadow of current MFW mailbox */
	ecore_mcp_read_mb(p_hwfn, p_main_ptt);
	OSAL_MEMCPY(p_hwfn->mcp_info->mfw_mb_shadow,
		    p_hwfn->mcp_info->mfw_mb_cur,
		    p_hwfn->mcp_info->mfw_mb_length);
}

static enum _ecore_status_t
ecore_fill_load_req_params(struct ecore_hwfn *p_hwfn,
			   struct ecore_load_req_params *p_load_req,
			   struct ecore_drv_load_params *p_drv_load)
{
	/* Make sure that if ecore-client didn't provide inputs, all the
	 * expected defaults are indeed zero.
	 */
	OSAL_BUILD_BUG_ON(ECORE_DRV_ROLE_OS != 0);
	OSAL_BUILD_BUG_ON(ECORE_LOAD_REQ_LOCK_TO_DEFAULT != 0);
	OSAL_BUILD_BUG_ON(ECORE_OVERRIDE_FORCE_LOAD_NONE != 0);

	OSAL_MEM_ZERO(p_load_req, sizeof(*p_load_req));

	if (p_drv_load == OSAL_NULL)
		goto out;

	p_load_req->drv_role = p_drv_load->is_crash_kernel ?
			       ECORE_DRV_ROLE_KDUMP :
			       ECORE_DRV_ROLE_OS;
	p_load_req->avoid_eng_reset = p_drv_load->avoid_eng_reset;
	p_load_req->override_force_load = p_drv_load->override_force_load;

	/* Old MFW versions don't support timeout values other than default and
	 * none, so these values are replaced according to the fall-back action.
	 */

	if (p_drv_load->mfw_timeout_val == ECORE_LOAD_REQ_LOCK_TO_DEFAULT ||
	    p_drv_load->mfw_timeout_val == ECORE_LOAD_REQ_LOCK_TO_NONE ||
	    (p_hwfn->mcp_info->capabilities &
	     FW_MB_PARAM_FEATURE_SUPPORT_DRV_LOAD_TO)) {
		p_load_req->timeout_val = p_drv_load->mfw_timeout_val;
		goto out;
	}

	switch (p_drv_load->mfw_timeout_fallback) {
	case ECORE_TO_FALLBACK_TO_NONE:
		p_load_req->timeout_val = ECORE_LOAD_REQ_LOCK_TO_NONE;
		break;
	case ECORE_TO_FALLBACK_TO_DEFAULT:
		p_load_req->timeout_val = ECORE_LOAD_REQ_LOCK_TO_DEFAULT;
		break;
	case ECORE_TO_FALLBACK_FAIL_LOAD:
		DP_NOTICE(p_hwfn, false,
			  "Received %d as a value for MFW timeout while the MFW supports only default [%d] or none [%d]. Abort.\n",
			  p_drv_load->mfw_timeout_val,
			  ECORE_LOAD_REQ_LOCK_TO_DEFAULT,
			  ECORE_LOAD_REQ_LOCK_TO_NONE);
		return ECORE_ABORTED;
	}

	DP_INFO(p_hwfn,
		"Modified the MFW timeout value from %d to %s [%d] due to lack of MFW support\n",
		p_drv_load->mfw_timeout_val,
		(p_load_req->timeout_val == ECORE_LOAD_REQ_LOCK_TO_DEFAULT) ?
		"default" : "none",
		p_load_req->timeout_val);
out:
	return ECORE_SUCCESS;
}

static enum _ecore_status_t ecore_vf_start(struct ecore_hwfn *p_hwfn,
				    struct ecore_hw_init_params *p_params)
{
	if (p_params->p_tunn) {
		ecore_vf_set_vf_start_tunn_update_param(p_params->p_tunn);
		ecore_vf_pf_tunnel_param_update(p_hwfn, p_params->p_tunn);
	}

	p_hwfn->b_int_enabled = 1;

	return ECORE_SUCCESS;
}

static void ecore_pglueb_clear_err(struct ecore_hwfn *p_hwfn,
				   struct ecore_ptt *p_ptt)
{
	ecore_wr(p_hwfn, p_ptt, PGLUE_B_REG_WAS_ERROR_PF_31_0_CLR,
		 1 << p_hwfn->abs_pf_id);
}

enum _ecore_status_t ecore_hw_init(struct ecore_dev *p_dev,
				   struct ecore_hw_init_params *p_params)
{
	struct ecore_load_req_params load_req_params;
	u32 load_code, resp, param, drv_mb_param;
	bool b_default_mtu = true;
	struct ecore_hwfn *p_hwfn;
	enum _ecore_status_t rc = ECORE_SUCCESS, cancel_load;
	u16 ether_type;
	int i;

	if ((p_params->int_mode == ECORE_INT_MODE_MSI) && ECORE_IS_CMT(p_dev)) {
		DP_NOTICE(p_dev, false,
			  "MSI mode is not supported for CMT devices\n");
		return ECORE_INVAL;
	}

	if (IS_PF(p_dev)) {
		rc = ecore_init_fw_data(p_dev, p_params->bin_fw_data);
		if (rc != ECORE_SUCCESS)
			return rc;
	}

	for_each_hwfn(p_dev, i) {
		p_hwfn = &p_dev->hwfns[i];

		/* If management didn't provide a default, set one of our own */
		if (!p_hwfn->hw_info.mtu) {
			p_hwfn->hw_info.mtu = 1500;
			b_default_mtu = false;
		}

		if (IS_VF(p_dev)) {
			ecore_vf_start(p_hwfn, p_params);
			continue;
		}

		rc = ecore_calc_hw_mode(p_hwfn);
		if (rc != ECORE_SUCCESS)
			return rc;

		if (IS_PF(p_dev) && (OSAL_TEST_BIT(ECORE_MF_8021Q_TAGGING,
						   &p_dev->mf_bits) ||
				     OSAL_TEST_BIT(ECORE_MF_8021AD_TAGGING,
						   &p_dev->mf_bits))) {
			if (OSAL_TEST_BIT(ECORE_MF_8021Q_TAGGING,
					  &p_dev->mf_bits))
				ether_type = ETH_P_8021Q;
			else
				ether_type = ETH_P_8021AD;
			STORE_RT_REG(p_hwfn, PRS_REG_TAG_ETHERTYPE_0_RT_OFFSET,
				     ether_type);
			STORE_RT_REG(p_hwfn, NIG_REG_TAG_ETHERTYPE_0_RT_OFFSET,
				     ether_type);
			STORE_RT_REG(p_hwfn, PBF_REG_TAG_ETHERTYPE_0_RT_OFFSET,
				     ether_type);
			STORE_RT_REG(p_hwfn, DORQ_REG_TAG1_ETHERTYPE_RT_OFFSET,
				     ether_type);
		}

		rc = ecore_fill_load_req_params(p_hwfn, &load_req_params,
						p_params->p_drv_load_params);
		if (rc != ECORE_SUCCESS)
			return rc;

		rc = ecore_mcp_load_req(p_hwfn, p_hwfn->p_main_ptt,
					&load_req_params);
		if (rc != ECORE_SUCCESS) {
			DP_NOTICE(p_hwfn, false,
				  "Failed sending a LOAD_REQ command\n");
			return rc;
		}

		load_code = load_req_params.load_code;
		DP_VERBOSE(p_hwfn, ECORE_MSG_SP,
			   "Load request was sent. Load code: 0x%x\n",
			   load_code);

		ecore_mcp_set_capabilities(p_hwfn, p_hwfn->p_main_ptt);

		/* CQ75580:
		 * When coming back from hibernate state, the registers from
		 * which shadow is read initially are not initialized. It turns
		 * out that these registers get initialized during the call to
		 * ecore_mcp_load_req request. So we need to reread them here
		 * to get the proper shadow register value.
		 * Note: This is a workaround for the missing MFW
		 * initialization. It may be removed once the implementation
		 * is done.
		 */
		ecore_reset_mb_shadow(p_hwfn, p_hwfn->p_main_ptt);

		/* Only relevant for recovery:
		 * Clear the indication after the LOAD_REQ command is responded
		 * by the MFW.
		 */
		p_dev->recov_in_prog = false;

		if (!qm_lock_ref_cnt) {
#ifdef CONFIG_ECORE_LOCK_ALLOC
			rc = OSAL_SPIN_LOCK_ALLOC(p_hwfn, &qm_lock);
			if (rc) {
				DP_ERR(p_hwfn, "qm_lock allocation failed\n");
				goto qm_lock_fail;
			}
#endif
			OSAL_SPIN_LOCK_INIT(&qm_lock);
		}
		++qm_lock_ref_cnt;

		/* Clean up chip from previous driver if such remains exist.
		 * This is not needed when the PF is the first one on the
		 * engine, since afterwards we are going to init the FW.
		 */
		if (load_code != FW_MSG_CODE_DRV_LOAD_ENGINE) {
			rc = ecore_final_cleanup(p_hwfn, p_hwfn->p_main_ptt,
						 p_hwfn->rel_pf_id, false);
			if (rc != ECORE_SUCCESS) {
				ecore_hw_err_notify(p_hwfn,
						    ECORE_HW_ERR_RAMROD_FAIL);
				goto load_err;
			}
		}

		/* Log and clear previous pglue_b errors if such exist */
		ecore_pglueb_rbc_attn_handler(p_hwfn, p_hwfn->p_main_ptt);

		/* Enable the PF's internal FID_enable in the PXP */
		rc = ecore_pglueb_set_pfid_enable(p_hwfn, p_hwfn->p_main_ptt,
						  true);
		if (rc != ECORE_SUCCESS)
			goto load_err;

		/* Clear the pglue_b was_error indication.
		 * In E4 it must be done after the BME and the internal
		 * FID_enable for the PF are set, since VDMs may cause the
		 * indication to be set again.
		 */
		ecore_pglueb_clear_err(p_hwfn, p_hwfn->p_main_ptt);

		switch (load_code) {
		case FW_MSG_CODE_DRV_LOAD_ENGINE:
			rc = ecore_hw_init_common(p_hwfn, p_hwfn->p_main_ptt,
						  p_hwfn->hw_info.hw_mode);
			if (rc != ECORE_SUCCESS)
				break;
			/* Fall into */
		case FW_MSG_CODE_DRV_LOAD_PORT:
			rc = ecore_hw_init_port(p_hwfn, p_hwfn->p_main_ptt,
						p_hwfn->hw_info.hw_mode);
			if (rc != ECORE_SUCCESS)
				break;
			/* Fall into */
		case FW_MSG_CODE_DRV_LOAD_FUNCTION:
			rc = ecore_hw_init_pf(p_hwfn, p_hwfn->p_main_ptt,
					      p_hwfn->hw_info.hw_mode,
					      p_params);
			break;
		default:
			DP_NOTICE(p_hwfn, false,
				  "Unexpected load code [0x%08x]", load_code);
			rc = ECORE_NOTIMPL;
			break;
		}

		if (rc != ECORE_SUCCESS) {
			DP_NOTICE(p_hwfn, false,
				  "init phase failed for loadcode 0x%x (rc %d)\n",
				  load_code, rc);
			goto load_err;
		}

		rc = ecore_mcp_load_done(p_hwfn, p_hwfn->p_main_ptt);
		if (rc != ECORE_SUCCESS) {
			DP_NOTICE(p_hwfn, false, "Sending load done failed, rc = %d\n", rc);
			if (rc == ECORE_NOMEM) {
				DP_NOTICE(p_hwfn, false,
					  "Sending load done was failed due to memory allocation failure\n");
				goto load_err;
			}
			return rc;
		}

		/* send DCBX attention request command */
		DP_VERBOSE(p_hwfn, ECORE_MSG_DCB,
			   "sending phony dcbx set command to trigger DCBx attention handling\n");
		rc = ecore_mcp_cmd(p_hwfn, p_hwfn->p_main_ptt,
				   DRV_MSG_CODE_SET_DCBX,
				   1 << DRV_MB_PARAM_DCBX_NOTIFY_OFFSET, &resp,
				   &param);
		if (rc != ECORE_SUCCESS) {
			DP_NOTICE(p_hwfn, false,
				  "Failed to send DCBX attention request\n");
			return rc;
		}

		p_hwfn->hw_init_done = true;
	}

	if (IS_PF(p_dev)) {
		/* Get pre-negotiated values for stag, bandwidth etc. */
		p_hwfn = ECORE_LEADING_HWFN(p_dev);
		DP_VERBOSE(p_hwfn, ECORE_MSG_SPQ,
			   "Sending GET_OEM_UPDATES command to trigger stag/bandwidth attention handling\n");
		rc = ecore_mcp_cmd(p_hwfn, p_hwfn->p_main_ptt,
				   DRV_MSG_CODE_GET_OEM_UPDATES,
				   1 << DRV_MB_PARAM_DUMMY_OEM_UPDATES_OFFSET,
				   &resp, &param);
		if (rc != ECORE_SUCCESS)
			DP_NOTICE(p_hwfn, false,
				  "Failed to send GET_OEM_UPDATES attention request\n");
	}

	if (IS_PF(p_dev)) {
		p_hwfn = ECORE_LEADING_HWFN(p_dev);
		drv_mb_param = STORM_FW_VERSION;
		rc = ecore_mcp_cmd(p_hwfn, p_hwfn->p_main_ptt,
				   DRV_MSG_CODE_OV_UPDATE_STORM_FW_VER,
				   drv_mb_param, &resp, &param);
		if (rc != ECORE_SUCCESS)
			DP_INFO(p_hwfn, "Failed to update firmware version\n");

		if (!b_default_mtu) {
			rc = ecore_mcp_ov_update_mtu(p_hwfn, p_hwfn->p_main_ptt,
						      p_hwfn->hw_info.mtu);
			if (rc != ECORE_SUCCESS)
				DP_INFO(p_hwfn, "Failed to update default mtu\n");
		}

		rc = ecore_mcp_ov_update_driver_state(p_hwfn,
						      p_hwfn->p_main_ptt,
						      ECORE_OV_DRIVER_STATE_DISABLED);
		if (rc != ECORE_SUCCESS)
			DP_INFO(p_hwfn, "Failed to update driver state\n");

		rc = ecore_mcp_ov_update_eswitch(p_hwfn, p_hwfn->p_main_ptt,
						 ECORE_OV_ESWITCH_VEB);
		if (rc != ECORE_SUCCESS)
			DP_INFO(p_hwfn, "Failed to update eswitch mode\n");
	}

	return rc;

load_err:
	--qm_lock_ref_cnt;
#ifdef CONFIG_ECORE_LOCK_ALLOC
	if (!qm_lock_ref_cnt)
		OSAL_SPIN_LOCK_DEALLOC(&qm_lock);
qm_lock_fail:
#endif
	/* The MFW load lock should be released also when initialization fails.
	 * If supported, use a cancel_load request to update the MFW with the
	 * load failure.
	 */
	cancel_load = ecore_mcp_cancel_load_req(p_hwfn, p_hwfn->p_main_ptt);
	if (cancel_load == ECORE_NOTIMPL) {
		DP_INFO(p_hwfn,
			"Send a load done request instead of cancel load\n");
		ecore_mcp_load_done(p_hwfn, p_hwfn->p_main_ptt);
	}
	return rc;
}

#define ECORE_HW_STOP_RETRY_LIMIT	(10)
static void ecore_hw_timers_stop(struct ecore_dev *p_dev,
				 struct ecore_hwfn *p_hwfn,
				 struct ecore_ptt *p_ptt)
{
	int i;

	/* close timers */
	ecore_wr(p_hwfn, p_ptt, TM_REG_PF_ENABLE_CONN, 0x0);
	ecore_wr(p_hwfn, p_ptt, TM_REG_PF_ENABLE_TASK, 0x0);
	for (i = 0;
	     i < ECORE_HW_STOP_RETRY_LIMIT && !p_dev->recov_in_prog;
	     i++) {
		if ((!ecore_rd(p_hwfn, p_ptt,
			       TM_REG_PF_SCAN_ACTIVE_CONN)) &&
		    (!ecore_rd(p_hwfn, p_ptt,
			       TM_REG_PF_SCAN_ACTIVE_TASK)))
			break;

		/* Dependent on number of connection/tasks, possibly
		 * 1ms sleep is required between polls
		 */
		OSAL_MSLEEP(1);
	}

	if (i < ECORE_HW_STOP_RETRY_LIMIT)
		return;

	DP_NOTICE(p_hwfn, false,
		  "Timers linear scans are not over [Connection %02x Tasks %02x]\n",
		  (u8)ecore_rd(p_hwfn, p_ptt, TM_REG_PF_SCAN_ACTIVE_CONN),
		  (u8)ecore_rd(p_hwfn, p_ptt, TM_REG_PF_SCAN_ACTIVE_TASK));
}

void ecore_hw_timers_stop_all(struct ecore_dev *p_dev)
{
	int j;

	for_each_hwfn(p_dev, j) {
		struct ecore_hwfn *p_hwfn = &p_dev->hwfns[j];
		struct ecore_ptt *p_ptt = p_hwfn->p_main_ptt;

		ecore_hw_timers_stop(p_dev, p_hwfn, p_ptt);
	}
}

static enum _ecore_status_t ecore_verify_reg_val(struct ecore_hwfn *p_hwfn,
						 struct ecore_ptt *p_ptt,
						 u32 addr, u32 expected_val)
{
	u32 val = ecore_rd(p_hwfn, p_ptt, addr);

	if (val != expected_val) {
		DP_NOTICE(p_hwfn, true,
			  "Value at address 0x%08x is 0x%08x while the expected value is 0x%08x\n",
			  addr, val, expected_val);
		return ECORE_UNKNOWN_ERROR;
	}

	return ECORE_SUCCESS;
}

enum _ecore_status_t ecore_hw_stop(struct ecore_dev *p_dev)
{
	struct ecore_hwfn *p_hwfn;
	struct ecore_ptt *p_ptt;
	enum _ecore_status_t rc, rc2 = ECORE_SUCCESS;
	int j;

	for_each_hwfn(p_dev, j) {
		p_hwfn = &p_dev->hwfns[j];
		p_ptt = p_hwfn->p_main_ptt;

		DP_VERBOSE(p_hwfn, ECORE_MSG_IFDOWN, "Stopping hw/fw\n");

		if (IS_VF(p_dev)) {
			ecore_vf_pf_int_cleanup(p_hwfn);
			rc = ecore_vf_pf_reset(p_hwfn);
			if (rc != ECORE_SUCCESS) {
				DP_NOTICE(p_hwfn, true,
					  "ecore_vf_pf_reset failed. rc = %d.\n",
					  rc);
				rc2 = ECORE_UNKNOWN_ERROR;
			}
			continue;
		}

		/* mark the hw as uninitialized... */
		p_hwfn->hw_init_done = false;

		/* Send unload command to MCP */
		if (!p_dev->recov_in_prog) {
			rc = ecore_mcp_unload_req(p_hwfn, p_ptt);
			if (rc != ECORE_SUCCESS) {
				DP_NOTICE(p_hwfn, false,
					  "Failed sending a UNLOAD_REQ command. rc = %d.\n",
					  rc);
				rc2 = ECORE_UNKNOWN_ERROR;
			}
		}

		OSAL_DPC_SYNC(p_hwfn);

		/* After this point no MFW attentions are expected, e.g. prevent
		 * race between pf stop and dcbx pf update.
		 */

		rc = ecore_sp_pf_stop(p_hwfn);
		if (rc != ECORE_SUCCESS) {
			DP_NOTICE(p_hwfn, false,
				  "Failed to close PF against FW [rc = %d]. Continue to stop HW to prevent illegal host access by the device.\n",
				  rc);
			rc2 = ECORE_UNKNOWN_ERROR;
		}

		/* perform debug action after PF stop was sent */
		OSAL_AFTER_PF_STOP((void *)p_dev, p_hwfn->my_id);

		/* close NIG to BRB gate */
		ecore_wr(p_hwfn, p_ptt,
			 NIG_REG_RX_LLH_BRB_GATE_DNTFWD_PERPF, 0x1);

		/* close parser */
		ecore_wr(p_hwfn, p_ptt, PRS_REG_SEARCH_TCP, 0x0);
		ecore_wr(p_hwfn, p_ptt, PRS_REG_SEARCH_UDP, 0x0);
		ecore_wr(p_hwfn, p_ptt, PRS_REG_SEARCH_FCOE, 0x0);
		ecore_wr(p_hwfn, p_ptt, PRS_REG_SEARCH_ROCE, 0x0);
		ecore_wr(p_hwfn, p_ptt, PRS_REG_SEARCH_OPENFLOW, 0x0);

		/* @@@TBD - clean transmission queues (5.b) */
		/* @@@TBD - clean BTB (5.c) */

		ecore_hw_timers_stop(p_dev, p_hwfn, p_ptt);

		/* @@@TBD - verify DMAE requests are done (8) */

		/* Disable Attention Generation */
		ecore_int_igu_disable_int(p_hwfn, p_ptt);
		ecore_wr(p_hwfn, p_ptt, IGU_REG_LEADING_EDGE_LATCH, 0);
		ecore_wr(p_hwfn, p_ptt, IGU_REG_TRAILING_EDGE_LATCH, 0);
		ecore_int_igu_init_pure_rt(p_hwfn, p_ptt, false, true);
		rc = ecore_int_igu_reset_cam_default(p_hwfn, p_ptt);
		if (rc != ECORE_SUCCESS) {
			DP_NOTICE(p_hwfn, true,
				  "Failed to return IGU CAM to default\n");
			rc2 = ECORE_UNKNOWN_ERROR;
		}

		/* Need to wait 1ms to guarantee SBs are cleared */
		OSAL_MSLEEP(1);

		if (!p_dev->recov_in_prog) {
			ecore_verify_reg_val(p_hwfn, p_ptt,
					     QM_REG_USG_CNT_PF_TX, 0);
			ecore_verify_reg_val(p_hwfn, p_ptt,
					     QM_REG_USG_CNT_PF_OTHER, 0);
			/* @@@TBD - assert on incorrect xCFC values (10.b) */
		}

		/* Disable PF in HW blocks */
		ecore_wr(p_hwfn, p_ptt, DORQ_REG_PF_DB_ENABLE, 0);
		ecore_wr(p_hwfn, p_ptt, QM_REG_PF_EN, 0);

		if (IS_LEAD_HWFN(p_hwfn) &&
		    OSAL_TEST_BIT(ECORE_MF_LLH_MAC_CLSS, &p_dev->mf_bits) &&
		    !ECORE_IS_FCOE_PERSONALITY(p_hwfn))
			ecore_llh_remove_mac_filter(p_dev, 0,
						    p_hwfn->hw_info.hw_mac_addr);

		--qm_lock_ref_cnt;
#ifdef CONFIG_ECORE_LOCK_ALLOC
		if (!qm_lock_ref_cnt)
			OSAL_SPIN_LOCK_DEALLOC(&qm_lock);
#endif

		if (!p_dev->recov_in_prog) {
			rc = ecore_mcp_unload_done(p_hwfn, p_ptt);
			if (rc == ECORE_NOMEM) {
				DP_NOTICE(p_hwfn, false,
					 "Failed sending an UNLOAD_DONE command due to a memory allocation failure. Resending.\n");
				rc = ecore_mcp_unload_done(p_hwfn, p_ptt);
			}
			if (rc != ECORE_SUCCESS) {
				DP_NOTICE(p_hwfn, false,
					  "Failed sending a UNLOAD_DONE command. rc = %d.\n",
					  rc);
				rc2 = ECORE_UNKNOWN_ERROR;
			}
		}
	} /* hwfn loop */

	if (IS_PF(p_dev) && !p_dev->recov_in_prog) {
		p_hwfn = ECORE_LEADING_HWFN(p_dev);
		p_ptt = ECORE_LEADING_HWFN(p_dev)->p_main_ptt;

		 /* Clear the PF's internal FID_enable in the PXP.
		  * In CMT this should only be done for first hw-function, and
		  * only after all transactions have stopped for all active
		  * hw-functions.
		  */
		rc = ecore_pglueb_set_pfid_enable(p_hwfn, p_hwfn->p_main_ptt,
						  false);
		if (rc != ECORE_SUCCESS) {
			DP_NOTICE(p_hwfn, true,
				  "ecore_pglueb_set_pfid_enable() failed. rc = %d.\n",
				  rc);
			rc2 = ECORE_UNKNOWN_ERROR;
		}
	}

	return rc2;
}

enum _ecore_status_t ecore_hw_stop_fastpath(struct ecore_dev *p_dev)
{
	int j;

	for_each_hwfn(p_dev, j) {
		struct ecore_hwfn *p_hwfn = &p_dev->hwfns[j];
		struct ecore_ptt *p_ptt;

		if (IS_VF(p_dev)) {
			ecore_vf_pf_int_cleanup(p_hwfn);
			continue;
		}
		p_ptt = ecore_ptt_acquire(p_hwfn);
		if (!p_ptt)
			return ECORE_AGAIN;

		DP_VERBOSE(p_hwfn, ECORE_MSG_IFDOWN, "Shutting down the fastpath\n");

		ecore_wr(p_hwfn, p_ptt,
			 NIG_REG_RX_LLH_BRB_GATE_DNTFWD_PERPF, 0x1);

		ecore_wr(p_hwfn, p_ptt, PRS_REG_SEARCH_TCP, 0x0);
		ecore_wr(p_hwfn, p_ptt, PRS_REG_SEARCH_UDP, 0x0);
		ecore_wr(p_hwfn, p_ptt, PRS_REG_SEARCH_FCOE, 0x0);
		ecore_wr(p_hwfn, p_ptt, PRS_REG_SEARCH_ROCE, 0x0);
		ecore_wr(p_hwfn, p_ptt, PRS_REG_SEARCH_OPENFLOW, 0x0);

		/* @@@TBD - clean transmission queues (5.b) */
		/* @@@TBD - clean BTB (5.c) */

		/* @@@TBD - verify DMAE requests are done (8) */

		ecore_int_igu_init_pure_rt(p_hwfn, p_ptt, false, false);
		/* Need to wait 1ms to guarantee SBs are cleared */
		OSAL_MSLEEP(1);
		ecore_ptt_release(p_hwfn, p_ptt);
	}

	return ECORE_SUCCESS;
}

enum _ecore_status_t ecore_hw_start_fastpath(struct ecore_hwfn *p_hwfn)
{
	struct ecore_ptt *p_ptt;

	if (IS_VF(p_hwfn->p_dev))
		return ECORE_SUCCESS;

	p_ptt = ecore_ptt_acquire(p_hwfn);
	if (!p_ptt)
		return ECORE_AGAIN;

	/* If roce info is allocated it means roce is initialized and should
	 * be enabled in searcher.
	 */
	if (p_hwfn->p_rdma_info &&
	    p_hwfn->p_rdma_info->active &&
	    p_hwfn->b_rdma_enabled_in_prs)
		ecore_wr(p_hwfn, p_ptt, p_hwfn->rdma_prs_search_reg, 0x1);

	/* Re-open incoming traffic */
	ecore_wr(p_hwfn, p_ptt,
		 NIG_REG_RX_LLH_BRB_GATE_DNTFWD_PERPF, 0x0);
	ecore_ptt_release(p_hwfn, p_ptt);

	return ECORE_SUCCESS;
}

enum _ecore_status_t ecore_set_nwuf_reg(struct ecore_dev *p_dev, u32 reg_idx,
					u32 pattern_size, u32 crc)
{
	struct ecore_hwfn *p_hwfn = ECORE_LEADING_HWFN(p_dev);
	enum _ecore_status_t rc = ECORE_SUCCESS;
	struct ecore_ptt *p_ptt;
	u32 reg_len = 0;
	u32 reg_crc = 0;

	p_ptt = ecore_ptt_acquire(p_hwfn);
	if (!p_ptt)
		return ECORE_AGAIN;

	/* Get length and CRC register offsets */
	switch (reg_idx)
	{
	case 0:
		reg_len = ECORE_IS_BB(p_dev) ? NIG_REG_ACPI_PAT_0_LEN_BB :
				WOL_REG_ACPI_PAT_0_LEN_K2_E5;
		reg_crc = ECORE_IS_BB(p_dev) ? NIG_REG_ACPI_PAT_0_CRC_BB :
				WOL_REG_ACPI_PAT_0_CRC_K2_E5;
		break;
	case 1:
		reg_len = ECORE_IS_BB(p_dev) ? NIG_REG_ACPI_PAT_1_LEN_BB :
				WOL_REG_ACPI_PAT_1_LEN_K2_E5;
		reg_crc = ECORE_IS_BB(p_dev) ? NIG_REG_ACPI_PAT_1_CRC_BB :
				WOL_REG_ACPI_PAT_1_CRC_K2_E5;
		break;
	case 2:
		reg_len = ECORE_IS_BB(p_dev) ? NIG_REG_ACPI_PAT_2_LEN_BB :
				WOL_REG_ACPI_PAT_2_LEN_K2_E5;
		reg_crc = ECORE_IS_BB(p_dev) ? NIG_REG_ACPI_PAT_2_CRC_BB :
				WOL_REG_ACPI_PAT_2_CRC_K2_E5;
		break;
	case 3:
		reg_len = ECORE_IS_BB(p_dev) ? NIG_REG_ACPI_PAT_3_LEN_BB :
				WOL_REG_ACPI_PAT_3_LEN_K2_E5;
		reg_crc = ECORE_IS_BB(p_dev) ? NIG_REG_ACPI_PAT_3_CRC_BB :
				WOL_REG_ACPI_PAT_3_CRC_K2_E5;
		break;
	case 4:
		reg_len = ECORE_IS_BB(p_dev) ? NIG_REG_ACPI_PAT_4_LEN_BB :
				WOL_REG_ACPI_PAT_4_LEN_K2_E5;
		reg_crc = ECORE_IS_BB(p_dev) ? NIG_REG_ACPI_PAT_4_CRC_BB :
				WOL_REG_ACPI_PAT_4_CRC_K2_E5;
		break;
	case 5:
		reg_len = ECORE_IS_BB(p_dev) ? NIG_REG_ACPI_PAT_5_LEN_BB :
				WOL_REG_ACPI_PAT_5_LEN_K2_E5;
		reg_crc = ECORE_IS_BB(p_dev) ? NIG_REG_ACPI_PAT_5_CRC_BB :
				WOL_REG_ACPI_PAT_5_CRC_K2_E5;
		break;
	case 6:
		reg_len = ECORE_IS_BB(p_dev) ? NIG_REG_ACPI_PAT_6_LEN_BB :
				WOL_REG_ACPI_PAT_6_LEN_K2_E5;
		reg_crc = ECORE_IS_BB(p_dev) ? NIG_REG_ACPI_PAT_6_CRC_BB :
				WOL_REG_ACPI_PAT_6_CRC_K2_E5;
		break;
	case 7:
		reg_len = ECORE_IS_BB(p_dev) ? NIG_REG_ACPI_PAT_7_LEN_BB :
				WOL_REG_ACPI_PAT_7_LEN_K2_E5;
		reg_crc = ECORE_IS_BB(p_dev) ? NIG_REG_ACPI_PAT_7_CRC_BB :
				WOL_REG_ACPI_PAT_7_CRC_K2_E5;
		break;
	default:
		rc = ECORE_UNKNOWN_ERROR;
		goto out;
	}

	/* Allign pattern size to 4 */
	while (pattern_size % 4)
		pattern_size++;

	/* Write pattern length and crc value */
	if (ECORE_IS_BB(p_dev)) {
		rc = ecore_all_ppfids_wr(p_hwfn, p_ptt, reg_len, pattern_size);
		if (rc != ECORE_SUCCESS) {
			DP_NOTICE(p_hwfn, false,
				  "Failed to update the ACPI pattern length\n");
			return rc;
		}

		rc = ecore_all_ppfids_wr(p_hwfn, p_ptt, reg_crc, crc);
		if (rc != ECORE_SUCCESS) {
			DP_NOTICE(p_hwfn, false,
				  "Failed to update the ACPI pattern crc value\n");
			return rc;
		}
	} else {
		ecore_mcp_wol_wr(p_hwfn, p_ptt, reg_len, pattern_size);
		ecore_mcp_wol_wr(p_hwfn, p_ptt, reg_crc, crc);
	}

	DP_INFO(p_dev,
		"ecore_set_nwuf_reg: idx[%d] reg_crc[0x%x=0x%08x] "
		"reg_len[0x%x=0x%x]\n",
		reg_idx, reg_crc, crc, reg_len, pattern_size);
out:
	 ecore_ptt_release(p_hwfn, p_ptt);

	return rc;
}

void ecore_wol_buffer_clear(struct ecore_hwfn *p_hwfn,
			    struct ecore_ptt *p_ptt)
{
	const u32 wake_buffer_clear_offset =
		ECORE_IS_BB(p_hwfn->p_dev) ?
		NIG_REG_WAKE_BUFFER_CLEAR_BB : WOL_REG_WAKE_BUFFER_CLEAR_K2_E5;

	DP_INFO(p_hwfn->p_dev,
		"ecore_wol_buffer_clear: reset "
		"REG_WAKE_BUFFER_CLEAR offset=0x%08x\n",
		wake_buffer_clear_offset);

	if (ECORE_IS_BB(p_hwfn->p_dev)) {
		ecore_wr(p_hwfn, p_ptt, wake_buffer_clear_offset, 1);
		ecore_wr(p_hwfn, p_ptt, wake_buffer_clear_offset, 0);
	} else {
		ecore_mcp_wol_wr(p_hwfn, p_ptt, wake_buffer_clear_offset, 1);
		ecore_mcp_wol_wr(p_hwfn, p_ptt, wake_buffer_clear_offset, 0);
	}
}

enum _ecore_status_t ecore_get_wake_info(struct ecore_hwfn *p_hwfn,
					 struct ecore_ptt *p_ptt,
					 struct ecore_wake_info *wake_info)
{
	struct ecore_dev *p_dev = p_hwfn->p_dev;
	u32 *buf = OSAL_NULL;
	u32 i    = 0;
	const u32 reg_wake_buffer_offest =
		ECORE_IS_BB(p_dev) ? NIG_REG_WAKE_BUFFER_BB :
			WOL_REG_WAKE_BUFFER_K2_E5;

	wake_info->wk_info    = ecore_rd(p_hwfn, p_ptt,
				ECORE_IS_BB(p_dev) ? NIG_REG_WAKE_INFO_BB :
				WOL_REG_WAKE_INFO_K2_E5);
	wake_info->wk_details = ecore_rd(p_hwfn, p_ptt,
				ECORE_IS_BB(p_dev) ? NIG_REG_WAKE_DETAILS_BB :
				WOL_REG_WAKE_DETAILS_K2_E5);
	wake_info->wk_pkt_len = ecore_rd(p_hwfn, p_ptt,
				ECORE_IS_BB(p_dev) ? NIG_REG_WAKE_PKT_LEN_BB :
				WOL_REG_WAKE_PKT_LEN_K2_E5);

	DP_INFO(p_dev,
		"ecore_get_wake_info: REG_WAKE_INFO=0x%08x "
		"REG_WAKE_DETAILS=0x%08x "
		"REG_WAKE_PKT_LEN=0x%08x\n",
		wake_info->wk_info,
		wake_info->wk_details,
		wake_info->wk_pkt_len);

	buf = (u32 *)wake_info->wk_buffer;

	for (i = 0; i < (wake_info->wk_pkt_len / sizeof(u32)); i++)
	{
		if ((i*sizeof(u32)) >=  sizeof(wake_info->wk_buffer))
		{
			DP_INFO(p_dev,
				"ecore_get_wake_info: i index to 0 high=%d\n",
				 i);
			break;
		}
		buf[i] = ecore_rd(p_hwfn, p_ptt,
				  reg_wake_buffer_offest + (i * sizeof(u32)));
		DP_INFO(p_dev, "ecore_get_wake_info: wk_buffer[%u]: 0x%08x\n",
			i, buf[i]);
	}

	ecore_wol_buffer_clear(p_hwfn, p_ptt);

	return ECORE_SUCCESS;
}

/* Free hwfn memory and resources acquired in hw_hwfn_prepare */
static void ecore_hw_hwfn_free(struct ecore_hwfn *p_hwfn)
{
	ecore_ptt_pool_free(p_hwfn);
	OSAL_FREE(p_hwfn->p_dev, p_hwfn->hw_info.p_igu_info);
	p_hwfn->hw_info.p_igu_info = OSAL_NULL;
}

/* Setup bar access */
static void ecore_hw_hwfn_prepare(struct ecore_hwfn *p_hwfn)
{
	/* clear indirect access */
	if (ECORE_IS_AH(p_hwfn->p_dev) || ECORE_IS_E5(p_hwfn->p_dev)) {
		ecore_wr(p_hwfn, p_hwfn->p_main_ptt,
			 PGLUE_B_REG_PGL_ADDR_E8_F0_K2_E5, 0);
		ecore_wr(p_hwfn, p_hwfn->p_main_ptt,
			 PGLUE_B_REG_PGL_ADDR_EC_F0_K2_E5, 0);
		ecore_wr(p_hwfn, p_hwfn->p_main_ptt,
			 PGLUE_B_REG_PGL_ADDR_F0_F0_K2_E5, 0);
		ecore_wr(p_hwfn, p_hwfn->p_main_ptt,
			 PGLUE_B_REG_PGL_ADDR_F4_F0_K2_E5, 0);
	} else {
		ecore_wr(p_hwfn, p_hwfn->p_main_ptt,
			 PGLUE_B_REG_PGL_ADDR_88_F0_BB, 0);
		ecore_wr(p_hwfn, p_hwfn->p_main_ptt,
			 PGLUE_B_REG_PGL_ADDR_8C_F0_BB, 0);
		ecore_wr(p_hwfn, p_hwfn->p_main_ptt,
			 PGLUE_B_REG_PGL_ADDR_90_F0_BB, 0);
		ecore_wr(p_hwfn, p_hwfn->p_main_ptt,
			 PGLUE_B_REG_PGL_ADDR_94_F0_BB, 0);
	}

	/* Clean previous pglue_b errors if such exist */
	ecore_pglueb_clear_err(p_hwfn, p_hwfn->p_main_ptt);

	/* enable internal target-read */
	ecore_wr(p_hwfn, p_hwfn->p_main_ptt,
		 PGLUE_B_REG_INTERNAL_PFID_ENABLE_TARGET_READ, 1);
}

static void get_function_id(struct ecore_hwfn *p_hwfn)
{
	/* ME Register */
	p_hwfn->hw_info.opaque_fid = (u16) REG_RD(p_hwfn,
						  PXP_PF_ME_OPAQUE_ADDR);

	p_hwfn->hw_info.concrete_fid = REG_RD(p_hwfn, PXP_PF_ME_CONCRETE_ADDR);

	/* Bits 16-19 from the ME registers are the pf_num */
	p_hwfn->abs_pf_id = (p_hwfn->hw_info.concrete_fid >> 16) & 0xf;
	p_hwfn->rel_pf_id = GET_FIELD(p_hwfn->hw_info.concrete_fid,
				      PXP_CONCRETE_FID_PFID);
	p_hwfn->port_id = GET_FIELD(p_hwfn->hw_info.concrete_fid,
				    PXP_CONCRETE_FID_PORT);

	DP_VERBOSE(p_hwfn, ECORE_MSG_PROBE,
		   "Read ME register: Concrete 0x%08x Opaque 0x%04x\n",
		   p_hwfn->hw_info.concrete_fid, p_hwfn->hw_info.opaque_fid);
}

void ecore_hw_set_feat(struct ecore_hwfn *p_hwfn)
{
	u32 *feat_num = p_hwfn->hw_info.feat_num;
	struct ecore_sb_cnt_info sb_cnt;
	u32 non_l2_sbs = 0;

	OSAL_MEM_ZERO(&sb_cnt, sizeof(sb_cnt));
	ecore_int_get_num_sbs(p_hwfn, &sb_cnt);

#ifdef CONFIG_ECORE_ROCE
	/* Roce CNQ require each: 1 status block. 1 CNQ, we divide the
	 * status blocks equally between L2 / RoCE but with consideration as
	 * to how many l2 queues / cnqs we have
	 */
	if (ECORE_IS_RDMA_PERSONALITY(p_hwfn)) {
#ifndef __EXTRACT__LINUX__THROW__
		u32 max_cnqs;
#endif

		feat_num[ECORE_RDMA_CNQ] =
			OSAL_MIN_T(u32,
				   sb_cnt.cnt / 2,
				   RESC_NUM(p_hwfn, ECORE_RDMA_CNQ_RAM));

#ifndef __EXTRACT__LINUX__THROW__
		/* Upper layer might require less */
		max_cnqs = (u32)p_hwfn->pf_params.rdma_pf_params.max_cnqs;
		if (max_cnqs) {
			if (max_cnqs == ECORE_RDMA_PF_PARAMS_CNQS_NONE)
				max_cnqs = 0;
			feat_num[ECORE_RDMA_CNQ] =
				OSAL_MIN_T(u32,
					   feat_num[ECORE_RDMA_CNQ],
					   max_cnqs);
		}
#endif

		non_l2_sbs = feat_num[ECORE_RDMA_CNQ];
	}
#endif

	/* L2 Queues require each: 1 status block. 1 L2 queue */
	if (ECORE_IS_L2_PERSONALITY(p_hwfn)) {
		/* Start by allocating VF queues, then PF's */
		feat_num[ECORE_VF_L2_QUE] =
			OSAL_MIN_T(u32,
				   RESC_NUM(p_hwfn, ECORE_L2_QUEUE),
				   sb_cnt.iov_cnt);
		feat_num[ECORE_PF_L2_QUE] =
			OSAL_MIN_T(u32,
				   sb_cnt.cnt - non_l2_sbs,
				   RESC_NUM(p_hwfn, ECORE_L2_QUEUE) -
				   FEAT_NUM(p_hwfn, ECORE_VF_L2_QUE));
	}

	if (ECORE_IS_FCOE_PERSONALITY(p_hwfn))
		feat_num[ECORE_FCOE_CQ] =
			OSAL_MIN_T(u32, sb_cnt.cnt, RESC_NUM(p_hwfn,
							     ECORE_CMDQS_CQS));

	if (ECORE_IS_ISCSI_PERSONALITY(p_hwfn))
		feat_num[ECORE_ISCSI_CQ] =
			OSAL_MIN_T(u32, sb_cnt.cnt, RESC_NUM(p_hwfn,
							     ECORE_CMDQS_CQS));

	DP_VERBOSE(p_hwfn, ECORE_MSG_PROBE,
		   "#PF_L2_QUEUE=%d VF_L2_QUEUES=%d #ROCE_CNQ=%d #FCOE_CQ=%d #ISCSI_CQ=%d #SB=%d\n",
		   (int)FEAT_NUM(p_hwfn, ECORE_PF_L2_QUE),
		   (int)FEAT_NUM(p_hwfn, ECORE_VF_L2_QUE),
		   (int)FEAT_NUM(p_hwfn, ECORE_RDMA_CNQ),
		   (int)FEAT_NUM(p_hwfn, ECORE_FCOE_CQ),
		   (int)FEAT_NUM(p_hwfn, ECORE_ISCSI_CQ),
		   (int)sb_cnt.cnt);
}

const char *ecore_hw_get_resc_name(enum ecore_resources res_id)
{
	switch (res_id) {
	case ECORE_L2_QUEUE:
		return "L2_QUEUE";
	case ECORE_VPORT:
		return "VPORT";
	case ECORE_RSS_ENG:
		return "RSS_ENG";
	case ECORE_PQ:
		return "PQ";
	case ECORE_RL:
		return "RL";
	case ECORE_MAC:
		return "MAC";
	case ECORE_VLAN:
		return "VLAN";
	case ECORE_RDMA_CNQ_RAM:
		return "RDMA_CNQ_RAM";
	case ECORE_ILT:
		return "ILT";
	case ECORE_LL2_QUEUE:
		return "LL2_QUEUE";
	case ECORE_CMDQS_CQS:
		return "CMDQS_CQS";
	case ECORE_RDMA_STATS_QUEUE:
		return "RDMA_STATS_QUEUE";
	case ECORE_BDQ:
		return "BDQ";
	case ECORE_SB:
		return "SB";
	default:
		return "UNKNOWN_RESOURCE";
	}
}

static enum _ecore_status_t
__ecore_hw_set_soft_resc_size(struct ecore_hwfn *p_hwfn,
			      struct ecore_ptt *p_ptt,
			      enum ecore_resources res_id,
			      u32 resc_max_val,
			      u32 *p_mcp_resp)
{
	enum _ecore_status_t rc;

	rc = ecore_mcp_set_resc_max_val(p_hwfn, p_ptt, res_id,
					resc_max_val, p_mcp_resp);
	if (rc != ECORE_SUCCESS) {
		DP_NOTICE(p_hwfn, false,
			  "MFW response failure for a max value setting of resource %d [%s]\n",
			  res_id, ecore_hw_get_resc_name(res_id));
		return rc;
	}

	if (*p_mcp_resp != FW_MSG_CODE_RESOURCE_ALLOC_OK)
		DP_INFO(p_hwfn,
			"Failed to set the max value of resource %d [%s]. mcp_resp = 0x%08x.\n",
			res_id, ecore_hw_get_resc_name(res_id), *p_mcp_resp);

	return ECORE_SUCCESS;
}

static enum _ecore_status_t
ecore_hw_set_soft_resc_size(struct ecore_hwfn *p_hwfn,
			    struct ecore_ptt *p_ptt)
{
	bool b_ah = ECORE_IS_AH(p_hwfn->p_dev);
	u32 resc_max_val, mcp_resp;
	u8 res_id;
	enum _ecore_status_t rc;

	for (res_id = 0; res_id < ECORE_MAX_RESC; res_id++) {
		switch (res_id) {
		case ECORE_LL2_QUEUE:
			resc_max_val = MAX_NUM_LL2_RX_QUEUES;
			break;
		case ECORE_RDMA_CNQ_RAM:
			/* No need for a case for ECORE_CMDQS_CQS since
			 * CNQ/CMDQS are the same resource.
			 */
			resc_max_val = NUM_OF_GLOBAL_QUEUES;
			break;
		case ECORE_RDMA_STATS_QUEUE:
			resc_max_val = b_ah ? RDMA_NUM_STATISTIC_COUNTERS_K2
					    : RDMA_NUM_STATISTIC_COUNTERS_BB;
			break;
		case ECORE_BDQ:
			resc_max_val = BDQ_NUM_RESOURCES;
			break;
		default:
			continue;
		}

		rc = __ecore_hw_set_soft_resc_size(p_hwfn, p_ptt, res_id,
						   resc_max_val, &mcp_resp);
		if (rc != ECORE_SUCCESS)
			return rc;

		/* There's no point to continue to the next resource if the
		 * command is not supported by the MFW.
		 * We do continue if the command is supported but the resource
		 * is unknown to the MFW. Such a resource will be later
		 * configured with the default allocation values.
		 */
		if (mcp_resp == FW_MSG_CODE_UNSUPPORTED)
			return ECORE_NOTIMPL;
	}

	return ECORE_SUCCESS;
}

static
enum _ecore_status_t ecore_hw_get_dflt_resc(struct ecore_hwfn *p_hwfn,
					    enum ecore_resources res_id,
					    u32 *p_resc_num, u32 *p_resc_start)
{
	u8 num_funcs = p_hwfn->num_funcs_on_engine;
	bool b_ah = ECORE_IS_AH(p_hwfn->p_dev);

	switch (res_id) {
	case ECORE_L2_QUEUE:
		*p_resc_num = (b_ah ? MAX_NUM_L2_QUEUES_K2 :
				      MAX_NUM_L2_QUEUES_BB) / num_funcs;
		break;
	case ECORE_VPORT:
		*p_resc_num = (b_ah ? MAX_NUM_VPORTS_K2 :
				      MAX_NUM_VPORTS_BB) / num_funcs;
		break;
	case ECORE_RSS_ENG:
		*p_resc_num = (b_ah ? ETH_RSS_ENGINE_NUM_K2 :
				      ETH_RSS_ENGINE_NUM_BB) / num_funcs;
		break;
	case ECORE_PQ:
		*p_resc_num = (b_ah ? MAX_QM_TX_QUEUES_K2 :
				      MAX_QM_TX_QUEUES_BB) / num_funcs;
		*p_resc_num &= ~0x7; /* The granularity of the PQs is 8 */
		break;
	case ECORE_RL:
		*p_resc_num = MAX_QM_GLOBAL_RLS / num_funcs;
		break;
	case ECORE_MAC:
	case ECORE_VLAN:
		/* Each VFC resource can accommodate both a MAC and a VLAN */
		*p_resc_num = ETH_NUM_MAC_FILTERS / num_funcs;
		break;
	case ECORE_ILT:
		*p_resc_num = (b_ah ? PXP_NUM_ILT_RECORDS_K2 :
				      PXP_NUM_ILT_RECORDS_BB) / num_funcs;
		break;
	case ECORE_LL2_QUEUE:
		*p_resc_num = MAX_NUM_LL2_RX_QUEUES / num_funcs;
		break;
	case ECORE_RDMA_CNQ_RAM:
	case ECORE_CMDQS_CQS:
		/* CNQ/CMDQS are the same resource */
		*p_resc_num = NUM_OF_GLOBAL_QUEUES / num_funcs;
		break;
	case ECORE_RDMA_STATS_QUEUE:
		*p_resc_num = (b_ah ? RDMA_NUM_STATISTIC_COUNTERS_K2 :
				      RDMA_NUM_STATISTIC_COUNTERS_BB) /
			      num_funcs;
		break;
	case ECORE_BDQ:
		if (p_hwfn->hw_info.personality != ECORE_PCI_ISCSI &&
		    p_hwfn->hw_info.personality != ECORE_PCI_FCOE)
			*p_resc_num = 0;
		else
			*p_resc_num = 1;
		break;
	case ECORE_SB:
		/* Since we want its value to reflect whether MFW supports
		 * the new scheme, have a default of 0.
		 */
		*p_resc_num = 0;
		break;
	default:
		return ECORE_INVAL;
	}

	switch (res_id) {
	case ECORE_BDQ:
		if (!*p_resc_num)
			*p_resc_start = 0;
		else if (p_hwfn->p_dev->num_ports_in_engine == 4)
			*p_resc_start = p_hwfn->port_id;
		else if (p_hwfn->hw_info.personality == ECORE_PCI_ISCSI)
			*p_resc_start = p_hwfn->port_id;
		else if (p_hwfn->hw_info.personality == ECORE_PCI_FCOE)
			*p_resc_start = p_hwfn->port_id + 2;
		break;
	default:
		*p_resc_start = *p_resc_num * p_hwfn->enabled_func_idx;
		break;
	}

	return ECORE_SUCCESS;
}

static enum _ecore_status_t
__ecore_hw_set_resc_info(struct ecore_hwfn *p_hwfn, enum ecore_resources res_id,
			 bool drv_resc_alloc)
{
	u32 dflt_resc_num = 0, dflt_resc_start = 0;
	u32 mcp_resp, *p_resc_num, *p_resc_start;
	enum _ecore_status_t rc;

	p_resc_num = &RESC_NUM(p_hwfn, res_id);
	p_resc_start = &RESC_START(p_hwfn, res_id);

	rc = ecore_hw_get_dflt_resc(p_hwfn, res_id, &dflt_resc_num,
				    &dflt_resc_start);
	if (rc != ECORE_SUCCESS) {
		DP_ERR(p_hwfn,
		       "Failed to get default amount for resource %d [%s]\n",
			res_id, ecore_hw_get_resc_name(res_id));
		return rc;
	}

#ifndef ASIC_ONLY
	if (CHIP_REV_IS_SLOW(p_hwfn->p_dev)) {
		*p_resc_num = dflt_resc_num;
		*p_resc_start = dflt_resc_start;
		goto out;
	}
#endif

	rc = ecore_mcp_get_resc_info(p_hwfn, p_hwfn->p_main_ptt, res_id,
				     &mcp_resp, p_resc_num, p_resc_start);
	if (rc != ECORE_SUCCESS) {
		DP_NOTICE(p_hwfn, false,
			  "MFW response failure for an allocation request for resource %d [%s]\n",
			  res_id, ecore_hw_get_resc_name(res_id));
		return rc;
	}

	/* Default driver values are applied in the following cases:
	 * - The resource allocation MB command is not supported by the MFW
	 * - There is an internal error in the MFW while processing the request
	 * - The resource ID is unknown to the MFW
	 */
	if (mcp_resp != FW_MSG_CODE_RESOURCE_ALLOC_OK) {
		DP_INFO(p_hwfn,
			"Failed to receive allocation info for resource %d [%s]. mcp_resp = 0x%x. Applying default values [%d,%d].\n",
			res_id, ecore_hw_get_resc_name(res_id), mcp_resp,
			dflt_resc_num, dflt_resc_start);
		*p_resc_num = dflt_resc_num;
		*p_resc_start = dflt_resc_start;
		goto out;
	}

	if ((*p_resc_num != dflt_resc_num ||
	     *p_resc_start != dflt_resc_start) &&
	    res_id != ECORE_SB) {
		DP_INFO(p_hwfn,
			"MFW allocation for resource %d [%s] differs from default values [%d,%d vs. %d,%d]%s\n",
			res_id, ecore_hw_get_resc_name(res_id), *p_resc_num,
			*p_resc_start, dflt_resc_num, dflt_resc_start,
			drv_resc_alloc ? " - Applying default values" : "");
		if (drv_resc_alloc) {
			*p_resc_num = dflt_resc_num;
			*p_resc_start = dflt_resc_start;
		}
	}
out:
	/* PQs have to divide by 8 [that's the HW granularity].
	 * Reduce number so it would fit.
	 */
	if ((res_id == ECORE_PQ) &&
	    ((*p_resc_num % 8) || (*p_resc_start % 8))) {
		DP_INFO(p_hwfn,
			"PQs need to align by 8; Number %08x --> %08x, Start %08x --> %08x\n",
			*p_resc_num, (*p_resc_num) & ~0x7,
			*p_resc_start, (*p_resc_start) & ~0x7);
		*p_resc_num &= ~0x7;
		*p_resc_start &= ~0x7;
	}

	return ECORE_SUCCESS;
}

static enum _ecore_status_t ecore_hw_set_resc_info(struct ecore_hwfn *p_hwfn,
						   bool drv_resc_alloc)
{
	enum _ecore_status_t rc;
	u8 res_id;

	for (res_id = 0; res_id < ECORE_MAX_RESC; res_id++) {
		rc = __ecore_hw_set_resc_info(p_hwfn, res_id, drv_resc_alloc);
		if (rc != ECORE_SUCCESS)
			return rc;
	}

	return ECORE_SUCCESS;
}

static enum _ecore_status_t ecore_hw_get_ppfid_bitmap(struct ecore_hwfn *p_hwfn,
						      struct ecore_ptt *p_ptt)
{
	u8 native_ppfid_idx = ECORE_PPFID_BY_PFID(p_hwfn);
	struct ecore_dev *p_dev = p_hwfn->p_dev;
	enum _ecore_status_t rc;

	rc = ecore_mcp_get_ppfid_bitmap(p_hwfn, p_ptt);
	if (rc != ECORE_SUCCESS && rc != ECORE_NOTIMPL)
		return rc;
	else if (rc == ECORE_NOTIMPL)
		p_dev->ppfid_bitmap = 0x1 << native_ppfid_idx;

	if (!(p_dev->ppfid_bitmap & (0x1 << native_ppfid_idx))) {
		DP_INFO(p_hwfn,
			"Fix the PPFID bitmap to inculde the native PPFID [native_ppfid_idx %hhd, orig_bitmap 0x%hhx]\n",
			native_ppfid_idx, p_dev->ppfid_bitmap);
		p_dev->ppfid_bitmap = 0x1 << native_ppfid_idx;
	}

	return ECORE_SUCCESS;
}

static enum _ecore_status_t ecore_hw_get_resc(struct ecore_hwfn *p_hwfn,
					      struct ecore_ptt *p_ptt,
					      bool drv_resc_alloc)
{
	struct ecore_resc_unlock_params resc_unlock_params;
	struct ecore_resc_lock_params resc_lock_params;
	bool b_ah = ECORE_IS_AH(p_hwfn->p_dev);
	u8 res_id;
	enum _ecore_status_t rc;
#ifndef ASIC_ONLY
	u32 *resc_start = p_hwfn->hw_info.resc_start;
	u32 *resc_num = p_hwfn->hw_info.resc_num;
	/* For AH, an equal share of the ILT lines between the maximal number of
	 * PFs is not enough for RoCE. This would be solved by the future
	 * resource allocation scheme, but isn't currently present for
	 * FPGA/emulation. For now we keep a number that is sufficient for RoCE
	 * to work - the BB number of ILT lines divided by its max PFs number.
	 */
	u32 roce_min_ilt_lines = PXP_NUM_ILT_RECORDS_BB / MAX_NUM_PFS_BB;
#endif

	/* Setting the max values of the soft resources and the following
	 * resources allocation queries should be atomic. Since several PFs can
	 * run in parallel - a resource lock is needed.
	 * If either the resource lock or resource set value commands are not
	 * supported - skip the the max values setting, release the lock if
	 * needed, and proceed to the queries. Other failures, including a
	 * failure to acquire the lock, will cause this function to fail.
	 * Old drivers that don't acquire the lock can run in parallel, and
	 * their allocation values won't be affected by the updated max values.
	 */

	ecore_mcp_resc_lock_default_init(&resc_lock_params, &resc_unlock_params,
					 ECORE_RESC_LOCK_RESC_ALLOC, false);

	/* Changes on top of the default values to accommodate parallel attempts
	 * of several PFs.
	 * [10 x 10 msec by default ==> 20 x 50 msec]
	 */
	resc_lock_params.retry_num *= 2;
	resc_lock_params.retry_interval *= 5;

	rc = ecore_mcp_resc_lock(p_hwfn, p_ptt, &resc_lock_params);
	if (rc != ECORE_SUCCESS && rc != ECORE_NOTIMPL) {
		return rc;
	} else if (rc == ECORE_NOTIMPL) {
		DP_INFO(p_hwfn,
			"Skip the max values setting of the soft resources since the resource lock is not supported by the MFW\n");
	} else if (rc == ECORE_SUCCESS && !resc_lock_params.b_granted) {
		DP_NOTICE(p_hwfn, false,
			  "Failed to acquire the resource lock for the resource allocation commands\n");
		return ECORE_BUSY;
	} else {
		rc = ecore_hw_set_soft_resc_size(p_hwfn, p_ptt);
		if (rc != ECORE_SUCCESS && rc != ECORE_NOTIMPL) {
			DP_NOTICE(p_hwfn, false,
				  "Failed to set the max values of the soft resources\n");
			goto unlock_and_exit;
		} else if (rc == ECORE_NOTIMPL) {
			DP_INFO(p_hwfn,
				"Skip the max values setting of the soft resources since it is not supported by the MFW\n");
			rc = ecore_mcp_resc_unlock(p_hwfn, p_ptt,
						   &resc_unlock_params);
			if (rc != ECORE_SUCCESS)
				DP_INFO(p_hwfn,
					"Failed to release the resource lock for the resource allocation commands\n");
		}
	}

	rc = ecore_hw_set_resc_info(p_hwfn, drv_resc_alloc);
	if (rc != ECORE_SUCCESS)
		goto unlock_and_exit;

	if (resc_lock_params.b_granted && !resc_unlock_params.b_released) {
		rc = ecore_mcp_resc_unlock(p_hwfn, p_ptt,
					   &resc_unlock_params);
		if (rc != ECORE_SUCCESS)
			DP_INFO(p_hwfn,
				"Failed to release the resource lock for the resource allocation commands\n");
	}

	/* PPFID bitmap */
	if (IS_LEAD_HWFN(p_hwfn)) {
		rc = ecore_hw_get_ppfid_bitmap(p_hwfn, p_ptt);
		if (rc != ECORE_SUCCESS)
			return rc;
	}

#ifndef ASIC_ONLY
	if (CHIP_REV_IS_SLOW(p_hwfn->p_dev)) {
		/* Reduced build contains less PQs */
		if (!(p_hwfn->p_dev->b_is_emul_full)) {
			resc_num[ECORE_PQ] = 32;
			resc_start[ECORE_PQ] = resc_num[ECORE_PQ] *
					       p_hwfn->enabled_func_idx;
		}

		/* For AH emulation, since we have a possible maximal number of
		 * 16 enabled PFs, in case there are not enough ILT lines -
		 * allocate only first PF as RoCE and have all the other ETH
		 * only with less ILT lines.
		 */
		if (!p_hwfn->rel_pf_id && p_hwfn->p_dev->b_is_emul_full)
			resc_num[ECORE_ILT] = OSAL_MAX_T(u32,
							 resc_num[ECORE_ILT],
							 roce_min_ilt_lines);
	}

	/* Correct the common ILT calculation if PF0 has more */
	if (CHIP_REV_IS_SLOW(p_hwfn->p_dev) &&
	    p_hwfn->p_dev->b_is_emul_full &&
	    p_hwfn->rel_pf_id &&
	    resc_num[ECORE_ILT] < roce_min_ilt_lines)
		resc_start[ECORE_ILT] += roce_min_ilt_lines -
					 resc_num[ECORE_ILT];
#endif

	/* Sanity for ILT */
	if ((b_ah && (RESC_END(p_hwfn, ECORE_ILT) > PXP_NUM_ILT_RECORDS_K2)) ||
	    (!b_ah && (RESC_END(p_hwfn, ECORE_ILT) > PXP_NUM_ILT_RECORDS_BB))) {
		DP_NOTICE(p_hwfn, true, "Can't assign ILT pages [%08x,...,%08x]\n",
			  RESC_START(p_hwfn, ECORE_ILT),
			  RESC_END(p_hwfn, ECORE_ILT) - 1);
		return ECORE_INVAL;
	}

	/* This will also learn the number of SBs from MFW */
	if (ecore_int_igu_reset_cam(p_hwfn, p_ptt))
		return ECORE_INVAL;

	ecore_hw_set_feat(p_hwfn);

	DP_VERBOSE(p_hwfn, ECORE_MSG_PROBE,
		   "The numbers for each resource are:\n");
	for (res_id = 0; res_id < ECORE_MAX_RESC; res_id++)
		DP_VERBOSE(p_hwfn, ECORE_MSG_PROBE, "%s = %d start = %d\n",
			   ecore_hw_get_resc_name(res_id),
			   RESC_NUM(p_hwfn, res_id),
			   RESC_START(p_hwfn, res_id));

	return ECORE_SUCCESS;

unlock_and_exit:
	if (resc_lock_params.b_granted && !resc_unlock_params.b_released)
		ecore_mcp_resc_unlock(p_hwfn, p_ptt,
				      &resc_unlock_params);
	return rc;
}

static enum _ecore_status_t
ecore_hw_get_nvm_info(struct ecore_hwfn *p_hwfn,
		      struct ecore_ptt *p_ptt,
		      struct ecore_hw_prepare_params *p_params)
{
	u32 port_cfg_addr, link_temp, nvm_cfg_addr, device_capabilities;
	u32 nvm_cfg1_offset, mf_mode, addr, generic_cont0, core_cfg;
	struct ecore_mcp_link_capabilities *p_caps;
	struct ecore_mcp_link_params *link;
	enum _ecore_status_t rc;
	u32 dcbx_mode;  /* __LINUX__THROW__ */

	/* Read global nvm_cfg address */
	nvm_cfg_addr = ecore_rd(p_hwfn, p_ptt, MISC_REG_GEN_PURP_CR0);

	/* Verify MCP has initialized it */
	if (!nvm_cfg_addr) {
		DP_NOTICE(p_hwfn, false, "Shared memory not initialized\n");
		if (p_params->b_relaxed_probe)
			p_params->p_relaxed_res = ECORE_HW_PREPARE_FAILED_NVM;
		return ECORE_INVAL;
	}

	/* Read nvm_cfg1  (Notice this is just offset, and not offsize (TBD) */
	nvm_cfg1_offset = ecore_rd(p_hwfn, p_ptt, nvm_cfg_addr + 4);

	addr = MCP_REG_SCRATCH  + nvm_cfg1_offset +
		   OFFSETOF(struct nvm_cfg1, glob) +
		   OFFSETOF(struct nvm_cfg1_glob, core_cfg);

	core_cfg = ecore_rd(p_hwfn, p_ptt, addr);

	switch ((core_cfg & NVM_CFG1_GLOB_NETWORK_PORT_MODE_MASK) >>
		NVM_CFG1_GLOB_NETWORK_PORT_MODE_OFFSET) {
	case NVM_CFG1_GLOB_NETWORK_PORT_MODE_BB_2X40G:
		p_hwfn->hw_info.port_mode = ECORE_PORT_MODE_DE_2X40G;
		break;
	case NVM_CFG1_GLOB_NETWORK_PORT_MODE_2X50G:
		p_hwfn->hw_info.port_mode = ECORE_PORT_MODE_DE_2X50G;
		break;
	case NVM_CFG1_GLOB_NETWORK_PORT_MODE_BB_1X100G:
		p_hwfn->hw_info.port_mode = ECORE_PORT_MODE_DE_1X100G;
		break;
	case NVM_CFG1_GLOB_NETWORK_PORT_MODE_4X10G_F:
		p_hwfn->hw_info.port_mode = ECORE_PORT_MODE_DE_4X10G_F;
		break;
	case NVM_CFG1_GLOB_NETWORK_PORT_MODE_BB_4X10G_E:
		p_hwfn->hw_info.port_mode = ECORE_PORT_MODE_DE_4X10G_E;
		break;
	case NVM_CFG1_GLOB_NETWORK_PORT_MODE_BB_4X20G:
		p_hwfn->hw_info.port_mode = ECORE_PORT_MODE_DE_4X20G;
		break;
	case NVM_CFG1_GLOB_NETWORK_PORT_MODE_1X40G:
		p_hwfn->hw_info.port_mode = ECORE_PORT_MODE_DE_1X40G;
		break;
	case NVM_CFG1_GLOB_NETWORK_PORT_MODE_2X25G:
		p_hwfn->hw_info.port_mode = ECORE_PORT_MODE_DE_2X25G;
		break;
	case NVM_CFG1_GLOB_NETWORK_PORT_MODE_2X10G:
		p_hwfn->hw_info.port_mode = ECORE_PORT_MODE_DE_2X10G;
		break;
	case NVM_CFG1_GLOB_NETWORK_PORT_MODE_1X25G:
		p_hwfn->hw_info.port_mode = ECORE_PORT_MODE_DE_1X25G;
		break;
	case NVM_CFG1_GLOB_NETWORK_PORT_MODE_4X25G:
		p_hwfn->hw_info.port_mode = ECORE_PORT_MODE_DE_4X25G;
		break;
	default:
		DP_NOTICE(p_hwfn, true, "Unknown port mode in 0x%08x\n",
			  core_cfg);
		break;
	}

#ifndef __EXTRACT__LINUX__THROW__
	/* Read DCBX configuration */
	port_cfg_addr = MCP_REG_SCRATCH + nvm_cfg1_offset +
			OFFSETOF(struct nvm_cfg1, port[MFW_PORT(p_hwfn)]);
	dcbx_mode = ecore_rd(p_hwfn, p_ptt,
			     port_cfg_addr +
			     OFFSETOF(struct nvm_cfg1_port, generic_cont0));
	dcbx_mode = (dcbx_mode & NVM_CFG1_PORT_DCBX_MODE_MASK)
		>> NVM_CFG1_PORT_DCBX_MODE_OFFSET;
	switch (dcbx_mode) {
	case NVM_CFG1_PORT_DCBX_MODE_DYNAMIC:
		p_hwfn->hw_info.dcbx_mode = ECORE_DCBX_VERSION_DYNAMIC;
		break;
	case NVM_CFG1_PORT_DCBX_MODE_CEE:
		p_hwfn->hw_info.dcbx_mode = ECORE_DCBX_VERSION_CEE;
		break;
	case NVM_CFG1_PORT_DCBX_MODE_IEEE:
		p_hwfn->hw_info.dcbx_mode = ECORE_DCBX_VERSION_IEEE;
		break;
	default:
		p_hwfn->hw_info.dcbx_mode = ECORE_DCBX_VERSION_DISABLED;
	}
#endif

	/* Read default link configuration */
	link = &p_hwfn->mcp_info->link_input;
	p_caps = &p_hwfn->mcp_info->link_capabilities;
	port_cfg_addr = MCP_REG_SCRATCH + nvm_cfg1_offset +
			OFFSETOF(struct nvm_cfg1, port[MFW_PORT(p_hwfn)]);
	link_temp = ecore_rd(p_hwfn, p_ptt,
			     port_cfg_addr +
			     OFFSETOF(struct nvm_cfg1_port, speed_cap_mask));
	link_temp &= NVM_CFG1_PORT_DRV_SPEED_CAPABILITY_MASK_MASK;
	link->speed.advertised_speeds = link_temp;
	p_caps->speed_capabilities = link->speed.advertised_speeds;

	link_temp = ecore_rd(p_hwfn, p_ptt,
				 port_cfg_addr +
				 OFFSETOF(struct nvm_cfg1_port, link_settings));
	switch ((link_temp & NVM_CFG1_PORT_DRV_LINK_SPEED_MASK) >>
		NVM_CFG1_PORT_DRV_LINK_SPEED_OFFSET) {
	case NVM_CFG1_PORT_DRV_LINK_SPEED_AUTONEG:
		link->speed.autoneg = true;
		break;
	case NVM_CFG1_PORT_DRV_LINK_SPEED_1G:
		link->speed.forced_speed = 1000;
		break;
	case NVM_CFG1_PORT_DRV_LINK_SPEED_10G:
		link->speed.forced_speed = 10000;
		break;
	case NVM_CFG1_PORT_DRV_LINK_SPEED_20G:
		link->speed.forced_speed = 20000;
		break;
	case NVM_CFG1_PORT_DRV_LINK_SPEED_25G:
		link->speed.forced_speed = 25000;
		break;
	case NVM_CFG1_PORT_DRV_LINK_SPEED_40G:
		link->speed.forced_speed = 40000;
		break;
	case NVM_CFG1_PORT_DRV_LINK_SPEED_50G:
		link->speed.forced_speed = 50000;
		break;
	case NVM_CFG1_PORT_DRV_LINK_SPEED_BB_100G:
		link->speed.forced_speed = 100000;
		break;
	default:
		DP_NOTICE(p_hwfn, true, "Unknown Speed in 0x%08x\n",
			  link_temp);
	}

	p_caps->default_speed = link->speed.forced_speed; /* __LINUX__THROW__ */
	p_caps->default_speed_autoneg = link->speed.autoneg;

	link_temp &= NVM_CFG1_PORT_DRV_FLOW_CONTROL_MASK;
	link_temp >>= NVM_CFG1_PORT_DRV_FLOW_CONTROL_OFFSET;
	link->pause.autoneg = !!(link_temp &
				 NVM_CFG1_PORT_DRV_FLOW_CONTROL_AUTONEG);
	link->pause.forced_rx = !!(link_temp &
				   NVM_CFG1_PORT_DRV_FLOW_CONTROL_RX);
	link->pause.forced_tx = !!(link_temp &
				   NVM_CFG1_PORT_DRV_FLOW_CONTROL_TX);
	link->loopback_mode = 0;

	if (p_hwfn->mcp_info->capabilities & FW_MB_PARAM_FEATURE_SUPPORT_EEE) {
		link_temp = ecore_rd(p_hwfn, p_ptt, port_cfg_addr +
				     OFFSETOF(struct nvm_cfg1_port, ext_phy));
		link_temp &= NVM_CFG1_PORT_EEE_POWER_SAVING_MODE_MASK;
		link_temp >>= NVM_CFG1_PORT_EEE_POWER_SAVING_MODE_OFFSET;
		p_caps->default_eee = ECORE_MCP_EEE_ENABLED;
		link->eee.enable = true;
		switch (link_temp) {
		case NVM_CFG1_PORT_EEE_POWER_SAVING_MODE_DISABLED:
			p_caps->default_eee = ECORE_MCP_EEE_DISABLED;
			link->eee.enable = false;
			break;
		case NVM_CFG1_PORT_EEE_POWER_SAVING_MODE_BALANCED:
			p_caps->eee_lpi_timer = EEE_TX_TIMER_USEC_BALANCED_TIME;
			break;
		case NVM_CFG1_PORT_EEE_POWER_SAVING_MODE_AGGRESSIVE:
			p_caps->eee_lpi_timer =
				EEE_TX_TIMER_USEC_AGGRESSIVE_TIME;
			break;
		case NVM_CFG1_PORT_EEE_POWER_SAVING_MODE_LOW_LATENCY:
			p_caps->eee_lpi_timer = EEE_TX_TIMER_USEC_LATENCY_TIME;
			break;
		}

		link->eee.tx_lpi_timer = p_caps->eee_lpi_timer;
		link->eee.tx_lpi_enable = link->eee.enable;
		link->eee.adv_caps = ECORE_EEE_1G_ADV | ECORE_EEE_10G_ADV;
	} else {
		p_caps->default_eee = ECORE_MCP_EEE_UNSUPPORTED;
	}

	DP_VERBOSE(p_hwfn, ECORE_MSG_LINK,
		   "Read default link: Speed 0x%08x, Adv. Speed 0x%08x, AN: 0x%02x, PAUSE AN: 0x%02x EEE: %02x [%08x usec]\n",
		   link->speed.forced_speed, link->speed.advertised_speeds,
		   link->speed.autoneg, link->pause.autoneg,
		   p_caps->default_eee, p_caps->eee_lpi_timer);

	/* Read Multi-function information from shmem */
	addr = MCP_REG_SCRATCH + nvm_cfg1_offset +
		   OFFSETOF(struct nvm_cfg1, glob) +
		   OFFSETOF(struct nvm_cfg1_glob, generic_cont0);

	generic_cont0 = ecore_rd(p_hwfn, p_ptt, addr);

	mf_mode = (generic_cont0 & NVM_CFG1_GLOB_MF_MODE_MASK) >>
		  NVM_CFG1_GLOB_MF_MODE_OFFSET;

	switch (mf_mode) {
	case NVM_CFG1_GLOB_MF_MODE_MF_ALLOWED:
		p_hwfn->p_dev->mf_bits = 1 << ECORE_MF_OVLAN_CLSS;
		break;
	case NVM_CFG1_GLOB_MF_MODE_UFP:
		p_hwfn->p_dev->mf_bits = 1 << ECORE_MF_OVLAN_CLSS |
					 1 << ECORE_MF_LLH_PROTO_CLSS |
					 1 << ECORE_MF_UFP_SPECIFIC |
					 1 << ECORE_MF_8021Q_TAGGING;
		break;
	case NVM_CFG1_GLOB_MF_MODE_BD:
		p_hwfn->p_dev->mf_bits = 1 << ECORE_MF_OVLAN_CLSS |
					 1 << ECORE_MF_LLH_PROTO_CLSS |
					 1 << ECORE_MF_8021AD_TAGGING;
		break;
	case NVM_CFG1_GLOB_MF_MODE_NPAR1_0:
		p_hwfn->p_dev->mf_bits = 1 << ECORE_MF_LLH_MAC_CLSS |
					 1 << ECORE_MF_LLH_PROTO_CLSS |
					 1 << ECORE_MF_LL2_NON_UNICAST |
					 1 << ECORE_MF_INTER_PF_SWITCH |
					 1 << ECORE_MF_DISABLE_ARFS;
		break;
	case NVM_CFG1_GLOB_MF_MODE_DEFAULT:
		p_hwfn->p_dev->mf_bits = 1 << ECORE_MF_LLH_MAC_CLSS |
					 1 << ECORE_MF_LLH_PROTO_CLSS |
					 1 << ECORE_MF_LL2_NON_UNICAST;
		if (ECORE_IS_BB(p_hwfn->p_dev))
			p_hwfn->p_dev->mf_bits |= 1 << ECORE_MF_NEED_DEF_PF;
		break;
	}
	DP_INFO(p_hwfn, "Multi function mode is 0x%lx\n",
		p_hwfn->p_dev->mf_bits);

	if (ECORE_IS_CMT(p_hwfn->p_dev))
		p_hwfn->p_dev->mf_bits |= (1 << ECORE_MF_DISABLE_ARFS);

#ifndef __EXTRACT__LINUX__THROW__
	/* It's funny since we have another switch, but it's easier
	 * to throw this away in linux this way. Long term, it might be
	 * better to have have getters for needed ECORE_MF_* fields,
	 * convert client code and eliminate this.
	 */
	switch (mf_mode) {
	case NVM_CFG1_GLOB_MF_MODE_MF_ALLOWED:
		p_hwfn->p_dev->mf_mode = ECORE_MF_OVLAN;
		break;
	case NVM_CFG1_GLOB_MF_MODE_NPAR1_0:
		p_hwfn->p_dev->mf_mode = ECORE_MF_NPAR;
		break;
	case NVM_CFG1_GLOB_MF_MODE_DEFAULT:
		p_hwfn->p_dev->mf_mode = ECORE_MF_DEFAULT;
		break;
	case NVM_CFG1_GLOB_MF_MODE_UFP:
		p_hwfn->p_dev->mf_mode = ECORE_MF_UFP;
		break;
	}
#endif

	/* Read Multi-function information from shmem */
	addr = MCP_REG_SCRATCH + nvm_cfg1_offset +
		   OFFSETOF(struct nvm_cfg1, glob) +
		   OFFSETOF(struct nvm_cfg1_glob, device_capabilities);

	device_capabilities = ecore_rd(p_hwfn, p_ptt, addr);
	if (device_capabilities & NVM_CFG1_GLOB_DEVICE_CAPABILITIES_ETHERNET)
		OSAL_SET_BIT(ECORE_DEV_CAP_ETH,
				 &p_hwfn->hw_info.device_capabilities);
	if (device_capabilities & NVM_CFG1_GLOB_DEVICE_CAPABILITIES_FCOE)
		OSAL_SET_BIT(ECORE_DEV_CAP_FCOE,
				 &p_hwfn->hw_info.device_capabilities);
	if (device_capabilities & NVM_CFG1_GLOB_DEVICE_CAPABILITIES_ISCSI)
		OSAL_SET_BIT(ECORE_DEV_CAP_ISCSI,
				 &p_hwfn->hw_info.device_capabilities);
	if (device_capabilities & NVM_CFG1_GLOB_DEVICE_CAPABILITIES_ROCE)
		OSAL_SET_BIT(ECORE_DEV_CAP_ROCE,
				 &p_hwfn->hw_info.device_capabilities);
	if (device_capabilities & NVM_CFG1_GLOB_DEVICE_CAPABILITIES_IWARP)
		OSAL_SET_BIT(ECORE_DEV_CAP_IWARP,
				 &p_hwfn->hw_info.device_capabilities);

	rc = ecore_mcp_fill_shmem_func_info(p_hwfn, p_ptt);
	if (rc != ECORE_SUCCESS && p_params->b_relaxed_probe) {
		rc = ECORE_SUCCESS;
		p_params->p_relaxed_res = ECORE_HW_PREPARE_BAD_MCP;
	}

	return rc;
}

static void ecore_get_num_funcs(struct ecore_hwfn *p_hwfn,
				struct ecore_ptt *p_ptt)
{
	u8 num_funcs, enabled_func_idx = p_hwfn->rel_pf_id;
	u32 reg_function_hide, tmp, eng_mask, low_pfs_mask;
	struct ecore_dev *p_dev = p_hwfn->p_dev;

	num_funcs = ECORE_IS_AH(p_dev) ? MAX_NUM_PFS_K2 : MAX_NUM_PFS_BB;

	/* Bit 0 of MISCS_REG_FUNCTION_HIDE indicates whether the bypass values
	 * in the other bits are selected.
	 * Bits 1-15 are for functions 1-15, respectively, and their value is
	 * '0' only for enabled functions (function 0 always exists and
	 * enabled).
	 * In case of CMT in BB, only the "even" functions are enabled, and thus
	 * the number of functions for both hwfns is learnt from the same bits.
	 */
	reg_function_hide = ecore_rd(p_hwfn, p_ptt, MISCS_REG_FUNCTION_HIDE);

	if (reg_function_hide & 0x1) {
		if (ECORE_IS_BB(p_dev)) {
			if (ECORE_PATH_ID(p_hwfn) && !ECORE_IS_CMT(p_dev)) {
				num_funcs = 0;
				eng_mask = 0xaaaa;
			} else {
				num_funcs = 1;
				eng_mask = 0x5554;
			}
		} else {
			num_funcs = 1;
			eng_mask = 0xfffe;
		}

		/* Get the number of the enabled functions on the engine */
		tmp = (reg_function_hide ^ 0xffffffff) & eng_mask;
		while (tmp) {
			if (tmp & 0x1)
				num_funcs++;
			tmp >>= 0x1;
		}

		/* Get the PF index within the enabled functions */
		low_pfs_mask = (0x1 << p_hwfn->abs_pf_id) - 1;
		tmp = reg_function_hide & eng_mask & low_pfs_mask;
		while (tmp) {
			if (tmp & 0x1)
				enabled_func_idx--;
			tmp >>= 0x1;
		}
	}

	p_hwfn->num_funcs_on_engine = num_funcs;
	p_hwfn->enabled_func_idx = enabled_func_idx;

#ifndef ASIC_ONLY
	if (CHIP_REV_IS_FPGA(p_dev)) {
		DP_NOTICE(p_hwfn, false,
			  "FPGA: Limit number of PFs to 4 [would affect resource allocation, needed for IOV]\n");
		p_hwfn->num_funcs_on_engine = 4;
	}
#endif

	DP_VERBOSE(p_hwfn, ECORE_MSG_PROBE,
		   "PF [rel_id %d, abs_id %d] occupies index %d within the %d enabled functions on the engine\n",
		   p_hwfn->rel_pf_id, p_hwfn->abs_pf_id,
		   p_hwfn->enabled_func_idx, p_hwfn->num_funcs_on_engine);
}

static void ecore_hw_info_port_num_bb(struct ecore_hwfn *p_hwfn,
				      struct ecore_ptt *p_ptt)
{
	struct ecore_dev *p_dev = p_hwfn->p_dev;
	u32 port_mode;

#ifndef ASIC_ONLY
	/* Read the port mode */
	if (CHIP_REV_IS_FPGA(p_dev))
		port_mode = 4;
	else if (CHIP_REV_IS_EMUL(p_dev) && ECORE_IS_CMT(p_dev))
		/* In CMT on emulation, assume 1 port */
		port_mode = 1;
	else
#endif
	port_mode = ecore_rd(p_hwfn, p_ptt, CNIG_REG_NW_PORT_MODE_BB);

	if (port_mode < 3) {
		p_dev->num_ports_in_engine = 1;
	} else if (port_mode <= 5) {
		p_dev->num_ports_in_engine = 2;
	} else {
		DP_NOTICE(p_hwfn, true, "PORT MODE: %d not supported\n",
			  p_dev->num_ports_in_engine);

		/* Default num_ports_in_engine to something */
		p_dev->num_ports_in_engine = 1;
	}
}

static void ecore_hw_info_port_num_ah_e5(struct ecore_hwfn *p_hwfn,
					 struct ecore_ptt *p_ptt)
{
	struct ecore_dev *p_dev = p_hwfn->p_dev;
	u32 port;
	int i;

	p_dev->num_ports_in_engine = 0;

#ifndef ASIC_ONLY
	if (CHIP_REV_IS_EMUL(p_dev)) {
		port = ecore_rd(p_hwfn, p_ptt, MISCS_REG_ECO_RESERVED);
		switch ((port & 0xf000) >> 12) {
		case 1:
			p_dev->num_ports_in_engine = 1;
			break;
		case 3:
			p_dev->num_ports_in_engine = 2;
			break;
		case 0xf:
			p_dev->num_ports_in_engine = 4;
			break;
		default:
			DP_NOTICE(p_hwfn, false,
				  "Unknown port mode in ECO_RESERVED %08x\n",
				  port);
		}
	} else
#endif
	for (i = 0; i < MAX_NUM_PORTS_K2; i++) {
		port = ecore_rd(p_hwfn, p_ptt,
				CNIG_REG_NIG_PORT0_CONF_K2_E5 + (i * 4));
		if (port & 1)
			p_dev->num_ports_in_engine++;
	}

	if (!p_dev->num_ports_in_engine) {
		DP_NOTICE(p_hwfn, true, "All NIG ports are inactive\n");

		/* Default num_ports_in_engine to something */
		p_dev->num_ports_in_engine = 1;
	}
}

static void ecore_hw_info_port_num(struct ecore_hwfn *p_hwfn,
				   struct ecore_ptt *p_ptt)
{
	struct ecore_dev *p_dev = p_hwfn->p_dev;

	/* Determine the number of ports per engine */
	if (ECORE_IS_BB(p_dev))
		ecore_hw_info_port_num_bb(p_hwfn, p_ptt);
	else
		ecore_hw_info_port_num_ah_e5(p_hwfn, p_ptt);

	/* Get the total number of ports of the device */
	if (ECORE_IS_CMT(p_dev)) {
		/* In CMT there is always only one port */
		p_dev->num_ports = 1;
#ifndef ASIC_ONLY
	} else if (CHIP_REV_IS_EMUL(p_dev) || CHIP_REV_IS_TEDIBEAR(p_dev)) {
		p_dev->num_ports = p_dev->num_ports_in_engine *
				   ecore_device_num_engines(p_dev);
#endif
	} else {
		u32 addr, global_offsize, global_addr;

		addr = SECTION_OFFSIZE_ADDR(p_hwfn->mcp_info->public_base,
					    PUBLIC_GLOBAL);
		global_offsize = ecore_rd(p_hwfn, p_ptt, addr);
		global_addr = SECTION_ADDR(global_offsize, 0);
		addr = global_addr + OFFSETOF(struct public_global, max_ports);
		p_dev->num_ports = (u8)ecore_rd(p_hwfn, p_ptt, addr);
	}
}

static void ecore_mcp_get_eee_caps(struct ecore_hwfn *p_hwfn,
				   struct ecore_ptt *p_ptt)
{
	struct ecore_mcp_link_capabilities *p_caps;
	u32 eee_status;

	p_caps = &p_hwfn->mcp_info->link_capabilities;
	if (p_caps->default_eee == ECORE_MCP_EEE_UNSUPPORTED)
		return;

	p_caps->eee_speed_caps = 0;
	eee_status = ecore_rd(p_hwfn, p_ptt, p_hwfn->mcp_info->port_addr +
			      OFFSETOF(struct public_port, eee_status));
	eee_status = (eee_status & EEE_SUPPORTED_SPEED_MASK) >>
			EEE_SUPPORTED_SPEED_OFFSET;
	if (eee_status & EEE_1G_SUPPORTED)
		p_caps->eee_speed_caps |= ECORE_EEE_1G_ADV;
	if (eee_status & EEE_10G_ADV)
		p_caps->eee_speed_caps |= ECORE_EEE_10G_ADV;
}

static enum _ecore_status_t
ecore_get_hw_info(struct ecore_hwfn *p_hwfn, struct ecore_ptt *p_ptt,
		  enum ecore_pci_personality personality,
		  struct ecore_hw_prepare_params *p_params)
{
	bool drv_resc_alloc = p_params->drv_resc_alloc;
	enum _ecore_status_t rc;

	/* Since all information is common, only first hwfns should do this */
	if (IS_LEAD_HWFN(p_hwfn)) {
		rc = ecore_iov_hw_info(p_hwfn);
		if (rc != ECORE_SUCCESS) {
			if (p_params->b_relaxed_probe)
				p_params->p_relaxed_res =
						ECORE_HW_PREPARE_BAD_IOV;
			else
				return rc;
		}
	}

	if (IS_LEAD_HWFN(p_hwfn))
		ecore_hw_info_port_num(p_hwfn, p_ptt);

	ecore_mcp_get_capabilities(p_hwfn, p_ptt);

#ifndef ASIC_ONLY
	if (CHIP_REV_IS_ASIC(p_hwfn->p_dev)) {
#endif
	rc = ecore_hw_get_nvm_info(p_hwfn, p_ptt, p_params);
	if (rc != ECORE_SUCCESS)
		return rc;
#ifndef ASIC_ONLY
	}
#endif

	rc = ecore_int_igu_read_cam(p_hwfn, p_ptt);
	if (rc != ECORE_SUCCESS) {
		if (p_params->b_relaxed_probe)
			p_params->p_relaxed_res = ECORE_HW_PREPARE_BAD_IGU;
		else
			return rc;
	}

#ifndef ASIC_ONLY
	if (CHIP_REV_IS_ASIC(p_hwfn->p_dev) && ecore_mcp_is_init(p_hwfn)) {
#endif
	OSAL_MEMCPY(p_hwfn->hw_info.hw_mac_addr,
		    p_hwfn->mcp_info->func_info.mac, ETH_ALEN);
#ifndef ASIC_ONLY
	} else {
		static u8 mcp_hw_mac[6] = {0, 2, 3, 4, 5, 6};

		OSAL_MEMCPY(p_hwfn->hw_info.hw_mac_addr, mcp_hw_mac, ETH_ALEN);
		p_hwfn->hw_info.hw_mac_addr[5] = p_hwfn->abs_pf_id;
	}
#endif

	if (ecore_mcp_is_init(p_hwfn)) {
		if (p_hwfn->mcp_info->func_info.ovlan != ECORE_MCP_VLAN_UNSET)
			p_hwfn->hw_info.ovlan =
				p_hwfn->mcp_info->func_info.ovlan;

		ecore_mcp_cmd_port_init(p_hwfn, p_ptt);

		ecore_mcp_get_eee_caps(p_hwfn, p_ptt);

		ecore_mcp_read_ufp_config(p_hwfn, p_ptt);
	}

	if (personality != ECORE_PCI_DEFAULT) {
		p_hwfn->hw_info.personality = personality;
	} else if (ecore_mcp_is_init(p_hwfn)) {
		enum ecore_pci_personality protocol;

		protocol = p_hwfn->mcp_info->func_info.protocol;
		p_hwfn->hw_info.personality = protocol;
	}

#ifndef ASIC_ONLY
	/* To overcome ILT lack for emulation, until at least until we'll have
	 * a definite answer from system about it, allow only PF0 to be RoCE.
	 */
	if (CHIP_REV_IS_EMUL(p_hwfn->p_dev) && ECORE_IS_AH(p_hwfn->p_dev)) {
		if (!p_hwfn->rel_pf_id)
			p_hwfn->hw_info.personality = ECORE_PCI_ETH_ROCE;
		else
			p_hwfn->hw_info.personality = ECORE_PCI_ETH;
	}
#endif

	/* although in BB some constellations may support more than 4 tcs,
	 * that can result in performance penalty in some cases. 4
	 * represents a good tradeoff between performance and flexibility.
	 */
	p_hwfn->hw_info.num_hw_tc = NUM_PHYS_TCS_4PORT_K2;

	/* start out with a single active tc. This can be increased either
	 * by dcbx negotiation or by upper layer driver
	 */
	p_hwfn->hw_info.num_active_tc = 1;

	ecore_get_num_funcs(p_hwfn, p_ptt);

	if (ecore_mcp_is_init(p_hwfn))
		p_hwfn->hw_info.mtu = p_hwfn->mcp_info->func_info.mtu;

	/* In case of forcing the driver's default resource allocation, calling
	 * ecore_hw_get_resc() should come after initializing the personality
	 * and after getting the number of functions, since the calculation of
	 * the resources/features depends on them.
	 * This order is not harmful if not forcing.
	 */
	rc = ecore_hw_get_resc(p_hwfn, p_ptt, drv_resc_alloc);
	if (rc != ECORE_SUCCESS && p_params->b_relaxed_probe) {
		rc = ECORE_SUCCESS;
		p_params->p_relaxed_res = ECORE_HW_PREPARE_BAD_MCP;
	}

	return rc;
}

#define ECORE_MAX_DEVICE_NAME_LEN	(8)

void ecore_get_dev_name(struct ecore_dev *p_dev, u8 *name, u8 max_chars)
{
	u8 n;

	n = OSAL_MIN_T(u8, max_chars, ECORE_MAX_DEVICE_NAME_LEN);
	OSAL_SNPRINTF(name, n, "%s %c%d", ECORE_IS_BB(p_dev) ? "BB" : "AH",
		      'A' + p_dev->chip_rev, (int)p_dev->chip_metal);
}

static enum _ecore_status_t ecore_get_dev_info(struct ecore_hwfn *p_hwfn,
					       struct ecore_ptt *p_ptt)
{
	struct ecore_dev *p_dev = p_hwfn->p_dev;
	u16 device_id_mask;
	u32 tmp;

	/* Read Vendor Id / Device Id */
	OSAL_PCI_READ_CONFIG_WORD(p_dev, PCICFG_VENDOR_ID_OFFSET,
				  &p_dev->vendor_id);
	OSAL_PCI_READ_CONFIG_WORD(p_dev, PCICFG_DEVICE_ID_OFFSET,
				  &p_dev->device_id);

	/* Determine type */
	device_id_mask = p_dev->device_id & ECORE_DEV_ID_MASK;
	switch (device_id_mask) {
	case ECORE_DEV_ID_MASK_BB:
		p_dev->type = ECORE_DEV_TYPE_BB;
		break;
	case ECORE_DEV_ID_MASK_AH:
		p_dev->type = ECORE_DEV_TYPE_AH;
		break;
	case ECORE_DEV_ID_MASK_E5:
		p_dev->type = ECORE_DEV_TYPE_E5;
		break;
	default:
		DP_NOTICE(p_hwfn, true, "Unknown device id 0x%x\n",
			  p_dev->device_id);
		return ECORE_ABORTED;
	}

	tmp = ecore_rd(p_hwfn, p_ptt, MISCS_REG_CHIP_NUM);
	p_dev->chip_num = (u16)GET_FIELD(tmp, CHIP_NUM);
	tmp = ecore_rd(p_hwfn, p_ptt, MISCS_REG_CHIP_REV);
	p_dev->chip_rev = (u8)GET_FIELD(tmp, CHIP_REV);

	/* Learn number of HW-functions */
	tmp = ecore_rd(p_hwfn, p_ptt, MISCS_REG_CMT_ENABLED_FOR_PAIR);

	if (tmp & (1 << p_hwfn->rel_pf_id)) {
		DP_NOTICE(p_dev->hwfns, false, "device in CMT mode\n");
		p_dev->num_hwfns = 2;
	} else {
		p_dev->num_hwfns = 1;
	}

#ifndef ASIC_ONLY
	if (CHIP_REV_IS_EMUL(p_dev)) {
		/* For some reason we have problems with this register
		 * in B0 emulation; Simply assume no CMT
		 */
		DP_NOTICE(p_dev->hwfns, false, "device on emul - assume no CMT\n");
		p_dev->num_hwfns = 1;
	}
#endif

	tmp = ecore_rd(p_hwfn, p_ptt, MISCS_REG_CHIP_TEST_REG);
	p_dev->chip_bond_id = (u8)GET_FIELD(tmp, CHIP_BOND_ID);
	tmp = ecore_rd(p_hwfn, p_ptt, MISCS_REG_CHIP_METAL);
	p_dev->chip_metal = (u8)GET_FIELD(tmp, CHIP_METAL);

	DP_INFO(p_dev->hwfns,
		"Chip details - %s %c%d, Num: %04x Rev: %02x Bond id: %02x Metal: %02x\n",
		ECORE_IS_BB(p_dev) ? "BB" : "AH",
		'A' + p_dev->chip_rev, (int)p_dev->chip_metal,
		p_dev->chip_num, p_dev->chip_rev, p_dev->chip_bond_id,
		p_dev->chip_metal);

	if (ECORE_IS_BB_A0(p_dev)) {
		DP_NOTICE(p_dev->hwfns, false,
			  "The chip type/rev (BB A0) is not supported!\n");
		return ECORE_ABORTED;
	}

#ifndef ASIC_ONLY
	if (CHIP_REV_IS_EMUL(p_dev) && ECORE_IS_AH(p_dev))
		ecore_wr(p_hwfn, p_ptt, MISCS_REG_PLL_MAIN_CTRL_4, 0x1);

	if (CHIP_REV_IS_EMUL(p_dev)) {
		tmp = ecore_rd(p_hwfn, p_ptt, MISCS_REG_ECO_RESERVED);
		if (tmp & (1 << 29)) {
			DP_NOTICE(p_hwfn, false, "Emulation: Running on a FULL build\n");
			p_dev->b_is_emul_full = true;
		} else {
			DP_NOTICE(p_hwfn, false, "Emulation: Running on a REDUCED build\n");
		}
	}
#endif

	return ECORE_SUCCESS;
}

#ifndef LINUX_REMOVE
void ecore_hw_hibernate_prepare(struct ecore_dev *p_dev)
{
	int j;

	if (IS_VF(p_dev))
		return;

	for_each_hwfn(p_dev, j) {
		struct ecore_hwfn *p_hwfn = &p_dev->hwfns[j];

		DP_VERBOSE(p_hwfn, ECORE_MSG_IFDOWN, "Mark hw/fw uninitialized\n");

		p_hwfn->hw_init_done = false;

		ecore_ptt_invalidate(p_hwfn);
	}
}

void ecore_hw_hibernate_resume(struct ecore_dev *p_dev)
{
	int j = 0;

	if (IS_VF(p_dev))
		return;

	for_each_hwfn(p_dev, j) {
		struct ecore_hwfn *p_hwfn = &p_dev->hwfns[j];
		struct ecore_ptt *p_ptt = ecore_ptt_acquire(p_hwfn);

		ecore_hw_hwfn_prepare(p_hwfn);

		if (!p_ptt)
			DP_NOTICE(p_hwfn, false, "ptt acquire failed\n");
		else {
			ecore_load_mcp_offsets(p_hwfn, p_ptt);
			ecore_ptt_release(p_hwfn, p_ptt);
		}
		DP_VERBOSE(p_hwfn, ECORE_MSG_IFUP, "Reinitialized hw after low power state\n");
	}
}

#endif

static enum _ecore_status_t
ecore_hw_prepare_single(struct ecore_hwfn *p_hwfn, void OSAL_IOMEM *p_regview,
			void OSAL_IOMEM *p_doorbells, u64 db_phys_addr,
			struct ecore_hw_prepare_params *p_params)
{
	struct ecore_mdump_retain_data mdump_retain;
	struct ecore_dev *p_dev = p_hwfn->p_dev;
	struct ecore_mdump_info mdump_info;
	enum _ecore_status_t rc = ECORE_SUCCESS;

	/* Split PCI bars evenly between hwfns */
	p_hwfn->regview = p_regview;
	p_hwfn->doorbells = p_doorbells;
	p_hwfn->db_phys_addr = db_phys_addr;

#ifndef LINUX_REMOVE
       p_hwfn->reg_offset = (u8 *)p_hwfn->regview - (u8 *)p_hwfn->p_dev->regview;
       p_hwfn->db_offset = (u8 *)p_hwfn->doorbells - (u8 *)p_hwfn->p_dev->doorbells;
#endif

	if (IS_VF(p_dev))
		return ecore_vf_hw_prepare(p_hwfn);

	/* Validate that chip access is feasible */
	if (REG_RD(p_hwfn, PXP_PF_ME_OPAQUE_ADDR) == 0xffffffff) {
		DP_ERR(p_hwfn, "Reading the ME register returns all Fs; Preventing further chip access\n");
		if (p_params->b_relaxed_probe)
			p_params->p_relaxed_res = ECORE_HW_PREPARE_FAILED_ME;
		return ECORE_INVAL;
	}

	get_function_id(p_hwfn);

	/* Allocate PTT pool */
	rc = ecore_ptt_pool_alloc(p_hwfn);
	if (rc) {
		DP_NOTICE(p_hwfn, false, "Failed to prepare hwfn's hw\n");
		if (p_params->b_relaxed_probe)
			p_params->p_relaxed_res = ECORE_HW_PREPARE_FAILED_MEM;
		goto err0;
	}

	/* Allocate the main PTT */
	p_hwfn->p_main_ptt = ecore_get_reserved_ptt(p_hwfn, RESERVED_PTT_MAIN);

	/* First hwfn learns basic information, e.g., number of hwfns */
	if (!p_hwfn->my_id) {
		rc = ecore_get_dev_info(p_hwfn, p_hwfn->p_main_ptt);
		if (rc != ECORE_SUCCESS) {
			if (p_params->b_relaxed_probe)
				p_params->p_relaxed_res =
					ECORE_HW_PREPARE_FAILED_DEV;
			goto err1;
		}
	}

	ecore_hw_hwfn_prepare(p_hwfn);

	/* Initialize MCP structure */
	rc = ecore_mcp_cmd_init(p_hwfn, p_hwfn->p_main_ptt);
	if (rc) {
		DP_NOTICE(p_hwfn, false, "Failed initializing mcp command\n");
		if (p_params->b_relaxed_probe)
			p_params->p_relaxed_res = ECORE_HW_PREPARE_FAILED_MEM;
		goto err1;
	}

	/* Read the device configuration information from the HW and SHMEM */
	rc = ecore_get_hw_info(p_hwfn, p_hwfn->p_main_ptt,
			       p_params->personality, p_params);
	if (rc) {
		DP_NOTICE(p_hwfn, false, "Failed to get HW information\n");
		goto err2;
	}

	/* Sending a mailbox to the MFW should be after ecore_get_hw_info() is
	 * called, since among others it sets the ports number in an engine.
	 */
	if (p_params->initiate_pf_flr && IS_LEAD_HWFN(p_hwfn) &&
	    !p_dev->recov_in_prog) {
		rc = ecore_mcp_initiate_pf_flr(p_hwfn, p_hwfn->p_main_ptt);
		if (rc != ECORE_SUCCESS)
			DP_NOTICE(p_hwfn, false, "Failed to initiate PF FLR\n");
	}

	/* Check if mdump logs/data are present and update the epoch value */
	if (IS_LEAD_HWFN(p_hwfn)) {
#ifndef ASIC_ONLY
		if (!CHIP_REV_IS_EMUL(p_dev)) {
#endif
		rc = ecore_mcp_mdump_get_info(p_hwfn, p_hwfn->p_main_ptt,
					      &mdump_info);
		if (rc == ECORE_SUCCESS && mdump_info.num_of_logs)
			DP_NOTICE(p_hwfn, false,
				  "* * * IMPORTANT - HW ERROR register dump captured by device * * *\n");

		rc = ecore_mcp_mdump_get_retain(p_hwfn, p_hwfn->p_main_ptt,
						&mdump_retain);
		if (rc == ECORE_SUCCESS && mdump_retain.valid)
			DP_NOTICE(p_hwfn, false,
				  "mdump retained data: epoch 0x%08x, pf 0x%x, status 0x%08x\n",
				  mdump_retain.epoch, mdump_retain.pf,
				  mdump_retain.status);

		ecore_mcp_mdump_set_values(p_hwfn, p_hwfn->p_main_ptt,
					   p_params->epoch);
#ifndef ASIC_ONLY
		}
#endif
	}

	/* Allocate the init RT array and initialize the init-ops engine */
	rc = ecore_init_alloc(p_hwfn);
	if (rc) {
		DP_NOTICE(p_hwfn, false, "Failed to allocate the init array\n");
		if (p_params->b_relaxed_probe)
			p_params->p_relaxed_res = ECORE_HW_PREPARE_FAILED_MEM;
		goto err2;
	}

#ifndef ASIC_ONLY
	if (CHIP_REV_IS_FPGA(p_dev)) {
		DP_NOTICE(p_hwfn, false,
			  "FPGA: workaround; Prevent DMAE parities\n");
		ecore_wr(p_hwfn, p_hwfn->p_main_ptt, PCIE_REG_PRTY_MASK_K2_E5,
			 7);

		DP_NOTICE(p_hwfn, false,
			  "FPGA: workaround: Set VF bar0 size\n");
		ecore_wr(p_hwfn, p_hwfn->p_main_ptt,
			 PGLUE_B_REG_VF_BAR0_SIZE_K2_E5, 4);
	}
#endif

	return rc;
err2:
	if (IS_LEAD_HWFN(p_hwfn))
		ecore_iov_free_hw_info(p_dev);
	ecore_mcp_free(p_hwfn);
err1:
	ecore_hw_hwfn_free(p_hwfn);
err0:
	return rc;
}

enum _ecore_status_t ecore_hw_prepare(struct ecore_dev *p_dev,
				      struct ecore_hw_prepare_params *p_params)
{
	struct ecore_hwfn *p_hwfn = ECORE_LEADING_HWFN(p_dev);
	enum _ecore_status_t rc;

	p_dev->chk_reg_fifo = p_params->chk_reg_fifo;
	p_dev->allow_mdump = p_params->allow_mdump;

	if (p_params->b_relaxed_probe)
		p_params->p_relaxed_res = ECORE_HW_PREPARE_SUCCESS;

	/* Store the precompiled init data ptrs */
	if (IS_PF(p_dev))
		ecore_init_iro_array(p_dev);

	/* Initialize the first hwfn - will learn number of hwfns */
	rc = ecore_hw_prepare_single(p_hwfn, p_dev->regview,
				     p_dev->doorbells, p_dev->db_phys_addr,
				     p_params);
	if (rc != ECORE_SUCCESS)
		return rc;

	p_params->personality = p_hwfn->hw_info.personality;

	/* initilalize 2nd hwfn if necessary */
	if (ECORE_IS_CMT(p_dev)) {
		void OSAL_IOMEM *p_regview, *p_doorbell;
		u8 OSAL_IOMEM *addr;
		u64 db_phys_addr;
		u32 offset;

		/* adjust bar offset for second engine */
		offset = ecore_hw_bar_size(p_hwfn, p_hwfn->p_main_ptt,
					   BAR_ID_0) / 2;
		addr = (u8 OSAL_IOMEM *)p_dev->regview + offset;
		p_regview = (void OSAL_IOMEM *)addr;

		offset = ecore_hw_bar_size(p_hwfn, p_hwfn->p_main_ptt,
					   BAR_ID_1) / 2;
		addr = (u8 OSAL_IOMEM *)p_dev->doorbells + offset;
		p_doorbell = (void OSAL_IOMEM *)addr;
		db_phys_addr = p_dev->db_phys_addr + offset;

		/* prepare second hw function */
		rc = ecore_hw_prepare_single(&p_dev->hwfns[1], p_regview,
					     p_doorbell, db_phys_addr,
					     p_params);

		/* in case of error, need to free the previously
		 * initiliazed hwfn 0.
		 */
		if (rc != ECORE_SUCCESS) {
			if (p_params->b_relaxed_probe)
				p_params->p_relaxed_res =
						ECORE_HW_PREPARE_FAILED_ENG2;

			if (IS_PF(p_dev)) {
				ecore_init_free(p_hwfn);
				ecore_mcp_free(p_hwfn);
				ecore_hw_hwfn_free(p_hwfn);
			} else {
				DP_NOTICE(p_dev, false, "What do we need to free when VF hwfn1 init fails\n");
			}
			return rc;
		}
	}

	return rc;
}

void ecore_hw_remove(struct ecore_dev *p_dev)
{
	struct ecore_hwfn *p_hwfn = ECORE_LEADING_HWFN(p_dev);
	int i;

	if (IS_PF(p_dev))
		ecore_mcp_ov_update_driver_state(p_hwfn, p_hwfn->p_main_ptt,
						 ECORE_OV_DRIVER_STATE_NOT_LOADED);

	for_each_hwfn(p_dev, i) {
		struct ecore_hwfn *p_hwfn = &p_dev->hwfns[i];

		if (IS_VF(p_dev)) {
			ecore_vf_pf_release(p_hwfn);
			continue;
		}

		ecore_init_free(p_hwfn);
		ecore_hw_hwfn_free(p_hwfn);
		ecore_mcp_free(p_hwfn);

#ifdef CONFIG_ECORE_LOCK_ALLOC
		OSAL_SPIN_LOCK_DEALLOC(&p_hwfn->dmae_info.lock);
#endif
	}

	ecore_iov_free_hw_info(p_dev);
}

static void ecore_chain_free_next_ptr(struct ecore_dev *p_dev,
				      struct ecore_chain *p_chain)
{
	void *p_virt = p_chain->p_virt_addr, *p_virt_next = OSAL_NULL;
	dma_addr_t p_phys = p_chain->p_phys_addr, p_phys_next = 0;
	struct ecore_chain_next *p_next;
	u32 size, i;

	if (!p_virt)
		return;

	size = p_chain->elem_size * p_chain->usable_per_page;

	for (i = 0; i < p_chain->page_cnt; i++) {
		if (!p_virt)
			break;

		p_next = (struct ecore_chain_next *)((u8 *)p_virt + size);
		p_virt_next = p_next->next_virt;
		p_phys_next = HILO_DMA_REGPAIR(p_next->next_phys);

		OSAL_DMA_FREE_COHERENT(p_dev, p_virt, p_phys,
				       ECORE_CHAIN_PAGE_SIZE);

		p_virt = p_virt_next;
		p_phys = p_phys_next;
	}
}

static void ecore_chain_free_single(struct ecore_dev *p_dev,
				    struct ecore_chain *p_chain)
{
	if (!p_chain->p_virt_addr)
		return;

	OSAL_DMA_FREE_COHERENT(p_dev, p_chain->p_virt_addr,
			       p_chain->p_phys_addr, ECORE_CHAIN_PAGE_SIZE);
}

static void ecore_chain_free_pbl(struct ecore_dev *p_dev,
				 struct ecore_chain *p_chain)
{
	void **pp_virt_addr_tbl = p_chain->pbl.pp_virt_addr_tbl;
	u8 *p_pbl_virt = (u8 *)p_chain->pbl_sp.p_virt_table;
	u32 page_cnt = p_chain->page_cnt, i, pbl_size;

	if (!pp_virt_addr_tbl)
		return;

	if (!p_pbl_virt)
		goto out;

	for (i = 0; i < page_cnt; i++) {
		if (!pp_virt_addr_tbl[i])
			break;

		OSAL_DMA_FREE_COHERENT(p_dev, pp_virt_addr_tbl[i],
				       *(dma_addr_t *)p_pbl_virt,
				       ECORE_CHAIN_PAGE_SIZE);

		p_pbl_virt += ECORE_CHAIN_PBL_ENTRY_SIZE;
	}

	pbl_size = page_cnt * ECORE_CHAIN_PBL_ENTRY_SIZE;

	if (!p_chain->b_external_pbl) {
		OSAL_DMA_FREE_COHERENT(p_dev, p_chain->pbl_sp.p_virt_table,
				       p_chain->pbl_sp.p_phys_table, pbl_size);
	}
out:
	OSAL_VFREE(p_dev, p_chain->pbl.pp_virt_addr_tbl);
	p_chain->pbl.pp_virt_addr_tbl = OSAL_NULL;
}

void ecore_chain_free(struct ecore_dev *p_dev,
		      struct ecore_chain *p_chain)
{
	switch (p_chain->mode) {
	case ECORE_CHAIN_MODE_NEXT_PTR:
		ecore_chain_free_next_ptr(p_dev, p_chain);
		break;
	case ECORE_CHAIN_MODE_SINGLE:
		ecore_chain_free_single(p_dev, p_chain);
		break;
	case ECORE_CHAIN_MODE_PBL:
		ecore_chain_free_pbl(p_dev, p_chain);
		break;
	}
}

static enum _ecore_status_t
ecore_chain_alloc_sanity_check(struct ecore_dev *p_dev,
			       enum ecore_chain_cnt_type cnt_type,
			       osal_size_t elem_size, u32 page_cnt)
{
	u64 chain_size = ELEMS_PER_PAGE(elem_size) * page_cnt;

	/* The actual chain size can be larger than the maximal possible value
	 * after rounding up the requested elements number to pages, and after
	 * taking into acount the unusuable elements (next-ptr elements).
	 * The size of a "u16" chain can be (U16_MAX + 1) since the chain
	 * size/capacity fields are of a u32 type.
	 */
	if ((cnt_type == ECORE_CHAIN_CNT_TYPE_U16 &&
	     chain_size > ((u32)ECORE_U16_MAX + 1)) ||
	    (cnt_type == ECORE_CHAIN_CNT_TYPE_U32 &&
	     chain_size > ECORE_U32_MAX)) {
		DP_NOTICE(p_dev, true,
			  "The actual chain size (0x%llx) is larger than the maximal possible value\n",
			  (unsigned long long)chain_size);
		return ECORE_INVAL;
	}

	return ECORE_SUCCESS;
}

static enum _ecore_status_t
ecore_chain_alloc_next_ptr(struct ecore_dev *p_dev, struct ecore_chain *p_chain)
{
	void *p_virt = OSAL_NULL, *p_virt_prev = OSAL_NULL;
	dma_addr_t p_phys = 0;
	u32 i;

	for (i = 0; i < p_chain->page_cnt; i++) {
		p_virt = OSAL_DMA_ALLOC_COHERENT(p_dev, &p_phys,
						 ECORE_CHAIN_PAGE_SIZE);
		if (!p_virt) {
			DP_NOTICE(p_dev, false,
				  "Failed to allocate chain memory\n");
			return ECORE_NOMEM;
		}

		if (i == 0) {
			ecore_chain_init_mem(p_chain, p_virt, p_phys);
			ecore_chain_reset(p_chain);
		} else {
			ecore_chain_init_next_ptr_elem(p_chain, p_virt_prev,
						       p_virt, p_phys);
		}

		p_virt_prev = p_virt;
	}
	/* Last page's next element should point to the beginning of the
	 * chain.
	 */
	ecore_chain_init_next_ptr_elem(p_chain, p_virt_prev,
				       p_chain->p_virt_addr,
				       p_chain->p_phys_addr);

	return ECORE_SUCCESS;
}

static enum _ecore_status_t
ecore_chain_alloc_single(struct ecore_dev *p_dev, struct ecore_chain *p_chain)
{
	dma_addr_t p_phys = 0;
	void *p_virt = OSAL_NULL;

	p_virt = OSAL_DMA_ALLOC_COHERENT(p_dev, &p_phys, ECORE_CHAIN_PAGE_SIZE);
	if (!p_virt) {
		DP_NOTICE(p_dev, false, "Failed to allocate chain memory\n");
		return ECORE_NOMEM;
	}

	ecore_chain_init_mem(p_chain, p_virt, p_phys);
	ecore_chain_reset(p_chain);

	return ECORE_SUCCESS;
}

static enum _ecore_status_t
ecore_chain_alloc_pbl(struct ecore_dev *p_dev,
		      struct ecore_chain *p_chain,
		      struct ecore_chain_ext_pbl *ext_pbl)
{
	u32 page_cnt = p_chain->page_cnt, size, i;
	dma_addr_t p_phys = 0, p_pbl_phys = 0;
	void **pp_virt_addr_tbl = OSAL_NULL;
	u8 *p_pbl_virt = OSAL_NULL;
	void *p_virt = OSAL_NULL;

	size = page_cnt * sizeof(*pp_virt_addr_tbl);
	pp_virt_addr_tbl = (void **)OSAL_VZALLOC(p_dev, size);
	if (!pp_virt_addr_tbl) {
		DP_NOTICE(p_dev, false,
			  "Failed to allocate memory for the chain virtual addresses table\n");
		return ECORE_NOMEM;
	}

	/* The allocation of the PBL table is done with its full size, since it
	 * is expected to be successive.
	 * ecore_chain_init_pbl_mem() is called even in a case of an allocation
	 * failure, since pp_virt_addr_tbl was previously allocated, and it
	 * should be saved to allow its freeing during the error flow.
	 */
	size = page_cnt * ECORE_CHAIN_PBL_ENTRY_SIZE;

	if (ext_pbl == OSAL_NULL) {
		p_pbl_virt = OSAL_DMA_ALLOC_COHERENT(p_dev, &p_pbl_phys, size);
	} else {
		p_pbl_virt = ext_pbl->p_pbl_virt;
		p_pbl_phys = ext_pbl->p_pbl_phys;
		p_chain->b_external_pbl = true;
	}

	ecore_chain_init_pbl_mem(p_chain, p_pbl_virt, p_pbl_phys,
				 pp_virt_addr_tbl);
	if (!p_pbl_virt) {
		DP_NOTICE(p_dev, false, "Failed to allocate chain pbl memory\n");
		return ECORE_NOMEM;
	}

	for (i = 0; i < page_cnt; i++) {
		p_virt = OSAL_DMA_ALLOC_COHERENT(p_dev, &p_phys,
						 ECORE_CHAIN_PAGE_SIZE);
		if (!p_virt) {
			DP_NOTICE(p_dev, false,
				  "Failed to allocate chain memory\n");
			return ECORE_NOMEM;
		}

		if (i == 0) {
			ecore_chain_init_mem(p_chain, p_virt, p_phys);
			ecore_chain_reset(p_chain);
		}

		/* Fill the PBL table with the physical address of the page */
		*(dma_addr_t *)p_pbl_virt = p_phys;
		/* Keep the virtual address of the page */
		p_chain->pbl.pp_virt_addr_tbl[i] = p_virt;

		p_pbl_virt += ECORE_CHAIN_PBL_ENTRY_SIZE;
	}

	return ECORE_SUCCESS;
}

enum _ecore_status_t ecore_chain_alloc(struct ecore_dev *p_dev,
				       enum ecore_chain_use_mode intended_use,
				       enum ecore_chain_mode mode,
				       enum ecore_chain_cnt_type cnt_type,
				       u32 num_elems, osal_size_t elem_size,
				       struct ecore_chain *p_chain,
				       struct ecore_chain_ext_pbl *ext_pbl)
{
	u32 page_cnt;
	enum _ecore_status_t rc = ECORE_SUCCESS;

	if (mode == ECORE_CHAIN_MODE_SINGLE)
		page_cnt = 1;
	else
		page_cnt = ECORE_CHAIN_PAGE_CNT(num_elems, elem_size, mode);

	rc = ecore_chain_alloc_sanity_check(p_dev, cnt_type, elem_size,
					    page_cnt);
	if (rc) {
		DP_NOTICE(p_dev, false,
			  "Cannot allocate a chain with the given arguments:\n"
			  "[use_mode %d, mode %d, cnt_type %d, num_elems %d, elem_size %zu]\n",
			  intended_use, mode, cnt_type, num_elems, elem_size);
		return rc;
	}

	ecore_chain_init_params(p_chain, page_cnt, (u8)elem_size, intended_use,
				mode, cnt_type, p_dev->dp_ctx);

	switch (mode) {
	case ECORE_CHAIN_MODE_NEXT_PTR:
		rc = ecore_chain_alloc_next_ptr(p_dev, p_chain);
		break;
	case ECORE_CHAIN_MODE_SINGLE:
		rc = ecore_chain_alloc_single(p_dev, p_chain);
		break;
	case ECORE_CHAIN_MODE_PBL:
		rc = ecore_chain_alloc_pbl(p_dev, p_chain, ext_pbl);
		break;
	}
	if (rc)
		goto nomem;

	return ECORE_SUCCESS;

nomem:
	ecore_chain_free(p_dev, p_chain);
	return rc;
}

enum _ecore_status_t ecore_fw_l2_queue(struct ecore_hwfn *p_hwfn,
				       u16 src_id, u16 *dst_id)
{
	if (src_id >= RESC_NUM(p_hwfn, ECORE_L2_QUEUE)) {
		u16 min, max;

		min = (u16)RESC_START(p_hwfn, ECORE_L2_QUEUE);
		max = min + RESC_NUM(p_hwfn, ECORE_L2_QUEUE);
		DP_NOTICE(p_hwfn, true, "l2_queue id [%d] is not valid, available indices [%d - %d]\n",
			  src_id, min, max);

		return ECORE_INVAL;
	}

	*dst_id = RESC_START(p_hwfn, ECORE_L2_QUEUE) + src_id;

	return ECORE_SUCCESS;
}

enum _ecore_status_t ecore_fw_vport(struct ecore_hwfn *p_hwfn,
				    u8 src_id, u8 *dst_id)
{
	if (src_id >= RESC_NUM(p_hwfn, ECORE_VPORT)) {
		u8 min, max;

		min = (u8)RESC_START(p_hwfn, ECORE_VPORT);
		max = min + RESC_NUM(p_hwfn, ECORE_VPORT);
		DP_NOTICE(p_hwfn, true, "vport id [%d] is not valid, available indices [%d - %d]\n",
			  src_id, min, max);

		return ECORE_INVAL;
	}

	*dst_id = RESC_START(p_hwfn, ECORE_VPORT) + src_id;

	return ECORE_SUCCESS;
}

enum _ecore_status_t ecore_fw_rss_eng(struct ecore_hwfn *p_hwfn,
				      u8 src_id, u8 *dst_id)
{
	if (src_id >= RESC_NUM(p_hwfn, ECORE_RSS_ENG)) {
		u8 min, max;

		min = (u8)RESC_START(p_hwfn, ECORE_RSS_ENG);
		max = min + RESC_NUM(p_hwfn, ECORE_RSS_ENG);
		DP_NOTICE(p_hwfn, true, "rss_eng id [%d] is not valid, available indices [%d - %d]\n",
			  src_id, min, max);

		return ECORE_INVAL;
	}

	*dst_id = RESC_START(p_hwfn, ECORE_RSS_ENG) + src_id;

	return ECORE_SUCCESS;
}

enum _ecore_status_t
ecore_llh_set_function_as_default(struct ecore_hwfn *p_hwfn,
				  struct ecore_ptt *p_ptt)
{
	if (OSAL_TEST_BIT(ECORE_MF_NEED_DEF_PF, &p_hwfn->p_dev->mf_bits)) {
		ecore_wr(p_hwfn, p_ptt,
			 NIG_REG_LLH_TAGMAC_DEF_PF_VECTOR,
			 1 << p_hwfn->abs_pf_id / 2);
		ecore_wr(p_hwfn, p_ptt, PRS_REG_MSG_INFO, 0);
		return ECORE_SUCCESS;
	} else {
		DP_NOTICE(p_hwfn, false,
			  "This function can't be set as default\n");
		return ECORE_INVAL;
	}
}

static enum _ecore_status_t ecore_set_coalesce(struct ecore_hwfn *p_hwfn,
					       struct ecore_ptt *p_ptt,
					       u32 hw_addr, void *p_eth_qzone,
					       osal_size_t eth_qzone_size,
					       u8 timeset)
{
	struct coalescing_timeset *p_coal_timeset;

	if (p_hwfn->p_dev->int_coalescing_mode != ECORE_COAL_MODE_ENABLE) {
		DP_NOTICE(p_hwfn, true,
			  "Coalescing configuration not enabled\n");
		return ECORE_INVAL;
	}

	p_coal_timeset = p_eth_qzone;
	OSAL_MEMSET(p_eth_qzone, 0, eth_qzone_size);
	SET_FIELD(p_coal_timeset->value, COALESCING_TIMESET_TIMESET, timeset);
	SET_FIELD(p_coal_timeset->value, COALESCING_TIMESET_VALID, 1);
	ecore_memcpy_to(p_hwfn, p_ptt, hw_addr, p_eth_qzone, eth_qzone_size);

	return ECORE_SUCCESS;
}

enum _ecore_status_t ecore_set_queue_coalesce(struct ecore_hwfn *p_hwfn,
					      u16 rx_coal, u16 tx_coal,
					      void *p_handle)
{
	struct ecore_queue_cid *p_cid = (struct ecore_queue_cid *)p_handle;
	enum _ecore_status_t rc = ECORE_SUCCESS;
	struct ecore_ptt *p_ptt;

	/* TODO - Configuring a single queue's coalescing but
	 * claiming all queues are abiding same configuration
	 * for PF and VF both.
	 */

#ifdef CONFIG_ECORE_SRIOV
	if (IS_VF(p_hwfn->p_dev))
		return ecore_vf_pf_set_coalesce(p_hwfn, rx_coal,
						tx_coal, p_cid);
#endif /* #ifdef CONFIG_ECORE_SRIOV */

	p_ptt = ecore_ptt_acquire(p_hwfn);
	if (!p_ptt)
		return ECORE_AGAIN;

	if (rx_coal) {
		rc = ecore_set_rxq_coalesce(p_hwfn, p_ptt, rx_coal, p_cid);
		if (rc)
			goto out;
		p_hwfn->p_dev->rx_coalesce_usecs = rx_coal;
	}

	if (tx_coal) {
		rc = ecore_set_txq_coalesce(p_hwfn, p_ptt, tx_coal, p_cid);
		if (rc)
			goto out;
		p_hwfn->p_dev->tx_coalesce_usecs = tx_coal;
	}
out:
	ecore_ptt_release(p_hwfn, p_ptt);

	return rc;
}

enum _ecore_status_t ecore_set_rxq_coalesce(struct ecore_hwfn *p_hwfn,
					    struct ecore_ptt *p_ptt,
					    u16 coalesce,
					    struct ecore_queue_cid *p_cid)
{
	struct ustorm_eth_queue_zone eth_qzone;
	u8 timeset, timer_res;
	u32 address;
	enum _ecore_status_t rc;

	/* Coalesce = (timeset << timer-resolution), timeset is 7bit wide */
	if (coalesce <= 0x7F)
		timer_res = 0;
	else if (coalesce <= 0xFF)
		timer_res = 1;
	else if (coalesce <= 0x1FF)
		timer_res = 2;
	else {
		DP_ERR(p_hwfn, "Invalid coalesce value - %d\n", coalesce);
		return ECORE_INVAL;
	}
	timeset = (u8)(coalesce >> timer_res);

	rc = ecore_int_set_timer_res(p_hwfn, p_ptt, timer_res,
				     p_cid->sb_igu_id, false);
	if (rc != ECORE_SUCCESS)
		goto out;

	address = BAR0_MAP_REG_USDM_RAM +
		  USTORM_ETH_QUEUE_ZONE_OFFSET(p_cid->abs.queue_id);

	rc = ecore_set_coalesce(p_hwfn, p_ptt, address, &eth_qzone,
				sizeof(struct ustorm_eth_queue_zone), timeset);
	if (rc != ECORE_SUCCESS)
		goto out;

out:
	return rc;
}

enum _ecore_status_t ecore_set_txq_coalesce(struct ecore_hwfn *p_hwfn,
					    struct ecore_ptt *p_ptt,
					    u16 coalesce,
					    struct ecore_queue_cid *p_cid)
{
	struct xstorm_eth_queue_zone eth_qzone;
	u8 timeset, timer_res;
	u32 address;
	enum _ecore_status_t rc;

	/* Coalesce = (timeset << timer-resolution), timeset is 7bit wide */
	if (coalesce <= 0x7F)
		timer_res = 0;
	else if (coalesce <= 0xFF)
		timer_res = 1;
	else if (coalesce <= 0x1FF)
		timer_res = 2;
	else {
		DP_ERR(p_hwfn, "Invalid coalesce value - %d\n", coalesce);
		return ECORE_INVAL;
	}
	timeset = (u8)(coalesce >> timer_res);

	rc = ecore_int_set_timer_res(p_hwfn, p_ptt, timer_res,
				     p_cid->sb_igu_id, true);
	if (rc != ECORE_SUCCESS)
		goto out;

	address = BAR0_MAP_REG_XSDM_RAM +
		  XSTORM_ETH_QUEUE_ZONE_OFFSET(p_cid->abs.queue_id);

	rc = ecore_set_coalesce(p_hwfn, p_ptt, address, &eth_qzone,
				sizeof(struct xstorm_eth_queue_zone), timeset);
out:
	return rc;
}

/* Calculate final WFQ values for all vports and configure it.
 * After this configuration each vport must have
 * approx min rate =  vport_wfq * min_pf_rate / ECORE_WFQ_UNIT
 */
static void ecore_configure_wfq_for_all_vports(struct ecore_hwfn *p_hwfn,
					       struct ecore_ptt *p_ptt,
					       u32 min_pf_rate)
{
	struct init_qm_vport_params *vport_params;
	int i;

	vport_params = p_hwfn->qm_info.qm_vport_params;

	for (i = 0; i < p_hwfn->qm_info.num_vports; i++) {
		u32 wfq_speed = p_hwfn->qm_info.wfq_data[i].min_speed;

		vport_params[i].vport_wfq = (wfq_speed * ECORE_WFQ_UNIT) /
					    min_pf_rate;
		ecore_init_vport_wfq(p_hwfn, p_ptt,
				     vport_params[i].first_tx_pq_id,
				     vport_params[i].vport_wfq);
	}
}

static void ecore_init_wfq_default_param(struct ecore_hwfn *p_hwfn)

{
	int i;

	for (i = 0; i < p_hwfn->qm_info.num_vports; i++)
		p_hwfn->qm_info.qm_vport_params[i].vport_wfq = 1;
}

static void ecore_disable_wfq_for_all_vports(struct ecore_hwfn *p_hwfn,
					     struct ecore_ptt *p_ptt)
{
	struct init_qm_vport_params *vport_params;
	int i;

	vport_params = p_hwfn->qm_info.qm_vport_params;

	for (i = 0; i < p_hwfn->qm_info.num_vports; i++) {
		ecore_init_wfq_default_param(p_hwfn);
		ecore_init_vport_wfq(p_hwfn, p_ptt,
				     vport_params[i].first_tx_pq_id,
				     vport_params[i].vport_wfq);
	}
}

/* This function performs several validations for WFQ
 * configuration and required min rate for a given vport
 * 1. req_rate must be greater than one percent of min_pf_rate.
 * 2. req_rate should not cause other vports [not configured for WFQ explicitly]
 *    rates to get less than one percent of min_pf_rate.
 * 3. total_req_min_rate [all vports min rate sum] shouldn't exceed min_pf_rate.
 */
static enum _ecore_status_t ecore_init_wfq_param(struct ecore_hwfn *p_hwfn,
						 u16 vport_id, u32 req_rate,
						 u32 min_pf_rate)
{
	u32 total_req_min_rate = 0, total_left_rate = 0, left_rate_per_vp = 0;
	int non_requested_count = 0, req_count = 0, i, num_vports;

	num_vports = p_hwfn->qm_info.num_vports;

	/* Accounting for the vports which are configured for WFQ explicitly */
	for (i = 0; i < num_vports; i++) {
		u32 tmp_speed;

		if ((i != vport_id) && p_hwfn->qm_info.wfq_data[i].configured) {
			req_count++;
			tmp_speed = p_hwfn->qm_info.wfq_data[i].min_speed;
			total_req_min_rate += tmp_speed;
		}
	}

	/* Include current vport data as well */
	req_count++;
	total_req_min_rate += req_rate;
	non_requested_count = num_vports - req_count;

	/* validate possible error cases */
	if (req_rate < min_pf_rate / ECORE_WFQ_UNIT) {
		DP_VERBOSE(p_hwfn, ECORE_MSG_LINK,
			   "Vport [%d] - Requested rate[%d Mbps] is less than one percent of configured PF min rate[%d Mbps]\n",
			   vport_id, req_rate, min_pf_rate);
		return ECORE_INVAL;
	}

	/* TBD - for number of vports greater than 100 */
	if (num_vports > ECORE_WFQ_UNIT) {
		DP_VERBOSE(p_hwfn, ECORE_MSG_LINK,
			   "Number of vports is greater than %d\n",
			   ECORE_WFQ_UNIT);
		return ECORE_INVAL;
	}

	if (total_req_min_rate > min_pf_rate) {
		DP_VERBOSE(p_hwfn, ECORE_MSG_LINK,
			   "Total requested min rate for all vports[%d Mbps] is greater than configured PF min rate[%d Mbps]\n",
			   total_req_min_rate, min_pf_rate);
		return ECORE_INVAL;
	}

	/* Data left for non requested vports */
	total_left_rate = min_pf_rate - total_req_min_rate;
	left_rate_per_vp = total_left_rate / non_requested_count;

	/* validate if non requested get < 1% of min bw */
	if (left_rate_per_vp < min_pf_rate / ECORE_WFQ_UNIT) {
		DP_VERBOSE(p_hwfn, ECORE_MSG_LINK,
			   "Non WFQ configured vports rate [%d Mbps] is less than one percent of configured PF min rate[%d Mbps]\n",
			   left_rate_per_vp, min_pf_rate);
		return ECORE_INVAL;
	}

	/* now req_rate for given vport passes all scenarios.
	 * assign final wfq rates to all vports.
	 */
	p_hwfn->qm_info.wfq_data[vport_id].min_speed = req_rate;
	p_hwfn->qm_info.wfq_data[vport_id].configured = true;

	for (i = 0; i < num_vports; i++) {
		if (p_hwfn->qm_info.wfq_data[i].configured)
			continue;

		p_hwfn->qm_info.wfq_data[i].min_speed = left_rate_per_vp;
	}

	return ECORE_SUCCESS;
}

static int __ecore_configure_vport_wfq(struct ecore_hwfn *p_hwfn,
				       struct ecore_ptt *p_ptt,
				       u16 vp_id, u32 rate)
{
	struct ecore_mcp_link_state *p_link;
	int rc = ECORE_SUCCESS;

	p_link = &p_hwfn->p_dev->hwfns[0].mcp_info->link_output;

	if (!p_link->min_pf_rate) {
		p_hwfn->qm_info.wfq_data[vp_id].min_speed = rate;
		p_hwfn->qm_info.wfq_data[vp_id].configured = true;
		return rc;
	}

	rc = ecore_init_wfq_param(p_hwfn, vp_id, rate, p_link->min_pf_rate);

	if (rc == ECORE_SUCCESS)
		ecore_configure_wfq_for_all_vports(p_hwfn, p_ptt,
						   p_link->min_pf_rate);
	else
		DP_NOTICE(p_hwfn, false,
			  "Validation failed while configuring min rate\n");

	return rc;
}

static int __ecore_configure_vp_wfq_on_link_change(struct ecore_hwfn *p_hwfn,
						   struct ecore_ptt *p_ptt,
						   u32 min_pf_rate)
{
	bool use_wfq = false;
	int rc = ECORE_SUCCESS;
	u16 i;

	/* Validate all pre configured vports for wfq */
	for (i = 0; i < p_hwfn->qm_info.num_vports; i++) {
		u32 rate;

		if (!p_hwfn->qm_info.wfq_data[i].configured)
			continue;

		rate = p_hwfn->qm_info.wfq_data[i].min_speed;
		use_wfq = true;

		rc = ecore_init_wfq_param(p_hwfn, i, rate, min_pf_rate);
		if (rc != ECORE_SUCCESS) {
			DP_NOTICE(p_hwfn, false,
				  "WFQ validation failed while configuring min rate\n");
			break;
		}
	}

	if (rc == ECORE_SUCCESS && use_wfq)
		ecore_configure_wfq_for_all_vports(p_hwfn, p_ptt, min_pf_rate);
	else
		ecore_disable_wfq_for_all_vports(p_hwfn, p_ptt);

	return rc;
}

/* Main API for ecore clients to configure vport min rate.
 * vp_id - vport id in PF Range[0 - (total_num_vports_per_pf - 1)]
 * rate - Speed in Mbps needs to be assigned to a given vport.
 */
int ecore_configure_vport_wfq(struct ecore_dev *p_dev, u16 vp_id, u32 rate)
{
	int i, rc = ECORE_INVAL;

	/* TBD - for multiple hardware functions - that is 100 gig */
	if (ECORE_IS_CMT(p_dev)) {
		DP_NOTICE(p_dev, false,
			  "WFQ configuration is not supported for this device\n");
		return rc;
	}

	for_each_hwfn(p_dev, i) {
		struct ecore_hwfn *p_hwfn = &p_dev->hwfns[i];
		struct ecore_ptt *p_ptt;

		p_ptt = ecore_ptt_acquire(p_hwfn);
		if (!p_ptt)
			return ECORE_TIMEOUT;

		rc = __ecore_configure_vport_wfq(p_hwfn, p_ptt, vp_id, rate);

		if (rc != ECORE_SUCCESS) {
			ecore_ptt_release(p_hwfn, p_ptt);
			return rc;
		}

		ecore_ptt_release(p_hwfn, p_ptt);
	}

	return rc;
}

/* API to configure WFQ from mcp link change */
void ecore_configure_vp_wfq_on_link_change(struct ecore_dev *p_dev,
					   struct ecore_ptt *p_ptt,
					   u32 min_pf_rate)
{
	int i;

	/* TBD - for multiple hardware functions - that is 100 gig */
	if (ECORE_IS_CMT(p_dev)) {
		DP_VERBOSE(p_dev, ECORE_MSG_LINK,
			   "WFQ configuration is not supported for this device\n");
		return;
	}

	for_each_hwfn(p_dev, i) {
		struct ecore_hwfn *p_hwfn = &p_dev->hwfns[i];

		__ecore_configure_vp_wfq_on_link_change(p_hwfn, p_ptt,
							min_pf_rate);
	}
}

int __ecore_configure_pf_max_bandwidth(struct ecore_hwfn *p_hwfn,
				       struct ecore_ptt *p_ptt,
				       struct ecore_mcp_link_state *p_link,
				       u8 max_bw)
{
	int rc = ECORE_SUCCESS;

	p_hwfn->mcp_info->func_info.bandwidth_max = max_bw;

	if (!p_link->line_speed && (max_bw != 100))
		return rc;

	p_link->speed = (p_link->line_speed * max_bw) / 100;
	p_hwfn->qm_info.pf_rl = p_link->speed;

	/* Since the limiter also affects Tx-switched traffic, we don't want it
	 * to limit such traffic in case there's no actual limit.
	 * In that case, set limit to imaginary high boundary.
	 */
	if (max_bw == 100)
		p_hwfn->qm_info.pf_rl = 100000;

	rc = ecore_init_pf_rl(p_hwfn, p_ptt, p_hwfn->rel_pf_id,
			      p_hwfn->qm_info.pf_rl);

	DP_VERBOSE(p_hwfn, ECORE_MSG_LINK,
		   "Configured MAX bandwidth to be %08x Mb/sec\n",
		   p_link->speed);

	return rc;
}

/* Main API to configure PF max bandwidth where bw range is [1 - 100] */
int ecore_configure_pf_max_bandwidth(struct ecore_dev *p_dev, u8 max_bw)
{
	int i, rc = ECORE_INVAL;

	if (max_bw < 1 || max_bw > 100) {
		DP_NOTICE(p_dev, false, "PF max bw valid range is [1-100]\n");
		return rc;
	}

	for_each_hwfn(p_dev, i) {
		struct ecore_hwfn *p_hwfn = &p_dev->hwfns[i];
		struct ecore_hwfn *p_lead = ECORE_LEADING_HWFN(p_dev);
		struct ecore_mcp_link_state *p_link;
		struct ecore_ptt *p_ptt;

		p_link = &p_lead->mcp_info->link_output;

		p_ptt = ecore_ptt_acquire(p_hwfn);
		if (!p_ptt)
			return ECORE_TIMEOUT;

		rc = __ecore_configure_pf_max_bandwidth(p_hwfn, p_ptt,
							p_link, max_bw);

		ecore_ptt_release(p_hwfn, p_ptt);

		if (rc != ECORE_SUCCESS)
			break;
	}

	return rc;
}

int __ecore_configure_pf_min_bandwidth(struct ecore_hwfn *p_hwfn,
				       struct ecore_ptt *p_ptt,
				       struct ecore_mcp_link_state *p_link,
				       u8 min_bw)
{
	int rc = ECORE_SUCCESS;

	p_hwfn->mcp_info->func_info.bandwidth_min = min_bw;
	p_hwfn->qm_info.pf_wfq = min_bw;

	if (!p_link->line_speed)
		return rc;

	p_link->min_pf_rate = (p_link->line_speed * min_bw) / 100;

	rc = ecore_init_pf_wfq(p_hwfn, p_ptt, p_hwfn->rel_pf_id, min_bw);

	DP_VERBOSE(p_hwfn, ECORE_MSG_LINK,
		   "Configured MIN bandwidth to be %d Mb/sec\n",
		   p_link->min_pf_rate);

	return rc;
}

/* Main API to configure PF min bandwidth where bw range is [1-100] */
int ecore_configure_pf_min_bandwidth(struct ecore_dev *p_dev, u8 min_bw)
{
	int i, rc = ECORE_INVAL;

	if (min_bw < 1 || min_bw > 100) {
		DP_NOTICE(p_dev, false, "PF min bw valid range is [1-100]\n");
		return rc;
	}

	for_each_hwfn(p_dev, i) {
		struct ecore_hwfn *p_hwfn = &p_dev->hwfns[i];
		struct ecore_hwfn *p_lead = ECORE_LEADING_HWFN(p_dev);
		struct ecore_mcp_link_state *p_link;
		struct ecore_ptt *p_ptt;

		p_link = &p_lead->mcp_info->link_output;

		p_ptt = ecore_ptt_acquire(p_hwfn);
		if (!p_ptt)
			return ECORE_TIMEOUT;

		rc = __ecore_configure_pf_min_bandwidth(p_hwfn, p_ptt,
							p_link, min_bw);
		if (rc != ECORE_SUCCESS) {
			ecore_ptt_release(p_hwfn, p_ptt);
			return rc;
		}

		if (p_link->min_pf_rate) {
			u32 min_rate = p_link->min_pf_rate;

			rc = __ecore_configure_vp_wfq_on_link_change(p_hwfn,
								     p_ptt,
								     min_rate);
		}

		ecore_ptt_release(p_hwfn, p_ptt);
	}

	return rc;
}

void ecore_clean_wfq_db(struct ecore_hwfn *p_hwfn, struct ecore_ptt *p_ptt)
{
	struct ecore_mcp_link_state *p_link;

	p_link = &p_hwfn->mcp_info->link_output;

	if (p_link->min_pf_rate)
		ecore_disable_wfq_for_all_vports(p_hwfn, p_ptt);

	OSAL_MEMSET(p_hwfn->qm_info.wfq_data, 0,
		    sizeof(*p_hwfn->qm_info.wfq_data) *
				p_hwfn->qm_info.num_vports);
}

int ecore_device_num_engines(struct ecore_dev *p_dev)
{
	return ECORE_IS_BB(p_dev) ? 2 : 1;
}

int ecore_device_num_ports(struct ecore_dev *p_dev)
{
	return p_dev->num_ports;
}

void ecore_set_fw_mac_addr(__le16 *fw_msb,
			  __le16 *fw_mid,
			  __le16 *fw_lsb,
			  u8 *mac)
{
	((u8 *)fw_msb)[0] = mac[1];
	((u8 *)fw_msb)[1] = mac[0];
	((u8 *)fw_mid)[0] = mac[3];
	((u8 *)fw_mid)[1] = mac[2];
	((u8 *)fw_lsb)[0] = mac[5];
	((u8 *)fw_lsb)[1] = mac[4];
}

void ecore_set_dev_access_enable(struct ecore_dev *p_dev, bool b_enable)
{
	if (p_dev->recov_in_prog != !b_enable) {
		DP_INFO(p_dev, "%s access to the device\n",
			b_enable ?  "Enable" : "Disable");
		p_dev->recov_in_prog = !b_enable;
	}
}

#ifdef _NTDDK_
#pragma warning(pop)
#endif
