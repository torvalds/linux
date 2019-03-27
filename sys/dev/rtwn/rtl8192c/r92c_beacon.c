/*-
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

#include <dev/rtwn/if_rtwnvar.h>
#include <dev/rtwn/if_rtwnreg.h>

#include <dev/rtwn/if_rtwn_ridx.h>

#include <dev/rtwn/rtl8192c/r92c.h>
#include <dev/rtwn/rtl8192c/r92c_var.h>
#include <dev/rtwn/rtl8192c/r92c_reg.h>
#include <dev/rtwn/rtl8192c/r92c_tx_desc.h>


void
r92c_beacon_init(struct rtwn_softc *sc, void *buf, int id)
{
	struct r92c_tx_desc *txd = (struct r92c_tx_desc *)buf;

	/*
	 * NB: there is no need to setup HWSEQ_EN bit;
	 * QSEL_BEACON already implies it.
	 */
	txd->flags0 |= R92C_FLAGS0_BMCAST | R92C_FLAGS0_FSG | R92C_FLAGS0_LSG;
	txd->txdw1 |= htole32(
	    SM(R92C_TXDW1_QSEL, R92C_TXDW1_QSEL_BEACON) |
	    SM(R92C_TXDW1_RAID, R92C_RAID_11B));

	rtwn_r92c_tx_setup_macid(sc, buf, id);
	txd->txdw4 |= htole32(R92C_TXDW4_DRVRATE);
	txd->txdw4 |= htole32(SM(R92C_TXDW4_SEQ_SEL, id));
	txd->txdw4 |= htole32(SM(R92C_TXDW4_PORT_ID, id));
	txd->txdw5 |= htole32(SM(R92C_TXDW5_DATARATE, RTWN_RIDX_CCK1));
}

void
r92c_beacon_enable(struct rtwn_softc *sc, int id, int enable)
{

	if (enable) {
		rtwn_setbits_1(sc, R92C_BCN_CTRL(id),
		    0, R92C_BCN_CTRL_EN_BCN);
	} else {
		rtwn_setbits_1(sc, R92C_BCN_CTRL(id),
		    R92C_BCN_CTRL_EN_BCN, 0);
	}
}
