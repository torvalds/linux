/*	$OpenBSD: sysbutton.c,v 1.7 2022/03/13 12:33:01 mpi Exp $	*/
/*
 * Copyright (c) 2007 Gordon Willem Klok <gwk@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/device.h>

#include <ddb/db_var.h>
#include <dev/ofw/openfirm.h>

#include <machine/bus.h>
#include <machine/autoconf.h>

struct sysbutton_softc {
	struct device	sc_dev;
	int		sc_node;
	int 		sc_intr;
};

int sysbutton_match(struct device *, void *, void *);
void sysbutton_attach(struct device *, struct device *, void *);
int sysbutton_intr(void *);

const struct cfattach sysbutton_ca = {
	sizeof(struct sysbutton_softc), sysbutton_match,
	sysbutton_attach
};

struct cfdriver sysbutton_cd = {
	NULL, "sysbutton", DV_DULL
};

int
sysbutton_match(struct device *parent, void *arg, void *aux)
{
	struct confargs *ca = aux;

	if (strcmp(ca->ca_name, "indicatorSwitch-gpio") == 0)
		return 1;

	return 0;
}

void
sysbutton_attach(struct device *parent, struct device *self, void *aux)
{
	struct sysbutton_softc *sc = (struct sysbutton_softc *)self;
	struct confargs *ca = aux;
	int intr[2];

	sc->sc_node = ca->ca_node;

	OF_getprop(sc->sc_node, "interrupts", intr, sizeof(intr));
	sc->sc_intr = intr[0];

	printf(": irq %d\n", sc->sc_intr);

	mac_intr_establish(parent, sc->sc_intr, IST_EDGE,
	    IPL_NONE, sysbutton_intr, sc, sc->sc_dev.dv_xname);
}

int
sysbutton_intr(void *v)
{

	/* 
	 * XXX: Holding this button causes an interrupt storm if
	 * ddb.console=0.
	 */
#ifdef DDB
	if (db_console)
		db_enter();
#endif

	return 1;
}
