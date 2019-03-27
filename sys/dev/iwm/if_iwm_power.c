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
#include <dev/iwm/if_iwm_constants.h>
#include <dev/iwm/if_iwm_util.h>
#include <dev/iwm/if_iwm_power.h>

static int iwm_power_scheme = IWM_POWER_SCHEME_BPS;

TUNABLE_INT("hw.iwm.power_scheme", &iwm_power_scheme);

/*
 * BEGIN mvm/power.c
 */

#define IWM_POWER_KEEP_ALIVE_PERIOD_SEC    25

static int
iwm_mvm_beacon_filter_send_cmd(struct iwm_softc *sc,
	struct iwm_beacon_filter_cmd *cmd)
{
	int ret;

	ret = iwm_mvm_send_cmd_pdu(sc, IWM_REPLY_BEACON_FILTERING_CMD,
	    0, sizeof(struct iwm_beacon_filter_cmd), cmd);

	if (!ret) {
		IWM_DPRINTF(sc, IWM_DEBUG_PWRSAVE | IWM_DEBUG_CMD,
		    "ba_enable_beacon_abort is: %d\n",
		    le32toh(cmd->ba_enable_beacon_abort));
		IWM_DPRINTF(sc, IWM_DEBUG_PWRSAVE | IWM_DEBUG_CMD,
		    "ba_escape_timer is: %d\n",
		    le32toh(cmd->ba_escape_timer));
		IWM_DPRINTF(sc, IWM_DEBUG_PWRSAVE | IWM_DEBUG_CMD,
		    "bf_debug_flag is: %d\n",
		    le32toh(cmd->bf_debug_flag));
		IWM_DPRINTF(sc, IWM_DEBUG_PWRSAVE | IWM_DEBUG_CMD,
		    "bf_enable_beacon_filter is: %d\n",
		    le32toh(cmd->bf_enable_beacon_filter));
		IWM_DPRINTF(sc, IWM_DEBUG_PWRSAVE | IWM_DEBUG_CMD,
		    "bf_energy_delta is: %d\n",
		    le32toh(cmd->bf_energy_delta));
		IWM_DPRINTF(sc, IWM_DEBUG_PWRSAVE | IWM_DEBUG_CMD,
		    "bf_escape_timer is: %d\n",
		    le32toh(cmd->bf_escape_timer));
		IWM_DPRINTF(sc, IWM_DEBUG_PWRSAVE | IWM_DEBUG_CMD,
		    "bf_roaming_energy_delta is: %d\n",
		    le32toh(cmd->bf_roaming_energy_delta));
		IWM_DPRINTF(sc, IWM_DEBUG_PWRSAVE | IWM_DEBUG_CMD,
		    "bf_roaming_state is: %d\n",
		    le32toh(cmd->bf_roaming_state));
		IWM_DPRINTF(sc, IWM_DEBUG_PWRSAVE | IWM_DEBUG_CMD,
		    "bf_temp_threshold is: %d\n",
		    le32toh(cmd->bf_temp_threshold));
		IWM_DPRINTF(sc, IWM_DEBUG_PWRSAVE | IWM_DEBUG_CMD,
		    "bf_temp_fast_filter is: %d\n",
		    le32toh(cmd->bf_temp_fast_filter));
		IWM_DPRINTF(sc, IWM_DEBUG_PWRSAVE | IWM_DEBUG_CMD,
		    "bf_temp_slow_filter is: %d\n",
		    le32toh(cmd->bf_temp_slow_filter));
	}
	return ret;
}

static void
iwm_mvm_beacon_filter_set_cqm_params(struct iwm_softc *sc,
	struct iwm_vap *ivp, struct iwm_beacon_filter_cmd *cmd)
{
	cmd->ba_enable_beacon_abort = htole32(sc->sc_bf.ba_enabled);
}

static void
iwm_mvm_power_log(struct iwm_softc *sc, struct iwm_mac_power_cmd *cmd)
{
	IWM_DPRINTF(sc, IWM_DEBUG_PWRSAVE | IWM_DEBUG_CMD,
	    "Sending power table command on mac id 0x%X for "
	    "power level %d, flags = 0x%X\n",
	    cmd->id_and_color, iwm_power_scheme, le16toh(cmd->flags));
	IWM_DPRINTF(sc, IWM_DEBUG_PWRSAVE | IWM_DEBUG_CMD,
	    "Keep alive = %u sec\n", le16toh(cmd->keep_alive_seconds));

	if (!(cmd->flags & htole16(IWM_POWER_FLAGS_POWER_MANAGEMENT_ENA_MSK))) {
		IWM_DPRINTF(sc, IWM_DEBUG_PWRSAVE | IWM_DEBUG_CMD,
		    "Disable power management\n");
		return;
	}

	IWM_DPRINTF(sc, IWM_DEBUG_PWRSAVE | IWM_DEBUG_CMD,
	    "Rx timeout = %u usec\n", le32toh(cmd->rx_data_timeout));
	IWM_DPRINTF(sc, IWM_DEBUG_PWRSAVE | IWM_DEBUG_CMD,
	    "Tx timeout = %u usec\n", le32toh(cmd->tx_data_timeout));
	if (cmd->flags & htole16(IWM_POWER_FLAGS_SKIP_OVER_DTIM_MSK))
		IWM_DPRINTF(sc, IWM_DEBUG_PWRSAVE | IWM_DEBUG_CMD,
		    "DTIM periods to skip = %u\n", cmd->skip_dtim_periods);
}

