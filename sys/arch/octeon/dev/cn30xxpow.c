/*	$OpenBSD: cn30xxpow.c,v 1.17 2022/12/28 01:39:21 yasuoka Exp $	*/

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

#include <machine/bus.h>
#include <machine/octeonvar.h>

#include <octeon/dev/iobusvar.h>
#include <octeon/dev/cn30xxpowreg.h>
#include <octeon/dev/cn30xxpowvar.h>

void	cn30xxpow_bootstrap(struct octeon_config *);

void	cn30xxpow_init(struct cn30xxpow_softc *);
void	cn30xxpow_init_regs(struct cn30xxpow_softc *);
void	cn30xxpow_config_int(struct cn30xxpow_softc *, int,
	    uint64_t, uint64_t, uint64_t);

struct cn30xxpow_softc	cn30xxpow_softc;

/* -------------------------------------------------------------------------- */

/* ---- operation primitive functions */

/* 5.11.1 Load Operations */

/* 5.11.2 IOBDMA Operations */

/* 5.11.3 Store Operations */

/* -------------------------------------------------------------------------- */

/* ---- utility functions */

void
cn30xxpow_work_request_async(uint64_t scraddr, uint64_t wait)
{
        cn30xxpow_ops_get_work_iobdma(scraddr, wait);
}

uint64_t *
cn30xxpow_work_response_async(uint64_t scraddr)
{
	uint64_t result;

	octeon_synciobdma();
	result = octeon_cvmseg_read_8(scraddr);

	return (result & POW_IOBDMA_GET_WORK_RESULT_NO_WORK) ?
	    NULL :
	    (uint64_t *)PHYS_TO_XKPHYS(
		result & POW_IOBDMA_GET_WORK_RESULT_ADDR, CCA_CACHED);
}

/* -------------------------------------------------------------------------- */

/* ---- initialization and configuration */

void
cn30xxpow_bootstrap(struct octeon_config *mcp)
{
	struct cn30xxpow_softc *sc = &cn30xxpow_softc;

	sc->sc_regt = mcp->mc_iobus_bust;
	/* XXX */

	cn30xxpow_init(sc);
}

void
cn30xxpow_config_int(struct cn30xxpow_softc *sc, int group,
   uint64_t tc_thr, uint64_t ds_thr, uint64_t iq_thr)
{
	uint64_t wq_int_thr;

	wq_int_thr =
	    POW_WQ_INT_THRX_TC_EN |
	    (tc_thr << POW_WQ_INT_THRX_TC_THR_SHIFT) |
	    (ds_thr << POW_WQ_INT_THRX_DS_THR_SHIFT) |
	    (iq_thr << POW_WQ_INT_THRX_IQ_THR_SHIFT);
	_POW_WR8(sc, POW_WQ_INT_THR0_OFFSET + (group * 8), wq_int_thr);
}

/*
 * interrupt threshold configuration
 *
 * => DS / IQ
 *    => ...
 * => time counter threshold
 *    => unit is 1msec
 *    => each group can set timeout
 * => temporary disable bit
 *    => use CIU generic timer
 */

void
cn30xxpow_config(struct cn30xxpow_softc *sc, int group)
{

	cn30xxpow_config_int(sc, group,
	    0x0f,		/* TC */
	    0x00,		/* DS */
	    0x00);		/* IQ */
}

void
cn30xxpow_init(struct cn30xxpow_softc *sc)
{
	cn30xxpow_init_regs(sc);

	sc->sc_int_pc_base = 10000;
	cn30xxpow_config_int_pc(sc, sc->sc_int_pc_base);
}

void
cn30xxpow_init_regs(struct cn30xxpow_softc *sc)
{
	int status;

	status = bus_space_map(sc->sc_regt, POW_BASE, POW_SIZE, 0,
	    &sc->sc_regh);
	if (status != 0)
		panic("can't map %s space", "pow register");
}
