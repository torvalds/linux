/*	$OpenBSD: if_iwm.c,v 1.167 2017/04/04 00:40:52 claudio Exp $	*/

/*
 * Copyright (c) 2014 genua mbh <info@genua.de>
 * Copyright (c) 2014 Fixup Software Ltd.
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
 * Driver version we are currently based off of is
 * Linux 3.14.3 (tag id a2df521e42b1d9a23f620ac79dbfe8655a8391dd)
 *
 ***********************************************************************
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2007 - 2013 Intel Corporation. All rights reserved.
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
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_wlan.h"
#include "opt_iwm.h"

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/conf.h>
#include <sys/endian.h>
#include <sys/firmware.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/mutex.h>
#include <sys/module.h>
#include <sys/proc.h>
#include <sys/rman.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/sysctl.h>
#include <sys/linker.h>

#include <machine/bus.h>
#include <machine/endian.h>
#include <machine/resource.h>

#include <dev/pci/pcivar.h>
#include <dev/pci/pcireg.h>

#include <net/bpf.h>

#include <net/if.h>
#include <net/if_var.h>
#include <net/if_arp.h>
#include <net/if_dl.h>
#include <net/if_media.h>
#include <net/if_types.h>

#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/if_ether.h>
#include <netinet/ip.h>

#include <net80211/ieee80211_var.h>
#include <net80211/ieee80211_regdomain.h>
#include <net80211/ieee80211_ratectl.h>
#include <net80211/ieee80211_radiotap.h>

#include <dev/iwm/if_iwmreg.h>
#include <dev/iwm/if_iwmvar.h>
#include <dev/iwm/if_iwm_config.h>
#include <dev/iwm/if_iwm_debug.h>
#include <dev/iwm/if_iwm_notif_wait.h>
#include <dev/iwm/if_iwm_util.h>
#include <dev/iwm/if_iwm_binding.h>
#include <dev/iwm/if_iwm_phy_db.h>
#include <dev/iwm/if_iwm_mac_ctxt.h>
#include <dev/iwm/if_iwm_phy_ctxt.h>
#include <dev/iwm/if_iwm_time_event.h>
#include <dev/iwm/if_iwm_power.h>
#include <dev/iwm/if_iwm_scan.h>
#include <dev/iwm/if_iwm_sf.h>
#include <dev/iwm/if_iwm_sta.h>

#include <dev/iwm/if_iwm_pcie_trans.h>
#include <dev/iwm/if_iwm_led.h>
#include <dev/iwm/if_iwm_fw.h>

/* From DragonflyBSD */
#define mtodoff(m, t, off)      ((t)((m)->m_data + (off)))

const uint8_t iwm_nvm_channels[] = {
	/* 2.4 GHz */
	1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14,
	/* 5 GHz */
	36, 40, 44, 48, 52, 56, 60, 64,
	100, 104, 108, 112, 116, 120, 124, 128, 132, 136, 140, 144,
	149, 153, 157, 161, 165
};
_Static_assert(nitems(iwm_nvm_channels) <= IWM_NUM_CHANNELS,
    "IWM_NUM_CHANNELS is too small");

const uint8_t iwm_nvm_channels_8000[] = {
	/* 2.4 GHz */
	1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14,
	/* 5 GHz */
	36, 40, 44, 48, 52, 56, 60, 64, 68, 72, 76, 80, 84, 88, 92,
	96, 100, 104, 108, 112, 116, 120, 124, 128, 132, 136, 140, 144,
	149, 153, 157, 161, 165, 169, 173, 177, 181
};
_Static_assert(nitems(iwm_nvm_channels_8000) <= IWM_NUM_CHANNELS_8000,
    "IWM_NUM_CHANNELS_8000 is too small");

#define IWM_NUM_2GHZ_CHANNELS	14
#define IWM_N_HW_ADDR_MASK	0xF

/*
 * XXX For now, there's simply a fixed set of rate table entries
 * that are populated.
 */
const struct iwm_rate {
	uint8_t rate;
	uint8_t plcp;
} iwm_rates[] = {
	{   2,	IWM_RATE_1M_PLCP  },
	{   4,	IWM_RATE_2M_PLCP  },
	{  11,	IWM_RATE_5M_PLCP  },
	{  22,	IWM_RATE_11M_PLCP },
	{  12,	IWM_RATE_6M_PLCP  },
	{  18,	IWM_RATE_9M_PLCP  },
	{  24,	IWM_RATE_12M_PLCP },
	{  36,	IWM_RATE_18M_PLCP },
	{  48,	IWM_RATE_24M_PLCP },
	{  72,	IWM_RATE_36M_PLCP },
	{  96,	IWM_RATE_48M_PLCP },
	{ 108,	IWM_RATE_54M_PLCP },
};
#define IWM_RIDX_CCK	0
#define IWM_RIDX_OFDM	4
#define IWM_RIDX_MAX	(nitems(iwm_rates)-1)
#define IWM_RIDX_IS_CCK(_i_) ((_i_) < IWM_RIDX_OFDM)
#define IWM_RIDX_IS_OFDM(_i_) ((_i_) >= IWM_RIDX_OFDM)

struct iwm_nvm_section {
	uint16_t length;
	uint8_t *data;
};

#define IWM_MVM_UCODE_ALIVE_TIMEOUT	hz
#define IWM_MVM_UCODE_CALIB_TIMEOUT	(2*hz)

struct iwm_mvm_alive_data {
	int valid;
	uint32_t scd_base_addr;
};

static int	iwm_store_cscheme(struct iwm_softc *, const uint8_t *, size_t);
static int	iwm_firmware_store_section(struct iwm_softc *,
                                           enum iwm_ucode_type,
                                           const uint8_t *, size_t);
static int	iwm_set_default_calib(struct iwm_softc *, const void *);
static void	iwm_fw_info_free(struct iwm_fw_info *);
static int	iwm_read_firmware(struct iwm_softc *);
static int	iwm_alloc_fwmem(struct iwm_softc *);
static int	iwm_alloc_sched(struct iwm_softc *);
static int	iwm_alloc_kw(struct iwm_softc *);
static int	iwm_alloc_ict(struct iwm_softc *);
static int	iwm_alloc_rx_ring(struct iwm_softc *, struct iwm_rx_ring *);
static void	iwm_reset_rx_ring(struct iwm_softc *, struct iwm_rx_ring *);
static void	iwm_free_rx_ring(struct iwm_softc *, struct iwm_rx_ring *);
static int	iwm_alloc_tx_ring(struct iwm_softc *, struct iwm_tx_ring *,
                                  int);
static void	iwm_reset_tx_ring(struct iwm_softc *, struct iwm_tx_ring *);
static void	iwm_free_tx_ring(struct iwm_softc *, struct iwm_tx_ring *);
static void	iwm_enable_interrupts(struct iwm_softc *);
static void	iwm_restore_interrupts(struct iwm_softc *);
static void	iwm_disable_interrupts(struct iwm_softc *);
static void	iwm_ict_reset(struct iwm_softc *);
static int	iwm_allow_mcast(struct ieee80211vap *, struct iwm_softc *);
static void	iwm_stop_device(struct iwm_softc *);
static void	iwm_mvm_nic_config(struct iwm_softc *);
static int	iwm_nic_rx_init(struct iwm_softc *);
static int	iwm_nic_tx_init(struct iwm_softc *);
static int	iwm_nic_init(struct iwm_softc *);
static int	iwm_trans_pcie_fw_alive(struct iwm_softc *, uint32_t);
static int	iwm_nvm_read_chunk(struct iwm_softc *, uint16_t, uint16_t,
                                   uint16_t, uint8_t *, uint16_t *);
static int	iwm_nvm_read_section(struct iwm_softc *, uint16_t, uint8_t *,
				     uint16_t *, uint32_t);
static uint32_t	iwm_eeprom_channel_flags(uint16_t);
static void	iwm_add_channel_band(struct iwm_softc *,
		    struct ieee80211_channel[], int, int *, int, size_t,
		    const uint8_t[]);
static void	iwm_init_channel_map(struct ieee80211com *, int, int *,
		    struct ieee80211_channel[]);
static struct iwm_nvm_data *
	iwm_parse_nvm_data(struct iwm_softc *, const uint16_t *,
			   const uint16_t *, const uint16_t *,
			   const uint16_t *, const uint16_t *,
			   const uint16_t *);
static void	iwm_free_nvm_data(struct iwm_nvm_data *);
static void	iwm_set_hw_address_family_8000(struct iwm_softc *,
					       struct iwm_nvm_data *,
					       const uint16_t *,
					       const uint16_t *);
static int	iwm_get_sku(const struct iwm_softc *, const uint16_t *,
			    const uint16_t *);
static int	iwm_get_nvm_version(const struct iwm_softc *, const uint16_t *);
static int	iwm_get_radio_cfg(const struct iwm_softc *, const uint16_t *,
				  const uint16_t *);
static int	iwm_get_n_hw_addrs(const struct iwm_softc *,
				   const uint16_t *);
static void	iwm_set_radio_cfg(const struct iwm_softc *,
				  struct iwm_nvm_data *, uint32_t);
static struct iwm_nvm_data *
	iwm_parse_nvm_sections(struct iwm_softc *, struct iwm_nvm_section *);
static int	iwm_nvm_init(struct iwm_softc *);
static int	iwm_pcie_load_section(struct iwm_softc *, uint8_t,
				      const struct iwm_fw_desc *);
static int	iwm_pcie_load_firmware_chunk(struct iwm_softc *, uint32_t,
					     bus_addr_t, uint32_t);
static int	iwm_pcie_load_cpu_sections_8000(struct iwm_softc *sc,
						const struct iwm_fw_img *,
						int, int *);
static int	iwm_pcie_load_cpu_sections(struct iwm_softc *,
					   const struct iwm_fw_img *,
					   int, int *);
static int	iwm_pcie_load_given_ucode_8000(struct iwm_softc *,
					       const struct iwm_fw_img *);
static int	iwm_pcie_load_given_ucode(struct iwm_softc *,
					  const struct iwm_fw_img *);
static int	iwm_start_fw(struct iwm_softc *, const struct iwm_fw_img *);
static int	iwm_send_tx_ant_cfg(struct iwm_softc *, uint8_t);
static int	iwm_send_phy_cfg_cmd(struct iwm_softc *);
static int	iwm_mvm_load_ucode_wait_alive(struct iwm_softc *,
                                              enum iwm_ucode_type);
static int	iwm_run_init_mvm_ucode(struct iwm_softc *, int);
static int	iwm_mvm_config_ltr(struct iwm_softc *sc);
static int	iwm_rx_addbuf(struct iwm_softc *, int, int);
static int	iwm_mvm_get_signal_strength(struct iwm_softc *,
					    struct iwm_rx_phy_info *);
static void	iwm_mvm_rx_rx_phy_cmd(struct iwm_softc *,
                                      struct iwm_rx_packet *);
static int	iwm_get_noise(struct iwm_softc *,
		    const struct iwm_mvm_statistics_rx_non_phy *);
static void	iwm_mvm_handle_rx_statistics(struct iwm_softc *,
		    struct iwm_rx_packet *);
static boolean_t iwm_mvm_rx_rx_mpdu(struct iwm_softc *, struct mbuf *,
				    uint32_t, boolean_t);
static int	iwm_mvm_rx_tx_cmd_single(struct iwm_softc *,
                                         struct iwm_rx_packet *,
				         struct iwm_node *);
static void	iwm_mvm_rx_tx_cmd(struct iwm_softc *, struct iwm_rx_packet *);
static void	iwm_cmd_done(struct iwm_softc *, struct iwm_rx_packet *);
#if 0
static void	iwm_update_sched(struct iwm_softc *, int, int, uint8_t,
                                 uint16_t);
#endif
static const struct iwm_rate *
	iwm_tx_fill_cmd(struct iwm_softc *, struct iwm_node *,
			struct mbuf *, struct iwm_tx_cmd *);
static int	iwm_tx(struct iwm_softc *, struct mbuf *,
                       struct ieee80211_node *, int);
static int	iwm_raw_xmit(struct ieee80211_node *, struct mbuf *,
			     const struct ieee80211_bpf_params *);
static int	iwm_mvm_update_quotas(struct iwm_softc *, struct iwm_vap *);
static int	iwm_auth(struct ieee80211vap *, struct iwm_softc *);
static struct ieee80211_node *
		iwm_node_alloc(struct ieee80211vap *,
		               const uint8_t[IEEE80211_ADDR_LEN]);
static uint8_t	iwm_rate_from_ucode_rate(uint32_t);
static int	iwm_rate2ridx(struct iwm_softc *, uint8_t);
static void	iwm_setrates(struct iwm_softc *, struct iwm_node *, int);
static int	iwm_media_change(struct ifnet *);
static int	iwm_newstate(struct ieee80211vap *, enum ieee80211_state, int);
static void	iwm_endscan_cb(void *, int);
static int	iwm_send_bt_init_conf(struct iwm_softc *);
static boolean_t iwm_mvm_is_lar_supported(struct iwm_softc *);
static boolean_t iwm_mvm_is_wifi_mcc_supported(struct iwm_softc *);
static int	iwm_send_update_mcc_cmd(struct iwm_softc *, const char *);
static void	iwm_mvm_tt_tx_backoff(struct iwm_softc *, uint32_t);
static int	iwm_init_hw(struct iwm_softc *);
static void	iwm_init(struct iwm_softc *);
static void	iwm_start(struct iwm_softc *);
static void	iwm_stop(struct iwm_softc *);
static void	iwm_watchdog(void *);
static void	iwm_parent(struct ieee80211com *);
#ifdef IWM_DEBUG
static const char *
		iwm_desc_lookup(uint32_t);
static void	iwm_nic_error(struct iwm_softc *);
static void	iwm_nic_umac_error(struct iwm_softc *);
#endif
static void	iwm_handle_rxb(struct iwm_softc *, struct mbuf *);
static void	iwm_notif_intr(struct iwm_softc *);
static void	iwm_intr(void *);
static int	iwm_attach(device_t);
static int	iwm_is_valid_ether_addr(uint8_t *);
static void	iwm_preinit(void *);
static int	iwm_detach_local(struct iwm_softc *sc, int);
static void	iwm_init_task(void *);
static void	iwm_radiotap_attach(struct iwm_softc *);
static struct ieee80211vap *
		iwm_vap_create(struct ieee80211com *,
		               const char [IFNAMSIZ], int,
		               enum ieee80211_opmode, int,
		               const uint8_t [IEEE80211_ADDR_LEN],
		               const uint8_t [IEEE80211_ADDR_LEN]);
static void	iwm_vap_delete(struct ieee80211vap *);
static void	iwm_xmit_queue_drain(struct iwm_softc *);
static void	iwm_scan_start(struct ieee80211com *);
static void	iwm_scan_end(struct ieee80211com *);
static void	iwm_update_mcast(struct ieee80211com *);
static void	iwm_set_channel(struct ieee80211com *);
static void	iwm_scan_curchan(struct ieee80211_scan_state *, unsigned long);
static void	iwm_scan_mindwell(struct ieee80211_scan_state *);
static int	iwm_detach(device_t);

static int	iwm_lar_disable = 0;
TUNABLE_INT("hw.iwm.lar.disable", &iwm_lar_disable);

/*
 * Firmware parser.
 */

static int
iwm_store_cscheme(struct iwm_softc *sc, const uint8_t *data, size_t dlen)
{
	const struct iwm_fw_cscheme_list *l = (const void *)data;

	if (dlen < sizeof(*l) ||
	    dlen < sizeof(l->size) + l->size * sizeof(*l->cs))
		return EINVAL;

	/* we don't actually store anything for now, always use s/w crypto */

	return 0;
}

static int
iwm_firmware_store_section(struct iwm_softc *sc,
    enum iwm_ucode_type type, const uint8_t *data, size_t dlen)
{
	struct iwm_fw_img *fws;
	struct iwm_fw_desc *fwone;

	if (type >= IWM_UCODE_TYPE_MAX)
		return EINVAL;
	if (dlen < sizeof(uint32_t))
		return EINVAL;

	fws = &sc->sc_fw.img[type];
	if (fws->fw_count >= IWM_UCODE_SECTION_MAX)
		return EINVAL;

	fwone = &fws->sec[fws->fw_count];

	/* first 32bit are device load offset */
	memcpy(&fwone->offset, data, sizeof(uint32_t));

	/* rest is data */
	fwone->data = data + sizeof(uint32_t);
	fwone->len = dlen - sizeof(uint32_t);

	fws->fw_count++;

	return 0;
}

#define IWM_DEFAULT_SCAN_CHANNELS 40

/* iwlwifi: iwl-drv.c */
struct iwm_tlv_calib_data {
	uint32_t ucode_type;
	struct iwm_tlv_calib_ctrl calib;
} __packed;

static int
iwm_set_default_calib(struct iwm_softc *sc, const void *data)
{
	const struct iwm_tlv_calib_data *def_calib = data;
	uint32_t ucode_type = le32toh(def_calib->ucode_type);

	if (ucode_type >= IWM_UCODE_TYPE_MAX) {
		device_printf(sc->sc_dev,
		    "Wrong ucode_type %u for default "
		    "calibration.\n", ucode_type);
		return EINVAL;
	}

	sc->sc_default_calib[ucode_type].flow_trigger =
	    def_calib->calib.flow_trigger;
	sc->sc_default_calib[ucode_type].event_trigger =
	    def_calib->calib.event_trigger;

	return 0;
}

static int
iwm_set_ucode_api_flags(struct iwm_softc *sc, const uint8_t *data,
			struct iwm_ucode_capabilities *capa)
{
	const struct iwm_ucode_api *ucode_api = (const void *)data;
	uint32_t api_index = le32toh(ucode_api->api_index);
	uint32_t api_flags = le32toh(ucode_api->api_flags);
	int i;

	if (api_index >= howmany(IWM_NUM_UCODE_TLV_API, 32)) {
		device_printf(sc->sc_dev,
		    "api flags index %d larger than supported by driver\n",
		    api_index);
		/* don't return an error so we can load FW that has more bits */
		return 0;
	}

	for (i = 0; i < 32; i++) {
		if (api_flags & (1U << i))
			setbit(capa->enabled_api, i + 32 * api_index);
	}

	return 0;
}

static int
iwm_set_ucode_capabilities(struct iwm_softc *sc, const uint8_t *data,
			   struct iwm_ucode_capabilities *capa)
{
	const struct iwm_ucode_capa *ucode_capa = (const void *)data;
	uint32_t api_index = le32toh(ucode_capa->api_index);
	uint32_t api_flags = le32toh(ucode_capa->api_capa);
	int i;

	if (api_index >= howmany(IWM_NUM_UCODE_TLV_CAPA, 32)) {
		device_printf(sc->sc_dev,
		    "capa flags index %d larger than supported by driver\n",
		    api_index);
		/* don't return an error so we can load FW that has more bits */
		return 0;
	}

	for (i = 0; i < 32; i++) {
		if (api_flags & (1U << i))
			setbit(capa->enabled_capa, i + 32 * api_index);
	}

	return 0;
}

static void
iwm_fw_info_free(struct iwm_fw_info *fw)
{
	firmware_put(fw->fw_fp, FIRMWARE_UNLOAD);
	fw->fw_fp = NULL;
	memset(fw->img, 0, sizeof(fw->img));
}

