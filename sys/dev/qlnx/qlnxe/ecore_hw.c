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
 * File : ecore_hw.c
 */
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "bcm_osal.h"
#include "ecore_hsi_common.h"
#include "ecore_status.h"
#include "ecore.h"
#include "ecore_hw.h"
#include "reg_addr.h"
#include "ecore_utils.h"
#include "ecore_iov_api.h"

#ifdef _NTDDK_
#pragma warning(push)
#pragma warning(disable : 28167)
#pragma warning(disable : 28123)
#pragma warning(disable : 28121)
#endif

#ifndef ASIC_ONLY
#define ECORE_EMUL_FACTOR 2000
#define ECORE_FPGA_FACTOR 200
#endif

#define ECORE_BAR_ACQUIRE_TIMEOUT 1000

/* Invalid values */
#define ECORE_BAR_INVALID_OFFSET	(OSAL_CPU_TO_LE32(-1))

struct ecore_ptt {
	osal_list_entry_t	list_entry;
	unsigned int		idx;
	struct pxp_ptt_entry	pxp;
	u8			hwfn_id;
};

struct ecore_ptt_pool {
	osal_list_t		free_list;
	osal_spinlock_t		lock; /* ptt synchronized access */
	struct ecore_ptt	ptts[PXP_EXTERNAL_BAR_PF_WINDOW_NUM];
};

static void __ecore_ptt_pool_free(struct ecore_hwfn *p_hwfn)
{
	OSAL_FREE(p_hwfn->p_dev, p_hwfn->p_ptt_pool);
	p_hwfn->p_ptt_pool = OSAL_NULL;
}

enum _ecore_status_t ecore_ptt_pool_alloc(struct ecore_hwfn *p_hwfn)
{
	struct ecore_ptt_pool *p_pool = OSAL_ALLOC(p_hwfn->p_dev,
						   GFP_KERNEL,
						   sizeof(*p_pool));
	int i;

	if (!p_pool)
		return ECORE_NOMEM;

	OSAL_LIST_INIT(&p_pool->free_list);
	for (i = 0; i < PXP_EXTERNAL_BAR_PF_WINDOW_NUM; i++) {
		p_pool->ptts[i].idx = i;
		p_pool->ptts[i].pxp.offset = ECORE_BAR_INVALID_OFFSET;
		p_pool->ptts[i].pxp.pretend.control = 0;
		p_pool->ptts[i].hwfn_id = p_hwfn->my_id;

		/* There are special PTT entries that are taken only by design.
		 * The rest are added ot the list for general usage.
		 */
		if (i >= RESERVED_PTT_MAX)
			OSAL_LIST_PUSH_HEAD(&p_pool->ptts[i].list_entry,
					    &p_pool->free_list);
	}

	p_hwfn->p_ptt_pool = p_pool;
#ifdef CONFIG_ECORE_LOCK_ALLOC
	if (OSAL_SPIN_LOCK_ALLOC(p_hwfn, &p_pool->lock)) {
		__ecore_ptt_pool_free(p_hwfn);
		return ECORE_NOMEM;
	}
#endif
	OSAL_SPIN_LOCK_INIT(&p_pool->lock);
	return ECORE_SUCCESS;
}

void ecore_ptt_invalidate(struct ecore_hwfn *p_hwfn)
{
	struct ecore_ptt *p_ptt;
	int i;

	for (i = 0; i < PXP_EXTERNAL_BAR_PF_WINDOW_NUM; i++) {
		p_ptt = &p_hwfn->p_ptt_pool->ptts[i];
		p_ptt->pxp.offset = ECORE_BAR_INVALID_OFFSET;
	}
}

void ecore_ptt_pool_free(struct ecore_hwfn *p_hwfn)
{
#ifdef CONFIG_ECORE_LOCK_ALLOC
	if (p_hwfn->p_ptt_pool)
		OSAL_SPIN_LOCK_DEALLOC(&p_hwfn->p_ptt_pool->lock);
#endif
	__ecore_ptt_pool_free(p_hwfn);
}

struct ecore_ptt *ecore_ptt_acquire(struct ecore_hwfn *p_hwfn)
{
	struct ecore_ptt *p_ptt;
	unsigned int i;

	/* Take the free PTT from the list */
	for (i = 0; i < ECORE_BAR_ACQUIRE_TIMEOUT; i++) {
		OSAL_SPIN_LOCK(&p_hwfn->p_ptt_pool->lock);

		if (!OSAL_LIST_IS_EMPTY(&p_hwfn->p_ptt_pool->free_list)) {
			p_ptt = OSAL_LIST_FIRST_ENTRY(&p_hwfn->p_ptt_pool->free_list,
						      struct ecore_ptt, list_entry);
			OSAL_LIST_REMOVE_ENTRY(&p_ptt->list_entry,
					       &p_hwfn->p_ptt_pool->free_list);

			OSAL_SPIN_UNLOCK(&p_hwfn->p_ptt_pool->lock);

			DP_VERBOSE(p_hwfn, ECORE_MSG_HW,
				   "allocated ptt %d\n", p_ptt->idx);

			return p_ptt;
		}

		OSAL_SPIN_UNLOCK(&p_hwfn->p_ptt_pool->lock);
		OSAL_MSLEEP(1);
	}

	DP_NOTICE(p_hwfn, true, "PTT acquire timeout - failed to allocate PTT\n");
	return OSAL_NULL;
}

void ecore_ptt_release(struct ecore_hwfn *p_hwfn,
		       struct ecore_ptt *p_ptt) {
	/* This PTT should not be set to pretend if it is being released */
	/* TODO - add some pretend sanity checks, to make sure pretend isn't set on this ptt */

	OSAL_SPIN_LOCK(&p_hwfn->p_ptt_pool->lock);
	OSAL_LIST_PUSH_HEAD(&p_ptt->list_entry, &p_hwfn->p_ptt_pool->free_list);
	OSAL_SPIN_UNLOCK(&p_hwfn->p_ptt_pool->lock);
}

static u32 ecore_ptt_get_hw_addr(struct ecore_ptt *p_ptt)
{
	/* The HW is using DWORDS and we need to translate it to Bytes */
	return OSAL_LE32_TO_CPU(p_ptt->pxp.offset) << 2;
}

static u32 ecore_ptt_config_addr(struct ecore_ptt *p_ptt)
{
	return PXP_PF_WINDOW_ADMIN_PER_PF_START +
	       p_ptt->idx * sizeof(struct pxp_ptt_entry);
}

