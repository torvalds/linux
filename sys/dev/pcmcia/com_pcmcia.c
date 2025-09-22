/*	$OpenBSD: com_pcmcia.c,v 1.62 2024/05/26 08:46:28 jsg Exp $	*/
/*	$NetBSD: com_pcmcia.c,v 1.15 1998/08/22 17:47:58 msaitoh Exp $	*/

/*
 * Copyright (c) 1997 - 1999, Jason Downs.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name(s) of the author(s) nor the name OpenBSD
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
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
 * Copyright (c) 1998 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Charles M. Hannum.
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

/*-
 * Copyright (c) 1991 The Regents of the University of California.
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
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)com.c	7.5 (Berkeley) 5/16/91
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/tty.h>
#include <sys/device.h>

#include <machine/intr.h>

#include <dev/pcmcia/pcmciavar.h>
#include <dev/pcmcia/pcmciareg.h>
#include <dev/pcmcia/pcmciadevs.h>

#include <dev/ic/comreg.h>
#include <dev/ic/comvar.h>
#include <dev/ic/ns16550reg.h>

#include <dev/isa/isareg.h>

#define	com_lcr		com_cfcr

/* Devices that we need to match by CIS strings */
struct com_pcmcia_product {
	char *cis1_info[4];
} com_pcmcia_prod[] = {
	{ PCMCIA_CIS_MEGAHERTZ_XJ2288 },
	{ PCMCIA_CIS_NOVATEL_NRM6831 },
};

int com_pcmcia_match(struct device *, void *, void *);
void com_pcmcia_attach(struct device *, struct device *, void *);
int com_pcmcia_detach(struct device *, int);
int com_pcmcia_activate(struct device *, int);

int com_pcmcia_enable(struct com_softc *);
void com_pcmcia_disable(struct com_softc *);
int com_pcmcia_enable1(struct com_softc *);
void com_pcmcia_disable1(struct com_softc *);

struct com_pcmcia_softc {
	struct com_softc sc_com;		/* real "com" softc */

	/* PCMCIA-specific goo */
	struct pcmcia_io_handle sc_pcioh;	/* PCMCIA i/o space info */
	int sc_io_window;			/* our i/o window */
	struct pcmcia_function *sc_pf;		/* our PCMCIA function */
	void *sc_ih;				/* interrupt handler */
};

const struct cfattach com_pcmcia_ca = {
	sizeof(struct com_pcmcia_softc), com_pcmcia_match, com_pcmcia_attach,
	com_pcmcia_detach, com_pcmcia_activate
};

int
com_pcmcia_match(struct device *parent, void *match, void *aux)
{
	struct pcmcia_attach_args *pa = aux;
	struct pcmcia_config_entry *cfe;
	int i, j, comportmask;

	/* 1. Does it claim to be a serial device? */
	if (pa->pf->function == PCMCIA_FUNCTION_SERIAL)
	    return 1;

	/* 2. Does it have all four 'standard' port ranges? */
	comportmask = 0;
	SIMPLEQ_FOREACH(cfe, &pa->pf->cfe_head, cfe_list) {
		switch (cfe->iospace[0].start) {
		case IO_COM1:
			comportmask |= 1;
			break;
		case IO_COM2:
			comportmask |= 2;
			break;
		case IO_COM3:
			comportmask |= 4;
			break;
		case IO_COM4:
			comportmask |= 8;
			break;
		}
	}

	if (comportmask == 15)
		return 1;

	/* 3. Is this a card we know about? */
	for (i = 0; i < nitems(com_pcmcia_prod); i++) {
		for (j = 0; j < 4; j++)
			if (com_pcmcia_prod[i].cis1_info[j] &&
			    pa->card->cis1_info[j] &&
			    strcmp(pa->card->cis1_info[j],
			    com_pcmcia_prod[i].cis1_info[j]))
				break;
		if (j == 4)
			return 1;
	}

	return 0;
}

int
com_pcmcia_activate(struct device *dev, int act)
{
	struct com_pcmcia_softc *sc = (void *) dev;

	switch (act) {
	case DVACT_SUSPEND:
		if (sc->sc_ih)
			pcmcia_intr_disestablish(sc->sc_pf, sc->sc_ih);
		sc->sc_ih = NULL;
		pcmcia_function_disable(sc->sc_pf);
		break;
	case DVACT_RESUME:
		pcmcia_function_enable(sc->sc_pf);
		sc->sc_ih = pcmcia_intr_establish(sc->sc_pf, IPL_TTY,
		    comintr, sc, sc->sc_com.sc_dev.dv_xname);
		com_resume(&sc->sc_com);
		break;
	case DVACT_DEACTIVATE:
		if (sc->sc_ih)
			pcmcia_intr_disestablish(sc->sc_pf, sc->sc_ih);
		sc->sc_ih = NULL;
		pcmcia_function_disable(sc->sc_pf);
		break;
	}
	return (0);
}

