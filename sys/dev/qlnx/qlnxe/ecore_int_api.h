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

#ifndef __ECORE_INT_API_H__
#define __ECORE_INT_API_H__

#include "common_hsi.h"

#ifndef __EXTRACT__LINUX__
#define ECORE_SB_IDX		0x0002

#define RX_PI		0
#define TX_PI(tc)	(RX_PI + 1 + tc)

#ifndef ECORE_INT_MODE
#define ECORE_INT_MODE
enum ecore_int_mode {
	ECORE_INT_MODE_INTA,
	ECORE_INT_MODE_MSIX,
	ECORE_INT_MODE_MSI,
	ECORE_INT_MODE_POLL,
};
#endif

struct ecore_sb_info {
	struct status_block_e4 *sb_virt;
	dma_addr_t sb_phys;
	u32 sb_ack; /* Last given ack */
	u16 igu_sb_id;
	void OSAL_IOMEM *igu_addr;
	u8 flags;
#define ECORE_SB_INFO_INIT 	0x1
#define ECORE_SB_INFO_SETUP 	0x2

#ifdef ECORE_CONFIG_DIRECT_HWFN
	struct ecore_hwfn *p_hwfn;
#endif
	struct ecore_dev *p_dev;
};

struct ecore_sb_info_dbg {
	u32 igu_prod;
	u32 igu_cons;
	u16 pi[PIS_PER_SB_E4];
};

struct ecore_sb_cnt_info {
	/* Original, current, and free SBs for PF */
	int orig;
	int cnt;
	int free_cnt;

	/* Original, current and free SBS for child VFs */
	int iov_orig;
	int iov_cnt;
	int free_cnt_iov;
};

static OSAL_INLINE u16 ecore_sb_update_sb_idx(struct ecore_sb_info *sb_info)
{
	u32 prod = 0;
	u16 rc   = 0;

	// barrier(); /* status block is written to by the chip */
	// FIXME: need some sort of barrier.
	prod = OSAL_LE32_TO_CPU(sb_info->sb_virt->prod_index) &
	       STATUS_BLOCK_E4_PROD_INDEX_MASK;
	if (sb_info->sb_ack != prod) {
		sb_info->sb_ack = prod;
		rc |= ECORE_SB_IDX;
	}

	OSAL_MMIOWB(sb_info->p_dev);
	return rc;
}

/**
 * @brief This function creates an update command for interrupts that is
 *        written to the IGU.
 *
 * @param sb_info 	- This is the structure allocated and
 *      	   initialized per status block. Assumption is
 *      	   that it was initialized using ecore_sb_init
 * @param int_cmd 	- Enable/Disable/Nop
 * @param upd_flg 	- whether igu consumer should be
 *      	   updated.
 *
 * @return OSAL_INLINE void
 */
static OSAL_INLINE void ecore_sb_ack(struct ecore_sb_info *sb_info,
				     enum igu_int_cmd int_cmd, u8 upd_flg)
{
	struct igu_prod_cons_update igu_ack = { 0 };

#ifndef ECORE_CONFIG_DIRECT_HWFN
	u32 val;
#endif

#ifndef LINUX_REMOVE
	if (sb_info->p_dev->int_mode == ECORE_INT_MODE_POLL)
		return;
#endif
	igu_ack.sb_id_and_flags =
		 OSAL_CPU_TO_LE32((sb_info->sb_ack <<
		 IGU_PROD_CONS_UPDATE_SB_INDEX_SHIFT) |
		 (upd_flg << IGU_PROD_CONS_UPDATE_UPDATE_FLAG_SHIFT) |
		 (int_cmd << IGU_PROD_CONS_UPDATE_ENABLE_INT_SHIFT) |
		 (IGU_SEG_ACCESS_REG <<
		 IGU_PROD_CONS_UPDATE_SEGMENT_ACCESS_SHIFT));

#ifdef ECORE_CONFIG_DIRECT_HWFN
	DIRECT_REG_WR(sb_info->p_hwfn, sb_info->igu_addr,
		      igu_ack.sb_id_and_flags);
#else
	val = OSAL_LE32_TO_CPU(igu_ack.sb_id_and_flags);
	DIRECT_REG_WR(OSAL_NULL, sb_info->igu_addr, val);
#endif
	/* Both segments (interrupts & acks) are written to same place address;
	 * Need to guarantee all commands will be received (in-order) by HW.
	 */
	OSAL_MMIOWB(sb_info->p_dev);
	OSAL_BARRIER(sb_info->p_dev);
}

