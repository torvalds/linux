/*	$OpenBSD: pgs.c,v 1.6 2022/03/13 12:33:01 mpi Exp $	*/
/*
 * Copyright (c) 2010 Jasper Lievisse Adriaanse <jasper@openbsd.org>
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

/*
 * This driver allows entering DDB by pushing the "Programmers Switch",
 * which can be found on many "Old World" and some early "New World" MacPPC.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/device.h>

#include <ddb/db_var.h>
#include <dev/ofw/openfirm.h>

#include <machine/bus.h>
#include <machine/autoconf.h>

struct pgs_softc {
	struct device	sc_dev;
	int		sc_node;
	int 		sc_intr;
};

void	pgs_attach(struct device *, struct device *, void *);
int	pgs_match(struct device *, void *, void *);
int	pgs_intr(void *);

const struct cfattach pgs_ca = {
	sizeof(struct pgs_softc), pgs_match, pgs_attach
};

struct cfdriver pgs_cd = {
	NULL, "pgs", DV_DULL
};

int
pgs_match(struct device *parent, void *arg, void *aux)
{
	struct confargs *ca = aux;
	char type[32];

	if (strcmp(ca->ca_name, "programmer-switch") != 0)
		return 0;

	OF_getprop(ca->ca_node, "device_type", type, sizeof(type));
	if (strcmp(type, "programmer-switch") != 0)
		return 0;

	return 1;
}

void
pgs_attach(struct device *parent, struct device *self, void *aux)
{
	struct pgs_softc *sc = (struct pgs_softc *)self;
	struct confargs *ca = aux;
	int intr[2];

	sc->sc_node = ca->ca_node;

	OF_getprop(sc->sc_node, "interrupts", intr, sizeof(intr));
	sc->sc_intr = intr[0];

	printf(": irq %d\n", sc->sc_intr);

	mac_intr_establish(parent, sc->sc_intr, IST_LEVEL,
	    IPL_HIGH, pgs_intr, sc, sc->sc_dev.dv_xname);
}

int
pgs_intr(void *v)
{
#ifdef DDB
	if (db_console)
		db_enter();
#else
	printf("programmer-switch pressed, debugger not available.\n");
#endif

	return 1;
}
