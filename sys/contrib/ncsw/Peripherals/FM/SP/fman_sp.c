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

#include "fsl_fman_sp.h"


uint32_t fman_vsp_get_statistics(struct fm_pcd_storage_profile_regs   *regs,
                    uint16_t                      index)
{
    struct fm_pcd_storage_profile_regs *sp_regs;
    sp_regs = &regs[index];
    return ioread32be(&sp_regs->fm_sp_acnt);
}

void fman_vsp_set_statistics(struct fm_pcd_storage_profile_regs *regs,
            uint16_t index,	uint32_t value)
{
    struct fm_pcd_storage_profile_regs *sp_regs;
    sp_regs = &regs[index];
    iowrite32be(value, &sp_regs->fm_sp_acnt);
}

void fman_vsp_defconfig(struct fm_storage_profile_params *cfg)
{
    cfg->dma_swap_data =
            DEFAULT_FMAN_SP_DMA_SWAP_DATA;
    cfg->int_context_cache_attr =
            DEFAULT_FMAN_SP_DMA_INT_CONTEXT_CACHE_ATTR;
    cfg->header_cache_attr =
            DEFAULT_FMAN_SP_DMA_HEADER_CACHE_ATTR;
    cfg->scatter_gather_cache_attr =
            DEFAULT_FMAN_SP_DMA_SCATTER_GATHER_CACHE_ATTR;
    cfg->dma_write_optimize =
            DEFAULT_FMAN_SP_DMA_WRITE_OPTIMIZE;
    cfg->no_scather_gather =
            DEFAULT_FMAN_SP_NO_SCATTER_GATHER;
}

static inline uint32_t calc_vec_dep(int max_pools, bool *pools,
        struct fman_ext_pools *ext_buf_pools, uint32_t mask)
{
    int i, j;
    uint32_t vector = 0;
    for (i = 0; i < max_pools; i++)
        if (pools[i])
            for (j = 0; j < ext_buf_pools->num_pools_used; j++)
                if (i == ext_buf_pools->ext_buf_pool[j].id) {
                    vector |= mask >> j;
                    break;
                }
    return vector;
}

