/*	$OpenBSD: if_iwm.c,v 1.39 2015/03/23 00:35:19 jsg Exp $	*/

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
#include <dev/iwm/if_iwm_debug.h>
#include <dev/iwm/if_iwm_notif_wait.h>
#include <dev/iwm/if_iwm_util.h>
#include <dev/iwm/if_iwm_scan.h>

/*
 * BEGIN mvm/scan.c
 */

#define IWM_DENSE_EBS_SCAN_RATIO 5
#define IWM_SPARSE_EBS_SCAN_RATIO 1

static uint16_t
iwm_mvm_scan_rx_chain(struct iwm_softc *sc)
{
	uint16_t rx_chain;
	uint8_t rx_ant;

	rx_ant = iwm_mvm_get_valid_rx_ant(sc);
	rx_chain = rx_ant << IWM_PHY_RX_CHAIN_VALID_POS;
	rx_chain |= rx_ant << IWM_PHY_RX_CHAIN_FORCE_MIMO_SEL_POS;
	rx_chain |= rx_ant << IWM_PHY_RX_CHAIN_FORCE_SEL_POS;
	rx_chain |= 0x1 << IWM_PHY_RX_CHAIN_DRIVER_FORCE_POS;
	return htole16(rx_chain);
}

static uint32_t
iwm_mvm_scan_rxon_flags(struct ieee80211_channel *c)
{
	if (IEEE80211_IS_CHAN_2GHZ(c))
		return htole32(IWM_PHY_BAND_24);
	else
		return htole32(IWM_PHY_BAND_5);
}