static boolean_t
iwm_mvm_power_is_radar(struct iwm_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ieee80211_channel *chan;
	boolean_t radar_detect = FALSE;

	chan = ic->ic_bsschan;
	if (chan == IEEE80211_CHAN_ANYC ||
	    (chan->ic_flags & IEEE80211_CHAN_DFS) != 0) {
		radar_detect = TRUE;
	}

        return radar_detect;
}

static void
iwm_mvm_power_config_skip_dtim(struct iwm_softc *sc,
	struct iwm_mac_power_cmd *cmd)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ieee80211vap *vap = TAILQ_FIRST(&ic->ic_vaps);
	int dtimper = vap->iv_dtim_period ?: 1;
	int skip;

	/* disable, in case we're supposed to override */
	cmd->skip_dtim_periods = 0;
	cmd->flags &= ~htole16(IWM_POWER_FLAGS_SKIP_OVER_DTIM_MSK);

        if (iwm_mvm_power_is_radar(sc))
                return;

	if (dtimper >= 10)
		return;

	/* TODO: check that multicast wake lock is off */

	if (iwm_power_scheme != IWM_POWER_SCHEME_LP)
		return;
	skip = 2;

	/* the firmware really expects "look at every X DTIMs", so add 1 */
	cmd->skip_dtim_periods = 1 + skip;
	cmd->flags |= htole16(IWM_POWER_FLAGS_SKIP_OVER_DTIM_MSK);
}

static void
iwm_mvm_power_build_cmd(struct iwm_softc *sc, struct iwm_vap *ivp,
	struct iwm_mac_power_cmd *cmd)
{
	struct ieee80211vap *vap = &ivp->iv_vap;
	struct ieee80211_node *ni = vap->iv_bss;
	int dtimper, dtimper_msec;
	int keep_alive;
	boolean_t bss_conf_ps = FALSE;

	cmd->id_and_color = htole32(IWM_FW_CMD_ID_AND_COLOR(ivp->id,
	    ivp->color));
	dtimper = vap->iv_dtim_period ?: 1;

	/*
	 * Regardless of power management state the driver must set
	 * keep alive period. FW will use it for sending keep alive NDPs
	 * immediately after association. Check that keep alive period
	 * is at least 3 * DTIM
	 */
	dtimper_msec = dtimper * ni->ni_intval;
	keep_alive
	    = imax(3 * dtimper_msec, 1000 * IWM_POWER_KEEP_ALIVE_PERIOD_SEC);
	keep_alive = roundup(keep_alive, 1000) / 1000;
	cmd->keep_alive_seconds = htole16(keep_alive);

	if (sc->sc_ps_disabled)
		return;

	cmd->flags |= htole16(IWM_POWER_FLAGS_POWER_SAVE_ENA_MSK);

	if (IWM_NODE(ni)->in_assoc &&
	    (vap->iv_flags & IEEE80211_F_PMGTON) != 0) {
		bss_conf_ps = TRUE;
	}
	if (!bss_conf_ps)
		return;

	cmd->flags |= htole16(IWM_POWER_FLAGS_POWER_MANAGEMENT_ENA_MSK);

	iwm_mvm_power_config_skip_dtim(sc, cmd);

	cmd->rx_data_timeout =
		htole32(IWM_MVM_DEFAULT_PS_RX_DATA_TIMEOUT);
	cmd->tx_data_timeout =
		htole32(IWM_MVM_DEFAULT_PS_TX_DATA_TIMEOUT);
}

static int
iwm_mvm_power_send_cmd(struct iwm_softc *sc, struct iwm_vap *ivp)
{
	struct iwm_mac_power_cmd cmd = {};

	iwm_mvm_power_build_cmd(sc, ivp, &cmd);
	iwm_mvm_power_log(sc, &cmd);

	return iwm_mvm_send_cmd_pdu(sc, IWM_MAC_PM_POWER_TABLE, 0,
	    sizeof(cmd), &cmd);
}

static int
_iwm_mvm_enable_beacon_filter(struct iwm_softc *sc, struct iwm_vap *ivp,
	struct iwm_beacon_filter_cmd *cmd)
{
	int ret;

