/*	$OpenBSD: xics.c,v 1.4 2022/04/06 18:59:27 naddy Exp $	*/
/*
 * Copyright (c) 2020 Mark Kettenis <kettenis@openbsd.org>
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
#include <sys/device.h>
#include <sys/evcount.h>
#include <sys/malloc.h>
#include <sys/queue.h>

#include <machine/bus.h>
#include <machine/fdt.h>
#include <machine/opal.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/fdt.h>

struct xics_softc {
	struct device		sc_dev;

	struct interrupt_controller sc_ic;
};

int	xics_match(struct device *, void *, void *);
void	xics_attach(struct device *, struct device *, void *);

const struct cfattach xics_ca = {
	sizeof (struct xics_softc), xics_match, xics_attach
};

struct cfdriver xics_cd = {
	NULL, "xics", DV_DULL
};

void	*xics_intr_establish(void *, int *, int,
	    struct cpu_info *, int (*)(void *), void *, char *);
void	xics_intr_send_ipi(void *);

int
xics_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return OF_is_compatible(faa->fa_node, "ibm,opal-xive-vc");
}

void
xics_attach(struct device *parent, struct device *self, void *aux)
{
	struct xics_softc *sc = (struct xics_softc *)self;
	struct fdt_attach_args *faa = aux;

	printf("\n");

	sc->sc_ic.ic_node = faa->fa_node;
	sc->sc_ic.ic_cookie = self;
	sc->sc_ic.ic_establish = xics_intr_establish;
	sc->sc_ic.ic_send_ipi = xics_intr_send_ipi;
	interrupt_controller_register(&sc->sc_ic);
}

void *
xics_intr_establish(void *cookie, int *cell, int level,
    struct cpu_info *ci, int (*func)(void *), void *arg, char *name)
{
	uint32_t girq = cell[0];
	int type = cell[1];

	return _intr_establish(girq, type, level, ci, func, arg, name);
}

void
xics_intr_send_ipi(void *cookie)
{
	return _intr_send_ipi(cookie);
}
