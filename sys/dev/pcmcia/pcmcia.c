/*	$OpenBSD: pcmcia.c,v 1.51 2024/05/13 01:15:51 jsg Exp $	*/
/*	$NetBSD: pcmcia.c,v 1.9 1998/08/13 02:10:55 eeh Exp $	*/

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
#include <sys/malloc.h>

#include <dev/pcmcia/pcmciareg.h>
#include <dev/pcmcia/pcmciachip.h>
#include <dev/pcmcia/pcmciavar.h>

#ifdef PCMCIADEBUG
#define	DPRINTF(arg) printf arg
#else
#define	DPRINTF(arg)
#endif

#ifdef PCMCIAVERBOSE
int	pcmcia_verbose = 1;
#else
int	pcmcia_verbose = 0;
#endif

int	pcmcia_match(struct device *, void *, void *);
int	pcmcia_submatch(struct device *, void *, void *);
void	pcmcia_attach(struct device *, struct device *, void *);
int	pcmcia_activate(struct device *, int);
int	pcmcia_print(void *, const char *);
int	pcmcia_card_intr(void *);

struct cfdriver pcmcia_cd = {
	NULL, "pcmcia", DV_DULL
};

const struct cfattach pcmcia_ca = {
	sizeof(struct pcmcia_softc), pcmcia_match, pcmcia_attach, NULL,
	pcmcia_activate
};

int
pcmcia_ccr_read(struct pcmcia_function *pf, int ccr)
{

	return (bus_space_read_1(pf->pf_ccrt, pf->pf_ccrh,
	    pf->pf_ccr_offset + ccr));
}

void
pcmcia_ccr_write(struct pcmcia_function *pf, int ccr, int val)
{

	if ((pf->ccr_mask) & (1 << (ccr / 2))) {
		bus_space_write_1(pf->pf_ccrt, pf->pf_ccrh,
		    pf->pf_ccr_offset + ccr, val);
	}
}

int
pcmcia_match(struct device *parent, void *match, void *aux)
{
	struct cfdata *cf = match;
	struct pcmciabus_attach_args *paa = aux;

	if (strcmp(paa->paa_busname, cf->cf_driver->cd_name))
		return 0;

	/* If the autoconfiguration got this far, there's a socket here. */
	return (1);
}

void
pcmcia_attach(struct device *parent, struct device *self, void *aux)
{
	struct pcmciabus_attach_args *paa = aux;
	struct pcmcia_softc *sc = (struct pcmcia_softc *) self;

	printf("\n");

	sc->pct = paa->pct;
	sc->pch = paa->pch;
	sc->iobase = paa->iobase;
	sc->iosize = paa->iosize;

	sc->ih = NULL;
}

int
pcmcia_activate(struct device *self, int act)
{
	struct pcmcia_softc *sc = (struct pcmcia_softc *)self;
	struct pcmcia_function *pf;

	switch (act) {
	case DVACT_QUIESCE:
	case DVACT_SUSPEND:
	case DVACT_POWERDOWN:
	case DVACT_RESUME:
		for (pf = SIMPLEQ_FIRST(&sc->card.pf_head); pf != NULL;
		     pf = SIMPLEQ_NEXT(pf, pf_list)) {
			if (SIMPLEQ_FIRST(&pf->cfe_head) == NULL ||
			    pf->child == NULL)
				continue;
			config_suspend(pf->child, act);
		}
		break;
	case DVACT_DEACTIVATE:
		for (pf = SIMPLEQ_FIRST(&sc->card.pf_head); pf != NULL;
		     pf = SIMPLEQ_NEXT(pf, pf_list)) {
			if (SIMPLEQ_FIRST(&pf->cfe_head) == NULL ||
			    pf->child == NULL)
				continue;
			config_deactivate(pf->child);
		}
		break;
	}
	return (0);
}

