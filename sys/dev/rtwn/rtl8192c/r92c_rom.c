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
#include <dev/rtwn/if_rtwn_efuse.h>

#include <dev/rtwn/rtl8192c/r92c.h>
#include <dev/rtwn/rtl8192c/r92c_priv.h>
#include <dev/rtwn/rtl8192c/r92c_var.h>
#include <dev/rtwn/rtl8192c/r92c_rom_image.h>


static void
r92c_set_chains(struct rtwn_softc *sc)
{
	struct r92c_softc *rs = sc->sc_priv;

	if (rs->chip & R92C_CHIP_92C) {
		sc->ntxchains = (rs->chip & R92C_CHIP_92C_1T2R) ? 1 : 2;
		sc->nrxchains = 2;
	} else {
		sc->ntxchains = 1;
		sc->nrxchains = 1;
	}
}

void
r92c_efuse_postread(struct rtwn_softc *sc)
{
	struct r92c_softc *rs = sc->sc_priv;

	/* XXX Weird but this is what the vendor driver does. */
	sc->next_rom_addr = 0x1fa;
	(void) rtwn_efuse_read_next(sc, &rs->pa_setting);
	RTWN_DPRINTF(sc, RTWN_DEBUG_ROM, "%s: PA setting=0x%x\n", __func__,
	    rs->pa_setting);
}

void
r92c_parse_rom(struct rtwn_softc *sc, uint8_t *buf)
{
	struct r92c_softc *rs = sc->sc_priv;
	struct rtwn_r92c_txpwr *rt = rs->rs_txpwr;
	struct r92c_rom *rom = (struct r92c_rom *)buf;
	int i, j;

	rs->board_type = MS(rom->rf_opt1, R92C_ROM_RF1_BOARD_TYPE);
	rs->regulatory = MS(rom->rf_opt1, R92C_ROM_RF1_REGULATORY);
	RTWN_DPRINTF(sc, RTWN_DEBUG_ROM, "%s: regulatory type=%d\n",
	    __func__, rs->regulatory);

	/* Need to be set before postinit() (but after preinit()). */
	rtwn_r92c_set_rom_opts(sc, buf);
	r92c_set_chains(sc);

	for (j = 0; j < R92C_GROUP_2G; j++) {
		for (i = 0; i < sc->ntxchains; i++) {
			rt->cck_tx_pwr[i][j] = rom->cck_tx_pwr[i][j];
			rt->ht40_1s_tx_pwr[i][j] = rom->ht40_1s_tx_pwr[i][j];
		}

		rt->ht40_2s_tx_pwr_diff[0][j] =
		    MS(rom->ht40_2s_tx_pwr_diff[j], LOW_PART);
		rt->ht20_tx_pwr_diff[0][j] =
		    RTWN_SIGN4TO8(MS(rom->ht20_tx_pwr_diff[j],
			LOW_PART));
		rt->ofdm_tx_pwr_diff[0][j] =
		    MS(rom->ofdm_tx_pwr_diff[j], LOW_PART);
		rt->ht40_max_pwr[0][j] =
		    MS(rom->ht40_max_pwr[j], LOW_PART);
		rt->ht20_max_pwr[0][j] =
		    MS(rom->ht20_max_pwr[j], LOW_PART);

		if (sc->ntxchains > 1) {
			rt->ht40_2s_tx_pwr_diff[1][j] =
			    MS(rom->ht40_2s_tx_pwr_diff[j], HIGH_PART);
			rt->ht20_tx_pwr_diff[1][j] =
			    RTWN_SIGN4TO8(MS(rom->ht20_tx_pwr_diff[j],
				HIGH_PART));
			rt->ofdm_tx_pwr_diff[1][j] =
			    MS(rom->ofdm_tx_pwr_diff[j], HIGH_PART);
			rt->ht40_max_pwr[1][j] =
			    MS(rom->ht40_max_pwr[j], HIGH_PART);
			rt->ht20_max_pwr[1][j] =
			    MS(rom->ht20_max_pwr[j], HIGH_PART);
		}
	}

	sc->thermal_meter = MS(rom->thermal_meter, R92C_ROM_THERMAL_METER);
	if (sc->thermal_meter == R92C_ROM_THERMAL_METER_M)
		sc->thermal_meter = 0xff;
	IEEE80211_ADDR_COPY(sc->sc_ic.ic_macaddr, rom->macaddr);
}
