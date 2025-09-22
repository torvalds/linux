/*	$OpenBSD: if_an_pcmcia.c,v 1.28 2024/05/26 08:46:28 jsg Exp $	*/

/*
 * Copyright (c) 1999 Michael Shalayeff
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR OR HIS RELATIVES BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF MIND, USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>

#include <net/if.h>
#include <net/if_media.h>

#include <netinet/in.h>
#include <netinet/if_ether.h>

#include <net80211/ieee80211_var.h>
#include <net80211/ieee80211_radiotap.h>

#include <dev/pcmcia/pcmciareg.h>
#include <dev/pcmcia/pcmciavar.h>
#include <dev/pcmcia/pcmciadevs.h>

#include <dev/ic/anreg.h>
#include <dev/ic/anvar.h>

int  an_pcmcia_match(struct device *, void *, void *);
void an_pcmcia_attach(struct device *, struct device *, void *);
int  an_pcmcia_detach(struct device *, int);
int  an_pcmcia_activate(struct device *, int);

struct an_pcmcia_softc {
	struct an_softc sc_an;

	struct pcmcia_io_handle sc_pcioh;
	int sc_io_window;
	struct pcmcia_function *sc_pf;

	int sc_state;
#define	AN_PCMCIA_ATTACHED	3
};

const struct cfattach an_pcmcia_ca = {   
	sizeof(struct an_pcmcia_softc), an_pcmcia_match, an_pcmcia_attach,
	an_pcmcia_detach, an_pcmcia_activate
};

int
an_pcmcia_match(struct device *parent, void *match, void *aux)
{
	struct pcmcia_attach_args *pa = aux;

	if (pa->pf->function != PCMCIA_FUNCTION_NETWORK)
		return 0;

	switch (pa->manufacturer) {
	case PCMCIA_VENDOR_AIRONET:
		switch (pa->product) {
		case PCMCIA_PRODUCT_AIRONET_PC4500:
		case PCMCIA_PRODUCT_AIRONET_PC4800:
		case PCMCIA_PRODUCT_AIRONET_350:
			return 1;
		}
	}

	return 0;
}

void
an_pcmcia_attach(struct device *parent, struct device *self, void *aux)
{
	struct an_pcmcia_softc *psc = (struct an_pcmcia_softc *)self;
	struct an_softc *sc = (struct an_softc *)self;
	struct pcmcia_attach_args *pa = aux;
	struct pcmcia_config_entry *cfe;
	const char *intrstr;
	int error;

	psc->sc_pf = pa->pf;
	cfe = SIMPLEQ_FIRST(&pa->pf->cfe_head);

	pcmcia_function_init(pa->pf, cfe);
	if (pcmcia_function_enable(pa->pf)) {
		printf(": function enable failed\n");
		return;
	}

	if (pcmcia_io_alloc(pa->pf, 0, AN_IOSIZ, AN_IOSIZ, &psc->sc_pcioh)) {
		printf(": can't alloc i/o space\n");
		pcmcia_function_disable(pa->pf);
		return;
	}

	if (pcmcia_io_map(pa->pf, PCMCIA_WIDTH_IO16, 0, AN_IOSIZ,
	    &psc->sc_pcioh, &psc->sc_io_window)) {
		printf(": can't map i/o space\n");
		pcmcia_io_free(pa->pf, &psc->sc_pcioh);
		pcmcia_function_disable(pa->pf);
		return;
	}

	sc->sc_iot = psc->sc_pcioh.iot;
	sc->sc_ioh = psc->sc_pcioh.ioh;
	sc->sc_enabled = 1;

	sc->sc_ih = pcmcia_intr_establish(psc->sc_pf, IPL_NET, an_intr, sc,
	    sc->sc_dev.dv_xname);
	intrstr = pcmcia_intr_string(psc->sc_pf, sc->sc_ih);
	if (*intrstr)
		printf(", %s", intrstr);
	printf("\n");

	error = an_attach(sc);
	if (error) {
		printf("%s: failed to attach controller\n",
		    self->dv_xname);
		return;
	}

	sc->sc_enabled = 0;
	psc->sc_state = AN_PCMCIA_ATTACHED;
}

int
an_pcmcia_detach(struct device *dev, int flags)
{
	struct an_pcmcia_softc *psc = (struct an_pcmcia_softc *)dev;
	int error;

	if (psc->sc_state != AN_PCMCIA_ATTACHED)
		return (0);

	error = an_detach(&psc->sc_an);
	if (error)
		return (error);

	pcmcia_io_unmap(psc->sc_pf, psc->sc_io_window);
	pcmcia_io_free(psc->sc_pf, &psc->sc_pcioh);

	return 0;
}

int
an_pcmcia_activate(struct device *dev, int act)
{
	struct an_pcmcia_softc *psc = (struct an_pcmcia_softc *)dev;
	struct an_softc *sc = &psc->sc_an;
	struct ieee80211com	*ic = &sc->sc_ic;
	struct ifnet		*ifp = &ic->ic_if;

	switch (act) {
	case DVACT_DEACTIVATE:
		ifp->if_timer = 0;
		if (ifp->if_flags & IFF_RUNNING)
			an_stop(ifp, 1);
		if (sc->sc_ih)
			pcmcia_intr_disestablish(psc->sc_pf, sc->sc_ih);
		sc->sc_ih = NULL;
		pcmcia_function_disable(psc->sc_pf);
		break;
	}
	return (0);
}