static int
iwm_read_firmware(struct iwm_softc *sc)
{
	struct iwm_fw_info *fw = &sc->sc_fw;
	const struct iwm_tlv_ucode_header *uhdr;
	const struct iwm_ucode_tlv *tlv;
	struct iwm_ucode_capabilities *capa = &sc->sc_fw.ucode_capa;
	enum iwm_ucode_tlv_type tlv_type;
	const struct firmware *fwp;
	const uint8_t *data;
	uint32_t tlv_len;
	uint32_t usniffer_img;
	const uint8_t *tlv_data;
	uint32_t paging_mem_size;
	int num_of_cpus;
	int error = 0;
	size_t len;

	/*
	 * Load firmware into driver memory.
	 * fw_fp will be set.
	 */
	fwp = firmware_get(sc->cfg->fw_name);
	if (fwp == NULL) {
		device_printf(sc->sc_dev,
		    "could not read firmware %s (error %d)\n",
		    sc->cfg->fw_name, error);
		goto out;
	}
	fw->fw_fp = fwp;

	/* (Re-)Initialize default values. */
	capa->flags = 0;
	capa->max_probe_length = IWM_DEFAULT_MAX_PROBE_LENGTH;
	capa->n_scan_channels = IWM_DEFAULT_SCAN_CHANNELS;
	memset(capa->enabled_capa, 0, sizeof(capa->enabled_capa));
	memset(capa->enabled_api, 0, sizeof(capa->enabled_api));
	memset(sc->sc_fw_mcc, 0, sizeof(sc->sc_fw_mcc));

	/*
	 * Parse firmware contents
	 */

	uhdr = (const void *)fw->fw_fp->data;
	if (*(const uint32_t *)fw->fw_fp->data != 0
	    || le32toh(uhdr->magic) != IWM_TLV_UCODE_MAGIC) {
		device_printf(sc->sc_dev, "invalid firmware %s\n",
		    sc->cfg->fw_name);
		error = EINVAL;
		goto out;
	}

	snprintf(sc->sc_fwver, sizeof(sc->sc_fwver), "%u.%u (API ver %u)",
	    IWM_UCODE_MAJOR(le32toh(uhdr->ver)),
	    IWM_UCODE_MINOR(le32toh(uhdr->ver)),
	    IWM_UCODE_API(le32toh(uhdr->ver)));
	data = uhdr->data;
	len = fw->fw_fp->datasize - sizeof(*uhdr);

	while (len >= sizeof(*tlv)) {
		len -= sizeof(*tlv);
		tlv = (const void *)data;

		tlv_len = le32toh(tlv->length);
		tlv_type = le32toh(tlv->type);
		tlv_data = tlv->data;

		if (len < tlv_len) {
			device_printf(sc->sc_dev,
			    "firmware too short: %zu bytes\n",
			    len);
			error = EINVAL;
			goto parse_out;
		}
		len -= roundup2(tlv_len, 4);
		data += sizeof(*tlv) + roundup2(tlv_len, 4);

		switch ((int)tlv_type) {
		case IWM_UCODE_TLV_PROBE_MAX_LEN:
			if (tlv_len != sizeof(uint32_t)) {
				device_printf(sc->sc_dev,
				    "%s: PROBE_MAX_LEN (%u) != sizeof(uint32_t)\n",
				    __func__, tlv_len);
				error = EINVAL;
				goto parse_out;
			}
			capa->max_probe_length =
			    le32_to_cpup((const uint32_t *)tlv_data);
			/* limit it to something sensible */
			if (capa->max_probe_length >
			    IWM_SCAN_OFFLOAD_PROBE_REQ_SIZE) {
				IWM_DPRINTF(sc, IWM_DEBUG_FIRMWARE_TLV,
				    "%s: IWM_UCODE_TLV_PROBE_MAX_LEN "
				    "ridiculous\n", __func__);
				error = EINVAL;
				goto parse_out;
			}
			break;
		case IWM_UCODE_TLV_PAN:
			if (tlv_len) {
				device_printf(sc->sc_dev,
				    "%s: IWM_UCODE_TLV_PAN: tlv_len (%u) > 0\n",
				    __func__, tlv_len);
				error = EINVAL;
				goto parse_out;
			}
			capa->flags |= IWM_UCODE_TLV_FLAGS_PAN;
			break;
		case IWM_UCODE_TLV_FLAGS:
			if (tlv_len < sizeof(uint32_t)) {
				device_printf(sc->sc_dev,
				    "%s: IWM_UCODE_TLV_FLAGS: tlv_len (%u) < sizeof(uint32_t)\n",
				    __func__, tlv_len);
				error = EINVAL;
				goto parse_out;
			}
			if (tlv_len % sizeof(uint32_t)) {
				device_printf(sc->sc_dev,
				    "%s: IWM_UCODE_TLV_FLAGS: tlv_len (%u) %% sizeof(uint32_t)\n",
				    __func__, tlv_len);
				error = EINVAL;
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
			capa->flags = le32_to_cpup((const uint32_t *)tlv_data);
			break;
		case IWM_UCODE_TLV_CSCHEME:
			if ((error = iwm_store_cscheme(sc,
			    tlv_data, tlv_len)) != 0) {
				device_printf(sc->sc_dev,
				    "%s: iwm_store_cscheme(): returned %d\n",
				    __func__, error);
				goto parse_out;
			}
			break;
		case IWM_UCODE_TLV_NUM_OF_CPU:
			if (tlv_len != sizeof(uint32_t)) {
				device_printf(sc->sc_dev,
				    "%s: IWM_UCODE_TLV_NUM_OF_CPU: tlv_len (%u) != sizeof(uint32_t)\n",
				    __func__, tlv_len);
				error = EINVAL;
				goto parse_out;
			}
			num_of_cpus = le32_to_cpup((const uint32_t *)tlv_data);
			if (num_of_cpus == 2) {
				fw->img[IWM_UCODE_REGULAR].is_dual_cpus =
					TRUE;
				fw->img[IWM_UCODE_INIT].is_dual_cpus =
					TRUE;
				fw->img[IWM_UCODE_WOWLAN].is_dual_cpus =
					TRUE;
			} else if ((num_of_cpus > 2) || (num_of_cpus < 1)) {
				device_printf(sc->sc_dev,
				    "%s: Driver supports only 1 or 2 CPUs\n",
				    __func__);
				error = EINVAL;
				goto parse_out;
			}
			break;
		case IWM_UCODE_TLV_SEC_RT:
			if ((error = iwm_firmware_store_section(sc,
			    IWM_UCODE_REGULAR, tlv_data, tlv_len)) != 0) {
				device_printf(sc->sc_dev,
				    "%s: IWM_UCODE_REGULAR: iwm_firmware_store_section() failed; %d\n",
				    __func__, error);
				goto parse_out;
			}
			break;
		case IWM_UCODE_TLV_SEC_INIT:
			if ((error = iwm_firmware_store_section(sc,
			    IWM_UCODE_INIT, tlv_data, tlv_len)) != 0) {
				device_printf(sc->sc_dev,
				    "%s: IWM_UCODE_INIT: iwm_firmware_store_section() failed; %d\n",
				    __func__, error);
				goto parse_out;
			}
			break;
		case IWM_UCODE_TLV_SEC_WOWLAN:
			if ((error = iwm_firmware_store_section(sc,
			    IWM_UCODE_WOWLAN, tlv_data, tlv_len)) != 0) {
				device_printf(sc->sc_dev,
				    "%s: IWM_UCODE_WOWLAN: iwm_firmware_store_section() failed; %d\n",
				    __func__, error);
				goto parse_out;
			}
			break;
		case IWM_UCODE_TLV_DEF_CALIB:
			if (tlv_len != sizeof(struct iwm_tlv_calib_data)) {
				device_printf(sc->sc_dev,
				    "%s: IWM_UCODE_TLV_DEV_CALIB: tlv_len (%u) < sizeof(iwm_tlv_calib_data) (%zu)\n",
				    __func__, tlv_len,
				    sizeof(struct iwm_tlv_calib_data));
				error = EINVAL;
				goto parse_out;
			}
			if ((error = iwm_set_default_calib(sc, tlv_data)) != 0) {
				device_printf(sc->sc_dev,
				    "%s: iwm_set_default_calib() failed: %d\n",
				    __func__, error);
				goto parse_out;
			}
			break;
		case IWM_UCODE_TLV_PHY_SKU:
			if (tlv_len != sizeof(uint32_t)) {
				error = EINVAL;
				device_printf(sc->sc_dev,
				    "%s: IWM_UCODE_TLV_PHY_SKU: tlv_len (%u) < sizeof(uint32_t)\n",
				    __func__, tlv_len);
				goto parse_out;
			}
			sc->sc_fw.phy_config =
			    le32_to_cpup((const uint32_t *)tlv_data);
			sc->sc_fw.valid_tx_ant = (sc->sc_fw.phy_config &
						  IWM_FW_PHY_CFG_TX_CHAIN) >>
						  IWM_FW_PHY_CFG_TX_CHAIN_POS;
			sc->sc_fw.valid_rx_ant = (sc->sc_fw.phy_config &
						  IWM_FW_PHY_CFG_RX_CHAIN) >>
						  IWM_FW_PHY_CFG_RX_CHAIN_POS;
			break;

		case IWM_UCODE_TLV_API_CHANGES_SET: {
			if (tlv_len != sizeof(struct iwm_ucode_api)) {
				error = EINVAL;
				goto parse_out;
			}
			if (iwm_set_ucode_api_flags(sc, tlv_data, capa)) {
				error = EINVAL;
				goto parse_out;
			}
			break;
		}

		case IWM_UCODE_TLV_ENABLED_CAPABILITIES: {
			if (tlv_len != sizeof(struct iwm_ucode_capa)) {
				error = EINVAL;
				goto parse_out;
			}
			if (iwm_set_ucode_capabilities(sc, tlv_data, capa)) {
				error = EINVAL;
				goto parse_out;
			}
			break;
		}

		case 48: /* undocumented TLV */
		case IWM_UCODE_TLV_SDIO_ADMA_ADDR:
		case IWM_UCODE_TLV_FW_GSCAN_CAPA:
			/* ignore, not used by current driver */
			break;

		case IWM_UCODE_TLV_SEC_RT_USNIFFER:
			if ((error = iwm_firmware_store_section(sc,
			    IWM_UCODE_REGULAR_USNIFFER, tlv_data,
			    tlv_len)) != 0)
				goto parse_out;
			break;

		case IWM_UCODE_TLV_PAGING:
			if (tlv_len != sizeof(uint32_t)) {
				error = EINVAL;
				goto parse_out;
			}
			paging_mem_size = le32_to_cpup((const uint32_t *)tlv_data);

			IWM_DPRINTF(sc, IWM_DEBUG_FIRMWARE_TLV,
			    "%s: Paging: paging enabled (size = %u bytes)\n",
			    __func__, paging_mem_size);
			if (paging_mem_size > IWM_MAX_PAGING_IMAGE_SIZE) {
				device_printf(sc->sc_dev,
					"%s: Paging: driver supports up to %u bytes for paging image\n",
					__func__, IWM_MAX_PAGING_IMAGE_SIZE);
				error = EINVAL;
				goto out;
			}
			if (paging_mem_size & (IWM_FW_PAGING_SIZE - 1)) {
				device_printf(sc->sc_dev,
				    "%s: Paging: image isn't multiple %u\n",
				    __func__, IWM_FW_PAGING_SIZE);
				error = EINVAL;
				goto out;
			}

			sc->sc_fw.img[IWM_UCODE_REGULAR].paging_mem_size =
			    paging_mem_size;
			usniffer_img = IWM_UCODE_REGULAR_USNIFFER;
			sc->sc_fw.img[usniffer_img].paging_mem_size =
			    paging_mem_size;
			break;

		case IWM_UCODE_TLV_N_SCAN_CHANNELS:
			if (tlv_len != sizeof(uint32_t)) {
				error = EINVAL;
				goto parse_out;
			}
			capa->n_scan_channels =
			    le32_to_cpup((const uint32_t *)tlv_data);
			break;

		case IWM_UCODE_TLV_FW_VERSION:
			if (tlv_len != sizeof(uint32_t) * 3) {
				error = EINVAL;
				goto parse_out;
			}
			snprintf(sc->sc_fwver, sizeof(sc->sc_fwver),
			    "%d.%d.%d",
			    le32toh(((const uint32_t *)tlv_data)[0]),
			    le32toh(((const uint32_t *)tlv_data)[1]),
			    le32toh(((const uint32_t *)tlv_data)[2]));
			break;

		case IWM_UCODE_TLV_FW_MEM_SEG:
			break;

		default:
			device_printf(sc->sc_dev,
			    "%s: unknown firmware section %d, abort\n",
			    __func__, tlv_type);
			error = EINVAL;
			goto parse_out;
		}
	}

	KASSERT(error == 0, ("unhandled error"));

 parse_out:
	if (error) {
		device_printf(sc->sc_dev, "firmware parse error %d, "
		    "section type %d\n", error, tlv_type);
	}

 out:
	if (error) {
		if (fw->fw_fp != NULL)
			iwm_fw_info_free(fw);
	}

	return error;
}

/*
 * DMA resource routines
 */

/* fwmem is used to load firmware onto the card */
static int
iwm_alloc_fwmem(struct iwm_softc *sc)
{
	/* Must be aligned on a 16-byte boundary. */
	return iwm_dma_contig_alloc(sc->sc_dmat, &sc->fw_dma,
	    IWM_FH_MEM_TB_MAX_LENGTH, 16);
}

/* tx scheduler rings.  not used? */
static int
iwm_alloc_sched(struct iwm_softc *sc)
{
	/* TX scheduler rings must be aligned on a 1KB boundary. */
	return iwm_dma_contig_alloc(sc->sc_dmat, &sc->sched_dma,
	    nitems(sc->txq) * sizeof(struct iwm_agn_scd_bc_tbl), 1024);
}

/* keep-warm page is used internally by the card.  see iwl-fh.h for more info */
static int
iwm_alloc_kw(struct iwm_softc *sc)
{
	return iwm_dma_contig_alloc(sc->sc_dmat, &sc->kw_dma, 4096, 4096);
}

/* interrupt cause table */
static int
iwm_alloc_ict(struct iwm_softc *sc)
{
	return iwm_dma_contig_alloc(sc->sc_dmat, &sc->ict_dma,
	    IWM_ICT_SIZE, 1<<IWM_ICT_PADDR_SHIFT);
}

static int
iwm_alloc_rx_ring(struct iwm_softc *sc, struct iwm_rx_ring *ring)
{
	bus_size_t size;
	int i, error;

	ring->cur = 0;

	/* Allocate RX descriptors (256-byte aligned). */
	size = IWM_RX_RING_COUNT * sizeof(uint32_t);
	error = iwm_dma_contig_alloc(sc->sc_dmat, &ring->desc_dma, size, 256);
	if (error != 0) {
		device_printf(sc->sc_dev,
		    "could not allocate RX ring DMA memory\n");
		goto fail;
	}
	ring->desc = ring->desc_dma.vaddr;

	/* Allocate RX status area (16-byte aligned). */
	error = iwm_dma_contig_alloc(sc->sc_dmat, &ring->stat_dma,
	    sizeof(*ring->stat), 16);
	if (error != 0) {
		device_printf(sc->sc_dev,
		    "could not allocate RX status DMA memory\n");
		goto fail;
	}
	ring->stat = ring->stat_dma.vaddr;

        /* Create RX buffer DMA tag. */
        error = bus_dma_tag_create(sc->sc_dmat, 1, 0,
            BUS_SPACE_MAXADDR_32BIT, BUS_SPACE_MAXADDR, NULL, NULL,
            IWM_RBUF_SIZE, 1, IWM_RBUF_SIZE, 0, NULL, NULL, &ring->data_dmat);
        if (error != 0) {
                device_printf(sc->sc_dev,
                    "%s: could not create RX buf DMA tag, error %d\n",
                    __func__, error);
                goto fail;
        }

	/* Allocate spare bus_dmamap_t for iwm_rx_addbuf() */
	error = bus_dmamap_create(ring->data_dmat, 0, &ring->spare_map);
	if (error != 0) {
		device_printf(sc->sc_dev,
		    "%s: could not create RX buf DMA map, error %d\n",
		    __func__, error);
		goto fail;
	}
	/*
	 * Allocate and map RX buffers.
	 */
	for (i = 0; i < IWM_RX_RING_COUNT; i++) {
		struct iwm_rx_data *data = &ring->data[i];
		error = bus_dmamap_create(ring->data_dmat, 0, &data->map);
		if (error != 0) {
			device_printf(sc->sc_dev,
			    "%s: could not create RX buf DMA map, error %d\n",
			    __func__, error);
			goto fail;
		}
		data->m = NULL;

		if ((error = iwm_rx_addbuf(sc, IWM_RBUF_SIZE, i)) != 0) {
			goto fail;
		}
	}
	return 0;

fail:	iwm_free_rx_ring(sc, ring);
	return error;
}

static void
iwm_reset_rx_ring(struct iwm_softc *sc, struct iwm_rx_ring *ring)
{
	/* Reset the ring state */
	ring->cur = 0;

	/*
	 * The hw rx ring index in shared memory must also be cleared,
	 * otherwise the discrepancy can cause reprocessing chaos.
	 */
	if (sc->rxq.stat)
		memset(sc->rxq.stat, 0, sizeof(*sc->rxq.stat));
}

static void
iwm_free_rx_ring(struct iwm_softc *sc, struct iwm_rx_ring *ring)
{
	int i;

	iwm_dma_contig_free(&ring->desc_dma);
	iwm_dma_contig_free(&ring->stat_dma);

	for (i = 0; i < IWM_RX_RING_COUNT; i++) {
		struct iwm_rx_data *data = &ring->data[i];

		if (data->m != NULL) {
			bus_dmamap_sync(ring->data_dmat, data->map,
			    BUS_DMASYNC_POSTREAD);
			bus_dmamap_unload(ring->data_dmat, data->map);
			m_freem(data->m);
			data->m = NULL;
		}
		if (data->map != NULL) {
			bus_dmamap_destroy(ring->data_dmat, data->map);
			data->map = NULL;
		}
	}
	if (ring->spare_map != NULL) {
		bus_dmamap_destroy(ring->data_dmat, ring->spare_map);
		ring->spare_map = NULL;
	}
	if (ring->data_dmat != NULL) {
		bus_dma_tag_destroy(ring->data_dmat);
		ring->data_dmat = NULL;
	}
}

static int
iwm_alloc_tx_ring(struct iwm_softc *sc, struct iwm_tx_ring *ring, int qid)
{
	bus_addr_t paddr;
	bus_size_t size;
	size_t maxsize;
	int nsegments;
	int i, error;

	ring->qid = qid;
	ring->queued = 0;
	ring->cur = 0;

	/* Allocate TX descriptors (256-byte aligned). */
	size = IWM_TX_RING_COUNT * sizeof (struct iwm_tfd);
	error = iwm_dma_contig_alloc(sc->sc_dmat, &ring->desc_dma, size, 256);
	if (error != 0) {
		device_printf(sc->sc_dev,
		    "could not allocate TX ring DMA memory\n");
		goto fail;
	}
	ring->desc = ring->desc_dma.vaddr;

	/*
	 * We only use rings 0 through 9 (4 EDCA + cmd) so there is no need
	 * to allocate commands space for other rings.
	 */
	if (qid > IWM_MVM_CMD_QUEUE)
		return 0;

	size = IWM_TX_RING_COUNT * sizeof(struct iwm_device_cmd);
	error = iwm_dma_contig_alloc(sc->sc_dmat, &ring->cmd_dma, size, 4);
	if (error != 0) {
		device_printf(sc->sc_dev,
		    "could not allocate TX cmd DMA memory\n");
		goto fail;
	}
	ring->cmd = ring->cmd_dma.vaddr;

	/* FW commands may require more mapped space than packets. */
	if (qid == IWM_MVM_CMD_QUEUE) {
		maxsize = IWM_RBUF_SIZE;
		nsegments = 1;
	} else {
		maxsize = MCLBYTES;
		nsegments = IWM_MAX_SCATTER - 2;
	}

	error = bus_dma_tag_create(sc->sc_dmat, 1, 0,
	    BUS_SPACE_MAXADDR_32BIT, BUS_SPACE_MAXADDR, NULL, NULL, maxsize,
            nsegments, maxsize, 0, NULL, NULL, &ring->data_dmat);
	if (error != 0) {
		device_printf(sc->sc_dev, "could not create TX buf DMA tag\n");
		goto fail;
	}

	paddr = ring->cmd_dma.paddr;
	for (i = 0; i < IWM_TX_RING_COUNT; i++) {
		struct iwm_tx_data *data = &ring->data[i];

		data->cmd_paddr = paddr;
		data->scratch_paddr = paddr + sizeof(struct iwm_cmd_header)
		    + offsetof(struct iwm_tx_cmd, scratch);
		paddr += sizeof(struct iwm_device_cmd);

		error = bus_dmamap_create(ring->data_dmat, 0, &data->map);
		if (error != 0) {
			device_printf(sc->sc_dev,
			    "could not create TX buf DMA map\n");
			goto fail;
		}
	}
	KASSERT(paddr == ring->cmd_dma.paddr + size,
	    ("invalid physical address"));
	return 0;

fail:	iwm_free_tx_ring(sc, ring);
	return error;
}

static void
iwm_reset_tx_ring(struct iwm_softc *sc, struct iwm_tx_ring *ring)
{
	int i;

	for (i = 0; i < IWM_TX_RING_COUNT; i++) {
		struct iwm_tx_data *data = &ring->data[i];

		if (data->m != NULL) {
			bus_dmamap_sync(ring->data_dmat, data->map,
			    BUS_DMASYNC_POSTWRITE);
			bus_dmamap_unload(ring->data_dmat, data->map);
			m_freem(data->m);
			data->m = NULL;
		}
	}
	/* Clear TX descriptors. */
	memset(ring->desc, 0, ring->desc_dma.size);
	bus_dmamap_sync(ring->desc_dma.tag, ring->desc_dma.map,
	    BUS_DMASYNC_PREWRITE);
	sc->qfullmsk &= ~(1 << ring->qid);
	ring->queued = 0;
	ring->cur = 0;

	if (ring->qid == IWM_MVM_CMD_QUEUE && sc->cmd_hold_nic_awake)
		iwm_pcie_clear_cmd_in_flight(sc);
}

static void
iwm_free_tx_ring(struct iwm_softc *sc, struct iwm_tx_ring *ring)
{
	int i;

	iwm_dma_contig_free(&ring->desc_dma);
	iwm_dma_contig_free(&ring->cmd_dma);

	for (i = 0; i < IWM_TX_RING_COUNT; i++) {
		struct iwm_tx_data *data = &ring->data[i];

		if (data->m != NULL) {
			bus_dmamap_sync(ring->data_dmat, data->map,
			    BUS_DMASYNC_POSTWRITE);
			bus_dmamap_unload(ring->data_dmat, data->map);
			m_freem(data->m);
			data->m = NULL;
		}
		if (data->map != NULL) {
			bus_dmamap_destroy(ring->data_dmat, data->map);
			data->map = NULL;
		}
	}
	if (ring->data_dmat != NULL) {
		bus_dma_tag_destroy(ring->data_dmat);
		ring->data_dmat = NULL;
	}
}

/*
 * High-level hardware frobbing routines
 */

static void
iwm_enable_interrupts(struct iwm_softc *sc)
{
	sc->sc_intmask = IWM_CSR_INI_SET_MASK;
	IWM_WRITE(sc, IWM_CSR_INT_MASK, sc->sc_intmask);
}

static void
iwm_restore_interrupts(struct iwm_softc *sc)
{
	IWM_WRITE(sc, IWM_CSR_INT_MASK, sc->sc_intmask);
}

static void
iwm_disable_interrupts(struct iwm_softc *sc)
{
	/* disable interrupts */
	IWM_WRITE(sc, IWM_CSR_INT_MASK, 0);

	/* acknowledge all interrupts */
	IWM_WRITE(sc, IWM_CSR_INT, ~0);
	IWM_WRITE(sc, IWM_CSR_FH_INT_STATUS, ~0);
}

static void
iwm_ict_reset(struct iwm_softc *sc)
{
	iwm_disable_interrupts(sc);

	/* Reset ICT table. */
	memset(sc->ict_dma.vaddr, 0, IWM_ICT_SIZE);
	sc->ict_cur = 0;

	/* Set physical address of ICT table (4KB aligned). */
	IWM_WRITE(sc, IWM_CSR_DRAM_INT_TBL_REG,
	    IWM_CSR_DRAM_INT_TBL_ENABLE
	    | IWM_CSR_DRAM_INIT_TBL_WRITE_POINTER
	    | IWM_CSR_DRAM_INIT_TBL_WRAP_CHECK
	    | sc->ict_dma.paddr >> IWM_ICT_PADDR_SHIFT);

	/* Switch to ICT interrupt mode in driver. */
	sc->sc_flags |= IWM_FLAG_USE_ICT;

	/* Re-enable interrupts. */
	IWM_WRITE(sc, IWM_CSR_INT, ~0);
	iwm_enable_interrupts(sc);
}

/* iwlwifi pcie/trans.c */

/*
 * Since this .. hard-resets things, it's time to actually
 * mark the first vap (if any) as having no mac context.
 * It's annoying, but since the driver is potentially being
 * stop/start'ed whilst active (thanks openbsd port!) we
 * have to correctly track this.
 */
static void
iwm_stop_device(struct iwm_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ieee80211vap *vap = TAILQ_FIRST(&ic->ic_vaps);
	int chnl, qid;
	uint32_t mask = 0;

	/* tell the device to stop sending interrupts */
	iwm_disable_interrupts(sc);

	/*
	 * FreeBSD-local: mark the first vap as not-uploaded,
	 * so the next transition through auth/assoc
	 * will correctly populate the MAC context.
	 */
	if (vap) {
		struct iwm_vap *iv = IWM_VAP(vap);
		iv->phy_ctxt = NULL;
		iv->is_uploaded = 0;
	}
	sc->sc_firmware_state = 0;
	sc->sc_flags &= ~IWM_FLAG_TE_ACTIVE;

	/* device going down, Stop using ICT table */
	sc->sc_flags &= ~IWM_FLAG_USE_ICT;

	/* stop tx and rx.  tx and rx bits, as usual, are from if_iwn */

	if (iwm_nic_lock(sc)) {
		iwm_write_prph(sc, IWM_SCD_TXFACT, 0);

		/* Stop each Tx DMA channel */
		for (chnl = 0; chnl < IWM_FH_TCSR_CHNL_NUM; chnl++) {
			IWM_WRITE(sc,
			    IWM_FH_TCSR_CHNL_TX_CONFIG_REG(chnl), 0);
			mask |= IWM_FH_TSSR_TX_STATUS_REG_MSK_CHNL_IDLE(chnl);
		}

		/* Wait for DMA channels to be idle */
		if (!iwm_poll_bit(sc, IWM_FH_TSSR_TX_STATUS_REG, mask, mask,
		    5000)) {
			device_printf(sc->sc_dev,
			    "Failing on timeout while stopping DMA channel: [0x%08x]\n",
			    IWM_READ(sc, IWM_FH_TSSR_TX_STATUS_REG));
		}
		iwm_nic_unlock(sc);
	}
	iwm_pcie_rx_stop(sc);

	/* Stop RX ring. */
	iwm_reset_rx_ring(sc, &sc->rxq);

	/* Reset all TX rings. */
	for (qid = 0; qid < nitems(sc->txq); qid++)
		iwm_reset_tx_ring(sc, &sc->txq[qid]);

	if (sc->cfg->device_family == IWM_DEVICE_FAMILY_7000) {
		/* Power-down device's busmaster DMA clocks */
		if (iwm_nic_lock(sc)) {
			iwm_write_prph(sc, IWM_APMG_CLK_DIS_REG,
			    IWM_APMG_CLK_VAL_DMA_CLK_RQT);
			iwm_nic_unlock(sc);
		}
		DELAY(5);
	}

	/* Make sure (redundant) we've released our request to stay awake */
	IWM_CLRBITS(sc, IWM_CSR_GP_CNTRL,
	    IWM_CSR_GP_CNTRL_REG_FLAG_MAC_ACCESS_REQ);

	/* Stop the device, and put it in low power state */
	iwm_apm_stop(sc);

	/* Upon stop, the APM issues an interrupt if HW RF kill is set.
	 * Clean again the interrupt here
	 */
	iwm_disable_interrupts(sc);
	/* stop and reset the on-board processor */
	IWM_WRITE(sc, IWM_CSR_RESET, IWM_CSR_RESET_REG_FLAG_SW_RESET);

	/*
	 * Even if we stop the HW, we still want the RF kill
	 * interrupt
	 */
	iwm_enable_rfkill_int(sc);
	iwm_check_rfkill(sc);
}

/* iwlwifi: mvm/ops.c */
static void
iwm_mvm_nic_config(struct iwm_softc *sc)
{
	uint8_t radio_cfg_type, radio_cfg_step, radio_cfg_dash;
	uint32_t reg_val = 0;
	uint32_t phy_config = iwm_mvm_get_phy_config(sc);

	radio_cfg_type = (phy_config & IWM_FW_PHY_CFG_RADIO_TYPE) >>
	    IWM_FW_PHY_CFG_RADIO_TYPE_POS;
	radio_cfg_step = (phy_config & IWM_FW_PHY_CFG_RADIO_STEP) >>
	    IWM_FW_PHY_CFG_RADIO_STEP_POS;
	radio_cfg_dash = (phy_config & IWM_FW_PHY_CFG_RADIO_DASH) >>
	    IWM_FW_PHY_CFG_RADIO_DASH_POS;

	/* SKU control */
	reg_val |= IWM_CSR_HW_REV_STEP(sc->sc_hw_rev) <<
	    IWM_CSR_HW_IF_CONFIG_REG_POS_MAC_STEP;
	reg_val |= IWM_CSR_HW_REV_DASH(sc->sc_hw_rev) <<
	    IWM_CSR_HW_IF_CONFIG_REG_POS_MAC_DASH;

	/* radio configuration */
	reg_val |= radio_cfg_type << IWM_CSR_HW_IF_CONFIG_REG_POS_PHY_TYPE;
	reg_val |= radio_cfg_step << IWM_CSR_HW_IF_CONFIG_REG_POS_PHY_STEP;
	reg_val |= radio_cfg_dash << IWM_CSR_HW_IF_CONFIG_REG_POS_PHY_DASH;

	IWM_WRITE(sc, IWM_CSR_HW_IF_CONFIG_REG, reg_val);

	IWM_DPRINTF(sc, IWM_DEBUG_RESET,
	    "Radio type=0x%x-0x%x-0x%x\n", radio_cfg_type,
	    radio_cfg_step, radio_cfg_dash);

	/*
	 * W/A : NIC is stuck in a reset state after Early PCIe power off
	 * (PCIe power is lost before PERST# is asserted), causing ME FW
	 * to lose ownership and not being able to obtain it back.
	 */
	if (sc->cfg->device_family == IWM_DEVICE_FAMILY_7000) {
		iwm_set_bits_mask_prph(sc, IWM_APMG_PS_CTRL_REG,
		    IWM_APMG_PS_CTRL_EARLY_PWR_OFF_RESET_DIS,
		    ~IWM_APMG_PS_CTRL_EARLY_PWR_OFF_RESET_DIS);
	}
}

static int
iwm_nic_rx_init(struct iwm_softc *sc)
{
	/*
	 * Initialize RX ring.  This is from the iwn driver.
	 */
	memset(sc->rxq.stat, 0, sizeof(*sc->rxq.stat));

	/* Stop Rx DMA */
	iwm_pcie_rx_stop(sc);

	if (!iwm_nic_lock(sc))
		return EBUSY;

	/* reset and flush pointers */
	IWM_WRITE(sc, IWM_FH_MEM_RCSR_CHNL0_RBDCB_WPTR, 0);
	IWM_WRITE(sc, IWM_FH_MEM_RCSR_CHNL0_FLUSH_RB_REQ, 0);
	IWM_WRITE(sc, IWM_FH_RSCSR_CHNL0_RDPTR, 0);
	IWM_WRITE(sc, IWM_FH_RSCSR_CHNL0_RBDCB_WPTR_REG, 0);

	/* Set physical address of RX ring (256-byte aligned). */
	IWM_WRITE(sc,
	    IWM_FH_RSCSR_CHNL0_RBDCB_BASE_REG, sc->rxq.desc_dma.paddr >> 8);

	/* Set physical address of RX status (16-byte aligned). */
	IWM_WRITE(sc,
	    IWM_FH_RSCSR_CHNL0_STTS_WPTR_REG, sc->rxq.stat_dma.paddr >> 4);

	/* Enable Rx DMA
	 * XXX 5000 HW isn't supported by the iwm(4) driver.
	 * IWM_FH_RCSR_CHNL0_RX_IGNORE_RXF_EMPTY is set because of HW bug in
	 *      the credit mechanism in 5000 HW RX FIFO
	 * Direct rx interrupts to hosts
	 * Rx buffer size 4 or 8k or 12k
	 * RB timeout 0x10
	 * 256 RBDs
	 */
	IWM_WRITE(sc, IWM_FH_MEM_RCSR_CHNL0_CONFIG_REG,
	    IWM_FH_RCSR_RX_CONFIG_CHNL_EN_ENABLE_VAL		|
	    IWM_FH_RCSR_CHNL0_RX_IGNORE_RXF_EMPTY		|  /* HW bug */
	    IWM_FH_RCSR_CHNL0_RX_CONFIG_IRQ_DEST_INT_HOST_VAL	|
	    IWM_FH_RCSR_RX_CONFIG_REG_VAL_RB_SIZE_4K		|
	    (IWM_RX_RB_TIMEOUT << IWM_FH_RCSR_RX_CONFIG_REG_IRQ_RBTH_POS) |
	    IWM_RX_QUEUE_SIZE_LOG << IWM_FH_RCSR_RX_CONFIG_RBDCB_SIZE_POS);

	IWM_WRITE_1(sc, IWM_CSR_INT_COALESCING, IWM_HOST_INT_TIMEOUT_DEF);

	/* W/A for interrupt coalescing bug in 7260 and 3160 */
	if (sc->cfg->host_interrupt_operation_mode)
		IWM_SETBITS(sc, IWM_CSR_INT_COALESCING, IWM_HOST_INT_OPER_MODE);

	/*
	 * Thus sayeth el jefe (iwlwifi) via a comment:
	 *
	 * This value should initially be 0 (before preparing any
	 * RBs), should be 8 after preparing the first 8 RBs (for example)
	 */
	IWM_WRITE(sc, IWM_FH_RSCSR_CHNL0_WPTR, 8);

	iwm_nic_unlock(sc);

	return 0;
}

static int
iwm_nic_tx_init(struct iwm_softc *sc)
{
	int qid;

	if (!iwm_nic_lock(sc))
		return EBUSY;

	/* Deactivate TX scheduler. */
	iwm_write_prph(sc, IWM_SCD_TXFACT, 0);

	/* Set physical address of "keep warm" page (16-byte aligned). */
	IWM_WRITE(sc, IWM_FH_KW_MEM_ADDR_REG, sc->kw_dma.paddr >> 4);

	/* Initialize TX rings. */
	for (qid = 0; qid < nitems(sc->txq); qid++) {
		struct iwm_tx_ring *txq = &sc->txq[qid];

		/* Set physical address of TX ring (256-byte aligned). */
		IWM_WRITE(sc, IWM_FH_MEM_CBBC_QUEUE(qid),
		    txq->desc_dma.paddr >> 8);
		IWM_DPRINTF(sc, IWM_DEBUG_XMIT,
		    "%s: loading ring %d descriptors (%p) at %lx\n",
		    __func__,
		    qid, txq->desc,
		    (unsigned long) (txq->desc_dma.paddr >> 8));
	}

	iwm_write_prph(sc, IWM_SCD_GP_CTRL, IWM_SCD_GP_CTRL_AUTO_ACTIVE_MODE);

	iwm_nic_unlock(sc);

	return 0;
}

static int
iwm_nic_init(struct iwm_softc *sc)
{
	int error;

	iwm_apm_init(sc);
	if (sc->cfg->device_family == IWM_DEVICE_FAMILY_7000)
		iwm_set_pwr(sc);

	iwm_mvm_nic_config(sc);

	if ((error = iwm_nic_rx_init(sc)) != 0)
		return error;

	/*
	 * Ditto for TX, from iwn
	 */
	if ((error = iwm_nic_tx_init(sc)) != 0)
		return error;

	IWM_DPRINTF(sc, IWM_DEBUG_RESET,
	    "%s: shadow registers enabled\n", __func__);
	IWM_SETBITS(sc, IWM_CSR_MAC_SHADOW_REG_CTRL, 0x800fffff);

	return 0;
}

int
iwm_enable_txq(struct iwm_softc *sc, int sta_id, int qid, int fifo)
{
	if (!iwm_nic_lock(sc)) {
		device_printf(sc->sc_dev,
		    "%s: cannot enable txq %d\n",
		    __func__,
		    qid);
		return EBUSY;
	}

	IWM_WRITE(sc, IWM_HBUS_TARG_WRPTR, qid << 8 | 0);

	if (qid == IWM_MVM_CMD_QUEUE) {
		/* unactivate before configuration */
		iwm_write_prph(sc, IWM_SCD_QUEUE_STATUS_BITS(qid),
		    (0 << IWM_SCD_QUEUE_STTS_REG_POS_ACTIVE)
		    | (1 << IWM_SCD_QUEUE_STTS_REG_POS_SCD_ACT_EN));

		iwm_nic_unlock(sc);

		iwm_clear_bits_prph(sc, IWM_SCD_AGGR_SEL, (1 << qid));

		if (!iwm_nic_lock(sc)) {
			device_printf(sc->sc_dev,
			    "%s: cannot enable txq %d\n", __func__, qid);
			return EBUSY;
		}
		iwm_write_prph(sc, IWM_SCD_QUEUE_RDPTR(qid), 0);
		iwm_nic_unlock(sc);

		iwm_write_mem32(sc, sc->scd_base_addr + IWM_SCD_CONTEXT_QUEUE_OFFSET(qid), 0);
		/* Set scheduler window size and frame limit. */
		iwm_write_mem32(sc,
		    sc->scd_base_addr + IWM_SCD_CONTEXT_QUEUE_OFFSET(qid) +
		    sizeof(uint32_t),
		    ((IWM_FRAME_LIMIT << IWM_SCD_QUEUE_CTX_REG2_WIN_SIZE_POS) &
		    IWM_SCD_QUEUE_CTX_REG2_WIN_SIZE_MSK) |
		    ((IWM_FRAME_LIMIT << IWM_SCD_QUEUE_CTX_REG2_FRAME_LIMIT_POS) &
		    IWM_SCD_QUEUE_CTX_REG2_FRAME_LIMIT_MSK));

		if (!iwm_nic_lock(sc)) {
			device_printf(sc->sc_dev,
			    "%s: cannot enable txq %d\n", __func__, qid);
			return EBUSY;
		}
		iwm_write_prph(sc, IWM_SCD_QUEUE_STATUS_BITS(qid),
		    (1 << IWM_SCD_QUEUE_STTS_REG_POS_ACTIVE) |
		    (fifo << IWM_SCD_QUEUE_STTS_REG_POS_TXF) |
		    (1 << IWM_SCD_QUEUE_STTS_REG_POS_WSL) |
		    IWM_SCD_QUEUE_STTS_REG_MSK);
	} else {
		struct iwm_scd_txq_cfg_cmd cmd;
		int error;

		iwm_nic_unlock(sc);

		memset(&cmd, 0, sizeof(cmd));
		cmd.scd_queue = qid;
		cmd.enable = 1;
		cmd.sta_id = sta_id;
		cmd.tx_fifo = fifo;
		cmd.aggregate = 0;
		cmd.window = IWM_FRAME_LIMIT;

		error = iwm_mvm_send_cmd_pdu(sc, IWM_SCD_QUEUE_CFG, IWM_CMD_SYNC,
		    sizeof(cmd), &cmd);
		if (error) {
			device_printf(sc->sc_dev,
			    "cannot enable txq %d\n", qid);
			return error;
		}

		if (!iwm_nic_lock(sc))
			return EBUSY;
	}

	iwm_write_prph(sc, IWM_SCD_EN_CTRL,
	    iwm_read_prph(sc, IWM_SCD_EN_CTRL) | qid);

	iwm_nic_unlock(sc);

	IWM_DPRINTF(sc, IWM_DEBUG_XMIT, "%s: enabled txq %d FIFO %d\n",
	    __func__, qid, fifo);

	return 0;
}

static int
iwm_trans_pcie_fw_alive(struct iwm_softc *sc, uint32_t scd_base_addr)
{
	int error, chnl;

	int clear_dwords = (IWM_SCD_TRANS_TBL_MEM_UPPER_BOUND -
	    IWM_SCD_CONTEXT_MEM_LOWER_BOUND) / sizeof(uint32_t);

	if (!iwm_nic_lock(sc))
		return EBUSY;

	iwm_ict_reset(sc);

	sc->scd_base_addr = iwm_read_prph(sc, IWM_SCD_SRAM_BASE_ADDR);
	if (scd_base_addr != 0 &&
	    scd_base_addr != sc->scd_base_addr) {
		device_printf(sc->sc_dev,
		    "%s: sched addr mismatch: alive: 0x%x prph: 0x%x\n",
		    __func__, sc->scd_base_addr, scd_base_addr);
	}

	iwm_nic_unlock(sc);

	/* reset context data, TX status and translation data */
	error = iwm_write_mem(sc,
	    sc->scd_base_addr + IWM_SCD_CONTEXT_MEM_LOWER_BOUND,
	    NULL, clear_dwords);
	if (error)
		return EBUSY;

	if (!iwm_nic_lock(sc))
		return EBUSY;

	/* Set physical address of TX scheduler rings (1KB aligned). */
	iwm_write_prph(sc, IWM_SCD_DRAM_BASE_ADDR, sc->sched_dma.paddr >> 10);

	iwm_write_prph(sc, IWM_SCD_CHAINEXT_EN, 0);

	iwm_nic_unlock(sc);

	/* enable command channel */
	error = iwm_enable_txq(sc, 0 /* unused */, IWM_MVM_CMD_QUEUE, 7);
	if (error)
		return error;

	if (!iwm_nic_lock(sc))
		return EBUSY;

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
	if (sc->cfg->device_family != IWM_DEVICE_FAMILY_8000) {
		iwm_clear_bits_prph(sc, IWM_APMG_PCIDEV_STT_REG,
		    IWM_APMG_PCIDEV_STT_VAL_L1_ACT_DIS);
	}

	return error;
}

/*
 * NVM read access and content parsing.  We do not support
 * external NVM or writing NVM.
 * iwlwifi/mvm/nvm.c
 */

/* Default NVM size to read */
#define IWM_NVM_DEFAULT_CHUNK_SIZE	(2*1024)

#define IWM_NVM_WRITE_OPCODE 1
#define IWM_NVM_READ_OPCODE 0

/* load nvm chunk response */
enum {
	IWM_READ_NVM_CHUNK_SUCCEED = 0,
	IWM_READ_NVM_CHUNK_NOT_VALID_ADDRESS = 1
};

static int
iwm_nvm_read_chunk(struct iwm_softc *sc, uint16_t section,
	uint16_t offset, uint16_t length, uint8_t *data, uint16_t *len)
{
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
		.flags = IWM_CMD_WANT_SKB | IWM_CMD_SEND_IN_RFKILL,
		.data = { &nvm_access_cmd, },
	};
	int ret, bytes_read, offset_read;
	uint8_t *resp_data;

	cmd.len[0] = sizeof(struct iwm_nvm_access_cmd);

	ret = iwm_send_cmd(sc, &cmd);
	if (ret) {
		device_printf(sc->sc_dev,
		    "Could not send NVM_ACCESS command (error=%d)\n", ret);
		return ret;
	}

	pkt = cmd.resp_pkt;

	/* Extract NVM response */
	nvm_resp = (void *)pkt->data;
	ret = le16toh(nvm_resp->status);
	bytes_read = le16toh(nvm_resp->length);
	offset_read = le16toh(nvm_resp->offset);
	resp_data = nvm_resp->data;
	if (ret) {
		if ((offset != 0) &&
		    (ret == IWM_READ_NVM_CHUNK_NOT_VALID_ADDRESS)) {
			/*
			 * meaning of NOT_VALID_ADDRESS:
			 * driver try to read chunk from address that is
			 * multiple of 2K and got an error since addr is empty.
			 * meaning of (offset != 0): driver already
			 * read valid data from another chunk so this case
			 * is not an error.
			 */
			IWM_DPRINTF(sc, IWM_DEBUG_EEPROM | IWM_DEBUG_RESET,
				    "NVM access command failed on offset 0x%x since that section size is multiple 2K\n",
				    offset);
			*len = 0;
			ret = 0;
		} else {
			IWM_DPRINTF(sc, IWM_DEBUG_EEPROM | IWM_DEBUG_RESET,
				    "NVM access command failed with status %d\n", ret);
			ret = EIO;
		}
		goto exit;
	}

	if (offset_read != offset) {
		device_printf(sc->sc_dev,
		    "NVM ACCESS response with invalid offset %d\n",
		    offset_read);
		ret = EINVAL;
		goto exit;
	}

	if (bytes_read > length) {
		device_printf(sc->sc_dev,
		    "NVM ACCESS response with too much data "
		    "(%d bytes requested, %d bytes received)\n",
		    length, bytes_read);
		ret = EINVAL;
		goto exit;
	}

	/* Write data to NVM */
	memcpy(data + offset, resp_data, bytes_read);
	*len = bytes_read;

 exit:
	iwm_free_resp(sc, &cmd);
	return ret;
}

