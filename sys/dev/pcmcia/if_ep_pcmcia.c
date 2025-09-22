/*	$OpenBSD: if_ep_pcmcia.c,v 1.52 2024/05/26 08:46:28 jsg Exp $	*/
/*	$NetBSD: if_ep_pcmcia.c,v 1.16 1998/08/17 23:20:40 thorpej Exp $  */

/*-
 * Copyright (c) 1998 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Jason R. Thorpe of the Numerical Aerospace Simulation Facility,
 * NASA Ames Research Center.
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
#include <sys/timeout.h>
#include <sys/device.h>

#include <net/if.h>
#include <net/if_media.h>

#include <netinet/in.h>
#include <netinet/if_ether.h>

#include <machine/bus.h>

#include <dev/mii/miivar.h>

#include <dev/ic/elink3var.h>

#include <dev/pcmcia/pcmciareg.h>
#include <dev/pcmcia/pcmciavar.h>
#include <dev/pcmcia/pcmciadevs.h>

int	ep_pcmcia_match(struct device *, void *, void *);
void	ep_pcmcia_attach(struct device *, struct device *, void *);
int	ep_pcmcia_detach(struct device *, int);
int	ep_pcmcia_activate(struct device *, int);

int	ep_pcmcia_get_enaddr(struct pcmcia_tuple *, void *);
#ifdef notyet
int	ep_pcmcia_enable(struct ep_softc *);
void	ep_pcmcia_disable(struct ep_softc *);
void	ep_pcmcia_disable1(struct ep_softc *);
#endif

int	ep_pcmcia_enable1(struct ep_softc *);

struct ep_pcmcia_softc {
	struct ep_softc sc_ep;			/* real "ep" softc */

	/* PCMCIA-specific goo */
	struct pcmcia_io_handle sc_pcioh;	/* PCMCIA i/o space info */
	int sc_io_window;			/* our i/o window */
	struct pcmcia_function *sc_pf;		/* our PCMCIA function */
};

const struct cfattach ep_pcmcia_ca = {
	sizeof(struct ep_pcmcia_softc), ep_pcmcia_match, ep_pcmcia_attach,
	ep_pcmcia_detach, ep_pcmcia_activate
};

struct ep_pcmcia_product {
	u_int16_t	epp_product;	/* PCMCIA product ID */
	u_short		epp_chipset;	/* 3Com chipset used */
	int		epp_flags;	/* initial softc flags */
	int		epp_expfunc;	/* expected function */
} ep_pcmcia_prod[] = {
	{ PCMCIA_PRODUCT_3COM_3C562,	EP_CHIPSET_3C509,
	  0,				0 },

	{ PCMCIA_PRODUCT_3COM_3C589,	EP_CHIPSET_3C509,
	  0,				0 },

	{ PCMCIA_PRODUCT_3COM_3CXEM556,	EP_CHIPSET_3C509,
	  0,				0 },

	{ PCMCIA_PRODUCT_3COM_3CXEM556B,EP_CHIPSET_3C509,
	  0,				0 },

	{ PCMCIA_PRODUCT_3COM_3C1,	EP_CHIPSET_3C509,
	  0,				0 },

	{ PCMCIA_PRODUCT_3COM_3CCFEM556BI, EP_CHIPSET_ROADRUNNER,
	  EP_FLAGS_MII,			0 },

	{ PCMCIA_PRODUCT_3COM_3C574,	EP_CHIPSET_ROADRUNNER,
	  EP_FLAGS_MII,			0 }
};

struct ep_pcmcia_product *ep_pcmcia_lookup(struct pcmcia_attach_args *);

struct ep_pcmcia_product *
ep_pcmcia_lookup(struct pcmcia_attach_args *pa)
{
	int i;

	for (i = 0; i < nitems(ep_pcmcia_prod); i++)
		if (pa->product == ep_pcmcia_prod[i].epp_product &&
		    pa->pf->number == ep_pcmcia_prod[i].epp_expfunc)
			return &ep_pcmcia_prod[i];

	return (NULL);
}

int
ep_pcmcia_match(struct device *parent, void *match, void *aux)
{
	struct pcmcia_attach_args *pa = aux;

	if (pa->manufacturer != PCMCIA_VENDOR_3COM)
		return (0);

	if (ep_pcmcia_lookup(pa) != NULL)
		return (1);

	return (0);
}

