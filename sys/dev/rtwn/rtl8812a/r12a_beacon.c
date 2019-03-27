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

#include <dev/rtwn/if_rtwn_ridx.h>

#include <dev/rtwn/rtl8812a/r12a.h>
#include <dev/rtwn/rtl8812a/r12a_reg.h>
#include <dev/rtwn/rtl8812a/r12a_tx_desc.h>


void
r12a_beacon_init(struct rtwn_softc *sc, void *buf, int id)
{
	struct r12a_tx_desc *txd = (struct r12a_tx_desc *)buf;

	txd->flags0 = R12A_FLAGS0_LSG | R12A_FLAGS0_FSG | R12A_FLAGS0_BMCAST;

	/*
	 * NB: there is no need to setup HWSEQ_EN bit;
	 * QSEL_BEACON already implies it.
	 */
	txd->txdw1 = htole32(SM(R12A_TXDW1_QSEL, R12A_TXDW1_QSEL_BEACON));
	txd->txdw1 |= htole32(SM(R12A_TXDW1_MACID, RTWN_MACID_BC));

	txd->txdw3 = htole32(R12A_TXDW3_DRVRATE);
	txd->txdw3 |= htole32(SM(R12A_TXDW3_SEQ_SEL, id));

	txd->txdw4 = htole32(SM(R12A_TXDW4_DATARATE, RTWN_RIDX_CCK1));

	txd->txdw6 = htole32(SM(R21A_TXDW6_MBSSID, id));
}

void
r12a_beacon_set_rate(void *buf, int is5ghz)
{
	struct r12a_tx_desc *txd = (struct r12a_tx_desc *)buf;

	txd->txdw4 &= ~htole32(R12A_TXDW4_DATARATE_M);
	if (is5ghz) {
		txd->txdw4 = htole32(SM(R12A_TXDW4_DATARATE,
		    RTWN_RIDX_OFDM6));
	} else
		txd->txdw4 = htole32(SM(R12A_TXDW4_DATARATE, RTWN_RIDX_CCK1));
}