/*
 * Reads an NVM section completely.
 * NICs prior to 7000 family don't have a real NVM, but just read
 * section 0 which is the EEPROM. Because the EEPROM reading is unlimited
 * by uCode, we need to manually check in this case that we don't
 * overflow and try to read more than the EEPROM size.
 * For 7000 family NICs, we supply the maximal size we can read, and
 * the uCode fills the response with as much data as we can,
 * without overflowing, so no check is needed.
 */
static int
iwm_nvm_read_section(struct iwm_softc *sc,
	uint16_t section, uint8_t *data, uint16_t *len, uint32_t size_read)
{
	uint16_t seglen, length, offset = 0;
	int ret;

	/* Set nvm section read length */
	length = IWM_NVM_DEFAULT_CHUNK_SIZE;

	seglen = length;

	/* Read the NVM until exhausted (reading less than requested) */
	while (seglen == length) {
		/* Check no memory assumptions fail and cause an overflow */
		if ((size_read + offset + length) >
		    sc->cfg->eeprom_size) {
			device_printf(sc->sc_dev,
			    "EEPROM size is too small for NVM\n");
			return ENOBUFS;
		}

		ret = iwm_nvm_read_chunk(sc, section, offset, length, data, &seglen);
		if (ret) {
			IWM_DPRINTF(sc, IWM_DEBUG_EEPROM | IWM_DEBUG_RESET,
				    "Cannot read NVM from section %d offset %d, length %d\n",
				    section, offset, length);
			return ret;
		}
		offset += seglen;
	}

	IWM_DPRINTF(sc, IWM_DEBUG_EEPROM | IWM_DEBUG_RESET,
		    "NVM section %d read completed\n", section);
	*len = offset;
	return 0;
}

/*
 * BEGIN IWM_NVM_PARSE
 */

/* iwlwifi/iwl-nvm-parse.c */

/* NVM offsets (in words) definitions */
enum iwm_nvm_offsets {
	/* NVM HW-Section offset (in words) definitions */
	IWM_HW_ADDR = 0x15,

/* NVM SW-Section offset (in words) definitions */
	IWM_NVM_SW_SECTION = 0x1C0,
	IWM_NVM_VERSION = 0,
	IWM_RADIO_CFG = 1,
	IWM_SKU = 2,
	IWM_N_HW_ADDRS = 3,
	IWM_NVM_CHANNELS = 0x1E0 - IWM_NVM_SW_SECTION,

/* NVM calibration section offset (in words) definitions */
	IWM_NVM_CALIB_SECTION = 0x2B8,
	IWM_XTAL_CALIB = 0x316 - IWM_NVM_CALIB_SECTION
};

enum iwm_8000_nvm_offsets {
	/* NVM HW-Section offset (in words) definitions */
	IWM_HW_ADDR0_WFPM_8000 = 0x12,
	IWM_HW_ADDR1_WFPM_8000 = 0x16,
	IWM_HW_ADDR0_PCIE_8000 = 0x8A,
	IWM_HW_ADDR1_PCIE_8000 = 0x8E,
	IWM_MAC_ADDRESS_OVERRIDE_8000 = 1,

	/* NVM SW-Section offset (in words) definitions */
	IWM_NVM_SW_SECTION_8000 = 0x1C0,
	IWM_NVM_VERSION_8000 = 0,
	IWM_RADIO_CFG_8000 = 0,
	IWM_SKU_8000 = 2,
	IWM_N_HW_ADDRS_8000 = 3,

	/* NVM REGULATORY -Section offset (in words) definitions */
	IWM_NVM_CHANNELS_8000 = 0,
	IWM_NVM_LAR_OFFSET_8000_OLD = 0x4C7,
	IWM_NVM_LAR_OFFSET_8000 = 0x507,
	IWM_NVM_LAR_ENABLED_8000 = 0x7,

	/* NVM calibration section offset (in words) definitions */
	IWM_NVM_CALIB_SECTION_8000 = 0x2B8,
	IWM_XTAL_CALIB_8000 = 0x316 - IWM_NVM_CALIB_SECTION_8000
};

/* SKU Capabilities (actual values from NVM definition) */
enum nvm_sku_bits {
	IWM_NVM_SKU_CAP_BAND_24GHZ	= (1 << 0),
	IWM_NVM_SKU_CAP_BAND_52GHZ	= (1 << 1),
	IWM_NVM_SKU_CAP_11N_ENABLE	= (1 << 2),
	IWM_NVM_SKU_CAP_11AC_ENABLE	= (1 << 3),
};

/* radio config bits (actual values from NVM definition) */
#define IWM_NVM_RF_CFG_DASH_MSK(x)   (x & 0x3)         /* bits 0-1   */
#define IWM_NVM_RF_CFG_STEP_MSK(x)   ((x >> 2)  & 0x3) /* bits 2-3   */
#define IWM_NVM_RF_CFG_TYPE_MSK(x)   ((x >> 4)  & 0x3) /* bits 4-5   */
#define IWM_NVM_RF_CFG_PNUM_MSK(x)   ((x >> 6)  & 0x3) /* bits 6-7   */
#define IWM_NVM_RF_CFG_TX_ANT_MSK(x) ((x >> 8)  & 0xF) /* bits 8-11  */
#define IWM_NVM_RF_CFG_RX_ANT_MSK(x) ((x >> 12) & 0xF) /* bits 12-15 */

#define IWM_NVM_RF_CFG_FLAVOR_MSK_8000(x)	(x & 0xF)
#define IWM_NVM_RF_CFG_DASH_MSK_8000(x)		((x >> 4) & 0xF)
#define IWM_NVM_RF_CFG_STEP_MSK_8000(x)		((x >> 8) & 0xF)
#define IWM_NVM_RF_CFG_TYPE_MSK_8000(x)		((x >> 12) & 0xFFF)
#define IWM_NVM_RF_CFG_TX_ANT_MSK_8000(x)	((x >> 24) & 0xF)
#define IWM_NVM_RF_CFG_RX_ANT_MSK_8000(x)	((x >> 28) & 0xF)

/**
 * enum iwm_nvm_channel_flags - channel flags in NVM
 * @IWM_NVM_CHANNEL_VALID: channel is usable for this SKU/geo
 * @IWM_NVM_CHANNEL_IBSS: usable as an IBSS channel
 * @IWM_NVM_CHANNEL_ACTIVE: active scanning allowed
 * @IWM_NVM_CHANNEL_RADAR: radar detection required
 * XXX cannot find this (DFS) flag in iwm-nvm-parse.c
 * @IWM_NVM_CHANNEL_DFS: dynamic freq selection candidate
 * @IWM_NVM_CHANNEL_WIDE: 20 MHz channel okay (?)
 * @IWM_NVM_CHANNEL_40MHZ: 40 MHz channel okay (?)
 * @IWM_NVM_CHANNEL_80MHZ: 80 MHz channel okay (?)
 * @IWM_NVM_CHANNEL_160MHZ: 160 MHz channel okay (?)
 */
enum iwm_nvm_channel_flags {
	IWM_NVM_CHANNEL_VALID = (1 << 0),
	IWM_NVM_CHANNEL_IBSS = (1 << 1),
	IWM_NVM_CHANNEL_ACTIVE = (1 << 3),
	IWM_NVM_CHANNEL_RADAR = (1 << 4),
	IWM_NVM_CHANNEL_DFS = (1 << 7),
	IWM_NVM_CHANNEL_WIDE = (1 << 8),
	IWM_NVM_CHANNEL_40MHZ = (1 << 9),
	IWM_NVM_CHANNEL_80MHZ = (1 << 10),
	IWM_NVM_CHANNEL_160MHZ = (1 << 11),
};

/*
 * Translate EEPROM flags to net80211.
 */
static uint32_t
iwm_eeprom_channel_flags(uint16_t ch_flags)
{
	uint32_t nflags;

	nflags = 0;
	if ((ch_flags & IWM_NVM_CHANNEL_ACTIVE) == 0)
		nflags |= IEEE80211_CHAN_PASSIVE;
	if ((ch_flags & IWM_NVM_CHANNEL_IBSS) == 0)
		nflags |= IEEE80211_CHAN_NOADHOC;
	if (ch_flags & IWM_NVM_CHANNEL_RADAR) {
		nflags |= IEEE80211_CHAN_DFS;
		/* Just in case. */
		nflags |= IEEE80211_CHAN_NOADHOC;
	}

	return (nflags);
}

static void
iwm_add_channel_band(struct iwm_softc *sc, struct ieee80211_channel chans[],
    int maxchans, int *nchans, int ch_idx, size_t ch_num,
    const uint8_t bands[])
{
	const uint16_t * const nvm_ch_flags = sc->nvm_data->nvm_ch_flags;
	uint32_t nflags;
	uint16_t ch_flags;
	uint8_t ieee;
	int error;

	for (; ch_idx < ch_num; ch_idx++) {
		ch_flags = le16_to_cpup(nvm_ch_flags + ch_idx);
		if (sc->cfg->device_family == IWM_DEVICE_FAMILY_7000)
			ieee = iwm_nvm_channels[ch_idx];
		else
			ieee = iwm_nvm_channels_8000[ch_idx];

		if (!(ch_flags & IWM_NVM_CHANNEL_VALID)) {
			IWM_DPRINTF(sc, IWM_DEBUG_EEPROM,
			    "Ch. %d Flags %x [%sGHz] - No traffic\n",
			    ieee, ch_flags,
			    (ch_idx >= IWM_NUM_2GHZ_CHANNELS) ?
			    "5.2" : "2.4");
			continue;
		}

		nflags = iwm_eeprom_channel_flags(ch_flags);
		error = ieee80211_add_channel(chans, maxchans, nchans,
		    ieee, 0, 0, nflags, bands);
		if (error != 0)
			break;

		IWM_DPRINTF(sc, IWM_DEBUG_EEPROM,
		    "Ch. %d Flags %x [%sGHz] - Added\n",
		    ieee, ch_flags,
		    (ch_idx >= IWM_NUM_2GHZ_CHANNELS) ?
		    "5.2" : "2.4");
	}
}

static void
iwm_init_channel_map(struct ieee80211com *ic, int maxchans, int *nchans,
    struct ieee80211_channel chans[])
{
	struct iwm_softc *sc = ic->ic_softc;
	struct iwm_nvm_data *data = sc->nvm_data;
	uint8_t bands[IEEE80211_MODE_BYTES];
	size_t ch_num;

	memset(bands, 0, sizeof(bands));
	/* 1-13: 11b/g channels. */
	setbit(bands, IEEE80211_MODE_11B);
	setbit(bands, IEEE80211_MODE_11G);
	iwm_add_channel_band(sc, chans, maxchans, nchans, 0,
	    IWM_NUM_2GHZ_CHANNELS - 1, bands);

	/* 14: 11b channel only. */
	clrbit(bands, IEEE80211_MODE_11G);
	iwm_add_channel_band(sc, chans, maxchans, nchans,
	    IWM_NUM_2GHZ_CHANNELS - 1, IWM_NUM_2GHZ_CHANNELS, bands);

	if (data->sku_cap_band_52GHz_enable) {
		if (sc->cfg->device_family == IWM_DEVICE_FAMILY_7000)
			ch_num = nitems(iwm_nvm_channels);
		else
			ch_num = nitems(iwm_nvm_channels_8000);
		memset(bands, 0, sizeof(bands));
		setbit(bands, IEEE80211_MODE_11A);
		iwm_add_channel_band(sc, chans, maxchans, nchans,
		    IWM_NUM_2GHZ_CHANNELS, ch_num, bands);
	}
}

static void
iwm_set_hw_address_family_8000(struct iwm_softc *sc, struct iwm_nvm_data *data,
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
		IEEE80211_ADDR_COPY(data->hw_addr, hw_addr);

		/*
		 * Force the use of the OTP MAC address in case of reserved MAC
		 * address in the NVM, or if address is given but invalid.
		 */
		if (!IEEE80211_ADDR_EQ(reserved_mac, hw_addr) &&
		    !IEEE80211_ADDR_EQ(ieee80211broadcastaddr, data->hw_addr) &&
		    iwm_is_valid_ether_addr(data->hw_addr) &&
		    !IEEE80211_IS_MULTICAST(data->hw_addr))
			return;

		IWM_DPRINTF(sc, IWM_DEBUG_RESET,
		    "%s: mac address from nvm override section invalid\n",
		    __func__);
	}

	if (nvm_hw) {
		/* read the mac address from WFMP registers */
		uint32_t mac_addr0 =
		    htole32(iwm_read_prph(sc, IWM_WFMP_MAC_ADDR_0));
		uint32_t mac_addr1 =
		    htole32(iwm_read_prph(sc, IWM_WFMP_MAC_ADDR_1));

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

	device_printf(sc->sc_dev, "%s: mac address not found\n", __func__);
	memset(data->hw_addr, 0, sizeof(data->hw_addr));
}

static int
iwm_get_sku(const struct iwm_softc *sc, const uint16_t *nvm_sw,
	    const uint16_t *phy_sku)
{
	if (sc->cfg->device_family != IWM_DEVICE_FAMILY_8000)
		return le16_to_cpup(nvm_sw + IWM_SKU);

	return le32_to_cpup((const uint32_t *)(phy_sku + IWM_SKU_8000));
}

static int
iwm_get_nvm_version(const struct iwm_softc *sc, const uint16_t *nvm_sw)
{
	if (sc->cfg->device_family != IWM_DEVICE_FAMILY_8000)
		return le16_to_cpup(nvm_sw + IWM_NVM_VERSION);
	else
		return le32_to_cpup((const uint32_t *)(nvm_sw +
						IWM_NVM_VERSION_8000));
}

static int
iwm_get_radio_cfg(const struct iwm_softc *sc, const uint16_t *nvm_sw,
		  const uint16_t *phy_sku)
{
        if (sc->cfg->device_family != IWM_DEVICE_FAMILY_8000)
                return le16_to_cpup(nvm_sw + IWM_RADIO_CFG);

        return le32_to_cpup((const uint32_t *)(phy_sku + IWM_RADIO_CFG_8000));
}

static int
iwm_get_n_hw_addrs(const struct iwm_softc *sc, const uint16_t *nvm_sw)
{
	int n_hw_addr;

	if (sc->cfg->device_family != IWM_DEVICE_FAMILY_8000)
		return le16_to_cpup(nvm_sw + IWM_N_HW_ADDRS);

	n_hw_addr = le32_to_cpup((const uint32_t *)(nvm_sw + IWM_N_HW_ADDRS_8000));

        return n_hw_addr & IWM_N_HW_ADDR_MASK;
}

static void
iwm_set_radio_cfg(const struct iwm_softc *sc, struct iwm_nvm_data *data,
		  uint32_t radio_cfg)
{
	if (sc->cfg->device_family != IWM_DEVICE_FAMILY_8000) {
		data->radio_cfg_type = IWM_NVM_RF_CFG_TYPE_MSK(radio_cfg);
		data->radio_cfg_step = IWM_NVM_RF_CFG_STEP_MSK(radio_cfg);
		data->radio_cfg_dash = IWM_NVM_RF_CFG_DASH_MSK(radio_cfg);
		data->radio_cfg_pnum = IWM_NVM_RF_CFG_PNUM_MSK(radio_cfg);
		return;
	}

	/* set the radio configuration for family 8000 */
	data->radio_cfg_type = IWM_NVM_RF_CFG_TYPE_MSK_8000(radio_cfg);
	data->radio_cfg_step = IWM_NVM_RF_CFG_STEP_MSK_8000(radio_cfg);
	data->radio_cfg_dash = IWM_NVM_RF_CFG_DASH_MSK_8000(radio_cfg);
	data->radio_cfg_pnum = IWM_NVM_RF_CFG_FLAVOR_MSK_8000(radio_cfg);
	data->valid_tx_ant = IWM_NVM_RF_CFG_TX_ANT_MSK_8000(radio_cfg);
	data->valid_rx_ant = IWM_NVM_RF_CFG_RX_ANT_MSK_8000(radio_cfg);
}

static int
iwm_set_hw_address(struct iwm_softc *sc, struct iwm_nvm_data *data,
		   const uint16_t *nvm_hw, const uint16_t *mac_override)
{
#ifdef notyet /* for FAMILY 9000 */
	if (cfg->mac_addr_from_csr) {
		iwm_set_hw_address_from_csr(sc, data);
        } else
#endif
	if (sc->cfg->device_family != IWM_DEVICE_FAMILY_8000) {
		const uint8_t *hw_addr = (const uint8_t *)(nvm_hw + IWM_HW_ADDR);

		/* The byte order is little endian 16 bit, meaning 214365 */
		data->hw_addr[0] = hw_addr[1];
		data->hw_addr[1] = hw_addr[0];
		data->hw_addr[2] = hw_addr[3];
		data->hw_addr[3] = hw_addr[2];
		data->hw_addr[4] = hw_addr[5];
		data->hw_addr[5] = hw_addr[4];
	} else {
		iwm_set_hw_address_family_8000(sc, data, mac_override, nvm_hw);
	}

	if (!iwm_is_valid_ether_addr(data->hw_addr)) {
		device_printf(sc->sc_dev, "no valid mac address was found\n");
		return EINVAL;
	}

	return 0;
}

static struct iwm_nvm_data *
iwm_parse_nvm_data(struct iwm_softc *sc,
		   const uint16_t *nvm_hw, const uint16_t *nvm_sw,
		   const uint16_t *nvm_calib, const uint16_t *mac_override,
		   const uint16_t *phy_sku, const uint16_t *regulatory)
{
	struct iwm_nvm_data *data;
	uint32_t sku, radio_cfg;
	uint16_t lar_config;

	if (sc->cfg->device_family != IWM_DEVICE_FAMILY_8000) {
		data = malloc(sizeof(*data) +
		    IWM_NUM_CHANNELS * sizeof(uint16_t),
		    M_DEVBUF, M_NOWAIT | M_ZERO);
	} else {
		data = malloc(sizeof(*data) +
		    IWM_NUM_CHANNELS_8000 * sizeof(uint16_t),
		    M_DEVBUF, M_NOWAIT | M_ZERO);
	}
	if (!data)
		return NULL;

	data->nvm_version = iwm_get_nvm_version(sc, nvm_sw);

	radio_cfg = iwm_get_radio_cfg(sc, nvm_sw, phy_sku);
	iwm_set_radio_cfg(sc, data, radio_cfg);

	sku = iwm_get_sku(sc, nvm_sw, phy_sku);
	data->sku_cap_band_24GHz_enable = sku & IWM_NVM_SKU_CAP_BAND_24GHZ;
	data->sku_cap_band_52GHz_enable = sku & IWM_NVM_SKU_CAP_BAND_52GHZ;
	data->sku_cap_11n_enable = 0;

	data->n_hw_addrs = iwm_get_n_hw_addrs(sc, nvm_sw);

	if (sc->cfg->device_family == IWM_DEVICE_FAMILY_8000) {
		uint16_t lar_offset = data->nvm_version < 0xE39 ?
				       IWM_NVM_LAR_OFFSET_8000_OLD :
				       IWM_NVM_LAR_OFFSET_8000;

		lar_config = le16_to_cpup(regulatory + lar_offset);
		data->lar_enabled = !!(lar_config &
				       IWM_NVM_LAR_ENABLED_8000);
	}

	/* If no valid mac address was found - bail out */
	if (iwm_set_hw_address(sc, data, nvm_hw, mac_override)) {
		free(data, M_DEVBUF);
		return NULL;
	}

	if (sc->cfg->device_family == IWM_DEVICE_FAMILY_7000) {
		memcpy(data->nvm_ch_flags, sc->cfg->nvm_type == IWM_NVM_SDP ?
		    &regulatory[0] : &nvm_sw[IWM_NVM_CHANNELS],
		    IWM_NUM_CHANNELS * sizeof(uint16_t));
	} else {
		memcpy(data->nvm_ch_flags, &regulatory[IWM_NVM_CHANNELS_8000],
		    IWM_NUM_CHANNELS_8000 * sizeof(uint16_t));
	}

	return data;
}

static void
iwm_free_nvm_data(struct iwm_nvm_data *data)
{
	if (data != NULL)
		free(data, M_DEVBUF);
}

static struct iwm_nvm_data *
iwm_parse_nvm_sections(struct iwm_softc *sc, struct iwm_nvm_section *sections)
{
	const uint16_t *hw, *sw, *calib, *regulatory, *mac_override, *phy_sku;

	/* Checking for required sections */
	if (sc->cfg->device_family == IWM_DEVICE_FAMILY_7000) {
		if (!sections[IWM_NVM_SECTION_TYPE_SW].data ||
		    !sections[sc->cfg->nvm_hw_section_num].data) {
			device_printf(sc->sc_dev,
			    "Can't parse empty OTP/NVM sections\n");
			return NULL;
		}
	} else if (sc->cfg->device_family == IWM_DEVICE_FAMILY_8000) {
		/* SW and REGULATORY sections are mandatory */
		if (!sections[IWM_NVM_SECTION_TYPE_SW].data ||
		    !sections[IWM_NVM_SECTION_TYPE_REGULATORY].data) {
			device_printf(sc->sc_dev,
			    "Can't parse empty OTP/NVM sections\n");
			return NULL;
		}
		/* MAC_OVERRIDE or at least HW section must exist */
		if (!sections[sc->cfg->nvm_hw_section_num].data &&
		    !sections[IWM_NVM_SECTION_TYPE_MAC_OVERRIDE].data) {
			device_printf(sc->sc_dev,
			    "Can't parse mac_address, empty sections\n");
			return NULL;
		}

		/* PHY_SKU section is mandatory in B0 */
		if (!sections[IWM_NVM_SECTION_TYPE_PHY_SKU].data) {
			device_printf(sc->sc_dev,
			    "Can't parse phy_sku in B0, empty sections\n");
			return NULL;
		}
	} else {
		panic("unknown device family %d\n", sc->cfg->device_family);
	}

	hw = (const uint16_t *) sections[sc->cfg->nvm_hw_section_num].data;
	sw = (const uint16_t *)sections[IWM_NVM_SECTION_TYPE_SW].data;
	calib = (const uint16_t *)
	    sections[IWM_NVM_SECTION_TYPE_CALIBRATION].data;
	regulatory = sc->cfg->nvm_type == IWM_NVM_SDP ?
	    (const uint16_t *)sections[IWM_NVM_SECTION_TYPE_REGULATORY_SDP].data :
	    (const uint16_t *)sections[IWM_NVM_SECTION_TYPE_REGULATORY].data;
	mac_override = (const uint16_t *)
	    sections[IWM_NVM_SECTION_TYPE_MAC_OVERRIDE].data;
	phy_sku = (const uint16_t *)sections[IWM_NVM_SECTION_TYPE_PHY_SKU].data;

	return iwm_parse_nvm_data(sc, hw, sw, calib, mac_override,
	    phy_sku, regulatory);
}

