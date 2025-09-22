/*	$OpenBSD: octsctl.c,v 1.2 2019/01/12 13:50:52 visa Exp $	*/

/*
 * Copyright (c) 2017 Visa Hankala
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
 * Driver for OCTEON SATA controller bridge.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>

#include <machine/bus.h>
#include <machine/fdt.h>

#include <dev/ofw/fdt.h>
#include <dev/ofw/openfirm.h>

#define SCTL_SHIM_CFG			0xe8
#define   SCTL_SHIM_CFG_READ_CMD		0x0000000000001000ul
#define   SCTL_SHIM_CFG_DMA_BYTE_SWAP		0x0000000000000300ul
#define   SCTL_SHIM_CFG_DMA_BYTE_SWAP_SHIFT	8
#define   SCTL_SHIM_CFG_CSR_BYTE_SWAP		0x0000000000000003ul
#define   SCTL_SHIM_CFG_CSR_BYTE_SWAP_SHIFT	0

struct octsctl_softc {
	struct device		sc_dev;
	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh;
};

int	octsctl_match(struct device *, void *, void *);
void	octsctl_attach(struct device *, struct device *, void *);

const struct cfattach octsctl_ca = {
	sizeof(struct octsctl_softc), octsctl_match, octsctl_attach
};

struct cfdriver octsctl_cd = {
	NULL, "octsctl", DV_DULL
};

int
octsctl_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return OF_is_compatible(faa->fa_node, "cavium,octeon-7130-sata-uctl");
}

void
octsctl_attach(struct device *parent, struct device *self, void *aux)
{
	struct fdt_reg child_reg;
	struct fdt_attach_args child_faa;
	struct fdt_attach_args *faa = aux;
	struct octsctl_softc *sc = (struct octsctl_softc *)self;
	uint64_t val;
	uint32_t reg[4];
	int child;

	child = OF_child(faa->fa_node);

	/*
	 * On some machines, the bridge controller node does not have
	 * an AHCI controller node as a child.
	 */
	if (child == 0) {
		printf(": disabled\n");
		return;
	}

	if (faa->fa_nreg != 1) {
		printf(": expected one IO space, got %d\n", faa->fa_nreg);
		return;
	}

	if (OF_getpropint(faa->fa_node, "#address-cells", 0) != 2 ||
	    OF_getpropint(faa->fa_node, "#size-cells", 0) != 2) {
		printf(": invalid fdt reg cells\n");
		return;
	}
	if (OF_getproplen(child, "reg") != sizeof(reg)) {
		printf(": invalid child fdt reg\n");
		return;
	}
	OF_getpropintarray(child, "reg", reg, sizeof(reg));
	child_reg.addr = ((uint64_t)reg[0] << 32) | reg[1];
	child_reg.size = ((uint64_t)reg[2] << 32) | reg[3];

	sc->sc_iot = faa->fa_iot;
	if (bus_space_map(sc->sc_iot, faa->fa_reg[0].addr, faa->fa_reg[0].size,
	    0, &sc->sc_ioh)) {
		printf(": could not map registers\n");
		goto error;
	}

	val = bus_space_read_8(sc->sc_iot, sc->sc_ioh, SCTL_SHIM_CFG);
	val &= ~SCTL_SHIM_CFG_CSR_BYTE_SWAP;
	val &= ~SCTL_SHIM_CFG_DMA_BYTE_SWAP;
	val |= 3ul << SCTL_SHIM_CFG_CSR_BYTE_SWAP_SHIFT;
	val |= 1ul << SCTL_SHIM_CFG_DMA_BYTE_SWAP_SHIFT;
	val |= SCTL_SHIM_CFG_READ_CMD;
	bus_space_write_8(sc->sc_iot, sc->sc_ioh, SCTL_SHIM_CFG, val);
	(void)bus_space_read_8(sc->sc_iot, sc->sc_ioh, SCTL_SHIM_CFG);

	printf("\n");

	memset(&child_faa, 0, sizeof(child_faa));
	child_faa.fa_name = "";
	child_faa.fa_node = child;
	child_faa.fa_iot = faa->fa_iot;
	child_faa.fa_dmat = faa->fa_dmat;
	child_faa.fa_reg = &child_reg;
	child_faa.fa_nreg = 1;
	/* child_faa.fa_intr is not utilized. */

	config_found(self, &child_faa, NULL);

	return;

error:
	if (sc->sc_ioh != 0)
		bus_space_unmap(sc->sc_iot, sc->sc_ioh, faa->fa_reg[0].size);
}
