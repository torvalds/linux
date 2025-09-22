/*	$OpenBSD: bcm2835_aux.c,v 1.7 2021/10/24 17:52:26 mpi Exp $	*/
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

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_clock.h>
#include <dev/ofw/fdt.h>

/*
 * This auxiliary device handles interrupts and clocks for one UART
 * and two SPI controllers.  For now we only support the UART, so we
 * simply register its interrupt handler directly with our parent
 * interrupt controller.
 */
#define BCMAUX_UART	0
#define BCMAUX_SPI0	1
#define BCMAUX_SPI1	2

struct bcmaux_softc {
	struct device		sc_dev;
	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh;

	struct clock_device	sc_cd;
	struct interrupt_controller sc_ic;
};

int	bcmaux_match(struct device *, void *, void *);
void	bcmaux_attach(struct device *, struct device *, void *);

const struct cfattach bcmaux_ca = {
	sizeof(struct bcmaux_softc), bcmaux_match, bcmaux_attach
};

struct cfdriver bcmaux_cd = {
	NULL, "bcmaux", DV_DULL
};

uint32_t bcm_aux_get_frequency(void *, uint32_t *);
void	*bcm_aux_intr_establish_fdt(void *, int *, int, struct cpu_info *,
    int (*)(void *), void *, char *);

int
bcmaux_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return OF_is_compatible(faa->fa_node, "brcm,bcm2835-aux");
}

void
bcmaux_attach(struct device *parent, struct device *self, void *aux)
{
	struct bcmaux_softc *sc = (struct bcmaux_softc *)self;
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

	sc->sc_cd.cd_node = faa->fa_node;
	sc->sc_cd.cd_cookie = sc;
	sc->sc_cd.cd_get_frequency = bcm_aux_get_frequency;
	clock_register(&sc->sc_cd);

	sc->sc_ic.ic_node = faa->fa_node;
	sc->sc_ic.ic_cookie = &sc->sc_ic;
	sc->sc_ic.ic_establish = bcm_aux_intr_establish_fdt;
	sc->sc_ic.ic_disestablish = fdt_intr_disestablish;
	sc->sc_ic.ic_barrier = intr_barrier;
	fdt_intr_register(&sc->sc_ic);
}

uint32_t
bcm_aux_get_frequency(void *cookie, uint32_t *cells)
{
	uint32_t idx = cells[0];

	/* Only support the UART for now. */
	if (idx == BCMAUX_UART)
		return 500000000;

	printf("%s: 0x%08x\n", __func__, idx);
	return 0;
}

void *
bcm_aux_intr_establish_fdt(void *cookie, int *cells, int level,
    struct cpu_info *ci, int (*func)(void *), void *arg, char *name)
{
	struct interrupt_controller *ic = cookie;
	uint32_t idx = cells[0];

	/* Only support the UART for now. */
	if (idx != BCMAUX_UART)
		return NULL;

	return fdt_intr_establish_cpu(ic->ic_node, level, ci, func, arg, name);
}