static int
iwm_nvm_init(struct iwm_softc *sc)
{
	struct iwm_nvm_section nvm_sections[IWM_NVM_MAX_NUM_SECTIONS];
	int i, ret, section;
	uint32_t size_read = 0;
	uint8_t *nvm_buffer, *temp;
	uint16_t len;

	memset(nvm_sections, 0, sizeof(nvm_sections));

	if (sc->cfg->nvm_hw_section_num >= IWM_NVM_MAX_NUM_SECTIONS)
		return EINVAL;

	/* load NVM values from nic */
	/* Read From FW NVM */
	IWM_DPRINTF(sc, IWM_DEBUG_EEPROM, "Read from NVM\n");

	nvm_buffer = malloc(sc->cfg->eeprom_size, M_DEVBUF, M_NOWAIT | M_ZERO);
	if (!nvm_buffer)
		return ENOMEM;
	for (section = 0; section < IWM_NVM_MAX_NUM_SECTIONS; section++) {
		/* we override the constness for initial read */
		ret = iwm_nvm_read_section(sc, section, nvm_buffer,
					   &len, size_read);
		if (ret)
			continue;
		size_read += len;
		temp = malloc(len, M_DEVBUF, M_NOWAIT);
		if (!temp) {
			ret = ENOMEM;
			break;
		}
		memcpy(temp, nvm_buffer, len);

		nvm_sections[section].data = temp;
		nvm_sections[section].length = len;
	}
	if (!size_read)
		device_printf(sc->sc_dev, "OTP is blank\n");
	free(nvm_buffer, M_DEVBUF);

	sc->nvm_data = iwm_parse_nvm_sections(sc, nvm_sections);
	if (!sc->nvm_data)
		return EINVAL;
	IWM_DPRINTF(sc, IWM_DEBUG_EEPROM | IWM_DEBUG_RESET,
		    "nvm version = %x\n", sc->nvm_data->nvm_version);

	for (i = 0; i < IWM_NVM_MAX_NUM_SECTIONS; i++) {
		if (nvm_sections[i].data != NULL)
			free(nvm_sections[i].data, M_DEVBUF);
	}

	return 0;
}

static int
iwm_pcie_load_section(struct iwm_softc *sc, uint8_t section_num,
	const struct iwm_fw_desc *section)
{
	struct iwm_dma_info *dma = &sc->fw_dma;
	uint8_t *v_addr;
	bus_addr_t p_addr;
	uint32_t offset, chunk_sz = MIN(IWM_FH_MEM_TB_MAX_LENGTH, section->len);
	int ret = 0;

	IWM_DPRINTF(sc, IWM_DEBUG_RESET,
		    "%s: [%d] uCode section being loaded...\n",
		    __func__, section_num);

	v_addr = dma->vaddr;
	p_addr = dma->paddr;

	for (offset = 0; offset < section->len; offset += chunk_sz) {
		uint32_t copy_size, dst_addr;
		int extended_addr = FALSE;

		copy_size = MIN(chunk_sz, section->len - offset);
		dst_addr = section->offset + offset;

		if (dst_addr >= IWM_FW_MEM_EXTENDED_START &&
		    dst_addr <= IWM_FW_MEM_EXTENDED_END)
			extended_addr = TRUE;

		if (extended_addr)
			iwm_set_bits_prph(sc, IWM_LMPM_CHICK,
					  IWM_LMPM_CHICK_EXTENDED_ADDR_SPACE);

		memcpy(v_addr, (const uint8_t *)section->data + offset,
		    copy_size);
		bus_dmamap_sync(dma->tag, dma->map, BUS_DMASYNC_PREWRITE);
		ret = iwm_pcie_load_firmware_chunk(sc, dst_addr, p_addr,
						   copy_size);

		if (extended_addr)
			iwm_clear_bits_prph(sc, IWM_LMPM_CHICK,
					    IWM_LMPM_CHICK_EXTENDED_ADDR_SPACE);

		if (ret) {
			device_printf(sc->sc_dev,
			    "%s: Could not load the [%d] uCode section\n",
			    __func__, section_num);
			break;
		}
	}

	return ret;
}

/*
 * ucode
 */
