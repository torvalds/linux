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

#include <dev/ic/quicc.h>

#include "scc_if.h"

#define	quicc_read2(bas, reg)		\
	bus_space_read_2((bas)->bst, (bas)->bsh, reg)
#define	quicc_read4(bas, reg)		\
	bus_space_read_4((bas)->bst, (bas)->bsh, reg)

#define	quicc_write2(bas, reg, val)	\
	bus_space_write_2((bas)->bst, (bas)->bsh, reg, val)
#define	quicc_write4(bas, reg, val)	\
	bus_space_write_4((bas)->bst, (bas)->bsh, reg, val)

static int quicc_bfe_attach(struct scc_softc *, int);
static int quicc_bfe_enabled(struct scc_softc *, struct scc_chan *);
static int quicc_bfe_iclear(struct scc_softc *, struct scc_chan *);
static int quicc_bfe_ipend(struct scc_softc *);
static int quicc_bfe_probe(struct scc_softc *);

static kobj_method_t quicc_methods[] = {
	KOBJMETHOD(scc_attach,	quicc_bfe_attach),
	KOBJMETHOD(scc_enabled,	quicc_bfe_enabled),
	KOBJMETHOD(scc_iclear,	quicc_bfe_iclear),
	KOBJMETHOD(scc_ipend,	quicc_bfe_ipend),
	KOBJMETHOD(scc_probe,	quicc_bfe_probe),
	KOBJMETHOD_END
};

struct scc_class scc_quicc_class = {
	"QUICC class",
	quicc_methods,
	sizeof(struct scc_softc),
	.cl_channels = 4,
	.cl_class = SCC_CLASS_QUICC,
	.cl_modes = SCC_MODE_ASYNC | SCC_MODE_BISYNC | SCC_MODE_HDLC,
	.cl_range = 0,
};

static int
quicc_bfe_attach(struct scc_softc *sc __unused, int reset __unused)
{

	return (0);
}

static int
quicc_bfe_enabled(struct scc_softc *sc, struct scc_chan *ch)
{
	struct scc_bas *bas;
	int unit;
	uint16_t val0, val1;

	bas = &sc->sc_bas;
	unit = ch->ch_nr - 1;
	val0 = quicc_read2(bas, QUICC_REG_SCC_TODR(unit));
	quicc_write2(bas, QUICC_REG_SCC_TODR(unit), ~val0);
	val1 = quicc_read2(bas, QUICC_REG_SCC_TODR(unit));
	quicc_write2(bas, QUICC_REG_SCC_TODR(unit), val0);
	return (((val0 | val1) == 0x8000) ? 1 : 0);
}

static int
quicc_bfe_iclear(struct scc_softc *sc, struct scc_chan *ch)
{
	struct scc_bas *bas;
	uint16_t rb, st;

	bas = &sc->sc_bas;
	mtx_lock_spin(&sc->sc_hwmtx);
	if (ch->ch_ipend & SER_INT_RXREADY) {
		rb = quicc_read2(bas, QUICC_PRAM_SCC_RBASE(ch->ch_nr - 1));
		st = quicc_read2(bas, rb);
		(void)quicc_read4(bas, rb + 4);
		quicc_write2(bas, rb, st | 0x9000);
	}
	mtx_unlock_spin(&sc->sc_hwmtx);
	return (0);
}

static int
quicc_bfe_ipend(struct scc_softc *sc)
{
	struct scc_bas *bas;
	struct scc_chan *ch;
	int c, ipend;
	uint16_t scce;

	bas = &sc->sc_bas;
	ipend = 0;
	for (c = 0; c < 4; c++) {
		ch = &sc->sc_chan[c];
		if (!ch->ch_enabled)
			continue;
		ch->ch_ipend = 0;
		mtx_lock_spin(&sc->sc_hwmtx);
		scce = quicc_read2(bas, QUICC_REG_SCC_SCCE(c));
		quicc_write2(bas, QUICC_REG_SCC_SCCE(c), ~0);
		mtx_unlock_spin(&sc->sc_hwmtx);
		if (scce & 0x0001)
			ch->ch_ipend |= SER_INT_RXREADY;
		if (scce & 0x0002)
			ch->ch_ipend |= SER_INT_TXIDLE;
		if (scce & 0x0004)
			ch->ch_ipend |= SER_INT_OVERRUN;
		if (scce & 0x0020)
			ch->ch_ipend |= SER_INT_BREAK;
		/* XXX SIGNALS */
		ipend |= ch->ch_ipend;
	}
	return (ipend);
}

static int
quicc_bfe_probe(struct scc_softc *sc __unused)
{

	return (0);
}
