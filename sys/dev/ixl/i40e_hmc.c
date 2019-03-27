/******************************************************************************

  Copyright (c) 2013-2018, Intel Corporation
  All rights reserved.
  
  Redistribution and use in source and binary forms, with or without 
  modification, are permitted provided that the following conditions are met:
  
   1. Redistributions of source code must retain the above copyright notice, 
      this list of conditions and the following disclaimer.
  
   2. Redistributions in binary form must reproduce the above copyright 
      notice, this list of conditions and the following disclaimer in the 
      documentation and/or other materials provided with the distribution.
  
   3. Neither the name of the Intel Corporation nor the names of its 
      contributors may be used to endorse or promote products derived from 
      this software without specific prior written permission.
  
  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE 
  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE 
  ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE 
  LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR 
  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF 
  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS 
  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN 
  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) 
  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
  POSSIBILITY OF SUCH DAMAGE.

******************************************************************************/
/*$FreeBSD$*/

#include "i40e_osdep.h"
#include "i40e_register.h"
#include "i40e_status.h"
#include "i40e_alloc.h"
#include "i40e_hmc.h"
#include "i40e_type.h"

/**
 * i40e_add_sd_table_entry - Adds a segment descriptor to the table
 * @hw: pointer to our hw struct
 * @hmc_info: pointer to the HMC configuration information struct
 * @sd_index: segment descriptor index to manipulate
 * @type: what type of segment descriptor we're manipulating
 * @direct_mode_sz: size to alloc in direct mode
 **/
enum i40e_status_code i40e_add_sd_table_entry(struct i40e_hw *hw,
					      struct i40e_hmc_info *hmc_info,
					      u32 sd_index,
					      enum i40e_sd_entry_type type,
					      u64 direct_mode_sz)
{
	enum i40e_status_code ret_code = I40E_SUCCESS;
	struct i40e_hmc_sd_entry *sd_entry;
	enum   i40e_memory_type mem_type;
	bool dma_mem_alloc_done = FALSE;
	struct i40e_dma_mem mem;
	u64 alloc_len;

	if (NULL == hmc_info->sd_table.sd_entry) {
		ret_code = I40E_ERR_BAD_PTR;
		DEBUGOUT("i40e_add_sd_table_entry: bad sd_entry\n");
		goto exit;
	}

	if (sd_index >= hmc_info->sd_table.sd_cnt) {
		ret_code = I40E_ERR_INVALID_SD_INDEX;
		DEBUGOUT("i40e_add_sd_table_entry: bad sd_index\n");
		goto exit;
	}

	sd_entry = &hmc_info->sd_table.sd_entry[sd_index];
	if (!sd_entry->valid) {
		if (I40E_SD_TYPE_PAGED == type) {
			mem_type = i40e_mem_pd;
			alloc_len = I40E_HMC_PAGED_BP_SIZE;
		} else {
			mem_type = i40e_mem_bp_jumbo;
			alloc_len = direct_mode_sz;
		}

		/* allocate a 4K pd page or 2M backing page */
		ret_code = i40e_allocate_dma_mem(hw, &mem, mem_type, alloc_len,
						 I40E_HMC_PD_BP_BUF_ALIGNMENT);
		if (ret_code)
			goto exit;
		dma_mem_alloc_done = TRUE;
		if (I40E_SD_TYPE_PAGED == type) {
			ret_code = i40e_allocate_virt_mem(hw,
					&sd_entry->u.pd_table.pd_entry_virt_mem,
					sizeof(struct i40e_hmc_pd_entry) * 512);
			if (ret_code)
				goto exit;
			sd_entry->u.pd_table.pd_entry =
				(struct i40e_hmc_pd_entry *)
				sd_entry->u.pd_table.pd_entry_virt_mem.va;
			i40e_memcpy(&sd_entry->u.pd_table.pd_page_addr,
				    &mem, sizeof(struct i40e_dma_mem),
				    I40E_NONDMA_TO_NONDMA);
		} else {
			i40e_memcpy(&sd_entry->u.bp.addr,
				    &mem, sizeof(struct i40e_dma_mem),
				    I40E_NONDMA_TO_NONDMA);
			sd_entry->u.bp.sd_pd_index = sd_index;
		}
		/* initialize the sd entry */
		hmc_info->sd_table.sd_entry[sd_index].entry_type = type;

		/* increment the ref count */
		I40E_INC_SD_REFCNT(&hmc_info->sd_table);
	}
	/* Increment backing page reference count */
	if (I40E_SD_TYPE_DIRECT == sd_entry->entry_type)
		I40E_INC_BP_REFCNT(&sd_entry->u.bp);
exit:
	if (I40E_SUCCESS != ret_code)
		if (dma_mem_alloc_done)
			i40e_free_dma_mem(hw, &mem);

	return ret_code;
}