	iwm_mvm_beacon_filter_set_cqm_params(sc, ivp, cmd);
	ret = iwm_mvm_beacon_filter_send_cmd(sc, cmd);

	if (!ret)
		sc->sc_bf.bf_enabled = 1;

	return ret;
}

int
iwm_mvm_enable_beacon_filter(struct iwm_softc *sc, struct iwm_vap *ivp)
{
	struct iwm_beacon_filter_cmd cmd = {
		IWM_BF_CMD_CONFIG_DEFAULTS,
		.bf_enable_beacon_filter = htole32(1),
	};

	return _iwm_mvm_enable_beacon_filter(sc, ivp, &cmd);
}

int
iwm_mvm_disable_beacon_filter(struct iwm_softc *sc)
{
	struct iwm_beacon_filter_cmd cmd = {};
	int ret;

	ret = iwm_mvm_beacon_filter_send_cmd(sc, &cmd);
	if (ret == 0)
		sc->sc_bf.bf_enabled = 0;

	return ret;
}

static int
iwm_mvm_power_set_ps(struct iwm_softc *sc)
{
	struct ieee80211vap *vap;
	boolean_t disable_ps;
	int ret;

	/* disable PS if CAM */
	disable_ps = (iwm_power_scheme == IWM_POWER_SCHEME_CAM);
	/* ...or if any of the vifs require PS to be off */
	TAILQ_FOREACH(vap, &sc->sc_ic.ic_vaps, iv_next) {
		struct iwm_vap *ivp = IWM_VAP(vap);
		if (ivp->phy_ctxt != NULL && ivp->ps_disabled)
			disable_ps = TRUE;
	}

	/* update device power state if it has changed */
	if (sc->sc_ps_disabled != disable_ps) {
		boolean_t old_ps_disabled = sc->sc_ps_disabled;

		sc->sc_ps_disabled = disable_ps;
		ret = iwm_mvm_power_update_device(sc);
		if (ret) {
			sc->sc_ps_disabled = old_ps_disabled;
			return ret;
		}
	}

	return 0;
}

static int
iwm_mvm_power_set_ba(struct iwm_softc *sc, struct iwm_vap *ivp)
{
	struct iwm_beacon_filter_cmd cmd = {
		IWM_BF_CMD_CONFIG_DEFAULTS,
		.bf_enable_beacon_filter = htole32(1),
	};
	struct ieee80211vap *vap = &ivp->iv_vap;
	struct ieee80211_node *ni = vap->iv_bss;
	boolean_t bss_conf_ps = FALSE;

	if (!sc->sc_bf.bf_enabled)
		return 0;

	if (ni != NULL && IWM_NODE(ni)->in_assoc &&
	    (vap->iv_flags & IEEE80211_F_PMGTON) != 0) {
		bss_conf_ps = TRUE;
	}
	sc->sc_bf.ba_enabled = !sc->sc_ps_disabled && bss_conf_ps;

	return _iwm_mvm_enable_beacon_filter(sc, ivp, &cmd);
}

int
iwm_mvm_power_update_ps(struct iwm_softc *sc)
{
	struct ieee80211vap *vap = TAILQ_FIRST(&sc->sc_ic.ic_vaps);
	int ret;

	ret = iwm_mvm_power_set_ps(sc);
	if (ret)
		return ret;

	if (vap != NULL)
		return iwm_mvm_power_set_ba(sc, IWM_VAP(vap));

	return 0;
}

int
iwm_mvm_power_update_mac(struct iwm_softc *sc)
{
	struct ieee80211vap *vap = TAILQ_FIRST(&sc->sc_ic.ic_vaps);
	int ret;

	ret = iwm_mvm_power_set_ps(sc);
	if (ret)
		return ret;

	if (vap != NULL) {
		ret = iwm_mvm_power_send_cmd(sc, IWM_VAP(vap));
		if (ret)
			return ret;
	}

	if (vap != NULL)
		return iwm_mvm_power_set_ba(sc, IWM_VAP(vap));

	return 0;
}

int
iwm_mvm_power_update_device(struct iwm_softc *sc)
{
	struct iwm_device_power_cmd cmd = {
		.flags = 0,
	};

	if (iwm_power_scheme == IWM_POWER_SCHEME_CAM)
		sc->sc_ps_disabled = TRUE;

	if (!sc->sc_ps_disabled)
		cmd.flags |= htole16(IWM_DEVICE_POWER_FLAGS_POWER_SAVE_ENA_MSK);

	IWM_DPRINTF(sc, IWM_DEBUG_PWRSAVE | IWM_DEBUG_CMD,
	    "Sending device power command with flags = 0x%X\n", cmd.flags);

	return iwm_mvm_send_cmd_pdu(sc,
	    IWM_POWER_TABLE_CMD, 0, sizeof(cmd), &cmd);
}
