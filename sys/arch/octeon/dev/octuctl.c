/*	$OpenBSD: octuctl.c,v 1.2 2017/07/25 11:01:28 jmatthew Exp $ */

/*
 * Copyright (c) 2015 Jonathan Matthew  <jmatthew@openbsd.org>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
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
#include <machine/octeonreg.h>
#include <machine/octeonvar.h>
#include <machine/fdt.h>

#include <octeon/dev/iobusvar.h>
#include <octeon/dev/octuctlreg.h>
#include <octeon/dev/octuctlvar.h>

struct octuctl_softc {
	struct device		sc_dev;
	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh;
};

int	octuctl_match(struct device *, void *, void *);
void	octuctl_attach(struct device *, struct device *, void *);

const struct cfattach octuctl_ca = {
	sizeof(struct octuctl_softc), octuctl_match, octuctl_attach,
};

struct cfdriver octuctl_cd = {
	NULL, "octuctl", DV_DULL
};

uint8_t octuctl_read_1(bus_space_tag_t, bus_space_handle_t, bus_size_t);
uint16_t octuctl_read_2(bus_space_tag_t, bus_space_handle_t, bus_size_t);
uint32_t octuctl_read_4(bus_space_tag_t, bus_space_handle_t, bus_size_t);
void octuctl_write_1(bus_space_tag_t, bus_space_handle_t, bus_size_t, uint8_t);
void octuctl_write_2(bus_space_tag_t, bus_space_handle_t, bus_size_t, uint16_t);
void octuctl_write_4(bus_space_tag_t, bus_space_handle_t, bus_size_t, uint32_t);

bus_space_t octuctl_tag = {
	.bus_base = PHYS_TO_XKPHYS(0, CCA_NC),
	.bus_private = NULL,
	._space_read_1 = octuctl_read_1,
	._space_write_1 = octuctl_write_1,
	._space_read_2 = octuctl_read_2,
	._space_write_2 = octuctl_write_2,
	._space_read_4 = octuctl_read_4,
	._space_write_4 = octuctl_write_4,
	._space_map = iobus_space_map,
	._space_unmap = iobus_space_unmap,
	._space_subregion = generic_space_region,
	._space_vaddr = generic_space_vaddr
};

uint8_t
octuctl_read_1(bus_space_tag_t t, bus_space_handle_t h, bus_size_t o)
{
	return *(volatile uint8_t *)(h + (o^3));
}

uint16_t
octuctl_read_2(bus_space_tag_t t, bus_space_handle_t h, bus_size_t o)
{
	return *(volatile uint16_t *)(h + (o^2));
}

uint32_t
octuctl_read_4(bus_space_tag_t t, bus_space_handle_t h, bus_size_t o)
{
	return *(volatile uint32_t *)(h + o);
}

void
octuctl_write_1(bus_space_tag_t t, bus_space_handle_t h, bus_size_t o, uint8_t v)
{
	*(volatile uint8_t *)(h + (o^3)) = v;
}

void
octuctl_write_2(bus_space_tag_t t, bus_space_handle_t h, bus_size_t o, uint16_t v)
{
	*(volatile uint16_t *)(h + (o^2)) = v;
}

void
octuctl_write_4(bus_space_tag_t t, bus_space_handle_t h, bus_size_t o, uint32_t v)
{
	*(volatile uint32_t *)(h + o) = v;
}

int
octuctl_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return OF_is_compatible(faa->fa_node, "cavium,octeon-6335-uctl");
}

int
octuctlprint(void *aux, const char *parentname)
{
	return (QUIET);
}

void
octuctl_clock_setup(struct octuctl_softc *sc, uint64_t ctl)
{
	int div;
	int lastdiv;
	int validdiv[] = { 1, 2, 3, 4, 6, 8, 12, INT_MAX };
	int i;

	div = octeon_ioclock_speed() / UCTL_CLK_TARGET_FREQ;

	/* start usb controller reset */
	ctl |= UCTL_CLK_RST_CTL_P_POR;
	ctl &= ~(UCTL_CLK_RST_CTL_HRST |
	    UCTL_CLK_RST_CTL_P_PRST |
	    UCTL_CLK_RST_CTL_O_CLKDIV_EN |
	    UCTL_CLK_RST_CTL_H_CLKDIV_EN |
	    UCTL_CLK_RST_CTL_H_CLKDIV_RST |
	    UCTL_CLK_RST_CTL_O_CLKDIV_RST);
	bus_space_write_8(sc->sc_iot, sc->sc_ioh, UCTL_CLK_RST_CTL, ctl);

	/* set up for 12mhz crystal */
	ctl &= ~((3 << UCTL_CLK_RST_CTL_P_REFCLK_DIV_SHIFT) |
	    (3 << UCTL_CLK_RST_CTL_P_REFCLK_SEL_SHIFT));
	bus_space_write_8(sc->sc_iot, sc->sc_ioh, UCTL_CLK_RST_CTL, ctl);

	/* set clock divider */
	lastdiv = 1;
	for (i = 0; i < nitems(validdiv); i++) {
		if (div < validdiv[i]) {
			div = lastdiv;
			break;
		}
		lastdiv = validdiv[i];
	}

	ctl &= ~(0xf << UCTL_CLK_RST_CTL_H_DIV_SHIFT);
	ctl |= (div << UCTL_CLK_RST_CTL_H_DIV_SHIFT);
	bus_space_write_8(sc->sc_iot, sc->sc_ioh, UCTL_CLK_RST_CTL, ctl);

	/* turn hclk on */
	ctl = bus_space_read_8(sc->sc_iot, sc->sc_ioh,
	    UCTL_CLK_RST_CTL);
	ctl |= UCTL_CLK_RST_CTL_H_CLKDIV_EN;
	bus_space_write_8(sc->sc_iot, sc->sc_ioh, UCTL_CLK_RST_CTL, ctl);
	ctl |= UCTL_CLK_RST_CTL_H_CLKDIV_RST;
	bus_space_write_8(sc->sc_iot, sc->sc_ioh, UCTL_CLK_RST_CTL, ctl);

	delay(1);

	/* power-on-reset finished */
	ctl &= ~UCTL_CLK_RST_CTL_P_POR;
	bus_space_write_8(sc->sc_iot, sc->sc_ioh, UCTL_CLK_RST_CTL, ctl);
	
	delay(1000);

	/* set up ohci clocks */
	ctl |= UCTL_CLK_RST_CTL_O_CLKDIV_RST;
	bus_space_write_8(sc->sc_iot, sc->sc_ioh, UCTL_CLK_RST_CTL, ctl);
	ctl |= UCTL_CLK_RST_CTL_O_CLKDIV_EN;
	bus_space_write_8(sc->sc_iot, sc->sc_ioh, UCTL_CLK_RST_CTL, ctl);
	
	delay(1);

	/* phy reset */
	ctl |= UCTL_CLK_RST_CTL_P_PRST;
	bus_space_write_8(sc->sc_iot, sc->sc_ioh, UCTL_CLK_RST_CTL, ctl);

	delay(1);

	/* clear host reset */
	ctl |= UCTL_CLK_RST_CTL_HRST;
	bus_space_write_8(sc->sc_iot, sc->sc_ioh, UCTL_CLK_RST_CTL, ctl);
}

