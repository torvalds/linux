/*-
 * Copyright (c) 2017 Kevin Lo <kevlo@FreeBSD.org>
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
#include <dev/rtwn/rtl8188e/r88e_reg.h>

#include <dev/rtwn/rtl8812a/r12a_fw_cmd.h>

#include <dev/rtwn/rtl8192e/r92e.h>

#ifndef RTWN_WITHOUT_UCODE
void
r92e_fw_reset(struct rtwn_softc *sc, int reason)
{
	/* Reset MCU IO wrapper. */
	rtwn_setbits_1(sc, R92C_RSV_CTRL + 1, 0x01, 0);

	rtwn_setbits_1_shift(sc, R92C_SYS_FUNC_EN,
	    R92C_SYS_FUNC_EN_CPUEN, 0, 1);

	/* Enable MCU IO wrapper. */
	rtwn_setbits_1(sc, R92C_RSV_CTRL + 1, 0, 0x01);

	rtwn_setbits_1_shift(sc, R92C_SYS_FUNC_EN,
	    0, R92C_SYS_FUNC_EN_CPUEN, 1);
}

void
r92e_set_media_status(struct rtwn_softc *sc, int macid)
{
	struct r88e_fw_cmd_msrrpt status;

	if (macid & RTWN_MACID_VALID)
		status.msrb0 = R88E_MSRRPT_B0_ASSOC;
	else
		status.msrb0 = R88E_MSRRPT_B0_DISASSOC;
	status.macid = (macid & ~RTWN_MACID_VALID);

	if (r88e_fw_cmd(sc, R88E_CMD_MSR_RPT, &status, sizeof(status)) != 0) {
		device_printf(sc->sc_dev, "%s: cannot change media status!\n",
		    __func__);
	}
}

int
r92e_set_pwrmode(struct rtwn_softc *sc, struct ieee80211vap *vap, int off)
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
	mode.pwrb5 = 0;
	error = r88e_fw_cmd(sc, R88E_CMD_SET_PWRMODE, &mode, sizeof(mode));
	if (error != 0) {
		device_printf(sc->sc_dev,
		    "%s: CMD_SET_PWRMODE was not sent, error %d\n",
		    __func__, error);
	}

	return (error);
}
#endif
