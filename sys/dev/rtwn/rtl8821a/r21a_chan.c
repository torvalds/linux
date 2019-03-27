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

#include <dev/rtwn/if_rtwnvar.h>

#include <dev/rtwn/if_rtwn_ridx.h>
#include <dev/rtwn/if_rtwn_rx.h>

#include <dev/rtwn/rtl8812a/r12a_var.h>

#include <dev/rtwn/rtl8821a/r21a.h>
#include <dev/rtwn/rtl8821a/r21a_reg.h>


static void
r21a_bypass_ext_lna_2ghz(struct rtwn_softc *sc)
{
	rtwn_bb_setbits(sc, R12A_RFE_INV(0), 0x00100000, 0);
	rtwn_bb_setbits(sc, R12A_RFE_INV(0), 0x00400000, 0);
	rtwn_bb_setbits(sc, R12A_RFE_PINMUX(0), 0, 0x07);
	rtwn_bb_setbits(sc, R12A_RFE_PINMUX(0), 0, 0x0700);
}

void
r21a_set_band_2ghz(struct rtwn_softc *sc, uint32_t basicrates)
{
	struct r12a_softc *rs = sc->sc_priv;

	/* Enable CCK / OFDM. */
	rtwn_bb_setbits(sc, R12A_OFDMCCK_EN,
	    0, R12A_OFDMCCK_EN_CCK | R12A_OFDMCCK_EN_OFDM);

	/* Turn off RF PA and LNA. */
	rtwn_bb_setbits(sc, R12A_RFE_PINMUX(0),
	    R12A_RFE_PINMUX_LNA_MASK, 0x7000);
	rtwn_bb_setbits(sc, R12A_RFE_PINMUX(0),
	    R12A_RFE_PINMUX_PA_A_MASK, 0x70);

	if (rs->ext_lna_2g) {
		/* Turn on 2.4 GHz external LNA. */
		rtwn_bb_setbits(sc, R12A_RFE_INV(0), 0, 0x00100000);
		rtwn_bb_setbits(sc, R12A_RFE_INV(0), 0x00400000, 0);
		rtwn_bb_setbits(sc, R12A_RFE_PINMUX(0), 0x05, 0x02);
		rtwn_bb_setbits(sc, R12A_RFE_PINMUX(0), 0x0500, 0x0200);
	} else {
		/* Bypass 2.4 GHz external LNA. */
		r21a_bypass_ext_lna_2ghz(sc);
	}

	/* Select AGC table. */
	rtwn_bb_setbits(sc, R12A_TX_SCALE(0), 0x0f00, 0);

	rtwn_bb_setbits(sc, R12A_TX_PATH, 0xf0, 0x10);
	rtwn_bb_setbits(sc, R12A_CCK_RX_PATH, 0x0f000000, 0x01000000);

	/* Write basic rates. */
	rtwn_set_basicrates(sc, basicrates);

	rtwn_write_1(sc, R12A_CCK_CHECK, 0);
}

void
r21a_set_band_5ghz(struct rtwn_softc *sc, uint32_t basicrates)
{
	struct r12a_softc *rs = sc->sc_priv;
	int ntries;

	rtwn_bb_setbits(sc, R12A_RFE_PINMUX(0),
	    R12A_RFE_PINMUX_LNA_MASK, 0x5000);
	rtwn_bb_setbits(sc, R12A_RFE_PINMUX(0),
	    R12A_RFE_PINMUX_PA_A_MASK, 0x40);

	if (rs->ext_lna_2g) {
		/* Bypass 2.4 GHz external LNA. */
		r21a_bypass_ext_lna_2ghz(sc);
	}

	rtwn_write_1(sc, R12A_CCK_CHECK, R12A_CCK_CHECK_5GHZ);

	for (ntries = 0; ntries < 100; ntries++) {
		if ((rtwn_read_2(sc, R12A_TXPKT_EMPTY) & 0x30) == 0x30)
			break;

		rtwn_delay(sc, 25);
	}
	if (ntries == 100) {
		device_printf(sc->sc_dev,
		    "%s: TXPKT_EMPTY check failed (%04X)\n",
		    __func__, rtwn_read_2(sc, R12A_TXPKT_EMPTY));
	}

	/* Enable OFDM. */
	rtwn_bb_setbits(sc, R12A_OFDMCCK_EN, R12A_OFDMCCK_EN_CCK,
	    R12A_OFDMCCK_EN_OFDM);

	/* Select AGC table. */
	rtwn_bb_setbits(sc, R12A_TX_SCALE(0), 0x0f00, 0x0100);

	rtwn_bb_setbits(sc, R12A_TX_PATH, 0xf0, 0);
	rtwn_bb_setbits(sc, R12A_CCK_RX_PATH, 0, 0x0f000000);

	/* Write basic rates. */
	rtwn_set_basicrates(sc, basicrates);
}
