/*	$OpenBSD: sxirintc.c,v 1.1 2022/07/14 19:06:29 kettenis Exp $	*/
/*
 * Copyright (c) 2022 Mark Kettenis <kettenis@openbsd.org>
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

#include <dev/ofw/openfirm.h>
#include <dev/ofw/fdt.h>

#define RINTC_IRQ_PENDING	0x10
#define RINTC_IRQ_ENABLE	0x40
#define  RINTC_IRQ_ENABLE_NMI	(1 << 0)

#define HREAD4(sc, reg)							\
	(bus_space_read_4((sc)->sc_iot, (sc)->sc_ioh, (reg)))
#define HWRITE4(sc, reg, val)						\
	bus_space_write_4((sc)->sc_iot, (sc)->sc_ioh, (reg), (val))
#define HSET4(sc, reg, bits)						\
	HWRITE4((sc), (reg), HREAD4((sc), (reg)) | (bits))
#define HCLR4(sc, reg, bits)						\
	HWRITE4((sc), (reg), HREAD4((sc), (reg)) & ~(bits))

struct sxirintc_softc {
	struct device		sc_dev;
	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh;
};

int	sxirintc_match(struct device *, void *, void *);
void	sxirintc_attach(struct device *, struct device *, void *);
int	sxirintc_activate(struct device *, int);

const struct cfattach sxirintc_ca = {
	sizeof(struct sxirintc_softc), sxirintc_match, sxirintc_attach,
	NULL, sxirintc_activate
};

struct cfdriver sxirintc_cd = {
	NULL, "sxirintc", DV_DULL
};

int
sxirintc_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return OF_is_compatible(faa->fa_node, "allwinner,sun6i-a31-r-intc");
}

void
sxirintc_attach(struct device *parent, struct device *self, void *aux)
{
	struct sxirintc_softc *sc = (struct sxirintc_softc *)self;
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
}

int
sxirintc_activate(struct device *self, int act)
{
	struct sxirintc_softc *sc = (struct sxirintc_softc *)self;

	/*
	 * Typically the "NMI" interrupt is controlled by the PMIC.
	 * This interrupt is routed in parallel to the GIC and the
	 * ARISC coprocessor.  Enable this interrupt when we suspend
	 * such that the firmware running on the ARISC coprocessor can
	 * wake up the SoC when the PMIC triggers this interrupt.
	 */

	switch (act) {
	case DVACT_SUSPEND:
		HWRITE4(sc, RINTC_IRQ_PENDING, ~0);
		HSET4(sc, RINTC_IRQ_ENABLE, RINTC_IRQ_ENABLE_NMI);
		break;
	case DVACT_RESUME:
		HCLR4(sc, RINTC_IRQ_ENABLE, RINTC_IRQ_ENABLE_NMI);
		break;
	}

	return 0;
}