u32 ecore_ptt_get_bar_addr(struct ecore_ptt *p_ptt)
{
	return PXP_EXTERNAL_BAR_PF_WINDOW_START +
	       p_ptt->idx * PXP_EXTERNAL_BAR_PF_WINDOW_SINGLE_SIZE;
}

void ecore_ptt_set_win(struct ecore_hwfn *p_hwfn,
		       struct ecore_ptt *p_ptt,
		       u32 new_hw_addr)
{
	u32 prev_hw_addr;

	prev_hw_addr = ecore_ptt_get_hw_addr(p_ptt);

	if (new_hw_addr == prev_hw_addr)
		return;

	/* Update PTT entery in admin window */
	DP_VERBOSE(p_hwfn, ECORE_MSG_HW,
		   "Updating PTT entry %d to offset 0x%x\n",
		   p_ptt->idx, new_hw_addr);

	/* The HW is using DWORDS and the address is in Bytes */
	p_ptt->pxp.offset = OSAL_CPU_TO_LE32(new_hw_addr >> 2);

	REG_WR(p_hwfn,
	       ecore_ptt_config_addr(p_ptt) +
	       OFFSETOF(struct pxp_ptt_entry, offset),
	       OSAL_LE32_TO_CPU(p_ptt->pxp.offset));
}

static u32 ecore_set_ptt(struct ecore_hwfn *p_hwfn,
			 struct ecore_ptt *p_ptt,
			 u32 hw_addr)
{
	u32 win_hw_addr = ecore_ptt_get_hw_addr(p_ptt);
	u32 offset;

	offset = hw_addr - win_hw_addr;

	if (p_ptt->hwfn_id != p_hwfn->my_id)
		DP_NOTICE(p_hwfn, true,
			  "ptt[%d] of hwfn[%02x] is used by hwfn[%02x]!\n",
			  p_ptt->idx, p_ptt->hwfn_id, p_hwfn->my_id);

	/* Verify the address is within the window */
	if (hw_addr < win_hw_addr ||
	    offset >= PXP_EXTERNAL_BAR_PF_WINDOW_SINGLE_SIZE) {
		ecore_ptt_set_win(p_hwfn, p_ptt, hw_addr);
		offset = 0;
	}

	return ecore_ptt_get_bar_addr(p_ptt) + offset;
}

struct ecore_ptt *ecore_get_reserved_ptt(struct ecore_hwfn *p_hwfn,
					 enum reserved_ptts ptt_idx)
{
	if (ptt_idx >= RESERVED_PTT_MAX) {
		DP_NOTICE(p_hwfn, true,
			  "Requested PTT %d is out of range\n", ptt_idx);
		return OSAL_NULL;
	}

	return &p_hwfn->p_ptt_pool->ptts[ptt_idx];
}

static bool ecore_is_reg_fifo_empty(struct ecore_hwfn *p_hwfn,
				    struct ecore_ptt *p_ptt)
{
	bool is_empty = true;
	u32 bar_addr;

	if (!p_hwfn->p_dev->chk_reg_fifo)
		goto out;

	/* ecore_rd() cannot be used here since it calls this function */
	bar_addr = ecore_set_ptt(p_hwfn, p_ptt, GRC_REG_TRACE_FIFO_VALID_DATA);
	is_empty = REG_RD(p_hwfn, bar_addr) == 0;

#ifndef ASIC_ONLY
	if (CHIP_REV_IS_SLOW(p_hwfn->p_dev))
		OSAL_UDELAY(100);
#endif

out:
	return is_empty;
}

void ecore_wr(struct ecore_hwfn *p_hwfn, struct ecore_ptt *p_ptt, u32 hw_addr,
	      u32 val)
{
	bool prev_fifo_err;
	u32 bar_addr;

	prev_fifo_err = !ecore_is_reg_fifo_empty(p_hwfn, p_ptt);

	bar_addr = ecore_set_ptt(p_hwfn, p_ptt, hw_addr);
	REG_WR(p_hwfn, bar_addr, val);
	DP_VERBOSE(p_hwfn, ECORE_MSG_HW,
		   "bar_addr 0x%x, hw_addr 0x%x, val 0x%x\n",
		   bar_addr, hw_addr, val);

#ifndef ASIC_ONLY
	if (CHIP_REV_IS_SLOW(p_hwfn->p_dev))
		OSAL_UDELAY(100);
#endif

	OSAL_WARN(!prev_fifo_err && !ecore_is_reg_fifo_empty(p_hwfn, p_ptt),
		  "reg_fifo error was caused by a call to ecore_wr(0x%x, 0x%x)\n",
		  hw_addr, val);
}

u32 ecore_rd(struct ecore_hwfn *p_hwfn, struct ecore_ptt *p_ptt, u32 hw_addr)
{
	bool prev_fifo_err;
	u32 bar_addr, val;

	prev_fifo_err = !ecore_is_reg_fifo_empty(p_hwfn, p_ptt);

	bar_addr = ecore_set_ptt(p_hwfn, p_ptt, hw_addr);
	val = REG_RD(p_hwfn, bar_addr);

	DP_VERBOSE(p_hwfn, ECORE_MSG_HW,
		   "bar_addr 0x%x, hw_addr 0x%x, val 0x%x\n",
		   bar_addr, hw_addr, val);

#ifndef ASIC_ONLY
	if (CHIP_REV_IS_SLOW(p_hwfn->p_dev))
		OSAL_UDELAY(100);
#endif

	OSAL_WARN(!prev_fifo_err && !ecore_is_reg_fifo_empty(p_hwfn, p_ptt),
		  "reg_fifo error was caused by a call to ecore_rd(0x%x)\n",
		  hw_addr);

	return val;
}

static void ecore_memcpy_hw(struct ecore_hwfn *p_hwfn,
			    struct ecore_ptt *p_ptt,
			    void *addr,
			    u32 hw_addr,
			    osal_size_t n,
			    bool to_device)
{
	u32 dw_count, *host_addr, hw_offset;
	osal_size_t quota, done = 0;
	u32 OSAL_IOMEM *reg_addr;

	while (done < n) {
		quota = OSAL_MIN_T(osal_size_t, n - done,
				   PXP_EXTERNAL_BAR_PF_WINDOW_SINGLE_SIZE);

		if (IS_PF(p_hwfn->p_dev)) {
			ecore_ptt_set_win(p_hwfn, p_ptt, hw_addr + done);
			hw_offset = ecore_ptt_get_bar_addr(p_ptt);
		} else {
			hw_offset = hw_addr + done;
		}

		dw_count = quota / 4;
		host_addr = (u32 *)((u8 *)addr + done);
		reg_addr = (u32 OSAL_IOMEM *)OSAL_REG_ADDR(p_hwfn, hw_offset);

		if (to_device)
			while (dw_count--)
				DIRECT_REG_WR(p_hwfn, reg_addr++, *host_addr++);
		else
			while (dw_count--)
				*host_addr++ = DIRECT_REG_RD(p_hwfn,
							     reg_addr++);

		done += quota;
	}
}

