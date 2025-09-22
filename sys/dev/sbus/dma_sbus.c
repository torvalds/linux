/*	$OpenBSD: dma_sbus.c,v 1.19 2022/10/16 01:22:40 jsg Exp $	*/
/*	$NetBSD: dma_sbus.c,v 1.5 2000/07/09 20:57:42 pk Exp $ */

/*-
 * Copyright (c) 1998 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Paul Kranenburg.
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

/*
 * Copyright (c) 1994 Peter Galbavy.  All rights reserved.
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

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/errno.h>
#include <sys/device.h>
#include <sys/malloc.h>

#include <machine/bus.h>
#include <machine/intr.h>
#include <machine/autoconf.h>

#include <dev/sbus/sbusvar.h>

#include <dev/ic/lsi64854reg.h>
#include <dev/ic/lsi64854var.h>

#include <scsi/scsi_all.h>
#include <scsi/scsiconf.h>

#include <dev/ic/ncr53c9xreg.h>
#include <dev/ic/ncr53c9xvar.h>

struct dma_softc {
	struct lsi64854_softc	sc_lsi64854;	/* base device */
};

int	dmamatch_sbus(struct device *, void *, void *);
void	dmaattach_sbus(struct device *, struct device *, void *);

int	dmaprint_sbus(void *, const char *);

void	*dmabus_intr_establish(
		bus_space_tag_t,
		bus_space_tag_t,
		int,			/*bus interrupt priority*/
		int,			/*`device class' level*/
		int,			/*flags*/
		int (*)(void *),	/*handler*/
		void *,			/*handler arg*/
		const char *);		/*what*/

static	bus_space_tag_t dma_alloc_bustag(struct dma_softc *sc);

const struct cfattach dma_sbus_ca = {
	sizeof(struct dma_softc), dmamatch_sbus, dmaattach_sbus
};

const struct cfattach ledma_ca = {
	sizeof(struct dma_softc), dmamatch_sbus, dmaattach_sbus
};

struct cfdriver ledma_cd = {
	NULL, "ledma", DV_DULL
};

struct cfdriver dma_cd = {
	NULL, "dma", DV_DULL
};

int
dmaprint_sbus(void *aux, const char *busname)
{
	struct sbus_attach_args *sa = aux;
	bus_space_tag_t t = sa->sa_bustag;
	struct dma_softc *sc = t->cookie;

	sa->sa_bustag = sc->sc_lsi64854.sc_bustag;	/* XXX */
	sbus_print(aux, busname);	/* XXX */
	sa->sa_bustag = t;		/* XXX */
	return (UNCONF);
}

int
dmamatch_sbus(struct device *parent, void *vcf, void *aux)
{
	struct cfdata *cf = vcf;
	struct sbus_attach_args *sa = aux;

	return (strcmp(cf->cf_driver->cd_name, sa->sa_name) == 0 ||
		strcmp("espdma", sa->sa_name) == 0);
}

