/*	$OpenBSD: bcmstbrescal.c,v 1.1 2025/08/20 13:07:39 kettenis Exp $	*/
/*
 * Copyright (c) 2025 Mark Kettenis <kettenis@openbsd.org>
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

#include <machine/intr.h>
#include <machine/bus.h>
#include <machine/fdt.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_clock.h>
#include <dev/ofw/fdt.h>

/* Registers. */
#define RESCAL_START		0x00
#define  RESCAL_START_BIT	(1 << 0)
#define RESCAL_CTRL		0x04
#define RESCAL_STATUS		0x08
#define  RESCAL_STATUS_BIT	(1 << 0)

#define HREAD4(sc, reg)							\
	(bus_space_read_4((sc)->sc_iot, (sc)->sc_ioh, (reg)))
#define HWRITE4(sc, reg, val)						\
	bus_space_write_4((sc)->sc_iot, (sc)->sc_ioh, (reg), (val))
#define HSET4(sc, reg, bits)						\
	HWRITE4((sc), (reg), HREAD4((sc), (reg)) | (bits))
#define HCLR4(sc, reg, bits)						\
	HWRITE4((sc), (reg), HREAD4((sc), (reg)) & ~(bits))

struct bcmstbrescal_softc {
	struct device		sc_dev;
	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh;

	struct reset_device	sc_rd;
};

int	bcmstbrescal_match(struct device *, void *, void *);
void	bcmstbrescal_attach(struct device *, struct device *, void *);

const struct cfattach	bcmstbrescal_ca = {
	sizeof (struct bcmstbrescal_softc),
	bcmstbrescal_match, bcmstbrescal_attach
};

struct cfdriver bcmstbrescal_cd = {
	NULL, "bcmstbrescal", DV_DULL
};

void	bcmstbrescal_reset(void *, uint32_t *, int);

int
bcmstbrescal_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return OF_is_compatible(faa->fa_node, "brcm,bcm7216-pcie-sata-rescal");
}

void
bcmstbrescal_attach(struct device *parent, struct device *self, void *aux)
{
	struct bcmstbrescal_softc *sc = (struct bcmstbrescal_softc *)self;
	struct fdt_attach_args *faa = aux;

	if (faa->fa_nreg < 1) {
		printf(": no registers\n");
		return;
	}

	sc->sc_iot = faa->fa_iot;
	if (bus_space_map(sc->sc_iot, faa->fa_reg[0].addr,
	    faa->fa_reg[0].size, 0, &sc->sc_ioh)) {
		printf(": can't map registers\n");
		return;
	}

	printf("\n");

	sc->sc_rd.rd_node = faa->fa_node;
	sc->sc_rd.rd_cookie = sc;
	sc->sc_rd.rd_reset = bcmstbrescal_reset;
	reset_register(&sc->sc_rd);
}

void
bcmstbrescal_reset(void *cookie, uint32_t *cells, int assert)
{
	struct bcmstbrescal_softc *sc = cookie;
	uint32_t status;
	int timo;

	/*
	 * The reset is self-deasserting so only the assert operation
	 * is implemented.
	 */
	if (assert) {
		HSET4(sc, RESCAL_START, RESCAL_START_BIT);

		for (timo = 10; timo > 0; timo--) {
			status = HREAD4(sc, RESCAL_STATUS);
			if (status & RESCAL_STATUS_BIT)
				break;
			delay(100);
		}

		if (timo == 0) {
			printf("%s: timeout\n", sc->sc_dev.dv_xname);
			return;
		}

		HCLR4(sc, RESCAL_START, RESCAL_START_BIT);
	}
}
