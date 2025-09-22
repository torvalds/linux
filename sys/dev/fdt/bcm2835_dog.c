/*	$OpenBSD: bcm2835_dog.c,v 1.3 2021/10/24 17:52:26 mpi Exp $	*/
/*
 * Copyright (c) 2015 Patrick Wildt <patrick@blueri.se>
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
#include <sys/queue.h>
#include <sys/malloc.h>
#include <sys/device.h>
#include <sys/evcount.h>
#include <machine/bus.h>
#include <machine/fdt.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/fdt.h>

extern void (*cpuresetfn)(void);

/* registers */
#define PM_RSTC		0x1c
#define PM_RSTS		0x20
#define PM_WDOG		0x24

/* bits and bytes */
#define PM_PASSWORD		0x5a000000
#define PM_RSTC_CONFIGMASK	0x00000030
#define PM_RSTC_FULL_RESET	0x00000020
#define PM_RSTC_RESET		0x00000102
#define PM_WDOG_TIMEMASK	0x000fffff

#define HREAD4(sc, reg)							\
	(bus_space_read_4((sc)->sc_iot, (sc)->sc_ioh, (reg)))
#define HWRITE4(sc, reg, val)						\
	bus_space_write_4((sc)->sc_iot, (sc)->sc_ioh, (reg), (val))
#define HSET4(sc, reg, bits)						\
	HWRITE4((sc), (reg), HREAD4((sc), (reg)) | (bits))
#define HCLR4(sc, reg, bits)						\
	HWRITE4((sc), (reg), HREAD4((sc), (reg)) & ~(bits))

struct bcmdog_softc {
	struct device		 sc_dev;
	bus_space_tag_t		 sc_iot;
	bus_space_handle_t	 sc_ioh;
};

struct bcmdog_softc *bcmdog_sc;

int	 bcmdog_match(struct device *, void *, void *);
void	 bcmdog_attach(struct device *, struct device *, void *);
int	 bcmdog_activate(struct device *, int);
int	 bcmdog_wdog_cb(void *, int);
void	 bcmdog_wdog_reset(void);

const struct cfattach	bcmdog_ca = {
	sizeof (struct bcmdog_softc), bcmdog_match, bcmdog_attach, NULL,
	bcmdog_activate
};

struct cfdriver bcmdog_cd = {
	NULL, "bcmdog", DV_DULL
};

int
bcmdog_match(struct device *parent, void *cfdata, void *aux)
{
	struct fdt_attach_args *fa = aux;

	return OF_is_compatible(fa->fa_node, "brcm,bcm2835-pm-wdt");
}

void
bcmdog_attach(struct device *parent, struct device *self, void *aux)
{
	struct bcmdog_softc *sc = (struct bcmdog_softc *)self;
	struct fdt_attach_args *fa = aux;

	sc->sc_iot = fa->fa_iot;

	if (bus_space_map(sc->sc_iot, fa->fa_reg[0].addr,
	    fa->fa_reg[0].size, 0, &sc->sc_ioh))
		panic("%s: bus_space_map failed!", __func__);

	printf("\n");

	bcmdog_sc = sc;
	cpuresetfn = bcmdog_wdog_reset;

#ifndef SMALL_KERNEL
	wdog_register(bcmdog_wdog_cb, sc);
#endif
}

int
bcmdog_activate(struct device *self, int act)
{
	switch (act) {
	case DVACT_POWERDOWN:
#ifndef SMALL_KERNEL
		wdog_shutdown(self);
#endif
		break;
	}

	return 0;
}

void
bcmdog_wdog_set(struct bcmdog_softc *sc, uint32_t period)
{
	uint32_t rstc, wdog;

	if (period == 0) {
		HWRITE4(sc, PM_RSTC, PM_RSTC_RESET | PM_PASSWORD);
		return;
	}

	rstc = HREAD4(sc, PM_RSTC) & PM_RSTC_CONFIGMASK;
	rstc |= PM_RSTC_FULL_RESET;
	rstc |= PM_PASSWORD;

	wdog = period & PM_WDOG_TIMEMASK;
	wdog |= PM_PASSWORD;

	HWRITE4(sc, PM_WDOG, wdog);
	HWRITE4(sc, PM_RSTC, rstc);
}

int
bcmdog_wdog_cb(void *self, int period)
{
	struct bcmdog_softc *sc = self;

	bcmdog_wdog_set(sc, period << 16);
	return period;
}

void
bcmdog_wdog_reset(void)
{
	struct bcmdog_softc *sc = bcmdog_sc;

	bcmdog_wdog_set(sc, 10);
	delay(100000);
}
