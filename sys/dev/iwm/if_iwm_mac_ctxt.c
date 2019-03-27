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
#include <dev/iwm/if_iwm_util.h>
#include <dev/iwm/if_iwm_mac_ctxt.h>

/*
 * BEGIN mvm/mac-ctxt.c
 */

const uint8_t iwm_mvm_ac_to_tx_fifo[] = {
	IWM_MVM_TX_FIFO_BE,
	IWM_MVM_TX_FIFO_BK,
	IWM_MVM_TX_FIFO_VI,
	IWM_MVM_TX_FIFO_VO,
};

static void
iwm_mvm_ack_rates(struct iwm_softc *sc, int is2ghz,
	int *cck_rates, int *ofdm_rates, struct iwm_node *in)
{
	int lowest_present_ofdm = 100;
	int lowest_present_cck = 100;
	uint8_t cck = 0;
	uint8_t ofdm = 0;
	int i;
	struct ieee80211_rateset *rs = &in->in_ni.ni_rates;

	if (is2ghz) {
		for (i = IWM_FIRST_CCK_RATE; i <= IWM_LAST_CCK_RATE; i++) {
			if ((iwm_ridx2rate(rs, i) & IEEE80211_RATE_BASIC) == 0)
				continue;
			cck |= (1 << i);
			if (lowest_present_cck > i)
				lowest_present_cck = i;
		}
	}
	for (i = IWM_FIRST_OFDM_RATE; i <= IWM_LAST_NON_HT_RATE; i++) {
		if ((iwm_ridx2rate(rs, i) & IEEE80211_RATE_BASIC) == 0)
			continue;
		ofdm |= (1 << (i - IWM_FIRST_OFDM_RATE));
		if (lowest_present_ofdm > i)
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

static void
iwm_mvm_mac_ctxt_cmd_common(struct iwm_softc *sc, struct iwm_node *in,
	struct iwm_mac_ctx_cmd *cmd, uint32_t action)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ieee80211vap *vap = TAILQ_FIRST(&ic->ic_vaps);
	struct ieee80211_node *ni = vap->iv_bss;
	struct iwm_vap *ivp = IWM_VAP(vap);
	int cck_ack_rates, ofdm_ack_rates;
	int i;
	int is2ghz;

	/*
	 * id is the MAC address ID - something to do with MAC filtering.
	 * color - not sure.
	 *
	 * These are both functions of the vap, not of the node.
	 * So, for now, hard-code both to 0 (default).
	 */
	cmd->id_and_color = htole32(IWM_FW_CMD_ID_AND_COLOR(ivp->id,
	    ivp->color));
	cmd->action = htole32(action);

	cmd->mac_type = htole32(IWM_FW_MAC_TYPE_BSS_STA);

	/*
	 * The TSF ID is one of four TSF tracking resources in the firmware.
	 * Read the iwlwifi/mvm code for more details.
	 *
	 * For now, just hard-code it to TSF tracking ID 0; we only support
	 * a single STA mode VAP.
	 *
	 * It's per-vap, not per-node.
	 */
	cmd->tsf_id = htole32(IWM_DEFAULT_TSFID);

	IEEE80211_ADDR_COPY(cmd->node_addr, vap->iv_myaddr);

	/*
	 * XXX should we error out if in_assoc is 1 and ni == NULL?
	 */
#if 0
	if (in->in_assoc) {
		IEEE80211_ADDR_COPY(cmd->bssid_addr, ni->ni_bssid);
	} else {
		/* eth broadcast address */
		IEEE80211_ADDR_COPY(cmd->bssid_addr, ieee80211broadcastaddr);
	}
#else
	/*
	 * XXX This workaround makes the firmware behave more correctly once
	 *     we are associated, regularly giving us statistics notifications,
	 *     as well as signaling missed beacons to us.
	 *     Since we only call iwm_mvm_mac_ctxt_add() and
	 *     iwm_mvm_mac_ctxt_changed() when already authenticating or
	 *     associating, ni->ni_bssid should always make sense here.
	 */
	if (ivp->iv_auth) {
		IEEE80211_ADDR_COPY(cmd->bssid_addr, ni->ni_bssid);
	} else {
		/* XXX Or maybe all zeroes address? */
		IEEE80211_ADDR_COPY(cmd->bssid_addr, ieee80211broadcastaddr);
	}
#endif

	/*
	 * Default to 2ghz if no node information is given.
	 */
	if (in && in->in_ni.ni_chan != IEEE80211_CHAN_ANYC) {
		is2ghz = !! IEEE80211_IS_CHAN_2GHZ(in->in_ni.ni_chan);
	} else {
		is2ghz = 1;
	}
	iwm_mvm_ack_rates(sc, is2ghz, &cck_ack_rates, &ofdm_ack_rates, in);
	cmd->cck_rates = htole32(cck_ack_rates);
	cmd->ofdm_rates = htole32(ofdm_ack_rates);

	cmd->cck_short_preamble
	    = htole32((ic->ic_flags & IEEE80211_F_SHPREAMBLE)
	      ? IWM_MAC_FLG_SHORT_PREAMBLE : 0);
	cmd->short_slot
	    = htole32((ic->ic_flags & IEEE80211_F_SHSLOT)
	      ? IWM_MAC_FLG_SHORT_SLOT : 0);

	/*
	 * XXX TODO: if we're doing QOS..
	 * cmd->qos_flags |= cpu_to_le32(MAC_QOS_FLG_UPDATE_EDCA)
	 */

	for (i = 0; i < WME_NUM_AC; i++) {
		uint8_t txf = iwm_mvm_ac_to_tx_fifo[i];

		cmd->ac[txf].cw_min = htole16(ivp->queue_params[i].cw_min);
		cmd->ac[txf].cw_max = htole16(ivp->queue_params[i].cw_max);
		cmd->ac[txf].edca_txop =
		    htole16(ivp->queue_params[i].edca_txop);
		cmd->ac[txf].aifsn = ivp->queue_params[i].aifsn;
		cmd->ac[txf].fifos_mask = (1 << txf);
	}

	if (ivp->have_wme)
		cmd->qos_flags |= htole32(IWM_MAC_QOS_FLG_UPDATE_EDCA);

	if (ic->ic_flags & IEEE80211_F_USEPROT)
		cmd->protection_flags |= htole32(IWM_MAC_PROT_FLG_TGG_PROTECT);

	cmd->filter_flags = htole32(IWM_MAC_FILTER_ACCEPT_GRP);
}

static int
iwm_mvm_mac_ctxt_send_cmd(struct iwm_softc *sc, struct iwm_mac_ctx_cmd *cmd)
{
	int ret = iwm_mvm_send_cmd_pdu(sc, IWM_MAC_CONTEXT_CMD, IWM_CMD_SYNC,
				       sizeof(*cmd), cmd);
	if (ret)
		device_printf(sc->sc_dev,
		    "%s: Failed to send MAC context (action:%d): %d\n",
		    __func__, le32toh(cmd->action), ret);
	return ret;
}

/*
 * Fill the specific data for mac context of type station or p2p client
 */
static void
iwm_mvm_mac_ctxt_cmd_fill_sta(struct iwm_softc *sc, struct iwm_node *in,
	struct iwm_mac_data_sta *ctxt_sta, int force_assoc_off)
{
	struct ieee80211_node *ni = &in->in_ni;
	unsigned dtim_period, dtim_count;
	struct ieee80211com *ic = &sc->sc_ic;
	struct ieee80211vap *vap = TAILQ_FIRST(&ic->ic_vaps);

	/* will this work? */
	dtim_period = vap->iv_dtim_period;
	dtim_count = vap->iv_dtim_count;
	IWM_DPRINTF(sc, IWM_DEBUG_RESET | IWM_DEBUG_BEACON | IWM_DEBUG_CMD,
	    "%s: force_assoc_off=%d\n", __func__, force_assoc_off);
	IWM_DPRINTF(sc, IWM_DEBUG_RESET | IWM_DEBUG_BEACON | IWM_DEBUG_CMD,
	    "DTIM: period=%d count=%d\n", dtim_period, dtim_count);
	IWM_DPRINTF(sc, IWM_DEBUG_RESET | IWM_DEBUG_BEACON | IWM_DEBUG_CMD,
	    "BEACON: tsf: %llu, ni_intval=%d\n",
	    (unsigned long long) le64toh(ni->ni_tstamp.tsf),
	    ni->ni_intval);

	/* We need the dtim_period to set the MAC as associated */
	if (in->in_assoc && dtim_period && !force_assoc_off) {
		uint64_t tsf;
		uint32_t dtim_offs;

		/*
		 * The DTIM count counts down, so when it is N that means N
		 * more beacon intervals happen until the DTIM TBTT. Therefore
		 * add this to the current time. If that ends up being in the
		 * future, the firmware will handle it.
		 *
		 * Also note that the system_timestamp (which we get here as
		 * "sync_device_ts") and TSF timestamp aren't at exactly the
		 * same offset in the frame -- the TSF is at the first symbol
		 * of the TSF, the system timestamp is at signal acquisition
		 * time. This means there's an offset between them of at most
		 * a few hundred microseconds (24 * 8 bits + PLCP time gives
		 * 384us in the longest case), this is currently not relevant
		 * as the firmware wakes up around 2ms before the TBTT.
		 */
		dtim_offs = dtim_count * ni->ni_intval;
		/* convert TU to usecs */
		dtim_offs *= 1024;

		/*
		 * net80211: TSF is in 802.11 order, so convert up to local
		 * ordering before we manipulate things.
		 */
		tsf = le64toh(ni->ni_tstamp.tsf);

		ctxt_sta->dtim_tsf = htole64(tsf + dtim_offs);
		ctxt_sta->dtim_time = htole32(tsf + dtim_offs);

		IWM_DPRINTF(sc, IWM_DEBUG_RESET | IWM_DEBUG_BEACON | IWM_DEBUG_CMD,
		    "DTIM TBTT is 0x%llx/0x%x, offset %d\n",
		    (long long)le64toh(ctxt_sta->dtim_tsf),
		    le32toh(ctxt_sta->dtim_time), dtim_offs);

		ctxt_sta->is_assoc = htole32(1);
	} else {
		ctxt_sta->is_assoc = htole32(0);
	}

	IWM_DPRINTF(sc, IWM_DEBUG_RESET | IWM_DEBUG_CMD | IWM_DEBUG_BEACON,
	    "%s: ni_intval: %d, bi_reciprocal: %d, dtim_interval: %d, dtim_reciprocal: %d\n",
	    __func__,
	    ni->ni_intval,
	    iwm_mvm_reciprocal(ni->ni_intval),
	    ni->ni_intval * dtim_period,
	    iwm_mvm_reciprocal(ni->ni_intval * dtim_period));

	ctxt_sta->bi = htole32(ni->ni_intval);
	ctxt_sta->bi_reciprocal = htole32(iwm_mvm_reciprocal(ni->ni_intval));
	ctxt_sta->dtim_interval = htole32(ni->ni_intval * dtim_period);
	ctxt_sta->dtim_reciprocal =
	    htole32(iwm_mvm_reciprocal(ni->ni_intval * dtim_period));

	/* 10 = CONN_MAX_LISTEN_INTERVAL */
	ctxt_sta->listen_interval = htole32(10);
	IWM_DPRINTF(sc, IWM_DEBUG_RESET | IWM_DEBUG_CMD | IWM_DEBUG_BEACON,
	    "%s: associd=%d\n", __func__, IEEE80211_AID(ni->ni_associd));
	ctxt_sta->assoc_id = htole32(IEEE80211_AID(ni->ni_associd));
}

static int
iwm_mvm_mac_ctxt_cmd_station(struct iwm_softc *sc, struct ieee80211vap *vap,
	uint32_t action)
{
	struct ieee80211_node *ni = vap->iv_bss;
	struct iwm_node *in = IWM_NODE(ni);
	struct iwm_mac_ctx_cmd cmd = {};

	IWM_DPRINTF(sc, IWM_DEBUG_RESET,
	    "%s: called; action=%d\n", __func__, action);

	/* Fill the common data for all mac context types */
	iwm_mvm_mac_ctxt_cmd_common(sc, in, &cmd, action);

	/* Allow beacons to pass through as long as we are not associated,or we
	 * do not have dtim period information */
	if (!in->in_assoc || !vap->iv_dtim_period)
		cmd.filter_flags |= htole32(IWM_MAC_FILTER_IN_BEACON);
	else
		cmd.filter_flags &= ~htole32(IWM_MAC_FILTER_IN_BEACON);

	/* Fill the data specific for station mode */
	iwm_mvm_mac_ctxt_cmd_fill_sta(sc, in,
	    &cmd.sta, action == IWM_FW_CTXT_ACTION_ADD);

	return iwm_mvm_mac_ctxt_send_cmd(sc, &cmd);
}

static int
iwm_mvm_mac_ctx_send(struct iwm_softc *sc, struct ieee80211vap *vap,
    uint32_t action)
{
	return iwm_mvm_mac_ctxt_cmd_station(sc, vap, action);
}

int
iwm_mvm_mac_ctxt_add(struct iwm_softc *sc, struct ieee80211vap *vap)
{
	struct iwm_vap *iv = IWM_VAP(vap);
	int ret;

	if (iv->is_uploaded != 0) {
		device_printf(sc->sc_dev, "%s: called; uploaded != 0\n",
		    __func__);
		return (EIO);
	}

	ret = iwm_mvm_mac_ctx_send(sc, vap, IWM_FW_CTXT_ACTION_ADD);
	if (ret)
		return (ret);
	iv->is_uploaded = 1;
	return (0);
}

int
iwm_mvm_mac_ctxt_changed(struct iwm_softc *sc, struct ieee80211vap *vap)
{
	struct iwm_vap *iv = IWM_VAP(vap);

	if (iv->is_uploaded == 0) {
		device_printf(sc->sc_dev, "%s: called; uploaded = 0\n",
		    __func__);
		return (EIO);
	}
	return iwm_mvm_mac_ctx_send(sc, vap, IWM_FW_CTXT_ACTION_MODIFY);
}

#if 0
static int
iwm_mvm_mac_ctxt_remove(struct iwm_softc *sc, struct iwm_node *in)
{
	struct iwm_mac_ctx_cmd cmd;
	int ret;

	if (!in->in_uploaded) {
		device_printf(sc->sc_dev,
		    "attempt to remove !uploaded node %p", in);
		return EIO;
	}

	memset(&cmd, 0, sizeof(cmd));

	cmd.id_and_color = htole32(IWM_FW_CMD_ID_AND_COLOR(IWM_DEFAULT_MACID,
	    IWM_DEFAULT_COLOR));
	cmd.action = htole32(IWM_FW_CTXT_ACTION_REMOVE);

	ret = iwm_mvm_send_cmd_pdu(sc,
	    IWM_MAC_CONTEXT_CMD, IWM_CMD_SYNC, sizeof(cmd), &cmd);
	if (ret) {
		device_printf(sc->sc_dev,
		    "Failed to remove MAC context: %d\n", ret);
		return ret;
	}
	in->in_uploaded = 0;

	return 0;
}
#endif
