/*	$OpenBSD: if_iwm.c,v 1.418 2025/02/04 09:15:04 stsp Exp $	*/

/*
 * Copyright (c) 2014, 2016 genua gmbh <info@genua.de>
 *   Author: Stefan Sperling <stsp@openbsd.org>
 * Copyright (c) 2014 Fixup Software Ltd.
 * Copyright (c) 2017 Stefan Sperling <stsp@openbsd.org>
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
 ***********************************************************************
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2007 - 2013 Intel Corporation. All rights reserved.
 * Copyright(c) 2013 - 2015 Intel Mobile Communications GmbH
 * Copyright(c) 2016 Intel Deutschland GmbH
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
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110,
 * USA
 *
 * The full GNU General Public License is included in this distribution
 * in the file called COPYING.
 *
 * Contact Information:
 *  Intel Linux Wireless <ilw@linux.intel.com>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *
 *
 * BSD LICENSE
 *
 * Copyright(c) 2005 - 2013 Intel Corporation. All rights reserved.
 * Copyright(c) 2013 - 2015 Intel Mobile Communications GmbH
 * Copyright(c) 2016 Intel Deutschland GmbH
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
#include <net80211/ieee80211_amrr.h>
#include <net80211/ieee80211_ra.h>
#include <net80211/ieee80211_ra_vht.h>
#include <net80211/ieee80211_radiotap.h>
#include <net80211/ieee80211_priv.h> /* for SEQ_LT */
#undef DPRINTF /* defined in ieee80211_priv.h */

#define DEVNAME(_s)	((_s)->sc_dev.dv_xname)

#define IC2IFP(_ic_) (&(_ic_)->ic_if)

#define le16_to_cpup(_a_) (le16toh(*(const uint16_t *)(_a_)))
#define le32_to_cpup(_a_) (le32toh(*(const uint32_t *)(_a_)))

#ifdef IWM_DEBUG
#define DPRINTF(x)	do { if (iwm_debug > 0) printf x; } while (0)
#define DPRINTFN(n, x)	do { if (iwm_debug >= (n)) printf x; } while (0)
int iwm_debug = 1;
#else
#define DPRINTF(x)	do { ; } while (0)
#define DPRINTFN(n, x)	do { ; } while (0)
#endif

#include <dev/pci/if_iwmreg.h>
#include <dev/pci/if_iwmvar.h>

const uint8_t iwm_nvm_channels[] = {
	/* 2.4 GHz */
	1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14,
	/* 5 GHz */
	36, 40, 44 , 48, 52, 56, 60, 64,
	100, 104, 108, 112, 116, 120, 124, 128, 132, 136, 140, 144,
	149, 153, 157, 161, 165
};

const uint8_t iwm_nvm_channels_8000[] = {
	/* 2.4 GHz */
	1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14,
	/* 5 GHz */
	36, 40, 44, 48, 52, 56, 60, 64, 68, 72, 76, 80, 84, 88, 92,
	96, 100, 104, 108, 112, 116, 120, 124, 128, 132, 136, 140, 144,
	149, 153, 157, 161, 165, 169, 173, 177, 181
};

#define IWM_NUM_2GHZ_CHANNELS	14

const struct iwm_rate {
	uint16_t rate;
	uint8_t plcp;
	uint8_t ht_plcp;
} iwm_rates[] = {
		/* Legacy */		/* HT */
	{   2,	IWM_RATE_1M_PLCP,	IWM_RATE_HT_SISO_MCS_INV_PLCP  },
	{   4,	IWM_RATE_2M_PLCP,	IWM_RATE_HT_SISO_MCS_INV_PLCP },
	{  11,	IWM_RATE_5M_PLCP,	IWM_RATE_HT_SISO_MCS_INV_PLCP  },
	{  22,	IWM_RATE_11M_PLCP,	IWM_RATE_HT_SISO_MCS_INV_PLCP },
	{  12,	IWM_RATE_6M_PLCP,	IWM_RATE_HT_SISO_MCS_0_PLCP },
	{  18,	IWM_RATE_9M_PLCP,	IWM_RATE_HT_SISO_MCS_INV_PLCP  },
	{  24,	IWM_RATE_12M_PLCP,	IWM_RATE_HT_SISO_MCS_1_PLCP },
	{  26,	IWM_RATE_INVM_PLCP,	IWM_RATE_HT_MIMO2_MCS_8_PLCP },
	{  36,	IWM_RATE_18M_PLCP,	IWM_RATE_HT_SISO_MCS_2_PLCP },
	{  48,	IWM_RATE_24M_PLCP,	IWM_RATE_HT_SISO_MCS_3_PLCP },
	{  52,	IWM_RATE_INVM_PLCP,	IWM_RATE_HT_MIMO2_MCS_9_PLCP },
	{  72,	IWM_RATE_36M_PLCP,	IWM_RATE_HT_SISO_MCS_4_PLCP },
	{  78,	IWM_RATE_INVM_PLCP,	IWM_RATE_HT_MIMO2_MCS_10_PLCP },
	{  96,	IWM_RATE_48M_PLCP,	IWM_RATE_HT_SISO_MCS_5_PLCP },
	{ 104,	IWM_RATE_INVM_PLCP,	IWM_RATE_HT_MIMO2_MCS_11_PLCP },
	{ 108,	IWM_RATE_54M_PLCP,	IWM_RATE_HT_SISO_MCS_6_PLCP },
	{ 128,	IWM_RATE_INVM_PLCP,	IWM_RATE_HT_SISO_MCS_7_PLCP },
	{ 156,	IWM_RATE_INVM_PLCP,	IWM_RATE_HT_MIMO2_MCS_12_PLCP },
	{ 208,	IWM_RATE_INVM_PLCP,	IWM_RATE_HT_MIMO2_MCS_13_PLCP },
	{ 234,	IWM_RATE_INVM_PLCP,	IWM_RATE_HT_MIMO2_MCS_14_PLCP },
	{ 260,	IWM_RATE_INVM_PLCP,	IWM_RATE_HT_MIMO2_MCS_15_PLCP },
};
#define IWM_RIDX_CCK	0
#define IWM_RIDX_OFDM	4
#define IWM_RIDX_MAX	(nitems(iwm_rates)-1)
#define IWM_RIDX_IS_CCK(_i_) ((_i_) < IWM_RIDX_OFDM)
#define IWM_RIDX_IS_OFDM(_i_) ((_i_) >= IWM_RIDX_OFDM)
#define IWM_RVAL_IS_OFDM(_i_) ((_i_) >= 12 && (_i_) != 22)

/* Convert an MCS index into an iwm_rates[] index. */
const int iwm_ht_mcs2ridx[] = {
	IWM_RATE_MCS_0_INDEX,
	IWM_RATE_MCS_1_INDEX,
	IWM_RATE_MCS_2_INDEX,
	IWM_RATE_MCS_3_INDEX,
	IWM_RATE_MCS_4_INDEX,
	IWM_RATE_MCS_5_INDEX,
	IWM_RATE_MCS_6_INDEX,
	IWM_RATE_MCS_7_INDEX,
	IWM_RATE_MCS_8_INDEX,
	IWM_RATE_MCS_9_INDEX,
	IWM_RATE_MCS_10_INDEX,
	IWM_RATE_MCS_11_INDEX,
	IWM_RATE_MCS_12_INDEX,
	IWM_RATE_MCS_13_INDEX,
	IWM_RATE_MCS_14_INDEX,
	IWM_RATE_MCS_15_INDEX,
};

struct iwm_nvm_section {
	uint16_t length;
	uint8_t *data;
};

int	iwm_is_mimo_ht_plcp(uint8_t);
int	iwm_is_mimo_ht_mcs(int);
int	iwm_store_cscheme(struct iwm_softc *, uint8_t *, size_t);
int	iwm_firmware_store_section(struct iwm_softc *, enum iwm_ucode_type,
	    uint8_t *, size_t);
int	iwm_set_default_calib(struct iwm_softc *, const void *);
void	iwm_fw_info_free(struct iwm_fw_info *);
void	iwm_fw_version_str(char *, size_t, uint32_t, uint32_t, uint32_t);
int	iwm_read_firmware(struct iwm_softc *);
uint32_t iwm_read_prph_unlocked(struct iwm_softc *, uint32_t);
uint32_t iwm_read_prph(struct iwm_softc *, uint32_t);
void	iwm_write_prph_unlocked(struct iwm_softc *, uint32_t, uint32_t);
void	iwm_write_prph(struct iwm_softc *, uint32_t, uint32_t);
int	iwm_read_mem(struct iwm_softc *, uint32_t, void *, int);
int	iwm_write_mem(struct iwm_softc *, uint32_t, const void *, int);
int	iwm_write_mem32(struct iwm_softc *, uint32_t, uint32_t);
int	iwm_poll_bit(struct iwm_softc *, int, uint32_t, uint32_t, int);
int	iwm_nic_lock(struct iwm_softc *);
void	iwm_nic_assert_locked(struct iwm_softc *);
void	iwm_nic_unlock(struct iwm_softc *);
int	iwm_set_bits_mask_prph(struct iwm_softc *, uint32_t, uint32_t,
	    uint32_t);
int	iwm_set_bits_prph(struct iwm_softc *, uint32_t, uint32_t);
int	iwm_clear_bits_prph(struct iwm_softc *, uint32_t, uint32_t);
int	iwm_dma_contig_alloc(bus_dma_tag_t, struct iwm_dma_info *, bus_size_t,
	    bus_size_t);
void	iwm_dma_contig_free(struct iwm_dma_info *);
int	iwm_alloc_rx_ring(struct iwm_softc *, struct iwm_rx_ring *);
void	iwm_disable_rx_dma(struct iwm_softc *);
void	iwm_reset_rx_ring(struct iwm_softc *, struct iwm_rx_ring *);
void	iwm_free_rx_ring(struct iwm_softc *, struct iwm_rx_ring *);
int	iwm_alloc_tx_ring(struct iwm_softc *, struct iwm_tx_ring *, int);
void	iwm_reset_tx_ring(struct iwm_softc *, struct iwm_tx_ring *);
void	iwm_free_tx_ring(struct iwm_softc *, struct iwm_tx_ring *);
void	iwm_enable_rfkill_int(struct iwm_softc *);
int	iwm_check_rfkill(struct iwm_softc *);
void	iwm_enable_interrupts(struct iwm_softc *);
void	iwm_enable_fwload_interrupt(struct iwm_softc *);
void	iwm_restore_interrupts(struct iwm_softc *);
void	iwm_disable_interrupts(struct iwm_softc *);
void	iwm_ict_reset(struct iwm_softc *);
int	iwm_set_hw_ready(struct iwm_softc *);
int	iwm_prepare_card_hw(struct iwm_softc *);
void	iwm_apm_config(struct iwm_softc *);
int	iwm_apm_init(struct iwm_softc *);
void	iwm_apm_stop(struct iwm_softc *);
int	iwm_allow_mcast(struct iwm_softc *);
void	iwm_init_msix_hw(struct iwm_softc *);
void	iwm_conf_msix_hw(struct iwm_softc *, int);
int	iwm_clear_persistence_bit(struct iwm_softc *);
int	iwm_start_hw(struct iwm_softc *);
void	iwm_stop_device(struct iwm_softc *);
void	iwm_nic_config(struct iwm_softc *);
int	iwm_nic_rx_init(struct iwm_softc *);
int	iwm_nic_rx_legacy_init(struct iwm_softc *);
int	iwm_nic_rx_mq_init(struct iwm_softc *);
int	iwm_nic_tx_init(struct iwm_softc *);
int	iwm_nic_init(struct iwm_softc *);
int	iwm_enable_ac_txq(struct iwm_softc *, int, int);
int	iwm_enable_txq(struct iwm_softc *, int, int, int, int, uint8_t,
	    uint16_t);
int	iwm_disable_txq(struct iwm_softc *, int, int, uint8_t);
int	iwm_post_alive(struct iwm_softc *);
struct iwm_phy_db_entry *iwm_phy_db_get_section(struct iwm_softc *, uint16_t,
	    uint16_t);
int	iwm_phy_db_set_section(struct iwm_softc *,
	    struct iwm_calib_res_notif_phy_db *);
int	iwm_is_valid_channel(uint16_t);
uint8_t	iwm_ch_id_to_ch_index(uint16_t);
uint16_t iwm_channel_id_to_papd(uint16_t);
uint16_t iwm_channel_id_to_txp(struct iwm_softc *, uint16_t);
int	iwm_phy_db_get_section_data(struct iwm_softc *, uint32_t, uint8_t **,
	    uint16_t *, uint16_t);
int	iwm_send_phy_db_cmd(struct iwm_softc *, uint16_t, uint16_t, void *);
int	iwm_phy_db_send_all_channel_groups(struct iwm_softc *, uint16_t,
	    uint8_t);
int	iwm_send_phy_db_data(struct iwm_softc *);
void	iwm_protect_session(struct iwm_softc *, struct iwm_node *, uint32_t,
	    uint32_t);
void	iwm_unprotect_session(struct iwm_softc *, struct iwm_node *);
int	iwm_nvm_read_chunk(struct iwm_softc *, uint16_t, uint16_t, uint16_t,
	    uint8_t *, uint16_t *);
int	iwm_nvm_read_section(struct iwm_softc *, uint16_t, uint8_t *,
	    uint16_t *, size_t);
uint8_t	iwm_fw_valid_tx_ant(struct iwm_softc *);
uint8_t	iwm_fw_valid_rx_ant(struct iwm_softc *);
int	iwm_valid_siso_ant_rate_mask(struct iwm_softc *);
void	iwm_init_channel_map(struct iwm_softc *, const uint16_t * const,
	    const uint8_t *nvm_channels, int nchan);
int	iwm_mimo_enabled(struct iwm_softc *);
void	iwm_setup_ht_rates(struct iwm_softc *);
void	iwm_setup_vht_rates(struct iwm_softc *);
void	iwm_mac_ctxt_task(void *);
void	iwm_phy_ctxt_task(void *);
void	iwm_updateprot(struct ieee80211com *);
void	iwm_updateslot(struct ieee80211com *);
void	iwm_updateedca(struct ieee80211com *);
void	iwm_updatechan(struct ieee80211com *);
void	iwm_updatedtim(struct ieee80211com *);
void	iwm_init_reorder_buffer(struct iwm_reorder_buffer *, uint16_t,
	    uint16_t);
void	iwm_clear_reorder_buffer(struct iwm_softc *, struct iwm_rxba_data *);
int	iwm_ampdu_rx_start(struct ieee80211com *, struct ieee80211_node *,
	    uint8_t);
void	iwm_ampdu_rx_stop(struct ieee80211com *, struct ieee80211_node *,
	    uint8_t);
void	iwm_rx_ba_session_expired(void *);
void	iwm_reorder_timer_expired(void *);
int	iwm_sta_rx_agg(struct iwm_softc *, struct ieee80211_node *, uint8_t,
	    uint16_t, uint16_t, int, int);
int	iwm_ampdu_tx_start(struct ieee80211com *, struct ieee80211_node *,
	    uint8_t);
void	iwm_ampdu_tx_stop(struct ieee80211com *, struct ieee80211_node *,
	    uint8_t);
void	iwm_ba_task(void *);

int	iwm_parse_nvm_data(struct iwm_softc *, const uint16_t *,
	    const uint16_t *, const uint16_t *,
	    const uint16_t *, const uint16_t *,
	    const uint16_t *, int);
void	iwm_set_hw_address_8000(struct iwm_softc *, struct iwm_nvm_data *,
	    const uint16_t *, const uint16_t *);
int	iwm_parse_nvm_sections(struct iwm_softc *, struct iwm_nvm_section *);
int	iwm_nvm_init(struct iwm_softc *);
int	iwm_firmware_load_sect(struct iwm_softc *, uint32_t, const uint8_t *,
	    uint32_t);
int	iwm_firmware_load_chunk(struct iwm_softc *, uint32_t, const uint8_t *,
	    uint32_t);
int	iwm_load_firmware_7000(struct iwm_softc *, enum iwm_ucode_type);
int	iwm_load_cpu_sections_8000(struct iwm_softc *, struct iwm_fw_sects *,
	    int , int *);
int	iwm_load_firmware_8000(struct iwm_softc *, enum iwm_ucode_type);
int	iwm_load_firmware(struct iwm_softc *, enum iwm_ucode_type);
int	iwm_start_fw(struct iwm_softc *, enum iwm_ucode_type);
int	iwm_send_tx_ant_cfg(struct iwm_softc *, uint8_t);
int	iwm_send_phy_cfg_cmd(struct iwm_softc *);
int	iwm_load_ucode_wait_alive(struct iwm_softc *, enum iwm_ucode_type);
int	iwm_send_dqa_cmd(struct iwm_softc *);
int	iwm_run_init_mvm_ucode(struct iwm_softc *, int);
int	iwm_config_ltr(struct iwm_softc *);
int	iwm_rx_addbuf(struct iwm_softc *, int, int);
int	iwm_get_signal_strength(struct iwm_softc *, struct iwm_rx_phy_info *);
int	iwm_rxmq_get_signal_strength(struct iwm_softc *, struct iwm_rx_mpdu_desc *);
void	iwm_rx_rx_phy_cmd(struct iwm_softc *, struct iwm_rx_packet *,
	    struct iwm_rx_data *);
int	iwm_get_noise(const struct iwm_statistics_rx_non_phy *);
int	iwm_rx_hwdecrypt(struct iwm_softc *, struct mbuf *, uint32_t,
	    struct ieee80211_rxinfo *);
int	iwm_ccmp_decap(struct iwm_softc *, struct mbuf *,
	    struct ieee80211_node *, struct ieee80211_rxinfo *);
void	iwm_rx_frame(struct iwm_softc *, struct mbuf *, int, uint32_t, int, int,
	    uint32_t, struct ieee80211_rxinfo *, struct mbuf_list *);
void	iwm_ht_single_rate_control(struct iwm_softc *, struct ieee80211_node *,
	    int, uint8_t, int);
void	iwm_vht_single_rate_control(struct iwm_softc *, struct ieee80211_node *,
	    int, int, uint8_t, int);
void	iwm_rx_tx_cmd_single(struct iwm_softc *, struct iwm_rx_packet *,
	    struct iwm_node *, int, int);
void	iwm_txd_done(struct iwm_softc *, struct iwm_tx_data *);
void	iwm_txq_advance(struct iwm_softc *, struct iwm_tx_ring *, int);
void	iwm_rx_tx_cmd(struct iwm_softc *, struct iwm_rx_packet *,
	    struct iwm_rx_data *);
void	iwm_clear_oactive(struct iwm_softc *, struct iwm_tx_ring *);
void	iwm_ampdu_rate_control(struct iwm_softc *, struct ieee80211_node *,
	    struct iwm_tx_ring *, int, uint16_t, uint16_t);
void	iwm_rx_compressed_ba(struct iwm_softc *, struct iwm_rx_packet *);
void	iwm_rx_bmiss(struct iwm_softc *, struct iwm_rx_packet *,
	    struct iwm_rx_data *);
int	iwm_binding_cmd(struct iwm_softc *, struct iwm_node *, uint32_t);
uint8_t	iwm_get_vht_ctrl_pos(struct ieee80211com *, struct ieee80211_channel *);
int	iwm_phy_ctxt_cmd_uhb(struct iwm_softc *, struct iwm_phy_ctxt *, uint8_t,
	    uint8_t, uint32_t, uint32_t, uint8_t, uint8_t);
void	iwm_phy_ctxt_cmd_hdr(struct iwm_softc *, struct iwm_phy_ctxt *,
	    struct iwm_phy_context_cmd *, uint32_t, uint32_t);
void	iwm_phy_ctxt_cmd_data(struct iwm_softc *, struct iwm_phy_context_cmd *,
	    struct ieee80211_channel *, uint8_t, uint8_t, uint8_t, uint8_t);
int	iwm_phy_ctxt_cmd(struct iwm_softc *, struct iwm_phy_ctxt *, uint8_t,
	    uint8_t, uint32_t, uint32_t, uint8_t, uint8_t);
int	iwm_send_cmd(struct iwm_softc *, struct iwm_host_cmd *);
int	iwm_send_cmd_pdu(struct iwm_softc *, uint32_t, uint32_t, uint16_t,
	    const void *);
int	iwm_send_cmd_status(struct iwm_softc *, struct iwm_host_cmd *,
	    uint32_t *);
int	iwm_send_cmd_pdu_status(struct iwm_softc *, uint32_t, uint16_t,
	    const void *, uint32_t *);
void	iwm_free_resp(struct iwm_softc *, struct iwm_host_cmd *);
void	iwm_cmd_done(struct iwm_softc *, int, int, int);
void	iwm_update_sched(struct iwm_softc *, int, int, uint8_t, uint16_t);
void	iwm_reset_sched(struct iwm_softc *, int, int, uint8_t);
uint8_t	iwm_tx_fill_cmd(struct iwm_softc *, struct iwm_node *,
	    struct ieee80211_frame *, struct iwm_tx_cmd *);
int	iwm_tx(struct iwm_softc *, struct mbuf *, struct ieee80211_node *, int);
int	iwm_flush_tx_path(struct iwm_softc *, int);
int	iwm_wait_tx_queues_empty(struct iwm_softc *);
void	iwm_led_enable(struct iwm_softc *);
void	iwm_led_disable(struct iwm_softc *);
int	iwm_led_is_enabled(struct iwm_softc *);
void	iwm_led_blink_timeout(void *);
void	iwm_led_blink_start(struct iwm_softc *);
void	iwm_led_blink_stop(struct iwm_softc *);
int	iwm_beacon_filter_send_cmd(struct iwm_softc *,
	    struct iwm_beacon_filter_cmd *);
void	iwm_beacon_filter_set_cqm_params(struct iwm_softc *, struct iwm_node *,
	    struct iwm_beacon_filter_cmd *);
int	iwm_update_beacon_abort(struct iwm_softc *, struct iwm_node *, int);
void	iwm_power_build_cmd(struct iwm_softc *, struct iwm_node *,
	    struct iwm_mac_power_cmd *);
int	iwm_power_mac_update_mode(struct iwm_softc *, struct iwm_node *);
int	iwm_power_update_device(struct iwm_softc *);
int	iwm_enable_beacon_filter(struct iwm_softc *, struct iwm_node *);
int	iwm_disable_beacon_filter(struct iwm_softc *);
int	iwm_add_sta_cmd(struct iwm_softc *, struct iwm_node *, int);
int	iwm_add_aux_sta(struct iwm_softc *);
int	iwm_drain_sta(struct iwm_softc *sc, struct iwm_node *, int);
int	iwm_flush_sta(struct iwm_softc *, struct iwm_node *);
int	iwm_rm_sta_cmd(struct iwm_softc *, struct iwm_node *);
uint16_t iwm_scan_rx_chain(struct iwm_softc *);
uint32_t iwm_scan_rate_n_flags(struct iwm_softc *, int, int);
uint8_t	iwm_lmac_scan_fill_channels(struct iwm_softc *,
	    struct iwm_scan_channel_cfg_lmac *, int, int);
int	iwm_fill_probe_req(struct iwm_softc *, struct iwm_scan_probe_req *);
int	iwm_lmac_scan(struct iwm_softc *, int);
int	iwm_config_umac_scan(struct iwm_softc *);
int	iwm_umac_scan(struct iwm_softc *, int);
void	iwm_mcc_update(struct iwm_softc *, struct iwm_mcc_chub_notif *);
uint8_t	iwm_ridx2rate(struct ieee80211_rateset *, int);
int	iwm_rval2ridx(int);
void	iwm_ack_rates(struct iwm_softc *, struct iwm_node *, int *, int *);
void	iwm_mac_ctxt_cmd_common(struct iwm_softc *, struct iwm_node *,
	    struct iwm_mac_ctx_cmd *, uint32_t);
void	iwm_mac_ctxt_cmd_fill_sta(struct iwm_softc *, struct iwm_node *,
	    struct iwm_mac_data_sta *, int);
int	iwm_mac_ctxt_cmd(struct iwm_softc *, struct iwm_node *, uint32_t, int);
int	iwm_update_quotas(struct iwm_softc *, struct iwm_node *, int);
void	iwm_add_task(struct iwm_softc *, struct taskq *, struct task *);
void	iwm_del_task(struct iwm_softc *, struct taskq *, struct task *);
int	iwm_scan(struct iwm_softc *);
int	iwm_bgscan(struct ieee80211com *);
void	iwm_bgscan_done(struct ieee80211com *,
	    struct ieee80211_node_switch_bss_arg *, size_t);
void	iwm_bgscan_done_task(void *);
int	iwm_umac_scan_abort(struct iwm_softc *);
int	iwm_lmac_scan_abort(struct iwm_softc *);
int	iwm_scan_abort(struct iwm_softc *);
int	iwm_phy_ctxt_update(struct iwm_softc *, struct iwm_phy_ctxt *,
	    struct ieee80211_channel *, uint8_t, uint8_t, uint32_t, uint8_t,
	    uint8_t);
int	iwm_auth(struct iwm_softc *);
int	iwm_deauth(struct iwm_softc *);
int	iwm_run(struct iwm_softc *);
int	iwm_run_stop(struct iwm_softc *);
struct ieee80211_node *iwm_node_alloc(struct ieee80211com *);
int	iwm_set_key_v1(struct ieee80211com *, struct ieee80211_node *,
	    struct ieee80211_key *);
int	iwm_set_key(struct ieee80211com *, struct ieee80211_node *,
	    struct ieee80211_key *);
void	iwm_delete_key_v1(struct ieee80211com *,
	    struct ieee80211_node *, struct ieee80211_key *);
void	iwm_delete_key(struct ieee80211com *,
	    struct ieee80211_node *, struct ieee80211_key *);
void	iwm_calib_timeout(void *);
void	iwm_set_rate_table_vht(struct iwm_node *, struct iwm_lq_cmd *);
void	iwm_set_rate_table(struct iwm_node *, struct iwm_lq_cmd *);
void	iwm_setrates(struct iwm_node *, int);
int	iwm_media_change(struct ifnet *);
void	iwm_newstate_task(void *);
int	iwm_newstate(struct ieee80211com *, enum ieee80211_state, int);
void	iwm_endscan(struct iwm_softc *);
void	iwm_fill_sf_command(struct iwm_softc *, struct iwm_sf_cfg_cmd *,
	    struct ieee80211_node *);
int	iwm_sf_config(struct iwm_softc *, int);
int	iwm_send_bt_init_conf(struct iwm_softc *);
int	iwm_send_soc_conf(struct iwm_softc *);
int	iwm_send_update_mcc_cmd(struct iwm_softc *, const char *);
int	iwm_send_temp_report_ths_cmd(struct iwm_softc *);
void	iwm_tt_tx_backoff(struct iwm_softc *, uint32_t);
void	iwm_free_fw_paging(struct iwm_softc *);
int	iwm_save_fw_paging(struct iwm_softc *, const struct iwm_fw_sects *);
int	iwm_send_paging_cmd(struct iwm_softc *, const struct iwm_fw_sects *);
int	iwm_init_hw(struct iwm_softc *);
int	iwm_init(struct ifnet *);
void	iwm_start(struct ifnet *);
void	iwm_stop(struct ifnet *);
void	iwm_watchdog(struct ifnet *);
int	iwm_ioctl(struct ifnet *, u_long, caddr_t);
const char *iwm_desc_lookup(uint32_t);
void	iwm_nic_error(struct iwm_softc *);
void	iwm_dump_driver_status(struct iwm_softc *);
void	iwm_nic_umac_error(struct iwm_softc *);
void	iwm_rx_mpdu(struct iwm_softc *, struct mbuf *, void *, size_t,
	    struct mbuf_list *);
void	iwm_flip_address(uint8_t *);
int	iwm_detect_duplicate(struct iwm_softc *, struct mbuf *,
	    struct iwm_rx_mpdu_desc *, struct ieee80211_rxinfo *);
int	iwm_is_sn_less(uint16_t, uint16_t, uint16_t);
void	iwm_release_frames(struct iwm_softc *, struct ieee80211_node *,
	    struct iwm_rxba_data *, struct iwm_reorder_buffer *, uint16_t,
	    struct mbuf_list *);
int	iwm_oldsn_workaround(struct iwm_softc *, struct ieee80211_node *,
	    int, struct iwm_reorder_buffer *, uint32_t, uint32_t);
int	iwm_rx_reorder(struct iwm_softc *, struct mbuf *, int,
	    struct iwm_rx_mpdu_desc *, int, int, uint32_t,
	    struct ieee80211_rxinfo *, struct mbuf_list *);
void	iwm_rx_mpdu_mq(struct iwm_softc *, struct mbuf *, void *, size_t,
	    struct mbuf_list *);
int	iwm_rx_pkt_valid(struct iwm_rx_packet *);
void	iwm_rx_pkt(struct iwm_softc *, struct iwm_rx_data *,
	    struct mbuf_list *);
void	iwm_notif_intr(struct iwm_softc *);
int	iwm_intr(void *);
int	iwm_intr_msix(void *);
int	iwm_match(struct device *, void *, void *);
int	iwm_preinit(struct iwm_softc *);
void	iwm_attach_hook(struct device *);
void	iwm_attach(struct device *, struct device *, void *);
void	iwm_init_task(void *);
int	iwm_activate(struct device *, int);
void	iwm_resume(struct iwm_softc *);
int	iwm_wakeup(struct iwm_softc *);

#if NBPFILTER > 0
void	iwm_radiotap_attach(struct iwm_softc *);
#endif

uint8_t
iwm_lookup_cmd_ver(struct iwm_softc *sc, uint8_t grp, uint8_t cmd)
{
	const struct iwm_fw_cmd_version *entry;
	int i;

	for (i = 0; i < sc->n_cmd_versions; i++) {
		entry = &sc->cmd_versions[i];
		if (entry->group == grp && entry->cmd == cmd)
			return entry->cmd_ver;
	}

	return IWM_FW_CMD_VER_UNKNOWN;
}

int
iwm_is_mimo_ht_plcp(uint8_t ht_plcp)
{
	return (ht_plcp != IWM_RATE_HT_SISO_MCS_INV_PLCP &&
	    (ht_plcp & IWM_RATE_HT_MCS_NSS_MSK));
}

int
iwm_is_mimo_ht_mcs(int mcs)
{
	int ridx = iwm_ht_mcs2ridx[mcs];
	return iwm_is_mimo_ht_plcp(iwm_rates[ridx].ht_plcp);
	
}

int
iwm_store_cscheme(struct iwm_softc *sc, uint8_t *data, size_t dlen)
{
	struct iwm_fw_cscheme_list *l = (void *)data;

	if (dlen < sizeof(*l) ||
	    dlen < sizeof(l->size) + l->size * sizeof(*l->cs))
		return EINVAL;

	/* we don't actually store anything for now, always use s/w crypto */

	return 0;
}

int
iwm_firmware_store_section(struct iwm_softc *sc, enum iwm_ucode_type type,
    uint8_t *data, size_t dlen)
{
	struct iwm_fw_sects *fws;
	struct iwm_fw_onesect *fwone;

	if (type >= IWM_UCODE_TYPE_MAX)
		return EINVAL;
	if (dlen < sizeof(uint32_t))
		return EINVAL;

	fws = &sc->sc_fw.fw_sects[type];
	if (fws->fw_count >= IWM_UCODE_SECT_MAX)
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

#define IWM_DEFAULT_SCAN_CHANNELS	40
/* Newer firmware might support more channels. Raise this value if needed. */
#define IWM_MAX_SCAN_CHANNELS		52 /* as of 8265-34 firmware image */

struct iwm_tlv_calib_data {
	uint32_t ucode_type;
	struct iwm_tlv_calib_ctrl calib;
} __packed;

int
iwm_set_default_calib(struct iwm_softc *sc, const void *data)
{
	const struct iwm_tlv_calib_data *def_calib = data;
	uint32_t ucode_type = le32toh(def_calib->ucode_type);

	if (ucode_type >= IWM_UCODE_TYPE_MAX)
		return EINVAL;

	sc->sc_default_calib[ucode_type].flow_trigger =
	    def_calib->calib.flow_trigger;
	sc->sc_default_calib[ucode_type].event_trigger =
	    def_calib->calib.event_trigger;

	return 0;
}

void
iwm_fw_info_free(struct iwm_fw_info *fw)
{
	free(fw->fw_rawdata, M_DEVBUF, fw->fw_rawsize);
	fw->fw_rawdata = NULL;
	fw->fw_rawsize = 0;
	/* don't touch fw->fw_status */
	memset(fw->fw_sects, 0, sizeof(fw->fw_sects));
}
	    
void
iwm_fw_version_str(char *buf, size_t bufsize,
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
iwm_read_firmware(struct iwm_softc *sc)
{
	struct iwm_fw_info *fw = &sc->sc_fw;
	struct iwm_tlv_ucode_header *uhdr;
	struct iwm_ucode_tlv tlv;
	uint32_t tlv_type;
	uint8_t *data;
	uint32_t usniffer_img;
	uint32_t paging_mem_size;
	int err;
	size_t len;

	if (fw->fw_status == IWM_FW_STATUS_DONE)
		return 0;

	while (fw->fw_status == IWM_FW_STATUS_INPROGRESS)
		tsleep_nsec(&sc->sc_fw, 0, "iwmfwp", INFSLP);
	fw->fw_status = IWM_FW_STATUS_INPROGRESS;

	if (fw->fw_rawdata != NULL)
		iwm_fw_info_free(fw);

	err = loadfirmware(sc->sc_fwname,
	    (u_char **)&fw->fw_rawdata, &fw->fw_rawsize);
	if (err) {
		printf("%s: could not read firmware %s (error %d)\n",
		    DEVNAME(sc), sc->sc_fwname, err);
		goto out;
	}

	sc->sc_capaflags = 0;
	sc->sc_capa_n_scan_channels = IWM_DEFAULT_SCAN_CHANNELS;
	memset(sc->sc_enabled_capa, 0, sizeof(sc->sc_enabled_capa));
	memset(sc->sc_ucode_api, 0, sizeof(sc->sc_ucode_api));
	sc->n_cmd_versions = 0;

	uhdr = (void *)fw->fw_rawdata;
	if (*(uint32_t *)fw->fw_rawdata != 0
	    || le32toh(uhdr->magic) != IWM_TLV_UCODE_MAGIC) {
		printf("%s: invalid firmware %s\n",
		    DEVNAME(sc), sc->sc_fwname);
		err = EINVAL;
		goto out;
	}

	iwm_fw_version_str(sc->sc_fwver, sizeof(sc->sc_fwver),
	    IWM_UCODE_MAJOR(le32toh(uhdr->ver)),
	    IWM_UCODE_MINOR(le32toh(uhdr->ver)),
	    IWM_UCODE_API(le32toh(uhdr->ver)));

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
		case IWM_UCODE_TLV_PROBE_MAX_LEN:
			if (tlv_len < sizeof(uint32_t)) {
				err = EINVAL;
				goto parse_out;
			}
			sc->sc_capa_max_probe_len
			    = le32toh(*(uint32_t *)tlv_data);
			if (sc->sc_capa_max_probe_len >
			    IWM_SCAN_OFFLOAD_PROBE_REQ_SIZE) {
				err = EINVAL;
				goto parse_out;
			}
			break;
		case IWM_UCODE_TLV_PAN:
			if (tlv_len) {
				err = EINVAL;
				goto parse_out;
			}
			sc->sc_capaflags |= IWM_UCODE_TLV_FLAGS_PAN;
			break;
		case IWM_UCODE_TLV_FLAGS:
			if (tlv_len < sizeof(uint32_t)) {
				err = EINVAL;
				goto parse_out;
			}
			/*
			 * Apparently there can be many flags, but Linux driver
			 * parses only the first one, and so do we.
			 *
			 * XXX: why does this override IWM_UCODE_TLV_PAN?
			 * Intentional or a bug?  Observations from
			 * current firmware file:
			 *  1) TLV_PAN is parsed first
			 *  2) TLV_FLAGS contains TLV_FLAGS_PAN
			 * ==> this resets TLV_PAN to itself... hnnnk
			 */
			sc->sc_capaflags = le32toh(*(uint32_t *)tlv_data);
			break;
		case IWM_UCODE_TLV_CSCHEME:
			err = iwm_store_cscheme(sc, tlv_data, tlv_len);
			if (err)
				goto parse_out;
			break;
		case IWM_UCODE_TLV_NUM_OF_CPU: {
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
		case IWM_UCODE_TLV_SEC_RT:
			err = iwm_firmware_store_section(sc,
			    IWM_UCODE_TYPE_REGULAR, tlv_data, tlv_len);
			if (err)
				goto parse_out;
			break;
		case IWM_UCODE_TLV_SEC_INIT:
			err = iwm_firmware_store_section(sc,
			    IWM_UCODE_TYPE_INIT, tlv_data, tlv_len);
			if (err)
				goto parse_out;
			break;
		case IWM_UCODE_TLV_SEC_WOWLAN:
			err = iwm_firmware_store_section(sc,
			    IWM_UCODE_TYPE_WOW, tlv_data, tlv_len);
			if (err)
				goto parse_out;
			break;
		case IWM_UCODE_TLV_DEF_CALIB:
			if (tlv_len != sizeof(struct iwm_tlv_calib_data)) {
				err = EINVAL;
				goto parse_out;
			}
			err = iwm_set_default_calib(sc, tlv_data);
			if (err)
				goto parse_out;
			break;
		case IWM_UCODE_TLV_PHY_SKU:
			if (tlv_len != sizeof(uint32_t)) {
				err = EINVAL;
				goto parse_out;
			}
			sc->sc_fw_phy_config = le32toh(*(uint32_t *)tlv_data);
			break;

		case IWM_UCODE_TLV_API_CHANGES_SET: {
			struct iwm_ucode_api *api;
			int idx, i;
			if (tlv_len != sizeof(*api)) {
				err = EINVAL;
				goto parse_out;
			}
			api = (struct iwm_ucode_api *)tlv_data;
			idx = le32toh(api->api_index);
			if (idx >= howmany(IWM_NUM_UCODE_TLV_API, 32)) {
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

		case IWM_UCODE_TLV_ENABLED_CAPABILITIES: {
			struct iwm_ucode_capa *capa;
			int idx, i;
			if (tlv_len != sizeof(*capa)) {
				err = EINVAL;
				goto parse_out;
			}
			capa = (struct iwm_ucode_capa *)tlv_data;
			idx = le32toh(capa->api_index);
			if (idx >= howmany(IWM_NUM_UCODE_TLV_CAPA, 32)) {
				goto parse_out;
			}
			for (i = 0; i < 32; i++) {
				if ((le32toh(capa->api_capa) & (1 << i)) == 0)
					continue;
				setbit(sc->sc_enabled_capa, i + (32 * idx));
			}
			break;
		}

		case IWM_UCODE_TLV_CMD_VERSIONS:
			if (tlv_len % sizeof(struct iwm_fw_cmd_version)) {
				tlv_len /= sizeof(struct iwm_fw_cmd_version);
				tlv_len *= sizeof(struct iwm_fw_cmd_version);
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
			sc->n_cmd_versions = tlv_len / sizeof(struct iwm_fw_cmd_version);
			break;

		case IWM_UCODE_TLV_SDIO_ADMA_ADDR:
		case IWM_UCODE_TLV_FW_GSCAN_CAPA:
			/* ignore, not used by current driver */
			break;

		case IWM_UCODE_TLV_SEC_RT_USNIFFER:
			err = iwm_firmware_store_section(sc,
			    IWM_UCODE_TYPE_REGULAR_USNIFFER, tlv_data,
			    tlv_len);
			if (err)
				goto parse_out;
			break;

		case IWM_UCODE_TLV_PAGING:
			if (tlv_len != sizeof(uint32_t)) {
				err = EINVAL;
				goto parse_out;
			}
			paging_mem_size = le32toh(*(const uint32_t *)tlv_data);

			DPRINTF(("%s: Paging: paging enabled (size = %u bytes)\n",
			    DEVNAME(sc), paging_mem_size));
			if (paging_mem_size > IWM_MAX_PAGING_IMAGE_SIZE) {
				printf("%s: Driver only supports up to %u"
				    " bytes for paging image (%u requested)\n",
				    DEVNAME(sc), IWM_MAX_PAGING_IMAGE_SIZE,
				    paging_mem_size);
				err = EINVAL;
				goto out;
			}
			if (paging_mem_size & (IWM_FW_PAGING_SIZE - 1)) {
				printf("%s: Paging: image isn't multiple of %u\n",
				    DEVNAME(sc), IWM_FW_PAGING_SIZE);
				err = EINVAL;
				goto out;
			}

			fw->fw_sects[IWM_UCODE_TYPE_REGULAR].paging_mem_size =
			    paging_mem_size;
			usniffer_img = IWM_UCODE_TYPE_REGULAR_USNIFFER;
			fw->fw_sects[usniffer_img].paging_mem_size =
			    paging_mem_size;
			break;

		case IWM_UCODE_TLV_N_SCAN_CHANNELS:
			if (tlv_len != sizeof(uint32_t)) {
				err = EINVAL;
				goto parse_out;
			}
			sc->sc_capa_n_scan_channels =
			  le32toh(*(uint32_t *)tlv_data);
			if (sc->sc_capa_n_scan_channels > IWM_MAX_SCAN_CHANNELS) {
				err = ERANGE;
				goto parse_out;
			}
			break;

		case IWM_UCODE_TLV_FW_VERSION:
			if (tlv_len != sizeof(uint32_t) * 3) {
				err = EINVAL;
				goto parse_out;
			}

			iwm_fw_version_str(sc->sc_fwver, sizeof(sc->sc_fwver),
			    le32toh(((uint32_t *)tlv_data)[0]),
			    le32toh(((uint32_t *)tlv_data)[1]),
			    le32toh(((uint32_t *)tlv_data)[2]));
			break;

		case IWM_UCODE_TLV_FW_DBG_DEST:
		case IWM_UCODE_TLV_FW_DBG_CONF:
		case IWM_UCODE_TLV_UMAC_DEBUG_ADDRS:
		case IWM_UCODE_TLV_LMAC_DEBUG_ADDRS:
		case IWM_UCODE_TLV_TYPE_DEBUG_INFO:
		case IWM_UCODE_TLV_TYPE_BUFFER_ALLOCATION:
		case IWM_UCODE_TLV_TYPE_HCMD:
		case IWM_UCODE_TLV_TYPE_REGIONS:
		case IWM_UCODE_TLV_TYPE_TRIGGERS:
			break;

		case IWM_UCODE_TLV_HW_TYPE:
			break;

		case IWM_UCODE_TLV_FW_MEM_SEG:
			break;

		/* undocumented TLVs found in iwm-9000-43 image */
		case 0x1000003:
		case 0x1000004:
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
		fw->fw_status = IWM_FW_STATUS_NONE;
		if (fw->fw_rawdata != NULL)
			iwm_fw_info_free(fw);
	} else
		fw->fw_status = IWM_FW_STATUS_DONE;
	wakeup(&sc->sc_fw);

	return err;
}

uint32_t
iwm_read_prph_unlocked(struct iwm_softc *sc, uint32_t addr)
{
	IWM_WRITE(sc,
	    IWM_HBUS_TARG_PRPH_RADDR, ((addr & 0x000fffff) | (3 << 24)));
	IWM_BARRIER_READ_WRITE(sc);
	return IWM_READ(sc, IWM_HBUS_TARG_PRPH_RDAT);
}

uint32_t
iwm_read_prph(struct iwm_softc *sc, uint32_t addr)
{
	iwm_nic_assert_locked(sc);
	return iwm_read_prph_unlocked(sc, addr);
}

void
iwm_write_prph_unlocked(struct iwm_softc *sc, uint32_t addr, uint32_t val)
{
	IWM_WRITE(sc,
	    IWM_HBUS_TARG_PRPH_WADDR, ((addr & 0x000fffff) | (3 << 24)));
	IWM_BARRIER_WRITE(sc);
	IWM_WRITE(sc, IWM_HBUS_TARG_PRPH_WDAT, val);
}

void
iwm_write_prph(struct iwm_softc *sc, uint32_t addr, uint32_t val)
{
	iwm_nic_assert_locked(sc);
	iwm_write_prph_unlocked(sc, addr, val);
}

void
iwm_write_prph64(struct iwm_softc *sc, uint64_t addr, uint64_t val)
{
	iwm_write_prph(sc, (uint32_t)addr, val & 0xffffffff);
	iwm_write_prph(sc, (uint32_t)addr + 4, val >> 32);
}

int
iwm_read_mem(struct iwm_softc *sc, uint32_t addr, void *buf, int dwords)
{
	int offs, err = 0;
	uint32_t *vals = buf;

	if (iwm_nic_lock(sc)) {
		IWM_WRITE(sc, IWM_HBUS_TARG_MEM_RADDR, addr);
		for (offs = 0; offs < dwords; offs++)
			vals[offs] = IWM_READ(sc, IWM_HBUS_TARG_MEM_RDAT);
		iwm_nic_unlock(sc);
	} else {
		err = EBUSY;
	}
	return err;
}

int
iwm_write_mem(struct iwm_softc *sc, uint32_t addr, const void *buf, int dwords)
{
	int offs;	
	const uint32_t *vals = buf;

	if (iwm_nic_lock(sc)) {
		IWM_WRITE(sc, IWM_HBUS_TARG_MEM_WADDR, addr);
		/* WADDR auto-increments */
		for (offs = 0; offs < dwords; offs++) {
			uint32_t val = vals ? vals[offs] : 0;
			IWM_WRITE(sc, IWM_HBUS_TARG_MEM_WDAT, val);
		}
		iwm_nic_unlock(sc);
	} else {
		return EBUSY;
	}
	return 0;
}

int
iwm_write_mem32(struct iwm_softc *sc, uint32_t addr, uint32_t val)
{
	return iwm_write_mem(sc, addr, &val, 1);
}

int
iwm_poll_bit(struct iwm_softc *sc, int reg, uint32_t bits, uint32_t mask,
    int timo)
{
	for (;;) {
		if ((IWM_READ(sc, reg) & mask) == (bits & mask)) {
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
iwm_nic_lock(struct iwm_softc *sc)
{
	if (sc->sc_nic_locks > 0) {
		iwm_nic_assert_locked(sc);
		sc->sc_nic_locks++;
		return 1; /* already locked */
	}

	IWM_SETBITS(sc, IWM_CSR_GP_CNTRL,
	    IWM_CSR_GP_CNTRL_REG_FLAG_MAC_ACCESS_REQ);

	if (sc->sc_device_family >= IWM_DEVICE_FAMILY_8000)
		DELAY(2);

	if (iwm_poll_bit(sc, IWM_CSR_GP_CNTRL,
	    IWM_CSR_GP_CNTRL_REG_VAL_MAC_ACCESS_EN,
	    IWM_CSR_GP_CNTRL_REG_FLAG_MAC_CLOCK_READY
	     | IWM_CSR_GP_CNTRL_REG_FLAG_GOING_TO_SLEEP, 150000)) {
		sc->sc_nic_locks++;
		return 1;
	}

	printf("%s: acquiring device failed\n", DEVNAME(sc));
	return 0;
}

void
iwm_nic_assert_locked(struct iwm_softc *sc)
{
	if (sc->sc_nic_locks <= 0)
		panic("%s: nic locks counter %d", DEVNAME(sc), sc->sc_nic_locks);
}

void
iwm_nic_unlock(struct iwm_softc *sc)
{
	if (sc->sc_nic_locks > 0) {
		if (--sc->sc_nic_locks == 0)
			IWM_CLRBITS(sc, IWM_CSR_GP_CNTRL,
			    IWM_CSR_GP_CNTRL_REG_FLAG_MAC_ACCESS_REQ);
	} else
		printf("%s: NIC already unlocked\n", DEVNAME(sc));
}

int
iwm_set_bits_mask_prph(struct iwm_softc *sc, uint32_t reg, uint32_t bits,
    uint32_t mask)
{
	uint32_t val;

	if (iwm_nic_lock(sc)) {
		val = iwm_read_prph(sc, reg) & mask;
		val |= bits;
		iwm_write_prph(sc, reg, val);
		iwm_nic_unlock(sc);
		return 0;
	}
	return EBUSY;
}

int
iwm_set_bits_prph(struct iwm_softc *sc, uint32_t reg, uint32_t bits)
{
	return iwm_set_bits_mask_prph(sc, reg, bits, ~0);
}

int
iwm_clear_bits_prph(struct iwm_softc *sc, uint32_t reg, uint32_t bits)
{
	return iwm_set_bits_mask_prph(sc, reg, 0, ~bits);
}

int
iwm_dma_contig_alloc(bus_dma_tag_t tag, struct iwm_dma_info *dma,
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
	    BUS_DMA_NOWAIT);
	if (err)
		goto fail;

	err = bus_dmamem_map(tag, &dma->seg, 1, size, &va,
	    BUS_DMA_NOWAIT);
	if (err)
		goto fail;
	dma->vaddr = va;

	err = bus_dmamap_load(tag, dma->map, dma->vaddr, size, NULL,
	    BUS_DMA_NOWAIT);
	if (err)
		goto fail;

	memset(dma->vaddr, 0, size);
	bus_dmamap_sync(tag, dma->map, 0, size, BUS_DMASYNC_PREWRITE);
	dma->paddr = dma->map->dm_segs[0].ds_addr;

	return 0;

fail:	iwm_dma_contig_free(dma);
	return err;
}

void
iwm_dma_contig_free(struct iwm_dma_info *dma)
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
iwm_alloc_rx_ring(struct iwm_softc *sc, struct iwm_rx_ring *ring)
{
	bus_size_t size;
	size_t descsz;
	int count, i, err;

	ring->cur = 0;

	if (sc->sc_mqrx_supported) {
		count = IWM_RX_MQ_RING_COUNT;
		descsz = sizeof(uint64_t);
	} else {
		count = IWM_RX_RING_COUNT;
		descsz = sizeof(uint32_t);
	}

	/* Allocate RX descriptors (256-byte aligned). */
	size = count * descsz;
	err = iwm_dma_contig_alloc(sc->sc_dmat, &ring->free_desc_dma, size, 256);
	if (err) {
		printf("%s: could not allocate RX ring DMA memory\n",
		    DEVNAME(sc));
		goto fail;
	}
	ring->desc = ring->free_desc_dma.vaddr;

	/* Allocate RX status area (16-byte aligned). */
	err = iwm_dma_contig_alloc(sc->sc_dmat, &ring->stat_dma,
	    sizeof(*ring->stat), 16);
	if (err) {
		printf("%s: could not allocate RX status DMA memory\n",
		    DEVNAME(sc));
		goto fail;
	}
	ring->stat = ring->stat_dma.vaddr;

	if (sc->sc_mqrx_supported) {
		size = count * sizeof(uint32_t);
		err = iwm_dma_contig_alloc(sc->sc_dmat, &ring->used_desc_dma,
		    size, 256);
		if (err) {
			printf("%s: could not allocate RX ring DMA memory\n",
			    DEVNAME(sc));
			goto fail;
		}
	}

	for (i = 0; i < count; i++) {
		struct iwm_rx_data *data = &ring->data[i];

		memset(data, 0, sizeof(*data));
		err = bus_dmamap_create(sc->sc_dmat, IWM_RBUF_SIZE, 1,
		    IWM_RBUF_SIZE, 0, BUS_DMA_NOWAIT | BUS_DMA_ALLOCNOW,
		    &data->map);
		if (err) {
			printf("%s: could not create RX buf DMA map\n",
			    DEVNAME(sc));
			goto fail;
		}

		err = iwm_rx_addbuf(sc, IWM_RBUF_SIZE, i);
		if (err)
			goto fail;
	}
	return 0;

fail:	iwm_free_rx_ring(sc, ring);
	return err;
}

void
iwm_disable_rx_dma(struct iwm_softc *sc)
{
	int ntries;

	if (iwm_nic_lock(sc)) {
		if (sc->sc_mqrx_supported) {
			iwm_write_prph(sc, IWM_RFH_RXF_DMA_CFG, 0);
			for (ntries = 0; ntries < 1000; ntries++) {
				if (iwm_read_prph(sc, IWM_RFH_GEN_STATUS) &
				    IWM_RXF_DMA_IDLE)
					break;
				DELAY(10);
			}
		} else {
			IWM_WRITE(sc, IWM_FH_MEM_RCSR_CHNL0_CONFIG_REG, 0);
			for (ntries = 0; ntries < 1000; ntries++) {
				if (IWM_READ(sc, IWM_FH_MEM_RSSR_RX_STATUS_REG)&
				    IWM_FH_RSSR_CHNL0_RX_STATUS_CHNL_IDLE)
					break;
				DELAY(10);
			}
		}
		iwm_nic_unlock(sc);
	}
}

void
iwm_reset_rx_ring(struct iwm_softc *sc, struct iwm_rx_ring *ring)
{
	ring->cur = 0;
	bus_dmamap_sync(sc->sc_dmat, ring->stat_dma.map, 0,
	    ring->stat_dma.size, BUS_DMASYNC_PREWRITE);
	memset(ring->stat, 0, sizeof(*ring->stat));
	bus_dmamap_sync(sc->sc_dmat, ring->stat_dma.map, 0,
	    ring->stat_dma.size, BUS_DMASYNC_POSTWRITE);

}

void
iwm_free_rx_ring(struct iwm_softc *sc, struct iwm_rx_ring *ring)
{
	int count, i;

	iwm_dma_contig_free(&ring->free_desc_dma);
	iwm_dma_contig_free(&ring->stat_dma);
	iwm_dma_contig_free(&ring->used_desc_dma);

	if (sc->sc_mqrx_supported)
		count = IWM_RX_MQ_RING_COUNT;
	else
		count = IWM_RX_RING_COUNT;

	for (i = 0; i < count; i++) {
		struct iwm_rx_data *data = &ring->data[i];

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
iwm_alloc_tx_ring(struct iwm_softc *sc, struct iwm_tx_ring *ring, int qid)
{
	bus_addr_t paddr;
	bus_size_t size;
	int i, err;

	ring->qid = qid;
	ring->queued = 0;
	ring->cur = 0;
	ring->tail = 0;

	/* Allocate TX descriptors (256-byte aligned). */
	size = IWM_TX_RING_COUNT * sizeof (struct iwm_tfd);
	err = iwm_dma_contig_alloc(sc->sc_dmat, &ring->desc_dma, size, 256);
	if (err) {
		printf("%s: could not allocate TX ring DMA memory\n",
		    DEVNAME(sc));
		goto fail;
	}
	ring->desc = ring->desc_dma.vaddr;

	/*
	 * There is no need to allocate DMA buffers for unused rings.
	 * 7k/8k/9k hardware supports up to 31 Tx rings which is more
	 * than we currently need.
	 *
	 * In DQA mode we use 1 command queue + 4 DQA mgmt/data queues.
	 * The command is queue 0 (sc->txq[0]), and 4 mgmt/data frame queues
	 * are sc->tqx[IWM_DQA_MIN_MGMT_QUEUE + ac], i.e. sc->txq[5:8],
	 * in order to provide one queue per EDCA category.
	 * Tx aggregation requires additional queues, one queue per TID for
	 * which aggregation is enabled. We map TID 0-7 to sc->txq[10:17].
	 *
	 * In non-DQA mode, we use rings 0 through 9 (0-3 are EDCA, 9 is cmd),
	 * and Tx aggregation is not supported.
	 *
	 * Unfortunately, we cannot tell if DQA will be used until the
	 * firmware gets loaded later, so just allocate sufficient rings
	 * in order to satisfy both cases.
	 */
	if (qid > IWM_LAST_AGG_TX_QUEUE)
		return 0;

	size = IWM_TX_RING_COUNT * sizeof(struct iwm_device_cmd);
	err = iwm_dma_contig_alloc(sc->sc_dmat, &ring->cmd_dma, size, 4);
	if (err) {
		printf("%s: could not allocate cmd DMA memory\n", DEVNAME(sc));
		goto fail;
	}
	ring->cmd = ring->cmd_dma.vaddr;

	paddr = ring->cmd_dma.paddr;
	for (i = 0; i < IWM_TX_RING_COUNT; i++) {
		struct iwm_tx_data *data = &ring->data[i];
		size_t mapsize;

		data->cmd_paddr = paddr;
		data->scratch_paddr = paddr + sizeof(struct iwm_cmd_header)
		    + offsetof(struct iwm_tx_cmd, scratch);
		paddr += sizeof(struct iwm_device_cmd);

		/* FW commands may require more mapped space than packets. */
		if (qid == IWM_CMD_QUEUE || qid == IWM_DQA_CMD_QUEUE)
			mapsize = (sizeof(struct iwm_cmd_header) +
			    IWM_MAX_CMD_PAYLOAD_SIZE);
		else
			mapsize = MCLBYTES;
		err = bus_dmamap_create(sc->sc_dmat, mapsize,
		    IWM_NUM_OF_TBS - 2, mapsize, 0, BUS_DMA_NOWAIT,
		    &data->map);
		if (err) {
			printf("%s: could not create TX buf DMA map\n",
			    DEVNAME(sc));
			goto fail;
		}
	}
	KASSERT(paddr == ring->cmd_dma.paddr + size);
	return 0;

fail:	iwm_free_tx_ring(sc, ring);
	return err;
}

void
iwm_reset_tx_ring(struct iwm_softc *sc, struct iwm_tx_ring *ring)
{
	int i;

	for (i = 0; i < IWM_TX_RING_COUNT; i++) {
		struct iwm_tx_data *data = &ring->data[i];

		if (data->m != NULL) {
			bus_dmamap_sync(sc->sc_dmat, data->map, 0,
			    data->map->dm_mapsize, BUS_DMASYNC_POSTWRITE);
			bus_dmamap_unload(sc->sc_dmat, data->map);
			m_freem(data->m);
			data->m = NULL;
		}
	}
	/* Clear TX descriptors. */
	memset(ring->desc, 0, ring->desc_dma.size);
	bus_dmamap_sync(sc->sc_dmat, ring->desc_dma.map, 0,
	    ring->desc_dma.size, BUS_DMASYNC_PREWRITE);
	sc->qfullmsk &= ~(1 << ring->qid);
	sc->qenablemsk &= ~(1 << ring->qid);
	/* 7000 family NICs are locked while commands are in progress. */
	if (ring->qid == sc->cmdqid && ring->queued > 0) {
		if (sc->sc_device_family == IWM_DEVICE_FAMILY_7000)
			iwm_nic_unlock(sc);
	}
	ring->queued = 0;
	ring->cur = 0;
	ring->tail = 0;
}

void
iwm_free_tx_ring(struct iwm_softc *sc, struct iwm_tx_ring *ring)
{
	int i;

	iwm_dma_contig_free(&ring->desc_dma);
	iwm_dma_contig_free(&ring->cmd_dma);

	for (i = 0; i < IWM_TX_RING_COUNT; i++) {
		struct iwm_tx_data *data = &ring->data[i];

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
iwm_enable_rfkill_int(struct iwm_softc *sc)
{
	if (!sc->sc_msix) {
		sc->sc_intmask = IWM_CSR_INT_BIT_RF_KILL;
		IWM_WRITE(sc, IWM_CSR_INT_MASK, sc->sc_intmask);
	} else {
		IWM_WRITE(sc, IWM_CSR_MSIX_FH_INT_MASK_AD,
		    sc->sc_fh_init_mask);
		IWM_WRITE(sc, IWM_CSR_MSIX_HW_INT_MASK_AD,
		    ~IWM_MSIX_HW_INT_CAUSES_REG_RF_KILL);
		sc->sc_hw_mask = IWM_MSIX_HW_INT_CAUSES_REG_RF_KILL;
	}

	if (sc->sc_device_family >= IWM_DEVICE_FAMILY_9000)
		IWM_SETBITS(sc, IWM_CSR_GP_CNTRL,
		    IWM_CSR_GP_CNTRL_REG_FLAG_RFKILL_WAKE_L1A_EN);
}

int
iwm_check_rfkill(struct iwm_softc *sc)
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
	v = IWM_READ(sc, IWM_CSR_GP_CNTRL);
	rv = (v & IWM_CSR_GP_CNTRL_REG_FLAG_HW_RF_KILL_SW) == 0;
	if (rv) {
		sc->sc_flags |= IWM_FLAG_RFKILL;
	} else {
		sc->sc_flags &= ~IWM_FLAG_RFKILL;
	}

	return rv;
}

void
iwm_enable_interrupts(struct iwm_softc *sc)
{
	if (!sc->sc_msix) {
		sc->sc_intmask = IWM_CSR_INI_SET_MASK;
		IWM_WRITE(sc, IWM_CSR_INT_MASK, sc->sc_intmask);
	} else {
		/*
		 * fh/hw_mask keeps all the unmasked causes.
		 * Unlike msi, in msix cause is enabled when it is unset.
		 */
		sc->sc_hw_mask = sc->sc_hw_init_mask;
		sc->sc_fh_mask = sc->sc_fh_init_mask;
		IWM_WRITE(sc, IWM_CSR_MSIX_FH_INT_MASK_AD,
		    ~sc->sc_fh_mask);
		IWM_WRITE(sc, IWM_CSR_MSIX_HW_INT_MASK_AD,
		    ~sc->sc_hw_mask);
	}
}

void
iwm_enable_fwload_interrupt(struct iwm_softc *sc)
{
	if (!sc->sc_msix) {
		sc->sc_intmask = IWM_CSR_INT_BIT_FH_TX;
		IWM_WRITE(sc, IWM_CSR_INT_MASK, sc->sc_intmask);
	} else {
		IWM_WRITE(sc, IWM_CSR_MSIX_HW_INT_MASK_AD,
		    sc->sc_hw_init_mask);
		IWM_WRITE(sc, IWM_CSR_MSIX_FH_INT_MASK_AD,
		    ~IWM_MSIX_FH_INT_CAUSES_D2S_CH0_NUM);
		sc->sc_fh_mask = IWM_MSIX_FH_INT_CAUSES_D2S_CH0_NUM;
	}
}

void
iwm_restore_interrupts(struct iwm_softc *sc)
{
	IWM_WRITE(sc, IWM_CSR_INT_MASK, sc->sc_intmask);
}

void
iwm_disable_interrupts(struct iwm_softc *sc)
{
	if (!sc->sc_msix) {
		IWM_WRITE(sc, IWM_CSR_INT_MASK, 0);

		/* acknowledge all interrupts */
		IWM_WRITE(sc, IWM_CSR_INT, ~0);
		IWM_WRITE(sc, IWM_CSR_FH_INT_STATUS, ~0);
	} else {
		IWM_WRITE(sc, IWM_CSR_MSIX_FH_INT_MASK_AD,
		    sc->sc_fh_init_mask);
		IWM_WRITE(sc, IWM_CSR_MSIX_HW_INT_MASK_AD,
		    sc->sc_hw_init_mask);
	}
}

void
iwm_ict_reset(struct iwm_softc *sc)
{
	iwm_disable_interrupts(sc);

	memset(sc->ict_dma.vaddr, 0, IWM_ICT_SIZE);
	sc->ict_cur = 0;

	/* Set physical address of ICT (4KB aligned). */
	IWM_WRITE(sc, IWM_CSR_DRAM_INT_TBL_REG,
	    IWM_CSR_DRAM_INT_TBL_ENABLE
	    | IWM_CSR_DRAM_INIT_TBL_WRAP_CHECK
	    | IWM_CSR_DRAM_INIT_TBL_WRITE_POINTER
	    | sc->ict_dma.paddr >> IWM_ICT_PADDR_SHIFT);

	/* Switch to ICT interrupt mode in driver. */
	sc->sc_flags |= IWM_FLAG_USE_ICT;

	IWM_WRITE(sc, IWM_CSR_INT, ~0);
	iwm_enable_interrupts(sc);
}

#define IWM_HW_READY_TIMEOUT 50
int
iwm_set_hw_ready(struct iwm_softc *sc)
{
	int ready;

	IWM_SETBITS(sc, IWM_CSR_HW_IF_CONFIG_REG,
	    IWM_CSR_HW_IF_CONFIG_REG_BIT_NIC_READY);

	ready = iwm_poll_bit(sc, IWM_CSR_HW_IF_CONFIG_REG,
	    IWM_CSR_HW_IF_CONFIG_REG_BIT_NIC_READY,
	    IWM_CSR_HW_IF_CONFIG_REG_BIT_NIC_READY,
	    IWM_HW_READY_TIMEOUT);
	if (ready)
		IWM_SETBITS(sc, IWM_CSR_MBOX_SET_REG,
		    IWM_CSR_MBOX_SET_REG_OS_ALIVE);

	return ready;
}
#undef IWM_HW_READY_TIMEOUT

int
iwm_prepare_card_hw(struct iwm_softc *sc)
{
	int t = 0;
	int ntries;

	if (iwm_set_hw_ready(sc))
		return 0;

	IWM_SETBITS(sc, IWM_CSR_DBG_LINK_PWR_MGMT_REG,
	    IWM_CSR_RESET_LINK_PWR_MGMT_DISABLED);
	DELAY(1000);
 
	for (ntries = 0; ntries < 10; ntries++) {
		/* If HW is not ready, prepare the conditions to check again */
		IWM_SETBITS(sc, IWM_CSR_HW_IF_CONFIG_REG,
		    IWM_CSR_HW_IF_CONFIG_REG_PREPARE);

		do {
			if (iwm_set_hw_ready(sc))
				return 0;
			DELAY(200);
			t += 200;
		} while (t < 150000);
		DELAY(25000);
	}

	return ETIMEDOUT;
}

void
iwm_apm_config(struct iwm_softc *sc)
{
	pcireg_t lctl, cap;

	/*
	 * HW bug W/A for instability in PCIe bus L0S->L1 transition.
	 * Check if BIOS (or OS) enabled L1-ASPM on this device.
	 * If so (likely), disable L0S, so device moves directly L0->L1;
	 *    costs negligible amount of power savings.
	 * If not (unlikely), enable L0S, so there is at least some
	 *    power savings, even without L1.
	 */
	lctl = pci_conf_read(sc->sc_pct, sc->sc_pcitag,
	    sc->sc_cap_off + PCI_PCIE_LCSR);
	if (lctl & PCI_PCIE_LCSR_ASPM_L1) {
		IWM_SETBITS(sc, IWM_CSR_GIO_REG,
		    IWM_CSR_GIO_REG_VAL_L0S_ENABLED);
	} else {
		IWM_CLRBITS(sc, IWM_CSR_GIO_REG,
		    IWM_CSR_GIO_REG_VAL_L0S_ENABLED);
	}

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
iwm_apm_init(struct iwm_softc *sc)
{
	int err = 0;

	/* Disable L0S exit timer (platform NMI workaround) */
	if (sc->sc_device_family < IWM_DEVICE_FAMILY_8000)
		IWM_SETBITS(sc, IWM_CSR_GIO_CHICKEN_BITS,
		    IWM_CSR_GIO_CHICKEN_BITS_REG_BIT_DIS_L0S_EXIT_TIMER);

	/*
	 * Disable L0s without affecting L1;
	 *  don't wait for ICH L0s (ICH bug W/A)
	 */
	IWM_SETBITS(sc, IWM_CSR_GIO_CHICKEN_BITS,
	    IWM_CSR_GIO_CHICKEN_BITS_REG_BIT_L1A_NO_L0S_RX);

	/* Set FH wait threshold to maximum (HW error during stress W/A) */
	IWM_SETBITS(sc, IWM_CSR_DBG_HPET_MEM_REG, IWM_CSR_DBG_HPET_MEM_REG_VAL);

	/*
	 * Enable HAP INTA (interrupt from management bus) to
	 * wake device's PCI Express link L1a -> L0s
	 */
	IWM_SETBITS(sc, IWM_CSR_HW_IF_CONFIG_REG,
	    IWM_CSR_HW_IF_CONFIG_REG_BIT_HAP_WAKE_L1A);

	iwm_apm_config(sc);

#if 0 /* not for 7k/8k */
	/* Configure analog phase-lock-loop before activating to D0A */
	if (trans->cfg->base_params->pll_cfg_val)
		IWM_SETBITS(trans, IWM_CSR_ANA_PLL_CFG,
		    trans->cfg->base_params->pll_cfg_val);
#endif

	/*
	 * Set "initialization complete" bit to move adapter from
	 * D0U* --> D0A* (powered-up active) state.
	 */
	IWM_SETBITS(sc, IWM_CSR_GP_CNTRL, IWM_CSR_GP_CNTRL_REG_FLAG_INIT_DONE);

	/*
	 * Wait for clock stabilization; once stabilized, access to
	 * device-internal resources is supported, e.g. iwm_write_prph()
	 * and accesses to uCode SRAM.
	 */
	if (!iwm_poll_bit(sc, IWM_CSR_GP_CNTRL,
	    IWM_CSR_GP_CNTRL_REG_FLAG_MAC_CLOCK_READY,
	    IWM_CSR_GP_CNTRL_REG_FLAG_MAC_CLOCK_READY, 25000)) {
		printf("%s: timeout waiting for clock stabilization\n",
		    DEVNAME(sc));
		err = ETIMEDOUT;
		goto out;
	}

	if (sc->host_interrupt_operation_mode) {
		/*
		 * This is a bit of an abuse - This is needed for 7260 / 3160
		 * only check host_interrupt_operation_mode even if this is
		 * not related to host_interrupt_operation_mode.
		 *
		 * Enable the oscillator to count wake up time for L1 exit. This
		 * consumes slightly more power (100uA) - but allows to be sure
		 * that we wake up from L1 on time.
		 *
		 * This looks weird: read twice the same register, discard the
		 * value, set a bit, and yet again, read that same register
		 * just to discard the value. But that's the way the hardware
		 * seems to like it.
		 */
		if (iwm_nic_lock(sc)) {
			iwm_read_prph(sc, IWM_OSC_CLK);
			iwm_read_prph(sc, IWM_OSC_CLK);
			iwm_nic_unlock(sc);
		}
		err = iwm_set_bits_prph(sc, IWM_OSC_CLK,
		    IWM_OSC_CLK_FORCE_CONTROL);
		if (err)
			goto out;
		if (iwm_nic_lock(sc)) {
			iwm_read_prph(sc, IWM_OSC_CLK);
			iwm_read_prph(sc, IWM_OSC_CLK);
			iwm_nic_unlock(sc);
		}
	}

	/*
	 * Enable DMA clock and wait for it to stabilize.
	 *
	 * Write to "CLK_EN_REG"; "1" bits enable clocks, while "0" bits
	 * do not disable clocks.  This preserves any hardware bits already
	 * set by default in "CLK_CTRL_REG" after reset.
	 */
	if (sc->sc_device_family == IWM_DEVICE_FAMILY_7000) {
		if (iwm_nic_lock(sc)) {
			iwm_write_prph(sc, IWM_APMG_CLK_EN_REG,
			    IWM_APMG_CLK_VAL_DMA_CLK_RQT);
			iwm_nic_unlock(sc);
		}
		DELAY(20);

		/* Disable L1-Active */
		err = iwm_set_bits_prph(sc, IWM_APMG_PCIDEV_STT_REG,
		    IWM_APMG_PCIDEV_STT_VAL_L1_ACT_DIS);
		if (err)
			goto out;

		/* Clear the interrupt in APMG if the NIC is in RFKILL */
		if (iwm_nic_lock(sc)) {
			iwm_write_prph(sc, IWM_APMG_RTC_INT_STT_REG,
			    IWM_APMG_RTC_INT_STT_RFKILL);
			iwm_nic_unlock(sc);
		}
	}
 out:
	if (err)
		printf("%s: apm init error %d\n", DEVNAME(sc), err);
	return err;
}

void
iwm_apm_stop(struct iwm_softc *sc)
{
	IWM_SETBITS(sc, IWM_CSR_DBG_LINK_PWR_MGMT_REG,
	    IWM_CSR_RESET_LINK_PWR_MGMT_DISABLED);
	IWM_SETBITS(sc, IWM_CSR_HW_IF_CONFIG_REG,
	    IWM_CSR_HW_IF_CONFIG_REG_PREPARE |
	    IWM_CSR_HW_IF_CONFIG_REG_ENABLE_PME);
	DELAY(1000);
	IWM_CLRBITS(sc, IWM_CSR_DBG_LINK_PWR_MGMT_REG,
	    IWM_CSR_RESET_LINK_PWR_MGMT_DISABLED);
	DELAY(5000);

	/* stop device's busmaster DMA activity */
	IWM_SETBITS(sc, IWM_CSR_RESET, IWM_CSR_RESET_REG_FLAG_STOP_MASTER);

	if (!iwm_poll_bit(sc, IWM_CSR_RESET,
	    IWM_CSR_RESET_REG_FLAG_MASTER_DISABLED,
	    IWM_CSR_RESET_REG_FLAG_MASTER_DISABLED, 100))
		printf("%s: timeout waiting for master\n", DEVNAME(sc));

	/*
	 * Clear "initialization complete" bit to move adapter from
	 * D0A* (powered-up Active) --> D0U* (Uninitialized) state.
	 */
	IWM_CLRBITS(sc, IWM_CSR_GP_CNTRL,
	    IWM_CSR_GP_CNTRL_REG_FLAG_INIT_DONE);
}

void
iwm_init_msix_hw(struct iwm_softc *sc)
{
	iwm_conf_msix_hw(sc, 0);

	if (!sc->sc_msix)
		return;

	sc->sc_fh_init_mask = ~IWM_READ(sc, IWM_CSR_MSIX_FH_INT_MASK_AD);
	sc->sc_fh_mask = sc->sc_fh_init_mask;
	sc->sc_hw_init_mask = ~IWM_READ(sc, IWM_CSR_MSIX_HW_INT_MASK_AD);
	sc->sc_hw_mask = sc->sc_hw_init_mask;
}

void
iwm_conf_msix_hw(struct iwm_softc *sc, int stopped)
{
	int vector = 0;

	if (!sc->sc_msix) {
		/* Newer chips default to MSIX. */
		if (sc->sc_mqrx_supported && !stopped && iwm_nic_lock(sc)) {
			iwm_write_prph(sc, IWM_UREG_CHICK,
			    IWM_UREG_CHICK_MSI_ENABLE);
			iwm_nic_unlock(sc);
		}
		return;
	}

	if (!stopped && iwm_nic_lock(sc)) {
		iwm_write_prph(sc, IWM_UREG_CHICK, IWM_UREG_CHICK_MSIX_ENABLE);
		iwm_nic_unlock(sc);
	}

	/* Disable all interrupts */
	IWM_WRITE(sc, IWM_CSR_MSIX_FH_INT_MASK_AD, ~0);
	IWM_WRITE(sc, IWM_CSR_MSIX_HW_INT_MASK_AD, ~0);

	/* Map fallback-queue (command/mgmt) to a single vector */
	IWM_WRITE_1(sc, IWM_CSR_MSIX_RX_IVAR(0),
	    vector | IWM_MSIX_NON_AUTO_CLEAR_CAUSE);
	/* Map RSS queue (data) to the same vector */
	IWM_WRITE_1(sc, IWM_CSR_MSIX_RX_IVAR(1),
	    vector | IWM_MSIX_NON_AUTO_CLEAR_CAUSE);

	/* Enable the RX queues cause interrupts */
	IWM_CLRBITS(sc, IWM_CSR_MSIX_FH_INT_MASK_AD,
	    IWM_MSIX_FH_INT_CAUSES_Q0 | IWM_MSIX_FH_INT_CAUSES_Q1);

	/* Map non-RX causes to the same vector */
	IWM_WRITE_1(sc, IWM_CSR_MSIX_IVAR(IWM_MSIX_IVAR_CAUSE_D2S_CH0_NUM),
	    vector | IWM_MSIX_NON_AUTO_CLEAR_CAUSE);
	IWM_WRITE_1(sc, IWM_CSR_MSIX_IVAR(IWM_MSIX_IVAR_CAUSE_D2S_CH1_NUM),
	    vector | IWM_MSIX_NON_AUTO_CLEAR_CAUSE);
	IWM_WRITE_1(sc, IWM_CSR_MSIX_IVAR(IWM_MSIX_IVAR_CAUSE_S2D),
	    vector | IWM_MSIX_NON_AUTO_CLEAR_CAUSE);
	IWM_WRITE_1(sc, IWM_CSR_MSIX_IVAR(IWM_MSIX_IVAR_CAUSE_FH_ERR),
	    vector | IWM_MSIX_NON_AUTO_CLEAR_CAUSE);
	IWM_WRITE_1(sc, IWM_CSR_MSIX_IVAR(IWM_MSIX_IVAR_CAUSE_REG_ALIVE),
	    vector | IWM_MSIX_NON_AUTO_CLEAR_CAUSE);
	IWM_WRITE_1(sc, IWM_CSR_MSIX_IVAR(IWM_MSIX_IVAR_CAUSE_REG_WAKEUP),
	    vector | IWM_MSIX_NON_AUTO_CLEAR_CAUSE);
	IWM_WRITE_1(sc, IWM_CSR_MSIX_IVAR(IWM_MSIX_IVAR_CAUSE_REG_IML),
	    vector | IWM_MSIX_NON_AUTO_CLEAR_CAUSE);
	IWM_WRITE_1(sc, IWM_CSR_MSIX_IVAR(IWM_MSIX_IVAR_CAUSE_REG_CT_KILL),
	    vector | IWM_MSIX_NON_AUTO_CLEAR_CAUSE);
	IWM_WRITE_1(sc, IWM_CSR_MSIX_IVAR(IWM_MSIX_IVAR_CAUSE_REG_RF_KILL),
	    vector | IWM_MSIX_NON_AUTO_CLEAR_CAUSE);
	IWM_WRITE_1(sc, IWM_CSR_MSIX_IVAR(IWM_MSIX_IVAR_CAUSE_REG_PERIODIC),
	    vector | IWM_MSIX_NON_AUTO_CLEAR_CAUSE);
	IWM_WRITE_1(sc, IWM_CSR_MSIX_IVAR(IWM_MSIX_IVAR_CAUSE_REG_SW_ERR),
	    vector | IWM_MSIX_NON_AUTO_CLEAR_CAUSE);
	IWM_WRITE_1(sc, IWM_CSR_MSIX_IVAR(IWM_MSIX_IVAR_CAUSE_REG_SCD),
	    vector | IWM_MSIX_NON_AUTO_CLEAR_CAUSE);
	IWM_WRITE_1(sc, IWM_CSR_MSIX_IVAR(IWM_MSIX_IVAR_CAUSE_REG_FH_TX),
	    vector | IWM_MSIX_NON_AUTO_CLEAR_CAUSE);
	IWM_WRITE_1(sc, IWM_CSR_MSIX_IVAR(IWM_MSIX_IVAR_CAUSE_REG_HW_ERR),
	    vector | IWM_MSIX_NON_AUTO_CLEAR_CAUSE);
	IWM_WRITE_1(sc, IWM_CSR_MSIX_IVAR(IWM_MSIX_IVAR_CAUSE_REG_HAP),
	    vector | IWM_MSIX_NON_AUTO_CLEAR_CAUSE);

	/* Enable non-RX causes interrupts */
	IWM_CLRBITS(sc, IWM_CSR_MSIX_FH_INT_MASK_AD,
	    IWM_MSIX_FH_INT_CAUSES_D2S_CH0_NUM |
	    IWM_MSIX_FH_INT_CAUSES_D2S_CH1_NUM |
	    IWM_MSIX_FH_INT_CAUSES_S2D |
	    IWM_MSIX_FH_INT_CAUSES_FH_ERR);
	IWM_CLRBITS(sc, IWM_CSR_MSIX_HW_INT_MASK_AD,
	    IWM_MSIX_HW_INT_CAUSES_REG_ALIVE |
	    IWM_MSIX_HW_INT_CAUSES_REG_WAKEUP |
	    IWM_MSIX_HW_INT_CAUSES_REG_IML |
	    IWM_MSIX_HW_INT_CAUSES_REG_CT_KILL |
	    IWM_MSIX_HW_INT_CAUSES_REG_RF_KILL |
	    IWM_MSIX_HW_INT_CAUSES_REG_PERIODIC |
	    IWM_MSIX_HW_INT_CAUSES_REG_SW_ERR |
	    IWM_MSIX_HW_INT_CAUSES_REG_SCD |
	    IWM_MSIX_HW_INT_CAUSES_REG_FH_TX |
	    IWM_MSIX_HW_INT_CAUSES_REG_HW_ERR |
	    IWM_MSIX_HW_INT_CAUSES_REG_HAP);
}

int
iwm_clear_persistence_bit(struct iwm_softc *sc)
{
	uint32_t hpm, wprot;

	hpm = iwm_read_prph_unlocked(sc, IWM_HPM_DEBUG);
	if (hpm != 0xa5a5a5a0 && (hpm & IWM_HPM_PERSISTENCE_BIT)) {
		wprot = iwm_read_prph_unlocked(sc, IWM_PREG_PRPH_WPROT_9000);
		if (wprot & IWM_PREG_WFPM_ACCESS) {
			printf("%s: cannot clear persistence bit\n",
			    DEVNAME(sc));
			return EPERM;
		}
		iwm_write_prph_unlocked(sc, IWM_HPM_DEBUG,
		    hpm & ~IWM_HPM_PERSISTENCE_BIT);
	}

	return 0;
}

int
iwm_start_hw(struct iwm_softc *sc)
{
	int err;

	err = iwm_prepare_card_hw(sc);
	if (err)
		return err;

	if (sc->sc_device_family == IWM_DEVICE_FAMILY_9000) {
		err = iwm_clear_persistence_bit(sc);
		if (err)
			return err;
	}

	/* Reset the entire device */
	IWM_WRITE(sc, IWM_CSR_RESET, IWM_CSR_RESET_REG_FLAG_SW_RESET);
	DELAY(5000);

	err = iwm_apm_init(sc);
	if (err)
		return err;

	iwm_init_msix_hw(sc);

	iwm_enable_rfkill_int(sc);
	iwm_check_rfkill(sc);

	return 0;
}


void
iwm_stop_device(struct iwm_softc *sc)
{
	int chnl, ntries;
	int qid;

	iwm_disable_interrupts(sc);
	sc->sc_flags &= ~IWM_FLAG_USE_ICT;

	/* Stop all DMA channels. */
	if (iwm_nic_lock(sc)) {
		/* Deactivate TX scheduler. */
		iwm_write_prph(sc, IWM_SCD_TXFACT, 0);

		for (chnl = 0; chnl < IWM_FH_TCSR_CHNL_NUM; chnl++) {
			IWM_WRITE(sc,
			    IWM_FH_TCSR_CHNL_TX_CONFIG_REG(chnl), 0);
			for (ntries = 0; ntries < 200; ntries++) {
				uint32_t r;

				r = IWM_READ(sc, IWM_FH_TSSR_TX_STATUS_REG);
				if (r & IWM_FH_TSSR_TX_STATUS_REG_MSK_CHNL_IDLE(
				    chnl))
					break;
				DELAY(20);
			}
		}
		iwm_nic_unlock(sc);
	}
	iwm_disable_rx_dma(sc);

	iwm_reset_rx_ring(sc, &sc->rxq);

	for (qid = 0; qid < nitems(sc->txq); qid++)
		iwm_reset_tx_ring(sc, &sc->txq[qid]);

	if (sc->sc_device_family == IWM_DEVICE_FAMILY_7000) {
		if (iwm_nic_lock(sc)) {
			/* Power-down device's busmaster DMA clocks */
			iwm_write_prph(sc, IWM_APMG_CLK_DIS_REG,
			    IWM_APMG_CLK_VAL_DMA_CLK_RQT);
			iwm_nic_unlock(sc);
		}
		DELAY(5);
	}

	/* Make sure (redundant) we've released our request to stay awake */
	IWM_CLRBITS(sc, IWM_CSR_GP_CNTRL,
	    IWM_CSR_GP_CNTRL_REG_FLAG_MAC_ACCESS_REQ);
	if (sc->sc_nic_locks > 0)
		printf("%s: %d active NIC locks forcefully cleared\n",
		    DEVNAME(sc), sc->sc_nic_locks);
	sc->sc_nic_locks = 0;

	/* Stop the device, and put it in low power state */
	iwm_apm_stop(sc);

	/* Reset the on-board processor. */
	IWM_WRITE(sc, IWM_CSR_RESET, IWM_CSR_RESET_REG_FLAG_SW_RESET);
	DELAY(5000);

	/*
	 * Upon stop, the IVAR table gets erased, so msi-x won't
	 * work. This causes a bug in RF-KILL flows, since the interrupt
	 * that enables radio won't fire on the correct irq, and the
	 * driver won't be able to handle the interrupt.
	 * Configure the IVAR table again after reset.
	 */
	iwm_conf_msix_hw(sc, 1);

	/* 
	 * Upon stop, the APM issues an interrupt if HW RF kill is set.
	 * Clear the interrupt again.
	 */
	iwm_disable_interrupts(sc);

	/* Even though we stop the HW we still want the RF kill interrupt. */
	iwm_enable_rfkill_int(sc);
	iwm_check_rfkill(sc);

	iwm_prepare_card_hw(sc);
}

void
iwm_nic_config(struct iwm_softc *sc)
{
	uint8_t radio_cfg_type, radio_cfg_step, radio_cfg_dash;
	uint32_t mask, val, reg_val = 0;

	radio_cfg_type = (sc->sc_fw_phy_config & IWM_FW_PHY_CFG_RADIO_TYPE) >>
	    IWM_FW_PHY_CFG_RADIO_TYPE_POS;
	radio_cfg_step = (sc->sc_fw_phy_config & IWM_FW_PHY_CFG_RADIO_STEP) >>
	    IWM_FW_PHY_CFG_RADIO_STEP_POS;
	radio_cfg_dash = (sc->sc_fw_phy_config & IWM_FW_PHY_CFG_RADIO_DASH) >>
	    IWM_FW_PHY_CFG_RADIO_DASH_POS;

	reg_val |= IWM_CSR_HW_REV_STEP(sc->sc_hw_rev) <<
	    IWM_CSR_HW_IF_CONFIG_REG_POS_MAC_STEP;
	reg_val |= IWM_CSR_HW_REV_DASH(sc->sc_hw_rev) <<
	    IWM_CSR_HW_IF_CONFIG_REG_POS_MAC_DASH;

	/* radio configuration */
	reg_val |= radio_cfg_type << IWM_CSR_HW_IF_CONFIG_REG_POS_PHY_TYPE;
	reg_val |= radio_cfg_step << IWM_CSR_HW_IF_CONFIG_REG_POS_PHY_STEP;
	reg_val |= radio_cfg_dash << IWM_CSR_HW_IF_CONFIG_REG_POS_PHY_DASH;

	mask = IWM_CSR_HW_IF_CONFIG_REG_MSK_MAC_DASH |
	    IWM_CSR_HW_IF_CONFIG_REG_MSK_MAC_STEP |
	    IWM_CSR_HW_IF_CONFIG_REG_MSK_PHY_STEP |
	    IWM_CSR_HW_IF_CONFIG_REG_MSK_PHY_DASH |
	    IWM_CSR_HW_IF_CONFIG_REG_MSK_PHY_TYPE |
	    IWM_CSR_HW_IF_CONFIG_REG_BIT_RADIO_SI |
	    IWM_CSR_HW_IF_CONFIG_REG_BIT_MAC_SI;

	val = IWM_READ(sc, IWM_CSR_HW_IF_CONFIG_REG);
	val &= ~mask;
	val |= reg_val;
	IWM_WRITE(sc, IWM_CSR_HW_IF_CONFIG_REG, val);

	/*
	 * W/A : NIC is stuck in a reset state after Early PCIe power off
	 * (PCIe power is lost before PERST# is asserted), causing ME FW
	 * to lose ownership and not being able to obtain it back.
	 */
	if (sc->sc_device_family == IWM_DEVICE_FAMILY_7000)
		iwm_set_bits_mask_prph(sc, IWM_APMG_PS_CTRL_REG,
		    IWM_APMG_PS_CTRL_EARLY_PWR_OFF_RESET_DIS,
		    ~IWM_APMG_PS_CTRL_EARLY_PWR_OFF_RESET_DIS);
}

int
iwm_nic_rx_init(struct iwm_softc *sc)
{
	if (sc->sc_mqrx_supported)
		return iwm_nic_rx_mq_init(sc);
	else
		return iwm_nic_rx_legacy_init(sc);
}

int
iwm_nic_rx_mq_init(struct iwm_softc *sc)
{
	int enabled;

	if (!iwm_nic_lock(sc))
		return EBUSY;

	/* Stop RX DMA. */
	iwm_write_prph(sc, IWM_RFH_RXF_DMA_CFG, 0);
	/* Disable RX used and free queue operation. */
	iwm_write_prph(sc, IWM_RFH_RXF_RXQ_ACTIVE, 0);

	iwm_write_prph64(sc, IWM_RFH_Q0_FRBDCB_BA_LSB,
	    sc->rxq.free_desc_dma.paddr);
	iwm_write_prph64(sc, IWM_RFH_Q0_URBDCB_BA_LSB,
	    sc->rxq.used_desc_dma.paddr);
	iwm_write_prph64(sc, IWM_RFH_Q0_URBD_STTS_WPTR_LSB,
	    sc->rxq.stat_dma.paddr);
	iwm_write_prph(sc, IWM_RFH_Q0_FRBDCB_WIDX, 0);
	iwm_write_prph(sc, IWM_RFH_Q0_FRBDCB_RIDX, 0);
	iwm_write_prph(sc, IWM_RFH_Q0_URBDCB_WIDX, 0);

	/* We configure only queue 0 for now. */
	enabled = ((1 << 0) << 16) | (1 << 0);

	/* Enable RX DMA, 4KB buffer size. */
	iwm_write_prph(sc, IWM_RFH_RXF_DMA_CFG,
	    IWM_RFH_DMA_EN_ENABLE_VAL |
	    IWM_RFH_RXF_DMA_RB_SIZE_4K |
	    IWM_RFH_RXF_DMA_MIN_RB_4_8 |
	    IWM_RFH_RXF_DMA_DROP_TOO_LARGE_MASK |
	    IWM_RFH_RXF_DMA_RBDCB_SIZE_512);

	/* Enable RX DMA snooping. */
	iwm_write_prph(sc, IWM_RFH_GEN_CFG,
	    IWM_RFH_GEN_CFG_RFH_DMA_SNOOP |
	    IWM_RFH_GEN_CFG_SERVICE_DMA_SNOOP |
	    (sc->sc_integrated ? IWM_RFH_GEN_CFG_RB_CHUNK_SIZE_64 :
	    IWM_RFH_GEN_CFG_RB_CHUNK_SIZE_128));

	/* Enable the configured queue(s). */
	iwm_write_prph(sc, IWM_RFH_RXF_RXQ_ACTIVE, enabled);

	iwm_nic_unlock(sc);

	IWM_WRITE_1(sc, IWM_CSR_INT_COALESCING, IWM_HOST_INT_TIMEOUT_DEF);

	IWM_WRITE(sc, IWM_RFH_Q0_FRBDCB_WIDX_TRG, 8);

	return 0;
}

int
iwm_nic_rx_legacy_init(struct iwm_softc *sc)
{
	memset(sc->rxq.stat, 0, sizeof(*sc->rxq.stat));

	iwm_disable_rx_dma(sc);

	if (!iwm_nic_lock(sc))
		return EBUSY;

	/* reset and flush pointers */
	IWM_WRITE(sc, IWM_FH_MEM_RCSR_CHNL0_RBDCB_WPTR, 0);
	IWM_WRITE(sc, IWM_FH_MEM_RCSR_CHNL0_FLUSH_RB_REQ, 0);
	IWM_WRITE(sc, IWM_FH_RSCSR_CHNL0_RDPTR, 0);
	IWM_WRITE(sc, IWM_FH_RSCSR_CHNL0_RBDCB_WPTR_REG, 0);

	/* Set physical address of RX ring (256-byte aligned). */
	IWM_WRITE(sc,
	    IWM_FH_RSCSR_CHNL0_RBDCB_BASE_REG, sc->rxq.free_desc_dma.paddr >> 8);

	/* Set physical address of RX status (16-byte aligned). */
	IWM_WRITE(sc,
	    IWM_FH_RSCSR_CHNL0_STTS_WPTR_REG, sc->rxq.stat_dma.paddr >> 4);

	/* Enable RX. */
	IWM_WRITE(sc, IWM_FH_MEM_RCSR_CHNL0_CONFIG_REG,
	    IWM_FH_RCSR_RX_CONFIG_CHNL_EN_ENABLE_VAL		|
	    IWM_FH_RCSR_CHNL0_RX_IGNORE_RXF_EMPTY		|  /* HW bug */
	    IWM_FH_RCSR_CHNL0_RX_CONFIG_IRQ_DEST_INT_HOST_VAL	|
	    (IWM_RX_RB_TIMEOUT << IWM_FH_RCSR_RX_CONFIG_REG_IRQ_RBTH_POS) |
	    IWM_FH_RCSR_RX_CONFIG_REG_VAL_RB_SIZE_4K		|
	    IWM_RX_QUEUE_SIZE_LOG << IWM_FH_RCSR_RX_CONFIG_RBDCB_SIZE_POS);

	IWM_WRITE_1(sc, IWM_CSR_INT_COALESCING, IWM_HOST_INT_TIMEOUT_DEF);

	/* W/A for interrupt coalescing bug in 7260 and 3160 */
	if (sc->host_interrupt_operation_mode)
		IWM_SETBITS(sc, IWM_CSR_INT_COALESCING, IWM_HOST_INT_OPER_MODE);

	iwm_nic_unlock(sc);

	/*
	 * This value should initially be 0 (before preparing any RBs),
	 * and should be 8 after preparing the first 8 RBs (for example).
	 */
	IWM_WRITE(sc, IWM_FH_RSCSR_CHNL0_WPTR, 8);

	return 0;
}

int
iwm_nic_tx_init(struct iwm_softc *sc)
{
	int qid, err;

	if (!iwm_nic_lock(sc))
		return EBUSY;

	/* Deactivate TX scheduler. */
	iwm_write_prph(sc, IWM_SCD_TXFACT, 0);

	/* Set physical address of "keep warm" page (16-byte aligned). */
	IWM_WRITE(sc, IWM_FH_KW_MEM_ADDR_REG, sc->kw_dma.paddr >> 4);

	for (qid = 0; qid < nitems(sc->txq); qid++) {
		struct iwm_tx_ring *txq = &sc->txq[qid];

		/* Set physical address of TX ring (256-byte aligned). */
		IWM_WRITE(sc, IWM_FH_MEM_CBBC_QUEUE(qid),
		    txq->desc_dma.paddr >> 8);
	}

	err = iwm_set_bits_prph(sc, IWM_SCD_GP_CTRL,
	    IWM_SCD_GP_CTRL_AUTO_ACTIVE_MODE |
	    IWM_SCD_GP_CTRL_ENABLE_31_QUEUES);

	iwm_nic_unlock(sc);

	return err;
}

int
iwm_nic_init(struct iwm_softc *sc)
{
	int err;

	iwm_apm_init(sc);
	if (sc->sc_device_family == IWM_DEVICE_FAMILY_7000)
		iwm_set_bits_mask_prph(sc, IWM_APMG_PS_CTRL_REG,
		    IWM_APMG_PS_CTRL_VAL_PWR_SRC_VMAIN,
		    ~IWM_APMG_PS_CTRL_MSK_PWR_SRC);

	iwm_nic_config(sc);

	err = iwm_nic_rx_init(sc);
	if (err)
		return err;

	err = iwm_nic_tx_init(sc);
	if (err)
		return err;

	IWM_SETBITS(sc, IWM_CSR_MAC_SHADOW_REG_CTRL, 0x800fffff);

	return 0;
}

/* Map a TID to an ieee80211_edca_ac category. */
const uint8_t iwm_tid_to_ac[IWM_MAX_TID_COUNT] = {
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
const uint8_t iwm_ac_to_tx_fifo[] = {
	IWM_TX_FIFO_BE,
	IWM_TX_FIFO_BK,
	IWM_TX_FIFO_VI,
	IWM_TX_FIFO_VO,
};

int
iwm_enable_ac_txq(struct iwm_softc *sc, int qid, int fifo)
{
	int err;
	iwm_nic_assert_locked(sc);

	IWM_WRITE(sc, IWM_HBUS_TARG_WRPTR, qid << 8 | 0);

	iwm_write_prph(sc, IWM_SCD_QUEUE_STATUS_BITS(qid),
	    (0 << IWM_SCD_QUEUE_STTS_REG_POS_ACTIVE)
	    | (1 << IWM_SCD_QUEUE_STTS_REG_POS_SCD_ACT_EN));

	err = iwm_clear_bits_prph(sc, IWM_SCD_AGGR_SEL, (1 << qid));
	if (err) {
		return err;
	}

	iwm_write_prph(sc, IWM_SCD_QUEUE_RDPTR(qid), 0);

	iwm_write_mem32(sc,
	    sc->sched_base + IWM_SCD_CONTEXT_QUEUE_OFFSET(qid), 0);

	/* Set scheduler window size and frame limit. */
	iwm_write_mem32(sc,
	    sc->sched_base + IWM_SCD_CONTEXT_QUEUE_OFFSET(qid) +
	    sizeof(uint32_t),
	    ((IWM_FRAME_LIMIT << IWM_SCD_QUEUE_CTX_REG2_WIN_SIZE_POS) &
	    IWM_SCD_QUEUE_CTX_REG2_WIN_SIZE_MSK) |
	    ((IWM_FRAME_LIMIT
		<< IWM_SCD_QUEUE_CTX_REG2_FRAME_LIMIT_POS) &
	    IWM_SCD_QUEUE_CTX_REG2_FRAME_LIMIT_MSK));

	iwm_write_prph(sc, IWM_SCD_QUEUE_STATUS_BITS(qid),
	    (1 << IWM_SCD_QUEUE_STTS_REG_POS_ACTIVE) |
	    (fifo << IWM_SCD_QUEUE_STTS_REG_POS_TXF) |
	    (1 << IWM_SCD_QUEUE_STTS_REG_POS_WSL) |
	    IWM_SCD_QUEUE_STTS_REG_MSK);

	if (qid == sc->cmdqid)
		iwm_write_prph(sc, IWM_SCD_EN_CTRL,
		    iwm_read_prph(sc, IWM_SCD_EN_CTRL) | (1 << qid));

	return 0;
}

int
iwm_enable_txq(struct iwm_softc *sc, int sta_id, int qid, int fifo,
    int aggregate, uint8_t tid, uint16_t ssn)
{
	struct iwm_tx_ring *ring = &sc->txq[qid];
	struct iwm_scd_txq_cfg_cmd cmd;
	int err, idx, scd_bug;

	iwm_nic_assert_locked(sc);

	/*
	 * If we need to move the SCD write pointer by steps of
	 * 0x40, 0x80 or 0xc0, it gets stuck.
	 * This is really ugly, but this is the easiest way out for
	 * this sad hardware issue.
	 * This bug has been fixed on devices 9000 and up.
	 */
	scd_bug = !sc->sc_mqrx_supported &&
		!((ssn - ring->cur) & 0x3f) &&
		(ssn != ring->cur);
	if (scd_bug)
		ssn = (ssn + 1) & 0xfff;

	idx = IWM_AGG_SSN_TO_TXQ_IDX(ssn);
	IWM_WRITE(sc, IWM_HBUS_TARG_WRPTR, qid << 8 | idx);
	ring->cur = idx;
	ring->tail = idx;

	memset(&cmd, 0, sizeof(cmd));
	cmd.tid = tid;
	cmd.scd_queue = qid;
	cmd.enable = 1;
	cmd.sta_id = sta_id;
	cmd.tx_fifo = fifo;
	cmd.aggregate = aggregate;
	cmd.ssn = htole16(ssn);
	cmd.window = IWM_FRAME_LIMIT;

	err = iwm_send_cmd_pdu(sc, IWM_SCD_QUEUE_CFG, 0,
	    sizeof(cmd), &cmd);
	if (err)
		return err;

	sc->qenablemsk |= (1 << qid);
	return 0;
}

int
iwm_disable_txq(struct iwm_softc *sc, int sta_id, int qid, uint8_t tid)
{
	struct iwm_scd_txq_cfg_cmd cmd;
	int err;

	memset(&cmd, 0, sizeof(cmd));
	cmd.tid = tid;
	cmd.scd_queue = qid;
	cmd.enable = 0;
	cmd.sta_id = sta_id;

	err = iwm_send_cmd_pdu(sc, IWM_SCD_QUEUE_CFG, 0, sizeof(cmd), &cmd);
	if (err)
		return err;

	sc->qenablemsk &= ~(1 << qid);
	return 0;
}

int
iwm_post_alive(struct iwm_softc *sc)
{
	int nwords;
	int err, chnl;
	uint32_t base;

	if (!iwm_nic_lock(sc))
		return EBUSY;

	base = iwm_read_prph(sc, IWM_SCD_SRAM_BASE_ADDR);

	iwm_ict_reset(sc);

	iwm_nic_unlock(sc);

	/* Clear TX scheduler state in SRAM. */
	nwords = (IWM_SCD_TRANS_TBL_MEM_UPPER_BOUND -
	    IWM_SCD_CONTEXT_MEM_LOWER_BOUND)
	    / sizeof(uint32_t);
	err = iwm_write_mem(sc,
	    sc->sched_base + IWM_SCD_CONTEXT_MEM_LOWER_BOUND,
	    NULL, nwords);
	if (err)
		return err;

	if (!iwm_nic_lock(sc))
		return EBUSY;

	/* Set physical address of TX scheduler rings (1KB aligned). */
	iwm_write_prph(sc, IWM_SCD_DRAM_BASE_ADDR, sc->sched_dma.paddr >> 10);

	iwm_write_prph(sc, IWM_SCD_CHAINEXT_EN, 0);

	/* enable command channel */
	err = iwm_enable_ac_txq(sc, sc->cmdqid, IWM_TX_FIFO_CMD);
	if (err) {
		iwm_nic_unlock(sc);
		return err;
	}

	/* Activate TX scheduler. */
	iwm_write_prph(sc, IWM_SCD_TXFACT, 0xff);

	/* Enable DMA channels. */
	for (chnl = 0; chnl < IWM_FH_TCSR_CHNL_NUM; chnl++) {
		IWM_WRITE(sc, IWM_FH_TCSR_CHNL_TX_CONFIG_REG(chnl),
		    IWM_FH_TCSR_TX_CONFIG_REG_VAL_DMA_CHNL_ENABLE |
		    IWM_FH_TCSR_TX_CONFIG_REG_VAL_DMA_CREDIT_ENABLE);
	}

	IWM_SETBITS(sc, IWM_FH_TX_CHICKEN_BITS_REG,
	    IWM_FH_TX_CHICKEN_BITS_SCD_AUTO_RETRY_EN);

	iwm_nic_unlock(sc);

	/* Enable L1-Active */
	if (sc->sc_device_family < IWM_DEVICE_FAMILY_8000) {
		err = iwm_clear_bits_prph(sc, IWM_APMG_PCIDEV_STT_REG,
		    IWM_APMG_PCIDEV_STT_VAL_L1_ACT_DIS);
	}

	return err;
}

struct iwm_phy_db_entry *
iwm_phy_db_get_section(struct iwm_softc *sc, uint16_t type, uint16_t chg_id)
{
	struct iwm_phy_db *phy_db = &sc->sc_phy_db;

	if (type >= IWM_PHY_DB_MAX)
		return NULL;

	switch (type) {
	case IWM_PHY_DB_CFG:
		return &phy_db->cfg;
	case IWM_PHY_DB_CALIB_NCH:
		return &phy_db->calib_nch;
	case IWM_PHY_DB_CALIB_CHG_PAPD:
		if (chg_id >= IWM_NUM_PAPD_CH_GROUPS)
			return NULL;
		return &phy_db->calib_ch_group_papd[chg_id];
	case IWM_PHY_DB_CALIB_CHG_TXP:
		if (chg_id >= IWM_NUM_TXP_CH_GROUPS)
			return NULL;
		return &phy_db->calib_ch_group_txp[chg_id];
	default:
		return NULL;
	}
	return NULL;
}

int
iwm_phy_db_set_section(struct iwm_softc *sc,
    struct iwm_calib_res_notif_phy_db *phy_db_notif)
{
	uint16_t type = le16toh(phy_db_notif->type);
	uint16_t size  = le16toh(phy_db_notif->length);
	struct iwm_phy_db_entry *entry;
	uint16_t chg_id = 0;

	if (type == IWM_PHY_DB_CALIB_CHG_PAPD ||
	    type == IWM_PHY_DB_CALIB_CHG_TXP)
		chg_id = le16toh(*(uint16_t *)phy_db_notif->data);

	entry = iwm_phy_db_get_section(sc, type, chg_id);
	if (!entry)
		return EINVAL;

	if (entry->data)
		free(entry->data, M_DEVBUF, entry->size);
	entry->data = malloc(size, M_DEVBUF, M_NOWAIT);
	if (!entry->data) {
		entry->size = 0;
		return ENOMEM;
	}
	memcpy(entry->data, phy_db_notif->data, size);
	entry->size = size;

	return 0;
}

int
iwm_is_valid_channel(uint16_t ch_id)
{
	if (ch_id <= 14 ||
	    (36 <= ch_id && ch_id <= 64 && ch_id % 4 == 0) ||
	    (100 <= ch_id && ch_id <= 140 && ch_id % 4 == 0) ||
	    (145 <= ch_id && ch_id <= 165 && ch_id % 4 == 1))
		return 1;
	return 0;
}

uint8_t
iwm_ch_id_to_ch_index(uint16_t ch_id)
{
	if (!iwm_is_valid_channel(ch_id))
		return 0xff;

	if (ch_id <= 14)
		return ch_id - 1;
	if (ch_id <= 64)
		return (ch_id + 20) / 4;
	if (ch_id <= 140)
		return (ch_id - 12) / 4;
	return (ch_id - 13) / 4;
}


uint16_t
iwm_channel_id_to_papd(uint16_t ch_id)
{
	if (!iwm_is_valid_channel(ch_id))
		return 0xff;

	if (1 <= ch_id && ch_id <= 14)
		return 0;
	if (36 <= ch_id && ch_id <= 64)
		return 1;
	if (100 <= ch_id && ch_id <= 140)
		return 2;
	return 3;
}

uint16_t
iwm_channel_id_to_txp(struct iwm_softc *sc, uint16_t ch_id)
{
	struct iwm_phy_db *phy_db = &sc->sc_phy_db;
	struct iwm_phy_db_chg_txp *txp_chg;
	int i;
	uint8_t ch_index = iwm_ch_id_to_ch_index(ch_id);

	if (ch_index == 0xff)
		return 0xff;

	for (i = 0; i < IWM_NUM_TXP_CH_GROUPS; i++) {
		txp_chg = (void *)phy_db->calib_ch_group_txp[i].data;
		if (!txp_chg)
			return 0xff;
		/*
		 * Looking for the first channel group the max channel
		 * of which is higher than the requested channel.
		 */
		if (le16toh(txp_chg->max_channel_idx) >= ch_index)
			return i;
	}
	return 0xff;
}

int
iwm_phy_db_get_section_data(struct iwm_softc *sc, uint32_t type, uint8_t **data,
    uint16_t *size, uint16_t ch_id)
{
	struct iwm_phy_db_entry *entry;
	uint16_t ch_group_id = 0;

	if (type == IWM_PHY_DB_CALIB_CHG_PAPD)
		ch_group_id = iwm_channel_id_to_papd(ch_id);
	else if (type == IWM_PHY_DB_CALIB_CHG_TXP)
		ch_group_id = iwm_channel_id_to_txp(sc, ch_id);

	entry = iwm_phy_db_get_section(sc, type, ch_group_id);
	if (!entry)
		return EINVAL;

	*data = entry->data;
	*size = entry->size;

	return 0;
}

int
iwm_send_phy_db_cmd(struct iwm_softc *sc, uint16_t type, uint16_t length,
    void *data)
{
	struct iwm_phy_db_cmd phy_db_cmd;
	struct iwm_host_cmd cmd = {
		.id = IWM_PHY_DB_CMD,
		.flags = IWM_CMD_ASYNC,
	};

	phy_db_cmd.type = le16toh(type);
	phy_db_cmd.length = le16toh(length);

	cmd.data[0] = &phy_db_cmd;
	cmd.len[0] = sizeof(struct iwm_phy_db_cmd);
	cmd.data[1] = data;
	cmd.len[1] = length;

	return iwm_send_cmd(sc, &cmd);
}

int
iwm_phy_db_send_all_channel_groups(struct iwm_softc *sc, uint16_t type,
    uint8_t max_ch_groups)
{
	uint16_t i;
	int err;
	struct iwm_phy_db_entry *entry;

	for (i = 0; i < max_ch_groups; i++) {
		entry = iwm_phy_db_get_section(sc, type, i);
		if (!entry)
			return EINVAL;

		if (!entry->size)
			continue;

		err = iwm_send_phy_db_cmd(sc, type, entry->size, entry->data);
		if (err)
			return err;

		DELAY(1000);
	}

	return 0;
}

int
iwm_send_phy_db_data(struct iwm_softc *sc)
{
	uint8_t *data = NULL;
	uint16_t size = 0;
	int err;

	err = iwm_phy_db_get_section_data(sc, IWM_PHY_DB_CFG, &data, &size, 0);
	if (err)
		return err;

	err = iwm_send_phy_db_cmd(sc, IWM_PHY_DB_CFG, size, data);
	if (err)
		return err;

	err = iwm_phy_db_get_section_data(sc, IWM_PHY_DB_CALIB_NCH,
	    &data, &size, 0);
	if (err)
		return err;

	err = iwm_send_phy_db_cmd(sc, IWM_PHY_DB_CALIB_NCH, size, data);
	if (err)
		return err;

	err = iwm_phy_db_send_all_channel_groups(sc,
	    IWM_PHY_DB_CALIB_CHG_PAPD, IWM_NUM_PAPD_CH_GROUPS);
	if (err)
		return err;

	err = iwm_phy_db_send_all_channel_groups(sc,
	    IWM_PHY_DB_CALIB_CHG_TXP, IWM_NUM_TXP_CH_GROUPS);
	if (err)
		return err;

	return 0;
}

/*
 * For the high priority TE use a time event type that has similar priority to
 * the FW's action scan priority.
 */
#define IWM_ROC_TE_TYPE_NORMAL IWM_TE_P2P_DEVICE_DISCOVERABLE
#define IWM_ROC_TE_TYPE_MGMT_TX IWM_TE_P2P_CLIENT_ASSOC

int
iwm_send_time_event_cmd(struct iwm_softc *sc,
    const struct iwm_time_event_cmd *cmd)
{
	struct iwm_rx_packet *pkt;
	struct iwm_time_event_resp *resp;
	struct iwm_host_cmd hcmd = {
		.id = IWM_TIME_EVENT_CMD,
		.flags = IWM_CMD_WANT_RESP,
		.resp_pkt_len = sizeof(*pkt) + sizeof(*resp),
	};
	uint32_t resp_len;
	int err;

	hcmd.data[0] = cmd;
	hcmd.len[0] = sizeof(*cmd);
	err = iwm_send_cmd(sc, &hcmd);
	if (err)
		return err;

	pkt = hcmd.resp_pkt;
	if (!pkt || (pkt->hdr.flags & IWM_CMD_FAILED_MSK)) {
		err = EIO;
		goto out;
	}

	resp_len = iwm_rx_packet_payload_len(pkt);
	if (resp_len != sizeof(*resp)) {
		err = EIO;
		goto out;
	}

	resp = (void *)pkt->data;
	if (le32toh(resp->status) == 0)
		sc->sc_time_event_uid = le32toh(resp->unique_id);
	else
		err = EIO;
out:
	iwm_free_resp(sc, &hcmd);
	return err;
}

void
iwm_protect_session(struct iwm_softc *sc, struct iwm_node *in,
    uint32_t duration, uint32_t max_delay)
{
	struct iwm_time_event_cmd time_cmd;

	/* Do nothing if a time event is already scheduled. */
	if (sc->sc_flags & IWM_FLAG_TE_ACTIVE)
		return;

	memset(&time_cmd, 0, sizeof(time_cmd));

	time_cmd.action = htole32(IWM_FW_CTXT_ACTION_ADD);
	time_cmd.id_and_color =
	    htole32(IWM_FW_CMD_ID_AND_COLOR(in->in_id, in->in_color));
	time_cmd.id = htole32(IWM_TE_BSS_STA_AGGRESSIVE_ASSOC);

	time_cmd.apply_time = htole32(0);

	time_cmd.max_frags = IWM_TE_V2_FRAG_NONE;
	time_cmd.max_delay = htole32(max_delay);
	/* TODO: why do we need to interval = bi if it is not periodic? */
	time_cmd.interval = htole32(1);
	time_cmd.duration = htole32(duration);
	time_cmd.repeat = 1;
	time_cmd.policy
	    = htole16(IWM_TE_V2_NOTIF_HOST_EVENT_START |
	        IWM_TE_V2_NOTIF_HOST_EVENT_END |
		IWM_T2_V2_START_IMMEDIATELY);

	if (iwm_send_time_event_cmd(sc, &time_cmd) == 0)
		sc->sc_flags |= IWM_FLAG_TE_ACTIVE;

	DELAY(100);
}

void
iwm_unprotect_session(struct iwm_softc *sc, struct iwm_node *in)
{
	struct iwm_time_event_cmd time_cmd;

	/* Do nothing if the time event has already ended. */
	if ((sc->sc_flags & IWM_FLAG_TE_ACTIVE) == 0)
		return;

	memset(&time_cmd, 0, sizeof(time_cmd));

	time_cmd.action = htole32(IWM_FW_CTXT_ACTION_REMOVE);
	time_cmd.id_and_color =
	    htole32(IWM_FW_CMD_ID_AND_COLOR(in->in_id, in->in_color));
	time_cmd.id = htole32(sc->sc_time_event_uid);

	if (iwm_send_time_event_cmd(sc, &time_cmd) == 0)
		sc->sc_flags &= ~IWM_FLAG_TE_ACTIVE;

	DELAY(100);
}

/*
 * NVM read access and content parsing.  We do not support
 * external NVM or writing NVM.
 */

/* list of NVM sections we are allowed/need to read */
const int iwm_nvm_to_read[] = {
	IWM_NVM_SECTION_TYPE_HW,
	IWM_NVM_SECTION_TYPE_SW,
	IWM_NVM_SECTION_TYPE_REGULATORY,
	IWM_NVM_SECTION_TYPE_CALIBRATION,
	IWM_NVM_SECTION_TYPE_PRODUCTION,
	IWM_NVM_SECTION_TYPE_REGULATORY_SDP,
	IWM_NVM_SECTION_TYPE_HW_8000,
	IWM_NVM_SECTION_TYPE_MAC_OVERRIDE,
	IWM_NVM_SECTION_TYPE_PHY_SKU,
};

#define IWM_NVM_DEFAULT_CHUNK_SIZE	(2*1024)

#define IWM_NVM_WRITE_OPCODE 1
#define IWM_NVM_READ_OPCODE 0

int
iwm_nvm_read_chunk(struct iwm_softc *sc, uint16_t section, uint16_t offset,
    uint16_t length, uint8_t *data, uint16_t *len)
{
	offset = 0;
	struct iwm_nvm_access_cmd nvm_access_cmd = {
		.offset = htole16(offset),
		.length = htole16(length),
		.type = htole16(section),
		.op_code = IWM_NVM_READ_OPCODE,
	};
	struct iwm_nvm_access_resp *nvm_resp;
	struct iwm_rx_packet *pkt;
	struct iwm_host_cmd cmd = {
		.id = IWM_NVM_ACCESS_CMD,
		.flags = (IWM_CMD_WANT_RESP | IWM_CMD_SEND_IN_RFKILL),
		.resp_pkt_len = IWM_CMD_RESP_MAX,
		.data = { &nvm_access_cmd, },
	};
	int err, offset_read;
	size_t bytes_read;
	uint8_t *resp_data;

	cmd.len[0] = sizeof(struct iwm_nvm_access_cmd);

	err = iwm_send_cmd(sc, &cmd);
	if (err)
		return err;

	pkt = cmd.resp_pkt;
	if (pkt->hdr.flags & IWM_CMD_FAILED_MSK) {
		err = EIO;
		goto exit;
	}

	/* Extract NVM response */
	nvm_resp = (void *)pkt->data;
	if (nvm_resp == NULL)
		return EIO;

	err = le16toh(nvm_resp->status);
	bytes_read = le16toh(nvm_resp->length);
	offset_read = le16toh(nvm_resp->offset);
	resp_data = nvm_resp->data;
	if (err) {
		err = EINVAL;
		goto exit;
	}

	if (offset_read != offset) {
		err = EINVAL;
		goto exit;
	}

	if (bytes_read > length) {
		err = EINVAL;
		goto exit;
	}

	memcpy(data + offset, resp_data, bytes_read);
	*len = bytes_read;

 exit:
	iwm_free_resp(sc, &cmd);
	return err;
}

/*
 * Reads an NVM section completely.
 * NICs prior to 7000 family doesn't have a real NVM, but just read
 * section 0 which is the EEPROM. Because the EEPROM reading is unlimited
 * by uCode, we need to manually check in this case that we don't
 * overflow and try to read more than the EEPROM size.
 */
int
iwm_nvm_read_section(struct iwm_softc *sc, uint16_t section, uint8_t *data,
    uint16_t *len, size_t max_len)
{
	uint16_t chunklen, seglen;
	int err = 0;

	chunklen = seglen = IWM_NVM_DEFAULT_CHUNK_SIZE;
	*len = 0;

	/* Read NVM chunks until exhausted (reading less than requested) */
	while (seglen == chunklen && *len < max_len) {
		err = iwm_nvm_read_chunk(sc,
		    section, *len, chunklen, data, &seglen);
		if (err)
			return err;

		*len += seglen;
	}

	return err;
}

uint8_t
iwm_fw_valid_tx_ant(struct iwm_softc *sc)
{
	uint8_t tx_ant;

	tx_ant = ((sc->sc_fw_phy_config & IWM_FW_PHY_CFG_TX_CHAIN)
	    >> IWM_FW_PHY_CFG_TX_CHAIN_POS);

	if (sc->sc_nvm.valid_tx_ant)
		tx_ant &= sc->sc_nvm.valid_tx_ant;

	return tx_ant;
}

uint8_t
iwm_fw_valid_rx_ant(struct iwm_softc *sc)
{
	uint8_t rx_ant;

	rx_ant = ((sc->sc_fw_phy_config & IWM_FW_PHY_CFG_RX_CHAIN)
	    >> IWM_FW_PHY_CFG_RX_CHAIN_POS);

	if (sc->sc_nvm.valid_rx_ant)
		rx_ant &= sc->sc_nvm.valid_rx_ant;

	return rx_ant;
}

int
iwm_valid_siso_ant_rate_mask(struct iwm_softc *sc)
{
	uint8_t valid_tx_ant = iwm_fw_valid_tx_ant(sc);

	/*
	 * According to the Linux driver, antenna B should be preferred
	 * on 9k devices since it is not shared with bluetooth. However,
	 * there are 9k devices which do not support antenna B at all.
	 */
	if (sc->sc_device_family == IWM_DEVICE_FAMILY_9000 &&
	    (valid_tx_ant & IWM_ANT_B))
		return IWM_RATE_MCS_ANT_B_MSK;

	return IWM_RATE_MCS_ANT_A_MSK;
}

void
iwm_init_channel_map(struct iwm_softc *sc, const uint16_t * const nvm_ch_flags,
    const uint8_t *nvm_channels, int nchan)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct iwm_nvm_data *data = &sc->sc_nvm;
	int ch_idx;
	struct ieee80211_channel *channel;
	uint16_t ch_flags;
	int is_5ghz;
	int flags, hw_value;

	for (ch_idx = 0; ch_idx < nchan; ch_idx++) {
		ch_flags = le16_to_cpup(nvm_ch_flags + ch_idx);

		if (ch_idx >= IWM_NUM_2GHZ_CHANNELS &&
		    !data->sku_cap_band_52GHz_enable)
			ch_flags &= ~IWM_NVM_CHANNEL_VALID;

		if (!(ch_flags & IWM_NVM_CHANNEL_VALID))
			continue;

		hw_value = nvm_channels[ch_idx];
		channel = &ic->ic_channels[hw_value];

		is_5ghz = ch_idx >= IWM_NUM_2GHZ_CHANNELS;
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

		if (!(ch_flags & IWM_NVM_CHANNEL_ACTIVE))
			channel->ic_flags |= IEEE80211_CHAN_PASSIVE;

		if (data->sku_cap_11n_enable) {
			channel->ic_flags |= IEEE80211_CHAN_HT;
			if (ch_flags & IWM_NVM_CHANNEL_40MHZ)
				channel->ic_flags |= IEEE80211_CHAN_40MHZ;
		}

		if (is_5ghz && data->sku_cap_11ac_enable) {
			channel->ic_flags |= IEEE80211_CHAN_VHT;
			if (ch_flags & IWM_NVM_CHANNEL_80MHZ)
				channel->ic_xflags |= IEEE80211_CHANX_80MHZ;
		}
	}
}

int
iwm_mimo_enabled(struct iwm_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;

	return !sc->sc_nvm.sku_cap_mimo_disable &&
	    (ic->ic_userflags & IEEE80211_F_NOMIMO) == 0;
}

void
iwm_setup_ht_rates(struct iwm_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	uint8_t rx_ant;

	/* TX is supported with the same MCS as RX. */
	ic->ic_tx_mcs_set = IEEE80211_TX_MCS_SET_DEFINED;

	memset(ic->ic_sup_mcs, 0, sizeof(ic->ic_sup_mcs));
	ic->ic_sup_mcs[0] = 0xff;		/* MCS 0-7 */

	if (!iwm_mimo_enabled(sc))
		return;

	rx_ant = iwm_fw_valid_rx_ant(sc);
	if ((rx_ant & IWM_ANT_AB) == IWM_ANT_AB ||
	    (rx_ant & IWM_ANT_BC) == IWM_ANT_BC)
		ic->ic_sup_mcs[1] = 0xff;	/* MCS 8-15 */
}

void
iwm_setup_vht_rates(struct iwm_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	uint8_t rx_ant = iwm_fw_valid_rx_ant(sc);
	int n;

	ic->ic_vht_rxmcs = (IEEE80211_VHT_MCS_0_9 <<
	    IEEE80211_VHT_MCS_FOR_SS_SHIFT(1));

	if (iwm_mimo_enabled(sc) &&
	    ((rx_ant & IWM_ANT_AB) == IWM_ANT_AB ||
	    (rx_ant & IWM_ANT_BC) == IWM_ANT_BC)) {
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
iwm_init_reorder_buffer(struct iwm_reorder_buffer *reorder_buf,
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
iwm_clear_reorder_buffer(struct iwm_softc *sc, struct iwm_rxba_data *rxba)
{
	int i;
	struct iwm_reorder_buffer *reorder_buf = &rxba->reorder_buf;
	struct iwm_reorder_buf_entry *entry;

	for (i = 0; i < reorder_buf->buf_size; i++) {
		entry = &rxba->entries[i];
		ml_purge(&entry->frames);
		timerclear(&entry->reorder_time);
	}

	reorder_buf->removed = 1;
	timeout_del(&reorder_buf->reorder_timer);
	timerclear(&rxba->last_rx);
	timeout_del(&rxba->session_timer);
	rxba->baid = IWM_RX_REORDER_DATA_INVALID_BAID;
}

#define RX_REORDER_BUF_TIMEOUT_MQ_USEC (100000ULL)

void
iwm_rx_ba_session_expired(void *arg)
{
	struct iwm_rxba_data *rxba = arg;
	struct iwm_softc *sc = rxba->sc;
	struct ieee80211com *ic = &sc->sc_ic;
	struct ieee80211_node *ni = ic->ic_bss;
	struct timeval now, timeout, expiry;
	int s;

	s = splnet();
	if ((sc->sc_flags & IWM_FLAG_SHUTDOWN) == 0 &&
	    ic->ic_state == IEEE80211_S_RUN &&
	    rxba->baid != IWM_RX_REORDER_DATA_INVALID_BAID) {
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
iwm_reorder_timer_expired(void *arg)
{
	struct mbuf_list ml = MBUF_LIST_INITIALIZER();
	struct iwm_reorder_buffer *buf = arg;
	struct iwm_rxba_data *rxba = iwm_rxba_data_from_reorder_buf(buf);
	struct iwm_reorder_buf_entry *entries = &rxba->entries[0];
	struct iwm_softc *sc = rxba->sc;
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
		iwm_release_frames(sc, ni, rxba, buf, sn, &ml);
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

#define IWM_MAX_RX_BA_SESSIONS 16

int
iwm_sta_rx_agg(struct iwm_softc *sc, struct ieee80211_node *ni, uint8_t tid,
    uint16_t ssn, uint16_t winsize, int timeout_val, int start)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct iwm_add_sta_cmd cmd;
	struct iwm_node *in = (void *)ni;
	int err, s;
	uint32_t status;
	size_t cmdsize;
	struct iwm_rxba_data *rxba = NULL;
	uint8_t baid = 0;

	s = splnet();

	if (start && sc->sc_rx_ba_sessions >= IWM_MAX_RX_BA_SESSIONS) {
		ieee80211_addba_req_refuse(ic, ni, tid);
		splx(s);
		return 0;
	}

	memset(&cmd, 0, sizeof(cmd));

	cmd.sta_id = IWM_STATION_ID;
	cmd.mac_id_n_color
	    = htole32(IWM_FW_CMD_ID_AND_COLOR(in->in_id, in->in_color));
	cmd.add_modify = IWM_STA_MODE_MODIFY;

	if (start) {
		cmd.add_immediate_ba_tid = (uint8_t)tid;
		cmd.add_immediate_ba_ssn = ssn;
		cmd.rx_ba_window = winsize;
	} else {
		cmd.remove_immediate_ba_tid = (uint8_t)tid;
	}
	cmd.modify_mask = start ? IWM_STA_MODIFY_ADD_BA_TID :
	    IWM_STA_MODIFY_REMOVE_BA_TID;

	status = IWM_ADD_STA_SUCCESS;
	if (isset(sc->sc_ucode_api, IWM_UCODE_TLV_API_STA_TYPE))
		cmdsize = sizeof(cmd);
	else
		cmdsize = sizeof(struct iwm_add_sta_cmd_v7);
	err = iwm_send_cmd_pdu_status(sc, IWM_ADD_STA, cmdsize, &cmd,
	    &status);
	if (!err && (status & IWM_ADD_STA_STATUS_MASK) != IWM_ADD_STA_SUCCESS)
		err = EIO;
	if (err) {
		if (start)
			ieee80211_addba_req_refuse(ic, ni, tid);
		splx(s);
		return err;
	}

	if (sc->sc_mqrx_supported) {
		/* Deaggregation is done in hardware. */
		if (start) {
			if (!(status & IWM_ADD_STA_BAID_VALID_MASK)) {
				ieee80211_addba_req_refuse(ic, ni, tid);
				splx(s);
				return EIO;
			}
			baid = (status & IWM_ADD_STA_BAID_MASK) >>
			    IWM_ADD_STA_BAID_SHIFT;
			if (baid == IWM_RX_REORDER_DATA_INVALID_BAID ||
			    baid >= nitems(sc->sc_rxba_data)) {
				ieee80211_addba_req_refuse(ic, ni, tid);
				splx(s);
				return EIO;
			}
			rxba = &sc->sc_rxba_data[baid];
			if (rxba->baid != IWM_RX_REORDER_DATA_INVALID_BAID) {
				ieee80211_addba_req_refuse(ic, ni, tid);
				splx(s);
				return 0;
			}
			rxba->sta_id = IWM_STATION_ID;
			rxba->tid = tid;
			rxba->baid = baid;
			rxba->timeout = timeout_val;
			getmicrouptime(&rxba->last_rx);
			iwm_init_reorder_buffer(&rxba->reorder_buf, ssn,
			    winsize);
			if (timeout_val != 0) {
				struct ieee80211_rx_ba *ba;
				timeout_add_usec(&rxba->session_timer,
				    timeout_val);
				/* XXX disable net80211's BA timeout handler */
				ba = &ni->ni_rx_ba[tid];
				ba->ba_timeout_val = 0;
			}
		} else {
			int i;
			for (i = 0; i < nitems(sc->sc_rxba_data); i++) {
				rxba = &sc->sc_rxba_data[i];
				if (rxba->baid ==
				    IWM_RX_REORDER_DATA_INVALID_BAID)
					continue;
				if (rxba->tid != tid)
					continue;
				iwm_clear_reorder_buffer(sc, rxba);
				break;
			}
		}
	}

	if (start) {
		sc->sc_rx_ba_sessions++;
		ieee80211_addba_req_accept(ic, ni, tid);
	} else if (sc->sc_rx_ba_sessions > 0)
		sc->sc_rx_ba_sessions--;

	splx(s);
	return 0;
}

void
iwm_mac_ctxt_task(void *arg)
{
	struct iwm_softc *sc = arg;
	struct ieee80211com *ic = &sc->sc_ic;
	struct iwm_node *in = (void *)ic->ic_bss;
	int err, s = splnet();

	if ((sc->sc_flags & IWM_FLAG_SHUTDOWN) ||
	    ic->ic_state != IEEE80211_S_RUN) {
		refcnt_rele_wake(&sc->task_refs);
		splx(s);
		return;
	}

	err = iwm_mac_ctxt_cmd(sc, in, IWM_FW_CTXT_ACTION_MODIFY, 1);
	if (err)
		printf("%s: failed to update MAC\n", DEVNAME(sc));
	
	iwm_unprotect_session(sc, in);

	refcnt_rele_wake(&sc->task_refs);
	splx(s);
}

void
iwm_updateprot(struct ieee80211com *ic)
{
	struct iwm_softc *sc = ic->ic_softc;

	if (ic->ic_state == IEEE80211_S_RUN &&
	    !task_pending(&sc->newstate_task))
		iwm_add_task(sc, systq, &sc->mac_ctxt_task);
}

void
iwm_updateslot(struct ieee80211com *ic)
{
	struct iwm_softc *sc = ic->ic_softc;

	if (ic->ic_state == IEEE80211_S_RUN &&
	    !task_pending(&sc->newstate_task))
		iwm_add_task(sc, systq, &sc->mac_ctxt_task);
}

void
iwm_updateedca(struct ieee80211com *ic)
{
	struct iwm_softc *sc = ic->ic_softc;

	if (ic->ic_state == IEEE80211_S_RUN &&
	    !task_pending(&sc->newstate_task))
		iwm_add_task(sc, systq, &sc->mac_ctxt_task);
}

void
iwm_phy_ctxt_task(void *arg)
{
	struct iwm_softc *sc = arg;
	struct ieee80211com *ic = &sc->sc_ic;
	struct iwm_node *in = (void *)ic->ic_bss;
	struct ieee80211_node *ni = &in->in_ni;
	uint8_t chains, sco, vht_chan_width;
	int err, s = splnet();

	if ((sc->sc_flags & IWM_FLAG_SHUTDOWN) ||
	    ic->ic_state != IEEE80211_S_RUN ||
	    in->in_phyctxt == NULL) {
		refcnt_rele_wake(&sc->task_refs);
		splx(s);
		return;
	}

	chains = iwm_mimo_enabled(sc) ? 2 : 1;
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
		err = iwm_phy_ctxt_update(sc, in->in_phyctxt,
		    in->in_phyctxt->channel, chains, chains, 0, sco,
		    vht_chan_width);
		if (err)
			printf("%s: failed to update PHY\n", DEVNAME(sc));
		iwm_setrates(in, 0);
	}

	refcnt_rele_wake(&sc->task_refs);
	splx(s);
}

void
iwm_updatechan(struct ieee80211com *ic)
{
	struct iwm_softc *sc = ic->ic_softc;

	if (ic->ic_state == IEEE80211_S_RUN &&
	    !task_pending(&sc->newstate_task))
		iwm_add_task(sc, systq, &sc->phy_ctxt_task);
}

void
iwm_updatedtim(struct ieee80211com *ic)
{
	struct iwm_softc *sc = ic->ic_softc;

	if (ic->ic_state == IEEE80211_S_RUN &&
	    !task_pending(&sc->newstate_task))
		iwm_add_task(sc, systq, &sc->mac_ctxt_task);
}

int
iwm_sta_tx_agg(struct iwm_softc *sc, struct ieee80211_node *ni, uint8_t tid,
    uint16_t ssn, uint16_t winsize, int start)
{
	struct iwm_add_sta_cmd cmd;
	struct ieee80211com *ic = &sc->sc_ic;
	struct iwm_node *in = (void *)ni;
	int qid = IWM_FIRST_AGG_TX_QUEUE + tid;
	struct iwm_tx_ring *ring;
	enum ieee80211_edca_ac ac;
	int fifo;
	uint32_t status;
	int err;
	size_t cmdsize;

	/* Ensure we can map this TID to an aggregation queue. */
	if (tid >= IWM_MAX_TID_COUNT || qid > IWM_LAST_AGG_TX_QUEUE)
		return ENOSPC;

	if (start) {
		if ((sc->tx_ba_queue_mask & (1 << qid)) != 0)
			return 0;
	} else {
		if ((sc->tx_ba_queue_mask & (1 << qid)) == 0)
			return 0;
	}

	ring = &sc->txq[qid];
	ac = iwm_tid_to_ac[tid];
	fifo = iwm_ac_to_tx_fifo[ac];

	memset(&cmd, 0, sizeof(cmd));

	cmd.sta_id = IWM_STATION_ID;
	cmd.mac_id_n_color = htole32(IWM_FW_CMD_ID_AND_COLOR(in->in_id,
	    in->in_color));
	cmd.add_modify = IWM_STA_MODE_MODIFY;

	if (start) {
		/* Enable Tx aggregation for this queue. */
		in->tid_disable_ampdu &= ~(1 << tid);
		in->tfd_queue_msk |= (1 << qid);
	} else {
		in->tid_disable_ampdu |= (1 << tid);
		/*
		 * Queue remains enabled in the TFD queue mask
		 * until we leave RUN state.
		 */
		err = iwm_flush_sta(sc, in);
		if (err)
			return err;
	}

	cmd.tfd_queue_msk |= htole32(in->tfd_queue_msk);
	cmd.tid_disable_tx = htole16(in->tid_disable_ampdu);
	cmd.modify_mask = (IWM_STA_MODIFY_QUEUES |
	    IWM_STA_MODIFY_TID_DISABLE_TX);

	if (start && (sc->qenablemsk & (1 << qid)) == 0) {
		if (!iwm_nic_lock(sc)) {
			if (start)
				ieee80211_addba_resp_refuse(ic, ni, tid, 
				    IEEE80211_STATUS_UNSPECIFIED);
			return EBUSY;
		}
		err = iwm_enable_txq(sc, IWM_STATION_ID, qid, fifo, 1, tid,
		    ssn);
		iwm_nic_unlock(sc);
		if (err) {
			printf("%s: could not enable Tx queue %d (error %d)\n",
			    DEVNAME(sc), qid, err);
			if (start)
				ieee80211_addba_resp_refuse(ic, ni, tid, 
				    IEEE80211_STATUS_UNSPECIFIED);
			return err;
		}
		/*
		 * If iwm_enable_txq() employed the SCD hardware bug
		 * workaround we must skip the frame with seqnum SSN.
		 */
		if (ring->cur != IWM_AGG_SSN_TO_TXQ_IDX(ssn)) {
			ssn = (ssn + 1) & 0xfff;
			KASSERT(ring->cur == IWM_AGG_SSN_TO_TXQ_IDX(ssn));
			ieee80211_output_ba_move_window(ic, ni, tid, ssn);
			ni->ni_qos_txseqs[tid] = ssn;
		}
	}

	if (isset(sc->sc_ucode_api, IWM_UCODE_TLV_API_STA_TYPE))
		cmdsize = sizeof(cmd);
	else
		cmdsize = sizeof(struct iwm_add_sta_cmd_v7);

	status = 0;
	err = iwm_send_cmd_pdu_status(sc, IWM_ADD_STA, cmdsize, &cmd, &status);
	if (!err && (status & IWM_ADD_STA_STATUS_MASK) != IWM_ADD_STA_SUCCESS)
		err = EIO;
	if (err) {
		printf("%s: could not update sta (error %d)\n",
		    DEVNAME(sc), err);
		if (start)
			ieee80211_addba_resp_refuse(ic, ni, tid, 
			    IEEE80211_STATUS_UNSPECIFIED);
		return err;
	}

	if (start) {
		sc->tx_ba_queue_mask |= (1 << qid);
		ieee80211_addba_resp_accept(ic, ni, tid);
	} else {
		sc->tx_ba_queue_mask &= ~(1 << qid);

		/*
		 * Clear pending frames but keep the queue enabled.
		 * Firmware panics if we disable the queue here.
		 */
		iwm_txq_advance(sc, ring, ring->cur);
		iwm_clear_oactive(sc, ring);
	}

	return 0;
}

void
iwm_ba_task(void *arg)
{
	struct iwm_softc *sc = arg;
	struct ieee80211com *ic = &sc->sc_ic;
	struct ieee80211_node *ni = ic->ic_bss;
	int s = splnet();
	int tid, err = 0;

	if ((sc->sc_flags & IWM_FLAG_SHUTDOWN) ||
	    ic->ic_state != IEEE80211_S_RUN) {
		refcnt_rele_wake(&sc->task_refs);
		splx(s);
		return;
	}

	for (tid = 0; tid < IWM_MAX_TID_COUNT && !err; tid++) {
		if (sc->sc_flags & IWM_FLAG_SHUTDOWN)
			break;
		if (sc->ba_rx.start_tidmask & (1 << tid)) {
			struct ieee80211_rx_ba *ba = &ni->ni_rx_ba[tid];
			err = iwm_sta_rx_agg(sc, ni, tid, ba->ba_winstart,
			    ba->ba_winsize, ba->ba_timeout_val, 1);
			sc->ba_rx.start_tidmask &= ~(1 << tid);
		} else if (sc->ba_rx.stop_tidmask & (1 << tid)) {
			err = iwm_sta_rx_agg(sc, ni, tid, 0, 0, 0, 0);
			sc->ba_rx.stop_tidmask &= ~(1 << tid);
		}
	}

	for (tid = 0; tid < IWM_MAX_TID_COUNT && !err; tid++) {
		if (sc->sc_flags & IWM_FLAG_SHUTDOWN)
			break;
		if (sc->ba_tx.start_tidmask & (1 << tid)) {
			struct ieee80211_tx_ba *ba = &ni->ni_tx_ba[tid];
			err = iwm_sta_tx_agg(sc, ni, tid, ba->ba_winstart,
			    ba->ba_winsize, 1);
			sc->ba_tx.start_tidmask &= ~(1 << tid);
		} else if (sc->ba_tx.stop_tidmask & (1 << tid)) {
			err = iwm_sta_tx_agg(sc, ni, tid, 0, 0, 0);
			sc->ba_tx.stop_tidmask &= ~(1 << tid);
		}
	}

	/*
	 * We "recover" from failure to start or stop a BA session
	 * by resetting the device.
	 */
	if (err && (sc->sc_flags & IWM_FLAG_SHUTDOWN) == 0)
		task_add(systq, &sc->init_task);

	refcnt_rele_wake(&sc->task_refs);
	splx(s);
}

/*
 * This function is called by upper layer when an ADDBA request is received
 * from another STA and before the ADDBA response is sent.
 */
int
iwm_ampdu_rx_start(struct ieee80211com *ic, struct ieee80211_node *ni,
    uint8_t tid)
{
	struct iwm_softc *sc = IC2IFP(ic)->if_softc;

	if (sc->sc_rx_ba_sessions >= IWM_MAX_RX_BA_SESSIONS ||
	    tid > IWM_MAX_TID_COUNT)
		return ENOSPC;

	if (sc->ba_rx.start_tidmask & (1 << tid))
		return EBUSY;

	sc->ba_rx.start_tidmask |= (1 << tid);
	iwm_add_task(sc, systq, &sc->ba_task);

	return EBUSY;
}

/*
 * This function is called by upper layer on teardown of an HT-immediate
 * Block Ack agreement (eg. upon receipt of a DELBA frame).
 */
void
iwm_ampdu_rx_stop(struct ieee80211com *ic, struct ieee80211_node *ni,
    uint8_t tid)
{
	struct iwm_softc *sc = IC2IFP(ic)->if_softc;

	if (tid > IWM_MAX_TID_COUNT || sc->ba_rx.stop_tidmask & (1 << tid))
		return;

	sc->ba_rx.stop_tidmask |= (1 << tid);
	iwm_add_task(sc, systq, &sc->ba_task);
}

int
iwm_ampdu_tx_start(struct ieee80211com *ic, struct ieee80211_node *ni,
    uint8_t tid)
{
	struct iwm_softc *sc = IC2IFP(ic)->if_softc;
	struct ieee80211_tx_ba *ba = &ni->ni_tx_ba[tid];
	int qid = IWM_FIRST_AGG_TX_QUEUE + tid;

	/* We only implement Tx aggregation with DQA-capable firmware. */
	if (!isset(sc->sc_enabled_capa, IWM_UCODE_TLV_CAPA_DQA_SUPPORT))
		return ENOTSUP;

	/* Ensure we can map this TID to an aggregation queue. */
	if (tid >= IWM_MAX_TID_COUNT)
		return EINVAL;

	/* We only support a fixed Tx aggregation window size, for now. */
	if (ba->ba_winsize != IWM_FRAME_LIMIT)
		return ENOTSUP;

	/* Is firmware already using Tx aggregation on this queue? */
	if ((sc->tx_ba_queue_mask & (1 << qid)) != 0)
		return ENOSPC;

	/* Are we already processing an ADDBA request? */
	if (sc->ba_tx.start_tidmask & (1 << tid))
		return EBUSY;

	sc->ba_tx.start_tidmask |= (1 << tid);
	iwm_add_task(sc, systq, &sc->ba_task);

	return EBUSY;
}

void
iwm_ampdu_tx_stop(struct ieee80211com *ic, struct ieee80211_node *ni,
    uint8_t tid)
{
	struct iwm_softc *sc = IC2IFP(ic)->if_softc;
	int qid = IWM_FIRST_AGG_TX_QUEUE + tid;

	if (tid > IWM_MAX_TID_COUNT || sc->ba_tx.stop_tidmask & (1 << tid))
		return;

	/* Is firmware currently using Tx aggregation on this queue? */
	if ((sc->tx_ba_queue_mask & (1 << qid)) == 0)
		return;

	sc->ba_tx.stop_tidmask |= (1 << tid);
	iwm_add_task(sc, systq, &sc->ba_task);
}

void
iwm_set_hw_address_8000(struct iwm_softc *sc, struct iwm_nvm_data *data,
    const uint16_t *mac_override, const uint16_t *nvm_hw)
{
	const uint8_t *hw_addr;

	if (mac_override) {
		static const uint8_t reserved_mac[] = {
			0x02, 0xcc, 0xaa, 0xff, 0xee, 0x00
		};

		hw_addr = (const uint8_t *)(mac_override +
				 IWM_MAC_ADDRESS_OVERRIDE_8000);

		/*
		 * Store the MAC address from MAO section.
		 * No byte swapping is required in MAO section
		 */
		memcpy(data->hw_addr, hw_addr, ETHER_ADDR_LEN);

		/*
		 * Force the use of the OTP MAC address in case of reserved MAC
		 * address in the NVM, or if address is given but invalid.
		 */
		if (memcmp(reserved_mac, hw_addr, ETHER_ADDR_LEN) != 0 &&
		    (memcmp(etherbroadcastaddr, data->hw_addr,
		    sizeof(etherbroadcastaddr)) != 0) &&
		    (memcmp(etheranyaddr, data->hw_addr,
		    sizeof(etheranyaddr)) != 0) &&
		    !ETHER_IS_MULTICAST(data->hw_addr))
			return;
	}

	if (nvm_hw) {
		/* Read the mac address from WFMP registers. */
		uint32_t mac_addr0, mac_addr1;

		if (!iwm_nic_lock(sc))
			goto out;
		mac_addr0 = htole32(iwm_read_prph(sc, IWM_WFMP_MAC_ADDR_0));
		mac_addr1 = htole32(iwm_read_prph(sc, IWM_WFMP_MAC_ADDR_1));
		iwm_nic_unlock(sc);

		hw_addr = (const uint8_t *)&mac_addr0;
		data->hw_addr[0] = hw_addr[3];
		data->hw_addr[1] = hw_addr[2];
		data->hw_addr[2] = hw_addr[1];
		data->hw_addr[3] = hw_addr[0];

		hw_addr = (const uint8_t *)&mac_addr1;
		data->hw_addr[4] = hw_addr[1];
		data->hw_addr[5] = hw_addr[0];

		return;
	}
out:
	printf("%s: mac address not found\n", DEVNAME(sc));
	memset(data->hw_addr, 0, sizeof(data->hw_addr));
}

int
iwm_parse_nvm_data(struct iwm_softc *sc, const uint16_t *nvm_hw,
    const uint16_t *nvm_sw, const uint16_t *nvm_calib,
    const uint16_t *mac_override, const uint16_t *phy_sku,
    const uint16_t *regulatory, int n_regulatory)
{
	struct iwm_nvm_data *data = &sc->sc_nvm;
	uint8_t hw_addr[ETHER_ADDR_LEN];
	uint32_t sku;
	uint16_t lar_config;

	data->nvm_version = le16_to_cpup(nvm_sw + IWM_NVM_VERSION);

	if (sc->sc_device_family == IWM_DEVICE_FAMILY_7000) {
		uint16_t radio_cfg = le16_to_cpup(nvm_sw + IWM_RADIO_CFG);
		data->radio_cfg_type = IWM_NVM_RF_CFG_TYPE_MSK(radio_cfg);
		data->radio_cfg_step = IWM_NVM_RF_CFG_STEP_MSK(radio_cfg);
		data->radio_cfg_dash = IWM_NVM_RF_CFG_DASH_MSK(radio_cfg);
		data->radio_cfg_pnum = IWM_NVM_RF_CFG_PNUM_MSK(radio_cfg);

		sku = le16_to_cpup(nvm_sw + IWM_SKU);
	} else {
		uint32_t radio_cfg =
		    le32_to_cpup((uint32_t *)(phy_sku + IWM_RADIO_CFG_8000));
		data->radio_cfg_type = IWM_NVM_RF_CFG_TYPE_MSK_8000(radio_cfg);
		data->radio_cfg_step = IWM_NVM_RF_CFG_STEP_MSK_8000(radio_cfg);
		data->radio_cfg_dash = IWM_NVM_RF_CFG_DASH_MSK_8000(radio_cfg);
		data->radio_cfg_pnum = IWM_NVM_RF_CFG_PNUM_MSK_8000(radio_cfg);
		data->valid_tx_ant = IWM_NVM_RF_CFG_TX_ANT_MSK_8000(radio_cfg);
		data->valid_rx_ant = IWM_NVM_RF_CFG_RX_ANT_MSK_8000(radio_cfg);

		sku = le32_to_cpup((uint32_t *)(phy_sku + IWM_SKU_8000));
	}

	data->sku_cap_band_24GHz_enable = sku & IWM_NVM_SKU_CAP_BAND_24GHZ;
	data->sku_cap_band_52GHz_enable = sku & IWM_NVM_SKU_CAP_BAND_52GHZ;
	data->sku_cap_11n_enable = sku & IWM_NVM_SKU_CAP_11N_ENABLE;
	data->sku_cap_11ac_enable = sku & IWM_NVM_SKU_CAP_11AC_ENABLE;
	data->sku_cap_mimo_disable = sku & IWM_NVM_SKU_CAP_MIMO_DISABLE;

	if (sc->sc_device_family >= IWM_DEVICE_FAMILY_8000) {
		uint16_t lar_offset = data->nvm_version < 0xE39 ?
				       IWM_NVM_LAR_OFFSET_8000_OLD :
				       IWM_NVM_LAR_OFFSET_8000;

		lar_config = le16_to_cpup(regulatory + lar_offset);
		data->lar_enabled = !!(lar_config &
				       IWM_NVM_LAR_ENABLED_8000);
		data->n_hw_addrs = le16_to_cpup(nvm_sw + IWM_N_HW_ADDRS_8000);
	} else
		data->n_hw_addrs = le16_to_cpup(nvm_sw + IWM_N_HW_ADDRS);


	/* The byte order is little endian 16 bit, meaning 214365 */
	if (sc->sc_device_family == IWM_DEVICE_FAMILY_7000) {
		memcpy(hw_addr, nvm_hw + IWM_HW_ADDR, ETHER_ADDR_LEN);
		data->hw_addr[0] = hw_addr[1];
		data->hw_addr[1] = hw_addr[0];
		data->hw_addr[2] = hw_addr[3];
		data->hw_addr[3] = hw_addr[2];
		data->hw_addr[4] = hw_addr[5];
		data->hw_addr[5] = hw_addr[4];
	} else
		iwm_set_hw_address_8000(sc, data, mac_override, nvm_hw);

	if (sc->sc_device_family == IWM_DEVICE_FAMILY_7000) {
		if (sc->nvm_type == IWM_NVM_SDP) {
			iwm_init_channel_map(sc, regulatory, iwm_nvm_channels,
			    MIN(n_regulatory, nitems(iwm_nvm_channels)));
		} else {
			iwm_init_channel_map(sc, &nvm_sw[IWM_NVM_CHANNELS],
			    iwm_nvm_channels, nitems(iwm_nvm_channels));
		}
	} else
		iwm_init_channel_map(sc, &regulatory[IWM_NVM_CHANNELS_8000],
		    iwm_nvm_channels_8000,
		    MIN(n_regulatory, nitems(iwm_nvm_channels_8000)));

	data->calib_version = 255;   /* TODO:
					this value will prevent some checks from
					failing, we need to check if this
					field is still needed, and if it does,
					where is it in the NVM */

	return 0;
}

int
iwm_parse_nvm_sections(struct iwm_softc *sc, struct iwm_nvm_section *sections)
{
	const uint16_t *hw, *sw, *calib, *mac_override = NULL, *phy_sku = NULL;
	const uint16_t *regulatory = NULL;
	int n_regulatory = 0;

	/* Checking for required sections */
	if (sc->sc_device_family == IWM_DEVICE_FAMILY_7000) {
		if (!sections[IWM_NVM_SECTION_TYPE_SW].data ||
		    !sections[IWM_NVM_SECTION_TYPE_HW].data) {
			return ENOENT;
		}

		hw = (const uint16_t *) sections[IWM_NVM_SECTION_TYPE_HW].data;

		if (sc->nvm_type == IWM_NVM_SDP) {
			if (!sections[IWM_NVM_SECTION_TYPE_REGULATORY_SDP].data)
				return ENOENT;
			regulatory = (const uint16_t *)
			    sections[IWM_NVM_SECTION_TYPE_REGULATORY_SDP].data;
			n_regulatory =
			    sections[IWM_NVM_SECTION_TYPE_REGULATORY_SDP].length;
		}
	} else if (sc->sc_device_family >= IWM_DEVICE_FAMILY_8000) {
		/* SW and REGULATORY sections are mandatory */
		if (!sections[IWM_NVM_SECTION_TYPE_SW].data ||
		    !sections[IWM_NVM_SECTION_TYPE_REGULATORY].data) {
			return ENOENT;
		}
		/* MAC_OVERRIDE or at least HW section must exist */
		if (!sections[IWM_NVM_SECTION_TYPE_HW_8000].data &&
		    !sections[IWM_NVM_SECTION_TYPE_MAC_OVERRIDE].data) {
			return ENOENT;
		}

		/* PHY_SKU section is mandatory in B0 */
		if (!sections[IWM_NVM_SECTION_TYPE_PHY_SKU].data) {
			return ENOENT;
		}

		regulatory = (const uint16_t *)
		    sections[IWM_NVM_SECTION_TYPE_REGULATORY].data;
		n_regulatory = sections[IWM_NVM_SECTION_TYPE_REGULATORY].length;
		hw = (const uint16_t *)
		    sections[IWM_NVM_SECTION_TYPE_HW_8000].data;
		mac_override =
			(const uint16_t *)
			sections[IWM_NVM_SECTION_TYPE_MAC_OVERRIDE].data;
		phy_sku = (const uint16_t *)
		    sections[IWM_NVM_SECTION_TYPE_PHY_SKU].data;
	} else {
		panic("unknown device family %d", sc->sc_device_family);
	}

	sw = (const uint16_t *)sections[IWM_NVM_SECTION_TYPE_SW].data;
	calib = (const uint16_t *)
	    sections[IWM_NVM_SECTION_TYPE_CALIBRATION].data;

	/* XXX should pass in the length of every section */
	return iwm_parse_nvm_data(sc, hw, sw, calib, mac_override,
	    phy_sku, regulatory, n_regulatory);
}

int
iwm_nvm_init(struct iwm_softc *sc)
{
	struct iwm_nvm_section nvm_sections[IWM_NVM_NUM_OF_SECTIONS];
	int i, section, err;
	uint16_t len;
	uint8_t *buf;
	const size_t bufsz = sc->sc_nvm_max_section_size;

	memset(nvm_sections, 0, sizeof(nvm_sections));

	buf = malloc(bufsz, M_DEVBUF, M_WAIT);
	if (buf == NULL)
		return ENOMEM;

	for (i = 0; i < nitems(iwm_nvm_to_read); i++) {
		section = iwm_nvm_to_read[i];
		KASSERT(section <= nitems(nvm_sections));

		err = iwm_nvm_read_section(sc, section, buf, &len, bufsz);
		if (err) {
			err = 0;
			continue;
		}
		nvm_sections[section].data = malloc(len, M_DEVBUF, M_WAIT);
		if (nvm_sections[section].data == NULL) {
			err = ENOMEM;
			break;
		}
		memcpy(nvm_sections[section].data, buf, len);
		nvm_sections[section].length = len;
	}
	free(buf, M_DEVBUF, bufsz);
	if (err == 0)
		err = iwm_parse_nvm_sections(sc, nvm_sections);

	for (i = 0; i < IWM_NVM_NUM_OF_SECTIONS; i++) {
		if (nvm_sections[i].data != NULL)
			free(nvm_sections[i].data, M_DEVBUF,
			    nvm_sections[i].length);
	}

	return err;
}

int
iwm_firmware_load_sect(struct iwm_softc *sc, uint32_t dst_addr,
    const uint8_t *section, uint32_t byte_cnt)
{
	int err = EINVAL;
	uint32_t chunk_sz, offset;

	chunk_sz = MIN(IWM_FH_MEM_TB_MAX_LENGTH, byte_cnt);

	for (offset = 0; offset < byte_cnt; offset += chunk_sz) {
		uint32_t addr, len;
		const uint8_t *data;

		addr = dst_addr + offset;
		len = MIN(chunk_sz, byte_cnt - offset);
		data = section + offset;

		err = iwm_firmware_load_chunk(sc, addr, data, len);
		if (err)
			break;
	}

	return err;
}

int
iwm_firmware_load_chunk(struct iwm_softc *sc, uint32_t dst_addr,
    const uint8_t *chunk, uint32_t byte_cnt)
{
	struct iwm_dma_info *dma = &sc->fw_dma;
	int err;

	/* Copy firmware chunk into pre-allocated DMA-safe memory. */
	memcpy(dma->vaddr, chunk, byte_cnt);
	bus_dmamap_sync(sc->sc_dmat,
	    dma->map, 0, byte_cnt, BUS_DMASYNC_PREWRITE);

	if (dst_addr >= IWM_FW_MEM_EXTENDED_START &&
	    dst_addr <= IWM_FW_MEM_EXTENDED_END) {
		err = iwm_set_bits_prph(sc, IWM_LMPM_CHICK,
		    IWM_LMPM_CHICK_EXTENDED_ADDR_SPACE);
		if (err)
			return err;
	}

	sc->sc_fw_chunk_done = 0;

	if (!iwm_nic_lock(sc))
		return EBUSY;

	IWM_WRITE(sc, IWM_FH_TCSR_CHNL_TX_CONFIG_REG(IWM_FH_SRVC_CHNL),
	    IWM_FH_TCSR_TX_CONFIG_REG_VAL_DMA_CHNL_PAUSE);
	IWM_WRITE(sc, IWM_FH_SRVC_CHNL_SRAM_ADDR_REG(IWM_FH_SRVC_CHNL),
	    dst_addr);
	IWM_WRITE(sc, IWM_FH_TFDIB_CTRL0_REG(IWM_FH_SRVC_CHNL),
	    dma->paddr & IWM_FH_MEM_TFDIB_DRAM_ADDR_LSB_MSK);
	IWM_WRITE(sc, IWM_FH_TFDIB_CTRL1_REG(IWM_FH_SRVC_CHNL),
	    (iwm_get_dma_hi_addr(dma->paddr)
	      << IWM_FH_MEM_TFDIB_REG1_ADDR_BITSHIFT) | byte_cnt);
	IWM_WRITE(sc, IWM_FH_TCSR_CHNL_TX_BUF_STS_REG(IWM_FH_SRVC_CHNL),
	    1 << IWM_FH_TCSR_CHNL_TX_BUF_STS_REG_POS_TB_NUM |
	    1 << IWM_FH_TCSR_CHNL_TX_BUF_STS_REG_POS_TB_IDX |
	    IWM_FH_TCSR_CHNL_TX_BUF_STS_REG_VAL_TFDB_VALID);
	IWM_WRITE(sc, IWM_FH_TCSR_CHNL_TX_CONFIG_REG(IWM_FH_SRVC_CHNL),
	    IWM_FH_TCSR_TX_CONFIG_REG_VAL_DMA_CHNL_ENABLE    |
	    IWM_FH_TCSR_TX_CONFIG_REG_VAL_DMA_CREDIT_DISABLE |
	    IWM_FH_TCSR_TX_CONFIG_REG_VAL_CIRQ_HOST_ENDTFD);

	iwm_nic_unlock(sc);

	/* Wait for this segment to load. */
	err = 0;
	while (!sc->sc_fw_chunk_done) {
		err = tsleep_nsec(&sc->sc_fw, 0, "iwmfw", SEC_TO_NSEC(1));
		if (err)
			break;
	}

	if (!sc->sc_fw_chunk_done)
		printf("%s: fw chunk addr 0x%x len %d failed to load\n",
		    DEVNAME(sc), dst_addr, byte_cnt);

	if (dst_addr >= IWM_FW_MEM_EXTENDED_START &&
	    dst_addr <= IWM_FW_MEM_EXTENDED_END) {
		int err2 = iwm_clear_bits_prph(sc, IWM_LMPM_CHICK,
		    IWM_LMPM_CHICK_EXTENDED_ADDR_SPACE);
		if (!err)
			err = err2;
	}

	return err;
}

int
iwm_load_firmware_7000(struct iwm_softc *sc, enum iwm_ucode_type ucode_type)
{
	struct iwm_fw_sects *fws;
	int err, i;
	void *data;
	uint32_t dlen;
	uint32_t offset;

	fws = &sc->sc_fw.fw_sects[ucode_type];
	for (i = 0; i < fws->fw_count; i++) {
		data = fws->fw_sect[i].fws_data;
		dlen = fws->fw_sect[i].fws_len;
		offset = fws->fw_sect[i].fws_devoff;
		if (dlen > sc->sc_fwdmasegsz) {
			err = EFBIG;
		} else
			err = iwm_firmware_load_sect(sc, offset, data, dlen);
		if (err) {
			printf("%s: could not load firmware chunk %u of %u\n",
			    DEVNAME(sc), i, fws->fw_count);
			return err;
		}
	}

	iwm_enable_interrupts(sc);

	IWM_WRITE(sc, IWM_CSR_RESET, 0);

	return 0;
}

int
iwm_load_cpu_sections_8000(struct iwm_softc *sc, struct iwm_fw_sects *fws,
    int cpu, int *first_ucode_section)
{
	int shift_param;
	int i, err = 0, sec_num = 0x1;
	uint32_t val, last_read_idx = 0;
	void *data;
	uint32_t dlen;
	uint32_t offset;

	if (cpu == 1) {
		shift_param = 0;
		*first_ucode_section = 0;
	} else {
		shift_param = 16;
		(*first_ucode_section)++;
	}

	for (i = *first_ucode_section; i < IWM_UCODE_SECT_MAX; i++) {
		last_read_idx = i;
		data = fws->fw_sect[i].fws_data;
		dlen = fws->fw_sect[i].fws_len;
		offset = fws->fw_sect[i].fws_devoff;

		/*
		 * CPU1_CPU2_SEPARATOR_SECTION delimiter - separate between
		 * CPU1 to CPU2.
		 * PAGING_SEPARATOR_SECTION delimiter - separate between
		 * CPU2 non paged to CPU2 paging sec.
		 */
		if (!data || offset == IWM_CPU1_CPU2_SEPARATOR_SECTION ||
		    offset == IWM_PAGING_SEPARATOR_SECTION)
			break;

		if (dlen > sc->sc_fwdmasegsz) {
			err = EFBIG;
		} else
			err = iwm_firmware_load_sect(sc, offset, data, dlen);
		if (err) {
			printf("%s: could not load firmware chunk %d "
			    "(error %d)\n", DEVNAME(sc), i, err);
			return err;
		}

		/* Notify the ucode of the loaded section number and status */
		if (iwm_nic_lock(sc)) {
			val = IWM_READ(sc, IWM_FH_UCODE_LOAD_STATUS);
			val = val | (sec_num << shift_param);
			IWM_WRITE(sc, IWM_FH_UCODE_LOAD_STATUS, val);
			sec_num = (sec_num << 1) | 0x1;
			iwm_nic_unlock(sc);
		} else {
			err = EBUSY;
			printf("%s: could not load firmware chunk %d "
			    "(error %d)\n", DEVNAME(sc), i, err);
			return err;
		}
	}

	*first_ucode_section = last_read_idx;

	if (iwm_nic_lock(sc)) {
		if (cpu == 1)
			IWM_WRITE(sc, IWM_FH_UCODE_LOAD_STATUS, 0xFFFF);
		else
			IWM_WRITE(sc, IWM_FH_UCODE_LOAD_STATUS, 0xFFFFFFFF);
		iwm_nic_unlock(sc);
	} else {
		err = EBUSY;
		printf("%s: could not finalize firmware loading (error %d)\n",
		    DEVNAME(sc), err);
		return err;
	}

	return 0;
}

int
iwm_load_firmware_8000(struct iwm_softc *sc, enum iwm_ucode_type ucode_type)
{
	struct iwm_fw_sects *fws;
	int err = 0;
	int first_ucode_section;

	fws = &sc->sc_fw.fw_sects[ucode_type];

	/* configure the ucode to be ready to get the secured image */
	/* release CPU reset */
	if (iwm_nic_lock(sc)) {
		iwm_write_prph(sc, IWM_RELEASE_CPU_RESET,
		    IWM_RELEASE_CPU_RESET_BIT);
		iwm_nic_unlock(sc);
	}

	/* load to FW the binary Secured sections of CPU1 */
	err = iwm_load_cpu_sections_8000(sc, fws, 1, &first_ucode_section);
	if (err)
		return err;

	/* load to FW the binary sections of CPU2 */
	err = iwm_load_cpu_sections_8000(sc, fws, 2, &first_ucode_section);
	if (err)
		return err;

	iwm_enable_interrupts(sc);
	return 0;
}

int
iwm_load_firmware(struct iwm_softc *sc, enum iwm_ucode_type ucode_type)
{
	int err;

	splassert(IPL_NET);

	sc->sc_uc.uc_intr = 0;
	sc->sc_uc.uc_ok = 0;

	if (sc->sc_device_family >= IWM_DEVICE_FAMILY_8000)
		err = iwm_load_firmware_8000(sc, ucode_type);
	else
		err = iwm_load_firmware_7000(sc, ucode_type);

	if (err)
		return err;

	/* wait for the firmware to load */
	err = tsleep_nsec(&sc->sc_uc, 0, "iwmuc", SEC_TO_NSEC(1));
	if (err || !sc->sc_uc.uc_ok)
		printf("%s: could not load firmware\n", DEVNAME(sc));

	return err;
}

int
iwm_start_fw(struct iwm_softc *sc, enum iwm_ucode_type ucode_type)
{
	int err;

	IWM_WRITE(sc, IWM_CSR_INT, ~0);

	err = iwm_nic_init(sc);
	if (err) {
		printf("%s: unable to init nic\n", DEVNAME(sc));
		return err;
	}

	/* make sure rfkill handshake bits are cleared */
	IWM_WRITE(sc, IWM_CSR_UCODE_DRV_GP1_CLR, IWM_CSR_UCODE_SW_BIT_RFKILL);
	IWM_WRITE(sc, IWM_CSR_UCODE_DRV_GP1_CLR,
	    IWM_CSR_UCODE_DRV_GP1_BIT_CMD_BLOCKED);

	/* clear (again), then enable firmware load interrupt */
	IWM_WRITE(sc, IWM_CSR_INT, ~0);
	iwm_enable_fwload_interrupt(sc);

	/* really make sure rfkill handshake bits are cleared */
	/* maybe we should write a few times more?  just to make sure */
	IWM_WRITE(sc, IWM_CSR_UCODE_DRV_GP1_CLR, IWM_CSR_UCODE_SW_BIT_RFKILL);
	IWM_WRITE(sc, IWM_CSR_UCODE_DRV_GP1_CLR, IWM_CSR_UCODE_SW_BIT_RFKILL);

	return iwm_load_firmware(sc, ucode_type);
}

int
iwm_send_tx_ant_cfg(struct iwm_softc *sc, uint8_t valid_tx_ant)
{
	struct iwm_tx_ant_cfg_cmd tx_ant_cmd = {
		.valid = htole32(valid_tx_ant),
	};

	return iwm_send_cmd_pdu(sc, IWM_TX_ANT_CONFIGURATION_CMD,
	    0, sizeof(tx_ant_cmd), &tx_ant_cmd);
}

int
iwm_send_phy_cfg_cmd(struct iwm_softc *sc)
{
	struct iwm_phy_cfg_cmd phy_cfg_cmd;
	enum iwm_ucode_type ucode_type = sc->sc_uc_current;

	phy_cfg_cmd.phy_cfg = htole32(sc->sc_fw_phy_config |
	    sc->sc_extra_phy_config);
	phy_cfg_cmd.calib_control.event_trigger =
	    sc->sc_default_calib[ucode_type].event_trigger;
	phy_cfg_cmd.calib_control.flow_trigger =
	    sc->sc_default_calib[ucode_type].flow_trigger;

	return iwm_send_cmd_pdu(sc, IWM_PHY_CONFIGURATION_CMD, 0,
	    sizeof(phy_cfg_cmd), &phy_cfg_cmd);
}

int
iwm_send_dqa_cmd(struct iwm_softc *sc)
{
	struct iwm_dqa_enable_cmd dqa_cmd = {
		.cmd_queue = htole32(IWM_DQA_CMD_QUEUE),
	};
	uint32_t cmd_id;

	cmd_id = iwm_cmd_id(IWM_DQA_ENABLE_CMD, IWM_DATA_PATH_GROUP, 0);
	return iwm_send_cmd_pdu(sc, cmd_id, 0, sizeof(dqa_cmd), &dqa_cmd);
}

int
iwm_load_ucode_wait_alive(struct iwm_softc *sc,
	enum iwm_ucode_type ucode_type)
{
	enum iwm_ucode_type old_type = sc->sc_uc_current;
	struct iwm_fw_sects *fw = &sc->sc_fw.fw_sects[ucode_type];
	int err;

	err = iwm_read_firmware(sc);
	if (err)
		return err;

	if (isset(sc->sc_enabled_capa, IWM_UCODE_TLV_CAPA_DQA_SUPPORT))
		sc->cmdqid = IWM_DQA_CMD_QUEUE;
	else
		sc->cmdqid = IWM_CMD_QUEUE;

	sc->sc_uc_current = ucode_type;
	err = iwm_start_fw(sc, ucode_type);
	if (err) {
		sc->sc_uc_current = old_type;
		return err;
	}

	err = iwm_post_alive(sc);
	if (err)
		return err;

	/*
	 * configure and operate fw paging mechanism.
	 * driver configures the paging flow only once, CPU2 paging image
	 * included in the IWM_UCODE_INIT image.
	 */
	if (fw->paging_mem_size) {
		err = iwm_save_fw_paging(sc, fw);
		if (err) {
			printf("%s: failed to save the FW paging image\n",
			    DEVNAME(sc));
			return err;
		}

		err = iwm_send_paging_cmd(sc, fw);
		if (err) {
			printf("%s: failed to send the paging cmd\n",
			    DEVNAME(sc));
			iwm_free_fw_paging(sc);
			return err;
		}
	}

	return 0;
}

int
iwm_run_init_mvm_ucode(struct iwm_softc *sc, int justnvm)
{
	const int wait_flags = (IWM_INIT_COMPLETE | IWM_CALIB_COMPLETE);
	int err, s;

	if ((sc->sc_flags & IWM_FLAG_RFKILL) && !justnvm) {
		printf("%s: radio is disabled by hardware switch\n",
		    DEVNAME(sc));
		return EPERM;
	}

	s = splnet();
	sc->sc_init_complete = 0;
	err = iwm_load_ucode_wait_alive(sc, IWM_UCODE_TYPE_INIT);
	if (err) {
		printf("%s: failed to load init firmware\n", DEVNAME(sc));
		splx(s);
		return err;
	}

	if (sc->sc_device_family < IWM_DEVICE_FAMILY_8000) {
		err = iwm_send_bt_init_conf(sc);
		if (err) {
			printf("%s: could not init bt coex (error %d)\n",
			    DEVNAME(sc), err);
			splx(s);
			return err;
		}
	}

	if (justnvm) {
		err = iwm_nvm_init(sc);
		if (err) {
			printf("%s: failed to read nvm\n", DEVNAME(sc));
			splx(s);
			return err;
		}

		if (IEEE80211_ADDR_EQ(etheranyaddr, sc->sc_ic.ic_myaddr))
			IEEE80211_ADDR_COPY(sc->sc_ic.ic_myaddr,
			    sc->sc_nvm.hw_addr);

		splx(s);
		return 0;
	}

	err = iwm_sf_config(sc, IWM_SF_INIT_OFF);
	if (err) {
		splx(s);
		return err;
	}

	/* Send TX valid antennas before triggering calibrations */
	err = iwm_send_tx_ant_cfg(sc, iwm_fw_valid_tx_ant(sc));
	if (err) {
		splx(s);
		return err;
	}

	/*
	 * Send phy configurations command to init uCode
	 * to start the 16.0 uCode init image internal calibrations.
	 */
	err = iwm_send_phy_cfg_cmd(sc);
	if (err) {
		splx(s);
		return err;
	}

	/*
	 * Nothing to do but wait for the init complete and phy DB
	 * notifications from the firmware.
	 */
	while ((sc->sc_init_complete & wait_flags) != wait_flags) {
		err = tsleep_nsec(&sc->sc_init_complete, 0, "iwminit",
		    SEC_TO_NSEC(2));
		if (err)
			break;
	}

	splx(s);
	return err;
}

int
iwm_config_ltr(struct iwm_softc *sc)
{
	struct iwm_ltr_config_cmd cmd = {
		.flags = htole32(IWM_LTR_CFG_FLAG_FEATURE_ENABLE),
	};

	if (!sc->sc_ltr_enabled)
		return 0;

	return iwm_send_cmd_pdu(sc, IWM_LTR_CONFIG, 0, sizeof(cmd), &cmd);
}

int
iwm_rx_addbuf(struct iwm_softc *sc, int size, int idx)
{
	struct iwm_rx_ring *ring = &sc->rxq;
	struct iwm_rx_data *data = &ring->data[idx];
	struct mbuf *m;
	int err;
	int fatal = 0;

	m = m_gethdr(M_DONTWAIT, MT_DATA);
	if (m == NULL)
		return ENOBUFS;

	if (size <= MCLBYTES) {
		MCLGET(m, M_DONTWAIT);
	} else {
		MCLGETL(m, M_DONTWAIT, IWM_RBUF_SIZE);
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
			panic("iwm: could not load RX mbuf");
		m_freem(m);
		return err;
	}
	data->m = m;
	bus_dmamap_sync(sc->sc_dmat, data->map, 0, size, BUS_DMASYNC_PREREAD);

	/* Update RX descriptor. */
	if (sc->sc_mqrx_supported) {
		((uint64_t *)ring->desc)[idx] =
		    htole64(data->map->dm_segs[0].ds_addr);
		bus_dmamap_sync(sc->sc_dmat, ring->free_desc_dma.map,
		    idx * sizeof(uint64_t), sizeof(uint64_t),
		    BUS_DMASYNC_PREWRITE);
	} else {
		((uint32_t *)ring->desc)[idx] =
		    htole32(data->map->dm_segs[0].ds_addr >> 8);
		bus_dmamap_sync(sc->sc_dmat, ring->free_desc_dma.map,
		    idx * sizeof(uint32_t), sizeof(uint32_t),
		    BUS_DMASYNC_PREWRITE);
	}

	return 0;
}

/*
 * RSSI values are reported by the FW as positive values - need to negate
 * to obtain their dBM.  Account for missing antennas by replacing 0
 * values by -256dBm: practically 0 power and a non-feasible 8 bit value.
 */
int
iwm_get_signal_strength(struct iwm_softc *sc, struct iwm_rx_phy_info *phy_info)
{
	int energy_a, energy_b, energy_c, max_energy;
	uint32_t val;

	val = le32toh(phy_info->non_cfg_phy[IWM_RX_INFO_ENERGY_ANT_ABC_IDX]);
	energy_a = (val & IWM_RX_INFO_ENERGY_ANT_A_MSK) >>
	    IWM_RX_INFO_ENERGY_ANT_A_POS;
	energy_a = energy_a ? -energy_a : -256;
	energy_b = (val & IWM_RX_INFO_ENERGY_ANT_B_MSK) >>
	    IWM_RX_INFO_ENERGY_ANT_B_POS;
	energy_b = energy_b ? -energy_b : -256;
	energy_c = (val & IWM_RX_INFO_ENERGY_ANT_C_MSK) >>
	    IWM_RX_INFO_ENERGY_ANT_C_POS;
	energy_c = energy_c ? -energy_c : -256;
	max_energy = MAX(energy_a, energy_b);
	max_energy = MAX(max_energy, energy_c);

	return max_energy;
}

int
iwm_rxmq_get_signal_strength(struct iwm_softc *sc,
    struct iwm_rx_mpdu_desc *desc)
{
	int energy_a, energy_b;

	energy_a = desc->v1.energy_a;
	energy_b = desc->v1.energy_b;
	energy_a = energy_a ? -energy_a : -256;
	energy_b = energy_b ? -energy_b : -256;
	return MAX(energy_a, energy_b);
}

void
iwm_rx_rx_phy_cmd(struct iwm_softc *sc, struct iwm_rx_packet *pkt,
    struct iwm_rx_data *data)
{
	struct iwm_rx_phy_info *phy_info = (void *)pkt->data;

	bus_dmamap_sync(sc->sc_dmat, data->map, sizeof(*pkt),
	    sizeof(*phy_info), BUS_DMASYNC_POSTREAD);

	memcpy(&sc->sc_last_phy_info, phy_info, sizeof(sc->sc_last_phy_info));
}

/*
 * Retrieve the average noise (in dBm) among receivers.
 */
int
iwm_get_noise(const struct iwm_statistics_rx_non_phy *stats)
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
iwm_ccmp_decap(struct iwm_softc *sc, struct mbuf *m, struct ieee80211_node *ni,
    struct ieee80211_rxinfo *rxi)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ieee80211_key *k = &ni->ni_pairwise_key;
	struct ieee80211_frame *wh;
	uint64_t pn, *prsc;
	uint8_t *ivp;
	uint8_t tid;
	int hdrlen, hasqos;

	wh = mtod(m, struct ieee80211_frame *);
	hdrlen = ieee80211_get_hdrlen(wh);
	ivp = (uint8_t *)wh + hdrlen;

	/* Check that ExtIV bit is set. */
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
iwm_rx_hwdecrypt(struct iwm_softc *sc, struct mbuf *m, uint32_t rx_pkt_status,
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

	if (IEEE80211_IS_MULTICAST(wh->i_addr1) ||
	    !(wh->i_fc[1] & IEEE80211_FC1_PROTECTED))
		return 0;

	ni = ieee80211_find_rxnode(ic, wh);
	/* Handle hardware decryption. */
	if ((ni->ni_flags & IEEE80211_NODE_RXPROT) &&
	    ni->ni_pairwise_key.k_cipher == IEEE80211_CIPHER_CCMP) {
		if ((rx_pkt_status & IWM_RX_MPDU_RES_STATUS_SEC_ENC_MSK) !=
		    IWM_RX_MPDU_RES_STATUS_SEC_CCM_ENC) {
			ic->ic_stats.is_ccmp_dec_errs++;
			ret = 1;
			goto out;
		}
		/* Check whether decryption was successful or not. */
		if ((rx_pkt_status &
		    (IWM_RX_MPDU_RES_STATUS_DEC_DONE |
		    IWM_RX_MPDU_RES_STATUS_MIC_OK)) !=
		    (IWM_RX_MPDU_RES_STATUS_DEC_DONE |
		    IWM_RX_MPDU_RES_STATUS_MIC_OK)) {
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
iwm_rx_frame(struct iwm_softc *sc, struct mbuf *m, int chanidx,
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
	    iwm_ccmp_decap(sc, m, ni, rxi) != 0) {
		ifp->if_ierrors++;
		m_freem(m);
		ieee80211_release_node(ic, ni);
		return;
	}

#if NBPFILTER > 0
	if (sc->sc_drvbpf != NULL) {
		struct iwm_rx_radiotap_header *tap = &sc->sc_rxtap;
		uint16_t chan_flags;

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
		if (rate_n_flags & IWM_RATE_MCS_HT_MSK) {
			uint8_t mcs = (rate_n_flags &
			    (IWM_RATE_HT_MCS_RATE_CODE_MSK |
			    IWM_RATE_HT_MCS_NSS_MSK));
			tap->wr_rate = (0x80 | mcs);
		} else {
			uint8_t rate = (rate_n_flags &
			    IWM_RATE_LEGACY_RATE_MSK);
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

void
iwm_rx_mpdu(struct iwm_softc *sc, struct mbuf *m, void *pktdata,
    size_t maxlen, struct mbuf_list *ml)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ieee80211_rxinfo rxi;
	struct iwm_rx_phy_info *phy_info;
	struct iwm_rx_mpdu_res_start *rx_res;
	int device_timestamp;
	uint16_t phy_flags;
	uint32_t len;
	uint32_t rx_pkt_status;
	int rssi, chanidx, rate_n_flags;

	memset(&rxi, 0, sizeof(rxi));

	phy_info = &sc->sc_last_phy_info;
	rx_res = (struct iwm_rx_mpdu_res_start *)pktdata;
	len = le16toh(rx_res->byte_count);
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
	if (len > maxlen - sizeof(*rx_res)) {
		IC2IFP(ic)->if_ierrors++;
		m_freem(m);
		return;
	}

	if (__predict_false(phy_info->cfg_phy_cnt > 20)) {
		m_freem(m);
		return;
	}

	rx_pkt_status = le32toh(*(uint32_t *)(pktdata + sizeof(*rx_res) + len));
	if (!(rx_pkt_status & IWM_RX_MPDU_RES_STATUS_CRC_OK) ||
	    !(rx_pkt_status & IWM_RX_MPDU_RES_STATUS_OVERRUN_OK)) {
		m_freem(m);
		return; /* drop */
	}

	m->m_data = pktdata + sizeof(*rx_res);
	m->m_pkthdr.len = m->m_len = len;

	if (iwm_rx_hwdecrypt(sc, m, rx_pkt_status, &rxi)) {
		m_freem(m);
		return;
	}

	chanidx = letoh32(phy_info->channel);
	device_timestamp = le32toh(phy_info->system_timestamp);
	phy_flags = letoh16(phy_info->phy_flags);
	rate_n_flags = le32toh(phy_info->rate_n_flags);

	rssi = iwm_get_signal_strength(sc, phy_info);
	rssi = (0 - IWM_MIN_DBM) + rssi;	/* normalize */
	rssi = MIN(rssi, ic->ic_max_rssi);	/* clip to max. 100% */

	rxi.rxi_rssi = rssi;
	rxi.rxi_tstamp = device_timestamp;
	rxi.rxi_chan = chanidx;

	iwm_rx_frame(sc, m, chanidx, rx_pkt_status,
	    (phy_flags & IWM_PHY_INFO_FLAG_SHPREAMBLE),
	    rate_n_flags, device_timestamp, &rxi, ml);
}

void
iwm_flip_address(uint8_t *addr)
{
	int i;
	uint8_t mac_addr[ETHER_ADDR_LEN];

	for (i = 0; i < ETHER_ADDR_LEN; i++)
		mac_addr[i] = addr[ETHER_ADDR_LEN - i - 1];
	IEEE80211_ADDR_COPY(addr, mac_addr);
}

/*
 * Drop duplicate 802.11 retransmissions
 * (IEEE 802.11-2012: 9.3.2.10 "Duplicate detection and recovery")
 * and handle pseudo-duplicate frames which result from deaggregation
 * of A-MSDU frames in hardware.
 */
int
iwm_detect_duplicate(struct iwm_softc *sc, struct mbuf *m,
    struct iwm_rx_mpdu_desc *desc, struct ieee80211_rxinfo *rxi)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct iwm_node *in = (void *)ic->ic_bss;
	struct iwm_rxq_dup_data *dup_data = &in->dup_data;
	uint8_t tid = IWM_MAX_TID_COUNT, subframe_idx;
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
		if (tid > IWM_MAX_TID_COUNT)
			tid = IWM_MAX_TID_COUNT;
	}

	/* If this wasn't a part of an A-MSDU the sub-frame index will be 0 */
	subframe_idx = desc->amsdu_info &
		IWM_RX_MPDU_AMSDU_SUBFRAME_IDX_MASK;

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
	    (desc->mac_flags2 & IWM_RX_MPDU_MFLG2_AMSDU)) {
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
iwm_is_sn_less(uint16_t sn1, uint16_t sn2, uint16_t buffer_size)
{
	return SEQ_LT(sn1, sn2) && !SEQ_LT(sn1, sn2 - buffer_size);
}

void
iwm_release_frames(struct iwm_softc *sc, struct ieee80211_node *ni,
    struct iwm_rxba_data *rxba, struct iwm_reorder_buffer *reorder_buf,
    uint16_t nssn, struct mbuf_list *ml)
{
	struct iwm_reorder_buf_entry *entries = &rxba->entries[0];
	uint16_t ssn = reorder_buf->head_sn;

	/* ignore nssn smaller than head sn - this can happen due to timeout */
	if (iwm_is_sn_less(nssn, ssn, reorder_buf->buf_size))
		goto set_timer;

	while (iwm_is_sn_less(ssn, nssn, reorder_buf->buf_size)) {
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
			iwm_rx_frame(sc, m, chanidx, rx_pkt_status, is_shortpre,
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
iwm_oldsn_workaround(struct iwm_softc *sc, struct ieee80211_node *ni, int tid,
    struct iwm_reorder_buffer *buffer, uint32_t reorder_data, uint32_t gp2)
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
	if (!(reorder_data & IWM_RX_MPDU_REORDER_BA_OLD_SN))
		return 0;

	/* update state */
	buffer->consec_oldsn_prev_drop = 1;
	buffer->consec_oldsn_drops++;

	/* if limit is reached, send del BA and reset state */
	if (buffer->consec_oldsn_drops == IWM_AMPDU_CONSEC_DROPS_DELBA) {
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
iwm_rx_reorder(struct iwm_softc *sc, struct mbuf *m, int chanidx,
    struct iwm_rx_mpdu_desc *desc, int is_shortpre, int rate_n_flags,
    uint32_t device_timestamp, struct ieee80211_rxinfo *rxi,
    struct mbuf_list *ml)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ieee80211_frame *wh;
	struct ieee80211_node *ni;
	struct iwm_rxba_data *rxba;
	struct iwm_reorder_buffer *buffer;
	uint32_t reorder_data = le32toh(desc->reorder_data);
	int is_amsdu = (desc->mac_flags2 & IWM_RX_MPDU_MFLG2_AMSDU);
	int last_subframe =
		(desc->amsdu_info & IWM_RX_MPDU_AMSDU_LAST_SUBFRAME);
	uint8_t tid;
	uint8_t subframe_idx = (desc->amsdu_info &
	    IWM_RX_MPDU_AMSDU_SUBFRAME_IDX_MASK);
	struct iwm_reorder_buf_entry *entries;
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

	baid = (reorder_data & IWM_RX_MPDU_REORDER_BAID_MASK) >>
		IWM_RX_MPDU_REORDER_BAID_SHIFT;
	if (baid == IWM_RX_REORDER_DATA_INVALID_BAID ||
	    baid >= nitems(sc->sc_rxba_data))
		return 0;

	rxba = &sc->sc_rxba_data[baid];
	if (rxba->baid == IWM_RX_REORDER_DATA_INVALID_BAID ||
	    tid != rxba->tid || rxba->sta_id != IWM_STATION_ID)
		return 0;

	if (rxba->timeout != 0)
		getmicrouptime(&rxba->last_rx);

	/* Bypass A-MPDU re-ordering in net80211. */
	rxi->rxi_flags |= IEEE80211_RXI_AMPDU_DONE;

	nssn = reorder_data & IWM_RX_MPDU_REORDER_NSSN_MASK;
	sn = (reorder_data & IWM_RX_MPDU_REORDER_SN_MASK) >>
		IWM_RX_MPDU_REORDER_SN_SHIFT;

	buffer = &rxba->reorder_buf;
	entries = &rxba->entries[0];

	if (!buffer->valid) {
		if (reorder_data & IWM_RX_MPDU_REORDER_BA_OLD_SN)
			return 0;
		buffer->valid = 1;
	}

	ni = ieee80211_find_rxnode(ic, wh);
	if (type == IEEE80211_FC0_TYPE_CTL &&
	    subtype == IEEE80211_FC0_SUBTYPE_BAR) {
		iwm_release_frames(sc, ni, rxba, buffer, nssn, ml);
		goto drop;
	}

	/*
	 * If there was a significant jump in the nssn - adjust.
	 * If the SN is smaller than the NSSN it might need to first go into
	 * the reorder buffer, in which case we just release up to it and the
	 * rest of the function will take care of storing it and releasing up to
	 * the nssn.
	 */
	if (!iwm_is_sn_less(nssn, buffer->head_sn + buffer->buf_size,
	    buffer->buf_size) ||
	    !SEQ_LT(sn, buffer->head_sn + buffer->buf_size)) {
		uint16_t min_sn = SEQ_LT(sn, nssn) ? sn : nssn;
		ic->ic_stats.is_ht_rx_frame_above_ba_winend++;
		iwm_release_frames(sc, ni, rxba, buffer, min_sn, ml);
	}

	if (iwm_oldsn_workaround(sc, ni, tid, buffer, reorder_data,
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
		if (iwm_is_sn_less(buffer->head_sn, nssn, buffer->buf_size) &&
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
		iwm_release_frames(sc, ni, rxba, buffer, nssn, ml);

	ieee80211_release_node(ic, ni);
	return 1;

drop:
	m_freem(m);
	ieee80211_release_node(ic, ni);
	return 1;
}

void
iwm_rx_mpdu_mq(struct iwm_softc *sc, struct mbuf *m, void *pktdata,
    size_t maxlen, struct mbuf_list *ml)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ieee80211_rxinfo rxi;
	struct iwm_rx_mpdu_desc *desc;
	uint32_t len, hdrlen, rate_n_flags, device_timestamp;
	int rssi;
	uint8_t chanidx;
	uint16_t phy_info;

	memset(&rxi, 0, sizeof(rxi));

	desc = (struct iwm_rx_mpdu_desc *)pktdata;

	if (!(desc->status & htole16(IWM_RX_MPDU_RES_STATUS_CRC_OK)) ||
	    !(desc->status & htole16(IWM_RX_MPDU_RES_STATUS_OVERRUN_OK))) {
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
	if (len > maxlen - sizeof(*desc)) {
		IC2IFP(ic)->if_ierrors++;
		m_freem(m);
		return;
	}

	m->m_data = pktdata + sizeof(*desc);
	m->m_pkthdr.len = m->m_len = len;

	/* Account for padding following the frame header. */
	if (desc->mac_flags2 & IWM_RX_MPDU_MFLG2_PAD) {
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
		    IWM_RX_MPDU_RES_STATUS_SEC_ENC_MSK) ==
		    IWM_RX_MPDU_RES_STATUS_SEC_CCM_ENC) {
			/* Padding is inserted after the IV. */
			hdrlen += IEEE80211_CCMP_HDRLEN;
		}
	
		memmove(m->m_data + 2, m->m_data, hdrlen);
		m_adj(m, 2);
	}

	/*
	 * Hardware de-aggregates A-MSDUs and copies the same MAC header
	 * in place for each subframe. But it leaves the 'A-MSDU present'
	 * bit set in the frame header. We need to clear this bit ourselves.
	 *
	 * And we must allow the same CCMP PN for subframes following the
	 * first subframe. Otherwise they would be discarded as replays.
	 */
	if (desc->mac_flags2 & IWM_RX_MPDU_MFLG2_AMSDU) {
		struct ieee80211_frame *wh = mtod(m, struct ieee80211_frame *);
		uint8_t subframe_idx = (desc->amsdu_info &
		    IWM_RX_MPDU_AMSDU_SUBFRAME_IDX_MASK);
		if (subframe_idx > 0)
			rxi.rxi_flags |= IEEE80211_RXI_HWDEC_SAME_PN;
		if (ieee80211_has_qos(wh) && ieee80211_has_addr4(wh) &&
		    m->m_len >= sizeof(struct ieee80211_qosframe_addr4)) {
			struct ieee80211_qosframe_addr4 *qwh4 = mtod(m,
			    struct ieee80211_qosframe_addr4 *);
			qwh4->i_qos[0] &= htole16(~IEEE80211_QOS_AMSDU);

			/* HW reverses addr3 and addr4. */
			iwm_flip_address(qwh4->i_addr3);
			iwm_flip_address(qwh4->i_addr4);
		} else if (ieee80211_has_qos(wh) &&
		    m->m_len >= sizeof(struct ieee80211_qosframe)) {
			struct ieee80211_qosframe *qwh = mtod(m,
			    struct ieee80211_qosframe *);
			qwh->i_qos[0] &= htole16(~IEEE80211_QOS_AMSDU);

			/* HW reverses addr3. */
			iwm_flip_address(qwh->i_addr3);
		}	
	}

	/*
	 * Verify decryption before duplicate detection. The latter uses
	 * the TID supplied in QoS frame headers and this TID is implicitly
	 * verified as part of the CCMP nonce.
	 */
	if (iwm_rx_hwdecrypt(sc, m, le16toh(desc->status), &rxi)) {
		m_freem(m);
		return;
	}

	if (iwm_detect_duplicate(sc, m, desc, &rxi)) {
		m_freem(m);
		return;
	}

	phy_info = le16toh(desc->phy_info);
	rate_n_flags = le32toh(desc->v1.rate_n_flags);
	chanidx = desc->v1.channel;
	device_timestamp = desc->v1.gp2_on_air_rise;

	rssi = iwm_rxmq_get_signal_strength(sc, desc);
	rssi = (0 - IWM_MIN_DBM) + rssi;	/* normalize */
	rssi = MIN(rssi, ic->ic_max_rssi);	/* clip to max. 100% */

	rxi.rxi_rssi = rssi;
	rxi.rxi_tstamp = le64toh(desc->v1.tsf_on_air_rise);
	rxi.rxi_chan = chanidx;

	if (iwm_rx_reorder(sc, m, chanidx, desc,
	    (phy_info & IWM_RX_MPDU_PHY_SHORT_PREAMBLE),
	    rate_n_flags, device_timestamp, &rxi, ml))
		return;

	iwm_rx_frame(sc, m, chanidx, le16toh(desc->status),
	    (phy_info & IWM_RX_MPDU_PHY_SHORT_PREAMBLE),
	    rate_n_flags, device_timestamp, &rxi, ml);
}

void
iwm_ra_choose(struct iwm_softc *sc, struct ieee80211_node *ni)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct iwm_node *in = (void *)ni;
	int old_txmcs = ni->ni_txmcs;
	int old_nss = ni->ni_vht_ss;

	if (ni->ni_flags & IEEE80211_NODE_VHT)
		ieee80211_ra_vht_choose(&in->in_rn_vht, ic, ni);
	else
		ieee80211_ra_choose(&in->in_rn, ic, ni);

	/* 
	 * If RA has chosen a new TX rate we must update
	 * the firmware's LQ rate table.
	 */
	if (ni->ni_txmcs != old_txmcs || ni->ni_vht_ss != old_nss)
		iwm_setrates(in, 1);
}

void
iwm_ht_single_rate_control(struct iwm_softc *sc, struct ieee80211_node *ni,
    int txmcs, uint8_t failure_frame, int txfail)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct iwm_node *in = (void *)ni;

	/* Ignore Tx reports which don't match our last LQ command. */
	if (txmcs != ni->ni_txmcs) {
		if (++in->lq_rate_mismatch > 15) {
			/* Try to sync firmware with the driver... */
			iwm_setrates(in, 1);
			in->lq_rate_mismatch = 0;
		}
	} else {
		int mcs = txmcs;
		const struct ieee80211_ht_rateset *rs =
		    ieee80211_ra_get_ht_rateset(txmcs,
		        ieee80211_node_supports_ht_chan40(ni),
			ieee80211_ra_use_ht_sgi(ni));
		unsigned int retries = 0, i;

		in->lq_rate_mismatch = 0;

		for (i = 0; i < failure_frame; i++) {
			if (mcs > rs->min_mcs) {
				ieee80211_ra_add_stats_ht(&in->in_rn,
				    ic, ni, mcs, 1, 1);
				mcs--;
			} else
				retries++;
		}

		if (txfail && failure_frame == 0) {
			ieee80211_ra_add_stats_ht(&in->in_rn, ic, ni,
			    txmcs, 1, 1);
		} else {
			ieee80211_ra_add_stats_ht(&in->in_rn, ic, ni,
			    mcs, retries + 1, retries);
		}

		iwm_ra_choose(sc, ni);
	}
}

void
iwm_vht_single_rate_control(struct iwm_softc *sc, struct ieee80211_node *ni,
    int txmcs, int nss, uint8_t failure_frame, int txfail)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct iwm_node *in = (void *)ni;
	uint8_t vht_chan_width = IEEE80211_VHTOP0_CHAN_WIDTH_80;
	uint8_t sco = IEEE80211_HTOP0_SCO_SCN;

	/* Ignore Tx reports which don't match our last LQ command. */
	if (txmcs != ni->ni_txmcs || nss != ni->ni_vht_ss) {
		if (++in->lq_rate_mismatch > 15) {
			/* Try to sync firmware with the driver... */
			iwm_setrates(in, 1);
			in->lq_rate_mismatch = 0;
		}
	} else {
		int mcs = txmcs;
		unsigned int retries = 0, i;

		if (in->in_phyctxt) {
			vht_chan_width = in->in_phyctxt->vht_chan_width;
			sco = in->in_phyctxt->sco;
		}
		in->lq_rate_mismatch = 0;

		for (i = 0; i < failure_frame; i++) {
			if (mcs > 0) {
				ieee80211_ra_vht_add_stats(&in->in_rn_vht,
				    ic, ni, mcs, nss, 1, 1);
				if (vht_chan_width >=
				    IEEE80211_VHTOP0_CHAN_WIDTH_80) {
					/*
					 * First 4 Tx attempts used same MCS,
					 * twice at 80MHz and twice at 40MHz.
					 */
					if (i >= 4)
						mcs--;
				} else if (sco == IEEE80211_HTOP0_SCO_SCA ||
				    sco == IEEE80211_HTOP0_SCO_SCB) {
					/*
					 * First 4 Tx attempts used same MCS,
					 * four times at 40MHz.
					 */
					if (i >= 4)
						mcs--;
				} else
					mcs--;
			} else
				retries++;
		}

		if (txfail && failure_frame == 0) {
			ieee80211_ra_vht_add_stats(&in->in_rn_vht, ic, ni,
			    txmcs, nss, 1, 1);
		} else {
			ieee80211_ra_vht_add_stats(&in->in_rn_vht, ic, ni,
			    mcs, nss, retries + 1, retries);
		}

		iwm_ra_choose(sc, ni);
	}
}

void
iwm_rx_tx_cmd_single(struct iwm_softc *sc, struct iwm_rx_packet *pkt,
    struct iwm_node *in, int txmcs, int txrate)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ieee80211_node *ni = &in->in_ni;
	struct ifnet *ifp = IC2IFP(ic);
	struct iwm_tx_resp *tx_resp = (void *)pkt->data;
	int status = le16toh(tx_resp->status.status) & IWM_TX_STATUS_MSK;
	uint32_t initial_rate = le32toh(tx_resp->initial_rate);
	int txfail;
	
	KASSERT(tx_resp->frame_count == 1);

	txfail = (status != IWM_TX_STATUS_SUCCESS &&
	    status != IWM_TX_STATUS_DIRECT_DONE);

	/*
	 * Update rate control statistics.
	 * Only report frames which were actually queued with the currently
	 * selected Tx rate. Because Tx queues are relatively long we may
	 * encounter previously selected rates here during Tx bursts.
	 * Providing feedback based on such frames can lead to suboptimal
	 * Tx rate control decisions.
	 */
	if ((ni->ni_flags & IEEE80211_NODE_HT) == 0) {
		if (txrate != ni->ni_txrate) {
			if (++in->lq_rate_mismatch > 15) {
				/* Try to sync firmware with the driver... */
				iwm_setrates(in, 1);
				in->lq_rate_mismatch = 0;
			}
		} else {
			in->lq_rate_mismatch = 0;

			in->in_amn.amn_txcnt++;
			if (txfail)
				in->in_amn.amn_retrycnt++;
			if (tx_resp->failure_frame > 0)
				in->in_amn.amn_retrycnt++;
		}
	} else if ((ni->ni_flags & IEEE80211_NODE_VHT) &&
	    ic->ic_fixed_mcs == -1 && ic->ic_state == IEEE80211_S_RUN &&
	    (initial_rate & IWM_RATE_MCS_VHT_MSK)) {
		int txmcs = initial_rate & IWM_RATE_VHT_MCS_RATE_CODE_MSK;
		int nss = ((initial_rate & IWM_RATE_VHT_MCS_NSS_MSK) >>
		    IWM_RATE_VHT_MCS_NSS_POS) + 1;
		iwm_vht_single_rate_control(sc, ni, txmcs, nss,
		    tx_resp->failure_frame, txfail);
	} else if (ic->ic_fixed_mcs == -1 && ic->ic_state == IEEE80211_S_RUN &&
	    (initial_rate & IWM_RATE_MCS_HT_MSK)) {
		int txmcs = initial_rate &
		    (IWM_RATE_HT_MCS_RATE_CODE_MSK | IWM_RATE_HT_MCS_NSS_MSK);
		iwm_ht_single_rate_control(sc, ni, txmcs,
		    tx_resp->failure_frame, txfail);
	}

	if (txfail)
		ifp->if_oerrors++;
}

void
iwm_txd_done(struct iwm_softc *sc, struct iwm_tx_data *txd)
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
	txd->ampdu_nframes = 0;
	txd->ampdu_txmcs = 0;
	txd->ampdu_txnss = 0;
}

void
iwm_txq_advance(struct iwm_softc *sc, struct iwm_tx_ring *ring, int idx)
{
	struct iwm_tx_data *txd;

	while (ring->tail != idx) {
		txd = &ring->data[ring->tail];
		if (txd->m != NULL) {
			iwm_reset_sched(sc, ring->qid, ring->tail, IWM_STATION_ID);
			iwm_txd_done(sc, txd);
			ring->queued--;
		}
		ring->tail = (ring->tail + 1) % IWM_TX_RING_COUNT;
	}

	wakeup(ring);
}

void
iwm_ampdu_tx_done(struct iwm_softc *sc, struct iwm_cmd_header *cmd_hdr,
    struct iwm_node *in, struct iwm_tx_ring *txq, uint32_t initial_rate,
    uint8_t nframes, uint8_t failure_frame, uint16_t ssn, int status,
    struct iwm_agg_tx_status *agg_status)
{
	struct ieee80211com *ic = &sc->sc_ic;
	int tid = cmd_hdr->qid - IWM_FIRST_AGG_TX_QUEUE;
	struct iwm_tx_data *txdata = &txq->data[cmd_hdr->idx];
	struct ieee80211_node *ni = &in->in_ni;
	struct ieee80211_tx_ba *ba;
	int txfail = (status != IWM_TX_STATUS_SUCCESS &&
	    status != IWM_TX_STATUS_DIRECT_DONE);
	uint16_t seq;

	if (ic->ic_state != IEEE80211_S_RUN)
		return;

	if (nframes > 1) {
		int i;
 		/*
		 * Collect information about this A-MPDU.
		 */

		for (i = 0; i < nframes; i++) {
			uint8_t qid = agg_status[i].qid;
			uint8_t idx = agg_status[i].idx;
			uint16_t txstatus = (le16toh(agg_status[i].status) &
			    IWM_AGG_TX_STATE_STATUS_MSK);

			if (txstatus != IWM_AGG_TX_STATE_TRANSMITTED)
				continue;

			if (qid != cmd_hdr->qid)
				continue;

			txdata = &txq->data[idx];
			if (txdata->m == NULL)
				continue;

			/* The Tx rate was the same for all subframes. */
			if ((ni->ni_flags & IEEE80211_NODE_VHT) &&
			    (initial_rate & IWM_RATE_MCS_VHT_MSK)) {
				txdata->ampdu_txmcs = initial_rate &
				    IWM_RATE_VHT_MCS_RATE_CODE_MSK;
				txdata->ampdu_txnss = ((initial_rate &
				    IWM_RATE_VHT_MCS_NSS_MSK) >>
				    IWM_RATE_VHT_MCS_NSS_POS) + 1;
				txdata->ampdu_nframes = nframes;
			} else if (initial_rate & IWM_RATE_MCS_HT_MSK) {
				txdata->ampdu_txmcs = initial_rate &
				    (IWM_RATE_HT_MCS_RATE_CODE_MSK |
				    IWM_RATE_HT_MCS_NSS_MSK);
				txdata->ampdu_nframes = nframes;
			}
		}
		return;
	}

	ba = &ni->ni_tx_ba[tid];
	if (ba->ba_state != IEEE80211_BA_AGREED)
		return;
	if (SEQ_LT(ssn, ba->ba_winstart))
		return;

	/* This was a final single-frame Tx attempt for frame SSN-1. */
	seq = (ssn - 1) & 0xfff;

	/*
	 * Skip rate control if our Tx rate is fixed.
	 * Don't report frames to MiRA which were sent at a different
	 * Tx rate than ni->ni_txmcs.
	 */
	if (ic->ic_fixed_mcs == -1) {
		if (txdata->ampdu_nframes > 1) {
			/*
			 * This frame was once part of an A-MPDU.
			 * Report one failed A-MPDU Tx attempt.
			 * The firmware might have made several such
			 * attempts but we don't keep track of this.
			 */
			if (ni->ni_flags & IEEE80211_NODE_VHT) {
				ieee80211_ra_vht_add_stats(&in->in_rn_vht,
				    ic, ni, txdata->ampdu_txmcs,
				    txdata->ampdu_txnss, 1, 1);
			} else {
				ieee80211_ra_add_stats_ht(&in->in_rn, ic, ni,
				    txdata->ampdu_txmcs, 1, 1);
			}
		}

		/* Report the final single-frame Tx attempt. */
		if ((ni->ni_flags & IEEE80211_NODE_VHT) &&
		    (initial_rate & IWM_RATE_MCS_VHT_MSK)) {
			int txmcs = initial_rate &
			    IWM_RATE_VHT_MCS_RATE_CODE_MSK;
			int nss = ((initial_rate &
			    IWM_RATE_VHT_MCS_NSS_MSK) >>
			    IWM_RATE_VHT_MCS_NSS_POS) + 1;
			iwm_vht_single_rate_control(sc, ni, txmcs, nss,
			    failure_frame, txfail);
		} else if (initial_rate & IWM_RATE_MCS_HT_MSK) {
			int txmcs = initial_rate &
			   (IWM_RATE_HT_MCS_RATE_CODE_MSK |
			   IWM_RATE_HT_MCS_NSS_MSK);
			iwm_ht_single_rate_control(sc, ni, txmcs,
			    failure_frame, txfail);
		}
	}

	if (txfail)
		ieee80211_tx_compressed_bar(ic, ni, tid, ssn);

	/*
	 * SSN corresponds to the first (perhaps not yet transmitted) frame
	 * in firmware's BA window. Firmware is not going to retransmit any
	 * frames before its BA window so mark them all as done.
	 */
	ieee80211_output_ba_move_window(ic, ni, tid, ssn);
	iwm_txq_advance(sc, txq, IWM_AGG_SSN_TO_TXQ_IDX(ssn));
	iwm_clear_oactive(sc, txq);
}

void
iwm_rx_tx_cmd(struct iwm_softc *sc, struct iwm_rx_packet *pkt,
    struct iwm_rx_data *data)
{
	struct iwm_cmd_header *cmd_hdr = &pkt->hdr;
	int idx = cmd_hdr->idx;
	int qid = cmd_hdr->qid;
	struct iwm_tx_ring *ring = &sc->txq[qid];
	struct iwm_tx_data *txd;
	struct iwm_tx_resp *tx_resp = (void *)pkt->data;
	uint32_t ssn;
	uint32_t len = iwm_rx_packet_len(pkt);

	bus_dmamap_sync(sc->sc_dmat, data->map, 0, IWM_RBUF_SIZE,
	    BUS_DMASYNC_POSTREAD);

	/* Sanity checks. */
	if (sizeof(*tx_resp) > len)
		return;
	if (qid < IWM_FIRST_AGG_TX_QUEUE && tx_resp->frame_count > 1)
		return;
	if (qid > IWM_LAST_AGG_TX_QUEUE)
		return;
	if (sizeof(*tx_resp) + sizeof(ssn) +
	    tx_resp->frame_count * sizeof(tx_resp->status) > len)
		return;

	sc->sc_tx_timer[qid] = 0;

	txd = &ring->data[idx];
	if (txd->m == NULL)
		return;

	memcpy(&ssn, &tx_resp->status + tx_resp->frame_count, sizeof(ssn));
	ssn = le32toh(ssn) & 0xfff;
	if (qid >= IWM_FIRST_AGG_TX_QUEUE) {
		int status;
		status = le16toh(tx_resp->status.status) & IWM_TX_STATUS_MSK;
		iwm_ampdu_tx_done(sc, cmd_hdr, txd->in, ring,
		    le32toh(tx_resp->initial_rate), tx_resp->frame_count,
		    tx_resp->failure_frame, ssn, status, &tx_resp->status);
	} else {
		/*
		 * Even though this is not an agg queue, we must only free
		 * frames before the firmware's starting sequence number.
		 */
		iwm_rx_tx_cmd_single(sc, pkt, txd->in, txd->txmcs, txd->txrate);
		iwm_txq_advance(sc, ring, IWM_AGG_SSN_TO_TXQ_IDX(ssn));
		iwm_clear_oactive(sc, ring);
	}
}

void
iwm_clear_oactive(struct iwm_softc *sc, struct iwm_tx_ring *ring)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ifnet *ifp = IC2IFP(ic);

	if (ring->queued < IWM_TX_RING_LOMARK) {
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
iwm_ampdu_rate_control(struct iwm_softc *sc, struct ieee80211_node *ni,
    struct iwm_tx_ring *txq, int tid, uint16_t seq, uint16_t ssn)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct iwm_node *in = (void *)ni;
	int idx, end_idx;

	/*
	 * Update Tx rate statistics for A-MPDUs before firmware's BA window.
	 */
	idx = IWM_AGG_SSN_TO_TXQ_IDX(seq);
	end_idx = IWM_AGG_SSN_TO_TXQ_IDX(ssn);
	while (idx != end_idx) {
		struct iwm_tx_data *txdata = &txq->data[idx];
		if (txdata->m != NULL && txdata->ampdu_nframes > 1) {
			/*
			 * We can assume that this subframe has been ACKed
			 * because ACK failures come as single frames and
			 * before failing an A-MPDU subframe the firmware
			 * sends it as a single frame at least once.
			 */
			if (ni->ni_flags & IEEE80211_NODE_VHT) {
				ieee80211_ra_vht_add_stats(&in->in_rn_vht,
				    ic, ni, txdata->ampdu_txmcs,
				    txdata->ampdu_txnss, 1, 0);
			} else {
				ieee80211_ra_add_stats_ht(&in->in_rn, ic, ni,
				    txdata->ampdu_txmcs, 1, 0);
			}
			/* Report this frame only once. */
			txdata->ampdu_nframes = 0;
		}

		idx = (idx + 1) % IWM_TX_RING_COUNT;
	}

	iwm_ra_choose(sc, ni);
}

void
iwm_rx_compressed_ba(struct iwm_softc *sc, struct iwm_rx_packet *pkt)
{
	struct iwm_ba_notif *ban = (void *)pkt->data;
	struct ieee80211com *ic = &sc->sc_ic;
	struct ieee80211_node *ni = ic->ic_bss;
	struct iwm_node *in = (void *)ni;
	struct ieee80211_tx_ba *ba;
	struct iwm_tx_ring *ring;
	uint16_t seq, ssn;
	int qid;

	if (ic->ic_state != IEEE80211_S_RUN)
		return;

	if (iwm_rx_packet_payload_len(pkt) < sizeof(*ban))
		return;

	if (ban->sta_id != IWM_STATION_ID ||
	    !IEEE80211_ADDR_EQ(in->in_macaddr, ban->sta_addr))
		return;

	qid = le16toh(ban->scd_flow);
	if (qid < IWM_FIRST_AGG_TX_QUEUE || qid > IWM_LAST_AGG_TX_QUEUE)
		return;

	/* Protect against a firmware bug where the queue/TID are off. */
	if (qid != IWM_FIRST_AGG_TX_QUEUE + ban->tid)
		return;

	sc->sc_tx_timer[qid] = 0;

	ba = &ni->ni_tx_ba[ban->tid];
	if (ba->ba_state != IEEE80211_BA_AGREED)
		return;

	ring = &sc->txq[qid];

	/*
	 * The first bit in ban->bitmap corresponds to the sequence number
	 * stored in the sequence control field ban->seq_ctl.
	 * Multiple BA notifications in a row may be using this number, with
	 * additional bits being set in cba->bitmap. It is unclear how the
	 * firmware decides to shift this window forward.
	 * We rely on ba->ba_winstart instead.
	 */
	seq = le16toh(ban->seq_ctl) >> IEEE80211_SEQ_SEQ_SHIFT;

	/*
	 * The firmware's new BA window starting sequence number
	 * corresponds to the first hole in ban->scd_ssn, implying
	 * that all frames between 'seq' and 'ssn' (non-inclusive)
	 * have been acked.
	 */
	ssn = le16toh(ban->scd_ssn);

	if (SEQ_LT(ssn, ba->ba_winstart))
		return;

	/* Skip rate control if our Tx rate is fixed. */
	if (ic->ic_fixed_mcs == -1)
		iwm_ampdu_rate_control(sc, ni, ring, ban->tid,
		    ba->ba_winstart, ssn);

	/*
	 * SSN corresponds to the first (perhaps not yet transmitted) frame
	 * in firmware's BA window. Firmware is not going to retransmit any
	 * frames before its BA window so mark them all as done.
	 */
	ieee80211_output_ba_move_window(ic, ni, ban->tid, ssn);
	iwm_txq_advance(sc, ring, IWM_AGG_SSN_TO_TXQ_IDX(ssn));
	iwm_clear_oactive(sc, ring);
}

void
iwm_rx_bmiss(struct iwm_softc *sc, struct iwm_rx_packet *pkt,
    struct iwm_rx_data *data)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct iwm_missed_beacons_notif *mbn = (void *)pkt->data;
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
iwm_binding_cmd(struct iwm_softc *sc, struct iwm_node *in, uint32_t action)
{
	struct iwm_binding_cmd cmd;
	struct iwm_phy_ctxt *phyctxt = in->in_phyctxt;
	uint32_t mac_id = IWM_FW_CMD_ID_AND_COLOR(in->in_id, in->in_color);
	int i, err, active = (sc->sc_flags & IWM_FLAG_BINDING_ACTIVE);
	uint32_t status;
	size_t len;

	if (action == IWM_FW_CTXT_ACTION_ADD && active)
		panic("binding already added");
	if (action == IWM_FW_CTXT_ACTION_REMOVE && !active)
		panic("binding already removed");

	if (phyctxt == NULL) /* XXX race with iwm_stop() */
		return EINVAL;

	memset(&cmd, 0, sizeof(cmd));

	cmd.id_and_color
	    = htole32(IWM_FW_CMD_ID_AND_COLOR(phyctxt->id, phyctxt->color));
	cmd.action = htole32(action);
	cmd.phy = htole32(IWM_FW_CMD_ID_AND_COLOR(phyctxt->id, phyctxt->color));

	cmd.macs[0] = htole32(mac_id);
	for (i = 1; i < IWM_MAX_MACS_IN_BINDING; i++)
		cmd.macs[i] = htole32(IWM_FW_CTXT_INVALID);

	if (IEEE80211_IS_CHAN_2GHZ(phyctxt->channel) ||
	    !isset(sc->sc_enabled_capa, IWM_UCODE_TLV_CAPA_CDB_SUPPORT))
		cmd.lmac_id = htole32(IWM_LMAC_24G_INDEX);
	else
		cmd.lmac_id = htole32(IWM_LMAC_5G_INDEX);

	if (isset(sc->sc_enabled_capa, IWM_UCODE_TLV_CAPA_BINDING_CDB_SUPPORT))
		len = sizeof(cmd);
	else
		len = sizeof(struct iwm_binding_cmd_v1);
	status = 0;
	err = iwm_send_cmd_pdu_status(sc, IWM_BINDING_CONTEXT_CMD, len, &cmd,
	    &status);
	if (err == 0 && status != 0)
		err = EIO;

	return err;
}

void
iwm_phy_ctxt_cmd_hdr(struct iwm_softc *sc, struct iwm_phy_ctxt *ctxt,
    struct iwm_phy_context_cmd *cmd, uint32_t action, uint32_t apply_time)
{
	memset(cmd, 0, sizeof(struct iwm_phy_context_cmd));

	cmd->id_and_color = htole32(IWM_FW_CMD_ID_AND_COLOR(ctxt->id,
	    ctxt->color));
	cmd->action = htole32(action);
	cmd->apply_time = htole32(apply_time);
}

void
iwm_phy_ctxt_cmd_data(struct iwm_softc *sc, struct iwm_phy_context_cmd *cmd,
    struct ieee80211_channel *chan, uint8_t chains_static,
    uint8_t chains_dynamic, uint8_t sco, uint8_t vht_chan_width)
{
	struct ieee80211com *ic = &sc->sc_ic;
	uint8_t active_cnt, idle_cnt;

	cmd->ci.band = IEEE80211_IS_CHAN_2GHZ(chan) ?
	    IWM_PHY_BAND_24 : IWM_PHY_BAND_5;
	cmd->ci.channel = ieee80211_chan2ieee(ic, chan);
	if (vht_chan_width == IEEE80211_VHTOP0_CHAN_WIDTH_80) {
		cmd->ci.ctrl_pos = iwm_get_vht_ctrl_pos(ic, chan);
		cmd->ci.width = IWM_PHY_VHT_CHANNEL_MODE80;
	} else if (chan->ic_flags & IEEE80211_CHAN_40MHZ) {
		if (sco == IEEE80211_HTOP0_SCO_SCA) {
			/* secondary chan above -> control chan below */
			cmd->ci.ctrl_pos = IWM_PHY_VHT_CTRL_POS_1_BELOW;
			cmd->ci.width = IWM_PHY_VHT_CHANNEL_MODE40;
		} else if (sco == IEEE80211_HTOP0_SCO_SCB) {
			/* secondary chan below -> control chan above */
			cmd->ci.ctrl_pos = IWM_PHY_VHT_CTRL_POS_1_ABOVE;
			cmd->ci.width = IWM_PHY_VHT_CHANNEL_MODE40;
		} else {
			cmd->ci.width = IWM_PHY_VHT_CHANNEL_MODE20;
			cmd->ci.ctrl_pos = IWM_PHY_VHT_CTRL_POS_1_BELOW;
		}
	} else {
		cmd->ci.width = IWM_PHY_VHT_CHANNEL_MODE20;
		cmd->ci.ctrl_pos = IWM_PHY_VHT_CTRL_POS_1_BELOW;
	}

	/* Set rx the chains */
	idle_cnt = chains_static;
	active_cnt = chains_dynamic;

	cmd->rxchain_info = htole32(iwm_fw_valid_rx_ant(sc) <<
					IWM_PHY_RX_CHAIN_VALID_POS);
	cmd->rxchain_info |= htole32(idle_cnt << IWM_PHY_RX_CHAIN_CNT_POS);
	cmd->rxchain_info |= htole32(active_cnt <<
	    IWM_PHY_RX_CHAIN_MIMO_CNT_POS);

	cmd->txchain_info = htole32(iwm_fw_valid_tx_ant(sc));
}

uint8_t
iwm_get_vht_ctrl_pos(struct ieee80211com *ic, struct ieee80211_channel *chan)
{
	int center_idx = ic->ic_bss->ni_vht_chan_center_freq_idx0;
	int primary_idx = ic->ic_bss->ni_primary_chan;
	/*
	 * The FW is expected to check the control channel position only
	 * when in HT/VHT and the channel width is not 20MHz. Return
	 * this value as the default one:
	 */
	uint8_t pos = IWM_PHY_VHT_CTRL_POS_1_BELOW;

	switch (primary_idx - center_idx) {
	case -6:
		pos = IWM_PHY_VHT_CTRL_POS_2_BELOW;
		break;
	case -2:
		pos = IWM_PHY_VHT_CTRL_POS_1_BELOW;
		break;
	case 2:
		pos = IWM_PHY_VHT_CTRL_POS_1_ABOVE;
		break;
	case 6:
		pos = IWM_PHY_VHT_CTRL_POS_2_ABOVE;
		break;
	default:
		break;
	}

	return pos;
}

int
iwm_phy_ctxt_cmd_uhb(struct iwm_softc *sc, struct iwm_phy_ctxt *ctxt,
    uint8_t chains_static, uint8_t chains_dynamic, uint32_t action,
    uint32_t apply_time, uint8_t sco, uint8_t vht_chan_width)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct iwm_phy_context_cmd_uhb cmd;
	uint8_t active_cnt, idle_cnt;
	struct ieee80211_channel *chan = ctxt->channel;

	memset(&cmd, 0, sizeof(cmd));
	cmd.id_and_color = htole32(IWM_FW_CMD_ID_AND_COLOR(ctxt->id,
	    ctxt->color));
	cmd.action = htole32(action);
	cmd.apply_time = htole32(apply_time);

	cmd.ci.band = IEEE80211_IS_CHAN_2GHZ(chan) ?
	    IWM_PHY_BAND_24 : IWM_PHY_BAND_5;
	cmd.ci.channel = htole32(ieee80211_chan2ieee(ic, chan));
	if (vht_chan_width == IEEE80211_VHTOP0_CHAN_WIDTH_80) {
		cmd.ci.ctrl_pos = iwm_get_vht_ctrl_pos(ic, chan);
		cmd.ci.width = IWM_PHY_VHT_CHANNEL_MODE80;
	} else if (chan->ic_flags & IEEE80211_CHAN_40MHZ) {
		if (sco == IEEE80211_HTOP0_SCO_SCA) {
			/* secondary chan above -> control chan below */
			cmd.ci.ctrl_pos = IWM_PHY_VHT_CTRL_POS_1_BELOW;
			cmd.ci.width = IWM_PHY_VHT_CHANNEL_MODE40;
		} else if (sco == IEEE80211_HTOP0_SCO_SCB) {
			/* secondary chan below -> control chan above */
			cmd.ci.ctrl_pos = IWM_PHY_VHT_CTRL_POS_1_ABOVE;
			cmd.ci.width = IWM_PHY_VHT_CHANNEL_MODE40;
		} else {
			cmd.ci.width = IWM_PHY_VHT_CHANNEL_MODE20;
			cmd.ci.ctrl_pos = IWM_PHY_VHT_CTRL_POS_1_BELOW;
		}
	} else {
		cmd.ci.width = IWM_PHY_VHT_CHANNEL_MODE20;
		cmd.ci.ctrl_pos = IWM_PHY_VHT_CTRL_POS_1_BELOW;
	}

	idle_cnt = chains_static;
	active_cnt = chains_dynamic;
	cmd.rxchain_info = htole32(iwm_fw_valid_rx_ant(sc) <<
					IWM_PHY_RX_CHAIN_VALID_POS);
	cmd.rxchain_info |= htole32(idle_cnt << IWM_PHY_RX_CHAIN_CNT_POS);
	cmd.rxchain_info |= htole32(active_cnt <<
	    IWM_PHY_RX_CHAIN_MIMO_CNT_POS);
	cmd.txchain_info = htole32(iwm_fw_valid_tx_ant(sc));

	return iwm_send_cmd_pdu(sc, IWM_PHY_CONTEXT_CMD, 0, sizeof(cmd), &cmd);
}

int
iwm_phy_ctxt_cmd(struct iwm_softc *sc, struct iwm_phy_ctxt *ctxt,
    uint8_t chains_static, uint8_t chains_dynamic, uint32_t action,
    uint32_t apply_time, uint8_t sco, uint8_t vht_chan_width)
{
	struct iwm_phy_context_cmd cmd;

	/*
	 * Intel increased the size of the fw_channel_info struct and neglected
	 * to bump the phy_context_cmd struct, which contains an fw_channel_info
	 * member in the middle.
	 * To keep things simple we use a separate function to handle the larger
	 * variant of the phy context command.
	 */
	if (isset(sc->sc_enabled_capa, IWM_UCODE_TLV_CAPA_ULTRA_HB_CHANNELS))
		return iwm_phy_ctxt_cmd_uhb(sc, ctxt, chains_static,
		    chains_dynamic, action, apply_time, sco, vht_chan_width);

	iwm_phy_ctxt_cmd_hdr(sc, ctxt, &cmd, action, apply_time);

	iwm_phy_ctxt_cmd_data(sc, &cmd, ctxt->channel,
	    chains_static, chains_dynamic, sco, vht_chan_width);

	return iwm_send_cmd_pdu(sc, IWM_PHY_CONTEXT_CMD, 0,
	    sizeof(struct iwm_phy_context_cmd), &cmd);
}

int
iwm_send_cmd(struct iwm_softc *sc, struct iwm_host_cmd *hcmd)
{
	struct iwm_tx_ring *ring = &sc->txq[sc->cmdqid];
	struct iwm_tfd *desc;
	struct iwm_tx_data *txdata;
	struct iwm_device_cmd *cmd;
	struct mbuf *m;
	bus_addr_t paddr;
	uint32_t addr_lo;
	int err = 0, i, paylen, off, s;
	int idx, code, async, group_id;
	size_t hdrlen, datasz;
	uint8_t *data;
	int generation = sc->sc_generation;

	code = hcmd->id;
	async = hcmd->flags & IWM_CMD_ASYNC;
	idx = ring->cur;

	for (i = 0, paylen = 0; i < nitems(hcmd->len); i++) {
		paylen += hcmd->len[i];
	}

	/* If this command waits for a response, allocate response buffer. */
	hcmd->resp_pkt = NULL;
	if (hcmd->flags & IWM_CMD_WANT_RESP) {
		uint8_t *resp_buf;
		KASSERT(!async);
		KASSERT(hcmd->resp_pkt_len >= sizeof(struct iwm_rx_packet));
		KASSERT(hcmd->resp_pkt_len <= IWM_CMD_RESP_MAX);
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

	group_id = iwm_cmd_groupid(code);
	if (group_id != 0) {
		hdrlen = sizeof(cmd->hdr_wide);
		datasz = sizeof(cmd->data_wide);
	} else {
		hdrlen = sizeof(cmd->hdr);
		datasz = sizeof(cmd->data);
	}

	if (paylen > datasz) {
		/* Command is too large to fit in pre-allocated space. */
		size_t totlen = hdrlen + paylen;
		if (paylen > IWM_MAX_CMD_PAYLOAD_SIZE) {
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
		cmd = mtod(m, struct iwm_device_cmd *);
		err = bus_dmamap_load(sc->sc_dmat, txdata->map, cmd,
		    totlen, NULL, BUS_DMA_NOWAIT | BUS_DMA_WRITE);
		if (err) {
			printf("%s: could not load fw cmd mbuf (%zd bytes)\n",
			    DEVNAME(sc), totlen);
			m_freem(m);
			goto out;
		}
		txdata->m = m; /* mbuf will be freed in iwm_cmd_done() */
		paddr = txdata->map->dm_segs[0].ds_addr;
	} else {
		cmd = &ring->cmd[idx];
		paddr = txdata->cmd_paddr;
	}

	if (group_id != 0) {
		cmd->hdr_wide.opcode = iwm_cmd_opcode(code);
		cmd->hdr_wide.group_id = group_id;
		cmd->hdr_wide.qid = ring->qid;
		cmd->hdr_wide.idx = idx;
		cmd->hdr_wide.length = htole16(paylen);
		cmd->hdr_wide.version = iwm_cmd_version(code);
		data = cmd->data_wide;
	} else {
		cmd->hdr.code = code;
		cmd->hdr.flags = 0;
		cmd->hdr.qid = ring->qid;
		cmd->hdr.idx = idx;
		data = cmd->data;
	}

	for (i = 0, off = 0; i < nitems(hcmd->data); i++) {
		if (hcmd->len[i] == 0)
			continue;
		memcpy(data + off, hcmd->data[i], hcmd->len[i]);
		off += hcmd->len[i];
	}
	KASSERT(off == paylen);

	/* lo field is not aligned */
	addr_lo = htole32((uint32_t)paddr);
	memcpy(&desc->tbs[0].lo, &addr_lo, sizeof(uint32_t));
	desc->tbs[0].hi_n_len  = htole16(iwm_get_dma_hi_addr(paddr)
	    | ((hdrlen + paylen) << 4));
	desc->num_tbs = 1;

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

	/*
	 * Wake up the NIC to make sure that the firmware will see the host
	 * command - we will let the NIC sleep once all the host commands
	 * returned. This needs to be done only on 7000 family NICs.
	 */
	if (sc->sc_device_family == IWM_DEVICE_FAMILY_7000) {
		if (ring->queued == 0 && !iwm_nic_lock(sc)) {
			err = EBUSY;
			goto out;
		}
	}

	iwm_update_sched(sc, ring->qid, ring->cur, 0, 0);

	/* Kick command ring. */
	ring->queued++;
	ring->cur = (ring->cur + 1) % IWM_TX_RING_COUNT;
	IWM_WRITE(sc, IWM_HBUS_TARG_WRPTR, ring->qid << 8 | ring->cur);

	if (!async) {
		err = tsleep_nsec(desc, PCATCH, "iwmcmd", SEC_TO_NSEC(1));
		if (err == 0) {
			/* if hardware is no longer up, return error */
			if (generation != sc->sc_generation) {
				err = ENXIO;
				goto out;
			}

			/* Response buffer will be freed in iwm_free_resp(). */
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
iwm_send_cmd_pdu(struct iwm_softc *sc, uint32_t id, uint32_t flags,
    uint16_t len, const void *data)
{
	struct iwm_host_cmd cmd = {
		.id = id,
		.len = { len, },
		.data = { data, },
		.flags = flags,
	};

	return iwm_send_cmd(sc, &cmd);
}

int
iwm_send_cmd_status(struct iwm_softc *sc, struct iwm_host_cmd *cmd,
    uint32_t *status)
{
	struct iwm_rx_packet *pkt;
	struct iwm_cmd_response *resp;
	int err, resp_len;

	KASSERT((cmd->flags & IWM_CMD_WANT_RESP) == 0);
	cmd->flags |= IWM_CMD_WANT_RESP;
	cmd->resp_pkt_len = sizeof(*pkt) + sizeof(*resp);

	err = iwm_send_cmd(sc, cmd);
	if (err)
		return err;

	pkt = cmd->resp_pkt;
	if (pkt == NULL || (pkt->hdr.flags & IWM_CMD_FAILED_MSK))
		return EIO;

	resp_len = iwm_rx_packet_payload_len(pkt);
	if (resp_len != sizeof(*resp)) {
		iwm_free_resp(sc, cmd);
		return EIO;
	}

	resp = (void *)pkt->data;
	*status = le32toh(resp->status);
	iwm_free_resp(sc, cmd);
	return err;
}

int
iwm_send_cmd_pdu_status(struct iwm_softc *sc, uint32_t id, uint16_t len,
    const void *data, uint32_t *status)
{
	struct iwm_host_cmd cmd = {
		.id = id,
		.len = { len, },
		.data = { data, },
	};

	return iwm_send_cmd_status(sc, &cmd, status);
}

void
iwm_free_resp(struct iwm_softc *sc, struct iwm_host_cmd *hcmd)
{
	KASSERT((hcmd->flags & (IWM_CMD_WANT_RESP)) == IWM_CMD_WANT_RESP);
	free(hcmd->resp_pkt, M_DEVBUF, hcmd->resp_pkt_len);
	hcmd->resp_pkt = NULL;
}

void
iwm_cmd_done(struct iwm_softc *sc, int qid, int idx, int code)
{
	struct iwm_tx_ring *ring = &sc->txq[sc->cmdqid];
	struct iwm_tx_data *data;

	if (qid != sc->cmdqid) {
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

	if (ring->queued == 0) {
		DPRINTF(("%s: unexpected firmware response to command 0x%x\n",
		    DEVNAME(sc), code));
	} else if (--ring->queued == 0) {
		/* 
		 * 7000 family NICs are locked while commands are in progress.
		 * All commands are now done so we may unlock the NIC again.
		 */
		if (sc->sc_device_family == IWM_DEVICE_FAMILY_7000)
			iwm_nic_unlock(sc);
	}
}

void
iwm_update_sched(struct iwm_softc *sc, int qid, int idx, uint8_t sta_id,
    uint16_t len)
{
	struct iwm_agn_scd_bc_tbl *scd_bc_tbl;
	uint16_t val;

	scd_bc_tbl = sc->sched_dma.vaddr;

	len += IWM_TX_CRC_SIZE + IWM_TX_DELIMITER_SIZE;
	if (sc->sc_capaflags & IWM_UCODE_TLV_FLAGS_DW_BC_TABLE)
		len = roundup(len, 4) / 4;

	val = htole16(sta_id << 12 | len);

	bus_dmamap_sync(sc->sc_dmat, sc->sched_dma.map,
	    0, sc->sched_dma.size, BUS_DMASYNC_PREWRITE);

	/* Update TX scheduler. */
	scd_bc_tbl[qid].tfd_offset[idx] = val;
	if (idx < IWM_TFD_QUEUE_SIZE_BC_DUP)
		scd_bc_tbl[qid].tfd_offset[IWM_TFD_QUEUE_SIZE_MAX + idx] = val;
	bus_dmamap_sync(sc->sc_dmat, sc->sched_dma.map,
	    0, sc->sched_dma.size, BUS_DMASYNC_POSTWRITE);
}

void
iwm_reset_sched(struct iwm_softc *sc, int qid, int idx, uint8_t sta_id)
{
	struct iwm_agn_scd_bc_tbl *scd_bc_tbl;
	uint16_t val;

	scd_bc_tbl = sc->sched_dma.vaddr;

	val = htole16(1 | (sta_id << 12));

	bus_dmamap_sync(sc->sc_dmat, sc->sched_dma.map,
	    0, sc->sched_dma.size, BUS_DMASYNC_PREWRITE);

	/* Update TX scheduler. */
	scd_bc_tbl[qid].tfd_offset[idx] = val;
	if (idx < IWM_TFD_QUEUE_SIZE_BC_DUP)
		scd_bc_tbl[qid].tfd_offset[IWM_TFD_QUEUE_SIZE_MAX + idx] = val;

	bus_dmamap_sync(sc->sc_dmat, sc->sched_dma.map,
	    0, sc->sched_dma.size, BUS_DMASYNC_POSTWRITE);
}

/*
 * Fill in various bit for management frames, and leave them
 * unfilled for data frames (firmware takes care of that).
 * Return the selected legacy TX rate, or zero if HT/VHT is used.
 */
uint8_t
iwm_tx_fill_cmd(struct iwm_softc *sc, struct iwm_node *in,
    struct ieee80211_frame *wh, struct iwm_tx_cmd *tx)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ieee80211_node *ni = &in->in_ni;
	const struct iwm_rate *rinfo;
	int type = wh->i_fc[0] & IEEE80211_FC0_TYPE_MASK;
	int min_ridx = iwm_rval2ridx(ieee80211_min_basic_rate(ic));
	int ridx, rate_flags;
	uint8_t rate = 0;

	tx->rts_retry_limit = IWM_RTS_DFAULT_RETRY_LIMIT;
	tx->data_retry_limit = IWM_LOW_RETRY_LIMIT;

	if (IEEE80211_IS_MULTICAST(wh->i_addr1) ||
	    type != IEEE80211_FC0_TYPE_DATA) {
		/* for non-data, use the lowest supported rate */
		ridx = min_ridx;
		tx->data_retry_limit = IWM_MGMT_DFAULT_RETRY_LIMIT;
	} else if (ic->ic_fixed_mcs != -1) {
		if (ni->ni_flags & IEEE80211_NODE_VHT)
			ridx = IWM_FIRST_OFDM_RATE;
		else
			ridx = sc->sc_fixed_ridx;
	} else if (ic->ic_fixed_rate != -1) {
		ridx = sc->sc_fixed_ridx;
 	} else {
		int i;
		/* Use firmware rateset retry table. */
		tx->initial_rate_index = 0;
		tx->tx_flags |= htole32(IWM_TX_CMD_FLG_STA_RATE);
		if (ni->ni_flags & IEEE80211_NODE_HT) /* VHT implies HT */
			return 0;
		ridx = (IEEE80211_IS_CHAN_5GHZ(ni->ni_chan)) ?
		    IWM_RIDX_OFDM : IWM_RIDX_CCK;
		for (i = 0; i < ni->ni_rates.rs_nrates; i++) {
			if (iwm_rates[i].rate == (ni->ni_txrate &
			    IEEE80211_RATE_VAL)) {
				ridx = i;
				break;
			}
		}
		return iwm_rates[ridx].rate & 0xff;
	}

	rinfo = &iwm_rates[ridx];
	if ((ni->ni_flags & IEEE80211_NODE_VHT) == 0 &&
	    iwm_is_mimo_ht_plcp(rinfo->ht_plcp))
		rate_flags = IWM_RATE_MCS_ANT_AB_MSK;
	else
		rate_flags = iwm_valid_siso_ant_rate_mask(sc);
	if (IWM_RIDX_IS_CCK(ridx))
		rate_flags |= IWM_RATE_MCS_CCK_MSK;
	if ((ni->ni_flags & IEEE80211_NODE_HT) &&
	    type == IEEE80211_FC0_TYPE_DATA &&
	    rinfo->ht_plcp != IWM_RATE_HT_SISO_MCS_INV_PLCP) {
		uint8_t sco = IEEE80211_HTOP0_SCO_SCN;
		uint8_t vht_chan_width = IEEE80211_VHTOP0_CHAN_WIDTH_HT;
		if ((ni->ni_flags & IEEE80211_NODE_VHT) &&
		    IEEE80211_CHAN_80MHZ_ALLOWED(ni->ni_chan) &&
		    ieee80211_node_supports_vht_chan80(ni))
			vht_chan_width = IEEE80211_VHTOP0_CHAN_WIDTH_80;
		else if (IEEE80211_CHAN_40MHZ_ALLOWED(ni->ni_chan) &&
		    ieee80211_node_supports_ht_chan40(ni))
			sco = (ni->ni_htop0 & IEEE80211_HTOP0_SCO_MASK);
		if (ni->ni_flags & IEEE80211_NODE_VHT)
			rate_flags |= IWM_RATE_MCS_VHT_MSK; 
		else
			rate_flags |= IWM_RATE_MCS_HT_MSK; 
		if (vht_chan_width == IEEE80211_VHTOP0_CHAN_WIDTH_80 &&
		    in->in_phyctxt != NULL &&
		    in->in_phyctxt->vht_chan_width == vht_chan_width) {
			rate_flags |= IWM_RATE_MCS_CHAN_WIDTH_80;
			if (ieee80211_node_supports_vht_sgi80(ni))
				rate_flags |= IWM_RATE_MCS_SGI_MSK;
		} else if ((sco == IEEE80211_HTOP0_SCO_SCA || 
		    sco == IEEE80211_HTOP0_SCO_SCB) &&
		    in->in_phyctxt != NULL && in->in_phyctxt->sco == sco) {
			rate_flags |= IWM_RATE_MCS_CHAN_WIDTH_40;
			if (ieee80211_node_supports_ht_sgi40(ni))
				rate_flags |= IWM_RATE_MCS_SGI_MSK;
		} else if (ieee80211_node_supports_ht_sgi20(ni))
			rate_flags |= IWM_RATE_MCS_SGI_MSK;
		if (ni->ni_flags & IEEE80211_NODE_VHT) {
			/*
			 * ifmedia only provides an MCS index, no NSS.
			 * Use a fixed SISO rate.
			 */
			tx->rate_n_flags = htole32(rate_flags |
			    (ic->ic_fixed_mcs &
			    IWM_RATE_VHT_MCS_RATE_CODE_MSK));
		} else
			tx->rate_n_flags = htole32(rate_flags | rinfo->ht_plcp);
	} else
		tx->rate_n_flags = htole32(rate_flags | rinfo->plcp);

	return rate;
}

#define TB0_SIZE 16
int
iwm_tx(struct iwm_softc *sc, struct mbuf *m, struct ieee80211_node *ni, int ac)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct iwm_node *in = (void *)ni;
	struct iwm_tx_ring *ring;
	struct iwm_tx_data *data;
	struct iwm_tfd *desc;
	struct iwm_device_cmd *cmd;
	struct iwm_tx_cmd *tx;
	struct ieee80211_frame *wh;
	struct ieee80211_key *k = NULL;
	uint8_t rate;
	uint8_t *ivp;
	uint32_t flags;
	u_int hdrlen;
	bus_dma_segment_t *seg;
	uint8_t tid, type, subtype;
	int i, totlen, err, pad;
	int qid, hasqos;

	wh = mtod(m, struct ieee80211_frame *);
	type = wh->i_fc[0] & IEEE80211_FC0_TYPE_MASK;
	subtype = wh->i_fc[0] & IEEE80211_FC0_SUBTYPE_MASK;
	if (type == IEEE80211_FC0_TYPE_CTL)
		hdrlen = sizeof(struct ieee80211_frame_min);
	else
		hdrlen = ieee80211_get_hdrlen(wh);

	hasqos = ieee80211_has_qos(wh);
	if (type == IEEE80211_FC0_TYPE_DATA)
		tid = IWM_TID_NON_QOS;
	else
		tid = IWM_MAX_TID_COUNT;

	/*
	 * Map EDCA categories to Tx data queues.
	 *
	 * We use static data queue assignments even in DQA mode. We do not
	 * need to share Tx queues between stations because we only implement
	 * client mode; the firmware's station table contains only one entry
	 * which represents our access point.
	 */
	if (ic->ic_opmode == IEEE80211_M_MONITOR) {
		if (isset(sc->sc_enabled_capa, IWM_UCODE_TLV_CAPA_DQA_SUPPORT))
			qid = IWM_DQA_INJECT_MONITOR_QUEUE;
		else
			qid = IWM_AUX_QUEUE;
	} else if (isset(sc->sc_enabled_capa, IWM_UCODE_TLV_CAPA_DQA_SUPPORT))
		qid = IWM_DQA_MIN_MGMT_QUEUE + ac;
	else
		qid = ac;

	/* If possible, put this frame on an aggregation queue. */
	if (hasqos) {
		struct ieee80211_tx_ba *ba;
		uint16_t qos = ieee80211_get_qos(wh);
		int qostid = qos & IEEE80211_QOS_TID;
		int agg_qid = IWM_FIRST_AGG_TX_QUEUE + qostid;

		ba = &ni->ni_tx_ba[qostid];
		if (!IEEE80211_IS_MULTICAST(wh->i_addr1) &&
		    type == IEEE80211_FC0_TYPE_DATA &&
		    subtype != IEEE80211_FC0_SUBTYPE_NODATA &&
		    (sc->tx_ba_queue_mask & (1 << agg_qid)) &&
		    ba->ba_state == IEEE80211_BA_AGREED) {
			qid = agg_qid;
			tid = qostid;
			ac = ieee80211_up_to_ac(ic, qostid);
		}
	}

	ring = &sc->txq[qid];
	desc = &ring->desc[ring->cur];
	memset(desc, 0, sizeof(*desc));
	data = &ring->data[ring->cur];

	cmd = &ring->cmd[ring->cur];
	cmd->hdr.code = IWM_TX_CMD;
	cmd->hdr.flags = 0;
	cmd->hdr.qid = ring->qid;
	cmd->hdr.idx = ring->cur;

	tx = (void *)cmd->data;
	memset(tx, 0, sizeof(*tx));

	rate = iwm_tx_fill_cmd(sc, in, wh, tx);

#if NBPFILTER > 0
	if (sc->sc_drvbpf != NULL) {
		struct iwm_tx_radiotap_header *tap = &sc->sc_txtap;
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
		    type == IEEE80211_FC0_TYPE_DATA) {
			tap->wt_rate = (0x80 | ni->ni_txmcs);
		} else
			tap->wt_rate = rate;
		if ((ic->ic_flags & IEEE80211_F_WEPON) &&
		    (wh->i_fc[1] & IEEE80211_FC1_PROTECTED))
			tap->wt_flags |= IEEE80211_RADIOTAP_F_WEP;

		bpf_mtap_hdr(sc->sc_drvbpf, tap, sc->sc_txtap_len,
		    m, BPF_DIRECTION_OUT);
	}
#endif
	totlen = m->m_pkthdr.len;

	if (ic->ic_opmode != IEEE80211_M_MONITOR &&
	    (wh->i_fc[1] & IEEE80211_FC1_PROTECTED)) {
		k = ieee80211_get_txkey(ic, wh, ni);
		if ((k->k_flags & IEEE80211_KEY_GROUP) ||
		    (k->k_cipher != IEEE80211_CIPHER_CCMP)) {
			if ((m = ieee80211_encrypt(ic, m, k)) == NULL)
				return ENOBUFS;
			/* 802.11 header may have moved. */
			wh = mtod(m, struct ieee80211_frame *);
			totlen = m->m_pkthdr.len;
			k = NULL; /* skip hardware crypto below */
		} else {
			/* HW appends CCMP MIC */
			totlen += IEEE80211_CCMP_HDRLEN;
		}
	}

	flags = 0;
	if (!IEEE80211_IS_MULTICAST(wh->i_addr1)) {
		flags |= IWM_TX_CMD_FLG_ACK;
	}

	if (type == IEEE80211_FC0_TYPE_DATA &&
	    !IEEE80211_IS_MULTICAST(wh->i_addr1) &&
	    (totlen + IEEE80211_CRC_LEN > ic->ic_rtsthreshold ||
	    (ic->ic_flags & IEEE80211_F_USEPROT)))
		flags |= IWM_TX_CMD_FLG_PROT_REQUIRE;

	if (ic->ic_opmode == IEEE80211_M_MONITOR)
		tx->sta_id = IWM_MONITOR_STA_ID;
	else
		tx->sta_id = IWM_STATION_ID;

	if (type == IEEE80211_FC0_TYPE_MGT) {
		if (subtype == IEEE80211_FC0_SUBTYPE_ASSOC_REQ ||
		    subtype == IEEE80211_FC0_SUBTYPE_REASSOC_REQ)
			tx->pm_frame_timeout = htole16(3);
		else
			tx->pm_frame_timeout = htole16(2);
	} else {
		if (type == IEEE80211_FC0_TYPE_CTL &&
		    subtype == IEEE80211_FC0_SUBTYPE_BAR) {
			struct ieee80211_frame_min *mwh;
			uint8_t *barfrm;
			uint16_t ctl;
			mwh = mtod(m, struct ieee80211_frame_min *);
			barfrm = (uint8_t *)&mwh[1];
			ctl = LE_READ_2(barfrm);
			tid = (ctl & IEEE80211_BA_TID_INFO_MASK) >>
			    IEEE80211_BA_TID_INFO_SHIFT;
			flags |= IWM_TX_CMD_FLG_ACK | IWM_TX_CMD_FLG_BAR;
			tx->data_retry_limit = IWM_BAR_DFAULT_RETRY_LIMIT;
		}

		tx->pm_frame_timeout = htole16(0);
	}

	if (hdrlen & 3) {
		/* First segment length must be a multiple of 4. */
		flags |= IWM_TX_CMD_FLG_MH_PAD;
		tx->offload_assist |= htole16(IWM_TX_CMD_OFFLD_PAD);
		pad = 4 - (hdrlen & 3);
	} else
		pad = 0;

	tx->len = htole16(totlen);
	tx->tid_tspec = tid;
	tx->life_time = htole32(IWM_TX_CMD_LIFE_TIME_INFINITE);

	/* Set physical address of "scratch area". */
	tx->dram_lsb_ptr = htole32(data->scratch_paddr);
	tx->dram_msb_ptr = iwm_get_dma_hi_addr(data->scratch_paddr);

	/* Copy 802.11 header in TX command. */
	memcpy(((uint8_t *)tx) + sizeof(*tx), wh, hdrlen);

	if  (k != NULL && k->k_cipher == IEEE80211_CIPHER_CCMP) {
		/* Trim 802.11 header and prepend CCMP IV. */
		m_adj(m, hdrlen - IEEE80211_CCMP_HDRLEN);
		ivp = mtod(m, u_int8_t *);
		k->k_tsc++;	/* increment the 48-bit PN */
		ivp[0] = k->k_tsc; /* PN0 */
		ivp[1] = k->k_tsc >> 8; /* PN1 */
		ivp[2] = 0;        /* Rsvd */
		ivp[3] = k->k_id << 6 | IEEE80211_WEP_EXTIV;
		ivp[4] = k->k_tsc >> 16; /* PN2 */
		ivp[5] = k->k_tsc >> 24; /* PN3 */
		ivp[6] = k->k_tsc >> 32; /* PN4 */
		ivp[7] = k->k_tsc >> 40; /* PN5 */

		tx->sec_ctl = IWM_TX_CMD_SEC_CCM;
		memcpy(tx->key, k->k_key, MIN(sizeof(tx->key), k->k_len));
		/* TX scheduler includes CCMP MIC length. */
		totlen += IEEE80211_CCMP_MICLEN;
	} else {
		/* Trim 802.11 header. */
		m_adj(m, hdrlen);
		tx->sec_ctl = 0;
	}

	flags |= IWM_TX_CMD_FLG_BT_DIS;
	if (!hasqos)
		flags |= IWM_TX_CMD_FLG_SEQ_CTL;

	tx->tx_flags |= htole32(flags);

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
	data->txmcs = ni->ni_txmcs;
	data->txrate = ni->ni_txrate;
	data->ampdu_txmcs = ni->ni_txmcs; /* updated upon Tx interrupt */
	data->ampdu_txnss = ni->ni_vht_ss; /* updated upon Tx interrupt */

	/* Fill TX descriptor. */
	desc->num_tbs = 2 + data->map->dm_nsegs;

	desc->tbs[0].lo = htole32(data->cmd_paddr);
	desc->tbs[0].hi_n_len = htole16(iwm_get_dma_hi_addr(data->cmd_paddr) |
	    (TB0_SIZE << 4));
	desc->tbs[1].lo = htole32(data->cmd_paddr + TB0_SIZE);
	desc->tbs[1].hi_n_len = htole16(iwm_get_dma_hi_addr(data->cmd_paddr) |
	    ((sizeof(struct iwm_cmd_header) + sizeof(*tx)
	      + hdrlen + pad - TB0_SIZE) << 4));

	/* Other DMA segments are for data payload. */
	seg = data->map->dm_segs;
	for (i = 0; i < data->map->dm_nsegs; i++, seg++) {
		desc->tbs[i+2].lo = htole32(seg->ds_addr);
		desc->tbs[i+2].hi_n_len = \
		    htole16(iwm_get_dma_hi_addr(seg->ds_addr)
		    | ((seg->ds_len) << 4));
	}

	bus_dmamap_sync(sc->sc_dmat, data->map, 0, data->map->dm_mapsize,
	    BUS_DMASYNC_PREWRITE);
	bus_dmamap_sync(sc->sc_dmat, ring->cmd_dma.map,
	    (char *)(void *)cmd - (char *)(void *)ring->cmd_dma.vaddr,
	    sizeof (*cmd), BUS_DMASYNC_PREWRITE);
	bus_dmamap_sync(sc->sc_dmat, ring->desc_dma.map,
	    (char *)(void *)desc - (char *)(void *)ring->desc_dma.vaddr,
	    sizeof (*desc), BUS_DMASYNC_PREWRITE);

	iwm_update_sched(sc, ring->qid, ring->cur, tx->sta_id, totlen);

	/* Kick TX ring. */
	ring->cur = (ring->cur + 1) % IWM_TX_RING_COUNT;
	IWM_WRITE(sc, IWM_HBUS_TARG_WRPTR, ring->qid << 8 | ring->cur);

	/* Mark TX ring as full if we reach a certain threshold. */
	if (++ring->queued > IWM_TX_RING_HIMARK) {
		sc->qfullmsk |= 1 << ring->qid;
	}

	if (ic->ic_if.if_flags & IFF_UP)
		sc->sc_tx_timer[ring->qid] = 15;

	return 0;
}

int
iwm_flush_tx_path(struct iwm_softc *sc, int tfd_queue_msk)
{
	struct iwm_tx_path_flush_cmd flush_cmd = {
		.sta_id = htole32(IWM_STATION_ID),
		.tid_mask = htole16(0xffff),
	};
	int err;

	err = iwm_send_cmd_pdu(sc, IWM_TXPATH_FLUSH, 0,
	    sizeof(flush_cmd), &flush_cmd);
	if (err)
                printf("%s: Flushing tx queue failed: %d\n", DEVNAME(sc), err);
	return err;
}

#define IWM_FLUSH_WAIT_MS	2000

int
iwm_wait_tx_queues_empty(struct iwm_softc *sc)
{
	int i, err;

	for (i = 0; i < IWM_MAX_QUEUES; i++) {
		struct iwm_tx_ring *ring = &sc->txq[i];

		if (i == sc->cmdqid)
			continue;

		while (ring->queued > 0) {
			err = tsleep_nsec(ring, 0, "iwmflush",
			    MSEC_TO_NSEC(IWM_FLUSH_WAIT_MS));
			if (err)
				return err;
		}
	}

	return 0;
}

void
iwm_led_enable(struct iwm_softc *sc)
{
	IWM_WRITE(sc, IWM_CSR_LED_REG, IWM_CSR_LED_REG_TURN_ON);
}

void
iwm_led_disable(struct iwm_softc *sc)
{
	IWM_WRITE(sc, IWM_CSR_LED_REG, IWM_CSR_LED_REG_TURN_OFF);
}

int
iwm_led_is_enabled(struct iwm_softc *sc)
{
	return (IWM_READ(sc, IWM_CSR_LED_REG) == IWM_CSR_LED_REG_TURN_ON);
}

#define IWM_LED_BLINK_TIMEOUT_MSEC    200

void
iwm_led_blink_timeout(void *arg)
{
	struct iwm_softc *sc = arg;

	if (iwm_led_is_enabled(sc))
		iwm_led_disable(sc);
	else
		iwm_led_enable(sc);

	timeout_add_msec(&sc->sc_led_blink_to, IWM_LED_BLINK_TIMEOUT_MSEC);
}

void
iwm_led_blink_start(struct iwm_softc *sc)
{
	timeout_add_msec(&sc->sc_led_blink_to, IWM_LED_BLINK_TIMEOUT_MSEC);
	iwm_led_enable(sc);
}

void
iwm_led_blink_stop(struct iwm_softc *sc)
{
	timeout_del(&sc->sc_led_blink_to);
	iwm_led_disable(sc);
}

#define IWM_POWER_KEEP_ALIVE_PERIOD_SEC    25

int
iwm_beacon_filter_send_cmd(struct iwm_softc *sc,
    struct iwm_beacon_filter_cmd *cmd)
{
	return iwm_send_cmd_pdu(sc, IWM_REPLY_BEACON_FILTERING_CMD,
	    0, sizeof(struct iwm_beacon_filter_cmd), cmd);
}

void
iwm_beacon_filter_set_cqm_params(struct iwm_softc *sc, struct iwm_node *in,
    struct iwm_beacon_filter_cmd *cmd)
{
	cmd->ba_enable_beacon_abort = htole32(sc->sc_bf.ba_enabled);
}

int
iwm_update_beacon_abort(struct iwm_softc *sc, struct iwm_node *in, int enable)
{
	struct iwm_beacon_filter_cmd cmd = {
		IWM_BF_CMD_CONFIG_DEFAULTS,
		.bf_enable_beacon_filter = htole32(1),
		.ba_enable_beacon_abort = htole32(enable),
	};

	if (!sc->sc_bf.bf_enabled)
		return 0;

	sc->sc_bf.ba_enabled = enable;
	iwm_beacon_filter_set_cqm_params(sc, in, &cmd);
	return iwm_beacon_filter_send_cmd(sc, &cmd);
}

void
iwm_power_build_cmd(struct iwm_softc *sc, struct iwm_node *in,
    struct iwm_mac_power_cmd *cmd)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ieee80211_node *ni = &in->in_ni;
	int dtim_period, dtim_msec, keep_alive;

	cmd->id_and_color = htole32(IWM_FW_CMD_ID_AND_COLOR(in->in_id,
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
	keep_alive = MAX(3 * dtim_msec, 1000 * IWM_POWER_KEEP_ALIVE_PERIOD_SEC);
	keep_alive = roundup(keep_alive, 1000) / 1000;
	cmd->keep_alive_seconds = htole16(keep_alive);

	if (ic->ic_opmode != IEEE80211_M_MONITOR)
		cmd->flags = htole16(IWM_POWER_FLAGS_POWER_SAVE_ENA_MSK);
}

int
iwm_power_mac_update_mode(struct iwm_softc *sc, struct iwm_node *in)
{
	int err;
	int ba_enable;
	struct iwm_mac_power_cmd cmd;

	memset(&cmd, 0, sizeof(cmd));

	iwm_power_build_cmd(sc, in, &cmd);

	err = iwm_send_cmd_pdu(sc, IWM_MAC_PM_POWER_TABLE, 0,
	    sizeof(cmd), &cmd);
	if (err != 0)
		return err;

	ba_enable = !!(cmd.flags &
	    htole16(IWM_POWER_FLAGS_POWER_MANAGEMENT_ENA_MSK));
	return iwm_update_beacon_abort(sc, in, ba_enable);
}

int
iwm_power_update_device(struct iwm_softc *sc)
{
	struct iwm_device_power_cmd cmd = { };
	struct ieee80211com *ic = &sc->sc_ic;

	if (ic->ic_opmode != IEEE80211_M_MONITOR)
		cmd.flags = htole16(IWM_DEVICE_POWER_FLAGS_POWER_SAVE_ENA_MSK);

	return iwm_send_cmd_pdu(sc,
	    IWM_POWER_TABLE_CMD, 0, sizeof(cmd), &cmd);
}

int
iwm_enable_beacon_filter(struct iwm_softc *sc, struct iwm_node *in)
{
	struct iwm_beacon_filter_cmd cmd = {
		IWM_BF_CMD_CONFIG_DEFAULTS,
		.bf_enable_beacon_filter = htole32(1),
	};
	int err;

	iwm_beacon_filter_set_cqm_params(sc, in, &cmd);
	err = iwm_beacon_filter_send_cmd(sc, &cmd);

	if (err == 0)
		sc->sc_bf.bf_enabled = 1;

	return err;
}

int
iwm_disable_beacon_filter(struct iwm_softc *sc)
{
	struct iwm_beacon_filter_cmd cmd;
	int err;

	memset(&cmd, 0, sizeof(cmd));

	err = iwm_beacon_filter_send_cmd(sc, &cmd);
	if (err == 0)
		sc->sc_bf.bf_enabled = 0;

	return err;
}

int
iwm_add_sta_cmd(struct iwm_softc *sc, struct iwm_node *in, int update)
{
	struct iwm_add_sta_cmd add_sta_cmd;
	int err;
	uint32_t status, aggsize;
	const uint32_t max_aggsize = (IWM_STA_FLG_MAX_AGG_SIZE_64K >>
		    IWM_STA_FLG_MAX_AGG_SIZE_SHIFT);
	size_t cmdsize;
	struct ieee80211com *ic = &sc->sc_ic;

	if (!update && (sc->sc_flags & IWM_FLAG_STA_ACTIVE))
		panic("STA already added");

	memset(&add_sta_cmd, 0, sizeof(add_sta_cmd));

	if (ic->ic_opmode == IEEE80211_M_MONITOR)
		add_sta_cmd.sta_id = IWM_MONITOR_STA_ID;
	else
		add_sta_cmd.sta_id = IWM_STATION_ID;
	if (isset(sc->sc_ucode_api, IWM_UCODE_TLV_API_STA_TYPE)) {
		if (ic->ic_opmode == IEEE80211_M_MONITOR)
			add_sta_cmd.station_type = IWM_STA_GENERAL_PURPOSE;
		else
			add_sta_cmd.station_type = IWM_STA_LINK;
	}
	add_sta_cmd.mac_id_n_color
	    = htole32(IWM_FW_CMD_ID_AND_COLOR(in->in_id, in->in_color));
	if (ic->ic_opmode == IEEE80211_M_MONITOR) {
		int qid;
		IEEE80211_ADDR_COPY(&add_sta_cmd.addr, etheranyaddr);
		if (isset(sc->sc_enabled_capa, IWM_UCODE_TLV_CAPA_DQA_SUPPORT))
			qid = IWM_DQA_INJECT_MONITOR_QUEUE;
		else
			qid = IWM_AUX_QUEUE;
		in->tfd_queue_msk |= (1 << qid);
	} else {
		int ac;
		for (ac = 0; ac < EDCA_NUM_AC; ac++) {
			int qid = ac;
			if (isset(sc->sc_enabled_capa,
			    IWM_UCODE_TLV_CAPA_DQA_SUPPORT))
				qid += IWM_DQA_MIN_MGMT_QUEUE;
			in->tfd_queue_msk |= (1 << qid);
		}
	}
	if (!update) {
		if (ic->ic_opmode == IEEE80211_M_MONITOR)
			IEEE80211_ADDR_COPY(&add_sta_cmd.addr,
			    etherbroadcastaddr);
		else
			IEEE80211_ADDR_COPY(&add_sta_cmd.addr,
			    in->in_macaddr);
	}
	add_sta_cmd.add_modify = update ? 1 : 0;
	add_sta_cmd.station_flags_msk
	    |= htole32(IWM_STA_FLG_FAT_EN_MSK | IWM_STA_FLG_MIMO_EN_MSK);
	if (update) {
		add_sta_cmd.modify_mask |= (IWM_STA_MODIFY_QUEUES |
		    IWM_STA_MODIFY_TID_DISABLE_TX);
	}
	add_sta_cmd.tid_disable_tx = htole16(in->tid_disable_ampdu);
	add_sta_cmd.tfd_queue_msk = htole32(in->tfd_queue_msk);

	if (in->in_ni.ni_flags & IEEE80211_NODE_HT) {
		add_sta_cmd.station_flags_msk
		    |= htole32(IWM_STA_FLG_MAX_AGG_SIZE_MSK |
		    IWM_STA_FLG_AGG_MPDU_DENS_MSK);

		if (iwm_mimo_enabled(sc)) {
			if (in->in_ni.ni_flags & IEEE80211_NODE_VHT) {
				uint16_t rx_mcs = (in->in_ni.ni_vht_rxmcs &
				    IEEE80211_VHT_MCS_FOR_SS_MASK(2)) >>
				    IEEE80211_VHT_MCS_FOR_SS_SHIFT(2);
				if (rx_mcs != IEEE80211_VHT_MCS_SS_NOT_SUPP) {
					add_sta_cmd.station_flags |=
					    htole32(IWM_STA_FLG_MIMO_EN_MIMO2);
				}
			} else {
				if (in->in_ni.ni_rxmcs[1] != 0) {
					add_sta_cmd.station_flags |=
					    htole32(IWM_STA_FLG_MIMO_EN_MIMO2);
				}
				if (in->in_ni.ni_rxmcs[2] != 0) {
					add_sta_cmd.station_flags |=
					    htole32(IWM_STA_FLG_MIMO_EN_MIMO3);
				}
			}
		}

		if (IEEE80211_CHAN_40MHZ_ALLOWED(in->in_ni.ni_chan) &&
		    ieee80211_node_supports_ht_chan40(&in->in_ni)) {
			add_sta_cmd.station_flags |= htole32(
			    IWM_STA_FLG_FAT_EN_40MHZ);
		}

		if (in->in_ni.ni_flags & IEEE80211_NODE_VHT) {
			if (IEEE80211_CHAN_80MHZ_ALLOWED(in->in_ni.ni_chan) &&
			    ieee80211_node_supports_vht_chan80(&in->in_ni)) {
				add_sta_cmd.station_flags |= htole32(
				    IWM_STA_FLG_FAT_EN_80MHZ);
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
		    IWM_STA_FLG_MAX_AGG_SIZE_SHIFT) &
		    IWM_STA_FLG_MAX_AGG_SIZE_MSK);

		switch (in->in_ni.ni_ampdu_param & IEEE80211_AMPDU_PARAM_SS) {
		case IEEE80211_AMPDU_PARAM_SS_2:
			add_sta_cmd.station_flags
			    |= htole32(IWM_STA_FLG_AGG_MPDU_DENS_2US);
			break;
		case IEEE80211_AMPDU_PARAM_SS_4:
			add_sta_cmd.station_flags
			    |= htole32(IWM_STA_FLG_AGG_MPDU_DENS_4US);
			break;
		case IEEE80211_AMPDU_PARAM_SS_8:
			add_sta_cmd.station_flags
			    |= htole32(IWM_STA_FLG_AGG_MPDU_DENS_8US);
			break;
		case IEEE80211_AMPDU_PARAM_SS_16:
			add_sta_cmd.station_flags
			    |= htole32(IWM_STA_FLG_AGG_MPDU_DENS_16US);
			break;
		default:
			break;
		}
	}

	status = IWM_ADD_STA_SUCCESS;
	if (isset(sc->sc_ucode_api, IWM_UCODE_TLV_API_STA_TYPE))
		cmdsize = sizeof(add_sta_cmd);
	else
		cmdsize = sizeof(struct iwm_add_sta_cmd_v7);
	err = iwm_send_cmd_pdu_status(sc, IWM_ADD_STA, cmdsize,
	    &add_sta_cmd, &status);
	if (!err && (status & IWM_ADD_STA_STATUS_MASK) != IWM_ADD_STA_SUCCESS)
		err = EIO;

	return err;
}

int
iwm_add_aux_sta(struct iwm_softc *sc)
{
	struct iwm_add_sta_cmd cmd;
	int err, qid;
	uint32_t status;
	size_t cmdsize;

	if (isset(sc->sc_enabled_capa, IWM_UCODE_TLV_CAPA_DQA_SUPPORT)) {
		qid = IWM_DQA_AUX_QUEUE;
		err = iwm_enable_txq(sc, IWM_AUX_STA_ID, qid,
		    IWM_TX_FIFO_MCAST, 0, IWM_MAX_TID_COUNT, 0);
	} else {
		qid = IWM_AUX_QUEUE;
		err = iwm_enable_ac_txq(sc, qid, IWM_TX_FIFO_MCAST);
	}
	if (err)
		return err;

	memset(&cmd, 0, sizeof(cmd));
	cmd.sta_id = IWM_AUX_STA_ID;
	if (isset(sc->sc_ucode_api, IWM_UCODE_TLV_API_STA_TYPE))
		cmd.station_type = IWM_STA_AUX_ACTIVITY;
	cmd.mac_id_n_color =
	    htole32(IWM_FW_CMD_ID_AND_COLOR(IWM_MAC_INDEX_AUX, 0));
	cmd.tfd_queue_msk = htole32(1 << qid);
	cmd.tid_disable_tx = htole16(0xffff);

	status = IWM_ADD_STA_SUCCESS;
	if (isset(sc->sc_ucode_api, IWM_UCODE_TLV_API_STA_TYPE))
		cmdsize = sizeof(cmd);
	else
		cmdsize = sizeof(struct iwm_add_sta_cmd_v7);
	err = iwm_send_cmd_pdu_status(sc, IWM_ADD_STA, cmdsize, &cmd,
	    &status);
	if (!err && (status & IWM_ADD_STA_STATUS_MASK) != IWM_ADD_STA_SUCCESS)
		err = EIO;

	return err;
}

int
iwm_drain_sta(struct iwm_softc *sc, struct iwm_node* in, int drain)
{
	struct iwm_add_sta_cmd cmd;
	int err;
	uint32_t status;
	size_t cmdsize;

	memset(&cmd, 0, sizeof(cmd));
	cmd.mac_id_n_color = htole32(IWM_FW_CMD_ID_AND_COLOR(in->in_id,
	    in->in_color));
	cmd.sta_id = IWM_STATION_ID;
	cmd.add_modify = IWM_STA_MODE_MODIFY;
	cmd.station_flags = drain ? htole32(IWM_STA_FLG_DRAIN_FLOW) : 0;
	cmd.station_flags_msk = htole32(IWM_STA_FLG_DRAIN_FLOW);

	if (isset(sc->sc_ucode_api, IWM_UCODE_TLV_API_STA_TYPE))
		cmdsize = sizeof(cmd);
	else
		cmdsize = sizeof(struct iwm_add_sta_cmd_v7);

	status = IWM_ADD_STA_SUCCESS;
	err = iwm_send_cmd_pdu_status(sc, IWM_ADD_STA,
	    cmdsize, &cmd, &status);
	if (err) {
		printf("%s: could not update sta (error %d)\n",
		    DEVNAME(sc), err);
		return err;
	}

	switch (status & IWM_ADD_STA_STATUS_MASK) {
	case IWM_ADD_STA_SUCCESS:
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
iwm_flush_sta(struct iwm_softc *sc, struct iwm_node *in)
{
	int err;

	sc->sc_flags |= IWM_FLAG_TXFLUSH;

	err = iwm_drain_sta(sc, in, 1);
	if (err)
		goto done;

	err = iwm_flush_tx_path(sc, in->tfd_queue_msk);
	if (err) {
		printf("%s: could not flush Tx path (error %d)\n",
		    DEVNAME(sc), err);
		goto done;
	}

	/*
	 * Flushing Tx rings may fail if the AP has disappeared.
	 * We can rely on iwm_newstate_task() to reset everything and begin
	 * scanning again if we are left with outstanding frames on queues.
	 */
	err = iwm_wait_tx_queues_empty(sc);
	if (err)
		goto done;

	err = iwm_drain_sta(sc, in, 0);
done:
	sc->sc_flags &= ~IWM_FLAG_TXFLUSH;
	return err;
}

int
iwm_rm_sta_cmd(struct iwm_softc *sc, struct iwm_node *in)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct iwm_rm_sta_cmd rm_sta_cmd;
	int err;

	if ((sc->sc_flags & IWM_FLAG_STA_ACTIVE) == 0)
		panic("sta already removed");

	memset(&rm_sta_cmd, 0, sizeof(rm_sta_cmd));
	if (ic->ic_opmode == IEEE80211_M_MONITOR)
		rm_sta_cmd.sta_id = IWM_MONITOR_STA_ID;
	else
		rm_sta_cmd.sta_id = IWM_STATION_ID;

	err = iwm_send_cmd_pdu(sc, IWM_REMOVE_STA, 0, sizeof(rm_sta_cmd),
	    &rm_sta_cmd);

	return err;
}

uint16_t
iwm_scan_rx_chain(struct iwm_softc *sc)
{
	uint16_t rx_chain;
	uint8_t rx_ant;

	rx_ant = iwm_fw_valid_rx_ant(sc);
	rx_chain = rx_ant << IWM_PHY_RX_CHAIN_VALID_POS;
	rx_chain |= rx_ant << IWM_PHY_RX_CHAIN_FORCE_MIMO_SEL_POS;
	rx_chain |= rx_ant << IWM_PHY_RX_CHAIN_FORCE_SEL_POS;
	rx_chain |= 0x1 << IWM_PHY_RX_CHAIN_DRIVER_FORCE_POS;
	return htole16(rx_chain);
}

uint32_t
iwm_scan_rate_n_flags(struct iwm_softc *sc, int flags, int no_cck)
{
	uint32_t tx_ant;
	int i, ind;

	for (i = 0, ind = sc->sc_scan_last_antenna;
	    i < IWM_RATE_MCS_ANT_NUM; i++) {
		ind = (ind + 1) % IWM_RATE_MCS_ANT_NUM;
		if (iwm_fw_valid_tx_ant(sc) & (1 << ind)) {
			sc->sc_scan_last_antenna = ind;
			break;
		}
	}
	tx_ant = (1 << sc->sc_scan_last_antenna) << IWM_RATE_MCS_ANT_POS;

	if ((flags & IEEE80211_CHAN_2GHZ) && !no_cck)
		return htole32(IWM_RATE_1M_PLCP | IWM_RATE_MCS_CCK_MSK |
				   tx_ant);
	else
		return htole32(IWM_RATE_6M_PLCP | tx_ant);
}

uint8_t
iwm_lmac_scan_fill_channels(struct iwm_softc *sc,
    struct iwm_scan_channel_cfg_lmac *chan, int n_ssids, int bgscan)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ieee80211_channel *c;
	uint8_t nchan;

	for (nchan = 0, c = &ic->ic_channels[1];
	    c <= &ic->ic_channels[IEEE80211_CHAN_MAX] &&
	    nchan < sc->sc_capa_n_scan_channels;
	    c++) {
		if (c->ic_flags == 0)
			continue;

		chan->channel_num = htole16(ieee80211_mhz2ieee(c->ic_freq, 0));
		chan->iter_count = htole16(1);
		chan->iter_interval = 0;
		chan->flags = htole32(IWM_UNIFIED_SCAN_CHANNEL_PARTIAL);
		if (n_ssids != 0 && !bgscan)
			chan->flags |= htole32(1 << 1); /* select SSID 0 */
		chan++;
		nchan++;
	}

	return nchan;
}

uint8_t
iwm_umac_scan_fill_channels(struct iwm_softc *sc,
    struct iwm_scan_channel_cfg_umac *chan, int n_ssids, int bgscan)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ieee80211_channel *c;
	uint8_t nchan;

	for (nchan = 0, c = &ic->ic_channels[1];
	    c <= &ic->ic_channels[IEEE80211_CHAN_MAX] &&
	    nchan < sc->sc_capa_n_scan_channels;
	    c++) {
		if (c->ic_flags == 0)
			continue;

		chan->channel_num = ieee80211_mhz2ieee(c->ic_freq, 0);
		chan->iter_count = 1;
		chan->iter_interval = htole16(0);
		if (n_ssids != 0 && !bgscan)
			chan->flags = htole32(1 << 0); /* select SSID 0 */
		chan++;
		nchan++;
	}

	return nchan;
}

int
iwm_fill_probe_req_v1(struct iwm_softc *sc, struct iwm_scan_probe_req_v1 *preq1)
{
	struct iwm_scan_probe_req preq2;
	int err, i;

	err = iwm_fill_probe_req(sc, &preq2);
	if (err)
		return err;

	preq1->mac_header = preq2.mac_header;
	for (i = 0; i < nitems(preq1->band_data); i++)
		preq1->band_data[i] = preq2.band_data[i];
	preq1->common_data = preq2.common_data;
	memcpy(preq1->buf, preq2.buf, sizeof(preq1->buf));
	return 0;
}

int
iwm_fill_probe_req(struct iwm_softc *sc, struct iwm_scan_probe_req *preq)
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

	/* Tell firmware where the MAC header and SSID IE are. */
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
	    IWM_UCODE_TLV_CAPA_DS_PARAM_SET_IE_SUPPORT)) {
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
iwm_lmac_scan(struct iwm_softc *sc, int bgscan)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct iwm_host_cmd hcmd = {
		.id = IWM_SCAN_OFFLOAD_REQUEST_CMD,
		.len = { 0, },
		.data = { NULL, },
		.flags = 0,
	};
	struct iwm_scan_req_lmac *req;
	struct iwm_scan_probe_req_v1 *preq;
	size_t req_len;
	int err, async = bgscan;

	req_len = sizeof(struct iwm_scan_req_lmac) +
	    (sizeof(struct iwm_scan_channel_cfg_lmac) *
	    sc->sc_capa_n_scan_channels) + sizeof(struct iwm_scan_probe_req_v1);
	if (req_len > IWM_MAX_CMD_PAYLOAD_SIZE)
		return ENOMEM;
	req = malloc(req_len, M_DEVBUF,
	    (async ? M_NOWAIT : M_WAIT) | M_CANFAIL | M_ZERO);
	if (req == NULL)
		return ENOMEM;

	hcmd.len[0] = (uint16_t)req_len;
	hcmd.data[0] = (void *)req;
	hcmd.flags |= async ? IWM_CMD_ASYNC : 0;

	/* These timings correspond to iwlwifi's UNASSOC scan. */
	req->active_dwell = 10;
	req->passive_dwell = 110;
	req->fragmented_dwell = 44;
	req->extended_dwell = 90;
	if (bgscan) {
		req->max_out_time = htole32(120);
		req->suspend_time = htole32(120);
	} else {
		req->max_out_time = htole32(0);
		req->suspend_time = htole32(0);
	}
	req->scan_prio = htole32(IWM_SCAN_PRIORITY_HIGH);
	req->rx_chain_select = iwm_scan_rx_chain(sc);
	req->iter_num = htole32(1);
	req->delay = 0;

	req->scan_flags = htole32(IWM_LMAC_SCAN_FLAG_PASS_ALL |
	    IWM_LMAC_SCAN_FLAG_ITER_COMPLETE |
	    IWM_LMAC_SCAN_FLAG_EXTENDED_DWELL);
	if (ic->ic_des_esslen == 0)
		req->scan_flags |= htole32(IWM_LMAC_SCAN_FLAG_PASSIVE);
	else
		req->scan_flags |=
		    htole32(IWM_LMAC_SCAN_FLAG_PRE_CONNECTION);
	if (isset(sc->sc_enabled_capa, 
	    IWM_UCODE_TLV_CAPA_DS_PARAM_SET_IE_SUPPORT) &&
	    isset(sc->sc_enabled_capa, 
	    IWM_UCODE_TLV_CAPA_WFA_TPC_REP_IE_SUPPORT))
		req->scan_flags |= htole32(IWM_LMAC_SCAN_FLAGS_RRM_ENABLED);

	req->flags = htole32(IWM_PHY_BAND_24);
	if (sc->sc_nvm.sku_cap_band_52GHz_enable)
		req->flags |= htole32(IWM_PHY_BAND_5);
	req->filter_flags =
	    htole32(IWM_MAC_FILTER_ACCEPT_GRP | IWM_MAC_FILTER_IN_BEACON);

	/* Tx flags 2 GHz. */
	req->tx_cmd[0].tx_flags = htole32(IWM_TX_CMD_FLG_SEQ_CTL |
	    IWM_TX_CMD_FLG_BT_DIS);
	req->tx_cmd[0].rate_n_flags =
	    iwm_scan_rate_n_flags(sc, IEEE80211_CHAN_2GHZ, 1/*XXX*/);
	req->tx_cmd[0].sta_id = IWM_AUX_STA_ID;

	/* Tx flags 5 GHz. */
	req->tx_cmd[1].tx_flags = htole32(IWM_TX_CMD_FLG_SEQ_CTL |
	    IWM_TX_CMD_FLG_BT_DIS);
	req->tx_cmd[1].rate_n_flags =
	    iwm_scan_rate_n_flags(sc, IEEE80211_CHAN_5GHZ, 1/*XXX*/);
	req->tx_cmd[1].sta_id = IWM_AUX_STA_ID;

	/* Check if we're doing an active directed scan. */
	if (ic->ic_des_esslen != 0) {
		req->direct_scan[0].id = IEEE80211_ELEMID_SSID;
		req->direct_scan[0].len = ic->ic_des_esslen;
		memcpy(req->direct_scan[0].ssid, ic->ic_des_essid,
		    ic->ic_des_esslen);
	}

	req->n_channels = iwm_lmac_scan_fill_channels(sc,
	    (struct iwm_scan_channel_cfg_lmac *)req->data,
	    ic->ic_des_esslen != 0, bgscan);

	preq = (struct iwm_scan_probe_req_v1 *)(req->data +
	    (sizeof(struct iwm_scan_channel_cfg_lmac) *
	    sc->sc_capa_n_scan_channels));
	err = iwm_fill_probe_req_v1(sc, preq);
	if (err) {
		free(req, M_DEVBUF, req_len);
		return err;
	}

	/* Specify the scan plan: We'll do one iteration. */
	req->schedule[0].iterations = 1;
	req->schedule[0].full_scan_mul = 1;

	/* Disable EBS. */
	req->channel_opt[0].non_ebs_ratio = 1;
	req->channel_opt[1].non_ebs_ratio = 1;

	err = iwm_send_cmd(sc, &hcmd);
	free(req, M_DEVBUF, req_len);
	return err;
}

int
iwm_config_umac_scan(struct iwm_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct iwm_scan_config *scan_config;
	int err, nchan;
	size_t cmd_size;
	struct ieee80211_channel *c;
	struct iwm_host_cmd hcmd = {
		.id = iwm_cmd_id(IWM_SCAN_CFG_CMD, IWM_LONG_GROUP, 0),
		.flags = 0,
	};
	static const uint32_t rates = (IWM_SCAN_CONFIG_RATE_1M |
	    IWM_SCAN_CONFIG_RATE_2M | IWM_SCAN_CONFIG_RATE_5M |
	    IWM_SCAN_CONFIG_RATE_11M | IWM_SCAN_CONFIG_RATE_6M |
	    IWM_SCAN_CONFIG_RATE_9M | IWM_SCAN_CONFIG_RATE_12M |
	    IWM_SCAN_CONFIG_RATE_18M | IWM_SCAN_CONFIG_RATE_24M |
	    IWM_SCAN_CONFIG_RATE_36M | IWM_SCAN_CONFIG_RATE_48M |
	    IWM_SCAN_CONFIG_RATE_54M);

	cmd_size = sizeof(*scan_config) + sc->sc_capa_n_scan_channels;

	scan_config = malloc(cmd_size, M_DEVBUF, M_WAIT | M_CANFAIL | M_ZERO);
	if (scan_config == NULL)
		return ENOMEM;

	scan_config->tx_chains = htole32(iwm_fw_valid_tx_ant(sc));
	scan_config->rx_chains = htole32(iwm_fw_valid_rx_ant(sc));
	scan_config->legacy_rates = htole32(rates |
	    IWM_SCAN_CONFIG_SUPPORTED_RATE(rates));

	/* These timings correspond to iwlwifi's UNASSOC scan. */
	scan_config->dwell_active = 10;
	scan_config->dwell_passive = 110;
	scan_config->dwell_fragmented = 44;
	scan_config->dwell_extended = 90;
	scan_config->out_of_channel_time = htole32(0);
	scan_config->suspend_time = htole32(0);

	IEEE80211_ADDR_COPY(scan_config->mac_addr, sc->sc_ic.ic_myaddr);

	scan_config->bcast_sta_id = IWM_AUX_STA_ID;
	scan_config->channel_flags = 0;

	for (c = &ic->ic_channels[1], nchan = 0;
	    c <= &ic->ic_channels[IEEE80211_CHAN_MAX] &&
	    nchan < sc->sc_capa_n_scan_channels; c++) {
		if (c->ic_flags == 0)
			continue;
		scan_config->channel_array[nchan++] =
		    ieee80211_mhz2ieee(c->ic_freq, 0);
	}

	scan_config->flags = htole32(IWM_SCAN_CONFIG_FLAG_ACTIVATE |
	    IWM_SCAN_CONFIG_FLAG_ALLOW_CHUB_REQS |
	    IWM_SCAN_CONFIG_FLAG_SET_TX_CHAINS |
	    IWM_SCAN_CONFIG_FLAG_SET_RX_CHAINS |
	    IWM_SCAN_CONFIG_FLAG_SET_AUX_STA_ID |
	    IWM_SCAN_CONFIG_FLAG_SET_ALL_TIMES |
	    IWM_SCAN_CONFIG_FLAG_SET_LEGACY_RATES |
	    IWM_SCAN_CONFIG_FLAG_SET_MAC_ADDR |
	    IWM_SCAN_CONFIG_FLAG_SET_CHANNEL_FLAGS|
	    IWM_SCAN_CONFIG_N_CHANNELS(nchan) |
	    IWM_SCAN_CONFIG_FLAG_CLEAR_FRAGMENTED);

	hcmd.data[0] = scan_config;
	hcmd.len[0] = cmd_size;

	err = iwm_send_cmd(sc, &hcmd);
	free(scan_config, M_DEVBUF, cmd_size);
	return err;
}

int
iwm_umac_scan_size(struct iwm_softc *sc)
{
	int base_size = IWM_SCAN_REQ_UMAC_SIZE_V1;
	int tail_size;

	if (isset(sc->sc_ucode_api, IWM_UCODE_TLV_API_ADAPTIVE_DWELL_V2))
		base_size = IWM_SCAN_REQ_UMAC_SIZE_V8;
	else if (isset(sc->sc_ucode_api, IWM_UCODE_TLV_API_ADAPTIVE_DWELL))
		base_size = IWM_SCAN_REQ_UMAC_SIZE_V7;
	if (isset(sc->sc_ucode_api, IWM_UCODE_TLV_API_SCAN_EXT_CHAN_VER))
		tail_size = sizeof(struct iwm_scan_req_umac_tail_v2);
	else
		tail_size = sizeof(struct iwm_scan_req_umac_tail_v1);

	return base_size + sizeof(struct iwm_scan_channel_cfg_umac) *
	    sc->sc_capa_n_scan_channels + tail_size;
}

struct iwm_scan_umac_chan_param *
iwm_get_scan_req_umac_chan_param(struct iwm_softc *sc,
    struct iwm_scan_req_umac *req)
{
	if (isset(sc->sc_ucode_api, IWM_UCODE_TLV_API_ADAPTIVE_DWELL_V2))
		return &req->v8.channel;

	if (isset(sc->sc_ucode_api, IWM_UCODE_TLV_API_ADAPTIVE_DWELL))
		return &req->v7.channel;

	return &req->v1.channel;
}

void *
iwm_get_scan_req_umac_data(struct iwm_softc *sc, struct iwm_scan_req_umac *req)
{
	if (isset(sc->sc_ucode_api, IWM_UCODE_TLV_API_ADAPTIVE_DWELL_V2))
		return (void *)&req->v8.data;

	if (isset(sc->sc_ucode_api, IWM_UCODE_TLV_API_ADAPTIVE_DWELL))
		return (void *)&req->v7.data;

	return (void *)&req->v1.data;

}

/* adaptive dwell max budget time [TU] for full scan */
#define IWM_SCAN_ADWELL_MAX_BUDGET_FULL_SCAN 300
/* adaptive dwell max budget time [TU] for directed scan */
#define IWM_SCAN_ADWELL_MAX_BUDGET_DIRECTED_SCAN 100
/* adaptive dwell default high band APs number */
#define IWM_SCAN_ADWELL_DEFAULT_HB_N_APS 8
/* adaptive dwell default low band APs number */
#define IWM_SCAN_ADWELL_DEFAULT_LB_N_APS 2
/* adaptive dwell default APs number in social channels (1, 6, 11) */
#define IWM_SCAN_ADWELL_DEFAULT_N_APS_SOCIAL 10

int
iwm_umac_scan(struct iwm_softc *sc, int bgscan)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct iwm_host_cmd hcmd = {
		.id = iwm_cmd_id(IWM_SCAN_REQ_UMAC, IWM_LONG_GROUP, 0),
		.len = { 0, },
		.data = { NULL, },
		.flags = 0,
	};
	struct iwm_scan_req_umac *req;
	void *cmd_data, *tail_data;
	struct iwm_scan_req_umac_tail_v2 *tail;
	struct iwm_scan_req_umac_tail_v1 *tailv1;
	struct iwm_scan_umac_chan_param *chanparam;
	size_t req_len;
	int err, async = bgscan;

	req_len = iwm_umac_scan_size(sc);
	if ((req_len < IWM_SCAN_REQ_UMAC_SIZE_V1 +
	    sizeof(struct iwm_scan_req_umac_tail_v1)) ||
	    req_len > IWM_MAX_CMD_PAYLOAD_SIZE)
		return ERANGE;
	req = malloc(req_len, M_DEVBUF,
	    (async ? M_NOWAIT : M_WAIT) | M_CANFAIL | M_ZERO);
	if (req == NULL)
		return ENOMEM;

	hcmd.len[0] = (uint16_t)req_len;
	hcmd.data[0] = (void *)req;
	hcmd.flags |= async ? IWM_CMD_ASYNC : 0;

	if (isset(sc->sc_ucode_api, IWM_UCODE_TLV_API_ADAPTIVE_DWELL)) {
		req->v7.adwell_default_n_aps_social =
			IWM_SCAN_ADWELL_DEFAULT_N_APS_SOCIAL;
		req->v7.adwell_default_n_aps =
			IWM_SCAN_ADWELL_DEFAULT_LB_N_APS;

		if (ic->ic_des_esslen != 0)
			req->v7.adwell_max_budget =
			    htole16(IWM_SCAN_ADWELL_MAX_BUDGET_DIRECTED_SCAN);
		else
			req->v7.adwell_max_budget =
			    htole16(IWM_SCAN_ADWELL_MAX_BUDGET_FULL_SCAN);

		req->v7.scan_priority = htole32(IWM_SCAN_PRIORITY_HIGH);
		req->v7.max_out_time[IWM_SCAN_LB_LMAC_IDX] = 0;
		req->v7.suspend_time[IWM_SCAN_LB_LMAC_IDX] = 0;

		if (isset(sc->sc_ucode_api,
		    IWM_UCODE_TLV_API_ADAPTIVE_DWELL_V2)) {
			req->v8.active_dwell[IWM_SCAN_LB_LMAC_IDX] = 10;
			req->v8.passive_dwell[IWM_SCAN_LB_LMAC_IDX] = 110;
		} else {
			req->v7.active_dwell = 10;
			req->v7.passive_dwell = 110;
			req->v7.fragmented_dwell = 44;
		}
	} else {
		/* These timings correspond to iwlwifi's UNASSOC scan. */
		req->v1.active_dwell = 10;
		req->v1.passive_dwell = 110;
		req->v1.fragmented_dwell = 44;
		req->v1.extended_dwell = 90;

		req->v1.scan_priority = htole32(IWM_SCAN_PRIORITY_HIGH);
	}

	if (bgscan) {
		const uint32_t timeout = htole32(120);
		if (isset(sc->sc_ucode_api,
		    IWM_UCODE_TLV_API_ADAPTIVE_DWELL_V2)) {
			req->v8.max_out_time[IWM_SCAN_LB_LMAC_IDX] = timeout;
			req->v8.suspend_time[IWM_SCAN_LB_LMAC_IDX] = timeout;
		} else if (isset(sc->sc_ucode_api,
		    IWM_UCODE_TLV_API_ADAPTIVE_DWELL)) {
			req->v7.max_out_time[IWM_SCAN_LB_LMAC_IDX] = timeout;
			req->v7.suspend_time[IWM_SCAN_LB_LMAC_IDX] = timeout;
		} else {
			req->v1.max_out_time = timeout;
			req->v1.suspend_time = timeout;
		}
	}

	req->ooc_priority = htole32(IWM_SCAN_PRIORITY_HIGH);

	cmd_data = iwm_get_scan_req_umac_data(sc, req);
	chanparam = iwm_get_scan_req_umac_chan_param(sc, req);
	chanparam->count = iwm_umac_scan_fill_channels(sc,
	    (struct iwm_scan_channel_cfg_umac *)cmd_data,
	    ic->ic_des_esslen != 0, bgscan);
	chanparam->flags = 0;

	tail_data = cmd_data + sizeof(struct iwm_scan_channel_cfg_umac) *
	    sc->sc_capa_n_scan_channels;
	tail = tail_data;
	/* tail v1 layout differs in preq and direct_scan member fields. */
	tailv1 = tail_data;

	req->general_flags = htole32(IWM_UMAC_SCAN_GEN_FLAGS_PASS_ALL |
	    IWM_UMAC_SCAN_GEN_FLAGS_ITER_COMPLETE);
	if (isset(sc->sc_ucode_api, IWM_UCODE_TLV_API_ADAPTIVE_DWELL_V2)) {
		req->v8.general_flags2 =
			IWM_UMAC_SCAN_GEN_FLAGS2_ALLOW_CHNL_REORDER;
	}

	if (ic->ic_des_esslen != 0) {
		if (isset(sc->sc_ucode_api,
		    IWM_UCODE_TLV_API_SCAN_EXT_CHAN_VER)) {
			tail->direct_scan[0].id = IEEE80211_ELEMID_SSID;
			tail->direct_scan[0].len = ic->ic_des_esslen;
			memcpy(tail->direct_scan[0].ssid, ic->ic_des_essid,
			    ic->ic_des_esslen);
		} else {
			tailv1->direct_scan[0].id = IEEE80211_ELEMID_SSID;
			tailv1->direct_scan[0].len = ic->ic_des_esslen;
			memcpy(tailv1->direct_scan[0].ssid, ic->ic_des_essid,
			    ic->ic_des_esslen);
		}
		req->general_flags |=
		    htole32(IWM_UMAC_SCAN_GEN_FLAGS_PRE_CONNECT);
	} else
		req->general_flags |= htole32(IWM_UMAC_SCAN_GEN_FLAGS_PASSIVE);

	if (isset(sc->sc_enabled_capa, 
	    IWM_UCODE_TLV_CAPA_DS_PARAM_SET_IE_SUPPORT) &&
	    isset(sc->sc_enabled_capa, 
	    IWM_UCODE_TLV_CAPA_WFA_TPC_REP_IE_SUPPORT))
		req->general_flags |=
		    htole32(IWM_UMAC_SCAN_GEN_FLAGS_RRM_ENABLED);

	if (isset(sc->sc_ucode_api, IWM_UCODE_TLV_API_ADAPTIVE_DWELL)) {
		req->general_flags |=
		    htole32(IWM_UMAC_SCAN_GEN_FLAGS_ADAPTIVE_DWELL);
	} else {
		req->general_flags |=
		    htole32(IWM_UMAC_SCAN_GEN_FLAGS_EXTENDED_DWELL);
	}

	if (isset(sc->sc_ucode_api, IWM_UCODE_TLV_API_SCAN_EXT_CHAN_VER))
		err = iwm_fill_probe_req(sc, &tail->preq);
	else
		err = iwm_fill_probe_req_v1(sc, &tailv1->preq);
	if (err) {
		free(req, M_DEVBUF, req_len);
		return err;
	}

	/* Specify the scan plan: We'll do one iteration. */
	tail->schedule[0].interval = 0;
	tail->schedule[0].iter_count = 1;

	err = iwm_send_cmd(sc, &hcmd);
	free(req, M_DEVBUF, req_len);
	return err;
}

void
iwm_mcc_update(struct iwm_softc *sc, struct iwm_mcc_chub_notif *notif)
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
iwm_ridx2rate(struct ieee80211_rateset *rs, int ridx)
{
	int i;
	uint8_t rval;

	for (i = 0; i < rs->rs_nrates; i++) {
		rval = (rs->rs_rates[i] & IEEE80211_RATE_VAL);
		if (rval == iwm_rates[ridx].rate)
			return rs->rs_rates[i];
	}

	return 0;
}

int
iwm_rval2ridx(int rval)
{
	int ridx;

	for (ridx = 0; ridx < nitems(iwm_rates); ridx++) {
		if (iwm_rates[ridx].plcp == IWM_RATE_INVM_PLCP)
			continue;
		if (rval == iwm_rates[ridx].rate)
			break;
	}

	return ridx;
}

void
iwm_ack_rates(struct iwm_softc *sc, struct iwm_node *in, int *cck_rates,
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
		for (i = IWM_FIRST_CCK_RATE; i < IWM_FIRST_OFDM_RATE; i++) {
			if ((iwm_ridx2rate(rs, i) & IEEE80211_RATE_BASIC) == 0)
				continue;
			cck |= (1 << i);
			if (lowest_present_cck == -1 || lowest_present_cck > i)
				lowest_present_cck = i;
		}
	}
	for (i = IWM_FIRST_OFDM_RATE; i <= IWM_LAST_NON_HT_RATE; i++) {
		if ((iwm_ridx2rate(rs, i) & IEEE80211_RATE_BASIC) == 0)
			continue;	
		ofdm |= (1 << (i - IWM_FIRST_OFDM_RATE));
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

	if (IWM_RATE_24M_INDEX < lowest_present_ofdm)
		ofdm |= IWM_RATE_BIT_MSK(24) >> IWM_FIRST_OFDM_RATE;
	if (IWM_RATE_12M_INDEX < lowest_present_ofdm)
		ofdm |= IWM_RATE_BIT_MSK(12) >> IWM_FIRST_OFDM_RATE;
	/* 6M already there or needed so always add */
	ofdm |= IWM_RATE_BIT_MSK(6) >> IWM_FIRST_OFDM_RATE;

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
	if (IWM_RATE_11M_INDEX < lowest_present_cck)
		cck |= IWM_RATE_BIT_MSK(11) >> IWM_FIRST_CCK_RATE;
	if (IWM_RATE_5M_INDEX < lowest_present_cck)
		cck |= IWM_RATE_BIT_MSK(5) >> IWM_FIRST_CCK_RATE;
	if (IWM_RATE_2M_INDEX < lowest_present_cck)
		cck |= IWM_RATE_BIT_MSK(2) >> IWM_FIRST_CCK_RATE;
	/* 1M already there or needed so always add */
	cck |= IWM_RATE_BIT_MSK(1) >> IWM_FIRST_CCK_RATE;

	*cck_rates = cck;
	*ofdm_rates = ofdm;
}

void
iwm_mac_ctxt_cmd_common(struct iwm_softc *sc, struct iwm_node *in,
    struct iwm_mac_ctx_cmd *cmd, uint32_t action)
{
#define IWM_EXP2(x)	((1 << (x)) - 1)	/* CWmin = 2^ECWmin - 1 */
	struct ieee80211com *ic = &sc->sc_ic;
	struct ieee80211_node *ni = ic->ic_bss;
	int cck_ack_rates, ofdm_ack_rates;
	int i;

	cmd->id_and_color = htole32(IWM_FW_CMD_ID_AND_COLOR(in->in_id,
	    in->in_color));
	cmd->action = htole32(action);

	if (ic->ic_opmode == IEEE80211_M_MONITOR)
		cmd->mac_type = htole32(IWM_FW_MAC_TYPE_LISTENER);
	else if (ic->ic_opmode == IEEE80211_M_STA)
		cmd->mac_type = htole32(IWM_FW_MAC_TYPE_BSS_STA);
	else
		panic("unsupported operating mode %d", ic->ic_opmode);
	cmd->tsf_id = htole32(IWM_TSF_ID_A);

	IEEE80211_ADDR_COPY(cmd->node_addr, ic->ic_myaddr);
	if (ic->ic_opmode == IEEE80211_M_MONITOR) {
		IEEE80211_ADDR_COPY(cmd->bssid_addr, etherbroadcastaddr);
		return;
	}

	IEEE80211_ADDR_COPY(cmd->bssid_addr, in->in_macaddr);
	iwm_ack_rates(sc, in, &cck_ack_rates, &ofdm_ack_rates);
	cmd->cck_rates = htole32(cck_ack_rates);
	cmd->ofdm_rates = htole32(ofdm_ack_rates);

	cmd->cck_short_preamble
	    = htole32((ic->ic_flags & IEEE80211_F_SHPREAMBLE)
	      ? IWM_MAC_FLG_SHORT_PREAMBLE : 0);
	cmd->short_slot
	    = htole32((ic->ic_flags & IEEE80211_F_SHSLOT)
	      ? IWM_MAC_FLG_SHORT_SLOT : 0);

	for (i = 0; i < EDCA_NUM_AC; i++) {
		struct ieee80211_edca_ac_params *ac = &ic->ic_edca_ac[i];
		int txf = iwm_ac_to_tx_fifo[i];

		cmd->ac[txf].cw_min = htole16(IWM_EXP2(ac->ac_ecwmin));
		cmd->ac[txf].cw_max = htole16(IWM_EXP2(ac->ac_ecwmax));
		cmd->ac[txf].aifsn = ac->ac_aifsn;
		cmd->ac[txf].fifos_mask = (1 << txf);
		cmd->ac[txf].edca_txop = htole16(ac->ac_txoplimit * 32);
	}
	if (ni->ni_flags & IEEE80211_NODE_QOS)
		cmd->qos_flags |= htole32(IWM_MAC_QOS_FLG_UPDATE_EDCA);

	if (ni->ni_flags & IEEE80211_NODE_HT) {
		enum ieee80211_htprot htprot =
		    (ni->ni_htop1 & IEEE80211_HTOP1_PROT_MASK);
		switch (htprot) {
		case IEEE80211_HTPROT_NONE:
			break;
		case IEEE80211_HTPROT_NONMEMBER:
		case IEEE80211_HTPROT_NONHT_MIXED:
			cmd->protection_flags |=
			    htole32(IWM_MAC_PROT_FLG_HT_PROT |
			    IWM_MAC_PROT_FLG_FAT_PROT);
			break;
		case IEEE80211_HTPROT_20MHZ:
			if (in->in_phyctxt &&
			    (in->in_phyctxt->sco == IEEE80211_HTOP0_SCO_SCA ||
			    in->in_phyctxt->sco == IEEE80211_HTOP0_SCO_SCB)) {
				cmd->protection_flags |=
				    htole32(IWM_MAC_PROT_FLG_HT_PROT |
				    IWM_MAC_PROT_FLG_FAT_PROT);
			}
			break;
		default:
			break;
		}

		cmd->qos_flags |= htole32(IWM_MAC_QOS_FLG_TGN);
	}
	if (ic->ic_flags & IEEE80211_F_USEPROT)
		cmd->protection_flags |= htole32(IWM_MAC_PROT_FLG_TGG_PROTECT);

	cmd->filter_flags = htole32(IWM_MAC_FILTER_ACCEPT_GRP);
#undef IWM_EXP2
}

void
iwm_mac_ctxt_cmd_fill_sta(struct iwm_softc *sc, struct iwm_node *in,
    struct iwm_mac_data_sta *sta, int assoc)
{
	struct ieee80211_node *ni = &in->in_ni;
	uint32_t dtim_off;
	uint64_t tsf;

	dtim_off = ni->ni_dtimcount * ni->ni_intval * IEEE80211_DUR_TU;
	memcpy(&tsf, ni->ni_tstamp, sizeof(tsf));
	tsf = letoh64(tsf);

	sta->is_assoc = htole32(assoc);
	sta->dtim_time = htole32(ni->ni_rstamp + dtim_off);
	sta->dtim_tsf = htole64(tsf + dtim_off);
	sta->bi = htole32(ni->ni_intval);
	sta->bi_reciprocal = htole32(iwm_reciprocal(ni->ni_intval));
	sta->dtim_interval = htole32(ni->ni_intval * ni->ni_dtimperiod);
	sta->dtim_reciprocal = htole32(iwm_reciprocal(sta->dtim_interval));
	sta->listen_interval = htole32(10);
	sta->assoc_id = htole32(ni->ni_associd);
	sta->assoc_beacon_arrive_time = htole32(ni->ni_rstamp);
}

int
iwm_mac_ctxt_cmd(struct iwm_softc *sc, struct iwm_node *in, uint32_t action,
    int assoc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ieee80211_node *ni = &in->in_ni;
	struct iwm_mac_ctx_cmd cmd;
	int active = (sc->sc_flags & IWM_FLAG_MAC_ACTIVE);

	if (action == IWM_FW_CTXT_ACTION_ADD && active)
		panic("MAC already added");
	if (action == IWM_FW_CTXT_ACTION_REMOVE && !active)
		panic("MAC already removed");

	memset(&cmd, 0, sizeof(cmd));

	iwm_mac_ctxt_cmd_common(sc, in, &cmd, action);

	if (ic->ic_opmode == IEEE80211_M_MONITOR) {
		cmd.filter_flags |= htole32(IWM_MAC_FILTER_IN_PROMISC |
		    IWM_MAC_FILTER_IN_CONTROL_AND_MGMT |
		    IWM_MAC_FILTER_ACCEPT_GRP |
		    IWM_MAC_FILTER_IN_BEACON |
		    IWM_MAC_FILTER_IN_PROBE_REQUEST |
		    IWM_MAC_FILTER_IN_CRC32);
	} else if (!assoc || !ni->ni_associd || !ni->ni_dtimperiod)
		/* 
		 * Allow beacons to pass through as long as we are not
		 * associated or we do not have dtim period information.
		 */
		cmd.filter_flags |= htole32(IWM_MAC_FILTER_IN_BEACON);
	else
		iwm_mac_ctxt_cmd_fill_sta(sc, in, &cmd.sta, assoc);

	return iwm_send_cmd_pdu(sc, IWM_MAC_CONTEXT_CMD, 0, sizeof(cmd), &cmd);
}

int
iwm_update_quotas(struct iwm_softc *sc, struct iwm_node *in, int running)
{
	struct iwm_time_quota_cmd_v1 cmd;
	int i, idx, num_active_macs, quota, quota_rem;
	int colors[IWM_MAX_BINDINGS] = { -1, -1, -1, -1, };
	int n_ifs[IWM_MAX_BINDINGS] = {0, };
	uint16_t id;

	memset(&cmd, 0, sizeof(cmd));

	/* currently, PHY ID == binding ID */
	if (in && in->in_phyctxt) {
		id = in->in_phyctxt->id;
		KASSERT(id < IWM_MAX_BINDINGS);
		colors[id] = in->in_phyctxt->color;
		if (running)
			n_ifs[id] = 1;
	}

	/*
	 * The FW's scheduling session consists of
	 * IWM_MAX_QUOTA fragments. Divide these fragments
	 * equally between all the bindings that require quota
	 */
	num_active_macs = 0;
	for (i = 0; i < IWM_MAX_BINDINGS; i++) {
		cmd.quotas[i].id_and_color = htole32(IWM_FW_CTXT_INVALID);
		num_active_macs += n_ifs[i];
	}

	quota = 0;
	quota_rem = 0;
	if (num_active_macs) {
		quota = IWM_MAX_QUOTA / num_active_macs;
		quota_rem = IWM_MAX_QUOTA % num_active_macs;
	}

	for (idx = 0, i = 0; i < IWM_MAX_BINDINGS; i++) {
		if (colors[i] < 0)
			continue;

		cmd.quotas[idx].id_and_color =
			htole32(IWM_FW_CMD_ID_AND_COLOR(i, colors[i]));

		if (n_ifs[i] <= 0) {
			cmd.quotas[idx].quota = htole32(0);
			cmd.quotas[idx].max_duration = htole32(0);
		} else {
			cmd.quotas[idx].quota = htole32(quota * n_ifs[i]);
			cmd.quotas[idx].max_duration = htole32(0);
		}
		idx++;
	}

	/* Give the remainder of the session to the first binding */
	cmd.quotas[0].quota = htole32(le32toh(cmd.quotas[0].quota) + quota_rem);

	if (isset(sc->sc_ucode_api, IWM_UCODE_TLV_API_QUOTA_LOW_LATENCY)) {
		struct iwm_time_quota_cmd cmd_v2;

		memset(&cmd_v2, 0, sizeof(cmd_v2));
		for (i = 0; i < IWM_MAX_BINDINGS; i++) {
			cmd_v2.quotas[i].id_and_color =
			    cmd.quotas[i].id_and_color;
			cmd_v2.quotas[i].quota = cmd.quotas[i].quota;
			cmd_v2.quotas[i].max_duration =
			    cmd.quotas[i].max_duration;
		}
		return iwm_send_cmd_pdu(sc, IWM_TIME_QUOTA_CMD, 0,
		    sizeof(cmd_v2), &cmd_v2);
	}

	return iwm_send_cmd_pdu(sc, IWM_TIME_QUOTA_CMD, 0, sizeof(cmd), &cmd);
}

void
iwm_add_task(struct iwm_softc *sc, struct taskq *taskq, struct task *task)
{
	int s = splnet();

	if (sc->sc_flags & IWM_FLAG_SHUTDOWN) {
		splx(s);
		return;
	}

	refcnt_take(&sc->task_refs);
	if (!task_add(taskq, task))
		refcnt_rele_wake(&sc->task_refs);
	splx(s);
}

void
iwm_del_task(struct iwm_softc *sc, struct taskq *taskq, struct task *task)
{
	if (task_del(taskq, task))
		refcnt_rele(&sc->task_refs);
}

int
iwm_scan(struct iwm_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ifnet *ifp = IC2IFP(ic);
	int err;

	if (sc->sc_flags & IWM_FLAG_BGSCAN) {
		err = iwm_scan_abort(sc);
		if (err) {
			printf("%s: could not abort background scan\n",
			    DEVNAME(sc));
			return err;
		}
	}

	if (isset(sc->sc_enabled_capa, IWM_UCODE_TLV_CAPA_UMAC_SCAN))
		err = iwm_umac_scan(sc, 0);
	else
		err = iwm_lmac_scan(sc, 0);
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

	sc->sc_flags |= IWM_FLAG_SCANNING;
	if (ifp->if_flags & IFF_DEBUG)
		printf("%s: %s -> %s\n", ifp->if_xname,
		    ieee80211_state_name[ic->ic_state],
		    ieee80211_state_name[IEEE80211_S_SCAN]);
	if ((sc->sc_flags & IWM_FLAG_BGSCAN) == 0) {
		ieee80211_set_link_state(ic, LINK_STATE_DOWN);
		ieee80211_node_cleanup(ic, ic->ic_bss);
	}
	ic->ic_state = IEEE80211_S_SCAN;
	iwm_led_blink_start(sc);
	wakeup(&ic->ic_state); /* wake iwm_init() */

	return 0;
}

int
iwm_bgscan(struct ieee80211com *ic) 
{
	struct iwm_softc *sc = IC2IFP(ic)->if_softc;
	int err;

	if (sc->sc_flags & IWM_FLAG_SCANNING)
		return 0;

	if (isset(sc->sc_enabled_capa, IWM_UCODE_TLV_CAPA_UMAC_SCAN))
		err = iwm_umac_scan(sc, 1);
	else
		err = iwm_lmac_scan(sc, 1);
	if (err) {
		printf("%s: could not initiate scan\n", DEVNAME(sc));
		return err;
	}

	sc->sc_flags |= IWM_FLAG_BGSCAN;
	return 0;
}

void
iwm_bgscan_done(struct ieee80211com *ic,
    struct ieee80211_node_switch_bss_arg *arg, size_t arg_size)
{
	struct iwm_softc *sc = ic->ic_softc;

	free(sc->bgscan_unref_arg, M_DEVBUF, sc->bgscan_unref_arg_size);
	sc->bgscan_unref_arg = arg;
	sc->bgscan_unref_arg_size = arg_size;
	iwm_add_task(sc, systq, &sc->bgscan_done_task);
}

void
iwm_bgscan_done_task(void *arg)
{
	struct iwm_softc *sc = arg;
	struct ieee80211com *ic = &sc->sc_ic;
	struct iwm_node *in = (void *)ic->ic_bss;
	struct ieee80211_node *ni = &in->in_ni;
	int tid, err = 0, s = splnet();

	if ((sc->sc_flags & IWM_FLAG_SHUTDOWN) ||
	    (ic->ic_flags & IEEE80211_F_BGSCAN) == 0 ||
	    ic->ic_state != IEEE80211_S_RUN) {
		err = ENXIO;
		goto done;
	}

	for (tid = 0; tid < IWM_MAX_TID_COUNT; tid++) {
		int qid = IWM_FIRST_AGG_TX_QUEUE + tid;

		if ((sc->tx_ba_queue_mask & (1 << qid)) == 0)
			continue;

		err = iwm_sta_tx_agg(sc, ni, tid, 0, 0, 0);
		if (err)
			goto done;
		err = iwm_disable_txq(sc, IWM_STATION_ID, qid, tid);
		if (err)
			goto done;
		in->tfd_queue_msk &= ~(1 << qid);
#if 0 /* disabled for now; we are going to DEAUTH soon anyway */
		IEEE80211_SEND_ACTION(ic, ni, IEEE80211_CATEG_BA,
		    IEEE80211_ACTION_DELBA,
		    IEEE80211_REASON_AUTH_LEAVE << 16 |
		    IEEE80211_FC1_DIR_TODS << 8 | tid);
#endif
		ieee80211_node_tx_ba_clear(ni, tid);
	}

	err = iwm_flush_sta(sc, in);
	if (err)
		goto done;

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
		if ((sc->sc_flags & IWM_FLAG_SHUTDOWN) == 0)
			task_add(systq, &sc->init_task);
	}
	refcnt_rele_wake(&sc->task_refs);
	splx(s);
}

int
iwm_umac_scan_abort(struct iwm_softc *sc)
{
	struct iwm_umac_scan_abort cmd = { 0 };

	return iwm_send_cmd_pdu(sc,
	    IWM_WIDE_ID(IWM_LONG_GROUP, IWM_SCAN_ABORT_UMAC),
	    0, sizeof(cmd), &cmd);
}

int
iwm_lmac_scan_abort(struct iwm_softc *sc)
{
	struct iwm_host_cmd cmd = {
		.id = IWM_SCAN_OFFLOAD_ABORT_CMD,
	};
	int err, status;

	err = iwm_send_cmd_status(sc, &cmd, &status);
	if (err)
		return err;

	if (status != IWM_CAN_ABORT_STATUS) {
		/*
		 * The scan abort will return 1 for success or
		 * 2 for "failure".  A failure condition can be
		 * due to simply not being in an active scan which
		 * can occur if we send the scan abort before the
		 * microcode has notified us that a scan is completed.
		 */
		return EBUSY;
	}

	return 0;
}

int
iwm_scan_abort(struct iwm_softc *sc)
{
	int err;

	if (isset(sc->sc_enabled_capa, IWM_UCODE_TLV_CAPA_UMAC_SCAN))
		err = iwm_umac_scan_abort(sc);
	else
		err = iwm_lmac_scan_abort(sc);

	if (err == 0)
		sc->sc_flags &= ~(IWM_FLAG_SCANNING | IWM_FLAG_BGSCAN);
	return err;
}

int
iwm_phy_ctxt_update(struct iwm_softc *sc, struct iwm_phy_ctxt *phyctxt,
    struct ieee80211_channel *chan, uint8_t chains_static,
    uint8_t chains_dynamic, uint32_t apply_time, uint8_t sco,
    uint8_t vht_chan_width)
{
	uint16_t band_flags = (IEEE80211_CHAN_2GHZ | IEEE80211_CHAN_5GHZ);
	int err;

	if (isset(sc->sc_enabled_capa,
	    IWM_UCODE_TLV_CAPA_BINDING_CDB_SUPPORT) &&
	    (phyctxt->channel->ic_flags & band_flags) !=
	    (chan->ic_flags & band_flags)) {
		err = iwm_phy_ctxt_cmd(sc, phyctxt, chains_static,
		    chains_dynamic, IWM_FW_CTXT_ACTION_REMOVE, apply_time, sco,
		    vht_chan_width);
		if (err) {
			printf("%s: could not remove PHY context "
			    "(error %d)\n", DEVNAME(sc), err);
			return err;
		}
		phyctxt->channel = chan;
		err = iwm_phy_ctxt_cmd(sc, phyctxt, chains_static,
		    chains_dynamic, IWM_FW_CTXT_ACTION_ADD, apply_time, sco,
		    vht_chan_width);
		if (err) {
			printf("%s: could not add PHY context "
			    "(error %d)\n", DEVNAME(sc), err);
			return err;
		}
	} else {
		phyctxt->channel = chan;
		err = iwm_phy_ctxt_cmd(sc, phyctxt, chains_static,
		    chains_dynamic, IWM_FW_CTXT_ACTION_MODIFY, apply_time, sco,
		    vht_chan_width);
		if (err) {
			printf("%s: could not update PHY context (error %d)\n",
			    DEVNAME(sc), err);
			return err;
		}
	}

	phyctxt->sco = sco;
	phyctxt->vht_chan_width = vht_chan_width;
	return 0;
}

int
iwm_auth(struct iwm_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct iwm_node *in = (void *)ic->ic_bss;
	uint32_t duration;
	int generation = sc->sc_generation, err;

	splassert(IPL_NET);

	if (ic->ic_opmode == IEEE80211_M_MONITOR) {
		err = iwm_phy_ctxt_update(sc, &sc->sc_phyctxt[0],
		    ic->ic_ibss_chan, 1, 1, 0, IEEE80211_HTOP0_SCO_SCN,
		    IEEE80211_VHTOP0_CHAN_WIDTH_HT);
		if (err)
			return err;
	} else {
		err = iwm_phy_ctxt_update(sc, &sc->sc_phyctxt[0],
		    in->in_ni.ni_chan, 1, 1, 0, IEEE80211_HTOP0_SCO_SCN,
		    IEEE80211_VHTOP0_CHAN_WIDTH_HT);
		if (err)
			return err;
	}
	in->in_phyctxt = &sc->sc_phyctxt[0];
	IEEE80211_ADDR_COPY(in->in_macaddr, in->in_ni.ni_macaddr); 
	iwm_setrates(in, 0);

	err = iwm_mac_ctxt_cmd(sc, in, IWM_FW_CTXT_ACTION_ADD, 0);
	if (err) {
		printf("%s: could not add MAC context (error %d)\n",
		    DEVNAME(sc), err);
		return err;
 	}
	sc->sc_flags |= IWM_FLAG_MAC_ACTIVE;

	err = iwm_binding_cmd(sc, in, IWM_FW_CTXT_ACTION_ADD);
	if (err) {
		printf("%s: could not add binding (error %d)\n",
		    DEVNAME(sc), err);
		goto rm_mac_ctxt;
	}
	sc->sc_flags |= IWM_FLAG_BINDING_ACTIVE;

	in->tid_disable_ampdu = 0xffff;
	err = iwm_add_sta_cmd(sc, in, 0);
	if (err) {
		printf("%s: could not add sta (error %d)\n",
		    DEVNAME(sc), err);
		goto rm_binding;
	}
	sc->sc_flags |= IWM_FLAG_STA_ACTIVE;

	if (ic->ic_opmode == IEEE80211_M_MONITOR)
		return 0;

	/*
	 * Prevent the FW from wandering off channel during association
	 * by "protecting" the session with a time event.
	 */
	if (in->in_ni.ni_intval)
		duration = in->in_ni.ni_intval * 2;
	else
		duration = IEEE80211_DUR_TU; 
	iwm_protect_session(sc, in, duration, in->in_ni.ni_intval / 2);

	return 0;

rm_binding:
	if (generation == sc->sc_generation) {
		iwm_binding_cmd(sc, in, IWM_FW_CTXT_ACTION_REMOVE);
		sc->sc_flags &= ~IWM_FLAG_BINDING_ACTIVE;
	}
rm_mac_ctxt:
	if (generation == sc->sc_generation) {
		iwm_mac_ctxt_cmd(sc, in, IWM_FW_CTXT_ACTION_REMOVE, 0);
		sc->sc_flags &= ~IWM_FLAG_MAC_ACTIVE;
	}
	return err;
}

int
iwm_deauth(struct iwm_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct iwm_node *in = (void *)ic->ic_bss;
	int err;

	splassert(IPL_NET);

	iwm_unprotect_session(sc, in);

	if (sc->sc_flags & IWM_FLAG_STA_ACTIVE) {
		err = iwm_flush_sta(sc, in);
		if (err)
			return err;
		err = iwm_rm_sta_cmd(sc, in);
		if (err) {
			printf("%s: could not remove STA (error %d)\n",
			    DEVNAME(sc), err);
			return err;
		}
		in->tid_disable_ampdu = 0xffff;
		sc->sc_flags &= ~IWM_FLAG_STA_ACTIVE;
		sc->sc_rx_ba_sessions = 0;
		sc->ba_rx.start_tidmask = 0;
		sc->ba_rx.stop_tidmask = 0;
		sc->tx_ba_queue_mask = 0;
		sc->ba_tx.start_tidmask = 0;
		sc->ba_tx.stop_tidmask = 0;
	}

	if (sc->sc_flags & IWM_FLAG_BINDING_ACTIVE) {
		err = iwm_binding_cmd(sc, in, IWM_FW_CTXT_ACTION_REMOVE);
		if (err) {
			printf("%s: could not remove binding (error %d)\n",
			    DEVNAME(sc), err);
			return err;
		}
		sc->sc_flags &= ~IWM_FLAG_BINDING_ACTIVE;
	}

	if (sc->sc_flags & IWM_FLAG_MAC_ACTIVE) {
		err = iwm_mac_ctxt_cmd(sc, in, IWM_FW_CTXT_ACTION_REMOVE, 0);
		if (err) {
			printf("%s: could not remove MAC context (error %d)\n",
			    DEVNAME(sc), err);
			return err;
		}
		sc->sc_flags &= ~IWM_FLAG_MAC_ACTIVE;
	}

	/* Move unused PHY context to a default channel. */
	err = iwm_phy_ctxt_update(sc, &sc->sc_phyctxt[0],
	    &ic->ic_channels[1], 1, 1, 0, IEEE80211_HTOP0_SCO_SCN,
	    IEEE80211_VHTOP0_CHAN_WIDTH_HT);
	if (err)
		return err;

	return 0;
}

int
iwm_run(struct iwm_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct iwm_node *in = (void *)ic->ic_bss;
	struct ieee80211_node *ni = &in->in_ni;
	int err;

	splassert(IPL_NET);

	if (ic->ic_opmode == IEEE80211_M_MONITOR) {
		/* Add a MAC context and a sniffing STA. */
		err = iwm_auth(sc);
		if (err)
			return err;
	}

	/* Configure Rx chains for MIMO and configure 40 MHz channel. */
	if (ic->ic_opmode == IEEE80211_M_MONITOR) {
		uint8_t chains = iwm_mimo_enabled(sc) ? 2 : 1;
		err = iwm_phy_ctxt_update(sc, in->in_phyctxt,
		    in->in_phyctxt->channel, chains, chains,
		    0, IEEE80211_HTOP0_SCO_SCN,
		    IEEE80211_VHTOP0_CHAN_WIDTH_HT);
		if (err) {
			printf("%s: failed to update PHY\n", DEVNAME(sc));
			return err;
		}
	} else if (ni->ni_flags & IEEE80211_NODE_HT) {
		uint8_t chains = iwm_mimo_enabled(sc) ? 2 : 1;
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
		err = iwm_phy_ctxt_update(sc, in->in_phyctxt,
		    in->in_phyctxt->channel, chains, chains,
		    0, sco, vht_chan_width);
		if (err) {
			printf("%s: failed to update PHY\n", DEVNAME(sc));
			return err;
		}
	}

	/* Update STA again to apply HT and VHT settings. */
	err = iwm_add_sta_cmd(sc, in, 1);
	if (err) {
		printf("%s: could not update STA (error %d)\n",
		    DEVNAME(sc), err);
		return err;
	}

	/* We have now been assigned an associd by the AP. */
	err = iwm_mac_ctxt_cmd(sc, in, IWM_FW_CTXT_ACTION_MODIFY, 1);
	if (err) {
		printf("%s: failed to update MAC\n", DEVNAME(sc));
		return err;
	}

	err = iwm_sf_config(sc, IWM_SF_FULL_ON);
	if (err) {
		printf("%s: could not set sf full on (error %d)\n",
		    DEVNAME(sc), err);
		return err;
	}

	err = iwm_allow_mcast(sc);
	if (err) {
		printf("%s: could not allow mcast (error %d)\n",
		    DEVNAME(sc), err);
		return err;
	}

	err = iwm_power_update_device(sc);
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
	err = iwm_enable_beacon_filter(sc, in);
	if (err) {
		printf("%s: could not enable beacon filter\n",
		    DEVNAME(sc));
		return err;
	}
#endif
	err = iwm_power_mac_update_mode(sc, in);
	if (err) {
		printf("%s: could not update MAC power (error %d)\n",
		    DEVNAME(sc), err);
		return err;
	}

	if (!isset(sc->sc_enabled_capa, IWM_UCODE_TLV_CAPA_DYNAMIC_QUOTA)) {
		err = iwm_update_quotas(sc, in, 1);
		if (err) {
			printf("%s: could not update quotas (error %d)\n",
			    DEVNAME(sc), err);
			return err;
		}
	}

	ieee80211_amrr_node_init(&sc->sc_amrr, &in->in_amn);
	ieee80211_ra_node_init(&in->in_rn);
	ieee80211_ra_vht_node_init(&in->in_rn_vht);

	if (ic->ic_opmode == IEEE80211_M_MONITOR) {
		iwm_led_blink_start(sc);
		return 0;
	}

	/* Start at lowest available bit-rate, AMRR will raise. */
	in->in_ni.ni_txrate = 0;
	in->in_ni.ni_txmcs = 0;
	in->in_ni.ni_vht_ss = 1;
	iwm_setrates(in, 0);

	timeout_add_msec(&sc->sc_calib_to, 500);
	iwm_led_enable(sc);

	return 0;
}

int
iwm_run_stop(struct iwm_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct iwm_node *in = (void *)ic->ic_bss;
	struct ieee80211_node *ni = &in->in_ni;
	int err, i, tid;

	splassert(IPL_NET);

	/*
	 * Stop Tx/Rx BA sessions now. We cannot rely on the BA task
	 * for this when moving out of RUN state since it runs in a
	 * separate thread.
	 * Note that in->in_ni (struct ieee80211_node) already represents
	 * our new access point in case we are roaming between APs.
	 * This means we cannot rely on struct ieee802111_node to tell
	 * us which BA sessions exist.
	 */
	for (i = 0; i < nitems(sc->sc_rxba_data); i++) {
		struct iwm_rxba_data *rxba = &sc->sc_rxba_data[i];
		if (rxba->baid == IWM_RX_REORDER_DATA_INVALID_BAID)
			continue;
		err = iwm_sta_rx_agg(sc, ni, rxba->tid, 0, 0, 0, 0);
		if (err)
			return err;
		iwm_clear_reorder_buffer(sc, rxba);
		if (sc->sc_rx_ba_sessions > 0)
			sc->sc_rx_ba_sessions--;
	}
	for (tid = 0; tid < IWM_MAX_TID_COUNT; tid++) {
		int qid = IWM_FIRST_AGG_TX_QUEUE + tid;
		if ((sc->tx_ba_queue_mask & (1 << qid)) == 0)
			continue;
		err = iwm_sta_tx_agg(sc, ni, tid, 0, 0, 0);
		if (err)
			return err;
		err = iwm_disable_txq(sc, IWM_STATION_ID, qid, tid);
		if (err)
			return err;
		in->tfd_queue_msk &= ~(1 << qid);
	}
	ieee80211_ba_del(ni);

	if (ic->ic_opmode == IEEE80211_M_MONITOR)
		iwm_led_blink_stop(sc);

	err = iwm_sf_config(sc, IWM_SF_INIT_OFF);
	if (err)
		return err;

	iwm_disable_beacon_filter(sc);

	if (!isset(sc->sc_enabled_capa, IWM_UCODE_TLV_CAPA_DYNAMIC_QUOTA)) {
		err = iwm_update_quotas(sc, in, 0);
		if (err) {
			printf("%s: could not update quotas (error %d)\n",
			    DEVNAME(sc), err);
			return err;
		}
	}

	/* Mark station as disassociated. */
	err = iwm_mac_ctxt_cmd(sc, in, IWM_FW_CTXT_ACTION_MODIFY, 0);
	if (err) {
		printf("%s: failed to update MAC\n", DEVNAME(sc));
		return err;
	}

	/* Reset Tx chains in case MIMO or 40 MHz channels were enabled. */
	if (in->in_ni.ni_flags & IEEE80211_NODE_HT) {
		err = iwm_phy_ctxt_update(sc, in->in_phyctxt,
		    in->in_phyctxt->channel, 1, 1, 0, IEEE80211_HTOP0_SCO_SCN,
		    IEEE80211_VHTOP0_CHAN_WIDTH_HT);
		if (err) {
			printf("%s: failed to update PHY\n", DEVNAME(sc));
			return err;
		}
	}

	return 0;
}

struct ieee80211_node *
iwm_node_alloc(struct ieee80211com *ic)
{
	return malloc(sizeof (struct iwm_node), M_DEVBUF, M_NOWAIT | M_ZERO);
}

int
iwm_set_key_v1(struct ieee80211com *ic, struct ieee80211_node *ni,
    struct ieee80211_key *k)
{
	struct iwm_softc *sc = ic->ic_softc;
	struct iwm_add_sta_key_cmd_v1 cmd;

	memset(&cmd, 0, sizeof(cmd));

	cmd.common.key_flags = htole16(IWM_STA_KEY_FLG_CCM |
	    IWM_STA_KEY_FLG_WEP_KEY_MAP |
	    ((k->k_id << IWM_STA_KEY_FLG_KEYID_POS) &
	    IWM_STA_KEY_FLG_KEYID_MSK));
	if (k->k_flags & IEEE80211_KEY_GROUP)
		cmd.common.key_flags |= htole16(IWM_STA_KEY_MULTICAST);

	memcpy(cmd.common.key, k->k_key, MIN(sizeof(cmd.common.key), k->k_len));
	cmd.common.key_offset = 0;
	cmd.common.sta_id = IWM_STATION_ID;

	return iwm_send_cmd_pdu(sc, IWM_ADD_STA_KEY, IWM_CMD_ASYNC,
	    sizeof(cmd), &cmd);
}

int
iwm_set_key(struct ieee80211com *ic, struct ieee80211_node *ni,
    struct ieee80211_key *k)
{
	struct iwm_softc *sc = ic->ic_softc;
	struct iwm_add_sta_key_cmd cmd;

	if ((k->k_flags & IEEE80211_KEY_GROUP) ||
	    k->k_cipher != IEEE80211_CIPHER_CCMP)  {
		/* Fallback to software crypto for other ciphers. */
		return (ieee80211_set_key(ic, ni, k));
	}

	if (!isset(sc->sc_ucode_api, IWM_UCODE_TLV_API_TKIP_MIC_KEYS))
		return iwm_set_key_v1(ic, ni, k);

	memset(&cmd, 0, sizeof(cmd));

	cmd.common.key_flags = htole16(IWM_STA_KEY_FLG_CCM |
	    IWM_STA_KEY_FLG_WEP_KEY_MAP |
	    ((k->k_id << IWM_STA_KEY_FLG_KEYID_POS) &
	    IWM_STA_KEY_FLG_KEYID_MSK));
	if (k->k_flags & IEEE80211_KEY_GROUP)
		cmd.common.key_flags |= htole16(IWM_STA_KEY_MULTICAST);

	memcpy(cmd.common.key, k->k_key, MIN(sizeof(cmd.common.key), k->k_len));
	cmd.common.key_offset = 0;
	cmd.common.sta_id = IWM_STATION_ID;

	cmd.transmit_seq_cnt = htole64(k->k_tsc);

	return iwm_send_cmd_pdu(sc, IWM_ADD_STA_KEY, IWM_CMD_ASYNC,
	    sizeof(cmd), &cmd);
}

void
iwm_delete_key_v1(struct ieee80211com *ic, struct ieee80211_node *ni,
    struct ieee80211_key *k)
{
	struct iwm_softc *sc = ic->ic_softc;
	struct iwm_add_sta_key_cmd_v1 cmd;

	memset(&cmd, 0, sizeof(cmd));

	cmd.common.key_flags = htole16(IWM_STA_KEY_NOT_VALID |
	    IWM_STA_KEY_FLG_NO_ENC | IWM_STA_KEY_FLG_WEP_KEY_MAP |
	    ((k->k_id << IWM_STA_KEY_FLG_KEYID_POS) &
	    IWM_STA_KEY_FLG_KEYID_MSK));
	memcpy(cmd.common.key, k->k_key, MIN(sizeof(cmd.common.key), k->k_len));
	cmd.common.key_offset = 0;
	cmd.common.sta_id = IWM_STATION_ID;

	iwm_send_cmd_pdu(sc, IWM_ADD_STA_KEY, IWM_CMD_ASYNC, sizeof(cmd), &cmd);
}

void
iwm_delete_key(struct ieee80211com *ic, struct ieee80211_node *ni,
    struct ieee80211_key *k)
{
	struct iwm_softc *sc = ic->ic_softc;
	struct iwm_add_sta_key_cmd cmd;

	if ((k->k_flags & IEEE80211_KEY_GROUP) ||
	    (k->k_cipher != IEEE80211_CIPHER_CCMP)) {
		/* Fallback to software crypto for other ciphers. */
                ieee80211_delete_key(ic, ni, k);
		return;
	}

	if ((sc->sc_flags & IWM_FLAG_STA_ACTIVE) == 0)
		return;

	if (!isset(sc->sc_ucode_api, IWM_UCODE_TLV_API_TKIP_MIC_KEYS))
		return iwm_delete_key_v1(ic, ni, k);

	memset(&cmd, 0, sizeof(cmd));

	cmd.common.key_flags = htole16(IWM_STA_KEY_NOT_VALID |
	    IWM_STA_KEY_FLG_NO_ENC | IWM_STA_KEY_FLG_WEP_KEY_MAP |
	    ((k->k_id << IWM_STA_KEY_FLG_KEYID_POS) &
	    IWM_STA_KEY_FLG_KEYID_MSK));
	memcpy(cmd.common.key, k->k_key, MIN(sizeof(cmd.common.key), k->k_len));
	cmd.common.key_offset = 0;
	cmd.common.sta_id = IWM_STATION_ID;

	iwm_send_cmd_pdu(sc, IWM_ADD_STA_KEY, IWM_CMD_ASYNC, sizeof(cmd), &cmd);
}

void
iwm_calib_timeout(void *arg)
{
	struct iwm_softc *sc = arg;
	struct ieee80211com *ic = &sc->sc_ic;
	struct iwm_node *in = (void *)ic->ic_bss;
	struct ieee80211_node *ni = &in->in_ni;
	int s;

	s = splnet();
	if ((ic->ic_fixed_rate == -1 || ic->ic_fixed_mcs == -1) &&
	    (ni->ni_flags & IEEE80211_NODE_HT) == 0 &&
	    ic->ic_opmode == IEEE80211_M_STA && ic->ic_bss) {
		int old_txrate = ni->ni_txrate;
		ieee80211_amrr_choose(&sc->sc_amrr, &in->in_ni, &in->in_amn);
		/* 
		 * If AMRR has chosen a new TX rate we must update
		 * the firmware's LQ rate table.
		 * ni_txrate may change again before the task runs so
		 * cache the chosen rate in the iwm_node structure.
		 */
		if (ni->ni_txrate != old_txrate)
			iwm_setrates(in, 1);
	}

	splx(s);

	timeout_add_msec(&sc->sc_calib_to, 500);
}

void
iwm_set_rate_table_vht(struct iwm_node *in, struct iwm_lq_cmd *lqcmd)
{
	struct ieee80211_node *ni = &in->in_ni;
	struct ieee80211com *ic = ni->ni_ic;
	struct iwm_softc *sc = IC2IFP(ic)->if_softc;
	int ridx_min = iwm_rval2ridx(ieee80211_min_basic_rate(ic));
	int i, tab, txmcs;

	/*
	 * Fill the LQ rate selection table with VHT rates in descending
	 * order, i.e. with the node's current TX rate first. Keep reducing
	 * channel width during later Tx attempts, and eventually fall back
	 * to legacy OFDM. Do not mix SISO and MIMO rates.
	 */
	lqcmd->mimo_delim = 0;
	txmcs = ni->ni_txmcs;
	for (i = 0; i < nitems(lqcmd->rs_table); i++) {
		if (txmcs >= 0) {
			tab = IWM_RATE_MCS_VHT_MSK;
			tab |= txmcs & IWM_RATE_VHT_MCS_RATE_CODE_MSK;
			tab |= ((ni->ni_vht_ss - 1) <<
			    IWM_RATE_VHT_MCS_NSS_POS) &
			    IWM_RATE_VHT_MCS_NSS_MSK;
			if (ni->ni_vht_ss > 1)
				tab |= IWM_RATE_MCS_ANT_AB_MSK;
			else
				tab |= iwm_valid_siso_ant_rate_mask(sc);

			/*
			 * First two Tx attempts may use 80MHz/40MHz/SGI.
			 * Next two Tx attempts may use 40MHz/SGI.
			 * Beyond that use 20 MHz and decrease the rate.
			 * As a special case, MCS 9 is invalid on 20 Mhz.
			 */
			if (txmcs == 9) {
				if (i < 2 && in->in_phyctxt->vht_chan_width >=
				    IEEE80211_VHTOP0_CHAN_WIDTH_80)
					tab |= IWM_RATE_MCS_CHAN_WIDTH_80;
				else if (in->in_phyctxt->sco ==
				    IEEE80211_HTOP0_SCO_SCA ||
				    in->in_phyctxt->sco ==
				    IEEE80211_HTOP0_SCO_SCB)
					tab |= IWM_RATE_MCS_CHAN_WIDTH_40;
				else {
					/* no 40 MHz, fall back on MCS 8 */
					tab &= ~IWM_RATE_VHT_MCS_RATE_CODE_MSK;
					tab |= 8;
				}
					
				tab |= IWM_RATE_MCS_RTS_REQUIRED_MSK;
				if (i < 4) {
					if (ieee80211_ra_vht_use_sgi(ni))
						tab |= IWM_RATE_MCS_SGI_MSK;
				} else
					txmcs--;
			} else if (i < 2 && in->in_phyctxt->vht_chan_width >=
			    IEEE80211_VHTOP0_CHAN_WIDTH_80) {
				tab |= IWM_RATE_MCS_CHAN_WIDTH_80;
				tab |= IWM_RATE_MCS_RTS_REQUIRED_MSK;
				if (ieee80211_ra_vht_use_sgi(ni))
					tab |= IWM_RATE_MCS_SGI_MSK;
			} else if (i < 4 &&
			    in->in_phyctxt->vht_chan_width >=
			    IEEE80211_VHTOP0_CHAN_WIDTH_HT &&
			    (in->in_phyctxt->sco == IEEE80211_HTOP0_SCO_SCA ||
			    in->in_phyctxt->sco == IEEE80211_HTOP0_SCO_SCB)) {
				tab |= IWM_RATE_MCS_CHAN_WIDTH_40;
				tab |= IWM_RATE_MCS_RTS_REQUIRED_MSK;
				if (ieee80211_ra_vht_use_sgi(ni))
					tab |= IWM_RATE_MCS_SGI_MSK;
			} else if (txmcs >= 0)
				txmcs--;
		} else {
			/* Fill the rest with the lowest possible rate. */
			tab = iwm_rates[ridx_min].plcp;
			tab |= iwm_valid_siso_ant_rate_mask(sc);
			if (ni->ni_vht_ss > 1 && lqcmd->mimo_delim == 0)
				lqcmd->mimo_delim = i;
		}

		lqcmd->rs_table[i] = htole32(tab);
	}
}

void
iwm_set_rate_table(struct iwm_node *in, struct iwm_lq_cmd *lqcmd)
{
	struct ieee80211_node *ni = &in->in_ni;
	struct ieee80211com *ic = ni->ni_ic;
	struct iwm_softc *sc = IC2IFP(ic)->if_softc;
	struct ieee80211_rateset *rs = &ni->ni_rates;
	int i, ridx, ridx_min, ridx_max, j, mimo, tab = 0;

	/*
	 * Fill the LQ rate selection table with legacy and/or HT rates
	 * in descending order, i.e. with the node's current TX rate first.
	 * In cases where throughput of an HT rate corresponds to a legacy
	 * rate it makes no sense to add both. We rely on the fact that
	 * iwm_rates is laid out such that equivalent HT/legacy rates share
	 * the same IWM_RATE_*_INDEX value. Also, rates not applicable to
	 * legacy/HT are assumed to be marked with an 'invalid' PLCP value.
	 */
	j = 0;
	ridx_min = iwm_rval2ridx(ieee80211_min_basic_rate(ic));
	mimo = iwm_is_mimo_ht_mcs(ni->ni_txmcs);
	ridx_max = (mimo ? IWM_RIDX_MAX : IWM_LAST_HT_SISO_RATE);
	for (ridx = ridx_max; ridx >= ridx_min; ridx--) {
		uint8_t plcp = iwm_rates[ridx].plcp;
		uint8_t ht_plcp = iwm_rates[ridx].ht_plcp;

		if (j >= nitems(lqcmd->rs_table))
			break;
		tab = 0;
		if (ni->ni_flags & IEEE80211_NODE_HT) {
		    	if (ht_plcp == IWM_RATE_HT_SISO_MCS_INV_PLCP)
				continue;
	 		/* Do not mix SISO and MIMO HT rates. */
			if ((mimo && !iwm_is_mimo_ht_plcp(ht_plcp)) ||
			    (!mimo && iwm_is_mimo_ht_plcp(ht_plcp)))
				continue;
			for (i = ni->ni_txmcs; i >= 0; i--) {
				if (isclr(ni->ni_rxmcs, i))
					continue;
				if (ridx != iwm_ht_mcs2ridx[i])
					continue;
				tab = ht_plcp;
				tab |= IWM_RATE_MCS_HT_MSK;
				/* First two Tx attempts may use 40MHz/SGI. */
				if (j > 1)
					break;
				if (in->in_phyctxt->sco ==
				    IEEE80211_HTOP0_SCO_SCA ||
				    in->in_phyctxt->sco ==
				    IEEE80211_HTOP0_SCO_SCB) {
					tab |= IWM_RATE_MCS_CHAN_WIDTH_40;
					tab |= IWM_RATE_MCS_RTS_REQUIRED_MSK;
				}
				if (ieee80211_ra_use_ht_sgi(ni))
					tab |= IWM_RATE_MCS_SGI_MSK;
				break;
			}
		} else if (plcp != IWM_RATE_INVM_PLCP) {
			for (i = ni->ni_txrate; i >= 0; i--) {
				if (iwm_rates[ridx].rate == (rs->rs_rates[i] &
				    IEEE80211_RATE_VAL)) {
					tab = plcp;
					break;
				}
			}
		}

		if (tab == 0)
			continue;

		if (iwm_is_mimo_ht_plcp(ht_plcp))
			tab |= IWM_RATE_MCS_ANT_AB_MSK;
		else
			tab |= iwm_valid_siso_ant_rate_mask(sc);

		if (IWM_RIDX_IS_CCK(ridx))
			tab |= IWM_RATE_MCS_CCK_MSK;
		lqcmd->rs_table[j++] = htole32(tab);
	}

	lqcmd->mimo_delim = (mimo ? j : 0);

	/* Fill the rest with the lowest possible rate */
	while (j < nitems(lqcmd->rs_table)) {
		tab = iwm_rates[ridx_min].plcp;
		if (IWM_RIDX_IS_CCK(ridx_min))
			tab |= IWM_RATE_MCS_CCK_MSK;
		tab |= iwm_valid_siso_ant_rate_mask(sc);
		lqcmd->rs_table[j++] = htole32(tab);
	}
}

void
iwm_setrates(struct iwm_node *in, int async)
{
	struct ieee80211_node *ni = &in->in_ni;
	struct ieee80211com *ic = ni->ni_ic;
	struct iwm_softc *sc = IC2IFP(ic)->if_softc;
	struct iwm_lq_cmd lqcmd;
	struct iwm_host_cmd cmd = {
		.id = IWM_LQ_CMD,
		.len = { sizeof(lqcmd), },
	};

	cmd.flags = async ? IWM_CMD_ASYNC : 0;

	memset(&lqcmd, 0, sizeof(lqcmd));
	lqcmd.sta_id = IWM_STATION_ID;

	if (ic->ic_flags & IEEE80211_F_USEPROT)
		lqcmd.flags |= IWM_LQ_FLAG_USE_RTS_MSK;

	if (ni->ni_flags & IEEE80211_NODE_VHT)
		iwm_set_rate_table_vht(in, &lqcmd);
	else
		iwm_set_rate_table(in, &lqcmd);

	if (sc->sc_device_family == IWM_DEVICE_FAMILY_9000 &&
	    (iwm_fw_valid_tx_ant(sc) & IWM_ANT_B))
		lqcmd.single_stream_ant_msk = IWM_ANT_B;
	else
		lqcmd.single_stream_ant_msk = IWM_ANT_A;
	lqcmd.dual_stream_ant_msk = IWM_ANT_AB;

	lqcmd.agg_time_limit = htole16(4000);	/* 4ms */
	lqcmd.agg_disable_start_th = 3;
	lqcmd.agg_frame_cnt_limit = 0x3f;

	cmd.data[0] = &lqcmd;
	iwm_send_cmd(sc, &cmd);
}

int
iwm_media_change(struct ifnet *ifp)
{
	struct iwm_softc *sc = ifp->if_softc;
	struct ieee80211com *ic = &sc->sc_ic;
	uint8_t rate, ridx;
	int err;

	err = ieee80211_media_change(ifp);
	if (err != ENETRESET)
		return err;

	if (ic->ic_fixed_mcs != -1)
		sc->sc_fixed_ridx = iwm_ht_mcs2ridx[ic->ic_fixed_mcs];
	else if (ic->ic_fixed_rate != -1) {
		rate = ic->ic_sup_rates[ic->ic_curmode].
		    rs_rates[ic->ic_fixed_rate] & IEEE80211_RATE_VAL;
		/* Map 802.11 rate to HW rate index. */
		for (ridx = 0; ridx <= IWM_RIDX_MAX; ridx++)
			if (iwm_rates[ridx].rate == rate)
				break;
		sc->sc_fixed_ridx = ridx;
	}

	if ((ifp->if_flags & (IFF_UP | IFF_RUNNING)) ==
	    (IFF_UP | IFF_RUNNING)) {
		iwm_stop(ifp);
		err = iwm_init(ifp);
	}
	return err;
}

void
iwm_newstate_task(void *psc)
{
	struct iwm_softc *sc = (struct iwm_softc *)psc;
	struct ieee80211com *ic = &sc->sc_ic;
	enum ieee80211_state nstate = sc->ns_nstate;
	enum ieee80211_state ostate = ic->ic_state;
	int arg = sc->ns_arg;
	int err = 0, s = splnet();

	if (sc->sc_flags & IWM_FLAG_SHUTDOWN) {
		/* iwm_stop() is waiting for us. */
		refcnt_rele_wake(&sc->task_refs);
		splx(s);
		return;
	}

	if (ostate == IEEE80211_S_SCAN) {
		if (nstate == ostate) {
			if (sc->sc_flags & IWM_FLAG_SCANNING) {
				refcnt_rele_wake(&sc->task_refs);
				splx(s);
				return;
			}
			/* Firmware is no longer scanning. Do another scan. */
			goto next_scan;
		} else
			iwm_led_blink_stop(sc);
	}

	if (nstate <= ostate) {
		switch (ostate) {
		case IEEE80211_S_RUN:
			err = iwm_run_stop(sc);
			if (err)
				goto out;
			/* FALLTHROUGH */
		case IEEE80211_S_ASSOC:
		case IEEE80211_S_AUTH:
			if (nstate <= IEEE80211_S_AUTH) {
				err = iwm_deauth(sc);
				if (err)
					goto out;
			}
			/* FALLTHROUGH */
		case IEEE80211_S_SCAN:
		case IEEE80211_S_INIT:
			break;
		}

		/* Die now if iwm_stop() was called while we were sleeping. */
		if (sc->sc_flags & IWM_FLAG_SHUTDOWN) {
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
		err = iwm_scan(sc);
		if (err)
			break;
		refcnt_rele_wake(&sc->task_refs);
		splx(s);
		return;

	case IEEE80211_S_AUTH:
		err = iwm_auth(sc);
		break;

	case IEEE80211_S_ASSOC:
		break;

	case IEEE80211_S_RUN:
		err = iwm_run(sc);
		break;
	}

out:
	if ((sc->sc_flags & IWM_FLAG_SHUTDOWN) == 0) {
		if (err)
			task_add(systq, &sc->init_task);
		else
			sc->sc_newstate(ic, nstate, arg);
	}
	refcnt_rele_wake(&sc->task_refs);
	splx(s);
}

int
iwm_newstate(struct ieee80211com *ic, enum ieee80211_state nstate, int arg)
{
	struct ifnet *ifp = IC2IFP(ic);
	struct iwm_softc *sc = ifp->if_softc;

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
		timeout_del(&sc->sc_calib_to);
		iwm_del_task(sc, systq, &sc->ba_task);
		iwm_del_task(sc, systq, &sc->mac_ctxt_task);
		iwm_del_task(sc, systq, &sc->phy_ctxt_task);
		iwm_del_task(sc, systq, &sc->bgscan_done_task);
	}

	sc->ns_nstate = nstate;
	sc->ns_arg = arg;

	iwm_add_task(sc, sc->sc_nswq, &sc->newstate_task);

	return 0;
}

void
iwm_endscan(struct iwm_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;

	if ((sc->sc_flags & (IWM_FLAG_SCANNING | IWM_FLAG_BGSCAN)) == 0)
		return;

	sc->sc_flags &= ~(IWM_FLAG_SCANNING | IWM_FLAG_BGSCAN);
	ieee80211_end_scan(&ic->ic_if);
}

/*
 * Aging and idle timeouts for the different possible scenarios
 * in default configuration
 */
static const uint32_t
iwm_sf_full_timeout_def[IWM_SF_NUM_SCENARIO][IWM_SF_NUM_TIMEOUT_TYPES] = {
	{
		htole32(IWM_SF_SINGLE_UNICAST_AGING_TIMER_DEF),
		htole32(IWM_SF_SINGLE_UNICAST_IDLE_TIMER_DEF)
	},
	{
		htole32(IWM_SF_AGG_UNICAST_AGING_TIMER_DEF),
		htole32(IWM_SF_AGG_UNICAST_IDLE_TIMER_DEF)
	},
	{
		htole32(IWM_SF_MCAST_AGING_TIMER_DEF),
		htole32(IWM_SF_MCAST_IDLE_TIMER_DEF)
	},
	{
		htole32(IWM_SF_BA_AGING_TIMER_DEF),
		htole32(IWM_SF_BA_IDLE_TIMER_DEF)
	},
	{
		htole32(IWM_SF_TX_RE_AGING_TIMER_DEF),
		htole32(IWM_SF_TX_RE_IDLE_TIMER_DEF)
	},
};

/*
 * Aging and idle timeouts for the different possible scenarios
 * in single BSS MAC configuration.
 */
static const uint32_t
iwm_sf_full_timeout[IWM_SF_NUM_SCENARIO][IWM_SF_NUM_TIMEOUT_TYPES] = {
	{
		htole32(IWM_SF_SINGLE_UNICAST_AGING_TIMER),
		htole32(IWM_SF_SINGLE_UNICAST_IDLE_TIMER)
	},
	{
		htole32(IWM_SF_AGG_UNICAST_AGING_TIMER),
		htole32(IWM_SF_AGG_UNICAST_IDLE_TIMER)
	},
	{
		htole32(IWM_SF_MCAST_AGING_TIMER),
		htole32(IWM_SF_MCAST_IDLE_TIMER)
	},
	{
		htole32(IWM_SF_BA_AGING_TIMER),
		htole32(IWM_SF_BA_IDLE_TIMER)
	},
	{
		htole32(IWM_SF_TX_RE_AGING_TIMER),
		htole32(IWM_SF_TX_RE_IDLE_TIMER)
	},
};

void
iwm_fill_sf_command(struct iwm_softc *sc, struct iwm_sf_cfg_cmd *sf_cmd,
    struct ieee80211_node *ni)
{
	int i, j, watermark;

	sf_cmd->watermark[IWM_SF_LONG_DELAY_ON] = htole32(IWM_SF_W_MARK_SCAN);

	/*
	 * If we are in association flow - check antenna configuration
	 * capabilities of the AP station, and choose the watermark accordingly.
	 */
	if (ni) {
		if (ni->ni_flags & IEEE80211_NODE_HT) {
			if (ni->ni_rxmcs[1] != 0)
				watermark = IWM_SF_W_MARK_MIMO2;
			else
				watermark = IWM_SF_W_MARK_SISO;
		} else {
			watermark = IWM_SF_W_MARK_LEGACY;
		}
	/* default watermark value for unassociated mode. */
	} else {
		watermark = IWM_SF_W_MARK_MIMO2;
	}
	sf_cmd->watermark[IWM_SF_FULL_ON] = htole32(watermark);

	for (i = 0; i < IWM_SF_NUM_SCENARIO; i++) {
		for (j = 0; j < IWM_SF_NUM_TIMEOUT_TYPES; j++) {
			sf_cmd->long_delay_timeouts[i][j] =
					htole32(IWM_SF_LONG_DELAY_AGING_TIMER);
		}
	}

	if (ni) {
		memcpy(sf_cmd->full_on_timeouts, iwm_sf_full_timeout,
		       sizeof(iwm_sf_full_timeout));
	} else {
		memcpy(sf_cmd->full_on_timeouts, iwm_sf_full_timeout_def,
		       sizeof(iwm_sf_full_timeout_def));
	}

}

int
iwm_sf_config(struct iwm_softc *sc, int new_state)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct iwm_sf_cfg_cmd sf_cmd = {
		.state = htole32(new_state),
	};
	int err = 0;

#if 0	/* only used for models with sdio interface, in iwlwifi */
	if (sc->sc_device_family == IWM_DEVICE_FAMILY_8000)
		sf_cmd.state |= htole32(IWM_SF_CFG_DUMMY_NOTIF_OFF);
#endif

	switch (new_state) {
	case IWM_SF_UNINIT:
	case IWM_SF_INIT_OFF:
		iwm_fill_sf_command(sc, &sf_cmd, NULL);
		break;
	case IWM_SF_FULL_ON:
		iwm_fill_sf_command(sc, &sf_cmd, ic->ic_bss);
		break;
	default:
		return EINVAL;
	}

	err = iwm_send_cmd_pdu(sc, IWM_REPLY_SF_CFG_CMD, IWM_CMD_ASYNC,
				   sizeof(sf_cmd), &sf_cmd);
	return err;
}

int
iwm_send_bt_init_conf(struct iwm_softc *sc)
{
	struct iwm_bt_coex_cmd bt_cmd;

	bt_cmd.mode = htole32(IWM_BT_COEX_WIFI);
	bt_cmd.enabled_modules = htole32(IWM_BT_COEX_HIGH_BAND_RET);

	return iwm_send_cmd_pdu(sc, IWM_BT_CONFIG, 0, sizeof(bt_cmd),
	    &bt_cmd);
}

int
iwm_send_soc_conf(struct iwm_softc *sc)
{
	struct iwm_soc_configuration_cmd cmd;
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
		flags = IWM_SOC_CONFIG_CMD_FLAGS_DISCRETE;
	} else { /* VER_2 */
		uint8_t scan_cmd_ver;
		if (sc->sc_ltr_delay != IWM_SOC_FLAGS_LTR_APPLY_DELAY_NONE)
			flags |= (sc->sc_ltr_delay &
			    IWM_SOC_FLAGS_LTR_APPLY_DELAY_MASK);
		scan_cmd_ver = iwm_lookup_cmd_ver(sc, IWM_LONG_GROUP,
		    IWM_SCAN_REQ_UMAC);
		if (scan_cmd_ver != IWM_FW_CMD_VER_UNKNOWN &&
		    scan_cmd_ver >= 2 && sc->sc_low_latency_xtal)
			flags |= IWM_SOC_CONFIG_CMD_FLAGS_LOW_LATENCY;
	}
	cmd.flags = htole32(flags);

	cmd.latency = htole32(sc->sc_xtal_latency);

	cmd_id = iwm_cmd_id(IWM_SOC_CONFIGURATION_CMD, IWM_SYSTEM_GROUP, 0);
	err = iwm_send_cmd_pdu(sc, cmd_id, 0, sizeof(cmd), &cmd);
	if (err)
		printf("%s: failed to set soc latency: %d\n", DEVNAME(sc), err);
	return err;
}

int
iwm_send_update_mcc_cmd(struct iwm_softc *sc, const char *alpha2)
{
	struct iwm_mcc_update_cmd mcc_cmd;
	struct iwm_host_cmd hcmd = {
		.id = IWM_MCC_UPDATE_CMD,
		.flags = IWM_CMD_WANT_RESP,
		.resp_pkt_len = IWM_CMD_RESP_MAX,
		.data = { &mcc_cmd },
	};
	struct iwm_rx_packet *pkt;
	size_t resp_len;
	int err;
	int resp_v3 = isset(sc->sc_enabled_capa,
	    IWM_UCODE_TLV_CAPA_LAR_SUPPORT_V3);

	if (sc->sc_device_family == IWM_DEVICE_FAMILY_8000 &&
	    !sc->sc_nvm.lar_enabled) {
		return 0;
	}

	memset(&mcc_cmd, 0, sizeof(mcc_cmd));
	mcc_cmd.mcc = htole16(alpha2[0] << 8 | alpha2[1]);
	if (isset(sc->sc_ucode_api, IWM_UCODE_TLV_API_WIFI_MCC_UPDATE) ||
	    isset(sc->sc_enabled_capa, IWM_UCODE_TLV_CAPA_LAR_MULTI_MCC))
		mcc_cmd.source_id = IWM_MCC_SOURCE_GET_CURRENT;
	else
		mcc_cmd.source_id = IWM_MCC_SOURCE_OLD_FW;

	if (resp_v3) { /* same size as resp_v2 */
		hcmd.len[0] = sizeof(struct iwm_mcc_update_cmd);
	} else {
		hcmd.len[0] = sizeof(struct iwm_mcc_update_cmd_v1);
	}

	err = iwm_send_cmd(sc, &hcmd);
	if (err)
		return err;

	pkt = hcmd.resp_pkt;
	if (!pkt || (pkt->hdr.flags & IWM_CMD_FAILED_MSK)) {
		err = EIO;
		goto out;
	}

	if (resp_v3) {
		struct iwm_mcc_update_resp_v3 *resp;
		resp_len = iwm_rx_packet_payload_len(pkt);
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
	} else {
		struct iwm_mcc_update_resp_v1 *resp_v1;
		resp_len = iwm_rx_packet_payload_len(pkt);
		if (resp_len < sizeof(*resp_v1)) {
			err = EIO;
			goto out;
		}

		resp_v1 = (void *)pkt->data;
		if (resp_len != sizeof(*resp_v1) +
		    resp_v1->n_channels * sizeof(resp_v1->channels[0])) {
			err = EIO;
			goto out;
		}
	}
out:
	iwm_free_resp(sc, &hcmd);
	return err;
}

int
iwm_send_temp_report_ths_cmd(struct iwm_softc *sc)
{
	struct iwm_temp_report_ths_cmd cmd;
	int err;

	/*
	 * In order to give responsibility for critical-temperature-kill
	 * and TX backoff to FW we need to send an empty temperature
	 * reporting command at init time.
	 */
	memset(&cmd, 0, sizeof(cmd));

	err = iwm_send_cmd_pdu(sc,
	    IWM_WIDE_ID(IWM_PHY_OPS_GROUP, IWM_TEMP_REPORTING_THRESHOLDS_CMD),
	    0, sizeof(cmd), &cmd);
	if (err)
		printf("%s: TEMP_REPORT_THS_CMD command failed (error %d)\n",
		    DEVNAME(sc), err);

	return err;
}

void
iwm_tt_tx_backoff(struct iwm_softc *sc, uint32_t backoff)
{
	struct iwm_host_cmd cmd = {
		.id = IWM_REPLY_THERMAL_MNG_BACKOFF,
		.len = { sizeof(uint32_t), },
		.data = { &backoff, },
	};

	iwm_send_cmd(sc, &cmd);
}

void
iwm_free_fw_paging(struct iwm_softc *sc)
{
	int i;

	if (sc->fw_paging_db[0].fw_paging_block.vaddr == NULL)
		return;

	for (i = 0; i < IWM_NUM_OF_FW_PAGING_BLOCKS; i++) {
		iwm_dma_contig_free(&sc->fw_paging_db[i].fw_paging_block);
	}

	memset(sc->fw_paging_db, 0, sizeof(sc->fw_paging_db));
}

int
iwm_fill_paging_mem(struct iwm_softc *sc, const struct iwm_fw_sects *image)
{
	int sec_idx, idx;
	uint32_t offset = 0;

	/*
	 * find where is the paging image start point:
	 * if CPU2 exist and it's in paging format, then the image looks like:
	 * CPU1 sections (2 or more)
	 * CPU1_CPU2_SEPARATOR_SECTION delimiter - separate between CPU1 to CPU2
	 * CPU2 sections (not paged)
	 * PAGING_SEPARATOR_SECTION delimiter - separate between CPU2
	 * non paged to CPU2 paging sec
	 * CPU2 paging CSS
	 * CPU2 paging image (including instruction and data)
	 */
	for (sec_idx = 0; sec_idx < IWM_UCODE_SECT_MAX; sec_idx++) {
		if (image->fw_sect[sec_idx].fws_devoff ==
		    IWM_PAGING_SEPARATOR_SECTION) {
			sec_idx++;
			break;
		}
	}

	/*
	 * If paging is enabled there should be at least 2 more sections left
	 * (one for CSS and one for Paging data)
	 */
	if (sec_idx >= nitems(image->fw_sect) - 1) {
		printf("%s: Paging: Missing CSS and/or paging sections\n",
		    DEVNAME(sc));
		iwm_free_fw_paging(sc);
		return EINVAL;
	}

	/* copy the CSS block to the dram */
	DPRINTF(("%s: Paging: load paging CSS to FW, sec = %d\n",
	    DEVNAME(sc), sec_idx));

	memcpy(sc->fw_paging_db[0].fw_paging_block.vaddr,
	    image->fw_sect[sec_idx].fws_data,
	    sc->fw_paging_db[0].fw_paging_size);

	DPRINTF(("%s: Paging: copied %d CSS bytes to first block\n",
	    DEVNAME(sc), sc->fw_paging_db[0].fw_paging_size));

	sec_idx++;

	/*
	 * copy the paging blocks to the dram
	 * loop index start from 1 since that CSS block already copied to dram
	 * and CSS index is 0.
	 * loop stop at num_of_paging_blk since that last block is not full.
	 */
	for (idx = 1; idx < sc->num_of_paging_blk; idx++) {
		memcpy(sc->fw_paging_db[idx].fw_paging_block.vaddr,
		    (const char *)image->fw_sect[sec_idx].fws_data + offset,
		    sc->fw_paging_db[idx].fw_paging_size);

		DPRINTF(("%s: Paging: copied %d paging bytes to block %d\n",
		    DEVNAME(sc), sc->fw_paging_db[idx].fw_paging_size, idx));

		offset += sc->fw_paging_db[idx].fw_paging_size;
	}

	/* copy the last paging block */
	if (sc->num_of_pages_in_last_blk > 0) {
		memcpy(sc->fw_paging_db[idx].fw_paging_block.vaddr,
		    (const char *)image->fw_sect[sec_idx].fws_data + offset,
		    IWM_FW_PAGING_SIZE * sc->num_of_pages_in_last_blk);

		DPRINTF(("%s: Paging: copied %d pages in the last block %d\n",
		    DEVNAME(sc), sc->num_of_pages_in_last_blk, idx));
	}

	return 0;
}

int
iwm_alloc_fw_paging_mem(struct iwm_softc *sc, const struct iwm_fw_sects *image)
{
	int blk_idx = 0;
	int error, num_of_pages;

	if (sc->fw_paging_db[0].fw_paging_block.vaddr != NULL) {
		int i;
		/* Device got reset, and we setup firmware paging again */
		bus_dmamap_sync(sc->sc_dmat,
		    sc->fw_paging_db[0].fw_paging_block.map,
		    0, IWM_FW_PAGING_SIZE,
		    BUS_DMASYNC_POSTWRITE | BUS_DMASYNC_POSTREAD);
		for (i = 1; i < sc->num_of_paging_blk + 1; i++) {
			bus_dmamap_sync(sc->sc_dmat,
			    sc->fw_paging_db[i].fw_paging_block.map,
			    0, IWM_PAGING_BLOCK_SIZE,
			    BUS_DMASYNC_POSTWRITE | BUS_DMASYNC_POSTREAD);
		}
		return 0;
	}

	/* ensure IWM_BLOCK_2_EXP_SIZE is power of 2 of IWM_PAGING_BLOCK_SIZE */
#if (1 << IWM_BLOCK_2_EXP_SIZE) != IWM_PAGING_BLOCK_SIZE
#error IWM_BLOCK_2_EXP_SIZE must be power of 2 of IWM_PAGING_BLOCK_SIZE
#endif

	num_of_pages = image->paging_mem_size / IWM_FW_PAGING_SIZE;
	sc->num_of_paging_blk =
	    ((num_of_pages - 1) / IWM_NUM_OF_PAGE_PER_GROUP) + 1;

	sc->num_of_pages_in_last_blk =
		num_of_pages -
		IWM_NUM_OF_PAGE_PER_GROUP * (sc->num_of_paging_blk - 1);

	DPRINTF(("%s: Paging: allocating mem for %d paging blocks, each block"
	    " holds 8 pages, last block holds %d pages\n", DEVNAME(sc),
	    sc->num_of_paging_blk,
	    sc->num_of_pages_in_last_blk));

	/* allocate block of 4Kbytes for paging CSS */
	error = iwm_dma_contig_alloc(sc->sc_dmat,
	    &sc->fw_paging_db[blk_idx].fw_paging_block, IWM_FW_PAGING_SIZE,
	    4096);
	if (error) {
		/* free all the previous pages since we failed */
		iwm_free_fw_paging(sc);
		return ENOMEM;
	}

	sc->fw_paging_db[blk_idx].fw_paging_size = IWM_FW_PAGING_SIZE;

	DPRINTF(("%s: Paging: allocated 4K(CSS) bytes for firmware paging.\n",
	    DEVNAME(sc)));

	/*
	 * allocate blocks in dram.
	 * since that CSS allocated in fw_paging_db[0] loop start from index 1
	 */
	for (blk_idx = 1; blk_idx < sc->num_of_paging_blk + 1; blk_idx++) {
		/* allocate block of IWM_PAGING_BLOCK_SIZE (32K) */
		/* XXX Use iwm_dma_contig_alloc for allocating */
		error = iwm_dma_contig_alloc(sc->sc_dmat,
		     &sc->fw_paging_db[blk_idx].fw_paging_block,
		    IWM_PAGING_BLOCK_SIZE, 4096);
		if (error) {
			/* free all the previous pages since we failed */
			iwm_free_fw_paging(sc);
			return ENOMEM;
		}

		sc->fw_paging_db[blk_idx].fw_paging_size =
		    IWM_PAGING_BLOCK_SIZE;

		DPRINTF((
		    "%s: Paging: allocated 32K bytes for firmware paging.\n",
		    DEVNAME(sc)));
	}

	return 0;
}

int
iwm_save_fw_paging(struct iwm_softc *sc, const struct iwm_fw_sects *fw)
{
	int ret;

	ret = iwm_alloc_fw_paging_mem(sc, fw);
	if (ret)
		return ret;

	return iwm_fill_paging_mem(sc, fw);
}

/* send paging cmd to FW in case CPU2 has paging image */
int
iwm_send_paging_cmd(struct iwm_softc *sc, const struct iwm_fw_sects *fw)
{
	int blk_idx;
	uint32_t dev_phy_addr;
	struct iwm_fw_paging_cmd fw_paging_cmd = {
		.flags =
			htole32(IWM_PAGING_CMD_IS_SECURED |
				IWM_PAGING_CMD_IS_ENABLED |
				(sc->num_of_pages_in_last_blk <<
				IWM_PAGING_CMD_NUM_OF_PAGES_IN_LAST_GRP_POS)),
		.block_size = htole32(IWM_BLOCK_2_EXP_SIZE),
		.block_num = htole32(sc->num_of_paging_blk),
	};

	/* loop for all paging blocks + CSS block */
	for (blk_idx = 0; blk_idx < sc->num_of_paging_blk + 1; blk_idx++) {
		dev_phy_addr = htole32(
		    sc->fw_paging_db[blk_idx].fw_paging_block.paddr >>
		    IWM_PAGE_2_EXP_SIZE);
		fw_paging_cmd.device_phy_addr[blk_idx] = dev_phy_addr;
		bus_dmamap_sync(sc->sc_dmat,
		    sc->fw_paging_db[blk_idx].fw_paging_block.map, 0,
		    blk_idx == 0 ? IWM_FW_PAGING_SIZE : IWM_PAGING_BLOCK_SIZE,
		    BUS_DMASYNC_PREWRITE | BUS_DMASYNC_PREREAD);
	}

	return iwm_send_cmd_pdu(sc, iwm_cmd_id(IWM_FW_PAGING_BLOCK_CMD,
					       IWM_LONG_GROUP, 0),
	    0, sizeof(fw_paging_cmd), &fw_paging_cmd);
}

int
iwm_init_hw(struct iwm_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	int err, i, ac, qid, s;

	err = iwm_run_init_mvm_ucode(sc, 0);
	if (err)
		return err;

	/* Should stop and start HW since INIT image just loaded. */
	iwm_stop_device(sc);
	err = iwm_start_hw(sc);
	if (err) {
		printf("%s: could not initialize hardware\n", DEVNAME(sc));
		return err;
	}

	/* Restart, this time with the regular firmware */
	s = splnet();
	err = iwm_load_ucode_wait_alive(sc, IWM_UCODE_TYPE_REGULAR);
	if (err) {
		printf("%s: could not load firmware\n", DEVNAME(sc));
		splx(s);
		return err;
	}

	if (!iwm_nic_lock(sc)) {
		splx(s);
		return EBUSY;
	}

	err = iwm_send_tx_ant_cfg(sc, iwm_fw_valid_tx_ant(sc));
	if (err) {
		printf("%s: could not init tx ant config (error %d)\n",
		    DEVNAME(sc), err);
		goto err;
	}

	err = iwm_send_phy_db_data(sc);
	if (err) {
		printf("%s: could not init phy db (error %d)\n",
		    DEVNAME(sc), err);
		goto err;
	}

	err = iwm_send_phy_cfg_cmd(sc);
	if (err) {
		printf("%s: could not send phy config (error %d)\n",
		    DEVNAME(sc), err);
		goto err;
	}

	err = iwm_send_bt_init_conf(sc);
	if (err) {
		printf("%s: could not init bt coex (error %d)\n",
		    DEVNAME(sc), err);
		goto err;
	}

	if (isset(sc->sc_enabled_capa,
	    IWM_UCODE_TLV_CAPA_SOC_LATENCY_SUPPORT)) {
		err = iwm_send_soc_conf(sc);
		if (err)
			goto err;
	}

	if (isset(sc->sc_enabled_capa, IWM_UCODE_TLV_CAPA_DQA_SUPPORT)) {
		err = iwm_send_dqa_cmd(sc);
		if (err)
			goto err;
	}

	/* Add auxiliary station for scanning */
	err = iwm_add_aux_sta(sc);
	if (err) {
		printf("%s: could not add aux station (error %d)\n",
		    DEVNAME(sc), err);
		goto err;
	}

	for (i = 0; i < IWM_NUM_PHY_CTX; i++) {
		/*
		 * The channel used here isn't relevant as it's
		 * going to be overwritten in the other flows.
		 * For now use the first channel we have.
		 */
		sc->sc_phyctxt[i].id = i;
		sc->sc_phyctxt[i].channel = &ic->ic_channels[1];
		err = iwm_phy_ctxt_cmd(sc, &sc->sc_phyctxt[i], 1, 1,
		    IWM_FW_CTXT_ACTION_ADD, 0, IEEE80211_HTOP0_SCO_SCN,
		    IEEE80211_VHTOP0_CHAN_WIDTH_HT);
		if (err) {
			printf("%s: could not add phy context %d (error %d)\n",
			    DEVNAME(sc), i, err);
			goto err;
		}
	}

	/* Initialize tx backoffs to the minimum. */
	if (sc->sc_device_family == IWM_DEVICE_FAMILY_7000)
		iwm_tt_tx_backoff(sc, 0);


	err = iwm_config_ltr(sc);
	if (err) {
		printf("%s: PCIe LTR configuration failed (error %d)\n",
		    DEVNAME(sc), err);
	}

	if (isset(sc->sc_enabled_capa, IWM_UCODE_TLV_CAPA_CT_KILL_BY_FW)) {
		err = iwm_send_temp_report_ths_cmd(sc);
		if (err)
			goto err;
	}

	err = iwm_power_update_device(sc);
	if (err) {
		printf("%s: could not send power command (error %d)\n",
		    DEVNAME(sc), err);
		goto err;
	}

	if (isset(sc->sc_enabled_capa, IWM_UCODE_TLV_CAPA_LAR_SUPPORT)) {
		err = iwm_send_update_mcc_cmd(sc, "ZZ");
		if (err) {
			printf("%s: could not init LAR (error %d)\n",
			    DEVNAME(sc), err);
			goto err;
		}
	}

	if (isset(sc->sc_enabled_capa, IWM_UCODE_TLV_CAPA_UMAC_SCAN)) {
		err = iwm_config_umac_scan(sc);
		if (err) {
			printf("%s: could not configure scan (error %d)\n",
			    DEVNAME(sc), err);
			goto err;
		}
	}

	if (ic->ic_opmode == IEEE80211_M_MONITOR) {
		if (isset(sc->sc_enabled_capa, IWM_UCODE_TLV_CAPA_DQA_SUPPORT))
			qid = IWM_DQA_INJECT_MONITOR_QUEUE;
		else
			qid = IWM_AUX_QUEUE;
		err = iwm_enable_txq(sc, IWM_MONITOR_STA_ID, qid,
		    iwm_ac_to_tx_fifo[EDCA_AC_BE], 0, IWM_MAX_TID_COUNT, 0);
		if (err) {
			printf("%s: could not enable monitor inject Tx queue "
			    "(error %d)\n", DEVNAME(sc), err);
			goto err;
		}
	} else {
		for (ac = 0; ac < EDCA_NUM_AC; ac++) {
			if (isset(sc->sc_enabled_capa,
			    IWM_UCODE_TLV_CAPA_DQA_SUPPORT))
				qid = ac + IWM_DQA_MIN_MGMT_QUEUE;
			else
				qid = ac;
			err = iwm_enable_txq(sc, IWM_STATION_ID, qid,
			    iwm_ac_to_tx_fifo[ac], 0, IWM_TID_NON_QOS, 0);
			if (err) {
				printf("%s: could not enable Tx queue %d "
				    "(error %d)\n", DEVNAME(sc), ac, err);
				goto err;
			}
		}
	}

	err = iwm_disable_beacon_filter(sc);
	if (err) {
		printf("%s: could not disable beacon filter (error %d)\n",
		    DEVNAME(sc), err);
		goto err;
	}

err:
	iwm_nic_unlock(sc);
	splx(s);
	return err;
}

/* Allow multicast from our BSSID. */
int
iwm_allow_mcast(struct iwm_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct iwm_node *in = (void *)ic->ic_bss;
	struct iwm_mcast_filter_cmd *cmd;
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

	err = iwm_send_cmd_pdu(sc, IWM_MCAST_FILTER_CMD,
	    0, size, cmd);
	free(cmd, M_DEVBUF, size);
	return err;
}

int
iwm_init(struct ifnet *ifp)
{
	struct iwm_softc *sc = ifp->if_softc;
	struct ieee80211com *ic = &sc->sc_ic;
	int err, generation;

	rw_assert_wrlock(&sc->ioctl_rwl);

	generation = ++sc->sc_generation;

	err = iwm_preinit(sc);
	if (err)
		return err;

	err = iwm_start_hw(sc);
	if (err) {
		printf("%s: could not initialize hardware\n", DEVNAME(sc));
		return err;
	}

	err = iwm_init_hw(sc);
	if (err) {
		if (generation == sc->sc_generation)
			iwm_stop_device(sc);
		return err;
	}

	if (sc->sc_nvm.sku_cap_11n_enable)
		iwm_setup_ht_rates(sc);
	if (sc->sc_nvm.sku_cap_11ac_enable)
		iwm_setup_vht_rates(sc);

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
	 * ieee80211_begin_scan() ends up scheduling iwm_newstate_task().
	 * Wait until the transition to SCAN state has completed.
	 */
	do {
		err = tsleep_nsec(&ic->ic_state, PCATCH, "iwminit",
		    SEC_TO_NSEC(1));
		if (generation != sc->sc_generation)
			return ENXIO;
		if (err) {
			iwm_stop(ifp);
			return err;
		}
	} while (ic->ic_state != IEEE80211_S_SCAN);

	return 0;
}

void
iwm_start(struct ifnet *ifp)
{
	struct iwm_softc *sc = ifp->if_softc;
	struct ieee80211com *ic = &sc->sc_ic;
	struct ieee80211_node *ni;
	struct ether_header *eh;
	struct mbuf *m;
	int ac = EDCA_AC_BE; /* XXX */

	if (!(ifp->if_flags & IFF_RUNNING) || ifq_is_oactive(&ifp->if_snd))
		return;

	for (;;) {
		/* why isn't this done per-queue? */
		if (sc->qfullmsk != 0) {
			ifq_set_oactive(&ifp->if_snd);
			break;
		}

		/* Don't queue additional frames while flushing Tx queues. */
		if (sc->sc_flags & IWM_FLAG_TXFLUSH)
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
		if (iwm_tx(sc, m, ni, ac) != 0) {
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
iwm_stop(struct ifnet *ifp)
{
	struct iwm_softc *sc = ifp->if_softc;
	struct ieee80211com *ic = &sc->sc_ic;
	struct iwm_node *in = (void *)ic->ic_bss;
	int i, s = splnet();

	rw_assert_wrlock(&sc->ioctl_rwl);

	sc->sc_flags |= IWM_FLAG_SHUTDOWN; /* Disallow new tasks. */

	/* Cancel scheduled tasks and let any stale tasks finish up. */
	task_del(systq, &sc->init_task);
	iwm_del_task(sc, sc->sc_nswq, &sc->newstate_task);
	iwm_del_task(sc, systq, &sc->ba_task);
	iwm_del_task(sc, systq, &sc->mac_ctxt_task);
	iwm_del_task(sc, systq, &sc->phy_ctxt_task);
	iwm_del_task(sc, systq, &sc->bgscan_done_task);
	KASSERT(sc->task_refs.r_refs >= 1);
	refcnt_finalize(&sc->task_refs, "iwmstop");

	iwm_stop_device(sc);

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
	in->tid_disable_ampdu = 0xffff;
	in->tfd_queue_msk = 0;
	IEEE80211_ADDR_COPY(in->in_macaddr, etheranyaddr);

	sc->sc_flags &= ~(IWM_FLAG_SCANNING | IWM_FLAG_BGSCAN);
	sc->sc_flags &= ~IWM_FLAG_MAC_ACTIVE;
	sc->sc_flags &= ~IWM_FLAG_BINDING_ACTIVE;
	sc->sc_flags &= ~IWM_FLAG_STA_ACTIVE;
	sc->sc_flags &= ~IWM_FLAG_TE_ACTIVE;
	sc->sc_flags &= ~IWM_FLAG_HW_ERR;
	sc->sc_flags &= ~IWM_FLAG_SHUTDOWN;
	sc->sc_flags &= ~IWM_FLAG_TXFLUSH;

	sc->sc_rx_ba_sessions = 0;
	sc->ba_rx.start_tidmask = 0;
	sc->ba_rx.stop_tidmask = 0;
	sc->tx_ba_queue_mask = 0;
	sc->ba_tx.start_tidmask = 0;
	sc->ba_tx.stop_tidmask = 0;

	sc->sc_newstate(ic, IEEE80211_S_INIT, -1);
	sc->ns_nstate = IEEE80211_S_INIT;

	timeout_del(&sc->sc_calib_to); /* XXX refcount? */
	for (i = 0; i < nitems(sc->sc_rxba_data); i++) {
		struct iwm_rxba_data *rxba = &sc->sc_rxba_data[i];
		iwm_clear_reorder_buffer(sc, rxba);
	}
	iwm_led_blink_stop(sc);
	memset(sc->sc_tx_timer, 0, sizeof(sc->sc_tx_timer));
	ifp->if_timer = 0;

	splx(s);
}

void
iwm_watchdog(struct ifnet *ifp)
{
	struct iwm_softc *sc = ifp->if_softc;
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
					iwm_nic_error(sc);
					iwm_dump_driver_status(sc);
				}
				if ((sc->sc_flags & IWM_FLAG_SHUTDOWN) == 0)
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
iwm_ioctl(struct ifnet *ifp, u_long cmd, caddr_t data)
{
	struct iwm_softc *sc = ifp->if_softc;
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
				sc->sc_fw.fw_status = IWM_FW_STATUS_NONE;
				err = iwm_init(ifp);
			}
		} else {
			if (ifp->if_flags & IFF_RUNNING)
				iwm_stop(ifp);
		}
		break;

	default:
		err = ieee80211_ioctl(ifp, cmd, data);
	}

	if (err == ENETRESET) {
		err = 0;
		if ((ifp->if_flags & (IFF_UP | IFF_RUNNING)) ==
		    (IFF_UP | IFF_RUNNING)) {
			iwm_stop(ifp);
			err = iwm_init(ifp);
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
struct iwm_error_event_table {
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
struct iwm_umac_error_event_table {
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
iwm_nic_umac_error(struct iwm_softc *sc)
{
	struct iwm_umac_error_event_table table;
	uint32_t base;

	base = sc->sc_uc.uc_umac_error_event_table;

	if (base < 0x800000) {
		printf("%s: Invalid error log pointer 0x%08x\n",
		    DEVNAME(sc), base);
		return;
	}

	if (iwm_read_mem(sc, base, &table, sizeof(table)/sizeof(uint32_t))) {
		printf("%s: reading errlog failed\n", DEVNAME(sc));
		return;
	}

	if (ERROR_START_OFFSET <= table.valid * ERROR_ELEM_SIZE) {
		printf("%s: Start UMAC Error Log Dump:\n", DEVNAME(sc));
		printf("%s: Status: 0x%x, count: %d\n", DEVNAME(sc),
			sc->sc_flags, table.valid);
	}

	printf("%s: 0x%08X | %s\n", DEVNAME(sc), table.error_id,
		iwm_desc_lookup(table.error_id));
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

#define IWM_FW_SYSASSERT_CPU_MASK 0xf0000000
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
iwm_desc_lookup(uint32_t num)
{
	int i;

	for (i = 0; i < nitems(advanced_lookup) - 1; i++)
		if (advanced_lookup[i].num ==
		    (num & ~IWM_FW_SYSASSERT_CPU_MASK))
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
iwm_nic_error(struct iwm_softc *sc)
{
	struct iwm_error_event_table table;
	uint32_t base;

	printf("%s: dumping device error log\n", DEVNAME(sc));
	base = sc->sc_uc.uc_error_event_table;
	if (base < 0x800000) {
		printf("%s: Invalid error log pointer 0x%08x\n",
		    DEVNAME(sc), base);
		return;
	}

	if (iwm_read_mem(sc, base, &table, sizeof(table)/sizeof(uint32_t))) {
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
	    iwm_desc_lookup(table.error_id));
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
		iwm_nic_umac_error(sc);
}

void
iwm_dump_driver_status(struct iwm_softc *sc)
{
	int i;

	printf("driver status:\n");
	for (i = 0; i < IWM_MAX_QUEUES; i++) {
		struct iwm_tx_ring *ring = &sc->txq[i];
		printf("  tx ring %2d: qid=%-2d cur=%-3d "
		    "queued=%-3d\n",
		    i, ring->qid, ring->cur, ring->queued);
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

#define ADVANCE_RXQ(sc) (sc->rxq.cur = (sc->rxq.cur + 1) % count);

int
iwm_rx_pkt_valid(struct iwm_rx_packet *pkt)
{
	int qid, idx, code;

	qid = pkt->hdr.qid & ~0x80;
	idx = pkt->hdr.idx;
	code = IWM_WIDE_ID(pkt->hdr.flags, pkt->hdr.code);

	return (!(qid == 0 && idx == 0 && code == 0) &&
	    pkt->len_n_flags != htole32(IWM_FH_RSCSR_FRAME_INVALID));
}

void
iwm_rx_pkt(struct iwm_softc *sc, struct iwm_rx_data *data, struct mbuf_list *ml)
{
	struct ifnet *ifp = IC2IFP(&sc->sc_ic);
	struct iwm_rx_packet *pkt, *nextpkt;
	uint32_t offset = 0, nextoff = 0, nmpdu = 0, len;
	struct mbuf *m0, *m;
	const size_t minsz = sizeof(pkt->len_n_flags) + sizeof(pkt->hdr);
	int qid, idx, code, handled = 1;

	bus_dmamap_sync(sc->sc_dmat, data->map, 0, IWM_RBUF_SIZE,
	    BUS_DMASYNC_POSTREAD);

	m0 = data->m;
	while (m0 && offset + minsz < IWM_RBUF_SIZE) {
		pkt = (struct iwm_rx_packet *)(m0->m_data + offset);
		qid = pkt->hdr.qid;
		idx = pkt->hdr.idx;

		code = IWM_WIDE_ID(pkt->hdr.flags, pkt->hdr.code);

		if (!iwm_rx_pkt_valid(pkt))
			break;

		len = sizeof(pkt->len_n_flags) + iwm_rx_packet_len(pkt);
		if (len < minsz || len > (IWM_RBUF_SIZE - offset))
			break;

		if (code == IWM_REPLY_RX_MPDU_CMD && ++nmpdu == 1) {
			/* Take mbuf m0 off the RX ring. */
			if (iwm_rx_addbuf(sc, IWM_RBUF_SIZE, sc->rxq.cur)) {
				ifp->if_ierrors++;
				break;
			}
			KASSERT(data->m != m0);
		}

		switch (code) {
		case IWM_REPLY_RX_PHY_CMD:
			iwm_rx_rx_phy_cmd(sc, pkt, data);
			break;

		case IWM_REPLY_RX_MPDU_CMD: {
			size_t maxlen = IWM_RBUF_SIZE - offset - minsz;
			nextoff = offset +
			    roundup(len, IWM_FH_RSCSR_FRAME_ALIGN);
			nextpkt = (struct iwm_rx_packet *)
			    (m0->m_data + nextoff);
			if (nextoff + minsz >= IWM_RBUF_SIZE ||
			    !iwm_rx_pkt_valid(nextpkt)) {
				/* No need to copy last frame in buffer. */
				if (offset > 0)
					m_adj(m0, offset);
				if (sc->sc_mqrx_supported)
					iwm_rx_mpdu_mq(sc, m0, pkt->data,
					    maxlen, ml);
				else
					iwm_rx_mpdu(sc, m0, pkt->data,
					    maxlen, ml);
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
				if (sc->sc_mqrx_supported)
					iwm_rx_mpdu_mq(sc, m, pkt->data,
					    maxlen, ml);
				else
					iwm_rx_mpdu(sc, m, pkt->data,
					    maxlen, ml);
			}
 			break;
		}

		case IWM_TX_CMD:
			iwm_rx_tx_cmd(sc, pkt, data);
			break;

		case IWM_BA_NOTIF:
			iwm_rx_compressed_ba(sc, pkt);
			break;

		case IWM_MISSED_BEACONS_NOTIFICATION:
			iwm_rx_bmiss(sc, pkt, data);
			break;

		case IWM_MFUART_LOAD_NOTIFICATION:
			break;

		case IWM_ALIVE: {
			struct iwm_alive_resp_v1 *resp1;
			struct iwm_alive_resp_v2 *resp2;
			struct iwm_alive_resp_v3 *resp3;

			if (iwm_rx_packet_payload_len(pkt) == sizeof(*resp1)) {
				SYNC_RESP_STRUCT(resp1, pkt);
				sc->sc_uc.uc_error_event_table
				    = le32toh(resp1->error_event_table_ptr);
				sc->sc_uc.uc_log_event_table
				    = le32toh(resp1->log_event_table_ptr);
				sc->sched_base = le32toh(resp1->scd_base_ptr);
				if (resp1->status == IWM_ALIVE_STATUS_OK)
					sc->sc_uc.uc_ok = 1;
				else
					sc->sc_uc.uc_ok = 0;
			}

			if (iwm_rx_packet_payload_len(pkt) == sizeof(*resp2)) {
				SYNC_RESP_STRUCT(resp2, pkt);
				sc->sc_uc.uc_error_event_table
				    = le32toh(resp2->error_event_table_ptr);
				sc->sc_uc.uc_log_event_table
				    = le32toh(resp2->log_event_table_ptr);
				sc->sched_base = le32toh(resp2->scd_base_ptr);
				sc->sc_uc.uc_umac_error_event_table
				    = le32toh(resp2->error_info_addr);
				if (resp2->status == IWM_ALIVE_STATUS_OK)
					sc->sc_uc.uc_ok = 1;
				else
					sc->sc_uc.uc_ok = 0;
			}

			if (iwm_rx_packet_payload_len(pkt) == sizeof(*resp3)) {
				SYNC_RESP_STRUCT(resp3, pkt);
				sc->sc_uc.uc_error_event_table
				    = le32toh(resp3->error_event_table_ptr);
				sc->sc_uc.uc_log_event_table
				    = le32toh(resp3->log_event_table_ptr);
				sc->sched_base = le32toh(resp3->scd_base_ptr);
				sc->sc_uc.uc_umac_error_event_table
				    = le32toh(resp3->error_info_addr);
				if (resp3->status == IWM_ALIVE_STATUS_OK)
					sc->sc_uc.uc_ok = 1;
				else
					sc->sc_uc.uc_ok = 0;
			}

			sc->sc_uc.uc_intr = 1;
			wakeup(&sc->sc_uc);
			break;
		}

		case IWM_CALIB_RES_NOTIF_PHY_DB: {
			struct iwm_calib_res_notif_phy_db *phy_db_notif;
			SYNC_RESP_STRUCT(phy_db_notif, pkt);
			iwm_phy_db_set_section(sc, phy_db_notif);
			sc->sc_init_complete |= IWM_CALIB_COMPLETE;
			wakeup(&sc->sc_init_complete);
			break;
		}

		case IWM_STATISTICS_NOTIFICATION: {
			struct iwm_notif_statistics *stats;
			SYNC_RESP_STRUCT(stats, pkt);
			memcpy(&sc->sc_stats, stats, sizeof(sc->sc_stats));
			sc->sc_noise = iwm_get_noise(&stats->rx.general);
			break;
		}

		case IWM_MCC_CHUB_UPDATE_CMD: {
			struct iwm_mcc_chub_notif *notif;
			SYNC_RESP_STRUCT(notif, pkt);
			iwm_mcc_update(sc, notif);
			break;
		}

		case IWM_DTS_MEASUREMENT_NOTIFICATION:
		case IWM_WIDE_ID(IWM_PHY_OPS_GROUP,
				 IWM_DTS_MEASUREMENT_NOTIF_WIDE):
		case IWM_WIDE_ID(IWM_PHY_OPS_GROUP,
				 IWM_TEMP_REPORTING_THRESHOLDS_CMD):
			break;

		case IWM_WIDE_ID(IWM_PHY_OPS_GROUP,
		    IWM_CT_KILL_NOTIFICATION): {
			struct iwm_ct_kill_notif *notif;
			SYNC_RESP_STRUCT(notif, pkt);
			printf("%s: device at critical temperature (%u degC), "
			    "stopping device\n",
			    DEVNAME(sc), le16toh(notif->temperature));
			sc->sc_flags |= IWM_FLAG_HW_ERR;
			task_add(systq, &sc->init_task);
			break;
		}

		case IWM_ADD_STA_KEY:
		case IWM_PHY_CONFIGURATION_CMD:
		case IWM_TX_ANT_CONFIGURATION_CMD:
		case IWM_ADD_STA:
		case IWM_MAC_CONTEXT_CMD:
		case IWM_REPLY_SF_CFG_CMD:
		case IWM_POWER_TABLE_CMD:
		case IWM_LTR_CONFIG:
		case IWM_PHY_CONTEXT_CMD:
		case IWM_BINDING_CONTEXT_CMD:
		case IWM_WIDE_ID(IWM_LONG_GROUP, IWM_SCAN_CFG_CMD):
		case IWM_WIDE_ID(IWM_LONG_GROUP, IWM_SCAN_REQ_UMAC):
		case IWM_WIDE_ID(IWM_LONG_GROUP, IWM_SCAN_ABORT_UMAC):
		case IWM_SCAN_OFFLOAD_REQUEST_CMD:
		case IWM_SCAN_OFFLOAD_ABORT_CMD:
		case IWM_REPLY_BEACON_FILTERING_CMD:
		case IWM_MAC_PM_POWER_TABLE:
		case IWM_TIME_QUOTA_CMD:
		case IWM_REMOVE_STA:
		case IWM_TXPATH_FLUSH:
		case IWM_LQ_CMD:
		case IWM_WIDE_ID(IWM_LONG_GROUP,
				 IWM_FW_PAGING_BLOCK_CMD):
		case IWM_BT_CONFIG:
		case IWM_REPLY_THERMAL_MNG_BACKOFF:
		case IWM_NVM_ACCESS_CMD:
		case IWM_MCC_UPDATE_CMD:
		case IWM_TIME_EVENT_CMD: {
			size_t pkt_len;

			if (sc->sc_cmd_resp_pkt[idx] == NULL)
				break;

			bus_dmamap_sync(sc->sc_dmat, data->map, 0,
			    sizeof(*pkt), BUS_DMASYNC_POSTREAD);

			pkt_len = sizeof(pkt->len_n_flags) +
			    iwm_rx_packet_len(pkt);

			if ((pkt->hdr.flags & IWM_CMD_FAILED_MSK) ||
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

		/* ignore */
		case IWM_PHY_DB_CMD:
			break;

		case IWM_INIT_COMPLETE_NOTIF:
			sc->sc_init_complete |= IWM_INIT_COMPLETE;
			wakeup(&sc->sc_init_complete);
			break;

		case IWM_SCAN_OFFLOAD_COMPLETE: {
			struct iwm_periodic_scan_complete *notif;
			SYNC_RESP_STRUCT(notif, pkt);
			break;
		}

		case IWM_SCAN_ITERATION_COMPLETE: {
			struct iwm_lmac_scan_complete_notif *notif;
			SYNC_RESP_STRUCT(notif, pkt);
			iwm_endscan(sc);
			break;
		}

		case IWM_SCAN_COMPLETE_UMAC: {
			struct iwm_umac_scan_complete *notif;
			SYNC_RESP_STRUCT(notif, pkt);
			iwm_endscan(sc);
			break;
		}

		case IWM_SCAN_ITERATION_COMPLETE_UMAC: {
			struct iwm_umac_scan_iter_complete_notif *notif;
			SYNC_RESP_STRUCT(notif, pkt);
			iwm_endscan(sc);
			break;
		}

		case IWM_REPLY_ERROR: {
			struct iwm_error_resp *resp;
			SYNC_RESP_STRUCT(resp, pkt);
			printf("%s: firmware error 0x%x, cmd 0x%x\n",
				DEVNAME(sc), le32toh(resp->error_type),
				resp->cmd_id);
			break;
		}

		case IWM_TIME_EVENT_NOTIFICATION: {
			struct iwm_time_event_notif *notif;
			uint32_t action;
			SYNC_RESP_STRUCT(notif, pkt);

			if (sc->sc_time_event_uid != le32toh(notif->unique_id))
				break;
			action = le32toh(notif->action);
			if (action & IWM_TE_V2_NOTIF_HOST_EVENT_END)
				sc->sc_flags &= ~IWM_FLAG_TE_ACTIVE;
			break;
		}

		case IWM_WIDE_ID(IWM_SYSTEM_GROUP,
		    IWM_FSEQ_VER_MISMATCH_NOTIFICATION):
		    break;

		/*
		 * Firmware versions 21 and 22 generate some DEBUG_LOG_MSG
		 * messages. Just ignore them for now.
		 */
		case IWM_DEBUG_LOG_MSG:
			break;

		case IWM_MCAST_FILTER_CMD:
			break;

		case IWM_SCD_QUEUE_CFG: {
			struct iwm_scd_txq_cfg_rsp *rsp;
			SYNC_RESP_STRUCT(rsp, pkt);

			break;
		}

		case IWM_WIDE_ID(IWM_DATA_PATH_GROUP, IWM_DQA_ENABLE_CMD):
			break;

		case IWM_WIDE_ID(IWM_SYSTEM_GROUP, IWM_SOC_CONFIGURATION_CMD):
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
		 * For example, uCode issues IWM_REPLY_RX when it sends a
		 * received frame to the driver.
		 */
		if (handled && !(qid & (1 << 7))) {
			iwm_cmd_done(sc, qid, idx, code);
		}

		offset += roundup(len, IWM_FH_RSCSR_FRAME_ALIGN);
	}

	if (m0 && m0 != data->m)
		m_freem(m0);
}

void
iwm_notif_intr(struct iwm_softc *sc)
{
	struct mbuf_list ml = MBUF_LIST_INITIALIZER();
	uint32_t wreg;
	uint16_t hw;
	int count;

	bus_dmamap_sync(sc->sc_dmat, sc->rxq.stat_dma.map,
	    0, sc->rxq.stat_dma.size, BUS_DMASYNC_POSTREAD);

	if (sc->sc_mqrx_supported) {
		count = IWM_RX_MQ_RING_COUNT;
		wreg = IWM_RFH_Q0_FRBDCB_WIDX_TRG;
	} else {
		count = IWM_RX_RING_COUNT;
		wreg = IWM_FH_RSCSR_CHNL0_WPTR;
	}

	hw = le16toh(sc->rxq.stat->closed_rb_num) & 0xfff;
	hw &= (count - 1);
	while (sc->rxq.cur != hw) {
		struct iwm_rx_data *data = &sc->rxq.data[sc->rxq.cur];
		iwm_rx_pkt(sc, data, &ml);
		ADVANCE_RXQ(sc);
	}
	if_input(&sc->sc_ic.ic_if, &ml);

	/*
	 * Tell the firmware what we have processed.
	 * Seems like the hardware gets upset unless we align the write by 8??
	 */
	hw = (hw == 0) ? count - 1 : hw - 1;
	IWM_WRITE(sc, wreg, hw & ~7);
}

int
iwm_intr(void *arg)
{
	struct iwm_softc *sc = arg;
	struct ieee80211com *ic = &sc->sc_ic;
	struct ifnet *ifp = IC2IFP(ic);
	int handled = 0;
	int rv = 0;
	uint32_t r1, r2;

	IWM_WRITE(sc, IWM_CSR_INT_MASK, 0);

	if (sc->sc_flags & IWM_FLAG_USE_ICT) {
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
			sc->ict_cur = (sc->ict_cur+1) % IWM_ICT_COUNT;
			tmp = htole32(ict[sc->ict_cur]);
		}

		/* this is where the fun begins.  don't ask */
		if (r1 == 0xffffffff)
			r1 = 0;

		/*
		 * Workaround for hardware bug where bits are falsely cleared
		 * when using interrupt coalescing.  Bit 15 should be set if
		 * bits 18 and 19 are set.
		 */
		if (r1 & 0xc0000)
			r1 |= 0x8000;

		r1 = (0xff & r1) | ((0xff00 & r1) << 16);
	} else {
		r1 = IWM_READ(sc, IWM_CSR_INT);
		r2 = IWM_READ(sc, IWM_CSR_FH_INT_STATUS);
	}
	if (r1 == 0 && r2 == 0) {
		goto out_ena;
	}
	if (r1 == 0xffffffff || (r1 & 0xfffffff0) == 0xa5a5a5a0)
		goto out;

	IWM_WRITE(sc, IWM_CSR_INT, r1 | ~sc->sc_intmask);

	/* ignored */
	handled |= (r1 & (IWM_CSR_INT_BIT_ALIVE /*| IWM_CSR_INT_BIT_SCD*/));

	if (r1 & IWM_CSR_INT_BIT_RF_KILL) {
		handled |= IWM_CSR_INT_BIT_RF_KILL;
		iwm_check_rfkill(sc);
		task_add(systq, &sc->init_task);
		rv = 1;
		goto out_ena;
	}

	if (r1 & IWM_CSR_INT_BIT_SW_ERR) {
		if (ifp->if_flags & IFF_DEBUG) {
			iwm_nic_error(sc);
			iwm_dump_driver_status(sc);
		}
		printf("%s: fatal firmware error\n", DEVNAME(sc));
		if ((sc->sc_flags & IWM_FLAG_SHUTDOWN) == 0)
			task_add(systq, &sc->init_task);
		rv = 1;
		goto out;

	}

	if (r1 & IWM_CSR_INT_BIT_HW_ERR) {
		handled |= IWM_CSR_INT_BIT_HW_ERR;
		printf("%s: hardware error, stopping device \n", DEVNAME(sc));
		if ((sc->sc_flags & IWM_FLAG_SHUTDOWN) == 0) {
			sc->sc_flags |= IWM_FLAG_HW_ERR;
			task_add(systq, &sc->init_task);
		}
		rv = 1;
		goto out;
	}

	/* firmware chunk loaded */
	if (r1 & IWM_CSR_INT_BIT_FH_TX) {
		IWM_WRITE(sc, IWM_CSR_FH_INT_STATUS, IWM_CSR_FH_INT_TX_MASK);
		handled |= IWM_CSR_INT_BIT_FH_TX;

		sc->sc_fw_chunk_done = 1;
		wakeup(&sc->sc_fw);
	}

	if (r1 & (IWM_CSR_INT_BIT_FH_RX | IWM_CSR_INT_BIT_SW_RX |
	    IWM_CSR_INT_BIT_RX_PERIODIC)) {
		if (r1 & (IWM_CSR_INT_BIT_FH_RX | IWM_CSR_INT_BIT_SW_RX)) {
			handled |= (IWM_CSR_INT_BIT_FH_RX | IWM_CSR_INT_BIT_SW_RX);
			IWM_WRITE(sc, IWM_CSR_FH_INT_STATUS, IWM_CSR_FH_INT_RX_MASK);
		}
		if (r1 & IWM_CSR_INT_BIT_RX_PERIODIC) {
			handled |= IWM_CSR_INT_BIT_RX_PERIODIC;
			IWM_WRITE(sc, IWM_CSR_INT, IWM_CSR_INT_BIT_RX_PERIODIC);
		}

		/* Disable periodic interrupt; we use it as just a one-shot. */
		IWM_WRITE_1(sc, IWM_CSR_INT_PERIODIC_REG, IWM_CSR_INT_PERIODIC_DIS);

		/*
		 * Enable periodic interrupt in 8 msec only if we received
		 * real RX interrupt (instead of just periodic int), to catch
		 * any dangling Rx interrupt.  If it was just the periodic
		 * interrupt, there was no dangling Rx activity, and no need
		 * to extend the periodic interrupt; one-shot is enough.
		 */
		if (r1 & (IWM_CSR_INT_BIT_FH_RX | IWM_CSR_INT_BIT_SW_RX))
			IWM_WRITE_1(sc, IWM_CSR_INT_PERIODIC_REG,
			    IWM_CSR_INT_PERIODIC_ENA);

		iwm_notif_intr(sc);
	}

	rv = 1;

 out_ena:
	iwm_restore_interrupts(sc);
 out:
	return rv;
}

int
iwm_intr_msix(void *arg)
{
	struct iwm_softc *sc = arg;
	struct ieee80211com *ic = &sc->sc_ic;
	struct ifnet *ifp = IC2IFP(ic);
	uint32_t inta_fh, inta_hw;
	int vector = 0;

	inta_fh = IWM_READ(sc, IWM_CSR_MSIX_FH_INT_CAUSES_AD);
	inta_hw = IWM_READ(sc, IWM_CSR_MSIX_HW_INT_CAUSES_AD);
	IWM_WRITE(sc, IWM_CSR_MSIX_FH_INT_CAUSES_AD, inta_fh);
	IWM_WRITE(sc, IWM_CSR_MSIX_HW_INT_CAUSES_AD, inta_hw);
	inta_fh &= sc->sc_fh_mask;
	inta_hw &= sc->sc_hw_mask;

	if (inta_fh & IWM_MSIX_FH_INT_CAUSES_Q0 ||
	    inta_fh & IWM_MSIX_FH_INT_CAUSES_Q1) {
		iwm_notif_intr(sc);
	}

	/* firmware chunk loaded */
	if (inta_fh & IWM_MSIX_FH_INT_CAUSES_D2S_CH0_NUM) {
		sc->sc_fw_chunk_done = 1;
		wakeup(&sc->sc_fw);
	}

	if ((inta_fh & IWM_MSIX_FH_INT_CAUSES_FH_ERR) ||
	    (inta_hw & IWM_MSIX_HW_INT_CAUSES_REG_SW_ERR) ||
	    (inta_hw & IWM_MSIX_HW_INT_CAUSES_REG_SW_ERR_V2)) {
		if (ifp->if_flags & IFF_DEBUG) {
			iwm_nic_error(sc);
			iwm_dump_driver_status(sc);
		}
		printf("%s: fatal firmware error\n", DEVNAME(sc));
		if ((sc->sc_flags & IWM_FLAG_SHUTDOWN) == 0)
			task_add(systq, &sc->init_task);
		return 1;
	}

	if (inta_hw & IWM_MSIX_HW_INT_CAUSES_REG_RF_KILL) {
		iwm_check_rfkill(sc);
		task_add(systq, &sc->init_task);
	}

	if (inta_hw & IWM_MSIX_HW_INT_CAUSES_REG_HW_ERR) {
		printf("%s: hardware error, stopping device \n", DEVNAME(sc));
		if ((sc->sc_flags & IWM_FLAG_SHUTDOWN) == 0) {
			sc->sc_flags |= IWM_FLAG_HW_ERR;
			task_add(systq, &sc->init_task);
		}
		return 1;
	}

	/*
	 * Before sending the interrupt the HW disables it to prevent
	 * a nested interrupt. This is done by writing 1 to the corresponding
	 * bit in the mask register. After handling the interrupt, it should be
	 * re-enabled by clearing this bit. This register is defined as
	 * write 1 clear (W1C) register, meaning that it's being clear
	 * by writing 1 to the bit.
	 */
	IWM_WRITE(sc, IWM_CSR_MSIX_AUTOMASK_ST_AD, 1 << vector);
	return 1;
}

typedef void *iwm_match_t;

static const struct pci_matchid iwm_devices[] = {
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_WL_3160_1 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_WL_3160_2 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_WL_3165_1 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_WL_3165_2 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_WL_3168_1 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_WL_7260_1 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_WL_7260_2 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_WL_7265_1 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_WL_7265_2 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_WL_8260_1 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_WL_8260_2 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_WL_8265_1 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_WL_9260_1 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_WL_9560_1 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_WL_9560_2 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_WL_9560_3 },
};

int
iwm_match(struct device *parent, iwm_match_t match __unused, void *aux)
{
	return pci_matchbyid((struct pci_attach_args *)aux, iwm_devices,
	    nitems(iwm_devices));
}

int
iwm_preinit(struct iwm_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ifnet *ifp = IC2IFP(ic);
	int err;

	err = iwm_prepare_card_hw(sc);
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

	err = iwm_start_hw(sc);
	if (err) {
		printf("%s: could not initialize hardware\n", DEVNAME(sc));
		return err;
	}

	err = iwm_run_init_mvm_ucode(sc, 1);
	iwm_stop_device(sc);
	if (err)
		return err;

	/* Print version info and MAC address on first successful fw load. */
	sc->attached = 1;
	printf("%s: hw rev 0x%x, fw ver %s, address %s\n",
	    DEVNAME(sc), sc->sc_hw_rev & IWM_CSR_HW_REV_TYPE_MSK,
	    sc->sc_fwver, ether_sprintf(sc->sc_nvm.hw_addr));

	if (sc->sc_nvm.sku_cap_11n_enable)
		iwm_setup_ht_rates(sc);

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

	ieee80211_media_init(ifp, iwm_media_change, ieee80211_media_status);

	return 0;
}

void
iwm_attach_hook(struct device *self)
{
	struct iwm_softc *sc = (void *)self;

	KASSERT(!cold);

	iwm_preinit(sc);
}

void
iwm_attach(struct device *parent, struct device *self, void *aux)
{
	struct iwm_softc *sc = (void *)self;
	struct pci_attach_args *pa = aux;
	pci_intr_handle_t ih;
	pcireg_t reg, memtype;
	struct ieee80211com *ic = &sc->sc_ic;
	struct ifnet *ifp = &ic->ic_if;
	const char *intrstr;
	int err;
	int txq_i, i, j;

	sc->sc_pct = pa->pa_pc;
	sc->sc_pcitag = pa->pa_tag;
	sc->sc_dmat = pa->pa_dmat;

	rw_init(&sc->ioctl_rwl, "iwmioctl");

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
		    iwm_intr_msix, sc, DEVNAME(sc));
	else
		sc->sc_ih = pci_intr_establish(sc->sc_pct, ih, IPL_NET,
		    iwm_intr, sc, DEVNAME(sc));

	if (sc->sc_ih == NULL) {
		printf("\n");
		printf("%s: can't establish interrupt", DEVNAME(sc));
		if (intrstr != NULL)
			printf(" at %s", intrstr);
		printf("\n");
		return;
	}
	printf(", %s\n", intrstr);

	sc->sc_hw_rev = IWM_READ(sc, IWM_CSR_HW_REV);
	switch (PCI_PRODUCT(pa->pa_id)) {
	case PCI_PRODUCT_INTEL_WL_3160_1:
	case PCI_PRODUCT_INTEL_WL_3160_2:
		sc->sc_fwname = "iwm-3160-17";
		sc->host_interrupt_operation_mode = 1;
		sc->sc_device_family = IWM_DEVICE_FAMILY_7000;
		sc->sc_fwdmasegsz = IWM_FWDMASEGSZ;
		sc->sc_nvm_max_section_size = 16384;
		sc->nvm_type = IWM_NVM;
		break;
	case PCI_PRODUCT_INTEL_WL_3165_1:
	case PCI_PRODUCT_INTEL_WL_3165_2:
		sc->sc_fwname = "iwm-7265D-29";
		sc->host_interrupt_operation_mode = 0;
		sc->sc_device_family = IWM_DEVICE_FAMILY_7000;
		sc->sc_fwdmasegsz = IWM_FWDMASEGSZ;
		sc->sc_nvm_max_section_size = 16384;
		sc->nvm_type = IWM_NVM;
		break;
	case PCI_PRODUCT_INTEL_WL_3168_1:
		sc->sc_fwname = "iwm-3168-29";
		sc->host_interrupt_operation_mode = 0;
		sc->sc_device_family = IWM_DEVICE_FAMILY_7000;
		sc->sc_fwdmasegsz = IWM_FWDMASEGSZ;
		sc->sc_nvm_max_section_size = 16384;
		sc->nvm_type = IWM_NVM_SDP;
		break;
	case PCI_PRODUCT_INTEL_WL_7260_1:
	case PCI_PRODUCT_INTEL_WL_7260_2:
		sc->sc_fwname = "iwm-7260-17";
		sc->host_interrupt_operation_mode = 1;
		sc->sc_device_family = IWM_DEVICE_FAMILY_7000;
		sc->sc_fwdmasegsz = IWM_FWDMASEGSZ;
		sc->sc_nvm_max_section_size = 16384;
		sc->nvm_type = IWM_NVM;
		break;
	case PCI_PRODUCT_INTEL_WL_7265_1:
	case PCI_PRODUCT_INTEL_WL_7265_2:
		sc->sc_fwname = "iwm-7265-17";
		sc->host_interrupt_operation_mode = 0;
		sc->sc_device_family = IWM_DEVICE_FAMILY_7000;
		sc->sc_fwdmasegsz = IWM_FWDMASEGSZ;
		sc->sc_nvm_max_section_size = 16384;
		sc->nvm_type = IWM_NVM;
		break;
	case PCI_PRODUCT_INTEL_WL_8260_1:
	case PCI_PRODUCT_INTEL_WL_8260_2:
		sc->sc_fwname = "iwm-8000C-36";
		sc->host_interrupt_operation_mode = 0;
		sc->sc_device_family = IWM_DEVICE_FAMILY_8000;
		sc->sc_fwdmasegsz = IWM_FWDMASEGSZ_8000;
		sc->sc_nvm_max_section_size = 32768;
		sc->nvm_type = IWM_NVM_EXT;
		break;
	case PCI_PRODUCT_INTEL_WL_8265_1:
		sc->sc_fwname = "iwm-8265-36";
		sc->host_interrupt_operation_mode = 0;
		sc->sc_device_family = IWM_DEVICE_FAMILY_8000;
		sc->sc_fwdmasegsz = IWM_FWDMASEGSZ_8000;
		sc->sc_nvm_max_section_size = 32768;
		sc->nvm_type = IWM_NVM_EXT;
		break;
	case PCI_PRODUCT_INTEL_WL_9260_1:
		sc->sc_fwname = "iwm-9260-46";
		sc->host_interrupt_operation_mode = 0;
		sc->sc_device_family = IWM_DEVICE_FAMILY_9000;
		sc->sc_fwdmasegsz = IWM_FWDMASEGSZ_8000;
		sc->sc_nvm_max_section_size = 32768;
		sc->sc_mqrx_supported = 1;
		break;
	case PCI_PRODUCT_INTEL_WL_9560_1:
	case PCI_PRODUCT_INTEL_WL_9560_2:
	case PCI_PRODUCT_INTEL_WL_9560_3:
		sc->sc_fwname = "iwm-9000-46";
		sc->host_interrupt_operation_mode = 0;
		sc->sc_device_family = IWM_DEVICE_FAMILY_9000;
		sc->sc_fwdmasegsz = IWM_FWDMASEGSZ_8000;
		sc->sc_nvm_max_section_size = 32768;
		sc->sc_mqrx_supported = 1;
		sc->sc_integrated = 1;
		if (PCI_PRODUCT(pa->pa_id) == PCI_PRODUCT_INTEL_WL_9560_3) {
			sc->sc_xtal_latency = 670;
			sc->sc_extra_phy_config = IWM_FW_PHY_CFG_SHARED_CLK;
		} else
			sc->sc_xtal_latency = 650;
		break;
	default:
		printf("%s: unknown adapter type\n", DEVNAME(sc));
		return;
	}

	/*
	 * In the 8000 HW family the format of the 4 bytes of CSR_HW_REV have
	 * changed, and now the revision step also includes bit 0-1 (no more
	 * "dash" value). To keep hw_rev backwards compatible - we'll store it
	 * in the old format.
	 */
	if (sc->sc_device_family >= IWM_DEVICE_FAMILY_8000) {
		uint32_t hw_step;

		sc->sc_hw_rev = (sc->sc_hw_rev & 0xfff0) |
				(IWM_CSR_HW_REV_STEP(sc->sc_hw_rev << 2) << 2);
		
		if (iwm_prepare_card_hw(sc) != 0) {
			printf("%s: could not initialize hardware\n",
			    DEVNAME(sc));
			return;
		}

		/*
		 * In order to recognize C step the driver should read the
		 * chip version id located at the AUX bus MISC address.
		 */
		IWM_SETBITS(sc, IWM_CSR_GP_CNTRL,
			    IWM_CSR_GP_CNTRL_REG_FLAG_INIT_DONE);
		DELAY(2);

		err = iwm_poll_bit(sc, IWM_CSR_GP_CNTRL,
				   IWM_CSR_GP_CNTRL_REG_FLAG_MAC_CLOCK_READY,
				   IWM_CSR_GP_CNTRL_REG_FLAG_MAC_CLOCK_READY,
				   25000);
		if (!err) {
			printf("%s: Failed to wake up the nic\n", DEVNAME(sc));
			return;
		}

		if (iwm_nic_lock(sc)) {
			hw_step = iwm_read_prph(sc, IWM_WFPM_CTRL_REG);
			hw_step |= IWM_ENABLE_WFPM;
			iwm_write_prph(sc, IWM_WFPM_CTRL_REG, hw_step);
			hw_step = iwm_read_prph(sc, IWM_AUX_MISC_REG);
			hw_step = (hw_step >> IWM_HW_STEP_LOCATION_BITS) & 0xF;
			if (hw_step == 0x3)
				sc->sc_hw_rev = (sc->sc_hw_rev & 0xFFFFFFF3) |
						(IWM_SILICON_C_STEP << 2);
			iwm_nic_unlock(sc);
		} else {
			printf("%s: Failed to lock the nic\n", DEVNAME(sc));
			return;
		}
	}

	/* 
	 * Allocate DMA memory for firmware transfers.
	 * Must be aligned on a 16-byte boundary.
	 */
	err = iwm_dma_contig_alloc(sc->sc_dmat, &sc->fw_dma,
	    sc->sc_fwdmasegsz, 16);
	if (err) {
		printf("%s: could not allocate memory for firmware\n",
		    DEVNAME(sc));
		return;
	}

	/* Allocate "Keep Warm" page, used internally by the card. */
	err = iwm_dma_contig_alloc(sc->sc_dmat, &sc->kw_dma, 4096, 4096);
	if (err) {
		printf("%s: could not allocate keep warm page\n", DEVNAME(sc));
		goto fail1;
	}

	/* Allocate interrupt cause table (ICT).*/
	err = iwm_dma_contig_alloc(sc->sc_dmat, &sc->ict_dma,
	    IWM_ICT_SIZE, 1<<IWM_ICT_PADDR_SHIFT);
	if (err) {
		printf("%s: could not allocate ICT table\n", DEVNAME(sc));
		goto fail2;
	}

	/* TX scheduler rings must be aligned on a 1KB boundary. */
	err = iwm_dma_contig_alloc(sc->sc_dmat, &sc->sched_dma,
	    nitems(sc->txq) * sizeof(struct iwm_agn_scd_bc_tbl), 1024);
	if (err) {
		printf("%s: could not allocate TX scheduler rings\n",
		    DEVNAME(sc));
		goto fail3;
	}

	for (txq_i = 0; txq_i < nitems(sc->txq); txq_i++) {
		err = iwm_alloc_tx_ring(sc, &sc->txq[txq_i], txq_i);
		if (err) {
			printf("%s: could not allocate TX ring %d\n",
			    DEVNAME(sc), txq_i);
			goto fail4;
		}
	}

	err = iwm_alloc_rx_ring(sc, &sc->rxq);
	if (err) {
		printf("%s: could not allocate RX ring\n", DEVNAME(sc));
		goto fail4;
	}

	sc->sc_nswq = taskq_create("iwmns", 1, IPL_NET, 0);
	if (sc->sc_nswq == NULL)
		goto fail4;

	/* Clear pending interrupts. */
	IWM_WRITE(sc, IWM_CSR_INT, 0xffffffff);

	ic->ic_phytype = IEEE80211_T_OFDM;	/* not only, but not used */
	ic->ic_opmode = IEEE80211_M_STA;	/* default to BSS mode */
	ic->ic_state = IEEE80211_S_INIT;

	/* Set device capabilities. */
	ic->ic_caps =
	    IEEE80211_C_QOS | IEEE80211_C_TX_AMPDU | /* A-MPDU */
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

	sc->sc_amrr.amrr_min_success_threshold =  1;
	sc->sc_amrr.amrr_max_success_threshold = 15;

	/* IBSS channel undefined for now. */
	ic->ic_ibss_chan = &ic->ic_channels[1];

	ic->ic_max_rssi = IWM_MAX_DBM - IWM_MIN_DBM;

	ifp->if_softc = sc;
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	ifp->if_ioctl = iwm_ioctl;
	ifp->if_start = iwm_start;
	ifp->if_watchdog = iwm_watchdog;
	memcpy(ifp->if_xname, DEVNAME(sc), IFNAMSIZ);

	if_attach(ifp);
	ieee80211_ifattach(ifp);
	ieee80211_media_init(ifp, iwm_media_change, ieee80211_media_status);

#if NBPFILTER > 0
	iwm_radiotap_attach(sc);
#endif
	timeout_set(&sc->sc_calib_to, iwm_calib_timeout, sc);
	timeout_set(&sc->sc_led_blink_to, iwm_led_blink_timeout, sc);
	for (i = 0; i < nitems(sc->sc_rxba_data); i++) {
		struct iwm_rxba_data *rxba = &sc->sc_rxba_data[i];
		rxba->baid = IWM_RX_REORDER_DATA_INVALID_BAID;
		rxba->sc = sc;
		timeout_set(&rxba->session_timer, iwm_rx_ba_session_expired,
		    rxba);
		timeout_set(&rxba->reorder_buf.reorder_timer,
		    iwm_reorder_timer_expired, &rxba->reorder_buf);
		for (j = 0; j < nitems(rxba->entries); j++)
			ml_init(&rxba->entries[j].frames);
	}
	task_set(&sc->init_task, iwm_init_task, sc);
	task_set(&sc->newstate_task, iwm_newstate_task, sc);
	task_set(&sc->ba_task, iwm_ba_task, sc);
	task_set(&sc->mac_ctxt_task, iwm_mac_ctxt_task, sc);
	task_set(&sc->phy_ctxt_task, iwm_phy_ctxt_task, sc);
	task_set(&sc->bgscan_done_task, iwm_bgscan_done_task, sc);

	ic->ic_node_alloc = iwm_node_alloc;
	ic->ic_bgscan_start = iwm_bgscan;
	ic->ic_bgscan_done = iwm_bgscan_done;
	ic->ic_set_key = iwm_set_key;
	ic->ic_delete_key = iwm_delete_key;

	/* Override 802.11 state transition machine. */
	sc->sc_newstate = ic->ic_newstate;
	ic->ic_newstate = iwm_newstate;
	ic->ic_updateprot = iwm_updateprot;
	ic->ic_updateslot = iwm_updateslot;
	ic->ic_updateedca = iwm_updateedca;
	ic->ic_updatechan = iwm_updatechan;
	ic->ic_updatedtim = iwm_updatedtim;
	ic->ic_ampdu_rx_start = iwm_ampdu_rx_start;
	ic->ic_ampdu_rx_stop = iwm_ampdu_rx_stop;
	ic->ic_ampdu_tx_start = iwm_ampdu_tx_start;
	ic->ic_ampdu_tx_stop = iwm_ampdu_tx_stop;
	/*
	 * We cannot read the MAC address without loading the
	 * firmware from disk. Postpone until mountroot is done.
	 */
	config_mountroot(self, iwm_attach_hook);

	return;

fail4:	while (--txq_i >= 0)
		iwm_free_tx_ring(sc, &sc->txq[txq_i]);
	iwm_free_rx_ring(sc, &sc->rxq);
	iwm_dma_contig_free(&sc->sched_dma);
fail3:	if (sc->ict_dma.vaddr != NULL)
		iwm_dma_contig_free(&sc->ict_dma);
	
fail2:	iwm_dma_contig_free(&sc->kw_dma);
fail1:	iwm_dma_contig_free(&sc->fw_dma);
	return;
}

#if NBPFILTER > 0
void
iwm_radiotap_attach(struct iwm_softc *sc)
{
	bpfattach(&sc->sc_drvbpf, &sc->sc_ic.ic_if, DLT_IEEE802_11_RADIO,
	    sizeof (struct ieee80211_frame) + IEEE80211_RADIOTAP_HDRLEN);

	sc->sc_rxtap_len = sizeof sc->sc_rxtapu;
	sc->sc_rxtap.wr_ihdr.it_len = htole16(sc->sc_rxtap_len);
	sc->sc_rxtap.wr_ihdr.it_present = htole32(IWM_RX_RADIOTAP_PRESENT);

	sc->sc_txtap_len = sizeof sc->sc_txtapu;
	sc->sc_txtap.wt_ihdr.it_len = htole16(sc->sc_txtap_len);
	sc->sc_txtap.wt_ihdr.it_present = htole32(IWM_TX_RADIOTAP_PRESENT);
}
#endif

void
iwm_init_task(void *arg1)
{
	struct iwm_softc *sc = arg1;
	struct ifnet *ifp = &sc->sc_ic.ic_if;
	int s = splnet();
	int generation = sc->sc_generation;
	int fatal = (sc->sc_flags & (IWM_FLAG_HW_ERR | IWM_FLAG_RFKILL));

	rw_enter_write(&sc->ioctl_rwl);
	if (generation != sc->sc_generation) {
		rw_exit(&sc->ioctl_rwl);
		splx(s);
		return;
	}

	if (ifp->if_flags & IFF_RUNNING)
		iwm_stop(ifp);
	else
		sc->sc_flags &= ~IWM_FLAG_HW_ERR;

	if (!fatal && (ifp->if_flags & (IFF_UP | IFF_RUNNING)) == IFF_UP)
		iwm_init(ifp);

	rw_exit(&sc->ioctl_rwl);
	splx(s);
}

void
iwm_resume(struct iwm_softc *sc)
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

	iwm_disable_interrupts(sc);
}

int
iwm_wakeup(struct iwm_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ifnet *ifp = &sc->sc_ic.ic_if;
	int err;

	err = iwm_start_hw(sc);
	if (err)
		return err;

	err = iwm_init_hw(sc);
	if (err)
		return err;

	refcnt_init(&sc->task_refs);
	ifq_clr_oactive(&ifp->if_snd);
	ifp->if_flags |= IFF_RUNNING;

	if (ic->ic_opmode == IEEE80211_M_MONITOR)
		ieee80211_new_state(ic, IEEE80211_S_RUN, -1);
	else
		ieee80211_begin_scan(ifp);

	return 0;
}

int
iwm_activate(struct device *self, int act)
{
	struct iwm_softc *sc = (struct iwm_softc *)self;
	struct ifnet *ifp = &sc->sc_ic.ic_if;
	int err = 0;

	switch (act) {
	case DVACT_QUIESCE:
		if (ifp->if_flags & IFF_RUNNING) {
			rw_enter_write(&sc->ioctl_rwl);
			iwm_stop(ifp);
			rw_exit(&sc->ioctl_rwl);
		}
		break;
	case DVACT_RESUME:
		iwm_resume(sc);
		break;
	case DVACT_WAKEUP:
		if ((ifp->if_flags & (IFF_UP | IFF_RUNNING)) == IFF_UP) {
			err = iwm_wakeup(sc);
			if (err)
				printf("%s: could not initialize hardware\n",
				    DEVNAME(sc));
		}
		break;
	}

	return 0;
}

struct cfdriver iwm_cd = {
	NULL, "iwm", DV_IFNET
};

const struct cfattach iwm_ca = {
	sizeof(struct iwm_softc), iwm_match, iwm_attach,
	NULL, iwm_activate
};