int
pcmcia_card_attach(struct device *dev)
{
	struct pcmcia_softc *sc = (struct pcmcia_softc *) dev;
	struct pcmcia_function *pf;
	struct pcmcia_attach_args paa;
	int attached;

	/*
	 * this is here so that when socket_enable calls gettype, trt happens
	 */
	SIMPLEQ_FIRST(&sc->card.pf_head) = NULL;

	pcmcia_chip_socket_enable(sc->pct, sc->pch);

	pcmcia_read_cis(sc);

	pcmcia_chip_socket_disable(sc->pct, sc->pch);

	pcmcia_check_cis_quirks(sc);

	/*
	 * Bail now if there was an error in the CIS.
	 */

	if (sc->card.error)
		return (1);

#if 0
	if (SIMPLEQ_EMPTY(&sc->card.pf_head))
		return (1);
#endif

	if (pcmcia_verbose)
		pcmcia_print_cis(sc);

	/*
	 * If there was no function, this might be CIS-less card we still
	 * want to probe.  Fixup a function element for it.
	 */
	if (SIMPLEQ_FIRST(&sc->card.pf_head) == NULL) {
		pf = malloc(sizeof *pf, M_DEVBUF, M_NOWAIT | M_ZERO);
		if (pf == NULL)
			panic("pcmcia_card_attach");
		pf->number = 0;
		pf->pf_flags = PFF_FAKE;
		pf->last_config_index = -1;
		SIMPLEQ_INIT(&pf->cfe_head);
		SIMPLEQ_INSERT_TAIL(&sc->card.pf_head, pf, pf_list);
	}

	attached = 0;

	for (pf = SIMPLEQ_FIRST(&sc->card.pf_head); pf != NULL;
	    pf = SIMPLEQ_NEXT(pf, pf_list)) {
		pf->sc = sc;
		pf->child = NULL;
		pf->cfe = NULL;
		pf->ih_fct = NULL;
		pf->ih_arg = NULL;
	}

	for (pf = SIMPLEQ_FIRST(&sc->card.pf_head); pf != NULL;
	    pf = SIMPLEQ_NEXT(pf, pf_list)) {
		paa.manufacturer = sc->card.manufacturer;
		paa.product = sc->card.product;
		paa.card = &sc->card;
		paa.pf = pf;

		pf->child = config_found_sm(&sc->dev, &paa, pcmcia_print,
		    pcmcia_submatch);
		if (pf->child) {
			attached++;

			if ((pf->pf_flags & (PFF_FAKE | PFF_ENABLED)) ==
			    PFF_ENABLED)
				DPRINTF(("%s: function %d CCR at %d offset %lx"
					": %x %x %x %x, %x %x %x %x, %x\n",
					sc->dev.dv_xname, pf->number,
					pf->pf_ccr_window, pf->pf_ccr_offset,
					pcmcia_ccr_read(pf, 0x00),
					pcmcia_ccr_read(pf, 0x02),
					pcmcia_ccr_read(pf, 0x04),
					pcmcia_ccr_read(pf, 0x06),
					pcmcia_ccr_read(pf, 0x0A),
					pcmcia_ccr_read(pf, 0x0C),
					pcmcia_ccr_read(pf, 0x0E),
					pcmcia_ccr_read(pf, 0x10),
					pcmcia_ccr_read(pf, 0x12)));
		}
	}
	return (attached ? 0 : 1);
}

void
pcmcia_card_detach(struct device *dev, int flags)
{
	struct pcmcia_softc *sc = (struct pcmcia_softc *) dev;
	struct pcmcia_function *pf;
	int error;

	/*
	 * We are running on either the PCMCIA socket's event thread
	 * or in user context detaching a device by user request.
	 */
	for (pf = SIMPLEQ_FIRST(&sc->card.pf_head); pf != NULL;
	     pf = SIMPLEQ_NEXT(pf, pf_list)) {
		if (SIMPLEQ_FIRST(&pf->cfe_head) == NULL)
			continue;
		if (pf->child == NULL)
			continue;
		DPRINTF(("%s: detaching %s (function %d)\n",
		    sc->dev.dv_xname, pf->child->dv_xname, pf->number));
		if ((error = config_detach(pf->child, flags)) != 0) {
			printf("%s: error %d detaching %s (function %d)\n",
			    sc->dev.dv_xname, error, pf->child->dv_xname,
			    pf->number);
		} else
			pf->child = NULL;
	}
}

