/*	$OpenBSD: isa.c,v 1.52 2025/09/16 12:18:10 hshoexer Exp $	*/
/*	$NetBSD: isa.c,v 1.85 1996/05/14 00:31:04 thorpej Exp $	*/

/*
 * Copyright (c) 1997, Jason Downs.  All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR(S) ``AS IS'' AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR(S) BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*-
 * Copyright (c) 1993, 1994 Charles Hannum.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by Charles Hannum.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
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
#include <sys/conf.h>
#include <sys/malloc.h>
#include <sys/device.h>
#include <sys/extent.h>
#include <sys/reboot.h>

#include <dev/isa/isareg.h>
#include <dev/isa/isavar.h>
#include <dev/isa/isadmareg.h>

#ifdef __HAVE_ACPI
#include "acpi.h"
#include <dev/acpi/acpivar.h>
#else
#define NACPI 0
#endif

int isamatch(struct device *, void *, void *);
void isaattach(struct device *, struct device *, void *);

extern int autoconf_verbose;

const struct cfattach isa_ca = {
	sizeof(struct isa_softc), isamatch, isaattach
};

struct cfdriver isa_cd = {
	NULL, "isa", DV_DULL, CD_INDIRECT | CD_COCOVM
};

int
isamatch(struct device *parent, void *match, void *aux)
{
	struct cfdata *cf = match;
	struct isabus_attach_args *iba = aux;

	if (strcmp(iba->iba_busname, cf->cf_driver->cd_name))
		return (0);

#if NACPI > 0
	if (acpi_legacy_free)
		return (0);
#endif
	return (1);
}

void
isaattach(struct device *parent, struct device *self, void *aux)
{
	struct isa_softc *sc = (struct isa_softc *)self;
	struct isabus_attach_args *iba = aux;

	isa_attach_hook(parent, self, iba);
	printf("\n");

	sc->sc_iot = iba->iba_iot;
	sc->sc_memt = iba->iba_memt;
#if NISADMA > 0
	sc->sc_dmat = iba->iba_dmat;
#endif /* NISADMA > 0 */
	sc->sc_ic = iba->iba_ic;

#if NISAPNP > 0
	isapnp_isa_attach_hook(sc);
#endif

#if NISADMA > 0
	/*
	 * Map the registers used by the ISA DMA controller.
	 * XXX Should be done in the isadmaattach routine.. but the delay
	 * XXX port makes it troublesome.  Note that these aren't really
	 * XXX valid on ISA busses without DMA.
	 */
	if (bus_space_map(sc->sc_iot, IO_DMA1, DMA1_IOSIZE, 0, &sc->sc_dma1h))
		panic("isaattach: can't map DMA controller #1");
	if (bus_space_map(sc->sc_iot, IO_DMA2, DMA2_IOSIZE, 0, &sc->sc_dma2h))
		panic("isaattach: can't map DMA controller #2");
	if (bus_space_map(sc->sc_iot, IO_DMAPG, 0xf, 0, &sc->sc_dmapgh))
		panic("isaattach: can't map DMA page registers");

	/*
  	 * Map port 0x84, which causes a 1.25us delay when read.
  	 * We do this now, since several drivers need it.
	 * XXX this port doesn't exist on all ISA busses...
	 */
	if (bus_space_subregion(sc->sc_iot, sc->sc_dmapgh, 0x04, 1,
	    &sc->sc_delaybah))
#else /* NISADMA > 0 */
	if (bus_space_map(sc->sc_iot, IO_DMAPG + 0x4, 0x1, 0,
	    &sc->sc_delaybah))
#endif /* NISADMA > 0 */
		panic("isaattach: can't map `delay port'");	/* XXX */

	TAILQ_INIT(&sc->sc_subdevs);
	config_scan(isascan, self);
}

int
isaprint(void *aux, const char *isa)
{
	struct isa_attach_args *ia = aux;
	int irq, nirq;
	int dma, ndma;

	if (ia->ia_iosize)
		printf(" port 0x%x", ia->ia_iobase);
	if (ia->ia_iosize > 1)
		printf("/%d", ia->ia_iosize);

	if (ia->ia_msize)
		printf(" iomem 0x%x", ia->ia_maddr);
	if (ia->ia_msize > 1)
		printf("/%d", ia->ia_msize);

	nirq = ia->ipa_nirq;
	if (nirq < 0 || nirq > nitems(ia->ipa_irq))
		nirq = 1;
	for (irq = 0; irq < nirq; irq++)
		if (ia->ipa_irq[irq].num != IRQUNK)
			printf(" irq %d", ia->ipa_irq[irq].num);

	ndma = ia->ipa_ndrq;
	if (ndma < 0 || ndma > nitems(ia->ipa_drq))
		ndma = 2;
	for (dma = 0; dma < ndma; dma++)
		if (ia->ipa_drq[dma].num != DRQUNK) {
			if (dma == 0)
				printf(" drq");
			else
				printf(" drq%d", dma + 1);
			printf(" %d", ia->ipa_drq[dma].num);
		}

	return (UNCONF);
}