#ifdef notdef
int
ep_pcmcia_enable(struct ep_softc *sc)
{
	struct ep_pcmcia_softc *psc = (struct ep_pcmcia_softc *) sc;
	struct pcmcia_function *pf = psc->sc_pf;

	/* establish the interrupt. */
	sc->sc_ih = pcmcia_intr_establish(pf, IPL_NET, epintr,
	    sc, sc->sc_dev.dv_xname);
	if (sc->sc_ih == NULL) {
		printf("%s: couldn't establish interrupt\n",
		    sc->sc_dev.dv_xname);
		return (1);
	}

	return (ep_pcmcia_enable1(sc));
}
#endif

int
ep_pcmcia_enable1(struct ep_softc *sc)
{
	struct ep_pcmcia_softc *psc = (struct ep_pcmcia_softc *) sc;
	struct pcmcia_function *pf = psc->sc_pf;
	int ret;

	if ((ret = pcmcia_function_enable(pf)))
		return (ret);

	if ((psc->sc_pf->sc->card.product == PCMCIA_PRODUCT_3COM_3C562) ||
	    (psc->sc_pf->sc->card.product == PCMCIA_PRODUCT_3COM_3CXEM556) ||
	    (psc->sc_pf->sc->card.product == PCMCIA_PRODUCT_3COM_3CXEM556B)) {
		int reg;

		/* turn off the serial-disable bit */

		reg = pcmcia_ccr_read(pf, PCMCIA_CCR_OPTION);
		if (reg & 0x08) {
			reg &= ~0x08;
			pcmcia_ccr_write(pf, PCMCIA_CCR_OPTION, reg);
		}

	}

	return (ret);
}

#ifdef notyet
void
ep_pcmcia_disable(struct ep_softc *sc)
{
	struct ep_pcmcia_softc *psc = (struct ep_pcmcia_softc *) sc;

	pcmcia_intr_disestablish(psc->sc_pf, sc->sc_ih);
	ep_pcmcia_disable1(sc);
}

void
ep_pcmcia_disable1(struct ep_softc *sc)
{
	struct ep_pcmcia_softc *psc = (struct ep_pcmcia_softc *) sc;

	pcmcia_function_disable(psc->sc_pf);
}
#endif

void
ep_pcmcia_attach(struct device *parent, struct device *self, void *aux)
{
	struct ep_pcmcia_softc *psc = (void *) self;
	struct ep_softc *sc = &psc->sc_ep;
	struct pcmcia_attach_args *pa = aux;
	struct pcmcia_config_entry *cfe;
	struct ep_pcmcia_product *epp;
	u_int8_t myla[ETHER_ADDR_LEN];
	u_int8_t *enaddr = NULL;
	const char *intrstr;
	int i;

	psc->sc_pf = pa->pf;
	cfe = SIMPLEQ_FIRST(&pa->pf->cfe_head);

	/* Enable the card. */
	pcmcia_function_init(pa->pf, cfe);
	if (ep_pcmcia_enable1(sc))
		printf(": function enable failed\n");

#ifdef notyet
	sc->enabled = 1;
#endif

	if (cfe->num_memspace != 0)
		printf(": unexpected number of memory spaces %d should be 0\n",
		    cfe->num_memspace);

	if (cfe->num_iospace != 1)
		printf(": unexpected number of I/O spaces %d should be 1\n",
		    cfe->num_iospace);

	if (pa->product == PCMCIA_PRODUCT_3COM_3C562) {
		bus_addr_t maxaddr = (pa->pf->sc->iobase + pa->pf->sc->iosize);

		for (i = pa->pf->sc->iobase; i < maxaddr; i += 0x10) {
			/*
			 * the 3c562 can only use 0x??00-0x??7f
			 * according to the Linux driver
			 */
			if (i & 0x80)
				continue;
			if (pcmcia_io_alloc(pa->pf, i, cfe->iospace[0].length,
			    cfe->iospace[0].length, &psc->sc_pcioh) == 0)
				break;
		}
		if (i >= maxaddr) {
			printf(": can't allocate i/o space\n");
			return;
		}
	} else {
		if (pcmcia_io_alloc(pa->pf, 0, cfe->iospace[0].length,
		    cfe->iospace[0].length, &psc->sc_pcioh))
			printf(": can't allocate i/o space\n");
	}

	sc->sc_iot = psc->sc_pcioh.iot;
	sc->sc_ioh = psc->sc_pcioh.ioh;

	if (pcmcia_io_map(pa->pf, ((cfe->flags & PCMCIA_CFE_IO16) ?
	    PCMCIA_WIDTH_IO16 : PCMCIA_WIDTH_IO8), 0, cfe->iospace[0].length,
	    &psc->sc_pcioh, &psc->sc_io_window)) {
		printf(": can't map i/o space\n");
		return;
	}

	printf(" port 0x%lx/%ld", psc->sc_pcioh.addr, psc->sc_pcioh.size);

	switch (pa->product) {
	case PCMCIA_PRODUCT_3COM_3C562:
		/*
		 * 3c562a-c use this; 3c562d does it in the regular way.
		 * we might want to check the revision and produce a warning
		 * in the future.
		 */
		/* FALLTHROUGH */
	case PCMCIA_PRODUCT_3COM_3C574:
	case PCMCIA_PRODUCT_3COM_3CCFEM556BI:
		/*
		 * Apparently, some 3c574s do it this way, as well.
		 */
		if (pcmcia_scan_cis(parent, ep_pcmcia_get_enaddr, myla))
			enaddr = myla;
		break;
	}

	sc->bustype = EP_BUS_PCMCIA;

	epp = ep_pcmcia_lookup(pa);
	if (epp == NULL)
		panic("ep_pcmcia_attach: impossible");

	sc->ep_flags = epp->epp_flags;

#ifdef notyet
	sc->enable = ep_pcmcia_enable;
	sc->disable = ep_pcmcia_disable;
#endif

	/* establish the interrupt. */
	sc->sc_ih = pcmcia_intr_establish(pa->pf, IPL_NET, epintr, sc,
	    sc->sc_dev.dv_xname);
	intrstr = pcmcia_intr_string(psc->sc_pf, sc->sc_ih);
	if (*intrstr)
		printf(", %s", intrstr);

	printf(":");

	epconfig(sc, epp->epp_chipset, enaddr);

#ifdef notyet
	sc->enabled = 0;

	ep_pcmcia_disable1(sc);
#endif
}

