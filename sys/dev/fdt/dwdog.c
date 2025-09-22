/*	$OpenBSD: dwdog.c,v 1.3 2021/10/24 17:52:26 mpi Exp $	*/
/*
 * Copyright (c) 2017 Mark Kettenis <kettenis@openbsd.org>
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

/* Registers */
#define WDT_CR			0x0000
#define  WDT_CR_WDT_EN		(1 << 0)
#define  WDT_CR_RESP_MODE	(1 << 1)
#define WDT_TORR		0x0004
#define WDT_CCVR		0x0008
#define WDT_CRR			0x000c
#define  WDT_CRR_KICK		0x76
#define WDT_STAT		0x0010
#define WDT_EOI			0x0014

#define HREAD4(sc, reg)							\
	(bus_space_read_4((sc)->sc_iot, (sc)->sc_ioh, (reg)))
#define HWRITE4(sc, reg, val)						\
	bus_space_write_4((sc)->sc_iot, (sc)->sc_ioh, (reg), (val))
#define HSET4(sc, reg, bits)						\
	HWRITE4((sc), (reg), HREAD4((sc), (reg)) | (bits))
#define HCLR4(sc, reg, bits)						\
	HWRITE4((sc), (reg), HREAD4((sc), (reg)) & ~(bits))

struct dwdog_softc {
	struct device		sc_dev;
	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh;
};

int	dwdog_match(struct device *, void *, void *);
void	dwdog_attach(struct device *, struct device *, void *);

const struct cfattach dwdog_ca = {
	sizeof(struct dwdog_softc), dwdog_match, dwdog_attach
};

struct cfdriver dwdog_cd = {
	NULL, "dwdog", DV_DULL
};

void	dwdog_reset(void);

int
dwdog_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return OF_is_compatible(faa->fa_node, "snps,dw-wdt");
}

void
dwdog_attach(struct device *parent, struct device *self, void *aux)
{
	struct dwdog_softc *sc = (struct dwdog_softc *)self;
	struct fdt_attach_args *faa = aux;

	if (faa->fa_nreg < 1) {
		printf(": no registers\n");
		return;
	}

	sc->sc_iot = faa->fa_iot;

	if (bus_space_map(sc->sc_iot, faa->fa_reg[0].addr,
	    faa->fa_reg[0].size, 0, &sc->sc_ioh)) {
		printf(": can't map registers\n");
	}

	printf("\n");

	if (cpuresetfn == NULL)
		cpuresetfn = dwdog_reset;
}

void
dwdog_reset(void)
{
	struct dwdog_softc *sc = dwdog_cd.cd_devs[0];

	/* 
	 * Generate system reset when timer expires and select
	 * smallest timeout.
	 */
	HCLR4(sc, WDT_CR, WDT_CR_RESP_MODE | WDT_CR_WDT_EN);
	HWRITE4(sc, WDT_TORR, 0);
	HSET4(sc, WDT_CR, WDT_CR_WDT_EN);

	/* Kick the dog! */
	HWRITE4(sc, WDT_CRR, WDT_CRR_KICK);

	delay(1000000);
}