static uint32_t
iwm_mvm_scan_rate_n_flags(struct iwm_softc *sc, int flags, int no_cck)
{
	uint32_t tx_ant;
	int i, ind;

	for (i = 0, ind = sc->sc_scan_last_antenna;
	    i < IWM_RATE_MCS_ANT_NUM; i++) {
		ind = (ind + 1) % IWM_RATE_MCS_ANT_NUM;
		if (iwm_mvm_get_valid_tx_ant(sc) & (1 << ind)) {
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

static inline boolean_t
iwm_mvm_rrm_scan_needed(struct iwm_softc *sc)
{
	/* require rrm scan whenever the fw supports it */
	return fw_has_capa(&sc->sc_fw.ucode_capa,
			   IWM_UCODE_TLV_CAPA_DS_PARAM_SET_IE_SUPPORT);
}

#ifdef IWM_DEBUG
static const char *
iwm_mvm_ebs_status_str(enum iwm_scan_ebs_status status)
{
	switch (status) {
	case IWM_SCAN_EBS_SUCCESS:
		return "successful";
	case IWM_SCAN_EBS_INACTIVE:
		return "inactive";
	case IWM_SCAN_EBS_FAILED:
	case IWM_SCAN_EBS_CHAN_NOT_FOUND:
	default:
		return "failed";
	}
}

static const char *
iwm_mvm_offload_status_str(enum iwm_scan_offload_complete_status status)
{
	return (status == IWM_SCAN_OFFLOAD_ABORTED) ? "aborted" : "completed";
}
#endif

void
iwm_mvm_rx_lmac_scan_complete_notif(struct iwm_softc *sc,
    struct iwm_rx_packet *pkt)
{
	struct iwm_periodic_scan_complete *scan_notif = (void *)pkt->data;

	/* If this happens, the firmware has mistakenly sent an LMAC
	 * notification during UMAC scans -- warn and ignore it.
	 */
	if (fw_has_capa(&sc->sc_fw.ucode_capa, IWM_UCODE_TLV_CAPA_UMAC_SCAN)) {
		device_printf(sc->sc_dev,
		    "%s: Mistakenly got LMAC notification during UMAC scan\n",
		    __func__);
		return;
	}

	IWM_DPRINTF(sc, IWM_DEBUG_SCAN, "Regular scan %s, EBS status %s (FW)\n",
	    iwm_mvm_offload_status_str(scan_notif->status),
	    iwm_mvm_ebs_status_str(scan_notif->ebs_status));

	sc->last_ebs_successful =
			scan_notif->ebs_status == IWM_SCAN_EBS_SUCCESS ||
			scan_notif->ebs_status == IWM_SCAN_EBS_INACTIVE;

}

void
iwm_mvm_rx_umac_scan_complete_notif(struct iwm_softc *sc,
    struct iwm_rx_packet *pkt)
{
	struct iwm_umac_scan_complete *notif = (void *)pkt->data;

	IWM_DPRINTF(sc, IWM_DEBUG_SCAN,
	    "Scan completed, uid %u, status %s, EBS status %s\n",
	    le32toh(notif->uid),
	    iwm_mvm_offload_status_str(notif->status),
	    iwm_mvm_ebs_status_str(notif->ebs_status));

	if (notif->ebs_status != IWM_SCAN_EBS_SUCCESS &&
	    notif->ebs_status != IWM_SCAN_EBS_INACTIVE)
		sc->last_ebs_successful = FALSE;
}

static int
iwm_mvm_scan_skip_channel(struct ieee80211_channel *c)
{
	if (IEEE80211_IS_CHAN_2GHZ(c) && IEEE80211_IS_CHAN_B(c))
		return 0;
	else if (IEEE80211_IS_CHAN_5GHZ(c) && IEEE80211_IS_CHAN_A(c))
		return 0;
	else
		return 1;
}

static uint8_t
iwm_mvm_lmac_scan_fill_channels(struct iwm_softc *sc,
    struct iwm_scan_channel_cfg_lmac *chan, int n_ssids)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ieee80211_scan_state *ss = ic->ic_scan;
	struct ieee80211_channel *c;
	uint8_t nchan;
	int j;

	for (nchan = j = 0;
	    j < ss->ss_last && nchan < sc->sc_fw.ucode_capa.n_scan_channels;
	    j++) {
		c = ss->ss_chans[j];
		/*
		 * Catch other channels, in case we have 900MHz channels or
		 * something in the chanlist.
		 */
		if (!IEEE80211_IS_CHAN_2GHZ(c) && !IEEE80211_IS_CHAN_5GHZ(c)) {
			IWM_DPRINTF(sc, IWM_DEBUG_RESET | IWM_DEBUG_EEPROM,
			    "%s: skipping channel (freq=%d, ieee=%d, flags=0x%08x)\n",
			    __func__, c->ic_freq, c->ic_ieee, c->ic_flags);
			continue;
		}

		IWM_DPRINTF(sc, IWM_DEBUG_RESET | IWM_DEBUG_EEPROM,
		    "Adding channel %d (%d Mhz) to the list\n",
		    nchan, c->ic_freq);
		chan->channel_num = htole16(ieee80211_mhz2ieee(c->ic_freq, 0));
		chan->iter_count = htole16(1);
		chan->iter_interval = htole32(0);
		chan->flags = htole32(IWM_UNIFIED_SCAN_CHANNEL_PARTIAL);
		chan->flags |= htole32(IWM_SCAN_CHANNEL_NSSIDS(n_ssids));
		/* XXX IEEE80211_SCAN_NOBCAST flag is never set. */
		if (!IEEE80211_IS_CHAN_PASSIVE(c) &&
		    (!(ss->ss_flags & IEEE80211_SCAN_NOBCAST) || n_ssids != 0))
			chan->flags |= htole32(IWM_SCAN_CHANNEL_TYPE_ACTIVE);
		chan++;
		nchan++;
	}

	return nchan;
}

static uint8_t
iwm_mvm_umac_scan_fill_channels(struct iwm_softc *sc,
    struct iwm_scan_channel_cfg_umac *chan, int n_ssids)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ieee80211_scan_state *ss = ic->ic_scan;
	struct ieee80211_channel *c;
	uint8_t nchan;
	int j;

	for (nchan = j = 0;
	    j < ss->ss_last && nchan < sc->sc_fw.ucode_capa.n_scan_channels;
	    j++) {
		c = ss->ss_chans[j];
		/*
		 * Catch other channels, in case we have 900MHz channels or
		 * something in the chanlist.
		 */
		if (!IEEE80211_IS_CHAN_2GHZ(c) && !IEEE80211_IS_CHAN_5GHZ(c)) {
			IWM_DPRINTF(sc, IWM_DEBUG_RESET | IWM_DEBUG_EEPROM,
			    "%s: skipping channel (freq=%d, ieee=%d, flags=0x%08x)\n",
			    __func__, c->ic_freq, c->ic_ieee, c->ic_flags);
			continue;
		}

		IWM_DPRINTF(sc, IWM_DEBUG_RESET | IWM_DEBUG_EEPROM,
		    "Adding channel %d (%d Mhz) to the list\n",
		    nchan, c->ic_freq);
		chan->channel_num = ieee80211_mhz2ieee(c->ic_freq, 0);
		chan->iter_count = 1;
		chan->iter_interval = htole16(0);
		chan->flags = htole32(IWM_SCAN_CHANNEL_UMAC_NSSIDS(n_ssids));
		chan++;
		nchan++;
	}

	return nchan;
}

static int
iwm_mvm_fill_probe_req(struct iwm_softc *sc, struct iwm_scan_probe_req *preq)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ieee80211vap *vap = TAILQ_FIRST(&ic->ic_vaps);
	struct ieee80211_frame *wh = (struct ieee80211_frame *)preq->buf;
	struct ieee80211_rateset *rs;
	size_t remain = sizeof(preq->buf);
	uint8_t *frm, *pos;

	memset(preq, 0, sizeof(*preq));

	/* Ensure enough space for header and SSID IE. */
	if (remain < sizeof(*wh) + 2)
		return ENOBUFS;

	/*
	 * Build a probe request frame.  Most of the following code is a
	 * copy & paste of what is done in net80211.
	 */
	wh->i_fc[0] = IEEE80211_FC0_VERSION_0 | IEEE80211_FC0_TYPE_MGT |
	    IEEE80211_FC0_SUBTYPE_PROBE_REQ;
	wh->i_fc[1] = IEEE80211_FC1_DIR_NODS;
	IEEE80211_ADDR_COPY(wh->i_addr1, ieee80211broadcastaddr);
	IEEE80211_ADDR_COPY(wh->i_addr2, vap ? vap->iv_myaddr : ic->ic_macaddr);
	IEEE80211_ADDR_COPY(wh->i_addr3, ieee80211broadcastaddr);
	*(uint16_t *)&wh->i_dur[0] = 0; /* filled by HW */
	*(uint16_t *)&wh->i_seq[0] = 0; /* filled by HW */

	frm = (uint8_t *)(wh + 1);
	frm = ieee80211_add_ssid(frm, NULL, 0);

	/* Tell the firmware where the MAC header is. */
	preq->mac_header.offset = 0;
	preq->mac_header.len = htole16(frm - (uint8_t *)wh);
	remain -= frm - (uint8_t *)wh;

	/* Fill in 2GHz IEs and tell firmware where they are. */
	rs = &ic->ic_sup_rates[IEEE80211_MODE_11G];
	if (rs->rs_nrates > IEEE80211_RATE_SIZE) {
		if (remain < 4 + rs->rs_nrates)
			return ENOBUFS;
	} else if (remain < 2 + rs->rs_nrates) {
		return ENOBUFS;
	}
	preq->band_data[0].offset = htole16(frm - (uint8_t *)wh);
	pos = frm;
	frm = ieee80211_add_rates(frm, rs);
	if (rs->rs_nrates > IEEE80211_RATE_SIZE)
		frm = ieee80211_add_xrates(frm, rs);
	preq->band_data[0].len = htole16(frm - pos);
	remain -= frm - pos;

	if (iwm_mvm_rrm_scan_needed(sc)) {
		if (remain < 3)
			return ENOBUFS;
		*frm++ = IEEE80211_ELEMID_DSPARMS;
		*frm++ = 1;
		*frm++ = 0;
		remain -= 3;
	}

	if (sc->nvm_data->sku_cap_band_52GHz_enable) {
		/* Fill in 5GHz IEs. */
		rs = &ic->ic_sup_rates[IEEE80211_MODE_11A];
		if (rs->rs_nrates > IEEE80211_RATE_SIZE) {
			if (remain < 4 + rs->rs_nrates)
				return ENOBUFS;
		} else if (remain < 2 + rs->rs_nrates) {
			return ENOBUFS;
		}
		preq->band_data[1].offset = htole16(frm - (uint8_t *)wh);
		pos = frm;
		frm = ieee80211_add_rates(frm, rs);
		if (rs->rs_nrates > IEEE80211_RATE_SIZE)
			frm = ieee80211_add_xrates(frm, rs);
		preq->band_data[1].len = htole16(frm - pos);
		remain -= frm - pos;
	}

	/* Send 11n IEs on both 2GHz and 5GHz bands. */
	preq->common_data.offset = htole16(frm - (uint8_t *)wh);
	pos = frm;
#if 0
	if (ic->ic_flags & IEEE80211_F_HTON) {
		if (remain < 28)
			return ENOBUFS;
		frm = ieee80211_add_htcaps(frm, ic);
		/* XXX add WME info? */
	}
#endif
	preq->common_data.len = htole16(frm - pos);

	return 0;
}