static int
iwm_pcie_load_firmware_chunk(struct iwm_softc *sc, uint32_t dst_addr,
			     bus_addr_t phy_addr, uint32_t byte_cnt)
{
	sc->sc_fw_chunk_done = 0;

	if (!iwm_nic_lock(sc))
		return EBUSY;

	IWM_WRITE(sc, IWM_FH_TCSR_CHNL_TX_CONFIG_REG(IWM_FH_SRVC_CHNL),
	    IWM_FH_TCSR_TX_CONFIG_REG_VAL_DMA_CHNL_PAUSE);

	IWM_WRITE(sc, IWM_FH_SRVC_CHNL_SRAM_ADDR_REG(IWM_FH_SRVC_CHNL),
	    dst_addr);

	IWM_WRITE(sc, IWM_FH_TFDIB_CTRL0_REG(IWM_FH_SRVC_CHNL),
	    phy_addr & IWM_FH_MEM_TFDIB_DRAM_ADDR_LSB_MSK);

	IWM_WRITE(sc, IWM_FH_TFDIB_CTRL1_REG(IWM_FH_SRVC_CHNL),
	    (iwm_get_dma_hi_addr(phy_addr)
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

	/* wait up to 5s for this segment to load */
	msleep(&sc->sc_fw, &sc->sc_mtx, 0, "iwmfw", hz * 5);

	if (!sc->sc_fw_chunk_done) {
		device_printf(sc->sc_dev,
		    "fw chunk addr 0x%x len %d failed to load\n",
		    dst_addr, byte_cnt);
		return ETIMEDOUT;
	}

	return 0;
}

static int
iwm_pcie_load_cpu_sections_8000(struct iwm_softc *sc,
	const struct iwm_fw_img *image, int cpu, int *first_ucode_section)
{
	int shift_param;
	int i, ret = 0, sec_num = 0x1;
	uint32_t val, last_read_idx = 0;

	if (cpu == 1) {
		shift_param = 0;
		*first_ucode_section = 0;
	} else {
		shift_param = 16;
		(*first_ucode_section)++;
	}

	for (i = *first_ucode_section; i < IWM_UCODE_SECTION_MAX; i++) {
		last_read_idx = i;

		/*
		 * CPU1_CPU2_SEPARATOR_SECTION delimiter - separate between
		 * CPU1 to CPU2.
		 * PAGING_SEPARATOR_SECTION delimiter - separate between
		 * CPU2 non paged to CPU2 paging sec.
		 */
		if (!image->sec[i].data ||
		    image->sec[i].offset == IWM_CPU1_CPU2_SEPARATOR_SECTION ||
		    image->sec[i].offset == IWM_PAGING_SEPARATOR_SECTION) {
			IWM_DPRINTF(sc, IWM_DEBUG_RESET,
				    "Break since Data not valid or Empty section, sec = %d\n",
				    i);
			break;
		}
		ret = iwm_pcie_load_section(sc, i, &image->sec[i]);
		if (ret)
			return ret;

		/* Notify the ucode of the loaded section number and status */
		if (iwm_nic_lock(sc)) {
			val = IWM_READ(sc, IWM_FH_UCODE_LOAD_STATUS);
			val = val | (sec_num << shift_param);
			IWM_WRITE(sc, IWM_FH_UCODE_LOAD_STATUS, val);
			sec_num = (sec_num << 1) | 0x1;
			iwm_nic_unlock(sc);
		}
	}

	*first_ucode_section = last_read_idx;

	iwm_enable_interrupts(sc);

	if (iwm_nic_lock(sc)) {
		if (cpu == 1)
			IWM_WRITE(sc, IWM_FH_UCODE_LOAD_STATUS, 0xFFFF);
		else
			IWM_WRITE(sc, IWM_FH_UCODE_LOAD_STATUS, 0xFFFFFFFF);
		iwm_nic_unlock(sc);
	}

	return 0;
}

static int
iwm_pcie_load_cpu_sections(struct iwm_softc *sc,
	const struct iwm_fw_img *image, int cpu, int *first_ucode_section)
{
	int shift_param;
	int i, ret = 0;
	uint32_t last_read_idx = 0;

	if (cpu == 1) {
		shift_param = 0;
		*first_ucode_section = 0;
	} else {
		shift_param = 16;
		(*first_ucode_section)++;
	}

	for (i = *first_ucode_section; i < IWM_UCODE_SECTION_MAX; i++) {
		last_read_idx = i;

		/*
		 * CPU1_CPU2_SEPARATOR_SECTION delimiter - separate between
		 * CPU1 to CPU2.
		 * PAGING_SEPARATOR_SECTION delimiter - separate between
		 * CPU2 non paged to CPU2 paging sec.
		 */
		if (!image->sec[i].data ||
		    image->sec[i].offset == IWM_CPU1_CPU2_SEPARATOR_SECTION ||
		    image->sec[i].offset == IWM_PAGING_SEPARATOR_SECTION) {
			IWM_DPRINTF(sc, IWM_DEBUG_RESET,
				    "Break since Data not valid or Empty section, sec = %d\n",
				     i);
			break;
		}

		ret = iwm_pcie_load_section(sc, i, &image->sec[i]);
		if (ret)
			return ret;
	}

	*first_ucode_section = last_read_idx;

	return 0;

}

static int
iwm_pcie_load_given_ucode(struct iwm_softc *sc, const struct iwm_fw_img *image)
{
	int ret = 0;
	int first_ucode_section;

	IWM_DPRINTF(sc, IWM_DEBUG_RESET, "working with %s CPU\n",
		     image->is_dual_cpus ? "Dual" : "Single");

	/* load to FW the binary non secured sections of CPU1 */
	ret = iwm_pcie_load_cpu_sections(sc, image, 1, &first_ucode_section);
	if (ret)
		return ret;

	if (image->is_dual_cpus) {
		/* set CPU2 header address */
		if (iwm_nic_lock(sc)) {
			iwm_write_prph(sc,
				       IWM_LMPM_SECURE_UCODE_LOAD_CPU2_HDR_ADDR,
				       IWM_LMPM_SECURE_CPU2_HDR_MEM_SPACE);
			iwm_nic_unlock(sc);
		}

		/* load to FW the binary sections of CPU2 */
		ret = iwm_pcie_load_cpu_sections(sc, image, 2,
						 &first_ucode_section);
		if (ret)
			return ret;
	}

	iwm_enable_interrupts(sc);

	/* release CPU reset */
	IWM_WRITE(sc, IWM_CSR_RESET, 0);

	return 0;
}

int
iwm_pcie_load_given_ucode_8000(struct iwm_softc *sc,
	const struct iwm_fw_img *image)
{
	int ret = 0;
	int first_ucode_section;

	IWM_DPRINTF(sc, IWM_DEBUG_RESET, "working with %s CPU\n",
		    image->is_dual_cpus ? "Dual" : "Single");

	/* configure the ucode to be ready to get the secured image */
	/* release CPU reset */
	if (iwm_nic_lock(sc)) {
		iwm_write_prph(sc, IWM_RELEASE_CPU_RESET,
		    IWM_RELEASE_CPU_RESET_BIT);
		iwm_nic_unlock(sc);
	}

	/* load to FW the binary Secured sections of CPU1 */
	ret = iwm_pcie_load_cpu_sections_8000(sc, image, 1,
	    &first_ucode_section);
	if (ret)
		return ret;

	/* load to FW the binary sections of CPU2 */
	return iwm_pcie_load_cpu_sections_8000(sc, image, 2,
	    &first_ucode_section);
}

/* XXX Get rid of this definition */
static inline void
iwm_enable_fw_load_int(struct iwm_softc *sc)
{
	IWM_DPRINTF(sc, IWM_DEBUG_INTR, "Enabling FW load interrupt\n");
	sc->sc_intmask = IWM_CSR_INT_BIT_FH_TX;
	IWM_WRITE(sc, IWM_CSR_INT_MASK, sc->sc_intmask);
}

/* XXX Add proper rfkill support code */
static int
iwm_start_fw(struct iwm_softc *sc, const struct iwm_fw_img *fw)
{
	int ret;

	/* This may fail if AMT took ownership of the device */
	if (iwm_prepare_card_hw(sc)) {
		device_printf(sc->sc_dev,
		    "%s: Exit HW not ready\n", __func__);
		ret = EIO;
		goto out;
	}

	IWM_WRITE(sc, IWM_CSR_INT, 0xFFFFFFFF);

	iwm_disable_interrupts(sc);

	/* make sure rfkill handshake bits are cleared */
	IWM_WRITE(sc, IWM_CSR_UCODE_DRV_GP1_CLR, IWM_CSR_UCODE_SW_BIT_RFKILL);
	IWM_WRITE(sc, IWM_CSR_UCODE_DRV_GP1_CLR,
	    IWM_CSR_UCODE_DRV_GP1_BIT_CMD_BLOCKED);

	/* clear (again), then enable host interrupts */
	IWM_WRITE(sc, IWM_CSR_INT, 0xFFFFFFFF);

	ret = iwm_nic_init(sc);
	if (ret) {
		device_printf(sc->sc_dev, "%s: Unable to init nic\n", __func__);
		goto out;
	}

	/*
	 * Now, we load the firmware and don't want to be interrupted, even
	 * by the RF-Kill interrupt (hence mask all the interrupt besides the
	 * FH_TX interrupt which is needed to load the firmware). If the
	 * RF-Kill switch is toggled, we will find out after having loaded
	 * the firmware and return the proper value to the caller.
	 */
	iwm_enable_fw_load_int(sc);

	/* really make sure rfkill handshake bits are cleared */
	/* maybe we should write a few times more?  just to make sure */
	IWM_WRITE(sc, IWM_CSR_UCODE_DRV_GP1_CLR, IWM_CSR_UCODE_SW_BIT_RFKILL);
	IWM_WRITE(sc, IWM_CSR_UCODE_DRV_GP1_CLR, IWM_CSR_UCODE_SW_BIT_RFKILL);

	/* Load the given image to the HW */
	if (sc->cfg->device_family == IWM_DEVICE_FAMILY_8000)
		ret = iwm_pcie_load_given_ucode_8000(sc, fw);
	else
		ret = iwm_pcie_load_given_ucode(sc, fw);

	/* XXX re-check RF-Kill state */

out:
	return ret;
}

static int
iwm_send_tx_ant_cfg(struct iwm_softc *sc, uint8_t valid_tx_ant)
{
	struct iwm_tx_ant_cfg_cmd tx_ant_cmd = {
		.valid = htole32(valid_tx_ant),
	};

	return iwm_mvm_send_cmd_pdu(sc, IWM_TX_ANT_CONFIGURATION_CMD,
	    IWM_CMD_SYNC, sizeof(tx_ant_cmd), &tx_ant_cmd);
}

/* iwlwifi: mvm/fw.c */
static int
iwm_send_phy_cfg_cmd(struct iwm_softc *sc)
{
	struct iwm_phy_cfg_cmd phy_cfg_cmd;
	enum iwm_ucode_type ucode_type = sc->cur_ucode;

	/* Set parameters */
	phy_cfg_cmd.phy_cfg = htole32(iwm_mvm_get_phy_config(sc));
	phy_cfg_cmd.calib_control.event_trigger =
	    sc->sc_default_calib[ucode_type].event_trigger;
	phy_cfg_cmd.calib_control.flow_trigger =
	    sc->sc_default_calib[ucode_type].flow_trigger;

	IWM_DPRINTF(sc, IWM_DEBUG_CMD | IWM_DEBUG_RESET,
	    "Sending Phy CFG command: 0x%x\n", phy_cfg_cmd.phy_cfg);
	return iwm_mvm_send_cmd_pdu(sc, IWM_PHY_CONFIGURATION_CMD, IWM_CMD_SYNC,
	    sizeof(phy_cfg_cmd), &phy_cfg_cmd);
}

static int
iwm_alive_fn(struct iwm_softc *sc, struct iwm_rx_packet *pkt, void *data)
{
	struct iwm_mvm_alive_data *alive_data = data;
	struct iwm_mvm_alive_resp_v3 *palive3;
	struct iwm_mvm_alive_resp *palive;
	struct iwm_umac_alive *umac;
	struct iwm_lmac_alive *lmac1;
	struct iwm_lmac_alive *lmac2 = NULL;
	uint16_t status;

	if (iwm_rx_packet_payload_len(pkt) == sizeof(*palive)) {
		palive = (void *)pkt->data;
		umac = &palive->umac_data;
		lmac1 = &palive->lmac_data[0];
		lmac2 = &palive->lmac_data[1];
		status = le16toh(palive->status);
	} else {
		palive3 = (void *)pkt->data;
		umac = &palive3->umac_data;
		lmac1 = &palive3->lmac_data;
		status = le16toh(palive3->status);
	}

	sc->error_event_table[0] = le32toh(lmac1->error_event_table_ptr);
	if (lmac2)
		sc->error_event_table[1] =
			le32toh(lmac2->error_event_table_ptr);
	sc->log_event_table = le32toh(lmac1->log_event_table_ptr);
	sc->umac_error_event_table = le32toh(umac->error_info_addr);
	alive_data->scd_base_addr = le32toh(lmac1->scd_base_ptr);
	alive_data->valid = status == IWM_ALIVE_STATUS_OK;
	if (sc->umac_error_event_table)
		sc->support_umac_log = TRUE;

	IWM_DPRINTF(sc, IWM_DEBUG_FW,
		    "Alive ucode status 0x%04x revision 0x%01X 0x%01X\n",
		    status, lmac1->ver_type, lmac1->ver_subtype);

	if (lmac2)
		IWM_DPRINTF(sc, IWM_DEBUG_FW, "Alive ucode CDB\n");

	IWM_DPRINTF(sc, IWM_DEBUG_FW,
		    "UMAC version: Major - 0x%x, Minor - 0x%x\n",
		    le32toh(umac->umac_major),
		    le32toh(umac->umac_minor));

	return TRUE;
}

static int
iwm_wait_phy_db_entry(struct iwm_softc *sc,
	struct iwm_rx_packet *pkt, void *data)
{
	struct iwm_phy_db *phy_db = data;

	if (pkt->hdr.code != IWM_CALIB_RES_NOTIF_PHY_DB) {
		if(pkt->hdr.code != IWM_INIT_COMPLETE_NOTIF) {
			device_printf(sc->sc_dev, "%s: Unexpected cmd: %d\n",
			    __func__, pkt->hdr.code);
		}
		return TRUE;
	}

	if (iwm_phy_db_set_section(phy_db, pkt)) {
		device_printf(sc->sc_dev,
		    "%s: iwm_phy_db_set_section failed\n", __func__);
	}

	return FALSE;
}

static int
iwm_mvm_load_ucode_wait_alive(struct iwm_softc *sc,
	enum iwm_ucode_type ucode_type)
{
	struct iwm_notification_wait alive_wait;
	struct iwm_mvm_alive_data alive_data;
	const struct iwm_fw_img *fw;
	enum iwm_ucode_type old_type = sc->cur_ucode;
	int error;
	static const uint16_t alive_cmd[] = { IWM_MVM_ALIVE };

	fw = &sc->sc_fw.img[ucode_type];
	sc->cur_ucode = ucode_type;
	sc->ucode_loaded = FALSE;

	memset(&alive_data, 0, sizeof(alive_data));
	iwm_init_notification_wait(sc->sc_notif_wait, &alive_wait,
				   alive_cmd, nitems(alive_cmd),
				   iwm_alive_fn, &alive_data);

	error = iwm_start_fw(sc, fw);
	if (error) {
		device_printf(sc->sc_dev, "iwm_start_fw: failed %d\n", error);
		sc->cur_ucode = old_type;
		iwm_remove_notification(sc->sc_notif_wait, &alive_wait);
		return error;
	}

	/*
	 * Some things may run in the background now, but we
	 * just wait for the ALIVE notification here.
	 */
	IWM_UNLOCK(sc);
	error = iwm_wait_notification(sc->sc_notif_wait, &alive_wait,
				      IWM_MVM_UCODE_ALIVE_TIMEOUT);
	IWM_LOCK(sc);
	if (error) {
		if (sc->cfg->device_family == IWM_DEVICE_FAMILY_8000) {
			uint32_t a = 0x5a5a5a5a, b = 0x5a5a5a5a;
			if (iwm_nic_lock(sc)) {
				a = iwm_read_prph(sc, IWM_SB_CPU_1_STATUS);
				b = iwm_read_prph(sc, IWM_SB_CPU_2_STATUS);
				iwm_nic_unlock(sc);
			}
			device_printf(sc->sc_dev,
			    "SecBoot CPU1 Status: 0x%x, CPU2 Status: 0x%x\n",
			    a, b);
		}
		sc->cur_ucode = old_type;
		return error;
	}

	if (!alive_data.valid) {
		device_printf(sc->sc_dev, "%s: Loaded ucode is not valid\n",
		    __func__);
		sc->cur_ucode = old_type;
		return EIO;
	}

	iwm_trans_pcie_fw_alive(sc, alive_data.scd_base_addr);

	/*
	 * configure and operate fw paging mechanism.
	 * driver configures the paging flow only once, CPU2 paging image
	 * included in the IWM_UCODE_INIT image.
	 */
	if (fw->paging_mem_size) {
		error = iwm_save_fw_paging(sc, fw);
		if (error) {
			device_printf(sc->sc_dev,
			    "%s: failed to save the FW paging image\n",
			    __func__);
			return error;
		}

		error = iwm_send_paging_cmd(sc, fw);
		if (error) {
			device_printf(sc->sc_dev,
			    "%s: failed to send the paging cmd\n", __func__);
			iwm_free_fw_paging(sc);
			return error;
		}
	}

	if (!error)
		sc->ucode_loaded = TRUE;
	return error;
}

/*
 * mvm misc bits
 */

/*
 * follows iwlwifi/fw.c
 */
static int
iwm_run_init_mvm_ucode(struct iwm_softc *sc, int justnvm)
{
	struct iwm_notification_wait calib_wait;
	static const uint16_t init_complete[] = {
		IWM_INIT_COMPLETE_NOTIF,
		IWM_CALIB_RES_NOTIF_PHY_DB
	};
	int ret;

	/* do not operate with rfkill switch turned on */
	if ((sc->sc_flags & IWM_FLAG_RFKILL) && !justnvm) {
		device_printf(sc->sc_dev,
		    "radio is disabled by hardware switch\n");
		return EPERM;
	}

	iwm_init_notification_wait(sc->sc_notif_wait,
				   &calib_wait,
				   init_complete,
				   nitems(init_complete),
				   iwm_wait_phy_db_entry,
				   sc->sc_phy_db);

	/* Will also start the device */
	ret = iwm_mvm_load_ucode_wait_alive(sc, IWM_UCODE_INIT);
	if (ret) {
		device_printf(sc->sc_dev, "Failed to start INIT ucode: %d\n",
		    ret);
		goto error;
	}

	if (justnvm) {
		/* Read nvm */
		ret = iwm_nvm_init(sc);
		if (ret) {
			device_printf(sc->sc_dev, "failed to read nvm\n");
			goto error;
		}
		IEEE80211_ADDR_COPY(sc->sc_ic.ic_macaddr, sc->nvm_data->hw_addr);
		goto error;
	}

	ret = iwm_send_bt_init_conf(sc);
	if (ret) {
		device_printf(sc->sc_dev,
		    "failed to send bt coex configuration: %d\n", ret);
		goto error;
	}

	/* Send TX valid antennas before triggering calibrations */
	ret = iwm_send_tx_ant_cfg(sc, iwm_mvm_get_valid_tx_ant(sc));
	if (ret) {
		device_printf(sc->sc_dev,
		    "failed to send antennas before calibration: %d\n", ret);
		goto error;
	}

	/*
	 * Send phy configurations command to init uCode
	 * to start the 16.0 uCode init image internal calibrations.
	 */
	ret = iwm_send_phy_cfg_cmd(sc);
	if (ret) {
		device_printf(sc->sc_dev,
		    "%s: Failed to run INIT calibrations: %d\n",
		    __func__, ret);
		goto error;
	}

	/*
	 * Nothing to do but wait for the init complete notification
	 * from the firmware.
	 */
	IWM_UNLOCK(sc);
	ret = iwm_wait_notification(sc->sc_notif_wait, &calib_wait,
	    IWM_MVM_UCODE_CALIB_TIMEOUT);
	IWM_LOCK(sc);


	goto out;

error:
	iwm_remove_notification(sc->sc_notif_wait, &calib_wait);
out:
	return ret;
}

static int
iwm_mvm_config_ltr(struct iwm_softc *sc)
{
	struct iwm_ltr_config_cmd cmd = {
		.flags = htole32(IWM_LTR_CFG_FLAG_FEATURE_ENABLE),
	};

	if (!sc->sc_ltr_enabled)
		return 0;

	return iwm_mvm_send_cmd_pdu(sc, IWM_LTR_CONFIG, 0, sizeof(cmd), &cmd);
}

/*
 * receive side
 */

/* (re)stock rx ring, called at init-time and at runtime */
static int
iwm_rx_addbuf(struct iwm_softc *sc, int size, int idx)
{
	struct iwm_rx_ring *ring = &sc->rxq;
	struct iwm_rx_data *data = &ring->data[idx];
	struct mbuf *m;
	bus_dmamap_t dmamap;
	bus_dma_segment_t seg;
	int nsegs, error;

	m = m_getjcl(M_NOWAIT, MT_DATA, M_PKTHDR, IWM_RBUF_SIZE);
	if (m == NULL)
		return ENOBUFS;

	m->m_len = m->m_pkthdr.len = m->m_ext.ext_size;
	error = bus_dmamap_load_mbuf_sg(ring->data_dmat, ring->spare_map, m,
	    &seg, &nsegs, BUS_DMA_NOWAIT);
	if (error != 0) {
		device_printf(sc->sc_dev,
		    "%s: can't map mbuf, error %d\n", __func__, error);
		m_freem(m);
		return error;
	}

	if (data->m != NULL)
		bus_dmamap_unload(ring->data_dmat, data->map);

	/* Swap ring->spare_map with data->map */
	dmamap = data->map;
	data->map = ring->spare_map;
	ring->spare_map = dmamap;

	bus_dmamap_sync(ring->data_dmat, data->map, BUS_DMASYNC_PREREAD);
	data->m = m;

	/* Update RX descriptor. */
	KASSERT((seg.ds_addr & 255) == 0, ("seg.ds_addr not aligned"));
	ring->desc[idx] = htole32(seg.ds_addr >> 8);
	bus_dmamap_sync(ring->desc_dma.tag, ring->desc_dma.map,
	    BUS_DMASYNC_PREWRITE);

	return 0;
}

/* iwlwifi: mvm/rx.c */
/*
 * iwm_mvm_get_signal_strength - use new rx PHY INFO API
 * values are reported by the fw as positive values - need to negate
 * to obtain their dBM.  Account for missing antennas by replacing 0
 * values by -256dBm: practically 0 power and a non-feasible 8 bit value.
 */
static int
iwm_mvm_get_signal_strength(struct iwm_softc *sc, struct iwm_rx_phy_info *phy_info)
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

	IWM_DPRINTF(sc, IWM_DEBUG_RECV,
	    "energy In A %d B %d C %d , and max %d\n",
	    energy_a, energy_b, energy_c, max_energy);

	return max_energy;
}

static void
iwm_mvm_rx_rx_phy_cmd(struct iwm_softc *sc, struct iwm_rx_packet *pkt)
{
	struct iwm_rx_phy_info *phy_info = (void *)pkt->data;

	IWM_DPRINTF(sc, IWM_DEBUG_RECV, "received PHY stats\n");

	memcpy(&sc->sc_last_phy_info, phy_info, sizeof(sc->sc_last_phy_info));
}

/*
 * Retrieve the average noise (in dBm) among receivers.
 */
static int
iwm_get_noise(struct iwm_softc *sc,
    const struct iwm_mvm_statistics_rx_non_phy *stats)
{
	int i, total, nbant, noise;

	total = nbant = noise = 0;
	for (i = 0; i < 3; i++) {
		noise = le32toh(stats->beacon_silence_rssi[i]) & 0xff;
		IWM_DPRINTF(sc, IWM_DEBUG_RECV, "%s: i=%d, noise=%d\n",
		    __func__,
		    i,
		    noise);

		if (noise) {
			total += noise;
			nbant++;
		}
	}

	IWM_DPRINTF(sc, IWM_DEBUG_RECV, "%s: nbant=%d, total=%d\n",
	    __func__, nbant, total);
#if 0
	/* There should be at least one antenna but check anyway. */
	return (nbant == 0) ? -127 : (total / nbant) - 107;
#else
	/* For now, just hard-code it to -96 to be safe */
	return (-96);
#endif
}

static void
iwm_mvm_handle_rx_statistics(struct iwm_softc *sc, struct iwm_rx_packet *pkt)
{
	struct iwm_notif_statistics_v10 *stats = (void *)&pkt->data;

	memcpy(&sc->sc_stats, stats, sizeof(sc->sc_stats));
	sc->sc_noise = iwm_get_noise(sc, &stats->rx.general);
}

/*
 * iwm_mvm_rx_rx_mpdu - IWM_REPLY_RX_MPDU_CMD handler
 *
 * Handles the actual data of the Rx packet from the fw
 */
static boolean_t
iwm_mvm_rx_rx_mpdu(struct iwm_softc *sc, struct mbuf *m, uint32_t offset,
	boolean_t stolen)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ieee80211vap *vap = TAILQ_FIRST(&ic->ic_vaps);
	struct ieee80211_frame *wh;
	struct ieee80211_node *ni;
	struct ieee80211_rx_stats rxs;
	struct iwm_rx_phy_info *phy_info;
	struct iwm_rx_mpdu_res_start *rx_res;
	struct iwm_rx_packet *pkt = mtodoff(m, struct iwm_rx_packet *, offset);
	uint32_t len;
	uint32_t rx_pkt_status;
	int rssi;

	phy_info = &sc->sc_last_phy_info;
	rx_res = (struct iwm_rx_mpdu_res_start *)pkt->data;
	wh = (struct ieee80211_frame *)(pkt->data + sizeof(*rx_res));
	len = le16toh(rx_res->byte_count);
	rx_pkt_status = le32toh(*(uint32_t *)(pkt->data + sizeof(*rx_res) + len));

	if (__predict_false(phy_info->cfg_phy_cnt > 20)) {
		device_printf(sc->sc_dev,
		    "dsp size out of range [0,20]: %d\n",
		    phy_info->cfg_phy_cnt);
		goto fail;
	}

	if (!(rx_pkt_status & IWM_RX_MPDU_RES_STATUS_CRC_OK) ||
	    !(rx_pkt_status & IWM_RX_MPDU_RES_STATUS_OVERRUN_OK)) {
		IWM_DPRINTF(sc, IWM_DEBUG_RECV,
		    "Bad CRC or FIFO: 0x%08X.\n", rx_pkt_status);
		goto fail;
	}

	rssi = iwm_mvm_get_signal_strength(sc, phy_info);

	/* Map it to relative value */
	rssi = rssi - sc->sc_noise;

	/* replenish ring for the buffer we're going to feed to the sharks */
	if (!stolen && iwm_rx_addbuf(sc, IWM_RBUF_SIZE, sc->rxq.cur) != 0) {
		device_printf(sc->sc_dev, "%s: unable to add more buffers\n",
		    __func__);
		goto fail;
	}

	m->m_data = pkt->data + sizeof(*rx_res);
	m->m_pkthdr.len = m->m_len = len;

	IWM_DPRINTF(sc, IWM_DEBUG_RECV,
	    "%s: rssi=%d, noise=%d\n", __func__, rssi, sc->sc_noise);

	ni = ieee80211_find_rxnode(ic, (struct ieee80211_frame_min *)wh);

	IWM_DPRINTF(sc, IWM_DEBUG_RECV,
	    "%s: phy_info: channel=%d, flags=0x%08x\n",
	    __func__,
	    le16toh(phy_info->channel),
	    le16toh(phy_info->phy_flags));

	/*
	 * Populate an RX state struct with the provided information.
	 */
	bzero(&rxs, sizeof(rxs));
	rxs.r_flags |= IEEE80211_R_IEEE | IEEE80211_R_FREQ;
	rxs.r_flags |= IEEE80211_R_NF | IEEE80211_R_RSSI;
	rxs.c_ieee = le16toh(phy_info->channel);
	if (le16toh(phy_info->phy_flags & IWM_RX_RES_PHY_FLAGS_BAND_24)) {
		rxs.c_freq = ieee80211_ieee2mhz(rxs.c_ieee, IEEE80211_CHAN_2GHZ);
	} else {
		rxs.c_freq = ieee80211_ieee2mhz(rxs.c_ieee, IEEE80211_CHAN_5GHZ);
	}

	/* rssi is in 1/2db units */
	rxs.c_rssi = rssi * 2;
	rxs.c_nf = sc->sc_noise;
	if (ieee80211_add_rx_params(m, &rxs) == 0) {
		if (ni)
			ieee80211_free_node(ni);
		goto fail;
	}

	if (ieee80211_radiotap_active_vap(vap)) {
		struct iwm_rx_radiotap_header *tap = &sc->sc_rxtap;

		tap->wr_flags = 0;
		if (phy_info->phy_flags & htole16(IWM_PHY_INFO_FLAG_SHPREAMBLE))
			tap->wr_flags |= IEEE80211_RADIOTAP_F_SHORTPRE;
		tap->wr_chan_freq = htole16(rxs.c_freq);
		/* XXX only if ic->ic_curchan->ic_ieee == rxs.c_ieee */
		tap->wr_chan_flags = htole16(ic->ic_curchan->ic_flags);
		tap->wr_dbm_antsignal = (int8_t)rssi;
		tap->wr_dbm_antnoise = (int8_t)sc->sc_noise;
		tap->wr_tsft = phy_info->system_timestamp;
		switch (phy_info->rate) {
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

	IWM_UNLOCK(sc);
	if (ni != NULL) {
		IWM_DPRINTF(sc, IWM_DEBUG_RECV, "input m %p\n", m);
		ieee80211_input_mimo(ni, m);
		ieee80211_free_node(ni);
	} else {
		IWM_DPRINTF(sc, IWM_DEBUG_RECV, "inputall m %p\n", m);
		ieee80211_input_mimo_all(ic, m);
	}
	IWM_LOCK(sc);

	return TRUE;

fail:
	counter_u64_add(ic->ic_ierrors, 1);
	return FALSE;
}

static int
iwm_mvm_rx_tx_cmd_single(struct iwm_softc *sc, struct iwm_rx_packet *pkt,
	struct iwm_node *in)
{
	struct iwm_mvm_tx_resp *tx_resp = (void *)pkt->data;
	struct ieee80211_ratectl_tx_status *txs = &sc->sc_txs;
	struct ieee80211_node *ni = &in->in_ni;
	struct ieee80211vap *vap = ni->ni_vap;
	int status = le16toh(tx_resp->status.status) & IWM_TX_STATUS_MSK;
	int new_rate, cur_rate = vap->iv_bss->ni_txrate;
	boolean_t rate_matched;
	uint8_t tx_resp_rate;

	KASSERT(tx_resp->frame_count == 1, ("too many frames"));

	/* Update rate control statistics. */
	IWM_DPRINTF(sc, IWM_DEBUG_XMIT, "%s: status=0x%04x, seq=%d, fc=%d, btc=%d, frts=%d, ff=%d, irate=%08x, wmt=%d\n",
	    __func__,
	    (int) le16toh(tx_resp->status.status),
	    (int) le16toh(tx_resp->status.sequence),
	    tx_resp->frame_count,
	    tx_resp->bt_kill_count,
	    tx_resp->failure_rts,
	    tx_resp->failure_frame,
	    le32toh(tx_resp->initial_rate),
	    (int) le16toh(tx_resp->wireless_media_time));

	tx_resp_rate = iwm_rate_from_ucode_rate(le32toh(tx_resp->initial_rate));

	/* For rate control, ignore frames sent at different initial rate */
	rate_matched = (tx_resp_rate != 0 && tx_resp_rate == cur_rate);

	if (tx_resp_rate != 0 && cur_rate != 0 && !rate_matched) {
		IWM_DPRINTF(sc, IWM_DEBUG_TXRATE,
		    "tx_resp_rate doesn't match ni_txrate (tx_resp_rate=%u "
		    "ni_txrate=%d)\n", tx_resp_rate, cur_rate);
	}

	txs->flags = IEEE80211_RATECTL_STATUS_SHORT_RETRY |
		     IEEE80211_RATECTL_STATUS_LONG_RETRY;
	txs->short_retries = tx_resp->failure_rts;
	txs->long_retries = tx_resp->failure_frame;
	if (status != IWM_TX_STATUS_SUCCESS &&
	    status != IWM_TX_STATUS_DIRECT_DONE) {
		switch (status) {
		case IWM_TX_STATUS_FAIL_SHORT_LIMIT:
			txs->status = IEEE80211_RATECTL_TX_FAIL_SHORT;
			break;
		case IWM_TX_STATUS_FAIL_LONG_LIMIT:
			txs->status = IEEE80211_RATECTL_TX_FAIL_LONG;
			break;
		case IWM_TX_STATUS_FAIL_LIFE_EXPIRE:
			txs->status = IEEE80211_RATECTL_TX_FAIL_EXPIRED;
			break;
		default:
			txs->status = IEEE80211_RATECTL_TX_FAIL_UNSPECIFIED;
			break;
		}
	} else {
		txs->status = IEEE80211_RATECTL_TX_SUCCESS;
	}

	if (rate_matched) {
		ieee80211_ratectl_tx_complete(ni, txs);

		int rix = ieee80211_ratectl_rate(vap->iv_bss, NULL, 0);
		new_rate = vap->iv_bss->ni_txrate;
		if (new_rate != 0 && new_rate != cur_rate) {
			struct iwm_node *in = IWM_NODE(vap->iv_bss);
			iwm_setrates(sc, in, rix);
			iwm_mvm_send_lq_cmd(sc, &in->in_lq, FALSE);
		}
 	}

	return (txs->status != IEEE80211_RATECTL_TX_SUCCESS);
}

static void
iwm_mvm_rx_tx_cmd(struct iwm_softc *sc, struct iwm_rx_packet *pkt)
{
	struct iwm_cmd_header *cmd_hdr = &pkt->hdr;
	int idx = cmd_hdr->idx;
	int qid = cmd_hdr->qid;
	struct iwm_tx_ring *ring = &sc->txq[qid];
	struct iwm_tx_data *txd = &ring->data[idx];
	struct iwm_node *in = txd->in;
	struct mbuf *m = txd->m;
	int status;

	KASSERT(txd->done == 0, ("txd not done"));
	KASSERT(txd->in != NULL, ("txd without node"));
	KASSERT(txd->m != NULL, ("txd without mbuf"));

	sc->sc_tx_timer = 0;

	status = iwm_mvm_rx_tx_cmd_single(sc, pkt, in);

	/* Unmap and free mbuf. */
	bus_dmamap_sync(ring->data_dmat, txd->map, BUS_DMASYNC_POSTWRITE);
	bus_dmamap_unload(ring->data_dmat, txd->map);

	IWM_DPRINTF(sc, IWM_DEBUG_XMIT,
	    "free txd %p, in %p\n", txd, txd->in);
	txd->done = 1;
	txd->m = NULL;
	txd->in = NULL;

	ieee80211_tx_complete(&in->in_ni, m, status);

	if (--ring->queued < IWM_TX_RING_LOMARK) {
		sc->qfullmsk &= ~(1 << ring->qid);
		if (sc->qfullmsk == 0) {
			iwm_start(sc);
		}
	}
}

/*
 * transmit side
 */

/*
 * Process a "command done" firmware notification.  This is where we wakeup
 * processes waiting for a synchronous command completion.
 * from if_iwn
 */
static void
iwm_cmd_done(struct iwm_softc *sc, struct iwm_rx_packet *pkt)
{
	struct iwm_tx_ring *ring = &sc->txq[IWM_MVM_CMD_QUEUE];
	struct iwm_tx_data *data;

	if (pkt->hdr.qid != IWM_MVM_CMD_QUEUE) {
		return;	/* Not a command ack. */
	}

	/* XXX wide commands? */
	IWM_DPRINTF(sc, IWM_DEBUG_CMD,
	    "cmd notification type 0x%x qid %d idx %d\n",
	    pkt->hdr.code, pkt->hdr.qid, pkt->hdr.idx);

	data = &ring->data[pkt->hdr.idx];

	/* If the command was mapped in an mbuf, free it. */
	if (data->m != NULL) {
		bus_dmamap_sync(ring->data_dmat, data->map,
		    BUS_DMASYNC_POSTWRITE);
		bus_dmamap_unload(ring->data_dmat, data->map);
		m_freem(data->m);
		data->m = NULL;
	}
	wakeup(&ring->desc[pkt->hdr.idx]);

	if (((pkt->hdr.idx + ring->queued) % IWM_TX_RING_COUNT) != ring->cur) {
		device_printf(sc->sc_dev,
		    "%s: Some HCMDs skipped?: idx=%d queued=%d cur=%d\n",
		    __func__, pkt->hdr.idx, ring->queued, ring->cur);
		/* XXX call iwm_force_nmi() */
	}

	KASSERT(ring->queued > 0, ("ring->queued is empty?"));
	ring->queued--;
	if (ring->queued == 0)
		iwm_pcie_clear_cmd_in_flight(sc);
}

#if 0
/*
 * necessary only for block ack mode
 */
void
iwm_update_sched(struct iwm_softc *sc, int qid, int idx, uint8_t sta_id,
	uint16_t len)
{
	struct iwm_agn_scd_bc_tbl *scd_bc_tbl;
	uint16_t w_val;

	scd_bc_tbl = sc->sched_dma.vaddr;

	len += 8; /* magic numbers came naturally from paris */
	len = roundup(len, 4) / 4;

	w_val = htole16(sta_id << 12 | len);

	/* Update TX scheduler. */
	scd_bc_tbl[qid].tfd_offset[idx] = w_val;
	bus_dmamap_sync(sc->sched_dma.tag, sc->sched_dma.map,
	    BUS_DMASYNC_PREWRITE);

	/* I really wonder what this is ?!? */
	if (idx < IWM_TFD_QUEUE_SIZE_BC_DUP) {
		scd_bc_tbl[qid].tfd_offset[IWM_TFD_QUEUE_SIZE_MAX + idx] = w_val;
		bus_dmamap_sync(sc->sched_dma.tag, sc->sched_dma.map,
		    BUS_DMASYNC_PREWRITE);
	}
}
#endif

static int
iwm_tx_rateidx_global_lookup(struct iwm_softc *sc, uint8_t rate)
{
	int i;

	for (i = 0; i < nitems(iwm_rates); i++) {
		if (iwm_rates[i].rate == rate)
			return (i);
	}
	/* XXX error? */
	IWM_DPRINTF(sc, IWM_DEBUG_XMIT | IWM_DEBUG_TXRATE,
	    "%s: couldn't find an entry for rate=%d\n",
	    __func__,
	    rate);
	return (0);
}

/*
 * Fill in the rate related information for a transmit command.
 */
static const struct iwm_rate *
iwm_tx_fill_cmd(struct iwm_softc *sc, struct iwm_node *in,
	struct mbuf *m, struct iwm_tx_cmd *tx)
{
	struct ieee80211_node *ni = &in->in_ni;
	struct ieee80211_frame *wh;
	const struct ieee80211_txparam *tp = ni->ni_txparms;
	const struct iwm_rate *rinfo;
	int type;
	int ridx, rate_flags;

	wh = mtod(m, struct ieee80211_frame *);
	type = wh->i_fc[0] & IEEE80211_FC0_TYPE_MASK;

	tx->rts_retry_limit = IWM_RTS_DFAULT_RETRY_LIMIT;
	tx->data_retry_limit = IWM_DEFAULT_TX_RETRY;

	if (type == IEEE80211_FC0_TYPE_MGT ||
	    type == IEEE80211_FC0_TYPE_CTL ||
	    (m->m_flags & M_EAPOL) != 0) {
		ridx = iwm_tx_rateidx_global_lookup(sc, tp->mgmtrate);
		IWM_DPRINTF(sc, IWM_DEBUG_TXRATE,
		    "%s: MGT (%d)\n", __func__, tp->mgmtrate);
	} else if (IEEE80211_IS_MULTICAST(wh->i_addr1)) {
		ridx = iwm_tx_rateidx_global_lookup(sc, tp->mcastrate);
		IWM_DPRINTF(sc, IWM_DEBUG_TXRATE,
		    "%s: MCAST (%d)\n", __func__, tp->mcastrate);
	} else if (tp->ucastrate != IEEE80211_FIXED_RATE_NONE) {
		ridx = iwm_tx_rateidx_global_lookup(sc, tp->ucastrate);
		IWM_DPRINTF(sc, IWM_DEBUG_TXRATE,
		    "%s: FIXED_RATE (%d)\n", __func__, tp->ucastrate);
	} else {
		/* for data frames, use RS table */
		IWM_DPRINTF(sc, IWM_DEBUG_TXRATE, "%s: DATA\n", __func__);
		ridx = iwm_rate2ridx(sc, ni->ni_txrate);
		if (ridx == -1)
			ridx = 0;

		/* This is the index into the programmed table */
		tx->initial_rate_index = 0;
		tx->tx_flags |= htole32(IWM_TX_CMD_FLG_STA_RATE);
	}

	IWM_DPRINTF(sc, IWM_DEBUG_XMIT | IWM_DEBUG_TXRATE,
	    "%s: frame type=%d txrate %d\n",
	        __func__, type, iwm_rates[ridx].rate);

	rinfo = &iwm_rates[ridx];

	IWM_DPRINTF(sc, IWM_DEBUG_TXRATE, "%s: ridx=%d; rate=%d, CCK=%d\n",
	    __func__, ridx,
	    rinfo->rate,
	    !! (IWM_RIDX_IS_CCK(ridx))
	    );

	/* XXX TODO: hard-coded TX antenna? */
	rate_flags = 1 << IWM_RATE_MCS_ANT_POS;
	if (IWM_RIDX_IS_CCK(ridx))
		rate_flags |= IWM_RATE_MCS_CCK_MSK;
	tx->rate_n_flags = htole32(rate_flags | rinfo->plcp);

	return rinfo;
}

#define TB0_SIZE 16
static int
iwm_tx(struct iwm_softc *sc, struct mbuf *m, struct ieee80211_node *ni, int ac)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ieee80211vap *vap = TAILQ_FIRST(&ic->ic_vaps);
	struct iwm_node *in = IWM_NODE(ni);
	struct iwm_tx_ring *ring;
	struct iwm_tx_data *data;
	struct iwm_tfd *desc;
	struct iwm_device_cmd *cmd;
	struct iwm_tx_cmd *tx;
	struct ieee80211_frame *wh;
	struct ieee80211_key *k = NULL;
	struct mbuf *m1;
	const struct iwm_rate *rinfo;
	uint32_t flags;
	u_int hdrlen;
	bus_dma_segment_t *seg, segs[IWM_MAX_SCATTER];
	int nsegs;
	uint8_t tid, type;
	int i, totlen, error, pad;

	wh = mtod(m, struct ieee80211_frame *);
	hdrlen = ieee80211_anyhdrsize(wh);
	type = wh->i_fc[0] & IEEE80211_FC0_TYPE_MASK;
	tid = 0;
	ring = &sc->txq[ac];
	desc = &ring->desc[ring->cur];
	memset(desc, 0, sizeof(*desc));
	data = &ring->data[ring->cur];

	/* Fill out iwm_tx_cmd to send to the firmware */
	cmd = &ring->cmd[ring->cur];
	cmd->hdr.code = IWM_TX_CMD;
	cmd->hdr.flags = 0;
	cmd->hdr.qid = ring->qid;
	cmd->hdr.idx = ring->cur;

	tx = (void *)cmd->data;
	memset(tx, 0, sizeof(*tx));

	rinfo = iwm_tx_fill_cmd(sc, in, m, tx);

	/* Encrypt the frame if need be. */
	if (wh->i_fc[1] & IEEE80211_FC1_PROTECTED) {
		/* Retrieve key for TX && do software encryption. */
		k = ieee80211_crypto_encap(ni, m);
		if (k == NULL) {
			m_freem(m);
			return (ENOBUFS);
		}
		/* 802.11 header may have moved. */
		wh = mtod(m, struct ieee80211_frame *);
	}

	if (ieee80211_radiotap_active_vap(vap)) {
		struct iwm_tx_radiotap_header *tap = &sc->sc_txtap;

		tap->wt_flags = 0;
		tap->wt_chan_freq = htole16(ni->ni_chan->ic_freq);
		tap->wt_chan_flags = htole16(ni->ni_chan->ic_flags);
		tap->wt_rate = rinfo->rate;
		if (k != NULL)
			tap->wt_flags |= IEEE80211_RADIOTAP_F_WEP;
		ieee80211_radiotap_tx(vap, m);
	}


	totlen = m->m_pkthdr.len;

	flags = 0;
	if (!IEEE80211_IS_MULTICAST(wh->i_addr1)) {
		flags |= IWM_TX_CMD_FLG_ACK;
	}

	if (type == IEEE80211_FC0_TYPE_DATA
	    && (totlen + IEEE80211_CRC_LEN > vap->iv_rtsthreshold)
	    && !IEEE80211_IS_MULTICAST(wh->i_addr1)) {
		flags |= IWM_TX_CMD_FLG_PROT_REQUIRE;
	}

	if (IEEE80211_IS_MULTICAST(wh->i_addr1) ||
	    type != IEEE80211_FC0_TYPE_DATA)
		tx->sta_id = sc->sc_aux_sta.sta_id;
	else
		tx->sta_id = IWM_STATION_ID;

	if (type == IEEE80211_FC0_TYPE_MGT) {
		uint8_t subtype = wh->i_fc[0] & IEEE80211_FC0_SUBTYPE_MASK;

		if (subtype == IEEE80211_FC0_SUBTYPE_ASSOC_REQ ||
		    subtype == IEEE80211_FC0_SUBTYPE_REASSOC_REQ) {
			tx->pm_frame_timeout = htole16(IWM_PM_FRAME_ASSOC);
		} else if (subtype == IEEE80211_FC0_SUBTYPE_ACTION) {
			tx->pm_frame_timeout = htole16(IWM_PM_FRAME_NONE);
		} else {
			tx->pm_frame_timeout = htole16(IWM_PM_FRAME_MGMT);
		}
	} else {
		tx->pm_frame_timeout = htole16(IWM_PM_FRAME_NONE);
	}

	if (hdrlen & 3) {
		/* First segment length must be a multiple of 4. */
		flags |= IWM_TX_CMD_FLG_MH_PAD;
		pad = 4 - (hdrlen & 3);
	} else
		pad = 0;

	tx->driver_txop = 0;
	tx->next_frame_len = 0;

	tx->len = htole16(totlen);
	tx->tid_tspec = tid;
	tx->life_time = htole32(IWM_TX_CMD_LIFE_TIME_INFINITE);

	/* Set physical address of "scratch area". */
	tx->dram_lsb_ptr = htole32(data->scratch_paddr);
	tx->dram_msb_ptr = iwm_get_dma_hi_addr(data->scratch_paddr);

	/* Copy 802.11 header in TX command. */
	memcpy(((uint8_t *)tx) + sizeof(*tx), wh, hdrlen);

	flags |= IWM_TX_CMD_FLG_BT_DIS | IWM_TX_CMD_FLG_SEQ_CTL;

	tx->sec_ctl = 0;
	tx->tx_flags |= htole32(flags);

	/* Trim 802.11 header. */
	m_adj(m, hdrlen);
	error = bus_dmamap_load_mbuf_sg(ring->data_dmat, data->map, m,
	    segs, &nsegs, BUS_DMA_NOWAIT);
	if (error != 0) {
		if (error != EFBIG) {
			device_printf(sc->sc_dev, "can't map mbuf (error %d)\n",
			    error);
			m_freem(m);
			return error;
		}
		/* Too many DMA segments, linearize mbuf. */
		m1 = m_collapse(m, M_NOWAIT, IWM_MAX_SCATTER - 2);
		if (m1 == NULL) {
			device_printf(sc->sc_dev,
			    "%s: could not defrag mbuf\n", __func__);
			m_freem(m);
			return (ENOBUFS);
		}
		m = m1;

		error = bus_dmamap_load_mbuf_sg(ring->data_dmat, data->map, m,
		    segs, &nsegs, BUS_DMA_NOWAIT);
		if (error != 0) {
			device_printf(sc->sc_dev, "can't map mbuf (error %d)\n",
			    error);
			m_freem(m);
			return error;
		}
	}
	data->m = m;
	data->in = in;
	data->done = 0;

	IWM_DPRINTF(sc, IWM_DEBUG_XMIT,
	    "sending txd %p, in %p\n", data, data->in);
	KASSERT(data->in != NULL, ("node is NULL"));

	IWM_DPRINTF(sc, IWM_DEBUG_XMIT,
	    "sending data: qid=%d idx=%d len=%d nsegs=%d txflags=0x%08x rate_n_flags=0x%08x rateidx=%u\n",
	    ring->qid, ring->cur, totlen, nsegs,
	    le32toh(tx->tx_flags),
	    le32toh(tx->rate_n_flags),
	    tx->initial_rate_index
	    );

	/* Fill TX descriptor. */
	desc->num_tbs = 2 + nsegs;

	desc->tbs[0].lo = htole32(data->cmd_paddr);
	desc->tbs[0].hi_n_len = htole16(iwm_get_dma_hi_addr(data->cmd_paddr)) |
	    (TB0_SIZE << 4);
	desc->tbs[1].lo = htole32(data->cmd_paddr + TB0_SIZE);
	desc->tbs[1].hi_n_len = htole16(iwm_get_dma_hi_addr(data->cmd_paddr)) |
	    ((sizeof(struct iwm_cmd_header) + sizeof(*tx)
	      + hdrlen + pad - TB0_SIZE) << 4);

	/* Other DMA segments are for data payload. */
	for (i = 0; i < nsegs; i++) {
		seg = &segs[i];
		desc->tbs[i+2].lo = htole32(seg->ds_addr);
		desc->tbs[i+2].hi_n_len = \
		    htole16(iwm_get_dma_hi_addr(seg->ds_addr))
		    | ((seg->ds_len) << 4);
	}

	bus_dmamap_sync(ring->data_dmat, data->map,
	    BUS_DMASYNC_PREWRITE);
	bus_dmamap_sync(ring->cmd_dma.tag, ring->cmd_dma.map,
	    BUS_DMASYNC_PREWRITE);
	bus_dmamap_sync(ring->desc_dma.tag, ring->desc_dma.map,
	    BUS_DMASYNC_PREWRITE);

#if 0
	iwm_update_sched(sc, ring->qid, ring->cur, tx->sta_id, le16toh(tx->len));
#endif

	/* Kick TX ring. */
	ring->cur = (ring->cur + 1) % IWM_TX_RING_COUNT;
	IWM_WRITE(sc, IWM_HBUS_TARG_WRPTR, ring->qid << 8 | ring->cur);

	/* Mark TX ring as full if we reach a certain threshold. */
	if (++ring->queued > IWM_TX_RING_HIMARK) {
		sc->qfullmsk |= 1 << ring->qid;
	}

	return 0;
}

static int
iwm_raw_xmit(struct ieee80211_node *ni, struct mbuf *m,
    const struct ieee80211_bpf_params *params)
{
	struct ieee80211com *ic = ni->ni_ic;
	struct iwm_softc *sc = ic->ic_softc;
	int error = 0;

	IWM_DPRINTF(sc, IWM_DEBUG_XMIT,
	    "->%s begin\n", __func__);

	if ((sc->sc_flags & IWM_FLAG_HW_INITED) == 0) {
		m_freem(m);
		IWM_DPRINTF(sc, IWM_DEBUG_XMIT,
		    "<-%s not RUNNING\n", __func__);
		return (ENETDOWN);
        }

	IWM_LOCK(sc);
	/* XXX fix this */
        if (params == NULL) {
		error = iwm_tx(sc, m, ni, 0);
	} else {
		error = iwm_tx(sc, m, ni, 0);
	}
	if (sc->sc_tx_timer == 0)
		callout_reset(&sc->sc_watchdog_to, hz, iwm_watchdog, sc);
	sc->sc_tx_timer = 5;
	IWM_UNLOCK(sc);

        return (error);
}

/*
 * mvm/tx.c
 */

/*
 * Note that there are transports that buffer frames before they reach
 * the firmware. This means that after flush_tx_path is called, the
 * queue might not be empty. The race-free way to handle this is to:
 * 1) set the station as draining
 * 2) flush the Tx path
 * 3) wait for the transport queues to be empty
 */
int
iwm_mvm_flush_tx_path(struct iwm_softc *sc, uint32_t tfd_msk, uint32_t flags)
{
	int ret;
	struct iwm_tx_path_flush_cmd flush_cmd = {
		.queues_ctl = htole32(tfd_msk),
		.flush_ctl = htole16(IWM_DUMP_TX_FIFO_FLUSH),
	};

	ret = iwm_mvm_send_cmd_pdu(sc, IWM_TXPATH_FLUSH, flags,
	    sizeof(flush_cmd), &flush_cmd);
	if (ret)
                device_printf(sc->sc_dev,
		    "Flushing tx queue failed: %d\n", ret);
	return ret;
}

/*
 * BEGIN mvm/quota.c
 */

static int
iwm_mvm_update_quotas(struct iwm_softc *sc, struct iwm_vap *ivp)
{
	struct iwm_time_quota_cmd cmd;
	int i, idx, ret, num_active_macs, quota, quota_rem;
	int colors[IWM_MAX_BINDINGS] = { -1, -1, -1, -1, };
	int n_ifs[IWM_MAX_BINDINGS] = {0, };
	uint16_t id;

	memset(&cmd, 0, sizeof(cmd));

	/* currently, PHY ID == binding ID */
	if (ivp) {
		id = ivp->phy_ctxt->id;
		KASSERT(id < IWM_MAX_BINDINGS, ("invalid id"));
		colors[id] = ivp->phy_ctxt->color;

		if (1)
			n_ifs[id] = 1;
	}

	/*
	 * The FW's scheduling session consists of
	 * IWM_MVM_MAX_QUOTA fragments. Divide these fragments
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
		quota = IWM_MVM_MAX_QUOTA / num_active_macs;
		quota_rem = IWM_MVM_MAX_QUOTA % num_active_macs;
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

	ret = iwm_mvm_send_cmd_pdu(sc, IWM_TIME_QUOTA_CMD, IWM_CMD_SYNC,
	    sizeof(cmd), &cmd);
	if (ret)
		device_printf(sc->sc_dev,
		    "%s: Failed to send quota: %d\n", __func__, ret);
	return ret;
}

/*
 * END mvm/quota.c
 */

/*
 * ieee80211 routines
 */

/*
 * Change to AUTH state in 80211 state machine.  Roughly matches what
 * Linux does in bss_info_changed().
 */
static int
iwm_auth(struct ieee80211vap *vap, struct iwm_softc *sc)
{
	struct ieee80211_node *ni;
	struct iwm_node *in;
	struct iwm_vap *iv = IWM_VAP(vap);
	uint32_t duration;
	int error;

	/*
	 * XXX i have a feeling that the vap node is being
	 * freed from underneath us. Grr.
	 */
	ni = ieee80211_ref_node(vap->iv_bss);
	in = IWM_NODE(ni);
	IWM_DPRINTF(sc, IWM_DEBUG_RESET | IWM_DEBUG_STATE,
	    "%s: called; vap=%p, bss ni=%p\n",
	    __func__,
	    vap,
	    ni);
	IWM_DPRINTF(sc, IWM_DEBUG_STATE, "%s: Current node bssid: %s\n",
	    __func__, ether_sprintf(ni->ni_bssid));

	in->in_assoc = 0;
	iv->iv_auth = 1;

	/*
	 * Firmware bug - it'll crash if the beacon interval is less
	 * than 16. We can't avoid connecting at all, so refuse the
	 * station state change, this will cause net80211 to abandon
	 * attempts to connect to this AP, and eventually wpa_s will
	 * blacklist the AP...
	 */
	if (ni->ni_intval < 16) {
		device_printf(sc->sc_dev,
		    "AP %s beacon interval is %d, refusing due to firmware bug!\n",
		    ether_sprintf(ni->ni_bssid), ni->ni_intval);
		error = EINVAL;
		goto out;
	}

	error = iwm_allow_mcast(vap, sc);
	if (error) {
		device_printf(sc->sc_dev,
		    "%s: failed to set multicast\n", __func__);
		goto out;
	}

	/*
	 * This is where it deviates from what Linux does.
	 *
	 * Linux iwlwifi doesn't reset the nic each time, nor does it
	 * call ctxt_add() here.  Instead, it adds it during vap creation,
	 * and always does a mac_ctx_changed().
	 *
	 * The openbsd port doesn't attempt to do that - it reset things
	 * at odd states and does the add here.
	 *
	 * So, until the state handling is fixed (ie, we never reset
	 * the NIC except for a firmware failure, which should drag
	 * the NIC back to IDLE, re-setup and re-add all the mac/phy
	 * contexts that are required), let's do a dirty hack here.
	 */
	if (iv->is_uploaded) {
		if ((error = iwm_mvm_mac_ctxt_changed(sc, vap)) != 0) {
			device_printf(sc->sc_dev,
			    "%s: failed to update MAC\n", __func__);
			goto out;
		}
	} else {
		if ((error = iwm_mvm_mac_ctxt_add(sc, vap)) != 0) {
			device_printf(sc->sc_dev,
			    "%s: failed to add MAC\n", __func__);
			goto out;
		}
	}
	sc->sc_firmware_state = 1;

	if ((error = iwm_mvm_phy_ctxt_changed(sc, &sc->sc_phyctxt[0],
	    in->in_ni.ni_chan, 1, 1)) != 0) {
		device_printf(sc->sc_dev,
		    "%s: failed update phy ctxt\n", __func__);
		goto out;
	}
	iv->phy_ctxt = &sc->sc_phyctxt[0];

	if ((error = iwm_mvm_binding_add_vif(sc, iv)) != 0) {
		device_printf(sc->sc_dev,
		    "%s: binding update cmd\n", __func__);
		goto out;
	}
	sc->sc_firmware_state = 2;
	/*
	 * Authentication becomes unreliable when powersaving is left enabled
	 * here. Powersaving will be activated again when association has
	 * finished or is aborted.
	 */
	iv->ps_disabled = TRUE;
	error = iwm_mvm_power_update_mac(sc);
	iv->ps_disabled = FALSE;
	if (error != 0) {
		device_printf(sc->sc_dev,
		    "%s: failed to update power management\n",
		    __func__);
		goto out;
	}
	if ((error = iwm_mvm_add_sta(sc, in)) != 0) {
		device_printf(sc->sc_dev,
		    "%s: failed to add sta\n", __func__);
		goto out;
	}
	sc->sc_firmware_state = 3;

	/*
	 * Prevent the FW from wandering off channel during association
	 * by "protecting" the session with a time event.
	 */
	/* XXX duration is in units of TU, not MS */
	duration = IWM_MVM_TE_SESSION_PROTECTION_MAX_TIME_MS;
	iwm_mvm_protect_session(sc, iv, duration, 500 /* XXX magic number */, TRUE);

	error = 0;
out:
	if (error != 0)
		iv->iv_auth = 0;
	ieee80211_free_node(ni);
	return (error);
}

static struct ieee80211_node *
iwm_node_alloc(struct ieee80211vap *vap, const uint8_t mac[IEEE80211_ADDR_LEN])
{
	return malloc(sizeof (struct iwm_node), M_80211_NODE,
	    M_NOWAIT | M_ZERO);
}

static uint8_t
iwm_rate_from_ucode_rate(uint32_t rate_n_flags)
{
	uint8_t plcp = rate_n_flags & 0xff;
	int i;

	for (i = 0; i <= IWM_RIDX_MAX; i++) {
		if (iwm_rates[i].plcp == plcp)
			return iwm_rates[i].rate;
	}
	return 0;
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

static int
iwm_rate2ridx(struct iwm_softc *sc, uint8_t rate)
{
	int i;

	for (i = 0; i <= IWM_RIDX_MAX; i++) {
		if (iwm_rates[i].rate == rate)
			return i;
	}

	device_printf(sc->sc_dev,
	    "%s: WARNING: device rate for %u not found!\n",
	    __func__, rate);

	return -1;
}


static void
iwm_setrates(struct iwm_softc *sc, struct iwm_node *in, int rix)
{
	struct ieee80211_node *ni = &in->in_ni;
	struct iwm_lq_cmd *lq = &in->in_lq;
	struct ieee80211_rateset *rs = &ni->ni_rates;
	int nrates = rs->rs_nrates;
	int i, ridx, tab = 0;
//	int txant = 0;

	KASSERT(rix >= 0 && rix < nrates, ("invalid rix"));

	if (nrates > nitems(lq->rs_table)) {
		device_printf(sc->sc_dev,
		    "%s: node supports %d rates, driver handles "
		    "only %zu\n", __func__, nrates, nitems(lq->rs_table));
		return;
	}
	if (nrates == 0) {
		device_printf(sc->sc_dev,
		    "%s: node supports 0 rates, odd!\n", __func__);
		return;
	}
	nrates = imin(rix + 1, nrates);

	IWM_DPRINTF(sc, IWM_DEBUG_TXRATE,
	    "%s: nrates=%d\n", __func__, nrates);

	/* then construct a lq_cmd based on those */
	memset(lq, 0, sizeof(*lq));
	lq->sta_id = IWM_STATION_ID;

	/* For HT, always enable RTS/CTS to avoid excessive retries. */
	if (ni->ni_flags & IEEE80211_NODE_HT)
		lq->flags |= IWM_LQ_FLAG_USE_RTS_MSK;

	/*
	 * are these used? (we don't do SISO or MIMO)
	 * need to set them to non-zero, though, or we get an error.
	 */
	lq->single_stream_ant_msk = 1;
	lq->dual_stream_ant_msk = 1;

	/*
	 * Build the actual rate selection table.
	 * The lowest bits are the rates.  Additionally,
	 * CCK needs bit 9 to be set.  The rest of the bits
	 * we add to the table select the tx antenna
	 * Note that we add the rates in the highest rate first
	 * (opposite of ni_rates).
	 */
	for (i = 0; i < nrates; i++) {
		int rate = rs->rs_rates[rix - i] & IEEE80211_RATE_VAL;
		int nextant;

		/* Map 802.11 rate to HW rate index. */
		ridx = iwm_rate2ridx(sc, rate);
		if (ridx == -1)
			continue;

#if 0
		if (txant == 0)
			txant = iwm_mvm_get_valid_tx_ant(sc);
		nextant = 1<<(ffs(txant)-1);
		txant &= ~nextant;
#else
		nextant = iwm_mvm_get_valid_tx_ant(sc);
#endif
		tab = iwm_rates[ridx].plcp;
		tab |= nextant << IWM_RATE_MCS_ANT_POS;
		if (IWM_RIDX_IS_CCK(ridx))
			tab |= IWM_RATE_MCS_CCK_MSK;
		IWM_DPRINTF(sc, IWM_DEBUG_TXRATE,
		    "station rate i=%d, rate=%d, hw=%x\n",
		    i, iwm_rates[ridx].rate, tab);
		lq->rs_table[i] = htole32(tab);
	}
	/* then fill the rest with the lowest possible rate */
	for (i = nrates; i < nitems(lq->rs_table); i++) {
		KASSERT(tab != 0, ("invalid tab"));
		lq->rs_table[i] = htole32(tab);
	}
}

static int
iwm_media_change(struct ifnet *ifp)
{
	struct ieee80211vap *vap = ifp->if_softc;
	struct ieee80211com *ic = vap->iv_ic;
	struct iwm_softc *sc = ic->ic_softc;
	int error;

	error = ieee80211_media_change(ifp);
	if (error != ENETRESET)
		return error;

	IWM_LOCK(sc);
	if (ic->ic_nrunning > 0) {
		iwm_stop(sc);
		iwm_init(sc);
	}
	IWM_UNLOCK(sc);
	return error;
}

static void
iwm_bring_down_firmware(struct iwm_softc *sc, struct ieee80211vap *vap)
{
	struct iwm_vap *ivp = IWM_VAP(vap);
	int error;

	/* Avoid Tx watchdog triggering, when transfers get dropped here. */
	sc->sc_tx_timer = 0;

	ivp->iv_auth = 0;
	if (sc->sc_firmware_state == 3) {
		iwm_xmit_queue_drain(sc);
//		iwm_mvm_flush_tx_path(sc, 0xf, IWM_CMD_SYNC);
		error = iwm_mvm_rm_sta(sc, vap, TRUE);
		if (error) {
			device_printf(sc->sc_dev,
			    "%s: Failed to remove station: %d\n",
			    __func__, error);
		}
	}
	if (sc->sc_firmware_state == 3) {
		error = iwm_mvm_mac_ctxt_changed(sc, vap);
		if (error) {
			device_printf(sc->sc_dev,
			    "%s: Failed to change mac context: %d\n",
			    __func__, error);
		}
	}
	if (sc->sc_firmware_state == 3) {
		error = iwm_mvm_sf_update(sc, vap, FALSE);
		if (error) {
			device_printf(sc->sc_dev,
			    "%s: Failed to update smart FIFO: %d\n",
			    __func__, error);
		}
	}
	if (sc->sc_firmware_state == 3) {
		error = iwm_mvm_rm_sta_id(sc, vap);
		if (error) {
			device_printf(sc->sc_dev,
			    "%s: Failed to remove station id: %d\n",
			    __func__, error);
		}
	}
	if (sc->sc_firmware_state == 3) {
		error = iwm_mvm_update_quotas(sc, NULL);
		if (error) {
			device_printf(sc->sc_dev,
			    "%s: Failed to update PHY quota: %d\n",
			    __func__, error);
		}
	}
	if (sc->sc_firmware_state == 3) {
		/* XXX Might need to specify bssid correctly. */
		error = iwm_mvm_mac_ctxt_changed(sc, vap);
		if (error) {
			device_printf(sc->sc_dev,
			    "%s: Failed to change mac context: %d\n",
			    __func__, error);
		}
	}
	if (sc->sc_firmware_state == 3) {
		sc->sc_firmware_state = 2;
	}
	if (sc->sc_firmware_state > 1) {
		error = iwm_mvm_binding_remove_vif(sc, ivp);
		if (error) {
			device_printf(sc->sc_dev,
			    "%s: Failed to remove channel ctx: %d\n",
			    __func__, error);
		}
	}
	if (sc->sc_firmware_state > 1) {
		sc->sc_firmware_state = 1;
	}
	ivp->phy_ctxt = NULL;
	if (sc->sc_firmware_state > 0) {
		error = iwm_mvm_mac_ctxt_changed(sc, vap);
		if (error) {
			device_printf(sc->sc_dev,
			    "%s: Failed to change mac context: %d\n",
			    __func__, error);
		}
	}
	if (sc->sc_firmware_state > 0) {
		error = iwm_mvm_power_update_mac(sc);
		if (error != 0) {
			device_printf(sc->sc_dev,
			    "%s: failed to update power management\n",
			    __func__);
		}
	}
	sc->sc_firmware_state = 0;
}

static int
iwm_newstate(struct ieee80211vap *vap, enum ieee80211_state nstate, int arg)
{
	struct iwm_vap *ivp = IWM_VAP(vap);
	struct ieee80211com *ic = vap->iv_ic;
	struct iwm_softc *sc = ic->ic_softc;
	struct iwm_node *in;
	int error;

	IWM_DPRINTF(sc, IWM_DEBUG_STATE,
	    "switching state %s -> %s arg=0x%x\n",
	    ieee80211_state_name[vap->iv_state],
	    ieee80211_state_name[nstate],
	    arg);

	IEEE80211_UNLOCK(ic);
	IWM_LOCK(sc);

	if ((sc->sc_flags & IWM_FLAG_SCAN_RUNNING) &&
	    (nstate == IEEE80211_S_AUTH ||
	     nstate == IEEE80211_S_ASSOC ||
	     nstate == IEEE80211_S_RUN)) {
		/* Stop blinking for a scan, when authenticating. */
		iwm_led_blink_stop(sc);
	}

	if (vap->iv_state == IEEE80211_S_RUN && nstate != IEEE80211_S_RUN) {
		iwm_mvm_led_disable(sc);
		/* disable beacon filtering if we're hopping out of RUN */
		iwm_mvm_disable_beacon_filter(sc);
		if (((in = IWM_NODE(vap->iv_bss)) != NULL))
			in->in_assoc = 0;
	}

	if ((vap->iv_state == IEEE80211_S_AUTH ||
	     vap->iv_state == IEEE80211_S_ASSOC ||
	     vap->iv_state == IEEE80211_S_RUN) &&
	    (nstate == IEEE80211_S_INIT ||
	     nstate == IEEE80211_S_SCAN ||
	     nstate == IEEE80211_S_AUTH)) {
		iwm_mvm_stop_session_protection(sc, ivp);
	}

	if ((vap->iv_state == IEEE80211_S_RUN ||
	     vap->iv_state == IEEE80211_S_ASSOC) &&
	    nstate == IEEE80211_S_INIT) {
		/*
		 * In this case, iv_newstate() wants to send an 80211 frame on
		 * the network that we are leaving. So we need to call it,
		 * before tearing down all the firmware state.
		 */
		IWM_UNLOCK(sc);
		IEEE80211_LOCK(ic);
		ivp->iv_newstate(vap, nstate, arg);
		IEEE80211_UNLOCK(ic);
		IWM_LOCK(sc);
		iwm_bring_down_firmware(sc, vap);
		IWM_UNLOCK(sc);
		IEEE80211_LOCK(ic);
		return 0;
	}

	switch (nstate) {
	case IEEE80211_S_INIT:
	case IEEE80211_S_SCAN:
		break;

	case IEEE80211_S_AUTH:
		iwm_bring_down_firmware(sc, vap);
		if ((error = iwm_auth(vap, sc)) != 0) {
			device_printf(sc->sc_dev,
			    "%s: could not move to auth state: %d\n",
			    __func__, error);
			iwm_bring_down_firmware(sc, vap);
			IWM_UNLOCK(sc);
			IEEE80211_LOCK(ic);
			return 1;
		}
		break;

	case IEEE80211_S_ASSOC:
		/*
		 * EBS may be disabled due to previous failures reported by FW.
		 * Reset EBS status here assuming environment has been changed.
		 */
		sc->last_ebs_successful = TRUE;
		break;

	case IEEE80211_S_RUN:
		in = IWM_NODE(vap->iv_bss);
		/* Update the association state, now we have it all */
		/* (eg associd comes in at this point */
		error = iwm_mvm_update_sta(sc, in);
		if (error != 0) {
			device_printf(sc->sc_dev,
			    "%s: failed to update STA\n", __func__);
			IWM_UNLOCK(sc);
			IEEE80211_LOCK(ic);
			return error;
		}
		in->in_assoc = 1;
		error = iwm_mvm_mac_ctxt_changed(sc, vap);
		if (error != 0) {
			device_printf(sc->sc_dev,
			    "%s: failed to update MAC: %d\n", __func__, error);
		}

		iwm_mvm_sf_update(sc, vap, FALSE);
		iwm_mvm_enable_beacon_filter(sc, ivp);
		iwm_mvm_power_update_mac(sc);
		iwm_mvm_update_quotas(sc, ivp);
		int rix = ieee80211_ratectl_rate(&in->in_ni, NULL, 0);
		iwm_setrates(sc, in, rix);

		if ((error = iwm_mvm_send_lq_cmd(sc, &in->in_lq, TRUE)) != 0) {
			device_printf(sc->sc_dev,
			    "%s: IWM_LQ_CMD failed: %d\n", __func__, error);
		}

		iwm_mvm_led_enable(sc);
		break;

	default:
		break;
	}
	IWM_UNLOCK(sc);
	IEEE80211_LOCK(ic);

	return (ivp->iv_newstate(vap, nstate, arg));
}

void
iwm_endscan_cb(void *arg, int pending)
{
	struct iwm_softc *sc = arg;
	struct ieee80211com *ic = &sc->sc_ic;

	IWM_DPRINTF(sc, IWM_DEBUG_SCAN | IWM_DEBUG_TRACE,
	    "%s: scan ended\n",
	    __func__);

	ieee80211_scan_done(TAILQ_FIRST(&ic->ic_vaps));
}

static int
iwm_send_bt_init_conf(struct iwm_softc *sc)
{
	struct iwm_bt_coex_cmd bt_cmd;

	bt_cmd.mode = htole32(IWM_BT_COEX_WIFI);
	bt_cmd.enabled_modules = htole32(IWM_BT_COEX_HIGH_BAND_RET);

	return iwm_mvm_send_cmd_pdu(sc, IWM_BT_CONFIG, 0, sizeof(bt_cmd),
	    &bt_cmd);
}

static boolean_t
iwm_mvm_is_lar_supported(struct iwm_softc *sc)
{
	boolean_t nvm_lar = sc->nvm_data->lar_enabled;
	boolean_t tlv_lar = fw_has_capa(&sc->sc_fw.ucode_capa,
					IWM_UCODE_TLV_CAPA_LAR_SUPPORT);

	if (iwm_lar_disable)
		return FALSE;

	/*
	 * Enable LAR only if it is supported by the FW (TLV) &&
	 * enabled in the NVM
	 */
	if (sc->cfg->device_family == IWM_DEVICE_FAMILY_8000)
		return nvm_lar && tlv_lar;
	else
		return tlv_lar;
}

static boolean_t
iwm_mvm_is_wifi_mcc_supported(struct iwm_softc *sc)
{
	return fw_has_api(&sc->sc_fw.ucode_capa,
			  IWM_UCODE_TLV_API_WIFI_MCC_UPDATE) ||
	       fw_has_capa(&sc->sc_fw.ucode_capa,
			   IWM_UCODE_TLV_CAPA_LAR_MULTI_MCC);
}

static int
iwm_send_update_mcc_cmd(struct iwm_softc *sc, const char *alpha2)
{
	struct iwm_mcc_update_cmd mcc_cmd;
	struct iwm_host_cmd hcmd = {
		.id = IWM_MCC_UPDATE_CMD,
		.flags = (IWM_CMD_SYNC | IWM_CMD_WANT_SKB),
		.data = { &mcc_cmd },
	};
	int ret;
#ifdef IWM_DEBUG
	struct iwm_rx_packet *pkt;
	struct iwm_mcc_update_resp_v1 *mcc_resp_v1 = NULL;
	struct iwm_mcc_update_resp *mcc_resp;
	int n_channels;
	uint16_t mcc;
#endif
	int resp_v2 = fw_has_capa(&sc->sc_fw.ucode_capa,
	    IWM_UCODE_TLV_CAPA_LAR_SUPPORT_V2);

	if (!iwm_mvm_is_lar_supported(sc)) {
		IWM_DPRINTF(sc, IWM_DEBUG_LAR, "%s: no LAR support\n",
		    __func__);
		return 0;
	}

	memset(&mcc_cmd, 0, sizeof(mcc_cmd));
	mcc_cmd.mcc = htole16(alpha2[0] << 8 | alpha2[1]);
	if (iwm_mvm_is_wifi_mcc_supported(sc))
		mcc_cmd.source_id = IWM_MCC_SOURCE_GET_CURRENT;
	else
		mcc_cmd.source_id = IWM_MCC_SOURCE_OLD_FW;

	if (resp_v2)
		hcmd.len[0] = sizeof(struct iwm_mcc_update_cmd);
	else
		hcmd.len[0] = sizeof(struct iwm_mcc_update_cmd_v1);

	IWM_DPRINTF(sc, IWM_DEBUG_LAR,
	    "send MCC update to FW with '%c%c' src = %d\n",
	    alpha2[0], alpha2[1], mcc_cmd.source_id);

	ret = iwm_send_cmd(sc, &hcmd);
	if (ret)
		return ret;

#ifdef IWM_DEBUG
	pkt = hcmd.resp_pkt;

	/* Extract MCC response */
	if (resp_v2) {
		mcc_resp = (void *)pkt->data;
		mcc = mcc_resp->mcc;
		n_channels =  le32toh(mcc_resp->n_channels);
	} else {
		mcc_resp_v1 = (void *)pkt->data;
		mcc = mcc_resp_v1->mcc;
		n_channels =  le32toh(mcc_resp_v1->n_channels);
	}

	/* W/A for a FW/NVM issue - returns 0x00 for the world domain */
	if (mcc == 0)
		mcc = 0x3030;  /* "00" - world */

	IWM_DPRINTF(sc, IWM_DEBUG_LAR,
	    "regulatory domain '%c%c' (%d channels available)\n",
	    mcc >> 8, mcc & 0xff, n_channels);
#endif
	iwm_free_resp(sc, &hcmd);

	return 0;
}

static void
iwm_mvm_tt_tx_backoff(struct iwm_softc *sc, uint32_t backoff)
{
	struct iwm_host_cmd cmd = {
		.id = IWM_REPLY_THERMAL_MNG_BACKOFF,
		.len = { sizeof(uint32_t), },
		.data = { &backoff, },
	};

	if (iwm_send_cmd(sc, &cmd) != 0) {
		device_printf(sc->sc_dev,
		    "failed to change thermal tx backoff\n");
	}
}

static int
iwm_init_hw(struct iwm_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	int error, i, ac;

	sc->sf_state = IWM_SF_UNINIT;

	if ((error = iwm_start_hw(sc)) != 0) {
		printf("iwm_start_hw: failed %d\n", error);
		return error;
	}

	if ((error = iwm_run_init_mvm_ucode(sc, 0)) != 0) {
		printf("iwm_run_init_mvm_ucode: failed %d\n", error);
		return error;
	}

	/*
	 * should stop and start HW since that INIT
	 * image just loaded
	 */
	iwm_stop_device(sc);
	sc->sc_ps_disabled = FALSE;
	if ((error = iwm_start_hw(sc)) != 0) {
		device_printf(sc->sc_dev, "could not initialize hardware\n");
		return error;
	}

	/* omstart, this time with the regular firmware */
	error = iwm_mvm_load_ucode_wait_alive(sc, IWM_UCODE_REGULAR);
	if (error) {
		device_printf(sc->sc_dev, "could not load firmware\n");
		goto error;
	}

	error = iwm_mvm_sf_update(sc, NULL, FALSE);
	if (error)
		device_printf(sc->sc_dev, "Failed to initialize Smart Fifo\n");

	if ((error = iwm_send_bt_init_conf(sc)) != 0) {
		device_printf(sc->sc_dev, "bt init conf failed\n");
		goto error;
	}

	error = iwm_send_tx_ant_cfg(sc, iwm_mvm_get_valid_tx_ant(sc));
	if (error != 0) {
		device_printf(sc->sc_dev, "antenna config failed\n");
		goto error;
	}

	/* Send phy db control command and then phy db calibration */
	if ((error = iwm_send_phy_db_data(sc->sc_phy_db)) != 0)
		goto error;

	if ((error = iwm_send_phy_cfg_cmd(sc)) != 0) {
		device_printf(sc->sc_dev, "phy_cfg_cmd failed\n");
		goto error;
	}

	/* Add auxiliary station for scanning */
	if ((error = iwm_mvm_add_aux_sta(sc)) != 0) {
		device_printf(sc->sc_dev, "add_aux_sta failed\n");
		goto error;
	}

	for (i = 0; i < IWM_NUM_PHY_CTX; i++) {
		/*
		 * The channel used here isn't relevant as it's
		 * going to be overwritten in the other flows.
		 * For now use the first channel we have.
		 */
		if ((error = iwm_mvm_phy_ctxt_add(sc,
		    &sc->sc_phyctxt[i], &ic->ic_channels[1], 1, 1)) != 0)
			goto error;
	}

	/* Initialize tx backoffs to the minimum. */
	if (sc->cfg->device_family == IWM_DEVICE_FAMILY_7000)
		iwm_mvm_tt_tx_backoff(sc, 0);

	if (iwm_mvm_config_ltr(sc) != 0)
		device_printf(sc->sc_dev, "PCIe LTR configuration failed\n");

	error = iwm_mvm_power_update_device(sc);
	if (error)
		goto error;

	if ((error = iwm_send_update_mcc_cmd(sc, "ZZ")) != 0)
		goto error;

	if (fw_has_capa(&sc->sc_fw.ucode_capa, IWM_UCODE_TLV_CAPA_UMAC_SCAN)) {
		if ((error = iwm_mvm_config_umac_scan(sc)) != 0)
			goto error;
	}

	/* Enable Tx queues. */
	for (ac = 0; ac < WME_NUM_AC; ac++) {
		error = iwm_enable_txq(sc, IWM_STATION_ID, ac,
		    iwm_mvm_ac_to_tx_fifo[ac]);
		if (error)
			goto error;
	}

	if ((error = iwm_mvm_disable_beacon_filter(sc)) != 0) {
		device_printf(sc->sc_dev, "failed to disable beacon filter\n");
		goto error;
	}

	return 0;

 error:
	iwm_stop_device(sc);
	return error;
}

/* Allow multicast from our BSSID. */
static int
iwm_allow_mcast(struct ieee80211vap *vap, struct iwm_softc *sc)
{
	struct ieee80211_node *ni = vap->iv_bss;
	struct iwm_mcast_filter_cmd *cmd;
	size_t size;
	int error;

	size = roundup(sizeof(*cmd), 4);
	cmd = malloc(size, M_DEVBUF, M_NOWAIT | M_ZERO);
	if (cmd == NULL)
		return ENOMEM;
	cmd->filter_own = 1;
	cmd->port_id = 0;
	cmd->count = 0;
	cmd->pass_all = 1;
	IEEE80211_ADDR_COPY(cmd->bssid, ni->ni_bssid);

	error = iwm_mvm_send_cmd_pdu(sc, IWM_MCAST_FILTER_CMD,
	    IWM_CMD_SYNC, size, cmd);
	free(cmd, M_DEVBUF);

	return (error);
}

/*
 * ifnet interfaces
 */

static void
iwm_init(struct iwm_softc *sc)
{
	int error;

	if (sc->sc_flags & IWM_FLAG_HW_INITED) {
		return;
	}
	sc->sc_generation++;
	sc->sc_flags &= ~IWM_FLAG_STOPPED;

	if ((error = iwm_init_hw(sc)) != 0) {
		printf("iwm_init_hw failed %d\n", error);
		iwm_stop(sc);
		return;
	}

	/*
	 * Ok, firmware loaded and we are jogging
	 */
	sc->sc_flags |= IWM_FLAG_HW_INITED;
}

static int
iwm_transmit(struct ieee80211com *ic, struct mbuf *m)
{
	struct iwm_softc *sc;
	int error;

	sc = ic->ic_softc;

	IWM_LOCK(sc);
	if ((sc->sc_flags & IWM_FLAG_HW_INITED) == 0) {
		IWM_UNLOCK(sc);
		return (ENXIO);
	}
	error = mbufq_enqueue(&sc->sc_snd, m);
	if (error) {
		IWM_UNLOCK(sc);
		return (error);
	}
	iwm_start(sc);
	IWM_UNLOCK(sc);
	return (0);
}

/*
 * Dequeue packets from sendq and call send.
 */
static void
iwm_start(struct iwm_softc *sc)
{
	struct ieee80211_node *ni;
	struct mbuf *m;
	int ac = 0;

	IWM_DPRINTF(sc, IWM_DEBUG_XMIT | IWM_DEBUG_TRACE, "->%s\n", __func__);
	while (sc->qfullmsk == 0 &&
		(m = mbufq_dequeue(&sc->sc_snd)) != NULL) {
		ni = (struct ieee80211_node *)m->m_pkthdr.rcvif;
		if (iwm_tx(sc, m, ni, ac) != 0) {
			if_inc_counter(ni->ni_vap->iv_ifp,
			    IFCOUNTER_OERRORS, 1);
			ieee80211_free_node(ni);
			continue;
		}
		if (sc->sc_tx_timer == 0) {
			callout_reset(&sc->sc_watchdog_to, hz, iwm_watchdog,
			    sc);
		}
		sc->sc_tx_timer = 15;
	}
	IWM_DPRINTF(sc, IWM_DEBUG_XMIT | IWM_DEBUG_TRACE, "<-%s\n", __func__);
}

static void
iwm_stop(struct iwm_softc *sc)
{

	sc->sc_flags &= ~IWM_FLAG_HW_INITED;
	sc->sc_flags |= IWM_FLAG_STOPPED;
	sc->sc_generation++;
	iwm_led_blink_stop(sc);
	sc->sc_tx_timer = 0;
	iwm_stop_device(sc);
	sc->sc_flags &= ~IWM_FLAG_SCAN_RUNNING;
}

static void
iwm_watchdog(void *arg)
{
	struct iwm_softc *sc = arg;
	struct ieee80211com *ic = &sc->sc_ic;

	if (sc->sc_attached == 0)
		return;

	if (sc->sc_tx_timer > 0) {
		if (--sc->sc_tx_timer == 0) {
			device_printf(sc->sc_dev, "device timeout\n");
#ifdef IWM_DEBUG
			iwm_nic_error(sc);
#endif
			ieee80211_restart_all(ic);
			counter_u64_add(sc->sc_ic.ic_oerrors, 1);
			return;
		}
		callout_reset(&sc->sc_watchdog_to, hz, iwm_watchdog, sc);
	}
}

static void
iwm_parent(struct ieee80211com *ic)
{
	struct iwm_softc *sc = ic->ic_softc;
	int startall = 0;

	IWM_LOCK(sc);
	if (ic->ic_nrunning > 0) {
		if (!(sc->sc_flags & IWM_FLAG_HW_INITED)) {
			iwm_init(sc);
			startall = 1;
		}
	} else if (sc->sc_flags & IWM_FLAG_HW_INITED)
		iwm_stop(sc);
	IWM_UNLOCK(sc);
	if (startall)
		ieee80211_start_all(ic);
}

/*
 * The interrupt side of things
 */

/*
 * error dumping routines are from iwlwifi/mvm/utils.c
 */

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

#ifdef IWM_DEBUG
struct {
	const char *name;
	uint8_t num;
} advanced_lookup[] = {
	{ "NMI_INTERRUPT_WDG", 0x34 },
	{ "SYSASSERT", 0x35 },
	{ "UCODE_VERSION_MISMATCH", 0x37 },
	{ "BAD_COMMAND", 0x38 },
	{ "NMI_INTERRUPT_DATA_ACTION_PT", 0x3C },
	{ "FATAL_ERROR", 0x3D },
	{ "NMI_TRM_HW_ERR", 0x46 },
	{ "NMI_INTERRUPT_TRM", 0x4C },
	{ "NMI_INTERRUPT_BREAK_POINT", 0x54 },
	{ "NMI_INTERRUPT_WDG_RXF_FULL", 0x5C },
	{ "NMI_INTERRUPT_WDG_NO_RBD_RXF_FULL", 0x64 },
	{ "NMI_INTERRUPT_HOST", 0x66 },
	{ "NMI_INTERRUPT_ACTION_PT", 0x7C },
	{ "NMI_INTERRUPT_UNKNOWN", 0x84 },
	{ "NMI_INTERRUPT_INST_ACTION_PT", 0x86 },
	{ "ADVANCED_SYSASSERT", 0 },
};

static const char *
iwm_desc_lookup(uint32_t num)
{
	int i;

	for (i = 0; i < nitems(advanced_lookup) - 1; i++)
		if (advanced_lookup[i].num == num)
			return advanced_lookup[i].name;

	/* No entry matches 'num', so it is the last: ADVANCED_SYSASSERT */
	return advanced_lookup[i].name;
}

static void
iwm_nic_umac_error(struct iwm_softc *sc)
{
	struct iwm_umac_error_event_table table;
	uint32_t base;

	base = sc->umac_error_event_table;

	if (base < 0x800000) {
		device_printf(sc->sc_dev, "Invalid error log pointer 0x%08x\n",
		    base);
		return;
	}

	if (iwm_read_mem(sc, base, &table, sizeof(table)/sizeof(uint32_t))) {
		device_printf(sc->sc_dev, "reading errlog failed\n");
		return;
	}

	if (ERROR_START_OFFSET <= table.valid * ERROR_ELEM_SIZE) {
		device_printf(sc->sc_dev, "Start UMAC Error Log Dump:\n");
		device_printf(sc->sc_dev, "Status: 0x%x, count: %d\n",
		    sc->sc_flags, table.valid);
	}

	device_printf(sc->sc_dev, "0x%08X | %s\n", table.error_id,
		iwm_desc_lookup(table.error_id));
	device_printf(sc->sc_dev, "0x%08X | umac branchlink1\n", table.blink1);
	device_printf(sc->sc_dev, "0x%08X | umac branchlink2\n", table.blink2);
	device_printf(sc->sc_dev, "0x%08X | umac interruptlink1\n",
	    table.ilink1);
	device_printf(sc->sc_dev, "0x%08X | umac interruptlink2\n",
	    table.ilink2);
	device_printf(sc->sc_dev, "0x%08X | umac data1\n", table.data1);
	device_printf(sc->sc_dev, "0x%08X | umac data2\n", table.data2);
	device_printf(sc->sc_dev, "0x%08X | umac data3\n", table.data3);
	device_printf(sc->sc_dev, "0x%08X | umac major\n", table.umac_major);
	device_printf(sc->sc_dev, "0x%08X | umac minor\n", table.umac_minor);
	device_printf(sc->sc_dev, "0x%08X | frame pointer\n",
	    table.frame_pointer);
	device_printf(sc->sc_dev, "0x%08X | stack pointer\n",
	    table.stack_pointer);
	device_printf(sc->sc_dev, "0x%08X | last host cmd\n", table.cmd_header);
	device_printf(sc->sc_dev, "0x%08X | isr status reg\n",
	    table.nic_isr_pref);
}

/*
 * Support for dumping the error log seemed like a good idea ...
 * but it's mostly hex junk and the only sensible thing is the
 * hw/ucode revision (which we know anyway).  Since it's here,
 * I'll just leave it in, just in case e.g. the Intel guys want to
 * help us decipher some "ADVANCED_SYSASSERT" later.
 */
static void
iwm_nic_error(struct iwm_softc *sc)
{
	struct iwm_error_event_table table;
	uint32_t base;

	device_printf(sc->sc_dev, "dumping device error log\n");
	base = sc->error_event_table[0];
	if (base < 0x800000) {
		device_printf(sc->sc_dev,
		    "Invalid error log pointer 0x%08x\n", base);
		return;
	}

	if (iwm_read_mem(sc, base, &table, sizeof(table)/sizeof(uint32_t))) {
		device_printf(sc->sc_dev, "reading errlog failed\n");
		return;
	}

	if (!table.valid) {
		device_printf(sc->sc_dev, "errlog not found, skipping\n");
		return;
	}

	if (ERROR_START_OFFSET <= table.valid * ERROR_ELEM_SIZE) {
		device_printf(sc->sc_dev, "Start Error Log Dump:\n");
		device_printf(sc->sc_dev, "Status: 0x%x, count: %d\n",
		    sc->sc_flags, table.valid);
	}

	device_printf(sc->sc_dev, "0x%08X | %-28s\n", table.error_id,
	    iwm_desc_lookup(table.error_id));
	device_printf(sc->sc_dev, "%08X | trm_hw_status0\n",
	    table.trm_hw_status0);
	device_printf(sc->sc_dev, "%08X | trm_hw_status1\n",
	    table.trm_hw_status1);
	device_printf(sc->sc_dev, "%08X | branchlink2\n", table.blink2);
	device_printf(sc->sc_dev, "%08X | interruptlink1\n", table.ilink1);
	device_printf(sc->sc_dev, "%08X | interruptlink2\n", table.ilink2);
	device_printf(sc->sc_dev, "%08X | data1\n", table.data1);
	device_printf(sc->sc_dev, "%08X | data2\n", table.data2);
	device_printf(sc->sc_dev, "%08X | data3\n", table.data3);
	device_printf(sc->sc_dev, "%08X | beacon time\n", table.bcon_time);
	device_printf(sc->sc_dev, "%08X | tsf low\n", table.tsf_low);
	device_printf(sc->sc_dev, "%08X | tsf hi\n", table.tsf_hi);
	device_printf(sc->sc_dev, "%08X | time gp1\n", table.gp1);
	device_printf(sc->sc_dev, "%08X | time gp2\n", table.gp2);
	device_printf(sc->sc_dev, "%08X | uCode revision type\n",
	    table.fw_rev_type);
	device_printf(sc->sc_dev, "%08X | uCode version major\n", table.major);
	device_printf(sc->sc_dev, "%08X | uCode version minor\n", table.minor);
	device_printf(sc->sc_dev, "%08X | hw version\n", table.hw_ver);
	device_printf(sc->sc_dev, "%08X | board version\n", table.brd_ver);
	device_printf(sc->sc_dev, "%08X | hcmd\n", table.hcmd);
	device_printf(sc->sc_dev, "%08X | isr0\n", table.isr0);
	device_printf(sc->sc_dev, "%08X | isr1\n", table.isr1);
	device_printf(sc->sc_dev, "%08X | isr2\n", table.isr2);
	device_printf(sc->sc_dev, "%08X | isr3\n", table.isr3);
	device_printf(sc->sc_dev, "%08X | isr4\n", table.isr4);
	device_printf(sc->sc_dev, "%08X | last cmd Id\n", table.last_cmd_id);
	device_printf(sc->sc_dev, "%08X | wait_event\n", table.wait_event);
	device_printf(sc->sc_dev, "%08X | l2p_control\n", table.l2p_control);
	device_printf(sc->sc_dev, "%08X | l2p_duration\n", table.l2p_duration);
	device_printf(sc->sc_dev, "%08X | l2p_mhvalid\n", table.l2p_mhvalid);
	device_printf(sc->sc_dev, "%08X | l2p_addr_match\n", table.l2p_addr_match);
	device_printf(sc->sc_dev, "%08X | lmpm_pmg_sel\n", table.lmpm_pmg_sel);
	device_printf(sc->sc_dev, "%08X | timestamp\n", table.u_timestamp);
	device_printf(sc->sc_dev, "%08X | flow_handler\n", table.flow_handler);

	if (sc->umac_error_event_table)
		iwm_nic_umac_error(sc);
}
#endif

static void
iwm_handle_rxb(struct iwm_softc *sc, struct mbuf *m)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct iwm_cmd_response *cresp;
	struct mbuf *m1;
	uint32_t offset = 0;
	uint32_t maxoff = IWM_RBUF_SIZE;
	uint32_t nextoff;
	boolean_t stolen = FALSE;

#define HAVEROOM(a)	\
    ((a) + sizeof(uint32_t) + sizeof(struct iwm_cmd_header) < maxoff)

	while (HAVEROOM(offset)) {
		struct iwm_rx_packet *pkt = mtodoff(m, struct iwm_rx_packet *,
		    offset);
		int qid, idx, code, len;

		qid = pkt->hdr.qid;
		idx = pkt->hdr.idx;

		code = IWM_WIDE_ID(pkt->hdr.flags, pkt->hdr.code);

		/*
		 * randomly get these from the firmware, no idea why.
		 * they at least seem harmless, so just ignore them for now
		 */
		if ((pkt->hdr.code == 0 && (qid & ~0x80) == 0 && idx == 0) ||
		    pkt->len_n_flags == htole32(IWM_FH_RSCSR_FRAME_INVALID)) {
			break;
		}

		IWM_DPRINTF(sc, IWM_DEBUG_INTR,
		    "rx packet qid=%d idx=%d type=%x\n",
		    qid & ~0x80, pkt->hdr.idx, code);

		len = iwm_rx_packet_len(pkt);
		len += sizeof(uint32_t); /* account for status word */
		nextoff = offset + roundup2(len, IWM_FH_RSCSR_FRAME_ALIGN);

		iwm_notification_wait_notify(sc->sc_notif_wait, code, pkt);

		switch (code) {
		case IWM_REPLY_RX_PHY_CMD:
			iwm_mvm_rx_rx_phy_cmd(sc, pkt);
			break;

		case IWM_REPLY_RX_MPDU_CMD: {
			/*
			 * If this is the last frame in the RX buffer, we
			 * can directly feed the mbuf to the sharks here.
			 */
			struct iwm_rx_packet *nextpkt = mtodoff(m,
			    struct iwm_rx_packet *, nextoff);
			if (!HAVEROOM(nextoff) ||
			    (nextpkt->hdr.code == 0 &&
			     (nextpkt->hdr.qid & ~0x80) == 0 &&
			     nextpkt->hdr.idx == 0) ||
			    (nextpkt->len_n_flags ==
			     htole32(IWM_FH_RSCSR_FRAME_INVALID))) {
				if (iwm_mvm_rx_rx_mpdu(sc, m, offset, stolen)) {
					stolen = FALSE;
					/* Make sure we abort the loop */
					nextoff = maxoff;
				}
				break;
			}

			/*
			 * Use m_copym instead of m_split, because that
			 * makes it easier to keep a valid rx buffer in
			 * the ring, when iwm_mvm_rx_rx_mpdu() fails.
			 *
			 * We need to start m_copym() at offset 0, to get the
			 * M_PKTHDR flag preserved.
			 */
			m1 = m_copym(m, 0, M_COPYALL, M_NOWAIT);
			if (m1) {
				if (iwm_mvm_rx_rx_mpdu(sc, m1, offset, stolen))
					stolen = TRUE;
				else
					m_freem(m1);
			}
			break;
		}

		case IWM_TX_CMD:
			iwm_mvm_rx_tx_cmd(sc, pkt);
			break;

		case IWM_MISSED_BEACONS_NOTIFICATION: {
			struct iwm_missed_beacons_notif *resp;
			int missed;

			/* XXX look at mac_id to determine interface ID */
			struct ieee80211vap *vap = TAILQ_FIRST(&ic->ic_vaps);

			resp = (void *)pkt->data;
			missed = le32toh(resp->consec_missed_beacons);

			IWM_DPRINTF(sc, IWM_DEBUG_BEACON | IWM_DEBUG_STATE,
			    "%s: MISSED_BEACON: mac_id=%d, "
			    "consec_since_last_rx=%d, consec=%d, num_expect=%d "
			    "num_rx=%d\n",
			    __func__,
			    le32toh(resp->mac_id),
			    le32toh(resp->consec_missed_beacons_since_last_rx),
			    le32toh(resp->consec_missed_beacons),
			    le32toh(resp->num_expected_beacons),
			    le32toh(resp->num_recvd_beacons));

			/* Be paranoid */
			if (vap == NULL)
				break;

			/* XXX no net80211 locking? */
			if (vap->iv_state == IEEE80211_S_RUN &&
			    (ic->ic_flags & IEEE80211_F_SCAN) == 0) {
				if (missed > vap->iv_bmissthreshold) {
					/* XXX bad locking; turn into task */
					IWM_UNLOCK(sc);
					ieee80211_beacon_miss(ic);
					IWM_LOCK(sc);
				}
			}

			break;
		}

		case IWM_MFUART_LOAD_NOTIFICATION:
			break;

		case IWM_MVM_ALIVE:
			break;

		case IWM_CALIB_RES_NOTIF_PHY_DB:
			break;

		case IWM_STATISTICS_NOTIFICATION:
			iwm_mvm_handle_rx_statistics(sc, pkt);
			break;

		case IWM_NVM_ACCESS_CMD:
		case IWM_MCC_UPDATE_CMD:
			if (sc->sc_wantresp == (((qid & ~0x80) << 16) | idx)) {
				memcpy(sc->sc_cmd_resp,
				    pkt, sizeof(sc->sc_cmd_resp));
			}
			break;

		case IWM_MCC_CHUB_UPDATE_CMD: {
			struct iwm_mcc_chub_notif *notif;
			notif = (void *)pkt->data;

			sc->sc_fw_mcc[0] = (notif->mcc & 0xff00) >> 8;
			sc->sc_fw_mcc[1] = notif->mcc & 0xff;
			sc->sc_fw_mcc[2] = '\0';
			IWM_DPRINTF(sc, IWM_DEBUG_LAR,
			    "fw source %d sent CC '%s'\n",
			    notif->source_id, sc->sc_fw_mcc);
			break;
		}

		case IWM_DTS_MEASUREMENT_NOTIFICATION:
		case IWM_WIDE_ID(IWM_PHY_OPS_GROUP,
				 IWM_DTS_MEASUREMENT_NOTIF_WIDE): {
			struct iwm_dts_measurement_notif_v1 *notif;

			if (iwm_rx_packet_payload_len(pkt) < sizeof(*notif)) {
				device_printf(sc->sc_dev,
				    "Invalid DTS_MEASUREMENT_NOTIFICATION\n");
				break;
			}
			notif = (void *)pkt->data;
			IWM_DPRINTF(sc, IWM_DEBUG_TEMP,
			    "IWM_DTS_MEASUREMENT_NOTIFICATION - %d\n",
			    notif->temp);
			break;
		}

		case IWM_PHY_CONFIGURATION_CMD:
		case IWM_TX_ANT_CONFIGURATION_CMD:
		case IWM_ADD_STA:
		case IWM_MAC_CONTEXT_CMD:
		case IWM_REPLY_SF_CFG_CMD:
		case IWM_POWER_TABLE_CMD:
		case IWM_LTR_CONFIG:
		case IWM_PHY_CONTEXT_CMD:
		case IWM_BINDING_CONTEXT_CMD:
		case IWM_TIME_EVENT_CMD:
		case IWM_WIDE_ID(IWM_ALWAYS_LONG_GROUP, IWM_SCAN_CFG_CMD):
		case IWM_WIDE_ID(IWM_ALWAYS_LONG_GROUP, IWM_SCAN_REQ_UMAC):
		case IWM_WIDE_ID(IWM_ALWAYS_LONG_GROUP, IWM_SCAN_ABORT_UMAC):
		case IWM_SCAN_OFFLOAD_REQUEST_CMD:
		case IWM_SCAN_OFFLOAD_ABORT_CMD:
		case IWM_REPLY_BEACON_FILTERING_CMD:
		case IWM_MAC_PM_POWER_TABLE:
		case IWM_TIME_QUOTA_CMD:
		case IWM_REMOVE_STA:
		case IWM_TXPATH_FLUSH:
		case IWM_LQ_CMD:
		case IWM_WIDE_ID(IWM_ALWAYS_LONG_GROUP,
				 IWM_FW_PAGING_BLOCK_CMD):
		case IWM_BT_CONFIG:
		case IWM_REPLY_THERMAL_MNG_BACKOFF:
			cresp = (void *)pkt->data;
			if (sc->sc_wantresp == (((qid & ~0x80) << 16) | idx)) {
				memcpy(sc->sc_cmd_resp,
				    pkt, sizeof(*pkt)+sizeof(*cresp));
			}
			break;

		/* ignore */
		case IWM_PHY_DB_CMD:
			break;

		case IWM_INIT_COMPLETE_NOTIF:
			break;

		case IWM_SCAN_OFFLOAD_COMPLETE:
			iwm_mvm_rx_lmac_scan_complete_notif(sc, pkt);
			if (sc->sc_flags & IWM_FLAG_SCAN_RUNNING) {
				sc->sc_flags &= ~IWM_FLAG_SCAN_RUNNING;
				ieee80211_runtask(ic, &sc->sc_es_task);
			}
			break;

		case IWM_SCAN_ITERATION_COMPLETE: {
			struct iwm_lmac_scan_complete_notif *notif;
			notif = (void *)pkt->data;
			break;
		}

		case IWM_SCAN_COMPLETE_UMAC:
			iwm_mvm_rx_umac_scan_complete_notif(sc, pkt);
			if (sc->sc_flags & IWM_FLAG_SCAN_RUNNING) {
				sc->sc_flags &= ~IWM_FLAG_SCAN_RUNNING;
				ieee80211_runtask(ic, &sc->sc_es_task);
			}
			break;

		case IWM_SCAN_ITERATION_COMPLETE_UMAC: {
			struct iwm_umac_scan_iter_complete_notif *notif;
			notif = (void *)pkt->data;

			IWM_DPRINTF(sc, IWM_DEBUG_SCAN, "UMAC scan iteration "
			    "complete, status=0x%x, %d channels scanned\n",
			    notif->status, notif->scanned_channels);
			break;
		}

		case IWM_REPLY_ERROR: {
			struct iwm_error_resp *resp;
			resp = (void *)pkt->data;

			device_printf(sc->sc_dev,
			    "firmware error 0x%x, cmd 0x%x\n",
			    le32toh(resp->error_type),
			    resp->cmd_id);
			break;
		}

		case IWM_TIME_EVENT_NOTIFICATION:
			iwm_mvm_rx_time_event_notif(sc, pkt);
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
			rsp = (void *)pkt->data;

			IWM_DPRINTF(sc, IWM_DEBUG_CMD,
			    "queue cfg token=0x%x sta_id=%d "
			    "tid=%d scd_queue=%d\n",
			    rsp->token, rsp->sta_id, rsp->tid,
			    rsp->scd_queue);
			break;
		}

		default:
			device_printf(sc->sc_dev,
			    "frame %d/%d %x UNHANDLED (this should "
			    "not happen)\n", qid & ~0x80, idx,
			    pkt->len_n_flags);
			break;
		}

		/*
		 * Why test bit 0x80?  The Linux driver:
		 *
		 * There is one exception:  uCode sets bit 15 when it
		 * originates the response/notification, i.e. when the
		 * response/notification is not a direct response to a
		 * command sent by the driver.  For example, uCode issues
		 * IWM_REPLY_RX when it sends a received frame to the driver;
		 * it is not a direct response to any driver command.
		 *
		 * Ok, so since when is 7 == 15?  Well, the Linux driver
		 * uses a slightly different format for pkt->hdr, and "qid"
		 * is actually the upper byte of a two-byte field.
		 */
		if (!(qid & (1 << 7)))
			iwm_cmd_done(sc, pkt);

		offset = nextoff;
	}
	if (stolen)
		m_freem(m);
#undef HAVEROOM
}

/*
 * Process an IWM_CSR_INT_BIT_FH_RX or IWM_CSR_INT_BIT_SW_RX interrupt.
 * Basic structure from if_iwn
 */
static void
iwm_notif_intr(struct iwm_softc *sc)
{
	uint16_t hw;

	bus_dmamap_sync(sc->rxq.stat_dma.tag, sc->rxq.stat_dma.map,
	    BUS_DMASYNC_POSTREAD);

	hw = le16toh(sc->rxq.stat->closed_rb_num) & 0xfff;

	/*
	 * Process responses
	 */
	while (sc->rxq.cur != hw) {
		struct iwm_rx_ring *ring = &sc->rxq;
		struct iwm_rx_data *data = &ring->data[ring->cur];

		bus_dmamap_sync(ring->data_dmat, data->map,
		    BUS_DMASYNC_POSTREAD);

		IWM_DPRINTF(sc, IWM_DEBUG_INTR,
		    "%s: hw = %d cur = %d\n", __func__, hw, ring->cur);
		iwm_handle_rxb(sc, data->m);

		ring->cur = (ring->cur + 1) % IWM_RX_RING_COUNT;
	}

	/*
	 * Tell the firmware that it can reuse the ring entries that
	 * we have just processed.
	 * Seems like the hardware gets upset unless we align
	 * the write by 8??
	 */
	hw = (hw == 0) ? IWM_RX_RING_COUNT - 1 : hw - 1;
	IWM_WRITE(sc, IWM_FH_RSCSR_CHNL0_WPTR, rounddown2(hw, 8));
}

static void
iwm_intr(void *arg)
{
	struct iwm_softc *sc = arg;
	int handled = 0;
	int r1, r2, rv = 0;
	int isperiodic = 0;

	IWM_LOCK(sc);
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

		/* i am not expected to understand this */
		if (r1 & 0xc0000)
			r1 |= 0x8000;
		r1 = (0xff & r1) | ((0xff00 & r1) << 16);
	} else {
		r1 = IWM_READ(sc, IWM_CSR_INT);
		/* "hardware gone" (where, fishing?) */
		if (r1 == 0xffffffff || (r1 & 0xfffffff0) == 0xa5a5a5a0)
			goto out;
		r2 = IWM_READ(sc, IWM_CSR_FH_INT_STATUS);
	}
	if (r1 == 0 && r2 == 0) {
		goto out_ena;
	}

	IWM_WRITE(sc, IWM_CSR_INT, r1 | ~sc->sc_intmask);

	/* Safely ignore these bits for debug checks below */
	r1 &= ~(IWM_CSR_INT_BIT_ALIVE | IWM_CSR_INT_BIT_SCD);

	if (r1 & IWM_CSR_INT_BIT_SW_ERR) {
		int i;
		struct ieee80211com *ic = &sc->sc_ic;
		struct ieee80211vap *vap = TAILQ_FIRST(&ic->ic_vaps);

#ifdef IWM_DEBUG
		iwm_nic_error(sc);
#endif
		/* Dump driver status (TX and RX rings) while we're here. */
		device_printf(sc->sc_dev, "driver status:\n");
		for (i = 0; i < IWM_MVM_MAX_QUEUES; i++) {
			struct iwm_tx_ring *ring = &sc->txq[i];
			device_printf(sc->sc_dev,
			    "  tx ring %2d: qid=%-2d cur=%-3d "
			    "queued=%-3d\n",
			    i, ring->qid, ring->cur, ring->queued);
		}
		device_printf(sc->sc_dev,
		    "  rx ring: cur=%d\n", sc->rxq.cur);
		device_printf(sc->sc_dev,
		    "  802.11 state %d\n", (vap == NULL) ? -1 : vap->iv_state);

		/* Reset our firmware state tracking. */
		sc->sc_firmware_state = 0;
		/* Don't stop the device; just do a VAP restart */
		IWM_UNLOCK(sc);

		if (vap == NULL) {
			printf("%s: null vap\n", __func__);
			return;
		}

		device_printf(sc->sc_dev, "%s: controller panicked, iv_state = %d; "
		    "restarting\n", __func__, vap->iv_state);

		ieee80211_restart_all(ic);
		return;
	}

	if (r1 & IWM_CSR_INT_BIT_HW_ERR) {
		handled |= IWM_CSR_INT_BIT_HW_ERR;
		device_printf(sc->sc_dev, "hardware error, stopping device\n");
		iwm_stop(sc);
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

	if (r1 & IWM_CSR_INT_BIT_RF_KILL) {
		handled |= IWM_CSR_INT_BIT_RF_KILL;
		if (iwm_check_rfkill(sc)) {
			device_printf(sc->sc_dev,
			    "%s: rfkill switch, disabling interface\n",
			    __func__);
			iwm_stop(sc);
		}
	}

	/*
	 * The Linux driver uses periodic interrupts to avoid races.
	 * We cargo-cult like it's going out of fashion.
	 */
	if (r1 & IWM_CSR_INT_BIT_RX_PERIODIC) {
		handled |= IWM_CSR_INT_BIT_RX_PERIODIC;
		IWM_WRITE(sc, IWM_CSR_INT, IWM_CSR_INT_BIT_RX_PERIODIC);
		if ((r1 & (IWM_CSR_INT_BIT_FH_RX | IWM_CSR_INT_BIT_SW_RX)) == 0)
			IWM_WRITE_1(sc,
			    IWM_CSR_INT_PERIODIC_REG, IWM_CSR_INT_PERIODIC_DIS);
		isperiodic = 1;
	}

	if ((r1 & (IWM_CSR_INT_BIT_FH_RX | IWM_CSR_INT_BIT_SW_RX)) || isperiodic) {
		handled |= (IWM_CSR_INT_BIT_FH_RX | IWM_CSR_INT_BIT_SW_RX);
		IWM_WRITE(sc, IWM_CSR_FH_INT_STATUS, IWM_CSR_FH_INT_RX_MASK);

		iwm_notif_intr(sc);

		/* enable periodic interrupt, see above */
		if (r1 & (IWM_CSR_INT_BIT_FH_RX | IWM_CSR_INT_BIT_SW_RX) && !isperiodic)
			IWM_WRITE_1(sc, IWM_CSR_INT_PERIODIC_REG,
			    IWM_CSR_INT_PERIODIC_ENA);
	}

	if (__predict_false(r1 & ~handled))
		IWM_DPRINTF(sc, IWM_DEBUG_INTR,
		    "%s: unhandled interrupts: %x\n", __func__, r1);
	rv = 1;

 out_ena:
	iwm_restore_interrupts(sc);
 out:
	IWM_UNLOCK(sc);
	return;
}

/*
 * Autoconf glue-sniffing
 */
#define	PCI_VENDOR_INTEL		0x8086
#define	PCI_PRODUCT_INTEL_WL_3160_1	0x08b3
#define	PCI_PRODUCT_INTEL_WL_3160_2	0x08b4
#define	PCI_PRODUCT_INTEL_WL_3165_1	0x3165
#define	PCI_PRODUCT_INTEL_WL_3165_2	0x3166
#define	PCI_PRODUCT_INTEL_WL_3168_1	0x24fb
#define	PCI_PRODUCT_INTEL_WL_7260_1	0x08b1
#define	PCI_PRODUCT_INTEL_WL_7260_2	0x08b2
#define	PCI_PRODUCT_INTEL_WL_7265_1	0x095a
#define	PCI_PRODUCT_INTEL_WL_7265_2	0x095b
#define	PCI_PRODUCT_INTEL_WL_8260_1	0x24f3
#define	PCI_PRODUCT_INTEL_WL_8260_2	0x24f4
#define	PCI_PRODUCT_INTEL_WL_8265_1	0x24fd

static const struct iwm_devices {
	uint16_t		device;
	const struct iwm_cfg	*cfg;
} iwm_devices[] = {
	{ PCI_PRODUCT_INTEL_WL_3160_1, &iwm3160_cfg },
	{ PCI_PRODUCT_INTEL_WL_3160_2, &iwm3160_cfg },
	{ PCI_PRODUCT_INTEL_WL_3165_1, &iwm3165_cfg },
	{ PCI_PRODUCT_INTEL_WL_3165_2, &iwm3165_cfg },
	{ PCI_PRODUCT_INTEL_WL_3168_1, &iwm3168_cfg },
	{ PCI_PRODUCT_INTEL_WL_7260_1, &iwm7260_cfg },
	{ PCI_PRODUCT_INTEL_WL_7260_2, &iwm7260_cfg },
	{ PCI_PRODUCT_INTEL_WL_7265_1, &iwm7265_cfg },
	{ PCI_PRODUCT_INTEL_WL_7265_2, &iwm7265_cfg },
	{ PCI_PRODUCT_INTEL_WL_8260_1, &iwm8260_cfg },
	{ PCI_PRODUCT_INTEL_WL_8260_2, &iwm8260_cfg },
	{ PCI_PRODUCT_INTEL_WL_8265_1, &iwm8265_cfg },
};

static int
iwm_probe(device_t dev)
{
	int i;

	for (i = 0; i < nitems(iwm_devices); i++) {
		if (pci_get_vendor(dev) == PCI_VENDOR_INTEL &&
		    pci_get_device(dev) == iwm_devices[i].device) {
			device_set_desc(dev, iwm_devices[i].cfg->name);
			return (BUS_PROBE_DEFAULT);
		}
	}

	return (ENXIO);
}

static int
iwm_dev_check(device_t dev)
{
	struct iwm_softc *sc;
	uint16_t devid;
	int i;

	sc = device_get_softc(dev);

	devid = pci_get_device(dev);
	for (i = 0; i < nitems(iwm_devices); i++) {
		if (iwm_devices[i].device == devid) {
			sc->cfg = iwm_devices[i].cfg;
			return (0);
		}
	}
	device_printf(dev, "unknown adapter type\n");
	return ENXIO;
}

/* PCI registers */
#define PCI_CFG_RETRY_TIMEOUT	0x041

static int
iwm_pci_attach(device_t dev)
{
	struct iwm_softc *sc;
	int count, error, rid;
	uint16_t reg;

	sc = device_get_softc(dev);

	/* We disable the RETRY_TIMEOUT register (0x41) to keep
	 * PCI Tx retries from interfering with C3 CPU state */
	pci_write_config(dev, PCI_CFG_RETRY_TIMEOUT, 0x00, 1);

	/* Enable bus-mastering and hardware bug workaround. */
	pci_enable_busmaster(dev);
	reg = pci_read_config(dev, PCIR_STATUS, sizeof(reg));
	/* if !MSI */
	if (reg & PCIM_STATUS_INTxSTATE) {
		reg &= ~PCIM_STATUS_INTxSTATE;
	}
	pci_write_config(dev, PCIR_STATUS, reg, sizeof(reg));

	rid = PCIR_BAR(0);
	sc->sc_mem = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &rid,
	    RF_ACTIVE);
	if (sc->sc_mem == NULL) {
		device_printf(sc->sc_dev, "can't map mem space\n");
		return (ENXIO);
	}
	sc->sc_st = rman_get_bustag(sc->sc_mem);
	sc->sc_sh = rman_get_bushandle(sc->sc_mem);

	/* Install interrupt handler. */
	count = 1;
	rid = 0;
	if (pci_alloc_msi(dev, &count) == 0)
		rid = 1;
	sc->sc_irq = bus_alloc_resource_any(dev, SYS_RES_IRQ, &rid, RF_ACTIVE |
	    (rid != 0 ? 0 : RF_SHAREABLE));
	if (sc->sc_irq == NULL) {
		device_printf(dev, "can't map interrupt\n");
			return (ENXIO);
	}
	error = bus_setup_intr(dev, sc->sc_irq, INTR_TYPE_NET | INTR_MPSAFE,
	    NULL, iwm_intr, sc, &sc->sc_ih);
	if (sc->sc_ih == NULL) {
		device_printf(dev, "can't establish interrupt");
			return (ENXIO);
	}
	sc->sc_dmat = bus_get_dma_tag(sc->sc_dev);

	return (0);
}

