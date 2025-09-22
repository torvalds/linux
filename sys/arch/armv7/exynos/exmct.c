/* $OpenBSD: exmct.c,v 1.7 2024/05/13 01:15:50 jsg Exp $ */
/*
 * Copyright (c) 2012-2013 Patrick Wildt <patrick@blueri.se>
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

#include <arm/cpufunc.h>

#include <machine/intr.h>
#include <machine/bus.h>
#include <machine/fdt.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/fdt.h>

/* registers */
#define MCT_CTRL	0x240
#define MCT_WRITE_STAT	0x24C

/* bits and bytes */
#define MCT_CTRL_START	(1 << 8)

struct exmct_softc {
	struct device		sc_dev;
	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh;
};

struct exmct_softc *exmct_sc;

int exmct_match(struct device *, void *, void *);
void exmct_attach(struct device *, struct device *, void *);

const struct cfattach	exmct_ca = {
	sizeof (struct exmct_softc), exmct_match, exmct_attach
};

struct cfdriver exmct_cd = {
	NULL, "exmct", DV_DULL
};

int
exmct_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return OF_is_compatible(faa->fa_node, "samsung,exynos4210-mct");
}

void
exmct_attach(struct device *parent, struct device *self, void *aux)
{
	struct exmct_softc *sc = (struct exmct_softc *)self;
	struct fdt_attach_args *faa = aux;
	uint32_t i, mask, reg;

	sc->sc_iot = faa->fa_iot;

	if (bus_space_map(sc->sc_iot, faa->fa_reg[0].addr,
	    faa->fa_reg[0].size, 0, &sc->sc_ioh))
		panic("%s: bus_space_map failed!", __func__);

	printf("\n");

	exmct_sc = sc;

	extern void agtimer_delay(u_int);
	arm_clock_register(NULL, agtimer_delay, NULL, NULL);

	bus_space_write_4(sc->sc_iot, sc->sc_ioh, MCT_CTRL,
	    bus_space_read_4(sc->sc_iot, sc->sc_ioh, MCT_CTRL) | MCT_CTRL_START);

	mask = (1 << 16);

	/* Wait 10 times until written value is applied */
	for (i = 0; i < 10; i++) {
		reg = bus_space_read_4(sc->sc_iot, sc->sc_ioh, MCT_WRITE_STAT);
		if (reg & mask) {
			bus_space_write_4(sc->sc_iot, sc->sc_ioh,
			    MCT_WRITE_STAT, mask);
			return;
		}
		cpufunc_nullop();
	}

	/* NOTREACHED */

	panic("%s: can't enable timer!", __func__);
}
