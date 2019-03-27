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

#include <dev/rtwn/rtl8192c/r92c_var.h>

#include <dev/rtwn/rtl8188e/r88e.h>
#include <dev/rtwn/rtl8188e/r88e_priv.h>
#include <dev/rtwn/rtl8188e/r88e_rom_image.h>


void
r88e_parse_rom(struct rtwn_softc *sc, uint8_t *buf)
{
	struct r92c_softc *rs = sc->sc_priv;
	struct rtwn_r88e_txpwr *rt = rs->rs_txpwr;
	struct r88e_rom *rom = (struct r88e_rom *)buf;
	int i;

	rt->bw20_tx_pwr_diff = RTWN_SIGN4TO8(MS(rom->tx_pwr_diff, HIGH_PART));
	rt->ofdm_tx_pwr_diff = RTWN_SIGN4TO8(MS(rom->tx_pwr_diff, LOW_PART));
	for (i = 0; i < nitems(rom->cck_tx_pwr); i++)
		rt->cck_tx_pwr[i] = rom->cck_tx_pwr[i];
	for (i = 0; i < nitems(rom->ht40_tx_pwr); i++)
		rt->ht40_tx_pwr[i] = rom->ht40_tx_pwr[i];

	rs->crystalcap = RTWN_GET_ROM_VAR(rom->crystalcap,
	    R88E_ROM_CRYSTALCAP_DEF);
	rs->regulatory = MS(rom->rf_board_opt, R92C_ROM_RF1_REGULATORY);
	rs->board_type =
	    MS(RTWN_GET_ROM_VAR(rom->rf_board_opt, R92C_BOARD_TYPE_DONGLE),
		R92C_ROM_RF1_BOARD_TYPE);
	RTWN_DPRINTF(sc, RTWN_DEBUG_ROM, "%s: regulatory type %d\n",
	    __func__,rs->regulatory);

	sc->thermal_meter = rom->thermal_meter;

	rtwn_r92c_set_rom_opts(sc, buf);
}