static void
iwm_pci_detach(device_t dev)
{
	struct iwm_softc *sc = device_get_softc(dev);

	if (sc->sc_irq != NULL) {
		bus_teardown_intr(dev, sc->sc_irq, sc->sc_ih);
		bus_release_resource(dev, SYS_RES_IRQ,
		    rman_get_rid(sc->sc_irq), sc->sc_irq);
		pci_release_msi(dev);
        }
	if (sc->sc_mem != NULL)
		bus_release_resource(dev, SYS_RES_MEMORY,
		    rman_get_rid(sc->sc_mem), sc->sc_mem);
}



static int
iwm_attach(device_t dev)
{
	struct iwm_softc *sc = device_get_softc(dev);
	struct ieee80211com *ic = &sc->sc_ic;
	int error;
	int txq_i, i;

	sc->sc_dev = dev;
	sc->sc_attached = 1;
	IWM_LOCK_INIT(sc);
	mbufq_init(&sc->sc_snd, ifqmaxlen);
	callout_init_mtx(&sc->sc_watchdog_to, &sc->sc_mtx, 0);
	callout_init_mtx(&sc->sc_led_blink_to, &sc->sc_mtx, 0);
	TASK_INIT(&sc->sc_es_task, 0, iwm_endscan_cb, sc);

	sc->sc_notif_wait = iwm_notification_wait_init(sc);
	if (sc->sc_notif_wait == NULL) {
		device_printf(dev, "failed to init notification wait struct\n");
		goto fail;
	}

	sc->sf_state = IWM_SF_UNINIT;

	/* Init phy db */
	sc->sc_phy_db = iwm_phy_db_init(sc);
	if (!sc->sc_phy_db) {
		device_printf(dev, "Cannot init phy_db\n");
		goto fail;
	}

	/* Set EBS as successful as long as not stated otherwise by the FW. */
	sc->last_ebs_successful = TRUE;

	/* PCI attach */
	error = iwm_pci_attach(dev);
	if (error != 0)
		goto fail;

	sc->sc_wantresp = -1;

	/* Match device id */
	error = iwm_dev_check(dev);
	if (error != 0)
		goto fail;

	sc->sc_hw_rev = IWM_READ(sc, IWM_CSR_HW_REV);
	/*
	 * In the 8000 HW family the format of the 4 bytes of CSR_HW_REV have
	 * changed, and now the revision step also includes bit 0-1 (no more
	 * "dash" value). To keep hw_rev backwards compatible - we'll store it
	 * in the old format.
	 */
	if (sc->cfg->device_family == IWM_DEVICE_FAMILY_8000) {
		int ret;
		uint32_t hw_step;

		sc->sc_hw_rev = (sc->sc_hw_rev & 0xfff0) |
				(IWM_CSR_HW_REV_STEP(sc->sc_hw_rev << 2) << 2);

		if (iwm_prepare_card_hw(sc) != 0) {
			device_printf(dev, "could not initialize hardware\n");
			goto fail;
		}

		/*
		 * In order to recognize C step the driver should read the
		 * chip version id located at the AUX bus MISC address.
		 */
		IWM_SETBITS(sc, IWM_CSR_GP_CNTRL,
			    IWM_CSR_GP_CNTRL_REG_FLAG_INIT_DONE);
		DELAY(2);

		ret = iwm_poll_bit(sc, IWM_CSR_GP_CNTRL,
				   IWM_CSR_GP_CNTRL_REG_FLAG_MAC_CLOCK_READY,
				   IWM_CSR_GP_CNTRL_REG_FLAG_MAC_CLOCK_READY,
				   25000);
		if (!ret) {
			device_printf(sc->sc_dev,
			    "Failed to wake up the nic\n");
			goto fail;
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
			device_printf(sc->sc_dev, "Failed to lock the nic\n");
			goto fail;
		}
	}

	/* special-case 7265D, it has the same PCI IDs. */
	if (sc->cfg == &iwm7265_cfg &&
	    (sc->sc_hw_rev & IWM_CSR_HW_REV_TYPE_MSK) == IWM_CSR_HW_REV_TYPE_7265D) {
		sc->cfg = &iwm7265d_cfg;
	}

	/* Allocate DMA memory for firmware transfers. */
	if ((error = iwm_alloc_fwmem(sc)) != 0) {
		device_printf(dev, "could not allocate memory for firmware\n");
		goto fail;
	}

	/* Allocate "Keep Warm" page. */
	if ((error = iwm_alloc_kw(sc)) != 0) {
		device_printf(dev, "could not allocate keep warm page\n");
		goto fail;
	}

	/* We use ICT interrupts */
	if ((error = iwm_alloc_ict(sc)) != 0) {
		device_printf(dev, "could not allocate ICT table\n");
		goto fail;
	}

	/* Allocate TX scheduler "rings". */
	if ((error = iwm_alloc_sched(sc)) != 0) {
		device_printf(dev, "could not allocate TX scheduler rings\n");
		goto fail;
	}

	/* Allocate TX rings */
	for (txq_i = 0; txq_i < nitems(sc->txq); txq_i++) {
		if ((error = iwm_alloc_tx_ring(sc,
		    &sc->txq[txq_i], txq_i)) != 0) {
			device_printf(dev,
			    "could not allocate TX ring %d\n",
			    txq_i);
			goto fail;
		}
	}

	/* Allocate RX ring. */
	if ((error = iwm_alloc_rx_ring(sc, &sc->rxq)) != 0) {
		device_printf(dev, "could not allocate RX ring\n");
		goto fail;
	}

	/* Clear pending interrupts. */
	IWM_WRITE(sc, IWM_CSR_INT, 0xffffffff);

	ic->ic_softc = sc;
	ic->ic_name = device_get_nameunit(sc->sc_dev);
	ic->ic_phytype = IEEE80211_T_OFDM;	/* not only, but not used */
	ic->ic_opmode = IEEE80211_M_STA;	/* default to BSS mode */

	/* Set device capabilities. */
	ic->ic_caps =
	    IEEE80211_C_STA |
	    IEEE80211_C_WPA |		/* WPA/RSN */
	    IEEE80211_C_WME |
	    IEEE80211_C_PMGT |
	    IEEE80211_C_SHSLOT |	/* short slot time supported */
	    IEEE80211_C_SHPREAMBLE	/* short preamble supported */
//	    IEEE80211_C_BGSCAN		/* capable of bg scanning */
	    ;
	/* Advertise full-offload scanning */
	ic->ic_flags_ext = IEEE80211_FEXT_SCAN_OFFLOAD;
	for (i = 0; i < nitems(sc->sc_phyctxt); i++) {
		sc->sc_phyctxt[i].id = i;
		sc->sc_phyctxt[i].color = 0;
		sc->sc_phyctxt[i].ref = 0;
		sc->sc_phyctxt[i].channel = NULL;
	}

	/* Default noise floor */
	sc->sc_noise = -96;

	/* Max RSSI */
	sc->sc_max_rssi = IWM_MAX_DBM - IWM_MIN_DBM;

#ifdef IWM_DEBUG
	SYSCTL_ADD_INT(device_get_sysctl_ctx(dev),
	    SYSCTL_CHILDREN(device_get_sysctl_tree(dev)), OID_AUTO, "debug",
	    CTLFLAG_RW, &sc->sc_debug, 0, "control debugging");
#endif

	error = iwm_read_firmware(sc);
	if (error) {
		goto fail;
	} else if (sc->sc_fw.fw_fp == NULL) {
		/*
		 * XXX Add a solution for properly deferring firmware load
		 *     during bootup.
		 */
		goto fail;
	} else {
		sc->sc_preinit_hook.ich_func = iwm_preinit;
		sc->sc_preinit_hook.ich_arg = sc;
		if (config_intrhook_establish(&sc->sc_preinit_hook) != 0) {
			device_printf(dev,
			    "config_intrhook_establish failed\n");
			goto fail;
		}
	}

	IWM_DPRINTF(sc, IWM_DEBUG_RESET | IWM_DEBUG_TRACE,
	    "<-%s\n", __func__);

	return 0;

	/* Free allocated memory if something failed during attachment. */
fail:
	iwm_detach_local(sc, 0);

	return ENXIO;
}

