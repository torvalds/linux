/*	$OpenBSD: octxctl.c,v 1.5 2021/03/11 11:16:59 jsg Exp $	*/

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
 * Driver for OCTEON USB3 controller bridge.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/malloc.h>

#include <machine/fdt.h>
#include <machine/octeonvar.h>

#include <dev/ofw/fdt.h>
#include <dev/ofw/ofw_gpio.h>
#include <dev/ofw/openfirm.h>

#include <octeon/dev/iobusvar.h>
#include <octeon/dev/octxctlreg.h>

#define XCTL_RD_8(sc, reg) \
	bus_space_read_8((sc)->sc_iot, (sc)->sc_ioh, (reg))
#define XCTL_WR_8(sc, reg, val) \
	bus_space_write_8((sc)->sc_iot, (sc)->sc_ioh, (reg), (val))

struct octxctl_softc {
	struct device		sc_dev;
	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh;
	int			sc_power_gpio[3];
	int			sc_unit;
};

int	 octxctl_match(struct device *, void *, void *);
void	 octxctl_attach(struct device *, struct device *, void *);

int	 octxctl_dwc3_init(struct octxctl_softc *, struct fdt_reg *);
void	 octxctl_uctl_init(struct octxctl_softc *, uint64_t, uint64_t);
uint8_t	 octxctl_read_1(bus_space_tag_t, bus_space_handle_t, bus_size_t);
uint16_t octxctl_read_2(bus_space_tag_t, bus_space_handle_t, bus_size_t);
uint32_t octxctl_read_4(bus_space_tag_t, bus_space_handle_t, bus_size_t);
void	 octxctl_write_1(bus_space_tag_t, bus_space_handle_t, bus_size_t,
	    uint8_t);
void	 octxctl_write_2(bus_space_tag_t, bus_space_handle_t, bus_size_t,
	    uint16_t);
void	 octxctl_write_4(bus_space_tag_t, bus_space_handle_t, bus_size_t,
	    uint32_t);

const struct cfattach octxctl_ca = {
	sizeof(struct octxctl_softc), octxctl_match, octxctl_attach
};

struct cfdriver octxctl_cd = {
	NULL, "octxctl", DV_DULL
};

bus_space_t octxctl_tag = {
	.bus_base = PHYS_TO_XKPHYS(0, CCA_NC),
	._space_read_1 = octxctl_read_1,
	._space_read_2 = octxctl_read_2,
	._space_read_4 = octxctl_read_4,
	._space_write_1 = octxctl_write_1,
	._space_write_2 = octxctl_write_2,
	._space_write_4 = octxctl_write_4,
	._space_map = iobus_space_map,
	._space_unmap = iobus_space_unmap,
	._space_subregion = generic_space_region,
	._space_vaddr = generic_space_vaddr
};

int
octxctl_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;
	int child;

	if (OF_is_compatible(faa->fa_node, "cavium,octeon-7130-usb-uctl") == 0)
		return 0;
	if ((child = OF_child(faa->fa_node)) == 0)
		return 0;
	return OF_is_compatible(child, "cavium,octeon-7130-xhci");
}