void
pcmcia_card_deactivate(struct device *dev)
{
	struct pcmcia_softc *sc = (struct pcmcia_softc *) dev;
	struct pcmcia_function *pf;

	/*
	 * We're in the chip's card removal interrupt handler.
	 * Deactivate the child driver.  The PCMCIA socket's
	 * event thread will run later to finish the detach.
	 */
	for (pf = SIMPLEQ_FIRST(&sc->card.pf_head); pf != NULL;
	     pf = SIMPLEQ_NEXT(pf, pf_list)) {
		if (SIMPLEQ_FIRST(&pf->cfe_head) == NULL ||
		    pf->child == NULL)
			continue;
		DPRINTF(("%s: deactivating %s (function %d)\n",
		    sc->dev.dv_xname, pf->child->dv_xname, pf->number));
		config_deactivate(pf->child);
	}
}

int
pcmcia_submatch(struct device *parent, void *match, void *aux)
{
	struct cfdata *cf = match;
	struct pcmcia_attach_args *paa = aux;

	if (cf->cf_loc[0 /* PCMCIACF_FUNCTION */] !=
	    -1 /* PCMCIACF_FUNCTION_DEFAULT */ &&
	    cf->cf_loc[0 /* PCMCIACF_FUNCTION */] != paa->pf->number)
		return (0);

	return ((*cf->cf_attach->ca_match)(parent, cf, aux));
}

int
pcmcia_print(void *arg, const char *pnp)
{
	struct pcmcia_attach_args *pa = arg;
	struct pcmcia_softc *sc = pa->pf->sc;
	struct pcmcia_card *card = &sc->card;
	int i;

	if (pnp) {
		for (i = 0; i < 4 && card->cis1_info[i]; i++)
			printf("%s%s", i ? ", " : "\"", card->cis1_info[i]);
		if (i != 0)
			printf("\"");

		if (card->manufacturer != PCMCIA_VENDOR_INVALID &&
		    card->product != PCMCIA_PRODUCT_INVALID) {
			if (i != 0)
				printf(" ");
			printf("(");
			if (card->manufacturer != PCMCIA_VENDOR_INVALID)
				printf("manufacturer 0x%x%s",
				    card->manufacturer,
				    card->product == PCMCIA_PRODUCT_INVALID ?
				    "" : ", ");
			if (card->product != PCMCIA_PRODUCT_INVALID)
				printf("product 0x%x",
				    card->product);
			printf(")");
		}
		if (i != 0)
			printf(" ");
		printf("at %s", pnp);
	}
	printf(" function %d", pa->pf->number);

	if (!pnp) {
		for (i = 0; i < 3 && card->cis1_info[i]; i++)
			printf("%s%s", i ? ", " : " \"", card->cis1_info[i]);
		if (i != 0)
			printf("\"");
	}

	return (UNCONF);
}

int
pcmcia_card_gettype(struct device *dev)
{
	struct pcmcia_softc *sc = (struct pcmcia_softc *)dev;
	struct pcmcia_function *pf;

	/*
	 * Set the iftype to memory if this card has no functions (not yet
	 * probed), or only one function, and that is not initialized yet or
	 * that is memory.
	 */
	pf = SIMPLEQ_FIRST(&sc->card.pf_head);
	if (pf == NULL || (SIMPLEQ_NEXT(pf, pf_list) == NULL &&
	    ((pf->pf_flags & PFF_FAKE) ||
	    pf->cfe == NULL || pf->cfe->iftype == PCMCIA_IFTYPE_MEMORY)))
		return (PCMCIA_IFTYPE_MEMORY);
	else
		return (PCMCIA_IFTYPE_IO);
}