static int
iwm_is_valid_ether_addr(uint8_t *addr)
{
	char zero_addr[IEEE80211_ADDR_LEN] = { 0, 0, 0, 0, 0, 0 };

	if ((addr[0] & 1) || IEEE80211_ADDR_EQ(zero_addr, addr))
		return (FALSE);

	return (TRUE);
}

static int
iwm_wme_update(struct ieee80211com *ic)
{
#define IWM_EXP2(x)	((1 << (x)) - 1)	/* CWmin = 2^ECWmin - 1 */
	struct iwm_softc *sc = ic->ic_softc;
	struct chanAccParams chp;
	struct ieee80211vap *vap = TAILQ_FIRST(&ic->ic_vaps);
	struct iwm_vap *ivp = IWM_VAP(vap);
	struct iwm_node *in;
	struct wmeParams tmp[WME_NUM_AC];
	int aci, error;

	if (vap == NULL)
		return (0);

	ieee80211_wme_ic_getparams(ic, &chp);

	IEEE80211_LOCK(ic);
	for (aci = 0; aci < WME_NUM_AC; aci++)
		tmp[aci] = chp.cap_wmeParams[aci];
	IEEE80211_UNLOCK(ic);

	IWM_LOCK(sc);
	for (aci = 0; aci < WME_NUM_AC; aci++) {
		const struct wmeParams *ac = &tmp[aci];
		ivp->queue_params[aci].aifsn = ac->wmep_aifsn;
		ivp->queue_params[aci].cw_min = IWM_EXP2(ac->wmep_logcwmin);
		ivp->queue_params[aci].cw_max = IWM_EXP2(ac->wmep_logcwmax);
		ivp->queue_params[aci].edca_txop =
		    IEEE80211_TXOP_TO_US(ac->wmep_txopLimit);
	}
	ivp->have_wme = TRUE;
	if (ivp->is_uploaded && vap->iv_bss != NULL) {
		in = IWM_NODE(vap->iv_bss);
		if (in->in_assoc) {
			if ((error = iwm_mvm_mac_ctxt_changed(sc, vap)) != 0) {
				device_printf(sc->sc_dev,
				    "%s: failed to update MAC\n", __func__);
			}
		}
	}
	IWM_UNLOCK(sc);

	return (0);
#undef IWM_EXP2
}