void fman_vsp_init(struct fm_pcd_storage_profile_regs   *regs,
    uint16_t index, struct fm_storage_profile_params *fm_vsp_params,
    int port_max_num_of_ext_pools, int bm_max_num_of_pools,
    int max_num_of_pfc_priorities)
{
    int i = 0, j = 0;
    struct fm_pcd_storage_profile_regs *sp_regs;
    uint32_t tmp_reg, vector;
    struct fman_ext_pools *ext_buf_pools = &fm_vsp_params->fm_ext_pools;
    struct fman_buf_pool_depletion *buf_pool_depletion =
                    &fm_vsp_params->buf_pool_depletion;
    struct fman_backup_bm_pools *backup_pools =
                    &fm_vsp_params->backup_pools;
    struct fman_sp_int_context_data_copy *int_context_data_copy =
                        fm_vsp_params->int_context;
    struct fman_sp_buf_margins *external_buffer_margins =
                        fm_vsp_params->buf_margins;
    bool no_scather_gather = fm_vsp_params->no_scather_gather;
    uint16_t liodn_offset = fm_vsp_params->liodn_offset;

    sp_regs = &regs[index];

    /* fill external buffers manager pool information register*/
    for (i = 0; i < ext_buf_pools->num_pools_used; i++) {
        tmp_reg = FMAN_SP_EXT_BUF_POOL_VALID |
            FMAN_SP_EXT_BUF_POOL_EN_COUNTER;
        tmp_reg |= ((uint32_t)ext_buf_pools->ext_buf_pool[i].id <<
            FMAN_SP_EXT_BUF_POOL_ID_SHIFT);
        tmp_reg |= ext_buf_pools->ext_buf_pool[i].size;
        /* functionality available only for some deriviatives
             (limited by config) */
        for (j = 0; j < backup_pools->num_backup_pools; j++)
            if (ext_buf_pools->ext_buf_pool[i].id ==
                backup_pools->pool_ids[j]) {
                tmp_reg |= FMAN_SP_EXT_BUF_POOL_BACKUP;
                break;
            }
        iowrite32be(tmp_reg, &sp_regs->fm_sp_ebmpi[i]);
    }

    /* clear unused pools */
    for (i = ext_buf_pools->num_pools_used;
        i < port_max_num_of_ext_pools; i++)
        iowrite32be(0, &sp_regs->fm_sp_ebmpi[i]);

    /* fill pool depletion register*/
    tmp_reg = 0;
    if (buf_pool_depletion->buf_pool_depletion_enabled && buf_pool_depletion->pools_grp_mode_enable) {
        /* calculate vector for number of pools depletion */
        vector = calc_vec_dep(bm_max_num_of_pools, buf_pool_depletion->
                pools_to_consider, ext_buf_pools, 0x80000000);

        /* configure num of pools and vector for number of pools mode */
        tmp_reg |= (((uint32_t)buf_pool_depletion->num_pools - 1) <<
            FMAN_SP_POOL_DEP_NUM_OF_POOLS_SHIFT);
        tmp_reg |= vector;
    }

    if (buf_pool_depletion->buf_pool_depletion_enabled && buf_pool_depletion->single_pool_mode_enable) {
        /* calculate vector for number of pools depletion */
        vector = calc_vec_dep(bm_max_num_of_pools, buf_pool_depletion->
                pools_to_consider_for_single_mode,
                ext_buf_pools, 0x00000080);

        /* configure num of pools and vector for number of pools mode */
        tmp_reg |= vector;
    }

    /* fill QbbPEV */
    if (buf_pool_depletion->buf_pool_depletion_enabled) {
        vector = 0;
        for (i = 0; i < max_num_of_pfc_priorities; i++)
            if (buf_pool_depletion->pfc_priorities_en[i] == TRUE)
                vector |= 0x00000100 << i;
        tmp_reg |= vector;
    }
    iowrite32be(tmp_reg, &sp_regs->fm_sp_mpd);

    /* fill dma attributes register */
    tmp_reg = 0;
    tmp_reg |= (uint32_t)fm_vsp_params->dma_swap_data <<
        FMAN_SP_DMA_ATTR_SWP_SHIFT;
    tmp_reg |= (uint32_t)fm_vsp_params->int_context_cache_attr <<
        FMAN_SP_DMA_ATTR_IC_CACHE_SHIFT;
    tmp_reg |= (uint32_t)fm_vsp_params->header_cache_attr <<
        FMAN_SP_DMA_ATTR_HDR_CACHE_SHIFT;
    tmp_reg |= (uint32_t)fm_vsp_params->scatter_gather_cache_attr <<
        FMAN_SP_DMA_ATTR_SG_CACHE_SHIFT;
    if (fm_vsp_params->dma_write_optimize)
        tmp_reg |= FMAN_SP_DMA_ATTR_WRITE_OPTIMIZE;
    iowrite32be(tmp_reg, &sp_regs->fm_sp_da);

    /* IC parameters - fill internal context parameters register */
    tmp_reg = 0;
    tmp_reg |= (((uint32_t)int_context_data_copy->ext_buf_offset/
        OFFSET_UNITS) << FMAN_SP_IC_TO_EXT_SHIFT);
    tmp_reg |= (((uint32_t)int_context_data_copy->int_context_offset/
        OFFSET_UNITS) << FMAN_SP_IC_FROM_INT_SHIFT);
    tmp_reg |= (((uint32_t)int_context_data_copy->size/OFFSET_UNITS) <<
        FMAN_SP_IC_SIZE_SHIFT);
    iowrite32be(tmp_reg, &sp_regs->fm_sp_icp);

    /* buffer margins - fill external buffer margins register */
    tmp_reg = 0;
    tmp_reg |= (((uint32_t)external_buffer_margins->start_margins) <<
        FMAN_SP_EXT_BUF_MARG_START_SHIFT);
    tmp_reg |= (((uint32_t)external_buffer_margins->end_margins) <<
        FMAN_SP_EXT_BUF_MARG_END_SHIFT);
    if (no_scather_gather)
        tmp_reg |= FMAN_SP_SG_DISABLE;
    iowrite32be(tmp_reg, &sp_regs->fm_sp_ebm);

    /* buffer margins - fill spliodn register */
    iowrite32be(liodn_offset, &sp_regs->fm_sp_spliodn);
}