/*
 * Initialize a PCMCIA function.  May be called as long as the function is
 * disabled.
 */
void
pcmcia_function_init(struct pcmcia_function *pf,
    struct pcmcia_config_entry *cfe)
{
	if (pf->pf_flags & PFF_ENABLED)
		panic("pcmcia_function_init: function is enabled");

	/* Remember which configuration entry we are using. */
	pf->cfe = cfe;
}

/* Enable a PCMCIA function */
int
pcmcia_function_enable(struct pcmcia_function *pf)
{
	struct pcmcia_function *tmp;
	int reg;

	if (pf->cfe == NULL)
		panic("pcmcia_function_enable: function not initialized");

	/*
	 * Increase the reference count on the socket, enabling power, if
	 * necessary.
	 */
	if (pf->sc->sc_enabled_count++ == 0)
		pcmcia_chip_socket_enable(pf->sc->pct, pf->sc->pch);
	DPRINTF(("%s: ++enabled_count = %d\n", pf->sc->dev.dv_xname,
		 pf->sc->sc_enabled_count));

	if (pf->pf_flags & PFF_ENABLED) {
		/*
		 * Don't do anything if we're already enabled.
		 */
		DPRINTF(("%s: pcmcia_function_enable on enabled func\n",
		    pf->sc->dev.dv_xname));
		return (0);
	}

	/* If there was no CIS don't mess with CCR */
	if (pf->pf_flags & PFF_FAKE)
		goto done;

	/*
	 * It's possible for different functions' CCRs to be in the same
	 * underlying page.  Check for that.
	 */
	SIMPLEQ_FOREACH(tmp, &pf->sc->card.pf_head, pf_list) {
		if ((tmp->pf_flags & PFF_ENABLED) &&
		    (pf->ccr_base >= (tmp->ccr_base - tmp->pf_ccr_offset)) &&
		    ((pf->ccr_base + PCMCIA_CCR_SIZE) <=
		     (tmp->ccr_base - tmp->pf_ccr_offset +
		      tmp->pf_ccr_realsize))) {
			pf->pf_ccrt = tmp->pf_ccrt;
			pf->pf_ccrh = tmp->pf_ccrh;
			pf->pf_ccr_realsize = tmp->pf_ccr_realsize;

			/*
			 * pf->pf_ccr_offset = (tmp->pf_ccr_offset -
			 * tmp->ccr_base) + pf->ccr_base;
			 */
			pf->pf_ccr_offset =
			    (tmp->pf_ccr_offset + pf->ccr_base) -
			    tmp->ccr_base;
			pf->pf_ccr_window = tmp->pf_ccr_window;
			break;
		}
	}

	if (tmp == NULL) {
		if (pcmcia_mem_alloc(pf, PCMCIA_CCR_SIZE, &pf->pf_pcmh))
			goto bad;

		if (pcmcia_mem_map(pf, PCMCIA_MEM_ATTR, pf->ccr_base,
		    PCMCIA_CCR_SIZE, &pf->pf_pcmh, &pf->pf_ccr_offset,
		    &pf->pf_ccr_window)) {
			pcmcia_mem_free(pf, &pf->pf_pcmh);
			goto bad;
		}
	}

	reg = (pf->cfe->number & PCMCIA_CCR_OPTION_CFINDEX);
	reg |= PCMCIA_CCR_OPTION_LEVIREQ;
	if (pcmcia_mfc(pf->sc)) {
		reg |= PCMCIA_CCR_OPTION_FUNC_ENABLE;
		if (pf->ccr_mask & (1 << (PCMCIA_CCR_IOBASE0 / 2)))
			reg |= PCMCIA_CCR_OPTION_ADDR_DECODE;
		if (pf->ih_fct)
			reg |= PCMCIA_CCR_OPTION_IREQ_ENABLE;

	}

	pcmcia_ccr_write(pf, PCMCIA_CCR_OPTION, reg);

	reg = 0;

	if ((pf->cfe->flags & PCMCIA_CFE_IO16) == 0)
		reg |= PCMCIA_CCR_STATUS_IOIS8;
	if (pf->cfe->flags & PCMCIA_CFE_AUDIO)
		reg |= PCMCIA_CCR_STATUS_AUDIO;
	pcmcia_ccr_write(pf, PCMCIA_CCR_STATUS, reg);

	pcmcia_ccr_write(pf, PCMCIA_CCR_SOCKETCOPY, 0);

	if (pcmcia_mfc(pf->sc)) {
		pcmcia_ccr_write(pf, PCMCIA_CCR_IOBASE0,
				 (pf->pf_mfc_iobase >>  0) & 0xff);
		pcmcia_ccr_write(pf, PCMCIA_CCR_IOBASE1,
				 (pf->pf_mfc_iobase >>  8) & 0xff);
		pcmcia_ccr_write(pf, PCMCIA_CCR_IOBASE2,
				 (pf->pf_mfc_iobase >> 16) & 0xff);
		pcmcia_ccr_write(pf, PCMCIA_CCR_IOBASE3,
				 (pf->pf_mfc_iobase >> 24) & 0xff);
		pcmcia_ccr_write(pf, PCMCIA_CCR_IOSIZE,
				 pf->pf_mfc_iomax - pf->pf_mfc_iobase);
	}

#ifdef PCMCIADEBUG
	SIMPLEQ_FOREACH(tmp, &pf->sc->card.pf_head, pf_list) {
		printf("%s: function %d CCR at %d offset %lx: "
		       "%x %x %x %x, %x %x %x %x, %x\n",
		       tmp->sc->dev.dv_xname, tmp->number,
		       tmp->pf_ccr_window, tmp->pf_ccr_offset,
		       pcmcia_ccr_read(tmp, 0x00),
		       pcmcia_ccr_read(tmp, 0x02),
		       pcmcia_ccr_read(tmp, 0x04),
		       pcmcia_ccr_read(tmp, 0x06),

		       pcmcia_ccr_read(tmp, 0x0A),
		       pcmcia_ccr_read(tmp, 0x0C),
		       pcmcia_ccr_read(tmp, 0x0E),
		       pcmcia_ccr_read(tmp, 0x10),

		       pcmcia_ccr_read(tmp, 0x12));
	}
#endif

 done:
	pf->pf_flags |= PFF_ENABLED;
	delay(1000);
	return (0);

 bad:
	/*
	 * Decrement the reference count, and power down the socket, if
	 * necessary.
	 */
	if (--pf->sc->sc_enabled_count == 0)
		pcmcia_chip_socket_disable(pf->sc->pct, pf->sc->pch);
	DPRINTF(("%s: --enabled_count = %d\n", pf->sc->dev.dv_xname,
		 pf->sc->sc_enabled_count));

	return (1);
}

