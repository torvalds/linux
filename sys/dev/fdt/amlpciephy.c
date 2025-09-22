/*	$OpenBSD: amlpciephy.c,v 1.5 2021/10/24 17:52:26 mpi Exp $	*/
/*
 * Copyright (c) 2019 Mark Kettenis <kettenis@openbsd.org>
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
#include <dev/ofw/ofw_misc.h>
#include <dev/ofw/fdt.h>

#define PHY_R0		0x00
#define  PHY_R0_PCIE_POWER_MASK	(0x1f << 0)
#define  PHY_R0_PCIE_POWER_ON	(0x1c << 0)
#define  PHY_R0_PCIE_POWER_OFF	(0x1d << 0)
#define  PHY_R0_MODE_MASK	(0x3 << 5)
#define  PHY_R0_MODE_USB3	(0x3 << 5)
#define PHY_R4		0x10
#define  PHY_R4_PHY_CR_WRITE	(1 << 0)
#define  PHY_R4_PHY_CR_READ	(1 << 1)
#define  PHY_R4_PHY_CR_CAP_DATA	(1 << 18)
#define  PHY_R4_PHY_CR_CAP_ADDR	(1 << 19)
#define PHY_R5		0x14
#define  PHY_R5_PHY_CR_ACK	(1 << 16)

#define HREAD4(sc, reg)							\
	(bus_space_read_4((sc)->sc_iot, (sc)->sc_ioh, (reg)))
#define HWRITE4(sc, reg, val)						\
	bus_space_write_4((sc)->sc_iot, (sc)->sc_ioh, (reg), (val))
#define HSET4(sc, reg, bits)						\
	HWRITE4((sc), (reg), HREAD4((sc), (reg)) | (bits))
#define HCLR4(sc, reg, bits)						\
	HWRITE4((sc), (reg), HREAD4((sc), (reg)) & ~(bits))

struct amlpciephy_softc {
	struct device		sc_dev;
	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh;

	struct phy_device	sc_pd;
};

int amlpciephy_match(struct device *, void *, void *);
void amlpciephy_attach(struct device *, struct device *, void *);

const struct cfattach	amlpciephy_ca = {
	sizeof (struct amlpciephy_softc), amlpciephy_match, amlpciephy_attach
};

struct cfdriver amlpciephy_cd = {
	NULL, "amlpciephy", DV_DULL
};

int	amlpciephy_enable(void *, uint32_t *);
uint16_t amlpciephy_read(struct amlpciephy_softc *, bus_addr_t);
void	amlpciephy_write(struct amlpciephy_softc *, bus_addr_t, uint16_t);

int
amlpciephy_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return OF_is_compatible(faa->fa_node, "amlogic,g12a-usb3-pcie-phy");
}

void
amlpciephy_attach(struct device *parent, struct device *self, void *aux)
{
	struct amlpciephy_softc *sc = (struct amlpciephy_softc *)self;
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

	sc->sc_pd.pd_node = faa->fa_node;
	sc->sc_pd.pd_cookie = sc;
	sc->sc_pd.pd_enable = amlpciephy_enable;
	phy_register(&sc->sc_pd);
}

int
amlpciephy_enable(void *cookie, uint32_t *cells)
{
	struct amlpciephy_softc *sc = cookie;
	int node = sc->sc_pd.pd_node;
	uint32_t type = cells[0];
	uint32_t reg;

	/* Hardware can be switched between PCIe 2.0 and USB 3.0 mode. */
	if (type != PHY_TYPE_PCIE && type != PHY_TYPE_USB3)
		return -1;

	clock_set_assigned(node);
	clock_enable_all(node);

	switch (type) {
	case PHY_TYPE_PCIE:
		/* Power on. */
		reg = HREAD4(sc, PHY_R0);
		reg &= ~PHY_R0_PCIE_POWER_MASK;
		reg |= PHY_R0_PCIE_POWER_ON;
		HWRITE4(sc, PHY_R0, reg);

		reset_assert_all(node);
		delay(500);
		reset_deassert_all(node);
		delay(500);

		break;
	case PHY_TYPE_USB3:
		reset_assert_all(node);
		delay(10);
		reset_deassert_all(node);

		/* Switch to USB 3.0 mode. */
		reg = HREAD4(sc, PHY_R0);
		reg &= ~PHY_R0_MODE_MASK;
		reg |= PHY_R0_MODE_USB3;
		HWRITE4(sc, PHY_R0, reg);

		/* Workaround for SuperSpeed PHY suspend bug. */
		reg = amlpciephy_read(sc, 0x102d);
		reg |= (1 << 7);
		amlpciephy_write(sc, 0x102d, reg);

		reg = amlpciephy_read(sc, 0x1010);
		reg &= ~0xff0;
		reg |= 0x10;
		amlpciephy_write(sc, 0x1010, reg);

		/* Rx equalization magic. */
		reg = amlpciephy_read(sc, 0x1006);
		reg &= (1 << 6);
		reg |= (1 << 7);
		reg &= ~(0x7 << 8);
		reg |= (0x3 << 8);
		reg |= (1 << 11);
		amlpciephy_write(sc, 0x1006, reg);

		/* Tx equalization magic. */
		reg = amlpciephy_read(sc, 0x1002);
		reg &= ~0x3f80;
		reg |= (0x16 << 7);
		reg &= ~0x7f;
		reg |= (0x7f | (1 << 14));
		amlpciephy_write(sc, 0x1002, reg);

		/* MPLL loop magic. */
		reg = amlpciephy_read(sc, 0x30);
		reg &= ~(0xf << 4);
		reg |= (8 << 4);
		amlpciephy_write(sc, 0x30, reg);

		break;
	}

	return 0;
}

