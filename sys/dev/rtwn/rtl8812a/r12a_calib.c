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

#include <dev/rtwn/rtl8812a/r12a.h>
#include <dev/rtwn/rtl8812a/r12a_priv.h>
#include <dev/rtwn/rtl8812a/r12a_reg.h>
#include <dev/rtwn/rtl8812a/r12a_var.h>


void
r12a_lc_calib(struct rtwn_softc *sc)
{
	uint32_t chnlbw;
	uint8_t txmode;

	RTWN_DPRINTF(sc, RTWN_DEBUG_CALIB,
	    "%s: LC calibration started\n", __func__);

	txmode = rtwn_read_1(sc, R12A_SINGLETONE_CONT_TX + 2);

	if ((txmode & 0x07) != 0) {
		/* Disable all continuous Tx. */
		/*
		 * Skipped because BB turns off continuous Tx until
		 * next packet comes in.
		 */
	} else {
		/* Block all Tx queues. */
		rtwn_write_1(sc, R92C_TXPAUSE, R92C_TX_QUEUE_ALL);
	}

	/* Enter LCK mode. */
	rtwn_rf_setbits(sc, 0, R12A_RF_LCK, 0, R12A_RF_LCK_MODE);

	/* Start calibration. */
	chnlbw = rtwn_rf_read(sc, 0, R92C_RF_CHNLBW);
	rtwn_rf_write(sc, 0, R92C_RF_CHNLBW, chnlbw | R92C_RF_CHNLBW_LCSTART);

	/* Give calibration the time to complete. */
	rtwn_delay(sc, 150000);	/* 150 ms */

	/* Leave LCK mode. */
	rtwn_rf_setbits(sc, 0, R12A_RF_LCK, R12A_RF_LCK_MODE, 0);

	/* Restore configuration. */
	if ((txmode & 0x07) != 0) {
		/* Continuous Tx case. */
		/*
		 * Skipped because BB turns off continuous Tx until
		 * next packet comes in.
		 */
	} else {
		/* Unblock all Tx queues. */
		rtwn_write_1(sc, R92C_TXPAUSE, 0);
	}

	/* Recover channel number. */
	rtwn_rf_write(sc, 0, R92C_RF_CHNLBW, chnlbw);

	RTWN_DPRINTF(sc, RTWN_DEBUG_CALIB,
	    "%s: LC calibration finished\n", __func__);
}

#ifndef RTWN_WITHOUT_UCODE
int
r12a_iq_calib_fw_supported(struct rtwn_softc *sc)
{
	if (sc->fwver == 0x19)
		return (1);

	return (0);
}
#endif

void
r12a_save_bb_afe_vals(struct rtwn_softc *sc, uint32_t vals[],
    const uint16_t regs[], int size)
{
	int i;

	/* Select page C. */
	rtwn_bb_setbits(sc, R12A_TXAGC_TABLE_SELECT, 0x80000000, 0);

	for (i = 0; i < size; i++)
		vals[i] = rtwn_bb_read(sc, regs[i]);
}

void
r12a_restore_bb_afe_vals(struct rtwn_softc *sc, uint32_t vals[],
    const uint16_t regs[], int size)
{
	int i;

	/* Select page C. */
	rtwn_bb_setbits(sc, R12A_TXAGC_TABLE_SELECT, 0x80000000, 0);

	for (i = 0; i < size; i++)
		rtwn_bb_write(sc, regs[i], vals[i]);
}

void
r12a_save_rf_vals(struct rtwn_softc *sc, uint32_t vals[],
    const uint8_t regs[], int size)
{
	int c, i;

	/* Select page C. */
	rtwn_bb_setbits(sc, R12A_TXAGC_TABLE_SELECT, 0x80000000, 0);

	for (c = 0; c < sc->nrxchains; c++)
		for (i = 0; i < size; i++)
			vals[c * size + i] = rtwn_rf_read(sc, c, regs[i]);
}

void
r12a_restore_rf_vals(struct rtwn_softc *sc, uint32_t vals[],
    const uint8_t regs[], int size)
{
	int c, i;

	/* Select page C. */
	rtwn_bb_setbits(sc, R12A_TXAGC_TABLE_SELECT, 0x80000000, 0);

	for (c = 0; c < sc->nrxchains; c++)
		for (i = 0; i < size; i++)
			rtwn_rf_write(sc, c, regs[i], vals[c * size + i]);
}

#ifdef RTWN_TODO
static void
r12a_iq_tx(struct rtwn_softc *sc)
{
	/* TODO */
}