void
octuctl_attach(struct device *parent, struct device *self, void *aux)
{
	struct fdt_attach_args *faa = aux;
	struct octuctl_softc *sc = (struct octuctl_softc *)self;
	struct octuctl_attach_args uaa;
	uint64_t port_ctl;
	uint64_t ctl;
	uint64_t preg;
	uint64_t txvref;
	uint32_t reg[4];
	int port;
	int node;
	int rc;

	if (faa->fa_nreg != 1) {
		printf(": expected one IO space, got %d\n", faa->fa_nreg);
		return;
	}

	sc->sc_iot = faa->fa_iot;
	if (bus_space_map(sc->sc_iot, faa->fa_reg[0].addr, faa->fa_reg[0].size,
	    0, &sc->sc_ioh)) {
		printf(": could not map IO space\n");
		return;
	}

	rc = OF_getpropint(faa->fa_node, "#address-cells", 0);
	if (rc != 2) {
		printf(": expected #address-cells 2, got %d\n", rc);
		return;
	}
	rc = OF_getpropint(faa->fa_node, "#size-cells", 0);
	if (rc != 2) {
		printf(": expected #size-cells 2, got %d\n", rc);
		return;
	}

	/* do clock setup if not already done */
	bus_space_write_8(sc->sc_iot, sc->sc_ioh, UCTL_IF_ENA,
	    UCTL_IF_ENA_EN);
	ctl = bus_space_read_8(sc->sc_iot, sc->sc_ioh, UCTL_CLK_RST_CTL);
	if ((ctl & UCTL_CLK_RST_CTL_HRST) == 0)
		octuctl_clock_setup(sc, ctl);

	/* port phy settings */
	for (port = 0; port < 2; port++) {
		preg = UCTL_UPHY_PORTX_STATUS + (port * 8);
		port_ctl = bus_space_read_8(sc->sc_iot, sc->sc_ioh, preg);
		txvref = 0xf;
		port_ctl |= (UCTL_UPHY_PORTX_STATUS_TXPREEMPHTUNE |
		    UCTL_UPHY_PORTX_STATUS_TXRISETUNE |
		    (txvref << UCTL_UPHY_PORTX_STATUS_TXVREF_SHIFT));
		bus_space_write_8(sc->sc_iot, sc->sc_ioh, preg, port_ctl);
	}

	printf("\n");

	uaa.aa_octuctl_bust = sc->sc_iot;
	uaa.aa_bust = &octuctl_tag;
	uaa.aa_dmat = faa->fa_dmat;
	uaa.aa_ioh = sc->sc_ioh;

	for (node = OF_child(faa->fa_node); node != 0; node = OF_peer(node)) {
		if (OF_getproplen(node, "reg") != sizeof(reg))
			continue;

		OF_getpropintarray(node, "reg", reg, sizeof(reg));
		uaa.aa_reg.addr = (((uint64_t)reg[0]) << 32) | reg[1];
		uaa.aa_reg.size = (((uint64_t)reg[2]) << 32) | reg[3];
		uaa.aa_node = node;
		config_found(self, &uaa, octuctlprint);
	}
}
