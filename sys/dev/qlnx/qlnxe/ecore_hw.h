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

#ifndef __ECORE_HW_H__
#define __ECORE_HW_H__

#include "ecore.h"
#include "ecore_dev_api.h"

/* Forward decleration */
struct ecore_ptt;

enum reserved_ptts {
	RESERVED_PTT_EDIAG,
	RESERVED_PTT_USER_SPACE,
	RESERVED_PTT_MAIN,
	RESERVED_PTT_DPC,
	RESERVED_PTT_MAX
};

/* @@@TMP - in earlier versions of the emulation, the HW lock started from 1
 * instead of 0, this should be fixed in later HW versions.
 */
#ifndef MISC_REG_DRIVER_CONTROL_0
#define MISC_REG_DRIVER_CONTROL_0	MISC_REG_DRIVER_CONTROL_1
#endif
#ifndef MISC_REG_DRIVER_CONTROL_0_SIZE
#define MISC_REG_DRIVER_CONTROL_0_SIZE	MISC_REG_DRIVER_CONTROL_1_SIZE
#endif

enum _dmae_cmd_dst_mask {
	DMAE_CMD_DST_MASK_NONE = 0,
	DMAE_CMD_DST_MASK_PCIE = 1,
	DMAE_CMD_DST_MASK_GRC = 2
};

enum _dmae_cmd_src_mask {
	DMAE_CMD_SRC_MASK_PCIE = 0,
	DMAE_CMD_SRC_MASK_GRC = 1
};

enum _dmae_cmd_crc_mask {
	DMAE_CMD_COMP_CRC_EN_MASK_NONE = 0,
	DMAE_CMD_COMP_CRC_EN_MASK_SET = 1
};

/* definitions for DMA constants */
#define DMAE_GO_VALUE	0x1

#ifdef __BIG_ENDIAN
#define DMAE_COMPLETION_VAL	0xAED10000
#define DMAE_CMD_ENDIANITY	0x3
#else
#define DMAE_COMPLETION_VAL	0xD1AE
#define DMAE_CMD_ENDIANITY	0x2
#endif

#define DMAE_CMD_SIZE	14
/* size of DMAE command structure to fill.. DMAE_CMD_SIZE-5 */
#define DMAE_CMD_SIZE_TO_FILL	(DMAE_CMD_SIZE - 5)
/* Minimum wait for dmae opertaion to complete 2 milliseconds */
#define DMAE_MIN_WAIT_TIME	0x2
#define DMAE_MAX_CLIENTS	32

/**
 * @brief ecore_ptt_invalidate - Forces all ptt entries to be re-configured
 *
 * @param p_hwfn
 */
void ecore_ptt_invalidate(struct ecore_hwfn *p_hwfn);

/**
 * @brief ecore_ptt_pool_alloc - Allocate and initialize PTT pool
 *
 * @param p_hwfn
 *
 * @return _ecore_status_t - success (0), negative - error.
 */
enum _ecore_status_t ecore_ptt_pool_alloc(struct ecore_hwfn *p_hwfn);

/**
 * @brief ecore_ptt_pool_free -
 *
 * @param p_hwfn
 */
void ecore_ptt_pool_free(struct ecore_hwfn *p_hwfn);

/**
 * @brief ecore_ptt_get_bar_addr - Get PPT's external BAR address
 *
 * @param p_hwfn
 * @param p_ptt
 *
 * @return u32
 */
u32 ecore_ptt_get_bar_addr(struct ecore_ptt	*p_ptt);

/**
 * @brief ecore_ptt_set_win - Set PTT Window's GRC BAR address
 *
 * @param p_hwfn
 * @param new_hw_addr
 * @param p_ptt
 */
void ecore_ptt_set_win(struct ecore_hwfn	*p_hwfn,
		       struct ecore_ptt		*p_ptt,
		       u32			new_hw_addr);

/**
 * @brief ecore_get_reserved_ptt - Get a specific reserved PTT
 *
 * @param p_hwfn
 * @param ptt_idx
 *
 * @return struct ecore_ptt *
 */
struct ecore_ptt *ecore_get_reserved_ptt(struct ecore_hwfn	*p_hwfn,
					 enum reserved_ptts	ptt_idx);

/**
 * @brief ecore_wr - Write value to BAR using the given ptt
 *
 * @param p_hwfn
 * @param p_ptt
 * @param hw_addr
 * @param val
 */
void ecore_wr(struct ecore_hwfn	*p_hwfn,
	      struct ecore_ptt	*p_ptt,
	      u32		hw_addr,
	      u32		val);