/* Disable PCMCIA function. */
void
pcmcia_function_disable(struct pcmcia_function *pf)
{
	struct pcmcia_function *tmp;

	if (pf->cfe == NULL)
		panic("pcmcia_function_enable: function not initialized");

	if ((pf->pf_flags & PFF_ENABLED) == 0) {
		/*
		 * Don't do anything if we're already disabled.
		 */
		return;
	}

	/* If there was no CIS don't mess with CCR */
	if (pf->pf_flags & PFF_FAKE) {
		pf->pf_flags &= ~PFF_ENABLED;
		goto done;
	}

	/*
	 * it's possible for different functions' CCRs to be in the same
	 * underlying page.  Check for that.  Note we mark us as disabled
	 * first to avoid matching ourself.
	 */
	pf->pf_flags &= ~PFF_ENABLED;
	SIMPLEQ_FOREACH(tmp, &pf->sc->card.pf_head, pf_list) {
		if ((tmp->pf_flags & PFF_ENABLED) &&
		    (pf->ccr_base >= (tmp->ccr_base - tmp->pf_ccr_offset)) &&
		    ((pf->ccr_base + PCMCIA_CCR_SIZE) <=
		(tmp->ccr_base - tmp->pf_ccr_offset + tmp->pf_ccr_realsize)))
			break;
	}

	/* Not used by anyone else; unmap the CCR. */
	if (tmp == NULL) {
		pcmcia_mem_unmap(pf, pf->pf_ccr_window);
		pcmcia_mem_free(pf, &pf->pf_pcmh);
	}

 done:
	/*
	 * Decrement the reference count, and power down the socket, if
	 * necessary.
	 */
	if (--pf->sc->sc_enabled_count == 0)
		pcmcia_chip_socket_disable(pf->sc->pct, pf->sc->pch);
	DPRINTF(("%s: --enabled_count = %d\n", pf->sc->dev.dv_xname,
		 pf->sc->sc_enabled_count));
}

