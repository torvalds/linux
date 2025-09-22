/*	$OpenBSD: gscbus.c,v 1.32 2025/06/28 13:24:21 miod Exp $	*/

/*
 * Copyright (c) 1998 Michael Shalayeff
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

/* #define GSCDEBUG */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/reboot.h>

#include <machine/iomod.h>
#include <machine/autoconf.h>
#include <machine/cpufunc.h>
#include <hppa/dev/viper.h>

#include <hppa/gsc/gscbusvar.h>

int	gscmatch(struct device *, void *, void *);
void	gscattach(struct device *, struct device *, void *);

const struct cfattach gsc_ca = {
	sizeof(struct gsc_softc), gscmatch, gscattach
};

struct cfdriver gsc_cd = {
	NULL, "gsc", DV_DULL
};

int
gscmatch(struct device *parent, void *cfdata, void *aux)
{
	struct confargs *ca = aux;

	return !strcmp(ca->ca_name, "gsc");
}

void
gscattach(struct device *parent, struct device *self, void *aux)
{
	struct gsc_softc *sc = (struct gsc_softc *)self;
	struct gsc_attach_args *ga = aux;
	int s, irqbit;

	sc->sc_iot = ga->ga_iot;
	sc->sc_ic = ga->ga_ic;

	irqbit = cpu_intr_findirq();
	if (irqbit >= 0)
		printf(" irq %d", irqbit);

#ifdef USELEDS
	if (machine_ledaddr)
		printf(": %sleds", machine_ledword? "word" : "");
#endif
	printf ("\n");

	if (irqbit < 0)
		sc->sc_ih = NULL;
	else
		sc->sc_ih = cpu_intr_establish(IPL_NESTED, irqbit,
		    gsc_intr, (void *)sc->sc_ic, sc->sc_dev.dv_xname);
	if (sc->sc_ih == NULL) {
		printf("%s: can't establish interrupt\n", sc->sc_dev.dv_xname);
		return;
	}

	/*
	 * On ASP, the IAR register is not writable; we need to go through
	 * the memory controller to achieve proper routing.
	 */
	s = splhigh();
	if (ga->ga_parent == gsc_asp)
		viper_setintrwnd(1 << irqbit);
	else
		sc->sc_ic->iar = cpu_gethpa(0) | (31 - irqbit);
	splx(s);

	pdc_scanbus(self, &ga->ga_ca, MAXMODBUS, 0, 0);
}

int
gscprint(void *aux, const char *pnp)
{
	struct gsc_attach_args *ga = aux;

	if (pnp)
		printf("%s at %s", ga->ga_name, pnp);
	return (UNCONF);
}

void *
gsc_intr_establish(struct gsc_softc *sc, int irq, int pri,
    int (*handler)(void *v), void *arg, const char *name)
{
	void *iv;

	if ((iv = cpu_intr_map(sc->sc_ih, pri, irq, handler, arg, name)))
		sc->sc_ic->imr |= (1 << irq);
	else {
#ifdef GSCDEBUG
		printf("%s: attaching irq %d, already occupied\n",
		       sc->sc_dev.dv_xname, irq);
#endif
	}

	return (iv);
}

void
gsc_intr_disestablish(struct gsc_softc *sc, void *v)
{
#if notyet
	sc->sc_ic->imr &= ~(1 << irq);

	cpu_intr_unmap(sc->sc_ih, v);
#endif
}
