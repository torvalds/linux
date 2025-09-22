/*	$OpenBSD: cn30xxasx.c,v 1.8 2022/12/28 01:39:21 yasuoka Exp $	*/

/*
 * Copyright (c) 2007 Internet Initiative Japan, Inc.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>

#include <machine/octeonvar.h>

#include <octeon/dev/cn30xxasxreg.h>
#include <octeon/dev/cn30xxasxvar.h>

void
cn30xxasx_init(struct cn30xxasx_attach_args *aa,
    struct cn30xxasx_softc **rsc)
{
	struct cn30xxasx_softc *sc;
	int status;

	sc = malloc(sizeof(*sc), M_DEVBUF, M_WAITOK | M_ZERO);
	if (sc == NULL)
		panic("can't allocate memory: %s", __func__);

	sc->sc_port = aa->aa_port;
	sc->sc_regt = aa->aa_regt;

	status = bus_space_map(sc->sc_regt, ASX0_BASE, ASX0_SIZE, 0,
	    &sc->sc_regh);
	if (status != 0)
		panic("can't map %s space", "asx register");

	*rsc = sc;
}

#define	_ASX_RD8(sc, off) \
	bus_space_read_8((sc)->sc_regt, (sc)->sc_regh, (off))
#define	_ASX_WR8(sc, off, v) \
	bus_space_write_8((sc)->sc_regt, (sc)->sc_regh, (off), (v))

int	cn30xxasx_enable_tx(struct cn30xxasx_softc *, int);
int	cn30xxasx_enable_rx(struct cn30xxasx_softc *, int);

int
cn30xxasx_enable(struct cn30xxasx_softc *sc, int enable)
{
	cn30xxasx_enable_tx(sc, enable);
	cn30xxasx_enable_rx(sc, enable);
	return 0;
}

int
cn30xxasx_enable_tx(struct cn30xxasx_softc *sc, int enable)
{
	uint64_t asx_tx_port;

	asx_tx_port = _ASX_RD8(sc, ASX0_TX_PRT_EN_OFFSET);
	if (enable)
		SET(asx_tx_port, 1 << sc->sc_port);
	else
		CLR(asx_tx_port, 1 << sc->sc_port);
	_ASX_WR8(sc, ASX0_TX_PRT_EN_OFFSET, asx_tx_port);
	return 0;
}

int
cn30xxasx_enable_rx(struct cn30xxasx_softc *sc, int enable)
{
	uint64_t asx_rx_port;

	asx_rx_port = _ASX_RD8(sc, ASX0_RX_PRT_EN_OFFSET);
	if (enable)
		SET(asx_rx_port, 1 << sc->sc_port);
	else
		CLR(asx_rx_port, 1 << sc->sc_port);
	_ASX_WR8(sc, ASX0_RX_PRT_EN_OFFSET, asx_rx_port);
	return 0;
}

int
cn30xxasx_clk_set(struct cn30xxasx_softc *sc, int tx_setting, int rx_setting)
{
	_ASX_WR8(sc, ASX0_TX_CLK_SET0_OFFSET + 8 * sc->sc_port, tx_setting);
	_ASX_WR8(sc, ASX0_RX_CLK_SET0_OFFSET + 8 * sc->sc_port, rx_setting);
	return 0;
}
