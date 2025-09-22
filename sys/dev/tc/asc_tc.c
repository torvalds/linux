/* $OpenBSD: asc_tc.c,v 1.15 2025/06/29 15:55:22 miod Exp $ */
/* $NetBSD: asc_tc.c,v 1.19 2001/11/15 09:48:19 lukem Exp $ */

/*-
 * Copyright (c) 2000 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Tohru Nishimura.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/buf.h>

#include <scsi/scsi_all.h>
#include <scsi/scsiconf.h>
#include <scsi/scsi_message.h>

#include <machine/bus.h>

#include <dev/ic/ncr53c9xreg.h>
#include <dev/ic/ncr53c9xvar.h>
#include <dev/tc/ascvar.h>

#include <dev/tc/tcvar.h>

struct asc_tc_softc {
	struct asc_softc asc;

	/* XXX XXX XXX */
	caddr_t sc_base, sc_bounce, sc_target;
};

int  asc_tc_match(struct device *, void *, void *);
void asc_tc_attach(struct device *, struct device *, void *);

const struct cfattach asc_tc_ca = {
	sizeof(struct asc_tc_softc), asc_tc_match, asc_tc_attach
};

int	asc_dma_isintr(struct ncr53c9x_softc *);
void	asc_tc_reset(struct ncr53c9x_softc *);
int	asc_tc_intr(struct ncr53c9x_softc *);
int	asc_tc_setup(struct ncr53c9x_softc *, caddr_t *,
						size_t *, int, size_t *);
void	asc_tc_go(struct ncr53c9x_softc *);
void	asc_tc_stop(struct ncr53c9x_softc *);
int	asc_dma_isactive(struct ncr53c9x_softc *);
void	asc_clear_latched_intr(struct ncr53c9x_softc *);

struct ncr53c9x_glue asc_tc_glue = {
	asc_read_reg,
	asc_write_reg,
	asc_dma_isintr,
	asc_tc_reset,
	asc_tc_intr,
	asc_tc_setup,
	asc_tc_go,
	asc_tc_stop,
	asc_dma_isactive,
	asc_clear_latched_intr,
};

/*
 * Parameters specific to PMAZ-A TC option card.
 */
#define PMAZ_OFFSET_53C94	0x0		/* from module base */
#define PMAZ_OFFSET_DMAR	0x40000		/* DMA Address Register */
#define PMAZ_OFFSET_RAM		0x80000		/* 128KB SRAM buffer */
#define PMAZ_OFFSET_ROM		0xc0000		/* diagnostic ROM */

#define PMAZ_RAM_SIZE		0x20000		/* 128k (32k*32) */
#define PER_TGT_DMA_SIZE	((PMAZ_RAM_SIZE/7) & ~(sizeof(int)-1))

#define PMAZ_DMAR_WRITE		0x80000000	/* DMA direction bit */
#define PMAZ_DMAR_MASK		0x1ffff		/* 17 bits, 128k */
#define PMAZ_DMA_ADDR(x)	((unsigned long)(x) & PMAZ_DMAR_MASK)

int
asc_tc_match(struct device *parent, void *cfdata, void *aux)
{
	struct tc_attach_args *d = aux;
	
	if (strncmp("PMAZ-AA ", d->ta_modname, TC_ROM_LLEN))
		return (0);

	return (1);
}

void
asc_tc_attach(struct device *parent, struct device *self, void *aux)
{
	struct tc_attach_args *ta = aux;
	struct asc_tc_softc *asc = (struct asc_tc_softc *)self;	
	struct ncr53c9x_softc *sc = &asc->asc.sc_ncr53c9x;

	/*
	 * Set up glue for MI code early; we use some of it here.
	 */
	sc->sc_glue = &asc_tc_glue;
	asc->asc.sc_bst = ta->ta_memt;
	asc->asc.sc_dmat = ta->ta_dmat;
	if (bus_space_map(asc->asc.sc_bst, ta->ta_addr,
		PMAZ_OFFSET_RAM + PMAZ_RAM_SIZE, 0, &asc->asc.sc_bsh)) {
		printf("%s: unable to map device\n", sc->sc_dev.dv_xname);
		return;
	}
	asc->sc_base = (caddr_t)ta->ta_addr;	/* XXX XXX XXX */

	tc_intr_establish(parent, ta->ta_cookie, IPL_BIO, ncr53c9x_intr, sc,
	    self->dv_xname);
	
	sc->sc_id = 7;
	sc->sc_freq = TC_SPEED_TO_KHZ(ta->ta_busspeed);	/* in kHz so far */

	/*
	 * XXX More of this should be in ncr53c9x_attach(), but
	 * XXX should we really poke around the chip that much in
	 * XXX the MI code?  Think about this more...
	 */

	/*
	 * Set up static configuration info.
	 */
	sc->sc_cfg1 = sc->sc_id | NCRCFG1_PARENB;
	sc->sc_cfg2 = NCRCFG2_SCSI2;
	sc->sc_cfg3 = 0;
	sc->sc_rev = NCR_VARIANT_NCR53C94;

	/*
	 * XXX minsync and maxxfer _should_ be set up in MI code,
	 * XXX but it appears to have some dependency on what sort
	 * XXX of DMA we're hooked up to, etc.
	 */

	/*
	 * This is the value used to start sync negotiations
	 * Note that the NCR register "SYNCTP" is programmed
	 * in "clocks per byte", and has a minimum value of 4.
	 * The SCSI period used in negotiation is one-fourth
	 * of the time (in nanoseconds) needed to transfer one byte.
	 * Since the chip's clock is given in kHz, we have the following
	 * formula: 4 * period = (1000000 / freq) * 4
	 */
	sc->sc_minsync = (1000000 / sc->sc_freq) * 5 / 4;

	sc->sc_maxxfer = 64 * 1024;

	/* convert sc_freq to MHz */
	sc->sc_freq /= 1000;

	/* Do the common parts of attachment. */
	ncr53c9x_attach(sc);
}

