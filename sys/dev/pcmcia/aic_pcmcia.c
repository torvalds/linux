/*	$OpenBSD: aic_pcmcia.c,v 1.21 2024/05/26 08:46:28 jsg Exp $	*/
/*	$NetBSD: aic_pcmcia.c,v 1.6 1998/07/19 17:28:15 christos Exp $	*/

/*
 * Copyright (c) 1997 Marc Horowitz.  All rights reserved.
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
 *	This product includes software developed by Marc Horowitz.
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
#include <sys/device.h>

#include <machine/bus.h>
#include <machine/intr.h>

#include <scsi/scsi_all.h>
#include <scsi/scsiconf.h>

#include <dev/ic/aic6360var.h>

#include <dev/pcmcia/pcmciavar.h>
#include <dev/pcmcia/pcmciadevs.h>

int	aic_pcmcia_match(struct device *, void *, void *);
void	aic_pcmcia_attach(struct device *, struct device *, void *);
int	aic_pcmcia_detach(struct device *, int);

struct aic_pcmcia_softc {
	struct aic_softc sc_aic;		/* real "aic" softc */

	/* PCMCIA-specific goo. */
	struct pcmcia_io_handle sc_pcioh;	/* PCMCIA i/o space info */
	int sc_io_window;			/* our i/o window */
	struct pcmcia_function *sc_pf;		/* our PCMCIA function */
	void *sc_ih;				/* interrupt handler */
};

const struct cfattach aic_pcmcia_ca = {
	sizeof(struct aic_pcmcia_softc), aic_pcmcia_match, aic_pcmcia_attach,
	aic_pcmcia_detach
};

struct aic_pcmcia_product {
	u_int16_t	app_vendor;		/* PCMCIA vendor ID */
	u_int16_t	app_product;		/* PCMCIA product ID */
	int		app_expfunc;		/* expected function number */
} aic_pcmcia_prod[] = {
	{ PCMCIA_VENDOR_ADAPTEC,	PCMCIA_PRODUCT_ADAPTEC_APA1460_1,
	  0 },

	{ PCMCIA_VENDOR_ADAPTEC,	PCMCIA_PRODUCT_ADAPTEC_APA1460_2,
	  0 },

	{ PCMCIA_VENDOR_NEWMEDIA,	PCMCIA_PRODUCT_NEWMEDIA_BUSTOASTER,
	  0 }
};

int
aic_pcmcia_match(struct device *parent, void *match, void *aux)
{
	struct pcmcia_attach_args *pa = aux;
	int i;

	for (i = 0; i < nitems(aic_pcmcia_prod); i++)
		if (pa->manufacturer == aic_pcmcia_prod[i].app_vendor &&
		    pa->product == aic_pcmcia_prod[i].app_product &&
		    pa->pf->number == aic_pcmcia_prod[i].app_expfunc)
			return (1);
	return (0);
}

void
aic_pcmcia_attach(struct device *parent, struct device *self, void *aux)
{
	struct aic_pcmcia_softc *psc = (void *)self;
	struct aic_softc *sc = &psc->sc_aic;
	struct pcmcia_attach_args *pa = aux;
	struct pcmcia_config_entry *cfe;
	struct pcmcia_function *pf = pa->pf;
	const char *intrstr;

	psc->sc_pf = pf;

	for (cfe = SIMPLEQ_FIRST(&pf->cfe_head); cfe != NULL;
	    cfe = SIMPLEQ_NEXT(cfe, cfe_list)) {
		if (cfe->num_memspace != 0 ||
		    cfe->num_iospace != 1)
			continue;

		/* The bustoaster has a default config as first
		 * entry, we don't want to use that. */

		if (pa->manufacturer == PCMCIA_VENDOR_NEWMEDIA &&
		    pa->product == PCMCIA_PRODUCT_NEWMEDIA_BUSTOASTER &&
		    cfe->iospace[0].start == 0)
			continue;

		if (pcmcia_io_alloc(pa->pf, cfe->iospace[0].start,
		    cfe->iospace[0].length, AIC_NPORTS, &psc->sc_pcioh) == 0)
			break;
	}

	if (cfe == 0) {
		printf(": can't alloc i/o space\n");
		return;
	}

	sc->sc_iot = psc->sc_pcioh.iot;
	sc->sc_ioh = psc->sc_pcioh.ioh;

	/* Enable the card. */
	pcmcia_function_init(pf, cfe);
	if (pcmcia_function_enable(pf)) {
		printf(": function enable failed\n");
		return;
	}

	/* Map in the io space */
	if (pcmcia_io_map(pa->pf, PCMCIA_WIDTH_AUTO, 0, psc->sc_pcioh.size,
	    &psc->sc_pcioh, &psc->sc_io_window)) {
		printf(": can't map i/o space\n");
		return;
	}

	printf(" port 0x%lx/%lu", psc->sc_pcioh.addr,
	    (u_long)psc->sc_pcioh.size);

	if (!aic_find(sc->sc_iot, sc->sc_ioh)) {
		printf(": unable to detect chip!\n");
		return;
	}

	/* Establish the interrupt handler. */
	psc->sc_ih = pcmcia_intr_establish(pa->pf, IPL_BIO,
	    aicintr, sc, sc->sc_dev.dv_xname);
	intrstr = pcmcia_intr_string(psc->sc_pf, psc->sc_ih);
	printf("%s%s\n", *intrstr ? ", " : "", intrstr);
	if (psc->sc_ih == NULL)
		return;

	aicattach(sc);

}

int
aic_pcmcia_detach(struct device *self, int flags)
{
	struct aic_pcmcia_softc *sc= (void *)self;
	int error;

	error = aic_detach(self, flags);
	if (error)
		return (error);

	/* Unmap our i/o window and i/o space. */
	pcmcia_io_unmap(sc->sc_pf, sc->sc_io_window);
	pcmcia_io_free(sc->sc_pf, &sc->sc_pcioh);

	return (0);
}
