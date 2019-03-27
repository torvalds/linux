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

#include <dev/ic/sab82532.h>

#include "scc_if.h"

static int sab82532_bfe_attach(struct scc_softc *, int);
static int sab82532_bfe_iclear(struct scc_softc *, struct scc_chan *);
static int sab82532_bfe_ipend(struct scc_softc *);
static int sab82532_bfe_probe(struct scc_softc *);

static kobj_method_t sab82532_methods[] = {
	KOBJMETHOD(scc_attach,	sab82532_bfe_attach),
	KOBJMETHOD(scc_iclear,	sab82532_bfe_iclear),
	KOBJMETHOD(scc_ipend,	sab82532_bfe_ipend),
	KOBJMETHOD(scc_probe,	sab82532_bfe_probe),
	KOBJMETHOD_END
};

struct scc_class scc_sab82532_class = {
	"sab82532 class",
	sab82532_methods,
	sizeof(struct scc_softc),
	.cl_channels = SAB_NCHAN,
	.cl_class = SCC_CLASS_SAB82532,
	.cl_modes = SCC_MODE_ASYNC | SCC_MODE_BISYNC | SCC_MODE_HDLC,
	.cl_range = SAB_CHANLEN,
};

static int
sab82532_bfe_attach(struct scc_softc *sc __unused, int reset __unused)
{

	return (0);
}

static int
sab82532_bfe_iclear(struct scc_softc *sc, struct scc_chan *ch)
{
	struct scc_bas *bas;
	int i, ofs, rbcl;

	bas = &sc->sc_bas;
	ofs = (ch->ch_nr - 1) * SAB_CHANLEN;
	mtx_lock_spin(&sc->sc_hwmtx);
	if (ch->ch_ipend & SER_INT_RXREADY) {
		if (scc_getreg(bas, ofs + SAB_STAR) & SAB_STAR_RFNE) {
			rbcl = scc_getreg(bas, ofs + SAB_RBCL) & 31;
			if (rbcl == 0)
				rbcl = 32;
			for (i = 0; i < rbcl; i += 2) {
				(void)scc_getreg(bas, ofs + SAB_RFIFO);
				(void)scc_getreg(bas, ofs + SAB_RFIFO + 1);
			}
		}
		while (scc_getreg(bas, ofs + SAB_STAR) & SAB_STAR_CEC)
			;
		scc_setreg(bas, ofs + SAB_CMDR, SAB_CMDR_RMC);
		scc_barrier(bas);
	}
	mtx_unlock_spin(&sc->sc_hwmtx);
	return (0);
}

static int
sab82532_bfe_ipend(struct scc_softc *sc)
{
	struct scc_bas *bas;
	struct scc_chan *ch;
	int ipend;
	int c, ofs;
	uint8_t isr0, isr1;

	bas = &sc->sc_bas;
	ipend = 0;
	for (c = 0; c < SAB_NCHAN; c++) {
		ch = &sc->sc_chan[c];
		ofs = c * SAB_CHANLEN;
		mtx_lock_spin(&sc->sc_hwmtx);
		isr0 = scc_getreg(bas, ofs + SAB_ISR0);
		isr1 = scc_getreg(bas, ofs + SAB_ISR1);
		scc_barrier(bas);
		if (isr0 & SAB_ISR0_TIME) {
			while (scc_getreg(bas, ofs + SAB_STAR) & SAB_STAR_CEC)
				;
			scc_setreg(bas, ofs + SAB_CMDR, SAB_CMDR_RFRD);
			scc_barrier(bas);
		}
		mtx_unlock_spin(&sc->sc_hwmtx);

		ch->ch_ipend = 0;
		if (isr1 & SAB_ISR1_BRKT)
			ch->ch_ipend |= SER_INT_BREAK;
		if (isr0 & SAB_ISR0_RFO)
			ch->ch_ipend |= SER_INT_OVERRUN;
		if (isr0 & (SAB_ISR0_TCD|SAB_ISR0_RPF))
			ch->ch_ipend |= SER_INT_RXREADY;
		if ((isr0 & SAB_ISR0_CDSC) || (isr1 & SAB_ISR1_CSC))
			ch->ch_ipend |= SER_INT_SIGCHG;
		if (isr1 & SAB_ISR1_ALLS)
			ch->ch_ipend |= SER_INT_TXIDLE;
		ipend |= ch->ch_ipend;
	}
	return (ipend);
}

static int
sab82532_bfe_probe(struct scc_softc *sc __unused)
{

	return (0);
}
