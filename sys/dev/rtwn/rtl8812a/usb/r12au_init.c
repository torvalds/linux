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

#include <dev/rtwn/rtl8812a/r12a_var.h>

#include <dev/rtwn/rtl8812a/usb/r12au.h>
#include <dev/rtwn/rtl8812a/usb/r12au_reg.h>


void
r12au_init_rx_agg(struct rtwn_softc *sc)
{
	struct r12a_softc *rs = sc->sc_priv;

	/* Rx aggregation (USB). */
	rtwn_write_2(sc, R92C_RXDMA_AGG_PG_TH,
	    rs->ac_usb_dma_size | (rs->ac_usb_dma_time << 8));
	rtwn_setbits_1(sc, R92C_TRXDMA_CTRL, 0,
	    R92C_TRXDMA_CTRL_RXDMA_AGG_EN);
}

void
r12au_init_burstlen_usb2(struct rtwn_softc *sc)
{
	const uint8_t dma_count = R12A_DMA_MODE | SM(R12A_BURST_CNT, 3);

	if ((rtwn_read_1(sc, R92C_USB_INFO) & 0x30) == 0) {
		/* Set burst packet length to 512 B. */
		rtwn_setbits_1(sc, R12A_RXDMA_PRO, R12A_BURST_SZ_M,
		    dma_count | SM(R12A_BURST_SZ, R12A_BURST_SZ_USB2));
	} else {
		/* Set burst packet length to 64 B. */
		rtwn_setbits_1(sc, R12A_RXDMA_PRO, R12A_BURST_SZ_M,
		    dma_count | SM(R12A_BURST_SZ, R12A_BURST_SZ_USB1));
	}
}

void
r12au_init_burstlen(struct rtwn_softc *sc)
{
	const uint8_t dma_count = R12A_DMA_MODE | SM(R12A_BURST_CNT, 3);

	if (rtwn_read_1(sc, R92C_TYPE_ID + 3) & 0x80)
		r12au_init_burstlen_usb2(sc);
	else {		/* USB 3.0 */
		/* Set burst packet length to 1 KB. */
		rtwn_setbits_1(sc, R12A_RXDMA_PRO, R12A_BURST_SZ_M,
		    dma_count | SM(R12A_BURST_SZ, R12A_BURST_SZ_USB3));

		rtwn_setbits_1(sc, 0xf008, 0x18, 0);
	}
}

static void
r12au_arfb_init(struct rtwn_softc *sc)
{
	/* ARFB table 9 for 11ac 5G 2SS. */
	rtwn_write_4(sc, R12A_ARFR_5G(0), 0x00000010);
	rtwn_write_4(sc, R12A_ARFR_5G(0) + 4, 0xfffff000);

	/* ARFB table 10 for 11ac 5G 1SS. */
	rtwn_write_4(sc, R12A_ARFR_5G(1), 0x00000010);
	rtwn_write_4(sc, R12A_ARFR_5G(1) + 4, 0x003ff000);

	/* ARFB table 11 for 11ac 2G 1SS. */
	rtwn_write_4(sc, R12A_ARFR_2G(0), 0x00000015);
	rtwn_write_4(sc, R12A_ARFR_2G(0) + 4, 0x003ff000);

	/* ARFB table 12 for 11ac 2G 2SS. */
	rtwn_write_4(sc, R12A_ARFR_2G(1), 0x00000015);
	rtwn_write_4(sc, R12A_ARFR_2G(1) + 4, 0xffcff000);
}

void
r12au_init_ampdu_fwhw(struct rtwn_softc *sc)
{
	rtwn_setbits_1(sc, R92C_FWHW_TXQ_CTRL,
	    R92C_FWHW_TXQ_CTRL_AMPDU_RTY_NEW, 0);
}

void
r12au_init_ampdu(struct rtwn_softc *sc)
{
	struct r12a_softc *rs = sc->sc_priv;

	/* Rx interval (USB3). */
	rtwn_write_1(sc, 0xf050, 0x01);

	/* burst length = 4 */
	rtwn_write_2(sc, R92C_RXDMA_STATUS, 0x7400);

	rtwn_write_1(sc, R92C_RXDMA_STATUS + 1, 0xf5);

	/* Setup AMPDU aggregation. */
	rtwn_write_1(sc, R12A_AMPDU_MAX_TIME, rs->ampdu_max_time);
	rtwn_write_4(sc, R12A_AMPDU_MAX_LENGTH, 0xffffffff);

	/* 80 MHz clock (again?) */
	rtwn_write_1(sc, R92C_USTIME_TSF, 0x50);
	rtwn_write_1(sc, R92C_USTIME_EDCA, 0x50);

	rtwn_r12a_init_burstlen(sc);

	/* Enable single packet AMPDU. */
	rtwn_setbits_1(sc, R12A_HT_SINGLE_AMPDU, 0,
	    R12A_HT_SINGLE_AMPDU_PKT_ENA);

	/* 11K packet length for VHT. */
	rtwn_write_1(sc, R92C_RX_PKT_LIMIT, 0x18);

	rtwn_write_1(sc, R92C_PIFS, 0);

	rtwn_write_2(sc, R92C_MAX_AGGR_NUM, 0x1f1f);

	rtwn_r12a_init_ampdu_fwhw(sc);

	/* Do not reset MAC. */
	rtwn_setbits_1(sc, R92C_RSV_CTRL, 0, 0x60);

	r12au_arfb_init(sc);
}

void
r12au_post_init(struct rtwn_softc *sc)
{

	/* Setup RTS BW (equal to data BW). */
	rtwn_setbits_1(sc, R92C_QUEUE_CTRL, 0x08, 0);

	rtwn_write_1(sc, R12A_EARLY_MODE_CONTROL + 3, 0x01);

	/* Reset USB mode switch setting. */
	rtwn_write_1(sc, R12A_SDIO_CTRL, 0);
	rtwn_write_1(sc, R92C_ACLK_MON, 0);

	rtwn_write_1(sc, R92C_USB_HRPWM, 0);

#ifndef RTWN_WITHOUT_UCODE
	if (sc->sc_flags & RTWN_FW_LOADED) {
		if (sc->sc_ratectl_sysctl == RTWN_RATECTL_FW) {
			/* TODO: implement */
			sc->sc_ratectl = RTWN_RATECTL_NET80211;
		} else
			sc->sc_ratectl = sc->sc_ratectl_sysctl;
	} else
#endif
		sc->sc_ratectl = RTWN_RATECTL_NONE;
}
