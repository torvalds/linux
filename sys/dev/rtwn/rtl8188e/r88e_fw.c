/*	$OpenBSD: if_urtwn.c,v 1.16 2011/02/10 17:26:40 jakemsr Exp $	*/

/*-
 * Copyright (c) 2010 Damien Bergamini <damien.bergamini@free.fr>
 * Copyright (c) 2014 Kevin Lo <kevlo@FreeBSD.org>
 * Copyright (c) 2015-2016 Andriy Voskoboinyk <avos@FreeBSD.org>
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
#include <dev/rtwn/rtl8188e/r88e_fw_cmd.h>


#ifndef RTWN_WITHOUT_UCODE
int
r88e_fw_cmd(struct rtwn_softc *sc, uint8_t id, const void *buf, int len)
{
	struct r88e_fw_cmd cmd;
	int ntries, error;

	if (!(sc->sc_flags & RTWN_FW_LOADED)) {
		RTWN_DPRINTF(sc, RTWN_DEBUG_FIRMWARE, "%s: firmware "
		    "was not loaded; command (id %d) will be discarded\n",
		    __func__, id);
		return (0);
	}

	/* Wait for current FW box to be empty. */
	for (ntries = 0; ntries < 100; ntries++) {
		if (!(rtwn_read_1(sc, R92C_HMETFR) & (1 << sc->fwcur)))
			break;
		rtwn_delay(sc, 2000);
	}
	if (ntries == 100) {
		device_printf(sc->sc_dev,
		    "could not send firmware command\n");
		return (ETIMEDOUT);
	}
	memset(&cmd, 0, sizeof(cmd));
	cmd.id = id;
	KASSERT(len <= sizeof(cmd.msg),
	    ("%s: firmware command too long (%d > %zu)\n",
	    __func__, len, sizeof(cmd.msg)));
	memcpy(cmd.msg, buf, len);

	/* Write the first word last since that will trigger the FW. */
	if (len > 3) {
		error = rtwn_write_4(sc, R88E_HMEBOX_EXT(sc->fwcur),
		    *(uint32_t *)((uint8_t *)&cmd + 4));
		if (error != 0)
			return (error);
	}
	error = rtwn_write_4(sc, R92C_HMEBOX(sc->fwcur), *(uint32_t *)&cmd);
	if (error != 0)
		return (error);

	sc->fwcur = (sc->fwcur + 1) % R92C_H2C_NBOX;

	return (0);
}

void
r88e_fw_reset(struct rtwn_softc *sc, int reason)
{
	uint16_t reg;

	reg = rtwn_read_2(sc, R92C_SYS_FUNC_EN);
	rtwn_write_2(sc, R92C_SYS_FUNC_EN, reg & ~R92C_SYS_FUNC_EN_CPUEN);

	if (reason != RTWN_FW_RESET_SHUTDOWN) {
		rtwn_write_2(sc, R92C_SYS_FUNC_EN,
		    reg | R92C_SYS_FUNC_EN_CPUEN);
	}
}

void
r88e_fw_download_enable(struct rtwn_softc *sc, int enable)
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
		/* Reserved for f/w extension. */
		rtwn_write_1(sc, R92C_MCUFWDL + 1, 0);
	}
}
#endif

void
r88e_macid_enable_link(struct rtwn_softc *sc, int id, int enable)
{
	uint32_t reg;

	reg = R88E_MACID_NO_LINK;
	if (id > 32)
		reg += 4;

	if (enable)
		rtwn_setbits_4(sc, reg, 1 << (id % 32), 0);
	else
		rtwn_setbits_4(sc, reg, 0, 1 << (id % 32));

	/* XXX max macid for tx reports */
}

void
r88e_set_media_status(struct rtwn_softc *sc, int macid)
{
	struct r88e_fw_cmd_msrrpt status;

	if (macid & RTWN_MACID_VALID)
		status.msrb0 = R88E_MSRRPT_B0_ASSOC;
	else
		status.msrb0 = R88E_MSRRPT_B0_DISASSOC;
	status.macid = (macid & ~RTWN_MACID_VALID);

	r88e_macid_enable_link(sc, status.macid,
	    (macid & RTWN_MACID_VALID) != 0);

#ifndef RTWN_WITHOUT_UCODE
	if (r88e_fw_cmd(sc, R88E_CMD_MSR_RPT, &status, sizeof(status)) != 0) {
		device_printf(sc->sc_dev, "%s: cannot change media status!\n",
		    __func__);
	}
#endif
}

#ifndef RTWN_WITHOUT_UCODE
int
r88e_set_rsvd_page(struct rtwn_softc *sc, int probe_resp, int null,
    int qos_null)
{
	struct r88e_fw_cmd_rsvdpage rsvd;

	rsvd.probe_resp = probe_resp;
	rsvd.ps_poll = 0;
	rsvd.null_data = null;
	rsvd.null_data_qos = qos_null;
	rsvd.null_data_qos_bt = 0;
	return (r88e_fw_cmd(sc, R88E_CMD_RSVD_PAGE, &rsvd, sizeof(rsvd)));
}

int
r88e_set_pwrmode(struct rtwn_softc *sc, struct ieee80211vap *vap,
    int off)
{
	struct r88e_fw_cmd_pwrmode mode;
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
	error = r88e_fw_cmd(sc, R88E_CMD_SET_PWRMODE, &mode, sizeof(mode));
	if (error != 0) {
		device_printf(sc->sc_dev,
		    "%s: CMD_SET_PWRMODE was not sent, error %d\n",
		    __func__, error);
	}

	return (error);
}
#endif