void
isascan(struct device *parent, void *match)
{
	struct isa_softc *sc = (struct isa_softc *)parent;
	struct device *dev = match;
	struct cfdata *cf = dev->dv_cfdata;
	struct isa_attach_args ia;

	ia.ia_iot = sc->sc_iot;
	ia.ia_memt = sc->sc_memt;
#if NISADMA > 0
	ia.ia_dmat = sc->sc_dmat;
#endif /* NISADMA > 0 */
	ia.ia_ic = sc->sc_ic;
	ia.ia_iobase = cf->cf_iobase;
	ia.ia_iosize = 0x666;
	ia.ia_maddr = cf->cf_maddr;
	ia.ia_msize = cf->cf_msize;
	ia.ia_irq = cf->cf_irq == 2 ? 9 : cf->cf_irq;
	ia.ipa_nirq = ia.ia_irq == IRQUNK ? 0 : 1;
	ia.ia_drq = cf->cf_drq;
	ia.ia_drq2 = cf->cf_drq2;
	ia.ipa_ndrq = 2;
	ia.ia_delaybah = sc->sc_delaybah;

	if (ISSET(boothowto, RB_COCOVM) &&
	    !ISSET(cf->cf_driver->cd_mode, CD_COCOVM))
		return;

	if (cf->cf_fstate == FSTATE_STAR) {
		struct isa_attach_args ia2 = ia;

		if (autoconf_verbose)
			printf(">>> probing for %s*\n",
			    cf->cf_driver->cd_name);
		while ((*cf->cf_attach->ca_match)(parent, dev, &ia2) > 0) {
#if !defined(__NO_ISA_INTR_CHECK)
			if ((ia2.ia_irq != IRQUNK) &&
			    !isa_intr_check(sc->sc_ic, ia2.ia_irq, IST_EDGE)) {
				printf("%s%d: irq %d already in use\n",
				    cf->cf_driver->cd_name, cf->cf_unit,
				    ia2.ia_irq);
				ia2 = ia;
				break;
			}
#endif

			if (autoconf_verbose)
				printf(">>> probe for %s* clone into %s%d\n",
				    cf->cf_driver->cd_name,
				    cf->cf_driver->cd_name, cf->cf_unit);
			if (ia2.ia_iosize == 0x666) {
				printf("%s: iosize not repaired by driver\n",
				    sc->sc_dev.dv_xname);
				ia2.ia_iosize = 0;
			}
			config_attach(parent, dev, &ia2, isaprint);
			dev = config_make_softc(parent, cf);
#if NISADMA > 0
			if (ia2.ia_drq != DRQUNK)
				ISA_DRQ_ALLOC((struct device *)sc, ia2.ia_drq);
			if (ia2.ia_drq2 != DRQUNK)
				ISA_DRQ_ALLOC((struct device *)sc, ia2.ia_drq2);
#endif /* NISAMDA > 0 */
			ia2 = ia;
		}
		if (autoconf_verbose)
			printf(">>> probing for %s* finished\n",
			    cf->cf_driver->cd_name);
		free(dev, M_DEVBUF, cf->cf_attach->ca_devsize);
		return;
	}

	if (autoconf_verbose)
		printf(">>> probing for %s%d\n", cf->cf_driver->cd_name,
		    cf->cf_unit);
	if ((*cf->cf_attach->ca_match)(parent, dev, &ia) > 0) {
#if !defined(__NO_ISA_INTR_CHECK)
		if ((ia.ia_irq != IRQUNK) &&
		    !isa_intr_check(sc->sc_ic, ia.ia_irq, IST_EDGE)) {
			printf("%s%d: irq %d already in use\n",
			    cf->cf_driver->cd_name, cf->cf_unit, ia.ia_irq);
			free(dev, M_DEVBUF, cf->cf_attach->ca_devsize);
		} else {
#endif
			if (autoconf_verbose)
				printf(">>> probing for %s%d succeeded\n",
				    cf->cf_driver->cd_name, cf->cf_unit);
			config_attach(parent, dev, &ia, isaprint);

#if NISADMA > 0
			if (ia.ia_drq != DRQUNK)
				ISA_DRQ_ALLOC((struct device *)sc, ia.ia_drq);
			if (ia.ia_drq2 != DRQUNK)
				ISA_DRQ_ALLOC((struct device *)sc, ia.ia_drq2);
#endif /* NISAMDA > 0 */
#if !defined(__NO_ISA_INTR_CHECK)
		}
#endif
	} else {
		if (autoconf_verbose)
			printf(">>> probing for %s%d failed\n",
			    cf->cf_driver->cd_name, cf->cf_unit);
		free(dev, M_DEVBUF, cf->cf_attach->ca_devsize);
	}
}

char *
isa_intr_typename(int type)
{

	switch (type) {
        case IST_NONE:
		return ("none");
        case IST_PULSE:
		return ("pulsed");
        case IST_EDGE:
		return ("edge-triggered");
        case IST_LEVEL:
		return ("level-triggered");
	default:
		panic("isa_intr_typename: invalid type %d", type);
	}
}