void
dmaattach_sbus(struct device *parent, struct device *self, void *aux)
{
	struct sbus_attach_args *sa = aux;
	struct dma_softc *dsc = (void *)self;
	struct lsi64854_softc *sc = &dsc->sc_lsi64854;
	bus_space_handle_t bh;
	bus_space_tag_t sbt;
	int sbusburst, burst;
	int node;

	node = sa->sa_node;

	sc->sc_bustag = sa->sa_bustag;
	sc->sc_dmatag = sa->sa_dmatag;

	/* Map registers */
	if (sa->sa_npromvaddrs != 0) {
		if (sbus_bus_map(sa->sa_bustag, 0, 
		    sa->sa_promvaddrs[0],
		    sa->sa_size,		/* ???? */
		    BUS_SPACE_MAP_PROMADDRESS,
		    0, &bh) != 0) {
			printf("%s: cannot map registers\n", self->dv_xname);
			return;
		}
	} else if (sbus_bus_map(sa->sa_bustag, sa->sa_slot,
	    sa->sa_offset,
	    sa->sa_size,
	    0, 0, &bh) != 0) {
		printf("%s: cannot map registers\n", self->dv_xname);
		return;
	}
	sc->sc_regs = bh;

	/*
	 * Get transfer burst size from PROM and plug it into the
	 * controller registers. This is needed on the Sun4m; do
	 * others need it too?
	 */
	sbusburst = ((struct sbus_softc *)parent)->sc_burst;
	if (sbusburst == 0)
		sbusburst = SBUS_BURST_32 - 1; /* 1->16 */

	burst = getpropint(node,"burst-sizes", -1);
	if (burst == -1)
		/* take SBus burst sizes */
		burst = sbusburst;

	/* Clamp at parent's burst sizes */
	burst &= sbusburst;
	sc->sc_burst = (burst & SBUS_BURST_32) ? 32 :
		       (burst & SBUS_BURST_16) ? 16 : 0;

	if (sc->sc_dev.dv_cfdata->cf_attach == &ledma_ca) {
		char *cabletype;
		u_int32_t csr;
		/*
		 * Check to see which cable type is currently active and
		 * set the appropriate bit in the ledma csr so that it
		 * gets used. If we didn't netboot, the PROM won't have
		 * the "cable-selection" property; default to TP and then
		 * the user can change it via a "media" option to ifconfig.
		 */
		cabletype = getpropstring(node, "cable-selection");
		csr = L64854_GCSR(sc);
		if (strcmp(cabletype, "tpe") == 0) {
			csr |= E_TP_AUI;
		} else if (strcmp(cabletype, "aui") == 0) {
			csr &= ~E_TP_AUI;
		} else {
			/* assume TP if nothing there */
			csr |= E_TP_AUI;
		}
		L64854_SCSR(sc, csr);
		delay(20000);	/* manual says we need a 20ms delay */
		sc->sc_channel = L64854_CHANNEL_ENET;
	} else {
		sc->sc_channel = L64854_CHANNEL_SCSI;
	}

	sbt = dma_alloc_bustag(dsc);
	if (lsi64854_attach(sc) != 0)
		return;

	/* Attach children */
	for (node = firstchild(sa->sa_node); node; node = nextsibling(node)) {
		struct sbus_attach_args sa;
		sbus_setup_attach_args((struct sbus_softc *)parent,
				       sbt, sc->sc_dmatag, node, &sa);
		(void) config_found(&sc->sc_dev, (void *)&sa, dmaprint_sbus);
		sbus_destroy_attach_args(&sa);
	}
}

void *
dmabus_intr_establish(
	bus_space_tag_t t,
	bus_space_tag_t t0,
	int pri,
	int level,
	int flags,
	int (*handler)(void *),
	void *arg,
	const char *what)
{
	struct lsi64854_softc *sc = t->cookie;

	/* XXX - for now only le; do esp later */
	if (sc->sc_channel == L64854_CHANNEL_ENET) {
		sc->sc_intrchain = handler;
		sc->sc_intrchainarg = arg;
		handler = lsi64854_enet_intr;
		arg = sc;
	}

	for (t = t->parent; t; t = t->parent) {
		if (t->sparc_intr_establish != NULL)
			return ((*t->sparc_intr_establish)
				(t, t0, pri, level, flags, handler, arg, what));

	}

	panic("dmabus_intr_establish: no handler found");

	return (NULL);
}

bus_space_tag_t
dma_alloc_bustag(struct dma_softc *sc)
{
	struct sparc_bus_space_tag *sbt;

	sbt = malloc(sizeof(*sbt), M_DEVBUF, M_NOWAIT | M_ZERO);
	if (sbt == NULL)
		return (NULL);

	sbt->cookie = sc;
	sbt->parent = sc->sc_lsi64854.sc_bustag;
	sbt->asi = sbt->parent->asi;
	sbt->sasi = sbt->parent->sasi;
	sbt->sparc_intr_establish = dmabus_intr_establish;
	return (sbt);
}
