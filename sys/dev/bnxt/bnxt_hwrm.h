/*-
 * Broadcom NetXtreme-C/E network driver.
 *
 * Copyright (c) 2016 Broadcom, All Rights Reserved.
 * The term Broadcom refers to Broadcom Limited and/or its subsidiaries
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS'
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#ifndef _BNXT_HWRM_H
#define _BNXT_HWRM_H

#define BNXT_PAUSE_TX 	 (HWRM_PORT_PHY_QCFG_OUTPUT_PAUSE_TX)
#define BNXT_PAUSE_RX 	 (HWRM_PORT_PHY_QCFG_OUTPUT_PAUSE_RX)
#define BNXT_AUTO_PAUSE_AUTONEG_PAUSE  				\
        (HWRM_PORT_PHY_QCFG_OUTPUT_AUTO_PAUSE_AUTONEG_PAUSE)
#define BNXT_HWRM_SHORT_REQ_LEN	sizeof(struct hwrm_short_input)

/* HWRM Function Prototypes */
int bnxt_alloc_hwrm_dma_mem(struct bnxt_softc *softc);
void bnxt_free_hwrm_dma_mem(struct bnxt_softc *softc);
int bnxt_hwrm_ring_alloc(struct bnxt_softc *softc, uint8_t type,
    struct bnxt_ring *ring, uint16_t cmpl_ring_id, uint32_t stat_ctx_id,
    bool irq);
int bnxt_hwrm_ver_get(struct bnxt_softc *softc);
int bnxt_hwrm_queue_qportcfg(struct bnxt_softc *softc);
int bnxt_hwrm_func_drv_rgtr(struct bnxt_softc *softc);
int bnxt_hwrm_func_drv_unrgtr(struct bnxt_softc *softc, bool shutdown);
int bnxt_hwrm_func_qcaps(struct bnxt_softc *softc);
int bnxt_hwrm_func_qcfg(struct bnxt_softc *softc);
int bnxt_hwrm_func_reset(struct bnxt_softc *softc);
int bnxt_hwrm_set_link_setting(struct bnxt_softc *softc, bool set_pause,
    bool set_eee, bool set_link); 
int bnxt_hwrm_set_pause(struct bnxt_softc *softc);
int bnxt_hwrm_vnic_ctx_alloc(struct bnxt_softc *softc, uint16_t *ctx_id);
int bnxt_hwrm_vnic_cfg(struct bnxt_softc *softc, struct bnxt_vnic_info *vnic);
int bnxt_hwrm_stat_ctx_alloc(struct bnxt_softc *softc, struct bnxt_cp_ring *cpr,
    uint64_t paddr);
int bnxt_hwrm_port_qstats(struct bnxt_softc *softc);
int bnxt_hwrm_ring_grp_alloc(struct bnxt_softc *softc,
    struct bnxt_grp_info *grp);
int bnxt_hwrm_vnic_alloc(struct bnxt_softc *softc, struct bnxt_vnic_info *vnic);
int bnxt_hwrm_cfa_l2_set_rx_mask(struct bnxt_softc *softc,
    struct bnxt_vnic_info *vnic);
int bnxt_hwrm_set_filter(struct bnxt_softc *softc, struct bnxt_vnic_info *vnic);
int bnxt_hwrm_rss_cfg(struct bnxt_softc *softc, struct bnxt_vnic_info *vnic,
    uint32_t hash_type);
int bnxt_cfg_async_cr(struct bnxt_softc *softc);
int bnxt_hwrm_vnic_tpa_cfg(struct bnxt_softc *softc);
void bnxt_validate_hw_lro_settings(struct bnxt_softc *softc);
int bnxt_hwrm_nvm_find_dir_entry(struct bnxt_softc *softc, uint16_t type,
    uint16_t *ordinal, uint16_t ext, uint16_t *index, bool use_index,
    uint8_t search_opt, uint32_t *data_length, uint32_t *item_length,
    uint32_t *fw_ver);
int bnxt_hwrm_nvm_read(struct bnxt_softc *softc, uint16_t index,
    uint32_t offset, uint32_t length, struct iflib_dma_info *data);
int bnxt_hwrm_nvm_modify(struct bnxt_softc *softc, uint16_t index,
    uint32_t offset, void *data, bool cpyin, uint32_t length);
int bnxt_hwrm_fw_reset(struct bnxt_softc *softc, uint8_t processor,
    uint8_t *selfreset);
int bnxt_hwrm_fw_qstatus(struct bnxt_softc *softc, uint8_t type,
    uint8_t *selfreset);
int bnxt_hwrm_nvm_write(struct bnxt_softc *softc, void *data, bool cpyin,
    uint16_t type, uint16_t ordinal, uint16_t ext, uint16_t attr,
    uint16_t option, uint32_t data_length, bool keep, uint32_t *item_length,
    uint16_t *index);
int bnxt_hwrm_nvm_erase_dir_entry(struct bnxt_softc *softc, uint16_t index);
int bnxt_hwrm_nvm_get_dir_info(struct bnxt_softc *softc, uint32_t *entries,
    uint32_t *entry_length);
int bnxt_hwrm_nvm_get_dir_entries(struct bnxt_softc *softc,
    uint32_t *entries, uint32_t *entry_length, struct iflib_dma_info *dma_data);
int bnxt_hwrm_nvm_get_dev_info(struct bnxt_softc *softc, uint16_t *mfg_id,
    uint16_t *device_id, uint32_t *sector_size, uint32_t *nvram_size,
    uint32_t *reserved_size, uint32_t *available_size);
int bnxt_hwrm_nvm_install_update(struct bnxt_softc *softc,
    uint32_t install_type, uint64_t *installed_items, uint8_t *result,
    uint8_t *problem_item, uint8_t *reset_required);
int bnxt_hwrm_nvm_verify_update(struct bnxt_softc *softc, uint16_t type,
    uint16_t ordinal, uint16_t ext);
int bnxt_hwrm_fw_get_time(struct bnxt_softc *softc, uint16_t *year,
    uint8_t *month, uint8_t *day, uint8_t *hour, uint8_t *minute,
    uint8_t *second, uint16_t *millisecond, uint16_t *zone);
int bnxt_hwrm_fw_set_time(struct bnxt_softc *softc, uint16_t year,
    uint8_t month, uint8_t day, uint8_t hour, uint8_t minute, uint8_t second,
    uint16_t millisecond, uint16_t zone);
int bnxt_hwrm_port_phy_qcfg(struct bnxt_softc *softc);
uint16_t bnxt_hwrm_get_wol_fltrs(struct bnxt_softc *softc, uint16_t handle);
int bnxt_hwrm_alloc_wol_fltr(struct bnxt_softc *softc);
int bnxt_hwrm_free_wol_fltr(struct bnxt_softc *softc);
int bnxt_hwrm_set_coal(struct bnxt_softc *softc);
int bnxt_hwrm_func_rgtr_async_events(struct bnxt_softc *softc, unsigned long *bmap,
                                     int bmap_size);
#endif