void
octxctl_attach(struct device *parent, struct device *self, void *aux)
{
	char clock_type_hs[32];
	char clock_type_ss[32];
	struct fdt_reg child_reg;
	struct fdt_attach_args child_faa;
	struct fdt_attach_args *faa = aux;
	struct octxctl_softc *sc = (struct octxctl_softc *)self;
	uint64_t clock_freq, clock_sel;
	uint32_t reg[4];
	int child;

	if (faa->fa_nreg != 1) {
		printf(": expected one IO space, got %d\n", faa->fa_nreg);
		return;
	}

	child = OF_child(faa->fa_node);
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

	clock_freq = OF_getpropint(faa->fa_node, "refclk-frequency", 0);

	if (OF_getprop(faa->fa_node, "refclk-type-hs", clock_type_hs,
	    sizeof(clock_type_hs)) < 0)
		goto error;
	if (OF_getprop(faa->fa_node, "refclk-type-ss", clock_type_ss,
	    sizeof(clock_type_ss)) < 0)
		goto error;
	clock_sel = 0;
	if (strcmp(clock_type_ss, "dlmc_ref_clk1") == 0)
		clock_sel |= 1;
	if (strcmp(clock_type_hs, "pll_ref_clk") == 0)
		clock_sel |= 2;

	OF_getpropintarray(faa->fa_node, "power", sc->sc_power_gpio,
	    sizeof(sc->sc_power_gpio));

	sc->sc_unit = (faa->fa_reg[0].addr >> 24) & 0x1;
	sc->sc_iot = faa->fa_iot;
	if (bus_space_map(sc->sc_iot, faa->fa_reg[0].addr, faa->fa_reg[0].size,
	    0, &sc->sc_ioh)) {
		printf(": could not map registers\n");
		goto error;
	}

	octxctl_uctl_init(sc, clock_freq, clock_sel);

	if (octxctl_dwc3_init(sc, &child_reg) != 0) {
		/* Error message has been printed already. */
		goto error;
	}

	printf("\n");

	memset(&child_faa, 0, sizeof(child_faa));
	child_faa.fa_name = "";
	child_faa.fa_node = child;
	child_faa.fa_iot = &octxctl_tag;
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

void
octxctl_uctl_init(struct octxctl_softc *sc, uint64_t clock_freq,
    uint64_t clock_sel)
{
	static const uint32_t clock_divs[] = { 1, 2, 4, 6, 8, 16, 24, 32 };
	uint64_t i, val;
	uint64_t ioclock = octeon_ioclock_speed();
	uint64_t mpll_mult;
	uint64_t refclk_fsel;
	int output_sel;

	/*
	 * Put the bridge controller, USB core, PHY, and clock divider
	 * into reset.
	 */
	val = XCTL_RD_8(sc, XCTL_CTL);
	val |= XCTL_CTL_UCTL_RST;
	val |= XCTL_CTL_UAHC_RST;
	val |= XCTL_CTL_UPHY_RST;
	XCTL_WR_8(sc, XCTL_CTL, val);
	val = XCTL_RD_8(sc, XCTL_CTL);
	val |= XCTL_CTL_CLKDIV_RST;
	XCTL_WR_8(sc, XCTL_CTL, val);

	/* Select IO clock divisor. */
	for (i = 0; i < nitems(clock_divs); i++) {
		if (ioclock / clock_divs[i] < 300000000)
			break;
	}

	/* Update the divisor and enable the clock. */
	val = XCTL_RD_8(sc, XCTL_CTL);
	val &= ~XCTL_CTL_CLKDIV_SEL;
	val |= (i << XCTL_CTL_CLKDIV_SEL_SHIFT) & XCTL_CTL_CLKDIV_SEL;
	val |= XCTL_CTL_CLK_EN;
	XCTL_WR_8(sc, XCTL_CTL, val);

	/* Take the clock divider out of reset. */
	val = XCTL_RD_8(sc, XCTL_CTL);
	val &= ~XCTL_CTL_CLKDIV_RST;
	XCTL_WR_8(sc, XCTL_CTL, val);

	/* Select the reference clock. */
	switch (clock_freq) {
	case 50000000:
		refclk_fsel = 0x07;
		mpll_mult = 0x32;
		break;
	case 125000000:
		refclk_fsel = 0x07;
		mpll_mult = 0x28;
		break;
	case 100000000:
	default:
		if (clock_sel < 2)
			refclk_fsel = 0x27;
		else
			refclk_fsel = 0x07;
		mpll_mult = 0x19;
		break;
	}

	/* Set the clock and power up PHYs. */
	val = XCTL_RD_8(sc, XCTL_CTL);
	val &= ~XCTL_CTL_REFCLK_SEL;
	val |= clock_sel << XCTL_CTL_REFCLK_SEL_SHIFT;
	val &= ~XCTL_CTL_REFCLK_DIV2;
	val &= ~XCTL_CTL_REFCLK_FSEL;
	val |= refclk_fsel << XCTL_CTL_REFCLK_FSEL_SHIFT;
	val &= ~XCTL_CTL_MPLL_MULT;
	val |= mpll_mult << XCTL_CTL_MPLL_MULT_SHIFT;
	val |= XCTL_CTL_SSC_EN;
	val |= XCTL_CTL_REFCLK_SSP_EN;
	val |= XCTL_CTL_SSPOWER_EN;
	val |= XCTL_CTL_HSPOWER_EN;
	XCTL_WR_8(sc, XCTL_CTL, val);

	delay(100);

	/* Take the bridge out of reset. */
	val = XCTL_RD_8(sc, XCTL_CTL);
	val &= ~XCTL_CTL_UCTL_RST;
	XCTL_WR_8(sc, XCTL_CTL, val);

	delay(100);

	if (sc->sc_power_gpio[0] != 0) {
		if (sc->sc_unit == 0)
			output_sel = GPIO_CONFIG_MD_USB0_VBUS_CTRL;
		else
			output_sel = GPIO_CONFIG_MD_USB1_VBUS_CTRL;
		gpio_controller_config_pin(sc->sc_power_gpio,
		    GPIO_CONFIG_OUTPUT | output_sel);

		/* Enable port power control. */
		val = XCTL_RD_8(sc, XCTL_HOST_CFG);
		val |= XCTL_HOST_CFG_PPC_EN;
		if (sc->sc_power_gpio[2] & GPIO_ACTIVE_LOW)
			val &= ~XCTL_HOST_CFG_PPC_ACTIVE_HIGH_EN;
		else
			val |= XCTL_HOST_CFG_PPC_ACTIVE_HIGH_EN;
		XCTL_WR_8(sc, XCTL_HOST_CFG, val);
	} else {
		/* Disable port power control. */
		val = XCTL_RD_8(sc, XCTL_HOST_CFG);
		val &= ~XCTL_HOST_CFG_PPC_EN;
		XCTL_WR_8(sc, XCTL_HOST_CFG, val);
	}

	/* Enable host-only mode. */
	val = XCTL_RD_8(sc, XCTL_CTL);
	val &= ~XCTL_CTL_DRD_MODE;
	XCTL_WR_8(sc, XCTL_CTL, val);

	delay(100);

	/* Take the USB core out of reset. */
	val = XCTL_RD_8(sc, XCTL_CTL);
	val &= ~XCTL_CTL_UAHC_RST;
	XCTL_WR_8(sc, XCTL_CTL, val);

	delay(100);

	val = XCTL_RD_8(sc, XCTL_CTL);
	val |= XCTL_CTL_CSCLK_EN;
	XCTL_WR_8(sc, XCTL_CTL, val);

	/* Take the PHY out of reset. */
	val = XCTL_RD_8(sc, XCTL_CTL);
	val &= ~XCTL_CTL_UPHY_RST;
	XCTL_WR_8(sc, XCTL_CTL, val);
	(void)XCTL_RD_8(sc, XCTL_CTL);

	/* Fix endianness. */
	val = XCTL_RD_8(sc, XCTL_SHIM_CFG);
	val &= ~XCTL_SHIM_CFG_CSR_BYTE_SWAP;
	val &= ~XCTL_SHIM_CFG_DMA_BYTE_SWAP;
	val |= 3ull << XCTL_SHIM_CFG_CSR_BYTE_SWAP_SHIFT;
	val |= 1ull << XCTL_SHIM_CFG_DMA_BYTE_SWAP_SHIFT;
	XCTL_WR_8(sc, XCTL_SHIM_CFG, val);
	(void)XCTL_RD_8(sc, XCTL_SHIM_CFG);
}

int
octxctl_dwc3_init(struct octxctl_softc *sc, struct fdt_reg *reg)
{
	bus_space_handle_t ioh;
	uint32_t rev;
	uint32_t val;
	int error = 0;

	if (bus_space_map(sc->sc_iot, reg->addr, reg->size, 0, &ioh) != 0) {
		printf(": could not map USB3 core registers\n");
		return EIO;
	}

	val = bus_space_read_4(sc->sc_iot, ioh, DWC3_GSNPSID);
	if ((val & 0xffff0000u) != 0x55330000u) {
		printf(": no DWC3 core\n");
		error = EIO;
		goto out;
	}
	rev = val & 0xffffu;
	printf(": DWC3 rev 0x%04x", rev);

	val = bus_space_read_4(sc->sc_iot, ioh, DWC3_GUSB3PIPECTL(0));
	val &= ~DWC3_GUSB3PIPECTL_UX_EXIT_PX;
	val |= DWC3_GUSB3PIPECTL_SUSPHY;
	bus_space_write_4(sc->sc_iot, ioh, DWC3_GUSB3PIPECTL(0), val);

	val = bus_space_read_4(sc->sc_iot, ioh, DWC3_GUSB2PHYCFG(0));
	val |= DWC3_GUSB2PHYCFG_SUSPHY;
	bus_space_write_4(sc->sc_iot, ioh, DWC3_GUSB2PHYCFG(0), val);

	/* Set the controller into host mode. */
	val = bus_space_read_4(sc->sc_iot, ioh, DWC3_GCTL);
	val &= ~DWC3_GCTL_PRTCAP_MASK;
	val |= DWC3_GCTL_PRTCAP_HOST;
	bus_space_write_4(sc->sc_iot, ioh, DWC3_GCTL, val);

	val = bus_space_read_4(sc->sc_iot, ioh, DWC3_GCTL);
	val &= ~DWC3_GCTL_SCALEDOWN_MASK;
	val &= ~DWC3_GCTL_DISSCRAMBLE;
	if (rev >= DWC3_REV_210A && rev <= DWC3_REV_250A)
		val |= DWC3_GCTL_DSBLCLKGTNG | DWC3_GCTL_SOFITPSYNC;
	else
		val &= ~DWC3_GCTL_DSBLCLKGTNG;
	bus_space_write_4(sc->sc_iot, ioh, DWC3_GCTL, val);

out:
	bus_space_unmap(sc->sc_iot, ioh, reg->size);

	return error;
}

/*
 * Bus access routines for xhci(4).
 */

uint8_t
octxctl_read_1(bus_space_tag_t t, bus_space_handle_t h, bus_size_t o)
{
	return *(volatile uint8_t *)(h + (o ^ 3));
}

uint16_t
octxctl_read_2(bus_space_tag_t t, bus_space_handle_t h, bus_size_t o)
{
	return *(volatile uint16_t *)(h + (o ^ 2));
}

uint32_t
octxctl_read_4(bus_space_tag_t t, bus_space_handle_t h, bus_size_t o)
{
	return *(volatile uint32_t *)(h + o);
}

void
octxctl_write_1(bus_space_tag_t t, bus_space_handle_t h, bus_size_t o,
    uint8_t v)
{
	*(volatile uint8_t *)(h + (o ^ 3)) = v;
}

void
octxctl_write_2(bus_space_tag_t t, bus_space_handle_t h, bus_size_t o,
    uint16_t v)
{
	*(volatile uint16_t *)(h + (o ^ 2)) = v;
}

void
octxctl_write_4(bus_space_tag_t t, bus_space_handle_t h, bus_size_t o,
    uint32_t v)
{
	*(volatile uint32_t *)(h + o) = v;
}