void
amlpciephy_addr(struct amlpciephy_softc *sc, bus_addr_t addr)
{
	int timo;
	
	HWRITE4(sc, PHY_R4, addr << 2);
	HWRITE4(sc, PHY_R4, addr << 2);
	HWRITE4(sc, PHY_R4, (addr << 2) | PHY_R4_PHY_CR_CAP_ADDR);
	for (timo = 200; timo > 0; timo--) {
		if (HREAD4(sc, PHY_R5) & PHY_R5_PHY_CR_ACK)
			break;
		delay(5);
	}
	if (timo == 0) {
		printf("%s: timeout\n", __func__);
		return;
	}
	HWRITE4(sc, PHY_R4, addr << 2);
	for (timo = 200; timo > 0; timo--) {
		if ((HREAD4(sc, PHY_R5) & PHY_R5_PHY_CR_ACK) == 0)
			break;
		delay(5);
	}
	if (timo == 0) {
		printf("%s: timeout\n", __func__);
		return;
	}
}

uint16_t
amlpciephy_read(struct amlpciephy_softc *sc, bus_addr_t addr)
{
	uint32_t reg;
	int timo;

	amlpciephy_addr(sc, addr);
	HWRITE4(sc, PHY_R4, 0);
	HWRITE4(sc, PHY_R4, PHY_R4_PHY_CR_READ);
	for (timo = 200; timo > 0; timo--) {
		if (HREAD4(sc, PHY_R5) & PHY_R5_PHY_CR_ACK)
			break;
		delay(5);
	}
	if (timo == 0) {
		printf("%s: timeout\n", __func__);
		return 0;
	}
	reg = HREAD4(sc, PHY_R5);
	HWRITE4(sc, PHY_R4, 0);
	for (timo = 200; timo > 0; timo--) {
		if ((HREAD4(sc, PHY_R5) & PHY_R5_PHY_CR_ACK) == 0)
			break;
		delay(5);
	}
	if (timo == 0) {
		printf("%s: timeout\n", __func__);
		return 0;
	}
	return reg;
}

void
amlpciephy_write(struct amlpciephy_softc *sc, bus_addr_t addr, uint16_t data)
{
	int timo;

	amlpciephy_addr(sc, addr);
	HWRITE4(sc, PHY_R4, data << 2);
	HWRITE4(sc, PHY_R4, data << 2);
	HWRITE4(sc, PHY_R4, data << 2 | PHY_R4_PHY_CR_CAP_DATA);
	for (timo = 200; timo > 0; timo--) {
		if (HREAD4(sc, PHY_R5) & PHY_R5_PHY_CR_ACK)
			break;
		delay(5);
	}
	if (timo == 0) {
		printf("%s: timeout\n", __func__);
		return;
	}
	HWRITE4(sc, PHY_R4, data << 2);
	for (timo = 200; timo > 0; timo--) {
		if ((HREAD4(sc, PHY_R5) & PHY_R5_PHY_CR_ACK) == 0)
			break;
		delay(5);
	}
	if (timo == 0) {
		printf("%s: timeout\n", __func__);
		return;
	}

	HWRITE4(sc, PHY_R4, data << 2);
	HWRITE4(sc, PHY_R4, data << 2 | PHY_R4_PHY_CR_WRITE);
	for (timo = 200; timo > 0; timo--) {
		if (HREAD4(sc, PHY_R5) & PHY_R5_PHY_CR_ACK)
			break;
		delay(5);
	}
	if (timo == 0) {
		printf("%s: timeout\n", __func__);
		return;
	}
	HWRITE4(sc, PHY_R4, data << 2);
	for (timo = 200; timo > 0; timo--) {
		if ((HREAD4(sc, PHY_R5) & PHY_R5_PHY_CR_ACK) == 0)
			break;
		delay(5);
	}
	if (timo == 0) {
		printf("%s: timeout\n", __func__);
		return;
	}
}
