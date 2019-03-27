/*-
 * Copyright (c) 2016 Andriy Voskoboinyk <avos@FreeBSD.org>
 * All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_wlan.h"

#include <sys/param.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/mbuf.h>
#include <sys/kernel.h>
#include <sys/socket.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/queue.h>
#include <sys/taskqueue.h>
#include <sys/bus.h>
#include <sys/endian.h>
#include <sys/linker.h>

#include <net/if.h>
#include <net/ethernet.h>
#include <net/if_media.h>

#include <net80211/ieee80211_var.h>
#include <net80211/ieee80211_radiotap.h>

#include <dev/rtwn/if_rtwnreg.h>
#include <dev/rtwn/if_rtwnvar.h>

#include <dev/rtwn/if_rtwn_debug.h>

#include <dev/rtwn/rtl8188e/r88e.h>

#include <dev/rtwn/rtl8812a/r12a.h>
#include <dev/rtwn/rtl8812a/r12a_reg.h>
#include <dev/rtwn/rtl8812a/r12a_var.h>
#include <dev/rtwn/rtl8812a/r12a_fw_cmd.h>


#ifndef RTWN_WITHOUT_UCODE
void
r12a_fw_reset(struct rtwn_softc *sc, int reason)
{
	/* Reset MCU IO wrapper. */
	rtwn_setbits_1(sc, R92C_RSV_CTRL, R92C_RSV_CTRL_WLOCK_00, 0);
	rtwn_setbits_1(sc, R92C_RSV_CTRL + 1, 0x08, 0);

	rtwn_setbits_1_shift(sc, R92C_SYS_FUNC_EN,
	    R92C_SYS_FUNC_EN_CPUEN, 0, 1);

	/* Enable MCU IO wrapper. */
	rtwn_setbits_1(sc, R92C_RSV_CTRL, R92C_RSV_CTRL_WLOCK_00, 0);
	rtwn_setbits_1(sc, R92C_RSV_CTRL + 1, 0, 0x08);

	rtwn_setbits_1_shift(sc, R92C_SYS_FUNC_EN,
	    0, R92C_SYS_FUNC_EN_CPUEN, 1);
}

void
r12a_fw_download_enable(struct rtwn_softc *sc, int enable)
{
	if (enable) {
		/* MCU firmware download enable. */
		rtwn_setbits_1(sc, R92C_MCUFWDL, 0, R92C_MCUFWDL_EN);
		/* 8051 reset. */
		rtwn_setbits_1_shift(sc, R92C_MCUFWDL, R92C_MCUFWDL_ROM_DLEN,
		    0, 2);
	} else {
		/* MCU download disable. */
		rtwn_setbits_1(sc, R92C_MCUFWDL, R92C_MCUFWDL_EN, 0);
	}
}

void
r12a_set_media_status(struct rtwn_softc *sc, int macid)
{
	struct r12a_fw_cmd_msrrpt status;
	int error;

	if (macid & RTWN_MACID_VALID)
		status.msrb0 = R12A_MSRRPT_B0_ASSOC;
	else
		status.msrb0 = R12A_MSRRPT_B0_DISASSOC;

	status.macid = (macid & ~RTWN_MACID_VALID);
	status.macid_end = 0;

	error = r88e_fw_cmd(sc, R12A_CMD_MSR_RPT, &status, sizeof(status));
	if (error != 0)
		device_printf(sc->sc_dev, "cannot change media status!\n");
}

int
r12a_set_pwrmode(struct rtwn_softc *sc, struct ieee80211vap *vap,
    int off)
{
	struct r12a_fw_cmd_pwrmode mode;
	int error;

	if (off && vap->iv_state == IEEE80211_S_RUN &&
	    (vap->iv_flags & IEEE80211_F_PMGTON)) {
		mode.mode = R88E_PWRMODE_LEG;
		/*
		 * TODO: switch to RFOFF state
		 * (something is missing here - Rx stops with it).
		 */
#ifdef RTWN_TODO
		mode.pwr_state = R88E_PWRMODE_STATE_RFOFF;
#else
		mode.pwr_state = R88E_PWRMODE_STATE_RFON;
#endif
	} else {
		mode.mode = R88E_PWRMODE_CAM;
		mode.pwr_state = R88E_PWRMODE_STATE_ALLON;
	}
	mode.pwrb1 =
	    SM(R88E_PWRMODE_B1_SMART_PS, R88E_PWRMODE_B1_LEG_NULLDATA) |
	    SM(R88E_PWRMODE_B1_RLBM, R88E_PWRMODE_B1_MODE_MIN);
	/* XXX ignored */
	mode.bcn_pass = 0;
	mode.queue_uapsd = 0;
	mode.pwrb5 = R12A_PWRMODE_B5_NO_BTCOEX;
	error = r88e_fw_cmd(sc, R12A_CMD_SET_PWRMODE, &mode, sizeof(mode));
	if (error != 0) {
		device_printf(sc->sc_dev,
		    "%s: CMD_SET_PWRMODE was not sent, error %d\n",
		    __func__, error);
	}

	return (error);
}

void
r12a_iq_calib_fw(struct rtwn_softc *sc)
{
	struct r12a_softc *rs = sc->sc_priv;
	struct ieee80211_channel *c = sc->sc_ic.ic_curchan;
	struct r12a_fw_cmd_iq_calib cmd;

	if (rs->rs_flags & R12A_IQK_RUNNING)
		return;

	RTWN_DPRINTF(sc, RTWN_DEBUG_CALIB, "Starting IQ calibration (FW)\n");

	cmd.chan = rtwn_chan2centieee(c);
	if (IEEE80211_IS_CHAN_5GHZ(c))
		cmd.band_bw = RTWN_CMD_IQ_BAND_5GHZ;
	else
		cmd.band_bw = RTWN_CMD_IQ_BAND_2GHZ;

	/* TODO: 80/160 MHz. */
	if (IEEE80211_IS_CHAN_HT40(c))
		cmd.band_bw |= RTWN_CMD_IQ_CHAN_WIDTH_40;
	else
		cmd.band_bw |= RTWN_CMD_IQ_CHAN_WIDTH_20;

	cmd.ext_5g_pa_lna = RTWN_CMD_IQ_EXT_PA_5G(rs->ext_pa_5g);
	cmd.ext_5g_pa_lna |= RTWN_CMD_IQ_EXT_LNA_5G(rs->ext_lna_5g);

	if (r88e_fw_cmd(sc, R12A_CMD_IQ_CALIBRATE, &cmd, sizeof(cmd)) != 0) {
		RTWN_DPRINTF(sc, RTWN_DEBUG_CALIB,
		    "error while sending IQ calibration command to FW!\n");
		return;
	}

	rs->rs_flags |= R12A_IQK_RUNNING;
}
#endif
