/* $OpenBSD: exdog.c,v 1.8 2021/10/24 17:52:27 mpi Exp $ */
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

#include <machine/bus.h>
#include <machine/fdt.h>

#include <armv7/armv7/armv7_machdep.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/fdt.h>

/* registers */
#define WTCON		0x00
#define WTDAT		0x04
#define WTCNT		0x08
#define WTCLRINT	0x0C

/* bits and bytes */
#define WTCON_RESET		(1 << 0)
#define WTCON_INT		(1 << 2)
#define WTCON_CLKSEL_16		(0x0 << 3)
#define WTCON_CLKSEL_32		(0x1 << 3)
#define WTCON_CLKSEL_64		(0x2 << 3)
#define WTCON_CLKSEL_128	(0x3 << 3)
#define WTCON_EN		(1 << 5)
#define WTCON_PRESCALER(x)	(((x) & 0xff) << 8)

struct exdog_softc {
	struct device		sc_dev;
	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh;
};

struct exdog_softc *exdog_sc;

int exdog_match(struct device *parent, void *v, void *aux);
void exdog_attach(struct device *parent, struct device *self, void *args);
void exdog_stop(void);
void exdog_reset(void);

const struct cfattach	exdog_ca = {
	sizeof (struct exdog_softc), exdog_match, exdog_attach
};

struct cfdriver exdog_cd = {
	NULL, "exdog", DV_DULL
};

int
exdog_match(struct device *parent, void *cfdata, void *aux)
{
	struct fdt_attach_args *fa = aux;

	return (OF_is_compatible(fa->fa_node, "samsung,exynos5250-wdt") ||
	    OF_is_compatible(fa->fa_node, "samsung,exynos5420-wdt"));
}

void
exdog_attach(struct device *parent, struct device *self, void *aux)
{
	struct exdog_softc *sc = (struct exdog_softc *)self;
	struct fdt_attach_args *fa = aux;

	sc->sc_iot = fa->fa_iot;

	if (bus_space_map(sc->sc_iot, fa->fa_reg[0].addr,
	    fa->fa_reg[0].size, 0, &sc->sc_ioh))
		panic("%s: bus_space_map failed!", __func__);

	printf("\n");

	exdog_sc = sc;
	if (cpuresetfn == NULL)
		cpuresetfn = exdog_reset;
}

void
exdog_stop(void)
{
	uint32_t wtcon;

	if (exdog_sc == NULL)
		return;

	wtcon = bus_space_read_4(exdog_sc->sc_iot, exdog_sc->sc_ioh, WTCON);

	wtcon &= ~(WTCON_EN | WTCON_INT | WTCON_RESET);

	bus_space_write_4(exdog_sc->sc_iot, exdog_sc->sc_ioh, WTCON, wtcon);
}

void
exdog_reset(void)
{
	uint32_t wtcon;

	if (exdog_sc == NULL)
		return;

	/* disable watchdog */
	exdog_stop();

	wtcon = bus_space_read_4(exdog_sc->sc_iot, exdog_sc->sc_ioh, WTCON);

	wtcon |= WTCON_EN | WTCON_CLKSEL_128;
	wtcon &= ~WTCON_INT;
	wtcon |= WTCON_RESET;
	wtcon |= WTCON_PRESCALER(0xff);

	/* set timeout to 1 */
	bus_space_write_4(exdog_sc->sc_iot, exdog_sc->sc_ioh, WTDAT, 1);
	bus_space_write_4(exdog_sc->sc_iot, exdog_sc->sc_ioh, WTCNT, 1);

	/* kick off the watchdog */
	bus_space_write_4(exdog_sc->sc_iot, exdog_sc->sc_ioh, WTCON, wtcon);

	delay(100000);
}