void ecore_memcpy_from(struct ecore_hwfn *p_hwfn,
		       struct ecore_ptt *p_ptt,
		       void *dest, u32 hw_addr, osal_size_t n)
{
	DP_VERBOSE(p_hwfn, ECORE_MSG_HW,
		   "hw_addr 0x%x, dest %p hw_addr 0x%x, size %lu\n",
		   hw_addr, dest, hw_addr, (unsigned long) n);

	ecore_memcpy_hw(p_hwfn, p_ptt, dest, hw_addr, n, false);
}

void ecore_memcpy_to(struct ecore_hwfn *p_hwfn,
		     struct ecore_ptt *p_ptt,
		     u32 hw_addr, void *src, osal_size_t n)
{
	DP_VERBOSE(p_hwfn, ECORE_MSG_HW,
		   "hw_addr 0x%x, hw_addr 0x%x, src %p size %lu\n",
		   hw_addr, hw_addr, src, (unsigned long)n);

	ecore_memcpy_hw(p_hwfn, p_ptt, src, hw_addr, n, true);
}

void ecore_fid_pretend(struct ecore_hwfn *p_hwfn,
		       struct ecore_ptt *p_ptt, u16 fid)
{
	u16 control = 0;

	SET_FIELD(control, PXP_PRETEND_CMD_IS_CONCRETE, 1);
	SET_FIELD(control, PXP_PRETEND_CMD_PRETEND_FUNCTION, 1);

	/* Every pretend undos previous pretends, including
	 * previous port pretend.
	 */
	SET_FIELD(control, PXP_PRETEND_CMD_PORT, 0);
	SET_FIELD(control, PXP_PRETEND_CMD_USE_PORT, 0);
	SET_FIELD(control, PXP_PRETEND_CMD_PRETEND_PORT, 1);

	if (!GET_FIELD(fid, PXP_CONCRETE_FID_VFVALID))
		fid = GET_FIELD(fid, PXP_CONCRETE_FID_PFID);

	p_ptt->pxp.pretend.control = OSAL_CPU_TO_LE16(control);
	p_ptt->pxp.pretend.fid.concrete_fid.fid = OSAL_CPU_TO_LE16(fid);

	REG_WR(p_hwfn,
	       ecore_ptt_config_addr(p_ptt) +
	       OFFSETOF(struct pxp_ptt_entry, pretend),
	       *(u32 *)&p_ptt->pxp.pretend);
}

void ecore_port_pretend(struct ecore_hwfn *p_hwfn,
			struct ecore_ptt *p_ptt, u8 port_id)
{
	u16 control = 0;

	SET_FIELD(control, PXP_PRETEND_CMD_PORT, port_id);
	SET_FIELD(control, PXP_PRETEND_CMD_USE_PORT, 1);
	SET_FIELD(control, PXP_PRETEND_CMD_PRETEND_PORT, 1);
	p_ptt->pxp.pretend.control = OSAL_CPU_TO_LE16(control);

	REG_WR(p_hwfn,
	       ecore_ptt_config_addr(p_ptt) +
	       OFFSETOF(struct pxp_ptt_entry, pretend),
	       *(u32 *)&p_ptt->pxp.pretend);
}

void ecore_port_unpretend(struct ecore_hwfn *p_hwfn,
			  struct ecore_ptt *p_ptt)
{
	u16 control = 0;

	SET_FIELD(control, PXP_PRETEND_CMD_PORT, 0);
	SET_FIELD(control, PXP_PRETEND_CMD_USE_PORT, 0);
	SET_FIELD(control, PXP_PRETEND_CMD_PRETEND_PORT, 1);

	p_ptt->pxp.pretend.control = OSAL_CPU_TO_LE16(control);

	REG_WR(p_hwfn,
	       ecore_ptt_config_addr(p_ptt) +
	       OFFSETOF(struct pxp_ptt_entry, pretend),
	       *(u32 *)&p_ptt->pxp.pretend);
}

u32 ecore_vfid_to_concrete(struct ecore_hwfn *p_hwfn, u8 vfid)
{
	u32 concrete_fid = 0;

	SET_FIELD(concrete_fid, PXP_CONCRETE_FID_PFID, p_hwfn->rel_pf_id);
	SET_FIELD(concrete_fid, PXP_CONCRETE_FID_VFID, vfid);
	SET_FIELD(concrete_fid, PXP_CONCRETE_FID_VFVALID, 1);

	return concrete_fid;
}

#if 0
/* Ecore HW lock
 * =============
 * Although the implemention is ready, today we don't have any flow that
 * utliizes said locks - and we want to keep it this way.
 * If this changes, this needs to be revisted.
 */
#define HW_LOCK_MAX_RETRIES 1000
enum _ecore_status_t ecore_hw_lock(struct ecore_hwfn		*p_hwfn,
				   struct ecore_ptt		*p_ptt,
				   u8                           resource,
				   bool				block)
{
	u32 cnt, lock_status, hw_lock_cntr_reg;
	enum _ecore_status_t ecore_status;

	/* Locate the proper lock register for this function.
	 * Note This code assumes all the H/W lock registers are sequential
	 * in memory.
	 */
	hw_lock_cntr_reg = MISCS_REG_DRIVER_CONTROL_0 +
			   p_hwfn->rel_pf_id *
			   MISCS_REG_DRIVER_CONTROL_0_SIZE * sizeof(u32);

	/* Validate that the resource is not already taken */
	lock_status = ecore_rd(p_hwfn, p_ptt, hw_lock_cntr_reg);

	if (lock_status & resource) {
		DP_NOTICE(p_hwfn, true,
			  "Resource already locked: lock_status=0x%x resource=0x%x\n",
			  lock_status, resource);

		return ECORE_BUSY;
	}

	/* Register for the lock */
	ecore_wr(p_hwfn, p_ptt, hw_lock_cntr_reg + sizeof(u32), resource);

	/* Try for 5 seconds every 5ms */
	for (cnt = 0; cnt < HW_LOCK_MAX_RETRIES; cnt++) {
		lock_status = ecore_rd(p_hwfn, p_ptt, hw_lock_cntr_reg);

		if (lock_status & resource)
			return ECORE_SUCCESS;

		if (!block) {
			ecore_status = ECORE_BUSY;
			break;
		}

		OSAL_MSLEEP(5);
	}

	if (cnt == HW_LOCK_MAX_RETRIES) {
		DP_NOTICE(p_hwfn, true, "Lock timeout resource=0x%x\n",
			  resource);
		ecore_status = ECORE_TIMEOUT;
	}

	/* Clear the pending request */
	ecore_wr(p_hwfn, p_ptt, hw_lock_cntr_reg, resource);

	return ecore_status;
}