int
pcmcia_io_map(struct pcmcia_function *pf, int width, bus_addr_t offset,
    bus_size_t size, struct pcmcia_io_handle *pcihp, int *windowp)
{
	int reg;

	if (pcmcia_chip_io_map(pf->sc->pct, pf->sc->pch,
	    width, offset, size, pcihp, windowp))
		return (1);

	/*
	 * XXX In the multifunction multi-iospace-per-function case, this
	 * needs to cooperate with io_alloc to make sure that the spaces
	 * don't overlap, and that the ccr's are set correctly.
	 */

	if (pcmcia_mfc(pf->sc) &&
	    (pf->ccr_mask & (1 << (PCMCIA_CCR_IOBASE0 / 2)))) {
		bus_addr_t iobase = pcihp->addr;
		bus_addr_t iomax = pcihp->addr + pcihp->size - 1;

		if (pf->pf_mfc_iomax == 0) {
			pf->pf_mfc_iobase = iobase;
			pf->pf_mfc_iomax = iomax;
		} else {
			if (iobase < pf->pf_mfc_iobase)
				pf->pf_mfc_iobase = iobase;
			if (iomax > pf->pf_mfc_iomax)
				pf->pf_mfc_iomax = iomax;
		}

		pcmcia_ccr_write(pf, PCMCIA_CCR_IOBASE0,
				 (pf->pf_mfc_iobase >>  0) & 0xff);
		pcmcia_ccr_write(pf, PCMCIA_CCR_IOBASE1,
				 (pf->pf_mfc_iobase >>  8) & 0xff);
		pcmcia_ccr_write(pf, PCMCIA_CCR_IOBASE2,
				 (pf->pf_mfc_iobase >> 16) & 0xff);
		pcmcia_ccr_write(pf, PCMCIA_CCR_IOBASE3,
				 (pf->pf_mfc_iobase >> 24) & 0xff);
		pcmcia_ccr_write(pf, PCMCIA_CCR_IOSIZE,
				 pf->pf_mfc_iomax - pf->pf_mfc_iobase);

		reg = pcmcia_ccr_read(pf, PCMCIA_CCR_OPTION);
		reg |= PCMCIA_CCR_OPTION_ADDR_DECODE;
		pcmcia_ccr_write(pf, PCMCIA_CCR_OPTION, reg);
	}
	return (0);
}