void
asc_tc_reset(struct ncr53c9x_softc *sc)
{
	struct asc_tc_softc *asc = (struct asc_tc_softc *)sc;

	asc->asc.sc_flags &= ~(ASC_DMAACTIVE|ASC_MAPLOADED);
}

int
asc_tc_intr(struct ncr53c9x_softc *sc)
{
	struct asc_tc_softc *asc = (struct asc_tc_softc *)sc;
	int trans, resid;

	resid = 0;
	if ((asc->asc.sc_flags & ASC_ISPULLUP) == 0 &&
	    (resid = (NCR_READ_REG(sc, NCR_FFLAG) & NCRFIFO_FF)) != 0) {
		NCR_DMA(("asc_tc_intr: empty FIFO of %d ", resid));
		DELAY(1);
	}

	resid += NCR_READ_REG(sc, NCR_TCL);
	resid += NCR_READ_REG(sc, NCR_TCM) << 8;

	trans = asc->asc.sc_dmasize - resid;

	if (asc->asc.sc_flags & ASC_ISPULLUP)
		memcpy(asc->sc_target, asc->sc_bounce, trans);
	*asc->asc.sc_dmalen -= trans;
	*asc->asc.sc_dmaaddr += trans;
	asc->asc.sc_flags &= ~(ASC_DMAACTIVE|ASC_MAPLOADED);

	return (0);
}

int
asc_tc_setup(struct ncr53c9x_softc *sc, caddr_t *addr, size_t *len, int datain,
    size_t *dmasize)
{
	struct asc_tc_softc *asc = (struct asc_tc_softc *)sc;
	u_int32_t tc_dmar;
	size_t size;

	asc->asc.sc_dmaaddr = addr;
	asc->asc.sc_dmalen = len;
	asc->asc.sc_flags = (datain) ? ASC_ISPULLUP : 0;

	NCR_DMA(("asc_tc_setup: start %ld@%p, %s\n", (long)*asc->asc.sc_dmalen,
		*asc->asc.sc_dmaaddr, datain ? "IN" : "OUT"));

	size = *dmasize;
	if (size > PER_TGT_DMA_SIZE)
		size = PER_TGT_DMA_SIZE;
	*dmasize = asc->asc.sc_dmasize = size;

	NCR_DMA(("asc_tc_setup: dmasize = %ld\n", (long)asc->asc.sc_dmasize));

	asc->sc_bounce = asc->sc_base + PMAZ_OFFSET_RAM;
	asc->sc_bounce += PER_TGT_DMA_SIZE *
	    sc->sc_nexus->xs->sc_link->target;
	asc->sc_target = *addr;

	if ((asc->asc.sc_flags & ASC_ISPULLUP) == 0)
		memcpy(asc->sc_bounce, asc->sc_target, size);

#if 1
	if (asc->asc.sc_flags & ASC_ISPULLUP)
		tc_dmar = PMAZ_DMA_ADDR(asc->sc_bounce);
	else
		tc_dmar = PMAZ_DMAR_WRITE | PMAZ_DMA_ADDR(asc->sc_bounce);
	bus_space_write_4(asc->asc.sc_bst, asc->asc.sc_bsh, PMAZ_OFFSET_DMAR,
	    tc_dmar);
	asc->asc.sc_flags |= ASC_MAPLOADED|ASC_DMAACTIVE;
#endif
	return (0);
}

void
asc_tc_go(struct ncr53c9x_softc *sc)
{
#if 0
	struct asc_tc_softc *asc = (struct asc_tc_softc *)sc;
	u_int32_t tc_dmar;

	if (asc->asc.sc_flags & ASC_ISPULLUP)
		tc_dmar = PMAZ_DMA_ADDR(asc->sc_bounce);
	else
		tc_dmar = PMAZ_DMAR_WRITE | PMAZ_DMA_ADDR(asc->sc_bounce);
	bus_space_write_4(asc->asc.sc_bst, asc->asc.sc_bsh, PMAZ_OFFSET_DMAR,
	    tc_dmar);
	asc->asc.sc_flags |= ASC_DMAACTIVE;
#endif
}

/* NEVER CALLED BY MI 53C9x ENGINE INDEED */
void
asc_tc_stop(struct ncr53c9x_softc *sc)
{
#if 0
	struct asc_tc_softc *asc = (struct asc_tc_softc *)sc;

	if (asc->asc.sc_flags & ASC_ISPULLUP)
		memcpy(asc->sc_target, asc->sc_bounce, asc->sc_dmasize);
	asc->asc.sc_flags &= ~ASC_DMAACTIVE;
#endif
}

/*
 * Glue functions.
 */
int
asc_dma_isintr(struct ncr53c9x_softc *sc)
{
	return !!(NCR_READ_REG(sc, NCR_STAT) & NCRSTAT_INT);
}

int
asc_dma_isactive(struct ncr53c9x_softc *sc)
{
	struct asc_tc_softc *asc = (struct asc_tc_softc *)sc;

	return !!(asc->asc.sc_flags & ASC_DMAACTIVE);
}

void
asc_clear_latched_intr(struct ncr53c9x_softc *sc)
{
}
