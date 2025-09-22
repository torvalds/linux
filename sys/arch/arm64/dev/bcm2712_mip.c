/*	$OpenBSD: bcm2712_mip.c,v 1.1 2025/08/21 14:53:07 kettenis Exp $	*/
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
#include <sys/malloc.h>

#include <machine/intr.h>
#include <machine/bus.h>
#include <machine/fdt.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_gpio.h>
#include <dev/ofw/fdt.h>

/* Registers. */
#define MIP_INT_RAISE		0x00
#define MIP_INT_CLEAR		0x10
#define MIP_INT_CFGL_HOST	0x20
#define MIP_INT_CFGH_HOST	0x30
#define MIP_INT_MASKL_HOST	0x40
#define MIP_INT_MASKH_HOST	0x50
#define MIP_INT_MASKL_VPU	0x60
#define MIP_INT_MASKH_VPU	0x70
#define MIP_INT_STATUSL_HOST	0x80
#define MIP_INT_STATUSH_HOST	0x90
#define MIP_INT_STATUSL_VPU	0xa0
#define MIP_INT_STATUSH_VPU	0xb0

#define HREAD4(sc, reg)							\
	(bus_space_read_4((sc)->sc_iot, (sc)->sc_ioh, (reg)))
#define HWRITE4(sc, reg, val)						\
	bus_space_write_4((sc)->sc_iot, (sc)->sc_ioh, (reg), (val))
#define HSET4(sc, reg, bits)						\
	HWRITE4((sc), (reg), HREAD4((sc), (reg)) | (bits))
#define HCLR4(sc, reg, bits)						\
	HWRITE4((sc), (reg), HREAD4((sc), (reg)) & ~(bits))

struct bcmmip_softc {
	struct device		sc_dev;
	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh;

	bus_addr_t		sc_msi_addr;
	uint32_t		sc_msi_ranges[5];
	uint32_t		sc_msi_offset;
	uint64_t		sc_msi_map;

	struct interrupt_controller sc_ic;
};

int bcmmip_match(struct device *, void *, void *);
void bcmmip_attach(struct device *, struct device *, void *);

const struct cfattach	bcmmip_ca = {
	sizeof (struct bcmmip_softc), bcmmip_match, bcmmip_attach
};

struct cfdriver bcmmip_cd = {
	NULL, "bcmmip", DV_DULL
};

void	*bcmmip_intr_establish_msi(void *, uint64_t *, uint64_t *,
	    int, struct cpu_info *, int (*)(void *), void *, char *);

int
bcmmip_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return OF_is_compatible(faa->fa_node, "brcm,bcm2712-mip");
}

void
bcmmip_attach(struct device *parent, struct device *self, void *aux)
{
	struct bcmmip_softc *sc = (struct bcmmip_softc *)self;
	struct fdt_attach_args *faa = aux;

	if (OF_getpropintarray(faa->fa_node, "msi-ranges", sc->sc_msi_ranges,
	    sizeof(sc->sc_msi_ranges)) != sizeof(sc->sc_msi_ranges)) {
		printf(": incorrect msi-ranges\n");
		return;
	}

	if (faa->fa_nreg < 2) {
		printf(": no registers\n");
		return;
	}

	sc->sc_iot = faa->fa_iot;
	if (bus_space_map(sc->sc_iot, faa->fa_reg[0].addr,
	    faa->fa_reg[0].size, 0, &sc->sc_ioh)) {
		printf(": can't map registers\n");
		return;
	}

	sc->sc_msi_addr = faa->fa_reg[1].addr;
	sc->sc_msi_offset = OF_getpropint(faa->fa_node, "brcm,msi-offset", 0);

	printf("\n");

	HWRITE4(sc, MIP_INT_MASKL_VPU, 0xffffffff);
	HWRITE4(sc, MIP_INT_MASKH_VPU, 0xffffffff);
	HWRITE4(sc, MIP_INT_CFGL_HOST, 0xffffffff);
	HWRITE4(sc, MIP_INT_CFGH_HOST, 0xffffffff);
	HWRITE4(sc, MIP_INT_MASKL_HOST, 0);
	HWRITE4(sc, MIP_INT_MASKH_HOST, 0);

	sc->sc_ic.ic_node = faa->fa_node;
	sc->sc_ic.ic_cookie = sc;
	sc->sc_ic.ic_establish_msi = bcmmip_intr_establish_msi;
	sc->sc_ic.ic_barrier = intr_barrier;
	fdt_intr_register(&sc->sc_ic);
}

void *
bcmmip_intr_establish_msi(void *cookie, uint64_t *addr, uint64_t *data,
    int level, struct cpu_info *ci, int (*func)(void *), void *arg, char *name)
{
	struct bcmmip_softc *sc = cookie;
	struct interrupt_controller *ic;
	struct machine_intr_handle *ih;
	uint32_t cells[3];
	u_int msi;

	for (msi = 0; msi < sc->sc_msi_ranges[4]; msi++) {
		if ((sc->sc_msi_map & (1U << msi)) == 0) {
			sc->sc_msi_map |= (1U << msi);
			break;
		}
	}
	if (msi == sc->sc_msi_ranges[4])
		return NULL;
	msi += sc->sc_msi_offset;

	*addr = sc->sc_msi_addr;
	*data = msi;

	extern LIST_HEAD(, interrupt_controller) interrupt_controllers;
	LIST_FOREACH(ic, &interrupt_controllers, ic_list) {
		if (ic->ic_phandle == sc->sc_msi_ranges[0])
			break;
	}
	if (ic == NULL)
		return NULL;

	cells[0] = sc->sc_msi_ranges[1];
	cells[1] = sc->sc_msi_ranges[2] + msi;
	cells[2] = sc->sc_msi_ranges[3];

	cookie = ic->ic_establish(ic->ic_cookie, cells, level, ci,
	    func, arg, name);
	if (cookie == NULL)
		return NULL;

	ih = malloc(sizeof(*ih), M_DEVBUF, M_WAITOK);
	ih->ih_ic = ic;
	ih->ih_ih = cookie;

	return ih;
}
