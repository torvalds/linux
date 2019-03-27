/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2004-2006 Marcel Moolenaar
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/conf.h>
#include <machine/bus.h>
#include <sys/rman.h>
#include <sys/serial.h>

#include <dev/scc/scc_bfe.h>
#include <dev/scc/scc_bus.h>

#include <dev/ic/z8530.h>

#include "scc_if.h"

static int z8530_bfe_attach(struct scc_softc *, int);
static int z8530_bfe_iclear(struct scc_softc *, struct scc_chan *);
static int z8530_bfe_ipend(struct scc_softc *);
static int z8530_bfe_probe(struct scc_softc *);

static kobj_method_t z8530_methods[] = {
	KOBJMETHOD(scc_attach,	z8530_bfe_attach),
	KOBJMETHOD(scc_iclear,	z8530_bfe_iclear),
	KOBJMETHOD(scc_ipend,	z8530_bfe_ipend),
	KOBJMETHOD(scc_probe,	z8530_bfe_probe),
	KOBJMETHOD_END
};

struct scc_class scc_z8530_class = {
	"z8530 class",
	z8530_methods,
	sizeof(struct scc_softc),
	.cl_channels = 2,
	.cl_class = SCC_CLASS_Z8530,
	.cl_modes = SCC_MODE_ASYNC | SCC_MODE_BISYNC | SCC_MODE_HDLC,
	.cl_range = CHAN_B - CHAN_A,
};

/* Multiplexed I/O. */
static __inline uint8_t
scc_getmreg(struct scc_bas *bas, int ch, int reg)
{

	scc_setreg(bas, ch + REG_CTRL, reg);
	scc_barrier(bas);
	return (scc_getreg(bas, ch + REG_CTRL));
}

static int
z8530_bfe_attach(struct scc_softc *sc __unused, int reset __unused)
{

	return (0);
}

static int
z8530_bfe_iclear(struct scc_softc *sc, struct scc_chan *ch)
{
	struct scc_bas *bas;
	int c;

	bas = &sc->sc_bas;
	c = (ch->ch_nr == 1) ? CHAN_A : CHAN_B;
	mtx_lock_spin(&sc->sc_hwmtx);
	if (ch->ch_ipend & SER_INT_TXIDLE) {
		scc_setreg(bas, c + REG_CTRL, CR_RSTTXI);
		scc_barrier(bas);
	}
	if (ch->ch_ipend & SER_INT_RXREADY) {
		scc_getreg(bas, c + REG_DATA);
		scc_barrier(bas);
	}
	if (ch->ch_ipend & (SER_INT_OVERRUN|SER_INT_BREAK))
		scc_setreg(bas, c + REG_CTRL, CR_RSTERR);
	mtx_unlock_spin(&sc->sc_hwmtx);
	return (0);
}

#define	SIGCHG(c, i, s, d)				\
	if (c) {					\
		i |= (i & s) ? s : s | d;		\
	} else {					\
		i = (i & s) ? (i & ~s) | d : i;		\
	}

static int
z8530_bfe_ipend(struct scc_softc *sc)
{
	struct scc_bas *bas;
	struct scc_chan *ch[2];
	uint32_t sig;
	uint8_t bes, ip, src;

	bas = &sc->sc_bas;
	ch[0] = &sc->sc_chan[0];
	ch[1] = &sc->sc_chan[1];
	ch[0]->ch_ipend = 0;
	ch[1]->ch_ipend = 0;

	mtx_lock_spin(&sc->sc_hwmtx);
	ip = scc_getmreg(bas, CHAN_A, RR_IP);
	if (ip & IP_RIA)
		ch[0]->ch_ipend |= SER_INT_RXREADY;
	if (ip & IP_RIB)
		ch[1]->ch_ipend |= SER_INT_RXREADY;
	if (ip & IP_TIA)
		ch[0]->ch_ipend |= SER_INT_TXIDLE;
	if (ip & IP_TIB)
		ch[1]->ch_ipend |= SER_INT_TXIDLE;
	if (ip & IP_SIA) {
		bes = scc_getmreg(bas, CHAN_A, CR_RSTXSI);
		if (bes & BES_BRK)
			ch[0]->ch_ipend |= SER_INT_BREAK;
		sig = ch[0]->ch_hwsig;
		SIGCHG(bes & BES_CTS, sig, SER_CTS, SER_DCTS);
		SIGCHG(bes & BES_DCD, sig, SER_DCD, SER_DDCD);
		SIGCHG(bes & BES_SYNC, sig, SER_DSR, SER_DDSR);
		if (sig & SER_MASK_DELTA) {
			ch[0]->ch_hwsig = sig;
			ch[0]->ch_ipend |= SER_INT_SIGCHG;
		}
		src = scc_getmreg(bas, CHAN_A, RR_SRC);
		if (src & SRC_OVR)
			ch[0]->ch_ipend |= SER_INT_OVERRUN;
	}
	if (ip & IP_SIB) {
		bes = scc_getmreg(bas, CHAN_B, CR_RSTXSI);
		if (bes & BES_BRK)
			ch[1]->ch_ipend |= SER_INT_BREAK;
		sig = ch[1]->ch_hwsig;
		SIGCHG(bes & BES_CTS, sig, SER_CTS, SER_DCTS);
		SIGCHG(bes & BES_DCD, sig, SER_DCD, SER_DDCD);
		SIGCHG(bes & BES_SYNC, sig, SER_DSR, SER_DDSR);
		if (sig & SER_MASK_DELTA) {
			ch[1]->ch_hwsig = sig;
			ch[1]->ch_ipend |= SER_INT_SIGCHG;
		}
		src = scc_getmreg(bas, CHAN_B, RR_SRC);
		if (src & SRC_OVR)
			ch[1]->ch_ipend |= SER_INT_OVERRUN;
	}
	mtx_unlock_spin(&sc->sc_hwmtx);

	return (ch[0]->ch_ipend | ch[1]->ch_ipend);
}

static int
z8530_bfe_probe(struct scc_softc *sc __unused)
{

	return (0);
}