enum _ecore_status_t ecore_hw_unlock(struct ecore_hwfn		*p_hwfn,
				     struct ecore_ptt		*p_ptt,
				     u8                         resource)
{
	u32 lock_status, hw_lock_cntr_reg;

	/* Locate the proper lock register for this function.
	 * Note This code assumes all the H/W lock registers are sequential
	 * in memory.
	 */
	hw_lock_cntr_reg = MISCS_REG_DRIVER_CONTROL_0 +
			   p_hwfn->rel_pf_id *
			   MISCS_REG_DRIVER_CONTROL_0_SIZE * sizeof(u32);

	/*  Validate that the resource is currently taken */
	lock_status = ecore_rd(p_hwfn, p_ptt, hw_lock_cntr_reg);

	if (!(lock_status & resource)) {
		DP_NOTICE(p_hwfn, true,
			  "resource 0x%x was not taken (lock status 0x%x)\n",
			  resource, lock_status);

		return ECORE_NODEV;
	}

	/* clear lock for resource */
	ecore_wr(p_hwfn, p_ptt, hw_lock_cntr_reg, resource);
	return ECORE_SUCCESS;
}
#endif /* HW locks logic */

/* DMAE */

#define ECORE_DMAE_FLAGS_IS_SET(params, flag)	\
	((params) != OSAL_NULL && ((params)->flags & ECORE_DMAE_FLAG_##flag))

static void ecore_dmae_opcode(struct ecore_hwfn	*p_hwfn,
			      const u8	is_src_type_grc,
			      const u8	is_dst_type_grc,
			      struct ecore_dmae_params *p_params)
{
	u8 src_pfid, dst_pfid, port_id;
	u16 opcode_b = 0;
	u32 opcode = 0;

	/* Whether the source is the PCIe or the GRC.
	 * 0- The source is the PCIe
	 * 1- The source is the GRC.
	 */
	opcode |= (is_src_type_grc ? DMAE_CMD_SRC_MASK_GRC
				   : DMAE_CMD_SRC_MASK_PCIE) <<
		  DMAE_CMD_SRC_SHIFT;
	src_pfid = ECORE_DMAE_FLAGS_IS_SET(p_params, PF_SRC) ?
		   p_params->src_pfid : p_hwfn->rel_pf_id;
	opcode |= (src_pfid & DMAE_CMD_SRC_PF_ID_MASK) <<
		  DMAE_CMD_SRC_PF_ID_SHIFT;

	/* The destination of the DMA can be: 0-None 1-PCIe 2-GRC 3-None */
	opcode |= (is_dst_type_grc ? DMAE_CMD_DST_MASK_GRC
				   : DMAE_CMD_DST_MASK_PCIE) <<
		  DMAE_CMD_DST_SHIFT;
	dst_pfid = ECORE_DMAE_FLAGS_IS_SET(p_params, PF_DST) ?
		   p_params->dst_pfid : p_hwfn->rel_pf_id;
	opcode |= (dst_pfid & DMAE_CMD_DST_PF_ID_MASK) <<
		  DMAE_CMD_DST_PF_ID_SHIFT;

	/* DMAE_E4_TODO need to check which value to specify here. */
	/* opcode |= (!b_complete_to_host)<< DMAE_CMD_C_DST_SHIFT;*/

	/* Whether to write a completion word to the completion destination:
	 * 0-Do not write a completion word
	 * 1-Write the completion word
	 */
	opcode |= DMAE_CMD_COMP_WORD_EN_MASK << DMAE_CMD_COMP_WORD_EN_SHIFT;
	opcode |= DMAE_CMD_SRC_ADDR_RESET_MASK <<
		  DMAE_CMD_SRC_ADDR_RESET_SHIFT;

	if (ECORE_DMAE_FLAGS_IS_SET(p_params, COMPLETION_DST))
		opcode |= 1 << DMAE_CMD_COMP_FUNC_SHIFT;

	/* swapping mode 3 - big endian there should be a define ifdefed in
	 * the HSI somewhere. Since it is currently
	 */
	opcode |= DMAE_CMD_ENDIANITY << DMAE_CMD_ENDIANITY_MODE_SHIFT;

	port_id = (ECORE_DMAE_FLAGS_IS_SET(p_params, PORT)) ?
		  p_params->port_id : p_hwfn->port_id;
	opcode |= port_id << DMAE_CMD_PORT_ID_SHIFT;

	/* reset source address in next go */
	opcode |= DMAE_CMD_SRC_ADDR_RESET_MASK <<
		  DMAE_CMD_SRC_ADDR_RESET_SHIFT;

	/* reset dest address in next go */
	opcode |= DMAE_CMD_DST_ADDR_RESET_MASK <<
		  DMAE_CMD_DST_ADDR_RESET_SHIFT;

	/* SRC/DST VFID: all 1's - pf, otherwise VF id */
	if (ECORE_DMAE_FLAGS_IS_SET(p_params, VF_SRC)) {
		opcode |= (1 << DMAE_CMD_SRC_VF_ID_VALID_SHIFT);
		opcode_b |= (p_params->src_vfid <<  DMAE_CMD_SRC_VF_ID_SHIFT);
	} else {
		opcode_b |= (DMAE_CMD_SRC_VF_ID_MASK <<
			     DMAE_CMD_SRC_VF_ID_SHIFT);
	}
	if (ECORE_DMAE_FLAGS_IS_SET(p_params, VF_DST)) {
		opcode |= 1 << DMAE_CMD_DST_VF_ID_VALID_SHIFT;
		opcode_b |= p_params->dst_vfid << DMAE_CMD_DST_VF_ID_SHIFT;
	} else {
		opcode_b |= DMAE_CMD_DST_VF_ID_MASK <<
			    DMAE_CMD_DST_VF_ID_SHIFT;
	}

	p_hwfn->dmae_info.p_dmae_cmd->opcode = OSAL_CPU_TO_LE32(opcode);
	p_hwfn->dmae_info.p_dmae_cmd->opcode_b = OSAL_CPU_TO_LE16(opcode_b);
}

static u32 ecore_dmae_idx_to_go_cmd(u8 idx)
{
	OSAL_BUILD_BUG_ON((DMAE_REG_GO_C31 - DMAE_REG_GO_C0) !=
			  31 * 4);

	/* All the DMAE 'go' registers form an array in internal memory */
	return DMAE_REG_GO_C0 + (idx << 2);
}

static enum _ecore_status_t ecore_dmae_post_command(struct ecore_hwfn *p_hwfn,
						    struct ecore_ptt *p_ptt)
{
	struct dmae_cmd *p_command = p_hwfn->dmae_info.p_dmae_cmd;
	u8 idx_cmd = p_hwfn->dmae_info.channel, i;
	enum _ecore_status_t ecore_status = ECORE_SUCCESS;

	/* verify address is not OSAL_NULL */
	if ((((!p_command->dst_addr_lo) && (!p_command->dst_addr_hi)) ||
	     ((!p_command->src_addr_lo) && (!p_command->src_addr_hi)))) {
		DP_NOTICE(p_hwfn, true,
			  "source or destination address 0 idx_cmd=%d\n"
			  "opcode = [0x%08x,0x%04x] len=0x%x src=0x%x:%x dst=0x%x:%x\n",
			  idx_cmd,
			  OSAL_LE32_TO_CPU(p_command->opcode),
			  OSAL_LE16_TO_CPU(p_command->opcode_b),
			  OSAL_LE16_TO_CPU(p_command->length_dw),
			  OSAL_LE32_TO_CPU(p_command->src_addr_hi),
			  OSAL_LE32_TO_CPU(p_command->src_addr_lo),
			  OSAL_LE32_TO_CPU(p_command->dst_addr_hi),
			  OSAL_LE32_TO_CPU(p_command->dst_addr_lo));

		return ECORE_INVAL;
	}

	DP_VERBOSE(p_hwfn, ECORE_MSG_HW,
		   "Posting DMAE command [idx %d]: opcode = [0x%08x,0x%04x] len=0x%x src=0x%x:%x dst=0x%x:%x\n",
		   idx_cmd,
		   OSAL_LE32_TO_CPU(p_command->opcode),
		   OSAL_LE16_TO_CPU(p_command->opcode_b),
		   OSAL_LE16_TO_CPU(p_command->length_dw),
		   OSAL_LE32_TO_CPU(p_command->src_addr_hi),
		   OSAL_LE32_TO_CPU(p_command->src_addr_lo),
		   OSAL_LE32_TO_CPU(p_command->dst_addr_hi),
		   OSAL_LE32_TO_CPU(p_command->dst_addr_lo));

	/* Copy the command to DMAE - need to do it before every call
	 * for source/dest address no reset.
	 * The number of commands have been increased to 16 (previous was 14)
	 * The first 9 DWs are the command registers, the 10 DW is the
	 * GO register, and
	 * the rest are result registers (which are read only by the client).
	 */
	for (i = 0; i < DMAE_CMD_SIZE; i++) {
		u32 data = (i < DMAE_CMD_SIZE_TO_FILL) ?
			    *(((u32 *)p_command) + i) : 0;

		ecore_wr(p_hwfn, p_ptt,
			 DMAE_REG_CMD_MEM +
			 (idx_cmd * DMAE_CMD_SIZE * sizeof(u32)) +
			 (i * sizeof(u32)), data);
	}

	ecore_wr(p_hwfn, p_ptt,
		 ecore_dmae_idx_to_go_cmd(idx_cmd),
		 DMAE_GO_VALUE);

	return ecore_status;
}

enum _ecore_status_t ecore_dmae_info_alloc(struct ecore_hwfn *p_hwfn)
{
	dma_addr_t *p_addr = &p_hwfn->dmae_info.completion_word_phys_addr;
	struct dmae_cmd **p_cmd = &p_hwfn->dmae_info.p_dmae_cmd;
	u32 **p_buff = &p_hwfn->dmae_info.p_intermediate_buffer;
	u32 **p_comp = &p_hwfn->dmae_info.p_completion_word;

	*p_comp = OSAL_DMA_ALLOC_COHERENT(p_hwfn->p_dev, p_addr, sizeof(u32));
	if (*p_comp == OSAL_NULL) {
		DP_NOTICE(p_hwfn, false,
			  "Failed to allocate `p_completion_word'\n");
		goto err;
	}

	p_addr =  &p_hwfn->dmae_info.dmae_cmd_phys_addr;
	*p_cmd = OSAL_DMA_ALLOC_COHERENT(p_hwfn->p_dev, p_addr,
					 sizeof(struct dmae_cmd));
	if (*p_cmd == OSAL_NULL) {
		DP_NOTICE(p_hwfn, false,
			  "Failed to allocate `struct dmae_cmd'\n");
		goto err;
	}

	p_addr = &p_hwfn->dmae_info.intermediate_buffer_phys_addr;
	*p_buff = OSAL_DMA_ALLOC_COHERENT(p_hwfn->p_dev, p_addr,
					  sizeof(u32) * DMAE_MAX_RW_SIZE);
	if (*p_buff == OSAL_NULL) {
		DP_NOTICE(p_hwfn, false,
			  "Failed to allocate `intermediate_buffer'\n");
		goto err;
	}

		p_hwfn->dmae_info.channel = p_hwfn->rel_pf_id;
		p_hwfn->dmae_info.b_mem_ready = true;

	return ECORE_SUCCESS;
err:
	ecore_dmae_info_free(p_hwfn);
	return ECORE_NOMEM;
}

void ecore_dmae_info_free(struct ecore_hwfn *p_hwfn)
{
	dma_addr_t p_phys;

	OSAL_SPIN_LOCK(&p_hwfn->dmae_info.lock);
	p_hwfn->dmae_info.b_mem_ready = false;
	OSAL_SPIN_UNLOCK(&p_hwfn->dmae_info.lock);

	if (p_hwfn->dmae_info.p_completion_word != OSAL_NULL) {
		p_phys = p_hwfn->dmae_info.completion_word_phys_addr;
		OSAL_DMA_FREE_COHERENT(p_hwfn->p_dev,
				       p_hwfn->dmae_info.p_completion_word,
				       p_phys, sizeof(u32));
		p_hwfn->dmae_info.p_completion_word = OSAL_NULL;
	}

	if (p_hwfn->dmae_info.p_dmae_cmd != OSAL_NULL) {
		p_phys = p_hwfn->dmae_info.dmae_cmd_phys_addr;
		OSAL_DMA_FREE_COHERENT(p_hwfn->p_dev,
				       p_hwfn->dmae_info.p_dmae_cmd,
				       p_phys, sizeof(struct dmae_cmd));
		p_hwfn->dmae_info.p_dmae_cmd = OSAL_NULL;
	}

	if (p_hwfn->dmae_info.p_intermediate_buffer != OSAL_NULL) {
		p_phys = p_hwfn->dmae_info.intermediate_buffer_phys_addr;
		OSAL_DMA_FREE_COHERENT(p_hwfn->p_dev,
				       p_hwfn->dmae_info.p_intermediate_buffer,
				       p_phys, sizeof(u32) * DMAE_MAX_RW_SIZE);
		p_hwfn->dmae_info.p_intermediate_buffer = OSAL_NULL;
	}
}

static enum _ecore_status_t
ecore_dmae_operation_wait(struct ecore_hwfn *p_hwfn)
{
	u32 wait_cnt_limit = 10000, wait_cnt = 0;
	enum _ecore_status_t ecore_status = ECORE_SUCCESS;

#ifndef ASIC_ONLY
	u32 factor = (CHIP_REV_IS_EMUL(p_hwfn->p_dev) ?
		      ECORE_EMUL_FACTOR :
		      (CHIP_REV_IS_FPGA(p_hwfn->p_dev) ?
		       ECORE_FPGA_FACTOR : 1));

	wait_cnt_limit *= factor;
#endif

	/* DMAE_E4_TODO : TODO check if we have to call any other function
	 * other than BARRIER to sync the completion_word since we are not
	 * using the volatile keyword for this
	 */
	OSAL_BARRIER(p_hwfn->p_dev);
	while (*p_hwfn->dmae_info.p_completion_word != DMAE_COMPLETION_VAL) {
		OSAL_UDELAY(DMAE_MIN_WAIT_TIME);
		if (++wait_cnt > wait_cnt_limit) {
			DP_NOTICE(p_hwfn->p_dev, ECORE_MSG_HW,
				  "Timed-out waiting for operation to complete. Completion word is 0x%08x expected 0x%08x.\n",
				  *(p_hwfn->dmae_info.p_completion_word),
				  DMAE_COMPLETION_VAL);
			ecore_status = ECORE_TIMEOUT;
			break;
		}

		/* to sync the completion_word since we are not
		 * using the volatile keyword for p_completion_word
		 */
		OSAL_BARRIER(p_hwfn->p_dev);
	}

	if (ecore_status == ECORE_SUCCESS)
		*p_hwfn->dmae_info.p_completion_word = 0;

	return ecore_status;
}

static enum _ecore_status_t ecore_dmae_execute_sub_operation(struct ecore_hwfn *p_hwfn,
							     struct ecore_ptt *p_ptt,
							     u64 src_addr,
							     u64 dst_addr,
							     u8 src_type,
							     u8 dst_type,
							     u32 length_dw)
{
	dma_addr_t phys = p_hwfn->dmae_info.intermediate_buffer_phys_addr;
	struct dmae_cmd *cmd = p_hwfn->dmae_info.p_dmae_cmd;
	enum _ecore_status_t ecore_status = ECORE_SUCCESS;

	switch (src_type) {
	case ECORE_DMAE_ADDRESS_GRC:
	case ECORE_DMAE_ADDRESS_HOST_PHYS:
		cmd->src_addr_hi = OSAL_CPU_TO_LE32(DMA_HI(src_addr));
		cmd->src_addr_lo = OSAL_CPU_TO_LE32(DMA_LO(src_addr));
		break;
	/* for virtual source addresses we use the intermediate buffer. */
	case ECORE_DMAE_ADDRESS_HOST_VIRT:
		cmd->src_addr_hi = OSAL_CPU_TO_LE32(DMA_HI(phys));
		cmd->src_addr_lo = OSAL_CPU_TO_LE32(DMA_LO(phys));
		OSAL_MEMCPY(&(p_hwfn->dmae_info.p_intermediate_buffer[0]),
			    (void *)(osal_uintptr_t)src_addr,
			    length_dw * sizeof(u32));
		break;
	default:
		return ECORE_INVAL;
	}

	switch (dst_type) {
	case ECORE_DMAE_ADDRESS_GRC:
	case ECORE_DMAE_ADDRESS_HOST_PHYS:
		cmd->dst_addr_hi = OSAL_CPU_TO_LE32(DMA_HI(dst_addr));
		cmd->dst_addr_lo = OSAL_CPU_TO_LE32(DMA_LO(dst_addr));
		break;
	/* for virtual destination addresses we use the intermediate buffer. */
	case ECORE_DMAE_ADDRESS_HOST_VIRT:
		cmd->dst_addr_hi = OSAL_CPU_TO_LE32(DMA_HI(phys));
		cmd->dst_addr_lo = OSAL_CPU_TO_LE32(DMA_LO(phys));
		break;
	default:
		return ECORE_INVAL;
	}

	cmd->length_dw = OSAL_CPU_TO_LE16((u16)length_dw);
#ifndef __EXTRACT__LINUX__
	if (src_type == ECORE_DMAE_ADDRESS_HOST_VIRT ||
	    src_type == ECORE_DMAE_ADDRESS_HOST_PHYS)
		OSAL_DMA_SYNC(p_hwfn->p_dev,
			      (void *)HILO_U64(cmd->src_addr_hi,
					       cmd->src_addr_lo),
			      length_dw * sizeof(u32), false);
#endif

	ecore_dmae_post_command(p_hwfn, p_ptt);

	ecore_status = ecore_dmae_operation_wait(p_hwfn);

#ifndef __EXTRACT__LINUX__
	/* TODO - is it true ? */
	if (src_type == ECORE_DMAE_ADDRESS_HOST_VIRT ||
	    src_type == ECORE_DMAE_ADDRESS_HOST_PHYS)
		OSAL_DMA_SYNC(p_hwfn->p_dev,
			      (void *)HILO_U64(cmd->src_addr_hi,
					       cmd->src_addr_lo),
			      length_dw * sizeof(u32), true);
#endif

	if (ecore_status != ECORE_SUCCESS) {
		DP_NOTICE(p_hwfn, ECORE_MSG_HW,
			  "Wait Failed. source_addr 0x%llx, grc_addr 0x%llx, size_in_dwords 0x%x, intermediate buffer 0x%llx.\n",
			  (unsigned long long)src_addr, (unsigned long long)dst_addr, length_dw,
			  (unsigned long long)p_hwfn->dmae_info.intermediate_buffer_phys_addr);
		return ecore_status;
	}

	if (dst_type == ECORE_DMAE_ADDRESS_HOST_VIRT)
		OSAL_MEMCPY((void *)(osal_uintptr_t)(dst_addr),
			    &p_hwfn->dmae_info.p_intermediate_buffer[0],
			    length_dw * sizeof(u32));

	return ECORE_SUCCESS;
}

static enum _ecore_status_t ecore_dmae_execute_command(struct ecore_hwfn *p_hwfn,
						       struct ecore_ptt *p_ptt,
						       u64 src_addr, u64 dst_addr,
						       u8 src_type, u8 dst_type,
						       u32 size_in_dwords,
						       struct ecore_dmae_params *p_params)
{
	dma_addr_t phys = p_hwfn->dmae_info.completion_word_phys_addr;
	u16 length_cur = 0, i = 0, cnt_split = 0, length_mod = 0;
	struct dmae_cmd *cmd = p_hwfn->dmae_info.p_dmae_cmd;
	u64 src_addr_split = 0, dst_addr_split = 0;
	u16 length_limit = DMAE_MAX_RW_SIZE;
	enum _ecore_status_t ecore_status = ECORE_SUCCESS;
	u32 offset = 0;

	if (!p_hwfn->dmae_info.b_mem_ready) {
		DP_VERBOSE(p_hwfn, ECORE_MSG_HW,
			   "No buffers allocated. Avoid DMAE transaction [{src: addr 0x%llx, type %d}, {dst: addr 0x%llx, type %d}, size %d].\n",
			   (unsigned long long)src_addr, src_type, (unsigned long long)dst_addr, dst_type,
			   size_in_dwords);
		return ECORE_NOMEM;
	}

	if (p_hwfn->p_dev->recov_in_prog) {
		DP_VERBOSE(p_hwfn, ECORE_MSG_HW,
			   "Recovery is in progress. Avoid DMAE transaction [{src: addr 0x%llx, type %d}, {dst: addr 0x%llx, type %d}, size %d].\n",
			   (unsigned long long)src_addr, src_type, (unsigned long long)dst_addr, dst_type,
			   size_in_dwords);
		/* Return success to let the flow to be completed successfully
		 * w/o any error handling.
		 */
		return ECORE_SUCCESS;
	}

	if (!cmd) {
		DP_NOTICE(p_hwfn, true,
			  "ecore_dmae_execute_sub_operation failed. Invalid state. source_addr 0x%llx, destination addr 0x%llx, size_in_dwords 0x%x\n",
			  (unsigned long long)src_addr, (unsigned long long)dst_addr, length_cur);
		return ECORE_INVAL;
	}

	ecore_dmae_opcode(p_hwfn,
			  (src_type == ECORE_DMAE_ADDRESS_GRC),
			  (dst_type == ECORE_DMAE_ADDRESS_GRC),
			  p_params);

	cmd->comp_addr_lo = OSAL_CPU_TO_LE32(DMA_LO(phys));
	cmd->comp_addr_hi = OSAL_CPU_TO_LE32(DMA_HI(phys));
	cmd->comp_val = OSAL_CPU_TO_LE32(DMAE_COMPLETION_VAL);

	/* Check if the grc_addr is valid like < MAX_GRC_OFFSET */
	cnt_split = size_in_dwords / length_limit;
	length_mod = size_in_dwords % length_limit;

	src_addr_split = src_addr;
	dst_addr_split = dst_addr;

	for (i = 0; i <= cnt_split; i++) {
		offset = length_limit * i;

		if (!ECORE_DMAE_FLAGS_IS_SET(p_params, RW_REPL_SRC)) {
			if (src_type == ECORE_DMAE_ADDRESS_GRC)
				src_addr_split = src_addr + offset;
			else
				src_addr_split = src_addr + (offset*4);
		}

		if (dst_type == ECORE_DMAE_ADDRESS_GRC)
			dst_addr_split = dst_addr + offset;
		else
			dst_addr_split = dst_addr + (offset*4);

		length_cur = (cnt_split == i) ? length_mod : length_limit;

		/* might be zero on last iteration */
		if (!length_cur)
			continue;

		ecore_status = ecore_dmae_execute_sub_operation(p_hwfn,
								p_ptt,
								src_addr_split,
								dst_addr_split,
								src_type,
								dst_type,
								length_cur);
		if (ecore_status != ECORE_SUCCESS) {
			DP_NOTICE(p_hwfn, false,
				  "ecore_dmae_execute_sub_operation Failed with error 0x%x. source_addr 0x%llx, destination addr 0x%llx, size_in_dwords 0x%x\n",
				  ecore_status, (unsigned long long)src_addr, (unsigned long long)dst_addr, length_cur);

			ecore_hw_err_notify(p_hwfn, ECORE_HW_ERR_DMAE_FAIL);
			break;
		}
	}

	return ecore_status;
}

enum _ecore_status_t ecore_dmae_host2grc(struct ecore_hwfn *p_hwfn,
					 struct ecore_ptt *p_ptt,
					 u64 source_addr,
					 u32 grc_addr,
					 u32 size_in_dwords,
					 struct ecore_dmae_params *p_params)
{
	u32 grc_addr_in_dw = grc_addr / sizeof(u32);
	enum _ecore_status_t rc;

	OSAL_SPIN_LOCK(&p_hwfn->dmae_info.lock);

	rc = ecore_dmae_execute_command(p_hwfn, p_ptt, source_addr,
					grc_addr_in_dw,
					ECORE_DMAE_ADDRESS_HOST_VIRT,
					ECORE_DMAE_ADDRESS_GRC,
					size_in_dwords, p_params);

	OSAL_SPIN_UNLOCK(&p_hwfn->dmae_info.lock);

	return rc;
}

enum _ecore_status_t ecore_dmae_grc2host(struct ecore_hwfn *p_hwfn,
					 struct ecore_ptt *p_ptt,
					 u32 grc_addr,
					 dma_addr_t dest_addr,
					 u32 size_in_dwords,
					 struct ecore_dmae_params *p_params)
{
	u32 grc_addr_in_dw = grc_addr / sizeof(u32);
	enum _ecore_status_t rc;

	OSAL_SPIN_LOCK(&(p_hwfn->dmae_info.lock));

	rc = ecore_dmae_execute_command(p_hwfn, p_ptt, grc_addr_in_dw,
					dest_addr, ECORE_DMAE_ADDRESS_GRC,
					ECORE_DMAE_ADDRESS_HOST_VIRT,
					size_in_dwords, p_params);

	OSAL_SPIN_UNLOCK(&(p_hwfn->dmae_info.lock));

	return rc;
}

enum _ecore_status_t ecore_dmae_host2host(struct ecore_hwfn *p_hwfn,
					  struct ecore_ptt *p_ptt,
					  dma_addr_t source_addr,
					  dma_addr_t dest_addr,
					  u32 size_in_dwords,
					  struct ecore_dmae_params *p_params)
{
	enum _ecore_status_t rc;

	OSAL_SPIN_LOCK(&p_hwfn->dmae_info.lock);

	rc = ecore_dmae_execute_command(p_hwfn, p_ptt, source_addr,
					dest_addr,
					ECORE_DMAE_ADDRESS_HOST_PHYS,
					ECORE_DMAE_ADDRESS_HOST_PHYS,
					size_in_dwords,
					p_params);

	OSAL_SPIN_UNLOCK(&p_hwfn->dmae_info.lock);

	return rc;
}

void ecore_hw_err_notify(struct ecore_hwfn *p_hwfn,
			 enum ecore_hw_err_type err_type)
{
	/* Fan failure cannot be masked by handling of another HW error */
	if (p_hwfn->p_dev->recov_in_prog && err_type != ECORE_HW_ERR_FAN_FAIL) {
		DP_VERBOSE(p_hwfn, ECORE_MSG_DRV,
			   "Recovery is in progress. Avoid notifying about HW error %d.\n",
			   err_type);
		return;
	}

	OSAL_HW_ERROR_OCCURRED(p_hwfn, err_type);
}

enum _ecore_status_t ecore_dmae_sanity(struct ecore_hwfn *p_hwfn,
				       struct ecore_ptt *p_ptt,
				       const char *phase)
{
	u32 size = OSAL_PAGE_SIZE / 2, val;
	enum _ecore_status_t rc = ECORE_SUCCESS;
	dma_addr_t p_phys;
	void *p_virt;
	u32 *p_tmp;

	p_virt = OSAL_DMA_ALLOC_COHERENT(p_hwfn->p_dev, &p_phys, 2 * size);
	if (!p_virt) {
		DP_NOTICE(p_hwfn, false,
			  "DMAE sanity [%s]: failed to allocate memory\n",
			  phase);
		return ECORE_NOMEM;
	}

	/* Fill the bottom half of the allocated memory with a known pattern */
	for (p_tmp = (u32 *)p_virt;
	     p_tmp < (u32 *)((u8 *)p_virt + size);
	     p_tmp++) {
		/* Save the address itself as the value */
		val = (u32)(osal_uintptr_t)p_tmp;
		*p_tmp = val;
	}

	/* Zero the top half of the allocated memory */
	OSAL_MEM_ZERO((u8 *)p_virt + size, size);

	DP_VERBOSE(p_hwfn, ECORE_MSG_SP,
		   "DMAE sanity [%s]: src_addr={phys 0x%llx, virt %p}, dst_addr={phys 0x%llx, virt %p}, size 0x%x\n",
		   phase, (unsigned long long)p_phys, p_virt,
		   (unsigned long long)(p_phys + size), (u8 *)p_virt + size,
		   size);

	rc = ecore_dmae_host2host(p_hwfn, p_ptt, p_phys, p_phys + size,
				  size / 4 /* size_in_dwords */,
				  OSAL_NULL /* default parameters */);
	if (rc != ECORE_SUCCESS) {
		DP_NOTICE(p_hwfn, false,
			  "DMAE sanity [%s]: ecore_dmae_host2host() failed. rc = %d.\n",
			  phase, rc);
		goto out;
	}

	/* Verify that the top half of the allocated memory has the pattern */
	for (p_tmp = (u32 *)((u8 *)p_virt + size);
	     p_tmp < (u32 *)((u8 *)p_virt + (2 * size));
	     p_tmp++) {
		/* The corresponding address in the bottom half */
		val = (u32)(osal_uintptr_t)p_tmp - size;

		if (*p_tmp != val) {
			DP_NOTICE(p_hwfn, false,
				  "DMAE sanity [%s]: addr={phys 0x%llx, virt %p}, read_val 0x%08x, expected_val 0x%08x\n",
				  phase,
				  (unsigned long long)(p_phys + (u32)((u8 *)p_tmp - (u8 *)p_virt)),
				  p_tmp, *p_tmp, val);
			rc = ECORE_UNKNOWN_ERROR;
			goto out;
		}
	}

out:
	OSAL_DMA_FREE_COHERENT(p_hwfn->p_dev, p_virt, p_phys, 2 * size);
	return rc;
}

void ecore_ppfid_wr(struct ecore_hwfn *p_hwfn, struct ecore_ptt *p_ptt,
		    u8 abs_ppfid, u32 hw_addr, u32 val)
{
	u8 pfid = ECORE_PFID_BY_PPFID(p_hwfn, abs_ppfid);

	ecore_fid_pretend(p_hwfn, p_ptt,
			  pfid << PXP_PRETEND_CONCRETE_FID_PFID_SHIFT);
	ecore_wr(p_hwfn, p_ptt, hw_addr, val);
	ecore_fid_pretend(p_hwfn, p_ptt,
			  p_hwfn->rel_pf_id <<
			  PXP_PRETEND_CONCRETE_FID_PFID_SHIFT);
}

u32 ecore_ppfid_rd(struct ecore_hwfn *p_hwfn, struct ecore_ptt *p_ptt,
		   u8 abs_ppfid, u32 hw_addr)
{
	u8 pfid = ECORE_PFID_BY_PPFID(p_hwfn, abs_ppfid);
	u32 val;

	ecore_fid_pretend(p_hwfn, p_ptt,
			  pfid << PXP_PRETEND_CONCRETE_FID_PFID_SHIFT);
	val = ecore_rd(p_hwfn, p_ptt, hw_addr);
	ecore_fid_pretend(p_hwfn, p_ptt,
			  p_hwfn->rel_pf_id <<
			  PXP_PRETEND_CONCRETE_FID_PFID_SHIFT);

	return val;
}

#ifdef _NTDDK_
#pragma warning(pop)
#endif
