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
#include <net80211/ieee80211_ratectl.h>

#include <dev/rtwn/if_rtwnreg.h>
#include <dev/rtwn/if_rtwnvar.h>

#include <dev/rtwn/if_rtwn_debug.h>
#include <dev/rtwn/if_rtwn_ridx.h>
#include <dev/rtwn/if_rtwn_rx.h>
#include <dev/rtwn/if_rtwn_task.h>
#include <dev/rtwn/if_rtwn_tx.h>

#include <dev/rtwn/rtl8192c/r92c.h>
#include <dev/rtwn/rtl8192c/r92c_reg.h>
#include <dev/rtwn/rtl8192c/r92c_var.h>
#include <dev/rtwn/rtl8192c/r92c_fw_cmd.h>
#include <dev/rtwn/rtl8192c/r92c_tx_desc.h>


#ifndef RTWN_WITHOUT_UCODE
static int
r92c_fw_cmd(struct rtwn_softc *sc, uint8_t id, const void *buf, int len)
{
	struct r92c_fw_cmd cmd;
	int ntries, error;

	KASSERT(len <= sizeof(cmd.msg),
	    ("%s: firmware command too long (%d > %zu)\n",
	    __func__, len, sizeof(cmd.msg)));

	if (!(sc->sc_flags & RTWN_FW_LOADED)) {
		RTWN_DPRINTF(sc, RTWN_DEBUG_FIRMWARE, "%s: firmware "
		    "was not loaded; command (id %u) will be discarded\n",
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
	if (len > 3) {
		/* Ext command: [id : byte2 : byte3 : byte4 : byte0 : byte1] */
		cmd.id |= R92C_CMD_FLAG_EXT;
		memcpy(cmd.msg, (const uint8_t *)buf + 2, len - 2);
		memcpy(cmd.msg + 3, buf, 2);
	} else
		memcpy(cmd.msg, buf, len);

	/* Write the first word last since that will trigger the FW. */
	if (len > 3) {
		error = rtwn_write_2(sc, R92C_HMEBOX_EXT(sc->fwcur),
		    *(uint16_t *)((uint8_t *)&cmd + 4));
		if (error != 0)
			return (error);
	}
	error = rtwn_write_4(sc, R92C_HMEBOX(sc->fwcur),
	    *(uint32_t *)&cmd);
	if (error != 0)
		return (error);

	sc->fwcur = (sc->fwcur + 1) % R92C_H2C_NBOX;

	return (0);
}

void
r92c_fw_reset(struct rtwn_softc *sc, int reason)
{
	int ntries;

	if (reason == RTWN_FW_RESET_CHECKSUM)
		return;

	/* Tell 8051 to reset itself. */
	rtwn_write_1(sc, R92C_HMETFR + 3, 0x20);

	/* Wait until 8051 resets by itself. */
	for (ntries = 0; ntries < 100; ntries++) {
		if ((rtwn_read_2(sc, R92C_SYS_FUNC_EN) &
		    R92C_SYS_FUNC_EN_CPUEN) == 0)
			return;
		rtwn_delay(sc, 50);
	}
	/* Force 8051 reset. */
	rtwn_setbits_1_shift(sc, R92C_SYS_FUNC_EN,
	    R92C_SYS_FUNC_EN_CPUEN, 0, 1);
}

void
r92c_fw_download_enable(struct rtwn_softc *sc, int enable)
{
	if (enable) {
		/* 8051 enable. */
		rtwn_setbits_1_shift(sc, R92C_SYS_FUNC_EN, 0,
		    R92C_SYS_FUNC_EN_CPUEN, 1);
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

/*
 * Initialize firmware rate adaptation.
 */
#ifndef RTWN_WITHOUT_UCODE
static int
r92c_send_ra_cmd(struct rtwn_softc *sc, int macid, uint32_t rates,
    int maxrate)
{
	struct r92c_fw_cmd_macid_cfg cmd;
	uint8_t mode;
	int error = 0;

	/* XXX should be called directly from iv_newstate() for MACID_BC */
	/* XXX joinbss, not send_ra_cmd() */
#ifdef RTWN_TODO
	/* NB: group addressed frames are done at 11bg rates for now */
	if (ic->ic_curmode == IEEE80211_MODE_11B)
		mode = R92C_RAID_11B;
	else
		mode = R92C_RAID_11BG;
	/* XXX misleading 'mode' value here for unicast frames */
	RTWN_DPRINTF(sc, RTWN_DEBUG_RA,
	    "%s: mode 0x%x, rates 0x%08x, basicrates 0x%08x\n", __func__,
	    mode, rates, basicrates);

	/* Set rates mask for group addressed frames. */
	cmd.macid = RTWN_MACID_BC | R92C_CMD_MACID_VALID;
	cmd.mask = htole32(mode << 28 | basicrates);
	error = rtwn_fw_cmd(sc, R92C_CMD_MACID_CONFIG, &cmd, sizeof(cmd));
	if (error != 0) {
		device_printf(sc->sc_dev,
		    "could not set RA mask for broadcast station\n");
		return (error);
	}
#endif

	/* Set rates mask for unicast frames. */
	if (maxrate >= RTWN_RIDX_HT_MCS(0))
		mode = R92C_RAID_11GN;
	else if (maxrate >= RTWN_RIDX_OFDM6)
		mode = R92C_RAID_11BG;
	else
		mode = R92C_RAID_11B;
	cmd.macid = macid | R92C_CMD_MACID_VALID;
	cmd.mask = htole32(mode << 28 | rates);
	error = r92c_fw_cmd(sc, R92C_CMD_MACID_CONFIG, &cmd, sizeof(cmd));
	if (error != 0) {
		device_printf(sc->sc_dev,
		    "%s: could not set RA mask for %d station\n",
		    __func__, macid);
		return (error);
	}

	return (0);
}
#endif

static void
r92c_init_ra(struct rtwn_softc *sc, int macid)
{
	struct ieee80211_htrateset *rs_ht;
	struct ieee80211_node *ni;
	uint32_t rates;
	int maxrate;

	RTWN_NT_LOCK(sc);
	if (sc->node_list[macid] == NULL) {
		RTWN_DPRINTF(sc, RTWN_DEBUG_RA, "%s: macid %d, ni is NULL\n",
		    __func__, macid);
		RTWN_NT_UNLOCK(sc);
		return;
	}

	ni = ieee80211_ref_node(sc->node_list[macid]);
	if (ni->ni_flags & IEEE80211_NODE_HT)
		rs_ht = &ni->ni_htrates;
	else
		rs_ht = NULL;
	/* XXX MACID_BC */
	rtwn_get_rates(sc, &ni->ni_rates, rs_ht, &rates, &maxrate, 0);
	RTWN_NT_UNLOCK(sc);

#ifndef RTWN_WITHOUT_UCODE
	if (sc->sc_ratectl == RTWN_RATECTL_FW) {
		r92c_send_ra_cmd(sc, macid, rates, maxrate);
	}
#endif

	rtwn_write_1(sc, R92C_INIDATA_RATE_SEL(macid), maxrate);

	ieee80211_free_node(ni);
}

void
r92c_joinbss_rpt(struct rtwn_softc *sc, int macid)
{
#ifndef RTWN_WITHOUT_UCODE
	struct r92c_softc *rs = sc->sc_priv;
	struct ieee80211vap *vap;
	struct r92c_fw_cmd_joinbss_rpt cmd;

	if (sc->vaps[0] == NULL)	/* XXX fix */
		goto end;

	vap = &sc->vaps[0]->vap;
	if ((vap->iv_state == IEEE80211_S_RUN) ^
	    !(rs->rs_flags & R92C_FLAG_ASSOCIATED))
		goto end;

	if (rs->rs_flags & R92C_FLAG_ASSOCIATED) {
		cmd.mstatus = R92C_MSTATUS_DISASSOC;
		rs->rs_flags &= ~R92C_FLAG_ASSOCIATED;
	} else {
		cmd.mstatus = R92C_MSTATUS_ASSOC;
		rs->rs_flags |= R92C_FLAG_ASSOCIATED;
	}

	if (r92c_fw_cmd(sc, R92C_CMD_JOINBSS_RPT, &cmd, sizeof(cmd)) != 0) {
		device_printf(sc->sc_dev, "%s: cannot change media status!\n",
		    __func__);
	}

end:
#endif

	/* TODO: init rates for RTWN_MACID_BC. */
	if (macid & RTWN_MACID_VALID)
		r92c_init_ra(sc, macid & ~RTWN_MACID_VALID);
}

#ifndef RTWN_WITHOUT_UCODE
int
r92c_set_rsvd_page(struct rtwn_softc *sc, int probe_resp, int null,
    int qos_null)
{
	struct r92c_fw_cmd_rsvdpage rsvd;

	rsvd.probe_resp = probe_resp;
	rsvd.ps_poll = 0;
	rsvd.null_data = null;

	return (r92c_fw_cmd(sc, R92C_CMD_RSVD_PAGE, &rsvd, sizeof(rsvd)));
}

int
r92c_set_pwrmode(struct rtwn_softc *sc, struct ieee80211vap *vap,
    int off)
{
	struct r92c_fw_cmd_pwrmode mode;
	int error;

	/* XXX dm_RF_saving */

	if (off && vap->iv_state == IEEE80211_S_RUN &&
	    (vap->iv_flags & IEEE80211_F_PMGTON))
		mode.mode = R92C_PWRMODE_MIN;
	else
		mode.mode = R92C_PWRMODE_CAM;
	mode.smart_ps = R92C_PWRMODE_SMARTPS_NULLDATA;
	mode.bcn_pass = 1;	/* XXX */
	error = r92c_fw_cmd(sc, R92C_CMD_SET_PWRMODE, &mode, sizeof(mode));
	if (error != 0) {
		device_printf(sc->sc_dev,
		    "%s: CMD_SET_PWRMODE was not sent, error %d\n",
		    __func__, error);
	}

	return (error);
}

void
r92c_set_rssi(struct rtwn_softc *sc)
{
	struct ieee80211_node *ni;
	struct rtwn_node *rn;
	struct r92c_fw_cmd_rssi cmd;
	int i;

	cmd.reserved = 0;

	RTWN_NT_LOCK(sc);
	for (i = 0; i < sc->macid_limit; i++) {
		/* XXX optimize? */
		ni = sc->node_list[i];
		if (ni == NULL)
			continue;

		rn = RTWN_NODE(ni);
		cmd.macid = i;
		cmd.pwdb = rn->avg_pwdb;
		RTWN_DPRINTF(sc, RTWN_DEBUG_RSSI,
		    "%s: sending RSSI command (macid %d, rssi %d)\n",
		    __func__, i, rn->avg_pwdb);

		RTWN_NT_UNLOCK(sc);
		r92c_fw_cmd(sc, R92C_CMD_RSSI_SETTING, &cmd, sizeof(cmd));
		RTWN_NT_LOCK(sc);
	}
	RTWN_NT_UNLOCK(sc);
}

static void
r92c_ratectl_tx_complete(struct rtwn_softc *sc, uint8_t *buf, int len)
{
#if __FreeBSD_version >= 1200012
	struct ieee80211_ratectl_tx_status txs;
#endif
	struct r92c_c2h_tx_rpt *rpt;
	struct ieee80211_node *ni;
	uint8_t macid;
	int ntries;

	if (sc->sc_ratectl != RTWN_RATECTL_NET80211) {
		/* shouldn't happen */
		device_printf(sc->sc_dev, "%s called while ratectl = %d!\n",
		    __func__, sc->sc_ratectl);
		return;
	}

	rpt = (struct r92c_c2h_tx_rpt *)buf;
	if (len != sizeof(*rpt)) {
		device_printf(sc->sc_dev,
		    "%s: wrong report size (%d, must be %zu)\n",
		    __func__, len, sizeof(*rpt));
		return;
	}

	RTWN_DPRINTF(sc, RTWN_DEBUG_INTR,
	    "%s: ccx report dump: 0: %02X, 1: %02X, queue time: "
	    "low %02X, high %02X, 4: %02X, 5: %02X, 6: %02X, 7: %02X\n",
	    __func__, rpt->rptb0, rpt->rptb1, rpt->queue_time_low,
	    rpt->queue_time_high, rpt->rptb4, rpt->rptb5, rpt->rptb6,
	    rpt->rptb7);

	macid = MS(rpt->rptb5, R92C_RPTB5_MACID);
	if (macid > sc->macid_limit) {
		device_printf(sc->sc_dev,
		    "macid %u is too big; increase MACID_MAX limit\n",
		    macid);
		return;
	}

	ntries = MS(rpt->rptb0, R92C_RPTB0_RETRY_CNT);

	RTWN_NT_LOCK(sc);
	ni = sc->node_list[macid];
	if (ni != NULL) {
		RTWN_DPRINTF(sc, RTWN_DEBUG_INTR, "%s: frame for macid %u was"
		    "%s sent (%d retries)\n", __func__, macid,
		    (rpt->rptb7 & R92C_RPTB7_PKT_OK) ? "" : " not",
		    ntries);

#if __FreeBSD_version >= 1200012
		txs.flags = IEEE80211_RATECTL_STATUS_LONG_RETRY;
		txs.long_retries = ntries;
		if (rpt->rptb7 & R92C_RPTB7_PKT_OK)
			txs.status = IEEE80211_RATECTL_TX_SUCCESS;
		else if (rpt->rptb6 & R92C_RPTB6_RETRY_OVER)
			txs.status = IEEE80211_RATECTL_TX_FAIL_LONG; /* XXX */
		else if (rpt->rptb6 & R92C_RPTB6_LIFE_EXPIRE)
			txs.status = IEEE80211_RATECTL_TX_FAIL_EXPIRED;
		else
			txs.status = IEEE80211_RATECTL_TX_FAIL_UNSPECIFIED;
		ieee80211_ratectl_tx_complete(ni, &txs);
#else
		struct ieee80211vap *vap = ni->ni_vap;
		if (rpt->rptb7 & R92C_RPTB7_PKT_OK) {
			ieee80211_ratectl_tx_complete(vap, ni,
			    IEEE80211_RATECTL_TX_SUCCESS, &ntries, NULL);
		} else {
			ieee80211_ratectl_tx_complete(vap, ni,
			    IEEE80211_RATECTL_TX_FAILURE, &ntries, NULL);
		}
#endif
	} else {
		RTWN_DPRINTF(sc, RTWN_DEBUG_INTR, "%s: macid %u, ni is NULL\n",
		    __func__, macid);
	}
	RTWN_NT_UNLOCK(sc);

#ifdef IEEE80211_SUPPORT_SUPERG
	if (sc->sc_tx_n_active > 0 && --sc->sc_tx_n_active <= 1)
		rtwn_cmd_sleepable(sc, NULL, 0, rtwn_ff_flush_all);
#endif
}

static void
r92c_handle_c2h_task(struct rtwn_softc *sc, union sec_param *data)
{
	const uint16_t off = R92C_C2H_EVT_MSG + sizeof(struct r92c_c2h_evt);
	struct r92c_softc *rs = sc->sc_priv;
	uint16_t buf[R92C_C2H_MSG_MAX_LEN / 2 + 1];
	uint8_t id, len, status;
	int i;

	/* Do not reschedule the task if device is not running. */
	if (!(sc->sc_flags & RTWN_RUNNING))
		return;

	/* Read current status. */
	status = rtwn_read_1(sc, R92C_C2H_EVT_CLEAR);
	if (status == R92C_C2H_EVT_HOST_CLOSE)
		goto end;	/* nothing to do */
	else if (status == R92C_C2H_EVT_FW_CLOSE) {
		len = rtwn_read_1(sc, R92C_C2H_EVT_MSG);
		id = MS(len, R92C_C2H_EVTB0_ID);
		len = MS(len, R92C_C2H_EVTB0_LEN);

		memset(buf, 0, sizeof(buf));
		/* Try to optimize event reads. */
		for (i = 0; i < len; i += 2)
			buf[i / 2] = rtwn_read_2(sc, off + i);
		KASSERT(i < sizeof(buf), ("%s: buffer overrun (%d >= %zu)!",
		    __func__, i, sizeof(buf)));

		switch (id) {
		case R92C_C2H_EVT_TX_REPORT:
			r92c_ratectl_tx_complete(sc, (uint8_t *)buf, len);
			break;
		default:
			device_printf(sc->sc_dev,
			    "%s: C2H report %u (len %u) was not handled\n",
			    __func__, id, len);
			break;
		}
	}

	/* Prepare for next event. */
	rtwn_write_1(sc, R92C_C2H_EVT_CLEAR, R92C_C2H_EVT_HOST_CLOSE);

end:
	/* Adjust timeout for next call. */
	if (rs->rs_c2h_pending != 0) {
		rs->rs_c2h_pending = 0;
		rs->rs_c2h_paused = 0;
	} else
		rs->rs_c2h_paused++;

	if (rs->rs_c2h_paused > R92C_TX_PAUSED_THRESHOLD)
		rs->rs_c2h_timeout = hz;
	else
		rs->rs_c2h_timeout = MAX(hz / 100, 1);

	/* Reschedule the task. */
	callout_reset(&rs->rs_c2h_report, rs->rs_c2h_timeout,
	    r92c_handle_c2h_report, sc);
}

void
r92c_handle_c2h_report(void *arg)
{
	struct rtwn_softc *sc = arg;

	rtwn_cmd_sleepable(sc, NULL, 0, r92c_handle_c2h_task);
}

#endif	/* RTWN_WITHOUT_UCODE */