void *
pcmcia_intr_establish(struct pcmcia_function *pf, int ipl,
    int (*ih_fct)(void *), void *ih_arg, char *xname)
{
	void *ret;
	int s, ihcnt, hiipl, reg;
	struct pcmcia_function *pf2;

	/* Behave differently if this is a multifunction card. */
	if (pcmcia_mfc(pf->sc)) {
		/*
		 * Mask all the ipl's which are already used by this card,
		 * and find the highest ipl number (lowest priority).
		 */
		ihcnt = 0;
		SIMPLEQ_FOREACH(pf2, &pf->sc->card.pf_head, pf_list) {
			if (pf2->ih_fct) {
				DPRINTF(("%s: function %d has ih_fct %p\n",
				    pf->sc->dev.dv_xname, pf2->number,
				    pf2->ih_fct));

				if (ihcnt == 0)
					hiipl = pf2->ih_ipl;
				else if (pf2->ih_ipl > hiipl)
					hiipl = pf2->ih_ipl;

				ihcnt++;
			}
		}

		/*
		 * Establish the real interrupt, changing the ipl if
		 * necessary.
		 */
		if (ihcnt == 0) {
#ifdef DIAGNOSTIC
			if (pf->sc->ih != NULL)
				panic("card has intr handler, "
				    "but no function does");
#endif
			s = spltty();

			/* Set up the handler for the new function. */
			pf->ih_fct = ih_fct;
			pf->ih_arg = ih_arg;
			pf->ih_ipl = ipl;

			pf->sc->ih = pcmcia_chip_intr_establish(pf->sc->pct,
			    pf->sc->pch, pf, ipl, pcmcia_card_intr, pf->sc,
			    xname);
			splx(s);
		} else if (ipl > hiipl) {
#ifdef DIAGNOSTIC
			if (pf->sc->ih == NULL)
				panic("functions have ih, "
				    "but the card does not");
#endif

			s = spltty();

			pcmcia_chip_intr_disestablish(pf->sc->pct, pf->sc->pch,
			    pf->sc->ih);

			/* set up the handler for the new function */
			pf->ih_fct = ih_fct;
			pf->ih_arg = ih_arg;
			pf->ih_ipl = ipl;

			pf->sc->ih = pcmcia_chip_intr_establish(pf->sc->pct,
			    pf->sc->pch, pf, ipl, pcmcia_card_intr, pf->sc,
			    xname);

			splx(s);
		} else {
			s = spltty();

			/* Set up the handler for the new function. */
			pf->ih_fct = ih_fct;
			pf->ih_arg = ih_arg;
			pf->ih_ipl = ipl;

			splx(s);
		}

		ret = pf->sc->ih;

		if (ret != NULL) {
			reg = pcmcia_ccr_read(pf, PCMCIA_CCR_OPTION);
			reg |= PCMCIA_CCR_OPTION_IREQ_ENABLE;
			pcmcia_ccr_write(pf, PCMCIA_CCR_OPTION, reg);

			reg = pcmcia_ccr_read(pf, PCMCIA_CCR_STATUS);
			reg |= PCMCIA_CCR_STATUS_INTRACK;
			pcmcia_ccr_write(pf, PCMCIA_CCR_STATUS, reg);
		}
	} else
		ret = pcmcia_chip_intr_establish(pf->sc->pct, pf->sc->pch,
		    pf, ipl, ih_fct, ih_arg, xname);

	return (ret);
}