#ifdef ECORE_CONFIG_DIRECT_HWFN
static OSAL_INLINE void __internal_ram_wr(struct ecore_hwfn *p_hwfn,
					  void OSAL_IOMEM *addr,
					  int size, u32 *data)
#else
static OSAL_INLINE void __internal_ram_wr(void *p_hwfn,
					  void OSAL_IOMEM *addr,
					  int size, u32 *data)

#endif
{
	unsigned int i;

	for (i = 0; i < size / sizeof(*data); i++)
		DIRECT_REG_WR(p_hwfn, &((u32 OSAL_IOMEM *)addr)[i], data[i]);
}

#ifdef ECORE_CONFIG_DIRECT_HWFN
static OSAL_INLINE void internal_ram_wr(struct ecore_hwfn *p_hwfn,
					void OSAL_IOMEM *addr,
					int size, u32 *data)
{
	__internal_ram_wr(p_hwfn, addr, size, data);
}
#else
static OSAL_INLINE void internal_ram_wr(void OSAL_IOMEM *addr,
					int size, u32 *data)
{
	__internal_ram_wr(OSAL_NULL, addr, size, data);
}
#endif
#endif

struct ecore_hwfn;
struct ecore_ptt;

enum ecore_coalescing_fsm {
	ECORE_COAL_RX_STATE_MACHINE,
	ECORE_COAL_TX_STATE_MACHINE
}; 

/**
 * @brief ecore_int_cau_conf_pi - configure cau for a given
 *        status block
 *
 * @param p_hwfn
 * @param p_ptt
 * @param p_sb
 * @param pi_index
 * @param state
 * @param timeset
 */
void ecore_int_cau_conf_pi(struct ecore_hwfn		*p_hwfn,
			   struct ecore_ptt		*p_ptt,
			   struct ecore_sb_info		*p_sb,
			   u32				pi_index,
			   enum ecore_coalescing_fsm	coalescing_fsm,
			   u8				timeset);

/**
 * @brief ecore_int_igu_enable_int - enable device interrupts
 *
 * @param p_hwfn
 * @param p_ptt
 * @param int_mode - interrupt mode to use
 */
void ecore_int_igu_enable_int(struct ecore_hwfn *p_hwfn,
			      struct ecore_ptt *p_ptt,
			      enum ecore_int_mode int_mode);

/**
 * @brief ecore_int_igu_disable_int - disable device interrupts
 *
 * @param p_hwfn
 * @param p_ptt
 */
void ecore_int_igu_disable_int(struct ecore_hwfn *p_hwfn,
			       struct ecore_ptt	*p_ptt); 

/**
 * @brief ecore_int_igu_read_sisr_reg - Reads the single isr multiple dpc
 *        register from igu.
 *
 * @param p_hwfn
 *
 * @return u64
 */
u64 ecore_int_igu_read_sisr_reg(struct ecore_hwfn *p_hwfn); 

#define ECORE_SP_SB_ID 0xffff

/**
 * @brief ecore_int_sb_init - Initializes the sb_info structure.
 *
 * once the structure is initialized it can be passed to sb related functions.
 *
 * @param p_hwfn
 * @param p_ptt
 * @param sb_info	points to an uninitialized (but
 *			allocated) sb_info structure
 * @param sb_virt_addr
 * @param sb_phy_addr
 * @param sb_id		the sb_id to be used (zero based in driver)
 *			should use ECORE_SP_SB_ID for SP Status block
 *
 * @return enum _ecore_status_t
 */