static void
r12a_iq_config_mac(struct rtwn_softc *sc)
{

	/* Select page C. */
	rtwn_bb_setbits(sc, R12A_TXAGC_TABLE_SELECT, 0x80000000, 0);
	rtwn_write_1(sc, R92C_TXPAUSE,
	    R92C_TX_QUEUE_AC | R92C_TX_QUEUE_MGT | R92C_TX_QUEUE_HIGH);
	/* BCN_CTRL & BCN_CTRL1 */
	rtwn_setbits_1(sc, R92C_BCN_CTRL(0), R92C_BCN_CTRL_EN_BCN, 0);
	rtwn_setbits_1(sc, R92C_BCN_CTRL(1), R92C_BCN_CTRL_EN_BCN, 0);
	/* Rx ant off */
	rtwn_write_1(sc, R12A_OFDMCCK_EN, 0);
	/* CCA off */
	rtwn_bb_setbits(sc, R12A_CCA_ON_SEC, 0x03, 0x0c);
	/* CCK RX Path off */
	rtwn_write_1(sc, R12A_CCK_RX_PATH + 3, 0x0f);
}
#endif

void
r12a_iq_calib_sw(struct rtwn_softc *sc)
{
#define R12A_MAX_NRXCHAINS	2
	uint32_t bb_vals[nitems(r12a_iq_bb_regs)];
	uint32_t afe_vals[nitems(r12a_iq_afe_regs)];
	uint32_t rf_vals[nitems(r12a_iq_rf_regs) * R12A_MAX_NRXCHAINS];
	uint32_t rfe[2];

	KASSERT(sc->nrxchains <= R12A_MAX_NRXCHAINS,
	    ("nrxchains > %d (%d)\n", R12A_MAX_NRXCHAINS, sc->nrxchains));

	/* Save registers. */
	r12a_save_bb_afe_vals(sc, bb_vals, r12a_iq_bb_regs,
	    nitems(r12a_iq_bb_regs));

	/* Select page C1. */
	rtwn_bb_setbits(sc, R12A_TXAGC_TABLE_SELECT, 0, 0x80000000);
	rfe[0] = rtwn_bb_read(sc, R12A_RFE(0));
	rfe[1] = rtwn_bb_read(sc, R12A_RFE(1));

	r12a_save_bb_afe_vals(sc, afe_vals, r12a_iq_afe_regs,
	    nitems(r12a_iq_afe_regs));
	r12a_save_rf_vals(sc, rf_vals, r12a_iq_rf_regs,
	    nitems(r12a_iq_rf_regs));

#ifdef RTWN_TODO
	/* Configure MAC. */
	rtwn_iq_config_mac(sc);
	rtwn_iq_tx(sc);
#endif

	r12a_restore_rf_vals(sc, rf_vals, r12a_iq_rf_regs,
	    nitems(r12a_iq_rf_regs));
	r12a_restore_bb_afe_vals(sc, afe_vals, r12a_iq_afe_regs,
	    nitems(r12a_iq_afe_regs));

	/* Select page C1. */
	rtwn_bb_setbits(sc, R12A_TXAGC_TABLE_SELECT, 0, 0x80000000);

	/* Chain 0. */
	rtwn_bb_write(sc, R12A_SLEEP_NAV(0), 0);
	rtwn_bb_write(sc, R12A_PMPD(0), 0);
	rtwn_bb_write(sc, 0xc88, 0);
	rtwn_bb_write(sc, 0xc8c, 0x3c000000);
	rtwn_bb_setbits(sc, 0xc90, 0, 0x00000080);
	rtwn_bb_setbits(sc, 0xcc4, 0, 0x20040000);
	rtwn_bb_setbits(sc, 0xcc8, 0, 0x20000000);

	/* Chain 1. */
	rtwn_bb_write(sc, R12A_SLEEP_NAV(1), 0);
	rtwn_bb_write(sc, R12A_PMPD(1), 0);
	rtwn_bb_write(sc, 0xe88, 0);
	rtwn_bb_write(sc, 0xe8c, 0x3c000000);
	rtwn_bb_setbits(sc, 0xe90, 0, 0x00000080);
	rtwn_bb_setbits(sc, 0xec4, 0, 0x20040000);
	rtwn_bb_setbits(sc, 0xec8, 0, 0x20000000);

	rtwn_bb_write(sc, R12A_RFE(0), rfe[0]);
	rtwn_bb_write(sc, R12A_RFE(1), rfe[1]);

	r12a_restore_bb_afe_vals(sc, bb_vals, r12a_iq_bb_regs,
	    nitems(r12a_iq_bb_regs));
#undef R12A_MAX_NRXCHAINS
}

void
r12a_iq_calib(struct rtwn_softc *sc)
{
#ifndef RTWN_WITHOUT_UCODE
	if ((sc->sc_flags & RTWN_FW_LOADED) &&
	    rtwn_r12a_iq_calib_fw_supported(sc))
		r12a_iq_calib_fw(sc);
	else
#endif
		rtwn_r12a_iq_calib_sw(sc);
}