void
pcmcia_intr_disestablish(struct pcmcia_function *pf, void *ih)
{
	int s, reg, ihcnt, hiipl;
	struct pcmcia_function *pf2;

	/* Behave differently if this is a multifunction card.  */
	if (pcmcia_mfc(pf->sc)) {
		/*
		 * Mask all the ipl's which are already used by this card,
		 * and find the highest ipl number (lowest priority).  Skip
		 * the current function.
		 */
		ihcnt = 0;
		SIMPLEQ_FOREACH(pf2, &pf->sc->card.pf_head, pf_list) {
			if (pf2 == pf)
				continue;

			if (pf2->ih_fct) {
				if (ihcnt == 0)
					hiipl = pf2->ih_ipl;
				else if (pf2->ih_ipl > hiipl)
					hiipl = pf2->ih_ipl;
				ihcnt++;
			}
		}

		/*
		 * If the ih being removed is lower priority than the lowest
		 * priority remaining interrupt, up the priority.
		 */

		/*
		 * ihcnt is the number of interrupt handlers *not* including
		 * the one about to be removed.
		 */
		if (ihcnt == 0) {
#ifdef DIAGNOSTIC
			if (pf->sc->ih == NULL)
				panic("disestablishing last function, but card has no ih");
#endif
			pcmcia_chip_intr_disestablish(pf->sc->pct, pf->sc->pch,
			    pf->sc->ih);

			reg = pcmcia_ccr_read(pf, PCMCIA_CCR_OPTION);
			reg &= ~PCMCIA_CCR_OPTION_IREQ_ENABLE;
			pcmcia_ccr_write(pf, PCMCIA_CCR_OPTION, reg);

			pf->ih_fct = NULL;
			pf->ih_arg = NULL;

			pf->sc->ih = NULL;
		} else if (pf->ih_ipl > hiipl) {
#ifdef DIAGNOSTIC
			if (pf->sc->ih == NULL)
				panic("changing ih ipl, but card has no ih");
#endif
			s = spltty();

			pcmcia_chip_intr_disestablish(pf->sc->pct, pf->sc->pch,
			    pf->sc->ih);
			pf->sc->ih = pcmcia_chip_intr_establish(pf->sc->pct,
			    pf->sc->pch, pf, hiipl, pcmcia_card_intr, pf->sc,
			    NULL);

			/* Null out the handler for this function. */
			pf->ih_fct = NULL;
			pf->ih_arg = NULL;

			splx(s);
		} else {
			s = spltty();

			pf->ih_fct = NULL;
			pf->ih_arg = NULL;

			splx(s);
		}
	} else
		pcmcia_chip_intr_disestablish(pf->sc->pct, pf->sc->pch, ih);
}

const char *
pcmcia_intr_string(struct pcmcia_function *pf, void *ih)
{
	return pcmcia_chip_intr_string(pf->sc->pct, pf->sc->pch, ih);
}

int
pcmcia_card_intr(void *arg)
{
	struct pcmcia_softc *sc = arg;
	struct pcmcia_function *pf;
	int reg, ret, ret2;

	ret = 0;

	for (pf = SIMPLEQ_FIRST(&sc->card.pf_head); pf != NULL;
	    pf = SIMPLEQ_NEXT(pf, pf_list)) {
#ifdef PCMCIADEBUG
		printf("%s: intr flags=%x fct=%d cor=%02x csr=%02x pin=%02x",
		       sc->dev.dv_xname, pf->pf_flags, pf->number,
		       pcmcia_ccr_read(pf, PCMCIA_CCR_OPTION),
		       pcmcia_ccr_read(pf, PCMCIA_CCR_STATUS),
		       pcmcia_ccr_read(pf, PCMCIA_CCR_PIN));
#endif
		if (pf->ih_fct != NULL &&
		    (pf->ccr_mask & (1 << (PCMCIA_CCR_STATUS / 2)))) {
			reg = pcmcia_ccr_read(pf, PCMCIA_CCR_STATUS);
			if (reg & PCMCIA_CCR_STATUS_INTR) {
				ret2 = (*pf->ih_fct)(pf->ih_arg);
				if (ret2 != 0 && ret == 0)
					ret = ret2;
				reg = pcmcia_ccr_read(pf, PCMCIA_CCR_STATUS);
#ifdef PCMCIADEBUG
				printf("; csr %02x->%02x",
				    reg, reg & ~PCMCIA_CCR_STATUS_INTR);
#endif
				pcmcia_ccr_write(pf, PCMCIA_CCR_STATUS,
				    reg & ~PCMCIA_CCR_STATUS_INTR);
			}
		}
#ifdef PCMCIADEBUG
		printf("\n");
#endif
	}

	return (ret);
}