enum _ecore_status_t ecore_int_sb_init(struct ecore_hwfn	*p_hwfn,
				       struct ecore_ptt		*p_ptt,
				       struct ecore_sb_info	*sb_info,
				       void			*sb_virt_addr,
				       dma_addr_t		sb_phy_addr,
				       u16			sb_id);
/**
 * @brief ecore_int_sb_setup - Setup the sb.
 *
 * @param p_hwfn
 * @param p_ptt
 * @param sb_info	initialized sb_info structure
 */
void ecore_int_sb_setup(
		struct ecore_hwfn	*p_hwfn,
		struct ecore_ptt		*p_ptt,
		struct ecore_sb_info	*sb_info);

/**
 * @brief ecore_int_sb_release - releases the sb_info structure.
 *
 * once the structure is released, it's memory can be freed
 *
 * @param p_hwfn
 * @param sb_info	points to an allocated sb_info structure
 * @param sb_id		the sb_id to be used (zero based in driver)
 *			should never be equal to ECORE_SP_SB_ID
 *			(SP Status block)
 *
 * @return enum _ecore_status_t
 */
enum _ecore_status_t ecore_int_sb_release(struct ecore_hwfn	*p_hwfn,
					  struct ecore_sb_info	*sb_info,
					  u16			sb_id);

/**
 * @brief ecore_int_sp_dpc - To be called when an interrupt is received on the
 *        default status block.
 *
 * @param p_hwfn - pointer to hwfn
 *
 */
void ecore_int_sp_dpc(osal_int_ptr_t hwfn_cookie);

/**
 * @brief ecore_int_get_num_sbs - get the number of status 
 *        blocks configured for this funciton in the igu.
 * 
 * @param p_hwfn
 * @param p_sb_cnt_info
 * 
 * @return
 */
void ecore_int_get_num_sbs(struct ecore_hwfn	    *p_hwfn,
			   struct ecore_sb_cnt_info *p_sb_cnt_info);

/**
 * @brief ecore_int_disable_post_isr_release - performs the cleanup post ISR
 *        release. The API need to be called after releasing all slowpath IRQs
 *        of the device.
 *
 * @param p_dev
 *
 */
void ecore_int_disable_post_isr_release(struct ecore_dev *p_dev);

/**
 * @brief ecore_int_attn_clr_enable - sets whether the general behavior is
 *        preventing attentions from being reasserted, or following the
 *        attributes of the specific attention.
 *
 * @param p_dev
 * @param clr_enable
 *
 */
void ecore_int_attn_clr_enable(struct ecore_dev *p_dev, bool clr_enable);

/**
 * @brief Read debug information regarding a given SB.
 *
 * @param p_hwfn
 * @param p_ptt
 * @param p_sb - point to Status block for which we want to get info.
 * @param p_info - pointer to struct to fill with information regarding SB.
 *
 * @return ECORE_SUCCESS if pointer is filled; failure otherwise.
 */
enum _ecore_status_t ecore_int_get_sb_dbg(struct ecore_hwfn *p_hwfn,
					  struct ecore_ptt *p_ptt,
					  struct ecore_sb_info *p_sb,
					  struct ecore_sb_info_dbg *p_info);

/**
 * @brief - Move a free Status block between PF and child VF
 *
 * @param p_hwfn
 * @param p_ptt
 * @param sb_id - The PF fastpath vector to be moved [re-assigned if claiming
 *                from VF, given-up if moving to VF]
 * @param b_to_vf - PF->VF == true, VF->PF == false
 *
 * @return ECORE_SUCCESS if SB successfully moved.
 */
enum _ecore_status_t
ecore_int_igu_relocate_sb(struct ecore_hwfn *p_hwfn, struct ecore_ptt *p_ptt,
			  u16 sb_id, bool b_to_vf);
#endif