void
com_pcmcia_attach(struct device *parent, struct device *self, void *aux)
{
	struct com_pcmcia_softc *psc = (void *) self;
	struct com_softc *sc = &psc->sc_com;
	struct pcmcia_attach_args *pa = aux;
	struct pcmcia_config_entry *cfe;
	const char *intrstr;
	int autoalloc = 0;

	psc->sc_pf = pa->pf;

retry:
	/* find a cfe we can use */

	for (cfe = SIMPLEQ_FIRST(&pa->pf->cfe_head); cfe;
	     cfe = SIMPLEQ_NEXT(cfe, cfe_list)) {
#if 0
		/*
		 * Some modem cards (e.g. Xircom CM33) also have
		 * mem space.  Don't bother with this check.
		 */
		if (cfe->num_memspace != 0)
			continue;
#endif

		if (cfe->num_iospace != 1)
			continue;

		if (!pcmcia_io_alloc(pa->pf,
		    autoalloc ? 0 : cfe->iospace[0].start,
		    cfe->iospace[0].length, COM_NPORTS, &psc->sc_pcioh)) {
			goto found;
		}
	}
	if (autoalloc == 0) {
		autoalloc = 1;
		goto retry;
	} else if (!cfe) {
		printf(": can't allocate i/o space\n");
		return;
	}

found:
	sc->sc_iot = psc->sc_pcioh.iot;
	sc->sc_ioh = psc->sc_pcioh.ioh;

	/* Enable the card. */
	pcmcia_function_init(pa->pf, cfe);
	if (com_pcmcia_enable1(sc))
		printf(": function enable failed\n");

	sc->enabled = 1;

	/* map in the io space */

	if (pcmcia_io_map(pa->pf, ((cfe->flags & PCMCIA_CFE_IO16) ?
	    PCMCIA_WIDTH_IO16 : PCMCIA_WIDTH_IO8), 0, psc->sc_pcioh.size,
	    &psc->sc_pcioh, &psc->sc_io_window)) {
		printf(": can't map i/o space\n");
		return;
	}

	printf(" port 0x%lx/%lu", psc->sc_pcioh.addr,
	    (u_long)psc->sc_pcioh.size);

	sc->sc_iobase = -1;
	sc->enable = com_pcmcia_enable;
	sc->disable = com_pcmcia_disable;
	sc->sc_frequency = COM_FREQ;

	sc->sc_hwflags = 0;
	sc->sc_swflags = 0;

	if (psc->sc_pf->sc->card.manufacturer == PCMCIA_VENDOR_AUDIOVOX &&
	    psc->sc_pf->sc->card.product == PCMCIA_PRODUCT_AUDIOVOX_RTM8000)
		sc->sc_fifolen = 16;

	com_attach_subr(sc);

	/* establish the interrupt. */
	psc->sc_ih = pcmcia_intr_establish(pa->pf, IPL_TTY, comintr, sc,
	    sc->sc_dev.dv_xname);
	intrstr = pcmcia_intr_string(psc->sc_pf, psc->sc_ih);
	if (*intrstr)
		printf(", %s", intrstr);

#ifdef notyet
	sc->enabled = 0;

	com_pcmcia_disable1(sc);
#endif
}

int
com_pcmcia_detach(struct device *dev, int flags)
{
	struct com_pcmcia_softc *psc = (struct com_pcmcia_softc *)dev;
	int error;

	/* Release all resources.  */
	error = com_detach(dev, flags);
	if (error)
	    return (error);

	pcmcia_io_unmap(psc->sc_pf, psc->sc_io_window);
	pcmcia_io_free(psc->sc_pf, &psc->sc_pcioh);

	return (0);
}

int
com_pcmcia_enable(struct com_softc *sc)
{
	struct com_pcmcia_softc *psc = (struct com_pcmcia_softc *) sc;
	struct pcmcia_function *pf = psc->sc_pf;

	/* establish the interrupt. */
	psc->sc_ih = pcmcia_intr_establish(pf, IPL_TTY, comintr, sc,
	    sc->sc_dev.dv_xname);
	if (psc->sc_ih == NULL) {
		printf("%s: couldn't establish interrupt\n",
		    sc->sc_dev.dv_xname);
		return (1);
	}
	return com_pcmcia_enable1(sc);
}

int
com_pcmcia_enable1(struct com_softc *sc)
{
	struct com_pcmcia_softc *psc = (struct com_pcmcia_softc *) sc;
	struct pcmcia_function *pf = psc->sc_pf;
	int ret;

	if ((ret = pcmcia_function_enable(pf)))
	    return(ret);

	if ((psc->sc_pf->sc->card.product == PCMCIA_PRODUCT_3COM_3C562) ||
	    (psc->sc_pf->sc->card.product == PCMCIA_PRODUCT_3COM_3CXEM556) ||
	    (psc->sc_pf->sc->card.product == PCMCIA_PRODUCT_3COM_3CXEM556B)) {
		int reg;

		/* turn off the ethernet-disable bit */

		reg = pcmcia_ccr_read(pf, PCMCIA_CCR_OPTION);
		if (reg & 0x08) {
		    reg &= ~0x08;
		    pcmcia_ccr_write(pf, PCMCIA_CCR_OPTION, reg);
		}
	}

	return(ret);
}

void
com_pcmcia_disable(struct com_softc *sc)
{
	struct com_pcmcia_softc *psc = (struct com_pcmcia_softc *) sc;

	pcmcia_intr_disestablish(psc->sc_pf, psc->sc_ih);
	com_pcmcia_disable1(sc);
}

void
com_pcmcia_disable1(struct com_softc *sc)
{
	struct com_pcmcia_softc *psc = (struct com_pcmcia_softc *) sc;

	pcmcia_function_disable(psc->sc_pf);
}