/**
 * @brief ecore_rd - Read value from BAR using the given ptt
 *
 * @param p_hwfn
 * @param p_ptt
 * @param hw_addr
 */
u32 ecore_rd(struct ecore_hwfn	*p_hwfn,
	     struct ecore_ptt	*p_ptt,
	     u32		hw_addr);

/**
 * @brief ecore_memcpy_from - copy n bytes from BAR using the given
 *        ptt
 *
 * @param p_hwfn
 * @param p_ptt
 * @param dest
 * @param hw_addr
 * @param n
 */
void ecore_memcpy_from(struct ecore_hwfn	*p_hwfn,
		       struct ecore_ptt		*p_ptt,
		       void			*dest,
		       u32			hw_addr,
		       osal_size_t		n);

/**
 * @brief ecore_memcpy_to - copy n bytes to BAR using the given
 *        ptt
 *
 * @param p_hwfn
 * @param p_ptt
 * @param hw_addr
 * @param src
 * @param n
 */
void ecore_memcpy_to(struct ecore_hwfn	*p_hwfn,
		     struct ecore_ptt	*p_ptt,
		     u32		hw_addr,
		     void		*src,
		     osal_size_t	n);
/**
 * @brief ecore_fid_pretend - pretend to another function when
 *        accessing the ptt window. There is no way to unpretend
 *        a function. The only way to cancel a pretend is to
 *        pretend back to the original function.
 *
 * @param p_hwfn
 * @param p_ptt
 * @param fid - fid field of pxp_pretend structure. Can contain
 *            either pf / vf, port/path fields are don't care.
 */
void ecore_fid_pretend(struct ecore_hwfn	*p_hwfn,
		       struct ecore_ptt		*p_ptt,
		       u16			fid);

/**
 * @brief ecore_port_pretend - pretend to another port when
 *        accessing the ptt window
 *
 * @param p_hwfn
 * @param p_ptt
 * @param port_id - the port to pretend to
 */
void ecore_port_pretend(struct ecore_hwfn	*p_hwfn,
			struct ecore_ptt	*p_ptt,
			u8			port_id);

/**
 * @brief ecore_port_unpretend - cancel any previously set port
 *        pretend
 *
 * @param p_hwfn
 * @param p_ptt
 */
void ecore_port_unpretend(struct ecore_hwfn	*p_hwfn,
			  struct ecore_ptt	*p_ptt);

/**
 * @brief ecore_vfid_to_concrete - build a concrete FID for a
 *        given VF ID
 *
 * @param p_hwfn
 * @param p_ptt
 * @param vfid
 */
u32 ecore_vfid_to_concrete(struct ecore_hwfn *p_hwfn, u8 vfid);

/**
* @brief ecore_dmae_info_alloc - Init the dmae_info structure
* which is part of p_hwfn.
* @param p_hwfn
*/
enum _ecore_status_t ecore_dmae_info_alloc(struct ecore_hwfn	*p_hwfn);

/**
* @brief ecore_dmae_info_free - Free the dmae_info structure
* which is part of p_hwfn
*
* @param p_hwfn
*/
void ecore_dmae_info_free(struct ecore_hwfn	*p_hwfn);

enum _ecore_status_t ecore_init_fw_data(struct ecore_dev *p_dev,
					const u8 *fw_data);

void ecore_hw_err_notify(struct ecore_hwfn *p_hwfn,
			 enum ecore_hw_err_type err_type);

enum _ecore_status_t ecore_dmae_sanity(struct ecore_hwfn *p_hwfn,
				       struct ecore_ptt *p_ptt,
				       const char *phase);

/**
 * @brief ecore_ppfid_wr - Write value to BAR using the given ptt while
 *	pretending to a PF to which the given PPFID pertains.
 *
 * @param p_hwfn
 * @param p_ptt
 * @param abs_ppfid
 * @param hw_addr
 * @param val
 */
void ecore_ppfid_wr(struct ecore_hwfn *p_hwfn, struct ecore_ptt *p_ptt,
		    u8 abs_ppfid, u32 hw_addr, u32 val);

/**
 * @brief ecore_ppfid_rd - Read value from BAR using the given ptt while
 *	 pretending to a PF to which the given PPFID pertains.
 *
 * @param p_hwfn
 * @param p_ptt
 * @param abs_ppfid
 * @param hw_addr
 */
u32 ecore_ppfid_rd(struct ecore_hwfn *p_hwfn, struct ecore_ptt *p_ptt,
		   u8 abs_ppfid, u32 hw_addr);

#endif /* __ECORE_HW_H__ */