int
ep_pcmcia_detach(struct device *dev, int flags)
{
	int rv;
	struct ep_pcmcia_softc *psc = (struct ep_pcmcia_softc *)dev;

	if ((rv = ep_detach(dev)) != 0)
		return (rv);

	pcmcia_io_unmap(psc->sc_pf, psc->sc_io_window);
	pcmcia_io_free(psc->sc_pf, &psc->sc_pcioh);

	return (0);
}

int
ep_pcmcia_activate(struct device *dev, int act)
{
	struct ep_pcmcia_softc *sc = (struct ep_pcmcia_softc *)dev;
	struct ep_softc *esc = &sc->sc_ep;
	struct ifnet *ifp = &esc->sc_arpcom.ac_if;

	switch (act) {
	case DVACT_SUSPEND:
		ifp->if_timer = 0;
		if (ifp->if_flags & IFF_RUNNING)
			epstop(esc);
		if (sc->sc_ep.sc_ih)
			pcmcia_intr_disestablish(sc->sc_pf, sc->sc_ep.sc_ih);
		sc->sc_ep.sc_ih = NULL;
		pcmcia_function_disable(sc->sc_pf);
		break;
	case DVACT_RESUME:
		pcmcia_function_enable(sc->sc_pf);
		sc->sc_ep.sc_ih = pcmcia_intr_establish(sc->sc_pf, IPL_NET,
		    epintr, sc, esc->sc_dev.dv_xname);
		if (ifp->if_flags & IFF_UP)
			epinit(esc);
		break;
	case DVACT_DEACTIVATE:
		if (sc->sc_ep.sc_ih)
			pcmcia_intr_disestablish(sc->sc_pf, sc->sc_ep.sc_ih);
		sc->sc_ep.sc_ih = NULL;
		pcmcia_function_disable(sc->sc_pf);
		break;
	}
	return (0);
}

int
ep_pcmcia_get_enaddr(struct pcmcia_tuple *tuple, void *arg)
{
	u_int8_t *myla = arg;
	int i;

	/* this is 3c562a-c magic */
	if (tuple->code == 0x88) {
		if (tuple->length < ETHER_ADDR_LEN)
			return (0);

		for (i = 0; i < ETHER_ADDR_LEN; i += 2) {
			myla[i] = pcmcia_tuple_read_1(tuple, i + 1);
			myla[i + 1] = pcmcia_tuple_read_1(tuple, i);
		}

		return (1);
	}
	return (0);
}
