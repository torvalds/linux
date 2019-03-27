/*
 * Copyright 2013 Freescale Semiconductor Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of Freescale Semiconductor nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 *
 * ALTERNATIVELY, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") as published by the Free Software
 * Foundation, either version 2 of that License or (at your option) any
 * later version.
 *
 * THIS SOFTWARE IS PROVIDED BY Freescale Semiconductor ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL Freescale Semiconductor BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef __FSL_FMAN_SP_H
#define __FSL_FMAN_SP_H

#include "common/general.h"
#include "fsl_fman.h"


struct fm_pcd_storage_profile_regs{
	uint32_t   fm_sp_ebmpi[8];
					/*offset 0 - 0xc*/
					/**< Buffer Manager pool Information */

	uint32_t   fm_sp_acnt;      /*offset 0x20*/
	uint32_t   fm_sp_ebm;       /*offset 0x24*/
	uint32_t   fm_sp_da;        /*offset 0x28*/
	uint32_t   fm_sp_icp;       /*offset 0x2c*/
	uint32_t   fm_sp_mpd;       /*offset 0x30*/
	uint32_t   res1[2];         /*offset 0x34 - 0x38*/
	uint32_t   fm_sp_spliodn;   /*offset 0x3c*/
};

/**************************************************************************//**
 @Description   structure for defining internal context copying
*//***************************************************************************/
struct fman_sp_int_context_data_copy{
	uint16_t ext_buf_offset;     /**< Offset in External buffer to which
					internal context is copied to (Rx)
					or taken from (Tx, Op). */
	uint8_t int_context_offset; /**< Offset within internal context to copy
					from (Rx) or to copy to (Tx, Op).*/
	uint16_t size;             /**< Internal offset size to be copied */
};

/**************************************************************************//**
 @Description   struct for defining external buffer margins
*//***************************************************************************/
struct fman_sp_buf_margins{
	uint16_t start_margins;	/**< Number of bytes to be left at the
				beginning of the external buffer (must be
				divisible by 16) */
	uint16_t end_margins;   /**< number of bytes to be left at the end of
				 the external buffer(must be divisible by 16)*/
};

struct fm_storage_profile_params {
	struct fman_ext_pools          		fm_ext_pools;
	struct fman_backup_bm_pools   		backup_pools;
	struct fman_sp_int_context_data_copy 	*int_context;
	struct fman_sp_buf_margins            	*buf_margins;
	enum fman_dma_swap_option    		dma_swap_data;
	enum fman_dma_cache_option          	int_context_cache_attr;
	enum fman_dma_cache_option          	header_cache_attr;
	enum fman_dma_cache_option          	scatter_gather_cache_attr;
	bool                        		dma_write_optimize;
	uint16_t                    		liodn_offset;
	bool                        		no_scather_gather;
	struct fman_buf_pool_depletion        buf_pool_depletion;
};

/**************************************************************************//**
 @Description       Registers bit fields
*//***************************************************************************/
#define FMAN_SP_EXT_BUF_POOL_EN_COUNTER             0x40000000
#define FMAN_SP_EXT_BUF_POOL_VALID                  0x80000000
#define FMAN_SP_EXT_BUF_POOL_BACKUP                 0x20000000
#define FMAN_SP_DMA_ATTR_WRITE_OPTIMIZE             0x00100000
#define FMAN_SP_SG_DISABLE                          0x80000000

/* shifts */
#define FMAN_SP_EXT_BUF_POOL_ID_SHIFT               16
#define FMAN_SP_POOL_DEP_NUM_OF_POOLS_SHIFT         16
#define FMAN_SP_EXT_BUF_MARG_START_SHIFT            16
#define FMAN_SP_EXT_BUF_MARG_END_SHIFT              0
#define FMAN_SP_DMA_ATTR_SWP_SHIFT                  30
#define FMAN_SP_DMA_ATTR_IC_CACHE_SHIFT             28
#define FMAN_SP_DMA_ATTR_HDR_CACHE_SHIFT            26
#define FMAN_SP_DMA_ATTR_SG_CACHE_SHIFT             24
#define FMAN_SP_IC_TO_EXT_SHIFT                     16
#define FMAN_SP_IC_FROM_INT_SHIFT                   8
#define FMAN_SP_IC_SIZE_SHIFT                       0

/**************************************************************************//**
 @Description       defaults
*//***************************************************************************/
#define DEFAULT_FMAN_SP_DMA_SWAP_DATA                         FMAN_DMA_NO_SWP
#define DEFAULT_FMAN_SP_DMA_INT_CONTEXT_CACHE_ATTR            FMAN_DMA_NO_STASH
#define DEFAULT_FMAN_SP_DMA_HEADER_CACHE_ATTR                 FMAN_DMA_NO_STASH
#define DEFAULT_FMAN_SP_DMA_SCATTER_GATHER_CACHE_ATTR         FMAN_DMA_NO_STASH
#define DEFAULT_FMAN_SP_DMA_WRITE_OPTIMIZE                    TRUE
#define DEFAULT_FMAN_SP_NO_SCATTER_GATHER                     FALSE

void fman_vsp_defconfig(struct fm_storage_profile_params *cfg);

void fman_vsp_init(struct fm_pcd_storage_profile_regs   *regs,
	uint16_t index, struct fm_storage_profile_params *fm_vsp_params,
	int port_max_num_of_ext_pools, int bm_max_num_of_pools,
	int max_num_of_pfc_priorities);

uint32_t fman_vsp_get_statistics(struct fm_pcd_storage_profile_regs *regs,
					uint16_t index);

void fman_vsp_set_statistics(struct fm_pcd_storage_profile_regs *regs,
			uint16_t index,	uint32_t value);


#endif /* __FSL_FMAN_SP_H */
