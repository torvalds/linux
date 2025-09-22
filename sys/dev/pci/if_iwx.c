/*	$OpenBSD: if_iwx.c,v 1.192 2025/07/18 16:09:28 stsp Exp $	*/

/*
 * Copyright (c) 2014, 2016 genua gmbh <info@genua.de>
 *   Author: Stefan Sperling <stsp@openbsd.org>
 * Copyright (c) 2014 Fixup Software Ltd.
 * Copyright (c) 2017, 2019, 2020 Stefan Sperling <stsp@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/*-
 * Based on BSD-licensed source modules in the Linux iwlwifi driver,
 * which were used as the reference documentation for this implementation.
 *
 ******************************************************************************
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2017 Intel Deutschland GmbH
 * Copyright(c) 2018 - 2019 Intel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * BSD LICENSE
 *
 * Copyright(c) 2017 Intel Deutschland GmbH
 * Copyright(c) 2018 - 2019 Intel Corporation
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *  * Neither the name Intel Corporation nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *****************************************************************************
 */

/*-
 * Copyright (c) 2007-2010 Damien Bergamini <damien.bergamini@free.fr>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include "bpfilter.h"

#include <sys/param.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/rwlock.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/systm.h>
#include <sys/endian.h>

#include <sys/refcnt.h>
#include <sys/task.h>
#include <machine/bus.h>
#include <machine/intr.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/pcidevs.h>

#if NBPFILTER > 0
#include <net/bpf.h>
#endif
#include <net/if.h>
#include <net/if_media.h>

#include <netinet/in.h>
#include <netinet/if_ether.h>

#include <net80211/ieee80211_var.h>
#include <net80211/ieee80211_radiotap.h>
#include <net80211/ieee80211_priv.h> /* for SEQ_LT */
#undef DPRINTF /* defined in ieee80211_priv.h */

#define DEVNAME(_s)	((_s)->sc_dev.dv_xname)

#define IC2IFP(_ic_) (&(_ic_)->ic_if)

#define le16_to_cpup(_a_) (le16toh(*(const uint16_t *)(_a_)))
#define le32_to_cpup(_a_) (le32toh(*(const uint32_t *)(_a_)))

#ifdef IWX_DEBUG
#define DPRINTF(x)	do { if (iwx_debug > 0) printf x; } while (0)
#define DPRINTFN(n, x)	do { if (iwx_debug >= (n)) printf x; } while (0)
int iwx_debug = 1;
#else
#define DPRINTF(x)	do { ; } while (0)
#define DPRINTFN(n, x)	do { ; } while (0)
#endif

#include <dev/pci/if_iwxreg.h>
#include <dev/pci/if_iwxvar.h>

const uint8_t iwx_nvm_channels_8000[] = {
	/* 2.4 GHz */
	1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14,
	/* 5 GHz */
	36, 40, 44, 48, 52, 56, 60, 64, 68, 72, 76, 80, 84, 88, 92,
	96, 100, 104, 108, 112, 116, 120, 124, 128, 132, 136, 140, 144,
	149, 153, 157, 161, 165, 169, 173, 177, 181
};

static const uint8_t iwx_nvm_channels_uhb[] = {
	/* 2.4 GHz */
	1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14,
	/* 5 GHz */
	36, 40, 44, 48, 52, 56, 60, 64, 68, 72, 76, 80, 84, 88, 92,
	96, 100, 104, 108, 112, 116, 120, 124, 128, 132, 136, 140, 144,
	149, 153, 157, 161, 165, 169, 173, 177, 181,
	/* 6-7 GHz */
	1, 5, 9, 13, 17, 21, 25, 29, 33, 37, 41, 45, 49, 53, 57, 61, 65, 69,
	73, 77, 81, 85, 89, 93, 97, 101, 105, 109, 113, 117, 121, 125, 129,
	133, 137, 141, 145, 149, 153, 157, 161, 165, 169, 173, 177, 181, 185,
	189, 193, 197, 201, 205, 209, 213, 217, 221, 225, 229, 233
};

#define IWX_NUM_2GHZ_CHANNELS	14
#define IWX_NUM_5GHZ_CHANNELS	37

const struct iwx_rate {
	uint16_t rate;
	uint8_t plcp;
	uint8_t ht_plcp;
} iwx_rates[] = {
		/* Legacy */		/* HT */
	{   2,	IWX_RATE_1M_PLCP,	IWX_RATE_HT_SISO_MCS_INV_PLCP  },
	{   4,	IWX_RATE_2M_PLCP,	IWX_RATE_HT_SISO_MCS_INV_PLCP },
	{  11,	IWX_RATE_5M_PLCP,	IWX_RATE_HT_SISO_MCS_INV_PLCP  },
	{  22,	IWX_RATE_11M_PLCP,	IWX_RATE_HT_SISO_MCS_INV_PLCP },
	{  12,	IWX_RATE_6M_PLCP,	IWX_RATE_HT_SISO_MCS_0_PLCP },
	{  18,	IWX_RATE_9M_PLCP,	IWX_RATE_HT_SISO_MCS_INV_PLCP  },
	{  24,	IWX_RATE_12M_PLCP,	IWX_RATE_HT_SISO_MCS_1_PLCP },
	{  26,	IWX_RATE_INVM_PLCP,	IWX_RATE_HT_MIMO2_MCS_8_PLCP },
	{  36,	IWX_RATE_18M_PLCP,	IWX_RATE_HT_SISO_MCS_2_PLCP },
	{  48,	IWX_RATE_24M_PLCP,	IWX_RATE_HT_SISO_MCS_3_PLCP },
	{  52,	IWX_RATE_INVM_PLCP,	IWX_RATE_HT_MIMO2_MCS_9_PLCP },
	{  72,	IWX_RATE_36M_PLCP,	IWX_RATE_HT_SISO_MCS_4_PLCP },
	{  78,	IWX_RATE_INVM_PLCP,	IWX_RATE_HT_MIMO2_MCS_10_PLCP },
	{  96,	IWX_RATE_48M_PLCP,	IWX_RATE_HT_SISO_MCS_5_PLCP },
	{ 104,	IWX_RATE_INVM_PLCP,	IWX_RATE_HT_MIMO2_MCS_11_PLCP },
	{ 108,	IWX_RATE_54M_PLCP,	IWX_RATE_HT_SISO_MCS_6_PLCP },
	{ 128,	IWX_RATE_INVM_PLCP,	IWX_RATE_HT_SISO_MCS_7_PLCP },
	{ 156,	IWX_RATE_INVM_PLCP,	IWX_RATE_HT_MIMO2_MCS_12_PLCP },
	{ 208,	IWX_RATE_INVM_PLCP,	IWX_RATE_HT_MIMO2_MCS_13_PLCP },
	{ 234,	IWX_RATE_INVM_PLCP,	IWX_RATE_HT_MIMO2_MCS_14_PLCP },
	{ 260,	IWX_RATE_INVM_PLCP,	IWX_RATE_HT_MIMO2_MCS_15_PLCP },
};
#define IWX_RIDX_CCK	0
#define IWX_RIDX_OFDM	4
#define IWX_RIDX_MAX	(nitems(iwx_rates)-1)
#define IWX_RIDX_IS_CCK(_i_) ((_i_) < IWX_RIDX_OFDM)
#define IWX_RIDX_IS_OFDM(_i_) ((_i_) >= IWX_RIDX_OFDM)
#define IWX_RVAL_IS_OFDM(_i_) ((_i_) >= 12 && (_i_) != 22)

/* Convert an MCS index into an iwx_rates[] index. */
const int iwx_mcs2ridx[] = {
	IWX_RATE_MCS_0_INDEX,
	IWX_RATE_MCS_1_INDEX,
	IWX_RATE_MCS_2_INDEX,
	IWX_RATE_MCS_3_INDEX,
	IWX_RATE_MCS_4_INDEX,
	IWX_RATE_MCS_5_INDEX,
	IWX_RATE_MCS_6_INDEX,
	IWX_RATE_MCS_7_INDEX,
	IWX_RATE_MCS_8_INDEX,
	IWX_RATE_MCS_9_INDEX,
	IWX_RATE_MCS_10_INDEX,
	IWX_RATE_MCS_11_INDEX,
	IWX_RATE_MCS_12_INDEX,
	IWX_RATE_MCS_13_INDEX,
	IWX_RATE_MCS_14_INDEX,
	IWX_RATE_MCS_15_INDEX,
};

uint8_t	iwx_lookup_cmd_ver(struct iwx_softc *, uint8_t, uint8_t);
uint8_t	iwx_lookup_notif_ver(struct iwx_softc *, uint8_t, uint8_t);
int	iwx_is_mimo_ht_plcp(uint8_t);
int	iwx_store_cscheme(struct iwx_softc *, uint8_t *, size_t);
int	iwx_alloc_fw_monitor_block(struct iwx_softc *, uint8_t, uint8_t);
int	iwx_alloc_fw_monitor(struct iwx_softc *, uint8_t);
int	iwx_apply_debug_destination(struct iwx_softc *);
void	iwx_set_ltr(struct iwx_softc *);
int	iwx_ctxt_info_init(struct iwx_softc *, const struct iwx_fw_sects *);
int	iwx_ctxt_info_gen3_init(struct iwx_softc *,
	    const struct iwx_fw_sects *);
void	iwx_ctxt_info_free_fw_img(struct iwx_softc *);
void	iwx_ctxt_info_free_paging(struct iwx_softc *);
int	iwx_init_fw_sec(struct iwx_softc *, const struct iwx_fw_sects *,
	    struct iwx_context_info_dram *);
void	iwx_fw_version_str(char *, size_t, uint32_t, uint32_t, uint32_t);
int	iwx_firmware_store_section(struct iwx_softc *, enum iwx_ucode_type,
	    uint8_t *, size_t);
int	iwx_set_default_calib(struct iwx_softc *, const void *);
void	iwx_fw_info_free(struct iwx_fw_info *);
int	iwx_read_firmware(struct iwx_softc *);
uint32_t iwx_prph_addr_mask(struct iwx_softc *);
uint32_t iwx_read_prph_unlocked(struct iwx_softc *, uint32_t);
uint32_t iwx_read_prph(struct iwx_softc *, uint32_t);
void	iwx_write_prph_unlocked(struct iwx_softc *, uint32_t, uint32_t);
void	iwx_write_prph(struct iwx_softc *, uint32_t, uint32_t);
uint32_t iwx_read_umac_prph_unlocked(struct iwx_softc *, uint32_t);
uint32_t iwx_read_umac_prph(struct iwx_softc *, uint32_t);
void	iwx_write_umac_prph_unlocked(struct iwx_softc *, uint32_t, uint32_t);
void	iwx_write_umac_prph(struct iwx_softc *, uint32_t, uint32_t);
int	iwx_read_mem(struct iwx_softc *, uint32_t, void *, int);
int	iwx_write_mem(struct iwx_softc *, uint32_t, const void *, int);
int	iwx_write_mem32(struct iwx_softc *, uint32_t, uint32_t);
int	iwx_poll_bit(struct iwx_softc *, int, uint32_t, uint32_t, int);
int	iwx_nic_lock(struct iwx_softc *);
void	iwx_nic_assert_locked(struct iwx_softc *);
void	iwx_nic_unlock(struct iwx_softc *);
int	iwx_set_bits_mask_prph(struct iwx_softc *, uint32_t, uint32_t,
	    uint32_t);
int	iwx_set_bits_prph(struct iwx_softc *, uint32_t, uint32_t);
int	iwx_clear_bits_prph(struct iwx_softc *, uint32_t, uint32_t);
int	iwx_dma_contig_alloc(bus_dma_tag_t, struct iwx_dma_info *, bus_size_t,
	    bus_size_t);
void	iwx_dma_contig_free(struct iwx_dma_info *);
int	iwx_alloc_rx_ring(struct iwx_softc *, struct iwx_rx_ring *);
void	iwx_disable_rx_dma(struct iwx_softc *);
void	iwx_reset_rx_ring(struct iwx_softc *, struct iwx_rx_ring *);
void	iwx_free_rx_ring(struct iwx_softc *, struct iwx_rx_ring *);
int	iwx_alloc_tx_ring(struct iwx_softc *, struct iwx_tx_ring *, int);
void	iwx_reset_tx_ring(struct iwx_softc *, struct iwx_tx_ring *);
void	iwx_free_tx_ring(struct iwx_softc *, struct iwx_tx_ring *);
void	iwx_enable_rfkill_int(struct iwx_softc *);
int	iwx_check_rfkill(struct iwx_softc *);
void	iwx_enable_interrupts(struct iwx_softc *);
void	iwx_enable_fwload_interrupt(struct iwx_softc *);
void	iwx_restore_interrupts(struct iwx_softc *);
void	iwx_disable_interrupts(struct iwx_softc *);
void	iwx_ict_reset(struct iwx_softc *);
int	iwx_set_hw_ready(struct iwx_softc *);
int	iwx_prepare_card_hw(struct iwx_softc *);
int	iwx_force_power_gating(struct iwx_softc *);
void	iwx_apm_config(struct iwx_softc *);
int	iwx_apm_init(struct iwx_softc *);
void	iwx_apm_stop(struct iwx_softc *);
int	iwx_allow_mcast(struct iwx_softc *);
void	iwx_init_msix_hw(struct iwx_softc *);
void	iwx_conf_msix_hw(struct iwx_softc *, int);
int	iwx_clear_persistence_bit(struct iwx_softc *);
int	iwx_start_hw(struct iwx_softc *);
void	iwx_stop_device(struct iwx_softc *);
void	iwx_nic_config(struct iwx_softc *);
int	iwx_nic_rx_init(struct iwx_softc *);
int	iwx_nic_init(struct iwx_softc *);
int	iwx_enable_txq(struct iwx_softc *, int, int, int, int);
int	iwx_disable_txq(struct iwx_softc *sc, int, int, uint8_t);
void	iwx_post_alive(struct iwx_softc *);
int	iwx_schedule_session_protection(struct iwx_softc *, struct iwx_node *,
	    uint32_t);
void	iwx_unprotect_session(struct iwx_softc *, struct iwx_node *);
void	iwx_init_channel_map(struct iwx_softc *, uint16_t *, uint32_t *, int);
void	iwx_setup_ht_rates(struct iwx_softc *);
void	iwx_setup_vht_rates(struct iwx_softc *);
int	iwx_mimo_enabled(struct iwx_softc *);
void	iwx_mac_ctxt_task(void *);
void	iwx_phy_ctxt_task(void *);
void	iwx_updatechan(struct ieee80211com *);
void	iwx_updateprot(struct ieee80211com *);
void	iwx_updateslot(struct ieee80211com *);
void	iwx_updateedca(struct ieee80211com *);
void	iwx_updatedtim(struct ieee80211com *);
void	iwx_init_reorder_buffer(struct iwx_reorder_buffer *, uint16_t,
	    uint16_t);
void	iwx_clear_reorder_buffer(struct iwx_softc *, struct iwx_rxba_data *);
int	iwx_ampdu_rx_start(struct ieee80211com *, struct ieee80211_node *,
	    uint8_t);
void	iwx_ampdu_rx_stop(struct ieee80211com *, struct ieee80211_node *,
	    uint8_t);
int	iwx_ampdu_tx_start(struct ieee80211com *, struct ieee80211_node *,
	    uint8_t);
void	iwx_rx_ba_session_expired(void *);
void	iwx_rx_bar_frame_release(struct iwx_softc *, struct iwx_rx_packet *,
	    struct mbuf_list *);
void	iwx_reorder_timer_expired(void *);
void	iwx_sta_rx_agg(struct iwx_softc *, struct ieee80211_node *, uint8_t,
	    uint16_t, uint16_t, int, int);
void	iwx_sta_tx_agg_start(struct iwx_softc *, struct ieee80211_node *,
	    uint8_t);
void	iwx_ba_task(void *);

void	iwx_set_mac_addr_from_csr(struct iwx_softc *, struct iwx_nvm_data *);
int	iwx_is_valid_mac_addr(const uint8_t *);
void	iwx_flip_hw_address(uint32_t, uint32_t, uint8_t *);
int	iwx_nvm_get(struct iwx_softc *);
int	iwx_load_firmware(struct iwx_softc *);
int	iwx_start_fw(struct iwx_softc *);
int	iwx_pnvm_handle_section(struct iwx_softc *, const uint8_t *, size_t);
int	iwx_pnvm_parse(struct iwx_softc *, const uint8_t *, size_t);
void	iwx_ctxt_info_gen3_set_pnvm(struct iwx_softc *);
int	iwx_load_pnvm(struct iwx_softc *);
int	iwx_send_tx_ant_cfg(struct iwx_softc *, uint8_t);
int	iwx_send_phy_cfg_cmd(struct iwx_softc *);
int	iwx_load_ucode_wait_alive(struct iwx_softc *);
int	iwx_send_dqa_cmd(struct iwx_softc *);
int	iwx_run_init_mvm_ucode(struct iwx_softc *, int);
int	iwx_config_ltr(struct iwx_softc *);
void	iwx_update_rx_desc(struct iwx_softc *, struct iwx_rx_ring *, int);
int	iwx_rx_addbuf(struct iwx_softc *, int, int);
int	iwx_rxmq_get_signal_strength(struct iwx_softc *, struct iwx_rx_mpdu_desc *);
void	iwx_rx_rx_phy_cmd(struct iwx_softc *, struct iwx_rx_packet *,
	    struct iwx_rx_data *);
int	iwx_get_noise(const struct iwx_statistics_rx_non_phy *);
int	iwx_rx_hwdecrypt(struct iwx_softc *, struct mbuf *, uint32_t,
	    struct ieee80211_rxinfo *);
int	iwx_ccmp_decap(struct iwx_softc *, struct mbuf *,
	    struct ieee80211_node *, struct ieee80211_rxinfo *);
void	iwx_rx_frame(struct iwx_softc *, struct mbuf *, int, uint32_t, int, int,
	    uint32_t, struct ieee80211_rxinfo *, struct mbuf_list *);
void	iwx_clear_tx_desc(struct iwx_softc *, struct iwx_tx_ring *, int);
void	iwx_txd_done(struct iwx_softc *, struct iwx_tx_data *);
void	iwx_txq_advance(struct iwx_softc *, struct iwx_tx_ring *, uint16_t);
void	iwx_rx_tx_cmd(struct iwx_softc *, struct iwx_rx_packet *,
	    struct iwx_rx_data *);
void	iwx_clear_oactive(struct iwx_softc *, struct iwx_tx_ring *);
void	iwx_rx_bmiss(struct iwx_softc *, struct iwx_rx_packet *,
	    struct iwx_rx_data *);
int	iwx_binding_cmd(struct iwx_softc *, struct iwx_node *, uint32_t);
uint8_t	iwx_get_vht_ctrl_pos(struct ieee80211com *, struct ieee80211_channel *);
int	iwx_phy_ctxt_cmd_uhb_v3_v4(struct iwx_softc *, struct iwx_phy_ctxt *,
	    uint8_t, uint8_t, uint32_t, uint8_t, uint8_t, int);
int	iwx_phy_ctxt_cmd_v3_v4(struct iwx_softc *, struct iwx_phy_ctxt *,
	    uint8_t, uint8_t, uint32_t, uint8_t, uint8_t, int);
int	iwx_phy_ctxt_cmd(struct iwx_softc *, struct iwx_phy_ctxt *, uint8_t,
	    uint8_t, uint32_t, uint32_t, uint8_t, uint8_t);
int	iwx_send_cmd(struct iwx_softc *, struct iwx_host_cmd *);
int	iwx_send_cmd_pdu(struct iwx_softc *, uint32_t, uint32_t, uint16_t,
	    const void *);
int	iwx_send_cmd_status(struct iwx_softc *, struct iwx_host_cmd *,
	    uint32_t *);
int	iwx_send_cmd_pdu_status(struct iwx_softc *, uint32_t, uint16_t,
	    const void *, uint32_t *);
void	iwx_free_resp(struct iwx_softc *, struct iwx_host_cmd *);
void	iwx_cmd_done(struct iwx_softc *, int, int, int);
uint32_t iwx_fw_rateidx_ofdm(uint8_t);
uint32_t iwx_fw_rateidx_cck(uint8_t);
const struct iwx_rate *iwx_tx_fill_cmd(struct iwx_softc *, struct iwx_node *,
	    struct ieee80211_frame *, uint16_t *, uint32_t *);
void	iwx_tx_update_byte_tbl(struct iwx_softc *, struct iwx_tx_ring *, int,
	    uint16_t, uint16_t);
int	iwx_tx(struct iwx_softc *, struct mbuf *, struct ieee80211_node *);
int	iwx_flush_sta_tids(struct iwx_softc *, int, uint16_t);
int	iwx_drain_sta(struct iwx_softc *sc, struct iwx_node *, int);
int	iwx_flush_sta(struct iwx_softc *, struct iwx_node *);
int	iwx_beacon_filter_send_cmd(struct iwx_softc *,
	    struct iwx_beacon_filter_cmd *);
int	iwx_update_beacon_abort(struct iwx_softc *, struct iwx_node *, int);
void	iwx_power_build_cmd(struct iwx_softc *, struct iwx_node *,
	    struct iwx_mac_power_cmd *);
int	iwx_power_mac_update_mode(struct iwx_softc *, struct iwx_node *);
int	iwx_power_update_device(struct iwx_softc *);
int	iwx_enable_beacon_filter(struct iwx_softc *, struct iwx_node *);
int	iwx_disable_beacon_filter(struct iwx_softc *);
int	iwx_add_sta_cmd(struct iwx_softc *, struct iwx_node *, int);
int	iwx_mld_add_sta_cmd(struct iwx_softc *, struct iwx_node *, int);
int	iwx_rm_sta_cmd(struct iwx_softc *, struct iwx_node *);
int	iwx_mld_rm_sta_cmd(struct iwx_softc *, struct iwx_node *);
int	iwx_rm_sta(struct iwx_softc *, struct iwx_node *);
int	iwx_fill_probe_req(struct iwx_softc *, struct iwx_scan_probe_req *);
int	iwx_config_umac_scan_reduced(struct iwx_softc *);
uint16_t iwx_scan_umac_flags_v2(struct iwx_softc *, int);
void	iwx_scan_umac_dwell_v10(struct iwx_softc *,
	    struct iwx_scan_general_params_v10 *, int);
void	iwx_scan_umac_fill_general_p_v10(struct iwx_softc *,
	    struct iwx_scan_general_params_v10 *, uint16_t, int);
void	iwx_scan_umac_fill_ch_p_v6(struct iwx_softc *,
	    struct iwx_scan_channel_params_v6 *, uint32_t, int);
int	iwx_umac_scan_v14(struct iwx_softc *, int);
void	iwx_mcc_update(struct iwx_softc *, struct iwx_mcc_chub_notif *);
uint8_t	iwx_ridx2rate(struct ieee80211_rateset *, int);
int	iwx_rval2ridx(int);
void	iwx_ack_rates(struct iwx_softc *, struct iwx_node *, int *, int *);
void	iwx_mac_ctxt_cmd_common(struct iwx_softc *, struct iwx_node *,
	    struct iwx_mac_ctx_cmd *, uint32_t);
void	iwx_mac_ctxt_cmd_fill_sta(struct iwx_softc *, struct iwx_node *,
	    struct iwx_mac_data_sta *, int);
int	iwx_mac_ctxt_cmd(struct iwx_softc *, struct iwx_node *, uint32_t, int);
int	iwx_mld_mac_ctxt_cmd(struct iwx_softc *, struct iwx_node *, uint32_t,
	    int);
int	iwx_clear_statistics(struct iwx_softc *);
void	iwx_add_task(struct iwx_softc *, struct taskq *, struct task *);
void	iwx_del_task(struct iwx_softc *, struct taskq *, struct task *);
int	iwx_scan(struct iwx_softc *);
int	iwx_bgscan(struct ieee80211com *);
void	iwx_bgscan_done(struct ieee80211com *,
	    struct ieee80211_node_switch_bss_arg *, size_t);
void	iwx_bgscan_done_task(void *);
int	iwx_umac_scan_abort(struct iwx_softc *);
int	iwx_scan_abort(struct iwx_softc *);
int	iwx_enable_mgmt_queue(struct iwx_softc *);
int	iwx_disable_mgmt_queue(struct iwx_softc *);
int	iwx_rs_rval2idx(uint8_t);
uint16_t iwx_rs_ht_rates(struct iwx_softc *, struct ieee80211_node *, int);
uint16_t iwx_rs_vht_rates(struct iwx_softc *, struct ieee80211_node *, int);
int	iwx_rs_init_v3(struct iwx_softc *, struct iwx_node *);
int	iwx_rs_init_v4(struct iwx_softc *, struct iwx_node *);
int	iwx_rs_init(struct iwx_softc *, struct iwx_node *);
int	iwx_phy_send_rlc(struct iwx_softc *, struct iwx_phy_ctxt *,
	    uint8_t, uint8_t);
int	iwx_phy_ctxt_update(struct iwx_softc *, struct iwx_phy_ctxt *,
	    struct ieee80211_channel *, uint8_t, uint8_t, uint32_t, uint8_t,
	    uint8_t);
int	iwx_auth(struct iwx_softc *);
int	iwx_deauth(struct iwx_softc *);
int	iwx_run(struct iwx_softc *);
int	iwx_run_stop(struct iwx_softc *);
struct ieee80211_node *iwx_node_alloc(struct ieee80211com *);
int	iwx_set_key(struct ieee80211com *, struct ieee80211_node *,
	    struct ieee80211_key *);
void	iwx_setkey_task(void *);
void	iwx_delete_key(struct ieee80211com *,
	    struct ieee80211_node *, struct ieee80211_key *);
int	iwx_media_change(struct ifnet *);
void	iwx_newstate_task(void *);
int	iwx_newstate(struct ieee80211com *, enum ieee80211_state, int);
void	iwx_endscan(struct iwx_softc *);
void	iwx_fill_sf_command(struct iwx_softc *, struct iwx_sf_cfg_cmd *,
	    struct ieee80211_node *);
int	iwx_sf_config(struct iwx_softc *, int);
int	iwx_send_bt_init_conf(struct iwx_softc *);
int	iwx_send_soc_conf(struct iwx_softc *);
int	iwx_send_update_mcc_cmd(struct iwx_softc *, const char *);
int	iwx_send_temp_report_ths_cmd(struct iwx_softc *);
int	iwx_init_hw(struct iwx_softc *);
int	iwx_init(struct ifnet *);
void	iwx_start(struct ifnet *);
void	iwx_stop(struct ifnet *);
void	iwx_watchdog(struct ifnet *);
int	iwx_ioctl(struct ifnet *, u_long, caddr_t);
const char *iwx_desc_lookup(uint32_t);
void	iwx_nic_error(struct iwx_softc *);
void	iwx_dump_driver_status(struct iwx_softc *);
void	iwx_nic_umac_error(struct iwx_softc *);
int	iwx_detect_duplicate(struct iwx_softc *, struct mbuf *,
	    struct iwx_rx_mpdu_desc *, struct ieee80211_rxinfo *);
int	iwx_is_sn_less(uint16_t, uint16_t, uint16_t);
void	iwx_release_frames(struct iwx_softc *, struct ieee80211_node *,
	    struct iwx_rxba_data *, struct iwx_reorder_buffer *, uint16_t,
	    struct mbuf_list *);
int	iwx_oldsn_workaround(struct iwx_softc *, struct ieee80211_node *,
	    int, struct iwx_reorder_buffer *, uint32_t, uint32_t);
int	iwx_rx_reorder(struct iwx_softc *, struct mbuf *, int,
	    struct iwx_rx_mpdu_desc *, int, int, uint32_t,
	    struct ieee80211_rxinfo *, struct mbuf_list *);
void	iwx_rx_mpdu_mq(struct iwx_softc *, struct mbuf *, void *, size_t,
	    struct mbuf_list *);
int	iwx_rx_pkt_valid(struct iwx_rx_packet *);
void	iwx_rx_pkt(struct iwx_softc *, struct iwx_rx_data *,
	    struct mbuf_list *);
void	iwx_notif_intr(struct iwx_softc *);
int	iwx_intr(void *);
int	iwx_intr_msix(void *);
int	iwx_match(struct device *, void *, void *);
int	iwx_preinit(struct iwx_softc *);
void	iwx_attach_hook(struct device *);
const struct iwx_device_cfg *iwx_find_device_cfg(struct iwx_softc *);
void	iwx_attach(struct device *, struct device *, void *);
void	iwx_init_task(void *);
int	iwx_activate(struct device *, int);
void	iwx_resume(struct iwx_softc *);
int	iwx_wakeup(struct iwx_softc *);

#if NBPFILTER > 0
void	iwx_radiotap_attach(struct iwx_softc *);
#endif

uint8_t
iwx_lookup_cmd_ver(struct iwx_softc *sc, uint8_t grp, uint8_t cmd)
{
	const struct iwx_fw_cmd_version *entry;
	int i;

	for (i = 0; i < sc->n_cmd_versions; i++) {
		entry = &sc->cmd_versions[i];
		if (entry->group == grp && entry->cmd == cmd)
			return entry->cmd_ver;
	}

	return IWX_FW_CMD_VER_UNKNOWN;
}

uint8_t
iwx_lookup_notif_ver(struct iwx_softc *sc, uint8_t grp, uint8_t cmd)
{
	const struct iwx_fw_cmd_version *entry;
	int i;

	for (i = 0; i < sc->n_cmd_versions; i++) {
		entry = &sc->cmd_versions[i];
		if (entry->group == grp && entry->cmd == cmd)
			return entry->notif_ver;
	}

	return IWX_FW_CMD_VER_UNKNOWN;
}

int
iwx_is_mimo_ht_plcp(uint8_t ht_plcp)
{
	switch (ht_plcp) {
	case IWX_RATE_HT_MIMO2_MCS_8_PLCP:
	case IWX_RATE_HT_MIMO2_MCS_9_PLCP:
	case IWX_RATE_HT_MIMO2_MCS_10_PLCP:
	case IWX_RATE_HT_MIMO2_MCS_11_PLCP:
	case IWX_RATE_HT_MIMO2_MCS_12_PLCP:
	case IWX_RATE_HT_MIMO2_MCS_13_PLCP:
	case IWX_RATE_HT_MIMO2_MCS_14_PLCP:
	case IWX_RATE_HT_MIMO2_MCS_15_PLCP:
		return 1;
	default:
		break;
	}

	return 0;
}

int
iwx_store_cscheme(struct iwx_softc *sc, uint8_t *data, size_t dlen)
{
	struct iwx_fw_cscheme_list *l = (void *)data;

	if (dlen < sizeof(*l) ||
	    dlen < sizeof(l->size) + l->size * sizeof(*l->cs))
		return EINVAL;

	/* we don't actually store anything for now, always use s/w crypto */

	return 0;
}

int
iwx_ctxt_info_alloc_dma(struct iwx_softc *sc,
    const struct iwx_fw_onesect *sec, struct iwx_dma_info *dram)
{
	int err = iwx_dma_contig_alloc(sc->sc_dmat, dram, sec->fws_len, 0);
	if (err) {
		printf("%s: could not allocate context info DMA memory\n",
		    DEVNAME(sc));
		return err;
	}

	memcpy(dram->vaddr, sec->fws_data, sec->fws_len);

	return 0;
}

void
iwx_ctxt_info_free_paging(struct iwx_softc *sc)
{
	struct iwx_self_init_dram *dram = &sc->init_dram;
	int i;

	if (!dram->paging)
		return;

	/* free paging*/
	for (i = 0; i < dram->paging_cnt; i++)
		iwx_dma_contig_free(&dram->paging[i]);

	free(dram->paging, M_DEVBUF, dram->paging_cnt * sizeof(*dram->paging));
	dram->paging_cnt = 0;
	dram->paging = NULL;
}

int
iwx_get_num_sections(const struct iwx_fw_sects *fws, int start)
{
	int i = 0;

	while (start < fws->fw_count &&
	       fws->fw_sect[start].fws_devoff != IWX_CPU1_CPU2_SEPARATOR_SECTION &&
	       fws->fw_sect[start].fws_devoff != IWX_PAGING_SEPARATOR_SECTION) {
		start++;
		i++;
	}

	return i;
}

int
iwx_init_fw_sec(struct iwx_softc *sc, const struct iwx_fw_sects *fws,
    struct iwx_context_info_dram *ctxt_dram)
{
	struct iwx_self_init_dram *dram = &sc->init_dram;
	int i, ret, fw_cnt = 0;

	KASSERT(dram->paging == NULL);

	dram->lmac_cnt = iwx_get_num_sections(fws, 0);
	/* add 1 due to separator */
	dram->umac_cnt = iwx_get_num_sections(fws, dram->lmac_cnt + 1);
	/* add 2 due to separators */
	dram->paging_cnt = iwx_get_num_sections(fws,
	    dram->lmac_cnt + dram->umac_cnt + 2);

	dram->fw = mallocarray(dram->umac_cnt + dram->lmac_cnt,
	    sizeof(*dram->fw), M_DEVBUF,  M_ZERO | M_NOWAIT);
	if (!dram->fw) {
		printf("%s: could not allocate memory for firmware sections\n",
		    DEVNAME(sc));
		return ENOMEM;
	}

	dram->paging = mallocarray(dram->paging_cnt, sizeof(*dram->paging),
	    M_DEVBUF, M_ZERO | M_NOWAIT);
	if (!dram->paging) {
		printf("%s: could not allocate memory for firmware paging\n",
		    DEVNAME(sc));
		return ENOMEM;
	}

	/* initialize lmac sections */
	for (i = 0; i < dram->lmac_cnt; i++) {
		ret = iwx_ctxt_info_alloc_dma(sc, &fws->fw_sect[i],
						   &dram->fw[fw_cnt]);
		if (ret)
			return ret;
		ctxt_dram->lmac_img[i] =
			htole64(dram->fw[fw_cnt].paddr);
		DPRINTF(("%s: firmware LMAC section %d at 0x%llx size %lld\n", __func__, i,
		    (unsigned long long)dram->fw[fw_cnt].paddr,
		    (unsigned long long)dram->fw[fw_cnt].size));
		fw_cnt++;
	}

	/* initialize umac sections */
	for (i = 0; i < dram->umac_cnt; i++) {
		/* access FW with +1 to make up for lmac separator */
		ret = iwx_ctxt_info_alloc_dma(sc,
		    &fws->fw_sect[fw_cnt + 1], &dram->fw[fw_cnt]);
		if (ret)
			return ret;
		ctxt_dram->umac_img[i] =
			htole64(dram->fw[fw_cnt].paddr);
		DPRINTF(("%s: firmware UMAC section %d at 0x%llx size %lld\n", __func__, i,
			(unsigned long long)dram->fw[fw_cnt].paddr,
			(unsigned long long)dram->fw[fw_cnt].size));
		fw_cnt++;
	}

	/*
	 * Initialize paging.
	 * Paging memory isn't stored in dram->fw as the umac and lmac - it is
	 * stored separately.
	 * This is since the timing of its release is different -
	 * while fw memory can be released on alive, the paging memory can be
	 * freed only when the device goes down.
	 * Given that, the logic here in accessing the fw image is a bit
	 * different - fw_cnt isn't changing so loop counter is added to it.
	 */
	for (i = 0; i < dram->paging_cnt; i++) {
		/* access FW with +2 to make up for lmac & umac separators */
		int fw_idx = fw_cnt + i + 2;

		ret = iwx_ctxt_info_alloc_dma(sc,
		    &fws->fw_sect[fw_idx], &dram->paging[i]);
		if (ret)
			return ret;

		ctxt_dram->virtual_img[i] = htole64(dram->paging[i].paddr);
		DPRINTF(("%s: firmware paging section %d at 0x%llx size %lld\n", __func__, i,
		    (unsigned long long)dram->paging[i].paddr,
		    (unsigned long long)dram->paging[i].size));
	}

	return 0;
}

void
iwx_fw_version_str(char *buf, size_t bufsize,
    uint32_t major, uint32_t minor, uint32_t api)
{
	/*
	 * Starting with major version 35 the Linux driver prints the minor
	 * version in hexadecimal.
	 */
	if (major >= 35)
		snprintf(buf, bufsize, "%u.%08x.%u", major, minor, api);
	else
		snprintf(buf, bufsize, "%u.%u.%u", major, minor, api);
}

int
iwx_alloc_fw_monitor_block(struct iwx_softc *sc, uint8_t max_power,
    uint8_t min_power)
{
	struct iwx_dma_info *fw_mon = &sc->fw_mon;
	uint32_t size = 0;
	uint8_t power;
	int err;

	if (fw_mon->size)
		return 0;

	for (power = max_power; power >= min_power; power--) {
		size = (1 << power);

		err = iwx_dma_contig_alloc(sc->sc_dmat, fw_mon, size, 0);
		if (err)
			continue;

		DPRINTF(("%s: allocated 0x%08x bytes for firmware monitor.\n",
			 DEVNAME(sc), size));
		break;
	}

	if (err) {
		fw_mon->size = 0;
		return err;
	}

	if (power != max_power)
		DPRINTF(("%s: Sorry - debug buffer is only %luK while you requested %luK\n",
			DEVNAME(sc), (unsigned long)(1 << (power - 10)),
			(unsigned long)(1 << (max_power - 10))));

	return 0;
}

int
iwx_alloc_fw_monitor(struct iwx_softc *sc, uint8_t max_power)
{
	if (!max_power) {
		/* default max_power is maximum */
		max_power = 26;
	} else {
		max_power += 11;
	}

	if (max_power > 26) {
		DPRINTF(("%s: External buffer size for monitor is too big %d, "
		     "check the FW TLV\n", DEVNAME(sc), max_power));
		return 0;
	}

	if (sc->fw_mon.size)
		return 0;

	return iwx_alloc_fw_monitor_block(sc, max_power, 11);
}

int
iwx_apply_debug_destination(struct iwx_softc *sc)
{
	struct iwx_fw_dbg_dest_tlv_v1 *dest_v1;
	int i, err;
	uint8_t mon_mode, size_power, base_shift, end_shift;
	uint32_t base_reg, end_reg;

	dest_v1 = sc->sc_fw.dbg_dest_tlv_v1;
	mon_mode = dest_v1->monitor_mode;
	size_power = dest_v1->size_power;
	base_reg = le32toh(dest_v1->base_reg);
	end_reg = le32toh(dest_v1->end_reg);
	base_shift = dest_v1->base_shift;
	end_shift = dest_v1->end_shift;

	DPRINTF(("%s: applying debug destination %d\n", DEVNAME(sc), mon_mode));

	if (mon_mode == EXTERNAL_MODE) {
		err = iwx_alloc_fw_monitor(sc, size_power);
		if (err)
			return err;
	}

	if (!iwx_nic_lock(sc))
		return EBUSY;

	for (i = 0; i < sc->sc_fw.n_dest_reg; i++) {
		uint32_t addr, val;
		uint8_t op;

		addr = le32toh(dest_v1->reg_ops[i].addr);
		val = le32toh(dest_v1->reg_ops[i].val);
		op = dest_v1->reg_ops[i].op;

		DPRINTF(("%s: op=%u addr=%u val=%u\n", __func__, op, addr, val));
		switch (op) {
		case CSR_ASSIGN:
			IWX_WRITE(sc, addr, val);
			break;
		case CSR_SETBIT:
			IWX_SETBITS(sc, addr, (1 << val));
			break;
		case CSR_CLEARBIT:
			IWX_CLRBITS(sc, addr, (1 << val));
			break;
		case PRPH_ASSIGN:
			iwx_write_prph(sc, addr, val);
			break;
		case PRPH_SETBIT:
			err = iwx_set_bits_prph(sc, addr, (1 << val));
			if (err)
				return err;
			break;
		case PRPH_CLEARBIT:
			err = iwx_clear_bits_prph(sc, addr, (1 << val));
			if (err)
				return err;
			break;
		case PRPH_BLOCKBIT:
			if (iwx_read_prph(sc, addr) & (1 << val))
				goto monitor;
			break;
		default:
			DPRINTF(("%s: FW debug - unknown OP %d\n",
			    DEVNAME(sc), op));
			break;
		}
	}

monitor:
	if (mon_mode == EXTERNAL_MODE && sc->fw_mon.size) {
		iwx_write_prph(sc, le32toh(base_reg),
		    sc->fw_mon.paddr >> base_shift);
		iwx_write_prph(sc, end_reg,
		    (sc->fw_mon.paddr + sc->fw_mon.size - 256)
		    >> end_shift);
	}

	iwx_nic_unlock(sc);
	return 0;
}

void
iwx_set_ltr(struct iwx_softc *sc)
{
	uint32_t ltr_val = IWX_CSR_LTR_LONG_VAL_AD_NO_SNOOP_REQ |
	    ((IWX_CSR_LTR_LONG_VAL_AD_SCALE_USEC <<
	    IWX_CSR_LTR_LONG_VAL_AD_NO_SNOOP_SCALE_SHIFT) &
	    IWX_CSR_LTR_LONG_VAL_AD_NO_SNOOP_SCALE_MASK) |
	    ((250 << IWX_CSR_LTR_LONG_VAL_AD_NO_SNOOP_VAL_SHIFT) &
	    IWX_CSR_LTR_LONG_VAL_AD_NO_SNOOP_VAL_MASK) |
	    IWX_CSR_LTR_LONG_VAL_AD_SNOOP_REQ |
	    ((IWX_CSR_LTR_LONG_VAL_AD_SCALE_USEC <<
	    IWX_CSR_LTR_LONG_VAL_AD_SNOOP_SCALE_SHIFT) &
	    IWX_CSR_LTR_LONG_VAL_AD_SNOOP_SCALE_MASK) |
	    (250 & IWX_CSR_LTR_LONG_VAL_AD_SNOOP_VAL);

	/*
	 * To workaround hardware latency issues during the boot process,
	 * initialize the LTR to ~250 usec (see ltr_val above).
	 * The firmware initializes this again later (to a smaller value).
	 */
	if (!sc->sc_integrated) {
		IWX_WRITE(sc, IWX_CSR_LTR_LONG_VAL_AD, ltr_val);
	} else if (sc->sc_integrated &&
		   sc->sc_device_family == IWX_DEVICE_FAMILY_22000) {
		iwx_write_prph(sc, IWX_HPM_MAC_LTR_CSR,
		    IWX_HPM_MAC_LRT_ENABLE_ALL);
		iwx_write_prph(sc, IWX_HPM_UMAC_LTR, ltr_val);
	}
}

int
iwx_ctxt_info_init(struct iwx_softc *sc, const struct iwx_fw_sects *fws)
{
	struct iwx_context_info *ctxt_info;
	struct iwx_context_info_rbd_cfg *rx_cfg;
	uint32_t control_flags = 0;
	uint64_t paddr;
	int err;

	ctxt_info = sc->ctxt_info_dma.vaddr;
	memset(ctxt_info, 0, sizeof(*ctxt_info));

	ctxt_info->version.version = 0;
	ctxt_info->version.mac_id =
		htole16((uint16_t)IWX_READ(sc, IWX_CSR_HW_REV));
	/* size is in DWs */
	ctxt_info->version.size = htole16(sizeof(*ctxt_info) / 4);

	KASSERT(IWX_RX_QUEUE_CB_SIZE(IWX_MQ_RX_TABLE_SIZE) < 0xF);
	control_flags = IWX_CTXT_INFO_TFD_FORMAT_LONG |
			(IWX_RX_QUEUE_CB_SIZE(IWX_MQ_RX_TABLE_SIZE) <<
			 IWX_CTXT_INFO_RB_CB_SIZE_POS) |
			(IWX_CTXT_INFO_RB_SIZE_4K << IWX_CTXT_INFO_RB_SIZE_POS);
	ctxt_info->control.control_flags = htole32(control_flags);

	/* initialize RX default queue */
	rx_cfg = &ctxt_info->rbd_cfg;
	rx_cfg->free_rbd_addr = htole64(sc->rxq.free_desc_dma.paddr);
	rx_cfg->used_rbd_addr = htole64(sc->rxq.used_desc_dma.paddr);
	rx_cfg->status_wr_ptr = htole64(sc->rxq.stat_dma.paddr);

	/* initialize TX command queue */
	ctxt_info->hcmd_cfg.cmd_queue_addr =
	    htole64(sc->txq[IWX_DQA_CMD_QUEUE].desc_dma.paddr);
	ctxt_info->hcmd_cfg.cmd_queue_size =
		IWX_TFD_QUEUE_CB_SIZE(IWX_TX_RING_COUNT);

	/* allocate ucode sections in dram and set addresses */
	err = iwx_init_fw_sec(sc, fws, &ctxt_info->dram);
	if (err) {
		iwx_ctxt_info_free_fw_img(sc);
		return err;
	}

	/* Configure debug, if exists */
	if (sc->sc_fw.dbg_dest_tlv_v1) {
		err = iwx_apply_debug_destination(sc);
		if (err) {
			iwx_ctxt_info_free_fw_img(sc);
			return err;
		}
	}

	/*
	 * Write the context info DMA base address. The device expects a
	 * 64-bit address but a simple bus_space_write_8 to this register
	 * won't work on some devices, such as the AX201.
	 */
	paddr = sc->ctxt_info_dma.paddr;
	IWX_WRITE(sc, IWX_CSR_CTXT_INFO_BA, paddr & 0xffffffff);
	IWX_WRITE(sc, IWX_CSR_CTXT_INFO_BA + 4, paddr >> 32);

	/* kick FW self load */
	if (!iwx_nic_lock(sc)) {
		iwx_ctxt_info_free_fw_img(sc);
		return EBUSY;
	}

	iwx_set_ltr(sc);
	iwx_write_prph(sc, IWX_UREG_CPU_INIT_RUN, 1);
	iwx_nic_unlock(sc);

	/* Context info will be released upon alive or failure to get one */

	return 0;
}

int
iwx_ctxt_info_gen3_init(struct iwx_softc *sc, const struct iwx_fw_sects *fws)
{
	struct iwx_context_info_gen3 *ctxt_info_gen3;
	struct iwx_prph_scratch *prph_scratch;
	struct iwx_prph_scratch_ctrl_cfg *prph_sc_ctrl;
	uint16_t cb_size;
	uint32_t control_flags, scratch_size;
	uint64_t paddr;
	int err;

	if (sc->sc_fw.iml == NULL || sc->sc_fw.iml_len == 0) {
		printf("%s: no image loader found in firmware file\n",
		    DEVNAME(sc));
		iwx_ctxt_info_free_fw_img(sc);
		return EINVAL;
	}

	err = iwx_dma_contig_alloc(sc->sc_dmat, &sc->iml_dma,
	    sc->sc_fw.iml_len, 0);
	if (err) {
		printf("%s: could not allocate DMA memory for "
		    "firmware image loader\n", DEVNAME(sc));
		iwx_ctxt_info_free_fw_img(sc);
		return ENOMEM;
	}

	prph_scratch = sc->prph_scratch_dma.vaddr;
	memset(prph_scratch, 0, sizeof(*prph_scratch));
	prph_sc_ctrl = &prph_scratch->ctrl_cfg;
	prph_sc_ctrl->version.version = 0;
	prph_sc_ctrl->version.mac_id = htole16(IWX_READ(sc, IWX_CSR_HW_REV));
	prph_sc_ctrl->version.size = htole16(sizeof(*prph_scratch) / 4);

	control_flags = IWX_PRPH_SCRATCH_RB_SIZE_4K |
	    IWX_PRPH_SCRATCH_MTR_MODE |
	    (IWX_PRPH_MTR_FORMAT_256B & IWX_PRPH_SCRATCH_MTR_FORMAT);
	if (sc->sc_imr_enabled)
		control_flags |= IWX_PRPH_SCRATCH_IMR_DEBUG_EN;
	prph_sc_ctrl->control.control_flags = htole32(control_flags);

	/* initialize RX default queue */
	prph_sc_ctrl->rbd_cfg.free_rbd_addr =
	    htole64(sc->rxq.free_desc_dma.paddr);

	/* allocate ucode sections in dram and set addresses */
	err = iwx_init_fw_sec(sc, fws, &prph_scratch->dram);
	if (err) {
		iwx_dma_contig_free(&sc->iml_dma);
		iwx_ctxt_info_free_fw_img(sc);
		return err;
	}

	ctxt_info_gen3 = sc->ctxt_info_dma.vaddr;
	memset(ctxt_info_gen3, 0, sizeof(*ctxt_info_gen3));
	ctxt_info_gen3->prph_info_base_addr = htole64(sc->prph_info_dma.paddr);
	ctxt_info_gen3->prph_scratch_base_addr =
	    htole64(sc->prph_scratch_dma.paddr);
	scratch_size = sizeof(*prph_scratch);
	ctxt_info_gen3->prph_scratch_size = htole32(scratch_size);
	ctxt_info_gen3->cr_head_idx_arr_base_addr =
	    htole64(sc->rxq.stat_dma.paddr);
	ctxt_info_gen3->tr_tail_idx_arr_base_addr =
	    htole64(sc->prph_info_dma.paddr + PAGE_SIZE / 2);
	ctxt_info_gen3->cr_tail_idx_arr_base_addr =
	    htole64(sc->prph_info_dma.paddr + 3 * PAGE_SIZE / 4);
	ctxt_info_gen3->mtr_base_addr =
	    htole64(sc->txq[IWX_DQA_CMD_QUEUE].desc_dma.paddr);
	ctxt_info_gen3->mcr_base_addr = htole64(sc->rxq.used_desc_dma.paddr);
	cb_size = IWX_TFD_QUEUE_CB_SIZE(IWX_TX_RING_COUNT);
	ctxt_info_gen3->mtr_size = htole16(cb_size);
	cb_size = IWX_RX_QUEUE_CB_SIZE(IWX_MQ_RX_TABLE_SIZE);
	ctxt_info_gen3->mcr_size = htole16(cb_size);

	memcpy(sc->iml_dma.vaddr, sc->sc_fw.iml, sc->sc_fw.iml_len);

	paddr = sc->ctxt_info_dma.paddr;
	IWX_WRITE(sc, IWX_CSR_CTXT_INFO_ADDR, paddr & 0xffffffff);
	IWX_WRITE(sc, IWX_CSR_CTXT_INFO_ADDR + 4, paddr >> 32);

	paddr = sc->iml_dma.paddr;
	IWX_WRITE(sc, IWX_CSR_IML_DATA_ADDR, paddr & 0xffffffff);
	IWX_WRITE(sc, IWX_CSR_IML_DATA_ADDR + 4, paddr >> 32);
	IWX_WRITE(sc, IWX_CSR_IML_SIZE_ADDR, sc->sc_fw.iml_len);

	IWX_SETBITS(sc, IWX_CSR_CTXT_INFO_BOOT_CTRL,
		    IWX_CSR_AUTO_FUNC_BOOT_ENA);

	/* kick FW self load */
	if (!iwx_nic_lock(sc)) {
		iwx_dma_contig_free(&sc->iml_dma);
		iwx_ctxt_info_free_fw_img(sc);
		return EBUSY;
	}
	iwx_set_ltr(sc);
	iwx_write_umac_prph(sc, IWX_UREG_CPU_INIT_RUN, 1);
	iwx_nic_unlock(sc);

	/* Context info will be released upon alive or failure to get one */
	return 0;
}

void
iwx_ctxt_info_free_fw_img(struct iwx_softc *sc)
{
	struct iwx_self_init_dram *dram = &sc->init_dram;
	int i;

	if (!dram->fw)
		return;

	for (i = 0; i < dram->lmac_cnt + dram->umac_cnt; i++)
		iwx_dma_contig_free(&dram->fw[i]);

	free(dram->fw, M_DEVBUF,
	    (dram->lmac_cnt + dram->umac_cnt) * sizeof(*dram->fw));
	dram->lmac_cnt = 0;
	dram->umac_cnt = 0;
	dram->fw = NULL;
}

int
iwx_firmware_store_section(struct iwx_softc *sc, enum iwx_ucode_type type,
    uint8_t *data, size_t dlen)
{
	struct iwx_fw_sects *fws;
	struct iwx_fw_onesect *fwone;

	if (type >= IWX_UCODE_TYPE_MAX)
		return EINVAL;
	if (dlen < sizeof(uint32_t))
		return EINVAL;

	fws = &sc->sc_fw.fw_sects[type];
	DPRINTF(("%s: ucode type %d section %d\n", DEVNAME(sc), type, fws->fw_count));
	if (fws->fw_count >= IWX_UCODE_SECT_MAX)
		return EINVAL;

	fwone = &fws->fw_sect[fws->fw_count];

	/* first 32bit are device load offset */
	memcpy(&fwone->fws_devoff, data, sizeof(uint32_t));

	/* rest is data */
	fwone->fws_data = data + sizeof(uint32_t);
	fwone->fws_len = dlen - sizeof(uint32_t);

	fws->fw_count++;
	fws->fw_totlen += fwone->fws_len;

	return 0;
}

#define IWX_DEFAULT_SCAN_CHANNELS	40
/* Newer firmware might support more channels. Raise this value if needed. */
#define IWX_MAX_SCAN_CHANNELS		67 /* as of iwx-cc-a0-62 firmware */

struct iwx_tlv_calib_data {
	uint32_t ucode_type;
	struct iwx_tlv_calib_ctrl calib;
} __packed;

int
iwx_set_default_calib(struct iwx_softc *sc, const void *data)
{
	const struct iwx_tlv_calib_data *def_calib = data;
	uint32_t ucode_type = le32toh(def_calib->ucode_type);

	if (ucode_type >= IWX_UCODE_TYPE_MAX)
		return EINVAL;

	sc->sc_default_calib[ucode_type].flow_trigger =
	    def_calib->calib.flow_trigger;
	sc->sc_default_calib[ucode_type].event_trigger =
	    def_calib->calib.event_trigger;

	return 0;
}

void
iwx_fw_info_free(struct iwx_fw_info *fw)
{
	free(fw->fw_rawdata, M_DEVBUF, fw->fw_rawsize);
	fw->fw_rawdata = NULL;
	fw->fw_rawsize = 0;
	/* don't touch fw->fw_status */
	memset(fw->fw_sects, 0, sizeof(fw->fw_sects));
	free(fw->iml, M_DEVBUF, fw->iml_len);
	fw->iml = NULL;
	fw->iml_len = 0;
}

#define IWX_FW_ADDR_CACHE_CONTROL 0xC0000000

int
iwx_read_firmware(struct iwx_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct iwx_fw_info *fw = &sc->sc_fw;
	struct iwx_tlv_ucode_header *uhdr;
	struct iwx_ucode_tlv tlv;
	uint32_t tlv_type;
	uint8_t *data;
	int err;
	size_t len;

	if (fw->fw_status == IWX_FW_STATUS_DONE)
		return 0;

	while (fw->fw_status == IWX_FW_STATUS_INPROGRESS)
		tsleep_nsec(&sc->sc_fw, 0, "iwxfwp", INFSLP);
	fw->fw_status = IWX_FW_STATUS_INPROGRESS;

	if (fw->fw_rawdata != NULL)
		iwx_fw_info_free(fw);

	err = loadfirmware(sc->sc_fwname,
	    (u_char **)&fw->fw_rawdata, &fw->fw_rawsize);
	if (err) {
		printf("%s: could not read firmware %s (error %d)\n",
		    DEVNAME(sc), sc->sc_fwname, err);
		goto out;
	}

	if (ic->ic_if.if_flags & IFF_DEBUG)
		printf("%s: using firmware %s\n", DEVNAME(sc), sc->sc_fwname);

	sc->sc_capaflags = 0;
	sc->sc_capa_n_scan_channels = IWX_DEFAULT_SCAN_CHANNELS;
	memset(sc->sc_enabled_capa, 0, sizeof(sc->sc_enabled_capa));
	memset(sc->sc_ucode_api, 0, sizeof(sc->sc_ucode_api));
	sc->n_cmd_versions = 0;

	uhdr = (void *)fw->fw_rawdata;
	if (*(uint32_t *)fw->fw_rawdata != 0
	    || le32toh(uhdr->magic) != IWX_TLV_UCODE_MAGIC) {
		printf("%s: invalid firmware %s\n",
		    DEVNAME(sc), sc->sc_fwname);
		err = EINVAL;
		goto out;
	}

	iwx_fw_version_str(sc->sc_fwver, sizeof(sc->sc_fwver),
	    IWX_UCODE_MAJOR(le32toh(uhdr->ver)),
	    IWX_UCODE_MINOR(le32toh(uhdr->ver)),
	    IWX_UCODE_API(le32toh(uhdr->ver)));

	data = uhdr->data;
	len = fw->fw_rawsize - sizeof(*uhdr);

	while (len >= sizeof(tlv)) {
		size_t tlv_len;
		void *tlv_data;

		memcpy(&tlv, data, sizeof(tlv));
		tlv_len = le32toh(tlv.length);
		tlv_type = le32toh(tlv.type);

		len -= sizeof(tlv);
		data += sizeof(tlv);
		tlv_data = data;

		if (len < tlv_len) {
			printf("%s: firmware too short: %zu bytes\n",
			    DEVNAME(sc), len);
			err = EINVAL;
			goto parse_out;
		}

		switch (tlv_type) {
		case IWX_UCODE_TLV_PROBE_MAX_LEN:
			if (tlv_len < sizeof(uint32_t)) {
				err = EINVAL;
				goto parse_out;
			}
			sc->sc_capa_max_probe_len
			    = le32toh(*(uint32_t *)tlv_data);
			if (sc->sc_capa_max_probe_len >
			    IWX_SCAN_OFFLOAD_PROBE_REQ_SIZE) {
				err = EINVAL;
				goto parse_out;
			}
			break;
		case IWX_UCODE_TLV_PAN:
			if (tlv_len) {
				err = EINVAL;
				goto parse_out;
			}
			sc->sc_capaflags |= IWX_UCODE_TLV_FLAGS_PAN;
			break;
		case IWX_UCODE_TLV_FLAGS:
			if (tlv_len < sizeof(uint32_t)) {
				err = EINVAL;
				goto parse_out;
			}
			/*
			 * Apparently there can be many flags, but Linux driver
			 * parses only the first one, and so do we.
			 *
			 * XXX: why does this override IWX_UCODE_TLV_PAN?
			 * Intentional or a bug?  Observations from
			 * current firmware file:
			 *  1) TLV_PAN is parsed first
			 *  2) TLV_FLAGS contains TLV_FLAGS_PAN
			 * ==> this resets TLV_PAN to itself... hnnnk
			 */
			sc->sc_capaflags = le32toh(*(uint32_t *)tlv_data);
			break;
		case IWX_UCODE_TLV_CSCHEME:
			err = iwx_store_cscheme(sc, tlv_data, tlv_len);
			if (err)
				goto parse_out;
			break;
		case IWX_UCODE_TLV_NUM_OF_CPU: {
			uint32_t num_cpu;
			if (tlv_len != sizeof(uint32_t)) {
				err = EINVAL;
				goto parse_out;
			}
			num_cpu = le32toh(*(uint32_t *)tlv_data);
			if (num_cpu < 1 || num_cpu > 2) {
				err = EINVAL;
				goto parse_out;
			}
			break;
		}
		case IWX_UCODE_TLV_SEC_RT:
			err = iwx_firmware_store_section(sc,
			    IWX_UCODE_TYPE_REGULAR, tlv_data, tlv_len);
			if (err)
				goto parse_out;
			break;
		case IWX_UCODE_TLV_SEC_INIT:
			err = iwx_firmware_store_section(sc,
			    IWX_UCODE_TYPE_INIT, tlv_data, tlv_len);
			if (err)
				goto parse_out;
			break;
		case IWX_UCODE_TLV_SEC_WOWLAN:
			err = iwx_firmware_store_section(sc,
			    IWX_UCODE_TYPE_WOW, tlv_data, tlv_len);
			if (err)
				goto parse_out;
			break;
		case IWX_UCODE_TLV_DEF_CALIB:
			if (tlv_len != sizeof(struct iwx_tlv_calib_data)) {
				err = EINVAL;
				goto parse_out;
			}
			err = iwx_set_default_calib(sc, tlv_data);
			if (err)
				goto parse_out;
			break;
		case IWX_UCODE_TLV_PHY_SKU:
			if (tlv_len != sizeof(uint32_t)) {
				err = EINVAL;
				goto parse_out;
			}
			sc->sc_fw_phy_config = le32toh(*(uint32_t *)tlv_data);
			break;

		case IWX_UCODE_TLV_API_CHANGES_SET: {
			struct iwx_ucode_api *api;
			int idx, i;
			if (tlv_len != sizeof(*api)) {
				err = EINVAL;
				goto parse_out;
			}
			api = (struct iwx_ucode_api *)tlv_data;
			idx = le32toh(api->api_index);
			if (idx >= howmany(IWX_NUM_UCODE_TLV_API, 32)) {
				err = EINVAL;
				goto parse_out;
			}
			for (i = 0; i < 32; i++) {
				if ((le32toh(api->api_flags) & (1 << i)) == 0)
					continue;
				setbit(sc->sc_ucode_api, i + (32 * idx));
			}
			break;
		}

		case IWX_UCODE_TLV_ENABLED_CAPABILITIES: {
			struct iwx_ucode_capa *capa;
			int idx, i;
			if (tlv_len != sizeof(*capa)) {
				err = EINVAL;
				goto parse_out;
			}
			capa = (struct iwx_ucode_capa *)tlv_data;
			idx = le32toh(capa->api_index);
			if (idx >= howmany(IWX_NUM_UCODE_TLV_CAPA, 32)) {
				goto parse_out;
			}
			for (i = 0; i < 32; i++) {
				if ((le32toh(capa->api_capa) & (1 << i)) == 0)
					continue;
				setbit(sc->sc_enabled_capa, i + (32 * idx));
			}
			break;
		}

		case IWX_UCODE_TLV_SDIO_ADMA_ADDR:
		case IWX_UCODE_TLV_FW_GSCAN_CAPA:
			/* ignore, not used by current driver */
			break;

		case IWX_UCODE_TLV_SEC_RT_USNIFFER:
			err = iwx_firmware_store_section(sc,
			    IWX_UCODE_TYPE_REGULAR_USNIFFER, tlv_data,
			    tlv_len);
			if (err)
				goto parse_out;
			break;

		case IWX_UCODE_TLV_PAGING:
			if (tlv_len != sizeof(uint32_t)) {
				err = EINVAL;
				goto parse_out;
			}
			break;

		case IWX_UCODE_TLV_N_SCAN_CHANNELS:
			if (tlv_len != sizeof(uint32_t)) {
				err = EINVAL;
				goto parse_out;
			}
			sc->sc_capa_n_scan_channels =
			  le32toh(*(uint32_t *)tlv_data);
			if (sc->sc_capa_n_scan_channels > IWX_MAX_SCAN_CHANNELS) {
				err = ERANGE;
				goto parse_out;
			}
			break;

		case IWX_UCODE_TLV_FW_VERSION:
			if (tlv_len != sizeof(uint32_t) * 3) {
				err = EINVAL;
				goto parse_out;
			}

			iwx_fw_version_str(sc->sc_fwver, sizeof(sc->sc_fwver),
			    le32toh(((uint32_t *)tlv_data)[0]),
			    le32toh(((uint32_t *)tlv_data)[1]),
			    le32toh(((uint32_t *)tlv_data)[2]));
			break;

		case IWX_UCODE_TLV_FW_DBG_DEST: {
			struct iwx_fw_dbg_dest_tlv_v1 *dest_v1 = NULL;

			fw->dbg_dest_ver = (uint8_t *)tlv_data;
			if (*fw->dbg_dest_ver != 0) {
				err = EINVAL;
				goto parse_out;
			}

			if (fw->dbg_dest_tlv_init)
				break;
			fw->dbg_dest_tlv_init = true;

			dest_v1 = (void *)tlv_data;
			fw->dbg_dest_tlv_v1 = dest_v1;
			fw->n_dest_reg = tlv_len -
			    offsetof(struct iwx_fw_dbg_dest_tlv_v1, reg_ops);
			fw->n_dest_reg /= sizeof(dest_v1->reg_ops[0]);
			DPRINTF(("%s: found debug dest; n_dest_reg=%d\n", __func__, fw->n_dest_reg));
			break;
		}

		case IWX_UCODE_TLV_FW_DBG_CONF: {
			struct iwx_fw_dbg_conf_tlv *conf = (void *)tlv_data;

			if (!fw->dbg_dest_tlv_init ||
			    conf->id >= nitems(fw->dbg_conf_tlv) ||
			    fw->dbg_conf_tlv[conf->id] != NULL)
				break;

			DPRINTF(("Found debug configuration: %d\n", conf->id));
			fw->dbg_conf_tlv[conf->id] = conf;
			fw->dbg_conf_tlv_len[conf->id] = tlv_len;
			break;
		}

		case IWX_UCODE_TLV_UMAC_DEBUG_ADDRS: {
			struct iwx_umac_debug_addrs *dbg_ptrs =
				(void *)tlv_data;

			if (tlv_len != sizeof(*dbg_ptrs)) {
				err = EINVAL;
				goto parse_out;
			}
			if (sc->sc_device_family < IWX_DEVICE_FAMILY_22000)
				break;
			sc->sc_uc.uc_umac_error_event_table =
				le32toh(dbg_ptrs->error_info_addr) &
				~IWX_FW_ADDR_CACHE_CONTROL;
			sc->sc_uc.error_event_table_tlv_status |=
				IWX_ERROR_EVENT_TABLE_UMAC;
			break;
		}

		case IWX_UCODE_TLV_LMAC_DEBUG_ADDRS: {
			struct iwx_lmac_debug_addrs *dbg_ptrs =
				(void *)tlv_data;

			if (tlv_len != sizeof(*dbg_ptrs)) {
				err = EINVAL;
				goto parse_out;
			}
			if (sc->sc_device_family < IWX_DEVICE_FAMILY_22000)
				break;
			sc->sc_uc.uc_lmac_error_event_table[0] =
				le32toh(dbg_ptrs->error_event_table_ptr) &
				~IWX_FW_ADDR_CACHE_CONTROL;
			sc->sc_uc.error_event_table_tlv_status |=
				IWX_ERROR_EVENT_TABLE_LMAC1;
			break;
		}

		case IWX_UCODE_TLV_FW_MEM_SEG:
			break;

		case IWX_UCODE_TLV_IML:
			if (sc->sc_fw.iml != NULL) {
				free(fw->iml, M_DEVBUF, fw->iml_len);
				fw->iml_len = 0;
			}
			sc->sc_fw.iml = malloc(tlv_len, M_DEVBUF,
			    M_WAIT | M_CANFAIL | M_ZERO);
			if (sc->sc_fw.iml == NULL) {
				err = ENOMEM;
				goto parse_out;
			}
			memcpy(sc->sc_fw.iml, tlv_data, tlv_len);
			sc->sc_fw.iml_len = tlv_len;
			break;

		case IWX_UCODE_TLV_CMD_VERSIONS:
			if (tlv_len % sizeof(struct iwx_fw_cmd_version)) {
				tlv_len /= sizeof(struct iwx_fw_cmd_version);
				tlv_len *= sizeof(struct iwx_fw_cmd_version);
			}
			if (sc->n_cmd_versions != 0) {
				err = EINVAL;
				goto parse_out;
			}
			if (tlv_len > sizeof(sc->cmd_versions)) {
				err = EINVAL;
				goto parse_out;
			}
			memcpy(&sc->cmd_versions[0], tlv_data, tlv_len);
			sc->n_cmd_versions = tlv_len / sizeof(struct iwx_fw_cmd_version);
			break;

		case IWX_UCODE_TLV_FW_RECOVERY_INFO:
			break;

		case IWX_UCODE_TLV_FW_FSEQ_VERSION:
		case IWX_UCODE_TLV_PHY_INTEGRATION_VERSION:
		case IWX_UCODE_TLV_FW_NUM_STATIONS:
		case IWX_UCODE_TLV_FW_NUM_BEACONS:
			break;

		/* undocumented TLVs found in iwx-cc-a0-46 image */
		case 58:
		case 0x1000003:
		case 0x1000004:
			break;

		/* undocumented TLVs found in iwx-cc-a0-48 image */
		case 0x1000000:
		case 0x1000002:
			break;

		case IWX_UCODE_TLV_TYPE_DEBUG_INFO:
		case IWX_UCODE_TLV_TYPE_BUFFER_ALLOCATION:
		case IWX_UCODE_TLV_TYPE_HCMD:
		case IWX_UCODE_TLV_TYPE_REGIONS:
		case IWX_UCODE_TLV_TYPE_TRIGGERS:
		case IWX_UCODE_TLV_TYPE_CONF_SET:
		case IWX_UCODE_TLV_SEC_TABLE_ADDR:
		case IWX_UCODE_TLV_D3_KEK_KCK_ADDR:
		case IWX_UCODE_TLV_CURRENT_PC:
			break;

		/* undocumented TLV found in iwx-cc-a0-67 image */
		case 0x100000b:
			break;

		/* undocumented TLV found in iwx-ty-a0-gf-a0-73 image */
		case 0x101:
			break;

		/* undocumented TLV found in iwx-ty-a0-gf-a0-77 image */
		case 0x100000c:
			break;

		default:
			err = EINVAL;
			goto parse_out;
		}

		/*
		 * Check for size_t overflow and ignore missing padding at
		 * end of firmware file.
		 */
		if (roundup(tlv_len, 4) > len)
			break;

		len -= roundup(tlv_len, 4);
		data += roundup(tlv_len, 4);
	}

	KASSERT(err == 0);

 parse_out:
	if (err) {
		printf("%s: firmware parse error %d, "
		    "section type %d\n", DEVNAME(sc), err, tlv_type);
	}

 out:
	if (err) {
		fw->fw_status = IWX_FW_STATUS_NONE;
		if (fw->fw_rawdata != NULL)
			iwx_fw_info_free(fw);
	} else
		fw->fw_status = IWX_FW_STATUS_DONE;
	wakeup(&sc->sc_fw);

	return err;
}

uint32_t
iwx_prph_addr_mask(struct iwx_softc *sc)
{
	if (sc->sc_device_family >= IWX_DEVICE_FAMILY_AX210)
		return 0x00ffffff;
	else
		return 0x000fffff;
}

uint32_t
iwx_read_prph_unlocked(struct iwx_softc *sc, uint32_t addr)
{
	uint32_t mask = iwx_prph_addr_mask(sc);
	IWX_WRITE(sc, IWX_HBUS_TARG_PRPH_RADDR, ((addr & mask) | (3 << 24)));
	IWX_BARRIER_READ_WRITE(sc);
	return IWX_READ(sc, IWX_HBUS_TARG_PRPH_RDAT);
}

uint32_t
iwx_read_prph(struct iwx_softc *sc, uint32_t addr)
{
	iwx_nic_assert_locked(sc);
	return iwx_read_prph_unlocked(sc, addr);
}

void
iwx_write_prph_unlocked(struct iwx_softc *sc, uint32_t addr, uint32_t val)
{
	uint32_t mask = iwx_prph_addr_mask(sc);
	IWX_WRITE(sc, IWX_HBUS_TARG_PRPH_WADDR, ((addr & mask) | (3 << 24)));
	IWX_BARRIER_WRITE(sc);
	IWX_WRITE(sc, IWX_HBUS_TARG_PRPH_WDAT, val);
}

void
iwx_write_prph(struct iwx_softc *sc, uint32_t addr, uint32_t val)
{
	iwx_nic_assert_locked(sc);
	iwx_write_prph_unlocked(sc, addr, val);
}

uint32_t
iwx_read_umac_prph_unlocked(struct iwx_softc *sc, uint32_t addr)
{
	return iwx_read_prph_unlocked(sc, addr + sc->sc_umac_prph_offset);
}

uint32_t
iwx_read_umac_prph(struct iwx_softc *sc, uint32_t addr)
{
	return iwx_read_prph(sc, addr + sc->sc_umac_prph_offset);
}

void
iwx_write_umac_prph_unlocked(struct iwx_softc *sc, uint32_t addr, uint32_t val)
{
	iwx_write_prph_unlocked(sc, addr + sc->sc_umac_prph_offset, val);
}

void
iwx_write_umac_prph(struct iwx_softc *sc, uint32_t addr, uint32_t val)
{
	iwx_write_prph(sc, addr + sc->sc_umac_prph_offset, val);
}

int
iwx_read_mem(struct iwx_softc *sc, uint32_t addr, void *buf, int dwords)
{
	int offs, err = 0;
	uint32_t *vals = buf;

	if (iwx_nic_lock(sc)) {
		IWX_WRITE(sc, IWX_HBUS_TARG_MEM_RADDR, addr);
		for (offs = 0; offs < dwords; offs++)
			vals[offs] = le32toh(IWX_READ(sc, IWX_HBUS_TARG_MEM_RDAT));
		iwx_nic_unlock(sc);
	} else {
		err = EBUSY;
	}
	return err;
}

int
iwx_write_mem(struct iwx_softc *sc, uint32_t addr, const void *buf, int dwords)
{
	int offs;
	const uint32_t *vals = buf;

	if (iwx_nic_lock(sc)) {
		IWX_WRITE(sc, IWX_HBUS_TARG_MEM_WADDR, addr);
		/* WADDR auto-increments */
		for (offs = 0; offs < dwords; offs++) {
			uint32_t val = vals ? vals[offs] : 0;
			IWX_WRITE(sc, IWX_HBUS_TARG_MEM_WDAT, val);
		}
		iwx_nic_unlock(sc);
	} else {
		return EBUSY;
	}
	return 0;
}

int
iwx_write_mem32(struct iwx_softc *sc, uint32_t addr, uint32_t val)
{
	return iwx_write_mem(sc, addr, &val, 1);
}

int
iwx_poll_bit(struct iwx_softc *sc, int reg, uint32_t bits, uint32_t mask,
    int timo)
{
	for (;;) {
		if ((IWX_READ(sc, reg) & mask) == (bits & mask)) {
			return 1;
		}
		if (timo < 10) {
			return 0;
		}
		timo -= 10;
		DELAY(10);
	}
}

int
iwx_nic_lock(struct iwx_softc *sc)
{
	if (sc->sc_nic_locks > 0) {
		iwx_nic_assert_locked(sc);
		sc->sc_nic_locks++;
		return 1; /* already locked */
	}

	IWX_SETBITS(sc, IWX_CSR_GP_CNTRL,
	    IWX_CSR_GP_CNTRL_REG_FLAG_MAC_ACCESS_REQ);

	DELAY(2);

	if (iwx_poll_bit(sc, IWX_CSR_GP_CNTRL,
	    IWX_CSR_GP_CNTRL_REG_VAL_MAC_ACCESS_EN,
	    IWX_CSR_GP_CNTRL_REG_FLAG_MAC_CLOCK_READY
	     | IWX_CSR_GP_CNTRL_REG_FLAG_GOING_TO_SLEEP, 150000)) {
		sc->sc_nic_locks++;
		return 1;
	}

	printf("%s: acquiring device failed\n", DEVNAME(sc));
	return 0;
}

void
iwx_nic_assert_locked(struct iwx_softc *sc)
{
	if (sc->sc_nic_locks <= 0)
		panic("%s: nic locks counter %d", DEVNAME(sc), sc->sc_nic_locks);
}

void
iwx_nic_unlock(struct iwx_softc *sc)
{
	if (sc->sc_nic_locks > 0) {
		if (--sc->sc_nic_locks == 0)
			IWX_CLRBITS(sc, IWX_CSR_GP_CNTRL,
			    IWX_CSR_GP_CNTRL_REG_FLAG_MAC_ACCESS_REQ);
	} else
		printf("%s: NIC already unlocked\n", DEVNAME(sc));
}

int
iwx_set_bits_mask_prph(struct iwx_softc *sc, uint32_t reg, uint32_t bits,
    uint32_t mask)
{
	uint32_t val;

	if (iwx_nic_lock(sc)) {
		val = iwx_read_prph(sc, reg) & mask;
		val |= bits;
		iwx_write_prph(sc, reg, val);
		iwx_nic_unlock(sc);
		return 0;
	}
	return EBUSY;
}

int
iwx_set_bits_prph(struct iwx_softc *sc, uint32_t reg, uint32_t bits)
{
	return iwx_set_bits_mask_prph(sc, reg, bits, ~0);
}

int
iwx_clear_bits_prph(struct iwx_softc *sc, uint32_t reg, uint32_t bits)
{
	return iwx_set_bits_mask_prph(sc, reg, 0, ~bits);
}

int
iwx_dma_contig_alloc(bus_dma_tag_t tag, struct iwx_dma_info *dma,
    bus_size_t size, bus_size_t alignment)
{
	int nsegs, err;
	caddr_t va;

	dma->tag = tag;
	dma->size = size;

	err = bus_dmamap_create(tag, size, 1, size, 0, BUS_DMA_NOWAIT,
	    &dma->map);
	if (err)
		goto fail;

	err = bus_dmamem_alloc(tag, size, alignment, 0, &dma->seg, 1, &nsegs,
	    BUS_DMA_NOWAIT | BUS_DMA_ZERO);
	if (err)
		goto fail;

	if (nsegs > 1) {
		err = ENOMEM;
		goto fail;
	}

	err = bus_dmamem_map(tag, &dma->seg, 1, size, &va,
	    BUS_DMA_NOWAIT | BUS_DMA_COHERENT);
	if (err)
		goto fail;
	dma->vaddr = va;

	err = bus_dmamap_load(tag, dma->map, dma->vaddr, size, NULL,
	    BUS_DMA_NOWAIT);
	if (err)
		goto fail;

	bus_dmamap_sync(tag, dma->map, 0, size, BUS_DMASYNC_PREWRITE);
	dma->paddr = dma->map->dm_segs[0].ds_addr;

	return 0;

fail:	iwx_dma_contig_free(dma);
	return err;
}

void
iwx_dma_contig_free(struct iwx_dma_info *dma)
{
	if (dma->map != NULL) {
		if (dma->vaddr != NULL) {
			bus_dmamap_sync(dma->tag, dma->map, 0, dma->size,
			    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);
			bus_dmamap_unload(dma->tag, dma->map);
			bus_dmamem_unmap(dma->tag, dma->vaddr, dma->size);
			bus_dmamem_free(dma->tag, &dma->seg, 1);
			dma->vaddr = NULL;
		}
		bus_dmamap_destroy(dma->tag, dma->map);
		dma->map = NULL;
	}
}

int
iwx_alloc_rx_ring(struct iwx_softc *sc, struct iwx_rx_ring *ring)
{
	bus_size_t size;
	int i, err;

	ring->cur = 0;

	/* Allocate RX descriptors (256-byte aligned). */
	if (sc->sc_device_family >= IWX_DEVICE_FAMILY_AX210)
		size = sizeof(struct iwx_rx_transfer_desc);
	else
		size = sizeof(uint64_t);
	err = iwx_dma_contig_alloc(sc->sc_dmat, &ring->free_desc_dma,
	    size * IWX_RX_MQ_RING_COUNT, 256);
	if (err) {
		printf("%s: could not allocate RX ring DMA memory\n",
		    DEVNAME(sc));
		goto fail;
	}
	ring->desc = ring->free_desc_dma.vaddr;

	/* Allocate RX status area (16-byte aligned). */
	if (sc->sc_device_family >= IWX_DEVICE_FAMILY_AX210)
		size = sizeof(uint16_t);
	else
		size = sizeof(*ring->stat);
	err = iwx_dma_contig_alloc(sc->sc_dmat, &ring->stat_dma, size, 16);
	if (err) {
		printf("%s: could not allocate RX status DMA memory\n",
		    DEVNAME(sc));
		goto fail;
	}
	ring->stat = ring->stat_dma.vaddr;

	if (sc->sc_device_family >= IWX_DEVICE_FAMILY_AX210)
		size = sizeof(struct iwx_rx_completion_desc);
	else
		size = sizeof(uint32_t);
	err = iwx_dma_contig_alloc(sc->sc_dmat, &ring->used_desc_dma,
	    size * IWX_RX_MQ_RING_COUNT, 256);
	if (err) {
		printf("%s: could not allocate RX ring DMA memory\n",
		    DEVNAME(sc));
		goto fail;
	}

	for (i = 0; i < IWX_RX_MQ_RING_COUNT; i++) {
		struct iwx_rx_data *data = &ring->data[i];

		memset(data, 0, sizeof(*data));
		err = bus_dmamap_create(sc->sc_dmat, IWX_RBUF_SIZE, 1,
		    IWX_RBUF_SIZE, 0, BUS_DMA_NOWAIT | BUS_DMA_ALLOCNOW,
		    &data->map);
		if (err) {
			printf("%s: could not create RX buf DMA map\n",
			    DEVNAME(sc));
			goto fail;
		}

		err = iwx_rx_addbuf(sc, IWX_RBUF_SIZE, i);
		if (err)
			goto fail;
	}
	return 0;

fail:	iwx_free_rx_ring(sc, ring);
	return err;
}

void
iwx_disable_rx_dma(struct iwx_softc *sc)
{
	int ntries;

	if (iwx_nic_lock(sc)) {
		if (sc->sc_device_family >= IWX_DEVICE_FAMILY_AX210)
			iwx_write_umac_prph(sc, IWX_RFH_RXF_DMA_CFG_GEN3, 0);
		else
			iwx_write_prph(sc, IWX_RFH_RXF_DMA_CFG, 0);
		for (ntries = 0; ntries < 1000; ntries++) {
			if (sc->sc_device_family >= IWX_DEVICE_FAMILY_AX210) {
				if (iwx_read_umac_prph(sc,
				    IWX_RFH_GEN_STATUS_GEN3) & IWX_RXF_DMA_IDLE)
					break;
			} else {
				if (iwx_read_prph(sc, IWX_RFH_GEN_STATUS) &
				    IWX_RXF_DMA_IDLE)
					break;
			}
			DELAY(10);
		}
		iwx_nic_unlock(sc);
	}
}

void
iwx_reset_rx_ring(struct iwx_softc *sc, struct iwx_rx_ring *ring)
{
	ring->cur = 0;
	bus_dmamap_sync(sc->sc_dmat, ring->stat_dma.map, 0,
	    ring->stat_dma.size, BUS_DMASYNC_PREWRITE);
	if (sc->sc_device_family >= IWX_DEVICE_FAMILY_AX210) {
		uint16_t *status = sc->rxq.stat_dma.vaddr;
		*status = 0;
	} else
		memset(ring->stat, 0, sizeof(*ring->stat));
	bus_dmamap_sync(sc->sc_dmat, ring->stat_dma.map, 0,
	    ring->stat_dma.size, BUS_DMASYNC_POSTWRITE);

}

void
iwx_free_rx_ring(struct iwx_softc *sc, struct iwx_rx_ring *ring)
{
	int i;

	iwx_dma_contig_free(&ring->free_desc_dma);
	iwx_dma_contig_free(&ring->stat_dma);
	iwx_dma_contig_free(&ring->used_desc_dma);

	for (i = 0; i < IWX_RX_MQ_RING_COUNT; i++) {
		struct iwx_rx_data *data = &ring->data[i];

		if (data->m != NULL) {
			bus_dmamap_sync(sc->sc_dmat, data->map, 0,
			    data->map->dm_mapsize, BUS_DMASYNC_POSTREAD);
			bus_dmamap_unload(sc->sc_dmat, data->map);
			m_freem(data->m);
			data->m = NULL;
		}
		if (data->map != NULL)
			bus_dmamap_destroy(sc->sc_dmat, data->map);
	}
}

int
iwx_alloc_tx_ring(struct iwx_softc *sc, struct iwx_tx_ring *ring, int qid)
{
	bus_addr_t paddr;
	bus_size_t size;
	int i, err;
	size_t bc_tbl_size;
	bus_size_t bc_align;

	ring->qid = qid;
	ring->queued = 0;
	ring->cur = 0;
	ring->cur_hw = 0;
	ring->tail = 0;
	ring->tail_hw = 0;

	/* Allocate TX descriptors (256-byte aligned). */
	size = IWX_TX_RING_COUNT * sizeof(struct iwx_tfh_tfd);
	err = iwx_dma_contig_alloc(sc->sc_dmat, &ring->desc_dma, size, 256);
	if (err) {
		printf("%s: could not allocate TX ring DMA memory\n",
		    DEVNAME(sc));
		goto fail;
	}
	ring->desc = ring->desc_dma.vaddr;

	/*
	 * The hardware supports up to 512 Tx rings which is more
	 * than we currently need.
	 *
	 * In DQA mode we use 1 command queue + 1 default queue for
	 * management, control, and non-QoS data frames.
	 * The command is queue sc->txq[0], our default queue is sc->txq[1].
	 *
	 * Tx aggregation requires additional queues, one queue per TID for
	 * which aggregation is enabled. We map TID 0-7 to sc->txq[2:9].
	 * Firmware may assign its own internal IDs for these queues
	 * depending on which TID gets aggregation enabled first.
	 * The driver maintains a table mapping driver-side queue IDs
	 * to firmware-side queue IDs.
	 */

	if (sc->sc_device_family >= IWX_DEVICE_FAMILY_AX210) {
		bc_tbl_size = sizeof(struct iwx_gen3_bc_tbl_entry) *
		    IWX_TFD_QUEUE_BC_SIZE_GEN3_AX210;
		bc_align = 128;
	} else {
		bc_tbl_size = sizeof(struct iwx_agn_scd_bc_tbl);
		bc_align = 64;
	}
	err = iwx_dma_contig_alloc(sc->sc_dmat, &ring->bc_tbl, bc_tbl_size,
	    bc_align);
	if (err) {
		printf("%s: could not allocate byte count table DMA memory\n",
		    DEVNAME(sc));
		goto fail;
	}

	size = IWX_TX_RING_COUNT * sizeof(struct iwx_device_cmd);
	err = iwx_dma_contig_alloc(sc->sc_dmat, &ring->cmd_dma, size,
	    IWX_FIRST_TB_SIZE_ALIGN);
	if (err) {
		printf("%s: could not allocate cmd DMA memory\n", DEVNAME(sc));
		goto fail;
	}
	ring->cmd = ring->cmd_dma.vaddr;

	paddr = ring->cmd_dma.paddr;
	for (i = 0; i < IWX_TX_RING_COUNT; i++) {
		struct iwx_tx_data *data = &ring->data[i];
		size_t mapsize;

		data->cmd_paddr = paddr;
		paddr += sizeof(struct iwx_device_cmd);

		/* FW commands may require more mapped space than packets. */
		if (qid == IWX_DQA_CMD_QUEUE)
			mapsize = (sizeof(struct iwx_cmd_header) +
			    IWX_MAX_CMD_PAYLOAD_SIZE);
		else
			mapsize = MCLBYTES;
		err = bus_dmamap_create(sc->sc_dmat, mapsize,
		    IWX_TFH_NUM_TBS - 2, mapsize, 0, BUS_DMA_NOWAIT,
		    &data->map);
		if (err) {
			printf("%s: could not create TX buf DMA map\n",
			    DEVNAME(sc));
			goto fail;
		}
	}
	KASSERT(paddr == ring->cmd_dma.paddr + size);
	return 0;

fail:	iwx_free_tx_ring(sc, ring);
	return err;
}

void
iwx_reset_tx_ring(struct iwx_softc *sc, struct iwx_tx_ring *ring)
{
	int i;

	for (i = 0; i < IWX_TX_RING_COUNT; i++) {
		struct iwx_tx_data *data = &ring->data[i];

		if (data->m != NULL) {
			bus_dmamap_sync(sc->sc_dmat, data->map, 0,
			    data->map->dm_mapsize, BUS_DMASYNC_POSTWRITE);
			bus_dmamap_unload(sc->sc_dmat, data->map);
			m_freem(data->m);
			data->m = NULL;
		}
	}

	/* Clear byte count table. */
	memset(ring->bc_tbl.vaddr, 0, ring->bc_tbl.size);

	/* Clear TX descriptors. */
	memset(ring->desc, 0, ring->desc_dma.size);
	bus_dmamap_sync(sc->sc_dmat, ring->desc_dma.map, 0,
	    ring->desc_dma.size, BUS_DMASYNC_PREWRITE);
	sc->qfullmsk &= ~(1 << ring->qid);
	sc->qenablemsk &= ~(1 << ring->qid);
	for (i = 0; i < nitems(sc->aggqid); i++) {
		if (sc->aggqid[i] == ring->qid) {
			sc->aggqid[i] = 0;
			break;
		}
	}
	ring->queued = 0;
	ring->cur = 0;
	ring->cur_hw = 0;
	ring->tail = 0;
	ring->tail_hw = 0;
	ring->tid = 0;
}

void
iwx_free_tx_ring(struct iwx_softc *sc, struct iwx_tx_ring *ring)
{
	int i;

	iwx_dma_contig_free(&ring->desc_dma);
	iwx_dma_contig_free(&ring->cmd_dma);
	iwx_dma_contig_free(&ring->bc_tbl);

	for (i = 0; i < IWX_TX_RING_COUNT; i++) {
		struct iwx_tx_data *data = &ring->data[i];

		if (data->m != NULL) {
			bus_dmamap_sync(sc->sc_dmat, data->map, 0,
			    data->map->dm_mapsize, BUS_DMASYNC_POSTWRITE);
			bus_dmamap_unload(sc->sc_dmat, data->map);
			m_freem(data->m);
			data->m = NULL;
		}
		if (data->map != NULL)
			bus_dmamap_destroy(sc->sc_dmat, data->map);
	}
}

void
iwx_enable_rfkill_int(struct iwx_softc *sc)
{
	if (!sc->sc_msix) {
		sc->sc_intmask = IWX_CSR_INT_BIT_RF_KILL;
		IWX_WRITE(sc, IWX_CSR_INT_MASK, sc->sc_intmask);
	} else {
		IWX_WRITE(sc, IWX_CSR_MSIX_FH_INT_MASK_AD,
		    sc->sc_fh_init_mask);
		IWX_WRITE(sc, IWX_CSR_MSIX_HW_INT_MASK_AD,
		    ~IWX_MSIX_HW_INT_CAUSES_REG_RF_KILL);
		sc->sc_hw_mask = IWX_MSIX_HW_INT_CAUSES_REG_RF_KILL;
	}

	IWX_SETBITS(sc, IWX_CSR_GP_CNTRL,
	    IWX_CSR_GP_CNTRL_REG_FLAG_RFKILL_WAKE_L1A_EN);
}

int
iwx_check_rfkill(struct iwx_softc *sc)
{
	uint32_t v;
	int rv;

	/*
	 * "documentation" is not really helpful here:
	 *  27:	HW_RF_KILL_SW
	 *	Indicates state of (platform's) hardware RF-Kill switch
	 *
	 * But apparently when it's off, it's on ...
	 */
	v = IWX_READ(sc, IWX_CSR_GP_CNTRL);
	rv = (v & IWX_CSR_GP_CNTRL_REG_FLAG_HW_RF_KILL_SW) == 0;
	if (rv) {
		sc->sc_flags |= IWX_FLAG_RFKILL;
	} else {
		sc->sc_flags &= ~IWX_FLAG_RFKILL;
	}

	return rv;
}

void
iwx_enable_interrupts(struct iwx_softc *sc)
{
	if (!sc->sc_msix) {
		sc->sc_intmask = IWX_CSR_INI_SET_MASK;
		IWX_WRITE(sc, IWX_CSR_INT_MASK, sc->sc_intmask);
	} else {
		/*
		 * fh/hw_mask keeps all the unmasked causes.
		 * Unlike msi, in msix cause is enabled when it is unset.
		 */
		sc->sc_hw_mask = sc->sc_hw_init_mask;
		sc->sc_fh_mask = sc->sc_fh_init_mask;
		IWX_WRITE(sc, IWX_CSR_MSIX_FH_INT_MASK_AD,
		    ~sc->sc_fh_mask);
		IWX_WRITE(sc, IWX_CSR_MSIX_HW_INT_MASK_AD,
		    ~sc->sc_hw_mask);
	}
}

void
iwx_enable_fwload_interrupt(struct iwx_softc *sc)
{
	if (!sc->sc_msix) {
		sc->sc_intmask = IWX_CSR_INT_BIT_ALIVE | IWX_CSR_INT_BIT_FH_RX;
		IWX_WRITE(sc, IWX_CSR_INT_MASK, sc->sc_intmask);
	} else {
		IWX_WRITE(sc, IWX_CSR_MSIX_HW_INT_MASK_AD,
		    ~IWX_MSIX_HW_INT_CAUSES_REG_ALIVE);
		sc->sc_hw_mask = IWX_MSIX_HW_INT_CAUSES_REG_ALIVE;
		/*
		 * Leave all the FH causes enabled to get the ALIVE
		 * notification.
		 */
		IWX_WRITE(sc, IWX_CSR_MSIX_FH_INT_MASK_AD,
		    ~sc->sc_fh_init_mask);
		sc->sc_fh_mask = sc->sc_fh_init_mask;
	}
}

void
iwx_restore_interrupts(struct iwx_softc *sc)
{
	IWX_WRITE(sc, IWX_CSR_INT_MASK, sc->sc_intmask);
}

void
iwx_disable_interrupts(struct iwx_softc *sc)
{
	if (!sc->sc_msix) {
		IWX_WRITE(sc, IWX_CSR_INT_MASK, 0);

		/* acknowledge all interrupts */
		IWX_WRITE(sc, IWX_CSR_INT, ~0);
		IWX_WRITE(sc, IWX_CSR_FH_INT_STATUS, ~0);
	} else {
		IWX_WRITE(sc, IWX_CSR_MSIX_FH_INT_MASK_AD,
		    sc->sc_fh_init_mask);
		IWX_WRITE(sc, IWX_CSR_MSIX_HW_INT_MASK_AD,
		    sc->sc_hw_init_mask);
	}
}

void
iwx_ict_reset(struct iwx_softc *sc)
{
	iwx_disable_interrupts(sc);

	memset(sc->ict_dma.vaddr, 0, IWX_ICT_SIZE);
	sc->ict_cur = 0;

	/* Set physical address of ICT (4KB aligned). */
	IWX_WRITE(sc, IWX_CSR_DRAM_INT_TBL_REG,
	    IWX_CSR_DRAM_INT_TBL_ENABLE
	    | IWX_CSR_DRAM_INIT_TBL_WRAP_CHECK
	    | IWX_CSR_DRAM_INIT_TBL_WRITE_POINTER
	    | sc->ict_dma.paddr >> IWX_ICT_PADDR_SHIFT);

	/* Switch to ICT interrupt mode in driver. */
	sc->sc_flags |= IWX_FLAG_USE_ICT;

	IWX_WRITE(sc, IWX_CSR_INT, ~0);
	iwx_enable_interrupts(sc);
}

#define IWX_HW_READY_TIMEOUT 50
int
iwx_set_hw_ready(struct iwx_softc *sc)
{
	int ready;

	IWX_SETBITS(sc, IWX_CSR_HW_IF_CONFIG_REG,
	    IWX_CSR_HW_IF_CONFIG_REG_BIT_NIC_READY);

	ready = iwx_poll_bit(sc, IWX_CSR_HW_IF_CONFIG_REG,
	    IWX_CSR_HW_IF_CONFIG_REG_BIT_NIC_READY,
	    IWX_CSR_HW_IF_CONFIG_REG_BIT_NIC_READY,
	    IWX_HW_READY_TIMEOUT);
	if (ready)
		IWX_SETBITS(sc, IWX_CSR_MBOX_SET_REG,
		    IWX_CSR_MBOX_SET_REG_OS_ALIVE);

	return ready;
}
#undef IWX_HW_READY_TIMEOUT

int
iwx_prepare_card_hw(struct iwx_softc *sc)
{
	int t = 0;
	int ntries;

	if (iwx_set_hw_ready(sc))
		return 0;

	IWX_SETBITS(sc, IWX_CSR_DBG_LINK_PWR_MGMT_REG,
	    IWX_CSR_RESET_LINK_PWR_MGMT_DISABLED);
	DELAY(1000);

	for (ntries = 0; ntries < 10; ntries++) {
		/* If HW is not ready, prepare the conditions to check again */
		IWX_SETBITS(sc, IWX_CSR_HW_IF_CONFIG_REG,
		    IWX_CSR_HW_IF_CONFIG_REG_PREPARE);

		do {
			if (iwx_set_hw_ready(sc))
				return 0;
			DELAY(200);
			t += 200;
		} while (t < 150000);
		DELAY(25000);
	}

	return ETIMEDOUT;
}

int
iwx_force_power_gating(struct iwx_softc *sc)
{
	int err;

	err = iwx_set_bits_prph(sc, IWX_HPM_HIPM_GEN_CFG,
	    IWX_HPM_HIPM_GEN_CFG_CR_FORCE_ACTIVE);
	if (err)
		return err;
	DELAY(20);
	err = iwx_set_bits_prph(sc, IWX_HPM_HIPM_GEN_CFG,
	    IWX_HPM_HIPM_GEN_CFG_CR_PG_EN |
	    IWX_HPM_HIPM_GEN_CFG_CR_SLP_EN);
	if (err)
		return err;
	DELAY(20);
	err = iwx_clear_bits_prph(sc, IWX_HPM_HIPM_GEN_CFG,
	    IWX_HPM_HIPM_GEN_CFG_CR_FORCE_ACTIVE);
	return err;
}

void
iwx_apm_config(struct iwx_softc *sc)
{
	pcireg_t lctl, cap;

	/*
	 * L0S states have been found to be unstable with our devices
	 * and in newer hardware they are not officially supported at
	 * all, so we must always set the L0S_DISABLED bit.
	 */
	IWX_SETBITS(sc, IWX_CSR_GIO_REG, IWX_CSR_GIO_REG_VAL_L0S_DISABLED);

	lctl = pci_conf_read(sc->sc_pct, sc->sc_pcitag,
	    sc->sc_cap_off + PCI_PCIE_LCSR);
	sc->sc_pm_support = !(lctl & PCI_PCIE_LCSR_ASPM_L0S);
	cap = pci_conf_read(sc->sc_pct, sc->sc_pcitag,
	    sc->sc_cap_off + PCI_PCIE_DCSR2);
	sc->sc_ltr_enabled = (cap & PCI_PCIE_DCSR2_LTREN) ? 1 : 0;
	DPRINTF(("%s: L1 %sabled - LTR %sabled\n",
	    DEVNAME(sc),
	    (lctl & PCI_PCIE_LCSR_ASPM_L1) ? "En" : "Dis",
	    sc->sc_ltr_enabled ? "En" : "Dis"));
}

/*
 * Start up NIC's basic functionality after it has been reset
 * e.g. after platform boot or shutdown.
 * NOTE:  This does not load uCode nor start the embedded processor
 */
int
iwx_apm_init(struct iwx_softc *sc)
{
	int err = 0;

	/*
	 * Disable L0s without affecting L1;
	 *  don't wait for ICH L0s (ICH bug W/A)
	 */
	IWX_SETBITS(sc, IWX_CSR_GIO_CHICKEN_BITS,
	    IWX_CSR_GIO_CHICKEN_BITS_REG_BIT_L1A_NO_L0S_RX);

	/* Set FH wait threshold to maximum (HW error during stress W/A) */
	IWX_SETBITS(sc, IWX_CSR_DBG_HPET_MEM_REG, IWX_CSR_DBG_HPET_MEM_REG_VAL);

	/*
	 * Enable HAP INTA (interrupt from management bus) to
	 * wake device's PCI Express link L1a -> L0s
	 */
	IWX_SETBITS(sc, IWX_CSR_HW_IF_CONFIG_REG,
	    IWX_CSR_HW_IF_CONFIG_REG_BIT_HAP_WAKE_L1A);

	iwx_apm_config(sc);

	/*
	 * Set "initialization complete" bit to move adapter from
	 * D0U* --> D0A* (powered-up active) state.
	 */
	IWX_SETBITS(sc, IWX_CSR_GP_CNTRL, IWX_CSR_GP_CNTRL_REG_FLAG_INIT_DONE);

	/*
	 * Wait for clock stabilization; once stabilized, access to
	 * device-internal resources is supported, e.g. iwx_write_prph()
	 * and accesses to uCode SRAM.
	 */
	if (!iwx_poll_bit(sc, IWX_CSR_GP_CNTRL,
	    IWX_CSR_GP_CNTRL_REG_FLAG_MAC_CLOCK_READY,
	    IWX_CSR_GP_CNTRL_REG_FLAG_MAC_CLOCK_READY, 25000)) {
		printf("%s: timeout waiting for clock stabilization\n",
		    DEVNAME(sc));
		err = ETIMEDOUT;
		goto out;
	}
 out:
	if (err)
		printf("%s: apm init error %d\n", DEVNAME(sc), err);
	return err;
}

void
iwx_apm_stop(struct iwx_softc *sc)
{
	IWX_SETBITS(sc, IWX_CSR_DBG_LINK_PWR_MGMT_REG,
	    IWX_CSR_RESET_LINK_PWR_MGMT_DISABLED);
	IWX_SETBITS(sc, IWX_CSR_HW_IF_CONFIG_REG,
	    IWX_CSR_HW_IF_CONFIG_REG_PREPARE |
	    IWX_CSR_HW_IF_CONFIG_REG_ENABLE_PME);
	DELAY(1000);
	IWX_CLRBITS(sc, IWX_CSR_DBG_LINK_PWR_MGMT_REG,
	    IWX_CSR_RESET_LINK_PWR_MGMT_DISABLED);
	DELAY(5000);

	/* stop device's busmaster DMA activity */
	IWX_SETBITS(sc, IWX_CSR_RESET, IWX_CSR_RESET_REG_FLAG_STOP_MASTER);

	if (!iwx_poll_bit(sc, IWX_CSR_RESET,
	    IWX_CSR_RESET_REG_FLAG_MASTER_DISABLED,
	    IWX_CSR_RESET_REG_FLAG_MASTER_DISABLED, 100))
		printf("%s: timeout waiting for master\n", DEVNAME(sc));

	/*
	 * Clear "initialization complete" bit to move adapter from
	 * D0A* (powered-up Active) --> D0U* (Uninitialized) state.
	 */
	IWX_CLRBITS(sc, IWX_CSR_GP_CNTRL,
	    IWX_CSR_GP_CNTRL_REG_FLAG_INIT_DONE);
}

void
iwx_init_msix_hw(struct iwx_softc *sc)
{
	iwx_conf_msix_hw(sc, 0);

	if (!sc->sc_msix)
		return;

	sc->sc_fh_init_mask = ~IWX_READ(sc, IWX_CSR_MSIX_FH_INT_MASK_AD);
	sc->sc_fh_mask = sc->sc_fh_init_mask;
	sc->sc_hw_init_mask = ~IWX_READ(sc, IWX_CSR_MSIX_HW_INT_MASK_AD);
	sc->sc_hw_mask = sc->sc_hw_init_mask;
}

void
iwx_conf_msix_hw(struct iwx_softc *sc, int stopped)
{
	int vector = 0;

	if (!sc->sc_msix) {
		/* Newer chips default to MSIX. */
		if (!stopped && iwx_nic_lock(sc)) {
			iwx_write_umac_prph(sc, IWX_UREG_CHICK,
			    IWX_UREG_CHICK_MSI_ENABLE);
			iwx_nic_unlock(sc);
		}
		return;
	}

	if (!stopped && iwx_nic_lock(sc)) {
		iwx_write_umac_prph(sc, IWX_UREG_CHICK,
		    IWX_UREG_CHICK_MSIX_ENABLE);
		iwx_nic_unlock(sc);
	}

	/* Disable all interrupts */
	IWX_WRITE(sc, IWX_CSR_MSIX_FH_INT_MASK_AD, ~0);
	IWX_WRITE(sc, IWX_CSR_MSIX_HW_INT_MASK_AD, ~0);

	/* Map fallback-queue (command/mgmt) to a single vector */
	IWX_WRITE_1(sc, IWX_CSR_MSIX_RX_IVAR(0),
	    vector | IWX_MSIX_NON_AUTO_CLEAR_CAUSE);
	/* Map RSS queue (data) to the same vector */
	IWX_WRITE_1(sc, IWX_CSR_MSIX_RX_IVAR(1),
	    vector | IWX_MSIX_NON_AUTO_CLEAR_CAUSE);

	/* Enable the RX queues cause interrupts */
	IWX_CLRBITS(sc, IWX_CSR_MSIX_FH_INT_MASK_AD,
	    IWX_MSIX_FH_INT_CAUSES_Q0 | IWX_MSIX_FH_INT_CAUSES_Q1);

	/* Map non-RX causes to the same vector */
	IWX_WRITE_1(sc, IWX_CSR_MSIX_IVAR(IWX_MSIX_IVAR_CAUSE_D2S_CH0_NUM),
	    vector | IWX_MSIX_NON_AUTO_CLEAR_CAUSE);
	IWX_WRITE_1(sc, IWX_CSR_MSIX_IVAR(IWX_MSIX_IVAR_CAUSE_D2S_CH1_NUM),
	    vector | IWX_MSIX_NON_AUTO_CLEAR_CAUSE);
	IWX_WRITE_1(sc, IWX_CSR_MSIX_IVAR(IWX_MSIX_IVAR_CAUSE_S2D),
	    vector | IWX_MSIX_NON_AUTO_CLEAR_CAUSE);
	IWX_WRITE_1(sc, IWX_CSR_MSIX_IVAR(IWX_MSIX_IVAR_CAUSE_FH_ERR),
	    vector | IWX_MSIX_NON_AUTO_CLEAR_CAUSE);
	IWX_WRITE_1(sc, IWX_CSR_MSIX_IVAR(IWX_MSIX_IVAR_CAUSE_REG_ALIVE),
	    vector | IWX_MSIX_NON_AUTO_CLEAR_CAUSE);
	IWX_WRITE_1(sc, IWX_CSR_MSIX_IVAR(IWX_MSIX_IVAR_CAUSE_REG_WAKEUP),
	    vector | IWX_MSIX_NON_AUTO_CLEAR_CAUSE);
	IWX_WRITE_1(sc, IWX_CSR_MSIX_IVAR(IWX_MSIX_IVAR_CAUSE_REG_RESET_DONE),
	    vector | IWX_MSIX_NON_AUTO_CLEAR_CAUSE);
	IWX_WRITE_1(sc, IWX_CSR_MSIX_IVAR(IWX_MSIX_IVAR_CAUSE_REG_CT_KILL),
	    vector | IWX_MSIX_NON_AUTO_CLEAR_CAUSE);
	IWX_WRITE_1(sc, IWX_CSR_MSIX_IVAR(IWX_MSIX_IVAR_CAUSE_REG_RF_KILL),
	    vector | IWX_MSIX_NON_AUTO_CLEAR_CAUSE);
	IWX_WRITE_1(sc, IWX_CSR_MSIX_IVAR(IWX_MSIX_IVAR_CAUSE_REG_PERIODIC),
	    vector | IWX_MSIX_NON_AUTO_CLEAR_CAUSE);
	IWX_WRITE_1(sc, IWX_CSR_MSIX_IVAR(IWX_MSIX_IVAR_CAUSE_REG_SW_ERR),
	    vector | IWX_MSIX_NON_AUTO_CLEAR_CAUSE);
	IWX_WRITE_1(sc, IWX_CSR_MSIX_IVAR(IWX_MSIX_IVAR_CAUSE_REG_SCD),
	    vector | IWX_MSIX_NON_AUTO_CLEAR_CAUSE);
	IWX_WRITE_1(sc, IWX_CSR_MSIX_IVAR(IWX_MSIX_IVAR_CAUSE_REG_FH_TX),
	    vector | IWX_MSIX_NON_AUTO_CLEAR_CAUSE);
	IWX_WRITE_1(sc, IWX_CSR_MSIX_IVAR(IWX_MSIX_IVAR_CAUSE_REG_HW_ERR),
	    vector | IWX_MSIX_NON_AUTO_CLEAR_CAUSE);
	IWX_WRITE_1(sc, IWX_CSR_MSIX_IVAR(IWX_MSIX_IVAR_CAUSE_REG_HAP),
	    vector | IWX_MSIX_NON_AUTO_CLEAR_CAUSE);

	/* Enable non-RX causes interrupts */
	IWX_CLRBITS(sc, IWX_CSR_MSIX_FH_INT_MASK_AD,
	    IWX_MSIX_FH_INT_CAUSES_D2S_CH0_NUM |
	    IWX_MSIX_FH_INT_CAUSES_D2S_CH1_NUM |
	    IWX_MSIX_FH_INT_CAUSES_S2D |
	    IWX_MSIX_FH_INT_CAUSES_FH_ERR);
	IWX_CLRBITS(sc, IWX_CSR_MSIX_HW_INT_MASK_AD,
	    IWX_MSIX_HW_INT_CAUSES_REG_ALIVE |
	    IWX_MSIX_HW_INT_CAUSES_REG_WAKEUP |
	    IWX_MSIX_HW_INT_CAUSES_REG_RESET_DONE |
	    IWX_MSIX_HW_INT_CAUSES_REG_CT_KILL |
	    IWX_MSIX_HW_INT_CAUSES_REG_RF_KILL |
	    IWX_MSIX_HW_INT_CAUSES_REG_PERIODIC |
	    IWX_MSIX_HW_INT_CAUSES_REG_SW_ERR |
	    IWX_MSIX_HW_INT_CAUSES_REG_SCD |
	    IWX_MSIX_HW_INT_CAUSES_REG_FH_TX |
	    IWX_MSIX_HW_INT_CAUSES_REG_HW_ERR |
	    IWX_MSIX_HW_INT_CAUSES_REG_HAP);
}

int
iwx_clear_persistence_bit(struct iwx_softc *sc)
{
	uint32_t hpm, wprot;

	hpm = iwx_read_prph_unlocked(sc, IWX_HPM_DEBUG);
	if (hpm != 0xa5a5a5a0 && (hpm & IWX_PERSISTENCE_BIT)) {
		wprot = iwx_read_prph_unlocked(sc, IWX_PREG_PRPH_WPROT_22000);
		if (wprot & IWX_PREG_WFPM_ACCESS) {
			printf("%s: cannot clear persistence bit\n",
			    DEVNAME(sc));
			return EPERM;
		}
		iwx_write_prph_unlocked(sc, IWX_HPM_DEBUG,
		    hpm & ~IWX_PERSISTENCE_BIT);
	}

	return 0;
}

int
iwx_start_hw(struct iwx_softc *sc)
{
	int err;

	err = iwx_prepare_card_hw(sc);
	if (err)
		return err;

	if (sc->sc_device_family == IWX_DEVICE_FAMILY_22000) {
		err = iwx_clear_persistence_bit(sc);
		if (err)
			return err;
	}

	/* Reset the entire device */
	IWX_SETBITS(sc, IWX_CSR_RESET, IWX_CSR_RESET_REG_FLAG_SW_RESET);
	DELAY(5000);

	if (sc->sc_device_family == IWX_DEVICE_FAMILY_22000 &&
	    sc->sc_integrated) {
		IWX_SETBITS(sc, IWX_CSR_GP_CNTRL,
		    IWX_CSR_GP_CNTRL_REG_FLAG_INIT_DONE);
		DELAY(20);
		if (!iwx_poll_bit(sc, IWX_CSR_GP_CNTRL,
		    IWX_CSR_GP_CNTRL_REG_FLAG_MAC_CLOCK_READY,
		    IWX_CSR_GP_CNTRL_REG_FLAG_MAC_CLOCK_READY, 25000)) {
			printf("%s: timeout waiting for clock stabilization\n",
			    DEVNAME(sc));
			return ETIMEDOUT;
		}

		err = iwx_force_power_gating(sc);
		if (err)
			return err;

		/* Reset the entire device */
		IWX_SETBITS(sc, IWX_CSR_RESET, IWX_CSR_RESET_REG_FLAG_SW_RESET);
		DELAY(5000);
	}

	err = iwx_apm_init(sc);
	if (err)
		return err;

	iwx_init_msix_hw(sc);

	iwx_enable_rfkill_int(sc);
	iwx_check_rfkill(sc);

	return 0;
}

void
iwx_stop_device(struct iwx_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ieee80211_node *ni = ic->ic_bss;
	int i;

	iwx_disable_interrupts(sc);
	sc->sc_flags &= ~IWX_FLAG_USE_ICT;

	iwx_disable_rx_dma(sc);
	iwx_reset_rx_ring(sc, &sc->rxq);
	for (i = 0; i < nitems(sc->txq); i++)
		iwx_reset_tx_ring(sc, &sc->txq[i]);
	for (i = 0; i < IEEE80211_NUM_TID; i++) {
		struct ieee80211_tx_ba *ba = &ni->ni_tx_ba[i];
		if (ba->ba_state != IEEE80211_BA_AGREED)
			continue;
		ieee80211_delba_request(ic, ni, 0, 1, i);
	}

	/* Make sure (redundant) we've released our request to stay awake */
	IWX_CLRBITS(sc, IWX_CSR_GP_CNTRL,
	    IWX_CSR_GP_CNTRL_REG_FLAG_MAC_ACCESS_REQ);
	if (sc->sc_nic_locks > 0)
		printf("%s: %d active NIC locks forcefully cleared\n",
		    DEVNAME(sc), sc->sc_nic_locks);
	sc->sc_nic_locks = 0;

	/* Stop the device, and put it in low power state */
	iwx_apm_stop(sc);

	/* Reset the on-board processor. */
	IWX_SETBITS(sc, IWX_CSR_RESET, IWX_CSR_RESET_REG_FLAG_SW_RESET);
	DELAY(5000);

	/*
	 * Upon stop, the IVAR table gets erased, so msi-x won't
	 * work. This causes a bug in RF-KILL flows, since the interrupt
	 * that enables radio won't fire on the correct irq, and the
	 * driver won't be able to handle the interrupt.
	 * Configure the IVAR table again after reset.
	 */
	iwx_conf_msix_hw(sc, 1);

	/*
	 * Upon stop, the APM issues an interrupt if HW RF kill is set.
	 * Clear the interrupt again.
	 */
	iwx_disable_interrupts(sc);

	/* Even though we stop the HW we still want the RF kill interrupt. */
	iwx_enable_rfkill_int(sc);
	iwx_check_rfkill(sc);

	iwx_prepare_card_hw(sc);

	iwx_ctxt_info_free_paging(sc);
	iwx_dma_contig_free(&sc->pnvm_dma);
	for (i = 0; i < sc->pnvm_segs; i++)
		iwx_dma_contig_free(&sc->pnvm_seg_dma[i]);
}

void
iwx_nic_config(struct iwx_softc *sc)
{
	uint8_t radio_cfg_type, radio_cfg_step, radio_cfg_dash;
	uint32_t mask, val, reg_val = 0;

	radio_cfg_type = (sc->sc_fw_phy_config & IWX_FW_PHY_CFG_RADIO_TYPE) >>
	    IWX_FW_PHY_CFG_RADIO_TYPE_POS;
	radio_cfg_step = (sc->sc_fw_phy_config & IWX_FW_PHY_CFG_RADIO_STEP) >>
	    IWX_FW_PHY_CFG_RADIO_STEP_POS;
	radio_cfg_dash = (sc->sc_fw_phy_config & IWX_FW_PHY_CFG_RADIO_DASH) >>
	    IWX_FW_PHY_CFG_RADIO_DASH_POS;

	reg_val |= IWX_CSR_HW_REV_STEP(sc->sc_hw_rev) <<
	    IWX_CSR_HW_IF_CONFIG_REG_POS_MAC_STEP;
	reg_val |= IWX_CSR_HW_REV_DASH(sc->sc_hw_rev) <<
	    IWX_CSR_HW_IF_CONFIG_REG_POS_MAC_DASH;

	/* radio configuration */
	reg_val |= radio_cfg_type << IWX_CSR_HW_IF_CONFIG_REG_POS_PHY_TYPE;
	reg_val |= radio_cfg_step << IWX_CSR_HW_IF_CONFIG_REG_POS_PHY_STEP;
	reg_val |= radio_cfg_dash << IWX_CSR_HW_IF_CONFIG_REG_POS_PHY_DASH;

	mask = IWX_CSR_HW_IF_CONFIG_REG_MSK_MAC_DASH |
	    IWX_CSR_HW_IF_CONFIG_REG_MSK_MAC_STEP |
	    IWX_CSR_HW_IF_CONFIG_REG_MSK_PHY_STEP |
	    IWX_CSR_HW_IF_CONFIG_REG_MSK_PHY_DASH |
	    IWX_CSR_HW_IF_CONFIG_REG_MSK_PHY_TYPE |
	    IWX_CSR_HW_IF_CONFIG_REG_BIT_RADIO_SI |
	    IWX_CSR_HW_IF_CONFIG_REG_BIT_MAC_SI;

	val = IWX_READ(sc, IWX_CSR_HW_IF_CONFIG_REG);
	val &= ~mask;
	val |= reg_val;
	IWX_WRITE(sc, IWX_CSR_HW_IF_CONFIG_REG, val);
}

int
iwx_nic_rx_init(struct iwx_softc *sc)
{
	IWX_WRITE_1(sc, IWX_CSR_INT_COALESCING, IWX_HOST_INT_TIMEOUT_DEF);

	/*
	 * We don't configure the RFH; the firmware will do that.
	 * Rx descriptors are set when firmware sends an ALIVE interrupt.
	 */
	return 0;
}

int
iwx_nic_init(struct iwx_softc *sc)
{
	int err;

	iwx_apm_init(sc);
	if (sc->sc_device_family < IWX_DEVICE_FAMILY_AX210)
		iwx_nic_config(sc);

	err = iwx_nic_rx_init(sc);
	if (err)
		return err;

	IWX_SETBITS(sc, IWX_CSR_MAC_SHADOW_REG_CTRL, 0x800fffff);

	return 0;
}

/* Map a TID to an ieee80211_edca_ac category. */
const uint8_t iwx_tid_to_ac[IWX_MAX_TID_COUNT] = {
	EDCA_AC_BE,
	EDCA_AC_BK,
	EDCA_AC_BK,
	EDCA_AC_BE,
	EDCA_AC_VI,
	EDCA_AC_VI,
	EDCA_AC_VO,
	EDCA_AC_VO,
};

/* Map ieee80211_edca_ac categories to firmware Tx FIFO. */
const uint8_t iwx_ac_to_tx_fifo[] = {
	IWX_GEN2_EDCA_TX_FIFO_BE,
	IWX_GEN2_EDCA_TX_FIFO_BK,
	IWX_GEN2_EDCA_TX_FIFO_VI,
	IWX_GEN2_EDCA_TX_FIFO_VO,
};

int
iwx_enable_txq(struct iwx_softc *sc, int sta_id, int qid, int tid,
    int num_slots)
{
	struct iwx_rx_packet *pkt;
	struct iwx_tx_queue_cfg_rsp *resp;
	struct iwx_tx_queue_cfg_cmd cmd_v0;
	struct iwx_scd_queue_cfg_cmd cmd_v3;
	struct iwx_host_cmd hcmd = {
		.flags = IWX_CMD_WANT_RESP,
		.resp_pkt_len = sizeof(*pkt) + sizeof(*resp),
	};
	struct iwx_tx_ring *ring = &sc->txq[qid];
	int err, fwqid, cmd_ver;
	uint32_t wr_idx;
	size_t resp_len;

	iwx_reset_tx_ring(sc, ring);

	cmd_ver = iwx_lookup_cmd_ver(sc, IWX_DATA_PATH_GROUP,
	    IWX_SCD_QUEUE_CONFIG_CMD);
	if (cmd_ver == 0 || cmd_ver == IWX_FW_CMD_VER_UNKNOWN) {
		memset(&cmd_v0, 0, sizeof(cmd_v0));
		cmd_v0.sta_id = sta_id;
		cmd_v0.tid = tid;
		cmd_v0.flags = htole16(IWX_TX_QUEUE_CFG_ENABLE_QUEUE);
		cmd_v0.cb_size = htole32(IWX_TFD_QUEUE_CB_SIZE(num_slots));
		cmd_v0.byte_cnt_addr = htole64(ring->bc_tbl.paddr);
		cmd_v0.tfdq_addr = htole64(ring->desc_dma.paddr);
		hcmd.id = IWX_SCD_QUEUE_CFG;
		hcmd.data[0] = &cmd_v0;
		hcmd.len[0] = sizeof(cmd_v0);
	} else if (cmd_ver == 3) {
		memset(&cmd_v3, 0, sizeof(cmd_v3));
		cmd_v3.operation = htole32(IWX_SCD_QUEUE_ADD);
		cmd_v3.u.add.tfdq_dram_addr = htole64(ring->desc_dma.paddr);
		cmd_v3.u.add.bc_dram_addr = htole64(ring->bc_tbl.paddr);
		cmd_v3.u.add.cb_size = htole32(IWX_TFD_QUEUE_CB_SIZE(num_slots));
		cmd_v3.u.add.flags = htole32(0);
		cmd_v3.u.add.sta_mask = htole32(1 << sta_id);
		cmd_v3.u.add.tid = tid;
		hcmd.id = IWX_WIDE_ID(IWX_DATA_PATH_GROUP,
		    IWX_SCD_QUEUE_CONFIG_CMD);
		hcmd.data[0] = &cmd_v3;
		hcmd.len[0] = sizeof(cmd_v3);
	} else {
		printf("%s: unsupported SCD_QUEUE_CFG command version %d\n",
		    DEVNAME(sc), cmd_ver);
		return ENOTSUP;
	}

	err = iwx_send_cmd(sc, &hcmd);
	if (err)
		return err;

	pkt = hcmd.resp_pkt;
	if (!pkt || (pkt->hdr.flags & IWX_CMD_FAILED_MSK)) {
		err = EIO;
		goto out;
	}

	resp_len = iwx_rx_packet_payload_len(pkt);
	if (resp_len != sizeof(*resp)) {
		err = EIO;
		goto out;
	}

	resp = (void *)pkt->data;
	fwqid = le16toh(resp->queue_number);
	wr_idx = le16toh(resp->write_pointer);

	/* Unlike iwlwifi, we do not support dynamic queue ID assignment. */
	if (fwqid != qid) {
		err = EIO;
		goto out;
	}

	if (wr_idx != ring->cur_hw) {
		err = EIO;
		goto out;
	}

	sc->qenablemsk |= (1 << qid);
	ring->tid = tid;
out:
	iwx_free_resp(sc, &hcmd);
	return err;
}

int
iwx_disable_txq(struct iwx_softc *sc, int sta_id, int qid, uint8_t tid)
{
	struct iwx_rx_packet *pkt;
	struct iwx_tx_queue_cfg_rsp *resp;
	struct iwx_tx_queue_cfg_cmd cmd_v0;
	struct iwx_scd_queue_cfg_cmd cmd_v3;
	struct iwx_host_cmd hcmd = {
		.flags = IWX_CMD_WANT_RESP,
		.resp_pkt_len = sizeof(*pkt) + sizeof(*resp),
	};
	struct iwx_tx_ring *ring = &sc->txq[qid];
	int err, cmd_ver;

	cmd_ver = iwx_lookup_cmd_ver(sc, IWX_DATA_PATH_GROUP,
	    IWX_SCD_QUEUE_CONFIG_CMD);
	if (cmd_ver == 0 || cmd_ver == IWX_FW_CMD_VER_UNKNOWN) {
		memset(&cmd_v0, 0, sizeof(cmd_v0));
		cmd_v0.sta_id = sta_id;
		cmd_v0.tid = tid;
		cmd_v0.flags = htole16(0); /* clear "queue enabled" flag */
		cmd_v0.cb_size = htole32(0);
		cmd_v0.byte_cnt_addr = htole64(0);
		cmd_v0.tfdq_addr = htole64(0);
		hcmd.id = IWX_SCD_QUEUE_CFG;
		hcmd.data[0] = &cmd_v0;
		hcmd.len[0] = sizeof(cmd_v0);
	} else if (cmd_ver == 3) {
		memset(&cmd_v3, 0, sizeof(cmd_v3));
		cmd_v3.operation = htole32(IWX_SCD_QUEUE_REMOVE);
		cmd_v3.u.remove.sta_mask = htole32(1 << sta_id);
		cmd_v3.u.remove.tid = tid;
		hcmd.id = IWX_WIDE_ID(IWX_DATA_PATH_GROUP,
		    IWX_SCD_QUEUE_CONFIG_CMD);
		hcmd.data[0] = &cmd_v3;
		hcmd.len[0] = sizeof(cmd_v3);
	} else {
		printf("%s: unsupported SCD_QUEUE_CFG command version %d\n",
		    DEVNAME(sc), cmd_ver);
		return ENOTSUP;
	}

	err = iwx_send_cmd(sc, &hcmd);
	if (err)
		return err;

	pkt = hcmd.resp_pkt;
	if (!pkt || (pkt->hdr.flags & IWX_CMD_FAILED_MSK)) {
		err = EIO;
		goto out;
	}

	sc->qenablemsk &= ~(1 << qid);
	iwx_reset_tx_ring(sc, ring);
out:
	iwx_free_resp(sc, &hcmd);
	return err;
}

void
iwx_post_alive(struct iwx_softc *sc)
{
	int txcmd_ver;

	iwx_ict_reset(sc);

	txcmd_ver = iwx_lookup_notif_ver(sc, IWX_LONG_GROUP, IWX_TX_CMD) ;
	if (txcmd_ver != IWX_FW_CMD_VER_UNKNOWN && txcmd_ver > 6)
		sc->sc_rate_n_flags_version = 2;
	else
		sc->sc_rate_n_flags_version = 1;

	txcmd_ver = iwx_lookup_cmd_ver(sc, IWX_LONG_GROUP, IWX_TX_CMD);
}

int
iwx_schedule_session_protection(struct iwx_softc *sc, struct iwx_node *in,
    uint32_t duration_tu)
{
	struct iwx_session_prot_cmd cmd = {
		.id_and_color = htole32(IWX_FW_CMD_ID_AND_COLOR(in->in_id,
		    in->in_color)),
		.action = htole32(IWX_FW_CTXT_ACTION_ADD),
		.conf_id = htole32(IWX_SESSION_PROTECT_CONF_ASSOC),
		.duration_tu = htole32(duration_tu),
	};
	uint32_t cmd_id;
	int err;

	cmd_id = iwx_cmd_id(IWX_SESSION_PROTECTION_CMD, IWX_MAC_CONF_GROUP, 0);
	err = iwx_send_cmd_pdu(sc, cmd_id, 0, sizeof(cmd), &cmd);
	if (!err)
		sc->sc_flags |= IWX_FLAG_TE_ACTIVE;
	return err;
}

void
iwx_unprotect_session(struct iwx_softc *sc, struct iwx_node *in)
{
	struct iwx_session_prot_cmd cmd = {
		.id_and_color = htole32(IWX_FW_CMD_ID_AND_COLOR(in->in_id,
		    in->in_color)),
		.action = htole32(IWX_FW_CTXT_ACTION_REMOVE),
		.conf_id = htole32(IWX_SESSION_PROTECT_CONF_ASSOC),
		.duration_tu = 0,
	};
	uint32_t cmd_id;

	/* Do nothing if the time event has already ended. */
	if ((sc->sc_flags & IWX_FLAG_TE_ACTIVE) == 0)
		return;

	cmd_id = iwx_cmd_id(IWX_SESSION_PROTECTION_CMD, IWX_MAC_CONF_GROUP, 0);
	if (iwx_send_cmd_pdu(sc, cmd_id, 0, sizeof(cmd), &cmd) == 0)
		sc->sc_flags &= ~IWX_FLAG_TE_ACTIVE;
}

/*
 * NVM read access and content parsing.  We do not support
 * external NVM or writing NVM.
 */

uint8_t
iwx_fw_valid_tx_ant(struct iwx_softc *sc)
{
	uint8_t tx_ant;

	tx_ant = ((sc->sc_fw_phy_config & IWX_FW_PHY_CFG_TX_CHAIN)
	    >> IWX_FW_PHY_CFG_TX_CHAIN_POS);

	if (sc->sc_nvm.valid_tx_ant)
		tx_ant &= sc->sc_nvm.valid_tx_ant;

	return tx_ant;
}

uint8_t
iwx_fw_valid_rx_ant(struct iwx_softc *sc)
{
	uint8_t rx_ant;

	rx_ant = ((sc->sc_fw_phy_config & IWX_FW_PHY_CFG_RX_CHAIN)
	    >> IWX_FW_PHY_CFG_RX_CHAIN_POS);

	if (sc->sc_nvm.valid_rx_ant)
		rx_ant &= sc->sc_nvm.valid_rx_ant;

	return rx_ant;
}

void
iwx_init_channel_map(struct iwx_softc *sc, uint16_t *channel_profile_v3,
    uint32_t *channel_profile_v4, int nchan_profile)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct iwx_nvm_data *data = &sc->sc_nvm;
	int ch_idx;
	struct ieee80211_channel *channel;
	uint32_t ch_flags;
	int is_5ghz;
	int flags, hw_value;
	int nchan;
	const uint8_t *nvm_channels;

	if (sc->sc_uhb_supported) {
		nchan = nitems(iwx_nvm_channels_uhb);
		nvm_channels = iwx_nvm_channels_uhb;
	} else {
		nchan = nitems(iwx_nvm_channels_8000);
		nvm_channels = iwx_nvm_channels_8000;
	}

	for (ch_idx = 0; ch_idx < nchan && ch_idx < nchan_profile; ch_idx++) {
		if (channel_profile_v4)
			ch_flags = le32_to_cpup(channel_profile_v4 + ch_idx);
		else
			ch_flags = le16_to_cpup(channel_profile_v3 + ch_idx);

		/* net80211 cannot handle 6 GHz channel numbers yet */
		if (ch_idx >= IWX_NUM_2GHZ_CHANNELS + IWX_NUM_5GHZ_CHANNELS)
			break;

		is_5ghz = ch_idx >= IWX_NUM_2GHZ_CHANNELS;
		if (is_5ghz && !data->sku_cap_band_52GHz_enable)
			ch_flags &= ~IWX_NVM_CHANNEL_VALID;

		hw_value = nvm_channels[ch_idx];
		channel = &ic->ic_channels[hw_value];

		if (!(ch_flags & IWX_NVM_CHANNEL_VALID)) {
			channel->ic_freq = 0;
			channel->ic_flags = 0;
			continue;
		}

		if (!is_5ghz) {
			flags = IEEE80211_CHAN_2GHZ;
			channel->ic_flags
			    = IEEE80211_CHAN_CCK
			    | IEEE80211_CHAN_OFDM
			    | IEEE80211_CHAN_DYN
			    | IEEE80211_CHAN_2GHZ;
		} else {
			flags = IEEE80211_CHAN_5GHZ;
			channel->ic_flags =
			    IEEE80211_CHAN_A;
		}
		channel->ic_freq = ieee80211_ieee2mhz(hw_value, flags);

		if (!(ch_flags & IWX_NVM_CHANNEL_ACTIVE))
			channel->ic_flags |= IEEE80211_CHAN_PASSIVE;

		if (data->sku_cap_11n_enable) {
			channel->ic_flags |= IEEE80211_CHAN_HT;
			if (ch_flags & IWX_NVM_CHANNEL_40MHZ)
				channel->ic_flags |= IEEE80211_CHAN_40MHZ;
		}

		if (is_5ghz && data->sku_cap_11ac_enable) {
			channel->ic_flags |= IEEE80211_CHAN_VHT;
			if (ch_flags & IWX_NVM_CHANNEL_80MHZ)
				channel->ic_xflags |= IEEE80211_CHANX_80MHZ;
		}
	}
}

int
iwx_mimo_enabled(struct iwx_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;

	return !sc->sc_nvm.sku_cap_mimo_disable &&
	    (ic->ic_userflags & IEEE80211_F_NOMIMO) == 0;
}

void
iwx_setup_ht_rates(struct iwx_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	uint8_t rx_ant;

	/* TX is supported with the same MCS as RX. */
	ic->ic_tx_mcs_set = IEEE80211_TX_MCS_SET_DEFINED;

	memset(ic->ic_sup_mcs, 0, sizeof(ic->ic_sup_mcs));
	ic->ic_sup_mcs[0] = 0xff;		/* MCS 0-7 */

	if (!iwx_mimo_enabled(sc))
		return;

	rx_ant = iwx_fw_valid_rx_ant(sc);
	if ((rx_ant & IWX_ANT_AB) == IWX_ANT_AB ||
	    (rx_ant & IWX_ANT_BC) == IWX_ANT_BC)
		ic->ic_sup_mcs[1] = 0xff;	/* MCS 8-15 */
}

void
iwx_setup_vht_rates(struct iwx_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	uint8_t rx_ant = iwx_fw_valid_rx_ant(sc);
	int n;

	ic->ic_vht_rxmcs = (IEEE80211_VHT_MCS_0_9 <<
	    IEEE80211_VHT_MCS_FOR_SS_SHIFT(1));

	if (iwx_mimo_enabled(sc) &&
	    ((rx_ant & IWX_ANT_AB) == IWX_ANT_AB ||
	    (rx_ant & IWX_ANT_BC) == IWX_ANT_BC)) {
		ic->ic_vht_rxmcs |= (IEEE80211_VHT_MCS_0_9 <<
		    IEEE80211_VHT_MCS_FOR_SS_SHIFT(2));
	} else {
		ic->ic_vht_rxmcs |= (IEEE80211_VHT_MCS_SS_NOT_SUPP <<
		    IEEE80211_VHT_MCS_FOR_SS_SHIFT(2));
	}

	for (n = 3; n <= IEEE80211_VHT_NUM_SS; n++) {
		ic->ic_vht_rxmcs |= (IEEE80211_VHT_MCS_SS_NOT_SUPP <<
		    IEEE80211_VHT_MCS_FOR_SS_SHIFT(n));
	}

	ic->ic_vht_txmcs = ic->ic_vht_rxmcs;
}

void
iwx_init_reorder_buffer(struct iwx_reorder_buffer *reorder_buf,
    uint16_t ssn, uint16_t buf_size)
{
	reorder_buf->head_sn = ssn;
	reorder_buf->num_stored = 0;
	reorder_buf->buf_size = buf_size;
	reorder_buf->last_amsdu = 0;
	reorder_buf->last_sub_index = 0;
	reorder_buf->removed = 0;
	reorder_buf->valid = 0;
	reorder_buf->consec_oldsn_drops = 0;
	reorder_buf->consec_oldsn_ampdu_gp2 = 0;
	reorder_buf->consec_oldsn_prev_drop = 0;
}

void
iwx_clear_reorder_buffer(struct iwx_softc *sc, struct iwx_rxba_data *rxba)
{
	int i;
	struct iwx_reorder_buffer *reorder_buf = &rxba->reorder_buf;
	struct iwx_reorder_buf_entry *entry;

	for (i = 0; i < reorder_buf->buf_size; i++) {
		entry = &rxba->entries[i];
		ml_purge(&entry->frames);
		timerclear(&entry->reorder_time);
	}

	reorder_buf->removed = 1;
	timeout_del(&reorder_buf->reorder_timer);
	timerclear(&rxba->last_rx);
	timeout_del(&rxba->session_timer);
	rxba->baid = IWX_RX_REORDER_DATA_INVALID_BAID;
}

#define RX_REORDER_BUF_TIMEOUT_MQ_USEC (100000ULL)

void
iwx_rx_ba_session_expired(void *arg)
{
	struct iwx_rxba_data *rxba = arg;
	struct iwx_softc *sc = rxba->sc;
	struct ieee80211com *ic = &sc->sc_ic;
	struct ieee80211_node *ni = ic->ic_bss;
	struct timeval now, timeout, expiry;
	int s;

	s = splnet();
	if ((sc->sc_flags & IWX_FLAG_SHUTDOWN) == 0 &&
	    ic->ic_state == IEEE80211_S_RUN &&
	    rxba->baid != IWX_RX_REORDER_DATA_INVALID_BAID) {
		getmicrouptime(&now);
		USEC_TO_TIMEVAL(RX_REORDER_BUF_TIMEOUT_MQ_USEC, &timeout);
		timeradd(&rxba->last_rx, &timeout, &expiry);
		if (timercmp(&now, &expiry, <)) {
			timeout_add_usec(&rxba->session_timer, rxba->timeout);
		} else {
			ic->ic_stats.is_ht_rx_ba_timeout++;
			ieee80211_delba_request(ic, ni,
			    IEEE80211_REASON_TIMEOUT, 0, rxba->tid);
		}
	}
	splx(s);
}

void
iwx_rx_bar_frame_release(struct iwx_softc *sc, struct iwx_rx_packet *pkt,
    struct mbuf_list *ml)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ieee80211_node *ni = ic->ic_bss;
	struct iwx_bar_frame_release *release = (void *)pkt->data;
	struct iwx_reorder_buffer *buf;
	struct iwx_rxba_data *rxba;
	unsigned int baid, nssn, sta_id, tid;

	if (iwx_rx_packet_payload_len(pkt) < sizeof(*release))
		return;

	baid = (le32toh(release->ba_info) & IWX_BAR_FRAME_RELEASE_BAID_MASK) >>
	    IWX_BAR_FRAME_RELEASE_BAID_SHIFT;
	if (baid == IWX_RX_REORDER_DATA_INVALID_BAID ||
	    baid >= nitems(sc->sc_rxba_data))
		return;

	rxba = &sc->sc_rxba_data[baid];
	if (rxba->baid == IWX_RX_REORDER_DATA_INVALID_BAID)
		return;

	tid = le32toh(release->sta_tid) & IWX_BAR_FRAME_RELEASE_TID_MASK;
	sta_id = (le32toh(release->sta_tid) &
	    IWX_BAR_FRAME_RELEASE_STA_MASK) >> IWX_BAR_FRAME_RELEASE_STA_SHIFT;
	if (tid != rxba->tid || rxba->sta_id != IWX_STATION_ID)
		return;

	nssn = le32toh(release->ba_info) & IWX_BAR_FRAME_RELEASE_NSSN_MASK;
	buf = &rxba->reorder_buf;
	iwx_release_frames(sc, ni, rxba, buf, nssn, ml);
}

void
iwx_reorder_timer_expired(void *arg)
{
	struct mbuf_list ml = MBUF_LIST_INITIALIZER();
	struct iwx_reorder_buffer *buf = arg;
	struct iwx_rxba_data *rxba = iwx_rxba_data_from_reorder_buf(buf);
	struct iwx_reorder_buf_entry *entries = &rxba->entries[0];
	struct iwx_softc *sc = rxba->sc;
	struct ieee80211com *ic = &sc->sc_ic;
	struct ieee80211_node *ni = ic->ic_bss;
	int i, s;
	uint16_t sn = 0, index = 0;
	int expired = 0;
	int cont = 0;
	struct timeval now, timeout, expiry;

	if (!buf->num_stored || buf->removed)
		return;

	s = splnet();
	getmicrouptime(&now);
	USEC_TO_TIMEVAL(RX_REORDER_BUF_TIMEOUT_MQ_USEC, &timeout);

	for (i = 0; i < buf->buf_size ; i++) {
		index = (buf->head_sn + i) % buf->buf_size;

		if (ml_empty(&entries[index].frames)) {
			/*
			 * If there is a hole and the next frame didn't expire
			 * we want to break and not advance SN.
			 */
			cont = 0;
			continue;
		}
		timeradd(&entries[index].reorder_time, &timeout, &expiry);
		if (!cont && timercmp(&now, &expiry, <))
			break;

		expired = 1;
		/* continue until next hole after this expired frame */
		cont = 1;
		sn = (buf->head_sn + (i + 1)) & 0xfff;
	}

	if (expired) {
		/* SN is set to the last expired frame + 1 */
		iwx_release_frames(sc, ni, rxba, buf, sn, &ml);
		if_input(&sc->sc_ic.ic_if, &ml);
		ic->ic_stats.is_ht_rx_ba_window_gap_timeout++;
	} else {
		/*
		 * If no frame expired and there are stored frames, index is now
		 * pointing to the first unexpired frame - modify reorder timeout
		 * accordingly.
		 */
		timeout_add_usec(&buf->reorder_timer,
		    RX_REORDER_BUF_TIMEOUT_MQ_USEC);
	}

	splx(s);
}

#define IWX_MAX_RX_BA_SESSIONS 16

struct iwx_rxba_data *
iwx_find_rxba_data(struct iwx_softc *sc, uint8_t tid)
{
	int i;

	for (i = 0; i < nitems(sc->sc_rxba_data); i++) {
		if (sc->sc_rxba_data[i].baid ==
		    IWX_RX_REORDER_DATA_INVALID_BAID)
			continue;
		if (sc->sc_rxba_data[i].tid == tid)
			return &sc->sc_rxba_data[i];
	}

	return NULL;
}

int
iwx_sta_rx_agg_baid_cfg_cmd(struct iwx_softc *sc, struct ieee80211_node *ni,
    uint8_t tid, uint16_t ssn, uint16_t winsize, int timeout_val, int start,
    uint8_t *baid)
{
	struct iwx_rx_baid_cfg_cmd cmd;
	uint32_t new_baid = 0;
	int err;

	splassert(IPL_NET);

	memset(&cmd, 0, sizeof(cmd));

	if (start) {
		cmd.action = IWX_RX_BAID_ACTION_ADD;
		cmd.alloc.sta_id_mask = htole32(1 << IWX_STATION_ID);
		cmd.alloc.tid = tid;
		cmd.alloc.ssn = htole16(ssn);
		cmd.alloc.win_size = htole16(winsize);
	} else {
		struct iwx_rxba_data *rxba;

		rxba = iwx_find_rxba_data(sc, tid);
		if (rxba == NULL)
			return ENOENT;
		*baid = rxba->baid;

		cmd.action = IWX_RX_BAID_ACTION_REMOVE;
		if (iwx_lookup_cmd_ver(sc, IWX_DATA_PATH_GROUP,
		    IWX_RX_BAID_ALLOCATION_CONFIG_CMD) == 1) {
			cmd.remove_v1.baid = rxba->baid;
		} else {
			cmd.remove.sta_id_mask = htole32(1 << IWX_STATION_ID);
			cmd.remove.tid = tid;
		}
	}

	err = iwx_send_cmd_pdu_status(sc, IWX_WIDE_ID(IWX_DATA_PATH_GROUP,
	    IWX_RX_BAID_ALLOCATION_CONFIG_CMD), sizeof(cmd), &cmd, &new_baid);
	if (err)
		return err;

	if (start) {
		if (new_baid >= nitems(sc->sc_rxba_data))
			return ERANGE;
		*baid = new_baid;
	}

	return 0;
}

int
iwx_sta_rx_agg_sta_cmd(struct iwx_softc *sc, struct ieee80211_node *ni,
    uint8_t tid, uint16_t ssn, uint16_t winsize, int timeout_val, int start,
    uint8_t *baid)
{
	struct iwx_add_sta_cmd cmd;
	struct iwx_node *in = (void *)ni;
	int err;
	uint32_t status;

	splassert(IPL_NET);

	memset(&cmd, 0, sizeof(cmd));

	cmd.sta_id = IWX_STATION_ID;
	cmd.mac_id_n_color
	    = htole32(IWX_FW_CMD_ID_AND_COLOR(in->in_id, in->in_color));
	cmd.add_modify = IWX_STA_MODE_MODIFY;

	if (start) {
		cmd.add_immediate_ba_tid = (uint8_t)tid;
		cmd.add_immediate_ba_ssn = htole16(ssn);
		cmd.rx_ba_window = htole16(winsize);
	} else {
		struct iwx_rxba_data *rxba;

		rxba = iwx_find_rxba_data(sc, tid);
		if (rxba == NULL)
			return ENOENT;
		*baid = rxba->baid;

		cmd.remove_immediate_ba_tid = (uint8_t)tid;
	}
	cmd.modify_mask = start ? IWX_STA_MODIFY_ADD_BA_TID :
	    IWX_STA_MODIFY_REMOVE_BA_TID;

	status = IWX_ADD_STA_SUCCESS;
	err = iwx_send_cmd_pdu_status(sc, IWX_ADD_STA, sizeof(cmd), &cmd,
	    &status);
	if (err)
		return err;

	if ((status & IWX_ADD_STA_STATUS_MASK) != IWX_ADD_STA_SUCCESS)
		return EIO;

	if (!(status & IWX_ADD_STA_BAID_VALID_MASK))
		return EINVAL;

	if (start) {
		*baid = (status & IWX_ADD_STA_BAID_MASK) >>
		    IWX_ADD_STA_BAID_SHIFT;
		if (*baid == IWX_RX_REORDER_DATA_INVALID_BAID ||
		    *baid >= nitems(sc->sc_rxba_data))
			return ERANGE;
	}

	return 0;
}

void
iwx_sta_rx_agg(struct iwx_softc *sc, struct ieee80211_node *ni, uint8_t tid,
    uint16_t ssn, uint16_t winsize, int timeout_val, int start)
{
	struct ieee80211com *ic = &sc->sc_ic;
	int err, s;
	struct iwx_rxba_data *rxba = NULL;
	uint8_t baid = 0;

	s = splnet();

	if (start && sc->sc_rx_ba_sessions >= IWX_MAX_RX_BA_SESSIONS) {
		ieee80211_addba_req_refuse(ic, ni, tid);
		splx(s);
		return;
	}

	if (isset(sc->sc_enabled_capa, IWX_UCODE_TLV_CAPA_BAID_ML_SUPPORT)) {
		err = iwx_sta_rx_agg_baid_cfg_cmd(sc, ni, tid, ssn, winsize,
		    timeout_val, start, &baid);
	} else {
		err = iwx_sta_rx_agg_sta_cmd(sc, ni, tid, ssn, winsize,
		    timeout_val, start, &baid);
	}
	if (err) {
		ieee80211_addba_req_refuse(ic, ni, tid);
		splx(s);
		return;
	}

	rxba = &sc->sc_rxba_data[baid];

	/* Deaggregation is done in hardware. */
	if (start) {
		if (rxba->baid != IWX_RX_REORDER_DATA_INVALID_BAID) {
			ieee80211_addba_req_refuse(ic, ni, tid);
			splx(s);
			return;
		}
		rxba->sta_id = IWX_STATION_ID;
		rxba->tid = tid;
		rxba->baid = baid;
		rxba->timeout = timeout_val;
		getmicrouptime(&rxba->last_rx);
		iwx_init_reorder_buffer(&rxba->reorder_buf, ssn,
		    winsize);
		if (timeout_val != 0) {
			struct ieee80211_rx_ba *ba;
			timeout_add_usec(&rxba->session_timer,
			    timeout_val);
			/* XXX disable net80211's BA timeout handler */
			ba = &ni->ni_rx_ba[tid];
			ba->ba_timeout_val = 0;
		}
	} else
		iwx_clear_reorder_buffer(sc, rxba);

	if (start) {
		sc->sc_rx_ba_sessions++;
		ieee80211_addba_req_accept(ic, ni, tid);
	} else if (sc->sc_rx_ba_sessions > 0)
		sc->sc_rx_ba_sessions--;

	splx(s);
}

void
iwx_mac_ctxt_task(void *arg)
{
	struct iwx_softc *sc = arg;
	struct ieee80211com *ic = &sc->sc_ic;
	struct iwx_node *in = (void *)ic->ic_bss;
	int err, s = splnet();

	if ((sc->sc_flags & IWX_FLAG_SHUTDOWN) ||
	    ic->ic_state != IEEE80211_S_RUN) {
		refcnt_rele_wake(&sc->task_refs);
		splx(s);
		return;
	}

	err = iwx_mac_ctxt_cmd(sc, in, IWX_FW_CTXT_ACTION_MODIFY, 1);
	if (err)
		printf("%s: failed to update MAC\n", DEVNAME(sc));

	iwx_unprotect_session(sc, in);

	refcnt_rele_wake(&sc->task_refs);
	splx(s);
}

void
iwx_phy_ctxt_task(void *arg)
{
	struct iwx_softc *sc = arg;
	struct ieee80211com *ic = &sc->sc_ic;
	struct iwx_node *in = (void *)ic->ic_bss;
	struct ieee80211_node *ni = &in->in_ni;
	uint8_t chains, sco, vht_chan_width;
	int err, s = splnet();

	if ((sc->sc_flags & IWX_FLAG_SHUTDOWN) ||
	    ic->ic_state != IEEE80211_S_RUN ||
	    in->in_phyctxt == NULL) {
		refcnt_rele_wake(&sc->task_refs);
		splx(s);
		return;
	}

	chains = iwx_mimo_enabled(sc) ? 2 : 1;
	if ((ni->ni_flags & IEEE80211_NODE_HT) &&
	    IEEE80211_CHAN_40MHZ_ALLOWED(ni->ni_chan) &&
	    ieee80211_node_supports_ht_chan40(ni))
		sco = (ni->ni_htop0 & IEEE80211_HTOP0_SCO_MASK);
	else
		sco = IEEE80211_HTOP0_SCO_SCN;
	if ((ni->ni_flags & IEEE80211_NODE_VHT) &&
	    IEEE80211_CHAN_80MHZ_ALLOWED(in->in_ni.ni_chan) &&
	    ieee80211_node_supports_vht_chan80(ni))
		vht_chan_width = IEEE80211_VHTOP0_CHAN_WIDTH_80;
	else
		vht_chan_width = IEEE80211_VHTOP0_CHAN_WIDTH_HT;
	if (in->in_phyctxt->sco != sco ||
	    in->in_phyctxt->vht_chan_width != vht_chan_width) {
		err = iwx_phy_ctxt_update(sc, in->in_phyctxt,
		    in->in_phyctxt->channel, chains, chains, 0, sco,
		    vht_chan_width);
		if (err)
			printf("%s: failed to update PHY\n", DEVNAME(sc));
	}

	refcnt_rele_wake(&sc->task_refs);
	splx(s);
}

void
iwx_updatechan(struct ieee80211com *ic)
{
	struct iwx_softc *sc = ic->ic_softc;

	if (ic->ic_state == IEEE80211_S_RUN &&
	    !task_pending(&sc->newstate_task))
		iwx_add_task(sc, systq, &sc->phy_ctxt_task);
}

void
iwx_updateprot(struct ieee80211com *ic)
{
	struct iwx_softc *sc = ic->ic_softc;

	if (ic->ic_state == IEEE80211_S_RUN &&
	    !task_pending(&sc->newstate_task))
		iwx_add_task(sc, systq, &sc->mac_ctxt_task);
}

void
iwx_updateslot(struct ieee80211com *ic)
{
	struct iwx_softc *sc = ic->ic_softc;

	if (ic->ic_state == IEEE80211_S_RUN &&
	    !task_pending(&sc->newstate_task))
		iwx_add_task(sc, systq, &sc->mac_ctxt_task);
}

void
iwx_updateedca(struct ieee80211com *ic)
{
	struct iwx_softc *sc = ic->ic_softc;

	if (ic->ic_state == IEEE80211_S_RUN &&
	    !task_pending(&sc->newstate_task))
		iwx_add_task(sc, systq, &sc->mac_ctxt_task);
}

void
iwx_updatedtim(struct ieee80211com *ic)
{
	struct iwx_softc *sc = ic->ic_softc;

	if (ic->ic_state == IEEE80211_S_RUN &&
	    !task_pending(&sc->newstate_task))
		iwx_add_task(sc, systq, &sc->mac_ctxt_task);
}

void
iwx_sta_tx_agg_start(struct iwx_softc *sc, struct ieee80211_node *ni,
    uint8_t tid)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ieee80211_tx_ba *ba;
	int err, qid;
	struct iwx_tx_ring *ring;

	/* Ensure we can map this TID to an aggregation queue. */
	if (tid >= IWX_MAX_TID_COUNT)
		return;

	ba = &ni->ni_tx_ba[tid];
	if (ba->ba_state != IEEE80211_BA_REQUESTED)
		return;

	qid = sc->aggqid[tid];
	if (qid == 0) {
		/* Firmware should pick the next unused Tx queue. */
		qid = fls(sc->qenablemsk);
	}

	/*
	 * Simply enable the queue.
	 * Firmware handles Tx Ba session setup and teardown.
	 */
	if ((sc->qenablemsk & (1 << qid)) == 0) {
		if (!iwx_nic_lock(sc)) {
			ieee80211_addba_resp_refuse(ic, ni, tid,
			    IEEE80211_STATUS_UNSPECIFIED);
			return;
		}
		err = iwx_enable_txq(sc, IWX_STATION_ID, qid, tid,
		    IWX_TX_RING_COUNT);
		iwx_nic_unlock(sc);
		if (err) {
			printf("%s: could not enable Tx queue %d "
			    "(error %d)\n", DEVNAME(sc), qid, err);
			ieee80211_addba_resp_refuse(ic, ni, tid,
			    IEEE80211_STATUS_UNSPECIFIED);
			return;
		}

		ba->ba_winstart = 0;
	} else
		ba->ba_winstart = ni->ni_qos_txseqs[tid];

	ba->ba_winend = (ba->ba_winstart + ba->ba_winsize - 1) & 0xfff;

	ring = &sc->txq[qid];
	ba->ba_timeout_val = 0;
	ieee80211_addba_resp_accept(ic, ni, tid);
	sc->aggqid[tid] = qid;
}

void
iwx_ba_task(void *arg)
{
	struct iwx_softc *sc = arg;
	struct ieee80211com *ic = &sc->sc_ic;
	struct ieee80211_node *ni = ic->ic_bss;
	int s = splnet();
	int tid;

	for (tid = 0; tid < IWX_MAX_TID_COUNT; tid++) {
		if (sc->sc_flags & IWX_FLAG_SHUTDOWN)
			break;
		if (sc->ba_rx.start_tidmask & (1 << tid)) {
			struct ieee80211_rx_ba *ba = &ni->ni_rx_ba[tid];
			iwx_sta_rx_agg(sc, ni, tid, ba->ba_winstart,
			    ba->ba_winsize, ba->ba_timeout_val, 1);
			sc->ba_rx.start_tidmask &= ~(1 << tid);
		} else if (sc->ba_rx.stop_tidmask & (1 << tid)) {
			iwx_sta_rx_agg(sc, ni, tid, 0, 0, 0, 0);
			sc->ba_rx.stop_tidmask &= ~(1 << tid);
		}
	}

	for (tid = 0; tid < IWX_MAX_TID_COUNT; tid++) {
		if (sc->sc_flags & IWX_FLAG_SHUTDOWN)
			break;
		if (sc->ba_tx.start_tidmask & (1 << tid)) {
			iwx_sta_tx_agg_start(sc, ni, tid);
			sc->ba_tx.start_tidmask &= ~(1 << tid);
		}
	}

	refcnt_rele_wake(&sc->task_refs);
	splx(s);
}

/*
 * This function is called by upper layer when an ADDBA request is received
 * from another STA and before the ADDBA response is sent.
 */
int
iwx_ampdu_rx_start(struct ieee80211com *ic, struct ieee80211_node *ni,
    uint8_t tid)
{
	struct iwx_softc *sc = IC2IFP(ic)->if_softc;

	if (sc->sc_rx_ba_sessions >= IWX_MAX_RX_BA_SESSIONS ||
	    tid >= IWX_MAX_TID_COUNT)
		return ENOSPC;

	if (sc->ba_rx.start_tidmask & (1 << tid))
		return EBUSY;

	sc->ba_rx.start_tidmask |= (1 << tid);
	iwx_add_task(sc, systq, &sc->ba_task);

	return EBUSY;
}

/*
 * This function is called by upper layer on teardown of an HT-immediate
 * Block Ack agreement (eg. upon receipt of a DELBA frame).
 */
void
iwx_ampdu_rx_stop(struct ieee80211com *ic, struct ieee80211_node *ni,
    uint8_t tid)
{
	struct iwx_softc *sc = IC2IFP(ic)->if_softc;

	if (tid >= IWX_MAX_TID_COUNT || sc->ba_rx.stop_tidmask & (1 << tid))
		return;

	sc->ba_rx.stop_tidmask |= (1 << tid);
	iwx_add_task(sc, systq, &sc->ba_task);
}

int
iwx_ampdu_tx_start(struct ieee80211com *ic, struct ieee80211_node *ni,
    uint8_t tid)
{
	struct iwx_softc *sc = IC2IFP(ic)->if_softc;
	struct ieee80211_tx_ba *ba = &ni->ni_tx_ba[tid];

	/*
	 * Require a firmware version which uses an internal AUX queue.
	 * The value of IWX_FIRST_AGG_TX_QUEUE would be incorrect otherwise.
	 */
	if (sc->first_data_qid != IWX_DQA_CMD_QUEUE + 1)
		return ENOTSUP;

	/* Ensure we can map this TID to an aggregation queue. */
	if (tid >= IWX_MAX_TID_COUNT)
		return EINVAL;

	/* We only support a fixed Tx aggregation window size, for now. */
	if (ba->ba_winsize != IWX_FRAME_LIMIT)
		return ENOTSUP;

	/* Is firmware already using an agg queue with this TID? */
	if (sc->aggqid[tid] != 0)
		return ENOSPC;

	/* Are we already processing an ADDBA request? */
	if (sc->ba_tx.start_tidmask & (1 << tid))
		return EBUSY;

	sc->ba_tx.start_tidmask |= (1 << tid);
	iwx_add_task(sc, systq, &sc->ba_task);

	return EBUSY;
}

void
iwx_set_mac_addr_from_csr(struct iwx_softc *sc, struct iwx_nvm_data *data)
{
	uint32_t mac_addr0, mac_addr1;

	memset(data->hw_addr, 0, sizeof(data->hw_addr));

	if (!iwx_nic_lock(sc))
		return;

	mac_addr0 = htole32(IWX_READ(sc, IWX_CSR_MAC_ADDR0_STRAP(sc)));
	mac_addr1 = htole32(IWX_READ(sc, IWX_CSR_MAC_ADDR1_STRAP(sc)));

	iwx_flip_hw_address(mac_addr0, mac_addr1, data->hw_addr);

	/* If OEM fused a valid address, use it instead of the one in OTP. */
	if (iwx_is_valid_mac_addr(data->hw_addr)) {
		iwx_nic_unlock(sc);
		return;
	}

	mac_addr0 = htole32(IWX_READ(sc, IWX_CSR_MAC_ADDR0_OTP(sc)));
	mac_addr1 = htole32(IWX_READ(sc, IWX_CSR_MAC_ADDR1_OTP(sc)));

	iwx_flip_hw_address(mac_addr0, mac_addr1, data->hw_addr);

	iwx_nic_unlock(sc);
}

int
iwx_is_valid_mac_addr(const uint8_t *addr)
{
	static const uint8_t reserved_mac[] = {
		0x02, 0xcc, 0xaa, 0xff, 0xee, 0x00
	};

	return (memcmp(reserved_mac, addr, ETHER_ADDR_LEN) != 0 &&
	    memcmp(etherbroadcastaddr, addr, sizeof(etherbroadcastaddr)) != 0 &&
	    memcmp(etheranyaddr, addr, sizeof(etheranyaddr)) != 0 &&
	    !ETHER_IS_MULTICAST(addr));
}

void
iwx_flip_hw_address(uint32_t mac_addr0, uint32_t mac_addr1, uint8_t *dest)
{
	const uint8_t *hw_addr;

	hw_addr = (const uint8_t *)&mac_addr0;
	dest[0] = hw_addr[3];
	dest[1] = hw_addr[2];
	dest[2] = hw_addr[1];
	dest[3] = hw_addr[0];

	hw_addr = (const uint8_t *)&mac_addr1;
	dest[4] = hw_addr[1];
	dest[5] = hw_addr[0];
}

int
iwx_nvm_get(struct iwx_softc *sc)
{
	struct iwx_nvm_get_info cmd = {};
	struct iwx_nvm_data *nvm = &sc->sc_nvm;
	struct iwx_host_cmd hcmd = {
		.flags = IWX_CMD_WANT_RESP | IWX_CMD_SEND_IN_RFKILL,
		.data = { &cmd, },
		.len = { sizeof(cmd) },
		.id = IWX_WIDE_ID(IWX_REGULATORY_AND_NVM_GROUP,
		    IWX_NVM_GET_INFO)
	};
	int err;
	uint32_t mac_flags;
	/*
	 * All the values in iwx_nvm_get_info_rsp v4 are the same as
	 * in v3, except for the channel profile part of the
	 * regulatory.  So we can just access the new struct, with the
	 * exception of the latter.
	 */
	struct iwx_nvm_get_info_rsp *rsp;
	struct iwx_nvm_get_info_rsp_v3 *rsp_v3;
	int v4 = isset(sc->sc_ucode_api, IWX_UCODE_TLV_API_REGULATORY_NVM_INFO);
	size_t resp_len = v4 ? sizeof(*rsp) : sizeof(*rsp_v3);

	hcmd.resp_pkt_len = sizeof(struct iwx_rx_packet) + resp_len;
	err = iwx_send_cmd(sc, &hcmd);
	if (err)
		return err;

	if (iwx_rx_packet_payload_len(hcmd.resp_pkt) != resp_len) {
		err = EIO;
		goto out;
	}

	memset(nvm, 0, sizeof(*nvm));

	iwx_set_mac_addr_from_csr(sc, nvm);
	if (!iwx_is_valid_mac_addr(nvm->hw_addr)) {
		printf("%s: no valid mac address was found\n", DEVNAME(sc));
		err = EINVAL;
		goto out;
	}

	rsp = (void *)hcmd.resp_pkt->data;

	/* Initialize general data */
	nvm->nvm_version = le16toh(rsp->general.nvm_version);
	nvm->n_hw_addrs = rsp->general.n_hw_addrs;

	/* Initialize MAC sku data */
	mac_flags = le32toh(rsp->mac_sku.mac_sku_flags);
	nvm->sku_cap_11ac_enable =
		!!(mac_flags & IWX_NVM_MAC_SKU_FLAGS_802_11AC_ENABLED);
	nvm->sku_cap_11n_enable =
		!!(mac_flags & IWX_NVM_MAC_SKU_FLAGS_802_11N_ENABLED);
	nvm->sku_cap_11ax_enable =
		!!(mac_flags & IWX_NVM_MAC_SKU_FLAGS_802_11AX_ENABLED);
	nvm->sku_cap_band_24GHz_enable =
		!!(mac_flags & IWX_NVM_MAC_SKU_FLAGS_BAND_2_4_ENABLED);
	nvm->sku_cap_band_52GHz_enable =
		!!(mac_flags & IWX_NVM_MAC_SKU_FLAGS_BAND_5_2_ENABLED);
	nvm->sku_cap_mimo_disable =
		!!(mac_flags & IWX_NVM_MAC_SKU_FLAGS_MIMO_DISABLED);

	/* Initialize PHY sku data */
	nvm->valid_tx_ant = (uint8_t)le32toh(rsp->phy_sku.tx_chains);
	nvm->valid_rx_ant = (uint8_t)le32toh(rsp->phy_sku.rx_chains);

	if (le32toh(rsp->regulatory.lar_enabled) &&
	    isset(sc->sc_enabled_capa, IWX_UCODE_TLV_CAPA_LAR_SUPPORT)) {
		nvm->lar_enabled = 1;
	}

	if (v4) {
		iwx_init_channel_map(sc, NULL,
		    rsp->regulatory.channel_profile, IWX_NUM_CHANNELS);
	} else {
		rsp_v3 = (void *)rsp;
		iwx_init_channel_map(sc, rsp_v3->regulatory.channel_profile,
		    NULL, IWX_NUM_CHANNELS_V1);
	}
out:
	iwx_free_resp(sc, &hcmd);
	return err;
}

int
iwx_load_firmware(struct iwx_softc *sc)
{
	struct iwx_fw_sects *fws;
	int err;

	splassert(IPL_NET);

	sc->sc_uc.uc_intr = 0;
	sc->sc_uc.uc_ok = 0;

	fws = &sc->sc_fw.fw_sects[IWX_UCODE_TYPE_REGULAR];
	if (sc->sc_device_family >= IWX_DEVICE_FAMILY_AX210)
		err = iwx_ctxt_info_gen3_init(sc, fws);
	else
		err = iwx_ctxt_info_init(sc, fws);
	if (err) {
		printf("%s: could not init context info\n", DEVNAME(sc));
		return err;
	}

	/* wait for the firmware to load */
	err = tsleep_nsec(&sc->sc_uc, 0, "iwxuc", SEC_TO_NSEC(1));
	if (err || !sc->sc_uc.uc_ok) {
		printf("%s: could not load firmware, %d\n", DEVNAME(sc), err);
		iwx_ctxt_info_free_paging(sc);
	}

	iwx_dma_contig_free(&sc->iml_dma);
	iwx_ctxt_info_free_fw_img(sc);

	if (!sc->sc_uc.uc_ok)
		return EINVAL;

	return err;
}

int
iwx_start_fw(struct iwx_softc *sc)
{
	int err;

	IWX_WRITE(sc, IWX_CSR_INT, ~0);

	iwx_disable_interrupts(sc);

	/* make sure rfkill handshake bits are cleared */
	IWX_WRITE(sc, IWX_CSR_UCODE_DRV_GP1_CLR, IWX_CSR_UCODE_SW_BIT_RFKILL);
	IWX_WRITE(sc, IWX_CSR_UCODE_DRV_GP1_CLR,
	    IWX_CSR_UCODE_DRV_GP1_BIT_CMD_BLOCKED);

	/* clear (again), then enable firmware load interrupt */
	IWX_WRITE(sc, IWX_CSR_INT, ~0);

	err = iwx_nic_init(sc);
	if (err) {
		printf("%s: unable to init nic\n", DEVNAME(sc));
		return err;
	}

	iwx_enable_fwload_interrupt(sc);

	return iwx_load_firmware(sc);
}

int
iwx_pnvm_setup_fragmented(struct iwx_softc *sc, uint8_t **pnvm_data,
    size_t *pnvm_size, int pnvm_segs)
{
	struct iwx_pnvm_info_dram *pnvm_info;
	int i, err;

	err = iwx_dma_contig_alloc(sc->sc_dmat, &sc->pnvm_dma,
	    sizeof(struct iwx_pnvm_info_dram), 0);
	if (err)
		return err;
	pnvm_info = (struct iwx_pnvm_info_dram *)sc->pnvm_dma.vaddr;

	for (i = 0; i < pnvm_segs; i++) {
		err = iwx_dma_contig_alloc(sc->sc_dmat, &sc->pnvm_seg_dma[i],
		    pnvm_size[i], 0);
		if (err)
			goto fail;
		memcpy(sc->pnvm_seg_dma[i].vaddr, pnvm_data[i], pnvm_size[i]);
		pnvm_info->pnvm_img[i] = htole64(sc->pnvm_seg_dma[i].paddr);
		sc->pnvm_size += pnvm_size[i];
		sc->pnvm_segs++;
	}

	return 0;

fail:
	for (i = 0; i < pnvm_segs; i++)
		iwx_dma_contig_free(&sc->pnvm_seg_dma[i]);
	sc->pnvm_size = 0;
	sc->pnvm_segs = 0;
	iwx_dma_contig_free(&sc->pnvm_dma);

	return err;
}

int
iwx_pnvm_setup(struct iwx_softc *sc, uint8_t **pnvm_data,
    size_t *pnvm_size, int pnvm_segs)
{
	uint8_t *data;
	size_t size = 0;
	int i, err;

	if (isset(sc->sc_enabled_capa, IWX_UCODE_TLV_CAPA_FRAGMENTED_PNVM_IMG))
		return iwx_pnvm_setup_fragmented(sc, pnvm_data, pnvm_size, pnvm_segs);

	for (i = 0; i < pnvm_segs; i++)
		size += pnvm_size[i];

	err = iwx_dma_contig_alloc(sc->sc_dmat, &sc->pnvm_dma, size, 0);
	if (err)
		return err;

	data = sc->pnvm_dma.vaddr;
	for (i = 0; i < pnvm_segs; i++) {
		memcpy(data, pnvm_data[i], pnvm_size[i]);
		data += pnvm_size[i];
	}
	sc->pnvm_size = size;

	return 0;
}

int
iwx_pnvm_handle_section(struct iwx_softc *sc, const uint8_t *data,
    size_t len)
{
	const struct iwx_ucode_tlv *tlv;
	uint32_t sha1 = 0;
	uint16_t mac_type = 0, rf_id = 0;
	uint8_t *pnvm_data[IWX_MAX_DRAM_ENTRY];
	size_t pnvm_size[IWX_MAX_DRAM_ENTRY];
	int pnvm_segs = 0;
	int hw_match = 0;
	uint32_t size = 0;
	int err;
	int i;

	while (len >= sizeof(*tlv)) {
		uint32_t tlv_len, tlv_type;

		len -= sizeof(*tlv);
		tlv = (const void *)data;

		tlv_len = le32toh(tlv->length);
		tlv_type = le32toh(tlv->type);

		if (len < tlv_len) {
			printf("%s: invalid TLV len: %zd/%u\n",
			    DEVNAME(sc), len, tlv_len);
			err = EINVAL;
			goto out;
		}

		data += sizeof(*tlv);

		switch (tlv_type) {
		case IWX_UCODE_TLV_PNVM_VERSION:
			if (tlv_len < sizeof(uint32_t))
				break;

			sha1 = le32_to_cpup((const uint32_t *)data);
			break;
		case IWX_UCODE_TLV_HW_TYPE:
			if (tlv_len < 2 * sizeof(uint16_t))
				break;

			if (hw_match)
				break;

			mac_type = le16_to_cpup((const uint16_t *)data);
			rf_id = le16_to_cpup((const uint16_t *)(data +
			    sizeof(uint16_t)));

			if (mac_type == IWX_CSR_HW_REV_TYPE(sc->sc_hw_rev) &&
			    rf_id == IWX_CSR_HW_RFID_TYPE(sc->sc_hw_rf_id))
				hw_match = 1;
			break;
		case IWX_UCODE_TLV_SEC_RT: {
			const struct iwx_pnvm_section *section;
			uint32_t data_len;

			section = (const void *)data;
			data_len = tlv_len - sizeof(*section);

			/* TODO: remove, this is a deprecated separator */
			if (le32_to_cpup((const uint32_t *)data) == 0xddddeeee)
				break;

			if (pnvm_segs >= nitems(pnvm_data)) {
				err = ERANGE;
				goto out;
			}

			pnvm_data[pnvm_segs] = malloc(data_len, M_DEVBUF,
			    M_WAITOK | M_CANFAIL | M_ZERO);
			if (pnvm_data[pnvm_segs] == NULL) {
				err = ENOMEM;
				goto out;
			}
			memcpy(pnvm_data[pnvm_segs], section->data, data_len);
			pnvm_size[pnvm_segs++] = data_len;
			size += data_len;
			break;
		}
		case IWX_UCODE_TLV_PNVM_SKU:
			/* New PNVM section started, stop parsing. */
			goto done;
		default:
			break;
		}

		if (roundup(tlv_len, 4) > len)
			break;
		len -= roundup(tlv_len, 4);
		data += roundup(tlv_len, 4);
	}
done:
	if (!hw_match || size == 0) {
		err = ENOENT;
		goto out;
	}

	err = iwx_pnvm_setup(sc, pnvm_data, pnvm_size, pnvm_segs);
	if (err) {
		printf("%s: could not allocate DMA memory for PNVM\n",
		    DEVNAME(sc));
		err = ENOMEM;
		goto out;
	}

	iwx_ctxt_info_gen3_set_pnvm(sc);
	sc->sc_pnvm_ver = sha1;
out:
	for (i = 0; i < pnvm_segs; i++)
		free(pnvm_data[i], M_DEVBUF, pnvm_size[i]);
	return err;
}

int
iwx_pnvm_parse(struct iwx_softc *sc, const uint8_t *data, size_t len)
{
	const struct iwx_ucode_tlv *tlv;

	while (len >= sizeof(*tlv)) {
		uint32_t tlv_len, tlv_type;

		len -= sizeof(*tlv);
		tlv = (const void *)data;

		tlv_len = le32toh(tlv->length);
		tlv_type = le32toh(tlv->type);

		if (len < tlv_len || roundup(tlv_len, 4) > len)
			return EINVAL;

		if (tlv_type == IWX_UCODE_TLV_PNVM_SKU) {
			const struct iwx_sku_id *sku_id =
				(const void *)(data + sizeof(*tlv));

			data += sizeof(*tlv) + roundup(tlv_len, 4);
			len -= roundup(tlv_len, 4);

			if (sc->sc_sku_id[0] == le32toh(sku_id->data[0]) &&
			    sc->sc_sku_id[1] == le32toh(sku_id->data[1]) &&
			    sc->sc_sku_id[2] == le32toh(sku_id->data[2]) &&
			    iwx_pnvm_handle_section(sc, data, len) == 0)
				return 0;
		} else {
			data += sizeof(*tlv) + roundup(tlv_len, 4);
			len -= roundup(tlv_len, 4);
		}
	}

	return ENOENT;
}

/* Make AX210 firmware loading context point at PNVM image in DMA memory. */
void
iwx_ctxt_info_gen3_set_pnvm(struct iwx_softc *sc)
{
	struct iwx_prph_scratch *prph_scratch;
	struct iwx_prph_scratch_ctrl_cfg *prph_sc_ctrl;
	int i;

	prph_scratch = sc->prph_scratch_dma.vaddr;
	prph_sc_ctrl = &prph_scratch->ctrl_cfg;

	prph_sc_ctrl->pnvm_cfg.pnvm_base_addr = htole64(sc->pnvm_dma.paddr);
	prph_sc_ctrl->pnvm_cfg.pnvm_size = htole32(sc->pnvm_size);

	bus_dmamap_sync(sc->sc_dmat, sc->pnvm_dma.map, 0,
	    sc->pnvm_dma.size, BUS_DMASYNC_PREWRITE);
	for (i = 0; i < sc->pnvm_segs; i++)
		bus_dmamap_sync(sc->sc_dmat, sc->pnvm_seg_dma[i].map, 0,
		    sc->pnvm_seg_dma[i].size, BUS_DMASYNC_PREWRITE);
}

/*
 * Load platform-NVM (non-volatile-memory) data from the filesystem.
 * This data apparently contains regulatory information and affects device
 * channel configuration.
 * The SKU of AX210 devices tells us which PNVM file section is needed.
 * Pre-AX210 devices store NVM data onboard.
 */
int
iwx_load_pnvm(struct iwx_softc *sc)
{
	const int wait_flags = IWX_PNVM_COMPLETE;
	int s, err = 0;
	u_char *pnvm_data = NULL;
	size_t pnvm_size = 0;

	if (sc->sc_sku_id[0] == 0 &&
	    sc->sc_sku_id[1] == 0 &&
	    sc->sc_sku_id[2] == 0)
		return 0;

	if (sc->sc_pnvm_name) {
		if (sc->pnvm_dma.vaddr == NULL) {
			err = loadfirmware(sc->sc_pnvm_name,
			    &pnvm_data, &pnvm_size);
			if (err) {
				printf("%s: could not read %s (error %d)\n",
				    DEVNAME(sc), sc->sc_pnvm_name, err);
				return err;
			}

			err = iwx_pnvm_parse(sc, pnvm_data, pnvm_size);
			if (err && err != ENOENT) {
				free(pnvm_data, M_DEVBUF, pnvm_size);
				return err;
			}
		} else
			iwx_ctxt_info_gen3_set_pnvm(sc);
	}

	s = splnet();

	if (!iwx_nic_lock(sc)) {
		splx(s);
		free(pnvm_data, M_DEVBUF, pnvm_size);
		return EBUSY;
	}

	/*
	 * If we don't have a platform NVM file simply ask firmware
	 * to proceed without it.
	 */

	iwx_write_umac_prph(sc, IWX_UREG_DOORBELL_TO_ISR6,
	    IWX_UREG_DOORBELL_TO_ISR6_PNVM);

	/* Wait for the pnvm complete notification from firmware. */
	while ((sc->sc_init_complete & wait_flags) != wait_flags) {
		err = tsleep_nsec(&sc->sc_init_complete, 0, "iwxinit",
		    SEC_TO_NSEC(2));
		if (err)
			break;
	}

	splx(s);
	iwx_nic_unlock(sc);
	free(pnvm_data, M_DEVBUF, pnvm_size);
	return err;
}

int
iwx_send_tx_ant_cfg(struct iwx_softc *sc, uint8_t valid_tx_ant)
{
	struct iwx_tx_ant_cfg_cmd tx_ant_cmd = {
		.valid = htole32(valid_tx_ant),
	};

	return iwx_send_cmd_pdu(sc, IWX_TX_ANT_CONFIGURATION_CMD,
	    0, sizeof(tx_ant_cmd), &tx_ant_cmd);
}

int
iwx_send_phy_cfg_cmd(struct iwx_softc *sc)
{
	struct iwx_phy_cfg_cmd phy_cfg_cmd;

	phy_cfg_cmd.phy_cfg = htole32(sc->sc_fw_phy_config);
	phy_cfg_cmd.calib_control.event_trigger =
	    sc->sc_default_calib[IWX_UCODE_TYPE_REGULAR].event_trigger;
	phy_cfg_cmd.calib_control.flow_trigger =
	    sc->sc_default_calib[IWX_UCODE_TYPE_REGULAR].flow_trigger;

	return iwx_send_cmd_pdu(sc, IWX_PHY_CONFIGURATION_CMD, 0,
	    sizeof(phy_cfg_cmd), &phy_cfg_cmd);
}

int
iwx_send_dqa_cmd(struct iwx_softc *sc)
{
	struct iwx_dqa_enable_cmd dqa_cmd = {
		.cmd_queue = htole32(IWX_DQA_CMD_QUEUE),
	};
	uint32_t cmd_id;

	cmd_id = iwx_cmd_id(IWX_DQA_ENABLE_CMD, IWX_DATA_PATH_GROUP, 0);
	return iwx_send_cmd_pdu(sc, cmd_id, 0, sizeof(dqa_cmd), &dqa_cmd);
}

int
iwx_load_ucode_wait_alive(struct iwx_softc *sc)
{
	int err;

	err = iwx_read_firmware(sc);
	if (err)
		return err;

	err = iwx_start_fw(sc);
	if (err)
		return err;

	if (sc->sc_device_family >= IWX_DEVICE_FAMILY_AX210) {
		err = iwx_load_pnvm(sc);
		if (err)
			return err;
	}

	iwx_post_alive(sc);

	return 0;
}

int
iwx_run_init_mvm_ucode(struct iwx_softc *sc, int readnvm)
{
	const int wait_flags = IWX_INIT_COMPLETE;
	struct iwx_nvm_access_complete_cmd nvm_complete = {};
	struct iwx_init_extended_cfg_cmd init_cfg = {
		.init_flags = htole32(IWX_INIT_NVM),
	};
	int err, s;

	if ((sc->sc_flags & IWX_FLAG_RFKILL) && !readnvm) {
		printf("%s: radio is disabled by hardware switch\n",
		    DEVNAME(sc));
		return EPERM;
	}

	s = splnet();
	sc->sc_init_complete = 0;
	err = iwx_load_ucode_wait_alive(sc);
	if (err) {
		printf("%s: failed to load init firmware\n", DEVNAME(sc));
		splx(s);
		return err;
	}

	/*
	 * Send init config command to mark that we are sending NVM
	 * access commands
	 */
	err = iwx_send_cmd_pdu(sc, IWX_WIDE_ID(IWX_SYSTEM_GROUP,
	    IWX_INIT_EXTENDED_CFG_CMD), 0, sizeof(init_cfg), &init_cfg);
	if (err) {
		splx(s);
		return err;
	}

	err = iwx_send_cmd_pdu(sc, IWX_WIDE_ID(IWX_REGULATORY_AND_NVM_GROUP,
	    IWX_NVM_ACCESS_COMPLETE), 0, sizeof(nvm_complete), &nvm_complete);
	if (err) {
		splx(s);
		return err;
	}

	/* Wait for the init complete notification from the firmware. */
	while ((sc->sc_init_complete & wait_flags) != wait_flags) {
		err = tsleep_nsec(&sc->sc_init_complete, 0, "iwxinit",
		    SEC_TO_NSEC(2));
		if (err) {
			splx(s);
			return err;
		}
	}
	splx(s);
	if (readnvm) {
		err = iwx_nvm_get(sc);
		if (err) {
			printf("%s: failed to read nvm\n", DEVNAME(sc));
			return err;
		}
		if (IEEE80211_ADDR_EQ(etheranyaddr, sc->sc_ic.ic_myaddr))
			IEEE80211_ADDR_COPY(sc->sc_ic.ic_myaddr,
			    sc->sc_nvm.hw_addr);

	}

	/*
	 * Only enable the MLD API on MA devices for now as the API 77
	 * firmware on some of the older firmware devices also claims
	 * support, but doesn't actually work.
	 */
	if (isset(sc->sc_enabled_capa, IWX_UCODE_TLV_CAPA_MLD_API_SUPPORT) &&
	    IWX_CSR_HW_REV_TYPE(sc->sc_hw_rev) == IWX_CFG_MAC_TYPE_MA)
		sc->sc_use_mld_api = 1;

	return 0;
}

int
iwx_config_ltr(struct iwx_softc *sc)
{
	struct iwx_ltr_config_cmd cmd = {
		.flags = htole32(IWX_LTR_CFG_FLAG_FEATURE_ENABLE),
	};

	if (!sc->sc_ltr_enabled)
		return 0;

	return iwx_send_cmd_pdu(sc, IWX_LTR_CONFIG, 0, sizeof(cmd), &cmd);
}

void
iwx_update_rx_desc(struct iwx_softc *sc, struct iwx_rx_ring *ring, int idx)
{
	struct iwx_rx_data *data = &ring->data[idx];

	if (sc->sc_device_family >= IWX_DEVICE_FAMILY_AX210) {
		struct iwx_rx_transfer_desc *desc = ring->desc;
		desc[idx].rbid = htole16(idx & 0xffff);
		desc[idx].addr = htole64(data->map->dm_segs[0].ds_addr);
		bus_dmamap_sync(sc->sc_dmat, ring->free_desc_dma.map,
		    idx * sizeof(*desc), sizeof(*desc),
		    BUS_DMASYNC_PREWRITE);
	} else {
		((uint64_t *)ring->desc)[idx] =
		    htole64(data->map->dm_segs[0].ds_addr | (idx & 0x0fff));
		bus_dmamap_sync(sc->sc_dmat, ring->free_desc_dma.map,
		    idx * sizeof(uint64_t), sizeof(uint64_t),
		    BUS_DMASYNC_PREWRITE);
	}
}

int
iwx_rx_addbuf(struct iwx_softc *sc, int size, int idx)
{
	struct iwx_rx_ring *ring = &sc->rxq;
	struct iwx_rx_data *data = &ring->data[idx];
	struct mbuf *m;
	int err;
	int fatal = 0;

	m = m_gethdr(M_DONTWAIT, MT_DATA);
	if (m == NULL)
		return ENOBUFS;

	if (size <= MCLBYTES) {
		MCLGET(m, M_DONTWAIT);
	} else {
		MCLGETL(m, M_DONTWAIT, IWX_RBUF_SIZE);
	}
	if ((m->m_flags & M_EXT) == 0) {
		m_freem(m);
		return ENOBUFS;
	}

	if (data->m != NULL) {
		bus_dmamap_unload(sc->sc_dmat, data->map);
		fatal = 1;
	}

	m->m_len = m->m_pkthdr.len = m->m_ext.ext_size;
	err = bus_dmamap_load_mbuf(sc->sc_dmat, data->map, m,
	    BUS_DMA_READ|BUS_DMA_NOWAIT);
	if (err) {
		/* XXX */
		if (fatal)
			panic("%s: could not load RX mbuf", DEVNAME(sc));
		m_freem(m);
		return err;
	}
	data->m = m;
	bus_dmamap_sync(sc->sc_dmat, data->map, 0, size, BUS_DMASYNC_PREREAD);

	/* Update RX descriptor. */
	iwx_update_rx_desc(sc, ring, idx);

	return 0;
}

int
iwx_rxmq_get_signal_strength(struct iwx_softc *sc,
    struct iwx_rx_mpdu_desc *desc)
{
	int energy_a, energy_b;

	if (sc->sc_device_family >= IWX_DEVICE_FAMILY_AX210) {
		energy_a = desc->v3.energy_a;
		energy_b = desc->v3.energy_b;
	} else {
		energy_a = desc->v1.energy_a;
		energy_b = desc->v1.energy_b;
	}
	energy_a = energy_a ? -energy_a : -256;
	energy_b = energy_b ? -energy_b : -256;
	return MAX(energy_a, energy_b);
}

void
iwx_rx_rx_phy_cmd(struct iwx_softc *sc, struct iwx_rx_packet *pkt,
    struct iwx_rx_data *data)
{
	struct iwx_rx_phy_info *phy_info = (void *)pkt->data;

	bus_dmamap_sync(sc->sc_dmat, data->map, sizeof(*pkt),
	    sizeof(*phy_info), BUS_DMASYNC_POSTREAD);

	memcpy(&sc->sc_last_phy_info, phy_info, sizeof(sc->sc_last_phy_info));
}

/*
 * Retrieve the average noise (in dBm) among receivers.
 */
int
iwx_get_noise(const struct iwx_statistics_rx_non_phy *stats)
{
	int i, total, nbant, noise;

	total = nbant = noise = 0;
	for (i = 0; i < 3; i++) {
		noise = letoh32(stats->beacon_silence_rssi[i]) & 0xff;
		if (noise) {
			total += noise;
			nbant++;
		}
	}

	/* There should be at least one antenna but check anyway. */
	return (nbant == 0) ? -127 : (total / nbant) - 107;
}

int
iwx_ccmp_decap(struct iwx_softc *sc, struct mbuf *m, struct ieee80211_node *ni,
    struct ieee80211_rxinfo *rxi)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ieee80211_key *k;
	struct ieee80211_frame *wh;
	uint64_t pn, *prsc;
	uint8_t *ivp;
	uint8_t tid;
	int hdrlen, hasqos;

	wh = mtod(m, struct ieee80211_frame *);
	hdrlen = ieee80211_get_hdrlen(wh);
	ivp = (uint8_t *)wh + hdrlen;

	/* find key for decryption */
	k = ieee80211_get_rxkey(ic, m, ni);
	if (k == NULL || k->k_cipher != IEEE80211_CIPHER_CCMP)
		return 1;

	/* Check that ExtIV bit is be set. */
	if (!(ivp[3] & IEEE80211_WEP_EXTIV))
		return 1;

	hasqos = ieee80211_has_qos(wh);
	tid = hasqos ? ieee80211_get_qos(wh) & IEEE80211_QOS_TID : 0;
	prsc = &k->k_rsc[tid];

	/* Extract the 48-bit PN from the CCMP header. */
	pn = (uint64_t)ivp[0]       |
	     (uint64_t)ivp[1] <<  8 |
	     (uint64_t)ivp[4] << 16 |
	     (uint64_t)ivp[5] << 24 |
	     (uint64_t)ivp[6] << 32 |
	     (uint64_t)ivp[7] << 40;
	if (rxi->rxi_flags & IEEE80211_RXI_HWDEC_SAME_PN) {
		if (pn < *prsc) {
			ic->ic_stats.is_ccmp_replays++;
			return 1;
		}
	} else if (pn <= *prsc) {
		ic->ic_stats.is_ccmp_replays++;
		return 1;
	}
	/* Last seen packet number is updated in ieee80211_inputm(). */

	/*
	 * Some firmware versions strip the MIC, and some don't. It is not
	 * clear which of the capability flags could tell us what to expect.
	 * For now, keep things simple and just leave the MIC in place if
	 * it is present.
	 *
	 * The IV will be stripped by ieee80211_inputm().
	 */
	return 0;
}

int
iwx_rx_hwdecrypt(struct iwx_softc *sc, struct mbuf *m, uint32_t rx_pkt_status,
    struct ieee80211_rxinfo *rxi)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ifnet *ifp = IC2IFP(ic);
	struct ieee80211_frame *wh;
	struct ieee80211_node *ni;
	int ret = 0;
	uint8_t type, subtype;

	wh = mtod(m, struct ieee80211_frame *);

	type = wh->i_fc[0] & IEEE80211_FC0_TYPE_MASK;
	if (type == IEEE80211_FC0_TYPE_CTL)
		return 0;

	subtype = wh->i_fc[0] & IEEE80211_FC0_SUBTYPE_MASK;
	if (ieee80211_has_qos(wh) && (subtype & IEEE80211_FC0_SUBTYPE_NODATA))
		return 0;

	ni = ieee80211_find_rxnode(ic, wh);
	/* Handle hardware decryption. */
	if (((wh->i_fc[0] & IEEE80211_FC0_TYPE_MASK) != IEEE80211_FC0_TYPE_CTL)
	    && (wh->i_fc[1] & IEEE80211_FC1_PROTECTED) &&
	    (ni->ni_flags & IEEE80211_NODE_RXPROT) &&
	    ((!IEEE80211_IS_MULTICAST(wh->i_addr1) &&
	    ni->ni_rsncipher == IEEE80211_CIPHER_CCMP) ||
	    (IEEE80211_IS_MULTICAST(wh->i_addr1) &&
	    ni->ni_rsngroupcipher == IEEE80211_CIPHER_CCMP))) {
		if ((rx_pkt_status & IWX_RX_MPDU_RES_STATUS_SEC_ENC_MSK) !=
		    IWX_RX_MPDU_RES_STATUS_SEC_CCM_ENC) {
			ic->ic_stats.is_ccmp_dec_errs++;
			ret = 1;
			goto out;
		}
		/* Check whether decryption was successful or not. */
		if ((rx_pkt_status &
		    (IWX_RX_MPDU_RES_STATUS_DEC_DONE |
		    IWX_RX_MPDU_RES_STATUS_MIC_OK)) !=
		    (IWX_RX_MPDU_RES_STATUS_DEC_DONE |
		    IWX_RX_MPDU_RES_STATUS_MIC_OK)) {
			ic->ic_stats.is_ccmp_dec_errs++;
			ret = 1;
			goto out;
		}
		rxi->rxi_flags |= IEEE80211_RXI_HWDEC;
	}
out:
	if (ret)
		ifp->if_ierrors++;
	ieee80211_release_node(ic, ni);
	return ret;
}

void
iwx_rx_frame(struct iwx_softc *sc, struct mbuf *m, int chanidx,
    uint32_t rx_pkt_status, int is_shortpre, int rate_n_flags,
    uint32_t device_timestamp, struct ieee80211_rxinfo *rxi,
    struct mbuf_list *ml)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ifnet *ifp = IC2IFP(ic);
	struct ieee80211_frame *wh;
	struct ieee80211_node *ni;

	if (chanidx < 0 || chanidx >= nitems(ic->ic_channels))
		chanidx = ieee80211_chan2ieee(ic, ic->ic_ibss_chan);

	wh = mtod(m, struct ieee80211_frame *);
	ni = ieee80211_find_rxnode(ic, wh);
	if ((rxi->rxi_flags & IEEE80211_RXI_HWDEC) &&
	    iwx_ccmp_decap(sc, m, ni, rxi) != 0) {
		ifp->if_ierrors++;
		m_freem(m);
		ieee80211_release_node(ic, ni);
		return;
	}

#if NBPFILTER > 0
	if (sc->sc_drvbpf != NULL) {
		struct iwx_rx_radiotap_header *tap = &sc->sc_rxtap;
		uint16_t chan_flags;
		int have_legacy_rate = 1;
		uint8_t mcs, rate;

		tap->wr_flags = 0;
		if (is_shortpre)
			tap->wr_flags |= IEEE80211_RADIOTAP_F_SHORTPRE;
		tap->wr_chan_freq =
		    htole16(ic->ic_channels[chanidx].ic_freq);
		chan_flags = ic->ic_channels[chanidx].ic_flags;
		if (ic->ic_curmode != IEEE80211_MODE_11N &&
		    ic->ic_curmode != IEEE80211_MODE_11AC) {
			chan_flags &= ~IEEE80211_CHAN_HT;
			chan_flags &= ~IEEE80211_CHAN_40MHZ;
		}
		if (ic->ic_curmode != IEEE80211_MODE_11AC)
			chan_flags &= ~IEEE80211_CHAN_VHT;
		tap->wr_chan_flags = htole16(chan_flags);
		tap->wr_dbm_antsignal = (int8_t)rxi->rxi_rssi;
		tap->wr_dbm_antnoise = (int8_t)sc->sc_noise;
		tap->wr_tsft = device_timestamp;
		if (sc->sc_rate_n_flags_version >= 2) {
			uint32_t mod_type = (rate_n_flags &
			    IWX_RATE_MCS_MOD_TYPE_MSK);
			const struct ieee80211_rateset *rs = NULL;
			uint32_t ridx;
			have_legacy_rate = (mod_type == IWX_RATE_MCS_CCK_MSK ||
			    mod_type == IWX_RATE_MCS_LEGACY_OFDM_MSK);
			mcs = (rate_n_flags & IWX_RATE_HT_MCS_CODE_MSK);
			ridx = (rate_n_flags & IWX_RATE_LEGACY_RATE_MSK);
			if (mod_type == IWX_RATE_MCS_CCK_MSK)
				rs = &ieee80211_std_rateset_11b;
			else if (mod_type == IWX_RATE_MCS_LEGACY_OFDM_MSK)
				rs = &ieee80211_std_rateset_11a;
			if (rs && ridx < rs->rs_nrates) {
				rate = (rs->rs_rates[ridx] &
				    IEEE80211_RATE_VAL);
			} else
				rate = 0;
		} else {
			have_legacy_rate = ((rate_n_flags &
			    (IWX_RATE_MCS_HT_MSK_V1 |
			    IWX_RATE_MCS_VHT_MSK_V1)) == 0);
			mcs = (rate_n_flags &
			    (IWX_RATE_HT_MCS_RATE_CODE_MSK_V1 |
			    IWX_RATE_HT_MCS_NSS_MSK_V1));
			rate = (rate_n_flags & IWX_RATE_LEGACY_RATE_MSK_V1);
		}
		if (!have_legacy_rate) {
			tap->wr_rate = (0x80 | mcs);
		} else {
			switch (rate) {
			/* CCK rates. */
			case  10: tap->wr_rate =   2; break;
			case  20: tap->wr_rate =   4; break;
			case  55: tap->wr_rate =  11; break;
			case 110: tap->wr_rate =  22; break;
			/* OFDM rates. */
			case 0xd: tap->wr_rate =  12; break;
			case 0xf: tap->wr_rate =  18; break;
			case 0x5: tap->wr_rate =  24; break;
			case 0x7: tap->wr_rate =  36; break;
			case 0x9: tap->wr_rate =  48; break;
			case 0xb: tap->wr_rate =  72; break;
			case 0x1: tap->wr_rate =  96; break;
			case 0x3: tap->wr_rate = 108; break;
			/* Unknown rate: should not happen. */
			default:  tap->wr_rate =   0;
			}
		}

		bpf_mtap_hdr(sc->sc_drvbpf, tap, sc->sc_rxtap_len,
		    m, BPF_DIRECTION_IN);
	}
#endif
	ieee80211_inputm(IC2IFP(ic), m, ni, rxi, ml);
	ieee80211_release_node(ic, ni);
}

/*
 * Drop duplicate 802.11 retransmissions
 * (IEEE 802.11-2012: 9.3.2.10 "Duplicate detection and recovery")
 * and handle pseudo-duplicate frames which result from deaggregation
 * of A-MSDU frames in hardware.
 */
int
iwx_detect_duplicate(struct iwx_softc *sc, struct mbuf *m,
    struct iwx_rx_mpdu_desc *desc, struct ieee80211_rxinfo *rxi)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct iwx_node *in = (void *)ic->ic_bss;
	struct iwx_rxq_dup_data *dup_data = &in->dup_data;
	uint8_t tid = IWX_MAX_TID_COUNT, subframe_idx;
	struct ieee80211_frame *wh = mtod(m, struct ieee80211_frame *);
	uint8_t type = wh->i_fc[0] & IEEE80211_FC0_TYPE_MASK;
	uint8_t subtype = wh->i_fc[0] & IEEE80211_FC0_SUBTYPE_MASK;
	int hasqos = ieee80211_has_qos(wh);
	uint16_t seq;

	if (type == IEEE80211_FC0_TYPE_CTL ||
	    (hasqos && (subtype & IEEE80211_FC0_SUBTYPE_NODATA)) ||
	    IEEE80211_IS_MULTICAST(wh->i_addr1))
		return 0;

	if (hasqos) {
		tid = (ieee80211_get_qos(wh) & IEEE80211_QOS_TID);
		if (tid > IWX_MAX_TID_COUNT)
			tid = IWX_MAX_TID_COUNT;
	}

	/* If this wasn't a part of an A-MSDU the sub-frame index will be 0 */
	subframe_idx = desc->amsdu_info &
		IWX_RX_MPDU_AMSDU_SUBFRAME_IDX_MASK;

	seq = letoh16(*(u_int16_t *)wh->i_seq) >> IEEE80211_SEQ_SEQ_SHIFT;
	if ((wh->i_fc[1] & IEEE80211_FC1_RETRY) &&
	    dup_data->last_seq[tid] == seq &&
	    dup_data->last_sub_frame[tid] >= subframe_idx)
		return 1;

	/*
	 * Allow the same frame sequence number for all A-MSDU subframes
	 * following the first subframe.
	 * Otherwise these subframes would be discarded as replays.
	 */
	if (dup_data->last_seq[tid] == seq &&
	    subframe_idx > dup_data->last_sub_frame[tid] &&
	    (desc->mac_flags2 & IWX_RX_MPDU_MFLG2_AMSDU)) {
		rxi->rxi_flags |= IEEE80211_RXI_SAME_SEQ;
	}

	dup_data->last_seq[tid] = seq;
	dup_data->last_sub_frame[tid] = subframe_idx;

	return 0;
}

/*
 * Returns true if sn2 - buffer_size < sn1 < sn2.
 * To be used only in order to compare reorder buffer head with NSSN.
 * We fully trust NSSN unless it is behind us due to reorder timeout.
 * Reorder timeout can only bring us up to buffer_size SNs ahead of NSSN.
 */
int
iwx_is_sn_less(uint16_t sn1, uint16_t sn2, uint16_t buffer_size)
{
	return SEQ_LT(sn1, sn2) && !SEQ_LT(sn1, sn2 - buffer_size);
}

void
iwx_release_frames(struct iwx_softc *sc, struct ieee80211_node *ni,
    struct iwx_rxba_data *rxba, struct iwx_reorder_buffer *reorder_buf,
    uint16_t nssn, struct mbuf_list *ml)
{
	struct iwx_reorder_buf_entry *entries = &rxba->entries[0];
	uint16_t ssn = reorder_buf->head_sn;

	/* ignore nssn smaller than head sn - this can happen due to timeout */
	if (iwx_is_sn_less(nssn, ssn, reorder_buf->buf_size))
		goto set_timer;

	while (iwx_is_sn_less(ssn, nssn, reorder_buf->buf_size)) {
		int index = ssn % reorder_buf->buf_size;
		struct mbuf *m;
		int chanidx, is_shortpre;
		uint32_t rx_pkt_status, rate_n_flags, device_timestamp;
		struct ieee80211_rxinfo *rxi;

		/* This data is the same for all A-MSDU subframes. */
		chanidx = entries[index].chanidx;
		rx_pkt_status = entries[index].rx_pkt_status;
		is_shortpre = entries[index].is_shortpre;
		rate_n_flags = entries[index].rate_n_flags;
		device_timestamp = entries[index].device_timestamp;
		rxi = &entries[index].rxi;

		/*
		 * Empty the list. Will have more than one frame for A-MSDU.
		 * Empty list is valid as well since nssn indicates frames were
		 * received.
		 */
		while ((m = ml_dequeue(&entries[index].frames)) != NULL) {
			iwx_rx_frame(sc, m, chanidx, rx_pkt_status, is_shortpre,
			    rate_n_flags, device_timestamp, rxi, ml);
			reorder_buf->num_stored--;

			/*
			 * Allow the same frame sequence number and CCMP PN for
			 * all A-MSDU subframes following the first subframe.
			 * Otherwise they would be discarded as replays.
			 */
			rxi->rxi_flags |= IEEE80211_RXI_SAME_SEQ;
			rxi->rxi_flags |= IEEE80211_RXI_HWDEC_SAME_PN;
		}

		ssn = (ssn + 1) & 0xfff;
	}
	reorder_buf->head_sn = nssn;

set_timer:
	if (reorder_buf->num_stored && !reorder_buf->removed) {
		timeout_add_usec(&reorder_buf->reorder_timer,
		    RX_REORDER_BUF_TIMEOUT_MQ_USEC);
	} else
		timeout_del(&reorder_buf->reorder_timer);
}

int
iwx_oldsn_workaround(struct iwx_softc *sc, struct ieee80211_node *ni, int tid,
    struct iwx_reorder_buffer *buffer, uint32_t reorder_data, uint32_t gp2)
{
	struct ieee80211com *ic = &sc->sc_ic;

	if (gp2 != buffer->consec_oldsn_ampdu_gp2) {
		/* we have a new (A-)MPDU ... */

		/*
		 * reset counter to 0 if we didn't have any oldsn in
		 * the last A-MPDU (as detected by GP2 being identical)
		 */
		if (!buffer->consec_oldsn_prev_drop)
			buffer->consec_oldsn_drops = 0;

		/* either way, update our tracking state */
		buffer->consec_oldsn_ampdu_gp2 = gp2;
	} else if (buffer->consec_oldsn_prev_drop) {
		/*
		 * tracking state didn't change, and we had an old SN
		 * indication before - do nothing in this case, we
		 * already noted this one down and are waiting for the
		 * next A-MPDU (by GP2)
		 */
		return 0;
	}

	/* return unless this MPDU has old SN */
	if (!(reorder_data & IWX_RX_MPDU_REORDER_BA_OLD_SN))
		return 0;

	/* update state */
	buffer->consec_oldsn_prev_drop = 1;
	buffer->consec_oldsn_drops++;

	/* if limit is reached, send del BA and reset state */
	if (buffer->consec_oldsn_drops == IWX_AMPDU_CONSEC_DROPS_DELBA) {
		ieee80211_delba_request(ic, ni, IEEE80211_REASON_UNSPECIFIED,
		    0, tid);
		buffer->consec_oldsn_prev_drop = 0;
		buffer->consec_oldsn_drops = 0;
		return 1;
	}

	return 0;
}

/*
 * Handle re-ordering of frames which were de-aggregated in hardware.
 * Returns 1 if the MPDU was consumed (buffered or dropped).
 * Returns 0 if the MPDU should be passed to upper layer.
 */
int
iwx_rx_reorder(struct iwx_softc *sc, struct mbuf *m, int chanidx,
    struct iwx_rx_mpdu_desc *desc, int is_shortpre, int rate_n_flags,
    uint32_t device_timestamp, struct ieee80211_rxinfo *rxi,
    struct mbuf_list *ml)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ieee80211_frame *wh;
	struct ieee80211_node *ni;
	struct iwx_rxba_data *rxba;
	struct iwx_reorder_buffer *buffer;
	uint32_t reorder_data = le32toh(desc->reorder_data);
	int is_amsdu = (desc->mac_flags2 & IWX_RX_MPDU_MFLG2_AMSDU);
	int last_subframe =
		(desc->amsdu_info & IWX_RX_MPDU_AMSDU_LAST_SUBFRAME);
	uint8_t tid;
	uint8_t subframe_idx = (desc->amsdu_info &
	    IWX_RX_MPDU_AMSDU_SUBFRAME_IDX_MASK);
	struct iwx_reorder_buf_entry *entries;
	int index;
	uint16_t nssn, sn;
	uint8_t baid, type, subtype;
	int hasqos;

	wh = mtod(m, struct ieee80211_frame *);
	hasqos = ieee80211_has_qos(wh);
	tid = hasqos ? ieee80211_get_qos(wh) & IEEE80211_QOS_TID : 0;

	type = wh->i_fc[0] & IEEE80211_FC0_TYPE_MASK;
	subtype = wh->i_fc[0] & IEEE80211_FC0_SUBTYPE_MASK;

	/*
	 * We are only interested in Block Ack requests and unicast QoS data.
	 */
	if (IEEE80211_IS_MULTICAST(wh->i_addr1))
		return 0;
	if (hasqos) {
		if (subtype & IEEE80211_FC0_SUBTYPE_NODATA)
			return 0;
	} else {
		if (type != IEEE80211_FC0_TYPE_CTL ||
		    subtype != IEEE80211_FC0_SUBTYPE_BAR)
			return 0;
	}

	baid = (reorder_data & IWX_RX_MPDU_REORDER_BAID_MASK) >>
		IWX_RX_MPDU_REORDER_BAID_SHIFT;
	if (baid == IWX_RX_REORDER_DATA_INVALID_BAID ||
	    baid >= nitems(sc->sc_rxba_data))
		return 0;

	rxba = &sc->sc_rxba_data[baid];
	if (rxba->baid == IWX_RX_REORDER_DATA_INVALID_BAID ||
	    tid != rxba->tid || rxba->sta_id != IWX_STATION_ID)
		return 0;

	if (rxba->timeout != 0)
		getmicrouptime(&rxba->last_rx);

	/* Bypass A-MPDU re-ordering in net80211. */
	rxi->rxi_flags |= IEEE80211_RXI_AMPDU_DONE;

	nssn = reorder_data & IWX_RX_MPDU_REORDER_NSSN_MASK;
	sn = (reorder_data & IWX_RX_MPDU_REORDER_SN_MASK) >>
		IWX_RX_MPDU_REORDER_SN_SHIFT;

	buffer = &rxba->reorder_buf;
	entries = &rxba->entries[0];

	if (!buffer->valid) {
		if (reorder_data & IWX_RX_MPDU_REORDER_BA_OLD_SN)
			return 0;
		buffer->valid = 1;
	}

	ni = ieee80211_find_rxnode(ic, wh);
	if (type == IEEE80211_FC0_TYPE_CTL &&
	    subtype == IEEE80211_FC0_SUBTYPE_BAR) {
		iwx_release_frames(sc, ni, rxba, buffer, nssn, ml);
		goto drop;
	}

	/*
	 * If there was a significant jump in the nssn - adjust.
	 * If the SN is smaller than the NSSN it might need to first go into
	 * the reorder buffer, in which case we just release up to it and the
	 * rest of the function will take care of storing it and releasing up to
	 * the nssn.
	 */
	if (!iwx_is_sn_less(nssn, buffer->head_sn + buffer->buf_size,
	    buffer->buf_size) ||
	    !SEQ_LT(sn, buffer->head_sn + buffer->buf_size)) {
		uint16_t min_sn = SEQ_LT(sn, nssn) ? sn : nssn;
		ic->ic_stats.is_ht_rx_frame_above_ba_winend++;
		iwx_release_frames(sc, ni, rxba, buffer, min_sn, ml);
	}

	if (iwx_oldsn_workaround(sc, ni, tid, buffer, reorder_data,
	    device_timestamp)) {
		 /* BA session will be torn down. */
		ic->ic_stats.is_ht_rx_ba_window_jump++;
		goto drop;

	}

	/* drop any outdated packets */
	if (SEQ_LT(sn, buffer->head_sn)) {
		ic->ic_stats.is_ht_rx_frame_below_ba_winstart++;
		goto drop;
	}

	/* release immediately if allowed by nssn and no stored frames */
	if (!buffer->num_stored && SEQ_LT(sn, nssn)) {
		if (iwx_is_sn_less(buffer->head_sn, nssn, buffer->buf_size) &&
		   (!is_amsdu || last_subframe))
			buffer->head_sn = nssn;
		ieee80211_release_node(ic, ni);
		return 0;
	}

	/*
	 * release immediately if there are no stored frames, and the sn is
	 * equal to the head.
	 * This can happen due to reorder timer, where NSSN is behind head_sn.
	 * When we released everything, and we got the next frame in the
	 * sequence, according to the NSSN we can't release immediately,
	 * while technically there is no hole and we can move forward.
	 */
	if (!buffer->num_stored && sn == buffer->head_sn) {
		if (!is_amsdu || last_subframe)
			buffer->head_sn = (buffer->head_sn + 1) & 0xfff;
		ieee80211_release_node(ic, ni);
		return 0;
	}

	index = sn % buffer->buf_size;

	/*
	 * Check if we already stored this frame
	 * As AMSDU is either received or not as whole, logic is simple:
	 * If we have frames in that position in the buffer and the last frame
	 * originated from AMSDU had a different SN then it is a retransmission.
	 * If it is the same SN then if the subframe index is incrementing it
	 * is the same AMSDU - otherwise it is a retransmission.
	 */
	if (!ml_empty(&entries[index].frames)) {
		if (!is_amsdu) {
			ic->ic_stats.is_ht_rx_ba_no_buf++;
			goto drop;
		} else if (sn != buffer->last_amsdu ||
		    buffer->last_sub_index >= subframe_idx) {
			ic->ic_stats.is_ht_rx_ba_no_buf++;
			goto drop;
		}
	} else {
		/* This data is the same for all A-MSDU subframes. */
		entries[index].chanidx = chanidx;
		entries[index].is_shortpre = is_shortpre;
		entries[index].rate_n_flags = rate_n_flags;
		entries[index].device_timestamp = device_timestamp;
		memcpy(&entries[index].rxi, rxi, sizeof(entries[index].rxi));
	}

	/* put in reorder buffer */
	ml_enqueue(&entries[index].frames, m);
	buffer->num_stored++;
	getmicrouptime(&entries[index].reorder_time);

	if (is_amsdu) {
		buffer->last_amsdu = sn;
		buffer->last_sub_index = subframe_idx;
	}

	/*
	 * We cannot trust NSSN for AMSDU sub-frames that are not the last.
	 * The reason is that NSSN advances on the first sub-frame, and may
	 * cause the reorder buffer to advance before all the sub-frames arrive.
	 * Example: reorder buffer contains SN 0 & 2, and we receive AMSDU with
	 * SN 1. NSSN for first sub frame will be 3 with the result of driver
	 * releasing SN 0,1, 2. When sub-frame 1 arrives - reorder buffer is
	 * already ahead and it will be dropped.
	 * If the last sub-frame is not on this queue - we will get frame
	 * release notification with up to date NSSN.
	 */
	if (!is_amsdu || last_subframe)
		iwx_release_frames(sc, ni, rxba, buffer, nssn, ml);

	ieee80211_release_node(ic, ni);
	return 1;

drop:
	m_freem(m);
	ieee80211_release_node(ic, ni);
	return 1;
}

void
iwx_rx_mpdu_mq(struct iwx_softc *sc, struct mbuf *m, void *pktdata,
    size_t maxlen, struct mbuf_list *ml)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ieee80211_rxinfo rxi;
	struct iwx_rx_mpdu_desc *desc;
	uint32_t len, hdrlen, rate_n_flags, device_timestamp;
	int rssi;
	uint8_t chanidx;
	uint16_t phy_info;
	size_t desc_size;

	if (sc->sc_device_family >= IWX_DEVICE_FAMILY_AX210)
		desc_size = sizeof(*desc);
	else
		desc_size = IWX_RX_DESC_SIZE_V1;

	if (maxlen < desc_size) {
		m_freem(m);
		return; /* drop */
	}

	desc = (struct iwx_rx_mpdu_desc *)pktdata;

	if (!(desc->status & htole16(IWX_RX_MPDU_RES_STATUS_CRC_OK)) ||
	    !(desc->status & htole16(IWX_RX_MPDU_RES_STATUS_OVERRUN_OK))) {
		m_freem(m);
		return; /* drop */
	}

	len = le16toh(desc->mpdu_len);
	if (ic->ic_opmode == IEEE80211_M_MONITOR) {
		/* Allow control frames in monitor mode. */
		if (len < sizeof(struct ieee80211_frame_cts)) {
			ic->ic_stats.is_rx_tooshort++;
			IC2IFP(ic)->if_ierrors++;
			m_freem(m);
			return;
		}
	} else if (len < sizeof(struct ieee80211_frame)) {
		ic->ic_stats.is_rx_tooshort++;
		IC2IFP(ic)->if_ierrors++;
		m_freem(m);
		return;
	}
	if (len > maxlen - desc_size) {
		IC2IFP(ic)->if_ierrors++;
		m_freem(m);
		return;
	}

	m->m_data = pktdata + desc_size;
	m->m_pkthdr.len = m->m_len = len;

	/* Account for padding following the frame header. */
	if (desc->mac_flags2 & IWX_RX_MPDU_MFLG2_PAD) {
		struct ieee80211_frame *wh = mtod(m, struct ieee80211_frame *);
		int type = wh->i_fc[0] & IEEE80211_FC0_TYPE_MASK;
		if (type == IEEE80211_FC0_TYPE_CTL) {
			switch (wh->i_fc[0] & IEEE80211_FC0_SUBTYPE_MASK) {
			case IEEE80211_FC0_SUBTYPE_CTS:
				hdrlen = sizeof(struct ieee80211_frame_cts);
				break;
			case IEEE80211_FC0_SUBTYPE_ACK:
				hdrlen = sizeof(struct ieee80211_frame_ack);
				break;
			default:
				hdrlen = sizeof(struct ieee80211_frame_min);
				break;
			}
		} else
			hdrlen = ieee80211_get_hdrlen(wh);

		if ((le16toh(desc->status) &
		    IWX_RX_MPDU_RES_STATUS_SEC_ENC_MSK) ==
		    IWX_RX_MPDU_RES_STATUS_SEC_CCM_ENC) {
			/* Padding is inserted after the IV. */
			hdrlen += IEEE80211_CCMP_HDRLEN;
		}

		memmove(m->m_data + 2, m->m_data, hdrlen);
		m_adj(m, 2);
	}

	memset(&rxi, 0, sizeof(rxi));

	/*
	 * Hardware de-aggregates A-MSDUs and copies the same MAC header
	 * in place for each subframe. But it leaves the 'A-MSDU present'
	 * bit set in the frame header. We need to clear this bit ourselves.
	 * (XXX This workaround is not required on AX200/AX201 devices that
	 * have been tested by me, but it's unclear when this problem was
	 * fixed in the hardware. It definitely affects the 9k generation.
	 * Leaving this in place for now since some 9k/AX200 hybrids seem
	 * to exist that we may eventually add support for.)
	 *
	 * And we must allow the same CCMP PN for subframes following the
	 * first subframe. Otherwise they would be discarded as replays.
	 */
	if (desc->mac_flags2 & IWX_RX_MPDU_MFLG2_AMSDU) {
		struct ieee80211_frame *wh = mtod(m, struct ieee80211_frame *);
		uint8_t subframe_idx = (desc->amsdu_info &
		    IWX_RX_MPDU_AMSDU_SUBFRAME_IDX_MASK);
		if (subframe_idx > 0)
			rxi.rxi_flags |= IEEE80211_RXI_HWDEC_SAME_PN;
		if (ieee80211_has_qos(wh) && ieee80211_has_addr4(wh) &&
		    m->m_len >= sizeof(struct ieee80211_qosframe_addr4)) {
			struct ieee80211_qosframe_addr4 *qwh4 = mtod(m,
			    struct ieee80211_qosframe_addr4 *);
			qwh4->i_qos[0] &= htole16(~IEEE80211_QOS_AMSDU);
		} else if (ieee80211_has_qos(wh) &&
		    m->m_len >= sizeof(struct ieee80211_qosframe)) {
			struct ieee80211_qosframe *qwh = mtod(m,
			    struct ieee80211_qosframe *);
			qwh->i_qos[0] &= htole16(~IEEE80211_QOS_AMSDU);
		}
	}

	/*
	 * Verify decryption before duplicate detection. The latter uses
	 * the TID supplied in QoS frame headers and this TID is implicitly
	 * verified as part of the CCMP nonce.
	 */
	if (iwx_rx_hwdecrypt(sc, m, le16toh(desc->status), &rxi)) {
		m_freem(m);
		return;
	}

	if (iwx_detect_duplicate(sc, m, desc, &rxi)) {
		m_freem(m);
		return;
	}

	if (sc->sc_device_family >= IWX_DEVICE_FAMILY_AX210) {
		rate_n_flags = le32toh(desc->v3.rate_n_flags);
		chanidx = desc->v3.channel;
		device_timestamp = le32toh(desc->v3.gp2_on_air_rise);
	} else {
		rate_n_flags = le32toh(desc->v1.rate_n_flags);
		chanidx = desc->v1.channel;
		device_timestamp = le32toh(desc->v1.gp2_on_air_rise);
	}

	phy_info = le16toh(desc->phy_info);

	rssi = iwx_rxmq_get_signal_strength(sc, desc);
	rssi = (0 - IWX_MIN_DBM) + rssi;	/* normalize */
	rssi = MIN(rssi, ic->ic_max_rssi);	/* clip to max. 100% */

	rxi.rxi_rssi = rssi;
	rxi.rxi_tstamp = device_timestamp;
	rxi.rxi_chan = chanidx;

	if (iwx_rx_reorder(sc, m, chanidx, desc,
	    (phy_info & IWX_RX_MPDU_PHY_SHORT_PREAMBLE),
	    rate_n_flags, device_timestamp, &rxi, ml))
		return;

	iwx_rx_frame(sc, m, chanidx, le16toh(desc->status),
	    (phy_info & IWX_RX_MPDU_PHY_SHORT_PREAMBLE),
	    rate_n_flags, device_timestamp, &rxi, ml);
}

void
iwx_clear_tx_desc(struct iwx_softc *sc, struct iwx_tx_ring *ring, int idx)
{
	struct iwx_tfh_tfd *desc = &ring->desc[idx];
	uint8_t num_tbs = le16toh(desc->num_tbs) & 0x1f;
	int i;

	/* First TB is never cleared - it is bidirectional DMA data. */
	for (i = 1; i < num_tbs; i++) {
		struct iwx_tfh_tb *tb = &desc->tbs[i];
		memset(tb, 0, sizeof(*tb));
	}
	desc->num_tbs = htole16(1);

	bus_dmamap_sync(sc->sc_dmat, ring->desc_dma.map,
	    (char *)(void *)desc - (char *)(void *)ring->desc_dma.vaddr,
	    sizeof(*desc), BUS_DMASYNC_PREWRITE);
}

void
iwx_txd_done(struct iwx_softc *sc, struct iwx_tx_data *txd)
{
	struct ieee80211com *ic = &sc->sc_ic;

	bus_dmamap_sync(sc->sc_dmat, txd->map, 0, txd->map->dm_mapsize,
	    BUS_DMASYNC_POSTWRITE);
	bus_dmamap_unload(sc->sc_dmat, txd->map);
	m_freem(txd->m);
	txd->m = NULL;

	KASSERT(txd->in);
	ieee80211_release_node(ic, &txd->in->in_ni);
	txd->in = NULL;
}

void
iwx_txq_advance(struct iwx_softc *sc, struct iwx_tx_ring *ring, uint16_t idx)
{
 	struct iwx_tx_data *txd;

	while (ring->tail_hw != idx) {
		txd = &ring->data[ring->tail];
		if (txd->m != NULL) {
			iwx_clear_tx_desc(sc, ring, ring->tail);
			iwx_tx_update_byte_tbl(sc, ring, ring->tail, 0, 0);
			iwx_txd_done(sc, txd);
			ring->queued--;
		}
		ring->tail = (ring->tail + 1) % IWX_TX_RING_COUNT;
		ring->tail_hw = (ring->tail_hw + 1) % sc->max_tfd_queue_size;
	}
}

void
iwx_rx_tx_cmd(struct iwx_softc *sc, struct iwx_rx_packet *pkt,
    struct iwx_rx_data *data)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ifnet *ifp = IC2IFP(ic);
	struct iwx_cmd_header *cmd_hdr = &pkt->hdr;
	int qid = cmd_hdr->qid, status, txfail;
	struct iwx_tx_ring *ring = &sc->txq[qid];
	struct iwx_tx_resp *tx_resp = (void *)pkt->data;
	uint32_t ssn;
	uint32_t len = iwx_rx_packet_len(pkt);

	bus_dmamap_sync(sc->sc_dmat, data->map, 0, IWX_RBUF_SIZE,
	    BUS_DMASYNC_POSTREAD);

	/* Sanity checks. */
	if (sizeof(*tx_resp) > len)
		return;
	if (qid < IWX_FIRST_AGG_TX_QUEUE && tx_resp->frame_count > 1)
		return;
	if (qid >= IWX_FIRST_AGG_TX_QUEUE && sizeof(*tx_resp) + sizeof(ssn) +
	    tx_resp->frame_count * sizeof(tx_resp->status) > len)
		return;

	sc->sc_tx_timer[qid] = 0;

	if (tx_resp->frame_count > 1) /* A-MPDU */
		return;

	status = le16toh(tx_resp->status.status) & IWX_TX_STATUS_MSK;
	txfail = (status != IWX_TX_STATUS_SUCCESS &&
	    status != IWX_TX_STATUS_DIRECT_DONE);

	if (txfail)
		ifp->if_oerrors++;

	/*
	 * On hardware supported by iwx(4) the SSN counter corresponds
	 * to a Tx ring index rather than a sequence number.
	 * Frames up to this index (non-inclusive) can now be freed.
	 */
	memcpy(&ssn, &tx_resp->status + tx_resp->frame_count, sizeof(ssn));
	ssn = le32toh(ssn);
	if (ssn < sc->max_tfd_queue_size) {
		iwx_txq_advance(sc, ring, ssn);
		iwx_clear_oactive(sc, ring);
	}
}

void
iwx_clear_oactive(struct iwx_softc *sc, struct iwx_tx_ring *ring)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ifnet *ifp = IC2IFP(ic);

	if (ring->queued < IWX_TX_RING_LOMARK) {
		sc->qfullmsk &= ~(1 << ring->qid);
		if (sc->qfullmsk == 0 && ifq_is_oactive(&ifp->if_snd)) {
			ifq_clr_oactive(&ifp->if_snd);
			/*
			 * Well, we're in interrupt context, but then again
			 * I guess net80211 does all sorts of stunts in
			 * interrupt context, so maybe this is no biggie.
			 */
			(*ifp->if_start)(ifp);
		}
	}
}

void
iwx_rx_compressed_ba(struct iwx_softc *sc, struct iwx_rx_packet *pkt)
{
	struct iwx_compressed_ba_notif *ba_res = (void *)pkt->data;
	struct ieee80211com *ic = &sc->sc_ic;
	struct ieee80211_node *ni;
	struct ieee80211_tx_ba *ba;
	struct iwx_node *in;
	struct iwx_tx_ring *ring;
	uint16_t i, tfd_cnt, ra_tid_cnt, idx;
	int qid;

	if (ic->ic_state != IEEE80211_S_RUN)
		return;

	if (iwx_rx_packet_payload_len(pkt) < sizeof(*ba_res))
		return;

	if (ba_res->sta_id != IWX_STATION_ID)
		return;

	ni = ic->ic_bss;
	in = (void *)ni;

	tfd_cnt = le16toh(ba_res->tfd_cnt);
	ra_tid_cnt = le16toh(ba_res->ra_tid_cnt);
	if (!tfd_cnt || iwx_rx_packet_payload_len(pkt) < (sizeof(*ba_res) +
	    sizeof(ba_res->ra_tid[0]) * ra_tid_cnt +
	    sizeof(ba_res->tfd[0]) * tfd_cnt))
		return;

	for (i = 0; i < tfd_cnt; i++) {
		struct iwx_compressed_ba_tfd *ba_tfd = &ba_res->tfd[i];
		uint8_t tid;

		tid = ba_tfd->tid;
		if (tid >= nitems(sc->aggqid))
			continue;

		qid = sc->aggqid[tid];
		if (qid != htole16(ba_tfd->q_num))
			continue;

		ring = &sc->txq[qid];

		ba = &ni->ni_tx_ba[tid];
		if (ba->ba_state != IEEE80211_BA_AGREED)
			continue;

		idx = le16toh(ba_tfd->tfd_index);
		sc->sc_tx_timer[qid] = 0;
		iwx_txq_advance(sc, ring, idx);
		iwx_clear_oactive(sc, ring);
	}
}

void
iwx_rx_bmiss(struct iwx_softc *sc, struct iwx_rx_packet *pkt,
    struct iwx_rx_data *data)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct iwx_missed_beacons_notif *mbn = (void *)pkt->data;
	uint32_t missed;

	if ((ic->ic_opmode != IEEE80211_M_STA) ||
	    (ic->ic_state != IEEE80211_S_RUN))
		return;

	bus_dmamap_sync(sc->sc_dmat, data->map, sizeof(*pkt),
	    sizeof(*mbn), BUS_DMASYNC_POSTREAD);

	missed = le32toh(mbn->consec_missed_beacons_since_last_rx);
	if (missed > ic->ic_bmissthres && ic->ic_mgt_timer == 0) {
		if (ic->ic_if.if_flags & IFF_DEBUG)
			printf("%s: receiving no beacons from %s; checking if "
			    "this AP is still responding to probe requests\n",
			    DEVNAME(sc), ether_sprintf(ic->ic_bss->ni_macaddr));
		/*
		 * Rather than go directly to scan state, try to send a
		 * directed probe request first. If that fails then the
		 * state machine will drop us into scanning after timing
		 * out waiting for a probe response.
		 */
		IEEE80211_SEND_MGMT(ic, ic->ic_bss,
		    IEEE80211_FC0_SUBTYPE_PROBE_REQ, 0);
	}

}

int
iwx_binding_cmd(struct iwx_softc *sc, struct iwx_node *in, uint32_t action)
{
	struct iwx_binding_cmd cmd;
	struct iwx_phy_ctxt *phyctxt = in->in_phyctxt;
	uint32_t mac_id = IWX_FW_CMD_ID_AND_COLOR(in->in_id, in->in_color);
	int i, err, active = (sc->sc_flags & IWX_FLAG_BINDING_ACTIVE);
	uint32_t status;

	/* No need to bind with MLD firmware. */
	if (sc->sc_use_mld_api)
		return 0;
	
	if (action == IWX_FW_CTXT_ACTION_ADD && active)
		panic("binding already added");
	if (action == IWX_FW_CTXT_ACTION_REMOVE && !active)
		panic("binding already removed");

	if (phyctxt == NULL) /* XXX race with iwx_stop() */
		return EINVAL;

	memset(&cmd, 0, sizeof(cmd));

	cmd.id_and_color
	    = htole32(IWX_FW_CMD_ID_AND_COLOR(phyctxt->id, phyctxt->color));
	cmd.action = htole32(action);
	cmd.phy = htole32(IWX_FW_CMD_ID_AND_COLOR(phyctxt->id, phyctxt->color));

	cmd.macs[0] = htole32(mac_id);
	for (i = 1; i < IWX_MAX_MACS_IN_BINDING; i++)
		cmd.macs[i] = htole32(IWX_FW_CTXT_INVALID);

	if (IEEE80211_IS_CHAN_2GHZ(phyctxt->channel) ||
	    !isset(sc->sc_enabled_capa, IWX_UCODE_TLV_CAPA_CDB_SUPPORT))
		cmd.lmac_id = htole32(IWX_LMAC_24G_INDEX);
	else
		cmd.lmac_id = htole32(IWX_LMAC_5G_INDEX);

	status = 0;
	err = iwx_send_cmd_pdu_status(sc, IWX_BINDING_CONTEXT_CMD, sizeof(cmd),
	    &cmd, &status);
	if (err == 0 && status != 0)
		err = EIO;

	return err;
}

uint8_t
iwx_get_vht_ctrl_pos(struct ieee80211com *ic, struct ieee80211_channel *chan)
{
	int center_idx = ic->ic_bss->ni_vht_chan_center_freq_idx0;
	int primary_idx = ic->ic_bss->ni_primary_chan;
	/*
	 * The FW is expected to check the control channel position only
	 * when in HT/VHT and the channel width is not 20MHz. Return
	 * this value as the default one:
	 */
	uint8_t pos = IWX_PHY_VHT_CTRL_POS_1_BELOW;

	switch (primary_idx - center_idx) {
	case -6:
		pos = IWX_PHY_VHT_CTRL_POS_2_BELOW;
		break;
	case -2:
		pos = IWX_PHY_VHT_CTRL_POS_1_BELOW;
		break;
	case 2:
		pos = IWX_PHY_VHT_CTRL_POS_1_ABOVE;
		break;
	case 6:
		pos = IWX_PHY_VHT_CTRL_POS_2_ABOVE;
		break;
	default:
		break;
	}

	return pos;
}

int
iwx_phy_ctxt_cmd_uhb_v3_v4(struct iwx_softc *sc, struct iwx_phy_ctxt *ctxt,
    uint8_t chains_static, uint8_t chains_dynamic, uint32_t action, uint8_t sco,
    uint8_t vht_chan_width, int cmdver)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct iwx_phy_context_cmd_uhb cmd;
	uint8_t active_cnt, idle_cnt;
	struct ieee80211_channel *chan = ctxt->channel;

	memset(&cmd, 0, sizeof(cmd));
	cmd.id_and_color = htole32(IWX_FW_CMD_ID_AND_COLOR(ctxt->id,
	    ctxt->color));
	cmd.action = htole32(action);

	if (IEEE80211_IS_CHAN_2GHZ(ctxt->channel) ||
	    !isset(sc->sc_enabled_capa, IWX_UCODE_TLV_CAPA_CDB_SUPPORT))
		cmd.lmac_id = htole32(IWX_LMAC_24G_INDEX);
	else
		cmd.lmac_id = htole32(IWX_LMAC_5G_INDEX);

	cmd.ci.band = IEEE80211_IS_CHAN_2GHZ(chan) ?
	    IWX_PHY_BAND_24 : IWX_PHY_BAND_5;
	cmd.ci.channel = htole32(ieee80211_chan2ieee(ic, chan));
	if (vht_chan_width == IEEE80211_VHTOP0_CHAN_WIDTH_80) {
		cmd.ci.ctrl_pos = iwx_get_vht_ctrl_pos(ic, chan);
		cmd.ci.width = IWX_PHY_VHT_CHANNEL_MODE80;
	} else if (chan->ic_flags & IEEE80211_CHAN_40MHZ) {
		if (sco == IEEE80211_HTOP0_SCO_SCA) {
			/* secondary chan above -> control chan below */
			cmd.ci.ctrl_pos = IWX_PHY_VHT_CTRL_POS_1_BELOW;
			cmd.ci.width = IWX_PHY_VHT_CHANNEL_MODE40;
		} else if (sco == IEEE80211_HTOP0_SCO_SCB) {
			/* secondary chan below -> control chan above */
			cmd.ci.ctrl_pos = IWX_PHY_VHT_CTRL_POS_1_ABOVE;
			cmd.ci.width = IWX_PHY_VHT_CHANNEL_MODE40;
		} else {
			cmd.ci.width = IWX_PHY_VHT_CHANNEL_MODE20;
			cmd.ci.ctrl_pos = IWX_PHY_VHT_CTRL_POS_1_BELOW;
		}
	} else {
		cmd.ci.width = IWX_PHY_VHT_CHANNEL_MODE20;
		cmd.ci.ctrl_pos = IWX_PHY_VHT_CTRL_POS_1_BELOW;
	}

	if (cmdver < 4 && iwx_lookup_cmd_ver(sc, IWX_DATA_PATH_GROUP,
	    IWX_RLC_CONFIG_CMD) != 2) {
		idle_cnt = chains_static;
		active_cnt = chains_dynamic;
		cmd.rxchain_info = htole32(iwx_fw_valid_rx_ant(sc) <<
		    IWX_PHY_RX_CHAIN_VALID_POS);
		cmd.rxchain_info |= htole32(idle_cnt <<
		    IWX_PHY_RX_CHAIN_CNT_POS);
		cmd.rxchain_info |= htole32(active_cnt <<
		    IWX_PHY_RX_CHAIN_MIMO_CNT_POS);
	}

	return iwx_send_cmd_pdu(sc, IWX_PHY_CONTEXT_CMD, 0, sizeof(cmd), &cmd);
}

int
iwx_phy_ctxt_cmd_v3_v4(struct iwx_softc *sc, struct iwx_phy_ctxt *ctxt,
    uint8_t chains_static, uint8_t chains_dynamic, uint32_t action, uint8_t sco,
    uint8_t vht_chan_width, int cmdver)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct iwx_phy_context_cmd cmd;
	uint8_t active_cnt, idle_cnt;
	struct ieee80211_channel *chan = ctxt->channel;

	memset(&cmd, 0, sizeof(cmd));
	cmd.id_and_color = htole32(IWX_FW_CMD_ID_AND_COLOR(ctxt->id,
	    ctxt->color));
	cmd.action = htole32(action);

	if (IEEE80211_IS_CHAN_2GHZ(ctxt->channel) ||
	    !isset(sc->sc_enabled_capa, IWX_UCODE_TLV_CAPA_CDB_SUPPORT))
		cmd.lmac_id = htole32(IWX_LMAC_24G_INDEX);
	else
		cmd.lmac_id = htole32(IWX_LMAC_5G_INDEX);

	cmd.ci.band = IEEE80211_IS_CHAN_2GHZ(chan) ?
	    IWX_PHY_BAND_24 : IWX_PHY_BAND_5;
	cmd.ci.channel = ieee80211_chan2ieee(ic, chan);
	if (vht_chan_width == IEEE80211_VHTOP0_CHAN_WIDTH_80) {
		cmd.ci.ctrl_pos = iwx_get_vht_ctrl_pos(ic, chan);
		cmd.ci.width = IWX_PHY_VHT_CHANNEL_MODE80;
	} else if (chan->ic_flags & IEEE80211_CHAN_40MHZ) {
		if (sco == IEEE80211_HTOP0_SCO_SCA) {
			/* secondary chan above -> control chan below */
			cmd.ci.ctrl_pos = IWX_PHY_VHT_CTRL_POS_1_BELOW;
			cmd.ci.width = IWX_PHY_VHT_CHANNEL_MODE40;
		} else if (sco == IEEE80211_HTOP0_SCO_SCB) {
			/* secondary chan below -> control chan above */
			cmd.ci.ctrl_pos = IWX_PHY_VHT_CTRL_POS_1_ABOVE;
			cmd.ci.width = IWX_PHY_VHT_CHANNEL_MODE40;
		} else {
			cmd.ci.width = IWX_PHY_VHT_CHANNEL_MODE20;
			cmd.ci.ctrl_pos = IWX_PHY_VHT_CTRL_POS_1_BELOW;
		}
	} else {
		cmd.ci.width = IWX_PHY_VHT_CHANNEL_MODE20;
		cmd.ci.ctrl_pos = IWX_PHY_VHT_CTRL_POS_1_BELOW;
	}

	if (cmdver < 4 && iwx_lookup_cmd_ver(sc, IWX_DATA_PATH_GROUP,
	    IWX_RLC_CONFIG_CMD) != 2) {
		idle_cnt = chains_static;
		active_cnt = chains_dynamic;
		cmd.rxchain_info = htole32(iwx_fw_valid_rx_ant(sc) <<
		    IWX_PHY_RX_CHAIN_VALID_POS);
		cmd.rxchain_info |= htole32(idle_cnt <<
		    IWX_PHY_RX_CHAIN_CNT_POS);
		cmd.rxchain_info |= htole32(active_cnt <<
		    IWX_PHY_RX_CHAIN_MIMO_CNT_POS);
	}

	return iwx_send_cmd_pdu(sc, IWX_PHY_CONTEXT_CMD, 0, sizeof(cmd), &cmd);
}

int
iwx_phy_ctxt_cmd(struct iwx_softc *sc, struct iwx_phy_ctxt *ctxt,
    uint8_t chains_static, uint8_t chains_dynamic, uint32_t action,
    uint32_t apply_time, uint8_t sco, uint8_t vht_chan_width)
{
	int cmdver;

	cmdver = iwx_lookup_cmd_ver(sc, IWX_LONG_GROUP, IWX_PHY_CONTEXT_CMD);
	if (cmdver != 3 && cmdver != 4) {
		printf("%s: firmware does not support phy-context-cmd v3/v4\n",
		    DEVNAME(sc));
		return ENOTSUP;
	}

	/*
	 * Intel increased the size of the fw_channel_info struct and neglected
	 * to bump the phy_context_cmd struct, which contains an fw_channel_info
	 * member in the middle.
	 * To keep things simple we use a separate function to handle the larger
	 * variant of the phy context command.
	 */
	if (isset(sc->sc_enabled_capa, IWX_UCODE_TLV_CAPA_ULTRA_HB_CHANNELS)) {
		return iwx_phy_ctxt_cmd_uhb_v3_v4(sc, ctxt, chains_static,
		    chains_dynamic, action, sco, vht_chan_width, cmdver);
	}

	return iwx_phy_ctxt_cmd_v3_v4(sc, ctxt, chains_static, chains_dynamic,
	    action, sco, vht_chan_width, cmdver);
}

int
iwx_send_cmd(struct iwx_softc *sc, struct iwx_host_cmd *hcmd)
{
	struct iwx_tx_ring *ring = &sc->txq[IWX_DQA_CMD_QUEUE];
	struct iwx_tfh_tfd *desc;
	struct iwx_tx_data *txdata;
	struct iwx_device_cmd *cmd;
	struct mbuf *m;
	bus_addr_t paddr;
	uint64_t addr;
	int err = 0, i, paylen, off, s;
	int idx, code, async, group_id;
	size_t hdrlen, datasz;
	uint8_t *data;
	int generation = sc->sc_generation;

	code = hcmd->id;
	async = hcmd->flags & IWX_CMD_ASYNC;
	idx = ring->cur;

	for (i = 0, paylen = 0; i < nitems(hcmd->len); i++) {
		paylen += hcmd->len[i];
	}

	/* If this command waits for a response, allocate response buffer. */
	hcmd->resp_pkt = NULL;
	if (hcmd->flags & IWX_CMD_WANT_RESP) {
		uint8_t *resp_buf;
		KASSERT(!async);
		KASSERT(hcmd->resp_pkt_len >= sizeof(struct iwx_rx_packet));
		KASSERT(hcmd->resp_pkt_len <= IWX_CMD_RESP_MAX);
		if (sc->sc_cmd_resp_pkt[idx] != NULL)
			return ENOSPC;
		resp_buf = malloc(hcmd->resp_pkt_len, M_DEVBUF,
		    M_NOWAIT | M_ZERO);
		if (resp_buf == NULL)
			return ENOMEM;
		sc->sc_cmd_resp_pkt[idx] = resp_buf;
		sc->sc_cmd_resp_len[idx] = hcmd->resp_pkt_len;
	} else {
		sc->sc_cmd_resp_pkt[idx] = NULL;
	}

	s = splnet();

	desc = &ring->desc[idx];
	txdata = &ring->data[idx];

	/*
	 * XXX Intel inside (tm)
	 * Firmware API versions >= 50 reject old-style commands in
	 * group 0 with a "BAD_COMMAND" firmware error. We must pretend
	 * that such commands were in the LONG_GROUP instead in order
	 * for firmware to accept them.
	 */
	if (iwx_cmd_groupid(code) == 0) {
		code = IWX_WIDE_ID(IWX_LONG_GROUP, code);
		txdata->flags |= IWX_TXDATA_FLAG_CMD_IS_NARROW;
	} else
		txdata->flags &= ~IWX_TXDATA_FLAG_CMD_IS_NARROW;

	group_id = iwx_cmd_groupid(code);

	hdrlen = sizeof(cmd->hdr_wide);
	datasz = sizeof(cmd->data_wide);

	if (paylen > datasz) {
		/* Command is too large to fit in pre-allocated space. */
		size_t totlen = hdrlen + paylen;
		if (paylen > IWX_MAX_CMD_PAYLOAD_SIZE) {
			printf("%s: firmware command too long (%zd bytes)\n",
			    DEVNAME(sc), totlen);
			err = EINVAL;
			goto out;
		}
		m = MCLGETL(NULL, M_DONTWAIT, totlen);
		if (m == NULL) {
			printf("%s: could not get fw cmd mbuf (%zd bytes)\n",
			    DEVNAME(sc), totlen);
			err = ENOMEM;
			goto out;
		}
		cmd = mtod(m, struct iwx_device_cmd *);
		err = bus_dmamap_load(sc->sc_dmat, txdata->map, cmd,
		    totlen, NULL, BUS_DMA_NOWAIT | BUS_DMA_WRITE);
		if (err) {
			printf("%s: could not load fw cmd mbuf (%zd bytes)\n",
			    DEVNAME(sc), totlen);
			m_freem(m);
			goto out;
		}
		txdata->m = m; /* mbuf will be freed in iwx_cmd_done() */
		paddr = txdata->map->dm_segs[0].ds_addr;
	} else {
		cmd = &ring->cmd[idx];
		paddr = txdata->cmd_paddr;
	}

	memset(cmd, 0, sizeof(*cmd));
	cmd->hdr_wide.opcode = iwx_cmd_opcode(code);
	cmd->hdr_wide.group_id = group_id;
	cmd->hdr_wide.qid = ring->qid;
	cmd->hdr_wide.idx = idx;
	cmd->hdr_wide.length = htole16(paylen);
	cmd->hdr_wide.version = iwx_cmd_version(code);
	data = cmd->data_wide;

	for (i = 0, off = 0; i < nitems(hcmd->data); i++) {
		if (hcmd->len[i] == 0)
			continue;
		memcpy(data + off, hcmd->data[i], hcmd->len[i]);
		off += hcmd->len[i];
	}
	KASSERT(off == paylen);

	desc->tbs[0].tb_len = htole16(MIN(hdrlen + paylen, IWX_FIRST_TB_SIZE));
	addr = htole64(paddr);
	memcpy(&desc->tbs[0].addr, &addr, sizeof(addr));
	if (hdrlen + paylen > IWX_FIRST_TB_SIZE) {
		desc->tbs[1].tb_len = htole16(hdrlen + paylen -
		    IWX_FIRST_TB_SIZE);
		addr = htole64(paddr + IWX_FIRST_TB_SIZE);
		memcpy(&desc->tbs[1].addr, &addr, sizeof(addr));
		desc->num_tbs = htole16(2);
	} else
		desc->num_tbs = htole16(1);

	if (paylen > datasz) {
		bus_dmamap_sync(sc->sc_dmat, txdata->map, 0,
		    hdrlen + paylen, BUS_DMASYNC_PREWRITE);
	} else {
		bus_dmamap_sync(sc->sc_dmat, ring->cmd_dma.map,
		    (char *)(void *)cmd - (char *)(void *)ring->cmd_dma.vaddr,
		    hdrlen + paylen, BUS_DMASYNC_PREWRITE);
	}
	bus_dmamap_sync(sc->sc_dmat, ring->desc_dma.map,
	    (char *)(void *)desc - (char *)(void *)ring->desc_dma.vaddr,
	    sizeof (*desc), BUS_DMASYNC_PREWRITE);
	/* Kick command ring. */
	DPRINTF(("%s: sending command 0x%x\n", __func__, code));
	ring->queued++;
	ring->cur = (ring->cur + 1) % IWX_TX_RING_COUNT;
	ring->cur_hw = (ring->cur_hw + 1) % sc->max_tfd_queue_size;
	IWX_WRITE(sc, IWX_HBUS_TARG_WRPTR, ring->qid << 16 | ring->cur_hw);

	if (!async) {
		err = tsleep_nsec(desc, PCATCH, "iwxcmd", SEC_TO_NSEC(1));
		if (err == 0) {
			/* if hardware is no longer up, return error */
			if (generation != sc->sc_generation) {
				err = ENXIO;
				goto out;
			}

			/* Response buffer will be freed in iwx_free_resp(). */
			hcmd->resp_pkt = (void *)sc->sc_cmd_resp_pkt[idx];
			sc->sc_cmd_resp_pkt[idx] = NULL;
		} else if (generation == sc->sc_generation) {
			free(sc->sc_cmd_resp_pkt[idx], M_DEVBUF,
			    sc->sc_cmd_resp_len[idx]);
			sc->sc_cmd_resp_pkt[idx] = NULL;
		}
	}
 out:
	splx(s);

	return err;
}

int
iwx_send_cmd_pdu(struct iwx_softc *sc, uint32_t id, uint32_t flags,
    uint16_t len, const void *data)
{
	struct iwx_host_cmd cmd = {
		.id = id,
		.len = { len, },
		.data = { data, },
		.flags = flags,
	};

	return iwx_send_cmd(sc, &cmd);
}

int
iwx_send_cmd_status(struct iwx_softc *sc, struct iwx_host_cmd *cmd,
    uint32_t *status)
{
	struct iwx_rx_packet *pkt;
	struct iwx_cmd_response *resp;
	int err, resp_len;

	KASSERT((cmd->flags & IWX_CMD_WANT_RESP) == 0);
	cmd->flags |= IWX_CMD_WANT_RESP;
	cmd->resp_pkt_len = sizeof(*pkt) + sizeof(*resp);

	err = iwx_send_cmd(sc, cmd);
	if (err)
		return err;

	pkt = cmd->resp_pkt;
	if (pkt == NULL || (pkt->hdr.flags & IWX_CMD_FAILED_MSK))
		return EIO;

	resp_len = iwx_rx_packet_payload_len(pkt);
	if (resp_len != sizeof(*resp)) {
		iwx_free_resp(sc, cmd);
		return EIO;
	}

	resp = (void *)pkt->data;
	*status = le32toh(resp->status);
	iwx_free_resp(sc, cmd);
	return err;
}

int
iwx_send_cmd_pdu_status(struct iwx_softc *sc, uint32_t id, uint16_t len,
    const void *data, uint32_t *status)
{
	struct iwx_host_cmd cmd = {
		.id = id,
		.len = { len, },
		.data = { data, },
	};

	return iwx_send_cmd_status(sc, &cmd, status);
}

void
iwx_free_resp(struct iwx_softc *sc, struct iwx_host_cmd *hcmd)
{
	KASSERT((hcmd->flags & (IWX_CMD_WANT_RESP)) == IWX_CMD_WANT_RESP);
	free(hcmd->resp_pkt, M_DEVBUF, hcmd->resp_pkt_len);
	hcmd->resp_pkt = NULL;
}

void
iwx_cmd_done(struct iwx_softc *sc, int qid, int idx, int code)
{
	struct iwx_tx_ring *ring = &sc->txq[IWX_DQA_CMD_QUEUE];
	struct iwx_tx_data *data;

	if (qid != IWX_DQA_CMD_QUEUE) {
		return;	/* Not a command ack. */
	}

	data = &ring->data[idx];

	if (data->m != NULL) {
		bus_dmamap_sync(sc->sc_dmat, data->map, 0,
		    data->map->dm_mapsize, BUS_DMASYNC_POSTWRITE);
		bus_dmamap_unload(sc->sc_dmat, data->map);
		m_freem(data->m);
		data->m = NULL;
	}
	wakeup(&ring->desc[idx]);

	DPRINTF(("%s: command 0x%x done\n", __func__, code));
	if (ring->queued == 0) {
		DPRINTF(("%s: unexpected firmware response to command 0x%x\n",
			DEVNAME(sc), code));
	} else if (ring->queued > 0)
		ring->queued--;
}

uint32_t
iwx_fw_rateidx_ofdm(uint8_t rval)
{
	/* Firmware expects indices which match our 11a rate set. */
	const struct ieee80211_rateset *rs = &ieee80211_std_rateset_11a;
	int i;

	for (i = 0; i < rs->rs_nrates; i++) {
		if ((rs->rs_rates[i] & IEEE80211_RATE_VAL) == rval)
			return i;
	}

	return 0;
}

uint32_t
iwx_fw_rateidx_cck(uint8_t rval)
{
	/* Firmware expects indices which match our 11b rate set. */
	const struct ieee80211_rateset *rs = &ieee80211_std_rateset_11b;
	int i;

	for (i = 0; i < rs->rs_nrates; i++) {
		if ((rs->rs_rates[i] & IEEE80211_RATE_VAL) == rval)
			return i;
	}

	return 0;
}

/*
 * Determine the Tx command flags and Tx rate+flags to use.
 * Return the selected Tx rate.
 */
const struct iwx_rate *
iwx_tx_fill_cmd(struct iwx_softc *sc, struct iwx_node *in,
    struct ieee80211_frame *wh, uint16_t *flags, uint32_t *rate_n_flags)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ieee80211_node *ni = &in->in_ni;
	struct ieee80211_rateset *rs = &ni->ni_rates;
	const struct iwx_rate *rinfo;
	int type = wh->i_fc[0] & IEEE80211_FC0_TYPE_MASK;
	int min_ridx = iwx_rval2ridx(ieee80211_min_basic_rate(ic));
	int ridx, rate_flags;
	uint8_t rval;

	*flags = 0;

	if (IEEE80211_IS_MULTICAST(wh->i_addr1) ||
	    type != IEEE80211_FC0_TYPE_DATA) {
		/* for non-data, use the lowest supported rate */
		ridx = min_ridx;
		*flags |= IWX_TX_FLAGS_CMD_RATE;
	} else if (ni->ni_flags & IEEE80211_NODE_HT) {
		ridx = iwx_mcs2ridx[ni->ni_txmcs];
	} else {
		rval = (rs->rs_rates[ni->ni_txrate] & IEEE80211_RATE_VAL);
		ridx = iwx_rval2ridx(rval);
		if (ridx < min_ridx)
			ridx = min_ridx;
	}

	if ((ic->ic_flags & IEEE80211_F_RSNON) &&
	    ni->ni_rsn_supp_state == RSNA_SUPP_PTKNEGOTIATING)
		*flags |= IWX_TX_FLAGS_HIGH_PRI;

	rinfo = &iwx_rates[ridx];

	/*
	 * Do not fill rate_n_flags if firmware controls the Tx rate.
	 * For data frames we rely on Tx rate scaling in firmware by default.
	 */
	if ((*flags & IWX_TX_FLAGS_CMD_RATE) == 0) {
		*rate_n_flags = 0;
		return rinfo;
	}

	/*
	 * Forcing a CCK/OFDM legacy rate is important for management frames.
	 * Association will only succeed if we do this correctly.
	 */
	rate_flags = IWX_RATE_MCS_ANT_A_MSK;
	if (IWX_RIDX_IS_CCK(ridx)) {
		if (sc->sc_rate_n_flags_version >= 2)
			rate_flags |= IWX_RATE_MCS_CCK_MSK;
		else
			rate_flags |= IWX_RATE_MCS_CCK_MSK_V1;
	} else if (sc->sc_rate_n_flags_version >= 2)
		rate_flags |= IWX_RATE_MCS_LEGACY_OFDM_MSK;

	if (sc->sc_rate_n_flags_version >= 2) {
		if (rate_flags & IWX_RATE_MCS_LEGACY_OFDM_MSK) {
			rate_flags |= (iwx_fw_rateidx_ofdm(rinfo->rate) &
			    IWX_RATE_LEGACY_RATE_MSK);
		} else {
			rate_flags |= (iwx_fw_rateidx_cck(rinfo->rate) &
			    IWX_RATE_LEGACY_RATE_MSK);
		}
	} else
		rate_flags |= rinfo->plcp;

	*rate_n_flags = rate_flags;

	return rinfo;
}

void
iwx_tx_update_byte_tbl(struct iwx_softc *sc, struct iwx_tx_ring *txq,
    int idx, uint16_t byte_cnt, uint16_t num_tbs)
{
	uint8_t filled_tfd_size, num_fetch_chunks;
	uint16_t len = byte_cnt;
	uint16_t bc_ent;

	filled_tfd_size = offsetof(struct iwx_tfh_tfd, tbs) +
			  num_tbs * sizeof(struct iwx_tfh_tb);
	/*
	 * filled_tfd_size contains the number of filled bytes in the TFD.
	 * Dividing it by 64 will give the number of chunks to fetch
	 * to SRAM- 0 for one chunk, 1 for 2 and so on.
	 * If, for example, TFD contains only 3 TBs then 32 bytes
	 * of the TFD are used, and only one chunk of 64 bytes should
	 * be fetched
	 */
	num_fetch_chunks = howmany(filled_tfd_size, 64) - 1;

	if (sc->sc_device_family >= IWX_DEVICE_FAMILY_AX210) {
		struct iwx_gen3_bc_tbl_entry *scd_bc_tbl = txq->bc_tbl.vaddr;
		/* Starting from AX210, the HW expects bytes */
		bc_ent = htole16(len | (num_fetch_chunks << 14));
		scd_bc_tbl[idx].tfd_offset = bc_ent;
	} else {
		struct iwx_agn_scd_bc_tbl *scd_bc_tbl = txq->bc_tbl.vaddr;
		/* Before AX210, the HW expects DW */
		len = howmany(len, 4);
		bc_ent = htole16(len | (num_fetch_chunks << 12));
		scd_bc_tbl->tfd_offset[idx] = bc_ent;
	}

	bus_dmamap_sync(sc->sc_dmat, txq->bc_tbl.map, 0,
	    txq->bc_tbl.map->dm_mapsize, BUS_DMASYNC_PREWRITE);
}

int
iwx_tx(struct iwx_softc *sc, struct mbuf *m, struct ieee80211_node *ni)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct iwx_node *in = (void *)ni;
	struct iwx_tx_ring *ring;
	struct iwx_tx_data *data;
	struct iwx_tfh_tfd *desc;
	struct iwx_device_cmd *cmd;
	struct ieee80211_frame *wh;
	struct ieee80211_key *k = NULL;
	const struct iwx_rate *rinfo;
	uint64_t paddr;
	u_int hdrlen;
	bus_dma_segment_t *seg;
	uint32_t rate_n_flags;
	uint16_t num_tbs, flags, offload_assist = 0;
	uint8_t type, subtype;
	int i, totlen, err, pad, qid;
	size_t txcmd_size;

	wh = mtod(m, struct ieee80211_frame *);
	type = wh->i_fc[0] & IEEE80211_FC0_TYPE_MASK;
	subtype = wh->i_fc[0] & IEEE80211_FC0_SUBTYPE_MASK;
	if (type == IEEE80211_FC0_TYPE_CTL)
		hdrlen = sizeof(struct ieee80211_frame_min);
	else
		hdrlen = ieee80211_get_hdrlen(wh);

	qid = sc->first_data_qid;

	/* Put QoS frames on the data queue which maps to their TID. */
	if (ieee80211_has_qos(wh)) {
		struct ieee80211_tx_ba *ba;
		uint16_t qos = ieee80211_get_qos(wh);
		uint8_t tid = qos & IEEE80211_QOS_TID;

		ba = &ni->ni_tx_ba[tid];
		if (!IEEE80211_IS_MULTICAST(wh->i_addr1) &&
		    type == IEEE80211_FC0_TYPE_DATA &&
		    subtype != IEEE80211_FC0_SUBTYPE_NODATA &&
		    sc->aggqid[tid] != 0 &&
		    ba->ba_state == IEEE80211_BA_AGREED) {
			qid = sc->aggqid[tid];
		}
	}

	ring = &sc->txq[qid];
	desc = &ring->desc[ring->cur];
	memset(desc, 0, sizeof(*desc));
	data = &ring->data[ring->cur];

	cmd = &ring->cmd[ring->cur];
	cmd->hdr.code = IWX_TX_CMD;
	cmd->hdr.flags = 0;
	cmd->hdr.qid = ring->qid;
	cmd->hdr.idx = ring->cur;

	rinfo = iwx_tx_fill_cmd(sc, in, wh, &flags, &rate_n_flags);

#if NBPFILTER > 0
	if (sc->sc_drvbpf != NULL) {
		struct iwx_tx_radiotap_header *tap = &sc->sc_txtap;
		uint16_t chan_flags;

		tap->wt_flags = 0;
		tap->wt_chan_freq = htole16(ni->ni_chan->ic_freq);
		chan_flags = ni->ni_chan->ic_flags;
		if (ic->ic_curmode != IEEE80211_MODE_11N &&
		    ic->ic_curmode != IEEE80211_MODE_11AC) {
			chan_flags &= ~IEEE80211_CHAN_HT;
			chan_flags &= ~IEEE80211_CHAN_40MHZ;
		}
		if (ic->ic_curmode != IEEE80211_MODE_11AC)
			chan_flags &= ~IEEE80211_CHAN_VHT;
		tap->wt_chan_flags = htole16(chan_flags);
		if ((ni->ni_flags & IEEE80211_NODE_HT) &&
		    !IEEE80211_IS_MULTICAST(wh->i_addr1) &&
		    type == IEEE80211_FC0_TYPE_DATA &&
		    rinfo->ht_plcp != IWX_RATE_HT_SISO_MCS_INV_PLCP) {
			tap->wt_rate = (0x80 | rinfo->ht_plcp);
		} else
			tap->wt_rate = rinfo->rate;
		if ((ic->ic_flags & IEEE80211_F_WEPON) &&
		    (wh->i_fc[1] & IEEE80211_FC1_PROTECTED))
			tap->wt_flags |= IEEE80211_RADIOTAP_F_WEP;

		bpf_mtap_hdr(sc->sc_drvbpf, tap, sc->sc_txtap_len,
		    m, BPF_DIRECTION_OUT);
	}
#endif

	if (wh->i_fc[1] & IEEE80211_FC1_PROTECTED) {
                k = ieee80211_get_txkey(ic, wh, ni);
		if (k->k_cipher != IEEE80211_CIPHER_CCMP) {
			if ((m = ieee80211_encrypt(ic, m, k)) == NULL)
				return ENOBUFS;
			/* 802.11 header may have moved. */
			wh = mtod(m, struct ieee80211_frame *);
			flags |= IWX_TX_FLAGS_ENCRYPT_DIS;
		} else {
			k->k_tsc++;
			/* Hardware increments PN internally and adds IV. */
		}
	} else
		flags |= IWX_TX_FLAGS_ENCRYPT_DIS;

	totlen = m->m_pkthdr.len;

	if (hdrlen & 3) {
		/* First segment length must be a multiple of 4. */
		pad = 4 - (hdrlen & 3);
		offload_assist |= IWX_TX_CMD_OFFLD_PAD;
	} else
		pad = 0;

	if (sc->sc_device_family >= IWX_DEVICE_FAMILY_AX210) {
		struct iwx_tx_cmd_gen3 *tx = (void *)cmd->data;
		memset(tx, 0, sizeof(*tx));
		tx->len = htole16(totlen);
		tx->offload_assist = htole32(offload_assist);
		tx->flags = htole16(flags);
		tx->rate_n_flags = htole32(rate_n_flags);
		memcpy(tx->hdr, wh, hdrlen);
		txcmd_size = sizeof(*tx);
	} else {
		struct iwx_tx_cmd_gen2 *tx = (void *)cmd->data;
		memset(tx, 0, sizeof(*tx));
		tx->len = htole16(totlen);
		tx->offload_assist = htole16(offload_assist);
		tx->flags = htole32(flags);
		tx->rate_n_flags = htole32(rate_n_flags);
		memcpy(tx->hdr, wh, hdrlen);
		txcmd_size = sizeof(*tx);
	}

	/* Trim 802.11 header. */
	m_adj(m, hdrlen);

	err = bus_dmamap_load_mbuf(sc->sc_dmat, data->map, m,
	    BUS_DMA_NOWAIT | BUS_DMA_WRITE);
	if (err && err != EFBIG) {
		printf("%s: can't map mbuf (error %d)\n", DEVNAME(sc), err);
		m_freem(m);
		return err;
	}
	if (err) {
		/* Too many DMA segments, linearize mbuf. */
		if (m_defrag(m, M_DONTWAIT)) {
			m_freem(m);
			return ENOBUFS;
		}
		err = bus_dmamap_load_mbuf(sc->sc_dmat, data->map, m,
		    BUS_DMA_NOWAIT | BUS_DMA_WRITE);
		if (err) {
			printf("%s: can't map mbuf (error %d)\n", DEVNAME(sc),
			    err);
			m_freem(m);
			return err;
		}
	}
	data->m = m;
	data->in = in;

	/* Fill TX descriptor. */
	num_tbs = 2 + data->map->dm_nsegs;
	desc->num_tbs = htole16(num_tbs);

	desc->tbs[0].tb_len = htole16(IWX_FIRST_TB_SIZE);
	paddr = htole64(data->cmd_paddr);
	memcpy(&desc->tbs[0].addr, &paddr, sizeof(paddr));
	if (data->cmd_paddr >> 32 != (data->cmd_paddr + le32toh(desc->tbs[0].tb_len)) >> 32)
		DPRINTF(("%s: TB0 crosses 32bit boundary\n", __func__));
	desc->tbs[1].tb_len = htole16(sizeof(struct iwx_cmd_header) +
	    txcmd_size + hdrlen + pad - IWX_FIRST_TB_SIZE);
	paddr = htole64(data->cmd_paddr + IWX_FIRST_TB_SIZE);
	memcpy(&desc->tbs[1].addr, &paddr, sizeof(paddr));

	if (data->cmd_paddr >> 32 != (data->cmd_paddr + le32toh(desc->tbs[1].tb_len)) >> 32)
		DPRINTF(("%s: TB1 crosses 32bit boundary\n", __func__));

	/* Other DMA segments are for data payload. */
	seg = data->map->dm_segs;
	for (i = 0; i < data->map->dm_nsegs; i++, seg++) {
		desc->tbs[i + 2].tb_len = htole16(seg->ds_len);
		paddr = htole64(seg->ds_addr);
		memcpy(&desc->tbs[i + 2].addr, &paddr, sizeof(paddr));
		if (data->cmd_paddr >> 32 != (data->cmd_paddr + le32toh(desc->tbs[i + 2].tb_len)) >> 32)
			DPRINTF(("%s: TB%d crosses 32bit boundary\n", __func__, i + 2));
	}

	bus_dmamap_sync(sc->sc_dmat, data->map, 0, data->map->dm_mapsize,
	    BUS_DMASYNC_PREWRITE);
	bus_dmamap_sync(sc->sc_dmat, ring->cmd_dma.map,
	    (char *)(void *)cmd - (char *)(void *)ring->cmd_dma.vaddr,
	    sizeof (*cmd), BUS_DMASYNC_PREWRITE);
	bus_dmamap_sync(sc->sc_dmat, ring->desc_dma.map,
	    (char *)(void *)desc - (char *)(void *)ring->desc_dma.vaddr,
	    sizeof (*desc), BUS_DMASYNC_PREWRITE);

	iwx_tx_update_byte_tbl(sc, ring, ring->cur, totlen, num_tbs);

	/* Kick TX ring. */
	ring->cur = (ring->cur + 1) % IWX_TX_RING_COUNT;
	ring->cur_hw = (ring->cur_hw + 1) % sc->max_tfd_queue_size;
	IWX_WRITE(sc, IWX_HBUS_TARG_WRPTR, ring->qid << 16 | ring->cur_hw);

	/* Mark TX ring as full if we reach a certain threshold. */
	if (++ring->queued > IWX_TX_RING_HIMARK) {
		sc->qfullmsk |= 1 << ring->qid;
	}

	if (ic->ic_if.if_flags & IFF_UP)
		sc->sc_tx_timer[ring->qid] = 15;

	return 0;
}

int
iwx_flush_sta_tids(struct iwx_softc *sc, int sta_id, uint16_t tids)
{
	struct iwx_rx_packet *pkt;
	struct iwx_tx_path_flush_cmd_rsp *resp;
	struct iwx_tx_path_flush_cmd flush_cmd = {
		.sta_id = htole32(sta_id),
		.tid_mask = htole16(tids),
	};
	struct iwx_host_cmd hcmd = {
		.id = IWX_TXPATH_FLUSH,
		.len = { sizeof(flush_cmd), },
		.data = { &flush_cmd, },
		.flags = IWX_CMD_WANT_RESP,
		.resp_pkt_len = sizeof(*pkt) + sizeof(*resp),
	};
	int err, resp_len, i, num_flushed_queues;

	err = iwx_send_cmd(sc, &hcmd);
	if (err)
		return err;

	pkt = hcmd.resp_pkt;
	if (!pkt || (pkt->hdr.flags & IWX_CMD_FAILED_MSK)) {
		err = EIO;
		goto out;
	}

	resp_len = iwx_rx_packet_payload_len(pkt);
	if (resp_len != sizeof(*resp)) {
		err = EIO;
		goto out;
	}

	resp = (void *)pkt->data;

	if (le16toh(resp->sta_id) != sta_id) {
		err = EIO;
		goto out;
	}

	num_flushed_queues = le16toh(resp->num_flushed_queues);
	if (num_flushed_queues > IWX_TX_FLUSH_QUEUE_RSP) {
		err = EIO;
		goto out;
	}

	for (i = 0; i < num_flushed_queues; i++) {
		struct iwx_flush_queue_info *queue_info = &resp->queues[i];
		uint16_t tid = le16toh(queue_info->tid);
		uint16_t read_after = le16toh(queue_info->read_after_flush);
		uint16_t qid = le16toh(queue_info->queue_num);
		struct iwx_tx_ring *txq;

		if (qid >= nitems(sc->txq))
			continue;

		txq = &sc->txq[qid];
		if (tid != txq->tid)
			continue;

		iwx_txq_advance(sc, txq, read_after);
	}
out:
	iwx_free_resp(sc, &hcmd);
	return err;
}

#define IWX_FLUSH_WAIT_MS	2000

int
iwx_drain_sta(struct iwx_softc *sc, struct iwx_node* in, int drain)
{
	struct iwx_add_sta_cmd cmd;
	int err;
	uint32_t status;

	/* No need to drain with MLD firmware. */
	if (sc->sc_use_mld_api)
		return 0;
	
	memset(&cmd, 0, sizeof(cmd));
	cmd.mac_id_n_color = htole32(IWX_FW_CMD_ID_AND_COLOR(in->in_id,
	    in->in_color));
	cmd.sta_id = IWX_STATION_ID;
	cmd.add_modify = IWX_STA_MODE_MODIFY;
	cmd.station_flags = drain ? htole32(IWX_STA_FLG_DRAIN_FLOW) : 0;
	cmd.station_flags_msk = htole32(IWX_STA_FLG_DRAIN_FLOW);

	status = IWX_ADD_STA_SUCCESS;
	err = iwx_send_cmd_pdu_status(sc, IWX_ADD_STA,
	    sizeof(cmd), &cmd, &status);
	if (err) {
		printf("%s: could not update sta (error %d)\n",
		    DEVNAME(sc), err);
		return err;
	}

	switch (status & IWX_ADD_STA_STATUS_MASK) {
	case IWX_ADD_STA_SUCCESS:
		break;
	default:
		err = EIO;
		printf("%s: Couldn't %s draining for station\n",
		    DEVNAME(sc), drain ? "enable" : "disable");
		break;
	}

	return err;
}

int
iwx_flush_sta(struct iwx_softc *sc, struct iwx_node *in)
{
	int err;

	splassert(IPL_NET);

	sc->sc_flags |= IWX_FLAG_TXFLUSH;

	err = iwx_drain_sta(sc, in, 1);
	if (err)
		goto done;

	err = iwx_flush_sta_tids(sc, IWX_STATION_ID, 0xffff);
	if (err) {
		printf("%s: could not flush Tx path (error %d)\n",
		    DEVNAME(sc), err);
		goto done;
	}

	err = iwx_drain_sta(sc, in, 0);
done:
	sc->sc_flags &= ~IWX_FLAG_TXFLUSH;
	return err;
}

#define IWX_POWER_KEEP_ALIVE_PERIOD_SEC    25

int
iwx_beacon_filter_send_cmd(struct iwx_softc *sc,
    struct iwx_beacon_filter_cmd *cmd)
{
	return iwx_send_cmd_pdu(sc, IWX_REPLY_BEACON_FILTERING_CMD,
	    0, sizeof(struct iwx_beacon_filter_cmd), cmd);
}

int
iwx_update_beacon_abort(struct iwx_softc *sc, struct iwx_node *in, int enable)
{
	struct iwx_beacon_filter_cmd cmd = {
		IWX_BF_CMD_CONFIG_DEFAULTS,
		.bf_enable_beacon_filter = htole32(1),
		.ba_enable_beacon_abort = htole32(enable),
	};

	if (!sc->sc_bf.bf_enabled)
		return 0;

	sc->sc_bf.ba_enabled = enable;
	return iwx_beacon_filter_send_cmd(sc, &cmd);
}

void
iwx_power_build_cmd(struct iwx_softc *sc, struct iwx_node *in,
    struct iwx_mac_power_cmd *cmd)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ieee80211_node *ni = &in->in_ni;
	int dtim_period, dtim_msec, keep_alive;

	cmd->id_and_color = htole32(IWX_FW_CMD_ID_AND_COLOR(in->in_id,
	    in->in_color));
	if (ni->ni_dtimperiod)
		dtim_period = ni->ni_dtimperiod;
	else
		dtim_period = 1;

	/*
	 * Regardless of power management state the driver must set
	 * keep alive period. FW will use it for sending keep alive NDPs
	 * immediately after association. Check that keep alive period
	 * is at least 3 * DTIM.
	 */
	dtim_msec = dtim_period * ni->ni_intval;
	keep_alive = MAX(3 * dtim_msec, 1000 * IWX_POWER_KEEP_ALIVE_PERIOD_SEC);
	keep_alive = roundup(keep_alive, 1000) / 1000;
	cmd->keep_alive_seconds = htole16(keep_alive);

	if (ic->ic_opmode != IEEE80211_M_MONITOR)
		cmd->flags = htole16(IWX_POWER_FLAGS_POWER_SAVE_ENA_MSK);
}

int
iwx_power_mac_update_mode(struct iwx_softc *sc, struct iwx_node *in)
{
	int err;
	int ba_enable;
	struct iwx_mac_power_cmd cmd;

	memset(&cmd, 0, sizeof(cmd));

	iwx_power_build_cmd(sc, in, &cmd);

	err = iwx_send_cmd_pdu(sc, IWX_MAC_PM_POWER_TABLE, 0,
	    sizeof(cmd), &cmd);
	if (err != 0)
		return err;

	ba_enable = !!(cmd.flags &
	    htole16(IWX_POWER_FLAGS_POWER_MANAGEMENT_ENA_MSK));
	return iwx_update_beacon_abort(sc, in, ba_enable);
}

int
iwx_power_update_device(struct iwx_softc *sc)
{
	struct iwx_device_power_cmd cmd = { };
	struct ieee80211com *ic = &sc->sc_ic;

	if (ic->ic_opmode != IEEE80211_M_MONITOR)
		cmd.flags = htole16(IWX_DEVICE_POWER_FLAGS_POWER_SAVE_ENA_MSK);

	return iwx_send_cmd_pdu(sc,
	    IWX_POWER_TABLE_CMD, 0, sizeof(cmd), &cmd);
}

int
iwx_enable_beacon_filter(struct iwx_softc *sc, struct iwx_node *in)
{
	struct iwx_beacon_filter_cmd cmd = {
		IWX_BF_CMD_CONFIG_DEFAULTS,
		.bf_enable_beacon_filter = htole32(1),
		.ba_enable_beacon_abort = htole32(sc->sc_bf.ba_enabled),
	};
	int err;

	err = iwx_beacon_filter_send_cmd(sc, &cmd);
	if (err == 0)
		sc->sc_bf.bf_enabled = 1;

	return err;
}

int
iwx_disable_beacon_filter(struct iwx_softc *sc)
{
	struct iwx_beacon_filter_cmd cmd;
	int err;

	memset(&cmd, 0, sizeof(cmd));

	err = iwx_beacon_filter_send_cmd(sc, &cmd);
	if (err == 0)
		sc->sc_bf.bf_enabled = 0;

	return err;
}

int
iwx_add_sta_cmd(struct iwx_softc *sc, struct iwx_node *in, int update)
{
	struct iwx_add_sta_cmd add_sta_cmd;
	int err;
	uint32_t status, aggsize;
	const uint32_t max_aggsize = (IWX_STA_FLG_MAX_AGG_SIZE_64K >>
		    IWX_STA_FLG_MAX_AGG_SIZE_SHIFT);
	struct ieee80211com *ic = &sc->sc_ic;

	if (!update && (sc->sc_flags & IWX_FLAG_STA_ACTIVE))
		panic("STA already added");

	if (sc->sc_use_mld_api)
		return iwx_mld_add_sta_cmd(sc, in, update);

	memset(&add_sta_cmd, 0, sizeof(add_sta_cmd));

	if (ic->ic_opmode == IEEE80211_M_MONITOR) {
		add_sta_cmd.sta_id = IWX_MONITOR_STA_ID;
		add_sta_cmd.station_type = IWX_STA_GENERAL_PURPOSE;
	} else {
		add_sta_cmd.sta_id = IWX_STATION_ID;
		add_sta_cmd.station_type = IWX_STA_LINK;
	}
	add_sta_cmd.mac_id_n_color
	    = htole32(IWX_FW_CMD_ID_AND_COLOR(in->in_id, in->in_color));
	if (!update) {
		if (ic->ic_opmode == IEEE80211_M_MONITOR)
			IEEE80211_ADDR_COPY(&add_sta_cmd.addr,
			    etheranyaddr);
		else
			IEEE80211_ADDR_COPY(&add_sta_cmd.addr,
			    in->in_macaddr);
	}
	add_sta_cmd.add_modify = update ? 1 : 0;
	add_sta_cmd.station_flags_msk
	    |= htole32(IWX_STA_FLG_FAT_EN_MSK | IWX_STA_FLG_MIMO_EN_MSK);

	if (in->in_ni.ni_flags & IEEE80211_NODE_HT) {
		add_sta_cmd.station_flags_msk
		    |= htole32(IWX_STA_FLG_MAX_AGG_SIZE_MSK |
		    IWX_STA_FLG_AGG_MPDU_DENS_MSK);

		if (iwx_mimo_enabled(sc)) {
			if (in->in_ni.ni_flags & IEEE80211_NODE_VHT) {
				uint16_t rx_mcs = (in->in_ni.ni_vht_rxmcs &
				    IEEE80211_VHT_MCS_FOR_SS_MASK(2)) >>
				    IEEE80211_VHT_MCS_FOR_SS_SHIFT(2);
				if (rx_mcs != IEEE80211_VHT_MCS_SS_NOT_SUPP) {
					add_sta_cmd.station_flags |=
					    htole32(IWX_STA_FLG_MIMO_EN_MIMO2);
				}
			} else {
				if (in->in_ni.ni_rxmcs[1] != 0) {
					add_sta_cmd.station_flags |=
					    htole32(IWX_STA_FLG_MIMO_EN_MIMO2);
				}
				if (in->in_ni.ni_rxmcs[2] != 0) {
					add_sta_cmd.station_flags |=
					    htole32(IWX_STA_FLG_MIMO_EN_MIMO3);
				}
			}
		}

		if (IEEE80211_CHAN_40MHZ_ALLOWED(in->in_ni.ni_chan) &&
		    ieee80211_node_supports_ht_chan40(&in->in_ni)) {
			add_sta_cmd.station_flags |= htole32(
			    IWX_STA_FLG_FAT_EN_40MHZ);
		}

		if (in->in_ni.ni_flags & IEEE80211_NODE_VHT) {
			if (IEEE80211_CHAN_80MHZ_ALLOWED(in->in_ni.ni_chan) &&
			    ieee80211_node_supports_vht_chan80(&in->in_ni)) {
				add_sta_cmd.station_flags |= htole32(
				    IWX_STA_FLG_FAT_EN_80MHZ);
			}
			aggsize = (in->in_ni.ni_vhtcaps &
			    IEEE80211_VHTCAP_MAX_AMPDU_LEN_MASK) >>
			    IEEE80211_VHTCAP_MAX_AMPDU_LEN_SHIFT;
		} else {
			aggsize = (in->in_ni.ni_ampdu_param &
			    IEEE80211_AMPDU_PARAM_LE);
		}
		if (aggsize > max_aggsize)
			aggsize = max_aggsize;
		add_sta_cmd.station_flags |= htole32((aggsize <<
		    IWX_STA_FLG_MAX_AGG_SIZE_SHIFT) &
		    IWX_STA_FLG_MAX_AGG_SIZE_MSK);

		switch (in->in_ni.ni_ampdu_param & IEEE80211_AMPDU_PARAM_SS) {
		case IEEE80211_AMPDU_PARAM_SS_2:
			add_sta_cmd.station_flags
			    |= htole32(IWX_STA_FLG_AGG_MPDU_DENS_2US);
			break;
		case IEEE80211_AMPDU_PARAM_SS_4:
			add_sta_cmd.station_flags
			    |= htole32(IWX_STA_FLG_AGG_MPDU_DENS_4US);
			break;
		case IEEE80211_AMPDU_PARAM_SS_8:
			add_sta_cmd.station_flags
			    |= htole32(IWX_STA_FLG_AGG_MPDU_DENS_8US);
			break;
		case IEEE80211_AMPDU_PARAM_SS_16:
			add_sta_cmd.station_flags
			    |= htole32(IWX_STA_FLG_AGG_MPDU_DENS_16US);
			break;
		default:
			break;
		}
	}

	status = IWX_ADD_STA_SUCCESS;
	err = iwx_send_cmd_pdu_status(sc, IWX_ADD_STA, sizeof(add_sta_cmd),
	    &add_sta_cmd, &status);
	if (!err && (status & IWX_ADD_STA_STATUS_MASK) != IWX_ADD_STA_SUCCESS)
		err = EIO;

	return err;
}

void
iwx_mld_modify_link_fill(struct iwx_softc *sc, struct iwx_node *in,
    struct iwx_link_config_cmd *cmd, int changes, int active)
{
#define IWX_EXP2(x)	((1 << (x)) - 1)	/* CWmin = 2^ECWmin - 1 */
	struct ieee80211com *ic = &sc->sc_ic;
	struct ieee80211_node *ni = &in->in_ni;
	int cck_ack_rates, ofdm_ack_rates;
	int i;

	cmd->link_id = htole32(0);
	cmd->mac_id = htole32(in->in_id);
	KASSERT(in->in_phyctxt);
	cmd->phy_id = htole32(in->in_phyctxt->id);
	IEEE80211_ADDR_COPY(cmd->local_link_addr, ic->ic_myaddr);
	cmd->active = htole32(active);

	iwx_ack_rates(sc, in, &cck_ack_rates, &ofdm_ack_rates);
	cmd->cck_rates = htole32(cck_ack_rates);
	cmd->ofdm_rates = htole32(ofdm_ack_rates);
	cmd->cck_short_preamble
	    = htole32((ic->ic_flags & IEEE80211_F_SHPREAMBLE) ? 1 : 0);
	cmd->short_slot
	    = htole32((ic->ic_flags & IEEE80211_F_SHSLOT) ? 1 : 0);

	for (i = 0; i < EDCA_NUM_AC; i++) {
		struct ieee80211_edca_ac_params *ac = &ic->ic_edca_ac[i];
		int txf = iwx_ac_to_tx_fifo[i];

		cmd->ac[txf].cw_min = htole16(IWX_EXP2(ac->ac_ecwmin));
		cmd->ac[txf].cw_max = htole16(IWX_EXP2(ac->ac_ecwmax));
		cmd->ac[txf].aifsn = ac->ac_aifsn;
		cmd->ac[txf].fifos_mask = (1 << txf);
		cmd->ac[txf].edca_txop = htole16(ac->ac_txoplimit * 32);
	}
	if (ni->ni_flags & IEEE80211_NODE_QOS)
		cmd->qos_flags |= htole32(IWX_MAC_QOS_FLG_UPDATE_EDCA);

	if (ni->ni_flags & IEEE80211_NODE_HT) {
		enum ieee80211_htprot htprot =
		    (ni->ni_htop1 & IEEE80211_HTOP1_PROT_MASK);
		switch (htprot) {
		case IEEE80211_HTPROT_NONE:
			break;
		case IEEE80211_HTPROT_NONMEMBER:
		case IEEE80211_HTPROT_NONHT_MIXED:
			cmd->protection_flags |=
			    htole32(IWX_LINK_PROT_FLG_HT_PROT |
			        IWX_LINK_PROT_FLG_FAT_PROT);
			break;
		case IEEE80211_HTPROT_20MHZ:
			if (in->in_phyctxt &&
			    (in->in_phyctxt->sco == IEEE80211_HTOP0_SCO_SCA ||
			    in->in_phyctxt->sco == IEEE80211_HTOP0_SCO_SCB)) {
				cmd->protection_flags |=
				    htole32(IWX_LINK_PROT_FLG_HT_PROT |
				        IWX_LINK_PROT_FLG_FAT_PROT);
			}
			break;
		default:
			break;
		}

		cmd->qos_flags |= htole32(IWX_MAC_QOS_FLG_TGN);
	}
	if (ic->ic_flags & IEEE80211_F_USEPROT)
		cmd->protection_flags |= htole32(IWX_LINK_PROT_FLG_TGG_PROTECT);

	cmd->bi = htole32(ni->ni_intval);
	cmd->dtim_interval = htole32(ni->ni_intval * ni->ni_dtimperiod);

	cmd->modify_mask = htole32(changes);
	cmd->flags = 0;
	cmd->flags_mask = 0;
	cmd->spec_link_id = 0;
	cmd->listen_lmac = 0;
	cmd->action = IWX_FW_CTXT_ACTION_MODIFY;
#undef IWX_EXP2
}

int
iwx_mld_add_sta_cmd(struct iwx_softc *sc, struct iwx_node *in, int update)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct iwx_link_config_cmd link_cmd;
	struct iwx_mvm_sta_cfg_cmd sta_cmd;
	uint32_t aggsize, mpdu_dens;
	const uint32_t max_aggsize = (IWX_STA_FLG_MAX_AGG_SIZE_4M >>
		    IWX_STA_FLG_MAX_AGG_SIZE_SHIFT);
	int err, changes;

	if (!update) {
		memset(&link_cmd, 0, sizeof(link_cmd));
		link_cmd.link_id = htole32(0);
		link_cmd.mac_id = htole32(in->in_id);
		link_cmd.spec_link_id = 0;
		if (in->in_phyctxt)
			link_cmd.phy_id = htole32(in->in_phyctxt->id);
		else
			link_cmd.phy_id = htole32(IWX_FW_CTXT_INVALID);
		IEEE80211_ADDR_COPY(link_cmd.local_link_addr, ic->ic_myaddr);
		link_cmd.listen_lmac = 0;
		link_cmd.action = IWX_FW_CTXT_ACTION_ADD;

		err = iwx_send_cmd_pdu(sc,
		    IWX_WIDE_ID(IWX_MAC_CONF_GROUP, IWX_LINK_CONFIG_CMD),
		    0, sizeof(link_cmd), &link_cmd);
		if (err)
			return err;
	}

	changes = IWX_LINK_CONTEXT_MODIFY_ACTIVE;
	changes |= IWX_LINK_CONTEXT_MODIFY_RATES_INFO;
	if (update) {
		changes |= IWX_LINK_CONTEXT_MODIFY_PROTECT_FLAGS;
		changes |= IWX_LINK_CONTEXT_MODIFY_QOS_PARAMS;
		changes |= IWX_LINK_CONTEXT_MODIFY_BEACON_TIMING;
	}

	memset(&link_cmd, 0, sizeof(link_cmd));
	iwx_mld_modify_link_fill(sc, in, &link_cmd, changes, 1);
	err = iwx_send_cmd_pdu(sc,
	    IWX_WIDE_ID(IWX_MAC_CONF_GROUP, IWX_LINK_CONFIG_CMD),
	    0, sizeof(link_cmd), &link_cmd);
	if (err)
		return err;

	memset(&sta_cmd, 0, sizeof(sta_cmd));
	if (ic->ic_opmode == IEEE80211_M_MONITOR) {
		sta_cmd.sta_id = htole32(IWX_MONITOR_STA_ID);
		sta_cmd.station_type = htole32(IWX_STA_GENERAL_PURPOSE);
	} else {
		sta_cmd.sta_id = htole32(IWX_STATION_ID);
		sta_cmd.station_type = htole32(IWX_STA_LINK);
	}
	sta_cmd.link_id = htole32(0);
	IEEE80211_ADDR_COPY(sta_cmd.peer_mld_address, in->in_macaddr);
	IEEE80211_ADDR_COPY(sta_cmd.peer_link_address, in->in_macaddr);
	sta_cmd.assoc_id = htole32(in->in_ni.ni_associd);

	if (in->in_ni.ni_flags & IEEE80211_NODE_HT) {
		if (iwx_mimo_enabled(sc))
			sta_cmd.mimo = htole32(1);

		mpdu_dens = (in->in_ni.ni_ampdu_param &
		    IEEE80211_AMPDU_PARAM_SS) >> 2;

		if (in->in_ni.ni_flags & IEEE80211_NODE_VHT) {
			aggsize = (in->in_ni.ni_vhtcaps &
			    IEEE80211_VHTCAP_MAX_AMPDU_LEN_MASK) >>
			    IEEE80211_VHTCAP_MAX_AMPDU_LEN_SHIFT;
		} else {
			aggsize = (in->in_ni.ni_ampdu_param &
			    IEEE80211_AMPDU_PARAM_LE);
		}
		if (aggsize > max_aggsize)
			aggsize = max_aggsize;

		sta_cmd.tx_ampdu_spacing = htole32(mpdu_dens);
		sta_cmd.tx_ampdu_max_size = htole32(aggsize);
	}

	return iwx_send_cmd_pdu(sc,
	    IWX_WIDE_ID(IWX_MAC_CONF_GROUP, IWX_STA_CONFIG_CMD),
	    0, sizeof(sta_cmd), &sta_cmd);
}

int
iwx_rm_sta_cmd(struct iwx_softc *sc, struct iwx_node *in)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct iwx_rm_sta_cmd rm_sta_cmd;
	int err;

	if ((sc->sc_flags & IWX_FLAG_STA_ACTIVE) == 0)
		panic("sta already removed");

	if (sc->sc_use_mld_api)
		return iwx_mld_rm_sta_cmd(sc, in);

	memset(&rm_sta_cmd, 0, sizeof(rm_sta_cmd));
	if (ic->ic_opmode == IEEE80211_M_MONITOR)
		rm_sta_cmd.sta_id = IWX_MONITOR_STA_ID;
	else
		rm_sta_cmd.sta_id = IWX_STATION_ID;

	err = iwx_send_cmd_pdu(sc, IWX_REMOVE_STA, 0, sizeof(rm_sta_cmd),
	    &rm_sta_cmd);

	return err;
}

int
iwx_rm_sta(struct iwx_softc *sc, struct iwx_node *in)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ieee80211_node *ni = &in->in_ni;
	int err, i, cmd_ver;

	err = iwx_flush_sta(sc, in);
	if (err) {
		printf("%s: could not flush Tx path (error %d)\n",
		    DEVNAME(sc), err);
		return err;
	}

	/*
	 * New SCD_QUEUE_CONFIG API requires explicit queue removal
	 * before a station gets removed.
	 */
	cmd_ver = iwx_lookup_cmd_ver(sc, IWX_DATA_PATH_GROUP,
	    IWX_SCD_QUEUE_CONFIG_CMD);
	if (cmd_ver != 0 && cmd_ver != IWX_FW_CMD_VER_UNKNOWN) {
		err = iwx_disable_mgmt_queue(sc);
		if (err)
			return err;
		for (i = IWX_FIRST_AGG_TX_QUEUE;
		    i < IWX_LAST_AGG_TX_QUEUE; i++) {
			struct iwx_tx_ring *ring = &sc->txq[i];
			if ((sc->qenablemsk & (1 << i)) == 0)
				continue;
			err = iwx_disable_txq(sc, IWX_STATION_ID,
			    ring->qid, ring->tid);
			if (err) {
				printf("%s: could not disable Tx queue %d "
				    "(error %d)\n", DEVNAME(sc), ring->qid,
				    err);
				return err;
			}
		}
	}

	err = iwx_rm_sta_cmd(sc, in);
	if (err) {
		printf("%s: could not remove STA (error %d)\n",
		    DEVNAME(sc), err);
		return err;
	}

	in->in_flags = 0;

	sc->sc_rx_ba_sessions = 0;
	sc->ba_rx.start_tidmask = 0;
	sc->ba_rx.stop_tidmask = 0;
	memset(sc->aggqid, 0, sizeof(sc->aggqid));
	sc->ba_tx.start_tidmask = 0;
	sc->ba_tx.stop_tidmask = 0;
	for (i = IWX_FIRST_AGG_TX_QUEUE; i < IWX_LAST_AGG_TX_QUEUE; i++)
		sc->qenablemsk &= ~(1 << i);
	for (i = 0; i < IEEE80211_NUM_TID; i++) {
		struct ieee80211_tx_ba *ba = &ni->ni_tx_ba[i];
		if (ba->ba_state != IEEE80211_BA_AGREED)
			continue;
		ieee80211_delba_request(ic, ni, 0, 1, i);
	}

	return 0;
}

int
iwx_mld_rm_sta_cmd(struct iwx_softc *sc, struct iwx_node *in)
{
	struct iwx_mvm_remove_sta_cmd sta_cmd;
	struct iwx_link_config_cmd link_cmd;
	int err;

	memset(&sta_cmd, 0, sizeof(sta_cmd));
	sta_cmd.sta_id = htole32(IWX_STATION_ID);

	err = iwx_send_cmd_pdu(sc,
	    IWX_WIDE_ID(IWX_MAC_CONF_GROUP, IWX_STA_REMOVE_CMD),
	    0, sizeof(sta_cmd), &sta_cmd);
	if (err)
		return err;

	memset(&link_cmd, 0, sizeof(link_cmd));
	iwx_mld_modify_link_fill(sc, in, &link_cmd,
	    IWX_LINK_CONTEXT_MODIFY_ACTIVE, 0);
	err = iwx_send_cmd_pdu(sc,
	    IWX_WIDE_ID(IWX_MAC_CONF_GROUP, IWX_LINK_CONFIG_CMD),
	    0, sizeof(link_cmd), &link_cmd);
	if (err)
		return err;

	memset(&link_cmd, 0, sizeof(link_cmd));
	link_cmd.link_id = htole32(0);
	link_cmd.spec_link_id = 0;
	link_cmd.action = IWX_FW_CTXT_ACTION_REMOVE;

	return iwx_send_cmd_pdu(sc,
	    IWX_WIDE_ID(IWX_MAC_CONF_GROUP, IWX_LINK_CONFIG_CMD),
	    0, sizeof(link_cmd), &link_cmd);
}

uint8_t
iwx_umac_scan_fill_channels(struct iwx_softc *sc,
    struct iwx_scan_channel_cfg_umac *chan, size_t chan_nitems,
    int n_ssids, uint32_t channel_cfg_flags)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ieee80211_channel *c;
	uint8_t nchan;

	for (nchan = 0, c = &ic->ic_channels[1];
	    c <= &ic->ic_channels[IEEE80211_CHAN_MAX] &&
	    nchan < chan_nitems &&
	    nchan < sc->sc_capa_n_scan_channels;
	    c++) {
		uint8_t channel_num;

		if (c->ic_flags == 0)
			continue;

		channel_num = ieee80211_mhz2ieee(c->ic_freq, 0);
		if (isset(sc->sc_ucode_api,
		    IWX_UCODE_TLV_API_SCAN_EXT_CHAN_VER)) {
			chan->v2.channel_num = channel_num;
			if (IEEE80211_IS_CHAN_2GHZ(c))
				chan->v2.band = IWX_PHY_BAND_24;
			else
				chan->v2.band = IWX_PHY_BAND_5;
			chan->v2.iter_count = 1;
			chan->v2.iter_interval = 0;
		} else {
			chan->v1.channel_num = channel_num;
			chan->v1.iter_count = 1;
			chan->v1.iter_interval = htole16(0);
		}

		chan->flags = htole32(channel_cfg_flags);
		chan++;
		nchan++;
	}

	return nchan;
}

int
iwx_fill_probe_req(struct iwx_softc *sc, struct iwx_scan_probe_req *preq)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ieee80211_frame *wh = (struct ieee80211_frame *)preq->buf;
	struct ieee80211_rateset *rs;
	size_t remain = sizeof(preq->buf);
	uint8_t *frm, *pos;

	memset(preq, 0, sizeof(*preq));

	if (remain < sizeof(*wh) + 2)
		return ENOBUFS;

	/*
	 * Build a probe request frame.  Most of the following code is a
	 * copy & paste of what is done in net80211.
	 */
	wh->i_fc[0] = IEEE80211_FC0_VERSION_0 | IEEE80211_FC0_TYPE_MGT |
	    IEEE80211_FC0_SUBTYPE_PROBE_REQ;
	wh->i_fc[1] = IEEE80211_FC1_DIR_NODS;
	IEEE80211_ADDR_COPY(wh->i_addr1, etherbroadcastaddr);
	IEEE80211_ADDR_COPY(wh->i_addr2, ic->ic_myaddr);
	IEEE80211_ADDR_COPY(wh->i_addr3, etherbroadcastaddr);
	*(uint16_t *)&wh->i_dur[0] = 0;	/* filled by HW */
	*(uint16_t *)&wh->i_seq[0] = 0;	/* filled by HW */

	frm = (uint8_t *)(wh + 1);
	*frm++ = IEEE80211_ELEMID_SSID;
	*frm++ = 0;
	/* hardware inserts SSID */

	/* Tell the firmware where the MAC header is. */
	preq->mac_header.offset = 0;
	preq->mac_header.len = htole16(frm - (uint8_t *)wh);
	remain -= frm - (uint8_t *)wh;

	/* Fill in 2GHz IEs and tell firmware where they are. */
	rs = &ic->ic_sup_rates[IEEE80211_MODE_11G];
	if (rs->rs_nrates > IEEE80211_RATE_SIZE) {
		if (remain < 4 + rs->rs_nrates)
			return ENOBUFS;
	} else if (remain < 2 + rs->rs_nrates)
		return ENOBUFS;
	preq->band_data[0].offset = htole16(frm - (uint8_t *)wh);
	pos = frm;
	frm = ieee80211_add_rates(frm, rs);
	if (rs->rs_nrates > IEEE80211_RATE_SIZE)
		frm = ieee80211_add_xrates(frm, rs);
	remain -= frm - pos;

	if (isset(sc->sc_enabled_capa,
	    IWX_UCODE_TLV_CAPA_DS_PARAM_SET_IE_SUPPORT)) {
		if (remain < 3)
			return ENOBUFS;
		*frm++ = IEEE80211_ELEMID_DSPARMS;
		*frm++ = 1;
		*frm++ = 0;
		remain -= 3;
	}
	preq->band_data[0].len = htole16(frm - pos);

	if (sc->sc_nvm.sku_cap_band_52GHz_enable) {
		/* Fill in 5GHz IEs. */
		rs = &ic->ic_sup_rates[IEEE80211_MODE_11A];
		if (rs->rs_nrates > IEEE80211_RATE_SIZE) {
			if (remain < 4 + rs->rs_nrates)
				return ENOBUFS;
		} else if (remain < 2 + rs->rs_nrates)
			return ENOBUFS;
		preq->band_data[1].offset = htole16(frm - (uint8_t *)wh);
		pos = frm;
		frm = ieee80211_add_rates(frm, rs);
		if (rs->rs_nrates > IEEE80211_RATE_SIZE)
			frm = ieee80211_add_xrates(frm, rs);
		preq->band_data[1].len = htole16(frm - pos);
		remain -= frm - pos;
		if (ic->ic_flags & IEEE80211_F_VHTON) {
			if (remain < 14)
				return ENOBUFS;
			frm = ieee80211_add_vhtcaps(frm, ic);
			remain -= frm - pos;
			preq->band_data[1].len = htole16(frm - pos);
		}
	}

	/* Send 11n IEs on both 2GHz and 5GHz bands. */
	preq->common_data.offset = htole16(frm - (uint8_t *)wh);
	pos = frm;
	if (ic->ic_flags & IEEE80211_F_HTON) {
		if (remain < 28)
			return ENOBUFS;
		frm = ieee80211_add_htcaps(frm, ic);
		/* XXX add WME info? */
		remain -= frm - pos;
	}

	preq->common_data.len = htole16(frm - pos);

	return 0;
}

int
iwx_config_umac_scan_reduced(struct iwx_softc *sc)
{
	struct iwx_scan_config scan_cfg;
	struct iwx_host_cmd hcmd = {
		.id = iwx_cmd_id(IWX_SCAN_CFG_CMD, IWX_LONG_GROUP, 0),
		.len[0] = sizeof(scan_cfg),
		.data[0] = &scan_cfg,
		.flags = 0,
	};
	int cmdver;

	if (!isset(sc->sc_ucode_api, IWX_UCODE_TLV_API_REDUCED_SCAN_CONFIG)) {
		printf("%s: firmware does not support reduced scan config\n",
		    DEVNAME(sc));
		return ENOTSUP;
	}

	memset(&scan_cfg, 0, sizeof(scan_cfg));

	/*
	 * SCAN_CFG version >= 5 implies that the broadcast
	 * STA ID field is deprecated.
	 */
	cmdver = iwx_lookup_cmd_ver(sc, IWX_LONG_GROUP, IWX_SCAN_CFG_CMD);
	if (cmdver == IWX_FW_CMD_VER_UNKNOWN || cmdver < 5)
		scan_cfg.bcast_sta_id = 0xff;

	scan_cfg.tx_chains = htole32(iwx_fw_valid_tx_ant(sc));
	scan_cfg.rx_chains = htole32(iwx_fw_valid_rx_ant(sc));

	return iwx_send_cmd(sc, &hcmd);
}

uint16_t
iwx_scan_umac_flags_v2(struct iwx_softc *sc, int bgscan)
{
	struct ieee80211com *ic = &sc->sc_ic;
	uint16_t flags = 0;

	if (ic->ic_des_esslen == 0)
		flags |= IWX_UMAC_SCAN_GEN_FLAGS_V2_FORCE_PASSIVE;

	flags |= IWX_UMAC_SCAN_GEN_FLAGS_V2_PASS_ALL;
	flags |= IWX_UMAC_SCAN_GEN_FLAGS_V2_NTFY_ITER_COMPLETE;
	flags |= IWX_UMAC_SCAN_GEN_FLAGS_V2_ADAPTIVE_DWELL;

	return flags;
}

#define IWX_SCAN_DWELL_ACTIVE		10
#define IWX_SCAN_DWELL_PASSIVE		110

/* adaptive dwell max budget time [TU] for full scan */
#define IWX_SCAN_ADWELL_MAX_BUDGET_FULL_SCAN 300
/* adaptive dwell max budget time [TU] for directed scan */
#define IWX_SCAN_ADWELL_MAX_BUDGET_DIRECTED_SCAN 100
/* adaptive dwell default high band APs number */
#define IWX_SCAN_ADWELL_DEFAULT_HB_N_APS 8
/* adaptive dwell default low band APs number */
#define IWX_SCAN_ADWELL_DEFAULT_LB_N_APS 2
/* adaptive dwell default APs number in social channels (1, 6, 11) */
#define IWX_SCAN_ADWELL_DEFAULT_N_APS_SOCIAL 10
/* adaptive dwell number of APs override for p2p friendly GO channels */
#define IWX_SCAN_ADWELL_N_APS_GO_FRIENDLY 10
/* adaptive dwell number of APs override for social channels */
#define IWX_SCAN_ADWELL_N_APS_SOCIAL_CHS 2

void
iwx_scan_umac_dwell_v10(struct iwx_softc *sc,
    struct iwx_scan_general_params_v10 *general_params, int bgscan)
{
	uint32_t suspend_time, max_out_time;
	uint8_t active_dwell, passive_dwell;

	active_dwell = IWX_SCAN_DWELL_ACTIVE;
	passive_dwell = IWX_SCAN_DWELL_PASSIVE;

	general_params->adwell_default_social_chn =
		IWX_SCAN_ADWELL_DEFAULT_N_APS_SOCIAL;
	general_params->adwell_default_2g = IWX_SCAN_ADWELL_DEFAULT_LB_N_APS;
	general_params->adwell_default_5g = IWX_SCAN_ADWELL_DEFAULT_HB_N_APS;

	if (bgscan)
		general_params->adwell_max_budget =
			htole16(IWX_SCAN_ADWELL_MAX_BUDGET_DIRECTED_SCAN);
	else
		general_params->adwell_max_budget =
			htole16(IWX_SCAN_ADWELL_MAX_BUDGET_FULL_SCAN);

	general_params->scan_priority = htole32(IWX_SCAN_PRIORITY_EXT_6);
	if (bgscan) {
		max_out_time = htole32(120);
		suspend_time = htole32(120);
	} else {
		max_out_time = htole32(0);
		suspend_time = htole32(0);
	}
	general_params->max_out_of_time[IWX_SCAN_LB_LMAC_IDX] =
		htole32(max_out_time);
	general_params->suspend_time[IWX_SCAN_LB_LMAC_IDX] =
		htole32(suspend_time);
	general_params->max_out_of_time[IWX_SCAN_HB_LMAC_IDX] =
		htole32(max_out_time);
	general_params->suspend_time[IWX_SCAN_HB_LMAC_IDX] =
		htole32(suspend_time);

	general_params->active_dwell[IWX_SCAN_LB_LMAC_IDX] = active_dwell;
	general_params->passive_dwell[IWX_SCAN_LB_LMAC_IDX] = passive_dwell;
	general_params->active_dwell[IWX_SCAN_HB_LMAC_IDX] = active_dwell;
	general_params->passive_dwell[IWX_SCAN_HB_LMAC_IDX] = passive_dwell;
}

void
iwx_scan_umac_fill_general_p_v10(struct iwx_softc *sc,
    struct iwx_scan_general_params_v10 *gp, uint16_t gen_flags, int bgscan)
{
	iwx_scan_umac_dwell_v10(sc, gp, bgscan);

	gp->flags = htole16(gen_flags);

	if (gen_flags & IWX_UMAC_SCAN_GEN_FLAGS_V2_FRAGMENTED_LMAC1)
		gp->num_of_fragments[IWX_SCAN_LB_LMAC_IDX] = 3;
	if (gen_flags & IWX_UMAC_SCAN_GEN_FLAGS_V2_FRAGMENTED_LMAC2)
		gp->num_of_fragments[IWX_SCAN_HB_LMAC_IDX] = 3;

	gp->scan_start_mac_id = 0;
}

void
iwx_scan_umac_fill_ch_p_v6(struct iwx_softc *sc,
    struct iwx_scan_channel_params_v6 *cp, uint32_t channel_cfg_flags,
    int n_ssid)
{
	cp->flags = IWX_SCAN_CHANNEL_FLAG_ENABLE_CHAN_ORDER;

	cp->count = iwx_umac_scan_fill_channels(sc, cp->channel_config,
	    nitems(cp->channel_config), n_ssid, channel_cfg_flags);

	cp->n_aps_override[0] = IWX_SCAN_ADWELL_N_APS_GO_FRIENDLY;
	cp->n_aps_override[1] = IWX_SCAN_ADWELL_N_APS_SOCIAL_CHS;
}

int
iwx_umac_scan_v14(struct iwx_softc *sc, int bgscan)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct iwx_host_cmd hcmd = {
		.id = iwx_cmd_id(IWX_SCAN_REQ_UMAC, IWX_LONG_GROUP, 0),
		.len = { 0, },
		.data = { NULL, },
		.flags = 0,
	};
	struct iwx_scan_req_umac_v14 *cmd;
	struct iwx_scan_req_params_v14 *scan_p;
	int err, async = bgscan, n_ssid = 0;
	uint16_t gen_flags;
	uint32_t bitmap_ssid = 0;

	cmd = malloc(sizeof(*cmd), M_DEVBUF,
	    (async ? M_NOWAIT : M_WAIT) | M_CANFAIL | M_ZERO);
	if (cmd == NULL)
		return ENOMEM;

	scan_p = &cmd->scan_params;

	cmd->ooc_priority = htole32(IWX_SCAN_PRIORITY_EXT_6);
	cmd->uid = htole32(0);

	gen_flags = iwx_scan_umac_flags_v2(sc, bgscan);
	iwx_scan_umac_fill_general_p_v10(sc, &scan_p->general_params,
	    gen_flags, bgscan);

	scan_p->periodic_params.schedule[0].interval = htole16(0);
	scan_p->periodic_params.schedule[0].iter_count = 1;

	err = iwx_fill_probe_req(sc, &scan_p->probe_params.preq);
	if (err) {
		free(cmd, M_DEVBUF, sizeof(*cmd));
		return err;
	}

	if (ic->ic_des_esslen != 0) {
		scan_p->probe_params.direct_scan[0].id = IEEE80211_ELEMID_SSID;
		scan_p->probe_params.direct_scan[0].len = ic->ic_des_esslen;
		memcpy(scan_p->probe_params.direct_scan[0].ssid,
		    ic->ic_des_essid, ic->ic_des_esslen);
		bitmap_ssid |= (1 << 0);
		n_ssid = 1;
	}

	iwx_scan_umac_fill_ch_p_v6(sc, &scan_p->channel_params, bitmap_ssid,
	    n_ssid);

	hcmd.len[0] = sizeof(*cmd);
	hcmd.data[0] = (void *)cmd;
	hcmd.flags |= async ? IWX_CMD_ASYNC : 0;

	err = iwx_send_cmd(sc, &hcmd);
	free(cmd, M_DEVBUF, sizeof(*cmd));
	return err;
}

void
iwx_mcc_update(struct iwx_softc *sc, struct iwx_mcc_chub_notif *notif)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ifnet *ifp = IC2IFP(ic);
	char alpha2[3];

	snprintf(alpha2, sizeof(alpha2), "%c%c",
	    (le16toh(notif->mcc) & 0xff00) >> 8, le16toh(notif->mcc) & 0xff);

	if (ifp->if_flags & IFF_DEBUG) {
		printf("%s: firmware has detected regulatory domain '%s' "
		    "(0x%x)\n", DEVNAME(sc), alpha2, le16toh(notif->mcc));
	}

	/* TODO: Schedule a task to send MCC_UPDATE_CMD? */
}

uint8_t
iwx_ridx2rate(struct ieee80211_rateset *rs, int ridx)
{
	int i;
	uint8_t rval;

	for (i = 0; i < rs->rs_nrates; i++) {
		rval = (rs->rs_rates[i] & IEEE80211_RATE_VAL);
		if (rval == iwx_rates[ridx].rate)
			return rs->rs_rates[i];
	}

	return 0;
}

int
iwx_rval2ridx(int rval)
{
	int ridx;

	for (ridx = 0; ridx < nitems(iwx_rates); ridx++) {
		if (iwx_rates[ridx].plcp == IWX_RATE_INVM_PLCP)
			continue;
		if (rval == iwx_rates[ridx].rate)
			break;
	}

	return ridx;
}

void
iwx_ack_rates(struct iwx_softc *sc, struct iwx_node *in, int *cck_rates,
    int *ofdm_rates)
{
	struct ieee80211_node *ni = &in->in_ni;
	struct ieee80211_rateset *rs = &ni->ni_rates;
	int lowest_present_ofdm = -1;
	int lowest_present_cck = -1;
	uint8_t cck = 0;
	uint8_t ofdm = 0;
	int i;

	if (ni->ni_chan == IEEE80211_CHAN_ANYC ||
	    IEEE80211_IS_CHAN_2GHZ(ni->ni_chan)) {
		for (i = IWX_FIRST_CCK_RATE; i < IWX_FIRST_OFDM_RATE; i++) {
			if ((iwx_ridx2rate(rs, i) & IEEE80211_RATE_BASIC) == 0)
				continue;
			cck |= (1 << i);
			if (lowest_present_cck == -1 || lowest_present_cck > i)
				lowest_present_cck = i;
		}
	}
	for (i = IWX_FIRST_OFDM_RATE; i <= IWX_LAST_NON_HT_RATE; i++) {
		if ((iwx_ridx2rate(rs, i) & IEEE80211_RATE_BASIC) == 0)
			continue;
		ofdm |= (1 << (i - IWX_FIRST_OFDM_RATE));
		if (lowest_present_ofdm == -1 || lowest_present_ofdm > i)
			lowest_present_ofdm = i;
	}

	/*
	 * Now we've got the basic rates as bitmaps in the ofdm and cck
	 * variables. This isn't sufficient though, as there might not
	 * be all the right rates in the bitmap. E.g. if the only basic
	 * rates are 5.5 Mbps and 11 Mbps, we still need to add 1 Mbps
	 * and 6 Mbps because the 802.11-2007 standard says in 9.6:
	 *
	 *    [...] a STA responding to a received frame shall transmit
	 *    its Control Response frame [...] at the highest rate in the
	 *    BSSBasicRateSet parameter that is less than or equal to the
	 *    rate of the immediately previous frame in the frame exchange
	 *    sequence ([...]) and that is of the same modulation class
	 *    ([...]) as the received frame. If no rate contained in the
	 *    BSSBasicRateSet parameter meets these conditions, then the
	 *    control frame sent in response to a received frame shall be
	 *    transmitted at the highest mandatory rate of the PHY that is
	 *    less than or equal to the rate of the received frame, and
	 *    that is of the same modulation class as the received frame.
	 *
	 * As a consequence, we need to add all mandatory rates that are
	 * lower than all of the basic rates to these bitmaps.
	 */

	if (IWX_RATE_24M_INDEX < lowest_present_ofdm)
		ofdm |= IWX_RATE_BIT_MSK(24) >> IWX_FIRST_OFDM_RATE;
	if (IWX_RATE_12M_INDEX < lowest_present_ofdm)
		ofdm |= IWX_RATE_BIT_MSK(12) >> IWX_FIRST_OFDM_RATE;
	/* 6M already there or needed so always add */
	ofdm |= IWX_RATE_BIT_MSK(6) >> IWX_FIRST_OFDM_RATE;

	/*
	 * CCK is a bit more complex with DSSS vs. HR/DSSS vs. ERP.
	 * Note, however:
	 *  - if no CCK rates are basic, it must be ERP since there must
	 *    be some basic rates at all, so they're OFDM => ERP PHY
	 *    (or we're in 5 GHz, and the cck bitmap will never be used)
	 *  - if 11M is a basic rate, it must be ERP as well, so add 5.5M
	 *  - if 5.5M is basic, 1M and 2M are mandatory
	 *  - if 2M is basic, 1M is mandatory
	 *  - if 1M is basic, that's the only valid ACK rate.
	 * As a consequence, it's not as complicated as it sounds, just add
	 * any lower rates to the ACK rate bitmap.
	 */
	if (IWX_RATE_11M_INDEX < lowest_present_cck)
		cck |= IWX_RATE_BIT_MSK(11) >> IWX_FIRST_CCK_RATE;
	if (IWX_RATE_5M_INDEX < lowest_present_cck)
		cck |= IWX_RATE_BIT_MSK(5) >> IWX_FIRST_CCK_RATE;
	if (IWX_RATE_2M_INDEX < lowest_present_cck)
		cck |= IWX_RATE_BIT_MSK(2) >> IWX_FIRST_CCK_RATE;
	/* 1M already there or needed so always add */
	cck |= IWX_RATE_BIT_MSK(1) >> IWX_FIRST_CCK_RATE;

	*cck_rates = cck;
	*ofdm_rates = ofdm;
}

void
iwx_mac_ctxt_cmd_common(struct iwx_softc *sc, struct iwx_node *in,
    struct iwx_mac_ctx_cmd *cmd, uint32_t action)
{
#define IWX_EXP2(x)	((1 << (x)) - 1)	/* CWmin = 2^ECWmin - 1 */
	struct ieee80211com *ic = &sc->sc_ic;
	struct ieee80211_node *ni = ic->ic_bss;
	int cck_ack_rates, ofdm_ack_rates;
	int i;

	cmd->id_and_color = htole32(IWX_FW_CMD_ID_AND_COLOR(in->in_id,
	    in->in_color));
	cmd->action = htole32(action);

	if (action == IWX_FW_CTXT_ACTION_REMOVE)
		return;

	if (ic->ic_opmode == IEEE80211_M_MONITOR)
		cmd->mac_type = htole32(IWX_FW_MAC_TYPE_LISTENER);
	else if (ic->ic_opmode == IEEE80211_M_STA)
		cmd->mac_type = htole32(IWX_FW_MAC_TYPE_BSS_STA);
	else
		panic("unsupported operating mode %d", ic->ic_opmode);
	cmd->tsf_id = htole32(IWX_TSF_ID_A);

	IEEE80211_ADDR_COPY(cmd->node_addr, ic->ic_myaddr);
	if (ic->ic_opmode == IEEE80211_M_MONITOR) {
		IEEE80211_ADDR_COPY(cmd->bssid_addr, etherbroadcastaddr);
		return;
	}

	IEEE80211_ADDR_COPY(cmd->bssid_addr, in->in_macaddr);
	iwx_ack_rates(sc, in, &cck_ack_rates, &ofdm_ack_rates);
	cmd->cck_rates = htole32(cck_ack_rates);
	cmd->ofdm_rates = htole32(ofdm_ack_rates);

	cmd->cck_short_preamble
	    = htole32((ic->ic_flags & IEEE80211_F_SHPREAMBLE)
	      ? IWX_MAC_FLG_SHORT_PREAMBLE : 0);
	cmd->short_slot
	    = htole32((ic->ic_flags & IEEE80211_F_SHSLOT)
	      ? IWX_MAC_FLG_SHORT_SLOT : 0);

	for (i = 0; i < EDCA_NUM_AC; i++) {
		struct ieee80211_edca_ac_params *ac = &ic->ic_edca_ac[i];
		int txf = iwx_ac_to_tx_fifo[i];

		cmd->ac[txf].cw_min = htole16(IWX_EXP2(ac->ac_ecwmin));
		cmd->ac[txf].cw_max = htole16(IWX_EXP2(ac->ac_ecwmax));
		cmd->ac[txf].aifsn = ac->ac_aifsn;
		cmd->ac[txf].fifos_mask = (1 << txf);
		cmd->ac[txf].edca_txop = htole16(ac->ac_txoplimit * 32);
	}
	if (ni->ni_flags & IEEE80211_NODE_QOS)
		cmd->qos_flags |= htole32(IWX_MAC_QOS_FLG_UPDATE_EDCA);

	if (ni->ni_flags & IEEE80211_NODE_HT) {
		enum ieee80211_htprot htprot =
		    (ni->ni_htop1 & IEEE80211_HTOP1_PROT_MASK);
		switch (htprot) {
		case IEEE80211_HTPROT_NONE:
			break;
		case IEEE80211_HTPROT_NONMEMBER:
		case IEEE80211_HTPROT_NONHT_MIXED:
			cmd->protection_flags |=
			    htole32(IWX_MAC_PROT_FLG_HT_PROT |
			    IWX_MAC_PROT_FLG_FAT_PROT);
			break;
		case IEEE80211_HTPROT_20MHZ:
			if (in->in_phyctxt &&
			    (in->in_phyctxt->sco == IEEE80211_HTOP0_SCO_SCA ||
			    in->in_phyctxt->sco == IEEE80211_HTOP0_SCO_SCB)) {
				cmd->protection_flags |=
				    htole32(IWX_MAC_PROT_FLG_HT_PROT |
				    IWX_MAC_PROT_FLG_FAT_PROT);
			}
			break;
		default:
			break;
		}

		cmd->qos_flags |= htole32(IWX_MAC_QOS_FLG_TGN);
	}
	if (ic->ic_flags & IEEE80211_F_USEPROT)
		cmd->protection_flags |= htole32(IWX_MAC_PROT_FLG_TGG_PROTECT);

	cmd->filter_flags = htole32(IWX_MAC_FILTER_ACCEPT_GRP);
#undef IWX_EXP2
}

void
iwx_mac_ctxt_cmd_fill_sta(struct iwx_softc *sc, struct iwx_node *in,
    struct iwx_mac_data_sta *sta, int assoc)
{
	struct ieee80211_node *ni = &in->in_ni;
	uint32_t dtim_off;
	uint64_t tsf;

	dtim_off = ni->ni_dtimcount * ni->ni_intval * IEEE80211_DUR_TU;
	memcpy(&tsf, ni->ni_tstamp, sizeof(tsf));
	tsf = letoh64(tsf);

	sta->is_assoc = htole32(assoc);
	if (assoc) {
		sta->dtim_time = htole32(ni->ni_rstamp + dtim_off);
		sta->dtim_tsf = htole64(tsf + dtim_off);
		sta->assoc_beacon_arrive_time = htole32(ni->ni_rstamp);
	}
	sta->bi = htole32(ni->ni_intval);
	sta->dtim_interval = htole32(ni->ni_intval * ni->ni_dtimperiod);
	sta->data_policy = htole32(0);
	sta->listen_interval = htole32(10);
	sta->assoc_id = htole32(ni->ni_associd);
}

int
iwx_mac_ctxt_cmd(struct iwx_softc *sc, struct iwx_node *in, uint32_t action,
    int assoc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ieee80211_node *ni = &in->in_ni;
	struct iwx_mac_ctx_cmd cmd;
	int active = (sc->sc_flags & IWX_FLAG_MAC_ACTIVE);

	if (action == IWX_FW_CTXT_ACTION_ADD && active)
		panic("MAC already added");
	if (action == IWX_FW_CTXT_ACTION_REMOVE && !active)
		panic("MAC already removed");

	if (sc->sc_use_mld_api)
		return iwx_mld_mac_ctxt_cmd(sc, in, action, assoc);

	memset(&cmd, 0, sizeof(cmd));

	iwx_mac_ctxt_cmd_common(sc, in, &cmd, action);

	if (action == IWX_FW_CTXT_ACTION_REMOVE) {
		return iwx_send_cmd_pdu(sc, IWX_MAC_CONTEXT_CMD, 0,
		    sizeof(cmd), &cmd);
	}

	if (ic->ic_opmode == IEEE80211_M_MONITOR) {
		cmd.filter_flags |= htole32(IWX_MAC_FILTER_IN_PROMISC |
		    IWX_MAC_FILTER_IN_CONTROL_AND_MGMT |
		    IWX_MAC_FILTER_ACCEPT_GRP |
		    IWX_MAC_FILTER_IN_BEACON |
		    IWX_MAC_FILTER_IN_PROBE_REQUEST |
		    IWX_MAC_FILTER_IN_CRC32);
	} else if (!assoc || !ni->ni_associd || !ni->ni_dtimperiod) {
		/*
		 * Allow beacons to pass through as long as we are not
		 * associated or we do not have dtim period information.
		 */
		cmd.filter_flags |= htole32(IWX_MAC_FILTER_IN_BEACON);
	}
	iwx_mac_ctxt_cmd_fill_sta(sc, in, &cmd.sta, assoc);
	return iwx_send_cmd_pdu(sc, IWX_MAC_CONTEXT_CMD, 0, sizeof(cmd), &cmd);
}

int
iwx_mld_mac_ctxt_cmd(struct iwx_softc *sc, struct iwx_node *in,
    uint32_t action, int assoc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ieee80211_node *ni = &in->in_ni;
	struct iwx_mac_config_cmd cmd;

	memset(&cmd, 0, sizeof(cmd));
	cmd.id_and_color = htole32(in->in_id);
	cmd.action = htole32(action);

	if (action == IWX_FW_CTXT_ACTION_REMOVE) {
		return iwx_send_cmd_pdu(sc,
		    IWX_WIDE_ID(IWX_MAC_CONF_GROUP, IWX_MAC_CONFIG_CMD),
		    0, sizeof(cmd), &cmd);
	}

	if (ic->ic_opmode == IEEE80211_M_MONITOR)
		cmd.mac_type = htole32(IWX_FW_MAC_TYPE_LISTENER);
	else if (ic->ic_opmode == IEEE80211_M_STA)
		cmd.mac_type = htole32(IWX_FW_MAC_TYPE_BSS_STA);
	else
		panic("unsupported operating mode %d", ic->ic_opmode);
	IEEE80211_ADDR_COPY(cmd.local_mld_addr, ic->ic_myaddr);
	cmd.client.assoc_id = htole32(ni->ni_associd);

	cmd.filter_flags = htole32(IWX_MAC_CFG_FILTER_ACCEPT_GRP);
	if (ic->ic_opmode == IEEE80211_M_MONITOR) {
		cmd.filter_flags |= htole32(IWX_MAC_CFG_FILTER_PROMISC |
		    IWX_MAC_FILTER_IN_CONTROL_AND_MGMT |
		    IWX_MAC_CFG_FILTER_ACCEPT_BEACON |
		    IWX_MAC_CFG_FILTER_ACCEPT_PROBE_REQ |
		    IWX_MAC_CFG_FILTER_ACCEPT_GRP);
	} else if (!assoc || !ni->ni_associd || !ni->ni_dtimperiod) {
		/*
		 * Allow beacons to pass through as long as we are not
		 * associated or we do not have dtim period information.
		 */
		cmd.filter_flags |= htole32(IWX_MAC_CFG_FILTER_ACCEPT_BEACON);
	}

	return iwx_send_cmd_pdu(sc,
	    IWX_WIDE_ID(IWX_MAC_CONF_GROUP, IWX_MAC_CONFIG_CMD),
	    0, sizeof(cmd), &cmd);
}

int
iwx_clear_statistics(struct iwx_softc *sc)
{
	struct iwx_statistics_cmd scmd = {
		.flags = htole32(IWX_STATISTICS_FLG_CLEAR)
	};
	struct iwx_host_cmd cmd = {
		.id = IWX_STATISTICS_CMD,
		.len[0] = sizeof(scmd),
		.data[0] = &scmd,
		.flags = IWX_CMD_WANT_RESP,
		.resp_pkt_len = sizeof(struct iwx_notif_statistics),
	};
	int err;

	err = iwx_send_cmd(sc, &cmd);
	if (err)
		return err;

	iwx_free_resp(sc, &cmd);
	return 0;
}

void
iwx_add_task(struct iwx_softc *sc, struct taskq *taskq, struct task *task)
{
	int s = splnet();

	if (sc->sc_flags & IWX_FLAG_SHUTDOWN) {
		splx(s);
		return;
	}

	refcnt_take(&sc->task_refs);
	if (!task_add(taskq, task))
		refcnt_rele_wake(&sc->task_refs);
	splx(s);
}

void
iwx_del_task(struct iwx_softc *sc, struct taskq *taskq, struct task *task)
{
	if (task_del(taskq, task))
		refcnt_rele(&sc->task_refs);
}

int
iwx_scan(struct iwx_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ifnet *ifp = IC2IFP(ic);
	int err;

	if (sc->sc_flags & IWX_FLAG_BGSCAN) {
		err = iwx_scan_abort(sc);
		if (err) {
			printf("%s: could not abort background scan\n",
			    DEVNAME(sc));
			return err;
		}
	}

	err = iwx_umac_scan_v14(sc, 0);
	if (err) {
		printf("%s: could not initiate scan\n", DEVNAME(sc));
		return err;
	}

	/*
	 * The current mode might have been fixed during association.
	 * Ensure all channels get scanned.
	 */
	if (IFM_MODE(ic->ic_media.ifm_cur->ifm_media) == IFM_AUTO)
		ieee80211_setmode(ic, IEEE80211_MODE_AUTO);

	sc->sc_flags |= IWX_FLAG_SCANNING;
	if (ifp->if_flags & IFF_DEBUG)
		printf("%s: %s -> %s\n", ifp->if_xname,
		    ieee80211_state_name[ic->ic_state],
		    ieee80211_state_name[IEEE80211_S_SCAN]);
	if ((sc->sc_flags & IWX_FLAG_BGSCAN) == 0) {
		ieee80211_set_link_state(ic, LINK_STATE_DOWN);
		ieee80211_node_cleanup(ic, ic->ic_bss);
	}
	ic->ic_state = IEEE80211_S_SCAN;
	wakeup(&ic->ic_state); /* wake iwx_init() */

	return 0;
}

int
iwx_bgscan(struct ieee80211com *ic)
{
	struct iwx_softc *sc = IC2IFP(ic)->if_softc;
	int err;

	if (sc->sc_flags & IWX_FLAG_SCANNING)
		return 0;

	err = iwx_umac_scan_v14(sc, 1);
	if (err) {
		printf("%s: could not initiate scan\n", DEVNAME(sc));
		return err;
	}

	sc->sc_flags |= IWX_FLAG_BGSCAN;
	return 0;
}

void
iwx_bgscan_done(struct ieee80211com *ic,
    struct ieee80211_node_switch_bss_arg *arg, size_t arg_size)
{
	struct iwx_softc *sc = ic->ic_softc;

	free(sc->bgscan_unref_arg, M_DEVBUF, sc->bgscan_unref_arg_size);
	sc->bgscan_unref_arg = arg;
	sc->bgscan_unref_arg_size = arg_size;
	iwx_add_task(sc, systq, &sc->bgscan_done_task);
}

void
iwx_bgscan_done_task(void *arg)
{
	struct iwx_softc *sc = arg;
	struct ieee80211com *ic = &sc->sc_ic;
	struct iwx_node *in = (void *)ic->ic_bss;
	struct ieee80211_node *ni = &in->in_ni;
	int tid, err = 0, s = splnet();

	if ((sc->sc_flags & IWX_FLAG_SHUTDOWN) ||
	    (ic->ic_flags & IEEE80211_F_BGSCAN) == 0 ||
	    ic->ic_state != IEEE80211_S_RUN) {
		err = ENXIO;
		goto done;
	}

	err = iwx_flush_sta(sc, in);
	if (err)
		goto done;

	for (tid = 0; tid < IWX_MAX_TID_COUNT; tid++) {
		int qid = IWX_FIRST_AGG_TX_QUEUE + tid;

		if (sc->aggqid[tid] == 0)
			continue;

		err = iwx_disable_txq(sc, IWX_STATION_ID, qid, tid);
		if (err)
			goto done;
#if 0 /* disabled for now; we are going to DEAUTH soon anyway */
		IEEE80211_SEND_ACTION(ic, ni, IEEE80211_CATEG_BA,
		    IEEE80211_ACTION_DELBA,
		    IEEE80211_REASON_AUTH_LEAVE << 16 |
		    IEEE80211_FC1_DIR_TODS << 8 | tid);
#endif
		ieee80211_node_tx_ba_clear(ni, tid);
		sc->aggqid[tid] = 0;
	}

	/*
	 * Tx queues have been flushed and Tx agg has been stopped.
	 * Allow roaming to proceed.
	 */
	ni->ni_unref_arg = sc->bgscan_unref_arg;
	ni->ni_unref_arg_size = sc->bgscan_unref_arg_size;
	sc->bgscan_unref_arg = NULL;
	sc->bgscan_unref_arg_size = 0;
	ieee80211_node_tx_stopped(ic, &in->in_ni);
done:
	if (err) {
		free(sc->bgscan_unref_arg, M_DEVBUF, sc->bgscan_unref_arg_size);
		sc->bgscan_unref_arg = NULL;
		sc->bgscan_unref_arg_size = 0;
		if ((sc->sc_flags & IWX_FLAG_SHUTDOWN) == 0)
			task_add(systq, &sc->init_task);
	}
	refcnt_rele_wake(&sc->task_refs);
	splx(s);
}

int
iwx_umac_scan_abort(struct iwx_softc *sc)
{
	struct iwx_umac_scan_abort cmd = { 0 };

	return iwx_send_cmd_pdu(sc,
	    IWX_WIDE_ID(IWX_LONG_GROUP, IWX_SCAN_ABORT_UMAC),
	    0, sizeof(cmd), &cmd);
}

int
iwx_scan_abort(struct iwx_softc *sc)
{
	int err;

	err = iwx_umac_scan_abort(sc);
	if (err == 0)
		sc->sc_flags &= ~(IWX_FLAG_SCANNING | IWX_FLAG_BGSCAN);
	return err;
}

int
iwx_enable_mgmt_queue(struct iwx_softc *sc)
{
	int err;

	sc->first_data_qid = IWX_DQA_CMD_QUEUE + 1;

	/*
	 * Non-QoS frames use the "MGMT" TID and queue.
	 * Other TIDs and data queues are reserved for QoS data frames.
	 */
	err = iwx_enable_txq(sc, IWX_STATION_ID, sc->first_data_qid,
	    IWX_MGMT_TID, IWX_TX_RING_COUNT);
	if (err) {
		printf("%s: could not enable Tx queue %d (error %d)\n",
		    DEVNAME(sc), sc->first_data_qid, err);
		return err;
	}

	return 0;
}

int
iwx_disable_mgmt_queue(struct iwx_softc *sc)
{
	int err, cmd_ver;

	/* Explicit removal is only required with old SCD_QUEUE_CFG command. */
	cmd_ver = iwx_lookup_cmd_ver(sc, IWX_DATA_PATH_GROUP,
	    IWX_SCD_QUEUE_CONFIG_CMD);
	if (cmd_ver == 0 || cmd_ver == IWX_FW_CMD_VER_UNKNOWN)
		return 0;

	sc->first_data_qid = IWX_DQA_CMD_QUEUE + 1;

	err = iwx_disable_txq(sc, IWX_STATION_ID, sc->first_data_qid,
	    IWX_MGMT_TID);
	if (err) {
		printf("%s: could not disable Tx queue %d (error %d)\n",
		    DEVNAME(sc), sc->first_data_qid, err);
		return err;
	}

	return 0;
}

int
iwx_rs_rval2idx(uint8_t rval)
{
	/* Firmware expects indices which match our 11g rate set. */
	const struct ieee80211_rateset *rs = &ieee80211_std_rateset_11g;
	int i;

	for (i = 0; i < rs->rs_nrates; i++) {
		if ((rs->rs_rates[i] & IEEE80211_RATE_VAL) == rval)
			return i;
	}

	return -1;
}

uint16_t
iwx_rs_ht_rates(struct iwx_softc *sc, struct ieee80211_node *ni, int rsidx)
{
	struct ieee80211com *ic = &sc->sc_ic;
	const struct ieee80211_ht_rateset *rs;
	uint16_t htrates = 0;
	int mcs;

	rs = &ieee80211_std_ratesets_11n[rsidx];
	for (mcs = rs->min_mcs; mcs <= rs->max_mcs; mcs++) {
		if (!isset(ni->ni_rxmcs, mcs) ||
		    !isset(ic->ic_sup_mcs, mcs))
			continue;
		htrates |= (1 << (mcs - rs->min_mcs));
	}

	return htrates;
}

uint16_t
iwx_rs_vht_rates(struct iwx_softc *sc, struct ieee80211_node *ni, int num_ss)
{
	uint16_t rx_mcs;
	int max_mcs = -1;

	rx_mcs = (ni->ni_vht_rxmcs & IEEE80211_VHT_MCS_FOR_SS_MASK(num_ss)) >>
	    IEEE80211_VHT_MCS_FOR_SS_SHIFT(num_ss);
	switch (rx_mcs) {
	case IEEE80211_VHT_MCS_SS_NOT_SUPP:
		break;
	case IEEE80211_VHT_MCS_0_7:
		max_mcs = 7;
		break;
	case IEEE80211_VHT_MCS_0_8:
		max_mcs = 8;
		break;
	case IEEE80211_VHT_MCS_0_9:
		/* Disable VHT MCS 9 for 20MHz-only stations. */
		if (!ieee80211_node_supports_ht_chan40(ni))
			max_mcs = 8;
		else
			max_mcs = 9;
		break;
	default:
		/* Should not happen; Values above cover the possible range. */
		panic("invalid VHT Rx MCS value %u", rx_mcs);
	}

	return ((1 << (max_mcs + 1)) - 1);
}

int
iwx_rs_init_v3(struct iwx_softc *sc, struct iwx_node *in)
{
	struct ieee80211_node *ni = &in->in_ni;
	struct ieee80211_rateset *rs = &ni->ni_rates;
	struct iwx_tlc_config_cmd_v3 cfg_cmd;
	uint32_t cmd_id;
	int i;
	size_t cmd_size = sizeof(cfg_cmd);

	memset(&cfg_cmd, 0, sizeof(cfg_cmd));

	for (i = 0; i < rs->rs_nrates; i++) {
		uint8_t rval = rs->rs_rates[i] & IEEE80211_RATE_VAL;
		int idx = iwx_rs_rval2idx(rval);
		if (idx == -1)
			return EINVAL;
		cfg_cmd.non_ht_rates |= (1 << idx);
	}

	if (ni->ni_flags & IEEE80211_NODE_VHT) {
		cfg_cmd.mode = IWX_TLC_MNG_MODE_VHT;
		cfg_cmd.ht_rates[IWX_TLC_NSS_1][IWX_TLC_MCS_PER_BW_80] =
		    htole16(iwx_rs_vht_rates(sc, ni, 1));
		cfg_cmd.ht_rates[IWX_TLC_NSS_2][IWX_TLC_MCS_PER_BW_80] =
		    htole16(iwx_rs_vht_rates(sc, ni, 2));
	} else if (ni->ni_flags & IEEE80211_NODE_HT) {
		cfg_cmd.mode = IWX_TLC_MNG_MODE_HT;
		cfg_cmd.ht_rates[IWX_TLC_NSS_1][IWX_TLC_MCS_PER_BW_80] =
		    htole16(iwx_rs_ht_rates(sc, ni,
		    IEEE80211_HT_RATESET_SISO));
		cfg_cmd.ht_rates[IWX_TLC_NSS_2][IWX_TLC_MCS_PER_BW_80] =
		    htole16(iwx_rs_ht_rates(sc, ni,
		    IEEE80211_HT_RATESET_MIMO2));
	} else
		cfg_cmd.mode = IWX_TLC_MNG_MODE_NON_HT;

	cfg_cmd.sta_id = IWX_STATION_ID;
	if (in->in_phyctxt->vht_chan_width == IEEE80211_VHTOP0_CHAN_WIDTH_80)
		cfg_cmd.max_ch_width = IWX_TLC_MNG_CH_WIDTH_80MHZ;
	else if (in->in_phyctxt->sco == IEEE80211_HTOP0_SCO_SCA ||
	    in->in_phyctxt->sco == IEEE80211_HTOP0_SCO_SCB)
		cfg_cmd.max_ch_width = IWX_TLC_MNG_CH_WIDTH_40MHZ;
	else
		cfg_cmd.max_ch_width = IWX_TLC_MNG_CH_WIDTH_20MHZ;
	cfg_cmd.chains = IWX_TLC_MNG_CHAIN_A_MSK | IWX_TLC_MNG_CHAIN_B_MSK;
	if (ni->ni_flags & IEEE80211_NODE_VHT)
		cfg_cmd.max_mpdu_len = htole16(3895);
	else
		cfg_cmd.max_mpdu_len = htole16(3839);
	if (ni->ni_flags & IEEE80211_NODE_HT) {
		if (ieee80211_node_supports_ht_sgi20(ni)) {
			cfg_cmd.sgi_ch_width_supp |= (1 <<
			    IWX_TLC_MNG_CH_WIDTH_20MHZ);
		}
		if (ieee80211_node_supports_ht_sgi40(ni)) {
			cfg_cmd.sgi_ch_width_supp |= (1 <<
			    IWX_TLC_MNG_CH_WIDTH_40MHZ);
		}
	}
	if ((ni->ni_flags & IEEE80211_NODE_VHT) &&
	    ieee80211_node_supports_vht_sgi80(ni))
		cfg_cmd.sgi_ch_width_supp |= (1 << IWX_TLC_MNG_CH_WIDTH_80MHZ);

	cmd_id = iwx_cmd_id(IWX_TLC_MNG_CONFIG_CMD, IWX_DATA_PATH_GROUP, 0);
	return iwx_send_cmd_pdu(sc, cmd_id, IWX_CMD_ASYNC, cmd_size, &cfg_cmd);
}

int
iwx_rs_init_v4(struct iwx_softc *sc, struct iwx_node *in)
{
	struct ieee80211_node *ni = &in->in_ni;
	struct ieee80211_rateset *rs = &ni->ni_rates;
	struct iwx_tlc_config_cmd_v4 cfg_cmd;
	uint32_t cmd_id;
	int i;
	size_t cmd_size = sizeof(cfg_cmd);

	memset(&cfg_cmd, 0, sizeof(cfg_cmd));

	for (i = 0; i < rs->rs_nrates; i++) {
		uint8_t rval = rs->rs_rates[i] & IEEE80211_RATE_VAL;
		int idx = iwx_rs_rval2idx(rval);
		if (idx == -1)
			return EINVAL;
		cfg_cmd.non_ht_rates |= (1 << idx);
	}

	if (ni->ni_flags & IEEE80211_NODE_VHT) {
		cfg_cmd.mode = IWX_TLC_MNG_MODE_VHT;
		cfg_cmd.ht_rates[IWX_TLC_NSS_1][IWX_TLC_MCS_PER_BW_80] =
		    htole16(iwx_rs_vht_rates(sc, ni, 1));
		cfg_cmd.ht_rates[IWX_TLC_NSS_2][IWX_TLC_MCS_PER_BW_80] =
		    htole16(iwx_rs_vht_rates(sc, ni, 2));
	} else if (ni->ni_flags & IEEE80211_NODE_HT) {
		cfg_cmd.mode = IWX_TLC_MNG_MODE_HT;
		cfg_cmd.ht_rates[IWX_TLC_NSS_1][IWX_TLC_MCS_PER_BW_80] =
		    htole16(iwx_rs_ht_rates(sc, ni,
		    IEEE80211_HT_RATESET_SISO));
		cfg_cmd.ht_rates[IWX_TLC_NSS_2][IWX_TLC_MCS_PER_BW_80] =
		    htole16(iwx_rs_ht_rates(sc, ni,
		    IEEE80211_HT_RATESET_MIMO2));
	} else
		cfg_cmd.mode = IWX_TLC_MNG_MODE_NON_HT;

	cfg_cmd.sta_id = IWX_STATION_ID;
	if (in->in_phyctxt->vht_chan_width == IEEE80211_VHTOP0_CHAN_WIDTH_80)
		cfg_cmd.max_ch_width = IWX_TLC_MNG_CH_WIDTH_80MHZ;
	else if (in->in_phyctxt->sco == IEEE80211_HTOP0_SCO_SCA ||
	    in->in_phyctxt->sco == IEEE80211_HTOP0_SCO_SCB)
		cfg_cmd.max_ch_width = IWX_TLC_MNG_CH_WIDTH_40MHZ;
	else
		cfg_cmd.max_ch_width = IWX_TLC_MNG_CH_WIDTH_20MHZ;
	cfg_cmd.chains = IWX_TLC_MNG_CHAIN_A_MSK | IWX_TLC_MNG_CHAIN_B_MSK;
	if (ni->ni_flags & IEEE80211_NODE_VHT)
		cfg_cmd.max_mpdu_len = htole16(3895);
	else
		cfg_cmd.max_mpdu_len = htole16(3839);
	if (ni->ni_flags & IEEE80211_NODE_HT) {
		if (ieee80211_node_supports_ht_sgi20(ni)) {
			cfg_cmd.sgi_ch_width_supp |= (1 <<
			    IWX_TLC_MNG_CH_WIDTH_20MHZ);
		}
		if (ieee80211_node_supports_ht_sgi40(ni)) {
			cfg_cmd.sgi_ch_width_supp |= (1 <<
			    IWX_TLC_MNG_CH_WIDTH_40MHZ);
		}
	}
	if ((ni->ni_flags & IEEE80211_NODE_VHT) &&
	    ieee80211_node_supports_vht_sgi80(ni))
		cfg_cmd.sgi_ch_width_supp |= (1 << IWX_TLC_MNG_CH_WIDTH_80MHZ);

	cmd_id = iwx_cmd_id(IWX_TLC_MNG_CONFIG_CMD, IWX_DATA_PATH_GROUP, 0);
	return iwx_send_cmd_pdu(sc, cmd_id, IWX_CMD_ASYNC, cmd_size, &cfg_cmd);
}

int
iwx_rs_init(struct iwx_softc *sc, struct iwx_node *in)
{
	int cmd_ver;

	cmd_ver = iwx_lookup_cmd_ver(sc, IWX_DATA_PATH_GROUP,
	    IWX_TLC_MNG_CONFIG_CMD);
	if (cmd_ver == 4)
		return iwx_rs_init_v4(sc, in);
	return iwx_rs_init_v3(sc, in);
}

void
iwx_rs_update(struct iwx_softc *sc, struct iwx_tlc_update_notif *notif)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ieee80211_node *ni = ic->ic_bss;
	struct ieee80211_rateset *rs = &ni->ni_rates;
	uint32_t rate_n_flags;
	uint8_t plcp, rval;
	int i, cmd_ver, rate_n_flags_ver2 = 0;

	if (notif->sta_id != IWX_STATION_ID ||
	    (le32toh(notif->flags) & IWX_TLC_NOTIF_FLAG_RATE) == 0)
		return;

	rate_n_flags = le32toh(notif->rate);

	cmd_ver = iwx_lookup_notif_ver(sc, IWX_DATA_PATH_GROUP,
	    IWX_TLC_MNG_UPDATE_NOTIF);
	if (cmd_ver != IWX_FW_CMD_VER_UNKNOWN && cmd_ver >= 3)
		rate_n_flags_ver2 = 1;
	if (rate_n_flags_ver2) {
		uint32_t mod_type = (rate_n_flags & IWX_RATE_MCS_MOD_TYPE_MSK);
		if (mod_type == IWX_RATE_MCS_VHT_MSK) {
			ni->ni_txmcs = (rate_n_flags &
			    IWX_RATE_HT_MCS_CODE_MSK);
			ni->ni_vht_ss = ((rate_n_flags &
			    IWX_RATE_MCS_NSS_MSK) >>
			    IWX_RATE_MCS_NSS_POS) + 1;
			return;
		} else if (mod_type == IWX_RATE_MCS_HT_MSK) {
			ni->ni_txmcs = IWX_RATE_HT_MCS_INDEX(rate_n_flags);
			return;
		}
	} else {
		if (rate_n_flags & IWX_RATE_MCS_VHT_MSK_V1) {
			ni->ni_txmcs = (rate_n_flags &
			    IWX_RATE_VHT_MCS_RATE_CODE_MSK);
			ni->ni_vht_ss = ((rate_n_flags &
			    IWX_RATE_VHT_MCS_NSS_MSK) >>
			    IWX_RATE_VHT_MCS_NSS_POS) + 1;
			return;
		} else if (rate_n_flags & IWX_RATE_MCS_HT_MSK_V1) {
			ni->ni_txmcs = (rate_n_flags &
			    (IWX_RATE_HT_MCS_RATE_CODE_MSK_V1 |
			    IWX_RATE_HT_MCS_NSS_MSK_V1));
			return;
		}
	}

	if (rate_n_flags_ver2) {
		const struct ieee80211_rateset *rs;
		uint32_t ridx = (rate_n_flags & IWX_RATE_LEGACY_RATE_MSK);
		if (rate_n_flags & IWX_RATE_MCS_LEGACY_OFDM_MSK)
			rs = &ieee80211_std_rateset_11a;
		else
			rs = &ieee80211_std_rateset_11b;
		if (ridx < rs->rs_nrates)
			rval = (rs->rs_rates[ridx] & IEEE80211_RATE_VAL);
		else
			rval = 0;
	} else {
		plcp = (rate_n_flags & IWX_RATE_LEGACY_RATE_MSK_V1);

		rval = 0;
		for (i = IWX_RATE_1M_INDEX; i < nitems(iwx_rates); i++) {
			if (iwx_rates[i].plcp == plcp) {
				rval = iwx_rates[i].rate;
				break;
			}
		}
	}

	if (rval) {
		uint8_t rv;
		for (i = 0; i < rs->rs_nrates; i++) {
			rv = rs->rs_rates[i] & IEEE80211_RATE_VAL;
			if (rv == rval) {
				ni->ni_txrate = i;
				break;
			}
		}
	}
}

int
iwx_phy_send_rlc(struct iwx_softc *sc, struct iwx_phy_ctxt *phyctxt,
    uint8_t chains_static, uint8_t chains_dynamic)
{
	struct iwx_rlc_config_cmd cmd;
	uint32_t cmd_id;
	uint8_t active_cnt, idle_cnt;

	memset(&cmd, 0, sizeof(cmd));

	idle_cnt = chains_static;
	active_cnt = chains_dynamic;

	cmd.phy_id = htole32(phyctxt->id);
	cmd.rlc.rx_chain_info = htole32(iwx_fw_valid_rx_ant(sc) <<
	    IWX_PHY_RX_CHAIN_VALID_POS);
	cmd.rlc.rx_chain_info |= htole32(idle_cnt << IWX_PHY_RX_CHAIN_CNT_POS);
	cmd.rlc.rx_chain_info |= htole32(active_cnt <<
	    IWX_PHY_RX_CHAIN_MIMO_CNT_POS);

	cmd_id = iwx_cmd_id(IWX_RLC_CONFIG_CMD, IWX_DATA_PATH_GROUP, 2);
	return iwx_send_cmd_pdu(sc, cmd_id, 0, sizeof(cmd), &cmd);
}

int
iwx_phy_ctxt_update(struct iwx_softc *sc, struct iwx_phy_ctxt *phyctxt,
    struct ieee80211_channel *chan, uint8_t chains_static,
    uint8_t chains_dynamic, uint32_t apply_time, uint8_t sco,
    uint8_t vht_chan_width)
{
	uint16_t band_flags = (IEEE80211_CHAN_2GHZ | IEEE80211_CHAN_5GHZ);
	int err;

	if (isset(sc->sc_enabled_capa,
	    IWX_UCODE_TLV_CAPA_BINDING_CDB_SUPPORT) &&
	    (phyctxt->channel->ic_flags & band_flags) !=
	    (chan->ic_flags & band_flags)) {
		err = iwx_phy_ctxt_cmd(sc, phyctxt, chains_static,
		    chains_dynamic, IWX_FW_CTXT_ACTION_REMOVE, apply_time, sco,
		    vht_chan_width);
		if (err) {
			printf("%s: could not remove PHY context "
			    "(error %d)\n", DEVNAME(sc), err);
			return err;
		}
		phyctxt->channel = chan;
		err = iwx_phy_ctxt_cmd(sc, phyctxt, chains_static,
		    chains_dynamic, IWX_FW_CTXT_ACTION_ADD, apply_time, sco,
		    vht_chan_width);
		if (err) {
			printf("%s: could not add PHY context "
			    "(error %d)\n", DEVNAME(sc), err);
			return err;
		}
	} else {
		phyctxt->channel = chan;
		err = iwx_phy_ctxt_cmd(sc, phyctxt, chains_static,
		    chains_dynamic, IWX_FW_CTXT_ACTION_MODIFY, apply_time, sco,
		    vht_chan_width);
		if (err) {
			printf("%s: could not update PHY context (error %d)\n",
			    DEVNAME(sc), err);
			return err;
		}
	}

	phyctxt->sco = sco;
	phyctxt->vht_chan_width = vht_chan_width;

	if (iwx_lookup_cmd_ver(sc, IWX_DATA_PATH_GROUP,
	    IWX_RLC_CONFIG_CMD) == 2)
		return iwx_phy_send_rlc(sc, phyctxt,
		    chains_static, chains_dynamic);

	return 0;
}

int
iwx_auth(struct iwx_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct iwx_node *in = (void *)ic->ic_bss;
	uint32_t duration;
	int generation = sc->sc_generation, err;

	splassert(IPL_NET);

	if (ic->ic_opmode == IEEE80211_M_MONITOR) {
		err = iwx_phy_ctxt_update(sc, &sc->sc_phyctxt[0],
		    ic->ic_ibss_chan, 1, 1, 0, IEEE80211_HTOP0_SCO_SCN,
		    IEEE80211_VHTOP0_CHAN_WIDTH_HT);
		if (err)
			return err;
	} else {
		err = iwx_phy_ctxt_update(sc, &sc->sc_phyctxt[0],
		    in->in_ni.ni_chan, 1, 1, 0, IEEE80211_HTOP0_SCO_SCN,
		    IEEE80211_VHTOP0_CHAN_WIDTH_HT);
		if (err)
			return err;
	}
	in->in_phyctxt = &sc->sc_phyctxt[0];
	IEEE80211_ADDR_COPY(in->in_macaddr, in->in_ni.ni_macaddr);

	err = iwx_mac_ctxt_cmd(sc, in, IWX_FW_CTXT_ACTION_ADD, 0);
	if (err) {
		printf("%s: could not add MAC context (error %d)\n",
		    DEVNAME(sc), err);
		return err;
 	}
	sc->sc_flags |= IWX_FLAG_MAC_ACTIVE;

	err = iwx_binding_cmd(sc, in, IWX_FW_CTXT_ACTION_ADD);
	if (err) {
		printf("%s: could not add binding (error %d)\n",
		    DEVNAME(sc), err);
		goto rm_mac_ctxt;
	}
	sc->sc_flags |= IWX_FLAG_BINDING_ACTIVE;

	err = iwx_add_sta_cmd(sc, in, 0);
	if (err) {
		printf("%s: could not add sta (error %d)\n",
		    DEVNAME(sc), err);
		goto rm_binding;
	}
	sc->sc_flags |= IWX_FLAG_STA_ACTIVE;

	if (ic->ic_opmode == IEEE80211_M_MONITOR) {
		err = iwx_enable_txq(sc, IWX_MONITOR_STA_ID,
		    IWX_DQA_INJECT_MONITOR_QUEUE, IWX_MGMT_TID,
		    IWX_TX_RING_COUNT);
		if (err)
			goto rm_sta;
		return 0;
	}

	err = iwx_enable_mgmt_queue(sc);
	if (err)
		goto rm_sta;

	err = iwx_clear_statistics(sc);
	if (err)
		goto rm_mgmt_queue;

	/*
	 * Prevent the FW from wandering off channel during association
	 * by "protecting" the session with a time event.
	 */
	if (in->in_ni.ni_intval)
		duration = in->in_ni.ni_intval * 9;
	else
		duration = 900;
	return iwx_schedule_session_protection(sc, in, duration);
rm_mgmt_queue:
	if (generation == sc->sc_generation)
		iwx_disable_mgmt_queue(sc);
rm_sta:
	if (generation == sc->sc_generation) {
		iwx_rm_sta_cmd(sc, in);
		sc->sc_flags &= ~IWX_FLAG_STA_ACTIVE;
	}
rm_binding:
	if (generation == sc->sc_generation) {
		iwx_binding_cmd(sc, in, IWX_FW_CTXT_ACTION_REMOVE);
		sc->sc_flags &= ~IWX_FLAG_BINDING_ACTIVE;
	}
rm_mac_ctxt:
	if (generation == sc->sc_generation) {
		iwx_mac_ctxt_cmd(sc, in, IWX_FW_CTXT_ACTION_REMOVE, 0);
		sc->sc_flags &= ~IWX_FLAG_MAC_ACTIVE;
	}
	return err;
}

int
iwx_deauth(struct iwx_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct iwx_node *in = (void *)ic->ic_bss;
	int err;

	splassert(IPL_NET);

	iwx_unprotect_session(sc, in);

	if (sc->sc_flags & IWX_FLAG_STA_ACTIVE) {
		err = iwx_rm_sta(sc, in);
		if (err)
			return err;
		sc->sc_flags &= ~IWX_FLAG_STA_ACTIVE;
	}

	if (sc->sc_flags & IWX_FLAG_BINDING_ACTIVE) {
		err = iwx_binding_cmd(sc, in, IWX_FW_CTXT_ACTION_REMOVE);
		if (err) {
			printf("%s: could not remove binding (error %d)\n",
			    DEVNAME(sc), err);
			return err;
		}
		sc->sc_flags &= ~IWX_FLAG_BINDING_ACTIVE;
	}

	if (sc->sc_flags & IWX_FLAG_MAC_ACTIVE) {
		err = iwx_mac_ctxt_cmd(sc, in, IWX_FW_CTXT_ACTION_REMOVE, 0);
		if (err) {
			printf("%s: could not remove MAC context (error %d)\n",
			    DEVNAME(sc), err);
			return err;
		}
		sc->sc_flags &= ~IWX_FLAG_MAC_ACTIVE;
	}

	/* Move unused PHY context to a default channel. */
	err = iwx_phy_ctxt_update(sc, &sc->sc_phyctxt[0],
	    &ic->ic_channels[1], 1, 1, 0, IEEE80211_HTOP0_SCO_SCN,
	    IEEE80211_VHTOP0_CHAN_WIDTH_HT);
	if (err)
		return err;

	return 0;
}

int
iwx_run(struct iwx_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct iwx_node *in = (void *)ic->ic_bss;
	struct ieee80211_node *ni = &in->in_ni;
	int err;

	splassert(IPL_NET);

	if (ic->ic_opmode == IEEE80211_M_MONITOR) {
		/* Add a MAC context and a sniffing STA. */
		err = iwx_auth(sc);
		if (err)
			return err;
	}

	/* Configure Rx chains for MIMO and configure 40 MHz channel. */
	if (ic->ic_opmode == IEEE80211_M_MONITOR) {
		uint8_t chains = iwx_mimo_enabled(sc) ? 2 : 1;
		err = iwx_phy_ctxt_update(sc, in->in_phyctxt,
		    in->in_phyctxt->channel, chains, chains,
		    0, IEEE80211_HTOP0_SCO_SCN,
		    IEEE80211_VHTOP0_CHAN_WIDTH_HT);
		if (err) {
			printf("%s: failed to update PHY\n", DEVNAME(sc));
			return err;
		}
	} else if (ni->ni_flags & IEEE80211_NODE_HT) {
		uint8_t chains = iwx_mimo_enabled(sc) ? 2 : 1;
		uint8_t sco, vht_chan_width;
		if (IEEE80211_CHAN_40MHZ_ALLOWED(in->in_ni.ni_chan) &&
		    ieee80211_node_supports_ht_chan40(ni))
			sco = (ni->ni_htop0 & IEEE80211_HTOP0_SCO_MASK);
		else
			sco = IEEE80211_HTOP0_SCO_SCN;
		if ((ni->ni_flags & IEEE80211_NODE_VHT) &&
		    IEEE80211_CHAN_80MHZ_ALLOWED(in->in_ni.ni_chan) &&
		    ieee80211_node_supports_vht_chan80(ni))
			vht_chan_width = IEEE80211_VHTOP0_CHAN_WIDTH_80;
		else
			vht_chan_width = IEEE80211_VHTOP0_CHAN_WIDTH_HT;
		err = iwx_phy_ctxt_update(sc, in->in_phyctxt,
		    in->in_phyctxt->channel, chains, chains,
		    0, sco, vht_chan_width);
		if (err) {
			printf("%s: failed to update PHY\n", DEVNAME(sc));
			return err;
		}
	}

	/* Update STA again to apply HT and VHT settings. */
	err = iwx_add_sta_cmd(sc, in, 1);
	if (err) {
		printf("%s: could not update STA (error %d)\n",
		    DEVNAME(sc), err);
		return err;
	}

	/* We have now been assigned an associd by the AP. */
	err = iwx_mac_ctxt_cmd(sc, in, IWX_FW_CTXT_ACTION_MODIFY, 1);
	if (err) {
		printf("%s: failed to update MAC\n", DEVNAME(sc));
		return err;
	}

	err = iwx_sf_config(sc, IWX_SF_FULL_ON);
	if (err) {
		printf("%s: could not set sf full on (error %d)\n",
		    DEVNAME(sc), err);
		return err;
	}

	err = iwx_allow_mcast(sc);
	if (err) {
		printf("%s: could not allow mcast (error %d)\n",
		    DEVNAME(sc), err);
		return err;
	}

	err = iwx_power_update_device(sc);
	if (err) {
		printf("%s: could not send power command (error %d)\n",
		    DEVNAME(sc), err);
		return err;
	}
#ifdef notyet
	/*
	 * Disabled for now. Default beacon filter settings
	 * prevent net80211 from getting ERP and HT protection
	 * updates from beacons.
	 */
	err = iwx_enable_beacon_filter(sc, in);
	if (err) {
		printf("%s: could not enable beacon filter\n",
		    DEVNAME(sc));
		return err;
	}
#endif
	if (ic->ic_opmode == IEEE80211_M_MONITOR)
		return 0;

	err = iwx_power_mac_update_mode(sc, in);
	if (err) {
		printf("%s: could not update MAC power (error %d)\n",
		    DEVNAME(sc), err);
		return err;
	}

	/* Start at lowest available bit-rate. Firmware will raise. */
	in->in_ni.ni_txrate = 0;
	in->in_ni.ni_txmcs = 0;

	err = iwx_rs_init(sc, in);
	if (err) {
		printf("%s: could not init rate scaling (error %d)\n",
		    DEVNAME(sc), err);
		return err;
	}

	return 0;
}

int
iwx_run_stop(struct iwx_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct iwx_node *in = (void *)ic->ic_bss;
	struct ieee80211_node *ni = &in->in_ni;
	int err, i;

	splassert(IPL_NET);

	err = iwx_flush_sta(sc, in);
	if (err) {
		printf("%s: could not flush Tx path (error %d)\n",
		    DEVNAME(sc), err);
		return err;
	}

	/*
	 * Stop Rx BA sessions now. We cannot rely on the BA task
	 * for this when moving out of RUN state since it runs in a
	 * separate thread.
	 * Note that in->in_ni (struct ieee80211_node) already represents
	 * our new access point in case we are roaming between APs.
	 * This means we cannot rely on struct ieee802111_node to tell
	 * us which BA sessions exist.
	 */
	for (i = 0; i < nitems(sc->sc_rxba_data); i++) {
		struct iwx_rxba_data *rxba = &sc->sc_rxba_data[i];
		if (rxba->baid == IWX_RX_REORDER_DATA_INVALID_BAID)
			continue;
		iwx_sta_rx_agg(sc, ni, rxba->tid, 0, 0, 0, 0);
	}

	err = iwx_sf_config(sc, IWX_SF_INIT_OFF);
	if (err)
		return err;

	err = iwx_disable_beacon_filter(sc);
	if (err) {
		printf("%s: could not disable beacon filter (error %d)\n",
		    DEVNAME(sc), err);
		return err;
	}

	/* Mark station as disassociated. */
	err = iwx_mac_ctxt_cmd(sc, in, IWX_FW_CTXT_ACTION_MODIFY, 0);
	if (err) {
		printf("%s: failed to update MAC\n", DEVNAME(sc));
		return err;
	}

	return 0;
}

struct ieee80211_node *
iwx_node_alloc(struct ieee80211com *ic)
{
	return malloc(sizeof (struct iwx_node), M_DEVBUF, M_NOWAIT | M_ZERO);
}

int
iwx_set_key(struct ieee80211com *ic, struct ieee80211_node *ni,
    struct ieee80211_key *k)
{
	struct iwx_softc *sc = ic->ic_softc;
	struct iwx_node *in = (void *)ni;
	struct iwx_setkey_task_arg *a;
	int err;

	if (k->k_cipher != IEEE80211_CIPHER_CCMP) {
		/* Fallback to software crypto for other ciphers. */
		err = ieee80211_set_key(ic, ni, k);
		if (!err && in != NULL && (k->k_flags & IEEE80211_KEY_GROUP))
			in->in_flags |= IWX_NODE_FLAG_HAVE_GROUP_KEY;
		return err;
	}

	if (sc->setkey_nkeys >= nitems(sc->setkey_arg))
		return ENOSPC;

	a = &sc->setkey_arg[sc->setkey_cur];
	a->sta_id = IWX_STATION_ID;
	a->ni = ni;
	a->k = k;
	sc->setkey_cur = (sc->setkey_cur + 1) % nitems(sc->setkey_arg);
	sc->setkey_nkeys++;
	iwx_add_task(sc, systq, &sc->setkey_task);
	return EBUSY;
}

int
iwx_mld_add_sta_key_cmd(struct iwx_softc *sc, int sta_id,
    struct ieee80211_node *ni, struct ieee80211_key *k)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct iwx_sec_key_cmd cmd;
	uint32_t flags = IWX_SEC_KEY_FLAG_CIPHER_CCMP;
	int err;

	if (k->k_flags & IEEE80211_KEY_GROUP)
		flags |= IWX_SEC_KEY_FLAG_MCAST_KEY;

	memset(&cmd, 0, sizeof(cmd));
	cmd.u.add.sta_mask = htole32(1 << sta_id);
	cmd.u.add.key_id = htole32(k->k_id);
	cmd.u.add.key_flags = htole32(flags);
	cmd.u.add.tx_seq = htole64(k->k_tsc);
	memcpy(cmd.u.add.key, k->k_key, k->k_len);
	cmd.action = IWX_FW_CTXT_ACTION_ADD;

	err = iwx_send_cmd_pdu(sc,
	    IWX_WIDE_ID(IWX_DATA_PATH_GROUP, IWX_SEC_KEY_CMD),
	    0, sizeof(cmd), &cmd);
	if (err) {
		IEEE80211_SEND_MGMT(ic, ni, IEEE80211_FC0_SUBTYPE_DEAUTH,
		    IEEE80211_REASON_AUTH_LEAVE);
		ieee80211_new_state(ic, IEEE80211_S_SCAN, -1);
		return err;
	}

	return 0;
}

int
iwx_add_sta_key_cmd(struct iwx_softc *sc, int sta_id,
    struct ieee80211_node *ni, struct ieee80211_key *k)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct iwx_add_sta_key_cmd cmd;
	uint32_t status;
	int err;

	memset(&cmd, 0, sizeof(cmd));

	cmd.common.key_flags = htole16(IWX_STA_KEY_FLG_CCM |
	    IWX_STA_KEY_FLG_WEP_KEY_MAP |
	    ((k->k_id << IWX_STA_KEY_FLG_KEYID_POS) &
	    IWX_STA_KEY_FLG_KEYID_MSK));
	if (k->k_flags & IEEE80211_KEY_GROUP) {
		cmd.common.key_offset = 1;
		cmd.common.key_flags |= htole16(IWX_STA_KEY_MULTICAST);
	} else
		cmd.common.key_offset = 0;

	memcpy(cmd.common.key, k->k_key, MIN(sizeof(cmd.common.key), k->k_len));
	cmd.common.sta_id = sta_id;

	cmd.transmit_seq_cnt = htole64(k->k_tsc);

	status = IWX_ADD_STA_SUCCESS;
	err = iwx_send_cmd_pdu_status(sc, IWX_ADD_STA_KEY, sizeof(cmd), &cmd,
	    &status);
	if (sc->sc_flags & IWX_FLAG_SHUTDOWN)
		return ECANCELED;
	if (!err && (status & IWX_ADD_STA_STATUS_MASK) != IWX_ADD_STA_SUCCESS)
		err = EIO;
	if (err) {
		IEEE80211_SEND_MGMT(ic, ni, IEEE80211_FC0_SUBTYPE_DEAUTH,
		    IEEE80211_REASON_AUTH_LEAVE);
		ieee80211_new_state(ic, IEEE80211_S_SCAN, -1);
		return err;
	}

	return 0;
}

int
iwx_add_sta_key(struct iwx_softc *sc, int sta_id, struct ieee80211_node *ni,
    struct ieee80211_key *k)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct iwx_node *in = (void *)ni;
	const int want_keymask = (IWX_NODE_FLAG_HAVE_PAIRWISE_KEY |
	    IWX_NODE_FLAG_HAVE_GROUP_KEY);
	uint8_t sec_key_ver;
	int err;

	/*
	 * Keys are stored in 'ni' so 'k' is valid if 'ni' is valid.
	 * Currently we only implement station mode where 'ni' is always
	 * ic->ic_bss so there is no need to validate arguments beyond this:
	 */
	KASSERT(ni == ic->ic_bss);

	sec_key_ver = iwx_lookup_cmd_ver(sc, IWX_DATA_PATH_GROUP,
	    IWX_SEC_KEY_CMD);
	if (sec_key_ver != 0 && sec_key_ver != IWX_FW_CMD_VER_UNKNOWN)
		err = iwx_mld_add_sta_key_cmd(sc, sta_id, ni, k);
	else
		err = iwx_add_sta_key_cmd(sc, sta_id, ni, k);
	if (err)
		return err;

	if (k->k_flags & IEEE80211_KEY_GROUP)
		in->in_flags |= IWX_NODE_FLAG_HAVE_GROUP_KEY;
	else
		in->in_flags |= IWX_NODE_FLAG_HAVE_PAIRWISE_KEY;

	if ((in->in_flags & want_keymask) == want_keymask) {
		DPRINTF(("marking port %s valid\n",
		    ether_sprintf(ni->ni_macaddr)));
		ni->ni_port_valid = 1;
		ieee80211_set_link_state(ic, LINK_STATE_UP);
	}

	return 0;
}

void
iwx_setkey_task(void *arg)
{
	struct iwx_softc *sc = arg;
	struct iwx_setkey_task_arg *a;
	int err = 0, s = splnet();

	while (sc->setkey_nkeys > 0) {
		if (err || (sc->sc_flags & IWX_FLAG_SHUTDOWN))
			break;
		a = &sc->setkey_arg[sc->setkey_tail];
		err = iwx_add_sta_key(sc, a->sta_id, a->ni, a->k);
		a->sta_id = 0;
		a->ni = NULL;
		a->k = NULL;
		sc->setkey_tail = (sc->setkey_tail + 1) %
		    nitems(sc->setkey_arg);
		sc->setkey_nkeys--;
	}

	refcnt_rele_wake(&sc->task_refs);
	splx(s);
}

void
iwx_delete_key(struct ieee80211com *ic, struct ieee80211_node *ni,
    struct ieee80211_key *k)
{
	struct iwx_softc *sc = ic->ic_softc;
	struct iwx_add_sta_key_cmd cmd;

	if (k->k_cipher != IEEE80211_CIPHER_CCMP) {
		/* Fallback to software crypto for other ciphers. */
                ieee80211_delete_key(ic, ni, k);
		return;
	}

	if ((sc->sc_flags & IWX_FLAG_STA_ACTIVE) == 0)
		return;

	memset(&cmd, 0, sizeof(cmd));

	cmd.common.key_flags = htole16(IWX_STA_KEY_NOT_VALID |
	    IWX_STA_KEY_FLG_NO_ENC | IWX_STA_KEY_FLG_WEP_KEY_MAP |
	    ((k->k_id << IWX_STA_KEY_FLG_KEYID_POS) &
	    IWX_STA_KEY_FLG_KEYID_MSK));
	memcpy(cmd.common.key, k->k_key, MIN(sizeof(cmd.common.key), k->k_len));
	if (k->k_flags & IEEE80211_KEY_GROUP)
		cmd.common.key_offset = 1;
	else
		cmd.common.key_offset = 0;
	cmd.common.sta_id = IWX_STATION_ID;

	iwx_send_cmd_pdu(sc, IWX_ADD_STA_KEY, IWX_CMD_ASYNC, sizeof(cmd), &cmd);
}

int
iwx_media_change(struct ifnet *ifp)
{
	int err;

	err = ieee80211_media_change(ifp);
	if (err != ENETRESET)
		return err;

	if ((ifp->if_flags & (IFF_UP | IFF_RUNNING)) ==
	    (IFF_UP | IFF_RUNNING)) {
		iwx_stop(ifp);
		err = iwx_init(ifp);
	}
	return err;
}

void
iwx_newstate_task(void *psc)
{
	struct iwx_softc *sc = (struct iwx_softc *)psc;
	struct ieee80211com *ic = &sc->sc_ic;
	enum ieee80211_state nstate = sc->ns_nstate;
	enum ieee80211_state ostate = ic->ic_state;
	int arg = sc->ns_arg;
	int err = 0, s = splnet();

	if (sc->sc_flags & IWX_FLAG_SHUTDOWN) {
		/* iwx_stop() is waiting for us. */
		refcnt_rele_wake(&sc->task_refs);
		splx(s);
		return;
	}

	if (ostate == IEEE80211_S_SCAN) {
		if (nstate == ostate) {
			if (sc->sc_flags & IWX_FLAG_SCANNING) {
				refcnt_rele_wake(&sc->task_refs);
				splx(s);
				return;
			}
			/* Firmware is no longer scanning. Do another scan. */
			goto next_scan;
		}
	}

	if (nstate <= ostate) {
		switch (ostate) {
		case IEEE80211_S_RUN:
			err = iwx_run_stop(sc);
			if (err)
				goto out;
			/* FALLTHROUGH */
		case IEEE80211_S_ASSOC:
		case IEEE80211_S_AUTH:
			if (nstate <= IEEE80211_S_AUTH) {
				err = iwx_deauth(sc);
				if (err)
					goto out;
			}
			/* FALLTHROUGH */
		case IEEE80211_S_SCAN:
		case IEEE80211_S_INIT:
			break;
		}

		/* Die now if iwx_stop() was called while we were sleeping. */
		if (sc->sc_flags & IWX_FLAG_SHUTDOWN) {
			refcnt_rele_wake(&sc->task_refs);
			splx(s);
			return;
		}
	}

	switch (nstate) {
	case IEEE80211_S_INIT:
		break;

	case IEEE80211_S_SCAN:
next_scan:
		err = iwx_scan(sc);
		if (err)
			break;
		refcnt_rele_wake(&sc->task_refs);
		splx(s);
		return;

	case IEEE80211_S_AUTH:
		err = iwx_auth(sc);
		break;

	case IEEE80211_S_ASSOC:
		break;

	case IEEE80211_S_RUN:
		err = iwx_run(sc);
		break;
	}

out:
	if ((sc->sc_flags & IWX_FLAG_SHUTDOWN) == 0) {
		if (err)
			task_add(systq, &sc->init_task);
		else
			sc->sc_newstate(ic, nstate, arg);
	}
	refcnt_rele_wake(&sc->task_refs);
	splx(s);
}

int
iwx_newstate(struct ieee80211com *ic, enum ieee80211_state nstate, int arg)
{
	struct ifnet *ifp = IC2IFP(ic);
	struct iwx_softc *sc = ifp->if_softc;

	/*
	 * Prevent attempts to transition towards the same state, unless
	 * we are scanning in which case a SCAN -> SCAN transition
	 * triggers another scan iteration. And AUTH -> AUTH is needed
	 * to support band-steering.
	 */
	if (sc->ns_nstate == nstate && nstate != IEEE80211_S_SCAN &&
	    nstate != IEEE80211_S_AUTH)
		return 0;

	if (ic->ic_state == IEEE80211_S_RUN) {
		iwx_del_task(sc, systq, &sc->ba_task);
		iwx_del_task(sc, systq, &sc->setkey_task);
		memset(sc->setkey_arg, 0, sizeof(sc->setkey_arg));
		sc->setkey_cur = sc->setkey_tail = sc->setkey_nkeys = 0;
		iwx_del_task(sc, systq, &sc->mac_ctxt_task);
		iwx_del_task(sc, systq, &sc->phy_ctxt_task);
		iwx_del_task(sc, systq, &sc->bgscan_done_task);
	}

	sc->ns_nstate = nstate;
	sc->ns_arg = arg;

	iwx_add_task(sc, sc->sc_nswq, &sc->newstate_task);

	return 0;
}

void
iwx_endscan(struct iwx_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;

	if ((sc->sc_flags & (IWX_FLAG_SCANNING | IWX_FLAG_BGSCAN)) == 0)
		return;

	sc->sc_flags &= ~(IWX_FLAG_SCANNING | IWX_FLAG_BGSCAN);
	ieee80211_end_scan(&ic->ic_if);
}

/*
 * Aging and idle timeouts for the different possible scenarios
 * in default configuration
 */
static const uint32_t
iwx_sf_full_timeout_def[IWX_SF_NUM_SCENARIO][IWX_SF_NUM_TIMEOUT_TYPES] = {
	{
		htole32(IWX_SF_SINGLE_UNICAST_AGING_TIMER_DEF),
		htole32(IWX_SF_SINGLE_UNICAST_IDLE_TIMER_DEF)
	},
	{
		htole32(IWX_SF_AGG_UNICAST_AGING_TIMER_DEF),
		htole32(IWX_SF_AGG_UNICAST_IDLE_TIMER_DEF)
	},
	{
		htole32(IWX_SF_MCAST_AGING_TIMER_DEF),
		htole32(IWX_SF_MCAST_IDLE_TIMER_DEF)
	},
	{
		htole32(IWX_SF_BA_AGING_TIMER_DEF),
		htole32(IWX_SF_BA_IDLE_TIMER_DEF)
	},
	{
		htole32(IWX_SF_TX_RE_AGING_TIMER_DEF),
		htole32(IWX_SF_TX_RE_IDLE_TIMER_DEF)
	},
};

/*
 * Aging and idle timeouts for the different possible scenarios
 * in single BSS MAC configuration.
 */
static const uint32_t
iwx_sf_full_timeout[IWX_SF_NUM_SCENARIO][IWX_SF_NUM_TIMEOUT_TYPES] = {
	{
		htole32(IWX_SF_SINGLE_UNICAST_AGING_TIMER),
		htole32(IWX_SF_SINGLE_UNICAST_IDLE_TIMER)
	},
	{
		htole32(IWX_SF_AGG_UNICAST_AGING_TIMER),
		htole32(IWX_SF_AGG_UNICAST_IDLE_TIMER)
	},
	{
		htole32(IWX_SF_MCAST_AGING_TIMER),
		htole32(IWX_SF_MCAST_IDLE_TIMER)
	},
	{
		htole32(IWX_SF_BA_AGING_TIMER),
		htole32(IWX_SF_BA_IDLE_TIMER)
	},
	{
		htole32(IWX_SF_TX_RE_AGING_TIMER),
		htole32(IWX_SF_TX_RE_IDLE_TIMER)
	},
};

void
iwx_fill_sf_command(struct iwx_softc *sc, struct iwx_sf_cfg_cmd *sf_cmd,
    struct ieee80211_node *ni)
{
	int i, j, watermark;

	sf_cmd->watermark[IWX_SF_LONG_DELAY_ON] = htole32(IWX_SF_W_MARK_SCAN);

	/*
	 * If we are in association flow - check antenna configuration
	 * capabilities of the AP station, and choose the watermark accordingly.
	 */
	if (ni) {
		if (ni->ni_flags & IEEE80211_NODE_HT) {
			if (ni->ni_rxmcs[1] != 0)
				watermark = IWX_SF_W_MARK_MIMO2;
			else
				watermark = IWX_SF_W_MARK_SISO;
		} else {
			watermark = IWX_SF_W_MARK_LEGACY;
		}
	/* default watermark value for unassociated mode. */
	} else {
		watermark = IWX_SF_W_MARK_MIMO2;
	}
	sf_cmd->watermark[IWX_SF_FULL_ON] = htole32(watermark);

	for (i = 0; i < IWX_SF_NUM_SCENARIO; i++) {
		for (j = 0; j < IWX_SF_NUM_TIMEOUT_TYPES; j++) {
			sf_cmd->long_delay_timeouts[i][j] =
					htole32(IWX_SF_LONG_DELAY_AGING_TIMER);
		}
	}

	if (ni) {
		memcpy(sf_cmd->full_on_timeouts, iwx_sf_full_timeout,
		       sizeof(iwx_sf_full_timeout));
	} else {
		memcpy(sf_cmd->full_on_timeouts, iwx_sf_full_timeout_def,
		       sizeof(iwx_sf_full_timeout_def));
	}

}

int
iwx_sf_config(struct iwx_softc *sc, int new_state)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct iwx_sf_cfg_cmd sf_cmd = {
		.state = htole32(new_state),
	};
	int err = 0;

	switch (new_state) {
	case IWX_SF_UNINIT:
	case IWX_SF_INIT_OFF:
		iwx_fill_sf_command(sc, &sf_cmd, NULL);
		break;
	case IWX_SF_FULL_ON:
		iwx_fill_sf_command(sc, &sf_cmd, ic->ic_bss);
		break;
	default:
		return EINVAL;
	}

	err = iwx_send_cmd_pdu(sc, IWX_REPLY_SF_CFG_CMD, IWX_CMD_ASYNC,
				   sizeof(sf_cmd), &sf_cmd);
	return err;
}

int
iwx_send_bt_init_conf(struct iwx_softc *sc)
{
	struct iwx_bt_coex_cmd bt_cmd;

	bt_cmd.mode = htole32(IWX_BT_COEX_WIFI);
	bt_cmd.enabled_modules = 0;

	return iwx_send_cmd_pdu(sc, IWX_BT_CONFIG, 0, sizeof(bt_cmd),
	    &bt_cmd);
}

int
iwx_send_soc_conf(struct iwx_softc *sc)
{
	struct iwx_soc_configuration_cmd cmd;
	int err;
	uint32_t cmd_id, flags = 0;

	memset(&cmd, 0, sizeof(cmd));

	/*
	 * In VER_1 of this command, the discrete value is considered
	 * an integer; In VER_2, it's a bitmask.  Since we have only 2
	 * values in VER_1, this is backwards-compatible with VER_2,
	 * as long as we don't set any other flag bits.
	 */
	if (!sc->sc_integrated) { /* VER_1 */
		flags = IWX_SOC_CONFIG_CMD_FLAGS_DISCRETE;
	} else { /* VER_2 */
		uint8_t scan_cmd_ver;
		if (sc->sc_ltr_delay != IWX_SOC_FLAGS_LTR_APPLY_DELAY_NONE)
			flags |= (sc->sc_ltr_delay &
			    IWX_SOC_FLAGS_LTR_APPLY_DELAY_MASK);
		scan_cmd_ver = iwx_lookup_cmd_ver(sc, IWX_LONG_GROUP,
		    IWX_SCAN_REQ_UMAC);
		if (scan_cmd_ver != IWX_FW_CMD_VER_UNKNOWN &&
		    scan_cmd_ver >= 2 && sc->sc_low_latency_xtal)
			flags |= IWX_SOC_CONFIG_CMD_FLAGS_LOW_LATENCY;
	}
	cmd.flags = htole32(flags);

	cmd.latency = htole32(sc->sc_xtal_latency);

	cmd_id = iwx_cmd_id(IWX_SOC_CONFIGURATION_CMD, IWX_SYSTEM_GROUP, 0);
	err = iwx_send_cmd_pdu(sc, cmd_id, 0, sizeof(cmd), &cmd);
	if (err)
		printf("%s: failed to set soc latency: %d\n", DEVNAME(sc), err);
	return err;
}

int
iwx_send_update_mcc_cmd(struct iwx_softc *sc, const char *alpha2)
{
	struct iwx_mcc_update_cmd mcc_cmd;
	struct iwx_host_cmd hcmd = {
		.id = IWX_MCC_UPDATE_CMD,
		.flags = IWX_CMD_WANT_RESP,
		.data = { &mcc_cmd },
	};
	struct iwx_rx_packet *pkt;
	struct iwx_mcc_update_resp *resp;
	size_t resp_len;
	int err;

	memset(&mcc_cmd, 0, sizeof(mcc_cmd));
	mcc_cmd.mcc = htole16(alpha2[0] << 8 | alpha2[1]);
	if (isset(sc->sc_ucode_api, IWX_UCODE_TLV_API_WIFI_MCC_UPDATE) ||
	    isset(sc->sc_enabled_capa, IWX_UCODE_TLV_CAPA_LAR_MULTI_MCC))
		mcc_cmd.source_id = IWX_MCC_SOURCE_GET_CURRENT;
	else
		mcc_cmd.source_id = IWX_MCC_SOURCE_OLD_FW;

	hcmd.len[0] = sizeof(struct iwx_mcc_update_cmd);
	hcmd.resp_pkt_len = IWX_CMD_RESP_MAX;

	err = iwx_send_cmd(sc, &hcmd);
	if (err)
		return err;

	pkt = hcmd.resp_pkt;
	if (!pkt || (pkt->hdr.flags & IWX_CMD_FAILED_MSK)) {
		err = EIO;
		goto out;
	}

	resp_len = iwx_rx_packet_payload_len(pkt);
	if (resp_len < sizeof(*resp)) {
		err = EIO;
		goto out;
	}

	resp = (void *)pkt->data;
	if (resp_len != sizeof(*resp) +
	    resp->n_channels * sizeof(resp->channels[0])) {
		err = EIO;
		goto out;
	}

	DPRINTF(("MCC status=0x%x mcc=0x%x cap=0x%x time=0x%x geo_info=0x%x source_id=0x%d n_channels=%u\n",
	    resp->status, resp->mcc, resp->cap, resp->time, resp->geo_info, resp->source_id, resp->n_channels));

	/* Update channel map for net80211 and our scan configuration. */
	iwx_init_channel_map(sc, NULL, resp->channels, resp->n_channels);

out:
	iwx_free_resp(sc, &hcmd);

	return err;
}

int
iwx_send_temp_report_ths_cmd(struct iwx_softc *sc)
{
	struct iwx_temp_report_ths_cmd cmd;
	int err;

	/*
	 * In order to give responsibility for critical-temperature-kill
	 * and TX backoff to FW we need to send an empty temperature
	 * reporting command at init time.
	 */
	memset(&cmd, 0, sizeof(cmd));

	err = iwx_send_cmd_pdu(sc,
	    IWX_WIDE_ID(IWX_PHY_OPS_GROUP, IWX_TEMP_REPORTING_THRESHOLDS_CMD),
	    0, sizeof(cmd), &cmd);
	if (err)
		printf("%s: TEMP_REPORT_THS_CMD command failed (error %d)\n",
		    DEVNAME(sc), err);

	return err;
}

int
iwx_init_hw(struct iwx_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	int err, i;

	err = iwx_run_init_mvm_ucode(sc, 0);
	if (err)
		return err;

	if (!iwx_nic_lock(sc))
		return EBUSY;

	err = iwx_send_tx_ant_cfg(sc, iwx_fw_valid_tx_ant(sc));
	if (err) {
		printf("%s: could not init tx ant config (error %d)\n",
		    DEVNAME(sc), err);
		goto err;
	}

	if (sc->sc_tx_with_siso_diversity) {
		err = iwx_send_phy_cfg_cmd(sc);
		if (err) {
			printf("%s: could not send phy config (error %d)\n",
			    DEVNAME(sc), err);
			goto err;
		}
	}

	err = iwx_send_bt_init_conf(sc);
	if (err) {
		printf("%s: could not init bt coex (error %d)\n",
		    DEVNAME(sc), err);
		return err;
	}

	err = iwx_send_soc_conf(sc);
	if (err)
		return err;

	if (isset(sc->sc_enabled_capa, IWX_UCODE_TLV_CAPA_DQA_SUPPORT)) {
		err = iwx_send_dqa_cmd(sc);
		if (err)
			return err;
	}

	for (i = 0; i < IWX_NUM_PHY_CTX; i++) {
		/*
		 * The channel used here isn't relevant as it's
		 * going to be overwritten in the other flows.
		 * For now use the first channel we have.
		 */
		sc->sc_phyctxt[i].id = i;
		sc->sc_phyctxt[i].channel = &ic->ic_channels[1];
		err = iwx_phy_ctxt_cmd(sc, &sc->sc_phyctxt[i], 1, 1,
		    IWX_FW_CTXT_ACTION_ADD, 0, IEEE80211_HTOP0_SCO_SCN,
		    IEEE80211_VHTOP0_CHAN_WIDTH_HT);
		if (err) {
			printf("%s: could not add phy context %d (error %d)\n",
			    DEVNAME(sc), i, err);
			goto err;
		}
		if (iwx_lookup_cmd_ver(sc, IWX_DATA_PATH_GROUP,
		    IWX_RLC_CONFIG_CMD) == 2) {
			err = iwx_phy_send_rlc(sc, &sc->sc_phyctxt[i], 1, 1);
			if (err) {
				printf("%s: could not configure RLC for PHY "
				    "%d (error %d)\n", DEVNAME(sc), i, err);
				goto err;
			}
		}
	}

	err = iwx_config_ltr(sc);
	if (err) {
		printf("%s: PCIe LTR configuration failed (error %d)\n",
		    DEVNAME(sc), err);
	}

	if (isset(sc->sc_enabled_capa, IWX_UCODE_TLV_CAPA_CT_KILL_BY_FW)) {
		err = iwx_send_temp_report_ths_cmd(sc);
		if (err)
			goto err;
	}

	err = iwx_power_update_device(sc);
	if (err) {
		printf("%s: could not send power command (error %d)\n",
		    DEVNAME(sc), err);
		goto err;
	}

	if (sc->sc_nvm.lar_enabled) {
		err = iwx_send_update_mcc_cmd(sc, "ZZ");
		if (err) {
			printf("%s: could not init LAR (error %d)\n",
			    DEVNAME(sc), err);
			goto err;
		}
	}

	err = iwx_config_umac_scan_reduced(sc);
	if (err) {
		printf("%s: could not configure scan (error %d)\n",
		    DEVNAME(sc), err);
		goto err;
	}

	err = iwx_disable_beacon_filter(sc);
	if (err) {
		printf("%s: could not disable beacon filter (error %d)\n",
		    DEVNAME(sc), err);
		goto err;
	}

err:
	iwx_nic_unlock(sc);
	return err;
}

/* Allow multicast from our BSSID. */
int
iwx_allow_mcast(struct iwx_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct iwx_node *in = (void *)ic->ic_bss;
	struct iwx_mcast_filter_cmd *cmd;
	size_t size;
	int err;

	size = roundup(sizeof(*cmd), 4);
	cmd = malloc(size, M_DEVBUF, M_NOWAIT | M_ZERO);
	if (cmd == NULL)
		return ENOMEM;
	cmd->filter_own = 1;
	cmd->port_id = 0;
	cmd->count = 0;
	cmd->pass_all = 1;
	IEEE80211_ADDR_COPY(cmd->bssid, in->in_macaddr);

	err = iwx_send_cmd_pdu(sc, IWX_MCAST_FILTER_CMD,
	    0, size, cmd);
	free(cmd, M_DEVBUF, size);
	return err;
}

int
iwx_init(struct ifnet *ifp)
{
	struct iwx_softc *sc = ifp->if_softc;
	struct ieee80211com *ic = &sc->sc_ic;
	int err, generation;

	rw_assert_wrlock(&sc->ioctl_rwl);

	generation = ++sc->sc_generation;

	err = iwx_preinit(sc);
	if (err)
		return err;

	err = iwx_start_hw(sc);
	if (err) {
		printf("%s: could not initialize hardware\n", DEVNAME(sc));
		return err;
	}

	err = iwx_init_hw(sc);
	if (err) {
		if (generation == sc->sc_generation)
			iwx_stop_device(sc);
		return err;
	}

	if (sc->sc_nvm.sku_cap_11n_enable)
		iwx_setup_ht_rates(sc);
	if (sc->sc_nvm.sku_cap_11ac_enable)
		iwx_setup_vht_rates(sc);

	KASSERT(sc->task_refs.r_refs == 0);
	refcnt_init(&sc->task_refs);
	ifq_clr_oactive(&ifp->if_snd);
	ifp->if_flags |= IFF_RUNNING;

	if (ic->ic_opmode == IEEE80211_M_MONITOR) {
		ic->ic_bss->ni_chan = ic->ic_ibss_chan;
		ieee80211_new_state(ic, IEEE80211_S_RUN, -1);
		return 0;
	}

	ieee80211_begin_scan(ifp);

	/*
	 * ieee80211_begin_scan() ends up scheduling iwx_newstate_task().
	 * Wait until the transition to SCAN state has completed.
	 */
	do {
		err = tsleep_nsec(&ic->ic_state, PCATCH, "iwxinit",
		    SEC_TO_NSEC(1));
		if (generation != sc->sc_generation)
			return ENXIO;
		if (err) {
			iwx_stop(ifp);
			return err;
		}
	} while (ic->ic_state != IEEE80211_S_SCAN);

	return 0;
}

void
iwx_start(struct ifnet *ifp)
{
	struct iwx_softc *sc = ifp->if_softc;
	struct ieee80211com *ic = &sc->sc_ic;
	struct ieee80211_node *ni;
	struct ether_header *eh;
	struct mbuf *m;

	if (!(ifp->if_flags & IFF_RUNNING) || ifq_is_oactive(&ifp->if_snd))
		return;

	for (;;) {
		/* why isn't this done per-queue? */
		if (sc->qfullmsk != 0) {
			ifq_set_oactive(&ifp->if_snd);
			break;
		}

		/* Don't queue additional frames while flushing Tx queues. */
		if (sc->sc_flags & IWX_FLAG_TXFLUSH)
			break;

		/* need to send management frames even if we're not RUNning */
		m = mq_dequeue(&ic->ic_mgtq);
		if (m) {
			ni = m->m_pkthdr.ph_cookie;
			goto sendit;
		}

		if (ic->ic_state != IEEE80211_S_RUN ||
		    (ic->ic_xflags & IEEE80211_F_TX_MGMT_ONLY))
			break;

		m = ifq_dequeue(&ifp->if_snd);
		if (!m)
			break;
		if (m->m_len < sizeof (*eh) &&
		    (m = m_pullup(m, sizeof (*eh))) == NULL) {
			ifp->if_oerrors++;
			continue;
		}
#if NBPFILTER > 0
		if (ifp->if_bpf != NULL)
			bpf_mtap(ifp->if_bpf, m, BPF_DIRECTION_OUT);
#endif
		if ((m = ieee80211_encap(ifp, m, &ni)) == NULL) {
			ifp->if_oerrors++;
			continue;
		}

 sendit:
#if NBPFILTER > 0
		if (ic->ic_rawbpf != NULL)
			bpf_mtap(ic->ic_rawbpf, m, BPF_DIRECTION_OUT);
#endif
		if (iwx_tx(sc, m, ni) != 0) {
			ieee80211_release_node(ic, ni);
			ifp->if_oerrors++;
			continue;
		}

		if (ifp->if_flags & IFF_UP)
			ifp->if_timer = 1;
	}

	return;
}

void
iwx_stop(struct ifnet *ifp)
{
	struct iwx_softc *sc = ifp->if_softc;
	struct ieee80211com *ic = &sc->sc_ic;
	struct iwx_node *in = (void *)ic->ic_bss;
	int i, s = splnet();

	rw_assert_wrlock(&sc->ioctl_rwl);

	sc->sc_flags |= IWX_FLAG_SHUTDOWN; /* Disallow new tasks. */

	/* Cancel scheduled tasks and let any stale tasks finish up. */
	task_del(systq, &sc->init_task);
	iwx_del_task(sc, sc->sc_nswq, &sc->newstate_task);
	iwx_del_task(sc, systq, &sc->ba_task);
	iwx_del_task(sc, systq, &sc->setkey_task);
	memset(sc->setkey_arg, 0, sizeof(sc->setkey_arg));
	sc->setkey_cur = sc->setkey_tail = sc->setkey_nkeys = 0;
	iwx_del_task(sc, systq, &sc->mac_ctxt_task);
	iwx_del_task(sc, systq, &sc->phy_ctxt_task);
	iwx_del_task(sc, systq, &sc->bgscan_done_task);
	KASSERT(sc->task_refs.r_refs >= 1);
	refcnt_finalize(&sc->task_refs, "iwxstop");

	iwx_stop_device(sc);

	free(sc->bgscan_unref_arg, M_DEVBUF, sc->bgscan_unref_arg_size);
	sc->bgscan_unref_arg = NULL;
	sc->bgscan_unref_arg_size = 0;

	/* Reset soft state. */

	sc->sc_generation++;
	for (i = 0; i < nitems(sc->sc_cmd_resp_pkt); i++) {
		free(sc->sc_cmd_resp_pkt[i], M_DEVBUF, sc->sc_cmd_resp_len[i]);
		sc->sc_cmd_resp_pkt[i] = NULL;
		sc->sc_cmd_resp_len[i] = 0;
	}
	ifp->if_flags &= ~IFF_RUNNING;
	ifq_clr_oactive(&ifp->if_snd);

	in->in_phyctxt = NULL;
	in->in_flags = 0;
	IEEE80211_ADDR_COPY(in->in_macaddr, etheranyaddr);

	sc->sc_flags &= ~(IWX_FLAG_SCANNING | IWX_FLAG_BGSCAN);
	sc->sc_flags &= ~IWX_FLAG_MAC_ACTIVE;
	sc->sc_flags &= ~IWX_FLAG_BINDING_ACTIVE;
	sc->sc_flags &= ~IWX_FLAG_STA_ACTIVE;
	sc->sc_flags &= ~IWX_FLAG_TE_ACTIVE;
	sc->sc_flags &= ~IWX_FLAG_HW_ERR;
	sc->sc_flags &= ~IWX_FLAG_SHUTDOWN;
	sc->sc_flags &= ~IWX_FLAG_TXFLUSH;

	sc->sc_rx_ba_sessions = 0;
	sc->ba_rx.start_tidmask = 0;
	sc->ba_rx.stop_tidmask = 0;
	memset(sc->aggqid, 0, sizeof(sc->aggqid));
	sc->ba_tx.start_tidmask = 0;
	sc->ba_tx.stop_tidmask = 0;

	sc->sc_newstate(ic, IEEE80211_S_INIT, -1);
	sc->ns_nstate = IEEE80211_S_INIT;

	for (i = 0; i < nitems(sc->sc_rxba_data); i++) {
		struct iwx_rxba_data *rxba = &sc->sc_rxba_data[i];
		iwx_clear_reorder_buffer(sc, rxba);
	}
	memset(sc->sc_tx_timer, 0, sizeof(sc->sc_tx_timer));
	ifp->if_timer = 0;

	splx(s);
}

void
iwx_watchdog(struct ifnet *ifp)
{
	struct iwx_softc *sc = ifp->if_softc;
	int i;

	ifp->if_timer = 0;

	/*
	 * We maintain a separate timer for each Tx queue because
	 * Tx aggregation queues can get "stuck" while other queues
	 * keep working. The Linux driver uses a similar workaround.
	 */
	for (i = 0; i < nitems(sc->sc_tx_timer); i++) {
		if (sc->sc_tx_timer[i] > 0) {
			if (--sc->sc_tx_timer[i] == 0) {
				printf("%s: device timeout\n", DEVNAME(sc));
				if (ifp->if_flags & IFF_DEBUG) {
					iwx_nic_error(sc);
					iwx_dump_driver_status(sc);
				}
				if ((sc->sc_flags & IWX_FLAG_SHUTDOWN) == 0)
					task_add(systq, &sc->init_task);
				ifp->if_oerrors++;
				return;
			}
			ifp->if_timer = 1;
		}
	}

	ieee80211_watchdog(ifp);
}

int
iwx_ioctl(struct ifnet *ifp, u_long cmd, caddr_t data)
{
	struct iwx_softc *sc = ifp->if_softc;
	int s, err = 0, generation = sc->sc_generation;

	/*
	 * Prevent processes from entering this function while another
	 * process is tsleep'ing in it.
	 */
	err = rw_enter(&sc->ioctl_rwl, RW_WRITE | RW_INTR);
	if (err == 0 && generation != sc->sc_generation) {
		rw_exit(&sc->ioctl_rwl);
		return ENXIO;
	}
	if (err)
		return err;
	s = splnet();

	switch (cmd) {
	case SIOCSIFADDR:
		ifp->if_flags |= IFF_UP;
		/* FALLTHROUGH */
	case SIOCSIFFLAGS:
		if (ifp->if_flags & IFF_UP) {
			if (!(ifp->if_flags & IFF_RUNNING)) {
				/* Force reload of firmware image from disk. */
				sc->sc_fw.fw_status = IWX_FW_STATUS_NONE;
				err = iwx_init(ifp);
			}
		} else {
			if (ifp->if_flags & IFF_RUNNING)
				iwx_stop(ifp);
		}
		break;

	default:
		err = ieee80211_ioctl(ifp, cmd, data);
	}

	if (err == ENETRESET) {
		err = 0;
		if ((ifp->if_flags & (IFF_UP | IFF_RUNNING)) ==
		    (IFF_UP | IFF_RUNNING)) {
			iwx_stop(ifp);
			err = iwx_init(ifp);
		}
	}

	splx(s);
	rw_exit(&sc->ioctl_rwl);

	return err;
}

/*
 * Note: This structure is read from the device with IO accesses,
 * and the reading already does the endian conversion. As it is
 * read with uint32_t-sized accesses, any members with a different size
 * need to be ordered correctly though!
 */
struct iwx_error_event_table {
	uint32_t valid;		/* (nonzero) valid, (0) log is empty */
	uint32_t error_id;		/* type of error */
	uint32_t trm_hw_status0;	/* TRM HW status */
	uint32_t trm_hw_status1;	/* TRM HW status */
	uint32_t blink2;		/* branch link */
	uint32_t ilink1;		/* interrupt link */
	uint32_t ilink2;		/* interrupt link */
	uint32_t data1;		/* error-specific data */
	uint32_t data2;		/* error-specific data */
	uint32_t data3;		/* error-specific data */
	uint32_t bcon_time;		/* beacon timer */
	uint32_t tsf_low;		/* network timestamp function timer */
	uint32_t tsf_hi;		/* network timestamp function timer */
	uint32_t gp1;		/* GP1 timer register */
	uint32_t gp2;		/* GP2 timer register */
	uint32_t fw_rev_type;	/* firmware revision type */
	uint32_t major;		/* uCode version major */
	uint32_t minor;		/* uCode version minor */
	uint32_t hw_ver;		/* HW Silicon version */
	uint32_t brd_ver;		/* HW board version */
	uint32_t log_pc;		/* log program counter */
	uint32_t frame_ptr;		/* frame pointer */
	uint32_t stack_ptr;		/* stack pointer */
	uint32_t hcmd;		/* last host command header */
	uint32_t isr0;		/* isr status register LMPM_NIC_ISR0:
				 * rxtx_flag */
	uint32_t isr1;		/* isr status register LMPM_NIC_ISR1:
				 * host_flag */
	uint32_t isr2;		/* isr status register LMPM_NIC_ISR2:
				 * enc_flag */
	uint32_t isr3;		/* isr status register LMPM_NIC_ISR3:
				 * time_flag */
	uint32_t isr4;		/* isr status register LMPM_NIC_ISR4:
				 * wico interrupt */
	uint32_t last_cmd_id;	/* last HCMD id handled by the firmware */
	uint32_t wait_event;		/* wait event() caller address */
	uint32_t l2p_control;	/* L2pControlField */
	uint32_t l2p_duration;	/* L2pDurationField */
	uint32_t l2p_mhvalid;	/* L2pMhValidBits */
	uint32_t l2p_addr_match;	/* L2pAddrMatchStat */
	uint32_t lmpm_pmg_sel;	/* indicate which clocks are turned on
				 * (LMPM_PMG_SEL) */
	uint32_t u_timestamp;	/* indicate when the date and time of the
				 * compilation */
	uint32_t flow_handler;	/* FH read/write pointers, RX credit */
} __packed /* LOG_ERROR_TABLE_API_S_VER_3 */;

/*
 * UMAC error struct - relevant starting from family 8000 chip.
 * Note: This structure is read from the device with IO accesses,
 * and the reading already does the endian conversion. As it is
 * read with u32-sized accesses, any members with a different size
 * need to be ordered correctly though!
 */
struct iwx_umac_error_event_table {
	uint32_t valid;		/* (nonzero) valid, (0) log is empty */
	uint32_t error_id;	/* type of error */
	uint32_t blink1;	/* branch link */
	uint32_t blink2;	/* branch link */
	uint32_t ilink1;	/* interrupt link */
	uint32_t ilink2;	/* interrupt link */
	uint32_t data1;		/* error-specific data */
	uint32_t data2;		/* error-specific data */
	uint32_t data3;		/* error-specific data */
	uint32_t umac_major;
	uint32_t umac_minor;
	uint32_t frame_pointer;	/* core register 27*/
	uint32_t stack_pointer;	/* core register 28 */
	uint32_t cmd_header;	/* latest host cmd sent to UMAC */
	uint32_t nic_isr_pref;	/* ISR status register */
} __packed;

#define ERROR_START_OFFSET  (1 * sizeof(uint32_t))
#define ERROR_ELEM_SIZE     (7 * sizeof(uint32_t))

void
iwx_nic_umac_error(struct iwx_softc *sc)
{
	struct iwx_umac_error_event_table table;
	uint32_t base;

	base = sc->sc_uc.uc_umac_error_event_table;

	if (base < 0x400000) {
		printf("%s: Invalid error log pointer 0x%08x\n",
		    DEVNAME(sc), base);
		return;
	}

	if (iwx_read_mem(sc, base, &table, sizeof(table)/sizeof(uint32_t))) {
		printf("%s: reading errlog failed\n", DEVNAME(sc));
		return;
	}

	if (ERROR_START_OFFSET <= table.valid * ERROR_ELEM_SIZE) {
		printf("%s: Start UMAC Error Log Dump:\n", DEVNAME(sc));
		printf("%s: Status: 0x%x, count: %d\n", DEVNAME(sc),
			sc->sc_flags, table.valid);
	}

	printf("%s: 0x%08X | %s\n", DEVNAME(sc), table.error_id,
		iwx_desc_lookup(table.error_id));
	printf("%s: 0x%08X | umac branchlink1\n", DEVNAME(sc), table.blink1);
	printf("%s: 0x%08X | umac branchlink2\n", DEVNAME(sc), table.blink2);
	printf("%s: 0x%08X | umac interruptlink1\n", DEVNAME(sc), table.ilink1);
	printf("%s: 0x%08X | umac interruptlink2\n", DEVNAME(sc), table.ilink2);
	printf("%s: 0x%08X | umac data1\n", DEVNAME(sc), table.data1);
	printf("%s: 0x%08X | umac data2\n", DEVNAME(sc), table.data2);
	printf("%s: 0x%08X | umac data3\n", DEVNAME(sc), table.data3);
	printf("%s: 0x%08X | umac major\n", DEVNAME(sc), table.umac_major);
	printf("%s: 0x%08X | umac minor\n", DEVNAME(sc), table.umac_minor);
	printf("%s: 0x%08X | frame pointer\n", DEVNAME(sc),
	    table.frame_pointer);
	printf("%s: 0x%08X | stack pointer\n", DEVNAME(sc),
	    table.stack_pointer);
	printf("%s: 0x%08X | last host cmd\n", DEVNAME(sc), table.cmd_header);
	printf("%s: 0x%08X | isr status reg\n", DEVNAME(sc),
	    table.nic_isr_pref);
}

#define IWX_FW_SYSASSERT_CPU_MASK 0xf0000000
static struct {
	const char *name;
	uint8_t num;
} advanced_lookup[] = {
	{ "NMI_INTERRUPT_WDG", 0x34 },
	{ "SYSASSERT", 0x35 },
	{ "UCODE_VERSION_MISMATCH", 0x37 },
	{ "BAD_COMMAND", 0x38 },
	{ "BAD_COMMAND", 0x39 },
	{ "NMI_INTERRUPT_DATA_ACTION_PT", 0x3C },
	{ "FATAL_ERROR", 0x3D },
	{ "NMI_TRM_HW_ERR", 0x46 },
	{ "NMI_INTERRUPT_TRM", 0x4C },
	{ "NMI_INTERRUPT_BREAK_POINT", 0x54 },
	{ "NMI_INTERRUPT_WDG_RXF_FULL", 0x5C },
	{ "NMI_INTERRUPT_WDG_NO_RBD_RXF_FULL", 0x64 },
	{ "NMI_INTERRUPT_HOST", 0x66 },
	{ "NMI_INTERRUPT_LMAC_FATAL", 0x70 },
	{ "NMI_INTERRUPT_UMAC_FATAL", 0x71 },
	{ "NMI_INTERRUPT_OTHER_LMAC_FATAL", 0x73 },
	{ "NMI_INTERRUPT_ACTION_PT", 0x7C },
	{ "NMI_INTERRUPT_UNKNOWN", 0x84 },
	{ "NMI_INTERRUPT_INST_ACTION_PT", 0x86 },
	{ "ADVANCED_SYSASSERT", 0 },
};

const char *
iwx_desc_lookup(uint32_t num)
{
	int i;

	for (i = 0; i < nitems(advanced_lookup) - 1; i++)
		if (advanced_lookup[i].num ==
		    (num & ~IWX_FW_SYSASSERT_CPU_MASK))
			return advanced_lookup[i].name;

	/* No entry matches 'num', so it is the last: ADVANCED_SYSASSERT */
	return advanced_lookup[i].name;
}

/*
 * Support for dumping the error log seemed like a good idea ...
 * but it's mostly hex junk and the only sensible thing is the
 * hw/ucode revision (which we know anyway).  Since it's here,
 * I'll just leave it in, just in case e.g. the Intel guys want to
 * help us decipher some "ADVANCED_SYSASSERT" later.
 */
void
iwx_nic_error(struct iwx_softc *sc)
{
	struct iwx_error_event_table table;
	uint32_t base;

	printf("%s: dumping device error log\n", DEVNAME(sc));
	base = sc->sc_uc.uc_lmac_error_event_table[0];
	if (base < 0x400000) {
		printf("%s: Invalid error log pointer 0x%08x\n",
		    DEVNAME(sc), base);
		return;
	}

	if (iwx_read_mem(sc, base, &table, sizeof(table)/sizeof(uint32_t))) {
		printf("%s: reading errlog failed\n", DEVNAME(sc));
		return;
	}

	if (!table.valid) {
		printf("%s: errlog not found, skipping\n", DEVNAME(sc));
		return;
	}

	if (ERROR_START_OFFSET <= table.valid * ERROR_ELEM_SIZE) {
		printf("%s: Start Error Log Dump:\n", DEVNAME(sc));
		printf("%s: Status: 0x%x, count: %d\n", DEVNAME(sc),
		    sc->sc_flags, table.valid);
	}

	printf("%s: 0x%08X | %-28s\n", DEVNAME(sc), table.error_id,
	    iwx_desc_lookup(table.error_id));
	printf("%s: %08X | trm_hw_status0\n", DEVNAME(sc),
	    table.trm_hw_status0);
	printf("%s: %08X | trm_hw_status1\n", DEVNAME(sc),
	    table.trm_hw_status1);
	printf("%s: %08X | branchlink2\n", DEVNAME(sc), table.blink2);
	printf("%s: %08X | interruptlink1\n", DEVNAME(sc), table.ilink1);
	printf("%s: %08X | interruptlink2\n", DEVNAME(sc), table.ilink2);
	printf("%s: %08X | data1\n", DEVNAME(sc), table.data1);
	printf("%s: %08X | data2\n", DEVNAME(sc), table.data2);
	printf("%s: %08X | data3\n", DEVNAME(sc), table.data3);
	printf("%s: %08X | beacon time\n", DEVNAME(sc), table.bcon_time);
	printf("%s: %08X | tsf low\n", DEVNAME(sc), table.tsf_low);
	printf("%s: %08X | tsf hi\n", DEVNAME(sc), table.tsf_hi);
	printf("%s: %08X | time gp1\n", DEVNAME(sc), table.gp1);
	printf("%s: %08X | time gp2\n", DEVNAME(sc), table.gp2);
	printf("%s: %08X | uCode revision type\n", DEVNAME(sc),
	    table.fw_rev_type);
	printf("%s: %08X | uCode version major\n", DEVNAME(sc),
	    table.major);
	printf("%s: %08X | uCode version minor\n", DEVNAME(sc),
	    table.minor);
	printf("%s: %08X | hw version\n", DEVNAME(sc), table.hw_ver);
	printf("%s: %08X | board version\n", DEVNAME(sc), table.brd_ver);
	printf("%s: %08X | hcmd\n", DEVNAME(sc), table.hcmd);
	printf("%s: %08X | isr0\n", DEVNAME(sc), table.isr0);
	printf("%s: %08X | isr1\n", DEVNAME(sc), table.isr1);
	printf("%s: %08X | isr2\n", DEVNAME(sc), table.isr2);
	printf("%s: %08X | isr3\n", DEVNAME(sc), table.isr3);
	printf("%s: %08X | isr4\n", DEVNAME(sc), table.isr4);
	printf("%s: %08X | last cmd Id\n", DEVNAME(sc), table.last_cmd_id);
	printf("%s: %08X | wait_event\n", DEVNAME(sc), table.wait_event);
	printf("%s: %08X | l2p_control\n", DEVNAME(sc), table.l2p_control);
	printf("%s: %08X | l2p_duration\n", DEVNAME(sc), table.l2p_duration);
	printf("%s: %08X | l2p_mhvalid\n", DEVNAME(sc), table.l2p_mhvalid);
	printf("%s: %08X | l2p_addr_match\n", DEVNAME(sc), table.l2p_addr_match);
	printf("%s: %08X | lmpm_pmg_sel\n", DEVNAME(sc), table.lmpm_pmg_sel);
	printf("%s: %08X | timestamp\n", DEVNAME(sc), table.u_timestamp);
	printf("%s: %08X | flow_handler\n", DEVNAME(sc), table.flow_handler);

	if (sc->sc_uc.uc_umac_error_event_table)
		iwx_nic_umac_error(sc);
}

void
iwx_dump_driver_status(struct iwx_softc *sc)
{
	int i;

	printf("driver status:\n");
	for (i = 0; i < nitems(sc->txq); i++) {
		struct iwx_tx_ring *ring = &sc->txq[i];
		printf("  tx ring %2d: qid=%-2d cur=%-3d "
		    "cur_hw=%-3d queued=%-3d\n",
		    i, ring->qid, ring->cur, ring->cur_hw,
		    ring->queued);
	}
	printf("  rx ring: cur=%d\n", sc->rxq.cur);
	printf("  802.11 state %s\n",
	    ieee80211_state_name[sc->sc_ic.ic_state]);
}

#define SYNC_RESP_STRUCT(_var_, _pkt_)					\
do {									\
	bus_dmamap_sync(sc->sc_dmat, data->map, sizeof(*(_pkt_)),	\
	    sizeof(*(_var_)), BUS_DMASYNC_POSTREAD);			\
	_var_ = (void *)((_pkt_)+1);					\
} while (/*CONSTCOND*/0)

#define SYNC_RESP_PTR(_ptr_, _len_, _pkt_)				\
do {									\
	bus_dmamap_sync(sc->sc_dmat, data->map, sizeof(*(_pkt_)),	\
	    sizeof(len), BUS_DMASYNC_POSTREAD);				\
	_ptr_ = (void *)((_pkt_)+1);					\
} while (/*CONSTCOND*/0)

int
iwx_rx_pkt_valid(struct iwx_rx_packet *pkt)
{
	int qid, idx, code;

	qid = pkt->hdr.qid & ~0x80;
	idx = pkt->hdr.idx;
	code = IWX_WIDE_ID(pkt->hdr.flags, pkt->hdr.code);

	return (!(qid == 0 && idx == 0 && code == 0) &&
	    pkt->len_n_flags != htole32(IWX_FH_RSCSR_FRAME_INVALID));
}

void
iwx_rx_pkt(struct iwx_softc *sc, struct iwx_rx_data *data, struct mbuf_list *ml)
{
	struct ifnet *ifp = IC2IFP(&sc->sc_ic);
	struct iwx_rx_packet *pkt, *nextpkt;
	uint32_t offset = 0, nextoff = 0, nmpdu = 0, len;
	struct mbuf *m0, *m;
	const size_t minsz = sizeof(pkt->len_n_flags) + sizeof(pkt->hdr);
	int qid, idx, code, handled = 1;

	bus_dmamap_sync(sc->sc_dmat, data->map, 0, IWX_RBUF_SIZE,
	    BUS_DMASYNC_POSTREAD);

	m0 = data->m;
	while (m0 && offset + minsz < IWX_RBUF_SIZE) {
		pkt = (struct iwx_rx_packet *)(m0->m_data + offset);
		qid = pkt->hdr.qid;
		idx = pkt->hdr.idx;

		code = IWX_WIDE_ID(pkt->hdr.flags, pkt->hdr.code);

		if (!iwx_rx_pkt_valid(pkt))
			break;

		/*
		 * XXX Intel inside (tm)
		 * Any commands in the LONG_GROUP could actually be in the
		 * LEGACY group. Firmware API versions >= 50 reject commands
		 * in group 0, forcing us to use this hack.
		 */
		if (iwx_cmd_groupid(code) == IWX_LONG_GROUP) {
			struct iwx_tx_ring *ring = &sc->txq[qid];
			struct iwx_tx_data *txdata = &ring->data[idx];
			if (txdata->flags & IWX_TXDATA_FLAG_CMD_IS_NARROW)
				code = iwx_cmd_opcode(code);
		}

		len = sizeof(pkt->len_n_flags) + iwx_rx_packet_len(pkt);
		if (len < minsz || len > (IWX_RBUF_SIZE - offset))
			break;

		if (code == IWX_REPLY_RX_MPDU_CMD && ++nmpdu == 1) {
			/* Take mbuf m0 off the RX ring. */
			if (iwx_rx_addbuf(sc, IWX_RBUF_SIZE, sc->rxq.cur)) {
				ifp->if_ierrors++;
				break;
			}
			KASSERT(data->m != m0);
		}

		switch (code) {
		case IWX_REPLY_RX_PHY_CMD:
			iwx_rx_rx_phy_cmd(sc, pkt, data);
			break;

		case IWX_REPLY_RX_MPDU_CMD: {
			size_t maxlen = IWX_RBUF_SIZE - offset - minsz;
			nextoff = offset +
			    roundup(len, IWX_FH_RSCSR_FRAME_ALIGN);
			nextpkt = (struct iwx_rx_packet *)
			    (m0->m_data + nextoff);
			/* AX210 devices ship only one packet per Rx buffer. */
			if (sc->sc_device_family >= IWX_DEVICE_FAMILY_AX210 ||
			    nextoff + minsz >= IWX_RBUF_SIZE ||
			    !iwx_rx_pkt_valid(nextpkt)) {
				/* No need to copy last frame in buffer. */
				if (offset > 0)
					m_adj(m0, offset);
				iwx_rx_mpdu_mq(sc, m0, pkt->data, maxlen, ml);
				m0 = NULL; /* stack owns m0 now; abort loop */
			} else {
				/*
				 * Create an mbuf which points to the current
				 * packet. Always copy from offset zero to
				 * preserve m_pkthdr.
				 */
				m = m_copym(m0, 0, M_COPYALL, M_DONTWAIT);
				if (m == NULL) {
					ifp->if_ierrors++;
					m_freem(m0);
					m0 = NULL;
					break;
				}
				m_adj(m, offset);
				iwx_rx_mpdu_mq(sc, m, pkt->data, maxlen, ml);
			}
 			break;
		}

		case IWX_BAR_FRAME_RELEASE:
			iwx_rx_bar_frame_release(sc, pkt, ml);
			break;

		case IWX_TX_CMD:
			iwx_rx_tx_cmd(sc, pkt, data);
			break;

		case IWX_BA_NOTIF:
			iwx_rx_compressed_ba(sc, pkt);
			break;

		case IWX_MISSED_BEACONS_NOTIFICATION:
			iwx_rx_bmiss(sc, pkt, data);
			break;

		case IWX_MFUART_LOAD_NOTIFICATION:
			break;

		case IWX_ALIVE: {
			struct iwx_alive_resp_v4 *resp4;
			struct iwx_alive_resp_v5 *resp5;
			struct iwx_alive_resp_v6 *resp6;

			DPRINTF(("%s: firmware alive\n", __func__));
			sc->sc_uc.uc_ok = 0;

			/*
			 * For v5 and above, we can check the version, for older
			 * versions we need to check the size.
			 */
			if (iwx_lookup_notif_ver(sc, IWX_LEGACY_GROUP,
			    IWX_ALIVE) == 6) {
				SYNC_RESP_STRUCT(resp6, pkt);
				if (iwx_rx_packet_payload_len(pkt) !=
				    sizeof(*resp6)) {
					sc->sc_uc.uc_intr = 1;
					wakeup(&sc->sc_uc);
					break;
				}
				sc->sc_uc.uc_lmac_error_event_table[0] = le32toh(
				    resp6->lmac_data[0].dbg_ptrs.error_event_table_ptr);
				sc->sc_uc.uc_lmac_error_event_table[1] = le32toh(
				    resp6->lmac_data[1].dbg_ptrs.error_event_table_ptr);
				sc->sc_uc.uc_log_event_table = le32toh(
				    resp6->lmac_data[0].dbg_ptrs.log_event_table_ptr);
				sc->sc_uc.uc_umac_error_event_table = le32toh(
				    resp6->umac_data.dbg_ptrs.error_info_addr);
				sc->sc_sku_id[0] =
				    le32toh(resp6->sku_id.data[0]);
				sc->sc_sku_id[1] =
				    le32toh(resp6->sku_id.data[1]);
				sc->sc_sku_id[2] =
				    le32toh(resp6->sku_id.data[2]);
				if (resp6->status == IWX_ALIVE_STATUS_OK)
					sc->sc_uc.uc_ok = 1;
			 } else if (iwx_lookup_notif_ver(sc, IWX_LEGACY_GROUP,
			    IWX_ALIVE) == 5) {
				SYNC_RESP_STRUCT(resp5, pkt);
				if (iwx_rx_packet_payload_len(pkt) !=
				    sizeof(*resp5)) {
					sc->sc_uc.uc_intr = 1;
					wakeup(&sc->sc_uc);
					break;
				}
				sc->sc_uc.uc_lmac_error_event_table[0] = le32toh(
				    resp5->lmac_data[0].dbg_ptrs.error_event_table_ptr);
				sc->sc_uc.uc_lmac_error_event_table[1] = le32toh(
				    resp5->lmac_data[1].dbg_ptrs.error_event_table_ptr);
				sc->sc_uc.uc_log_event_table = le32toh(
				    resp5->lmac_data[0].dbg_ptrs.log_event_table_ptr);
				sc->sc_uc.uc_umac_error_event_table = le32toh(
				    resp5->umac_data.dbg_ptrs.error_info_addr);
				sc->sc_sku_id[0] =
				    le32toh(resp5->sku_id.data[0]);
				sc->sc_sku_id[1] =
				    le32toh(resp5->sku_id.data[1]);
				sc->sc_sku_id[2] =
				    le32toh(resp5->sku_id.data[2]);
				if (resp5->status == IWX_ALIVE_STATUS_OK)
					sc->sc_uc.uc_ok = 1;
			} else if (iwx_rx_packet_payload_len(pkt) == sizeof(*resp4)) {
				SYNC_RESP_STRUCT(resp4, pkt);
				sc->sc_uc.uc_lmac_error_event_table[0] = le32toh(
				    resp4->lmac_data[0].dbg_ptrs.error_event_table_ptr);
				sc->sc_uc.uc_lmac_error_event_table[1] = le32toh(
				    resp4->lmac_data[1].dbg_ptrs.error_event_table_ptr);
				sc->sc_uc.uc_log_event_table = le32toh(
				    resp4->lmac_data[0].dbg_ptrs.log_event_table_ptr);
				sc->sc_uc.uc_umac_error_event_table = le32toh(
				    resp4->umac_data.dbg_ptrs.error_info_addr);
				if (resp4->status == IWX_ALIVE_STATUS_OK)
					sc->sc_uc.uc_ok = 1;
			}

			sc->sc_uc.uc_intr = 1;
			wakeup(&sc->sc_uc);
			break;
		}

		case IWX_STATISTICS_NOTIFICATION: {
			struct iwx_notif_statistics *stats;
			SYNC_RESP_STRUCT(stats, pkt);
			memcpy(&sc->sc_stats, stats, sizeof(sc->sc_stats));
			sc->sc_noise = iwx_get_noise(&stats->rx.general);
			break;
		}

		case IWX_DTS_MEASUREMENT_NOTIFICATION:
		case IWX_WIDE_ID(IWX_PHY_OPS_GROUP,
				 IWX_DTS_MEASUREMENT_NOTIF_WIDE):
		case IWX_WIDE_ID(IWX_PHY_OPS_GROUP,
				 IWX_TEMP_REPORTING_THRESHOLDS_CMD):
			break;

		case IWX_WIDE_ID(IWX_PHY_OPS_GROUP,
		    IWX_CT_KILL_NOTIFICATION): {
			struct iwx_ct_kill_notif *notif;
			SYNC_RESP_STRUCT(notif, pkt);
			printf("%s: device at critical temperature (%u degC), "
			    "stopping device\n",
			    DEVNAME(sc), le16toh(notif->temperature));
			sc->sc_flags |= IWX_FLAG_HW_ERR;
			task_add(systq, &sc->init_task);
			break;
		}

		case IWX_WIDE_ID(IWX_DATA_PATH_GROUP,
		    IWX_SCD_QUEUE_CONFIG_CMD):
		case IWX_WIDE_ID(IWX_DATA_PATH_GROUP,
		    IWX_RX_BAID_ALLOCATION_CONFIG_CMD):
		case IWX_WIDE_ID(IWX_DATA_PATH_GROUP,
		    IWX_SEC_KEY_CMD):
		case IWX_WIDE_ID(IWX_MAC_CONF_GROUP,
		    IWX_SESSION_PROTECTION_CMD):
		case IWX_WIDE_ID(IWX_MAC_CONF_GROUP,
		    IWX_MAC_CONFIG_CMD):
		case IWX_WIDE_ID(IWX_MAC_CONF_GROUP,
		    IWX_LINK_CONFIG_CMD):
		case IWX_WIDE_ID(IWX_MAC_CONF_GROUP,
		    IWX_STA_CONFIG_CMD):
		case IWX_WIDE_ID(IWX_MAC_CONF_GROUP,
		    IWX_STA_REMOVE_CMD):
		case IWX_WIDE_ID(IWX_REGULATORY_AND_NVM_GROUP,
		    IWX_NVM_GET_INFO):
		case IWX_ADD_STA_KEY:
		case IWX_PHY_CONFIGURATION_CMD:
		case IWX_TX_ANT_CONFIGURATION_CMD:
		case IWX_ADD_STA:
		case IWX_MAC_CONTEXT_CMD:
		case IWX_REPLY_SF_CFG_CMD:
		case IWX_POWER_TABLE_CMD:
		case IWX_LTR_CONFIG:
		case IWX_PHY_CONTEXT_CMD:
		case IWX_BINDING_CONTEXT_CMD:
		case IWX_WIDE_ID(IWX_LONG_GROUP, IWX_SCAN_CFG_CMD):
		case IWX_WIDE_ID(IWX_LONG_GROUP, IWX_SCAN_REQ_UMAC):
		case IWX_WIDE_ID(IWX_LONG_GROUP, IWX_SCAN_ABORT_UMAC):
		case IWX_REPLY_BEACON_FILTERING_CMD:
		case IWX_MAC_PM_POWER_TABLE:
		case IWX_TIME_QUOTA_CMD:
		case IWX_REMOVE_STA:
		case IWX_TXPATH_FLUSH:
		case IWX_BT_CONFIG:
		case IWX_MCC_UPDATE_CMD:
		case IWX_TIME_EVENT_CMD:
		case IWX_STATISTICS_CMD:
		case IWX_SCD_QUEUE_CFG: {
			size_t pkt_len;

			if (sc->sc_cmd_resp_pkt[idx] == NULL)
				break;

			bus_dmamap_sync(sc->sc_dmat, data->map, 0,
			    sizeof(*pkt), BUS_DMASYNC_POSTREAD);

			pkt_len = sizeof(pkt->len_n_flags) +
			    iwx_rx_packet_len(pkt);

			if ((pkt->hdr.flags & IWX_CMD_FAILED_MSK) ||
			    pkt_len < sizeof(*pkt) ||
			    pkt_len > sc->sc_cmd_resp_len[idx]) {
				free(sc->sc_cmd_resp_pkt[idx], M_DEVBUF,
				    sc->sc_cmd_resp_len[idx]);
				sc->sc_cmd_resp_pkt[idx] = NULL;
				break;
			}

			bus_dmamap_sync(sc->sc_dmat, data->map, sizeof(*pkt),
			    pkt_len - sizeof(*pkt), BUS_DMASYNC_POSTREAD);
			memcpy(sc->sc_cmd_resp_pkt[idx], pkt, pkt_len);
			break;
		}

		case IWX_INIT_COMPLETE_NOTIF:
			sc->sc_init_complete |= IWX_INIT_COMPLETE;
			wakeup(&sc->sc_init_complete);
			break;

		case IWX_SCAN_COMPLETE_UMAC: {
			struct iwx_umac_scan_complete *notif;
			SYNC_RESP_STRUCT(notif, pkt);
			iwx_endscan(sc);
			break;
		}

		case IWX_SCAN_ITERATION_COMPLETE_UMAC: {
			struct iwx_umac_scan_iter_complete_notif *notif;
			SYNC_RESP_STRUCT(notif, pkt);
			iwx_endscan(sc);
			break;
		}

		case IWX_MCC_CHUB_UPDATE_CMD: {
			struct iwx_mcc_chub_notif *notif;
			SYNC_RESP_STRUCT(notif, pkt);
			iwx_mcc_update(sc, notif);
			break;
		}

		case IWX_REPLY_ERROR: {
			struct iwx_error_resp *resp;
			SYNC_RESP_STRUCT(resp, pkt);
			printf("%s: firmware error 0x%x, cmd 0x%x\n",
				DEVNAME(sc), le32toh(resp->error_type),
				resp->cmd_id);
			break;
		}

		case IWX_TIME_EVENT_NOTIFICATION: {
			struct iwx_time_event_notif *notif;
			uint32_t action;
			SYNC_RESP_STRUCT(notif, pkt);

			if (sc->sc_time_event_uid != le32toh(notif->unique_id))
				break;
			action = le32toh(notif->action);
			if (action & IWX_TE_V2_NOTIF_HOST_EVENT_END)
				sc->sc_flags &= ~IWX_FLAG_TE_ACTIVE;
			break;
		}

		case IWX_WIDE_ID(IWX_MAC_CONF_GROUP,
		    IWX_SESSION_PROTECTION_NOTIF): {
			struct iwx_session_prot_notif *notif;
			uint32_t status, start, conf_id;

			SYNC_RESP_STRUCT(notif, pkt);

			status = le32toh(notif->status);
			start = le32toh(notif->start);
			conf_id = le32toh(notif->conf_id);
			/* Check for end of successful PROTECT_CONF_ASSOC. */
			if (status == 1 && start == 0 &&
			    conf_id == IWX_SESSION_PROTECT_CONF_ASSOC)
				sc->sc_flags &= ~IWX_FLAG_TE_ACTIVE;
			break;
		}

		case IWX_WIDE_ID(IWX_SYSTEM_GROUP,
		    IWX_FSEQ_VER_MISMATCH_NOTIFICATION):
		    break;

		/*
		 * Firmware versions 21 and 22 generate some DEBUG_LOG_MSG
		 * messages. Just ignore them for now.
		 */
		case IWX_DEBUG_LOG_MSG:
			break;

		case IWX_MCAST_FILTER_CMD:
			break;

		case IWX_WIDE_ID(IWX_DATA_PATH_GROUP, IWX_DQA_ENABLE_CMD):
			break;

		case IWX_WIDE_ID(IWX_SYSTEM_GROUP, IWX_SOC_CONFIGURATION_CMD):
			break;

		case IWX_WIDE_ID(IWX_SYSTEM_GROUP, IWX_INIT_EXTENDED_CFG_CMD):
			break;

		case IWX_WIDE_ID(IWX_REGULATORY_AND_NVM_GROUP,
		    IWX_NVM_ACCESS_COMPLETE):
			break;

		case IWX_WIDE_ID(IWX_DATA_PATH_GROUP, IWX_RX_NO_DATA_NOTIF):
			break; /* happens in monitor mode; ignore for now */

		case IWX_WIDE_ID(IWX_DATA_PATH_GROUP, IWX_TLC_MNG_CONFIG_CMD):
			break;

		case IWX_WIDE_ID(IWX_DATA_PATH_GROUP,
		    IWX_TLC_MNG_UPDATE_NOTIF): {
			struct iwx_tlc_update_notif *notif;
			SYNC_RESP_STRUCT(notif, pkt);
			if (iwx_rx_packet_payload_len(pkt) == sizeof(*notif))
				iwx_rs_update(sc, notif);
			break;
		}

		case IWX_WIDE_ID(IWX_DATA_PATH_GROUP, IWX_RLC_CONFIG_CMD):
			break;

		/*
		 * Ignore for now. The Linux driver only acts on this request
		 * with 160Mhz channels in 11ax mode.
		 */
		case IWX_WIDE_ID(IWX_DATA_PATH_GROUP,
		    IWX_THERMAL_DUAL_CHAIN_REQUEST):
			DPRINTF(("%s: thermal dual-chain request received\n",
			    DEVNAME(sc)));
			break;

		/* undocumented notification from iwx-ty-a0-gf-a0-77 image */
		case IWX_WIDE_ID(IWX_DATA_PATH_GROUP, 0xf8):
			break;

		case IWX_WIDE_ID(IWX_REGULATORY_AND_NVM_GROUP,
		    IWX_PNVM_INIT_COMPLETE):
			sc->sc_init_complete |= IWX_PNVM_COMPLETE;
			wakeup(&sc->sc_init_complete);
			break;

		default:
			handled = 0;
			printf("%s: unhandled firmware response 0x%x/0x%x "
			    "rx ring %d[%d]\n",
			    DEVNAME(sc), code, pkt->len_n_flags,
			    (qid & ~0x80), idx);
			break;
		}

		/*
		 * uCode sets bit 0x80 when it originates the notification,
		 * i.e. when the notification is not a direct response to a
		 * command sent by the driver.
		 * For example, uCode issues IWX_REPLY_RX when it sends a
		 * received frame to the driver.
		 */
		if (handled && !(qid & (1 << 7))) {
			iwx_cmd_done(sc, qid, idx, code);
		}

		offset += roundup(len, IWX_FH_RSCSR_FRAME_ALIGN);

		/* AX210 devices ship only one packet per Rx buffer. */
		if (sc->sc_device_family >= IWX_DEVICE_FAMILY_AX210)
			break;
	}

	if (m0 && m0 != data->m)
		m_freem(m0);
}

void
iwx_notif_intr(struct iwx_softc *sc)
{
	struct mbuf_list ml = MBUF_LIST_INITIALIZER();
	uint16_t hw;

	bus_dmamap_sync(sc->sc_dmat, sc->rxq.stat_dma.map,
	    0, sc->rxq.stat_dma.size, BUS_DMASYNC_POSTREAD);

	if (sc->sc_device_family >= IWX_DEVICE_FAMILY_AX210) {
		uint16_t *status = sc->rxq.stat_dma.vaddr;
		hw = le16toh(*status) & 0xfff;
	} else
		hw = le16toh(sc->rxq.stat->closed_rb_num) & 0xfff;
	hw &= (IWX_RX_MQ_RING_COUNT - 1);
	while (sc->rxq.cur != hw) {
		struct iwx_rx_data *data = &sc->rxq.data[sc->rxq.cur];
		iwx_rx_pkt(sc, data, &ml);
		sc->rxq.cur = (sc->rxq.cur + 1) % IWX_RX_MQ_RING_COUNT;
	}
	if_input(&sc->sc_ic.ic_if, &ml);

	/*
	 * Tell the firmware what we have processed.
	 * Seems like the hardware gets upset unless we align the write by 8??
	 */
	hw = (hw == 0) ? IWX_RX_MQ_RING_COUNT - 1 : hw - 1;
	IWX_WRITE(sc, IWX_RFH_Q0_FRBDCB_WIDX_TRG, hw & ~7);
}

int
iwx_intr(void *arg)
{
	struct iwx_softc *sc = arg;
	struct ieee80211com *ic = &sc->sc_ic;
	struct ifnet *ifp = IC2IFP(ic);
	int handled = 0;
	int r1, r2, rv = 0;

	IWX_WRITE(sc, IWX_CSR_INT_MASK, 0);

	if (sc->sc_flags & IWX_FLAG_USE_ICT) {
		uint32_t *ict = sc->ict_dma.vaddr;
		int tmp;

		tmp = htole32(ict[sc->ict_cur]);
		if (!tmp)
			goto out_ena;

		/*
		 * ok, there was something.  keep plowing until we have all.
		 */
		r1 = r2 = 0;
		while (tmp) {
			r1 |= tmp;
			ict[sc->ict_cur] = 0;
			sc->ict_cur = (sc->ict_cur+1) % IWX_ICT_COUNT;
			tmp = htole32(ict[sc->ict_cur]);
		}

		/* this is where the fun begins.  don't ask */
		if (r1 == 0xffffffff)
			r1 = 0;

		/* i am not expected to understand this */
		if (r1 & 0xc0000)
			r1 |= 0x8000;
		r1 = (0xff & r1) | ((0xff00 & r1) << 16);
	} else {
		r1 = IWX_READ(sc, IWX_CSR_INT);
		if (r1 == 0xffffffff || (r1 & 0xfffffff0) == 0xa5a5a5a0)
			goto out;
		r2 = IWX_READ(sc, IWX_CSR_FH_INT_STATUS);
	}
	if (r1 == 0 && r2 == 0) {
		goto out_ena;
	}

	IWX_WRITE(sc, IWX_CSR_INT, r1 | ~sc->sc_intmask);

	if (r1 & IWX_CSR_INT_BIT_ALIVE) {
		int i;

		/* Firmware has now configured the RFH. */
		for (i = 0; i < IWX_RX_MQ_RING_COUNT; i++)
			iwx_update_rx_desc(sc, &sc->rxq, i);
		IWX_WRITE(sc, IWX_RFH_Q0_FRBDCB_WIDX_TRG, 8);
	}

	handled |= (r1 & (IWX_CSR_INT_BIT_ALIVE /*| IWX_CSR_INT_BIT_SCD*/));

	if (r1 & IWX_CSR_INT_BIT_RF_KILL) {
		handled |= IWX_CSR_INT_BIT_RF_KILL;
		iwx_check_rfkill(sc);
		task_add(systq, &sc->init_task);
		rv = 1;
		goto out_ena;
	}

	if (r1 & IWX_CSR_INT_BIT_SW_ERR) {
		if (ifp->if_flags & IFF_DEBUG) {
			iwx_nic_error(sc);
			iwx_dump_driver_status(sc);
		}
		printf("%s: fatal firmware error\n", DEVNAME(sc));
		if ((sc->sc_flags & IWX_FLAG_SHUTDOWN) == 0)
			task_add(systq, &sc->init_task);
		rv = 1;
		goto out;

	}

	if (r1 & IWX_CSR_INT_BIT_HW_ERR) {
		handled |= IWX_CSR_INT_BIT_HW_ERR;
		printf("%s: hardware error, stopping device \n", DEVNAME(sc));
		if ((sc->sc_flags & IWX_FLAG_SHUTDOWN) == 0) {
			sc->sc_flags |= IWX_FLAG_HW_ERR;
			task_add(systq, &sc->init_task);
		}
		rv = 1;
		goto out;
	}

	/* firmware chunk loaded */
	if (r1 & IWX_CSR_INT_BIT_FH_TX) {
		IWX_WRITE(sc, IWX_CSR_FH_INT_STATUS, IWX_CSR_FH_INT_TX_MASK);
		handled |= IWX_CSR_INT_BIT_FH_TX;

		sc->sc_fw_chunk_done = 1;
		wakeup(&sc->sc_fw);
	}

	if (r1 & (IWX_CSR_INT_BIT_FH_RX | IWX_CSR_INT_BIT_SW_RX |
	    IWX_CSR_INT_BIT_RX_PERIODIC)) {
		if (r1 & (IWX_CSR_INT_BIT_FH_RX | IWX_CSR_INT_BIT_SW_RX)) {
			handled |= (IWX_CSR_INT_BIT_FH_RX | IWX_CSR_INT_BIT_SW_RX);
			IWX_WRITE(sc, IWX_CSR_FH_INT_STATUS, IWX_CSR_FH_INT_RX_MASK);
		}
		if (r1 & IWX_CSR_INT_BIT_RX_PERIODIC) {
			handled |= IWX_CSR_INT_BIT_RX_PERIODIC;
			IWX_WRITE(sc, IWX_CSR_INT, IWX_CSR_INT_BIT_RX_PERIODIC);
		}

		/* Disable periodic interrupt; we use it as just a one-shot. */
		IWX_WRITE_1(sc, IWX_CSR_INT_PERIODIC_REG, IWX_CSR_INT_PERIODIC_DIS);

		/*
		 * Enable periodic interrupt in 8 msec only if we received
		 * real RX interrupt (instead of just periodic int), to catch
		 * any dangling Rx interrupt.  If it was just the periodic
		 * interrupt, there was no dangling Rx activity, and no need
		 * to extend the periodic interrupt; one-shot is enough.
		 */
		if (r1 & (IWX_CSR_INT_BIT_FH_RX | IWX_CSR_INT_BIT_SW_RX))
			IWX_WRITE_1(sc, IWX_CSR_INT_PERIODIC_REG,
			    IWX_CSR_INT_PERIODIC_ENA);

		iwx_notif_intr(sc);
	}

	rv = 1;

 out_ena:
	iwx_restore_interrupts(sc);
 out:
	return rv;
}

int
iwx_intr_msix(void *arg)
{
	struct iwx_softc *sc = arg;
	struct ieee80211com *ic = &sc->sc_ic;
	struct ifnet *ifp = IC2IFP(ic);
	uint32_t inta_fh, inta_hw;
	int vector = 0;

	inta_fh = IWX_READ(sc, IWX_CSR_MSIX_FH_INT_CAUSES_AD);
	inta_hw = IWX_READ(sc, IWX_CSR_MSIX_HW_INT_CAUSES_AD);
	IWX_WRITE(sc, IWX_CSR_MSIX_FH_INT_CAUSES_AD, inta_fh);
	IWX_WRITE(sc, IWX_CSR_MSIX_HW_INT_CAUSES_AD, inta_hw);
	inta_fh &= sc->sc_fh_mask;
	inta_hw &= sc->sc_hw_mask;

	if (inta_fh & IWX_MSIX_FH_INT_CAUSES_Q0 ||
	    inta_fh & IWX_MSIX_FH_INT_CAUSES_Q1) {
		iwx_notif_intr(sc);
	}

	/* firmware chunk loaded */
	if (inta_fh & IWX_MSIX_FH_INT_CAUSES_D2S_CH0_NUM) {
		sc->sc_fw_chunk_done = 1;
		wakeup(&sc->sc_fw);
	}

	if ((inta_fh & IWX_MSIX_FH_INT_CAUSES_FH_ERR) ||
	    (inta_hw & IWX_MSIX_HW_INT_CAUSES_REG_SW_ERR) ||
	    (inta_hw & IWX_MSIX_HW_INT_CAUSES_REG_SW_ERR_V2)) {
		if (ifp->if_flags & IFF_DEBUG) {
			iwx_nic_error(sc);
			iwx_dump_driver_status(sc);
		}
		printf("%s: fatal firmware error\n", DEVNAME(sc));
		if ((sc->sc_flags & IWX_FLAG_SHUTDOWN) == 0)
			task_add(systq, &sc->init_task);
		return 1;
	}

	if (inta_hw & IWX_MSIX_HW_INT_CAUSES_REG_RF_KILL) {
		iwx_check_rfkill(sc);
		task_add(systq, &sc->init_task);
	}

	if (inta_hw & IWX_MSIX_HW_INT_CAUSES_REG_HW_ERR) {
		printf("%s: hardware error, stopping device \n", DEVNAME(sc));
		if ((sc->sc_flags & IWX_FLAG_SHUTDOWN) == 0) {
			sc->sc_flags |= IWX_FLAG_HW_ERR;
			task_add(systq, &sc->init_task);
		}
		return 1;
	}

	if (inta_hw & IWX_MSIX_HW_INT_CAUSES_REG_ALIVE) {
		int i;

		/* Firmware has now configured the RFH. */
		for (i = 0; i < IWX_RX_MQ_RING_COUNT; i++)
			iwx_update_rx_desc(sc, &sc->rxq, i);
		IWX_WRITE(sc, IWX_RFH_Q0_FRBDCB_WIDX_TRG, 8);
	}

	/*
	 * Before sending the interrupt the HW disables it to prevent
	 * a nested interrupt. This is done by writing 1 to the corresponding
	 * bit in the mask register. After handling the interrupt, it should be
	 * re-enabled by clearing this bit. This register is defined as
	 * write 1 clear (W1C) register, meaning that it's being clear
	 * by writing 1 to the bit.
	 */
	IWX_WRITE(sc, IWX_CSR_MSIX_AUTOMASK_ST_AD, 1 << vector);
	return 1;
}

typedef void *iwx_match_t;

static const struct pci_matchid iwx_devices[] = {
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_WL_22500_1 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_WL_22500_2 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_WL_22500_3 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_WL_22500_4,},
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_WL_22500_5,},
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_WL_22500_6,},
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_WL_22500_7,},
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_WL_22500_8,},
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_WL_22500_9,},
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_WL_22500_10,},
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_WL_22500_11,},
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_WL_22500_12,},
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_WL_22500_13,},
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_WL_22500_14,},
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_WL_22500_15,},
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_WL_22500_16,},
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_WL_22500_17,},
};


int
iwx_match(struct device *parent, iwx_match_t match __unused, void *aux)
{
	struct pci_attach_args *pa = aux;
	return pci_matchbyid(pa, iwx_devices, nitems(iwx_devices));
}

/*
 * The device info table below contains device-specific config overrides.
 * The most important parameter derived from this table is the name of the
 * firmware image to load.
 *
 * The Linux iwlwifi driver uses an "old" and a "new" device info table.
 * The "old" table matches devices based on PCI vendor/product IDs only.
 * The "new" table extends this with various device parameters derived
 * from MAC type, and RF type.
 *
 * In iwlwifi "old" and "new" tables share the same array, where "old"
 * entries contain dummy values for data defined only for "new" entries.
 * As of 2022, Linux developers are still in the process of moving entries
 * from "old" to "new" style and it looks like this effort has stalled in
 * in some work-in-progress state for quite a while. Linux commits moving
 * entries from "old" to "new" have at times been reverted due to regressions.
 * Part of this complexity comes from iwlwifi supporting both iwm(4) and iwx(4)
 * devices in the same driver.
 *
 * Our table below contains mostly "new" entries declared in iwlwifi
 * with the _IWL_DEV_INFO() macro (with a leading underscore).
 * Other devices are matched based on PCI vendor/product ID as usual,
 * unless matching specific PCI subsystem vendor/product IDs is required.
 *
 * Some "old"-style entries are required to identify the firmware image to use.
 * Others might be used to print a specific marketing name into Linux dmesg,
 * but we can't be sure whether the corresponding devices would be matched
 * correctly in the absence of their entries. So we include them just in case.
 */

struct iwx_dev_info {
	uint16_t device;
	uint16_t subdevice;
	uint16_t mac_type;
	uint16_t rf_type;
	uint8_t mac_step;
	uint8_t rf_id;
	uint8_t no_160;
	uint8_t cores;
	uint8_t cdb;
	uint8_t jacket;
	const struct iwx_device_cfg *cfg;
};

#define _IWX_DEV_INFO(_device, _subdevice, _mac_type, _mac_step, _rf_type, \
		      _rf_id, _no_160, _cores, _cdb, _jacket, _cfg) \
	{ .device = (_device), .subdevice = (_subdevice), .cfg = &(_cfg),  \
	  .mac_type = _mac_type, .rf_type = _rf_type,	   \
	  .no_160 = _no_160, .cores = _cores, .rf_id = _rf_id,		   \
	  .mac_step = _mac_step, .cdb = _cdb, .jacket = _jacket }

#define IWX_DEV_INFO(_device, _subdevice, _cfg) \
	_IWX_DEV_INFO(_device, _subdevice, IWX_CFG_ANY, IWX_CFG_ANY,	   \
		      IWX_CFG_ANY, IWX_CFG_ANY, IWX_CFG_ANY, IWX_CFG_ANY,  \
		      IWX_CFG_ANY, IWX_CFG_ANY, _cfg)

/*
 * When adding entries to this table keep in mind that entries must
 * be listed in the same order as in the Linux driver. Code walks this
 * table backwards and uses the first matching entry it finds.
 * Device firmware must be available in fw_update(8).
 */
static const struct iwx_dev_info iwx_dev_info_table[] = {
	/* So with HR */
	IWX_DEV_INFO(0x2725, 0x0090, iwx_2ax_cfg_so_gf_a0),
	IWX_DEV_INFO(0x2725, 0x0020, iwx_2ax_cfg_ty_gf_a0),
	IWX_DEV_INFO(0x2725, 0x2020, iwx_2ax_cfg_ty_gf_a0),
	IWX_DEV_INFO(0x2725, 0x0024, iwx_2ax_cfg_ty_gf_a0),
	IWX_DEV_INFO(0x2725, 0x0310, iwx_2ax_cfg_ty_gf_a0),
	IWX_DEV_INFO(0x2725, 0x0510, iwx_2ax_cfg_ty_gf_a0),
	IWX_DEV_INFO(0x2725, 0x0A10, iwx_2ax_cfg_ty_gf_a0),
	IWX_DEV_INFO(0x2725, 0xE020, iwx_2ax_cfg_ty_gf_a0),
	IWX_DEV_INFO(0x2725, 0xE024, iwx_2ax_cfg_ty_gf_a0),
	IWX_DEV_INFO(0x2725, 0x4020, iwx_2ax_cfg_ty_gf_a0),
	IWX_DEV_INFO(0x2725, 0x6020, iwx_2ax_cfg_ty_gf_a0),
	IWX_DEV_INFO(0x2725, 0x6024, iwx_2ax_cfg_ty_gf_a0),
	IWX_DEV_INFO(0x2725, 0x1673, iwx_2ax_cfg_ty_gf_a0), /* killer_1675w */
	IWX_DEV_INFO(0x2725, 0x1674, iwx_2ax_cfg_ty_gf_a0), /* killer_1675x */
	IWX_DEV_INFO(0x51f0, 0x1691, iwx_2ax_cfg_so_gf4_a0), /* killer_1690s */
	IWX_DEV_INFO(0x51f0, 0x1692, iwx_2ax_cfg_so_gf4_a0), /* killer_1690i */
	IWX_DEV_INFO(0x51f1, 0x1691, iwx_2ax_cfg_so_gf4_a0),
	IWX_DEV_INFO(0x51f1, 0x1692, iwx_2ax_cfg_so_gf4_a0),
	IWX_DEV_INFO(0x54f0, 0x1691, iwx_2ax_cfg_so_gf4_a0), /* killer_1690s */
	IWX_DEV_INFO(0x54f0, 0x1692, iwx_2ax_cfg_so_gf4_a0), /* killer_1690i */
	IWX_DEV_INFO(0x7a70, 0x0090, iwx_2ax_cfg_so_gf_a0_long),
	IWX_DEV_INFO(0x7a70, 0x0098, iwx_2ax_cfg_so_gf_a0_long),
	IWX_DEV_INFO(0x7a70, 0x00b0, iwx_2ax_cfg_so_gf4_a0_long),
	IWX_DEV_INFO(0x7a70, 0x0310, iwx_2ax_cfg_so_gf_a0_long),
	IWX_DEV_INFO(0x7a70, 0x0510, iwx_2ax_cfg_so_gf_a0_long),
	IWX_DEV_INFO(0x7a70, 0x0a10, iwx_2ax_cfg_so_gf_a0_long),
	IWX_DEV_INFO(0x7af0, 0x0090, iwx_2ax_cfg_so_gf_a0),
	IWX_DEV_INFO(0x7af0, 0x0098, iwx_2ax_cfg_so_gf_a0),
	IWX_DEV_INFO(0x7af0, 0x00b0, iwx_2ax_cfg_so_gf4_a0),
	IWX_DEV_INFO(0x7a70, 0x1691, iwx_2ax_cfg_so_gf4_a0), /* killer_1690s */
	IWX_DEV_INFO(0x7a70, 0x1692, iwx_2ax_cfg_so_gf4_a0), /* killer_1690i */
	IWX_DEV_INFO(0x7af0, 0x0310, iwx_2ax_cfg_so_gf_a0),
	IWX_DEV_INFO(0x7af0, 0x0510, iwx_2ax_cfg_so_gf_a0),
	IWX_DEV_INFO(0x7af0, 0x0a10, iwx_2ax_cfg_so_gf_a0),
	IWX_DEV_INFO(0x7f70, 0x1691, iwx_2ax_cfg_so_gf4_a0), /* killer_1690s */
	IWX_DEV_INFO(0x7f70, 0x1692, iwx_2ax_cfg_so_gf4_a0), /* killer_1690i */

	/* So with GF2 */
	IWX_DEV_INFO(0x2726, 0x1671, iwx_2ax_cfg_so_gf_a0), /* killer_1675s */
	IWX_DEV_INFO(0x2726, 0x1672, iwx_2ax_cfg_so_gf_a0), /* killer_1675i */
	IWX_DEV_INFO(0x51f0, 0x1671, iwx_2ax_cfg_so_gf_a0), /* killer_1675s */
	IWX_DEV_INFO(0x51f0, 0x1672, iwx_2ax_cfg_so_gf_a0), /* killer_1675i */
	IWX_DEV_INFO(0x54f0, 0x1671, iwx_2ax_cfg_so_gf_a0), /* killer_1675s */
	IWX_DEV_INFO(0x54f0, 0x1672, iwx_2ax_cfg_so_gf_a0), /* killer_1675i */
	IWX_DEV_INFO(0x7a70, 0x1671, iwx_2ax_cfg_so_gf_a0), /* killer_1675s */
	IWX_DEV_INFO(0x7a70, 0x1672, iwx_2ax_cfg_so_gf_a0), /* killer_1675i */
	IWX_DEV_INFO(0x7af0, 0x1671, iwx_2ax_cfg_so_gf_a0), /* killer_1675s */
	IWX_DEV_INFO(0x7af0, 0x1672, iwx_2ax_cfg_so_gf_a0), /* killer_1675i */
	IWX_DEV_INFO(0x7f70, 0x1671, iwx_2ax_cfg_so_gf_a0), /* killer_1675s */
	IWX_DEV_INFO(0x7f70, 0x1672, iwx_2ax_cfg_so_gf_a0), /* killer_1675i */

	/* MA with GF2 */
	IWX_DEV_INFO(0x7e40, 0x1671, iwx_cfg_ma_b0_gf_a0), /* killer_1675s */
	IWX_DEV_INFO(0x7e40, 0x1672, iwx_cfg_ma_b0_gf_a0), /* killer_1675i */

	/* Qu with Jf, C step */
	_IWX_DEV_INFO(IWX_CFG_ANY, IWX_CFG_ANY,
		      IWX_CFG_MAC_TYPE_QU, IWX_SILICON_C_STEP,
		      IWX_CFG_RF_TYPE_JF1, IWX_CFG_RF_ID_JF1,
		      IWX_CFG_160, IWX_CFG_CORES_BT, IWX_CFG_NO_CDB,
		      IWX_CFG_ANY, iwx_9560_qu_c0_jf_b0_cfg), /* 9461_160 */
	_IWX_DEV_INFO(IWX_CFG_ANY, IWX_CFG_ANY,
		      IWX_CFG_MAC_TYPE_QU, IWX_SILICON_C_STEP,
		      IWX_CFG_RF_TYPE_JF1, IWX_CFG_RF_ID_JF1,
		      IWX_CFG_NO_160, IWX_CFG_CORES_BT, IWX_CFG_NO_CDB,
		      IWX_CFG_ANY, iwx_9560_qu_c0_jf_b0_cfg), /* iwl9461 */
	_IWX_DEV_INFO(IWX_CFG_ANY, IWX_CFG_ANY,
		      IWX_CFG_MAC_TYPE_QU, IWX_SILICON_C_STEP,
		      IWX_CFG_RF_TYPE_JF1, IWX_CFG_RF_ID_JF1_DIV,
		      IWX_CFG_160, IWX_CFG_CORES_BT, IWX_CFG_NO_CDB,
		      IWX_CFG_ANY, iwx_9560_qu_c0_jf_b0_cfg), /* 9462_160 */
	_IWX_DEV_INFO(IWX_CFG_ANY, IWX_CFG_ANY,
		      IWX_CFG_MAC_TYPE_QU, IWX_SILICON_C_STEP,
		      IWX_CFG_RF_TYPE_JF1, IWX_CFG_RF_ID_JF1_DIV,
		      IWX_CFG_NO_160, IWX_CFG_CORES_BT, IWX_CFG_NO_CDB,
		      IWX_CFG_ANY, iwx_9560_qu_c0_jf_b0_cfg), /* 9462 */
	_IWX_DEV_INFO(IWX_CFG_ANY, IWX_CFG_ANY,
		      IWX_CFG_MAC_TYPE_QU, IWX_SILICON_C_STEP,
		      IWX_CFG_RF_TYPE_JF2, IWX_CFG_RF_ID_JF,
		      IWX_CFG_160, IWX_CFG_CORES_BT, IWX_CFG_NO_CDB,
		      IWX_CFG_ANY, iwx_9560_qu_c0_jf_b0_cfg), /* 9560_160 */
	_IWX_DEV_INFO(IWX_CFG_ANY, IWX_CFG_ANY,
		      IWX_CFG_MAC_TYPE_QU, IWX_SILICON_C_STEP,
		      IWX_CFG_RF_TYPE_JF2, IWX_CFG_RF_ID_JF,
		      IWX_CFG_NO_160, IWX_CFG_CORES_BT, IWX_CFG_NO_CDB,
		      IWX_CFG_ANY, iwx_9560_qu_c0_jf_b0_cfg), /* 9560 */
	_IWX_DEV_INFO(IWX_CFG_ANY, 0x1551,
		      IWX_CFG_MAC_TYPE_QU, IWX_SILICON_C_STEP,
		      IWX_CFG_RF_TYPE_JF2, IWX_CFG_RF_ID_JF,
		      IWX_CFG_160, IWX_CFG_CORES_BT, IWX_CFG_NO_CDB,
		      IWX_CFG_ANY,
		      iwx_9560_qu_c0_jf_b0_cfg), /* 9560_killer_1550s */
	_IWX_DEV_INFO(IWX_CFG_ANY, 0x1552,
		      IWX_CFG_MAC_TYPE_QU, IWX_SILICON_C_STEP,
		      IWX_CFG_RF_TYPE_JF2, IWX_CFG_RF_ID_JF,
		      IWX_CFG_NO_160, IWX_CFG_CORES_BT, IWX_CFG_NO_CDB,
		      IWX_CFG_ANY,
		      iwx_9560_qu_c0_jf_b0_cfg), /* 9560_killer_1550i */

	/* QuZ with Jf */
	_IWX_DEV_INFO(IWX_CFG_ANY, IWX_CFG_ANY,
		      IWX_CFG_MAC_TYPE_QUZ, IWX_CFG_ANY,
		      IWX_CFG_RF_TYPE_JF2, IWX_CFG_RF_ID_JF,
		      IWX_CFG_160, IWX_CFG_CORES_BT, IWX_CFG_NO_CDB,
		      IWX_CFG_ANY, iwx_9560_quz_a0_jf_b0_cfg), /* 9461_160 */
	_IWX_DEV_INFO(IWX_CFG_ANY, IWX_CFG_ANY,
		      IWX_CFG_MAC_TYPE_QUZ, IWX_CFG_ANY,
		      IWX_CFG_RF_TYPE_JF2, IWX_CFG_RF_ID_JF,
		      IWX_CFG_NO_160, IWX_CFG_CORES_BT, IWX_CFG_NO_CDB,
		      IWX_CFG_ANY, iwx_9560_quz_a0_jf_b0_cfg), /* 9461 */
	_IWX_DEV_INFO(IWX_CFG_ANY, IWX_CFG_ANY,
		      IWX_CFG_MAC_TYPE_QUZ, IWX_CFG_ANY,
		      IWX_CFG_RF_TYPE_JF1, IWX_CFG_RF_ID_JF1_DIV,
		      IWX_CFG_160, IWX_CFG_CORES_BT, IWX_CFG_NO_CDB,
		      IWX_CFG_ANY, iwx_9560_quz_a0_jf_b0_cfg), /* 9462_160 */
	_IWX_DEV_INFO(IWX_CFG_ANY, IWX_CFG_ANY,
		      IWX_CFG_MAC_TYPE_QUZ, IWX_CFG_ANY,
		      IWX_CFG_RF_TYPE_JF1, IWX_CFG_RF_ID_JF1_DIV,
		      IWX_CFG_NO_160, IWX_CFG_CORES_BT, IWX_CFG_NO_CDB,
		      IWX_CFG_ANY, iwx_9560_quz_a0_jf_b0_cfg), /* 9462 */
	_IWX_DEV_INFO(IWX_CFG_ANY, 0x1551,
		      IWX_CFG_MAC_TYPE_QUZ, IWX_CFG_ANY,
		      IWX_CFG_RF_TYPE_JF2, IWX_CFG_RF_ID_JF,
		      IWX_CFG_160, IWX_CFG_CORES_BT, IWX_CFG_NO_CDB,
		      IWX_CFG_ANY,
		      iwx_9560_quz_a0_jf_b0_cfg), /* killer_1550s */
	_IWX_DEV_INFO(IWX_CFG_ANY, 0x1552,
		      IWX_CFG_MAC_TYPE_QUZ, IWX_CFG_ANY,
		      IWX_CFG_RF_TYPE_JF2, IWX_CFG_RF_ID_JF,
		      IWX_CFG_NO_160, IWX_CFG_CORES_BT, IWX_CFG_NO_CDB,
		      IWX_CFG_ANY,
		      iwx_9560_quz_a0_jf_b0_cfg), /* 9560_killer_1550i */

	/* Qu with Hr, B step */
	_IWX_DEV_INFO(IWX_CFG_ANY, IWX_CFG_ANY,
		      IWX_CFG_MAC_TYPE_QU, IWX_SILICON_B_STEP,
		      IWX_CFG_RF_TYPE_HR1, IWX_CFG_ANY,
		      IWX_CFG_ANY, IWX_CFG_ANY, IWX_CFG_NO_CDB, IWX_CFG_ANY,
		      iwx_qu_b0_hr1_b0), /* AX101 */
	_IWX_DEV_INFO(IWX_CFG_ANY, IWX_CFG_ANY,
		      IWX_CFG_MAC_TYPE_QU, IWX_SILICON_B_STEP,
		      IWX_CFG_RF_TYPE_HR2, IWX_CFG_ANY,
		      IWX_CFG_NO_160, IWX_CFG_ANY, IWX_CFG_NO_CDB, IWX_CFG_ANY,
		      iwx_qu_b0_hr_b0), /* AX203 */

	/* Qu with Hr, C step */
	_IWX_DEV_INFO(IWX_CFG_ANY, IWX_CFG_ANY,
		      IWX_CFG_MAC_TYPE_QU, IWX_SILICON_C_STEP,
		      IWX_CFG_RF_TYPE_HR1, IWX_CFG_ANY,
		      IWX_CFG_ANY, IWX_CFG_ANY, IWX_CFG_NO_CDB, IWX_CFG_ANY,
		      iwx_qu_c0_hr1_b0), /* AX101 */
	_IWX_DEV_INFO(IWX_CFG_ANY, IWX_CFG_ANY,
		      IWX_CFG_MAC_TYPE_QU, IWX_SILICON_C_STEP,
		      IWX_CFG_RF_TYPE_HR2, IWX_CFG_ANY,
		      IWX_CFG_NO_160, IWX_CFG_ANY, IWX_CFG_NO_CDB, IWX_CFG_ANY,
		      iwx_qu_c0_hr_b0), /* AX203 */
	_IWX_DEV_INFO(IWX_CFG_ANY, IWX_CFG_ANY,
		      IWX_CFG_MAC_TYPE_QU, IWX_SILICON_C_STEP,
		      IWX_CFG_RF_TYPE_HR2, IWX_CFG_ANY,
		      IWX_CFG_160, IWX_CFG_ANY, IWX_CFG_NO_CDB, IWX_CFG_ANY,
		      iwx_qu_c0_hr_b0), /* AX201 */

	/* QuZ with Hr */
	_IWX_DEV_INFO(IWX_CFG_ANY, IWX_CFG_ANY,
		      IWX_CFG_MAC_TYPE_QUZ, IWX_CFG_ANY,
		      IWX_CFG_RF_TYPE_HR1, IWX_CFG_ANY,
		      IWX_CFG_ANY, IWX_CFG_ANY, IWX_CFG_NO_CDB, IWX_CFG_ANY,
		      iwx_quz_a0_hr1_b0), /* AX101 */
	_IWX_DEV_INFO(IWX_CFG_ANY, IWX_CFG_ANY,
		      IWX_CFG_MAC_TYPE_QUZ, IWX_SILICON_B_STEP,
		      IWX_CFG_RF_TYPE_HR2, IWX_CFG_ANY,
		      IWX_CFG_NO_160, IWX_CFG_ANY, IWX_CFG_NO_CDB, IWX_CFG_ANY,
		      iwx_cfg_quz_a0_hr_b0), /* AX203 */

	/* SoF with JF2 */
	_IWX_DEV_INFO(IWX_CFG_ANY, IWX_CFG_ANY,
		      IWX_CFG_MAC_TYPE_SOF, IWX_CFG_ANY,
		      IWX_CFG_RF_TYPE_JF2, IWX_CFG_RF_ID_JF,
		      IWX_CFG_160, IWX_CFG_CORES_BT, IWX_CFG_NO_CDB,
		      IWX_CFG_ANY, iwx_2ax_cfg_so_jf_b0), /* 9560_160 */
	_IWX_DEV_INFO(IWX_CFG_ANY, IWX_CFG_ANY,
		      IWX_CFG_MAC_TYPE_SOF, IWX_CFG_ANY,
		      IWX_CFG_RF_TYPE_JF2, IWX_CFG_RF_ID_JF,
		      IWX_CFG_NO_160, IWX_CFG_CORES_BT, IWX_CFG_NO_CDB,
		      IWX_CFG_ANY, iwx_2ax_cfg_so_jf_b0), /* 9560 */

	/* SoF with JF */
	_IWX_DEV_INFO(IWX_CFG_ANY, IWX_CFG_ANY,
		      IWX_CFG_MAC_TYPE_SOF, IWX_CFG_ANY,
		      IWX_CFG_RF_TYPE_JF1, IWX_CFG_RF_ID_JF1,
		      IWX_CFG_160, IWX_CFG_CORES_BT, IWX_CFG_NO_CDB,
		      IWX_CFG_ANY, iwx_2ax_cfg_so_jf_b0), /* 9461_160 */
	_IWX_DEV_INFO(IWX_CFG_ANY, IWX_CFG_ANY,
		      IWX_CFG_MAC_TYPE_SOF, IWX_CFG_ANY,
		      IWX_CFG_RF_TYPE_JF1, IWX_CFG_RF_ID_JF1_DIV,
		      IWX_CFG_160, IWX_CFG_CORES_BT, IWX_CFG_NO_CDB,
		      IWX_CFG_ANY, iwx_2ax_cfg_so_jf_b0), /* 9462_160 */
	_IWX_DEV_INFO(IWX_CFG_ANY, IWX_CFG_ANY,
		      IWX_CFG_MAC_TYPE_SOF, IWX_CFG_ANY,
		      IWX_CFG_RF_TYPE_JF1, IWX_CFG_RF_ID_JF1,
		      IWX_CFG_NO_160, IWX_CFG_CORES_BT, IWX_CFG_NO_CDB,
		      IWX_CFG_ANY, iwx_2ax_cfg_so_jf_b0), /* 9461_name */
	_IWX_DEV_INFO(IWX_CFG_ANY, IWX_CFG_ANY,
		      IWX_CFG_MAC_TYPE_SOF, IWX_CFG_ANY,
		      IWX_CFG_RF_TYPE_JF1, IWX_CFG_RF_ID_JF1_DIV,
		      IWX_CFG_NO_160, IWX_CFG_CORES_BT, IWX_CFG_NO_CDB,
		      IWX_CFG_ANY, iwx_2ax_cfg_so_jf_b0), /* 9462 */

	/* So with Hr */
	_IWX_DEV_INFO(IWX_CFG_ANY, IWX_CFG_ANY,
		      IWX_CFG_MAC_TYPE_SO, IWX_CFG_ANY,
		      IWX_CFG_RF_TYPE_HR2, IWX_CFG_ANY,
		      IWX_CFG_NO_160, IWX_CFG_ANY, IWX_CFG_NO_CDB, IWX_CFG_ANY,
		      iwx_cfg_so_a0_hr_b0), /* AX203 */
	_IWX_DEV_INFO(IWX_CFG_ANY, IWX_CFG_ANY,
		      IWX_CFG_MAC_TYPE_SO, IWX_CFG_ANY,
		      IWX_CFG_RF_TYPE_HR1, IWX_CFG_ANY,
		      IWX_CFG_NO_160, IWX_CFG_ANY, IWX_CFG_NO_CDB, IWX_CFG_ANY,
		      iwx_cfg_so_a0_hr_b0), /* ax101 */
	_IWX_DEV_INFO(IWX_CFG_ANY, IWX_CFG_ANY,
		      IWX_CFG_MAC_TYPE_SO, IWX_CFG_ANY,
		      IWX_CFG_RF_TYPE_HR2, IWX_CFG_ANY,
		      IWX_CFG_160, IWX_CFG_ANY, IWX_CFG_NO_CDB, IWX_CFG_ANY,
		      iwx_cfg_so_a0_hr_b0), /* ax201 */

	/* So-F with Hr */
	_IWX_DEV_INFO(IWX_CFG_ANY, IWX_CFG_ANY,
		      IWX_CFG_MAC_TYPE_SOF, IWX_CFG_ANY,
		      IWX_CFG_RF_TYPE_HR2, IWX_CFG_ANY,
		      IWX_CFG_NO_160, IWX_CFG_ANY, IWX_CFG_NO_CDB, IWX_CFG_ANY,
		      iwx_cfg_so_a0_hr_b0), /* AX203 */
	_IWX_DEV_INFO(IWX_CFG_ANY, IWX_CFG_ANY,
		      IWX_CFG_MAC_TYPE_SOF, IWX_CFG_ANY,
		      IWX_CFG_RF_TYPE_HR1, IWX_CFG_ANY,
		      IWX_CFG_NO_160, IWX_CFG_ANY, IWX_CFG_NO_CDB, IWX_CFG_ANY,
		      iwx_cfg_so_a0_hr_b0), /* AX101 */
	_IWX_DEV_INFO(IWX_CFG_ANY, IWX_CFG_ANY,
		      IWX_CFG_MAC_TYPE_SOF, IWX_CFG_ANY,
		      IWX_CFG_RF_TYPE_HR2, IWX_CFG_ANY,
		      IWX_CFG_160, IWX_CFG_ANY, IWX_CFG_NO_CDB, IWX_CFG_ANY,
		      iwx_cfg_so_a0_hr_b0), /* AX201 */

	/* So-F with GF */
	_IWX_DEV_INFO(IWX_CFG_ANY, IWX_CFG_ANY,
		      IWX_CFG_MAC_TYPE_SOF, IWX_CFG_ANY,
		      IWX_CFG_RF_TYPE_GF, IWX_CFG_ANY,
		      IWX_CFG_160, IWX_CFG_ANY, IWX_CFG_NO_CDB, IWX_CFG_ANY,
		      iwx_2ax_cfg_so_gf_a0), /* AX211 */
	_IWX_DEV_INFO(IWX_CFG_ANY, IWX_CFG_ANY,
		      IWX_CFG_MAC_TYPE_SOF, IWX_CFG_ANY,
		      IWX_CFG_RF_TYPE_GF, IWX_CFG_ANY,
		      IWX_CFG_160, IWX_CFG_ANY, IWX_CFG_CDB, IWX_CFG_ANY,
		      iwx_2ax_cfg_so_gf4_a0), /* AX411 */

	/* So with GF */
	_IWX_DEV_INFO(IWX_CFG_ANY, IWX_CFG_ANY,
		      IWX_CFG_MAC_TYPE_SO, IWX_CFG_ANY,
		      IWX_CFG_RF_TYPE_GF, IWX_CFG_ANY,
		      IWX_CFG_160, IWX_CFG_ANY, IWX_CFG_NO_CDB, IWX_CFG_ANY,
		      iwx_2ax_cfg_so_gf_a0), /* AX211 */
	_IWX_DEV_INFO(IWX_CFG_ANY, IWX_CFG_ANY,
		      IWX_CFG_MAC_TYPE_SO, IWX_CFG_ANY,
		      IWX_CFG_RF_TYPE_GF, IWX_CFG_ANY,
		      IWX_CFG_160, IWX_CFG_ANY, IWX_CFG_CDB, IWX_CFG_ANY,
		      iwx_2ax_cfg_so_gf4_a0), /* AX411 */

	/* So with JF2 */
	_IWX_DEV_INFO(IWX_CFG_ANY, IWX_CFG_ANY,
		      IWX_CFG_MAC_TYPE_SO, IWX_CFG_ANY,
		      IWX_CFG_RF_TYPE_JF2, IWX_CFG_RF_ID_JF,
		      IWX_CFG_160, IWX_CFG_CORES_BT, IWX_CFG_NO_CDB,
		      IWX_CFG_ANY, iwx_2ax_cfg_so_jf_b0), /* 9560_160 */
	_IWX_DEV_INFO(IWX_CFG_ANY, IWX_CFG_ANY,
		      IWX_CFG_MAC_TYPE_SO, IWX_CFG_ANY,
		      IWX_CFG_RF_TYPE_JF2, IWX_CFG_RF_ID_JF,
		      IWX_CFG_NO_160, IWX_CFG_CORES_BT, IWX_CFG_NO_CDB,
		      IWX_CFG_ANY, iwx_2ax_cfg_so_jf_b0), /* 9560 */

	/* So with JF */
	_IWX_DEV_INFO(IWX_CFG_ANY, IWX_CFG_ANY,
		      IWX_CFG_MAC_TYPE_SO, IWX_CFG_ANY,
		      IWX_CFG_RF_TYPE_JF1, IWX_CFG_RF_ID_JF1,
		      IWX_CFG_160, IWX_CFG_CORES_BT, IWX_CFG_NO_CDB,
		      IWX_CFG_ANY, iwx_2ax_cfg_so_jf_b0), /* 9461_160 */
	_IWX_DEV_INFO(IWX_CFG_ANY, IWX_CFG_ANY,
		      IWX_CFG_MAC_TYPE_SO, IWX_CFG_ANY,
		      IWX_CFG_RF_TYPE_JF1, IWX_CFG_RF_ID_JF1_DIV,
		      IWX_CFG_160, IWX_CFG_CORES_BT, IWX_CFG_NO_CDB,
		      IWX_CFG_ANY, iwx_2ax_cfg_so_jf_b0), /* 9462_160 */
	_IWX_DEV_INFO(IWX_CFG_ANY, IWX_CFG_ANY,
		      IWX_CFG_MAC_TYPE_SO, IWX_CFG_ANY,
		      IWX_CFG_RF_TYPE_JF1, IWX_CFG_RF_ID_JF1,
		      IWX_CFG_NO_160, IWX_CFG_CORES_BT, IWX_CFG_NO_CDB,
		      IWX_CFG_ANY, iwx_2ax_cfg_so_jf_b0), /* iwl9461 */
	_IWX_DEV_INFO(IWX_CFG_ANY, IWX_CFG_ANY,
		      IWX_CFG_MAC_TYPE_SO, IWX_CFG_ANY,
		      IWX_CFG_RF_TYPE_JF1, IWX_CFG_RF_ID_JF1_DIV,
		      IWX_CFG_NO_160, IWX_CFG_CORES_BT, IWX_CFG_NO_CDB,
		      IWX_CFG_ANY, iwx_2ax_cfg_so_jf_b0), /* 9462 */

	/* Ma */
	_IWX_DEV_INFO(IWX_CFG_ANY, IWX_CFG_ANY,
		      IWX_CFG_MAC_TYPE_MA, IWX_CFG_ANY,
		      IWX_CFG_RF_TYPE_HR2, IWX_CFG_ANY,
		      IWX_CFG_ANY, IWX_CFG_ANY, IWX_CFG_NO_CDB,
		      IWX_CFG_ANY, iwx_cfg_ma_b0_hr_b0), /* ax201 */
	_IWX_DEV_INFO(IWX_CFG_ANY, IWX_CFG_ANY,
		      IWX_CFG_MAC_TYPE_MA, IWX_CFG_ANY,
		      IWX_CFG_RF_TYPE_GF, IWX_CFG_ANY,
		      IWX_CFG_ANY, IWX_CFG_ANY, IWX_CFG_NO_CDB,
		      IWX_CFG_ANY, iwx_cfg_ma_b0_gf_a0), /* ax211 */
	_IWX_DEV_INFO(IWX_CFG_ANY, IWX_CFG_ANY,
		      IWX_CFG_MAC_TYPE_MA, IWX_CFG_ANY,
		      IWX_CFG_RF_TYPE_GF, IWX_CFG_ANY,
		      IWX_CFG_ANY, IWX_CFG_ANY, IWX_CFG_CDB,
		      IWX_CFG_ANY, iwx_cfg_ma_b0_gf4_a0), /* ax211 */
	_IWX_DEV_INFO(IWX_CFG_ANY, IWX_CFG_ANY,
		      IWX_CFG_MAC_TYPE_MA, IWX_CFG_ANY,
		      IWX_CFG_RF_TYPE_FM, IWX_CFG_ANY,
		      IWX_CFG_ANY, IWX_CFG_ANY, IWX_CFG_NO_CDB,
		      IWX_CFG_ANY, iwx_cfg_ma_a0_fm_a0), /* ax231 */
};

int
iwx_preinit(struct iwx_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ifnet *ifp = IC2IFP(ic);
	int err;

	err = iwx_prepare_card_hw(sc);
	if (err) {
		printf("%s: could not initialize hardware\n", DEVNAME(sc));
		return err;
	}

	if (sc->attached) {
		/* Update MAC in case the upper layers changed it. */
		IEEE80211_ADDR_COPY(sc->sc_ic.ic_myaddr,
		    ((struct arpcom *)ifp)->ac_enaddr);
		return 0;
	}

	err = iwx_start_hw(sc);
	if (err) {
		printf("%s: could not initialize hardware\n", DEVNAME(sc));
		return err;
	}

	err = iwx_run_init_mvm_ucode(sc, 1);
	iwx_stop_device(sc);
	if (err)
		return err;

	/* Print version info and MAC address on first successful fw load. */
	sc->attached = 1;
	if (sc->sc_pnvm_ver) {
		printf("%s: hw rev 0x%x, fw %s, pnvm %08x, "
		    "address %s\n",
		    DEVNAME(sc), sc->sc_hw_rev & IWX_CSR_HW_REV_TYPE_MSK,
		    sc->sc_fwver, sc->sc_pnvm_ver,
		    ether_sprintf(sc->sc_nvm.hw_addr));
	} else {
		printf("%s: hw rev 0x%x, fw %s, address %s\n",
		    DEVNAME(sc), sc->sc_hw_rev & IWX_CSR_HW_REV_TYPE_MSK,
		    sc->sc_fwver, ether_sprintf(sc->sc_nvm.hw_addr));
	}

	if (sc->sc_nvm.sku_cap_11n_enable)
		iwx_setup_ht_rates(sc);
	if (sc->sc_nvm.sku_cap_11ac_enable)
		iwx_setup_vht_rates(sc);

	/* not all hardware can do 5GHz band */
	if (!sc->sc_nvm.sku_cap_band_52GHz_enable)
		memset(&ic->ic_sup_rates[IEEE80211_MODE_11A], 0,
		    sizeof(ic->ic_sup_rates[IEEE80211_MODE_11A]));

	/* Configure channel information obtained from firmware. */
	ieee80211_channel_init(ifp);

	/* Configure MAC address. */
	err = if_setlladdr(ifp, ic->ic_myaddr);
	if (err)
		printf("%s: could not set MAC address (error %d)\n",
		    DEVNAME(sc), err);

	ieee80211_media_init(ifp, iwx_media_change, ieee80211_media_status);

	return 0;
}

void
iwx_attach_hook(struct device *self)
{
	struct iwx_softc *sc = (void *)self;

	KASSERT(!cold);

	iwx_preinit(sc);
}

const struct iwx_device_cfg *
iwx_find_device_cfg(struct iwx_softc *sc)
{
	pcireg_t sreg;
	pci_product_id_t sdev_id;
	uint16_t mac_type, rf_type;
	uint8_t mac_step, cdb, jacket, rf_id, no_160, cores;
	int i;

	sreg = pci_conf_read(sc->sc_pct, sc->sc_pcitag, PCI_SUBSYS_ID_REG);
	sdev_id = PCI_PRODUCT(sreg);
	mac_type = IWX_CSR_HW_REV_TYPE(sc->sc_hw_rev);
	mac_step = IWX_CSR_HW_REV_STEP(sc->sc_hw_rev << 2);
	rf_type = IWX_CSR_HW_RFID_TYPE(sc->sc_hw_rf_id);
	cdb = IWX_CSR_HW_RFID_IS_CDB(sc->sc_hw_rf_id);
	jacket = IWX_CSR_HW_RFID_IS_JACKET(sc->sc_hw_rf_id);

	rf_id = IWX_SUBDEVICE_RF_ID(sdev_id);
	no_160 = IWX_SUBDEVICE_NO_160(sdev_id);
	cores = IWX_SUBDEVICE_CORES(sdev_id);

	for (i = nitems(iwx_dev_info_table) - 1; i >= 0; i--) {
		const struct iwx_dev_info *dev_info = &iwx_dev_info_table[i];

		if (dev_info->device != (uint16_t)IWX_CFG_ANY &&
		    dev_info->device != sc->sc_pid)
			continue;

		if (dev_info->subdevice != (uint16_t)IWX_CFG_ANY &&
		    dev_info->subdevice != sdev_id)
			continue;

		if (dev_info->mac_type != (uint16_t)IWX_CFG_ANY &&
		    dev_info->mac_type != mac_type)
			continue;

		if (dev_info->mac_step != (uint8_t)IWX_CFG_ANY &&
		    dev_info->mac_step != mac_step)
			continue;

		if (dev_info->rf_type != (uint16_t)IWX_CFG_ANY &&
		    dev_info->rf_type != rf_type)
			continue;

		if (dev_info->cdb != (uint8_t)IWX_CFG_ANY &&
		    dev_info->cdb != cdb)
			continue;

		if (dev_info->jacket != (uint8_t)IWX_CFG_ANY &&
		    dev_info->jacket != jacket)
			continue;

		if (dev_info->rf_id != (uint8_t)IWX_CFG_ANY &&
		    dev_info->rf_id != rf_id)
			continue;

		if (dev_info->no_160 != (uint8_t)IWX_CFG_ANY &&
		    dev_info->no_160 != no_160)
			continue;

		if (dev_info->cores != (uint8_t)IWX_CFG_ANY &&
		    dev_info->cores != cores)
			continue;

		return dev_info->cfg;
	}

	return NULL;
}


void
iwx_attach(struct device *parent, struct device *self, void *aux)
{
	struct iwx_softc *sc = (void *)self;
	struct pci_attach_args *pa = aux;
	pci_intr_handle_t ih;
	pcireg_t reg, memtype;
	struct ieee80211com *ic = &sc->sc_ic;
	struct ifnet *ifp = &ic->ic_if;
	const char *intrstr;
	const struct iwx_device_cfg *cfg;
	int err;
	int txq_i, i, j;
	size_t ctxt_info_size;

	sc->sc_pid = PCI_PRODUCT(pa->pa_id);
	sc->sc_pct = pa->pa_pc;
	sc->sc_pcitag = pa->pa_tag;
	sc->sc_dmat = pa->pa_dmat;

	rw_init(&sc->ioctl_rwl, "iwxioctl");

	err = pci_get_capability(sc->sc_pct, sc->sc_pcitag,
	    PCI_CAP_PCIEXPRESS, &sc->sc_cap_off, NULL);
	if (err == 0) {
		printf("%s: PCIe capability structure not found!\n",
		    DEVNAME(sc));
		return;
	}

	/*
	 * We disable the RETRY_TIMEOUT register (0x41) to keep
	 * PCI Tx retries from interfering with C3 CPU state.
	 */
	reg = pci_conf_read(sc->sc_pct, sc->sc_pcitag, 0x40);
	pci_conf_write(sc->sc_pct, sc->sc_pcitag, 0x40, reg & ~0xff00);

	memtype = pci_mapreg_type(pa->pa_pc, pa->pa_tag, PCI_MAPREG_START);
	err = pci_mapreg_map(pa, PCI_MAPREG_START, memtype, 0,
	    &sc->sc_st, &sc->sc_sh, NULL, &sc->sc_sz, 0);
	if (err) {
		printf("%s: can't map mem space\n", DEVNAME(sc));
		return;
	}

	if (pci_intr_map_msix(pa, 0, &ih) == 0) {
		sc->sc_msix = 1;
	} else if (pci_intr_map_msi(pa, &ih)) {
		if (pci_intr_map(pa, &ih)) {
			printf("%s: can't map interrupt\n", DEVNAME(sc));
			return;
		}
		/* Hardware bug workaround. */
		reg = pci_conf_read(sc->sc_pct, sc->sc_pcitag,
		    PCI_COMMAND_STATUS_REG);
		if (reg & PCI_COMMAND_INTERRUPT_DISABLE)
			reg &= ~PCI_COMMAND_INTERRUPT_DISABLE;
		pci_conf_write(sc->sc_pct, sc->sc_pcitag,
		    PCI_COMMAND_STATUS_REG, reg);
	}

	intrstr = pci_intr_string(sc->sc_pct, ih);
	if (sc->sc_msix)
		sc->sc_ih = pci_intr_establish(sc->sc_pct, ih, IPL_NET,
		    iwx_intr_msix, sc, DEVNAME(sc));
	else
		sc->sc_ih = pci_intr_establish(sc->sc_pct, ih, IPL_NET,
		    iwx_intr, sc, DEVNAME(sc));

	if (sc->sc_ih == NULL) {
		printf("\n");
		printf("%s: can't establish interrupt", DEVNAME(sc));
		if (intrstr != NULL)
			printf(" at %s", intrstr);
		printf("\n");
		return;
	}
	printf(", %s\n", intrstr);

	/* Clear pending interrupts. */
	IWX_WRITE(sc, IWX_CSR_INT_MASK, 0);
	IWX_WRITE(sc, IWX_CSR_INT, ~0);
	IWX_WRITE(sc, IWX_CSR_FH_INT_STATUS, ~0);

	sc->sc_hw_rev = IWX_READ(sc, IWX_CSR_HW_REV);
	sc->sc_hw_rf_id = IWX_READ(sc, IWX_CSR_HW_RF_ID);

	/*
	 * In the 8000 HW family the format of the 4 bytes of CSR_HW_REV have
	 * changed, and now the revision step also includes bit 0-1 (no more
	 * "dash" value). To keep hw_rev backwards compatible - we'll store it
	 * in the old format.
	 */
	sc->sc_hw_rev = (sc->sc_hw_rev & 0xfff0) |
			(IWX_CSR_HW_REV_STEP(sc->sc_hw_rev << 2) << 2);

	switch (PCI_PRODUCT(pa->pa_id)) {
	case PCI_PRODUCT_INTEL_WL_22500_1:
		sc->sc_fwname = IWX_CC_A_FW;
		sc->sc_device_family = IWX_DEVICE_FAMILY_22000;
		sc->sc_integrated = 0;
		sc->sc_ltr_delay = IWX_SOC_FLAGS_LTR_APPLY_DELAY_NONE;
		sc->sc_low_latency_xtal = 0;
		sc->sc_xtal_latency = 0;
		sc->sc_tx_with_siso_diversity = 0;
		sc->sc_uhb_supported = 0;
		break;
	case PCI_PRODUCT_INTEL_WL_22500_2:
	case PCI_PRODUCT_INTEL_WL_22500_5:
		/* These devices should be QuZ only. */
		if (sc->sc_hw_rev != IWX_CSR_HW_REV_TYPE_QUZ) {
			printf("%s: unsupported AX201 adapter\n", DEVNAME(sc));
			return;
		}
		sc->sc_fwname = IWX_QUZ_A_HR_B_FW;
		sc->sc_device_family = IWX_DEVICE_FAMILY_22000;
		sc->sc_integrated = 1;
		sc->sc_ltr_delay = IWX_SOC_FLAGS_LTR_APPLY_DELAY_200;
		sc->sc_low_latency_xtal = 0;
		sc->sc_xtal_latency = 500;
		sc->sc_tx_with_siso_diversity = 0;
		sc->sc_uhb_supported = 0;
		break;
	case PCI_PRODUCT_INTEL_WL_22500_3:
		if (sc->sc_hw_rev == IWX_CSR_HW_REV_TYPE_QU_C0)
			sc->sc_fwname = IWX_QU_C_HR_B_FW;
		else if (sc->sc_hw_rev == IWX_CSR_HW_REV_TYPE_QUZ) {
			uint32_t rf_id = IWX_CSR_HW_RFID_TYPE(sc->sc_hw_rf_id);
			if (rf_id == IWX_CFG_RF_TYPE_JF1 ||
			    rf_id == IWX_CFG_RF_TYPE_JF2)
				sc->sc_fwname = IWX_QUZ_A_JF_B_FW;
			else
				sc->sc_fwname = IWX_QUZ_A_HR_B_FW;
		} else
			sc->sc_fwname = IWX_QU_B_HR_B_FW;
		sc->sc_device_family = IWX_DEVICE_FAMILY_22000;
		sc->sc_integrated = 1;
		sc->sc_ltr_delay = IWX_SOC_FLAGS_LTR_APPLY_DELAY_200;
		sc->sc_low_latency_xtal = 0;
		sc->sc_xtal_latency = 500;
		sc->sc_tx_with_siso_diversity = 0;
		sc->sc_uhb_supported = 0;
		break;
	case PCI_PRODUCT_INTEL_WL_22500_4:
	case PCI_PRODUCT_INTEL_WL_22500_7:
	case PCI_PRODUCT_INTEL_WL_22500_8:
		if (sc->sc_hw_rev == IWX_CSR_HW_REV_TYPE_QU_C0)
			sc->sc_fwname = IWX_QU_C_HR_B_FW;
		else if (sc->sc_hw_rev == IWX_CSR_HW_REV_TYPE_QUZ)
			sc->sc_fwname = IWX_QUZ_A_HR_B_FW;
		else
			sc->sc_fwname = IWX_QU_B_HR_B_FW;
		sc->sc_device_family = IWX_DEVICE_FAMILY_22000;
		sc->sc_integrated = 1;
		sc->sc_ltr_delay = IWX_SOC_FLAGS_LTR_APPLY_DELAY_1820;
		sc->sc_low_latency_xtal = 0;
		sc->sc_xtal_latency = 1820;
		sc->sc_tx_with_siso_diversity = 0;
		sc->sc_uhb_supported = 0;
		break;
	case PCI_PRODUCT_INTEL_WL_22500_6:
		if (sc->sc_hw_rev == IWX_CSR_HW_REV_TYPE_QU_C0)
			sc->sc_fwname = IWX_QU_C_HR_B_FW;
		else if (sc->sc_hw_rev == IWX_CSR_HW_REV_TYPE_QUZ)
			sc->sc_fwname = IWX_QUZ_A_HR_B_FW;
		else
			sc->sc_fwname = IWX_QU_B_HR_B_FW;
		sc->sc_device_family = IWX_DEVICE_FAMILY_22000;
		sc->sc_integrated = 1;
		sc->sc_ltr_delay = IWX_SOC_FLAGS_LTR_APPLY_DELAY_2500;
		sc->sc_low_latency_xtal = 1;
		sc->sc_xtal_latency = 12000;
		sc->sc_tx_with_siso_diversity = 0;
		sc->sc_uhb_supported = 0;
		break;
	case PCI_PRODUCT_INTEL_WL_22500_9:
	case PCI_PRODUCT_INTEL_WL_22500_10:
	case PCI_PRODUCT_INTEL_WL_22500_11:
	case PCI_PRODUCT_INTEL_WL_22500_13:
	case PCI_PRODUCT_INTEL_WL_22500_15:
	case PCI_PRODUCT_INTEL_WL_22500_16:
		sc->sc_fwname = IWX_SO_A_GF_A_FW;
		sc->sc_pnvm_name = IWX_SO_A_GF_A_PNVM;
		sc->sc_device_family = IWX_DEVICE_FAMILY_AX210;
		sc->sc_integrated = 0;
		sc->sc_ltr_delay = IWX_SOC_FLAGS_LTR_APPLY_DELAY_NONE;
		sc->sc_low_latency_xtal = 0;
		sc->sc_xtal_latency = 0;
		sc->sc_tx_with_siso_diversity = 0;
		sc->sc_uhb_supported = 1;
		break;
	case PCI_PRODUCT_INTEL_WL_22500_12:
	case PCI_PRODUCT_INTEL_WL_22500_17:
		sc->sc_fwname = IWX_SO_A_GF_A_FW;
		sc->sc_pnvm_name = IWX_SO_A_GF_A_PNVM;
		sc->sc_device_family = IWX_DEVICE_FAMILY_AX210;
		sc->sc_integrated = 1;
		sc->sc_ltr_delay = IWX_SOC_FLAGS_LTR_APPLY_DELAY_2500;
		sc->sc_low_latency_xtal = 1;
		sc->sc_xtal_latency = 12000;
		sc->sc_tx_with_siso_diversity = 0;
		sc->sc_uhb_supported = 0;
		sc->sc_imr_enabled = 1;
		break;
	case PCI_PRODUCT_INTEL_WL_22500_14:
		sc->sc_fwname = IWX_MA_B_GF_A_FW;
		sc->sc_pnvm_name = IWX_MA_B_GF_A_PNVM;
		sc->sc_device_family = IWX_DEVICE_FAMILY_AX210;
		sc->sc_integrated = 1;
		sc->sc_ltr_delay = IWX_SOC_FLAGS_LTR_APPLY_DELAY_NONE;
		sc->sc_low_latency_xtal = 0;
		sc->sc_xtal_latency = 0;
		sc->sc_tx_with_siso_diversity = 0;
		sc->sc_uhb_supported = 1;
		break;
	default:
		printf("%s: unknown adapter type\n", DEVNAME(sc));
		return;
	}

	cfg = iwx_find_device_cfg(sc);
	if (cfg) {
		sc->sc_fwname = cfg->fw_name;
		sc->sc_pnvm_name = cfg->pnvm_name;
		sc->sc_tx_with_siso_diversity = cfg->tx_with_siso_diversity;
		sc->sc_uhb_supported = cfg->uhb_supported;
		if (cfg->xtal_latency) {
			sc->sc_xtal_latency = cfg->xtal_latency;
			sc->sc_low_latency_xtal = cfg->low_latency_xtal;
		}
	}

	sc->mac_addr_from_csr = 0x380; /* differs on BZ hw generation */

	if (sc->sc_device_family >= IWX_DEVICE_FAMILY_AX210) {
		sc->sc_umac_prph_offset = 0x300000;
		sc->max_tfd_queue_size = IWX_TFD_QUEUE_SIZE_MAX_GEN3;
	} else
		sc->max_tfd_queue_size = IWX_TFD_QUEUE_SIZE_MAX;

	/* Allocate DMA memory for loading firmware. */
	if (sc->sc_device_family >= IWX_DEVICE_FAMILY_AX210)
		ctxt_info_size = sizeof(struct iwx_context_info_gen3);
	else
		ctxt_info_size = sizeof(struct iwx_context_info);
	err = iwx_dma_contig_alloc(sc->sc_dmat, &sc->ctxt_info_dma,
	    ctxt_info_size, 0);
	if (err) {
		printf("%s: could not allocate memory for loading firmware\n",
		    DEVNAME(sc));
		return;
	}

	if (sc->sc_device_family >= IWX_DEVICE_FAMILY_AX210) {
		err = iwx_dma_contig_alloc(sc->sc_dmat, &sc->prph_scratch_dma,
		    sizeof(struct iwx_prph_scratch), 0);
		if (err) {
			printf("%s: could not allocate prph scratch memory\n",
			    DEVNAME(sc));
			goto fail1;
		}

		/*
		 * Allocate prph information. The driver doesn't use this.
		 * We use the second half of this page to give the device
		 * some dummy TR/CR tail pointers - which shouldn't be
		 * necessary as we don't use this, but the hardware still
		 * reads/writes there and we can't let it go do that with
		 * a NULL pointer.
		 */
		KASSERT(sizeof(struct iwx_prph_info) < PAGE_SIZE / 2);
		err = iwx_dma_contig_alloc(sc->sc_dmat, &sc->prph_info_dma,
		    PAGE_SIZE, 0);
		if (err) {
			printf("%s: could not allocate prph info memory\n",
			    DEVNAME(sc));
			goto fail1;
		}
	}

	/* Allocate interrupt cause table (ICT).*/
	err = iwx_dma_contig_alloc(sc->sc_dmat, &sc->ict_dma,
	    IWX_ICT_SIZE, 1<<IWX_ICT_PADDR_SHIFT);
	if (err) {
		printf("%s: could not allocate ICT table\n", DEVNAME(sc));
		goto fail1;
	}

	for (txq_i = 0; txq_i < nitems(sc->txq); txq_i++) {
		err = iwx_alloc_tx_ring(sc, &sc->txq[txq_i], txq_i);
		if (err) {
			printf("%s: could not allocate TX ring %d\n",
			    DEVNAME(sc), txq_i);
			goto fail4;
		}
	}

	err = iwx_alloc_rx_ring(sc, &sc->rxq);
	if (err) {
		printf("%s: could not allocate RX ring\n", DEVNAME(sc));
		goto fail4;
	}

	sc->sc_nswq = taskq_create("iwxns", 1, IPL_NET, 0);
	if (sc->sc_nswq == NULL)
		goto fail4;

	ic->ic_phytype = IEEE80211_T_OFDM;	/* not only, but not used */
	ic->ic_opmode = IEEE80211_M_STA;	/* default to BSS mode */
	ic->ic_state = IEEE80211_S_INIT;

	/* Set device capabilities. */
	ic->ic_caps =
	    IEEE80211_C_QOS | IEEE80211_C_TX_AMPDU | /* A-MPDU */
	    IEEE80211_C_ADDBA_OFFLOAD | /* device sends ADDBA/DELBA frames */
	    IEEE80211_C_WEP |		/* WEP */
	    IEEE80211_C_RSN |		/* WPA/RSN */
	    IEEE80211_C_SCANALL |	/* device scans all channels at once */
	    IEEE80211_C_SCANALLBAND |	/* device scans all bands at once */
	    IEEE80211_C_MONITOR |	/* monitor mode supported */
	    IEEE80211_C_SHSLOT |	/* short slot time supported */
	    IEEE80211_C_SHPREAMBLE;	/* short preamble supported */

	ic->ic_htcaps = IEEE80211_HTCAP_SGI20 | IEEE80211_HTCAP_SGI40;
	ic->ic_htcaps |= IEEE80211_HTCAP_CBW20_40;
	ic->ic_htcaps |=
	    (IEEE80211_HTCAP_SMPS_DIS << IEEE80211_HTCAP_SMPS_SHIFT);
	ic->ic_htxcaps = 0;
	ic->ic_txbfcaps = 0;
	ic->ic_aselcaps = 0;
	ic->ic_ampdu_params = (IEEE80211_AMPDU_PARAM_SS_4 | 0x3 /* 64k */);

	ic->ic_vhtcaps = IEEE80211_VHTCAP_MAX_MPDU_LENGTH_3895 |
	    (IEEE80211_VHTCAP_MAX_AMPDU_LEN_64K <<
	    IEEE80211_VHTCAP_MAX_AMPDU_LEN_SHIFT) |
	    (IEEE80211_VHTCAP_CHAN_WIDTH_80 <<
	     IEEE80211_VHTCAP_CHAN_WIDTH_SHIFT) | IEEE80211_VHTCAP_SGI80 |
	    IEEE80211_VHTCAP_RX_ANT_PATTERN | IEEE80211_VHTCAP_TX_ANT_PATTERN;

	ic->ic_sup_rates[IEEE80211_MODE_11A] = ieee80211_std_rateset_11a;
	ic->ic_sup_rates[IEEE80211_MODE_11B] = ieee80211_std_rateset_11b;
	ic->ic_sup_rates[IEEE80211_MODE_11G] = ieee80211_std_rateset_11g;

	for (i = 0; i < nitems(sc->sc_phyctxt); i++) {
		sc->sc_phyctxt[i].id = i;
		sc->sc_phyctxt[i].sco = IEEE80211_HTOP0_SCO_SCN;
		sc->sc_phyctxt[i].vht_chan_width =
		    IEEE80211_VHTOP0_CHAN_WIDTH_HT;
	}

	/* IBSS channel undefined for now. */
	ic->ic_ibss_chan = &ic->ic_channels[1];

	ic->ic_max_rssi = IWX_MAX_DBM - IWX_MIN_DBM;

	ifp->if_softc = sc;
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	ifp->if_ioctl = iwx_ioctl;
	ifp->if_start = iwx_start;
	ifp->if_watchdog = iwx_watchdog;
	memcpy(ifp->if_xname, DEVNAME(sc), IFNAMSIZ);

	if_attach(ifp);
	ieee80211_ifattach(ifp);
	ieee80211_media_init(ifp, iwx_media_change, ieee80211_media_status);

#if NBPFILTER > 0
	iwx_radiotap_attach(sc);
#endif
	for (i = 0; i < nitems(sc->sc_rxba_data); i++) {
		struct iwx_rxba_data *rxba = &sc->sc_rxba_data[i];
		rxba->baid = IWX_RX_REORDER_DATA_INVALID_BAID;
		rxba->sc = sc;
		timeout_set(&rxba->session_timer, iwx_rx_ba_session_expired,
		    rxba);
		timeout_set(&rxba->reorder_buf.reorder_timer,
		    iwx_reorder_timer_expired, &rxba->reorder_buf);
		for (j = 0; j < nitems(rxba->entries); j++)
			ml_init(&rxba->entries[j].frames);
	}
	task_set(&sc->init_task, iwx_init_task, sc);
	task_set(&sc->newstate_task, iwx_newstate_task, sc);
	task_set(&sc->ba_task, iwx_ba_task, sc);
	task_set(&sc->setkey_task, iwx_setkey_task, sc);
	task_set(&sc->mac_ctxt_task, iwx_mac_ctxt_task, sc);
	task_set(&sc->phy_ctxt_task, iwx_phy_ctxt_task, sc);
	task_set(&sc->bgscan_done_task, iwx_bgscan_done_task, sc);

	ic->ic_node_alloc = iwx_node_alloc;
	ic->ic_bgscan_start = iwx_bgscan;
	ic->ic_bgscan_done = iwx_bgscan_done;
	ic->ic_set_key = iwx_set_key;
	ic->ic_delete_key = iwx_delete_key;

	/* Override 802.11 state transition machine. */
	sc->sc_newstate = ic->ic_newstate;
	ic->ic_newstate = iwx_newstate;
	ic->ic_updatechan = iwx_updatechan;
	ic->ic_updateprot = iwx_updateprot;
	ic->ic_updateslot = iwx_updateslot;
	ic->ic_updateedca = iwx_updateedca;
	ic->ic_updatedtim = iwx_updatedtim;
	ic->ic_ampdu_rx_start = iwx_ampdu_rx_start;
	ic->ic_ampdu_rx_stop = iwx_ampdu_rx_stop;
	ic->ic_ampdu_tx_start = iwx_ampdu_tx_start;
	ic->ic_ampdu_tx_stop = NULL;
	/*
	 * We cannot read the MAC address without loading the
	 * firmware from disk. Postpone until mountroot is done.
	 */
	config_mountroot(self, iwx_attach_hook);

	return;

fail4:	while (--txq_i >= 0)
		iwx_free_tx_ring(sc, &sc->txq[txq_i]);
	iwx_free_rx_ring(sc, &sc->rxq);
	if (sc->ict_dma.vaddr != NULL)
		iwx_dma_contig_free(&sc->ict_dma);

fail1:	iwx_dma_contig_free(&sc->ctxt_info_dma);
	iwx_dma_contig_free(&sc->prph_scratch_dma);
	iwx_dma_contig_free(&sc->prph_info_dma);
	return;
}

#if NBPFILTER > 0
void
iwx_radiotap_attach(struct iwx_softc *sc)
{
	bpfattach(&sc->sc_drvbpf, &sc->sc_ic.ic_if, DLT_IEEE802_11_RADIO,
	    sizeof (struct ieee80211_frame) + IEEE80211_RADIOTAP_HDRLEN);

	sc->sc_rxtap_len = sizeof sc->sc_rxtapu;
	sc->sc_rxtap.wr_ihdr.it_len = htole16(sc->sc_rxtap_len);
	sc->sc_rxtap.wr_ihdr.it_present = htole32(IWX_RX_RADIOTAP_PRESENT);

	sc->sc_txtap_len = sizeof sc->sc_txtapu;
	sc->sc_txtap.wt_ihdr.it_len = htole16(sc->sc_txtap_len);
	sc->sc_txtap.wt_ihdr.it_present = htole32(IWX_TX_RADIOTAP_PRESENT);
}
#endif

void
iwx_init_task(void *arg1)
{
	struct iwx_softc *sc = arg1;
	struct ifnet *ifp = &sc->sc_ic.ic_if;
	int s = splnet();
	int generation = sc->sc_generation;
	int fatal = (sc->sc_flags & (IWX_FLAG_HW_ERR | IWX_FLAG_RFKILL));

	rw_enter_write(&sc->ioctl_rwl);
	if (generation != sc->sc_generation) {
		rw_exit(&sc->ioctl_rwl);
		splx(s);
		return;
	}

	if (ifp->if_flags & IFF_RUNNING)
		iwx_stop(ifp);
	else
		sc->sc_flags &= ~IWX_FLAG_HW_ERR;

	if (!fatal && (ifp->if_flags & (IFF_UP | IFF_RUNNING)) == IFF_UP)
		iwx_init(ifp);

	rw_exit(&sc->ioctl_rwl);
	splx(s);
}

void
iwx_resume(struct iwx_softc *sc)
{
	pcireg_t reg;

	/*
	 * We disable the RETRY_TIMEOUT register (0x41) to keep
	 * PCI Tx retries from interfering with C3 CPU state.
	 */
	reg = pci_conf_read(sc->sc_pct, sc->sc_pcitag, 0x40);
	pci_conf_write(sc->sc_pct, sc->sc_pcitag, 0x40, reg & ~0xff00);

	if (!sc->sc_msix) {
		/* Hardware bug workaround. */
		reg = pci_conf_read(sc->sc_pct, sc->sc_pcitag,
		    PCI_COMMAND_STATUS_REG);
		if (reg & PCI_COMMAND_INTERRUPT_DISABLE)
			reg &= ~PCI_COMMAND_INTERRUPT_DISABLE;
		pci_conf_write(sc->sc_pct, sc->sc_pcitag,
		    PCI_COMMAND_STATUS_REG, reg);
	}

	iwx_disable_interrupts(sc);
}

int
iwx_wakeup(struct iwx_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ifnet *ifp = &sc->sc_ic.ic_if;
	int err;

	rw_enter_write(&sc->ioctl_rwl);

	err = iwx_start_hw(sc);
	if (err) {
		rw_exit(&sc->ioctl_rwl);
		return err;
	}

	err = iwx_init_hw(sc);
	if (err) {
		iwx_stop_device(sc);
		rw_exit(&sc->ioctl_rwl);
		return err;
	}

	refcnt_init(&sc->task_refs);
	ifq_clr_oactive(&ifp->if_snd);
	ifp->if_flags |= IFF_RUNNING;

	if (ic->ic_opmode == IEEE80211_M_MONITOR)
		ieee80211_new_state(ic, IEEE80211_S_RUN, -1);
	else
		ieee80211_begin_scan(ifp);

	rw_exit(&sc->ioctl_rwl);
	return 0;
}

int
iwx_activate(struct device *self, int act)
{
	struct iwx_softc *sc = (struct iwx_softc *)self;
	struct ifnet *ifp = &sc->sc_ic.ic_if;
	int err = 0;

	switch (act) {
	case DVACT_QUIESCE:
		if (ifp->if_flags & IFF_RUNNING) {
			rw_enter_write(&sc->ioctl_rwl);
			iwx_stop(ifp);
			rw_exit(&sc->ioctl_rwl);
		}
		break;
	case DVACT_RESUME:
		iwx_resume(sc);
		break;
	case DVACT_WAKEUP:
		if ((ifp->if_flags & (IFF_UP | IFF_RUNNING)) == IFF_UP) {
			err = iwx_wakeup(sc);
			if (err)
				printf("%s: could not initialize hardware\n",
				    DEVNAME(sc));
		}
		break;
	}

	return 0;
}

struct cfdriver iwx_cd = {
	NULL, "iwx", DV_IFNET
};

const struct cfattach iwx_ca = {
	sizeof(struct iwx_softc), iwx_match, iwx_attach,
	NULL, iwx_activate
};