int
iwm_mvm_config_umac_scan(struct iwm_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ieee80211vap *vap = TAILQ_FIRST(&ic->ic_vaps);

	struct iwm_scan_config *scan_config;
	int ret, j, nchan;
	size_t cmd_size;
	struct ieee80211_channel *c;
	struct iwm_host_cmd hcmd = {
		.id = iwm_cmd_id(IWM_SCAN_CFG_CMD, IWM_ALWAYS_LONG_GROUP, 0),
		.flags = IWM_CMD_SYNC,
	};
	static const uint32_t rates = (IWM_SCAN_CONFIG_RATE_1M |
	    IWM_SCAN_CONFIG_RATE_2M | IWM_SCAN_CONFIG_RATE_5M |
	    IWM_SCAN_CONFIG_RATE_11M | IWM_SCAN_CONFIG_RATE_6M |
	    IWM_SCAN_CONFIG_RATE_9M | IWM_SCAN_CONFIG_RATE_12M |
	    IWM_SCAN_CONFIG_RATE_18M | IWM_SCAN_CONFIG_RATE_24M |
	    IWM_SCAN_CONFIG_RATE_36M | IWM_SCAN_CONFIG_RATE_48M |
	    IWM_SCAN_CONFIG_RATE_54M);

	cmd_size = sizeof(*scan_config) + sc->sc_fw.ucode_capa.n_scan_channels;

	scan_config = malloc(cmd_size, M_DEVBUF, M_NOWAIT | M_ZERO);
	if (scan_config == NULL)
		return ENOMEM;

	scan_config->tx_chains = htole32(iwm_mvm_get_valid_tx_ant(sc));
	scan_config->rx_chains = htole32(iwm_mvm_get_valid_rx_ant(sc));
	scan_config->legacy_rates = htole32(rates |
	    IWM_SCAN_CONFIG_SUPPORTED_RATE(rates));

	/* These timings correspond to iwlwifi's UNASSOC scan. */
	scan_config->dwell_active = 10;
	scan_config->dwell_passive = 110;
	scan_config->dwell_fragmented = 44;
	scan_config->dwell_extended = 90;
	scan_config->out_of_channel_time = htole32(0);
	scan_config->suspend_time = htole32(0);

	IEEE80211_ADDR_COPY(scan_config->mac_addr,
	    vap ? vap->iv_myaddr : ic->ic_macaddr);

	scan_config->bcast_sta_id = sc->sc_aux_sta.sta_id;
	scan_config->channel_flags = IWM_CHANNEL_FLAG_EBS |
	    IWM_CHANNEL_FLAG_ACCURATE_EBS | IWM_CHANNEL_FLAG_EBS_ADD |
	    IWM_CHANNEL_FLAG_PRE_SCAN_PASSIVE2ACTIVE;

	for (nchan = j = 0;
	    j < ic->ic_nchans && nchan < sc->sc_fw.ucode_capa.n_scan_channels;
	    j++) {
		c = &ic->ic_channels[j];
		/* For 2GHz, only populate 11b channels */
		/* For 5GHz, only populate 11a channels */
		/*
		 * Catch other channels, in case we have 900MHz channels or
		 * something in the chanlist.
		 */
		if (iwm_mvm_scan_skip_channel(c))
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

	IWM_DPRINTF(sc, IWM_DEBUG_SCAN, "Sending UMAC scan config\n");

	ret = iwm_send_cmd(sc, &hcmd);
	if (!ret)
		IWM_DPRINTF(sc, IWM_DEBUG_SCAN,
		    "UMAC scan config was sent successfully\n");

	free(scan_config, M_DEVBUF);
	return ret;
}

static boolean_t
iwm_mvm_scan_use_ebs(struct iwm_softc *sc)
{
	const struct iwm_ucode_capabilities *capa = &sc->sc_fw.ucode_capa;

	/* We can only use EBS if:
	 *	1. the feature is supported;
	 *	2. the last EBS was successful;
	 *	3. if only single scan, the single scan EBS API is supported;
	 *	4. it's not a p2p find operation.
	 */
	return ((capa->flags & IWM_UCODE_TLV_FLAGS_EBS_SUPPORT) &&
		sc->last_ebs_successful);
}

int
iwm_mvm_umac_scan(struct iwm_softc *sc)
{
	struct iwm_host_cmd hcmd = {
		.id = iwm_cmd_id(IWM_SCAN_REQ_UMAC, IWM_ALWAYS_LONG_GROUP, 0),
		.len = { 0, },
		.data = { NULL, },
		.flags = IWM_CMD_SYNC,
	};
	struct ieee80211_scan_state *ss = sc->sc_ic.ic_scan;
	struct iwm_scan_req_umac *req;
	struct iwm_scan_req_umac_tail *tail;
	size_t req_len;
	uint8_t i, nssid;
	int ret;

	req_len = sizeof(struct iwm_scan_req_umac) +
	    (sizeof(struct iwm_scan_channel_cfg_umac) *
	    sc->sc_fw.ucode_capa.n_scan_channels) +
	    sizeof(struct iwm_scan_req_umac_tail);
	if (req_len > IWM_MAX_CMD_PAYLOAD_SIZE)
		return ENOMEM;
	req = malloc(req_len, M_DEVBUF, M_NOWAIT | M_ZERO);
	if (req == NULL)
		return ENOMEM;

	hcmd.len[0] = (uint16_t)req_len;
	hcmd.data[0] = (void *)req;

	IWM_DPRINTF(sc, IWM_DEBUG_SCAN, "Handling ieee80211 scan request\n");

	/* These timings correspond to iwlwifi's UNASSOC scan. */
	req->active_dwell = 10;
	req->passive_dwell = 110;
	req->fragmented_dwell = 44;
	req->extended_dwell = 90;
	req->max_out_time = 0;
	req->suspend_time = 0;

	req->scan_priority = htole32(IWM_SCAN_PRIORITY_HIGH);
	req->ooc_priority = htole32(IWM_SCAN_PRIORITY_HIGH);

	nssid = MIN(ss->ss_nssid, IWM_PROBE_OPTION_MAX);
	req->n_channels = iwm_mvm_umac_scan_fill_channels(sc,
	    (struct iwm_scan_channel_cfg_umac *)req->data, nssid);

	req->general_flags = htole32(IWM_UMAC_SCAN_GEN_FLAGS_PASS_ALL |
	    IWM_UMAC_SCAN_GEN_FLAGS_ITER_COMPLETE |
	    IWM_UMAC_SCAN_GEN_FLAGS_EXTENDED_DWELL);

	tail = (void *)((char *)&req->data +
		sizeof(struct iwm_scan_channel_cfg_umac) *
			sc->sc_fw.ucode_capa.n_scan_channels);

	/* Check if we're doing an active directed scan. */
	for (i = 0; i < nssid; i++) {
		tail->direct_scan[i].id = IEEE80211_ELEMID_SSID;
		tail->direct_scan[i].len = MIN(ss->ss_ssid[i].len,
		    IEEE80211_NWID_LEN);
		memcpy(tail->direct_scan[i].ssid, ss->ss_ssid[i].ssid,
		    tail->direct_scan[i].len);
		/* XXX debug */
	}
	if (nssid != 0) {
		req->general_flags |=
		    htole32(IWM_UMAC_SCAN_GEN_FLAGS_PRE_CONNECT);
	} else
		req->general_flags |= htole32(IWM_UMAC_SCAN_GEN_FLAGS_PASSIVE);

	if (iwm_mvm_scan_use_ebs(sc))
		req->channel_flags = IWM_SCAN_CHANNEL_FLAG_EBS |
				     IWM_SCAN_CHANNEL_FLAG_EBS_ACCURATE |
				     IWM_SCAN_CHANNEL_FLAG_CACHE_ADD;

	if (iwm_mvm_rrm_scan_needed(sc))
		req->general_flags |=
		    htole32(IWM_UMAC_SCAN_GEN_FLAGS_RRM_ENABLED);

	ret = iwm_mvm_fill_probe_req(sc, &tail->preq);
	if (ret) {
		free(req, M_DEVBUF);
		return ret;
	}

	/* Specify the scan plan: We'll do one iteration. */
	tail->schedule[0].interval = 0;
	tail->schedule[0].iter_count = 1;

	ret = iwm_send_cmd(sc, &hcmd);
	if (!ret)
		IWM_DPRINTF(sc, IWM_DEBUG_SCAN,
		    "Scan request was sent successfully\n");
	free(req, M_DEVBUF);
	return ret;
}

int
iwm_mvm_lmac_scan(struct iwm_softc *sc)
{
	struct iwm_host_cmd hcmd = {
		.id = IWM_SCAN_OFFLOAD_REQUEST_CMD,
		.len = { 0, },
		.data = { NULL, },
		.flags = IWM_CMD_SYNC,
	};
	struct ieee80211_scan_state *ss = sc->sc_ic.ic_scan;
	struct iwm_scan_req_lmac *req;
	size_t req_len;
	uint8_t i, nssid;
	int ret;

	IWM_DPRINTF(sc, IWM_DEBUG_SCAN,
	    "Handling ieee80211 scan request\n");

	req_len = sizeof(struct iwm_scan_req_lmac) +
	    (sizeof(struct iwm_scan_channel_cfg_lmac) *
	    sc->sc_fw.ucode_capa.n_scan_channels) + sizeof(struct iwm_scan_probe_req);
	if (req_len > IWM_MAX_CMD_PAYLOAD_SIZE)
		return ENOMEM;
	req = malloc(req_len, M_DEVBUF, M_NOWAIT | M_ZERO);
	if (req == NULL)
		return ENOMEM;

	hcmd.len[0] = (uint16_t)req_len;
	hcmd.data[0] = (void *)req;

	/* These timings correspond to iwlwifi's UNASSOC scan. */
	req->active_dwell = 10;
	req->passive_dwell = 110;
	req->fragmented_dwell = 44;
	req->extended_dwell = 90;
	req->max_out_time = 0;
	req->suspend_time = 0;

	req->scan_prio = htole32(IWM_SCAN_PRIORITY_HIGH);
	req->rx_chain_select = iwm_mvm_scan_rx_chain(sc);
	req->iter_num = htole32(1);
	req->delay = 0;

	req->scan_flags = htole32(IWM_MVM_LMAC_SCAN_FLAG_PASS_ALL |
	    IWM_MVM_LMAC_SCAN_FLAG_ITER_COMPLETE |
	    IWM_MVM_LMAC_SCAN_FLAG_EXTENDED_DWELL);
	if (iwm_mvm_rrm_scan_needed(sc))
		req->scan_flags |= htole32(IWM_MVM_LMAC_SCAN_FLAGS_RRM_ENABLED);

	req->flags = iwm_mvm_scan_rxon_flags(sc->sc_ic.ic_scan->ss_chans[0]);

	req->filter_flags =
	    htole32(IWM_MAC_FILTER_ACCEPT_GRP | IWM_MAC_FILTER_IN_BEACON);

	/* Tx flags 2 GHz. */
	req->tx_cmd[0].tx_flags = htole32(IWM_TX_CMD_FLG_SEQ_CTL |
	    IWM_TX_CMD_FLG_BT_DIS);
	req->tx_cmd[0].rate_n_flags =
	    iwm_mvm_scan_rate_n_flags(sc, IEEE80211_CHAN_2GHZ, 1/*XXX*/);
	req->tx_cmd[0].sta_id = sc->sc_aux_sta.sta_id;

	/* Tx flags 5 GHz. */
	req->tx_cmd[1].tx_flags = htole32(IWM_TX_CMD_FLG_SEQ_CTL |
	    IWM_TX_CMD_FLG_BT_DIS);
	req->tx_cmd[1].rate_n_flags =
	    iwm_mvm_scan_rate_n_flags(sc, IEEE80211_CHAN_5GHZ, 1/*XXX*/);
	req->tx_cmd[1].sta_id = sc->sc_aux_sta.sta_id;

	/* Check if we're doing an active directed scan. */
	nssid = MIN(ss->ss_nssid, IWM_PROBE_OPTION_MAX);
	for (i = 0; i < nssid; i++) {
		req->direct_scan[i].id = IEEE80211_ELEMID_SSID;
		req->direct_scan[i].len = MIN(ss->ss_ssid[i].len,
		    IEEE80211_NWID_LEN);
		memcpy(req->direct_scan[i].ssid, ss->ss_ssid[i].ssid,
		    req->direct_scan[i].len);
		/* XXX debug */
	}
	if (nssid != 0) {
		req->scan_flags |=
		    htole32(IWM_MVM_LMAC_SCAN_FLAG_PRE_CONNECTION);
	} else
		req->scan_flags |= htole32(IWM_MVM_LMAC_SCAN_FLAG_PASSIVE);

	req->n_channels = iwm_mvm_lmac_scan_fill_channels(sc,
	    (struct iwm_scan_channel_cfg_lmac *)req->data, nssid);

	ret = iwm_mvm_fill_probe_req(sc,
			    (struct iwm_scan_probe_req *)(req->data +
			    (sizeof(struct iwm_scan_channel_cfg_lmac) *
			    sc->sc_fw.ucode_capa.n_scan_channels)));
	if (ret) {
		free(req, M_DEVBUF);
		return ret;
	}

	/* Specify the scan plan: We'll do one iteration. */
	req->schedule[0].iterations = 1;
	req->schedule[0].full_scan_mul = 1;

	if (iwm_mvm_scan_use_ebs(sc)) {
		req->channel_opt[0].flags =
			htole16(IWM_SCAN_CHANNEL_FLAG_EBS |
				IWM_SCAN_CHANNEL_FLAG_EBS_ACCURATE |
				IWM_SCAN_CHANNEL_FLAG_CACHE_ADD);
		req->channel_opt[0].non_ebs_ratio =
			htole16(IWM_DENSE_EBS_SCAN_RATIO);
		req->channel_opt[1].flags =
			htole16(IWM_SCAN_CHANNEL_FLAG_EBS |
				IWM_SCAN_CHANNEL_FLAG_EBS_ACCURATE |
				IWM_SCAN_CHANNEL_FLAG_CACHE_ADD);
		req->channel_opt[1].non_ebs_ratio =
			htole16(IWM_SPARSE_EBS_SCAN_RATIO);
	}

	ret = iwm_send_cmd(sc, &hcmd);
	if (!ret) {
		IWM_DPRINTF(sc, IWM_DEBUG_SCAN,
		    "Scan request was sent successfully\n");
	}
	free(req, M_DEVBUF);
	return ret;
}

static int
iwm_mvm_lmac_scan_abort(struct iwm_softc *sc)
{
	int ret;
	struct iwm_host_cmd hcmd = {
		.id = IWM_SCAN_OFFLOAD_ABORT_CMD,
		.len = { 0, },
		.data = { NULL, },
		.flags = IWM_CMD_SYNC,
	};
	uint32_t status;

	ret = iwm_mvm_send_cmd_status(sc, &hcmd, &status);
	if (ret)
		return ret;

	if (status != IWM_CAN_ABORT_STATUS) {
		/*
		 * The scan abort will return 1 for success or
		 * 2 for "failure".  A failure condition can be
		 * due to simply not being in an active scan which
		 * can occur if we send the scan abort before the
		 * microcode has notified us that a scan is completed.
		 */
		IWM_DPRINTF(sc, IWM_DEBUG_SCAN,
		    "SCAN OFFLOAD ABORT ret %d.\n", status);
		ret = ENOENT;
	}

	return ret;
}

static int
iwm_mvm_umac_scan_abort(struct iwm_softc *sc)
{
	struct iwm_umac_scan_abort cmd = {};
	int uid, ret;

	uid = 0;
	cmd.uid = htole32(uid);

	IWM_DPRINTF(sc, IWM_DEBUG_SCAN, "Sending scan abort, uid %u\n", uid);

	ret = iwm_mvm_send_cmd_pdu(sc,
				   iwm_cmd_id(IWM_SCAN_ABORT_UMAC,
					      IWM_ALWAYS_LONG_GROUP, 0),
				   0, sizeof(cmd), &cmd);

	return ret;
}

int
iwm_mvm_scan_stop_wait(struct iwm_softc *sc)
{
	struct iwm_notification_wait wait_scan_done;
	static const uint16_t scan_done_notif[] = { IWM_SCAN_COMPLETE_UMAC,
						   IWM_SCAN_OFFLOAD_COMPLETE, };
	int ret;

	iwm_init_notification_wait(sc->sc_notif_wait, &wait_scan_done,
				   scan_done_notif, nitems(scan_done_notif),
				   NULL, NULL);

	IWM_DPRINTF(sc, IWM_DEBUG_SCAN, "Preparing to stop scan\n");

	if (fw_has_capa(&sc->sc_fw.ucode_capa, IWM_UCODE_TLV_CAPA_UMAC_SCAN))
		ret = iwm_mvm_umac_scan_abort(sc);
	else
		ret = iwm_mvm_lmac_scan_abort(sc);

	if (ret) {
		IWM_DPRINTF(sc, IWM_DEBUG_SCAN, "couldn't stop scan\n");
		iwm_remove_notification(sc->sc_notif_wait, &wait_scan_done);
		return ret;
	}

	IWM_UNLOCK(sc);
	ret = iwm_wait_notification(sc->sc_notif_wait, &wait_scan_done, hz);
	IWM_LOCK(sc);

	return ret;
}