/**
 * i40e_add_pd_table_entry - Adds page descriptor to the specified table
 * @hw: pointer to our HW structure
 * @hmc_info: pointer to the HMC configuration information structure
 * @pd_index: which page descriptor index to manipulate
 * @rsrc_pg: if not NULL, use preallocated page instead of allocating new one.
 *
 * This function:
 *	1. Initializes the pd entry
 *	2. Adds pd_entry in the pd_table
 *	3. Mark the entry valid in i40e_hmc_pd_entry structure
 *	4. Initializes the pd_entry's ref count to 1
 * assumptions:
 *	1. The memory for pd should be pinned down, physically contiguous and
 *	   aligned on 4K boundary and zeroed memory.
 *	2. It should be 4K in size.
 **/
enum i40e_status_code i40e_add_pd_table_entry(struct i40e_hw *hw,
					      struct i40e_hmc_info *hmc_info,
					      u32 pd_index,
					      struct i40e_dma_mem *rsrc_pg)
{
	enum i40e_status_code ret_code = I40E_SUCCESS;
	struct i40e_hmc_pd_table *pd_table;
	struct i40e_hmc_pd_entry *pd_entry;
	struct i40e_dma_mem mem;
	struct i40e_dma_mem *page = &mem;
	u32 sd_idx, rel_pd_idx;
	u64 *pd_addr;
	u64 page_desc;

	if (pd_index / I40E_HMC_PD_CNT_IN_SD >= hmc_info->sd_table.sd_cnt) {
		ret_code = I40E_ERR_INVALID_PAGE_DESC_INDEX;
		DEBUGOUT("i40e_add_pd_table_entry: bad pd_index\n");
		goto exit;
	}

	/* find corresponding sd */
	sd_idx = (pd_index / I40E_HMC_PD_CNT_IN_SD);
	if (I40E_SD_TYPE_PAGED !=
	    hmc_info->sd_table.sd_entry[sd_idx].entry_type)
		goto exit;

	rel_pd_idx = (pd_index % I40E_HMC_PD_CNT_IN_SD);
	pd_table = &hmc_info->sd_table.sd_entry[sd_idx].u.pd_table;
	pd_entry = &pd_table->pd_entry[rel_pd_idx];
	if (!pd_entry->valid) {
		if (rsrc_pg) {
			pd_entry->rsrc_pg = TRUE;
			page = rsrc_pg;
		} else {
			/* allocate a 4K backing page */
			ret_code = i40e_allocate_dma_mem(hw, page, i40e_mem_bp,
						I40E_HMC_PAGED_BP_SIZE,
						I40E_HMC_PD_BP_BUF_ALIGNMENT);
			if (ret_code)
				goto exit;
			pd_entry->rsrc_pg = FALSE;
		}

		i40e_memcpy(&pd_entry->bp.addr, page,
			    sizeof(struct i40e_dma_mem), I40E_NONDMA_TO_NONDMA);
		pd_entry->bp.sd_pd_index = pd_index;
		pd_entry->bp.entry_type = I40E_SD_TYPE_PAGED;
		/* Set page address and valid bit */
		page_desc = page->pa | 0x1;

		pd_addr = (u64 *)pd_table->pd_page_addr.va;
		pd_addr += rel_pd_idx;

		/* Add the backing page physical address in the pd entry */
		i40e_memcpy(pd_addr, &page_desc, sizeof(u64),
			    I40E_NONDMA_TO_DMA);

		pd_entry->sd_index = sd_idx;
		pd_entry->valid = TRUE;
		I40E_INC_PD_REFCNT(pd_table);
	}
	I40E_INC_BP_REFCNT(&pd_entry->bp);
exit:
	return ret_code;
}

/**
 * i40e_remove_pd_bp - remove a backing page from a page descriptor
 * @hw: pointer to our HW structure
 * @hmc_info: pointer to the HMC configuration information structure
 * @idx: the page index
 *
 * This function:
 *	1. Marks the entry in pd tabe (for paged address mode) or in sd table
 *	   (for direct address mode) invalid.
 *	2. Write to register PMPDINV to invalidate the backing page in FV cache
 *	3. Decrement the ref count for the pd _entry
 * assumptions:
 *	1. Caller can deallocate the memory used by backing storage after this
 *	   function returns.
 **/