static void
iwm_preinit(void *arg)
{
	struct iwm_softc *sc = arg;
	device_t dev = sc->sc_dev;
	struct ieee80211com *ic = &sc->sc_ic;
	int error;

	IWM_DPRINTF(sc, IWM_DEBUG_RESET | IWM_DEBUG_TRACE,
	    "->%s\n", __func__);

	IWM_LOCK(sc);
	if ((error = iwm_start_hw(sc)) != 0) {
		device_printf(dev, "could not initialize hardware\n");
		IWM_UNLOCK(sc);
		goto fail;
	}

	error = iwm_run_init_mvm_ucode(sc, 1);
	iwm_stop_device(sc);
	if (error) {
		IWM_UNLOCK(sc);
		goto fail;
	}
	device_printf(dev,
	    "hw rev 0x%x, fw ver %s, address %s\n",
	    sc->sc_hw_rev & IWM_CSR_HW_REV_TYPE_MSK,
	    sc->sc_fwver, ether_sprintf(sc->nvm_data->hw_addr));

	/* not all hardware can do 5GHz band */
	if (!sc->nvm_data->sku_cap_band_52GHz_enable)
		memset(&ic->ic_sup_rates[IEEE80211_MODE_11A], 0,
		    sizeof(ic->ic_sup_rates[IEEE80211_MODE_11A]));
	IWM_UNLOCK(sc);

	iwm_init_channel_map(ic, IEEE80211_CHAN_MAX, &ic->ic_nchans,
	    ic->ic_channels);

	/*
	 * At this point we've committed - if we fail to do setup,
	 * we now also have to tear down the net80211 state.
	 */
	ieee80211_ifattach(ic);
	ic->ic_vap_create = iwm_vap_create;
	ic->ic_vap_delete = iwm_vap_delete;
	ic->ic_raw_xmit = iwm_raw_xmit;
	ic->ic_node_alloc = iwm_node_alloc;
	ic->ic_scan_start = iwm_scan_start;
	ic->ic_scan_end = iwm_scan_end;
	ic->ic_update_mcast = iwm_update_mcast;
	ic->ic_getradiocaps = iwm_init_channel_map;
	ic->ic_set_channel = iwm_set_channel;
	ic->ic_scan_curchan = iwm_scan_curchan;
	ic->ic_scan_mindwell = iwm_scan_mindwell;
	ic->ic_wme.wme_update = iwm_wme_update;
	ic->ic_parent = iwm_parent;
	ic->ic_transmit = iwm_transmit;
	iwm_radiotap_attach(sc);
	if (bootverbose)
		ieee80211_announce(ic);

	IWM_DPRINTF(sc, IWM_DEBUG_RESET | IWM_DEBUG_TRACE,
	    "<-%s\n", __func__);
	config_intrhook_disestablish(&sc->sc_preinit_hook);

	return;
fail:
	config_intrhook_disestablish(&sc->sc_preinit_hook);
	iwm_detach_local(sc, 0);
}

/*
 * Attach the interface to 802.11 radiotap.
 */
static void
iwm_radiotap_attach(struct iwm_softc *sc)
{
        struct ieee80211com *ic = &sc->sc_ic;

	IWM_DPRINTF(sc, IWM_DEBUG_RESET | IWM_DEBUG_TRACE,
	    "->%s begin\n", __func__);
        ieee80211_radiotap_attach(ic,
            &sc->sc_txtap.wt_ihdr, sizeof(sc->sc_txtap),
                IWM_TX_RADIOTAP_PRESENT,
            &sc->sc_rxtap.wr_ihdr, sizeof(sc->sc_rxtap),
                IWM_RX_RADIOTAP_PRESENT);
	IWM_DPRINTF(sc, IWM_DEBUG_RESET | IWM_DEBUG_TRACE,
	    "->%s end\n", __func__);
}

static struct ieee80211vap *
iwm_vap_create(struct ieee80211com *ic, const char name[IFNAMSIZ], int unit,
    enum ieee80211_opmode opmode, int flags,
    const uint8_t bssid[IEEE80211_ADDR_LEN],
    const uint8_t mac[IEEE80211_ADDR_LEN])
{
	struct iwm_vap *ivp;
	struct ieee80211vap *vap;

	if (!TAILQ_EMPTY(&ic->ic_vaps))         /* only one at a time */
		return NULL;
	ivp = malloc(sizeof(struct iwm_vap), M_80211_VAP, M_WAITOK | M_ZERO);
	vap = &ivp->iv_vap;
	ieee80211_vap_setup(ic, vap, name, unit, opmode, flags, bssid);
	vap->iv_bmissthreshold = 10;            /* override default */
	/* Override with driver methods. */
	ivp->iv_newstate = vap->iv_newstate;
	vap->iv_newstate = iwm_newstate;

	ivp->id = IWM_DEFAULT_MACID;
	ivp->color = IWM_DEFAULT_COLOR;

	ivp->have_wme = FALSE;
	ivp->ps_disabled = FALSE;

	ieee80211_ratectl_init(vap);
	/* Complete setup. */
	ieee80211_vap_attach(vap, iwm_media_change, ieee80211_media_status,
	    mac);
	ic->ic_opmode = opmode;

	return vap;
}

static void
iwm_vap_delete(struct ieee80211vap *vap)
{
	struct iwm_vap *ivp = IWM_VAP(vap);

	ieee80211_ratectl_deinit(vap);
	ieee80211_vap_detach(vap);
	free(ivp, M_80211_VAP);
}

static void
iwm_xmit_queue_drain(struct iwm_softc *sc)
{
	struct mbuf *m;
	struct ieee80211_node *ni;

	while ((m = mbufq_dequeue(&sc->sc_snd)) != NULL) {
		ni = (struct ieee80211_node *)m->m_pkthdr.rcvif;
		ieee80211_free_node(ni);
		m_freem(m);
	}
}

static void
iwm_scan_start(struct ieee80211com *ic)
{
	struct ieee80211vap *vap = TAILQ_FIRST(&ic->ic_vaps);
	struct iwm_softc *sc = ic->ic_softc;
	int error;

	IWM_LOCK(sc);
	if (sc->sc_flags & IWM_FLAG_SCAN_RUNNING) {
		/* This should not be possible */
		device_printf(sc->sc_dev,
		    "%s: Previous scan not completed yet\n", __func__);
	}
	if (fw_has_capa(&sc->sc_fw.ucode_capa, IWM_UCODE_TLV_CAPA_UMAC_SCAN))
		error = iwm_mvm_umac_scan(sc);
	else
		error = iwm_mvm_lmac_scan(sc);
	if (error != 0) {
		device_printf(sc->sc_dev, "could not initiate scan\n");
		IWM_UNLOCK(sc);
		ieee80211_cancel_scan(vap);
	} else {
		sc->sc_flags |= IWM_FLAG_SCAN_RUNNING;
		iwm_led_blink_start(sc);
		IWM_UNLOCK(sc);
	}
}

static void
iwm_scan_end(struct ieee80211com *ic)
{
	struct ieee80211vap *vap = TAILQ_FIRST(&ic->ic_vaps);
	struct iwm_softc *sc = ic->ic_softc;

	IWM_LOCK(sc);
	iwm_led_blink_stop(sc);
	if (vap->iv_state == IEEE80211_S_RUN)
		iwm_mvm_led_enable(sc);
	if (sc->sc_flags & IWM_FLAG_SCAN_RUNNING) {
		/*
		 * Removing IWM_FLAG_SCAN_RUNNING now, is fine because
		 * both iwm_scan_end and iwm_scan_start run in the ic->ic_tq
		 * taskqueue.
		 */
		sc->sc_flags &= ~IWM_FLAG_SCAN_RUNNING;
		iwm_mvm_scan_stop_wait(sc);
	}
	IWM_UNLOCK(sc);

	/*
	 * Make sure we don't race, if sc_es_task is still enqueued here.
	 * This is to make sure that it won't call ieee80211_scan_done
	 * when we have already started the next scan.
	 */
	taskqueue_cancel(ic->ic_tq, &sc->sc_es_task, NULL);
}

static void
iwm_update_mcast(struct ieee80211com *ic)
{
}

static void
iwm_set_channel(struct ieee80211com *ic)
{
}

static void
iwm_scan_curchan(struct ieee80211_scan_state *ss, unsigned long maxdwell)
{
}

static void
iwm_scan_mindwell(struct ieee80211_scan_state *ss)
{
	return;
}

void
iwm_init_task(void *arg1)
{
	struct iwm_softc *sc = arg1;

	IWM_LOCK(sc);
	while (sc->sc_flags & IWM_FLAG_BUSY)
		msleep(&sc->sc_flags, &sc->sc_mtx, 0, "iwmpwr", 0);
	sc->sc_flags |= IWM_FLAG_BUSY;
	iwm_stop(sc);
	if (sc->sc_ic.ic_nrunning > 0)
		iwm_init(sc);
	sc->sc_flags &= ~IWM_FLAG_BUSY;
	wakeup(&sc->sc_flags);
	IWM_UNLOCK(sc);
}

static int
iwm_resume(device_t dev)
{
	struct iwm_softc *sc = device_get_softc(dev);
	int do_reinit = 0;

	/*
	 * We disable the RETRY_TIMEOUT register (0x41) to keep
	 * PCI Tx retries from interfering with C3 CPU state.
	 */
	pci_write_config(dev, PCI_CFG_RETRY_TIMEOUT, 0x00, 1);

	if (!sc->sc_attached)
		return 0;

	iwm_init_task(device_get_softc(dev));

	IWM_LOCK(sc);
	if (sc->sc_flags & IWM_FLAG_SCANNING) {
		sc->sc_flags &= ~IWM_FLAG_SCANNING;
		do_reinit = 1;
	}
	IWM_UNLOCK(sc);

	if (do_reinit)
		ieee80211_resume_all(&sc->sc_ic);

	return 0;
}

static int
iwm_suspend(device_t dev)
{
	int do_stop = 0;
	struct iwm_softc *sc = device_get_softc(dev);

	do_stop = !! (sc->sc_ic.ic_nrunning > 0);

	if (!sc->sc_attached)
		return (0);

	ieee80211_suspend_all(&sc->sc_ic);

	if (do_stop) {
		IWM_LOCK(sc);
		iwm_stop(sc);
		sc->sc_flags |= IWM_FLAG_SCANNING;
		IWM_UNLOCK(sc);
	}

	return (0);
}

static int
iwm_detach_local(struct iwm_softc *sc, int do_net80211)
{
	struct iwm_fw_info *fw = &sc->sc_fw;
	device_t dev = sc->sc_dev;
	int i;

	if (!sc->sc_attached)
		return 0;
	sc->sc_attached = 0;

	if (do_net80211)
		ieee80211_draintask(&sc->sc_ic, &sc->sc_es_task);

	callout_drain(&sc->sc_led_blink_to);
	callout_drain(&sc->sc_watchdog_to);
	iwm_stop_device(sc);
	if (do_net80211) {
		IWM_LOCK(sc);
		iwm_xmit_queue_drain(sc);
		IWM_UNLOCK(sc);
		ieee80211_ifdetach(&sc->sc_ic);
	}

	iwm_phy_db_free(sc->sc_phy_db);
	sc->sc_phy_db = NULL;

	iwm_free_nvm_data(sc->nvm_data);

	/* Free descriptor rings */
	iwm_free_rx_ring(sc, &sc->rxq);
	for (i = 0; i < nitems(sc->txq); i++)
		iwm_free_tx_ring(sc, &sc->txq[i]);

	/* Free firmware */
	if (fw->fw_fp != NULL)
		iwm_fw_info_free(fw);

	/* Free scheduler */
	iwm_dma_contig_free(&sc->sched_dma);
	iwm_dma_contig_free(&sc->ict_dma);
	iwm_dma_contig_free(&sc->kw_dma);
	iwm_dma_contig_free(&sc->fw_dma);

	iwm_free_fw_paging(sc);

	/* Finished with the hardware - detach things */
	iwm_pci_detach(dev);

	if (sc->sc_notif_wait != NULL) {
		iwm_notification_wait_free(sc->sc_notif_wait);
		sc->sc_notif_wait = NULL;
	}

	IWM_LOCK_DESTROY(sc);

	return (0);
}

static int
iwm_detach(device_t dev)
{
	struct iwm_softc *sc = device_get_softc(dev);

	return (iwm_detach_local(sc, 1));
}

static device_method_t iwm_pci_methods[] = {
        /* Device interface */
        DEVMETHOD(device_probe,         iwm_probe),
        DEVMETHOD(device_attach,        iwm_attach),
        DEVMETHOD(device_detach,        iwm_detach),
        DEVMETHOD(device_suspend,       iwm_suspend),
        DEVMETHOD(device_resume,        iwm_resume),

        DEVMETHOD_END
};

static driver_t iwm_pci_driver = {
        "iwm",
        iwm_pci_methods,
        sizeof (struct iwm_softc)
};

static devclass_t iwm_devclass;

DRIVER_MODULE(iwm, pci, iwm_pci_driver, iwm_devclass, NULL, NULL);
MODULE_PNP_INFO("U16:device;P:#;T:vendor=0x8086", pci, iwm_pci_driver,
    iwm_devices, nitems(iwm_devices));
MODULE_DEPEND(iwm, firmware, 1, 1, 1);
MODULE_DEPEND(iwm, pci, 1, 1, 1);
MODULE_DEPEND(iwm, wlan, 1, 1, 1);