enum i40e_status_code i40e_remove_pd_bp(struct i40e_hw *hw,
					struct i40e_hmc_info *hmc_info,
					u32 idx)
{
	enum i40e_status_code ret_code = I40E_SUCCESS;
	struct i40e_hmc_pd_entry *pd_entry;
	struct i40e_hmc_pd_table *pd_table;
	struct i40e_hmc_sd_entry *sd_entry;
	u32 sd_idx, rel_pd_idx;
	u64 *pd_addr;

	/* calculate index */
	sd_idx = idx / I40E_HMC_PD_CNT_IN_SD;
	rel_pd_idx = idx % I40E_HMC_PD_CNT_IN_SD;
	if (sd_idx >= hmc_info->sd_table.sd_cnt) {
		ret_code = I40E_ERR_INVALID_PAGE_DESC_INDEX;
		DEBUGOUT("i40e_remove_pd_bp: bad idx\n");
		goto exit;
	}
	sd_entry = &hmc_info->sd_table.sd_entry[sd_idx];
	if (I40E_SD_TYPE_PAGED != sd_entry->entry_type) {
		ret_code = I40E_ERR_INVALID_SD_TYPE;
		DEBUGOUT("i40e_remove_pd_bp: wrong sd_entry type\n");
		goto exit;
	}
	/* get the entry and decrease its ref counter */
	pd_table = &hmc_info->sd_table.sd_entry[sd_idx].u.pd_table;
	pd_entry = &pd_table->pd_entry[rel_pd_idx];
	I40E_DEC_BP_REFCNT(&pd_entry->bp);
	if (pd_entry->bp.ref_cnt)
		goto exit;

	/* mark the entry invalid */
	pd_entry->valid = FALSE;
	I40E_DEC_PD_REFCNT(pd_table);
	pd_addr = (u64 *)pd_table->pd_page_addr.va;
	pd_addr += rel_pd_idx;
	i40e_memset(pd_addr, 0, sizeof(u64), I40E_DMA_MEM);
	I40E_INVALIDATE_PF_HMC_PD(hw, sd_idx, idx);

	/* free memory here */
	if (!pd_entry->rsrc_pg)
		ret_code = i40e_free_dma_mem(hw, &(pd_entry->bp.addr));
	if (I40E_SUCCESS != ret_code)
		goto exit;
	if (!pd_table->ref_cnt)
		i40e_free_virt_mem(hw, &pd_table->pd_entry_virt_mem);
exit:
	return ret_code;
}

/**
 * i40e_prep_remove_sd_bp - Prepares to remove a backing page from a sd entry
 * @hmc_info: pointer to the HMC configuration information structure
 * @idx: the page index
 **/
enum i40e_status_code i40e_prep_remove_sd_bp(struct i40e_hmc_info *hmc_info,
					     u32 idx)
{
	enum i40e_status_code ret_code = I40E_SUCCESS;
	struct i40e_hmc_sd_entry *sd_entry;

	/* get the entry and decrease its ref counter */
	sd_entry = &hmc_info->sd_table.sd_entry[idx];
	I40E_DEC_BP_REFCNT(&sd_entry->u.bp);
	if (sd_entry->u.bp.ref_cnt) {
		ret_code = I40E_ERR_NOT_READY;
		goto exit;
	}
	I40E_DEC_SD_REFCNT(&hmc_info->sd_table);

	/* mark the entry invalid */
	sd_entry->valid = FALSE;
exit:
	return ret_code;
}

/**
 * i40e_remove_sd_bp_new - Removes a backing page from a segment descriptor
 * @hw: pointer to our hw struct
 * @hmc_info: pointer to the HMC configuration information structure
 * @idx: the page index
 * @is_pf: used to distinguish between VF and PF
 **/
enum i40e_status_code i40e_remove_sd_bp_new(struct i40e_hw *hw,
					    struct i40e_hmc_info *hmc_info,
					    u32 idx, bool is_pf)
{
	struct i40e_hmc_sd_entry *sd_entry;

	if (!is_pf)
		return I40E_NOT_SUPPORTED;

	/* get the entry and decrease its ref counter */
	sd_entry = &hmc_info->sd_table.sd_entry[idx];
	I40E_CLEAR_PF_SD_ENTRY(hw, idx, I40E_SD_TYPE_DIRECT);

	return i40e_free_dma_mem(hw, &(sd_entry->u.bp.addr));
}

/**
 * i40e_prep_remove_pd_page - Prepares to remove a PD page from sd entry.
 * @hmc_info: pointer to the HMC configuration information structure
 * @idx: segment descriptor index to find the relevant page descriptor
 **/
enum i40e_status_code i40e_prep_remove_pd_page(struct i40e_hmc_info *hmc_info,
					       u32 idx)
{
	enum i40e_status_code ret_code = I40E_SUCCESS;
	struct i40e_hmc_sd_entry *sd_entry;

	sd_entry = &hmc_info->sd_table.sd_entry[idx];

	if (sd_entry->u.pd_table.ref_cnt) {
		ret_code = I40E_ERR_NOT_READY;
		goto exit;
	}

	/* mark the entry invalid */
	sd_entry->valid = FALSE;

	I40E_DEC_SD_REFCNT(&hmc_info->sd_table);
exit:
	return ret_code;
}

/**
 * i40e_remove_pd_page_new - Removes a PD page from sd entry.
 * @hw: pointer to our hw struct
 * @hmc_info: pointer to the HMC configuration information structure
 * @idx: segment descriptor index to find the relevant page descriptor
 * @is_pf: used to distinguish between VF and PF
 **/
enum i40e_status_code i40e_remove_pd_page_new(struct i40e_hw *hw,
					      struct i40e_hmc_info *hmc_info,
					      u32 idx, bool is_pf)
{
	struct i40e_hmc_sd_entry *sd_entry;

	if (!is_pf)
		return I40E_NOT_SUPPORTED;

	sd_entry = &hmc_info->sd_table.sd_entry[idx];
	I40E_CLEAR_PF_SD_ENTRY(hw, idx, I40E_SD_TYPE_PAGED);

	return i40e_free_dma_mem(hw, &(sd_entry->u.pd_table.pd_page_addr));
}
